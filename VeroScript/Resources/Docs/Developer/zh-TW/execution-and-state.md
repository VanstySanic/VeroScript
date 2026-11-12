# 核心執行、狀態與包

[可替換 VM 後端](vm-backends.md)

## VM 狀態

請參閱[可替換 VM 後端](vm-backends.md#vm-provider-assembly)。

## 位元組碼後設資料與控制流

位元組碼攜帶保持原始碼和包行為所需的後設資料：引用引數標誌、閉包捕獲名稱與型別、僅宣告或 abstract 函式狀態、interface 與 enum 表、除錯檔案所有權、異常區域、`finally` 範圍、全域性初始化器範圍以及包元件所有權。引用 opcode、已檢查和可選 cast、`throw` 及受保護區域清理都是語義操作，不能作為可互換的 VM 最佳化對待。序列化、熱重載、反射和工具鏈必須使關聯後設資料與可執行表保持對齊。

## Worker 與同步

指令碼全域性變數和類靜態欄位按原生 worker 隔離。worker 任務登錄檔及其任務 ID 也是 worker 本地的。`channel<T>` 是明確的、帶同步的跨 worker 能力；宿主回撥和可達原生狀態需要宿主提供同步。worker 不能把另一 worker 的任務 ID 當作本地 ID 使用。

## 包與過載

包載入會驗證模組後設資料以及 application/library 入口形狀。會話過載必須顯式啟用，並在提交前暫存替換模組。基於檔案、已編譯模組和包的過載具有不同輸入。過載失敗時必須保留原先可工作的模組，並公開結構化診斷。

## 除錯與診斷

除錯位置、棧幀、變數、斷點、求值和停止原因，是 Tooling 與 DAP 消費的公共資料契約。診斷 ID、源範圍和替換引數是穩定的機器資料；本地化訊息僅承擔展示職責。

## 執行治理

`FExecutionLimits` 包含指令上限和牆鍾超時；某個欄位為零表示該欄位不限。`FExecutionControl` 是執行緒安全的共享控制物件。`Run` 活躍時宿主可呼叫 `RequestCancel`，VM 會在協作式執行點觀察該請求。取消絕不會強制停止已經執行中的原生或託管回撥，因此宿主回撥狀態仍需遵循正常的同步規則。

`FScriptCapabilities` 是標準庫副作用策略，可限制檔案、環境、控制檯和任務操作。預設配置是寬鬆的；宿主提供限制後，執行路徑必須在任何 JIT 入口執行前實施限制。這是安全策略，而不僅是最佳化提示。`FEngineConfig` 會將這些限制、控制物件和能力策略，與源覆蓋層及請求的 `EExecutionBackend` 一併攜帶。

執行限制、活躍取消控制和受限能力在必要時使用直譯器路徑以保證強制實施。偵錯程式附加、保留初始化區間和有界清理會使有效後端成為 `Vm`；請求後端仍只是配置值，診斷中不得把它報告為有效後端。

## 重入清理與異常轉移

值釋放可能呼叫指令碼解構函式，重入執行並使呼叫框架儲存空間重新配置。跨越這些呼叫時保存框架索引，返回後重新取得框架參照。位元組碼讀取運算元必須保持在目前框架的 `StackBase` 之上；跨框架展開清理則明確釋放保存的運算元。

待處理的回傳值或異常屬於外圍 `finally` 續體。`finally` 內的迴圈和區域捕獲的異常保留該轉移；離開區域的轉移透過正常所有權路徑釋放被取代的值。清理中的未捕獲異常可以取代正在傳播的指令碼異常，而解構函式內部已捕獲的異常必須保留外層異常。執行失敗和 VM 重用必須釋放待處理異常物件。使用 `VeroScriptExceptionTests` 檢查這些情境，包括偵錯器和指令上限模式；受治理的執行仍須獨立計數和檢查 `throw`。

## 數值服務

`VeroScriptNumerics.h` 提供不可變 `FBigInteger`／`FRational` 值及具交易性的 noexcept `Numerics::Try*` 運算。`FNumericError` 使用固定中繼資料；配置失敗不再配置錯誤字串。複製、移動、交換、解構不配置記憶體，移動後來源為零。不可變值可跨宿主執行緒共用；每次運算使用獨立上下文，工作回呼由宿主同步。

`FNumericOperationContext` 結合 `FNumericLimits` 與同步借用的工作回呼。零選擇有限預設值：值位元數 65,536（硬上限 1,048,576）、輸入／輸出各 1 MiB、scratch 16 MiB、每次運算 16,777,216 工作單位。scratch 計量原生暫存與待提交輸出的請求儲存，不是 VM 堆積或程序 RSS。失敗保留輸出。

執行前透過 `VeroScriptVm::SetNumericLimits`、`FEngineConfig::NumericLimits` 或 `VeroScriptSession::SetNumericLimits` 設定。worker 繼承限制；數值工作參與指令、逾時、取消及全域初始化控制。`VeroScriptNumericBindings.h` 提供已驗證來源的 FValue 交換及原生服務。C Interop 提供 `VS_INTEROP_CAP_NUMERICS`、`vs_interop_numeric_parse/to_text/convert`、型別識別與會話限制，字串保留明確長度。子語言 BigInteger／Fraction 不自動對應。獨立 AOT 拒絕數值執行階段操作。

安裝勾點在初始化前透過 `Packages::FLimits::HookNumericLimits` 設定相同服務。套件格式、登錄協定及 JSON 數字規則不變。

```cpp
#include "VeroScriptNumerics.h"

VeroScript::FNumericOperationContext context;
VeroScript::FNumericError error;
VeroScript::FBigInteger value;
bool parsed = VeroScript::Numerics::TryParse("18446744073709551616", value, context, error);
```

## variant / FValueRoot

請參閱[Interop SDK — variant](../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#exported-value-lifetime)。

裝箱前由 VM 驗證 `FVariantTypeDescriptor`。`Variant::Box` 和 `Variant::Snapshot` 重用一般複製服務，不公開可寫載荷。`VisitValueGraph` 遍歷盒、物件、閉包及容器；退休不會在終結執行緒執行腳本。回呼或複製載荷前預留結果槽，隨後無配置提交；外層活動在執行結束後清理退休值。回呼要求關閉工作階段時，當前活動呼叫結束後才完成銷毀。

VM 完整支援語意。Mixed／asmjit 對不支援的 variant 指令安全回退 VM。可攜位元組碼和套件保留宣告與指令。獨立 C++、C#、Python AOT 在產生輸出前明確拒絕 variant 相依。偵錯器提供唯讀 $present、$type、$value 子項；type<variant>().kind() 傳回 "variant"。
呼叫安全點在執行期間保留借用參數與接收物件；退出時保留回傳值、建構結果與可寫參數結果，直到呼叫端能夠登記根，純輸入借用隨呼叫結束。退役值清理失敗會使原本成功的呼叫回報執行階段失敗；既有執行錯誤仍優先保留。關閉時的清理失敗透過宿主診斷串流回報。

## 原生非同步回呼根

合法 native 呼叫可透過 `IScriptCallInvoker::RetainForGeneration` 保活函式及擷取值。只保留傳回的 `FValueRoot`，不保存借用的 invoker、VM 或 Args。根可在其他執行緒退休，讀取及語言清理仍由建立執行緒處理；一般匯出根和完成根共同保護存活圖。

完成根在 native 呼叫安全點清理，`DrainGeneration` 不消耗一般宿主借用根。解構工作累計計入目前執行預算，沿用取消及絕對期限。持久 Session 保留同代次根；裸 VM 頂層執行結束、程式替換、重新載入或關閉使其失效。一般匯出根仍會阻止程式替換；編譯失敗不使舊代次失效，進入遷移後的失效不隨回復而恢復。

## 準備快取與 JIT 執行服務

`VeroScriptProgramPreparation::TryGetCacheInputFingerprint` 只對無需處理器執行、套件操作重播或宿主繫結驗證的已解析輸入開放準備前快取查詢。必須同時使用載入器完整的原始碼與解析路徑指紋、編譯器建置識別；不符合條件時仍執行正常準備及準備結果的相依性驗證。原始碼中繼資料和實際生效的常數求值限制也參與輸入識別。

`ICompiledFunctionServices::ExecuteBlock` 合併有界指令序列的原生／Core 呼叫邊界。每條指令仍正常檢查和計入限制、期限、取消及權限；分支、返回或失敗立即結束序列。受控 Mixed 的預設策略維持原狀。

借用、不保留及隔離工作任務的 JIT 快取先準備入口，到達的被呼叫函式仍可按需編譯。保留的快取只在執行要求相同時重用已完成的準備。未支援或收益不足的路徑繼續使用 VM。

這些 C++ 服務變更要求 Core、後端及 SDK 使用方以相符原始碼重新建置。

整個模組的入口仍遵循設定的預先編譯預算。

`FJitCompiledFunction::bPreferVmForExecution` 允許後端發布不依賴實際引數的 VM 成本選擇。Core 在啟用後端服務前採用快取中的選擇，保留退回統計、正常 VM 檢查和被呼叫函式的獨立分派。

## 執行中斷後的有界清理

請參閱[宿主整合 — 執行中斷後的有界清理](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md#bounded-interruption-cleanup)。

## 型別相容性快取擁有權

`VeroScriptVm::IsActiveObjectTypeAssignableTo` 按模組與 `ProgramGeneration` 快取準確正規類別名稱／介面名稱的相容性。快取歸各執行內容擁有，模組／代次變化或達到 4096 項上限時清空。自有不可變程式允許在活動呼叫外重用；借用程式僅在執行期間快取，並由 `BeginExecutionGovernance` 在呼叫之間清空。別名與非正規名稱仍走權威中繼資料解析。最佳化 JIT 呼叫、值複製或宿主查詢時，須保留這些擁有權及失效條件。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/execution-and-state.md) · [fr-FR](../fr-FR/execution-and-state.md) · [ja-JP](../ja-JP/execution-and-state.md) · [ko-KR](../ko-KR/execution-and-state.md) · [zh-CN](../zh-CN/execution-and-state.md) · [zh-TW](execution-and-state.md)
