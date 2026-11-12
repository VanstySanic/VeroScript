# Network

安裝所用 API 的前端套件及選定原生後端。`vs-boost` 提供 `network` 和實際啟用的 HTTP 伺服器、WebSocket 服務；`vs-curl` 提供 `http-client`。僅安裝前端不會提供通訊實作。服務缺少或契約不符會明確回報錯誤。

<a id="backend-selection"></a>

腳本用 `Network::create_context_with_backend("boost", false)` 明確選擇執行基礎。HTTP 用戶端／伺服器、SSE 和 WebSocket 選項用 `set_backend` 覆寫環境選擇。空要求繼承 `services.<service>.backend`；未設定時，`auto` 按後端 ID 順序嘗試已啟用的提供者。明確的 `auto` 覆寫設定預設值，`none` 或設定中明確儲存的空值停用服務。缺少必要服務會明確失敗，執行錯誤不會在另一後端重播操作。`boost`、`curl` 是提供者 ID，不是固定預設值。C++ 消費者傳入具型別服務實例；這些腳本選擇值不取代 C++ 選項的實例比對契約。

`vs-network` · `VeroScript::Network`

- [完成回呼與程式生命週期](Reference/completion.md)
- [診斷與本地化](Reference/diagnostics.md)
- [檔案與原子輸出](Reference/files.md)
- [網路參考](Reference/README.md)
- [有界位元組串流](Reference/streaming.md)
- [TLS 設定](Reference/tls.md)

[Boost](../../../../../Backend/VeroScriptBoost/Resources/Docs/User/zh-TW/index.md) · [Curl](../../../../../Backend/VeroScriptCurl/Resources/Docs/User/zh-TW/index.md)

<!-- docs-navigation -->

---

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
