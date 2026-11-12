# VeroScriptCurl

Curl 維護 `Private/HttpClient.cpp` 中的 HTTP 用戶端、curl 相依設定與原始聲明。其提供者實作 Http 契約，不負責 Boost 傳輸或 WebSocket 服務。

公共內容脈絡／操作所有權、目錄組合、SDK 邊界與建置選項統一維護在[Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/zh-TW/index.md)。

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [使用者](../../User/zh-TW/index.md)

<a id="provider-registration"></a>

`curl` 提供者註冊 `http-client`／`veroscript.http.client`／`0.1.1`，工廠酬載為 `Http::Backend::IClientService*`。工廠無狀態，註冊不啟動 I/O，並拒絕未知設定。動態探索使用 `veroscript_extension_query`，靜態註冊使用 `veroscript_curl_query`／`Curl::QueryModule`。Curl 使用不透明、非擁有的通訊端就緒和計時器掛鉤，不依賴 Boost 標頭或程式庫；HTTPS 遵循 curl 自身的 TLS 設定。隨附 curl 修補、靜態相依部署及 Unix 系統程式庫要求由本模組維護；依實際建置驗證缺失／不符服務、租約和遷移後的 SDK／套件使用端。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
