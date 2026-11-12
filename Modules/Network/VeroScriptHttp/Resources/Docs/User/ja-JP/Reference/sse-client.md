# SSE クライアント

FSseClient は有界 HTTPstream、Poll/accessor/Close は単一 caller で並行不可。perconnectionworker/scriptI/O なし。Connect は body なし GET、upload/factory/length/multipart 不可、Accept:text/event-stream と Last-Event-ID を管理、CA/proxy/requestlimit は継承します。

既定 total 無限、head/bodyidle60s（HTTP30s を暗黙適用せず）、明示 Request は自身の期限保持。bodytotal 無限、ring256 KiB、line64 KiB、event1 MiB、Poll64 KiB。context は ring/parser 計上、eventcopy は caller。FSseWork は開始/各 block 前 0 で取消、実消費≤8 KiB だけ課金し suffix 二重なし。falseCancelled/例外 Callback、VM 予算/cancel は実行失敗です。

Poll0Pending/1EventReady のみ Out 更新/2Reconnecting/3Closed/4Failed。200 と media 必須、204 は再接続停止、誤 status/type も head 保持、EOF で未完 event 生成なし。再接続既定無限、3s 初期/60s 最大、network/timeout 可、protocol/config/parseLimit/allocation 不可。MaxReconnects は life 全体、0 無効、限界の正常 EOF は Closed、networkFailed。serverretry 保持上限、ID は空行完結時のみ確定（IDonly 含む）、後 event 継承。Closeidempotent、新 request は Poll 中だけ、businessreplay なし。scriptoptionslimits/reconnect/idle(-1)/request、connect_sse[_with_options]、poll/close/event_*/last_event_id/status/reconnect/error、event_*は直前 1 時だけ。serverreply_stream と bufferedreply_response は別です。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/sse-client.md) · [fr-FR](../../fr-FR/Reference/sse-client.md) · [ja-JP](sse-client.md) · [ko-KR](../../ko-KR/Reference/sse-client.md) · [zh-CN](../../zh-CN/Reference/sse-client.md) · [zh-TW](../../zh-TW/Reference/sse-client.md)
