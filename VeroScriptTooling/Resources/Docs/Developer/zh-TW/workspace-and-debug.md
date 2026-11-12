# 工作區、LSP 與 DAP

## 工作區狀態

`VeroScriptWorkspace` 是 project/module context 的 stateful boundary，擁有 open-document overlay、source/import resolution、cross-file analysis、workspace symbol、preprocessor context、binding-catalog input。open overlay 優先於 disk file；close 結束 overlay lifetime。option 包含 import path、standard-library path、implicit import、define、predefined macro、circular-import policy、project-wide access、ordered binding-catalog path。catalog 按序 merge，duplicate reject，描述 host function/type/member/annotation/template/documentation，但不 register runtime behavior。

`TryGetDocument` 回傳單一開啟緩衝區的獨立副本；路徑不存在時回傳 `false`，且不修改輸出。
需要全部緩衝區時使用 `SnapshotDocuments`；LSP 讀取單一文件時不會複製無關原始碼。

## 文件生命週期與失效

LSP 在 `textDocument/didOpen` create overlay、在 `textDocument/didChange` 應用 incremental change、在 `textDocument/didSave` 用 optional `text` refresh、在 `textDocument/didClose` remove。open、accepted change、save、workspace configuration change 後 publish diagnostic。version 按 URI track，version 小於等於 last accepted 的 change ignore；reopen 開始新 overlay lifetime。server 可在 request run 時繼續讀 input，按 wire order bounded work；`$/cancelRequest` 取消 pending request，返回 JSON-RPC `-32800`。

## 語言功能流程

direct service 是 `VeroScriptAnalyzer`（workspace diagnostic/包括 import）、`VeroScriptComplete`、`VeroScriptCallHelp`、`VeroScriptHover`、`VeroScriptNavigation`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptOutline`、`VeroScriptWorkspaceSymbols`、`VeroScriptRename`、`VeroScriptFormat`。LSP 透過 completion/resolve、hover、navigation/type hierarchy、document symbol、selection/folding range、document link、full/delta semantic token、signature/reference/highlight、rename、document/range/on-type formatting、`source.formatDocument`、`source.organizeImports`、`refactor.rewrite` code action、workspace symbol/resolve 暴露它們。只有能安全移除完全重複的 import 時才返回 `source.organizeImports`；類別或結構體本文的空游標位置可透過 `refactor.rewrite` 產生尚未實作的繼承方法骨架。range formatting expand selection 到 complete physical line，並返回 actual replacement range，不要求整個 document。

namespace 由 semantic resolve 而不是 identifier casing；含 nested user namespace 與 `std::` tree。host catalog entry/soft builtin 可參與 result，host-only location 使用 virtual `veroscript:host-bindings` URI。

completion 以 `FCompletionItem::MatchQuality` 作為首要文字排序，`SortPriority` 只用來打破相同
文字品質的同分。adapter 必須保留 result order；LSP `sortText` 表示同一順序，只有 exact、
case-insensitive-exact 或 prefix match 明確優於其他項目時才設定 `preselect`。primitive type word
在 completion、hover 和 semantic token 中仍為 `Keyword`；container type word 則為
`TypeAlias`／`type`。兩者都是帶 localized display detail 的 built-in type，沒有 source definition，
也絕不是 rename target。顯示文字應使用 `tooling.builtin_type.detail` 與
`tooling.builtin_type.documentation` catalog key；protocol field 與 type identifier 不得翻譯。

對於限定 namespace member，`FCompletionItem::Label` 和 `InsertText` 保留葉節名稱及 tooling 產生的
snippet，`LabelDescription` 保存限定 namespace。序列化為 LSP 時，只有用戶端宣告支援
`textDocument.completion.completionItem.snippetSupport` 才傳送 snippet；否則以一般文字傳送葉節名稱。
將 namespace 序列化為標準 LSP `labelDetails.description`。選用的 `$valencePresentation` 還會提供供 Valence 使用的最終
label/declaration 文字和 UTF-16 語意片段，其他 LSP 用戶端可忽略它。它不得改變 filtering、
sorting 或實際插入內容。

LSP semantic-token adapter 會 merge resolved semantic span 與 lexer baseline。後者在每個 valid
physical line 上涵蓋 language/type word、directive、macro、literal/comment，在 parse recovery
期間仍可使用，並在 encode LSP token stream 前移除 overlap。

## 診斷與在地化

analysis 按 source file group diagnostic，imported file 因而得到自身 `textDocument/publishDiagnostics` notification。analyzed document 即便 clean 也 publish；上次有 diagnostic、本次沒有的 file 明確用 empty list clear。 `initialize.locale` 是 BCP 47 preference，unsupported 保留 process environment catalog，missing file/key fallback `en-US`。stable identifier/message argument、protocol field、URI、path、type name、source text 永不 translate。

## 偵錯

`VeroScriptDebugSession` 將 core load/execution/stop callback/stack frame/scope/variable/evaluation 對映為 tooling data；`VeroScriptDapAdapter` 對映 DAP message，`VeroScriptDapStdio` 與 `VeroScriptDapTcp` 是獨立 transport，不得重實現 VM evaluation/breakpoint/object lifetime。 `VeroScriptDebugSession::SetOutputHandler` 在 `Run()` active 時接收 flush text/error flag；`FDebugRunResult` 在 `Output`、`ErrorOutput` 儲存 complete stream，DAP 在 terminal 前 emit `stdout`/`stderr`。user-facing LSP configuration 參見[語言伺服器參考](../../User/zh-TW/lsp.md)。

## 偵錯工作階段契約

`VeroScriptDebugSession` protocol agnostic，可直接由 IDE/test/adapter host。 `LoadSource`/`LoadFile` 返回 `FDebugLoadResult`，按 overlay/define/import policy/optional non-owned binding catalog compile；`Clear` invalidate module/debugger state；`SetBreakpoint` 返回 session-local ID，接收 file label、one-based line、optional condition。

`BindBreakpoint` 是 adapter 的 strict path：它透過唯一 compiled source 的 preprocessor map 解析 one-based physical source line，然後只以 `SetExactBreakpoint` 註冊解析出的 exact preprocessed location。inactive、missing、ambiguous、out-of-range request，或沒有 eligible location 的 non-executable request，都不會註冊，並附 localized display text。function 內的 request 可以前移至下一個 executable physical line；module initialization 則必須使用 exact line。

`SetRunMode` 選擇 continue/step-in/step-over/step-out，`SetExecutionBackend`、`SetMaxCallDepth`、`SetJitPolicy` 在 VM run 前 apply；`GetLastJitStats` 返回 latest snapshot。debug attach force effective `Vm`，invalidate JIT cache，所以 behavior 不依賴 native compiled entry。stop handler 收 `EDebugStopReason`，output handler 收 text/error flag；stopped 時 `GetStackTrace`、`GetLocals`、`GetChildren`、`Evaluate` valid，`SetLocal`、`SetChild` update paused value 並返回 structured evaluation error。 `FDebugRunResult` 保留 diagnostic/output/error/exit code，bSuccess 與 exit code 單獨解釋。

## DAP 配接器生命週期

`VeroScriptDapAdapter` 是 in-process request/event mapper，不是 wire server。host 順序呼叫 `Initialize`、`Launch` 或 `Attach`、安裝 breakpoint、`ConfigurationDone`；`ConfigurationDone` block through `Run`，並依情況 emit `Stopped`、output、terminal event。main thread ID 是 adapter stable single-thread ID。

continue、`Next`、`StepIn`、`StepOut`、`StackTrace`、`Scopes`、`Variables`、`Evaluate`、`SetVariable` 必須從 stopped-event callback 呼叫。 `Attach` 用同樣 source/file load semantics，不 attach 到已執行 VM；`Disconnect` resume active run，run return 後 cleanup。in-process 可經 launch arg 傳 live `IHostBindingCatalog`，out-of-process 用 serialized catalog，不能 retain pointer。DAP name/path/expression/coordinate/event field 都是 machine data。

`setBreakpoints` 即使 source replaces entries 也保留 stable DAP ID。load 前 entries 是 pending/unverified；load 後透過 `BindBreakpoint` rebind，並為每個 entry emit `Breakpoint` event。wire adapter 將它 serialize 成 standard DAP `breakpoint` event，`reason: "changed"`。return 的 DAP line 是 actual bound physical line，未必是 requested line。

## 偵錯輸出與執行完成語意

`VeroScriptDebugSession::Run` 區分 observable output/process completion。FOutputHandler 將 stdout line-buffered；complete line、`std::console::flush` 或 bounded pending buffer flush fragment，stderr 立即 forward。 `FDebugRunResult::Output`/`FDebugRunResult::ErrorOutput` 即使無 handler 也保留 complete stream，handler exception 與 debugged program isolate。bSuccess 是 execution success；successful int main 可以有 non-zero ExitCode，failed execution 返回 diagnostic/failure exit code。debug attach force VM/invalidate JIT cache，stored `SetExecutionBackend` preference 不覆蓋該 invariant。source load 記錄 preprocessor line map，breakpoint/stack remap physical source，conditional breakpoint/evaluation 使用 active define/predefined macro。

## DAP 傳輸與啟動契約

`VeroScriptDapStdio::Run` 用 caller input/output stream 的 DAP Content-Length framing。 `FDapStdioRunOptions` 提供 live binding/catalog、script arg、stdlib root、environment、working directory、initial input default。live binding 在 `Run` duration borrowed 且優先 catalog path；catalog 支援 compile/analyze，native call 要 live registry。

Launch/Attach 接收 `program` 或 inline `source`、`fileLabel`、`workspace`、`cwd`/`workingDirectory`、`stopOnEntry`、`args`、`input`、`env`/`environment`、`stdlibSearchPaths`/`stdlibPath`、import/implicit-import path、define/predefined macro、catalog path、policy、`backend`、`maxCallDepth`。 `args` 原樣給 `main(string[] args)`，沒有 script path；`input` 是 initial script stdin，DAP stdin 不 forward；environment scoped to run 後 restore。execution 前 reject malformed JSON、unsupported backend、invalid call-depth、oversized collection：4096 argument、2048 import/implicit-import/stdlib path、1024 environment。 `Initialize` 返回 protocol metadata/capability，method/machine field exact。

主控端可透過 `FDapTcpRunOptions::Control` 傳入共用的 `FDapTcpRunControl`，呼叫 `RequestStop()` 喚醒通訊端等待，並依網域的中斷連線約定恢復暫停的呼叫。釋放網域或卸載模組前請等待 `Run` 執行緒結束。停止後重新開啟端點時請建立新的控制物件。

`VeroScriptDapTcp::Run` 在 IPv4 順序接受 client，bind 預設 `127.0.0.1`，empty/`0.0.0.0` 為 all interface；`Port == 0` 選擇 ephemeral port 並 call `OnListening`；`MaxSessions == 1` 一 client，`0` unlimited，negative 當一。clean non-terminating attach disconnect 只 close client，terminate request stop server loop；host termination 對映 success，其他 failure non-zero。

## DAP 地區設定與附加終止語意

DAP stdio 將 initialize.arguments.locale 作為 BCP 47 preference，unsupported 不改變 environment catalog；selection 在 initialize response 前 apply，覆蓋後續 localized diagnostic。Launch/Attach 有相同 source-loading option；Attach 是 protocol request shape，不是 mid-run VM attach。

successful Attach 後 Disconnect 預設 terminateDebuggee false，Launch session 預設 true；host 可顯式傳送該值。false release current debug session，不請求 attached host terminate；true request host termination。TCP host 繼續服務 client 時該區別十分重要，仍遵循 MaxSessions policy。

## 偵錯既有 Session

宿主提供並保持 `IVeroScriptLiveDebugDomain` 存活；`Info()` 傳回同步快照。協定執行緒要求將 VM 操作排到執行安全點；一條連線只暴露一個活動內容（thread 1），須序列化被偵錯呼叫。`Connect()` 不發回呼、不啟動或重載程式碼；失敗不保留回呼。`Activate()` 完成組態。`SetBreakpoints` 不可分地替換單一來源檔案的中斷點，按要求順序傳回對應結果並保留所給 ID，不為同次更新重複發事件。`Disconnect()` 移除工作階段中斷點、排空回呼，按宿主策略釋放暫停呼叫；取消仍是獨立宿主操作。

全部橋回呼和暫停 VM 的檢查、求值、修改及恢復都屬於 Session 控制執行緒。暫停回呼須在該執行緒處理要求。只有 `RequestPause()` 和共用 `FExecutionControl::RequestCancel()` 可跨執行緒直接控制。借用的程式、繫結、原始碼 overlay 和入口名檢視在回呼傳回時失效；複製所需中繼資料。overlay 是目前組態，不是凍結的原始碼快照；原始碼及繫結修訂由宿主另行公開。

請參閱[偵錯既有 Session](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/session-debugging.md#debug-disconnect-lifetime)。

## 實作歸屬

請參閱[C++ 嵌入與會話 — 實作歸屬](../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-TW/session.md#session-debug-implementation)。

## 記憶體檢查

偵錯變數透過 `FMemoryInspection` 傳遞記憶體資訊。允許的儲存位址序列化成十進位字串 `memoryReference`，`veroMemory` 中可用的 `objectId`、`objectAddress`、`dataAddress`、`byteLength` 也使用十進位字串，保留 uint64 精確度，缺失值省略。中繼資料子項唯讀，不建立可寫目標或虛構儲存位址。`variablesReference` 仍是不透明 DAP 控制代碼，與物件身分無關，不宣告任意記憶體讀寫能力。

## 通用編譯處理與原始碼中繼資料

原生主控端透過查詢選項、`FWorkspaceOptions`、`FLspStdioRunOptions`、`FDapStdioRunOptions`、`FDapLaunchArgs` 的 `CompileProcessing` 傳入 `FCompileProcessingOptions`。一般處理器在模組載入前安裝，與 `AnnotationProcessing` 共用核心準備流程。分析、完成、導覽、索引、偵錯須使用相同組態。登錄必須凍結，各編譯工作建立獨立實例。原生回呼須於服務啟動時提供，不能透過 LSP/DAP JSON 傳送。

一般相依資源讀取器優先於舊註記讀取器。快取重新檢查處理器實際讀取的相依項，不重用不可快取的結果。組態變更後呼叫 `SetOptions`。核心來源區間將 `ActiveSource` 編輯映回原始座標，供導覽、語意權杖、診斷與偵錯共用。產生的宣告具有獨立唯讀來源；暫留與完成使用核心宣告說明並遵守註解保留策略，中繼資料反射簽章也須符合核心。

## 指令碼編譯擴充

同一批選項的 `ScriptCompileExtensions` 提供擴充根目錄、組態及執行策略。`import compiler "extension.vs";` 遵守核心前置宣告規則；路徑完成、導覽與文件連結可解析它，不執行其處理器。

編輯擴充實作時，透過 `Bindings` 提供 `GetScriptCompileServiceCatalog()`，或匯出後放入 `bindingCatalogPaths`。它提供編譯專用的 `std::compiler` 型別與簽章，不向一般程式碼授予執行服務。

語意快照驗證回呼讀取資源的存在與內容，包括原本不存在的資源。覆疊版本及擴充程式碼變更也會使結果失效。自訂讀取器下須重新分析，因已結束工作中的整合程式碼/資源讀取器不能保留為可重用回呼。full/delta 仍比較已公布權杖；組態變更後須 `SetOptions`。

## 協定編輯與原始碼座標

範圍及輸入時格式化保留多行註解與原始字串片段。若需調整周邊程式碼，應格式化完整常值或文件。

LSP 在支援動態監看時也監看繫結目錄；事件即使時間及大小相同也強制重載，無事件時則於查詢前檢查兩項屬性。無效目錄保留最後成功快照並回報狀態；組態變更替換監看。LSP/DAP 依模組識別規則正規化及去除重複路徑，Windows 大小寫別名視為同一檔案。DAP 在替換前驗證目錄與啟動組態；驗證拒絕保留程式及原生函式索引。

偵錯重載取消或發生例外時，先清除舊位元組碼、繫結、來源快照及中斷點再傳播例外，後續仍可成功載入。DAP 傳回失敗回應並保持連線。

DAP 僅公開執行緒 1。`stackTrace` 遵守 `startFrame` / `levels`，0 表示所有剩餘框架，`totalFrames` 為總數；分頁保留停止期間的框架識別。`FDebugStackFrame.Column` 為實體 UTF-8 位元組欄，`FDapStackFrame.Column` 為從 1 起算的 UTF-16 欄，再由傳輸層套用用戶端 0/1 起算偏好。匯入及產生來源在中繼資料裁剪前保存修正，後續磁碟編輯不改變座標。即時網域須提供 UTF-16 DAP 欄。

## 已解析模組環境

分析、查詢、工作區及偵錯的 `ModuleEnvironment` 接受核心編譯所用的不可變 `FModuleEnvironment`，`FLspStdioRunOptions` / `FDapStdioRunOptions` 也公開它。提供完整環境並保持明確即時繫結相容；衝突產生診斷。環境更新使索引失效，未編輯文件也會重新檢查處理器相依項。

共用解析器提供匯入、覆疊、來源所有權、相依可見性、處理組態與資源。主控端候選遵守繫結來源和公開簽章可見型別；剖析無法恢復範圍時，最後完成備援不列舉整個主控端目錄。標準目錄包含 `std::i18n`，僅有 getter 的原生屬性保留 `property` / `readonly`。

LSP 宣告 `capabilities.experimental.veroscriptModuleEnvironment`，帶編譯/執行指紋與 `moduleSourceMethod: "veroscript/moduleSource"`。`{ "uri": "veroscript-module:/..." }` 要求傳回 `uri`、`languageId`、`text`、`readOnly` 及編譯指紋，不存在或無權存取時為 `null`。回應須在訊框上限內；用戶端須提供此 URI 配置的內容提供者，一般檔案導覽仍用檔案 URI。合法再匯出只使目標可讀，不公開所有間接模組。這些文件唯讀，重新命名也不修改它們。

偵錯工作階段保留環境並驗證外部相依項。`TryGetSource` 與 DAP `sourceReference` 提供已編譯主緩衝區、記憶體模組及產生來源的凍結文字；即使帶實體檔案標籤，也不被過期或後續磁碟內容替換。reload/clear 使舊參考失效，即時網域繼續用來源回呼。同一路徑識別採最後覆疊優先，編輯器緩衝區在所有查詢中取代環境來源快照。

成員存取保留限定型別識別；不同命名空間的同末端名稱不授予 private/protected/friend 存取。短名稱須指向同一個不含糊宣告；全域宣告及 `::Same` 保持優先。

匯入整理只刪除同一連續區塊中，經目前前處理與緩衝區確認的完整重複全域匯入。巨集改寫行、停用分支、不確定相依項、常值及額外陳述式均保留。指示詞、註解、其他宣告及範圍邊界結束區塊；實體續行或詞法錯誤時拒絕編輯，並保留原換行。

## 附加主控端提供的即時網域

前述載入型 `Attach` 適用 `program` 或省略 `attachMode`。持續網域模式須將 `IVeroScriptLiveDebugDomain` 傳入 `VeroScriptDapStdio::Run` / `VeroScriptDapTcp::Run` 對應多載，再送出 `attachMode: "domain"` 與 `domainId`。請先確認 `initialize.body.veroscriptSupportsDomainAttach == true`，缺少或為 false 時不能退回 launch/program。欄位無效會失敗而不載入。attach 回應提供該網域的識別、版本與能力；`configurationDone` 只啟用中斷點，不啟動程式，連線保持至後續主控端呼叫。須維持網域存活，並將偵錯內容序列化為執行緒 1。僅當回應宣告 `veroscriptCanTerminate: true` 才接受終止，否則要求失敗且連線保留。

斷線前，配接器等待已派送 pause/cancel 呼叫傳回；關閉中的新控制失敗，舊控制永不作用於下次連線。派送不得等待斷線，也不得等待排至同一排空執行緒的工作。主控端斷線與回呼排空在控制 mutex 外執行；主控端仍須序列化 attach、繼續、斷線與停止狀態存取。

產生方法完成提供 `FCompletionItem::bOverrideMember`、`bImplementsInterfaceMember`、`Documentation`、`InsertText`、`bInsertTextSnippet`。純介面實作與繼承 override 分開分類，包括帶預設主體的虛擬方法；說明使用目前地區，同名多載分組。LSP 使用明確文字編輯，不支援 snippet 的用戶端收到一般方法文字。

## 協定的 JIT 中繼資料

`Initialize` 回傳協定中繼資料與能力旗標。`jitCapability` 表示 Core 的 JIT 服務契約（`backend-services`），並非所選後端或其原生程式碼能力；宿主透過 `IJitBackendFactory::GetDescription()` 取得後者。宿主可直接提供工廠，或以函式庫／目錄的明確 UTF-8 路徑呼叫 `SetJitBackendPath`。載入器檢查 ABI，並在工廠或產物仍被持有期間保留函式庫；不透過 `PATH` 或隱含環境設定探索。Tooling 本身不載入 JIT 模組，DAP 的 `backend` 只儲存執行策略；偵錯器附加期間使用 VM。

## 配接器的 VM 設定

須將 `FDebugLoadOptions::VmBackendFactory`／`VmBackendPath` 傳遞至載入流程。`FDapLaunchArgs` 與 stdio／TCP 選項承接宿主提供的選擇；DAP 要求本身不能傳遞原生工廠。工廠與路徑互斥。偵錯透過 VM 執行，`backend` 欄位不會載入 VM。提交執行後選擇凍結，並須與保留的網域相容。編譯擴充可在 `ScriptCompileExtensions` 明確指定另一組選擇，否則繼承偵錯載入的 VM 選擇。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/workspace-and-debug.md) · [fr-FR](../fr-FR/workspace-and-debug.md) · [ja-JP](../ja-JP/workspace-and-debug.md) · [ko-KR](../ko-KR/workspace-and-debug.md) · [zh-CN](../zh-CN/workspace-and-debug.md) · [zh-TW](workspace-and-debug.md)
