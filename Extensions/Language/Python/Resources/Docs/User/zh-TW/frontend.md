# Python 前端

`VeroScriptPythonFrontend` 是 Python BindGen 與 Python 轉 VeroScript 工具共用
的靜態前端。它不會 import 或執行輸入模組，而是產生由 VeroScript 定義的
Python 宣告模型。

前端保留來源位置、模組與 import、型別化函式、類別、dataclass、整數列舉、
帶註解的欄位與參數、裝飾器、文件和正規化型別註解。模組頂層執行、動態或
無法解析的型別，以及無法表示的語法都會產生診斷。

```powershell
python -m veroscript_python_frontend input.py -o input.python-ast.json
```

多個輸入可以作為專案解析：

```powershell
python -m veroscript_python_frontend package/models.py package/service.py `
  --project-root . --output-dir generated/frontend
```

工具先暫存每個輸入的 AST 和 `frontend.manifest.json`，再提交寫入；失敗時嘗試
復原原有輸出。API 提供
`parse_source`、`parse_file`、`parse_files`。需要結構分析時使用前端，需要
宿主綁定目錄時使用 BindGen，需要生成 VeroScript 時使用 Transpiler。

## 診斷回退

使用 `--lang` 選擇診斷語言，其優先順序高於 `VERO_LANG` 與系統語言。缺失或無法使用的翻譯訊息回退至英文；診斷代碼與原始碼位置在各語言中保持一致。

`--lang` 作用於使用 VeroScript 目錄的工具訊息，不會翻譯 `argparse` 說明和引數剖析錯誤，也不會翻譯 Python、作業系統或外部編譯器轉送的文字；這些內容沿用各自的語言行為。

## 選項與專案輸出

單檔模式透過 `--module-name` 指定邏輯模組名稱，`--version` 顯示工具版本。專案模式保留套件結構，拒絕重複輸入、重複模組名稱以及 `--project-root` 以外的檔案。`--output-dir` 不能與 `-o` 或 `--module-name` 一起使用。輸出採用 UTF-8，在取代前統一暫存。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](frontend.md)
