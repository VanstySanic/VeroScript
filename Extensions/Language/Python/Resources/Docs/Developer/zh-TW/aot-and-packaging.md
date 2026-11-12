# Python AOT 與打包

## 獨立發射器

`VeroScriptPythonAot` 發射可讀 Python 模組，其中含有小型相容執行時，不載入 VeroScript VM、Interop 庫或 CPython 擴充套件。控制包裝器可以從可執行檔案路徑、SDK 根、`bin` 目錄、環境變數或 `PATH` 查詢使用者提供的 CLI。

使用 `FCodegenOptions` 的 SDK 入口會將 `Bindings` 中的註記定義傳入處理階段，
包括 `CaseSensitive` 名稱規則。僅註記中繼資料不會引入執行階段相依性；獨立輸出
不支援執行階段宿主函式與宿主型別。

## Bundle 契約

Bundle 模式寫出 `__main__.py`、`__init__.py`、`_veroscript_runtime.py`、`_veroscript_program.py` 和 `python-aot.manifest.json`。完整匯入圖作為一個程式檢查。所有檔案先暫存，再分階段替換一般目錄。目標只能是空目錄，或是帶有結構有效 VeroScript 清單且沒有外來內容的目錄；已識別的直譯器 `__pycache__` 檔案屬於可容忍的生成內容。

最終路徑不可為符號連結或 Windows 目錄聯接。提交期間會先移開原目錄，輸出路徑可能短暫不存在；提交失敗時嘗試復原原目錄。

## 分發契約

wheel 與原始碼分發必須包含版本匹配的原生擴充套件、Interop 庫、Python 源、型別資訊、標準庫和本地化目錄。`VeroScriptVersion.h`、包後設資料和匯入執行時版本必須一致。釋出驗證檢查支援的 CPython/平臺矩陣、已安裝包的型別介面和原始碼分發重建。

Python 獨立執行階段將數學定義域／溢位情況對應為 Core 浮點結果，不外洩 Python `ValueError`／`OverflowError`：零的對數產生負無窮，冪溢位保留奇整數指數的符號情況，floor／ceil／trunc 保留有符號零。Python 與 C# 產生的 NaN 文字遵循包括 Darwin 在內的目標平台拼寫。透過後端回歸維護這些發射細節，公開有限值檢查請見 [Math](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/math.md#floating-results)。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/aot-and-packaging.md) · [fr-FR](../fr-FR/aot-and-packaging.md) · [ja-JP](../ja-JP/aot-and-packaging.md) · [ko-KR](../ko-KR/aot-and-packaging.md) · [zh-CN](../zh-CN/aot-and-packaging.md) · [zh-TW](aot-and-packaging.md)
