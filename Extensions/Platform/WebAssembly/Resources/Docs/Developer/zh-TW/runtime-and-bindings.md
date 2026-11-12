# Web 執行時與繫結

也保留單行正確寫法：`void main(string[] args)` 與 `int main(string[] args)`。
## 執行階段處理站

`createVeroScriptRuntime()` 是面向 interactive input 的 Asyncify-capable factory。 `createFastVeroScriptRuntime()` 省略 whole-program Asyncify stack instrumentation，適用於所有 input 都在 run 前提供的 preferred path。capability flag 允許時，兩種 factory 暴露相同 language/result contract。

`runSource` 在 module creation 後 synchronous。 `runSourceInteractive` 在 input provider 等待 logical line 時保持 VM/script state suspend。一個 Wasm module 只能有一個 active interactive run。provider exception 會在 runtime 安全離開 input wait 後 reject promise。

請參閱[WebAssembly/WASM SDK](../../User/zh-TW/index.md#runtime-factory-configuration)。

## input 與 result

run 接收 file label、optional virtual file、standard input、script argument 與選擇的 `vm`、`mixed`、`jit` backend。支援 `void main(string[]
args)` 和 `int main(string[]
args)`。non-zero integer result 仍是 successful run，並作為 `exitCode` 返回。embedded NUL input 會被 reject。

browser baseline 不 advertise script-level worker thread。unbounded program 透過 terminate owning Web Worker 停止。independent program 應使用 independent runtime module 或 worker。

請參閱[WebAssembly ABI、產物與發行契約 — 程式與輸入能力](release-contract.md#web-execution-policy)。

欄位與方法的可執行反射可在 `vm`、`mixed` 後端執行。發行的瀏覽器模組未啟用 pthread，因此新的 callable 工作與原有工作均無法啟動。`is_file`、`is_directory`、`remove_dir` 只存取模組的虛擬檔案系統（包括內建標準程式庫檔案），無法存取訪客的本機檔案。

## 已編譯程式

`compileSource` 接收 source file label 與 in-memory virtual file，返回 result 和 optional owned `VeroScriptProgram`。program handle 可用新的 input、argument、backend selection 重複 run 而無需 reparse source graph。每個 run 使用 fresh execution state；compilation diagnostic 保留在 compile result 中。

`VeroScriptProgram.dispose()` release native handle，且在 SDK boundary idempotent。runtime track live program 並在 runtime disposal 時 release。interactive compiled execution 需要 Asyncify artifact、`readLine` provider，並且同一 WebAssembly module 沒有其他 active interactive run。

## complete source 與 compiled-program surface

TypeScript SDK 將 runSource map 到 `vero_web_run_source_with_context`，包含 script argument、selected backend 和 paired in-memory virtual file。compileSource map 到 `vero_web_compile_source_with_context`；success result own VeroScriptProgram。VeroScriptProgram.run 使用 `vero_web_run_program`，runInteractive 使用 Asyncify-only `vero_web_run_program_interactive`。program run 獲得 fresh VM state，可選擇 input、argument、backend。

native C++ surface 提供 `FRuntime::RunSource`、`RunSourceWithInputStream`、`CheckSource`、`SetLanguage`、`GetAvailableLanguages`。C ABI 另外暴露 argument、option、context、compile、program、interactive、context-check entry point。C ABI array/string 僅在 call 期間 borrowed；returned JSON/result pointer 遵循 runtime-owned lifetime rule。VeroScriptResult 將 backend、output、error output、diagnostic、timing、program handle、per-run JIT statistic 作為 separate field。

## result 與 JIT field mapping

`VeroScriptResult.jit` 在檢查、編譯、API 錯誤或原始碼載入失敗時為 null。已載入的執行即使在指令碼初始化前被拒絕，也會回傳單次執行的計數，例如未選擇後端時明確要求 `jit`。發行模組的所有計數均為零。`directNativeCalls` 和 `compileAttempts` 是統計值，不是語言或 ABI 版本。

## 受管理記憶體

請參閱[WebAssembly/WASM SDK — 受管理記憶體](../../User/zh-TW/index.md#managed-memory-contract)。

## 失敗清理與快照

輸入橋接層配置失敗或超出 Wasm32 大小範圍時，會恢復原生呼叫堆疊並傳回 API 失敗結果，模組仍可用於後續執行。故障注入涵蓋 `vm` 和 `mixed` 的原始碼及已編譯執行，並驗證每次失敗後的恢復。正常 EOF 行為保持一致。

失敗清理保持所有權完整：複製互動輸入行時即使拋出例外，也會釋放輸入配置；已編譯程式的結果 JSON 若無法產生，會在控制代碼傳回宿主前移除該程式。唯讀租約產生一次獨立快照，`toArray` 直接傳回該快照，避免再次完整複製。迴歸涵蓋快照的獨立生命週期，以及已編譯執行失敗後受管理記憶體全域變數的重新初始化。

## 核心相容與診斷

請參閱[WebAssembly/WASM SDK — 核心相容與診斷](../../User/zh-TW/index.md#compiler-diagnostics)。

## 互動輸入所有權

互動呼叫在讀取選項後重新檢查模組狀態。重入要求或執行時期已銷毀的要求會在接管輸入提供器前被拒絕，正在執行的程式及其後續輸入保持有效。

字串指標表按實際編組項目數配置，避免自訂或動態陣列迭代寫入按初始長度配置的較小區域。同步和非同步輔助函式均檢查 Wasm32 邊界。執行時期銷毀直接走訪受追蹤程式，不再建立第二份陣列。

請參閱[WebAssembly/WASM SDK — 互動輸入所有權](../../User/zh-TW/index.md#input-provider-failures)。

## 主控台輸出與能力

請參閱[WebAssembly/WASM SDK — 主控台輸出與能力](../../User/zh-TW/index.md#console-capabilities)。

即使終端能力不可用，無效的顏色名稱、清屏模式、游標座標、參數型別或參數數量仍會產生診斷。請求被拒絕後，執行階段仍可重複使用。擷取文字會在執行完成後透過 `VeroScriptResult` 傳回。

## 目前語言功能

請參閱[WebAssembly/WASM SDK — 目前語言功能](../../User/zh-TW/index.md#compiler-and-application-services)。

已發佈的 C ABI 和 TypeScript 選項未開放 `FModuleEnvironment`、原生擴充註冊、資源提供者或 `FEngineConfig::I18nServices`。核心允許自訂 C++ WebAssembly 建置採用靜態原生註冊；已發佈引擎不能載入桌面動態程式庫。這些宿主整合需要另行設定建置產物。現有原始碼覆寫與程式所有權合約保持有效。

## 註記名稱比對

請參閱[WebAssembly/WASM SDK — 註記名稱比對](../../User/zh-TW/index.md#annotation-name-matching)。

接受範圍重疊且定義衝突的註解註冊會產生編譯診斷。兩個引擎在檢查、原始碼執行和編譯程式建立時採用相同原則。已編譯程式保留其定義原則，修改後須重新編譯。Web C ABI 和 TypeScript 選項不變。原始碼中繼資料契約變更後，舊開發版套件與快取須由原始碼重新產生；SDK 程式控制代碼仍僅保存在記憶體中。

## 核心執行與限額

請參閱[WebAssembly/WASM SDK — 核心執行與限額](../../User/zh-TW/index.md#numeric-and-library-limits)。

重複執行編譯程式可重複使用內部工作區，但每次執行均使用新的腳本全域變數、區域變數和參數，失敗後亦然。核心變更後應重新建置兩個 Wasm 引擎並執行既有 SDK 發行檢查。舊 SDK 驗證記錄僅適用於其記錄的建置產物。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/runtime-and-bindings.md) · [fr-FR](../fr-FR/runtime-and-bindings.md) · [ja-JP](../ja-JP/runtime-and-bindings.md) · [ko-KR](../ko-KR/runtime-and-bindings.md) · [zh-CN](../zh-CN/runtime-and-bindings.md) · [zh-TW](runtime-and-bindings.md)
