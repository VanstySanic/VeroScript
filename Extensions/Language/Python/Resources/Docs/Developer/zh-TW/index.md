# VeroScript Python 技術文件


本目錄面向維護者和繫結作者，描述原始碼與二進位制契約，不是 Python 使用者教程。Python/VeroScript 語法、API 與模組名、選項、JSON 鍵和診斷均按原始碼原樣書寫。

## 模組歸屬

- `VeroScriptPython`：穩定 C Interop ABI 之上的 CPython 介面卡，擁有會話、型別值、容器、包、回撥、反射、保留可呼叫物件、序列化和 Python 支援的宿主類。
- `VeroScriptPythonAot`：發射獨立 Python 源或多檔案原始碼包；輸出不載入 VM、Interop 共享庫或 CPython 擴充套件。
- `VeroScriptPythonFrontend`：靜態解析受支援的帶型別 Python 3.11 宣告子集，不匯入或執行輸入模組。
- `VeroScriptPythonBindGen`：將前端宣告轉為 Binding Catalog 後設資料和每輸入 API 引用。
- `VeroScriptPythonTranspiler`：將受支援帶型別 Python 子集降級為 VeroScript，並由真實編譯器驗證。

## 靜態工具邊界

Frontend、BindGen 和 Transpiler 是靜態工具，絕不匯入、執行或利用輸入模組的執行時副作用。不支援的動態特性、無型別簽名、無法保持的任意精度語義、async、反射和繼承必須明確失敗，不能近似。專案模式先解析完整模組圖；相對包佈局、模組名、匯入、原始碼對映、API 引用和清單條目相互一致，任何解析、降級、匯入解析或最終編譯失敗後均不替換輸出。

## 執行時與生成程式碼邊界

CPython 介面卡在允許時釋放 GIL，Python 回撥時重新取得 GIL；建立執行緒限制、可呼叫物件所有權、回撥重入和共享 Python 狀態同步是公共契約。worker 回撥不得呼叫建立執行緒受限的會話操作。獨立 AOT 執行時僅為文件規定的可執行子集保持 VeroScript 整數、容器、異常、模組和併發語義；生成 worker 採用 OS 執行緒，隔離指令碼全域性/靜態欄位，並保留 channel 身份作為顯式同步共享機制。

## Python 物件與產物細節

主要執行時物件是 `Session`、`Value`、`Package`、保留 callable 控制代碼、`HostParameter` 和宿主類/函式註冊物件。會話須在建立執行緒顯式關閉；空/窄容器使用顯式元素型別，遍歷及 `keys()`/`values()`/`items()` 返回擁有快照。宿主/變更回撥各有異常與執行緒規則，回撥擁有狀態必須自行同步。

AOT Bundle 有清單和相容執行時檔案，檔案先暫存，再分階段替換空的一般目錄或具有有效 VeroScript Python AOT 清單且無外來內容的目錄；直譯器 `__pycache__` 是唯一容忍的額外生成物。最終路徑不可為連結或 Windows 目錄聯接；提交期間輸出路徑可能短暫不存在，失敗時嘗試復原。生成程式選擇使用者 Python 直譯器，不發現或打包 Python SDK。

## 打包與版本

wheel/原始碼分發須攜帶匹配的原生擴充套件、Interop 庫、Python 源、型別資訊、標準庫和本地化資源；`VeroScriptVersion.h` 是語義版本依據。前端專案模式每輸入生成一個 AST 產物及專案清單；BindGen 生成合並目錄、每輸入 API 引用和 `bindgen.manifest.json`；Transpiler 保留相對包佈局，解析專案相對匯入，發射原始碼對映，僅在完整圖驗證後寫入 `transpile.manifest.json`。

## 詳細頁面

- [Python 執行時與靜態工具](runtime-and-tools.md)
- [Python AOT 與打包](aot-and-packaging.md)
- [Python 工具鏈與包契約](toolchain-and-packaging.md)

## 依據

以 Python 源包、原生介面卡、前端 AST、生成器/轉譯器測試、CMake、包後設資料和分發驗證工具為準。支援子集、執行緒規則、生成產物、包佈局或 ABI 對映變更時必須更新本頁。


[Android/iOS 中使用 Python](mobile.md)

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
