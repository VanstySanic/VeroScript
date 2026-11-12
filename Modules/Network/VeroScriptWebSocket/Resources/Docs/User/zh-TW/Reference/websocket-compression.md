# WebSocket 壓縮

CompressionSupported為建置能力、bCompression為要求（預設false）、CompressionInfo為關閉後仍有效的不變結果；對端拒絕仍普通訊息。window預設15、允許9–15，takeover預設false，Level8(0–9)、MemoryLevel4(1–9)，client/server為協定方向；無協商disabled/window0。8位不偷換9，server略過不可用提議、client拒絕選擇。本地配置錯InvalidArgument，未知/重複/缺值/不支援協商於codec前報Protocol；到達順首支援，允許多欄位/列表/引號數字。

限制按應用/解壓byte，小壓縮輸入不能繞過；超限Limit、DEFLATE/frameProtocol、正常Closed、I/ORead、idleTimedOut。首終因在排空完整queue後仍保留。codec前預留1 MiB，未協商釋放，成功至實際codec存續結束，不等物理峰值。建置停用時要求壓縮Unsupported、普通ws/wss可，descriptor記websocketCompression。腳本compressionoptions的window/level/memory/takeover與setter、set_compression_options複本、connection.compression為實選；text/binary/empty/fragment/control語意不變。

## 範例

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

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/websocket-compression.md) · [fr-FR](../../fr-FR/Reference/websocket-compression.md) · [ja-JP](../../ja-JP/Reference/websocket-compression.md) · [ko-KR](../../ko-KR/Reference/websocket-compression.md) · [zh-CN](../../zh-CN/Reference/websocket-compression.md) · [zh-TW](websocket-compression.md)
