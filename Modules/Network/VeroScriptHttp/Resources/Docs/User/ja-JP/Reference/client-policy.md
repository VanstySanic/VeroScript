# HTTP クライアントポリシー

`FClient(Context, Service, FClientOptions)` は接続プールと Cookie の設定を固定します。Cookie と自動再試行は既定で無効です。`MaxConnections=0` はコンテキストの上限を使用し、ホストごとの上限、待機接続の数と保持時間も適用します。`MaxPendingRequests` には接続待ちと再試行待ちを含み、元の期限を共有します。超過時は `QueueFull` です。`bInheritProxyEnvironment=true` でない限り環境変数のプロキシを無視し、空でない明示的な `Proxy` が優先します。プロセス環境は変更しません。

`Send` は応答全体を保持するため、`MaxBodyBytes` は context のバッファ予算とプラットフォームの `Network::FBytes::max_size()` の両方以内である必要があります。超過時は送信受付で `InvalidArgument` を返します。`SendStream` の総本文上限は 64 ビットのまま、待機中のメモリは `BufferBytes` で別に制限します。

Cookie store は client 別、MaxCookieBytes を保守的予約、超過 Limit で store を消去（物理 heap cap ではない）。Cookies/ClearCookies/ImportCookies は Netscape 形式を検査し snapshot/count を返します。永続化は明示、cwd file なし。不可回復な backend 割当失敗では適用済み更新を戻しません。

retry 既定 1 回、最大 32。複数は bAllowReplay、unsafe method は bAllowUnsafeMethods も必要。一回 source は replay 不可。UploadFactory は各 attempt/redirect rewind で新 reader/length を I/O executor から即時返し、script/blocking 不可。null/消費済み/例外/rewind 長不整合は失敗です。状態/transport 条件/指数上限 delay/Retry-After は同 deadline、cancel で timer 停止。curl は Resolve/Connect/Read/Write/TimedOut/Allocation、他 Protocol、SystemCode 保持、source 既存 error 保持。head/body 公開後 stream retry 不可。

Attempts/FAttemptHistory は不変 snapshot、error/bFinal/bTruncated。final は物理解放ではなく、並行再利用不可、final 後再使用は新記録で旧 snapshot 不変です。history/result は Owner/quota 保持、診断切捨ては元 error を変えません。MaxTotalBodyBytes は decode 後と redirect body、Header は全 header、Upload は multipart framing なし source。buffer は提出/replay 前課金、stream は受理時、単回上限も適用。予約は wire 統計ではありません。set_redirect_limit(0)は追従なし、script client/retry options、set_retry_options、total_*_limit、Cookie operation/history API で制御します。

## 例

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

## 既定値

| API | 値 |
| --- | --- |
| `MaxConnectionsPerHost / MaxIdleConnections / MaxPendingRequests` | `8 / 64 / 128` |
| `MaxConnectionIdleTime / MaxCookieBytes` | `120 s / 1 MiB` |
| `FRetryOptions::Delay / MaxDelay` | `100 ms / 30000 ms` |
| `bHonorRetryAfter / bRetryTransportErrors` | `true / true` |
| `Statuses` | `408, 429, 500, 502, 503, 504` |

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/client-policy.md) · [fr-FR](../../fr-FR/Reference/client-policy.md) · [ja-JP](client-policy.md) · [ko-KR](../../ko-KR/Reference/client-policy.md) · [zh-CN](../../zh-CN/Reference/client-policy.md) · [zh-TW](../../zh-TW/Reference/client-policy.md)
