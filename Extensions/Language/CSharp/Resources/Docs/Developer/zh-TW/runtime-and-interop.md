# C# 執行時與 Interop 介面卡

## 型別化迴圈統計

`NativeTypedLoopExecutions` 統計原生型別化迴圈核心的呼叫次數，包括回報原始碼可見執行階段錯誤後結束的呼叫。它是執行可觀測性計數器，而不是成功計數器。

`VeroScriptCSharp` 是 `VeroScriptInterop` 之上的託管包裝器。包裝器負責易用轉換、lease、回撥根和託管異常；C ABI 擁有原生布局、狀態值和生命週期規則。

## 相容性閘門

`VeroScriptRuntime` 公開 ABI 版本、語義執行時版本、宿主平臺、能力位、當前語言、可用語言 JSON 以及目錄載入/合併操作。`EnsureCompatible()` 是託管入口閘門。ABI 版本與產品語義版本是不同值，絕不能合併顯示。

`EnsureCompatible()` 將完整 ABI 值與 `SupportedAbiVersion` 比較，目前為 `0x00000101`（0.1.1）。任何不相符均拋出 `NotSupportedException`；主版本相同或 ABI 數值更大都不足以通過檢查。ABI 變更時須同步 `NativeMethods.AbiVersion`、原生標頭檔、公開 API 契約與套件文件。

`LoadCatalogDirectory` 在驗證 `en-US` 基線後替換執行時目錄樹。`MergeCatalogDirectory` 將已驗證目錄加入當前目錄，並可顯式允許覆蓋現有 ID。宿主必須有意識地選擇替換或增補，不能靠複製 JSON 或硬編碼文化列表模擬合併。

## 會話與控制代碼

`VeroScriptSession` 擁有原生會話控制代碼和已註冊託管根；其操作遵循底層空閒/執行中及建立執行緒規則。`VeroScriptValue` 可擁有原生值，或在 `BorrowScope` 內提供借用檢視，借用值不得逃出 scope。`VeroScriptPackage` 擁有包控制代碼，公開後設資料與規範簽名位元組，並跟蹤已編譯模組是否仍可載入。

`VeroScriptCallable` 可記錄所有者執行緒。呼叫進行時釋放會延後到原生呼叫放開控制代碼；這是生命週期保護，不使跨執行緒呼叫合法。

## 編組

介面卡顯式編組定寬整數、浮點、bool、UTF-8/寬字串、enum、宿主物件、buffer、陣列、map、set、queue、stack、deque 和 channel。UTF-8/UTF-16 輔助函式及原生 buffer lease 必須保留所有權，且在所有者釋放前複製借用檢視。

託管異常會在原生回撥邊界捕獲並轉換到 ABI 錯誤通道，絕不能未檢查地穿過非託管 C 回撥幀。

## 配置、取消和 JIT

載入或執行前應用 `VeroScriptConfiguration`。除源覆蓋層和匯入策略外，執行欄位有 `MaxCallDepth`、`MaxInstructions`、`TimeoutMilliseconds`、`EnableExecutionControl`、`CapabilityPolicyEnabled`、`AllowedScriptCapabilities` 和 `ExecutionBackend`；指令/超時為零即不限。當能力策略啟用時，允許標誌控制標準庫檔案、環境、控制檯與任務副作用，VM 必須先於任何 JIT 入口實施限制。

`RequestCancellation` 標記原生執行控制物件，可從非所有者執行緒呼叫；它是協作式的，不能停止已經在原生或託管程式碼中執行的回撥。`SetJitPolicy` 應用自適應編譯設定並使可複用已編譯狀態失效；`GetLastJitStats` 返回最近一次執行範圍內的增量快照。普通 `CallFunction` 預設可在新的 VM 例項間複用程式碼，指令碼全域性和物件不在呼叫間保留；將 `VeroScriptJitPolicy.ReuseSessionCode` 設為 false 可退出。重用要求有效後端不是 VM，並使用帶修訂編號的原生繫結目錄。繫結、觀察回呼或程式世代變更會使快取失效。啟用執行治理時，可複用治理要求相符的快取內容。編譯嘗試或快取項目預算為零表示不限制對應計數；保留原生程式碼與完整載荷的位元組上限仍然有效。

啟用執行治理時，`Mixed` 預設使用 VM；應先量測目標工作負載，再透過 `VeroScriptJitPolicy.EnableControlledMixed` 選擇啟用符合條件的受控 JIT。明確選擇 `Jit` 不受此開關控制。不支援或成本模型較偏好 VM 的入口仍使用受治理的 VM 回退。後端 `ControlledExecution` 表示支援能力，`ControlledNativeExecutions` 和 `AotExecutions` 分別記錄實際受控原生入口與 AOT 入口的呼叫次數。

`SetJitCacheLimits` / `GetJitCacheLimits` 控制原生程式碼和完整保留載荷上限，預設 8 MiB/32 MiB，零不允許對應資源保留。`GetJitCacheStats` 返回當前上下文的累計編譯嘗試、穩定保留位元組和資源限制鎖存，非實時或程序峰值計數。這些可選方法要求 `VeroScriptCapabilities.SessionJitCache`；獨立部署的相容執行時缺少該能力時丟擲 `NotSupportedException`。VM 負責初始化保留狀態，同一世代內符合條件的呼叫可重用已編譯程式碼。支援 `VeroScriptCapabilities.ReloadStatus` 時，在建立執行緒呼叫 `GetReloadStatus()` 可讀取原生操作和世代快照。

## 會話操作與生命週期

會話支援 `LoadSource`、`LoadFile`、`LoadPackage`、`Reload`、`ReloadPackage`、`BuildPackageFile`、`Run`、診斷、輸入、函式/方法檢查、`CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`Construct`、欄位/全域性訪問和 `ApplyClassDefaults`。繞過形式只跳過入口攔截器，方法體的巢狀呼叫仍可攔截。

宿主整合包括函式、註解、宿主型別/成員、協議、型別別名、泛型宿主型別、例項攔截器、變更處理器和外部序列化器註冊。反射與繫結 JSON 僅為檢查輸出。`VeroScriptValue` 還公開 sequence/map/set/channel 操作，`VeroScriptCallable` 按其所有者執行緒和弱 `this` 生命週期策略保留並呼叫函式。

`GetDiagnostics`、`GetDiagnosticArguments` 和 `FormatDiagnostic` 保留結構化訊息 ID、檔案標籤、源座標和替換引數。`VeroScriptRunResult` 報告執行是否成功、退出狀態、退出程式碼和診斷數；呼叫方不得用列印訊息取代它。借用值隨 `BorrowScope` 到期，回撥異常在原生列舉返回後重拋，原生 buffer 必須在 lease 釋放前複製。

## 診斷與託管生命週期

`GetDiagnostics`、`GetDiagnosticArguments` 和 `FormatDiagnostic` 會保留結構化訊息 ID、檔案標籤、源座標和替換引數。`VeroScriptRunResult` 報告執行成功狀態、退出狀態、退出程式碼和診斷數量；呼叫方不得以列印訊息取代此結果。借用值會隨 `BorrowScope` 到期，回撥異常會在原生列舉返回後重新丟擲，原生 buffer 必須在其 lease 釋放前複製。

## JIT 統計對映

託管 `VeroScriptJitStats` 快照保持標量快速路徑、trampoline 入口種類與直接巢狀原生呼叫之間的區分。`DirectNativeCalls` 不是編譯次數，不能與 `ScalarNativeFastPathCalls` 合併。這些值描述最近的執行快照，不會改變請求後端或建立新 ABI/版本契約；不支援或受保護入口繼續回退 VM。`CostModelVmFallbacks` 是因有效已編譯路徑預測成本高於 VM 而選擇的 `Fallbacks` 子集；`NativeTypedLoopExecutions` 統計核心呼叫，包括回報原始碼可見執行階段錯誤後結束的呼叫，不是成功計數。必須分別檢查 `VeroScriptJitBackendCapabilities.NativeCode` 與 `VeroScriptJitBackendCapabilities.NativeTypedLoops`，兩者都不能由 `HostPlatform` 推斷。`VeroScriptJitCacheStats` 的生命週期不同：它描述累計、穩定的可複用上下文，並在策略、限制、後端、模組或相關宿主可觀測狀態變化時重置。

請參閱[C# 工作階段與執行時期 — JIT 明確選擇](../../User/zh-TW/session.md#jit-backend-capabilities)。

請參閱[C# 工作階段與執行時期 — JIT 明確選擇](../../User/zh-TW/session.md#diagnostic-translation)。

## 保留呼叫控制代碼的釋放

請參閱[C# 工作階段與執行時期 — 保留呼叫控制代碼的釋放](../../User/zh-TW/session.md#callable-disposal)。

<a id="managed-marshalling-maintenance"></a>

原生呼叫保持值與套件的託管所有者存活，直到執行及原生檢視複製完成。阻塞通道操作與 map/set 訪問器保持接收物件存活；參數或元素串列保持索引器回傳的每個值包裝器存活，直到原生使用結束，包含按需產生的值。這可防止 GC 提前終結物件，明確釋放與回呼有效範圍的規則仍然適用。回歸檢查在原生接收、訪問器及按需串列編組期間強制 GC，並涵蓋最佳化後的 .NET 8/9 執行與 NativeAOT 套件使用端。

原生參數與元素編組保存實際提供控制代碼的值包裝器參考快照。串列索引器或託管回呼替換輸入陣列元素時，原始包裝器仍存活至原生使用結束，`ref`/`out` 回寫仍更新原始包裝器。回歸測試在 map 建構、直接函式呼叫及保留 callable 呼叫中替換陣列元素後強制 GC。

[Android 與 iOS / MAUI](../../User/zh-TW/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

行動封裝仍使用同一專案與套件識別名稱：`VeroScriptIncludeMobile=true` 將桌面 .NET 8/9 與 .NET 10 Android/iOS 目標及已驗證的原生 SDK 一起封裝。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/runtime-and-interop.md) · [fr-FR](../fr-FR/runtime-and-interop.md) · [ja-JP](../ja-JP/runtime-and-interop.md) · [ko-KR](../ko-KR/runtime-and-interop.md) · [zh-CN](../zh-CN/runtime-and-interop.md) · [zh-TW](runtime-and-interop.md)
