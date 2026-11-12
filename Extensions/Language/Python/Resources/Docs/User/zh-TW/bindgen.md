# Python BindGen

`VeroScriptPythonBindGen` 將受支援的靜態 Python 宣告轉換為 Binding Catalog
v1 JSON。它不會 import 模組、執行裝飾器或建立 Python 物件；Catalog 是宿主
API 契約，不是執行時實作。

支援公開函式、類別、dataclass、`IntEnum`/`IntFlag`、帶註解的執行個體欄位、建構函式、
執行個體/靜態/類別方法和 property。動態或無型別簽名、可變參數、預設參數、
模組全域量、不支援的繼承與無法表達的型別會失敗。

```powershell
python -m veroscript_python_bindgen host_api.py `
  --module-name host --catalog host.bindings.json `
  --api-reference host.vs.txt --validate --veroscript veroscript
```

`--timeout <seconds>` 用來限制外部驗證時間，必須是有限的正數。

## 專案模式命令

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py `
  --project-root . `
  --output-dir generated/bindings `
  --validate `
  --veroscript veroscript
```

`--output-dir` 選擇專案模式；單一輸入也可使用，多個輸入時必須指定。`--project-root` 與 `--manifest` 僅用於專案模式，後者可覆寫清單輸出路徑。此模式不能與 `--catalog`、`--api-reference` 或 `--module-name` 混用。輸出先一併暫存，失敗時盡力回復；清單必須與對應的 Catalog 和參考檔案配套使用。

專案模式輸出合併的 `bindings.json`、每個輸入的 API reference 和
`bindgen.manifest.json`。函式庫 API 包括 `generate_bindings`、
`generate_bindings_from_source`、`generate_bindings_from_file`、
`generate_binding_project`、`validate_catalog`、`bind_module` 和 `bind_modules`。
這兩個繫結輔助函式只使用應用程式明確匯入的模組；BindGen 本身不會匯入或執行
被分析的來源。`bind_modules` 會在修改工作階段前驗證專案中的所有目錄擁有者
模組都已提供。

繫結時，列舉類別必須繼承 `IntEnum` 或 `IntFlag`，且成員名稱與整數值必須和
Catalog 完全一致。Catalog 中的 instance method 必須仍是執行個體方法，
`staticMethod` 必須仍是 static/class method。發現不一致時會在修改工作階段前失敗。

專案中多個模組宣告同名型別時，請使用 `models.Item` 這類限定名稱註釋。
有歧義的未限定名稱會被明確拒絕，即使名稱恰好是 `Int8` 等數值別名。
目前模組宣告的型別優先於專案的未限定名稱別名和數值別名。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](bindgen.md)
