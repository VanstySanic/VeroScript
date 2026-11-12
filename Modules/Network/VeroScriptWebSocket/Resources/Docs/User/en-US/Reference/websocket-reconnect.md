# Bounded WebSocket reconnect

ConnectWithRetry creates one explicit bounded connection round, returning the same normal connection type as Connect. It neither replaces an old handle nor automatically sends/replays business data. Call again after an old connection closes if desired. Default MaxAttempts=1, maximum 32 including first handshake. Delay≥0, MaxDelay≥Delay, exponential backoff by default. Positive MaxElapsed bounds submit/queue/proxy/TLS/handshake/backoff, time settings ≤one year. Per-attempt options.Operation.Timeout is clipped to remaining round time, never resets it; observer Wait timeout does not change it.

Resolve/Connect/Read/Write/Closed/TimedOut may retry; Protocol only if bRetryProtocolErrors=true. InvalidArgument/Unsupported/QueueFull/Limit/Allocation/Cancelled do not. Async timers need continued HostDriven PollIo but not script completion dispatch. One round reserves a total-operation slot and at most one current-attempt slot, plus actual connections. Cancel stops current/waiting and future attempts but cannot prove the remote saw no request. Physical cleanup follows Shutdown. Generation/attempt IDs reject late previous-round completions. Applications own acknowledgements, sequencing and deduplication.

TrySnapshot yields an owned value copy, Snapshot a shared immutable one charged while retained. Fields: Generation/Attempts/bFinal/Error; each attempt Number/bComplete/bConnected/Error. Stable/system codes survive diagnostic truncation, text ≤4 KiB at UTF-8 boundary with bMessageTruncated. Pre-admission failures may have no history: inspect Operation.Error first. Snapshots never change, may be copied after context close under quotas, and start no I/O or retain borrowed VM services.

Script make_reconnect_policy/set_limits/set_backoff/set_retry_protocol_errors and connect_with_retry(context,url,options,policy) use value copies. task.operation/snapshot exposes generation/is_final/size/at/error_code/system_code/error/message_truncated; attempt adds number/complete/connected. An attempt retains its snapshot after report release. The reconnect.vs example explicitly sends once on the resulting connection and demonstrates proxy/optional CA and report ownership.

## Examples

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

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](websocket-reconnect.md) · [fr-FR](../../fr-FR/Reference/websocket-reconnect.md) · [ja-JP](../../ja-JP/Reference/websocket-reconnect.md) · [ko-KR](../../ko-KR/Reference/websocket-reconnect.md) · [zh-CN](../../zh-CN/Reference/websocket-reconnect.md) · [zh-TW](../../zh-TW/Reference/websocket-reconnect.md)
