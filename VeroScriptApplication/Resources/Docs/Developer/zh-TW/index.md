# Application 服務維護

## 架構與邊界

`VeroScriptApplication` 位於 Core 之上。Core 編碼已編譯映像與 `.vslib` 程式庫；Application 管理標準 ZIP `.vsapp`、描述符、內容清單、準備流程與內容生命週期。ZIP、原生密碼適配器及 GUI 整合應留在 Core 外。`VeroScriptApplicationPackages` 將不依賴 HTTP 的 `VeroScriptPackageRuntime` 接入明確凍結的環境；開啟應用不得觸發相依解析或安裝。

`BuildApplication` 傳回已封存的檔案集合，由 `WriteArchive` 序列化外層 ZIP；接受原始映像的多載必須保留映像位元組與中繼資料。`FApplication::Open` 驗證儲存內容，`ReadEntryPackage` 僅檢查編譯中繼資料，不載入提供者或呼叫環境工廠。`Prepare` 保留環境與位元組擁有者，`FPreparedApplication::CompileSource` 套用自持的可攜原始碼預設值。Session 載入與重新載入應使用共同流程。

## 完整性、所有權與原則

目錄、ZIP、單一檔案形式應使用一致的內容清單、路徑正規化、ZIP 本機標頭、檔案模式、大小／解壓縮限制及雜湊驗證。拒絕額外檔案、路徑穿越、大小寫衝突與特殊檔案。可寫入快取保留所有權憑據及協作處理程序租約，拒絕外來目錄；原生內容使用者保留具現化檔案的租約。重新載入同時暫存程式碼與環境，失敗時一併復原；原生繫結目錄變更時需建立新的應用生命週期。

保護服務先驗證內容，再將明文交給 Core。簽章信任及獨立金鑰門檻由使用方決定；缺少必要服務時失敗，不可退回明文。重新組裝已簽署內容必須明確指定新簽章原則。保留原生密碼模組擁有者，不序列化秘密金鑰或回呼，並區分內容簽章與平台簽章。原始碼入口保留可攜編譯預設值，不帶入開發專用路徑。

## 整合與驗證

根建置控制選用模組，`CMakeLists.txt` 分別匯出 Application 與套件適配器。同步維護 Core 身分、SDK 匯出、`Resources/Schemas`、Interop／語言適配器、CLI 及 Runtime 委派。根建置啟用的 `VeroScriptApplicationTests` 與選用的 `VeroScriptApplicationPackageServicesTests` 涵蓋容器／準備流程及環境整合。修改時核對損毀輸入、信任失敗、擁有者生命週期、復原、原始碼編譯、移動執行及目錄／ZIP／單一檔案一致性。平台啟動器、簽章與實機執行需另行記錄驗證，靜態文件檢查不能代替這些結果。

`BuildApplication` 也接受已烘焙的 Core 應用程式映像，保留其位元組與宿主中繼資料。`FApplicationServices::PreparePackage` 在環境準備完成後、Session 載入前設定編譯入口的宿主服務；僅檢視映像不會呼叫它。`ConfigurePackageApplicationServices` 的選用 `ProvidersReady` 回呼向嵌入配接器提供已註冊的提供者、其持有的環境及擁有快照的後端選擇函式。選擇順序為宿主選項、應用程式服務設定、專案預設值，並保留內置部署的預設值及明確停用選項。GUI 透過這些掛鉤使用標準 `.vsapp` 容器，Core 無需依賴 GUI 或 ZIP。 `PreparePackage` 僅準備繫結與資源，不得執行入口程式。

<!-- docs-navigation -->

---

[使用者指南](../../User/zh-TW/index.md) · [開發文件](../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
