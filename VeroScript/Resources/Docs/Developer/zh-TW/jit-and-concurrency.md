# 核心 JIT 服務與並行

[可替換 VM 後端](vm-backends.md)

<a id="native-abi-version"></a>

`VEROSCRIPT_VERSION_CODE` 依 major:8／minor:8／patch:16 位元編碼產品版本 `VEROSCRIPT_VERSION`。JIT 模組交握及 JIT／AOT 的 `CoreVersion` 統一使用此值，C AOT 描述欄位為 `core_version`；沒有獨立的 JIT 契約或 API 版本計數。二進位相容性仍檢查公開標頭指紋、描述結構配置、工具鏈及平台，AOT 另檢查 Core 實作身分。靜態 C++ 使用端及後端二進位檔須以匹配的標頭與程式庫重建；套件自身的發行版本仍是獨立的套件資訊。

## 所有權與選擇

請參閱[可替換 VM 後端](vm-backends.md#vm-provider-assembly)。

[可選 AsmJit 模組](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/zh-TW/index.md)擁有 IR、lowering、原生發碼、可攜式 Linear 執行、ABI 配接、直接呼叫目標與執行儲存。私有標頭及架構巨集不屬於核心 SDK。核心能力字串為 `backend-services`；實際目標與原生能力由所選工廠描述提供。

## 編譯與呼叫

`EJitCompilationResult` 區分 `Ready`、`Unsupported`、`Deferred`、`Failed`。不支援與資源暫緩可退回 VM；編譯失敗回報錯誤。產物通過所有權、服務及資源驗證後發布。程式身分包含模組與原生繫結世代。

`EJitInvocationResult` 區分 `NotEntered`、`Returned`、`ControlTransfer`、`Failed`。只有 `NotEntered` 可由直譯器重新執行；進入後的錯誤、throw、exit、取消直接傳遞，不重複副作用。核心驗證回傳值。產物保有程式碼與模組擁有者；上下文、框架令牌與資料檢視均為借用。巢狀呼叫或宿主重入後，重新取得物件與容器儲存。

服務標頭為 `VeroScriptJitRuntimeServices.h`、`VeroScriptJitFrameServices.h`、`VeroScriptJitValueServices.h`、`VeroScriptJitCallServices.h`、`VeroScriptJitScalarServices.h`。複製、參照、const、方法分派、變更通知、錯誤與邏輯呼叫深度沿用核心語意。

`SetJitProgramOwner` 提供的擁有者保活的是不可變模組世代，不得透過其他可寫別名修改其內容；模組在閒置邊界替換。裸 `VeroScriptVm` 執行不會自動推進設定的程式世代或更換擁有者。若程式更換後仍保留值，宿主須更新該身分，並保留定義模組至清理或受支援的遷移完成。Session 的替換與熱重載路徑會管理這些身分。

## 策略與 Session 快取

`FJitPolicy` 控制熱度、編譯次數、快取決定與預編譯，策略預算零表示不限。`Vm` 不進入後端；`Mixed` 等待熱度；`Jit` 依策略優先編譯。執行治理依後端能力選擇經驗證的受控 JIT 或 VM；除錯及不支援的路徑仍使用 VM。

`bReuseSessionCode` 預設啟用。合格的 `Run`、`CallFunction`、`CallMethod` 與保留 callable 呼叫租借同世代快取，累計編譯額度不會重設；無限預算也可複用。須注入工廠且模式非 VM；持久執行與穩定回呼可以複用；原生目錄透過 `IHostBindingCatalog::GetRevision()` 發布執行期修訂。策略、配置、Reload、Clear 使狀態失效；失敗 Reload 保留原程式。

`FSessionJitCacheLimits` 預設為可執行儲存 8 MiB、保留載荷 32 MiB；零表示不能保留該類資源，scratch 計入載荷。資源拒絕可退回 VM，不退還已用編譯次數。`FSessionJitCacheStats` 是已結算世代記帳；`FJitStats` 是最近執行快照。`NativeTypedLoopExecutions`、`DirectNativeCalls`、`CostModelVmFallbacks` 分別代表實際路徑；成本回退不是執行錯誤。

## 並行與驗證

核心負責任務、等待、逾時、協作取消、channel、mutex、semaphore 與同步值。阻塞 channel 操作觀察關閉。worker 共用不可變工廠與有擁有者的程式，實例、統計、scratch、錯誤各自獨立；任務結束釋放語言根。

驗證契約、後端差分、生命週期、除錯與並行。分別構建模組開啟、關閉及僅關閉原生發碼；不含後端目錄的獨立原始碼仍須構建核心與 C++ 消費者。保留效能門檻並驗證 SDK 明確注入。

<a id="construction-dispatch"></a>

建構或解構期間，虛擬呼叫目標必須由 Core 按目前生命週期階段解析。`bConstructionIncomplete`、`bDestroying` 表示需要此檢查；`GetTypeName` 和 `RuntimeTypeIndex` 仍表示物件的實際型別與欄位配置。最佳化路徑若無法維持階段分派，必須在產生副作用前退回，不能重用一般的實際型別虛擬呼叫快取。明確靜態繫結的非虛擬呼叫，以及由 Core 已選定目標的原生建構入口，保留原有路徑。

## 執行診斷

`PlanReason`／`PlannedKind` 表示讀取快照時的快取計畫；`Fallbacks` 保留本輪實際拒絕歷史，包含成功編譯前的暖機決定。詳細收集使用官方 VM 的觀測分派，可能減少指令融合並顯露更多 Core 邊界；其時間／邊界數不能代表關閉時的成本。聚合實際入口計數不依賴詳細收集。

`NativeLeafEntries`／`NativeCfgEntries` 計算編譯構件，`NativeLeafExecutions`／`NativeCfgExecutions` 計算實際入口，包含錯誤退出。controlled、型別化迴圈、直接呼叫與 Linear／Bridge／VM trampoline 保留獨立計數；`ScalarNativeFastPathCalls` 只計算宿主 scalar-native 快速路徑。這些事件重疊，不能當成獨立腳本呼叫相加。

執行前設定 `FJitPolicy::Diagnostics`，例如 `MaxFunctions=128`、`MaxDepth=256` 及選用 `bMeasureTime`；執行後查詢 `VeroScriptVm::GetJitDiagnostics()` 或 `VeroScriptSession::GetLastJitDiagnostics()`。記錄包含世代內函式身分、模組、實際入口類型、結果、回退分布及選用 inclusive／self 奈秒。只記錄 Core 呼叫邊界，內聯操作與未經 Core 入口的直接原生遞迴沒有獨立記錄。inclusive 重疊，self 只扣除已記錄子呼叫；最外執行重設快照，重入與清理歸同一執行。`ResetJitStats()` 亦清空。

預設 `MaxFunctions=0` 不配置記錄、不讀時鐘、不寫日誌。上限為 4096 函式、深度 32768、每個名稱／理由 4096 位元組，部分收集以 `DroppedRecords` 表示。只需計數時關閉計時，效能量測與診斷分開進行。

`GetFunctionReason()`／`GetFunctionFallbackReason()` 顯示編譯決定及最近執行期拒絕，包含成功 VM 計畫。例外／ref／所有權限制與成本模型分開；未知執行限制使用 `RuntimeGuard`。`CostModelVmFallbacks` 不含必須的 finally／ref 回退。finally 保留 Core 解捲與有界清理；無回呼／治理的有界純量運算可更新一個普通可寫 ref 捕獲，別名／多捕獲／readonly／變更來源／探測失敗仍走標準路徑。frame 快取以函式／模組／世代保護，getter 順序／次數及明確綁定狀態不變。

聚合計數映射到 C Interop／C++／C#／Python，詳細快照提供於 Core／C++。公開佈局／服務變更後，即使產品仍為 0.1.1，也須重建匹配的 Core、VM、JIT、Interop 與原生消費端。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/jit-and-concurrency.md) · [fr-FR](../fr-FR/jit-and-concurrency.md) · [ja-JP](../ja-JP/jit-and-concurrency.md) · [ko-KR](../ko-KR/jit-and-concurrency.md) · [zh-CN](../zh-CN/jit-and-concurrency.md) · [zh-TW](jit-and-concurrency.md)
