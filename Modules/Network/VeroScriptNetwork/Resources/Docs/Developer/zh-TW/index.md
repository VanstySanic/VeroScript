# 網路基礎: 原始碼職責

Network 擁有不透明情境、操作、配額及通用串流／檔案契約；Http/WebSocket 擁有協定介面和繫結。Boost 擁有 Asio/Beast、OpenSSL 整合和 BoostRuntime；Curl 擁有 HTTP 用戶端及 curl 原始碼。模組卸載期間仍須保留服務／基礎實作身分、建立映像的存續錨點和回傳物件的控制區塊。

手動註冊的主程式僅將 FBackendCatalog::Binding() 傳入前端註冊，註冊全部提供者後呼叫 Update。提供者註冊不得保留此目錄；套件管理器負責目錄組合及專案服務選擇。契約、基礎實作或相符 Core 身分改變後須重新建置所有參與映像。

`Public` · `Internal` （明確版本化的提供者擴充契約） · `Private` · `Resources/Docs` · `Resources/Examples` · `Resources/Localization` · `Resources/Tests` · `Resources/Tools/CMake`

`VeroScriptNetwork/Internal` → `VeroScript::NetworkProvider` （不透明 context/operation、配額、串流及升級契約）。

`Modules/Network/Backend/VeroScriptBoost/Private/Runtime` → `VeroScriptBoostRuntime`; `Modules/Network/Backend/VeroScriptCurl/Private` → HTTP 用戶端。共用的前端／提供者建置輔助歸 `VeroScriptNetwork/Resources/Tools/CMake`。Network、Http 和 WebSocket 的套件設定範本各歸自身的 `Resources/Tools/CMake`；分類根目錄僅協調 CMake 目標。

SDK 安裝 Public 及 include/VeroScript*/Backend 下明確版本化的不透明擴充契約；具體 context/operation 配置仍為私有。禁止引用其他模組的 Private。發行前使用相符 Core 配套重建後端和繫結，平台資格以實際測試結果為準。

## 原始碼建置與原生模組註冊

根建置的 `VEROSCRIPT_BUILD_NETWORK`、`VEROSCRIPT_BUILD_HTTP` 和 `VEROSCRIPT_BUILD_WEBSOCKET` 預設 OFF。選擇任一網路建置選項後 Boost 預設 ON，Curl 預設跟隨 HTTP；可用 `VEROSCRIPT_BUILD_BOOST` 和 `VEROSCRIPT_BUILD_CURL` 明確選擇後端。單獨建置 `Modules/Network` 時，Boost、HTTP 和 WebSocket 預設啟用。`VEROSCRIPT_NETWORK_BUILD_BINDINGS` 僅在 Core target 已存在時預設 ON；其他情況須明確啟用並提供精確匹配的已安裝 Core SDK。Curl 擁有 `VEROSCRIPT_CURL_USE_SYSTEM_LIBRARY`，預設 OFF，啟用時要求 curl 8.13 或更新版本。內建靜態程式庫啟用 PIC；Linux 內建設定需要 OpenSSL 開發檔案。

先註冊 Network，再註冊協定模組，並宣告可見相依。靜態主程式使用各模組的 `QueryModule` / `GetImplementationIdentity`，動態載入使用 `veroscript_extension_query`；靜態 `ContentIdentity` 識別實際實作。後端模組不應連結第二份 Core。BoostRuntime 與前端基礎實作須匹配參與的主程式映像；私有後端符號和編譯設定不進入前端 SDK。

<a id="network-execution-assembly"></a>

Core 提供執行契約，官方位元組碼 VM 已拆為獨立後端。選擇位元組碼執行的宿主須透過 `LoadVmBackend` / `SetVmBackendFactory` 明確提供相符的工廠；編譯期執行腳本擴充時，也須設定 `FCompileOptions::ScriptCompileExtensions.VmBackendFactory`。獨立建置繫結測試須指定 `-DVEROSCRIPT_TEST_VM_LIBRARY=<VM 程式庫絕對路徑>`；完整 Core SDK 的 `bin/Vm` 包含對應平台的程式庫。`BUILD_TESTING=OFF` 可僅建置繫結；僅資料層建置不需要 VM。

分別檢查無後端前端 SDK、功能開關、缺少／不相容服務、明確選擇、動態／靜態存續、保留結果、移位 SDK 和精確相依項。允許執行時再驗證原生協定及各平台；靜態文件檢查不代表執行期驗收。

<a id="cross-image-results"></a>

操作結果由產生映像中的 `CreateTypedResult`／`OwnResult` 解包，保留 `FOwnedResult`、FContext 及承載資料擁有權。`FOperation::Result<T>()` 比較產生端型別名稱、大小與對齊，不依賴跨映像 `std::any_cast` 的 RTTI 位址一致性；仍須匹配 C++ ABI／foundation 建置，不構成可攜 C ABI。標頭或結果儲存改變後重建全部消費端／提供者。保持透過 swap 提取回呼，使終態轉換僅釋放一次。

POSIX 原子不覆寫輸出優先使用可用的 `renameat2(RENAME_NOREPLACE)`，僅在系統呼叫／檔案系統不支援時退回硬連結；保留明確失敗及暫存檔清理。

<a id="provider-facade-ownership"></a>

提供者包裝 facade 時須傳入所屬不透明 context，並保留建立映像的壽命錨點。HTTP 前端提供型別化 exchange 工廠，使 `shared_from_this()` 和 HTTP upgrade 共用同一控制區塊。僅在提供者配置的控制區塊中以成員持有 context，不足以保證安全卸載；傳回物件和保留結果須使用共用工廠及擁有權輔助介面。後端目錄透過弱參考快取工廠，避免與環境形成參考環；提供者註冊不能持有該目錄。

## 本地化資源暫存

原始碼輔助檔案 `Resources/Tools/CMake/VeroScriptLocalization.cmake` 選擇 `network`、`http`、`websocket`、`network-all` 與獨立的 `json` profile。HTTP/WebSocket profile 包含 Network；`network-all` 合併三個網路模組。這些屬於建置及部署輸入，並非執行階段 SDK API。暫存會取代目標 profile 並刪除多餘項，因此應先選擇完整的資源根集合，不能依次疊加 profile。使用者的資源掛載方法由 User 診斷參考維護。

<!-- docs-navigation -->

---

[User](../../User/zh-TW/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
