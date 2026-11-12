# HTTP 响应链与统计

客户端每个 attempt 提供不可变 `FAttemptDetails`。成功响应的 `Details` 与最后一个
`Attempts.back().Details` 共享快照；启用 `FAttemptHistory` 后，传输失败、取消、超时和关闭
也可以通过历史获取已经收齐的响应头。快照持有独立的数据与代码 Owner，在 context 关闭后仍可读取。
复制历史只共享 details；新增的快照与响应链存储仍受 context 保留字节额度约束。

## 响应链

`Responses` 按接收顺序保留每个完整响应头：信息响应、重定向响应和最终响应。
不完整头与 CONNECT 代理头不在此列表中。每项包含：

| 原生字段 | 含义 |
| --- | --- |
| `Status`、`Url`、`Method` | 当前响应状态、产生它的实际 URL 和 HTTP 方法 |
| `Locations` | 全部 Location 字段，保留重复值、空值和到达顺序 |
| `bRedirectFollowed` | 后端是否实际选择继续此跳转；不是只判断 3xx 状态 |
| `RedirectUrl` | 已选择的后续实际 URL，包含相对 Location 的解析结果 |
| `RedirectMethod` | 后续完整响应头证明的方法；连接或握手失败、尚未收到目标响应头时为空 |

重定向默认关闭。`FRequest::bFollowRedirects` 与 `MaxRedirects` 显式配置是否继续和次数。
301/302 的 POST 可转 GET，303 转 GET（HEAD 保持 HEAD）；307/308 保持方法与正文。
上传流必须提供可重放工厂，所有跳转与重试共享原始期限和累计流量额度。
重定向上限、目标连接失败等是传输失败，诊断通过 history 读取，不能伪造成功 response。

应用自定义请求头沿用该请求的 curl 转发规则；普通自定义字段会继续发送到重定向目标。
后端管理的 Authorization、Cookie 与 Host 处理遵循锁定 curl 的默认规则。请求级代理和 Cookie store
保持 client 配置与原有隔离语义；本接口不把响应链当作请求头转发控制器。

`SendStream` 的早期 head 提供当时的独立 details，`Statistics.bFinal=false`。
正文继续到达后，早期快照的字节数与字段不会改变。最终结果产生另一份快照。

## 统计口径

`FTransferStatistics` 的后端字段使用 `optional<uint64_t>`，不可用时为空。脚本对应 `int64`，
不可用返回 `-1`；实际值超出 int64 时报告范围错误。零是合法测量值，不能据此推断执行过 DNS 或 TLS。

| 字段 | 口径 |
| --- | --- |
| `TotalTimeUs` | 本 attempt 的后端总耗时，不含前一次 attempt 或重试间隔 |
| `QueueTimeUs` | curl 连接/传输队列等待，不包含模块接纳之前的等待 |
| `NameLookupTimeUs`、`ConnectTimeUs` | curl 的解析/连接累计里程碑，单位微秒 |
| `AppConnectTimeUs` | TLS 等应用连接的累计里程碑；复用连接或非 TLS 可为零 |
| `PreTransferTimeUs`、`StartTransferTimeUs` | 预传输与首字节累计里程碑，不能直接称为单独阶段时长 |
| `RedirectTimeUs` | 最后一次传输前全部重定向累计时间 |
| `NewConnections`、`FollowedRedirects` | 本 attempt 的新建连接与实际跟随跳转数量 |
| `LastTransferDownloadedBytes`、`LastTransferUploadedBytes` | 最后一次传输的 curl payload 字节，不含 HTTP 头，下载计数在内容解码之前 |
| `ReceivedHeaderBytes` | 本 attempt 所有交付给头回调的行字节，包括信息响应和 trailers；不含被抑制的 CONNECT 头 |
| `AcceptedBodyBytes` | 本 attempt 的正文回调接受量，含实际交付给回调的重定向正文，暂停重放不会重复计数 |

curl 的里程碑会累计重定向请求的对应时间，不能把一条链的这些值当作某一个连接的独立阶段。
这些计数不包含 TCP 重传、TLS framing 或后端全部内部缓存，未提供物理线路总字节统计。
原有 attempt 的 `DownloadedBytes` 为接受的正文回调量，`UploadedBytes` 沿用后端最后传输的 payload 量；
累计上传额度的预扣量是限制依据，不是实际传输统计。

后端定义：[里程碑](https://curl.se/libcurl/c/CURLINFO_STARTTRANSFER_TIME_T.html)、
[下载 payload](https://curl.se/libcurl/c/CURLINFO_SIZE_DOWNLOAD_T.html)、
[实际跳转数](https://curl.se/libcurl/c/CURLINFO_REDIRECT_COUNT.html)。

## 脚本

```cpp
request.follow_redirects(true);
request.set_redirect_limit(4);
http::transfer_details details = response.details();
http::response_head first = details.response(0);
http::transfer_statistics stats = details.statistics();
print(first.url());
print(to_string(stats.total_time_us()));
```

`response.details()` 和 `attempt.details()` 返回 details；details 提供 `response_count()`、
`response(index)`、`statistics()`。head 的 `status()`、`url()`、`method()`、`location_count()`、
`location(index)`、`redirect_followed()`、`redirect_url()`、`redirect_method()` 对应原生字段。
统计字段使用 snake_case 方法名，另有 `is_final()`。无客户端传输记录的手工构造响应不提供 details。

完整本地服务器样例：[redirect_trace.vs](../../../../Examples/redirect_trace.vs)。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/http-trace.md) · [fr-FR](../../fr-FR/Reference/http-trace.md) · [ja-JP](../../ja-JP/Reference/http-trace.md) · [ko-KR](../../ko-KR/Reference/http-trace.md) · [zh-CN](http-trace.md) · [zh-TW](../../zh-TW/Reference/http-trace.md)
