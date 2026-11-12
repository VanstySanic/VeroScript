# 有界位元組串流

SendStream 提供 Operation/Body/TryHead，pending 是 false/無錯，頭不變，正文不另聚合 response.Body；終態有 BodyBytes/trailers。buffer 至少16 KiB、不超 context，總量另限；須邊傳邊排空再等待。curl 整塊 pause/replay 不重複、上下獨立、其他連線繼續。reader 取消/丟棄取消請求，先讀保有資料再報終錯，不限制完整 TLS/curl/HTTP2/OS 儲存。

Upload 是單消費 reader，可省長度、後端 framing；MaxUploadBytes 預設64 MiB，已知不足失敗，WouldBlock 等 write/Finish。單次來源不可重用或混用緩衝/multipart/HEAD/自動 redirect；原生 factory 允許明確重放。後端管理 Content-Length/Transfer-Encoding。腳本 set_upload(length=-1)/upload_limit/send_stream/operation/body/head_ready/head/body_bytes，保留 VM 預算並持續 HostDriven/yield。

ReplyStream 快照頭/固定 trailer，BodySource 與非空 Body 互斥。狀態0 Uncommitted/1 HeadersSent/2 BodyStreaming/3 Finished/4 Aborted，BodyBytes 算完整寫出塊；完成只代表本地接受、不代表對端業務，已送不可回復。HTTP1.1 未知長/trailer 用 chunked、其他 Content-Length；1.0 未知長關閉、不支援 trailer；HEAD 僅頭、204/304 無正文且取消來源。長錯 Protocol、總限 Limit，framing 重建、trailer 禁 framing/連線/路由欄位；提交前引數失敗不消耗 exchange。傳輸塊≤16 KiB、管道另計，錯/取消/期限關閉；ResponseTimeout 為 idle、options.Timeout 為 total，SSE 可關 total 但總量仍限。腳本 reply_stream 長/總期限 -1、同 context、operation/state/body_bytes，trailer 提交前固定。

bStreamRequests 預設false，開啟後頭完成即 Accept，Request.Body 空、單消費 BodyStream/Operation，最終結果 body_bytes/trailers，初始頭不變，讀前100-continue。預設buffer64 KiB、總 MaxStreamBodyBytes、塊≤16 KiB/容量。持續排空或取消，正文與回應皆完成才下一請求，舊 exchange 不能重用。入出獨立，server options 的 stream/buffer/body/header/pending/time/TLS 按值；緩衝模式流存取失敗。

Shutdown 停新連線/請求/Accept，關未交付/idle，已交付在 grace 內完成且無下請求。到期強制 bForced，ClosedConnections 是開始活躍數、不表物理回收。grace0–365天與總期限皆適用；取消/逾時/Close 保原錯，並行shutdown為State、已關再呼成功0。保有server，解構等Close。腳本 shutdown/forced/connections 及不分大小寫 header/trailer 依型別結果。

## 範例

```VeroScript
Network::byte_pipe output = Network::create_byte_pipe(context, 65536);
http::response_builder response = http::create_response(200, "");
response.set_header("Content-Type", "text/event-stream");
http::response_stream_operation sending = exchange.reply_stream(response, output.reader(), -1, -1);
Network::writable_stream writer = output.writer();
writer.write(http::encode_sse("hello", "", ""));
// On WouldBlock yield; retry the unaccepted suffix on a later tick.
writer.finish();
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](streaming.md)
