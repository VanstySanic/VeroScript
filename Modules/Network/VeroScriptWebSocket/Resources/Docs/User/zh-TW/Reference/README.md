# WebSocket — 網路參考

前端安裝與後端選擇的公共規則統一見 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-TW/index.md#backend-selection)。

`vs-websocket` 依賴相符的 `vs-network`。原生 ID 與腳本命名空間為 `websocket`，CMake 目標為 `VeroScript::WebSocket`；選用的 `VeroScript::WebSocketBindings` 需要相符 Core。先載入 Network，再載入 WebSocket，並宣告可見相依。Http、curl、Json 並非必要，JSON 組合使用 `COMPONENTS Json`。

`FConnection` 提供 ws/wss 用戶端與伺服器、文字／二進位訊息、選用壓縮、自動 Pong 及有界佇列。沒有待處理的腳本 receive 仍持續原生讀取，以處理 Ping 和關閉。期限、回呼派發、額度與生命週期依循 Network 契約；型別不符、未完成或失敗的結果存取會被拒絕，選項為值快照。隨提供者部署 BoostRuntime，並確保所有原生元件的 Core、組建、平台及工具鏈相符。

- [完成回呼與程式生命週期](completion.md)
- [診斷與本地化](diagnostics.md)
- [JSON 配接](json-adapters.md)
- [TLS 設定](tls.md)
- [WebSocket 壓縮](websocket-compression.md)
- [WebSocket CONNECT 代理](websocket-proxy.md)
- [WebSocket 有界重連](websocket-reconnect.md)
- [WebSocket 訊息與控制框架](websocket.md)

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](README.md)
