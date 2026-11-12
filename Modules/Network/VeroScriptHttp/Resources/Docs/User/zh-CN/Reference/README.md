# VeroScriptHttp

前端安装与后端选择的公共规则统一见 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/index.md#backend-selection)。

正式包 `vs-http`，原生入口 ID 和脚本命名空间为 `http`；CMake 包为 `VeroScriptHttp`，target 为 `VeroScript::Http`。精确依赖同版本 `vs-network`，共享原生运行时只由 Boost 后端包分发。

客户端 `Send` 返回 operation，结果类型为 `Http::FResponse`。
404/500 属于正常 HTTP 响应；只有完整正文收取成功才返回成功结果。字段保留顺序和重复项。
正文和头有显式上限；缓冲响应使用 `Send`，有界流式响应使用 `SendStream`，上传管道使用 `FRequest::Upload`。
`FRequest::CaFile` 为空时使用后端正常信任存储；也可为当前请求指定 PEM CA bundle。
重定向默认关闭；代理默认不继承环境变量。HTTP/2 通过实际后端能力查询，要求 HTTP/2 时不会静默降级。

`FRequest::Multipart` 提供独立于原始 Body 的 multipart，最多 256 项。每项保存名称、可选文件名、
可选 Content-Type，以及缓冲字节或流式来源。相同名称可以重复，二进制 NUL 不会截断。
curl 生成 boundary 和 framing；不能同时提供原始 Body、HEAD 方法或手工 Content-Type/Content-Length/Transfer-Encoding。
缓冲正文和元数据总量受 context 的 `MaxBufferBytes` 约束；流式载荷另受上传额度约束。客户端重放工厂及增量 reader 见 [multipart](multipart.md)；文件系统上传、下载和续传见 [文件传输](files.md)。

curl 的实际 socket（含池中空闲连接）现在也登记到 context 额度中；关闭时释放额度，复用连接不会重复登记。
当前 `MaxConnections` 是保守的资源额度，同时计入监听器和 client 管理对象，所以可用的实际连接数可能更少。

服务端 `Listen` 可使用端口 0，`Accept` 的结果为 `shared_ptr<FExchange>`。
读取 `Request()` 后调用一次 `Reply`。每个 exchange 保留独立快照与身份，旧 exchange 不能回复后续请求。
一个 HTTP 1.x 连接按响应完成顺序处理请求。服务端可通过 `FServerOptions::Tls` 使用 HTTPS；[路由快照](routing.md) 匹配请求目标，
[ReplyStream](streaming.md#%E6%9C%8D%E5%8A%A1%E7%AB%AF%E6%B5%81%E5%BC%8F%E5%93%8D%E5%BA%94) 支持有界逐块响应与固定 trailers。
FServerOptions::bStreamRequests 可提前交付请求头并以有界管道接收正文，最终 trailers/字节数通过 BodyOperation 查询。

## 脚本入口

| 入口 | 主要方法 |
| --- | --- |
| `http::create_request(method, url)` | 独立值；`set_body/set_header/add_header/set_timeout/set_body_limit/follow_redirects/require_http2/set_proxy/set_ca_file` |
| `http::create_client(context)` | `send(request)/close/tls_supported/http2_supported` |
| `http::create_form()` | 独立值；`add(name, value)/encode`，以 `request.set_form(form)` 设置正文 |
| `http::listen(context, address, port)` | `accept(timeout)/port/close`；完成后 `exchange()` 读取请求并 `reply(status, body, timeout)` |

## SDK 消费

安装后导入所用协议的 CMake 包，配置会自动查找匹配的基础包。context 驱动和操作结果约束见 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/Reference/README.md)。脚本宿主先注册 `network`，再注册本协议并声明依赖。

## SSE 数据与脚本接口

`VeroScriptHttpSse.h` 提供 `FSseReader`、`TryEncodeSseEvent` 和 `TryEncodeSseComment`，无需 Core。
解析遵循 [HTML event stream 解释规则](https://html.spec.whatwg.org/multipage/server-sent-events.html#event-stream-interpretation)：
支持跨块 UTF-8/BOM、替换解码、CR/LF/CRLF、重复 data 行、id 和 retry。只在空行后交付事件，EOF 丢弃未结束事件。
`Feed` 最多返回一个事件，保留未消费后缀供下一次提交；事件不借用输入。默认限制每行 64 KiB、data 1 MiB。
retry 非数字或超出 uint64 的字段被忽略；编码器要求合法 UTF-8 与 LF 文本，ID 不允许 NUL/换行。

脚本 `http::make_sse_reader()` 或 `make_sse_reader_with_limits(line_bytes, event_bytes)` 返回共享句柄。
`feed(chunk, final)` 返回消费字节数，`state()` 为 0 NeedInput、1 EventReady、2 Done、3 Failed。
EventReady 时读取 `event_data/event_type/event_id`；另有 `last_event_id/retry_ms/error_code/error/reset`。
`retry_ms` 未指定时为 -1；超出 int64 则明确报错。`encode_sse(data, event_type, id)` 和
`encode_sse_comment(text)` 返回编码文本。脚本长输入按批检查 VM 工作预算。
这些接口是独立事件编解码器；HTTP 客户端长流、有界背压和自动重连另由
[SSE 客户端](sse-client.md) 提供。服务端可用 ReplyStream 逐块发送事件与心跳。

[Cookie、连接池与重试策略](client-policy.md) 提供客户端配置、Cookie 隔离与显式重放。

## 专题

- [client-policy](client-policy.md)
- [files](files.md)
- [http-trace](http-trace.md)
- [json-adapters](json-adapters.md)
- [multipart](multipart.md)
- [routing](routing.md)
- [sse-client](sse-client.md)
- [streaming](streaming.md)
- [upgrade](upgrade.md)

## 表单与正文

表单保留重复键、空值及顺序，使用 application/x-www-form-urlencoded 编码；脚本表单最多 256 项，
按最坏编码长度约束到 16 MiB。multipart 使用 `request.begin_multipart()`，然后调用
`add_field(name, bytes)` 或 `add_file(name, filename, content_type, bytes)`；脚本同样限制 256 项/16 MiB 的元数据与缓冲正文。另有 add_field_stream / add_file_stream，见 [multipart](multipart.md)。
`set_body` 退出 multipart 模式；`set_form` 设置表单类型，`begin_multipart` 清理 framing 头供后端重建。

## 注册与结果

`VeroScriptHttpModule.h` 提供本模块的 `QueryModule` / `GetImplementationIdentity`，静态库为
`VeroScriptHttpBindings`，动态库为 `VeroScriptHttpModule`。先注册 network，并声明依赖可见性。
成功完成后的 `response()` 返回只读的 `status/body/effective_url/header_count/header`；
未完成、失败或结果类型不符时访问会失败。正文使用保留 NUL 的 string，不自动转换字符编码。
Core、程序代次和派发约束见基础模块的注册与完成回调文档。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](README.md) · [zh-TW](../../zh-TW/Reference/README.md)
