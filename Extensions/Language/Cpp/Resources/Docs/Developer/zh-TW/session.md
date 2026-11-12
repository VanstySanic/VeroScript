# C++ 嵌入與會話

## 無狀態引擎

`VeroScriptEngine` 為不需要持久狀態的呼叫方提供 `CheckSource`、`CheckFile`、`RunSource` 和 `RunFile`。檔案操作可使用位元組碼快取目錄；源標籤、輸入/輸出/錯誤流、迴圈匯入策略和專案範圍訪問策略均為顯式引數。結果包含成敗和結構化診斷，寫入錯誤流的一行文字不能替代結果物件。

## 有狀態會話

`VeroScriptSession` 擁有載入的模組、宿主註冊、輸入/輸出橋、執行時配置和可選熱重載狀態。正常流程是：

1. 使用 `IEngineHost` 構造會話。
2. 應用配置，註冊原生函式或宿主型別。
3. 載入源、檔案或已驗證包，或編譯並提交模組。
4. 執行、呼叫函式/方法、檢查值或列舉後設資料。
5. 僅在狀態前提允許時清理或過載。

執行期間拒絕配置、載入、編譯、提交、過載、清理和繫結變更。`LoadFile` 記錄檔案過載路徑，`LoadSource` 不會；過載在提交前驗證替換模組和包種類。

## 求值與值

`Run(ERunMode::Eval)` 執行模組初始化而不呼叫 `main`；跨呼叫的全域狀態僅在啟用 `SetHotReloadEnabled(true)` 時保留。函式/方法呼叫可返回 `FValue`、構造物件、訪問欄位/全域並保留可呼叫物件。保留可呼叫物件需顯式釋放，並可配置為僅所有者執行緒呼叫。外部序列化器按已註冊執行時型別和編碼版本工作；序列化、反序列化及引用列舉是獨立操作。

## 宿主繫結與後設資料

可註冊原生函式、宿主型別、泛型宿主型別模板、成員、運算子、clone 回撥、可繼承型別、作用域 enum 和子類工廠。註冊錯誤透過穩定診斷 ID 返回。反射列舉已載入型別、欄位、方法、函式、全域、enum 和 interface，且僅為只讀後設資料訪問。

## JIT 與清理

<a id="session-jit-cache"></a>

重用要求已選擇工廠、有效策略不是 VM，並啟用 `FJitPolicy::bReuseSessionCode`。符合條件的 `Run`、`CallFunction`、`CallMethod` 和保留 callable 呼叫共享同代快取及編譯配額。`MaxCompileAttempts` 或 `MaxCachedEntries` 為零表示無限制，不表示停用。持久執行和穩定回呼可以重用程式碼；原生目錄必須透過 `IHostBindingCatalog::GetRevision()` 發布執行期修訂。後端具備所需治理能力時，受控原生執行也能重用程式碼；Session 會跨 VM 保留該能力上下文。組態、策略、後端、模組或繫結變化會使保留程式碼失效；重載失敗保留原程式。預設上限為 8 MiB 可執行程式碼、32 MiB 保留承載。`GetJitCacheStats` 是已穩定的同代計量，`GetLastJitStats` 描述最近呼叫，不是程序記憶體測量。

## 執行治理

`SetExecutionLimits` 接受指令和牆鍾限制，零表示對應限制無限。`SetExecutionControl` 安裝共享執行緒安全取消物件，執行期間可呼叫 `RequestCancel`。取消是協作式的，既不會強制終止原生回撥，也不會讓未同步的宿主狀態變得安全。

`SetCapabilities` 控制檔案、環境、控制台和任務等標準庫副作用，Core 在直譯及受支援的原生路徑中實施權限檢查。將 `bAllowProcessDirectoryChange` 設為 `false` 可單獨禁止 `chdir` 修改宿主行程工作目錄；`chdir` 同時需要環境寫入權限。`SetExecutionBackend` 選擇 `Vm`、`Mixed` 或 `Jit`。受控 `Mixed` 預設使用 VM；可透過 `SetJitPolicy` 設定 `FJitPolicy::bEnableControlledMixed=true` 啟用動態原生執行。明確要求的 `Jit` 和在 `Mixed` 中選擇的靜態 Core AOT 不受此開關限制。缺少所需治理能力時，在進入原生程式碼前退回 VM；除錯、保留初始化區間和有界清理使用 VM。

## 呼叫、攔截與持久化

公共呼叫麵包括 `HasFreeFunction`、`HasInstanceMethod`、`CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`ConstructObject`、`RetainCallable`、`InvokeCallable`、欄位/全域訪問、`ApplyClassDefaults` 和反射列舉。繞過方法只跳過所選入口攔截器，方法體的巢狀呼叫仍按正常規則攔截。保留可呼叫物件的所有權、所有者執行緒模式、弱 `this` 捕獲及模組代次失效仍是顯式生命週期規則。

`RegisterExternalSerializer`、`SerializeObject`、`DeserializeObject` 和 `EnumerateObjectReferences` 遵循 Interop 序列化與借用引用契約。`Reload`、`ReloadCompiledModule` 與 `ReloadPackage` 事務性地暫存並驗證替換物；初始化失敗保留最後可工作的模組並報告結構化診斷。

## 保留位元組配額

`MaxNativeBytes` 或 `MaxRetainedPayloadBytes` 為零不允許保留對應位元組；與表示無限制的零策略預算不同。

## 取消與宿主投遞

<a id="session-control-and-streams"></a>

`RequestCancel()` 以原子方式取得目前控制物件的共用快照，擁有者在閒置時替換控制物件也安全。空指標時不執行操作，取消不會傳遞給另一個控制物件。`GetExecutionControl()` 僅提供擁有者執行緒借用參照。銷毀 Session 前須等待取消呼叫與執行結束。宿主行檢視僅在回呼期間有效；輸出回呼擲出例外會使串流報告失敗，重用或銷毀時不重播已嘗試投遞的事件。`ResetForReuse` 恢復串流狀態，解構抑制回呼例外。

共用同一個 `FExecutionControl` 權杖的 Session 會共用取消狀態。呼叫 `Reset()` 並重用權杖前，應等待先前的取消呼叫結束，避免舊要求取消下一次使用該權杖的執行。

## 實作歸屬

<a id="session-debug-implementation"></a>

`VeroScriptSessionDebug.h` 定義 Session 偵錯橋與借用檢視，目前實作位於 `VeroScriptSession.cpp`。`VeroScriptLiveDebugDomain.h` 定義由宿主持有的即時偵錯域契約，`VeroScriptDapAdapter.cpp` 使用此介面。同名 `.cpp` 入口目前僅包含標頭，不能依檔名推斷實作歸屬。維護時必須遵守上述控制執行緒與生命週期契約。

## 選用 JIT 診斷

`FJitPolicy::Diagnostics.MaxFunctions` 預設為零，關閉逐函式收集。需要時在既有 policy 副本設定記錄數、深度上限及選用 `bMeasureTime`，再呼叫 `SetJitPolicy`。

`GetLastJitDiagnostics()` 返回最近執行的自有快照，暫時 VM 解構後仍有效；要跨後續呼叫保留請複製。Run、除錯、函式／方法呼叫、建構及保留 Callable 都使用相同收集入口。關閉收集後的下一執行或 `Clear()` 會清空。`DroppedRecords` 表示部分收集；關閉計時時時間欄位為零。

`GetLastJitStats()` 的 `NativeLeafExecutions`、`NativeCfgExecutions`、`BridgeExecutions`、`LinearExecutions`、`VmTrampolineExecutions` 是實際入口，與編譯數分開。scalar／直接呼叫／型別化迴圈可為巢狀事件，不可加到函式入口次數；inclusive 時間也不可跨函式相加。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](session.md)
