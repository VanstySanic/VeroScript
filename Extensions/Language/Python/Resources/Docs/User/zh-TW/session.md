# Python 工作階段與執行

清空工作階段或關閉熱重載可能執行指令碼解構函式和 Python 回呼。回呼例外會立即以 `CallbackError` 回報；已完成的清理或設定變更仍然生效。

在操作或回呼執行期間關閉仍然有效的工作階段會擲出 `Error`，工作階段仍可繼續使用；應在外層操作返回後關閉。關閉工作階段的解構過程中，`closed` 已為真，重複呼叫 `close()` 不會再次釋放資源。

## 建立工作階段與解讀結果

建議使用 `Session(...)` 內容管理器，並在建立執行緒關閉工作階段。
`load_source`、`load_file`、`load_package` 和 `reload_package` 回傳
`RunResult`；`run(RunMode.SCRIPT)` 會呼叫 `main`，`RunMode.EVAL` 使用增量求值路徑。

結果包含 `success`、`exit_requested`、`exit_code` 和 `diagnostics`。非零退出碼
仍可能表示執行成功。回傳值呼叫失敗時會擲出 `VeroScriptError`；請檢查結構化的
`VeroScriptError.result`。傳輸、型別、執行緒和回呼錯誤使用模組對應的專用例外。

## 重要公開名稱

狀態存取的精確名稱是 `is_loaded`、`get_global(name)` 與 `set_global(name, value)`。
`load_source`、`load_file`、`load_package`、`reload_package` 回傳 `RunResult`；
`call`、`construct`、`call_method`、`get_field`、`set_field` 失敗時會拋出
`VeroScriptError`。

`Session` 管理匯入路徑、宿主繫結、輸入、診斷與生命週期。工作階段建立後應在建立執行緒使用，並呼叫 `close()` 或離開內容管理器。

建構 `Session` 時可設定 `backend`（`Backend.VM`、`Backend.MIXED`、`Backend.JIT`）、`import_paths`、`implicit_import_paths`、`script_args`、`defines`、`stdlib_search_paths`、`source_overlays`、`bytecode_cache_directory`、`use_bytecode_cache`、`max_call_depth`、`max_instructions`、`timeout_milliseconds`、`enable_execution_control`、`capability_policy_enabled`、`allowed_script_capabilities`、`allow_circular_imports`、`allow_project_wide_access`、`hot_reload_enabled`、`print_callback` 與 `diagnostic_callback`。

`load_source`、`load_file` 和 `load_package` 載入內容；`reload` 和 `reload_package` 重新載入；`run` 執行入口；`call`、`construct`、`call_method`、`get_field`、`set_field`、`get_global`、`set_global` 提供結構化呼叫與存取。

`max_instructions` 和 `timeout_milliseconds` 是單次執行的協作式限制；設為 `0` 表示不限制。設定任一限制或 `enable_execution_control=True` 會使用 VM 安全點路徑，即使請求的後端是 `MIXED` 或 `JIT`。限制和取消要求不會強制終止已在執行的宿主回呼。

啟用 `enable_execution_control=True` 後，可以從任意 Python 執行緒呼叫 `request_cancellation()` 要求取消目前執行。要求返回前必須保持工作階段有效。取消要求會保留在該工作階段中；開始下一次執行前應建立新的 `Session`。

啟用 `capability_policy_enabled=True` 後，標準函式庫副作用只能使用 `allowed_script_capabilities` 授予的能力：檔案讀/寫/刪除、環境讀/寫、主控台輸入/輸出、建立工作和程序目錄變更。`chdir` 同時需要 `ENVIRONMENT_WRITE` 和 `PROCESS_DIRECTORY_CHANGE`。此策略只約束內建函式；宿主回呼仍需自行執行授權檢查。關閉策略時，這些內建能力預設全部允許。

`RunResult` 同時提供成功狀態、退出要求、退出碼和結構化診斷。`set_jit_policy` 可調整熱呼叫門檻、編譯嘗試上限、快取項目與預先編譯預算；`jit_stats` 提供編譯、命中、回退與快取統計，其中包括 `direct_native_calls`，表示在同一代 JIT 程式碼中繞過虛擬機器值橋、直接到達另一個原生入口的呼叫次數。

`jit_stats` 是最近一次呼叫的獨立快照。實際執行入口由
`native_leaf_executions`、`native_cfg_executions`、`bridge_executions`、
`linear_executions`、`vm_trampoline_executions`、`controlled_native_executions`
和 `aot_executions` 記錄，包括以錯誤或控制轉移結束的入口。編譯次數與快取入口數
分開統計；`direct_native_calls`、純量快速路徑和型別化迴圈屬於巢狀事件，不可再作為
獨立函式入口相加。VM 執行時這些執行入口計數為零。每次新呼叫都會重設它們，即使
重用已編譯程式碼；`clear()` 會重設工作階段快照。`jit_cache_stats` 保留可重用
上下文的累計編譯及資源統計。

<a id="session-jit-cache"></a>

`JitCacheLimits` 管理符合條件的一般 `Session.call`／原生 `CallFunction` 呼叫間保留的程式碼，與位元組碼快取分開。`JitPolicy.reuse_session_code` 預設 `True`。`max_compile_attempts` 或 `max_cached_entries` 為零表示計數不限，在保留內容中亦然。重用需要相容的 JIT 工廠及允許 JIT 的有效執行策略。持久工作階段與穩定回呼可重用程式碼。原生繫結目錄須提供執行階段修訂號。執行治理要求一致時，受控原生執行亦可重用內容。

`max_native_bytes` 預設 8 MiB，`max_retained_payload_bytes` 預設 32 MiB；零禁止保留該項資源。透過 `session.set_jit_cache_limits(...)`、`session.jit_cache_limits`、`session.jit_cache_stats` 設定與觀察。統計是穩定的內容累計值，不是即時或全程序記憶體計量。策略、限制、設定、模組、後端或可觀測宿主邊界變動會使內容失效，達到資源限制時安全回退。

受治理的 `Backend.MIXED` 預設使用 VM。`session.set_jit_policy(JitPolicy(enable_controlled_mixed=True))` 選擇相容的受控原生執行；明確 `Backend.JIT` 與此開關獨立。不支援的入口在執行前回退；偵錯、保留狀態的初始化和有界清理使用 VM。

`set_input` 設定腳本輸入，`set_hot_reload_enabled` 切換熱重載。容器回傳快照，Python 列表或字典的原地修改不會寫回腳本容器。不要在多個執行緒間共享可變工作階段。


呼叫可能不存在的函式或方法前，可使用 `has_function(name, argument_count)` 和 `has_method(instance, name, argument_count)` 檢查。依 Core 執行契約，讀寫全域變數需要透過 `hot_reload_enabled=True` 保留狀態。

讀取 `session.reload_status` 前，可用 `CAPABILITIES & RuntimeCapabilities.RELOAD_STATUS` 確認支援。請在建立工作階段的執行緒讀取。`ReloadStatus` 快照以 `operation_id` 識別重載，分別記錄 `current_generation` 與 `committed_generation`。`PENDING` 表示候選程式已暫存並等待初始化。`REJECTED` 保留原程式，`ROLLED_BACK` 表示已還原原程式。`APPLIED_WITH_ERRORS` 表示候選程式已提交，後續執行或清理發生錯誤。`RESET_REQUIRED` 會阻止後續執行。若 `clear()` 無法重設，請重建工作階段。宿主仍持有外部值時，暫存可能遭拒並保留上次狀態。

## VM 選擇

標準 Python 套件內建靜態位元組碼 VM，一般工作階段無須另裝 VM 動態程式庫。`RuntimeCapabilities.VM_BACKEND_SELECTION` 表示支援選擇介面。可在首次執行前，由建立工作階段的執行緒於閒置時呼叫 `set_vm_backend(path)`，選擇相容的 VM 檔案或目錄；`None` 或空字串清除選擇。替換失敗會保留原 VM。開始執行後選擇即凍結，`clear()` 亦不解除；需要更換時建立新工作階段。此選擇與 `Backend.VM`、`MIXED`、`JIT` 執行策略相互獨立。

`get_vm_backend_info()` 傳回擁有自身資料的 `VmBackendInfo` 快照，包含 `id`、`version`、`target`、`library_path`、`capabilities`、`selected`、`frozen`。靜態 VM 的 `library_path` 為空，可用 `selected` 區分靜態 VM 與未選擇 VM。替換後端或關閉工作階段後，既有快照仍有效。

## JIT 明確選擇

執行策略與後端選擇分別設定。未提供已安裝模組環境時，Runtime 不提供預設模組：未選後端時 mixed 使用 VM，強制 jit 在執行前失敗。工作階段閒置時，在所有者執行緒使用 `Session.set_jit_backend(path)` 選擇獨立安裝且相容的程式庫檔案或目錄；`set_jit_backend(None)` / `set_jit_backend("")` 清除選擇。取代失敗保留先前後端。`get_jit_backend()` / `get_jit_backend_info()` 查詢實際身分與原生能力；`backend-services` 僅表示 Core 服務契約。持久工作階段中的合資格呼叫可使用選定後端。檔案/目錄搜尋與 ABI 規則見 JIT 指南。

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/zh-TW/index.md)

```python
with Session(backend=Backend.MIXED) as session:
	session.set_jit_backend("/absolute/path/to/Jit")
	session.load_source("int main(){ return 0; }")
	result = session.run()
```

## 一次性求值與後端能力

一般會話中，`run(RunMode.EVAL_ONCE)` 每次呼叫執行一次模組初始化，不呼叫 `main`，允許已選擇的 JIT 後端編譯初始化程式碼。`RunMode.EVAL` 保持增量求值路徑；`RunMode.SCRIPT` 還會呼叫 `main`。`RuntimeCapabilities.EVAL_ONCE` 表示執行庫支援該模式。熱重載工作階段保留已初始化的全域狀態，保留狀態的初始化使用 VM。

`get_jit_backend_info()` 回傳擁有自身資料的 `JitBackendInfo` 快照。其 `capabilities` 使用 `JitBackendCapabilities`：`NATIVE_CODE`、`PORTABLE_EXECUTION`、`NATIVE_TYPED_LOOPS`、`DIRECT_SCALAR_CALLS`。快照描述實際選擇的模組；`CAPABILITIES` 描述 Core 服務能力。後端替換或 Session 關閉後，已有快照仍然有效。

## 編譯服務與診斷嚴重程度

`COMPILE_CAPABILITIES` 與 `CompileCapabilities.ANNOTATION_PROCESSING` 描述編譯服務，獨立於 `CAPABILITIES` 和所選 JIT 後端。`Diagnostic.severity` 使用 `DiagnosticSeverity` 列舉：`ERROR`、`WARNING`、`INFORMATION`、`HINT`。請以 `RunResult.success` 判斷操作是否成功；非空診斷清單也可能僅包含非致命資訊。

`session.set_annotation_processing_service(handle)` 在工作階段閒置時接入原生 SDK 擴充建立的有效不透明控制代碼。工作階段複製服務設定，接入後產生端可釋放自己的控制代碼；處理器所在原生模組及回呼資料須保留至服務清除或工作階段關閉。`None` 或 `0` 清除服務，任意整數位址不是有效服務控制代碼。`register_annotation(...)` 僅宣告註記詞彙，不安裝編譯期處理器。

## 宿主控制台整合

`Session(console_query=..., console_control=..., console_write=...)` 可設定終端回呼。查詢回傳 `ConsoleInfo`，尺寸 0 表示未知；控制回呼接收 `ConsoleOperation` 及兩個整數：ANSI 色彩 0..15／預設 -1、清行 after/before/all=0/1/2、從 0 開始的欄／列、顯示 0/1。同步成功回傳 `True`。文字回呼接收不加換行的精確文字並回傳 `bool`，省略則保留緩衝列輸出。回呼須存活至關閉結束並同步工作執行緒，例外以 `CallbackError` 回報。終端狀態共享，請協調完整操作序列。

## 已解析模組環境與通用編譯處理

`Session(module_environment=handle)` 接收相符原生 SDK 中 `VeroScript::Interop::CreateModuleEnvironment` 建立的不透明快照控制代碼。工作階段持有不可變環境，建構成功後產生者可釋放自己的控制代碼。環境統一提供原始碼歸屬、精確匯入對應、資源、編譯服務與執行相依性；需要標準函式庫時，應由快照提供搜尋根及其歸屬，Python 在此模式下不自動加入套件內的標準函式庫根。替換快照時建立新工作階段。環境擁有繫結目錄時禁止本機登錄，應先組合繫結再發布快照。`None` 或 `0` 使用一般設定；任意整數位址無效。透過 `RuntimeCapabilities.MODULE_ENVIRONMENT` 查詢支援情況。

`set_compile_processing_service(handle)` 接入原生 SDK 的通用編譯處理服務，遵循註解服務相同的閒置、建立執行緒與生命週期要求。`None` 或 `0` 清除服務。對應能力為 `CompileCapabilities.GENERAL_PROCESSING`；指令碼編譯擴充使用 `set_script_compile_extensions(...)` 設定。

## 閉包與區域靜態儲存

腳本函式值支援初始化擷取與 `mutable`。`Value.clone()` 和 `retain_callable()`
共享原有閉包環境；再次求值同一 lambda 運算式會建立獨立環境。參照擷取會在建立它的
腳本函式返回後保留其儲存空間。關閉工作階段會使保留呼叫物件失效。

區域 `static` 變數屬於執行狀態。一般 `call()` 建立新的執行狀態；
`hot_reload_enabled=True` 會在呼叫之間保留狀態。關閉工作階段會釋放其持有的靜態狀態。
獨立 [Python AOT](aot.md) 拒絕原始碼 lambda 和區域 `static` 宣告。

## Session 後端選擇參數

`Session.set_vm_backend` / `set_jit_backend`

這些 Session 設定方法接受相符檔案／目錄；提供的模組環境含解析器時，也接受已安裝後端 ID。`auto` 要求環境候選，VM 還可使用保留的產品後備。`none` 或原生空字串停用選擇；`None` / `""` 對應該空要求。新建工作階段未明確選擇時可繼承環境；省略與明確清除不同。重新設定現有工作階段時，省略 VM 選擇會保留既有執行域。明確選擇失敗保留先前後端。僅在建立執行緒的閒置工作階段設定；VM 必須在執行前更換，因為 Clear／reload 不解除執行域固定。JIT 選擇與 VM／Mixed／JIT 執行策略獨立。

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](session.md)
