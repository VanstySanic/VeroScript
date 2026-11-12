# 已解析模組環境與原生擴充

## 職責與進入點

Core 接收已解析、篩選啟用狀態的不可變快照，並提供共用後端選擇契約。套件用戶端與宿主讀取專案設定、解析版本並提供已安裝候選；Core 不下載、解壓縮、安裝，也不掃描安裝位置或系統路徑。安裝不代表隱式匯入所有宣告。

建立 `FModuleEnvironmentDesc` 並呼叫 `FModuleEnvironment::Create`，將傳回的共用不可變環境設為 `FCompileOptions::ModuleEnvironment`，執行前再交給 `VeroScriptVm::SetModuleEnvironment`。原始碼匯出使用 `FCodegenOptions::ModuleEnvironment`。可建置的公開 SDK 範例位於 `Resources/Examples/ModuleEnvironment`。

## 模組與範圍

`Scopes` 定義擁有者及直接相依項，`EntryOwnerId` 選擇入口範圍；`bHostProvided` 明確允許所有模組存取宿主範圍。`SourceRoots` 使用帶有擁有權的絕對標準路徑。`Modules` 將公開匯入名稱精確對應至模組 ID、擁有者、內容身分及檔案，或帶 `Source` 的 `veroscript-module:/...` 記憶體模組。

一般匯入查詢精確對應，與本機候選衝突時報錯。明確的 `./` 或 `../` 只使用相對於匯入者的路徑，且仍受根目錄與相依檢查約束；目標不存在時不改查其他搜尋根。無效對應不退回同名檔案。`import`、`include`、再匯出匯入及巨集探索共用相同載入器。預設嚴格限制原始碼範圍，關閉環境與工作目錄的隱式搜尋；標準程式庫也須明確提供搜尋根與所屬範圍。

模組可以使用自身原始碼及宣告的直接相依項。再匯出的介面仍遵循語言可見性規則。原生宣告攜帶 `FBindingOrigin`，編譯時檢查所屬範圍與匯入可見性；公開 API 簽章可以公開使用其傳回值所需的原生型別。這是相依隔離，不是執行不可信原生程式碼的安全沙箱。

目錄與資源契約只發佈一次，之後不得修改宣告或回呼身分。`Apply` 以交易方式合併編譯輸入並拒絕衝突的作用中目錄。請以同一基礎登錄器建立共用快照，不要直接拼接具有獨立型別 ID 空間的作用中繫結。明確的原始碼覆寫仍以精確路徑生效。

## 資源服務與在地化

`FResourceProviderBinding` 包含提供者、擁有者、穩定契約身分及選用的純資源 `ModuleIds`。`IResourceProvider` 接收邏輯要求 `(ProviderId, ModuleId, Group, Path)`、取消檢查及位元組上限。路徑須為沒有目錄穿越的可攜相對路徑。成功但不存在與 I/O／提供者失敗須分開回報。存在的資源必須提供內容身分；選用的絕對實體路徑僅供診斷，不允許繞過提供者。

執行時使用 `FModuleEnvironment::ReadResource`；編譯處理器使用 `Context.ReadResource`；腳本編譯擴充使用 `read_resource(provider,module,group,path)`。讀取內容與不存在的觀測結果都加入一般相依記錄，變更時使準備結果失效。提供者須支援執行緒安全、遵守上限與取消並避免無限制讀取。Core 會複查輸出上限與身分。`MaxResourceBytes` 限制單次讀取；編譯另外限制累計輸入與相依數量。

透過環境重用既有的 `I18n::FI18nServices`、具型別訊息、內容及限制。套件中繼資料 JSON 由套件客戶端解讀，Core 不另增本地化格式。原生擴充可以透過 `GetService(id,version)` 要求宿主提供的具型別服務；具決定性的服務身分會加入處理器快取索引鍵。

## 原生註冊與生命週期

套件管理的原生 ID（`entries.native[].id`）遵循 `[a-z0-9]+(?:-[a-z0-9]+)*`。套件 Schema 允許 2–64 個字元，但 `Descriptor.Id[64]` 包含 NUL 結尾，實際可載入的原生 ID 最多為 63 個 ASCII 字元。描述符、資訊清單 ID 與 `ExpectedId` 必須完全一致；`entries.backends[].module` 與靜態註冊鍵 `package:ID` 參照同一 ID。Core 底層載入器檢查自身的有界 UTF-8 契約，不強制套件命名語法。隨附模組使用 `veroscript-sdl3`、`veroscript-rmlui`、`veroscript-stb`、`veroscript-font-backend` 與 `veroscript-gui-tools`；變更 ID 後需同步重建原生程式庫、繫結目錄與環境。

服務 ID、後端選擇值及介面契約 ID 各自遵循原有協定。例如，原生擴充 `veroscript-gui-tools` 請求的是獨立的宿主服務 `veroscript.gui.tools`，後者仍作為 `ToolServiceId` 查找鍵。

包含 `VeroScriptNativeExtensionLoader.h`。`LoadNativeExtension` 只接受明確的絕對程式庫檔案，不探索套件名稱、PATH 或工作目錄。靜態連結模組以相同查詢函式及擁有者呼叫 `QueryNativeExtension`。C 匯出符號為 `veroscript_extension_query`，`NativeExtensionApiVersion` 為 **0.1.1**。登錄前檢查預期 ID、版本與角色。

固定配置查詢描述元須符合公開 SDK ABI、Core 實作身分、編譯器/STL/執行階段、架構/平台、RTTI 及例外模式。C 查詢不代表後續 C++ 物件具穩定 C ABI。MSVC 動態擴充要求共用 CRT；iOS 與 WebAssembly 使用靜態登錄。二進位雜湊只識別快取，不驗證真實性或簽章；套件客戶端驗證可信產物並保留不可變檔案租約。

以 `FNativeExtensionRegistry::Add` 暫存模組。其 `Create` 接收 `INativeExtensionHost`，登錄函式、型別/成員、泛型範本、別名、註解詞彙、編譯處理器與服務提供者。擴充使用宿主虛擬函式表，不連結自己的靜態 Core。登錄具有原子性，失敗時保留先前快照。透過 `GetBindings`、`GetProcessors`、`GetProviders`、`GetOwners`、`GetDependencies`、`GetFingerprint` 建立已解析環境。可用契約應取自 `GetDependencies` 中已驗證的載入描述元與二進位身分，不可只由套件版本推測；其中的版本是原生元件版本，不會自動替換成發行套件版本。每個原生繫結提供者都須具精確的可用外部相依契約。

宿主服務是具有擁有生命週期的版本化介面，僅登錄階段可修改宣告。登錄後保留的服務可建立宿主物件、讀取資源及透過橋接提交編譯作業。使用 `MakeHostObject`/`MakeHostObjectNamed` 和不擲出例外的銷毀回呼，由 Core 配置擁有權控制區塊。回呼、處理器工廠/執行個體、提供者、服務與逸出物件仍存續時，強參考會保留模組程式碼。避免反向強持有擁有同一登錄的物件，此類連結使用弱參考。工廠與解構函式須遵循錯誤及擁有權契約，解構函式不得擲出例外。

登錄提供者不會初始化 GUI 或其他服務。上層選擇 `(ServiceId, BackendId)` 並驗證契約後，明確呼叫 `CreateNativeService`。傳回的執行個體持有環境與程式碼直到銷毀。JIT 沿用既有 `LoadJitBackend` 協定。

原生泛型範本沿用既有登錄契約：發佈不可變目錄前，在暫存的登錄器上以 `FHostBindingRegistry::InstantiateGenericHostType` 建立所需具體型別。執行期不修改已發佈目錄；腳本泛型由一般編譯器進行具現化。

## 快取、套件與重載

編譯指紋包含圖形擁有權、原始碼身分、繫結、處理器版本/設定、註解定義與編譯策略。即使沒有另外提供宿主設定指紋，原生登錄的設定項目也會自動加入處理器身分。資源另加入精確讀取相依。執行專用設定具有獨立執行身分。缺乏身分或非決定性的原生宿主服務會停用處理器快取；其他影響生成輸出的不透明回呼及設定也須由宿主提供穩定身分。

`FExternalDependency` 記錄精確 ID/版本/ABI/建置/內容身分；Core 只驗證、合併及序列化，不求解版本。必要契約保留於位元碼、`.vslib`/`.vsapp` 及連結結果；執行契約缺少或不符時，在初始化程式或 JIT 執行前失敗。開放泛型套件原始碼保留生產者範圍，匯入宿主須提供相容原生繫結及對應要求。套件輔助模組保留的擁有者 ID，即使其原始碼已內嵌於程式庫，也須在有效環境中宣告範圍與直接相依。產品及位元碼版本仍為 **0.1.1**，不接受較舊的開發版位元碼。

獨立 AOT 可透過相同環境與生成宣告匯出純腳本結果。具有執行相依的原生套件契約時，在目標執行階段配接器提供部署與繫結契約前會拒絕匯出，不會默默以無關 extern 宣告替換擴充回呼。

重新載入在閒置邊界發佈新環境，現有工作保留舊快照與檔案租約。未宣告 `NativeExtensionParallelRegistrations` 的模組再次登錄前須重新啟動程序。禁止就地替換已載入程式庫，請使用不可變的版本化路徑。原生物件攜帶登錄身分，即使型別同名，舊物件也不能進入新登錄的回呼。VM/工作階段的執行緒與取消規則繼續適用；阻塞的原生回呼無法強制中斷，已啟動的工作執行緒維持原環境。

## 驗證與整合

`VeroScriptModuleEnvironmentTests`、`VeroScriptNativeExtensionTests`、公開標頭測試與 `VeroScriptModuleEnvironmentExample` 驗證 Core 契約。動態測試程式庫刻意不連結 Core。各發行目標仍須執行自己的載入及裝置驗證。安裝與環境選擇由套件客戶端、CLI、Interop/各語言配接器負責，Core 接收其已解析快照。

## 命令列宿主服務

`CommandLine` 角色（`0x8`，套件拼寫為 `cli`）使用相符 SDK 的 `veroscript.cli` 服務，版本為 `0.1.1`。CLI 提供 `ICommandLineExtensionService`；原生模組在 Create 期間透過 `GetService` 取得，並在登錄關閉前提交描述元。組合角色模組須允許一般 SDK 宿主不提供 CLI 服務。套件客戶端在分析模式及未提供服務的宿主中跳過僅有 CLI 角色的項目。引數剖析及命令分派由 CLI 負責。

[CLI](../../../../../VeroScriptCli/Resources/Docs/Developer/zh-TW/process-and-assets.md)

## 後端選擇與環境適配

`VeroScriptBackendSelection.h` 定義 `ResolveBackendSelection`、`SelectServiceBackend` 和 `ResolveExecutionBackends`。省略要求繼承 `SelectedBackends`，設定中儲存的空值表示 `none`；明確的 `auto` 覆寫設定預設值。候選按 ID 排序，空、保留或重複候選 ID 會被拒絕。自動候選僅在工廠建立／準備失敗時繼續嘗試；取消直接傳播，執行中的操作不重播。

`FBackendSelectionResult` 區分 `Selected`、`Disabled`、`Unavailable`、`Failed`，保留 `BackendId`、拒絕的嘗試和錯誤。自動選用服務不可用時，環境建立仍可成功，呼叫者須同時檢查回傳的工廠。`FInstalledEnvironment::GetBackendCandidates` 僅查詢中繼資料，排除停用套件；`LoadJitBackend`／`LoadVmBackend` 可選擇回傳選擇記錄。`BackendDiagnostics` 保留不可用的自動提供者模組診斷。

`FModuleEnvironmentDesc` 攜帶 `SelectedBackends`、`ServiceProviders`、`BackendDiagnostics` 和 `ExecutionBackends`。套件適配層提供不可變解析回呼及 `ExecutionBackends.Identity`，並保留安裝快照與程式碼租約；`AnalysisOnly` 不安裝執行解析器。`FNativeServiceProviderDesc` 在選擇標頭中宣告，原生擴充輔助函式先驗證服務／後端／契約／版本組合，再建立實例。C、C++、C#、Python 共用原生 Session 解析，不分別實作套件探索。

`ResolveExecutionBackends` 先準備輸出副本，再提交。明確的 `VmBackendFactory`／`VmBackendPath` 優先於環境預設值；工廠、路徑與明確清除互斥。`FallbackVmBackendFactory`／`FallbackVmBackendPath` 是產品組裝輸入，僅在自動環境選擇沒有可用 VM 時使用，不能覆寫明確失敗或停用。`bDisableJitBackend` 阻止環境啟用預設 JIT。Session 保留產品 VM 後備；`SetFallbackVmBackendFactory` 修改該後備，執行前可用 `SetVmBackendPath("auto")` 恢復。執行開始後 VM 域仍固定，包括 JIT；Clear／reload 不解除固定。工廠、實例和保留程式碼存續期間保留模組與環境擁有者。

設定模組環境後，填寫 `Origin.ProviderId` 的處理器只接收有權存取該提供者的來源所屬域事件，也可透過可見且已匯出的 `Origin.ModulePath` 生效。套件保留的生產者所屬域、已擷取來源所屬域優先於實體路徑查詢。未填寫提供者的宿主全域處理器及無模組環境的宿主，保留既有範圍。腳本處理器仍透過 `import compiler` 或設定的腳本根明確啟用；事件篩選不授予來源、資源或原生繫結依賴。

<!-- docs-navigation -->

---

[開發文件](index.md) · [en-US](../en-US/module-environments.md) · [fr-FR](../fr-FR/module-environments.md) · [ja-JP](../ja-JP/module-environments.md) · [ko-KR](../ko-KR/module-environments.md) · [zh-CN](../zh-CN/module-environments.md) · [zh-TW](module-environments.md)
