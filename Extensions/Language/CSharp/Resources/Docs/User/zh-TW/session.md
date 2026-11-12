# C# 工作階段與執行時期

## 執行模式

`Run(VeroScriptRunMode.Script)` 初始化模組並呼叫 `main`；`Eval` 保留增量求值語義。
`EvalOnce` 只執行初始化、不呼叫 `main`，並允許已選定的 JIT 後端執行初始化。
一般工作階段每次執行都初始化新狀態；熱重載工作階段保留已初始化的全域狀態，初始化保留狀態時使用 VM。
此模式要求 `VeroScriptCapabilities.EvalOnce`；舊執行庫會在執行前擲回 `NotSupportedException`。
執行策略與 JIT 後端選擇分別設定。

## 型別化迴圈統計

`NativeTypedLoopExecutions` 統計原生型別化迴圈核心的呼叫次數，包括回報原始碼可見執行階段錯誤後結束的呼叫。它是執行可觀測性計數器，而不是成功計數器。

## 原生 JIT 能力與計數器

`NativeLeafExecutions`、`NativeCfgExecutions`、`BridgeExecutions`、`LinearExecutions`
及 `VmTrampolineExecutions` 統計最近一次執行實際進入一般可執行進入點的次數，包含以錯誤或
控制轉移結束的進入點。對應的 `*Entries` 屬性統計產生的程式碼產物。暖快取可不編譯而執行；
`GetJitCacheStats().CompileAttempts` 仍保留目前快取內容的累積值。
`ScalarNativeFastPathCalls` 統計宿主原生呼叫；直接呼叫與型別化迴圈核心屬於巢狀事件，
不可再加到一般進入點次數中。相容的舊執行階段對未提供的計數器傳回零。這些是彙總統計；
選用的逐函式 JIT 診斷仍屬於原生 Core/C++ API。

`CostModelVmFallbacks` 是有效已編譯路徑因預測成本高於 VM 而刻意選擇的
`Fallbacks` 子集，並非編譯失敗，也不會改變指令碼語意。
`NativeTypedLoopExecutions` 統計經原生後端呼叫的型別化迴圈核心次數，包含回報原始碼
可見執行階段錯誤的呼叫；它不是成功計數器。必須分別檢查 `session.GetJitBackendInfo().CapabilityFlags` 中的
`VeroScriptJitBackendCapabilities.NativeCode` 和
`VeroScriptJitBackendCapabilities.NativeTypedLoops`；不得由 `HostPlatform` 或非零計數器
推斷任何一項能力。

## 部署邊界與執行時期中繼資料

參照 `VeroScript.CSharp` 套件，並部署與作業系統和處理程序架構相符的
`VeroScriptInterop` 原生程式庫。受控套件提供 .NET 8 和 .NET 9 資產，但不會
安裝 .NET 執行時期，也不會替應用程式攜帶原生 RID 資產。若原生程式庫不在
安全的載入目錄中，請使用 `VEROSCRIPT_INTEROP_LIBRARY` 指定絕對路徑；相對路徑
會被拒絕。
請在首次呼叫 SDK 前設定此路徑。後續呼叫使用已選定的原生程式庫。

`VeroScriptRuntime` 提供 `AbiVersion`、`RuntimeVersion`、`HostPlatform`、
`Capabilities`、`CurrentLanguage` 和可用語言 JSON。應使用這些中繼資料檢查相容性，
不要從平台名稱推斷語意。診斷代碼和參數在切換顯示語言後仍保持穩定。

`VeroScriptSession` 管理設定、原始碼、套件、宿主繫結、值與診斷。工作階段與其擁有的值具有建立執行緒親和性，應在同一執行緒使用並透過 `Dispose` 結束。

`VeroScriptConfiguration` 的主要屬性包含 `ImportPaths`、`ImplicitImportPaths`、`ScriptArguments`、`Defines`、`PredefinedMacros`、`StandardLibrarySearchPaths`、`SourceOverlays`、`BytecodeCacheDirectory`、`UseBytecodeCache`、`MaxCallDepth`、`MaxInstructions`、`TimeoutMilliseconds`、`EnableExecutionControl`、`CapabilityPolicyEnabled`、`AllowedScriptCapabilities`、`ExecutionBackend`、`AllowCircularImports` 和 `AllowProjectWideAccess`。

`LoadSource`、`LoadFile`、`LoadPackage`、`Reload`、`ReloadPackage`、`BuildPackageFile`、`Clear` 管理內容；`Run`、`CallFunction`、`CallMethod`、`Construct`、`GetField`、`SetField`、`GetGlobal`、`SetGlobal` 執行或存取腳本。

`SetInput` 會提供下一次執行中標準函式庫輸入函式所使用的緩衝區。

選用呼叫前請使用 `HasFunction` 與 `HasMethod` 檢查目標。需要保留狀態的原始碼重載時，請在呼叫 `Reload` 前明確啟用：

```CSharp
session.HotReloadEnabled = true;
var reloaded = session.Reload();
```

在建立工作階段的執行緒上，於 `Reload`、`ReloadPackage` 或初始化替換程式的執行之後呼叫 `GetReloadStatus()`。`Pending` 表示等待初始化，`RolledBack` 表示已恢復原世代，`AppliedWithErrors` 表示替換程式已提交、隨後執行出錯。可用 `CommittedGeneration` 和 `OperationId` 確認結果。遇到 `ResetRequired` 時，呼叫 `Clear` 並重新載入程式。若無法清除工作階段，則重新建立。查詢保留既有診斷，並要求 `VeroScriptCapabilities.ReloadStatus`。

後端解析器拒絕 `ApplyConfiguration` 時，會擲回包含原生原因的 `VeroScriptException`，並保留原取消原則。`HotReloadEnabled` 設定被拒絕時同樣擲回例外狀況。透過 `GetReloadStatus()` 核對狀態，並依錯誤中的復原指示處理。 取消要求失敗時，例外狀況會回報取消操作本身的原生狀態，未啟用執行控制時，原有原生錯誤仍可由建立工作階段的執行緒查詢。

`VeroScriptRunResult.Success` 表示操作是否成功，`ExitRequested` 和 `ExitCode` 描述腳本退出狀態。使用 `GetDiagnostics`、`GetDiagnosticArguments` 和 `FormatDiagnostic` 讀取結構化診斷。`SetJitPolicy` 與 `GetLastJitStats` 用於熱呼叫門檻、編譯嘗試、快取和預算；統計中的 `DirectNativeCalls` 表示在同一代 JIT 程式碼中繞過虛擬機器值橋、直接到達另一個原生入口的呼叫次數。工作階段可重用程式碼快取與位元組碼快取彼此獨立。若 `VeroScriptRuntime.Capabilities` 包含 `SessionJitCache`，可透過 `SetJitCacheLimits`、`GetJitCacheLimits` 和 `GetJitCacheStats` 管理在符合條件的工作階段執行之間保留的已編譯程式碼。`VeroScriptJitPolicy.ReuseSessionCode` 預設為 `true`。`MaxCompileAttempts` 或 `MaxCachedEntries` 為零表示不限制對應計數；原生程式碼與完整保留承載的位元組上限仍然有效。重用要求有效後端不是 VM，原生函式繫結具有穩定的修訂編號。繫結或回呼變更會使快取失效。啟用執行治理時，可重用治理要求相符的快取內容。預設上限為 8 MiB 原生程式碼與 32 MiB 完整保留承載；設為零會禁止保留對應資源。統計是目前快取內容已穩定的累積值，並非即時或程序層級記憶體計數。變更策略、上限、後端、設定、模組世代、繫結或熱重載/可觀測狀態會使內容失效；達到資源上限會安全回退，不改變指令碼語意；效能最佳化不改變語意。

`MaxInstructions` 和 `TimeoutMilliseconds` 是單次執行的協作式限制；設為 `0` 表示不限制。VM 與符合條件的受控 JIT 路徑維持相同的指令計數、執行限制、取消與能力約束。限制和取消要求不會強制終止已在執行的原生或受控宿主回呼。

啟用執行治理時，`Mixed` 預設使用 VM；應先量測目標工作負載，再透過 `VeroScriptJitPolicy.EnableControlledMixed` 選擇啟用符合條件的受控 JIT。明確選擇 `Jit` 不受此開關控制。不支援或成本模型較偏好 VM 的入口仍使用受治理的 VM 回退。後端 `ControlledExecution` 表示支援能力，`ControlledNativeExecutions` 和 `AotExecutions` 分別記錄實際受控原生入口與 AOT 入口的呼叫次數。

啟用 `EnableExecutionControl` 後，可以呼叫 `RequestCancellation()` 要求取消目前執行。這是執行緒安全的，也是唯一可從非建立執行緒呼叫的工作階段操作。呼叫返回前必須保持工作階段有效；取消只會在執行安全點被觀察到。
成功再次套用設定會重設工作階段的取消令牌，因此取消後可以先重新設定，再準備下一次執行。

啟用 `CapabilityPolicyEnabled` 後，標準函式庫副作用只能使用 `AllowedScriptCapabilities` 中的旗標：`FileRead`、`FileWrite`、`FileDelete`、`EnvironmentRead`、`EnvironmentWrite`、`ConsoleInput`、`ConsoleOutput`、`TaskSpawn` 和 `ProcessDirectoryChange`。`chdir` 同時需要 `EnvironmentWrite` 和 `ProcessDirectoryChange`。此策略只約束內建函式；宿主回呼仍需自行執行授權檢查。關閉策略時，這些內建能力預設全部允許。

## 執行結果

`VeroScriptRunResult.Success` 表示編譯或執行是否成功；指令碼回傳非零結束碼仍屬成功。編譯與指令碼執行失敗通常會以 `Success == false` 回傳。原生所有權拒絕也可能傳回帶結構化診斷的失敗結果，應檢查結果和 `GetDiagnostics()`。其他原生狀態錯誤擲回 `VeroScriptException`；受控引數、生命週期及 ABI 相容性檢查擲回各自對應的受控例外。

## 後端與回呼

`Vm` 只使用直譯器。預設的 `Mixed` 會對支援的函式使用 JIT，其餘回退到 VM。`Jit` 會要求提早編譯，但不代表禁止回退到 VM。附加偵錯器時使用 VM。熱重載在 VM 中初始化保留狀態，符合條件的呼叫仍可使用所選 JIT 後端。

工作階段與建立執行緒綁定。指令碼回呼可能由 VM 工作執行緒同時呼叫，因此請同步共用的受控狀態。輸出、診斷與變更回呼的例外會被攔截；宿主函式、攔截器、泛型工廠和序列化器的例外會轉換為回呼失敗。

## 設定屬性摘要

| 屬性 | 用途 |
| --- | --- |
| `ImportPaths`、`ImplicitImportPaths` | import 搜尋路徑與隱含 import 路徑。 |
| `ScriptArguments` | 傳給 `main(string[] args)` 的引數。 |
| `Defines`、`PredefinedMacros` | 巨集與條件編譯定義。 |
| `StandardLibrarySearchPaths`、`SourceOverlays` | 標準程式庫根目錄與優先使用的記憶體來源。 |
| `BytecodeCacheDirectory`、`UseBytecodeCache` | 位元組碼快取的位置與開關。 |
| `MaxCallDepth` | 遞迴深度上限；0 使用 runtime 預設值。 |
| `MaxInstructions`、`TimeoutMilliseconds` | 每次執行的協作式限制；0 表示無限制。 |
| `EnableExecutionControl` | 啟用協作式取消。 |
| `CapabilityPolicyEnabled`、`AllowedScriptCapabilities` | 標準函式庫副作用策略與允許的能力。 |
| `ExecutionBackend` | 選擇 `Vm`、`Mixed` 或 `Jit`。 |
| `AllowCircularImports`、`AllowProjectWideAccess` | 循環 import 和全專案存取規則。 |

## 獨立 VM 選擇

標準 Interop 產品明確組裝位元組碼 VM。解譯器選擇獨立於 `ExecutionBackend` 與 JIT 選擇。
在工作階段閒置的建立執行緒呼叫 `SetVmBackend(path)`，可選擇匹配的解譯器程式庫檔案或目錄；
null 或空字串清除選擇，取代失敗保留原選擇。`GetVmBackendInfo()` 複製識別、版本、目標、
程式庫路徑、原始 VM 能力及選取/凍結狀態；靜態選擇的路徑為空。VM 能力位元分別代表位元組碼（1）、
執行治理（2）、偵錯（4）、熱重載（8）和清理（16），與處理序能力旗標獨立。

先檢查 `VeroScriptCapabilities.VmBackendSelection`；缺少此功能的相同 ABI 舊執行階段會在呼叫
選用匯出前擲出 `NotSupportedException`。已提交的執行會凍結 VM 領域，包括 JIT 與編譯擴充執行。
`Clear` 與重新載入不會解除凍結；若要更換凍結後的選擇，請建立新工作階段。清除不會還原產品 VM，
並可能使後續執行缺少解譯器。受控中繼資料快照在取代選擇或釋放工作階段後仍可讀取。

## JIT 明確選擇

執行策略與後端選擇分別設定。未提供已安裝模組環境時，Runtime 不提供預設模組：未選後端時 mixed 使用 VM，強制 jit 在執行前失敗。工作階段閒置時，在所有者執行緒使用 `VeroScriptSession.SetJitBackend(path)` 選擇獨立安裝且相容的程式庫檔案或目錄；`SetJitBackend(null)` / `SetJitBackend("")` 清除選擇。取代失敗保留先前後端。`GetJitBackend()` / `GetJitBackendInfo()` 查詢實際身分與原生能力；`backend-services` 僅表示 Core 服務契約。執行治理遵循上文的受控 JIT 策略。檔案/目錄搜尋與 ABI 規則見 JIT 指南。

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/zh-TW/index.md)

```CSharp
using var session = new VeroScriptSession();
session.SetJitBackend("/absolute/path/to/Jit");
session.LoadSource("int main(){ return 0; }");
var result = session.Run();
```

<a id="jit-backend-capabilities"></a>

透過 `session.GetJitBackendInfo().CapabilityFlags` 與 `VeroScriptJitBackendCapabilities` 查詢所選後端能力。`Capabilities` 保留原生位元遮罩，包括未知位元。快照擁有字串副本，替換後端或釋放工作階段後仍可讀取。未選後端時，旗標為 `None`。全域 `VeroScriptRuntime.Capabilities` 表示 Core 服務，不表示所選後端的原生能力。

<a id="diagnostic-translation"></a>

`VeroScriptRuntime.Translate(englishMessage)` 使用行程級目前語言和目錄翻譯診斷訊息。未知訊息原樣傳回並保留內嵌 NUL，空輸入傳回空字串；拒絕 null 輸入和無效 UTF-16。繫結將 `vs_interop_locale_translate` 傳回的模組自有 UTF-8 緩衝區複製為受控字串，並在成功或失敗時透過 `vs_interop_buffer_destroy` 釋放緩衝區。

## 保留呼叫控制代碼的釋放

<a id="callable-disposal"></a>

`VeroScriptCallable.Dispose()` 會立即使受控包裝器失效；若呼叫正在進行，原生銷毀會延後至最後一個活動呼叫返回。釋放不會等待原生執行結束，因此回呼可以等待另一執行緒釋放此包裝器。同一包裝器上的呼叫仍然循序執行。這項生命週期規則不會放寬擁有者執行緒檢查，也不會取消宿主必須從外部循序化工作階段存取的要求。

## 註解服務與診斷嚴重程度

`VeroScriptRuntime.CompileCapabilities` 獨立回報編譯期服務，不屬於執行或 JIT 能力。`AnnotationProcessing` 支援透過 `SetAnnotationProcessingService(nint)` 接入原生擴充提供的有效、不透明服務控制代碼。僅在工作階段閒置時由建立執行緒設定或清除，`0` 表示清除。工作階段複製服務設定，呼叫端可在設定成功後釋放自己的控制代碼；`ApplyConfiguration` 保留服務。處理器程式碼及回呼資料須在最後一次使用結束前保持有效。同 ABI 的舊執行階段回傳 `None`，並在呼叫此操作時擲回 `NotSupportedException`。

`GetDiagnostics()` 保留 `Severity`：`Error`、`Warning`、`Information` 或 `Hint`。診斷數非零不代表編譯失敗：處理器警告、資訊及提示可以與 `Success == true` 同時出現。舊執行階段缺少嚴重程度查詢時，診斷仍可讀取，嚴重程度以 `Error` 回傳。

## 宿主主控台回呼

`VeroScriptSessionOptions.ConsoleQuery` 傳回 `VeroScriptConsoleInfo`，報告輸出終端、顏色/游標/清屏能力與可見欄數和列數。非正尺寸表示未知。控制操作同時需要查詢回呼與 `ConsoleControl`；省略任一項時不支援控制。`ConsoleControl` 接收 `VeroScriptConsoleOperation` 與兩個整數：ANSI 前景/背景色 `0..15`，預設色 `-1`；清行模式 after/before/all 對應 `0/1/2`；從零開始的欄/列；游標可見性 `0/1` 使用第一個參數。未使用的參數為零。

`ConsoleWrite` 接收 `std::console::write` 的精確文字，保留嵌入 NUL，不額外加入換行；省略時仍透過 `Print` 緩衝輸出。僅在同步操作成功完成後傳回 `true`。查詢例外表示無能力，控制例外傳回失敗；寫入回呼傳回 `false` 或擲出例外會使腳本執行失敗。

回呼在工作階段銷毀完成前保持託管參考，可能在 VM 工作執行緒上並行執行，須同步共用狀態。執行階段可能在執行邊界重設樣式、顯示游標，包括失敗或取消之後。不要在回呼中釋放工作階段或重入一般工作階段操作。終端輸入獨立於輸出能力。新回呼需要相符的原生執行階段；同 ABI 的舊執行階段保留行輸出，但忽略尾端主控台回呼。

透過 C# SDK 載入的腳本可在可執行反映與函式簽章中使用基本型別別名，包括巢狀值容器與參考儲存。例如，可按 `array<int64>` 要求 `array<long>`。這不會放寬正負號、整數位元寬度、物件型別身分或參考唯讀性檢查。原始碼與序列化套件執行使用同一套 Core 檢查。

宿主協定回呼（`Clone`、`Equals`、`Hash`、子類工廠）由原生值持有，直到最後一個攜帶協定的值釋放；工作階段釋放後仍可能呼叫，最終釋放也可能發生在其他執行緒。回呼不得存取已釋放的工作階段。設定託管協定回呼需要可選的原生回呼持有入口；同 ABI 的舊執行階段在註冊前擲出 `NotSupportedException`。若協定註冊後持有失敗，工作階段會關閉以保護回呼資料。泛型工廠回復只釋放一次已轉移或託管持有的資料。

## 通用編譯服務與模組環境

`SetCompileProcessingService(nint)` 接入原生 SDK 提供的通用編譯服務，需要 `CompileCapabilities.GeneralProcessing`；`0` 清除服務。呼叫時工作階段必須閒置且位於建立執行緒。控制代碼須在呼叫期間有效；工作階段複製設定後，呼叫方可釋放原控制代碼。一般 `ApplyConfiguration` 保留此服務，處理器程式碼及回呼資料仍須保持到最後一次使用。

`ApplyConfiguration(configuration, moduleEnvironment)` 明確選取原生 SDK 建立的不可變模組環境，需要 `Capabilities.ModuleEnvironment`。`0` 移除環境，單參數多載保留目前環境。僅在建立執行緒的閒置工作階段上呼叫；成功後工作階段持有快照，呼叫方可釋放原控制代碼。更換環境前先 `Clear()`。環境自有繫結目錄不允許本機註冊或匯入目錄，應在原生端準備合併快照；本機繫結衝突會在修改設定和取消狀態之前被拒絕。提供者、繫結及處理器所在原生模組須保持載入直到最後一次使用。舊執行階段缺少相應能力時會擲回 `NotSupportedException`。

## Session 後端選擇參數

`VeroScriptSession.SetVmBackend` / `SetJitBackend`

這些 Session 設定方法接受相符檔案／目錄；提供的模組環境含解析器時，也接受已安裝後端 ID。`auto` 要求環境候選，VM 還可使用保留的產品後備。`none` 或原生空字串停用選擇；`null` / `""` 對應該空要求。新建工作階段未明確選擇時可繼承環境；省略與明確清除不同。重新設定現有工作階段時，省略 VM 選擇會保留既有執行域。明確選擇失敗保留先前後端。僅在建立執行緒的閒置工作階段設定；VM 必須在執行前更換，因為 Clear／reload 不解除執行域固定。JIT 選擇與 VM／Mixed／JIT 執行策略獨立。

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

`VeroScriptRuntime.CompileCapabilities` 使用 `VeroScriptCompileCapabilities`，與執行能力分開；不提供該查詢的舊同 ABI runtime 傳回 `VeroScriptCompileCapabilities.None`。`GetJitBackendInfo().CapabilityFlags` 使用包含 `DirectScalarCalls` 的 `VeroScriptJitBackendCapabilities`，表示後端支援，不證明某入口實際執行了原生程式碼。各結構化診斷由 `VeroScriptDiagnostic.Severity` 分類。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](session.md)
