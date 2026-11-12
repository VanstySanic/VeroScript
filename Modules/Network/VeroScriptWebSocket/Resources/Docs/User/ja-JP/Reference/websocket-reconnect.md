# 有界 WebSocket 再接続

ConnectWithRetry は明示一 round で旧 handle 置換/businessreplay しません。MaxAttempts 既定 1/最大 32 初回含む、Delay 非負/MaxDelay 以上、既定指数上限。positiveMaxElapsed≤1 年は queue/proxy/TLS/handshake/backoff 全部、attemptdeadline は残時間へ clip、Wait で変わりません。Resolve/Connect/Read/Write/Closed/TimedOut は再試行、Protocol は retryflag のみ、InvalidArgument/Unsupported/QueueFull/Limit/Allocation/Cancelled 不可。asyncTimer は HostDrivenPollIo 必要、scriptdispatch 不要、globaloperation1 と currentattempt 最大 1+実 connection。

cancel は current/wait/future 停止でも remote 未受信を保証せず Shutdown 回収。generation/attemptID で古い遅延完了を除外、ack/sequence/dedup は caller。TrySnapshot は所有 copy、Snapshot は quota 付き不変 shared。Generation/Attempts/bFinal/Error、attemptNumber/bComplete/bConnected/Error、stable/systemcode 保持、text≤4 KiB UTF8 境界と bMessageTruncated。pre-admissionhistory なしなら Operation.Error。snapshot は close 後 copy 可で新 I/O/VM 借用なし。scriptpolicy/set_limits/set_backoff/retryflag/connect_with_retry、reportgeneration/is_final/size/at/error 群、attemptnumber/complete/connected は report 解放後も snapshot 保持します。

## 例

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

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/websocket-reconnect.md) · [fr-FR](../../fr-FR/Reference/websocket-reconnect.md) · [ja-JP](websocket-reconnect.md) · [ko-KR](../../ko-KR/Reference/websocket-reconnect.md) · [zh-CN](../../zh-CN/Reference/websocket-reconnect.md) · [zh-TW](../../zh-TW/Reference/websocket-reconnect.md)
