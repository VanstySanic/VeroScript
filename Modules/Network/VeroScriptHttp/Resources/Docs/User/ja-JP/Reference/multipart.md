# multipart の送信と解析

`FMultipartReader::TryCreate(content_type, out, error, options, context)` は境界を解析し、失敗時は `out` を保持します。ネイティブのコンテキストは省略可能です。指定した場合、有界ストレージと保持中の不変イベントは配信後も使用量に計上されます。コピーはデータを共有し、イベントはコンテキスト終了後も有効ですが、リーダーは新しい処理を受け付けません。`Feed(bytes, final, event, work)` は消費バイト数と最大 1 イベントを返します。未消費の末尾は同じ `final` で再送してください。`event` を変更するのは `EventReady` のみです。`final` は実際の EOF を意味します。借用する work のキャンセルと予算は呼び出し中だけ検査します。

state0NeedInput/1EventReady/2Done/3Failed（新 reader 必要）。event0PartBegin(index/head)/1Data(非空≤16 KiB/context)/2PartEnd/3End、End 後も HTTPepilogue/EOF まで排出。head は name/filename 欠落と空/type/重複順 header 保持。form-data は単一明確 Disposition/form-data/name（空可）、他 subtype は name 任意、nested は raw。headercase-insensitive、foldspace、重複 metadata/曖昧 type 拒否。NUL/元 encode/順保持、charset/percent/transferdecode せず、filename は file 操作せず preamble/epilogue/padding/跨 block を処理します。

既定 256part、header64 KiB/part ・ 1 MiBtotal、body64 MiB/part ・ 256 MiBtotal、framing 込み input272 MiB、pre/epilogue 各 64 KiB。各上限独立、body 集約なし、保持 event も Limit 要因。scriptmultipartoptions の 8max_*、readerfeed/state/event/error、eventkind/part_index/name/has_filename/filename/content_type/header_count/header/data を型検査します。event/suffix 後次 chunk、HTTPEnd 後 final で Done へ。VMcancel は呼び出し失敗、format は readerstate です。

upload は Body/一回 Upload/nativefactory 排他。既知長は送信範囲、短いと Protocol、超過は未送信、未知長は Finish 必要で WouldBlock≠EOF。buffer/stream 混合と反復 binary 保持。replay は I/O 上即時新 factory、script/blocking なし、既知/不明長一致。singleconsumer、全終端で不要 source 取消。single/totalUpload は MIMEframing 除外、buffer 前払/stream 受理時計上、metadata/buffer は context、pipe は別有界。backend が Content-Type/Length/Transfer-Encoding 生成。scriptbegin_multipart/add_field/add_file/*_stream、length=-1、256part/metadata+buffer16 MiB 以下、scriptI/Ofactory なし。fileadapter は別 API で利用可能、filename だけで disk アクセスしません。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/multipart.md) · [fr-FR](../../fr-FR/Reference/multipart.md) · [ja-JP](multipart.md) · [ko-KR](../../ko-KR/Reference/multipart.md) · [zh-CN](../../zh-CN/Reference/multipart.md) · [zh-TW](../../zh-TW/Reference/multipart.md)
