# WebAssembly/WASM SDK

`VeroScriptWebAssembly` 是面向瀏覽器及其他 JavaScript 宿主的無介面 WebAssembly SDK。它提供記憶體中的原始碼檢查與執行；頁面配置、編輯器、輸出面板與框架整合由宿主負責。

<a id="runtime-factory-configuration"></a>

TypeScript 工廠在建立 Emscripten 實例前複製宿主設定。同一個設定物件（包括凍結物件）可用於建立獨立執行時期，不會共用實例匯出、堆積或資源所有權。繼承的 `locateFile` 和 `wasmBinary` 設定也會保留。

## 產物與部署

請將同一次建置產生的模組、WASM 檔案與宣告檔案作為一組部署：

| 產物 | 用途 |
| --- | --- |
| `veroscript-engine.mjs` | 支援互動式輸入的 Emscripten 模組。 |
| `veroscript-engine.wasm` | 一般執行時期的 WebAssembly 二進位檔。 |
| `veroscript-engine-fast.mjs` | 不啟用 Asyncify 的低延遲執行時期模組。 |
| `veroscript-engine-fast.wasm` | 快速執行時期的 WebAssembly 二進位檔。 |
| `veroscript-webassembly.ts` | TypeScript 包裝層原始碼。 |
| `veroscript-webassembly.d.ts` | 包裝層與 WebAssembly C ABI 的型別宣告。 |
| `veroscript-engine.d.mts`、`veroscript-engine-fast.d.mts` | Emscripten 模組選項宣告。 |

標準庫與在地化目錄會嵌入產生模組的虛擬檔案系統，因此 `import "string.vs"` 不需要宿主另外下載標準庫。模組、WASM、宣告與 ABI 必須相互配對；產生的發布產物不應手動修改。TypeScript 入口會按需載入選取的 Emscripten 引擎：只匯入包裝層不會下載任一引擎，選擇快速執行時期只載入快速引擎，選擇一般執行時期只載入互動引擎。

## 選擇執行時期

腳本可能暫停等待非同步輸入時使用 `createVeroScriptRuntime`；輸入已由宿主準備好、只需要檢查或執行腳本時使用 `createFastVeroScriptRuntime`：

```TypeScript
const fast = await createFastVeroScriptRuntime();
console.log(fast.supportsInteractiveInput); // false
fast.dispose();

const normal = await createVeroScriptRuntime();
console.log(normal.supportsInteractiveInput); // true
normal.dispose();
```

快速執行時期不包含完整的 Asyncify 插樁，呼叫 `runSourceInteractive` 會發生錯誤。一般執行時期支援非同步 `readLine`，並會在 Promise 完成前保持腳本暫停。

腳本使用 `read_line`、具型別的輸入函式或 `pause` 等待非同步輸入時，請使用一般執行階段與 `runSourceInteractive`。快速執行階段適用於輸入資料已備妥的情況，無法等待非同步輸入。

## 執行後端與 API

`checkSource` 只會檢查原始碼，不會呼叫 `main`；`runSource` 會先檢查再執行。
使用 `supportsCompiledPrograms` 和 `supportsThreads` 檢查構件能力；目前發布構件
分別回傳 `true` 和 `false`。快速執行時期不支援互動式輸入。

發行模組不選擇 JIT 後端。`vm` 解譯執行位元碼；`mixed` 維持預設值，也透過 VM 執行。`jit` 在應用程式初始化、`main` 執行或要求輸入前回傳結構化診斷 `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory`。原先指定 `jit` 的呼叫端需要改用 `vm` 或 `mixed`。策略名稱與 C ABI ID 保持穩定；`result.backend` 記錄要求的策略，所有 JIT 計數均為零。

主要公開 API 包含 `VeroScriptDiagnosticSeverity`、`VeroScriptExecutionBackend`、`VeroScriptLanguage`、`VeroScriptDiagnostic`、`VeroScriptResult`、`VeroScriptJitStatistics`、`VeroScriptVirtualFile`、`VeroScriptInputRequest`、來源與執行選項、`VeroScriptProgram`、`VeroScriptCompileResult`、`VeroScriptProgramRunOptions`、`VeroScriptProgramInteractiveRunOptions`、`VeroScriptRuntime`、`VeroScriptEngineModuleOptions`、`createVeroScriptRuntime` 和 `createFastVeroScriptRuntime`。`VeroScriptInputRequest.index` 是從 0 開始的輸入要求索引。`VeroScriptProgram` 提供 `disposed`、`run`、`runInteractive` 和 `dispose`。`runtime.supportsCompiledPrograms` 表示是否支援可重複使用的編譯。`checkSource` 只檢查原始碼，不呼叫 `main`；`runSource` 會檢查後執行。

`VeroScriptResult.success` 表示編譯和執行是否正常完成，不等同於 `exitCode === 0`。只有腳本明確呼叫 `exit(n)` 時 `exitRequested` 才是 true。結果也包含輸出、錯誤輸出、後端與結構化診斷。編譯和執行結果可能包含 `programHandle`、`timings`（`timings.compileMs` 和 `timings.executeMs`），以及表示單次 JIT 活動的 `jit`。`VeroScriptJitStatistics` 提供 `compileAttempts`、`compiledEntries`、`bridgeTrampolineEntries`、`linearTrampolineEntries`、`vmTrampolineEntries`、`unsupportedEntries`、`dispatchAttempts`、`dispatches`、`fallbacks`、`hotFunctions` 和 `directNativeCalls`；後者表示在同一代 JIT 程式碼中繞過虛擬機器值橋、直接到達另一個原生入口的呼叫次數。

繫結方法呼叫等透過原生框架重入 VM 的路徑受核心的平台堆疊保護約束。巢狀過深時會回傳 `diag.call_stack_overflow`。此保護獨立於一般位元組碼遞迴的呼叫深度限制；遇到此診斷時，應減少原生重入的巢狀層數後重試。

## 一次編譯，重複執行

對於不會變更但需要多次執行的原始碼，請呼叫一次 `compileSource`，然後
重複使用回傳的程式。這可避免每次重複解析、匯入解析、語意分析和位元碼產生：

```TypeScript
const compiled = runtime.compileSource(
	`int main(string[] args)
{
	return len(args);
}`,
	{ fileLabel: "main.vs" },
);

if (compiled.program) {
	const first = compiled.program.run({ args: ["one", "two"], backend: "mixed" });
	const second = compiled.program.run({ args: ["one"], backend: "vm" });
	console.log(first.exitCode, first.timings?.executeMs, first.jit?.dispatches);
	console.log(second.exitCode, second.timings?.executeMs);
	compiled.program.dispose();
}
```

編譯階段接受與原始碼檢查相同的 `fileLabel` 和虛擬 `files` 覆蓋，但不接受
執行時期的 `input`、`args` 或 `backend`。每次 `program.run` 都使用新的執行狀態，
可以個別選擇輸入、引數和後端；編譯後的位元碼會重複使用，而虛擬機器狀態和 JIT
統計屬於單次執行。一般執行時期也支援
`program.runInteractive({ args, backend, readLine })`，快速執行時期不支援互動執行。
處理可能來自不同版本的產物時，請檢查 `supportsCompiledPrograms` 和
`supportsInteractiveInput`。程式控制代碼不再需要時呼叫 `program.dispose()`；
銷毀執行時期會釋放仍存在的程式控制代碼。

工廠的 `options` 會傳遞給產生的 Emscripten 模組。若 `.wasm` 檔案由自訂 URL
提供，可使用 `locateFile(path, prefix)`；若二進位檔已載入為 `ArrayBuffer` 或
`Uint8Array`，則可使用 `wasmBinary`。

## 語言、虛擬檔案與互動輸入

執行時期初始語言為確定性的 `en-US`。使用 `availableLanguages` 發現目前建置實際嵌入的目錄，再用 `setLanguage` 選擇。Wasm 模組內的語言狀態是共享的；需要隔離不同語言時使用不同的 Web Worker。

`files` 可為一次檢查或執行提供不寫入真實檔案系統的匯入模組：

```TypeScript
const result = runtime.runSource(
	`import "helper.vs";

int main()
{
	return answer();
}`,
	{
		fileLabel: "main.vs",
		files: [{ path: "helper.vs", source: `export int answer()
{
	return 42;
}` }],
	},
);
```

互動式輸入請使用一般執行時期和 `runSourceInteractive`，檔案結束時回傳 `null`。同一個 Wasm 模組同時只能有一個等待輸入的互動執行。TypeScript 包裝層會在等待期間拒絕其他操作和銷毀。原始 C ABI 會保護整個模組：建立和文字 getter 回傳 null，有狀態文字操作回傳 API 失敗，數值 getter 和程式操作回傳 0；記憶體操作回傳 Busy（9），記憶體結果 getter 回傳 0，租約釋放仍可呼叫，銷毀要求會被忽略，直到互動呼叫完成。長時間執行的腳本應放在 Web Worker 中。請參閱[互動式輸入教學](../../../../../../../Resources/Docs/Tutorials/zh-TW/toolchain/16-web-interactive-input.md)與[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md)。

輸入橋接層配置失敗或輸入行超出 Wasm32 配置範圍時，會傳回 API 失敗結果。模組仍可用於後續執行；`null` 仍表示正常 EOF。

## 建置與瀏覽器邊界

發布的瀏覽器產物支援已編譯程式且不使用 pthread，因此不需要 COOP/COEP 回應標頭。
TypeScript 的 `runtime.supportsThreads` 在兩個發布產物中都是 `false`。
`void main(string[] args)` 和 `int main(string[] args)` 都是有效入口；成功的非零
`int main` 仍保持 `success: true`，並透過 `exitCode` 傳回。瀏覽器基線不支援腳本級
`spawn`，嘗試建立工作會傳回 `diag.failed_to_spawn_task_thread`；不建立工作的通道、
互斥鎖、信號量和同步值操作仍可在一次執行中使用。需要隔離時，請為每個程式使用
獨立的 Web Worker。

## C++ facade 與 C ABI

C++ `FRuntime` 成功執行原始碼時，JSON 結果也會提供相同的 JIT 統計，其中包括
`directNativeCalls`。

C++ 的 `VeroScript::WebAssembly::FRuntime` 支援語言選擇、原始碼檢查與執行、虛擬檔案、輸入串流、後端名稱與 JSON 結果轉換。目前 C++ facade 不直接提供已編譯程式的所有權 API；需要一次編譯後重複執行時，請使用 TypeScript 包裝層或 C ABI。不使用 TypeScript 包裝層的宿主可以包含 `VeroScriptWebAssemblyC.h`；C ABI 也提供 `vero_web_compile_source_with_context`、`vero_web_run_program`、`vero_web_run_program_interactive` 和 `vero_web_destroy_program`。程式控制代碼由執行時期擁有，必須明確釋放。後端 ID 為 `0 = VM`、`1 = Mixed`、`2 = JIT`。語言字串在成功切換語言或執行時期銷毀前有效；可用語言 JSON 在下一次可用語言查詢或執行時期銷毀前有效；其他回傳 JSON 由執行時期持有到下一次回傳執行時期擁有文字的 API 呼叫或銷毀。輸入字串和陣列只在呼叫期間借用。

## 受管理記憶體

宿主介面：`runtime.memory` / `runtime.memory.setLimits` (bigint)。所有權、配額、檢視失效與租借規則請參閱 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)。

<a id="managed-memory-contract"></a>

宿主封裝支援緩衝區、切片、複製、純量讀寫、快照與明確租約。大小、偏移、配額、ID 和 64 位元整數使用 `bigint`；純量支援小端或大端。`fromArray` 建立獨立快照，也適用於 Node.js `Buffer`；`toArray` 傳回副本。使用完後釋放區域。租約固定底層儲存、阻止調整大小，執行階段銷毀後仍可釋放。`withBytes` 只接受同步回呼：不要在回呼中釋放該租約、保留可寫入檢視或使 Wasm 堆積增長。唯讀租約提供副本；堆積增長後，可寫入檢視會在下一次回呼時重建。等待互動輸入期間會拒絕有狀態的記憶體操作。

宿主區域與指令碼工作階段的配額彼此獨立，各內容初始為單緩衝區 16 MiB、總保留 32 MiB。`runtime.memory.setLimits` 只修改宿主區域配額，零表示不限制。這些配額只涵蓋受管理的位元組儲存，不涵蓋整個 Wasm 堆積，也不是完整沙箱。

## 核心相容與診斷

<a id="compiler-diagnostics"></a>

內建編譯器會準備跨檔案宣告，並接受結構化註記；未知註記保留為中繼資料。瀏覽器 SDK 不註冊原生註記處理器。匯入 `delegate.vs` 後使用 `std::delegate` 和 `std::multicast_delegate`，其成員採用小寫命名。診斷保留 `error`、`warning`、`info`、`hint` 層級；成功結果也可能包含非錯誤診斷。原始碼執行保留編譯診斷，可重複使用的程式則在編譯結果中提供這些診斷。位元組碼使用固定 `VSBC` 識別碼和產品版本 `0.1.1`；舊開發組建產生的套件與快取應從原始碼重建。

## 互動輸入所有權

互動呼叫在讀取選項後重新檢查模組狀態。重入要求或執行時期已銷毀的要求會在接管輸入提供器前被拒絕，正在執行的程式及其後續輸入保持有效。

<a id="input-provider-failures"></a>

SDK 輸入提供器擲出錯誤或 Promise 被拒絕時，會終止本次執行並以原始例外拒絕；無效的傳回型別和包含 NUL 的輸入行會以 `TypeError` 拒絕。失敗會恢復原生呼叫堆疊並完成清理，不再被當作 EOF 後繼續正常執行指令碼。執行階段和已編譯程式仍可重複使用。`null` 仍表示正常 EOF。未標記的原始模組提供器保留對拒絕或無效輸入的既有 EOF 回退行為。

## 主控台輸出與能力

<a id="console-capabilities"></a>

`std::console::write` 將不含換行的文字附加至 `output`；`flush` 刷新標準輸出。`std::console::error` 使用宿主的逐行診斷通道，末尾未換行的文字在 `errorOutput` 中會補上換行。SDK 不提供終端宿主：能力查詢傳回 `false`，尺寸為零，有效的顏色、游標和清屏操作傳回 `false`，不會加入逸出序列或刪除已擷取的文字。非同步輸入支援與終端能力相互獨立。

即使終端能力不可用，無效的顏色名稱、清屏模式、游標座標、參數型別或參數數量仍會產生診斷。請求被拒絕後，執行階段仍可重複使用。擷取文字會在執行完成後透過 `VeroScriptResult` 傳回。

## 目前語言功能

<a id="compiler-and-application-services"></a>

兩種引擎均支援小寫 `pair<T, U>` / `std::pair`、`std::attribute<T>`、`std::bind_method`，以及內嵌的 `attribute.vs`、`callable.vs`、`i18n.vs` 和 `compiler.vs` 模組。應用程式的 `std::i18n::context` 支援字面文字、保留參數型別的具名格式化和獨立快照；其語言設定不改變 SDK 的診斷語言。SDK 未提供應用程式資源或格式化器提供者。

`import compiler "builder.vs";` 可在 `checkSource`、`compileSource` 或 `runSource` 期間，從 `files` 載入腳本編譯擴充。因此，檢查雖不呼叫應用程式的 `main`，仍可能執行擴充程式碼。核心採用預設的擴充限額與能力原則。前端需要能夠中斷編譯時，應放入 Web Worker。擴充失敗會傳回診斷，執行階段仍可重複使用。

## 互動堆疊工作區

互動輸入可以在巢狀的繫結方法呼叫內暫停執行。深層原生重入仍受主程式呼叫堆疊限制；快速引擎需要在執行前提供輸入。

## 註記名稱比對

<a id="annotation-name-matching"></a>

註記名稱預設區分大小寫。腳本編譯擴充可在註冊前設定 `annotation_definition.case_sensitive = false`，為該定義啟用不受地區設定影響的 ASCII 大小寫比對。處理器訂閱及反射 `has_annotation`、`find_annotations` 和 `annotation_info.matches_name` 遵循此原則。`name()` 和 `raw_text()` 保留原始碼拼寫，`definition().case_sensitive()` 傳回該原則。對於 `@Name` 形式的註記，`find_annotations` 和 `matches_name` 查詢須包含 `@` 前綴；`has_annotation` 允許省略。參數名稱、中繼資料鍵和未註冊註記仍區分大小寫。

## 核心執行與限額

<a id="numeric-and-library-limits"></a>

核心透過 `bigint.vs`、`rational.vs` 和 `core.vs` 提供 `std::bigint` 與 `std::rational`，在 VM 內保留精確整數及有理數值；匯出精確文字時使用 `to_string()`。它們與固定寬度 Wasm ABI 參數使用的 JavaScript `bigint` 相互獨立。巢狀泛型型別、繼承的巢狀型別和區域型別遵循核心的作用域及存取規則。

`runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts)` 接受無號 64 位元範圍的 `bigint`，依序限制單次操作的文字位元組數、批次讀取位元組數和分割項目數。零表示不增加主機限制，省略參數會將對應值重設為零。變更對後續原始碼及互動執行、既有編譯程式的下一次執行生效；等待互動輸入期間會拒絕變更。這些限額與受管理記憶體配額、核心數值限額相互獨立，不限制整個 Wasm 堆積。Web API 沿用核心預設的有限數值限額。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
