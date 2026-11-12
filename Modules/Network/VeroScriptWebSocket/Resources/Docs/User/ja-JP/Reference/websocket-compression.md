# WebSocket 圧縮

CompressionSupported は build、bCompression は要求（false 既定）、CompressionInfo は close 後も不変結果。peer 拒否は通常 message 継続。window 既定 15/範囲 9–15、takeoverfalse、Level8(0–9)、MemoryLevel4(1–9)、client/server は protocol 方向、未合意 disabled/window0。window8 は 9 に置換せず serveroffer を skip/clientselection を拒否。localinvalid は InvalidArgument、未知/重複/欠値/非対応 selection は codec 前 Protocol、到着順最初の support、複数 field/list/quotednumber 対応。

application/decompressedbyte で制限、小圧縮入力でも迂回不可。超過 Limit、DEFLATE/frameProtocol、cleanClosed、I/ORead、idleTimedOut。最初の終端理由を queue 排出後も保持。codec 前 1 MiB 予約、非合意で返し合意時 actualcodec 寿命まで保持、物理 peak ではありません。buildoff は有効化 Unsupported、普通 ws/wss 可、descriptorwebsocketCompression。scriptcompressionoptions の window/level/memory/takeover と setter、set_compression_optionscopy、connection.compression 結果。text/binary/empty/fragment/control 意味は不変です。

## 例

```VeroScript
websocket::compression_options compression = websocket::make_compression_options();
compression.set_server_window_bits(12);
compression.set_client_no_context_takeover(true);
websocket::options options = websocket::make_options();
options.set_compression(true);
options.set_compression_options(compression);
// Query after a successful connection.
websocket::compression_info actual = connection.compression();
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/websocket-compression.md) · [fr-FR](../../fr-FR/Reference/websocket-compression.md) · [ja-JP](websocket-compression.md) · [ko-KR](../../ko-KR/Reference/websocket-compression.md) · [zh-CN](../../zh-CN/Reference/websocket-compression.md) · [zh-TW](../../zh-TW/Reference/websocket-compression.md)
