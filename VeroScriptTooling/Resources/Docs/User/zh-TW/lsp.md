# VeroScript 語言伺服器

`veroscript-lsp` 是 VeroScript 的語言伺服器，透過標準輸入輸出傳輸，使用
`Content-Length` 分幀的 JSON-RPC 訊息。請啟動與專案使用的 VeroScript 發行套件相符的
版本。

伺服器直接使用編譯器和 Tooling 引擎，因此語言規則、import、前置處理器、宿主繫結中繼
資料和診斷目錄與 CLI 一致。

懸停回傳結構化 Markdown：符號標題、VeroScript 簽章程式碼區塊及逐行多載、文件和定義位置。
原生 Tooling 及 CLI 的懸停摘要仍保留純文字。

## 傳輸與生命週期

正常順序是 `initialize`、`initialized`、文件與工作區通知、功能請求、`shutdown`，最後
是 `exit`。必須先收到 `shutdown` 的回應再傳送 `exit`；未完成 shutdown 就退出時，程序
結果為非零。

收到 `exit` 通知後會取消排隊中與正在執行的語意分析，程序退出不會等待分析期限。

伺服器在請求執行期間繼續讀取輸入，並按照接收順序套用文件更新。用戶端可以對等待中的請求傳送 `$/cancelRequest`；被取消的請求會回傳 JSON-RPC 錯誤碼 `-32800`。輸入佇列
有界，會施加反壓而不會無限成長。`Content-Length` 以 UTF-8 位元組數計算，分幀或 JSON
錯誤會寫入錯誤串流。閉合檔案的工作區符號掃描會在列舉和解析檔案期間協作檢查取消
狀態。每個請求都有有界的伺服器截止時間，因此過時的編輯器查詢不會讓大型工作區掃描
無限期持續。

檔案 URI 遵循標準 LSP file-URI 規則。`file://server/share/module.vs` 這類 UNC URI
會保留為網路路徑，在來回轉換時不會遺失主機名稱或共用目錄。

如果用戶端宣告支援 `workspace/didChangeWatchedFiles` 的動態註冊，伺服器會在
`initialized` 後傳送 `client/registerCapability`，要求監看 `**/*.vs` 和 `**/*.[vV][sS][lL][iI][bB]`。
不支援動態註冊的用戶端可以自行設定這些比對模式，並明確傳送
`workspace/didChangeWatchedFiles` 通知。

## 語義分析與請求預算

文件更新按接收順序套用。語意權杖分析使用獨立工作執行緒及自有文件/繫結快照，因此著色期間仍可回應懸停與補全。未變更文件會重用有效的剖析快照與完整權杖串流。文件及相依變更使相關結果失效；分析設定、受監視檔案與繫結變更觸發重新分析。僅調整要求預算、追蹤或結果上限會保留語意快照；工作區掃描上限更新符號搜尋，不清除已剖析程式。重用前另檢查磁碟相依的時間戳記與大小。被取代的分析傳回 `-32801`，不得發佈過時權杖。

關閉並重新開啟同一 URI 會開始新的文件生命週期：即使版本號重新開始，舊語義 token 結果 ID 也會清除。攜帶舊 ID 的 delta 請求會收到完整 token 資料；後續請求應使用新回傳的 `resultId`。

預算從協定執行緒開始調度請求時計算；分析佇列等待計入預算，輸入佇列等待不計入。取消回傳 `-32800`，逾時回傳 `-32001`。排隊的過期請求不必等候目前分析完成。為相容舊設定，僅設定 `lspRequestTimeoutMs` 也會設定分析預算；請明確設定 `lspAnalysisTimeoutMs` 以分開預算。追蹤包含 `lex`、`preprocess`、`parse`、匯入路徑解析（`imports`）、符號、查詢、編碼與佇列耗時。`load` 包含內部解析、前置處理及匯入階段，不能直接相加。

客戶端逾時還應考慮協定佇列等待及文件更新/診斷處理時間。這些協作式分析預算不保證端對端回應時間上限。

## 初始化與設定

`initialize.locale` 是應在開啟文件前設定的 BCP 47 語言偏好。設定放在
`initializationOptions.veroscript` 之下，也可以在
`workspace/didChangeConfiguration.settings.veroscript` 中用同一個物件更新：

```json
{
	"locale": "zh-TW",
	"initializationOptions": {
		"veroscript": {
			"importPaths": ["src", "shared"],
			"stdlibPath": "stdlib",
			"defines": ["FEATURE_SCORE=1"],
			"implicitImportPaths": [],
			"allowCircularImports": true,
			"allowProjectWideAccess": false,
			"lspRequestTimeoutMs": 2000,
			"lspAnalysisTimeoutMs": 5000,
			"lspTraceAnalysis": false,
			"maxCompletionItems": 512,
			"maxWorkspaceSymbolResults": 2000,
			"maxReferenceResults": 5000,
			"maxWorkspaceFiles": 10000,
			"maxWorkspaceFileBytes": 2097152,
			"bindingCatalogPaths": ["bindings/core.json", "bindings/game.json"],
			"predefinedMacros": [
				{
					"name": "FEATURE",
					"parameters": ["value"],
					"variadic": false,
					"body": "value",
					"sourceName": "host"
				}
			]
		}
	}
}
```

| 設定 | 類型 | 作用 |
| --- | --- | --- |
| `importPaths` | 字串陣列 | 追加模組搜尋根目錄。 |
| `stdlibPath` | 字串 | 標準函式庫搜尋根目錄；空值會清除自訂根目錄。 |
| `defines` | 字串陣列 | 物件式前置處理定義，例如 `FEATURE_SCORE=1`。 |
| `implicitImportPaths` | 字串陣列 | 按正常可見性規則提供給每個原始檔的檔案。 |
| `allowCircularImports` | 布林值 | 是否允許循環模組相依性。 |
| `allowProjectWideAccess` | 布林值 | 是否允許專案範圍共用未匯出宣告。 |
| `lspRequestTimeoutMs` | 整數 | 互動請求截止時間（毫秒，50–30000），預設 2000。 |
| `lspAnalysisTimeoutMs` | 整數 | 語義 full/delta 預算（毫秒，50–30000），預設 5000。 |
| `lspTraceAnalysis` | boolean | 透過 `window/logMessage` 輸出分析耗時，預設 false。 |
| `maxCompletionItems` | 整數 | 單次補全清單回傳的最大項目數（32–5000）；截斷的清單會標記為 incomplete。 |
| `maxWorkspaceSymbolResults` | 整數 | 單次 `workspace/symbol` 請求回傳的最大符號數（64–20000）。 |
| `maxReferenceResults` | 整數 | 單次參照請求回傳的最大位置數（64–50000）。 |
| `maxWorkspaceFiles` | 整數 | LSP 工作區根目錄下可加入閉合檔案索引的 `.vs` 最大檔案數（1–100000）。 |
| `maxWorkspaceFileBytes` | 整數 | 閉合工作區索引中單一檔案的大小上限（1024–67108864 位元組）。 |
| `bindingCatalogPaths` | 字串陣列 | 依序合併的宿主繫結目錄；重複定義會被拒絕。 |
| `predefinedMacros` | 物件陣列 | 含 `name`、`parameters`、`variadic`、`body` 和選用 `sourceName` 的宿主巨集。 |

這些設定應與同一專案使用的 CLI 一致。伺服器啟動時以及檔案變更時都會檢查
`bindingCatalogPaths`。繫結目錄變更後會重新載入並重新分析所有開啟的文件；繫結目錄只
提供中繼資料，不會自行註冊執行階段回呼。

`initialize` 回應也包含 `serverInfo`，其中有伺服器名稱、產生的產品版本、宿主平台和
JIT 能力。這些欄位描述目前執行的發行套件，用戶端可以顯示它們，但不應硬編碼特定版本，
也不應根據平台或 JIT 值推斷語言語意。

## 已提供的能力

| 能力 | 行為 |
| --- | --- |
| 文件同步 | 開啟/關閉通知、增量變更，以及攜帶文字的儲存通知。 |
| 補全 | 觸發字元為 `.`, `:`, `@`；補全項目會攜帶可用的解析後文件，resolve 會保留這些文件。只有缺少 documentation 時才會將 `detail` 轉為 Markdown。 |
| 懸停 | 已解析的符號詳細資訊，以及連續的來源或宿主文件。來源中的 `@param`、`@return(s)` 標記使用在地化標籤。 |
| 導覽 | definition、declaration、type definition 和 implementation。 |
| 符號與範圍 | 巢狀文件符號，以及 token、行、全文件的選取範圍。 |
| 文件連結 | 為帶引號的 `import` 和 `#include` 目標產生連結。 |
| 摺疊範圍 | 提供 lexer-safe 的區塊、連續註解和 import 群組摺疊範圍。 |
| 語意 token | 支援全文件 token 與 full delta；不提供範圍請求。 |
| 簽名與參照 | 簽名說明、參照和文件醒目提示。 |
| 重新命名與格式化 | `prepareRename`、重新命名、全文件、依完整實體行擴展的範圍格式化，以及輸入時局部格式化。 |
| 程式碼動作 | `source.formatDocument`、安全移除完全重複匯入的 `source.organizeImports`、產生方法實作的 `refactor.rewrite`，以及 `codeAction/resolve`。 |
| 工作區符號 | 對原始碼符號、宿主符號和 soft symbol 進行不區分大小寫的搜尋。 |

## 協定方法範圍

伺服器接受以下生命週期和同步訊息：

- `initialize`、`initialized`、`shutdown` 和 `exit`；
- `textDocument/didOpen`、`textDocument/didChange`、`textDocument/didSave` 和
  `textDocument/didClose`；
- `workspace/didChangeConfiguration`、`workspace/didChangeWorkspaceFolders` 和
  `workspace/didChangeWatchedFiles`；以及
- 用於取消等待中請求的 `$/cancelRequest`。

功能請求方法包括：

- `textDocument/completion` 和 `completionItem/resolve`；
- `textDocument/hover`、`textDocument/definition`、
  `textDocument/declaration`、`textDocument/typeDefinition` 和
  `textDocument/implementation`；
- `textDocument/prepareTypeHierarchy`、`typeHierarchy/supertypes` 和
  `typeHierarchy/subtypes`；
- `textDocument/documentSymbol`、`textDocument/selectionRange` 和
  `textDocument/documentLink`、`textDocument/foldingRange`；
- `textDocument/semanticTokens/full` 和
  `textDocument/semanticTokens/full/delta`；
- `textDocument/signatureHelp`、`textDocument/references` 和
  `textDocument/documentHighlight`；
- `textDocument/prepareRename` 和 `textDocument/rename`；
- `textDocument/formatting`、`textDocument/rangeFormatting`、
  `textDocument/onTypeFormatting`、`textDocument/codeAction` 和
  `codeAction/resolve`；以及
- `workspace/symbol` 和 `workspaceSymbol/resolve`。

## 文件同步

文件同步使用增量模式（`textDocumentSync.change = 2`）。伺服器按照用戶端傳送順序將
變更套用到記憶體覆蓋層，同時接受完整文字變更和範圍變更。LSP 行號從 0 開始，字元位置
使用 UTF-16 code unit。

每個 URI 在一次開啟期間的文件版本必須遞增。版本號不大於上次接受版本的 `didChange` 會被忽略，避免
延遲訊息覆蓋較新的文字。`didSave` 可以攜帶完整文字，並在發布診斷前更新覆蓋層。
`didClose` 會移除覆蓋層並清除該 URI 已發布的診斷。開啟的緩衝區在 import 解析中優先於
磁碟檔案。

`workspace/didChangeConfiguration` 會套用新設定並重新分析開啟的文件。
`workspace/didChangeWorkspaceFolders` 會套用新增和移除的工作區根目錄。
`workspace/didChangeWatchedFiles` 會使工作區符號索引失效，並重新分析開啟的文件，讓磁碟
上的變更生效。

`initialize.workspaceFolders`（或 `rootUri`）會建立閉合檔案索引使用的明確工作區根目錄。
開啟的緩衝區永遠優先；`maxWorkspaceFiles` 和 `maxWorkspaceFileBytes` 會套用到這些
根目錄下的閉合檔案。

## Tooling 語意

- 補全會將可呼叫的同名多載合併為一個項目，並在 detail 中逐行列出簽名。在函式呼叫情境中，
  合併的多載項目會使用 `Select(${0})` 這類空參數停止點，不猜測應插入哪個多載的參數。
  非多載的可呼叫項目可以使用 `Helper(${1:count}, ${2:name})` 等 LSP
  snippet；泛型型別項目可以使用 `HostPair<${1}, ${2}>` 等 snippet。若泛型補全的替換範圍後已有 `<`
  （中間可有空格或 tab 字元），伺服器會省略泛型 snippet，只插入型別名稱，避免再次插入 `<`；
  可呼叫項目的補全目標後方已有 `(` 時，會省略 snippet，避免重複產生呼叫語法。
  只有用戶端宣告支援 `textDocument.completion.completionItem.snippetSupport` 時才回傳 snippet；
  否則使用一般文字插入。用戶端宣告支援 snippet 時，variadic host formatter 在函式呼叫情境中使用單一空參數停止點，
  例如 `print(${0})`，不會從合併的 overload detail 推測參數佔位符。不支援 snippet 時只插入函式名稱。
- 來自限定命名空間的補全會在 `label` 中保留葉節名稱。只有存在可用的 callable 或泛型型別 snippet 且用戶端宣告支援時，
  `insertText`（以及欄位存在時的 `textEdit.newText`）才使用該 snippet；否則只插入葉節名稱。其 container
  名稱位於標準欄位 `labelDetails.description`。選用的
  `$valencePresentation` metadata 為 Valence 提供最終 label/declaration 文字和 UTF-16 語意片段，
  其他 LSP 用戶端可以忽略它。它不會改變 filtering、sort order 或實際插入文字。
- namespace segment 補全會在 `label` 和 `filterText` 保留葉節名稱。在 expression/statement 限定路徑中接受 namespace 時，
  若替換範圍後面沒有 `::`，插入文字會加上它；若已有 `::`，或正在補全 `using namespace` 目標，則只插入名稱。
- 補全會先依文字比對品質、再依上下文優先權排序：精確比對、忽略大小寫的精確比對、前綴、
  單字邊界／camel-case、包含和子序列比對。`sortText` 帶有這個完整順序。只有首項屬於精確、
  忽略大小寫精確或前綴層級，且嚴格優於所有其他回傳項目時，才會標記 `preselect`。
- 基元與容器型別詞都是內建型別。基元拼寫在補全、hover 和 semantic token 中仍分類為
  `keyword`；容器拼寫則分類為 `type`／type alias。兩者的 hover detail 都遵循目前的在地化，
  都沒有來源定義，也不能重新命名。
- `completionItem/resolve` 保留原始補全項目與既有文件。只有缺少 documentation 且存在 `detail` 時，才會將 detail 作為 Markdown 文件回傳。
- 懸停會讀取來源宣告前的連續註解。來源定義的前置處理器巨集補全項目可攜帶其前置註解；宿主補全文件來自繫結中繼資料。懸停會使用目前語言目錄的標籤呈現 `@param`、`@return` 與 `@returns`。
- 使用者命名空間和保留的 `std::` 樹在懸停、導覽、參照、重新命名、大綱和語意 token 中
  都按 namespace 符號處理。標準函式庫命名空間不可重新命名。
- `textDocument/declaration` 回傳與 definition 相同的位置。type definition 會尋找
  class、struct、interface、enum 或 type alias 的宣告，並能在不完整緩衝區中使用大綱和
  宣告標頭恢復；implementation 會回傳衍生型別和覆寫成員的實作位置。只有參數與
  `const` 限定詞相符、且不是 `final` 的 virtual、`override` 或 abstract 成員才會被納入；
  型別名稱有歧義時會排除，不會把 definition 當成 implementation 回傳。
- selection range 按 token、行、全文件逐級巢狀。document link 會從目前文件解析帶引號的
  `import` 和 `#include` 路徑，也支援選用的 `export` 前綴。
  連結共用核心模組解析規則，包含目前目錄、設定的搜尋路徑、標準函式庫目錄和未儲存的開啟文件；
  尚未解析成功的目標保留本機連結。
- semantic-token delta 在前一個結果未知或過期時回傳完整資料；結果識別碼相符時回傳最小
  連續差異 edit 和新的 result id。
- semantic token 圖例的完整類型為 `namespace`、`type`、`class`、`enum`、`interface`、
  `struct`、`variable`、`property`、`enumMember`、`function`、`method`、`macro`、
  `keyword`、`decorator`、`decoratorArgument`、`constant`、`label`、`string`、`number`
  、`comment` 和 `preprocessor`。詞法基線也會分類語言／型別詞、指令標頭和巨集名稱；它涵蓋
  每個有效實體行且不會產生重疊的 LSP 範圍，並在宣告語意從不完整輸入復原時持續可用。
- range formatting 會安全擴展至完整實體行並保留外部程式碼區塊縮排。整份文件、範圍與輸入時格式化都遵循
  `insertSpaces`：設為 `false` 時每層縮排輸出一個 tab 字元；否則每層使用 `tabSize` 個空格（最多 16，
  缺少或無效時預設為 4）。輸入 `;`、`}` 或換行後也會套用同樣的局部編輯。
- code action 在未被 `context.only` 排除時回傳 `source.formatDocument`。只有能安全識別並刪除
  完全重複的匯入行時，才會回傳 `source.organizeImports`；該動作只刪除這些重複項目。
- 在類別或結構體本文中有效的空游標位置，code action 會為補全提供的相同方法實作產生
  `refactor.rewrite` 動作；`context.only` 可用 `refactor` 或 `refactor.rewrite` 篩選。
- 在類別或結構體本文中的空位置或識別字前綴位置，補全會提供可存取、尚未實作且不是 `final` 的
  繼承虛擬方法，以及可實作或覆寫的介面方法（包括待實作的純介面要求和可覆寫的虛擬預設方法）。
  泛型簽名會依衍生型別具體化。接受候選項會插入包含可編輯
  `throw 0;` 方法本體的宣告；純介面實作不會附加 `override`。用戶端不支援 snippet 時會收到一般方法文字。
- 工作區符號查詢不區分大小寫，並以空白分割查詢詞。它會比對符號名稱、限定名稱、容器、
  detail 和檔案標籤；`::` 與舊式 `.` 分隔符會視為相同形式。精確與前綴比對會排在模糊
  子序列比對之前。每個項目都帶有穩定的 `data.veroscriptSymbolId`；
  `workspaceSymbol/resolve` 會更新 detail 與 containerName，找不到 ID 時保留原始項目。
- 文件符號與工作區符號會公開 namespace 容器。同一個宣告 identity 即使經由多個匯入路徑
  被觀察到，也只會回傳一次。沒有來源檔案的宿主符號使用 `veroscript:host-bindings` URI
  和零範圍。

開啟、接受的變更、儲存、設定變更以及工作區檔案變更後，伺服器會發布
`textDocument/publishDiagnostics`。診斷按來源檔案分組，也包含匯入檔案；空陣列會清除舊
標記。每筆診斷都包含 LSP severity、message、可用時的穩定 code，以及
`source: "veroscript"`。沒有來源檔案的宿主符號使用 `veroscript:host-bindings` URI 和零範圍。

當請求沒有適用結果時，伺服器會回傳該 LSP 方法對應的 null 或空結果。不支援的方法回傳
JSON-RPC `-32601`；當方法要求特定文件或位置而參數格式錯誤時，回傳 `-32602`。

## 在地化

伺服器使用與 CLI 和執行階段相同的在地化策略。`initialize.locale` 只改變診斷和 Tooling
的顯示，不改變腳本語意或符號解析。用戶端應使用訊息 ID/診斷 code，而不是翻譯後的文字
進行邏輯判斷。缺少的翻譯檔案或鍵會回退到 `en-US`。請參閱[診斷在地化](../../../../../VeroScript/Resources/Docs/User/zh-TW/localization.md)。

## 相關參考

另請參閱 [Tooling 模組](index.md)、[CLI 參考](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md)、[宿主繫結](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/bindings.md)
和[除錯](debugging.md)。

## 主機提供的模組原始碼

主機可以提供已解析的模組環境與編譯處理器。標準 Runtime 伺服器不會自動沿用 CLI 的套件選擇，也不會自動取得應用程式的原生回呼。請使用與所編輯程式一致的匯入路徑、繫結目錄與主機服務；繫結目錄提供描述，不會安裝可執行處理器。

當 `capabilities.experimental.veroscriptModuleEnvironment` 宣告 `moduleSourceMethod` 時，支援此能力的用戶端可以透過該方法（`veroscript/moduleSource`），以唯讀方式開啟傳回的 `veroscript-module:` 位置。這些內容是原始碼快照，不是可編輯的工作區檔案。伺服器重新啟動或編譯指紋改變後，應從所屬原始碼重新跳轉；模組不存在或無權存取時傳回 `null`。一般磁碟原始碼仍使用檔案 URI。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/lsp.md) · [fr-FR](../fr-FR/lsp.md) · [ja-JP](../ja-JP/lsp.md) · [ko-KR](../ko-KR/lsp.md) · [zh-CN](../zh-CN/lsp.md) · [zh-TW](lsp.md)
