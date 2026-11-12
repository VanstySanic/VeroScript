# Tooling 模組

`VeroScriptTooling` 是圍繞 VeroScript 語言核心提供的公開分析層，面向編輯器、
語言伺服器、靜態檢查器和偵錯器整合。

## 公開能力

| 能力 | 作用 |
| --- | --- |
| 分析器 | 分析原始碼或檔案並回傳結構化診斷。 |
| 格式化 | 產生完整原始檔替換；格式化是詞法層級，不會展開巨集。 |
| 補全與呼叫說明 | 提供上下文補全、合併的多載候選、簽名說明、繼承/介面方法實作產生、宿主中繼資料和前置處理器項目。 |
| 懸停與導覽 | 提供已解析的宣告、文件註解、定義位置和命名空間感知的範圍。 |
| 大綱與語意 token | 根據宣告提供文件符號和語意醒目提示，包括命名空間和巨集。 |
| 參照與重新命名 | 提供跨檔案參照、讀寫資訊、重新命名準備和工作區文字編輯。 |
| 工作區 | 提供開啟緩衝區覆蓋、多檔案分析、import 解析和工作區符號搜尋。 |
| LSP/DAP 傳輸 | `VeroScriptLspStdio::Run` 提供 LSP，DAP 介面提供偵錯傳輸；LSP 也支援 resolve、型別導覽、範圍、連結、token delta、格式化和格式化程式碼動作。 |

產生的方法補全項目會設定 `FCompletionItem::bOverrideMember`；
`bImplementsInterfaceMember` 僅在產生的方法用於實作純介面要求時為 true；覆寫虛擬介面預設方法時，
會歸類為繼承成員覆寫。`Documentation` 遵循目前 Tooling 語言，
`InsertText` 包含產生的方法骨架，`bInsertTextSnippet` 表示其中是否有 snippet 佔位符。
同名多載會合併為一個項目，並在 detail 中逐行列出簽名。LSP 會將產生候選作為明確文字編輯傳送；
用戶端不支援 snippet 時會收到一般方法文字。

C++ 查詢 API 傳回的原始碼位置從 1 開始。已開啟文件的覆疊內容優先於磁碟檔案，因此工作區查詢可看到尚未儲存的匯入內容。`FWorkspaceOptions` 提供匯入路徑、標準函式庫搜尋根目錄、定義、預先定義巨集、隱含匯入、專案層級存取及選用的宿主繫結中繼資料。

原生嵌入宿主可在工作區／查詢選項、`FLspStdioRunOptions` 及 `FDapStdioRunOptions`／`FDapLaunchArgs` 中提供 `AnnotationProcessing`，並使用與編譯相同的凍結處理器登錄表。LSP 工廠及相依項讀取器須能安全處理協定與分析工作執行緒的並行呼叫。重用前會檢查已記錄的相依內容及存在狀態；`Cacheable=false` 的處理器結果正常傳回，但不進入快取。

`FWorkspaceSymbolsOptions.WorkspaceRoots` 提供閉合檔案符號索引的根目錄，並且刻意
與 `ImportPaths` 分開。索引專案不會讓每個被索引的目錄自動成為隱式匯入路徑。
`MaxWorkspaceFiles` 和 `MaxWorkspaceFileBytes` 分別限制大型或不受信任工作區的掃描
檔案數量與檔案大小。長時間掃描期間會協作檢查 `ShouldCancel`；設定
`FWorkspaceSymbolsResult::bCancelled` 的結果不得發布或快取為新的索引。
工作區符號收集會移除重複的宣告識別，因此同一個宣告即使透過多個匯入被發現，也只會
回傳一次。

`VeroScriptWorkspace::ImplementationDocument` 會尋找衍生型別，以及介面或虛擬成員的
實作。`FormatRangeDocument` 會將選取範圍擴展到完整實體行，並回傳 replacement range；
它不會只重寫不完整的 token 或行片段。
接受 `FWorkspaceSymbolsOptions` 的 `ImplementationDocument` 多載會套用相同的工作區
限制與取消回呼；如果查詢被取消，會設定 `FNavigationResult::bCancelled`，不得發布
部分宣告結果。
`FormatDocument` 和 `FormatRangeDocument` 也為函式庫呼叫者提供可選的縮排寬度多載；
寬度為 0 時保留工作區預設的每層 4 個空格縮排；`bInsertSpaces=false` 時每層輸出一個 tab 字元。
無狀態格式化器也接受相同的空格／tab 樣式選項。
無狀態的 `VeroScriptFormat::FormatRangeSource` 入口也提供相同的完整實體行範圍約定，
不需要開啟工作區即可使用。

`VeroScriptWorkspace::ImplementationDocument` 也能定位另行定義或由註解產生的函式／方法本體，宣告導覽仍指向宣告。`SourceImplementation` 持有本體檔名及實體位置；檔名為空表示未繫結獨立本體。直接查詢自由函式或非虛擬方法的實作不會掃描整個工作區。

格式化器的區塊本體使用 Allman 花括號；`else`、`else if`、`catch`、`finally` 和
`do…while` 尾部的 `while` 均另起一行，並與前一區塊的右花括號對齊。
運算式初始化器保留其語法形式，字面值內容和前置處理指令不會被改寫或展開。

範圍邊界落在區塊註解、原始字串或多行前置處理指令內部時，不回傳格式化編輯。
選取完整指令或後續一般程式碼後可繼續格式化；巨集原文與續行保持不變。

語意類型由解析結果和宿主中繼資料決定，不根據識別字大小寫猜測。使用者命名空間
和保留的 `std` 樹會在補全、懸停、導覽、參照、重新命名、大綱和語意 token 中保持
一致的 namespace 符號。補全項目可以攜帶參數或泛型型別的 LSP snippet 佔位符。
LSP adapter 只有在用戶端宣告支援
`textDocument.completion.completionItem.snippetSupport` 時才傳送 snippet 格式的插入文字；否則傳送一般文字。

namespace segment 補全與 member 補全不同：在 expression/statement 限定路徑中，若替換範圍後面沒有 separator，
`InsertText` 會追加 `::`。如果後面已有 `::`，或正在補全 `using namespace` 目標，則只插入葉節名稱。

對於限定命名空間中的成員，`FCompletionItem::Label` 和 `InsertText` 會保留葉節名稱及既有
snippet，`LabelDescription` 則另外保存限定命名空間。在 LSP 中它對應標準欄位
`labelDetails.description`；只有用戶端宣告支援
`textDocument.completion.completionItem.snippetSupport` 時才傳送 snippet 格式插入文字，否則使用一般文字。
server 也可傳送供 Valence 使用的 `$valencePresentation`，其中含有
最終 label/declaration 文字和 UTF-16 語意片段；其他 LSP 用戶端可以忽略這個 extension。這些展示
欄位不會改變 filtering、sorting 或實際插入文字。

補全會先按文字比對品質、再按內容優先順序排序：完全相符、不分大小寫的完全相符、前綴、
單字邊界或 CamelCase、包含、子序列依序降低。`FCompletionItem::MatchQuality` 是主要排序鍵，
`SortPriority` 只用於同級排序；整合端應保留傳回順序，LSP 的 `sortText` 也編碼相同完整順序。
只有首項處於完全相符／不分大小寫完全相符／前綴層級且嚴格優於其餘所有項目時，LSP 才會為它
標記 `preselect`。基本型別與容器型別詞都是內建型別：基本型別拼寫在補全、懸停和 semantic
token 中仍是 `keyword`，容器型別拼寫則是 `type`／型別別名。兩者的 definition 都沒有來源位置，
rename 會被拒絕。其說明性懸停文字遵循目前 Tooling 語言；型別識別字與通訊協定欄位保持不變。

LSP 配接器也提供 `completionItem/resolve`、型別定義、實作導覽與型別階層、選取範圍、
文件連結、摺疊範圍、完整和最小 delta 語意 token、完整實體行範圍格式化與 on-type formatting，
以及 `source.formatDocument`、安全移除完全重複 import 的 `source.organizeImports`，和產生方法實作的
`refactor.rewrite`。
取消請求和監視檔案通知也會觸發工作區更新。
`VeroScriptLspStdio::Run` 接受 `FLspStdioRunOptions`；當用戶端沒有提供
`workspaceFolders` 或 `rootUri` 時，可用 `DefaultWorkspaceRoots` 指定閉合檔案索引根目錄。
用戶端提供的根目錄會取代這個預設值。

協定、請求、取消和設定詳見[語言伺服器](lsp.md)，偵錯傳輸詳見[除錯](debugging.md)。

## DAP 偵錯配接器

透過 CLI 啟動標準輸入／輸出或 TCP 偵錯配接器：

```powershell
veroscript --debug hello.vs
veroscript --debug --dap-port=4711 hello.vs
```

`--dap-port` 需要 `--debug`，連接埠範圍為 1–65535。TCP 預設繫結 `127.0.0.1`；`--dap-bind=<位址>` 可更改繫結位址，且必須使用 TCP 模式。程式與引數由 DAP `launch` 或 `attach` 提供。偵錯使用 VM，其效能不代表 mixed 或 JIT 執行。

原始碼行中斷點透過 `setBreakpoints` 設定；不支援例外中斷點和函式中斷點。空的
`setExceptionBreakpoints` request 僅作為 standard client 啟動 handshake 的 clear operation 成功；
non-empty filter 和所有 `setFunctionBreakpoints` request 都會失敗。用戶端不應將這些
breakpoint category 顯示為 enabled。

在 `launch` 或 `attach` 前設定的原始碼中斷點處於 pending/unverified 狀態。code 載入後，adapter
會發出帶相同 DAP ID 和 `reason: "changed"` 的 standard `breakpoint` event。只有屬於唯一
compiled source、preprocessing 後仍 active 且擁有 executable location 的 line 才會 verified；絕不會
fallback 到同名檔案。用戶端必須使用回傳的 physical line：function 內的中斷點可能前移至下一個
executable line。完整 binding rule 請參見[除錯](debugging.md)。

處理程序內整合使用 `VeroScriptDapAdapter`，生命週期為
`Initialize` → `Launch` 或 `Attach` → `SetBreakpoints` → `ConfigurationDone`。
處理程序內的 `ConfigurationDone` 是同步呼叫；線協定宿主必須在執行工作執行緒中呼叫，
讓協定執行緒能繼續處理 `pause`、`terminate` 和暫停狀態請求。`Attach` 與 `Launch` 使用
相同的載入器，並不是附加到已經執行中的 VM。`Pause` 和 `Terminate` 是執行緒安全的協作式
控制，`IsStopped()` 和 `IsRunning()` 可讀取目前執行狀態。

`VeroScriptDapStdio::Run` 接受 `FDapStdioRunOptions`，可提供 `DefaultProgram`、
`DefaultScriptArgs`、`DefaultStdlibSearchPaths`、`DefaultEnvironment`、
`DefaultWorkingDirectory` 和 `DefaultInput` 等預設值。嵌入式宿主可以設定 `LiveBindings`
以執行已註冊的宿主函式；非擁有式繫結目錄必須在整個 DAP 執行期間保持有效。沒有即時繫結
時，`bindingCatalogPaths` 會為分析和啟動提供中繼資料/樁繫結。

`VeroScriptDapTcp::Run` 接受 `FDapTcpRunOptions`。`MaxSessions=0` 會持續接受用戶端直到
宿主停止，`Port=0` 會選擇暫時連接埠並透過 `OnListening` 回報，`BindAddress` 通常應保持為
`127.0.0.1`。嵌入式宿主自行回報就緒狀態時，可以將 `bAnnounceListen` 設為 `false`，
關閉預設的錯誤串流監聽通知。stdio 宿主在工作階段正常結束時回傳 `0`，協定或 I/O 失敗時
回傳 `1`，用戶端要求終止宿主時回傳 `2`。

### 與協定無關的偵錯工作階段

`VeroScriptDebugSession` 是 DAP 配接器下方可重複使用的 C++ 層。如果宿主或自訂配接器需要
管理載入、中斷點、同步暫停回呼、狀態檢查和執行結果，但不需要自行實作 DAP 線協定，請使用
它。`LoadSource` 和 `LoadFile` 接受與工作區層相同的模組路徑、定義、預先定義巨集、來源
覆蓋、可選繫結目錄、隱式匯入路徑、匯入策略和標準函式庫根目錄。

`BindBreakpoint` 是 adapter 使用的 strict 原始碼中斷點 path：它將 one-based physical line
解析至唯一 compiled source。繫結成功時，`FBreakpointBinding` 回傳 nonzero `Id` 和 actual physical
`Line`；無法繫結時回傳 localized `Message`。function 內的 line 可能前移到下一個 executable line，
因此 caller 必須使用回傳 line，不能假定 requested line 就是 actual bound line。

在 `Run` 之前，可以設定腳本引數、環境、工作目錄、輸入、最大呼叫深度、要求的執行後端或
JIT 策略。`FDebugRunResult` 包含 `bSuccess`、結構化 `Diagnostics`、分開的 `Output` 和
`ErrorOutput`，以及 `ExitCode`。偵錯器仍會將有效執行後端強制設為 `Vm`；要求的後端和 JIT
策略會保留為設定，JIT 統計主要記錄由此產生的快取失效。

`SetStopHandler` 會在執行暫停期間同步呼叫。處理器可以使用 `GetStackTrace`、`GetLocals`、
`GetChildren`、`Evaluate`、`SetLocal` 和 `SetChild` 檢查或修改狀態，然後透過 `SetRunMode`
選擇 `Continue`、`StepIn`、`StepOver` 或 `StepOut`。面向非同步宿主的 `RequestPause()` 和
`RequestCancel()` 是執行緒安全的協作式控制。一般工作階段設定和 `Run` 呼叫必須在控制執行緒
執行；在要求的操作返回前保持工作階段有效。

## 公開邊界與在地化

請使用 SDK 發佈的公開標頭；實作專用檔案不屬於受支援的整合介面。Tooling 與 CLI
和執行階段共用在地化目錄。診斷 code 和參數在各語言之間保持穩定，改變的只是
顯示文字。參見[診斷在地化](../../../../../VeroScript/Resources/Docs/User/zh-TW/localization.md)。

## 目前語法感知 editor 覆蓋範圍

completion、outline、hover、signature help 與 semantic token 已識別相同 type 的 declaration group、
`do`/`while`、帶有 expression list 的 classic `for` clause，以及 signed 8-bit `char` literal。initializer
會在自身 declaration 進入 scope 前解析；shadowing name 或 nested lambda local 不會洩漏到後續 declaration
initializer 或 `for` step expression。跨越多條 physical line 的 raw string 在每一條 physical line 都有一個
string semantic token，而且該 literal 中看似 comment marker 的 text 絕不會被誤判為 comment。

## 工作區分析觀察者

VeroScriptWorkspace::AnalyzeAll(OnDocument) 對每份文件同步呼叫觀察者一次，路徑與分析結果皆為回呼期間的借用資料。回呼後仍須使用的資料應先複製。走訪期間，觀察者不得開啟、關閉或修改同一工作區的文件。

## 嵌入分析與偵錯的 VM 選擇

直接嵌入 Tooling 的主機須明確選擇直譯器。在執行前呼叫 `VeroScriptDebugSession::SetVmBackendFactory`／`SetVmBackendPath`，或設定 `FDebugLoadOptions`／`FDapStdioRunOptions` 的 `VmBackendFactory`／`VmBackendPath`；工廠與路徑互斥。提交執行後選擇凍結，清除／重新載入保留該域，不能藉此更換 VM。設定失敗保留原選擇。相符的 Runtime 啟動程式會提供產品後端，僅連結 Tooling 不會自動組裝。

會執行編譯擴充指令碼的查詢使用分析／工作區選項中的 `ScriptCompileExtensions`，指定 `VmBackendFactory` 或 `VmBackendPath`。偵錯載入在未另行指定編譯擴充 VM 時繼承所選直譯器。不執行擴充的語法分析及一般分析不需 VM；操作期間須保留後端與其資源擁有者。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
