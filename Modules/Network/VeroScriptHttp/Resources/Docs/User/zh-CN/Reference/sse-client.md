# SSE 客户端

`VeroScriptHttpSseClient.h` 提供 `FSseClient`，在 HTTP 有界正文流上解析事件。
一个逻辑调用方使用 Poll、访问器和 Close；它们不支持对同一对象并发调用。
后台 context 继续使用自身 I/O 线程；HostDriven context 仍由宿主调用 PollIo。
没有每连接新增工作线程，也不会从 I/O 线程执行脚本。

## 请求与事件

通过 `FSseClient::Connect(context, options, error)` 创建，URL 位于 options.Request.Url。
SSE 使用无正文 GET；不接受上传源、重放工厂、上传长度、multipart 或原始正文。请求自动设置 Accept: text/event-stream，
重连时由客户端管理 Last-Event-ID。其余请求配置（例如请求专属 CA、代理和显式总期限）沿用 HTTP。

默认总期限为无限，空闲期限为 60 秒，同时覆盖等待响应头和等待后续正文。
普通 HTTP 请求的 30 秒总期限不自动套用到 SSE。显式替换 Request 会保留该请求自己的期限和总字节限制。
默认正文流总字节数不限，环缓冲为 256 KiB，单行 64 KiB、单事件 1 MiB，单次 Poll 最多处理 64 KiB。
管道容量和解析器的保守保留量计入 context，调用方取走的事件副本由调用方管理。
Poll 可接收借用的 FSseWork，在开始和每个有界输入块解析前以 0 检查取消，
解析后仅按实际消费字节计费；单块最多 8 KiB，事件边界后的未消费字节不会重复计费。
回调仅在本次调用内使用。
返回 false 停止并报告 Cancelled，普通回调异常报告 Callback。脚本转发当前调用的工作检查，
预算耗尽或取消仍作为 VM 调用失败传播，不作为普通可忽略的 SSE 状态。

| Poll 状态 | 含义 |
| --- | --- |
| Pending / 0 | 等待 I/O，或本次解析预算已耗尽 |
| EventReady / 1 | 返回一个完整事件，提交 Out |
| Reconnecting / 2 | 等待重连时间；可查询 ReconnectRemaining |
| Closed / 3 | 显式关闭、context 关闭、204 或正常结束且不再重连 |
| Failed / 4 | 永久错误；Error 保留首个失败 |

只有 EventReady 改写 Out。响应必须是 HTTP 200 且媒体类型为 text/event-stream；204 停止重连。
类型或状态错误保留 ResponseHead，调用方仍能读取原状态与头。EOF 不补造未完成事件。

## 重连

默认无限次重连，初始间隔 3 秒，最大间隔 60 秒。网络地址解析、连接、收发失败和超时可重连；
SSE 协议、配置、解析限额及分配失败不会自动重试。
MaxReconnects 是整个生命周期的重连次数上限，0 关闭自动重连。
达到上限后，正常 EOF 为 Closed，网络失败保留为 Failed。

服务端 retry 覆盖后续重连间隔并跨连接保留，受 MaxReconnectDelay 限制。
已提交的事件 ID 跨连接保留；只有完整空行分隔提交 ID，包括没有 data 的 ID 更新。
重连后没有新 id 的事件继承之前的 ID。Close 幂等，不重新发送业务操作。
重连只在 Poll 中启动；调用方不轮询时不会自动开始新请求。

## 脚本

http::make_sse_options() 创建选项，支持：

- set_limits(line_bytes, event_bytes, buffer_bytes, poll_bytes)
- set_reconnect(initial_ms, maximum_ms, maximum_reconnects)
- set_idle_timeout(milliseconds)，-1 关闭空闲期限
- set_request(http::request)，显式复制配置；connect 的 URL 参数覆盖其中 URL

http::connect_sse(context, url) 和 connect_sse_with_options(context, url, options) 返回 sse_client。
客户端提供 poll、close、event_data、event_type、event_id、last_event_id、response_status、
reconnect_count、reconnect_remaining、error_code、error。event_* 只在上一次 poll 为 1 时可读。

脚本服务端可用 http::create_response(status, body) 创建独立 response_builder，
通过 set_header / add_header 设置响应头，再调用 exchange.reply_response(builder, timeout)。
这是缓冲响应入口；需要持续推送时，可使用[服务端流式响应](streaming.md)，并设置 text/event-stream 媒体类型。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/sse-client.md) · [fr-FR](../../fr-FR/Reference/sse-client.md) · [ja-JP](../../ja-JP/Reference/sse-client.md) · [ko-KR](../../ko-KR/Reference/sse-client.md) · [zh-CN](sse-client.md) · [zh-TW](../../zh-TW/Reference/sse-client.md)
