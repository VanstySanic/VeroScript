# Tooling 架構與協議

`VeroScriptTooling` 是 command-line、LSP、DAP 與其他 host 共享的 language-service layer。它 consume public language-core contract，並將 semantic query 與 protocol transport 分離。

LSP 與 DAP 共用 `Private/Internal/VeroScriptToolingJson.h` 和 `.cpp` 中的私有 JSON 值、剖析器與序列化器，字串跳脫重用 `Shared/Private/VeroScriptJsonString.h`。訊息封裝、請求關聯、排程、取消及協定專有欄位仍由各自配接器負責。調整共用實作時保留確定的物件順序、數值轉換、既有深度上限及錯誤文字。

## 分層資料流

依賴方向為：language core 負責 parsing、semantic analysis、module loading、runtime metadata、diagnostic、debugger semantics；`VeroScriptWorkspace` 提供 project context、document overlay、source/import resolution、preprocessor context、binding catalog、cross-file state；public feature service query source/workspace 並返回 stable result type；LSP/DAP adapter serialize result 並處理 transport。feature service 不讀 editor state 或寫 source file；adapter 不得從 protocol text reconstruct 已有 public service 提供的 semantic result。

## 公共 feature 家族

`VeroScriptAnalyzer` 提供 workspace-aware diagnostic（包括 imported file），並按 file label/URI publish；需要 clear 時也 publish empty list。 `VeroScriptOutline`、`VeroScriptNavigation`、`VeroScriptHover`、`VeroScriptCallHelp`、`VeroScriptComplete`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptRename`、`VeroScriptFormat`、`VeroScriptWorkspaceSymbols` 處理 parse-tolerant structure、lookup、completion、call、reference、edit、format、workspace search。

confirmed namespace segment（包括 nested user namespace 與 `std::`）按 semantic namespace 處理。host binding catalog/soft builtin 可參與 lookup/completion/hover/workspace symbol；host-only symbol 需要 URI 時使用 virtual host-bindings location。completion 會將相同可見名稱的 callable overload 合併為一個 item，並在 detail 中逐行列出各 source-facing signature。call context 的合併 item 使用空 argument stop，不會猜測特定 overload 的參數。沒有 overload 的 callable 或 generic entry，只有在 client 宣告支援 `textDocument.completion.completionItem.snippetSupport` 時才會回傳 snippet，否則使用一般文字插入。若補全目標後已有左括號 `(`（中間可有空白或註解），會省略 callable snippet，以避免重複產生呼叫語法。rename 先 validate target，再 re-resolve 每個 reference，standard-library namespace 不可 rename；format 返回 text edit，tooling 永不 apply。

`FCompletionItem::MatchQuality` 是首要 textual ordering key，`SortPriority` 只用來打破相同
quality 的同分。adapter 必須透過 LSP `sortText` 保留此 order，且只能為明確優於其他項目的
exact、case-insensitive-exact 或 prefix match 設定 `preselect`。primitive type word 是 built-in
`Keyword` entry，container type word 是 `TypeAlias`／`type` entry。兩者的 display detail 會在地化，
type identifier 保持 stable，沒有 source definition，且 rename 會被拒絕。

限定 namespace 的補全會在 `label` 與插入文字保留葉節名稱；存在可用的 callable 或泛型型別 snippet 且用戶端宣告支援
`textDocument.completion.completionItem.snippetSupport` 時，插入文字才包含該 snippet，
否則只插入葉節名稱。插入文字使用 `insertText`，存在替換範圍時也使用 `textEdit.newText`。限定 namespace 放在
`labelDetails.description`。選用的 `$valencePresentation` 向 Valence
提供最終顯示文字與 UTF-16 語意片段；其他 LSP 用戶端可忽略，且此顯示資訊不會改變排序或插入內容。

namespace segment 補全與 namespace member 補全不同。在 expression/statement 限定路徑中，namespace item 會在
`label` 和 `filterText` 保留葉節名稱；只有替換範圍後面沒有 separator 時，插入文字才會追加 `::`。
補全 `using namespace` 目標時只插入名稱。用戶端支援 snippet 時，`print` 等 variadic host formatter 在函式呼叫情境中
使用空參數停止點（例如 `print(${0})`），不會從合併的 overload detail 推測參數佔位符。

對 semantic token，LSP adapter 會將 resolved span 與 lexer baseline merge；baseline 涵蓋
language/type word、directive、macro、literal/comment。在 incomplete input 時仍可使用，並在
encode LSP stream 前移除 overlap。

## LSP 協議契約

`VeroScriptLspStdio` 擁有 `Content-Length` framing、JSON-RPC correlation、request dispatch、cancellation、diagnostic publication、lifecycle state。advertised request 為 `textDocument/completion`、`completionItem/resolve`、`textDocument/hover`、`textDocument/definition`、`textDocument/declaration`、`textDocument/typeDefinition`、`textDocument/implementation`、`textDocument/prepareTypeHierarchy`、`typeHierarchy/supertypes`、`typeHierarchy/subtypes`、`textDocument/documentSymbol`、`textDocument/selectionRange`、`textDocument/documentLink`、`textDocument/foldingRange`、`textDocument/semanticTokens/full`、`textDocument/semanticTokens/full/delta`、`textDocument/signatureHelp`、`textDocument/references`、`textDocument/documentHighlight`、`textDocument/prepareRename`、`textDocument/rename`、`textDocument/formatting`、`textDocument/rangeFormatting`、`textDocument/onTypeFormatting`、`textDocument/codeAction`（`source.formatDocument`、`source.organizeImports`、`refactor.rewrite`）、`workspace/symbol`、`workspaceSymbol/resolve`。

lifecycle/synchronization notification 是 `initialize`、`initialized`、`textDocument/didOpen`、`textDocument/didChange`、`textDocument/didSave`、`textDocument/didClose`、`shutdown`、`exit`；`workspace/didChangeConfiguration` 更新 setting，server 接收 workspace-folder/watched-file notification 和 `$/cancelRequest`，取消返回 `-32800`。completion trigger 是 `.`、`:`、`@`，signature-help trigger 是 `(`、`,`。selection range 從 token 巢狀到 line/document；document link resolve quoted import/include；semantic-token delta 返回 complete data 或 replacement edit。range formatting 支援 partial selection，會 expand 至 complete physical lines 並返回 actual replacement range。LSP position 為 zero-based UTF-16，C++ position 為 one-based byte column，adapter 只轉換一次；舊 document version change 被 ignore。

## 配置與本地化

server 從 `initializationOptions` 和 `workspace/didChangeConfiguration.settings` 讀取 `veroscript` setting，包含 import/implicit-import path、`stdlibPath`、define、predefined macro、circular-import/project-wide-access policy、ordered `bindingCatalogPaths`。catalog 按序 merge，duplicate definition reject；改變後在下一次 non-lifecycle request reload，並 reanalyze open document。catalog 描述 host symbol/documentation，不註冊 runtime callback。

`initialize.locale` 是 BCP 47 preference；不支援時保留 environment catalog，missing file/key fallback `en-US`。diagnostic identifier/message argument、binding identifier、type/member name、protocol field、URI、path、source text 永不 translate。

## 不變數

必須保留 public source-position/range、附著 result 的 document identity/accepted version、cancellation/request correlation/wire order、stable diagnostic identifier/substitution argument、empty feature result 與 protocol failure 的區分、以及 tooling 產生 edit 而不 apply 的規則。user-facing method/configuration/editor behavior 見[VeroScript 語言伺服器參考](../../User/zh-TW/lsp.md)。

## Debug 與 DAP 邊界

`VeroScriptDebugSession` 負責 compilation、breakpoint、step、VM inspection、expression evaluation、variable write、output capture、JIT/backend selection。 `VeroScriptDapAdapter` 僅 map 為 DAP-shaped request/event，不能新增 JSON/stdio/TCP framing 或第二套 breakpoint/evaluation engine。 `FDapLaunchArgs` 帶 source/path、overlay、binding、import policy、stop-on-entry、call-depth、backend、script argument、standard-library root、launch-local environment、working directory、initial stdin；debug attach 即使請求 `Mixed`/`Jit` 也使用 `Vm`。 `Initialize` 報告 engine version/platform/JIT capability，各自獨立，不重構 product version。

`VeroScriptDebugSession::BindBreakpoint` 擁有 physical-line/preprocessor-map resolution，只註冊 exact compiled source label。invalid request，或沒有 eligible location 的 non-executable request，會返回帶 localized display text 的 unregistered result。adapter 保留 external DAP ID、回報 actual bound line，並在 pre-launch breakpoint rebind 後 publish `Breakpoint`/wire `breakpoint` 的 `reason: "changed"` event。

`ConfigurationDone` synchronous 地擁有 run，只有 stopped event callback 可進行 continue/step/stack/scope/variable/evaluate；`Disconnect` resume active run，並在 return 後 clear session。transport 應在 initialize response 後 emit `initialized`，不能由 `VeroScriptDapAdapter::Initialize()` 自行傳送。

### DAP transport 契約

`VeroScriptDapStdio::Run` 透過 caller stream 讀寫 Content-Length framed DAP JSON。 `FDapStdioRunOptions` 提供 binding/catalog path/script arg/stdlib path/environment/working directory/initial input default。live binding 優先於 catalog，借用到 `Run` 結束；catalog 只足以 compile/analyze，native call 需要 live registry。

stdio 接受 `program`/inline `source`、`fileLabel`、`workspace`、`cwd`/`workingDirectory`、`stopOnEntry`、`args`、`input`、`env`/`environment`、`stdlibSearchPaths`/`stdlibPath`、import/define/catalog/policy、`backend`、`maxCallDepth`。script arg 傳給 `main(string[]
args)`，不加 script path；`input` 是 initial script stdin，DAP stdin 不轉發。environment 只作用於 run，之後 restore。上限為 4096 argument、2048 import/implicit-import/stdlib path、1024 environment entry；invalid JSON/backend/call-depth 為 localized protocol diagnostic。

Core 偵錯器的 `bReadOnly` 狀態經 `FDebugVariable`、`FDapVariable` 傳至 DAP 的 `presentationHint.attributes = ["readOnly"]`。巢狀 variant 的不可變負載在監看結果中也保留此狀態；拒絕修改不可變值仍由 Core 負責。

`VeroScriptDapTcp::Run` 複用 IPv4 framing，順序服務 client。 `FDapTcpRunOptions::BindAddress` 預設 `127.0.0.1`；empty 或 `0.0.0.0` 為 all interface，推薦 loopback。 `Port == 0` 請求 ephemeral port，並經 `OnListening` 報告；`MaxSessions == 1` 一 client，`0` unlimited，negative 歸一為一。non-terminating attach disconnect 關閉 client 並允許下一 session；terminate 停 listen loop，host termination 對映 successful server return，其他 failure 為 non-zero。

## LSP workspace 與 request 契約

`VeroScriptLspStdio::Run` 是 Content-Length JSON-RPC server，worker 分析可讓 run 中 cancellation 被觀察。initialize locale unsupported 時不改 environment catalog。root precedence：array `workspaceFolders`（即使 empty）優先；無 array 才用 `rootUri`；兩者均無才用 FLspStdioRunOptions::DefaultWorkspaceRoots。root 是 indexing root 而非 import path，只遞迴 `.vs` 且跳過 .git/.verocache/build/dist/Binaries/Intermediate；open document/explicit overlay 優先。workspace-folder/watched-file change 更新 index/analysis。

server advertise completion/resolve、hover、definition/declaration/type definition/implementation/type hierarchy、documentSymbol/selectionRange/foldingRange/documentLink/full+delta semantic token、signature/references/highlight/rename、full/range/on-type format、workspace symbol/resolve、`source.formatDocument`/`source.organizeImports` action。cancel/timed-out request 不 publish/cache partial index。default safe limit：2000 ms interactive / 5000 ms semantic、512 completion、2000 workspace symbol、5000 reference、10000 indexed file、single file 2 MiB；setting lspRequestTimeoutMs、maxCompletionItems、maxWorkspaceSymbolResults、maxReferenceResults、maxWorkspaceFiles、maxWorkspaceFileBytes 受 server 範圍 clamp。

`VeroScriptFormat` 是 lexical formatter；`FFormatOptions` 預設每層縮排為 4 個空格，也可透過 `bInsertSpaces=false` 輸出一個 tab 字元。LSP formatting 遵循 `options.insertSpaces` 和 `options.tabSize`：前者為 `false` 時每層輸出一個 tab 字元，否則每層輸出 `tabSize` 個空格（最多 16，缺少或無效時預設 4）。range formatting expand complete physical line，並在 FRangeFormatResult 返回 actual range；failure 返回 empty LSP edit。organize imports 只刪除相同 lexical text 的 duplicate import，發現 block comment delimiter 則拒絕 edit。 `VeroScriptWorkspace::WorkspaceSymbols` merge open buffer/overlay/closed file；`FWorkspaceSymbolsOptions::ShouldCancel` 僅為單次 query borrowed、永不 retain，cancelled query 設 bCancelled，result 不得 cache/publish。default uncustomized cache 因 document/config change invalidate，query-specific option/overlay/root/limit/cancel probe 不進入 cache。

## 來源文件註解

來源定義的前置處理器巨集補全項目可攜帶緊鄰巨集之前的連續 `//`/`///` 或 `/* ... */` 註解；宿主補全文件來自繫結中繼資料。`completionItem/resolve` 會保留既有文件；只有缺少 documentation 時才會將 `detail` 作為 Markdown 使用。懸停會擷取來源宣告之前的連續註解，並使用目前語言目錄的標籤呈現 `@param 名稱 說明`、`@return 說明` 與 `@returns 說明`；未知標記會保留在註解本文。這是 Tooling 的顯示慣例，不會改變編譯器處理註解的方式。

## 語義著色調度

語義著色使用獨立分析執行緒。按序執行的協定執行緒擁有文件更新、請求狀態與輸出；分析工作擁有緩衝區、選項與繫結副本，驗證版本及相依項目後才發布不可變解析和 token 快照。同版本 full/delta 重用完整資料；懸停、導航與補全重用有效解析快照。復原後的著色快照不當作互動查詢的原始 AST。取消及期限檢查涵蓋詞法、前置處理、解析、匯入、符號遍歷及編碼。取消排隊工作會喚醒協定執行緒，過期工作不必等候目前分析。預設互動預算 2000 毫秒、語義分析 5000 毫秒；`lspAnalysisTimeoutMs` 分開分析預算，`lspTraceAnalysis` 開啟耗時追蹤。詳見[請求預算](../../User/zh-TW/lsp.md)。

### 回呼、快取與生命週期契約

`FSemanticTokensOptions::ShouldCancel` 和 `RecordStage` 僅為此次同步查詢借用，在其分析執行緒呼叫；擷取物件必須有效到呼叫回傳。取消會回傳 `bCancelled` 且沒有 token，不完整結果不得發布或快取；耗時觀察回呼的例外會被忽略。`FSemanticTokensResult::Snapshot` 擁有程式和原始碼儲存，但它是不透明的 Tooling 內部狀態，不是受支援的外部快取或重用 API。

重用需要文件內容／版本、設定和相依項均一致。磁碟記錄在讀取匯入原始碼／套件之前取得，在發布或重用之前再次檢查；無效記錄禁止重用。覆蓋層變更也會使受影響的匯入失效。關閉並重新開啟同一 URI 會清除剖析／token 狀態和舊結果 ID，即使新文件版本從較小數字重新開始。分析設定變更會使正在計算的工作失效。相同原始碼標籤跳過重複檔案系統正規化；不同標籤仍進行正規路徑比較。

`VeroScriptLspResponsivenessTest.py` 驗證真實 stdio 程序的 full/delta 重用、分析期間懸停、取消、期限、相依項編輯、設定變更及關閉重開。詞法和工作區測試涵蓋取消後的重試，以及載入與快照建構之間的磁碟變更。這些迴歸測試保留在直屬 Tooling／核心測試區域。

## 單次要求內的查詢重用

參考收集與重新命名透過 `Internal/VeroScriptSourceQuerySession` 保留目前檔案精確的剖析程式，供定點查詢重用。狀態僅在單次要求內存續，驗證磁碟相依，不重用錯誤復原或處理器驅動的程式。宣告位於目前原始碼的區塊/函式區域變數，不需掃描其他含有同名符號的已開啟模組。要求內的集合會去除重複權杖位置；每種既有路徑拼寫在一次掃描只解析一次。

僅影響協定的預算、追蹤及結果上限設定會保留語意快照；分析設定變更使快照及計算中的工作失效。工作區探索上限變更僅使符號索引失效，不會清除剖析程式。

Darwin 上 `Private/Internal/VeroScriptLspThread.h` 為協定與語意工作執行緒設定 8 MiB pthread 堆疊，其他平台使用 `std::thread`。保持 join 與擁有權約定；增大執行緒堆疊不代表提高剖析器或分析上限。JSON 浮點輸出使用 [Core 十進位轉換](../../../../../VeroScript/Resources/Docs/Developer/zh-TW/architecture-and-pipeline.md#floating-formatting-support)說明的私有格式化輔助。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/architecture-and-protocols.md) · [fr-FR](../fr-FR/architecture-and-protocols.md) · [ja-JP](../ja-JP/architecture-and-protocols.md) · [ko-KR](../ko-KR/architecture-and-protocols.md) · [zh-CN](../zh-CN/architecture-and-protocols.md) · [zh-TW](architecture-and-protocols.md)
