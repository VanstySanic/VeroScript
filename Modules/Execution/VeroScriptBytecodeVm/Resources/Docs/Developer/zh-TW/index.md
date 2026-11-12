# 位元組碼 VM 維護

Core 管理語言操作、值、邏輯框架、執行治理、清理及分派契約。本模組擁有 `Private/VeroScriptBytecodeVmExecution.cpp` 中的完整直譯器、成員執行與診斷。靜態程式庫及動態模組只連結標頭執行契約 `VeroScriptExecutionContract`，不連結第二份 Core 二進位；`BytecodeVm::CreateFactory` 接收實際所屬 Core 的服務。

`FVmInvocation` 接收 Core 已準備的框架，須保留域／上下文／程式世代、呼叫權杖及框架邊界，不得重複準備引數或再次進入框架。第一個語意操作前設定 `bEntered`；僅未進入執行時可傳回 `NotEntered`。接續執行、插樁、治理及未完成清理／reset 仍遵循 Core 契約。

每個作用中上下文有獨立實體，同步重入使用同一作用中實體。官方後端可在同域／同執行緒完全閒置時重新繫結；替換前解除舊程式，實體存續期間保留工廠／模組。配置實體的工廠負責銷毀，查詢描述符提供配置模組中的工廠銷毀函式。

動態入口是 `veroscript_vm_query`。使用工廠前檢查產品 `CoreVersion`、描述符大小、`BuildVmModuleAbi()`、ID／版本及能力。產品編碼為 major:8/minor:8/patch:16，與 C ABI 版本編碼不同；產生的實作身分追蹤模組原始碼。即使版本均為 `0.1.1`，公開標頭、編譯器／STL／CRT、平台與配置也須相符。

`VS_VEROSCRIPT_VM_VERSION` 是獨立 VM 產品的 CMake 快取設定，使用數字 `major.minor.patch` 格式，預設 `0.1.1`。它提供產生的 `VEROSCRIPT_VM_VERSION`、工廠／模組版本描述及已安裝 CMake 套件版本。全量封裝器傳入已登記的 `Vm` 業務版本，並拒絕舊快取或不相符產物。該產品版本與 `CoreVersion`、服務 ABI、實作／公開標頭指紋分別維護；修改版本號不能使不相容二進位變得可用。

獨立 CMake 專案使用已安裝 `VeroScriptExecutionContract`，根整合也向測試提供實際 Core。`VeroScriptVmBackendStaticTests` 與選用 `VeroScriptVmBackendDynamicTests` 涵蓋兩種組裝；`Resources/Tests/Contract.cpp`、`Package.py` 涉及契約及套件使用。變更時核對能力、進入前回退、重入、治理、清理／reset、擁有者生命週期與安裝使用方。iOS／Web 採靜態組裝，桌面結果不能代替目標平台及裝置驗收。

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/zh-TW/vm-backends.md)

<!-- docs-navigation -->

---

[Developer](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
