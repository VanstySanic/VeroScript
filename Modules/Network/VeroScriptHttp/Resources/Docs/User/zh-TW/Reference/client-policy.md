# HTTP 用戶端策略

`FClient(Context, Service, FClientOptions)` 固定連線池與 Cookie 策略，Cookie 和自動重試預設停用。`MaxConnections=0` 採用 context 上限；每主機上限、閒置連線數及保留時間也會生效。`MaxPendingRequests` 包含連線排隊及重試等待，並共用原始期限；超限回報 `QueueFull`。只有 `bInheritProxyEnvironment=true` 才繼承環境代理，非空的明確 `Proxy` 優先，且不修改處理序環境。

`Send` 緩衝完整回應，`MaxBodyBytes` 必須同時不超過 context 緩衝預算和目前平台的 `Network::FBytes::max_size()`，否則提交時回報 `InvalidArgument`。`SendStream` 仍使用 64 位元本文累計上限，並透過 `BufferBytes` 單獨限制排隊記憶體。

Cookie 每 client 隔離，MaxCookieBytes 保守預留，超限 Limit 並清空 store，不是物理 heap 上限。Cookies/ClearCookies/ImportCookies 使用 Netscape 格式驗證與快照/數量；檔案持久明確，不讀 cwd。後端不可恢復配置失敗不保證回復已套用 Cookie。

預設1次，最多32次；多次須 bAllowReplay，不安全方法另須 bAllowUnsafeMethods。單次流不可重放。UploadFactory 每次嘗試/redirect rewind 在 I/O 執行器立即提供新來源/長度，不執行腳本或阻塞；空/已消費/例外/長度不符失敗。狀態、傳輸條件、指數上限延遲、Retry-After 共用原期限，取消清 timer。curl 對應 Resolve/Connect/Read/Write/TimedOut/Allocation，其他 Protocol，SystemCode 保原值與來源既有錯誤。頭/正文已發布後不可重試串流。

Attempts/FAttemptHistory 保有不變快照/error/bFinal/bTruncated，final 不等物理釋放，不可並行重用；final 後重用新開記錄，舊快照不變。歷史/結果保有 Owner/額度，診斷截斷不改原錯。MaxTotalBodyBytes 算解碼後及 redirect 正文、Header 算全頭、Upload 算來源不含 MIME framing；緩衝提交/重放前預扣，串流按接收，單次限制同時有效，預扣不是線路統計。set_redirect_limit(0) 關閉追隨，腳本 client/retry options、set_retry_options、total_*_limit、Cookie/history 操作提供上述功能。

## 範例

```VeroScript
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

## 預設值

| API | 值 |
| --- | --- |
| `MaxConnectionsPerHost / MaxIdleConnections / MaxPendingRequests` | `8 / 64 / 128` |
| `MaxConnectionIdleTime / MaxCookieBytes` | `120 s / 1 MiB` |
| `FRetryOptions::Delay / MaxDelay` | `100 ms / 30000 ms` |
| `bHonorRetryAfter / bRetryTransportErrors` | `true / true` |
| `Statuses` | `408, 429, 500, 502, 503, 504` |

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/client-policy.md) · [fr-FR](../../fr-FR/Reference/client-policy.md) · [ja-JP](../../ja-JP/Reference/client-policy.md) · [ko-KR](../../ko-KR/Reference/client-policy.md) · [zh-CN](../../zh-CN/Reference/client-policy.md) · [zh-TW](client-policy.md)
