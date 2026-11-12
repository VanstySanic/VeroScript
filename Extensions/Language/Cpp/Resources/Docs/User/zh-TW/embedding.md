# 宿主整合

VeroScript 可以嵌入 C++ 宿主程式。宿主通常透過 `VeroScriptEngine` 使用一次性的檢查或執行入口，透過 `VeroScriptSession` 管理長期存在的模組、綁定、執行狀態和熱重載。

## 明確選擇 VM

Core 不提供隱含的直譯器。以下範例使用獨立分發的靜態位元組碼 VM：尋找兩個 SDK 套件、連結兩個目標，並為每份引擎設定或 Session 明確裝配。僅連結程式庫不會自動選擇後端。

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
```

動態載入時，使用 `FEngineConfig::VmBackendPath` 或 `Session::SetVmBackendPath` 指定已部署的 VM 檔案或目錄；工廠和路徑互斥。開始執行後選擇即固定，`Clear()` 不解除固定。除非 `ScriptCompileExtensions` 另外指定工廠或路徑，編譯腳本會繼承所選 VM。不執行編譯腳本的一般編譯不需要 VM。

對既有 Session，`FEngineConfig` 未指定 VM 工廠及路徑時保留目前選擇；凍結前可用 `bClearVmBackend=true` 明確清除。工廠、路徑與清除三者互斥。`Clear()` 重設已載入程式，不會解除執行後 VM 域的凍結；此後若需更換直譯器，請建立新 Session。

## 高層引擎入口

宿主需要包含 SDK 提供的 `VeroScriptEngine.h` 公共頭檔。 `VeroScriptEngine` 提供原始碼和檔案兩類入口：

- `CheckSource` / `CheckFile`：只進行詞法、語法和語意檢查；
- `RunSource` / `RunFile`：載入並執行腳本。

這些入口可以接收匯入路徑、腳本參數、巨集、標準庫搜尋路徑、位元組碼快取目錄、執行後端和綁定目錄等設定。

`FEngineConfig` 的公開設定還包括結構化預定義巨集、隱式匯入路徑、記憶體來源覆蓋、最大呼叫深度、循環匯入開關、專案範圍存取開關和輸入流。嵌入式 `RunFile` 預設不啟用位元組碼快取；需要快取時應明確設定 `bUseBytecodeCache` 與 `BytecodeCacheDirectory`。執行後端是 `Vm`、`Mixed` 或 `Jit`，預設是 `Mixed`。

位元組碼快取包含 Core 實作與公開 API 的身分識別。即使產品和位元組碼版本不變，Core 程式碼變化也會使舊快取失效；同一建置的重複執行仍會重用快取。

`ExecutionLimits.MaxInstructions` 和 `ExecutionLimits.TimeoutMilliseconds` 是單次執行的協作式限制；`0` 表示不限制。`ExecutionControl` 接受共用的 `FExecutionControl`，宿主可以從其他執行緒呼叫 `RequestCancel()` 要求取消。Core 對直譯和受支援的原生執行實施一致的指令、取消及權限檢查。受控 `Mixed` 預設使用 VM；Session 可透過 `SetJitPolicy` 設定 `FJitPolicy::bEnableControlledMixed=true`，啟用動態原生執行。明確要求的 `Jit` 和已選靜態 Core AOT 不受此開關限制；原生程式碼缺少所需治理能力時，會在進入前退回 VM。已在執行的原生回呼不會被強制終止。控制令牌不會自動重設；再次執行前應呼叫 `Reset()`。

`Capabilities` 控制標準函式庫副作用，包含檔案讀/寫/刪除、環境讀/寫、主控台輸入/輸出和建立工作。預設允許全部能力；限制只作用於內建函式，註冊回呼中的授權仍由宿主負責。
`bAllowProcessDirectoryChange` 可單獨禁止 `chdir` 修改整個宿主程序的工作目錄；`chdir` 同時要求環境寫入權限。

入口傳回 `FRunResult`。 `bSuccess` 表示編譯/執行是否正常完成；成功的 `int main` 或 `exit(n)` 可以產生非零 `ExitCode`。 `bExitRequested` 只表示腳本明確呼叫了 `exit(n)`，包含 `exit(0)`；普通的 `int main` 回傳不會設定該標誌。失敗原因可從 `Error` 和 `Diagnostics` 讀取。

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptEngine.h"

#include <iostream>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	config.ScriptArgs = {"alpha", "beta"};

	const VeroScript::FRunResult result = VeroScript::VeroScriptEngine::RunFile(
		"main.vs",
		host,
		config);

	if (!result.bSuccess)
	{
		return result.ExitCode;
	}
	return result.ExitCode;
}
```

`ERunMode` 與執行後端相互獨立：`Eval` 使用增量/REPL 初始化路徑，`EvalOnce` 對來源執行一次性評估，`Script` 在載入後繼續呼叫 `main`。偵錯、保留初始化區間和有界清理使用 VM；持久會話的一般函式可以使用所選 JIT。

## 嵌入式 VM 偵錯器

`VeroScriptDebugger` 是直接擁有 `VeroScriptVm` 的宿主可使用的同步處理程序內偵錯器。
它與 DAP 分開：不會建立線連線，也不會編碼 JSON。請包含 `VeroScriptCompile.h`、
`VeroScriptDebugger.h` 和 `VeroScriptVm.h`，將來源或檔案編譯為 `FBytecodeModule`，再透過
`SetDebugger` 連接非擁有式偵錯器並執行模組。

使用位元組碼直譯器時，請包含 `VeroScriptBytecodeVmAssembly.h`，並在執行前呼叫
`VeroScript::BytecodeVm::Configure(vm)`。

`SetBreakpoint` 使用從 1 開始的來源行號。空白檔案標籤會比對所有檔案；非空白標籤會比對
完整標籤或檔名。條件中斷點會在頂層堆疊框架中計算運算式。運算式計算失敗或條件為 false
時會略過暫停；布林真值、非零數字和其他非空結果會視為 true。

暫停回呼會在 VM 暫停期間同步執行。使用 `GetStackTrace`、`GetLocals` 和 `GetChildren`
檢查狀態。`Evaluate` 會在選取的堆疊框架中以唯讀方式計算運算式；要透過偵錯變數寫入值，
請使用 `SetLocal` 或 `SetChild`。在回呼中呼叫 `SetRunMode`，可選擇 `Continue`、`StepIn`、
`StepOver` 或 `StepOut`。

`RequestPause()` 是執行緒安全的協作式要求，會在 VM 安全點被觀察，並以
`EDebugStopReason::Pause` 回報；它不會強制中斷正在執行的宿主回呼。如果先前的暫停要求可能
仍在等待，請在重用偵錯器前呼叫 `ResetPauseRequest()`。`SetDebugger` 不會移轉所有權：VM
執行期間必須保持偵錯器有效，使用偵錯器的回呼期間也必須保持 VM 有效。連接偵錯器會強制
有效執行後端為 `Vm`，並使 JIT 快取失效。

需要非同步編輯器整合時，請使用 [Tooling](../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/index.md) 和 [偵錯](../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/debugging.md)
中介紹的 `VeroScriptDebugSession` 或 DAP 配接器。

## 診斷本地化

引擎入口會自動初始化核心本地化：優先使用 `VERO_LOCALIZATION_DIR`，隨後按照[診斷本地化](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/localization.md)中的部署搜尋順序尋找目錄，並使用 `VERO_LANG` 或系統語言選擇文化。宿主不需要為了顯示英文診斷額外呼叫初始化函式。

需要明確控制語言或目錄時，可以直接使用 `VeroScript::Locale`：

```cpp
#include "VeroScriptLocale.h"

#include <iostream>
#include <string>

int main()
{
	std::string error;
	if (!VeroScript::Locale::LoadCatalogDirectory("localization", &error))
	{
		std::cerr << error << '\n';
		return 1;
	}

	if (!VeroScript::Locale::TrySetLanguageFromTag("zh-CN"))
	{
		return 2;
	}

	const std::string language = VeroScript::Locale::GetLanguageTag();
	std::cout << language << '\n';
	return 0;
}
```

`LoadCatalogDirectory` 會取代處理程序層級目錄樹；函式庫或標準函式庫可以使用 `MergeCatalogDirectory` 追加同級 `localization/` 目錄，預設不覆寫已經存在的訊息 ID。語言和目錄操作在核心中是執行緒安全的，但 `VeroScriptSession` 本身仍不是執行緒安全物件；宿主應明確語言切換的時機，避免不同請求共享一個處理程序時互相改寫顯示語言。訊息 ID、佔位符和英文回退規則見[診斷本地化](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/localization.md)。

## 長期會話

`VeroScriptSession` 適合編輯器、遊戲或宿主工具中的長期腳本生命週期。會話可以：

- 載入、執行、重新載入和清除模組；
- 註冊原生函式、宿主類型、欄位、屬性、方法、運算子和泛型宿主類型；
- 設定輸出、診斷、呼叫攔截和狀態變更回呼；
- 選擇 VM、混合或 JIT 後端並設定位元組碼快取；
- 設定協作式指令/時間限制、取消和標準函式庫能力；
- 明確啟用狀態保留式熱重載。

會話呼叫本身不是內部同步的。宿主應使用一個控制執行緒呼叫 `Load`、`Run`、`Reload` 和 `Clear`，並在運作不活躍時請求重載。

典型的長期會話順序如下：

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <string>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::VeroScriptSession session(host);

	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	std::string configError;
	if (!session.ApplyConfig(config, &configError))
	{
		std::cerr << configError << '\n';
		return 1;
	}

	const VeroScript::FRunResult load = session.LoadFile("main.vs");
	if (!load.bSuccess)
	{
		return load.ExitCode;
	}

	const VeroScript::FRunResult run = session.Run();
	return run.ExitCode;
}
```

<a id="apply-config-result"></a>

`ApplyConfig` 拒絕設定時傳回 `false`，可透過 `OutError` 取得原因。載入或執行前應檢查結果。驗證拒絕發生在設定提交之前；配置記憶體或複製回呼產生的例外可能在部分變更後傳播，不保證回復。

`IEngineHost` 必須比會話存活；它接收 `print` 和診斷行回呼。 `RegisterNative`、`RegisterHostType` 和 `RegisterHostMember` 必須在 `Load` 之前完成，這樣編譯器才能解析腳本中的宿主宣告。註冊表在 `Reload` 和 `Clear` 後仍然存在；綁定目錄可以透過 `GetBindingCatalog` 讀取，或使用 `ExportBindingsToFile` 匯出供 IDE、LSP 和 DAP 使用。

會話也提供 `CompileFile`/`CommitCompiledFile`、`ReloadCompiledModule`、`LoadPackage` 和 `ReloadPackage`，適合編輯器、套件載入器和熱更新系統將「編譯」和「提交」分成兩個階段。套件的 `CanonicalSignedBytes` 必須在跨越信任邊界前由宿主驗證簽署。

`BuildPackage`／`ReadPackage` 與 Session 的 `LoadPackage`／`ReloadPackage` 處理 Core 映像（`.vslib` 或原始已編譯應用映像）。標準 ZIP `.vsapp` 是外層應用容器，請使用 Application 服務及 `LoadApplication`／`ReloadApplication`。將原始 Core 映像儲存為 `.vsapp` 檔名不會使其成為標準容器。

## 綁定邊界

腳本只能呼叫宿主明確註冊並且編譯器能夠解析的函式或成員。綁定描述需要提供參數、傳回值以及宿主類型、容器元素或鍵值類型資訊；註冊重複名稱、空回呼或與內建名稱衝突的綁定會被拒絕。

宿主結構體的值複製需要對應的克隆回呼；宿主物件控制代碼則遵循宿主註冊的生命週期和所有權約定。綁定 API 的完整宣告以 `VeroScriptCpp` 公共頭檔為準。

公開的呼叫和擴充能力還包括：

- `CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`ConstructObject`、`GetField`/`SetField` 及全域量讀寫；
- 帶有 `ref` 參數的呼叫寫回可變參數，失敗或退出時不寫回；
- `RetainCallable`/`InvokeCallable`，用來保留腳本函式值到目前宿主回呼之外；
- `SetInstanceCallInterceptor`、`SetMutationCallback` 及 `ApplyClassDefaults`；
- `RegisterGenericHostTypeTemplate`/`InstantiateGenericHostType`、型別別名、外部序列化和物件參考枚舉；
- `SetJitPolicy`, `SetJitCacheLimits`, `GetJitCacheStats`, `GetLastJitStats`: 重用要求已選擇工廠、有效策略不是 VM，並啟用 `FJitPolicy::bReuseSessionCode`。符合條件的 `Run`、`CallFunction`、`CallMethod` 和保留 callable 呼叫共享同代快取及編譯配額。`MaxCompileAttempts` 或 `MaxCachedEntries` 為零表示無限制，不表示停用。持久執行和穩定回呼可以重用程式碼；原生目錄必須透過 `IHostBindingCatalog::GetRevision()` 發布執行期修訂。後端具備所需治理能力時，受控原生執行也能重用程式碼；Session 會跨 VM 保留該能力上下文。組態、策略、後端、模組或繫結變化會使保留程式碼失效；重載失敗保留原程式。預設上限為 8 MiB 可執行程式碼、32 MiB 保留承載。`GetJitCacheStats` 是已穩定的同代計量，`GetLastJitStats` 描述最近呼叫，不是程序記憶體測量。

`VeroScriptSession` 不在內部同步執行緒存取。一般會話操作和最終銷毀應在建立會話的執行緒執行。宿主回呼可能來自 VM 工作執行緒，須自行同步外部狀態。`RetainCallable` 預設使用 `bOwnerThreadOnly = true`；即使宿主已將存取序列化，從其他執行緒呼叫保留的函式值仍會失敗。需要允許跨執行緒呼叫時，必須將 `RetainCallable` 的第二個參數明確設為 `false`，並由宿主序列化整個會話的所有存取，包括執行、設定、重新載入和銷毀。

腳本清理也是正在進行的 Session 操作：清空或替換程式、關閉熱重新載入、釋放保留的 callable 和銷毀 Session 都可能執行腳本解構函式及宿主回呼。這些回呼不得重入執行或修改同一 Session 的 API；清理期間 `RetainCallable` 也會拒絕增加保留根。後續 Session 操作應安排在清理返回之後；一般執行回呼仍可保留 callable。

## 原生 JIT 可觀測性

`FJitStats::CostModelVmFallbacks` 是有效已編譯路徑因預測較慢而被刻意留在 VM
中的 `Fallbacks` 子集，並非編譯失敗，也不會改變語意。
`FJitStats::NativeTypedLoopExecutions` 統計經原生後端呼叫的型別化迴圈核心次數，
包含回報原始碼可見執行階段錯誤的呼叫；它不是成功計數器。兩者都是最近一次執行的
觀測值；零值不能證明某個 target 或 capability 不可用。

<a id="threads-and-hot-reload"></a>

## 執行緒和熱重載

腳本 VM 執行、宿主會話控制和原生回呼之間的執行緒邊界必須由宿主負責協調。熱重載是明確選擇的能力；函式身分、擷取名稱及型別相容時，閉包及其擷取的 `this` 可以遷移。擷取新增、刪除、型別變化、函式無法重新對應、活動通道和不相容的型別變化仍會被拒絕。宿主保留的 callable 控制代碼仍在其註冊世代被成功替換後失效；清理會保留仍被有效全域量或值根使用的共用值圖。原生副作用不屬於腳本狀態交易。

啟用狀態保留式熱重載後，相容的全域量、物件和容器身分可以遷移；新增欄位使用語言預設值。重新載入會先編譯、驗證並遷移，初始化失敗時保留上一份可用模組。停用熱重載或呼叫 `Clear` 會正常釋放保留值，包括引用環。熱重載會跳過位元組碼快取，以確保 `Reload` 從目前來源重新編譯。

跨語言的穩定 C ABI 見[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md)；瀏覽器和 WebAssembly 的記憶體入口見[WebAssembly/WASM SDK](../../../../../../Platform/WebAssembly/Resources/Docs/User/zh-TW/index.md)。

## 偵錯既有 Session

[偵錯既有 Session](session-debugging.md). `SetDebugBridge`, `ISessionDebugBridge`, `SetDebugBridge(nullptr)`.

## 保留位元組配額

`MaxNativeBytes` 或 `MaxRetainedPayloadBytes` 為零不允許保留對應位元組；與表示無限制的零策略預算不同。

## 明確選擇 JIT 後端

未提供已安裝模組環境時，Core 與 C++ SDK 不會自動選擇具體 JIT 實作。使用 `VeroScriptJitBackendLoader.h`
的 `LoadJitBackend` 載入後端檔案或目錄，將共享工廠指定給
`FEngineConfig::JitBackendFactory`。Session 可使用 `SetJitBackendFactory` 或
`SetJitBackendPath`；空路徑移除後端，載入失敗保留目前後端及快取。

串流 `RunSource`／`RunFile` 多載可在 JIT 工廠之後接收 VM 工廠；較短的多載不提供直譯器。依路徑選擇或採用模組環境預設值時，使用 `FStreamEngineHost` 與 `FEngineConfig`，遵循[服務後端選擇](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection)。`Vm` 及 `Mixed` 的直譯執行部分需要可用的 VM 工廠；明確要求 `Jit` 時還需要 JIT 工廠。已設定的宿主可從模組環境解析工廠，也可接收明確工廠。除錯和狀態保留式熱重載需要具備對應能力的 VM。靜態 Core AOT 可在不提供動態 JIT 工廠時使用 `Mixed`，但直譯執行的初始化及回退仍需要 VM。

安裝除錯橋接會釋放 Session 保留的 JIT 程式碼。橋接之後回傳空除錯器時，
後續呼叫可使用仍被選中的工廠重新編譯。中斷除錯不會取消腳本或銷毀 Session。
Core 與 C++ 公開記憶體布局已改變，原生消費者須使用相符 SDK 重新建置。

## 協作式取消

透過 `ApplyConfig` 或 `SetExecutionControl` 設定共用的 `FExecutionControl`。
協定執行緒可以呼叫 `Session::RequestCancel()`，包括擁有者在閒置時替換控制物件的期間。
此呼叫取消目前控制物件的共用快照；空指標時不執行操作，取消不會傳遞給另一個控制物件。
`GetExecutionControl()` 傳回僅供擁有者執行緒使用的借用參照。
取消採用協作方式，無法中斷原生回呼。重複使用控制物件前應重設取消狀態。
銷毀 Session 前須等待取消呼叫與執行結束；中斷偵錯連線仍是獨立操作。

共用同一個 `FExecutionControl` 權杖的 Session 會共用取消狀態。呼叫 `Reset()` 並重用權杖前，應等待先前的取消呼叫結束，避免舊要求取消下一次使用該權杖的執行。

## C++ 宿主管理記憶體

包含 `VeroScriptMemory.h` 即可使用核心的 `Memory::*` 介面。
`FEngineConfig::MemoryLimits` 對設定式 `RunSource` 和 `RunFile` 都有效。
`MaxBufferBytes` 限制單一自有緩衝區的容量及外部區域長度，`MaxRetainedBytes` 限制自有與借用區域共享的總位元組預算；這兩個限制的零值均表示無限制。

Session 在各次執行的 VM 之間共用同一個記憶體上下文。在擁有者執行緒使用
`GetMemoryContext()`、`SetMemoryLimits()` 和 `GetMemoryStats()`。
修改配額要求執行閒置；降低配額失敗時保留原配額。`ApplyConfig` 的記憶體配額被拒絕時，其他執行設定也保持原值。工作執行緒共用預算，但使用各自的執行緒域。在原生回呼內，透過借用的
`FNativeCallContext::MemoryServices` 取得本次執行的上下文、工作檢查、繫結目錄及參數位址；不要在回呼結束後保留這些服務。`Capabilities.bAllowMemoryAddress` 控制位址存取，裸位址本身不延長位元組的生命週期。自訂原生回呼公開位址前應檢查 `MemoryAddressesAllowed()`；宿主直接呼叫核心記憶體函式時，權限由宿主負責。

`Clear` 和重新載入保留記憶體上下文。相容的熱重載保留區域身分，替換模組初始化失敗時恢復舊程式。
Session 銷毀時先執行腳本清理，再關閉存取域。不要手動關閉借用的上下文，也不要在 Session 銷毀後保留其參照。已取得的租借會保留位元組，直到呼叫
`Memory::ReleaseLease`，即使 Session 已銷毀也是如此。釋放租借是執行緒安全的，但宿主必須先結束或同步原生存取。外部區域的擁有者及釋放回呼資料必須比全部區域別名和租借存活更久；釋放回呼不得擲出例外。

參見 SDK 的 `CppMinimal/memory.cpp` 範例及核心的
[記憶體參考](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)。
獨立 C++ AOT 輸出不支援執行期記憶體操作或區域型別，產生器會明確報錯；未使用的 `memory.vs` 或 `core.vs` 匯入仍受支援。

`RetainCallable` 的第二個參數設為 `false` 不會轉移 Session 的記憶體存取域。
即使宿主已序列化存取，區域操作仍要求 Session 擁有者執行緒；在其他執行緒透過 callable 存取區域會失敗並報告 `diag.memory_wrong_thread`。腳本工作執行緒使用各自的記憶體域。

## 模組公開巨集

`Defines` 和 `PredefinedMacros` 獨立初始化每個模組。Engine 和 Session 使用核心模組載入器：明確匯入引入 `#define_export` 巨集，`export import` 繼續轉匯出它們。一般 `#define` 保持私有；`ImplicitImportPaths` 僅引入宣告。`SourceOverlays` 遵循相同規則。參見[前置處理器參考](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/preprocessor.md)。

修改巨集相依模組後，對檔案 Session 呼叫 `Reload()` 並再次執行以完成初始化，或透過 `LoadSource()` 載入更新後的原始碼。啟用熱重載時，`Reload()` 移轉相容的全域狀態；`LoadSource()` 取代模組並重設狀態。公開巨集介面改變了位元碼配置：部署的 `.vslib`／`.vsapp` 必須用目前核心重新建置，原生消費程式也須使用相符的 Core 和 C++ SDK 重新編譯。

## 編譯期註記處理

透過 `FEngineConfig::AnnotationProcessing` 設定 Engine 和 Session；Session 也提供 `SetAnnotationProcessing`／`GetAnnotationProcessing`。處理器登錄表必須先 `Freeze()`，之後保持不可變。工廠為每次宣告準備建立獨立處理器；處理器所屬動態函式庫及回呼擷取的資料須保持有效。C++ AOT 透過 `FCodegenOptions::AnnotationProcessing` 設定 `FCppCodegenBackend`。

處理器在發起編譯的執行緒上同步執行。準備回呼期間不得執行或修改同一 Session；這些重入操作會被拒絕。取消沿用核心的 `FAnalysisCancelled` 例外，取消或編譯失敗保留目前程式。`FRunResult::Diagnostics` 保留 Warning／Information／Hint，錯誤串流也會輸出它們；非致命診斷不會使成功結果失敗。`RunSource`／`RunFile` 的結果包含編譯及執行診斷。

透過 `ReadDependency` 讀取影響生成結果的外部輸入。快取命中仍先執行宣告準備，處理器身分、設定、生成內容及相依項目參與快取鍵；無法重現的處理器設為 `Cacheable=false`。自訂預算也適用於匯入已有生成宣告的程式庫套件。公開 API 見 Core 的 `VeroScriptAnnotationProcessing.h` 和 `VeroScriptProgramPreparation.h`，可建置的 SDK 使用範例見 `CppMinimal/main.cpp`。

## 宿主控制台整合

`IEngineHost` 可透過 `VeroScriptConsole.h` 實作 `QueryConsole`／`ExecuteConsole`，預設不提供終端能力。`HasConsoleTextOutput`／`WriteConsoleText` 輸出精確文字且不加換行，`FlushConsoleText` 刷新文字。`FStreamEngineHost` 轉送至底層串流。宿主必須同步工作執行緒並存活至清理結束。VM 在執行邊界恢復樣式與游標顯示；終端狀態共享，多任務須協調完整操作序列。

終端介面增加了虛擬方法，升級時須使用同一建置的 Core／C++ SDK 重新編譯宿主及相關原生擴充；使用 JIT 時，也須部署相符的後端。

## 通用編譯處理

Engine 和 Session 傳遞 `FEngineConfig::CompileProcessing`；Session 另提供 `SetCompileProcessing`/`GetCompileProcessing`。C++ AOT 使用 `FCodegenOptions::CompileProcessing`。編譯前必須凍結 `FCompileProcessorRegistry`。每次編譯的處理器實例貫穿 `RawSource`、`ActiveSource`、`Types`、`Members`、`Bodies`；對已準備程式進行 AOT 產生時重用已接受的產生結果。回呼在編譯執行緒同步執行，不能重入同一個 Session。

透過 `ReadDependency` 讀取影響產生的外部輸入；不確定性處理器應設定 `Cacheable=false`。產生內容、相依性、註記定義、註釋保留策略和預算均參與位元組碼快取識別；命中快取前仍執行處理。替換失敗或取消時保留 Session 的活動程式。`CompileProcessing` 與舊 `AnnotationProcessing` 登錄表共享核心交易和預算。僅使用舊註解介面時，提高舊操作數或原始碼預算會同步提高未修改的通用預設值；明確設定的通用登錄表或預算仍是上限。

註記名稱預設區分大小寫。若要接受 ASCII 大小寫變體，可將 `RegisterAnnotation` 的最後一個參數設為 `false`，或在 `CompileProcessing.AnnotationDefinitions` 中設定 `FAnnotationDefinition::CaseSensitive=false`。Session 使用自身繫結登錄表，不可變模組環境使用其發佈目錄，C++ AOT 透過 `FCodegenOptions::Bindings` 接收目錄。此策略同時影響處理器訂閱和執行時反射，可透過 `annotation_info.matches_name()` 與 `annotation_definition_info.case_sensitive()` 查詢。原始碼拼寫保持不變；參數名稱和中繼資料鍵仍精確比對。`matches_name()`、`find_annotations()` 區分 `@`，`has_annotation()` 也接受省略 `@`。比對範圍重疊的登錄會被拒絕，策略變化會使位元組碼快取失效。此類反射查詢依賴 Core 執行階段，可用於 VM、Mixed 及具有 Core 上下文的 JIT 宿主入口；獨立 C++ AOT 會明確拒絕此類呼叫。

`CommentRetention` 控制原始碼註釋保留；VM 支援執行時註解和註釋查詢。本次原始碼中繼資料格式變化後，應使用相符的 Core/C++ 程式庫重新編譯原生使用者端和二進位腳本套件。參見 `VeroScriptCompileProcessing.h`、`VeroScriptSourceMetadata.h` 以及 SDK 的 `CppMinimal/main.cpp` 範例。

升級 Core/C++ SDK 時，使用新建置目錄或完整重新編譯所有原生目的檔。在 Windows 的本地化 MSVC 與 Ninja 環境中，先在開發人員命令提示字元執行 `chcp 65001`，再進行 CMake 設定和建置，確保偵測到的包含前綴與編譯器輸出編碼一致，否則 Ninja 可能漏掉標頭檔變化。

## 指令碼編譯擴充

`FEngineConfig::ScriptCompileExtensions` 設定編譯指令碼；Session 提供 `SetScriptCompileExtensions`/`GetScriptCompileExtensions`。載入前設定 `Roots`、選用的 `RegistrationEntry`、`Configuration`、原始碼覆蓋及標準函式庫路徑。原始碼中的 `import compiler "builder.vs";` 預設可用；`bAllowSourceImports=false` 停用該入口。SDK 應部署相符的 `compiler.vs`。

擴充在編譯執行緒上的獨立 VM 中執行，各階段共用狀態；產生的業務程式碼使用所選後端。業務繫結不會自動提供給擴充；需要時明確設定僅供編譯使用的 `Bindings`，並確保編譯期間有效。編譯回呼不得執行或修改同一 Session；原生回呼的授權仍由宿主負責。

擴充的 `ExecutionControl` 為空時繼承 Engine/Session 的執行控制器；Session 中隨 `SetExecutionControl` 更新。明確提供的控制器保持獨立，即使最初與執行控制器相同。設定只影響後續編譯或重新載入。擴充的 `MaxInstructions`、`TimeoutMilliseconds`、路徑及橋接限額獨立於業務執行限制；取消採合作方式，無法中斷原生回呼。

擴充身分、設定、原始碼、產生結果及宣告的相依項目參與快取驗證。透過編譯器相依服務讀取產生輸入；額外原生繫結預設禁止重用，只有明確宣告確定性並提供穩定的 `BindingConfiguration` 才允許。擴充編譯或重新載入失敗保留 Session 的作用中程式。升級後須使用相符的 Core/C++ SDK 完整重新編譯原生使用端及二進位指令碼套件；SDK 的 `CppMinimal` 已涵蓋 Session 與 AOT 擴充流程。

`GetScriptCompileExtensions` 回傳原始設定，繼承控制器仍保持為空。讀取設定、修改其他欄位後再設定回去，不會改變控制器的繼承規則。

## 模組環境與原生擴充

檢查、載入或執行前，將 `FModuleEnvironment::Create` 傳回的不可變快照設至 `FEngineConfig::ModuleEnvironment`。Engine 與 Session 使用其模組映射、相依範圍、資源、編譯策略和原生契約。映射的記憶體模組可透過規範的 `veroscript-module:/...` 路徑交給 `CheckFile`／`RunFile`／`LoadFile`。匯入由快照授權；安裝不會自動匯入宣告。

環境提供 `Bindings` 時，`GetBindingCatalog`、綁定匯出和 Session 偵錯視圖均使用該目錄。發布環境前，使用同一個暫存登記器（或 `FNativeExtensionRegistry` 的基礎登記器）合併綁定，保持型別 ID 一致。Session 拒絕衝突的本機綁定及後續登記；環境擁有已發布目錄時，`GetBindingRegistry` 擲出 `std::logic_error`。不提供目錄的環境仍可使用 Session 本機登記。

Session 已載入程式時不能替換環境。結束執行，呼叫 `Clear`，再套用新設定並重新載入。設定遭驗證拒絕時保留原程式和設定；編譯失敗保留使用中的程式。`ApplyConfig` 例外遵循其[設定結果約定](#apply-config-result)。回呼與逸出的原生物件持有擴充程式碼；卸載宿主資源前先釋放工作、可呼叫句柄和物件。原生函式庫透過 `VeroScriptNativeExtensionLoader.h` 的明確介面載入；消費端和原生模組須使用匹配的 Core／C++ SDK 及共用 CRT 重新編譯。JIT 後端仍使用獨立載入協定。

目前環境編譯路徑直接使用 Core 準備流程，不使用 Session／Engine 的持久位元組碼快取。純腳本 AOT 支援此快照；需要執行期原生套件契約的程式在提供目標執行期適配器前會被明確拒絕。

`GetModuleEnvironment` 可保留目前快照；偵錯視圖借用的 `ModuleEnvironment` 提供映射的記憶體原始碼和提供者契約。明確的原始碼覆蓋優先。這些表示編譯輸入，不是已載入主原始碼的凍結副本；宿主仍應公開實際 `LoadSource` 修訂。

## 精確數值 API

Session 的 SetNumericLimits/GetNumericLimits 設定 FNumericLimits。VeroScriptNumerics.h 提供不變 FBigInteger/FRational 與受檢 Numerics::Try*，使用 FNumericOperationContext 的 Limits/Work。零選有限預設，明確處理工作、取消、錯誤及失敗輸出保留。獨立原始碼 AOT 目前拒絕 Core 精確數值操作。

## 執行中斷後的有界清理

<a id="bounded-interruption-cleanup"></a>

指令限額、逾時或取消會終止正常業務執行。Core 使用獨立的一份 **16,384 條邏輯指令與 50 ms** 清理預算，執行已進入作用域的 `finally` 與必要腳本解構；同步巢狀呼叫及暫時清理 VM 共用此預算。已耗盡的原限額或已要求的取消不會立即阻止清理，原有權限仍生效。清理不能建立新工作、等待主控台輸入或使用 `exit` 取代原中斷錯誤。

最初的中斷錯誤保留為主要錯誤；清理錯誤附加於 `FRunResult.Diagnostics` 和 `VeroScriptVm::GetCleanupDiagnostics()`。清理完成後，宿主解除限額或重設取消權杖即可再次呼叫。這不回復已發生的業務修改。

清理未完成、存在未捕捉的清理錯誤，或清理預算耗盡時，`VeroScriptVm::RequiresReset()` 與 `VeroScriptSession::RequiresReset()` 傳回 true。若中斷發生在已執行中的 finally 或解構函式內部，未完成的清理不會重播，而會要求重設。後續執行及保留狀態操作以 `diag.execution_state_reset_required` 失敗。宿主應呼叫 `Session.Clear()`，再載入新程式；Clear 會放棄不可用狀態中未完成的腳本清理並釋放原生所有權，不會重設宿主取消權杖。獨立 VM 及保留的 Core 狀態也須丟棄後重新建立。清理期限仍屬協作式限制，無法強行終止正在阻塞的宿主回呼；VM、Mixed、JIT 與適用 AOT 路徑使用相同的 Core 契約。 若失敗重新載入的復原未完成，或外部根阻止 `Clear()`，應在 Clear 後檢查 `RequiresReset()` 與 `IsLoaded()`；無法重設時重建 Session，不得繼續執行不可用執行個體。

## Session 後端選擇參數

`VeroScriptSession::SetVmBackendPath` / `SetJitBackendPath`

這些 Session 設定方法接受相符檔案／目錄；提供的模組環境含解析器時，也接受已安裝後端 ID。`auto` 要求環境候選，VM 還可使用保留的產品後備。`none` 或原生空字串停用選擇；`""` 對應該空要求。新建工作階段未明確選擇時可繼承環境；省略與明確清除不同。重新設定現有工作階段時，省略 VM 選擇會保留既有執行域。明確選擇失敗保留先前後端。僅在建立執行緒的閒置工作階段設定；VM 必須在執行前更換，因為 Clear／reload 不解除執行域固定。JIT 選擇與 VM／Mixed／JIT 執行策略獨立。

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` 提供 `FallbackVmBackendFactory`，明確的 `VmBackendFactory`／`VmBackendPath` 優先。Session 會保留產品退回選項，以便在執行前透過 `SetVmBackendPath("auto")` 恢復。

## 持久執行與重載結算

持久 Session 中的一般函式可以使用所選 Mixed/JIT 後端；保留初始化區間、偵錯和有界清理繼續使用 VM。不支援的原生操作在進入前回退，指令、逾時、取消、權限和呼叫深度規則保持一致。

受支援的被攔截方法本體也依後端能力選擇執行；`Proceed` 中的巢狀呼叫保留攔截、參考寫回和執行控制。攔截器錯誤未提供位置時使用呼叫處；明確位置和原始方法本體錯誤保持不變。

直接轉交 `Proceed` 的原始結果會保留按值回傳的複製邊界，包括結構體、原生值型別和固定陣列，不會再次觸發複製建構。替換或借用的結果仍依一般規則檢查型別並按值複製。宣告為參考回傳的方法繼續保有參考語意。

穩定的原生繫結和回呼可以重用同代程式碼。`IHostBindingCatalog::GetRevision()` 發布執行期繫結修訂；傳回零的自訂目錄含原生回呼時不參與重用。`FHostBindingRegistry` 自動更新修訂；外部變更影響回呼程式碼或生命週期所有權時，呼叫 `InvalidateRuntimeBindings()`。回呼正常讀取的可變業務資料不需要更新修訂。執行期間不得修改目錄。Session 的回呼設定介面會使保留程式碼失效，每次呼叫都使用目前回呼和服務。

`RetainMigratableValue(value, root, error)` 明確把宿主持有的值圖加入全域狀態使用的同一遷移圖，重用物件、容器、別名、循環和閉包的相容規則。替換初始化待結算時，`root.Read()` 傳回空；成功提交或完成回滾後恢復讀取。暫存期間不得存取複製出的原始控制代碼，它們不會自動重定位。一般根仍阻止替換，世代根仍按原規則失效；Load/Clear 仍要求釋放可遷移根。 原始碼重新載入需要先設定 `SetHotReloadEnabled(true)`；交易式套件／應用程式重新載入也使用此遷移服務。

在擁有者執行緒讀取 `GetReloadStatus()`。`OperationId` 獨立於 `PreviousGeneration`、`CandidateGeneration`、`CurrentGeneration` 和 `CommittedGeneration`，因為回滾後可能再次使用同一候選世代。`Rejected` 表示目前程式未被替換；`Pending` 表示已暫存；`RolledBack` 表示已完成恢復；`Applied` 表示初始化已提交；`AppliedWithErrors` 表示隨後業務執行或退役值清理失敗。`ResetRequired` 與 `RequiresReset()` 一致，禁止繼續執行；回滾或清理無法完成時，應重新建立 Session。快照描述最近一次被接收的重載請求。腳本回滾不撤銷宿主原生副作用。

Core、C++ 嵌入層和 Application 原始碼透過 `PathUtil::FromUtf8` / `ToUtf8` 支援 C++17/C++20 的 UTF-8 路徑，不需要關閉 `char8_t`。即使產品版本號相同，原生後端和模組仍須針對相符的 Core 實作重新建置。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](embedding.md)
