# SSE 用戶端

FSseClient在有界HTTP流上運作，Poll/存取/Close單一呼叫者不可並行；無每連線worker或I/O腳本。Connect只無正文GET，禁止upload/factory/length/multipart，管理Accept:text/event-stream及Last-Event-ID，CA/proxy/request限額沿用。

預設總期限無限、頭/正文idle60秒，不暗套HTTP30秒；明確Request保自身期限。正文總量無限、ring256 KiB、line64 KiB、event1 MiB、Poll64 KiB。context計ring/parser，事件複本由呼叫端管。FSseWork於開始/每塊前以0查取消，再只計實消費≤8 KiB，不重計尾段。falseCancelled、例外Callback，腳本預算/取消仍VM失敗。

Poll0Pending/1EventReady唯一改Out/2Reconnecting/3Closed/4Failed。須200+媒體型別，204停重連，錯status/type保頭，EOF不補未完整事件。重連預設無限、初3秒/最大60秒，網路/期限可重試，protocol/config/parseLimit/allocation不可。MaxReconnects算一生、0停，耗盡正常EOFClosed/網錯Failed。serverretry保留且限上限；ID僅完整空行確定，含僅ID更新，無新ID事件繼承。Close冪等、只在Poll啟動新request、不重放業務。腳本optionslimits/reconnect/idle(-1)/request、connect_sse[_with_options]、poll/close/event_*/last_event_id/status/reconnect/error，event_*限上次poll1。持續serverreply_stream與bufferreply_response不同。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/sse-client.md) · [fr-FR](../../fr-FR/Reference/sse-client.md) · [ja-JP](../../ja-JP/Reference/sse-client.md) · [ko-KR](../../ko-KR/Reference/sse-client.md) · [zh-CN](../../zh-CN/Reference/sse-client.md) · [zh-TW](sse-client.md)
