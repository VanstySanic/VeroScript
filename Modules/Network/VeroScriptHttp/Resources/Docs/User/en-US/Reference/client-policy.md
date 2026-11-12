# HTTP client policy

`FClient(Context, Service, FClientOptions)` freezes pool/Cookie policy; Cookies and automatic retries are disabled by default. MaxConnections=0 uses the context cap; per-host, idle cache count/age and MaxPendingRequests apply. Pending includes connection queues/retry waits, charged to the original deadline; excess is QueueFull. Proxy environment inheritance is disabled unless bInheritProxyEnvironment=true, and a nonempty request Proxy wins. The process environment is not modified.

`Send` buffers the complete response: `MaxBodyBytes` must fit both the context buffer budget and the platform's `Network::FBytes::max_size()`; otherwise submission fails with `InvalidArgument`. `SendStream` keeps a 64-bit total body limit and bounds queued memory separately with `BufferBytes`.

Cookies are isolated per client. MaxCookieBytes reserves conservative record quota in the context; exceeding it reports Limit and clears the store, not a physical-backend heap cap. Cookies()/ClearCookies()/ImportCookies use Netscape records, validate imports and return snapshot/count. Persistence is explicit, no cwd Cookie file. Unrecoverable backend allocation during import need not roll back already applied updates.

Retry starts with one attempt, at most 32. More attempts require bAllowReplay; unsafe methods additionally bAllowUnsafeMethods. One-shot uploads cannot replay. Native UploadFactory supplies a fresh source/length for each attempt/redirect rewind, immediately on the I/O executor, without script/blocking work. Null, consumed, throwing or inconsistent rewind sources fail. Retry status/transport conditions, exponential capped delay and Retry-After share one submission deadline; close/cancel removes timers. Resolve/Connect/Read/Write/TimedOut/Allocation map curl failures, other protocol errors to Protocol; SystemCode retains curl’s code and existing source errors survive. A stream response can retry only before published headers/body.

Response Attempts and optional FAttemptHistory retain immutable attempts, error, bFinal/bTruncated; final does not mean all native resources are freed. Do not use one history concurrently. Reuse after bFinal starts a new record without changing old snapshots. History/results/snapshots retain owners/quota; incomplete diagnostics set bTruncated without changing the operation error.

Limits accumulate across retries/redirects: MaxTotalBodyBytes is decoded delivered body including redirects; MaxTotalHeaderBytes is headers; MaxTotalUploadBytes is source payload, excluding multipart framing. Buffered/multipart input is conservatively charged before each submission/replay; streaming input on acceptance. MaxBodyBytes/MaxUploadBytes also apply. Reserved upload quota is not wire statistics. set_redirect_limit(0) disables following. Script make_client_options/make_retry_options, create_client_with_options, request.set_retry_options and total_*_limit setters expose these policies; Cookie methods return typed operations, make_attempt_history/set_attempt_history/snapshot expose diagnostics.

## Examples

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

## Defaults

| API | Value |
| --- | --- |
| `MaxConnectionsPerHost / MaxIdleConnections / MaxPendingRequests` | `8 / 64 / 128` |
| `MaxConnectionIdleTime / MaxCookieBytes` | `120 s / 1 MiB` |
| `FRetryOptions::Delay / MaxDelay` | `100 ms / 30000 ms` |
| `bHonorRetryAfter / bRetryTransportErrors` | `true / true` |
| `Statuses` | `408, 429, 500, 502, 503, 504` |

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](client-policy.md) · [fr-FR](../../fr-FR/Reference/client-policy.md) · [ja-JP](../../ja-JP/Reference/client-policy.md) · [ko-KR](../../ko-KR/Reference/client-policy.md) · [zh-CN](../../zh-CN/Reference/client-policy.md) · [zh-TW](../../zh-TW/Reference/client-policy.md)
