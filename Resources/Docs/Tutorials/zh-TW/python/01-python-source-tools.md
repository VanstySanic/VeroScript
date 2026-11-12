# 01：建立 Python 原始碼工具流程

前提：安裝相符的 Python Frontend、BindGen、Transpiler 套件，並確保可呼叫 `veroscript`。在可寫目錄準備受支援的型別化 `host_api.py`、`app.py`、`package/models.py`、`package/service.py`；更換輸入時一致修改路徑。執行期綁定另需設定好的 Python SDK Session，參閱 [Python SDK 指南](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/index.md)。

## 1. 分析宣告

```powershell
python -m veroscript_python_frontend host_api.py -o generated/host.python-ast.json
```

檢視產生的 AST，確認模組、函式、類別和來源位置。輸入模組不會被 import 或執行。

## 2. 產生宿主契約

```powershell
python -m veroscript_python_bindgen host_api.py `
  --catalog generated/bindings.json `
  --api-reference generated/host.vs.txt `
  --validate --veroscript veroscript
```

將 Catalog 中的函式與宿主要註冊的實際 callable 對照。Catalog 不會取代宿主實作。

## 3. 轉換原始碼

```powershell
python -m veroscript_python_transpiler app.py `
  -o generated/app.vs --source-map generated/app.vs.map.json `
  --veroscript veroscript
veroscript --check generated/app.vs
```

無法保持語意的 Python 功能不會被靜默近似，而會回報診斷。

## 4. 使用專案模式

當模組互相 import 時，請將所有輸入作為同一個專案傳入，讓宣告和 import
解析保持一致：

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py --project-root . --output-dir generated/bindings --validate --veroscript veroscript
python -m veroscript_python_transpiler package/models.py package/service.py --project-root . --output-dir generated/veroscript --veroscript veroscript
```

請將產生的 manifest、來源對映和輸出作為對應的一組保存，不要混用不同輸入集合
產生的構件。在執行階段，由應用程式管理模組 import，再一次註冊合併後的 Catalog：

註冊片段假設 `session` 是設定好的 Session，`models`／`service` 是明確匯入 `package.models`／`package.service` 得到的模組物件，`project_bindings` 是解析 `generated/bindings/bindings.json` 得到的映射，或 `generate_binding_project` 的返回結果。原始碼工具不會替應用程式匯入這些模組。

```Python
from veroscript_python_bindgen import bind_modules

bind_modules(session, (models, service), project_bindings)
```

此 helper 會在修改工作階段前檢查模組涵蓋範圍，並在連接跨模組成員前註冊專案型別。

## 第 5 步：驗證執行階段繫結與值

執行階段註冊的列舉應使用 `IntEnum` 或 `IntFlag`；一般 `enum.Enum` 會被拒絕。
列舉成員名稱與整數值必須和 Catalog 完全一致，Catalog 中的 instance method 和
`staticMethod` 也必須分別對應實際的執行個體方法與 static/class method。

`Value(None)` 表示腳本的 `null`，`Value.priority_queue("int", [3, 1, 2])` 建立
優先佇列。將 `None` 指派給不可為 null 的值會擲出 `TypeError`。建議把這些檢查加入
宿主測試，及時發現 Catalog 與執行階段模組不一致。

## 練習

1. 新增帶註解的列舉，確認 Catalog 包含其值。
2. 新增未註解的區域變數並閱讀診斷。
3. 以專案模式處理兩個相關模組並比較 manifest。
4. 將產生的 Catalog 載入 Python 宿主，確認註冊實作與產生的簽章一致。

如果需要獨立執行的 Python 構件，請繼續閱讀
[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/aot.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/python/01-python-source-tools.md) · [fr-FR](../../fr-FR/python/01-python-source-tools.md) · [ja-JP](../../ja-JP/python/01-python-source-tools.md) · [ko-KR](../../ko-KR/python/01-python-source-tools.md) · [zh-CN](../../zh-CN/python/01-python-source-tools.md) · [zh-TW](01-python-source-tools.md)
