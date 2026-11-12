# 通用編譯處理與原始碼中繼資料

核心 0.1.1 透過 VeroScriptCompileProcessing.h 提供原生編譯處理器。可轉換原始碼、生成型別與成員、替換宣告或正文，並附加註解和註釋。生成結果經過一般可見性、型別和泛型檢查；業務規則由擴充定義。

## 註冊與生命週期

在 FCompileProcessorRegistry 註冊 FCompileProcessorDesc，呼叫 Freeze()，再設定 FCompileOptions::CompileProcessing.Registry。描述包括身分、版本、設定鍵、順序、階段、訂閱、允許操作及工廠。每個編譯任務建立獨立實例，於任務階段間沿用。回呼中的上下文與 AST 檢視僅在呼叫期間有效；原生模組和設定的必要擁有者由工廠保留。

同一事件符合多個訂閱仍只呼叫一次，處理器依 Order、ProcessorId 排序；順序不會使衝突操作合法。未知註記仍為一般中繼資料。FAnnotationProcessorRegistry 使用相同交易管線：Members 新增方法，使用者實作繫結後於 Bodies 補全預設正文。

## 固定階段

| 階段 | 輸入與操作 |
| --- | --- |
| RawSource | 原始文字；EditSource 可轉換自訂語法及匯入 |
| ActiveSource | 前處理後文字；保留匯入序列及前處理邊界 |
| Types | 宣告輪廓；新增或替換型別、介面、列舉與別名 |
| Members | 已登記型別；新增或替換成員、函式與全域變數 |
| Bodies | 已繫結實作；CompleteBody、ReplaceBody、SetMetadata |

Types 後繫結別名和繼承；Members 後繫結跨檔案實作並解析延後正文。Bodies 不改變布局或簽章。生成宣告不遞迴觸發處理器，後續階段可查詢它們。

AddDeclaration 使用帶模組、namespace、可選 OwnerId 和目前 Snapshot 的 FCompileScope，不攜帶 TargetId。替換保留宣告種類、名稱和泛型參數數量。CompleteBody 只補缺少正文的宣告；純虛方法與 deleted 不接受正文，既有純虛解構函式規則除外。過期目標、同目標修改及不相容父子操作在套用前拒絕。

FCompileMetadata 區分未填寫與明確空集合。替換保留未受影響的說明，移除舊子節點資料。EditSource 使用 UTF-8 位元組範圍及來源對應。ActiveSource 不可引入前處理指令或改變已解析匯入序列。

## 查詢、快取與套件

原始碼中繼資料編碼在同一快照內共用重複路徑與識別字字串，完整保留原值、文件與節點識別、位置及來源對應，編碼總量上限仍為 64 MiB，展開後的字串內容另限 512 MiB。解碼器也接受先前直接儲存字串的中繼資料格式。準備結果指紋、位元組碼與套件共用此編碼；核心實作識別改變後，須重建相符的核心與原生後端。

VeroScriptSourceMetadata.h 擁有不可變的文件、宣告、註釋和註解引數樹，保存旗標、值種類、巢狀群組、重複、原文及位置。RegisterAnnotation 可以只註冊說明。反射資訊物件可查詢中繼資料，不因此取得額外執行權限。

CommentRetention 選擇 Full、Documentation 或 NoComments；NoComments 仍保留註解和獨立註冊說明。Unavailable 表示缺少原始碼資訊。熱重載後舊不可變快照仍可讀取。

PreparedProgram 保存宣告、操作、來源及依賴指紋。以 MatchesHost、MatchesProcessing、MatchesDependencies 複核，宿主快取鍵還須包含原始碼、覆寫層、巨集、匯入和一般編譯選項。不可快取的處理器結果不跨任務重用。單獨 AST 無法還原原始碼轉換或複製處理器狀態。

開放泛型套件保存已轉換模板、操作及生產者中繼資料。消費者重放 Types、Members、Bodies，不載入生產者處理器。封閉程式使用套件位元碼；泛型仍依一般規則代入。0.1.1 契約變動時重新生成舊開發產物。

取消是協作式。長運算使用 CheckCancelled，ReadDependency 記錄存在與缺少的資源。事件和生成量均有上限；阻塞原生回呼需要宿主配合。Interop、工具與 AOT 使用相同準備結果，JIT 執行生成後的業務程式。

範例：[原生處理器](../../../Examples/CompileProcessing/main.cpp)、[腳本擴充](script-compile-extensions.md)。

## 註記名稱比對

預設區分大小寫；設定 `annotation_definition.case_sensitive = false` 可讓 `@GuiWidget`、`@guiWidget` 和 `@GUIWIDGET` 使用同一定義。處理器、`has_annotation`、`find_annotations` 與 `annotation_info.matches_name(name)` 共用此規則。`name()` 保留原始拼寫，`definition().name()` 回傳註冊名，`definition().case_sensitive()` 回傳規則。參數和中繼資料鍵仍區分大小寫。`find_annotations` 和 `matches_name` 保留 `@` 前綴要求，`has_annotation` 可省略 `@`。未註冊的註記區分大小寫。

`FAnnotationDefinition::CaseSensitive`, `FHostBindingRegistry::RegisterAnnotation(..., false)`, JSON `caseSensitive`, C Interop `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE` (`name_flags`), C# `CaseSensitive`, Python `case_sensitive`. ASCII; 不依賴地區設定。拒絕活動名稱衝突；快取、位元碼、程式庫及泛型保留同一策略。

## 其他模組的整合

`FCompileResult::PreparedProgram` 是共用準備結果。C++ Session 提供 `SetCompileProcessing`，原始碼產生宿主傳入 `FCodegenOptions::CompileProcessing`，Tooling 分析與通訊協定選項攜帶相同設定。原生 Interop 宿主可透過 `VeroScriptInteropCompileProcessing.h` 的 `CreateCompileProcessingService` 建立不透明處理服務。這些整合點已經存在。

登錄與原生程式碼擁有者由宿主明確提供。一般 CLI 不自動探索任意原生處理器工廠；指令碼擴充與明確選擇的原生模組沿用各自設定的載入入口。使用已準備程式時不得重跑處理器，也不得透過重新解析註解建立另一套宣告。JIT 執行處理後的業務程式碼。平台與應用程式驗收仍須分別完成。


## 作用域宣告與區塊生成

宣告檢視提供 `ScopeId`、`ParentScopeId`、`bLocal`、`bNested` 與語意所屬關係。`FCompileProcessingContext::Blocks` 是回呼期間有效的唯讀借用檢視，包含區塊與陳述式邊界錨點。Types 階段以 `FCompileScope::ScopeId`、`InsertionBeforeId` 新增區域型別或別名；空區塊或附加使用結束錨點，模組及 owner 欄位維持原意。

腳本處理器使用 `context.blocks()`、`context.block_scope(scope_id, before_id)`。區塊及錨點以 getter 方法查詢，例如 `scope_id()`、`statements()`、`id()`、`range()`；來源範圍維持一般值欄位。不得自行建構 scope 或任意偏移以繞過快照驗證。

同錨點操作採確定順序，宣告從插入位置可見。替換父主體／宣告與操作舊錨點構成衝突；補全或替換主體會重建區域目錄。生成後代接受一般檢查，不再次執行處理器；泛型具現化只代入準備結果。作用域識別、生成記錄、子宣告目錄及所屬資訊隨 VSBC／編譯程式庫儲存。0.1.1 版面識別改變後須重建開發產物。

## 宣告附加批次與所屬型別參照

`Private/Processing/VeroScriptDeclarationProcessing.cpp` 的 Members 階段 `DeclarationAppendsOnly` 路徑要求所有操作都是 `AddDeclaration`，且 `TargetId`、`ScopeId` 為空。無 `OwnerId` 的命名空間輔助函式可與已在驗證快照中的 class／struct／interface 成員交錯附加。階段、快照、可寫模組和衝突檢查仍然有效；新產生的所屬型別不會成為舊快照中的可操作目標。

`ReuseOwners` 僅在 `P.Classes`、`P.Structs`、`P.Interfaces` 的儲存位址未改變時借用該宣告快照。產生的輔助函式本體仍可能附加區域型別並擴充這些容器；儲存變化後透過 `Collect(P)` 重新收集參照，不跨容器擴充保留宣告／AST 指標。維護時檢查交錯輔助函式與成員、泛型所屬型別、虛擬／介面呼叫及區域宣告；`Private/Tests/VeroScriptCompileProcessingTests.cpp` 的 `FInterleavedAppendGenerator` 提供原始碼情境，其存在不代表測試已經執行通過。

<!-- docs-navigation -->

---

[開發者文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

Language: [en-US](../en-US/compile-processing.md) · [fr-FR](../fr-FR/compile-processing.md) · [ja-JP](../ja-JP/compile-processing.md) · [ko-KR](../ko-KR/compile-processing.md) · [zh-CN](../zh-CN/compile-processing.md) · [zh-TW](compile-processing.md)
