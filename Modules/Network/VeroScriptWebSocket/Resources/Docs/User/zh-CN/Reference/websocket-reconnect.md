# WebSocket 有界重连

`VeroScriptWebSocketReconnect.h` 提供显式的 `ConnectWithRetry`。每次调用建立一轮独立连接，
成功结果仍为 `shared_ptr<WebSocket::FConnection>`，与普通 Connect 一致。
旧连接关闭后，应用可以再次调用它；这个接口不会替换调用方已经持有的连接或自动发送业务消息。

## 策略

```cpp
WebSocket::FReconnectPolicy policy;
policy.MaxAttempts = 3;
policy.Delay = std::chrono::milliseconds(100);
policy.MaxDelay = std::chrono::seconds(2);
policy.MaxElapsed = std::chrono::seconds(15);
auto task = WebSocket::ConnectWithRetry(context, url, policy, options);
auto operation = task.Operation();
```

- 默认只尝试一次；最大次数为 32，包含第一次握手。
- Delay 必须非负，MaxDelay 不小于 Delay。默认使用翻倍退避并在 MaxDelay 截止。
- MaxElapsed 是本轮总时长，覆盖提交、排队、连接、TLS、代理、WebSocket 握手和退避等待。
  总时长必须正数；时间配置最多一年。等待观察超时不改变这项期限。
- `options.Operation.Timeout` 限制每次尝试，并裁剪到本轮剩余时间。不会为后续尝试重置总期限。
- Resolve、Connect、Read、Write、Closed、TimedOut 可以在预算内重试。
  Protocol 默认不重试，显式 `bRetryProtocolErrors=true` 才允许重试被拒绝的握手等协议错误。
  InvalidArgument、Unsupported、QueueFull、Limit、Allocation、Cancelled 等不会自动重试。
- 等待使用异步计时器。HostDriven 模式需要宿主继续 PollIo；内部重试不依赖脚本完成回调派发。
- 本轮需要一个总 operation 槽和至多一个当前尝试槽；connection 配额继续按实际连接登记。

取消总 operation 同时取消当前尝试或等待，不开始后续握手。
取消终态不保证远端从未收到连接请求；物理回收遵守普通 context 的 Shutdown 契约。
每轮使用独立代次与当前尝试标识，旧轮次的迟到完成不能修改新轮次。
应用负责业务消息的确认、序号与去重；模块不会重发已发送或结果不确定的消息。

## 每次握手的结果

`task.TrySnapshot(value, error)` 返回调用方拥有的值副本。
`task.Snapshot(error)` 返回共享的不可变副本，其存储在独立存活期间计入 context 保留额度。
快照包含 Generation、Attempts、bFinal、Error；每项包含 Number、bComplete、bConnected 和 Error。
错误保留稳定代码与系统码；诊断文本最多 4 KiB，截断时保留 UTF-8 字符边界并设置 bMessageTruncated。
在接纳操作前发生的失败可能没有历史，先查询 `task.Operation().Error()`。

已发布快照不随后续尝试改变。context 关闭后仍可复制诊断，额度限制继续生效；读取诊断不会开启新的 I/O。
当前尝试、历史和共享副本的拥有关系不保存借用的 VM 调用器、参数或内存服务。

## 脚本

```vs
websocket::reconnect_policy policy=websocket::make_reconnect_policy();
policy.set_limits(3,15000);
policy.set_backoff(100,2000,true);
websocket::reconnect_task task=websocket::connect_with_retry(context,url,options,policy);
websocket::operation opening=task.operation();
```

policy 按值复制，提供 max_attempts、max_elapsed_ms 与 set_retry_protocol_errors。
`task.snapshot()` 返回拥有的报告；报告提供 generation、is_final、size、at、error_code、system_code、
error 和 message_truncated。尝试项提供 number、complete、connected 及同名诊断查询。
尝试项独立保留其原快照，释放报告句柄不会使它失效。

正式示例为 [reconnect.vs](../../../../Examples/reconnect.vs)。
它通过一次显式发送使用最终连接，配置代理和可选 CA PEM，并演示选项复制与报告所有权。

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/websocket-reconnect.md) · [fr-FR](../../fr-FR/Reference/websocket-reconnect.md) · [ja-JP](../../ja-JP/Reference/websocket-reconnect.md) · [ko-KR](../../ko-KR/Reference/websocket-reconnect.md) · [zh-CN](websocket-reconnect.md) · [zh-TW](../../zh-TW/Reference/websocket-reconnect.md)
