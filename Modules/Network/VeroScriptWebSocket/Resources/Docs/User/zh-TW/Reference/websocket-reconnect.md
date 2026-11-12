# WebSocket 有界重連

ConnectWithRetry為明確一輪，不取代舊handle或重送業務。MaxAttempts預設1/最多32含首，Delay非負、MaxDelay不少於Delay、預設指數上限。正MaxElapsed最多一年，含queue/proxy/TLS/handshake/backoff，各次期限裁至剩餘，不受Wait改變。Resolve/Connect/Read/Write/Closed/TimedOut可重試，Protocol須flag，InvalidArgument/Unsupported/QueueFull/Limit/Allocation/Cancelled不可。asyncTimer須HostDrivenPollIo、不需scriptdispatch，一總operation及至多一當前attempt加實際connection。

取消停目前/等待/未來，不保遠端未見請求，物理清理依Shutdown；generation/attemptID防舊遲到結果。ack/順序/去重屬應用。TrySnapshot擁有copy，Snapshot共享不變且計額度；Generation/Attempts/bFinal/Error，每項Number/bComplete/bConnected/Error，穩定/系統碼保留，文字4 KiB UTF8界並bMessageTruncated。接納前可無history先查Operation.Error，close後可複製，不啟新I/O或借VM。腳本policy/limits/backoff/retryflag/connect_with_retry，reportgeneration/is_final/size/at/error群及attemptnumber/complete/connected，釋放report後仍保快照。

## 範例

```cpp
WebSocket::FReconnectPolicy policy;
policy.MaxAttempts = 3;
policy.Delay = std::chrono::milliseconds(100);
policy.MaxDelay = std::chrono::seconds(2);
policy.MaxElapsed = std::chrono::seconds(15);
auto task = WebSocket::ConnectWithRetry(context, url, policy, options);
auto operation = task.Operation();
```

```VeroScript
websocket::reconnect_policy policy=websocket::make_reconnect_policy();
policy.set_limits(3,15000);
policy.set_backoff(100,2000,true);
websocket::reconnect_task task=websocket::connect_with_retry(context,url,options,policy);
websocket::operation opening=task.operation();
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/websocket-reconnect.md) · [fr-FR](../../fr-FR/Reference/websocket-reconnect.md) · [ja-JP](../../ja-JP/Reference/websocket-reconnect.md) · [ko-KR](../../ko-KR/Reference/websocket-reconnect.md) · [zh-CN](../../zh-CN/Reference/websocket-reconnect.md) · [zh-TW](websocket-reconnect.md)
