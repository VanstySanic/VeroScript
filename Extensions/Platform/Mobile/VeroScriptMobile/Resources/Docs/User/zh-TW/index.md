# 實驗性行動嵌入

Android 和 iOS 適配器保持實驗性狀態。使用與目標匹配的 SDK 產物，並在實際部署環境中驗證應用程式。`FSessionOptions` 提供絕對標準庫目錄和原始碼資源快照。真實路徑或虛擬路徑均可使用，但全部虛擬原始碼須透過 `Sources` 提供；入口與相對匯入須使用一致的絕對標籤。選用快取目錄須真實、可寫且為絕對路徑，空值停用快取；資源更新後重新建立 runner。

```cpp
VeroScript::Mobile::FSessionOptions options;
options.StdlibDirectory = "/app/resources/stdlib";
VeroScript::Mobile::FRunner runner(std::move(options));
auto result = runner.Execute("int main(){ print(42); return 0; }", "/app/scripts/main.vs");
```

`FRunner` 同時只允許一次執行，每次建立新 Session，全域狀態不保留。Execute 呼叫同步完成；Android 與 Apple 平台可能在內部工作執行緒執行，宿主回呼不能假定於呼叫執行緒或 UI 執行緒執行。建構及執行可能讀取資源或編譯，應安排在非 UI 執行緒。物件存活期間可由另一執行緒要求取消；銷毀前等待執行與取消呼叫結束。`FSession` 的建立、一般呼叫和銷毀屬於建立執行緒，宿主須提供至少 8 MiB 原生編譯堆疊，其取消狀態會鎖存。回呼自行執行權限和取消檢查；取消不搶先中斷編譯，也不強制結束阻塞原生回呼。

啟用快取後，入口與匯入原始碼的內容均參與失效判斷。命中會重用位元碼，但仍可能載入和剖析資源以核對目前內容，不能假定完全略過啟動階段的編譯準備工作。無法使用或損壞的快取退回一般編譯路徑。

預設：VM、1,000,000 條指令、100 ms、呼叫深度 256。只允許主控台輸出；輸入、檔案、環境、目錄變更和工作停用。輸出及診斷共享 1 MiB，錯誤副本最多 64 KiB。原始碼資源與單次入口各限制 16 MiB，最多 4096 資源，路徑最多 4096 位元組。這些限額不是 VM 堆配額或完整沙箱。宿主須透過 `vs_interop_locale_load_catalog_directory` 明確載入解包目錄並選擇語言；沒有環境、cwd 或可執行檔旁資源探索，未設定時診斷維持英文。iOS 真機與模擬器均禁止動態 JIT 程式庫和原生發碼。

## 行動本地化的明確設定

行動 SDK 攜帶六語言目錄，但不隱含載入。建立引擎、執行腳本前，將完整目錄解包或複製至應用管理的真實目錄並明確載入；不能傳 APK 資源或虛擬原始路徑。Android AAR 位於 `veroscript/localization`，iOS SDK 位於 `assets/localization`。Android 提供 `VeroScriptEngine.loadCatalogDirectory(path)`、`setLanguage(tag)`，傳回布林成功值；iOS/C++ 使用 `vs_interop_locale_load_catalog_directory`、`vs_interop_locale_set_language`，錯誤緩衝區用 `vs_interop_buffer_destroy` 釋放。檢查每個傳回值。語言選擇為程序級而非各引擎獨立，失敗保留原選擇。未載入時未知訊息維持英文；編譯診斷與適配驗證錯誤使用同一選擇，腳本輸出不變。記憶體配置失敗可能保留英文備援。 即使適配器內容脈絡已翻譯，作業系統錯誤詳情仍可能使用系統函式庫語言。

## 受管理位元組儲存限額

預設單一緩衝區 16 MiB、保留總量 32 MiB。Core 行動端選項透過 `MaxBufferBytes` / `MaxRetainedBytes` 設定，0 表示無限。配額計算擁有容量、外部位元組與擴充峰值，不約束整個 VM 堆積。預設僅允許主控台輸出的原則關閉原生位址。

## 受控原生執行與靜態 AOT

<a id="mobile-jit-selection"></a>

行動端預設保留 VM。Android 可明確選取獨立的 `vs-asmjit` AAR 並指定 JIT 檔案／目錄。僅保留一份相符的 `libc++_shared.so`；使用檔案路徑載入 JIT 的應用須解包原生程式庫（應用設定 `jniLibs.useLegacyPackaging = true`）。iOS 裝置與模擬器均禁止動態 JIT。

<a id="mobile-core-aot"></a>

`VeroScriptCppCoreAot.h` 的 `CompileSourceToCoreAot`／`GenerateCoreAotSourceBundle` 輸出目標 C++、精確配對的 `.vsapp`／`.vsbc` 與涵蓋報告 JSON。以目標 SDK 編譯輸出，選取 Mixed 並執行配套封裝。未涵蓋函式、模組及欄位初始化使用 VM；嚴格原生模式拒絕未涵蓋工作。此路徑保留 Core 語義，與獨立原始碼匯出不同。

平台 API：[Android](../../../../../VeroScriptAndroid/Resources/Docs/User/zh-TW/index.md#android-native-selection) · [iOS](../../../../../VeroScriptIOS/Resources/Docs/User/zh-TW/index.md#ios-native-selection)。

請參閱[可選 JIT 後端 — 受控原生執行與靜態 AOT](../../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/zh-TW/index.md#controlled-native-execution)。

完整行動 SDK 封裝提供編譯產生 C++ 所需的 `aot/include`，包含 Core 服務標頭及相符的目標建置身分；與 Interop 標頭一同加入包含路徑。這只是標頭集合，仍僅連結一份 Interop 執行階段。僅安裝 AAR 時，須另外取得相符標頭集合。

VM 選擇獨立於 `ExecutionBackend` 與 JIT。`FSessionOptions.VmBackendPath` 留空時保留產品明確裝配的靜態 VM；非空時僅接受絕對原生程式庫檔案／目錄，設定失敗不會退回。Android 完整建構函式提供 `vmBackendPath`，iOS 對應 `VSMConfiguration.vmBackendPath`；iOS 預設繼續使用靜態組合。

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
