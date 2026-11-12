# 有界バイトストリーム

SendStream は Operation/Body/TryHead、pending は false/error なし、head 不変、response.Body へ重複せず、終端 BodyBytes/trailers です。buffer は 16 KiB 以上かつ context 上限内、総 body は別。待つ前に転送中排出します。curl block 単位 pause/replay は二重計上せず、上下独立、他接続を止めません。reader 取消/解放は request 取消、保持 byte を読んでから終端 error。全 TLS/curl/HTTP2/OS heap の cap ではありません。

Upload は single-reader、length 任意、backend framing、MaxUploadBytes=64 MiB。既知不足は失敗、WouldBlock は write/Finish 待機。一回 source は再利用/body/multipart/HEAD/自動 redirect と併用不可、nativefactory で明示 replay 可。backend が Content-Length/Transfer-Encoding を管理します。script set_upload(length=-1)、upload_limit/send_stream/operation/body/head_ready/head/body_bytes、VM 予算/HostDriven poll と yield が必要です。

ReplyStream は header/fixedtrailer を snapshot、BodySource と非空 Body は排他。state0Uncommitted/1HeadersSent/2BodyStreaming/3Finished/4Aborted、BodyBytes は書き終え block、完了は local 受理で remote 処理ではなく送信済み rollback なし。HTTP1.1 不明長/trailer は chunked、他 Content-Length、1.0 不明長 close/trailer 不可。HEAD は header、204/304 は body なし source 取消。長短 Protocol、上限 Limit、framing 再構築、trailer に framing/connection/routing 不可。precommit 引数失敗は exchange 非消費。block≤16 KiB と pipe 別計上、failure/cancel/timeout で close。ResponseTimeout は idle、options.Timeout は total、SSE で total 無効可も byte 上限有効。script reply_stream の length/total は-1、同 context、operation/state/body_bytes、trailer は提出前です。

bStreamRequests 既定 false、true は head で Accept、Request.Body 空、BodyStream/Operation と final body_bytes/trailers、早期 head 不変、100-continue 後読取。buffer64 KiB、MaxStreamBodyBytes 総量、block≤16 KiB/容量。排出か取消、body と response 両方終わって次 request、古 exchange 再利用不可。input/output 独立、scriptserveroptions の stream/buffer/body/header/pending/deadline/TLS は値 snapshot、buffered mode で streamaccess 不可です。

Shutdown は新 connection/request/Accept を止め未配達/idleclose、配達済み exchange は grace 中完了、次 request なし。期限で強制 bForced、ClosedConnections は開始時 active 数で物理解放ではありません。grace0–365 日と operationtotal 有効、cancel/timeout/Close は元 error、並行 shutdown State、既 close は成功 0 です。server 保持、破棄 Close。scriptshutdown/grace/total、forced/connections、case-insensitive header/trailer を型付き結果として使います。

## 例

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
