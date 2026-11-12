# HTTP 流式传输

底层 reader/writer 及脚本管道接口见 [Network 字节管道](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/Reference/streaming.md)。

## 流式下载

`client.SendStream(request, buffer_bytes)` 返回 `FStreamingOperation`：

- `TryHead` 在最终响应头可用时返回不可变快照；pending 返回 false 且无错误。
- `Body` 返回可读流，正文不会同时聚合到 `FResponse::Body`。
- `Operation` 在传输完整结束、失败、取消或期限到达时进入终态。
- 成功结果的 `BodyBytes` 记录流式正文数量；最终结果保留 trailers。

`request.MaxBodyBytes` 限制累计正文，`buffer_bytes` 限制当前队列。
后者须至少 16 KiB，且不得超过 context 的单缓冲上限。
必须在传输期间读取正文；只等待完成而不读取，可能因背压一直等到期限。
缓冲满时 curl 整块暂停，恢复重放该块时才计数和入队，避免部分消费造成重复数据。
恢复排入 I/O 执行器，上传与下载分别保留暂停状态。
暂停的请求不会阻止其他连接推进。

提前取消／丢弃 reader 会取消请求。操作失败或超时会通知流终态；已拥有的字节先读完，再取得错误。
应用缓冲上限不代表 TLS、curl、HTTP/2、操作系统或浏览器内部全部存储的严格总上限。

## 流式上传

`FRequest::Upload` 指向管道 reader，生产者通过 paired writer 分批供应数据。
`UploadLength` 可省略；HTTP/1.1 下未知长度由后端生成 framing。
`MaxUploadBytes` 默认 64 MiB，限制接受用于上传的累计源载荷。已知长度不足会失败。
生产者可返回 WouldBlock，写入或 Finish 会唤醒上传。传输终态会取消不再需要的输入端。

上传流是单次来源，不能并发或再次提交。它与缓冲正文、multipart、HEAD 及自动重定向互斥。
原生 `UploadFactory` 可为重试与重定向返回新源，契约见 [客户端策略](client-policy.md)。
[multipart](multipart.md) 可组合缓冲和流部分，原生部分工厂支持显式重放。
Content-Length／Transfer-Encoding 由后端管理，手工冲突的请求会失败。

## 脚本入口

| 入口 | 行为 |
| --- | --- |
| `request.set_upload(reader, length)` | length 为 -1 表示未知；正常正文 setter 会清理旧上传来源 |
| `request.set_upload_limit(count)` | 设置累计上传上限 |
| `client.send_stream(request, buffer_bytes)` | 返回 `http::stream_operation` |
| `stream.operation/body/head_ready/head` | 操作、可读流和头快照 |
| `response.body_bytes()` | 累计正文数量，超出 int64 时明确失败 |

WouldBlock 时应返回宿主调度器，在后续 tick 再推进；HostDriven 模式由拥有线程限量调用 `context.poll()`。
流读写按有界块检查当前 VM 工作预算。后台只运行原生 I/O，脚本回调仍不能由 I/O 线程直接执行。
HTTP 流可逐块交给 JSON reader 或 SSE reader；自动 SSE 重连见 [SSE 客户端](sse-client.md)。
文件接入见[文件传输](files.md)；应用缓冲限额不覆盖后端全部内存。

## 服务端流式响应

`exchange.ReplyStream(response, FBodySource{reader, optional_length}, options)` 在提交时快照头与固定 trailers，
先发送响应头，再从单消费者 reader 逐块读取并发送。reader 与非空 `response.Body` 互斥；
无 reader 时使用拥有的 Body。writer 可以在响应运行期间继续供应数据。

`FResponseOperation` 的 Operation 表达整体结果，BodyBytes 记录已成功写完的正文块，
State 为 Uncommitted、HeadersSent、BodyStreaming、Finished、Aborted（0–4）。
完成表示本地传输已接受全部输出，不表示对端业务处理完成。已发送的部分字节不能回滚。

- HTTP/1.1 未知长度或含 trailers 时使用 chunked；已知长度且无 trailers 时使用 Content-Length。
- HTTP/1.0 未知长度在结束时关闭连接；不能发送 trailers。
- HEAD 只发送表示长度与响应头；204/304 不发送正文，输入端会取消。
- 明确长度不足或多出字节报告 Protocol；未知长度超过 MaxStreamBodyBytes 报告 Limit。
- Content-Length、Transfer-Encoding 和 Trailer 由后端重建；trailers 不允许 framing、连接和路由字段。
- 普通 Reply 也支持固定 trailers；未提交前的参数错误不消耗 exchange。
- 正文使用至多 16 KiB 的传输块，计入 context 额度；源管道单独核算容量。累计上限独立于保留容量。
- WouldBlock 等待通知，取消输入端也会唤醒消费者；响应取消、超时、源错误或关闭会结束连接并通知生产者。
- ResponseTimeout 是无进展空闲期限，Options.Timeout 是从提交起算的总期限。
  SSE 等长响应可显式取消总期限，持续写入事件或心跳；仍受累计字节上限约束。

脚本入口：

```vs
Network::byte_pipe output = Network::create_byte_pipe(context, 65536);
http::response_builder response = http::create_response(200, "");
response.set_header("Content-Type", "text/event-stream");
http::response_stream_operation sending = exchange.reply_stream(response, output.reader(), -1, -1);
Network::writable_stream writer = output.writer();
writer.write(http::encode_sse("hello", "", ""));
// WouldBlock 时返回应用调度器，后续 tick 重试尚未接受的后缀。
writer.finish();
```

`reply_stream` 的 length 为 -1 表示未知，最后一参为总期限毫秒。输入管道必须属于同一 context。
返回句柄提供 operation()、state()、body_bytes()。response builder 可 add_trailer/set_trailer；
这些字段必须在提交前确定。

## 服务端请求流

FServerOptions::bStreamRequests 默认 false，保持原有缓冲请求用法。开启后，
Accept 在请求头解析完成时交付 exchange；Request().Body 为空，BodyStream 是单消费者 reader，
BodyOperation 在完整接收、失败或取消时结束。成功结果 FRequestBodyResult 提供 BodyBytes 和最终 Trailers。
请求头快照不会因收到 trailers 而变化。`Expect: 100-continue` 在读取正文前应答。

RequestStreamBufferBytes 控制排队容量（默认 64 KiB），MaxStreamBodyBytes 控制累计输入；
传输块不大于 16 KiB 和管道容量。正文大于容量时暂停读取，消费者读取后恢复；不会先聚合整个正文。
应用必须持续排空正文，或取消 reader 结束连接。完整请求体和响应均结束后才读取下一请求，
旧 exchange 仍不能回复后续请求。请求流与响应流可分别推进，应用负责它们的数据依赖关系。

脚本使用 http::make_server_options()，设置 set_stream_requests(true)、set_request_stream_buffer_bytes(count)
和 set_max_stream_body_bytes(count)，通过 http::listen_with_options(context, address, port, options) 启动。
其他选项包括 max_body_bytes、max_header_bytes、max_pending_requests、response_timeout_ms、handshake_timeout_ms
及对应 set_*；set_tls(tls_options)/clear_tls() 控制 TLS。选项采用独立值语义，提交时快照。

exchange.has_body_stream() 判断模式，body_stream() 取得 reader，body_operation() 取得操作。
成功后 operation.request_body_result() 提供 body_bytes()、trailer_count()、trailer_name(index) 和 trailer_value(index)。
缓冲模式调用这两个流访问器会明确报错。string 读取保留 NUL；WouldBlock 时交还调度器，避免忙等。

## 平缓关闭

`server.Shutdown(grace, operation_options)` 停止接收新连接和新请求，结束排队的 Accept，
关闭尚未交付给 Accept 操作的请求及空闲连接。已经交付的 exchange 可在宽限期内完成请求体和响应；
期间连接不再进入下一请求。宽限期届满会关闭剩余连接，成功结果 FShutdownResult::bForced 为 true。
ClosedConnections 是开始关闭时属于该服务端的活动连接数量；结果表示逻辑关闭，不能据此认为所有原生完成处理器及外部快照已释放。

Grace 为 0 至 365 天；操作自己的总期限仍从提交起计算。取消、总期限或显式 Close 会立即结束剩余工作，
并保留 Cancelled、TimedOut 或 Closed 错误。关闭中的第二次 Shutdown 报告 State；已关闭服务器再次 Shutdown 成功且连接数为 0。
应保持 server 句柄直到完成；销毁句柄等同显式 Close。

脚本使用 server.shutdown(grace_ms, total_timeout_ms)，返回 http::operation。
成功后用 shutdown_forced()、shutdown_connections() 查看结果，其他结果类型上调用会报类型错误。
exchange.header_count(name)/header(name,index) 读取请求头，名称不区分大小写。
最终响应的 response.trailer_count(name)/trailer(name,index) 读取 trailers；提前发布的 head 保持原始快照，不追加最终 trailers。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
