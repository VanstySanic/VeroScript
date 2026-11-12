# VeroScriptBoost

Boost 維護 Asio/Beast、OpenSSL 整合和 `Private/Runtime`（`VeroScriptBoostRuntime`）。具體實作與第三方來源清單歸本模組，前端 SDK 不依賴這些具體型別。

公共內容脈絡／操作所有權、目錄組合、SDK 邊界與建置選項統一維護在[Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/zh-TW/index.md)。

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [使用者](../../User/zh-TW/index.md)

<a id="provider-registration"></a>

`boost` 提供者註冊 `network`（`veroscript.network`），並依啟用的協定目標註冊 `http-server`（`veroscript.http.server`）、`websocket-client`／`websocket-server`（`veroscript.websocket`）。這些契約版本為 `0.1.1`，工廠酬載依序是 `Network::Backend::IService*`、`Http::Backend::IServerService*`、`WebSocket::Backend::IService*`。工廠無狀態，註冊不啟動 I/O，未知設定會被拒絕。動態探索使用 `veroscript_extension_query`，靜態註冊使用 `veroscript_boost_query`／`Boost::QueryModule`。只有 Boost 擁有 `VeroScriptBoostRuntime`，前端和 Curl 不得重複安裝它。應以相符的 foundation／Core 身分驗證協定開關中繼資料、租約與遷移後的使用端。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
