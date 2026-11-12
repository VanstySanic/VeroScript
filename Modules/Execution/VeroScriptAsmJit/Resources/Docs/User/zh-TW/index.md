# 可選 JIT 後端

## 選擇與相容性

AsmJit 是與 Runtime 分開發行的選用 JIT 後端。執行策略與後端分別選擇；專案設定繼承、`auto`、`none`、明確 ID／路徑及回退行為見 [CLI 後端參考](../../../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md#cli-backend-policy)。本頁說明 AsmJit 套件、原生相容性與手動載入。

在 `MyProject` 中已安裝並啟用相容 JIT 時，以下命令分別使用繼承的選擇和明確的自動選擇：

```text
veroscript --path ./MyProject --backend=jit ./MyProject/main.vs
veroscript --path ./MyProject --jit-backend=auto --backend=mixed ./MyProject/main.vs
```

手動部署可攜式封存檔時：從 `Release/Modules/Execution/Jit/<target>/<version>/archives/` 取得功能套件，與 Runtime 分開解壓縮，不提供獨立安裝程式；明確選擇解壓後的程式庫檔案或目錄。僅版本號相同不能保證相容：須符合宿主的 Core、架構、編譯器、標準程式庫與執行程式庫組態。可攜式後端可以在不產生原生機器碼的情況下最佳化執行。

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
veroscript --jit-backend="/path/to/backend-library" --backend=jit main.vs
```

## 路徑與更換

以下檔案／目錄載入規則針對明確路徑，與模組環境的 ID／自動選擇相互獨立。指定檔案可固定一個模組。指定目錄則按 UTF-8 字典序搜尋根目錄及至多兩層子目錄，選擇第一個相容且可用的候選。Windows 程式庫名稱以 `VeroScriptJit` 開頭，Linux/macOS 以 `libVeroScriptJit` 開頭；最多接受 4096 個候選。不可讀子項會略過。相對路徑以目前工作目錄為基準；不透過環境變數、登錄或隱含預設路徑選擇後端。只在閒置時更換；失敗保留原選擇。嵌入 API 中的空路徑表示停用。

## 嵌入 API

C++ 使用 `SetJitBackendPath`；C 使用 `vs_interop_session_set_jit_backend` 和 `vs_interop_session_get_jit_backend_info`；C# 使用 `SetJitBackend` 和 `GetJitBackendInfo`；Python 使用 `set_jit_backend` 和 `get_jit_backend_info`。執行策略單獨設定。只要工廠、執行個體、工作執行緒或保留的可執行成品仍需要模組，模組就維持載入。

官方套件名稱與發布封存前綴統一為 `vs-asmjit`（例如 `vs-asmjit-0.1.1-win-x64.zip`）。依名稱選擇時使用的服務後端 ID 仍為 `asmjit`。

## 受控原生執行與靜態 AOT

<a id="controlled-native-execution"></a>

限額、取消、權限、框架與值由 Core 管理。不支援或未插樁的函式在進入前回退；故障後不重播函式。受控 Mixed JIT 須透過 `FJitPolicy::bEnableControlledMixed` 明確啟用（C ABI `enable_controlled_mixed`、C# `EnableControlledMixed`、Python `enable_controlled_mixed`）；首版逐步原生基線可能慢於 VM。明確的 Jit 不受此成本策略限制。`ControlledNativeExecutions`／`AotExecutions` 計算實際原生入口。Android／iOS SDK 與裝置驗收仍待完成，桌面測試不能替代。

請參閱[實驗性行動嵌入 — 受控原生執行與靜態 AOT](../../../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/zh-TW/index.md#mobile-jit-selection)。

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
