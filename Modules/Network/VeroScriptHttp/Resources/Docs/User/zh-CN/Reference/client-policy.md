# HTTP 客户端策略

`FClient(Context, Service, FClientOptions)` 为客户端固定连接池与 Cookie 配置；默认构造保持 Cookie 和自动重试关闭。
原生和脚本实现共用 curl 后端。系统 curl 最低版本为 8.13，使用 `CURLFOLLOW_OBEYCODE` 处理自定义方法重定向。

`Send` 缓冲完整响应，`MaxBodyBytes` 必须同时不超过 context 缓冲预算和当前平台的 `Network::FBytes::max_size()`，否则提交时报 `InvalidArgument`。`SendStream` 仍使用 64 位正文累计上限，并通过 `BufferBytes` 单独约束排队内存。

## 连接池与代理

`MaxConnections=0` 使用 context 上限；另有每目标连接数、空闲连接缓存数量、最大空闲秒数和
`MaxPendingRequests`。后者计算已提交但尚未结束的请求，包括等待连接及重试间隔。
排队计入请求原始期限，超出客户端上限报告 QueueFull。

代理默认显式关闭环境继承。`bInheritProxyEnvironment=true` 才允许空 Proxy 使用后端环境配置；
非空的请求 Proxy 始终采用请求配置。不会修改进程环境。

## Cookie

开启 `bCookies` 后，每个客户端拥有独立的后端 Cookie store，共享同客户端各请求的 Cookie。
`MaxCookieBytes` 限制导出记录的保守计费量；context 为开启的 store 预留该额度。
超限报告 Limit 并清空 store，防止超限状态持续保留。后端自身的分配开销不等同于这项逻辑字节额度。

原生入口：

- `Cookies()` 返回 `FCookies`，记录采用 Netscape cookie-file 格式。
- `ClearCookies()` 清空 store，返回剩余数量。
- `ImportCookies(records)` 加入或更新记录，返回 store 的总数量；先检查记录格式。

文件持久化由显式文件适配器处理，客户端不自行读取 cwd 下的 Cookie 文件。
后端导入过程中发生不可恢复分配失败时，已应用的 Cookie 更新不承诺回滚。

## 重试

`FRequest::Retry` 默认只有一次 attempt。增加次数必须同时设置 `bAllowReplay`；
POST 等非默认幂等方法还需要 `bAllowUnsafeMethods`。一次性上传流不允许配置自动重放。原生 `UploadFactory` 每次尝试或重定向 rewind 返回新源与长度；multipart 的各流部分使用同名工厂，见 [multipart](multipart.md)。
工厂在 I/O 执行器上调用，必须立即返回，不执行脚本或阻塞文件工作。返回空源、已消费源、异常或不一致的 rewind 长度会失败。

策略包括最多 32 次 attempt、可重试状态、传输错误开关、指数间隔上限和 Retry-After。
所有 attempt 共用提交时的总期限。取消和关闭会撤销等待中的重试定时器。
成功的 HTTP 响应仍可能是最终的 4xx/5xx；HTTP 状态不直接等同于操作传输失败。

curl 的地址解析、连接、接收中断、发送和期限错误分别映射为 Resolve、Connect、Read、Write、TimedOut；
分配失败映射为 Allocation，其余后端协议错误为 Protocol。SystemCode 保留原 curl 错误码，
attempt 的 ErrorCode 使用相同分类。上传源或回调已有的结构化错误保持原值。

流式响应只在响应头尚未发布、正文尚未交付时允许重试；已发布的数据不会撤回。
成功响应的 `Attempts` 记录各次状态、传输码、字节数量和最终 URL。

`FRequest::History` 可指向独立的 `FAttemptHistory`。`Snapshot()` 返回不可变 attempt、终态错误及
`bFinal`/`bTruncated`；`bFinal` 表示这份记录不会再追加。
逻辑终态、历史记录结束和底层资源全部释放是不同状态。
同一历史不得并发使用；`bFinal` 后复用会开启新记录，旧快照保持原值。
历史对象保留关联 operation 和其结果，快照与记录计入 context 保留额度；不再使用时释放它们。
记录因额度或分配失败而不完整时显式标记 `bTruncated`，不改变原操作错误。

累计额度不会随 attempt 重置：`MaxTotalBodyBytes` 计算解码后正文（含收到的重定向正文），
`MaxTotalHeaderBytes` 计算响应头，`MaxTotalUploadBytes` 计算源正文，不含 multipart framing。
缓冲正文和 multipart 的内容在各次提交/重定向重放前预扣；输入流按接受用于上传的数据计费。
因此缓冲正文的上传额度是保守的提交额度，不能当作实际线缆流量统计。
单响应 `MaxBodyBytes`、单次正文 `MaxUploadBytes` 与累计额度同时生效。
文件适配见[文件传输](files.md)；限额与实际传输统计的含义不同。

## 脚本

```cpp
http::client_options options = http::make_client_options();
options.set_cookies(true);
options.set_max_cookie_bytes(8192);
http::client client = http::create_client_with_options(context, options);

http::retry_options retry = http::make_retry_options();
retry.set_max_attempts(3);
retry.set_allow_replay(true);
retry.set_delay_ms(100);
request.set_retry_options(retry);
```

client 提供 `cookies(timeout)`、`clear_cookies(timeout)`、`import_cookies(snapshot, timeout)` 和
`import_cookie_record(record, timeout)`，均返回 operation。
查询结果使用 `operation.cookies()`，数量结果使用 `operation.count()`。
Cookie 快照提供 `size()` 与 `record(index)`。
响应提供 `attempt_count()` 与 `attempt(index)`，后者可查询 number、status、transport_code、
uploaded_bytes、downloaded_bytes、error_code 和 effective_url。
`make_attempt_history()`、`request.set_attempt_history(history)` 和 `history.snapshot()` 对应原生历史入口；
快照提供 is_final、is_truncated、attempt_count、attempt、error_code、system_code 与 error。
context 关闭后仍可复制历史诊断，复制继续占用保留字节额度，不启动新的网络操作。
累计额度可通过 request 的 set_total_body_limit、set_total_header_limit、set_total_upload_limit 设置。

逐跳头、相对跳转结果与可用计时/字节口径见 [HTTP 响应链与统计](http-trace.md)。
request 的 `set_redirect_limit(int64)` 显式设置重定向次数；零表示不允许跟随。

## 默认值

| API | 值 |
| --- | --- |
| `MaxConnectionsPerHost / MaxIdleConnections / MaxPendingRequests` | `8 / 64 / 128` |
| `MaxConnectionIdleTime / MaxCookieBytes` | `120 s / 1 MiB` |
| `FRetryOptions::Delay / MaxDelay` | `100 ms / 30000 ms` |
| `bHonorRetryAfter / bRetryTransportErrors` | `true / true` |
| `Statuses` | `408, 429, 500, 502, 503, 504` |

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/client-policy.md) · [fr-FR](../../fr-FR/Reference/client-policy.md) · [ja-JP](../../ja-JP/Reference/client-policy.md) · [ko-KR](../../ko-KR/Reference/client-policy.md) · [zh-CN](client-policy.md) · [zh-TW](../../zh-TW/Reference/client-policy.md)
