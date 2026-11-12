# Python 轉換器

`VeroScriptPythonTranspiler` 將嚴格的靜態型別 Python 3.11 子集轉換為
VeroScript 原始碼。它共用 Python 前端的宣告分析，與 Python 執行時期繫結及
VeroScript 到 Python 的 AOT 後端彼此獨立。

支援型別化函式、一般類別、整數列舉、帶註解的欄位和區域變數、建構
函式、方法、字面值、呼叫、指定、條件、`while` 和一般運算式。Python 任意
精度整數、動態 import、async、推導式、產生器、描述器、property、多重繼承、
反射和未註解區域變數不會被靜默近似，而是回報診斷。

`@dataclass` 會被明確拒絕，因為尚未實作其自動產生的建構函式、相等比較等
執行時期行為。包含 `metaclass` 的類別關鍵字引數也因執行時期行為尚未實作而明確拒絕。
請在支援的子集內使用明確編寫建構函式和方法的一般類別。
內建 `str(...)` 轉換會被明確拒絕，因為尚未實作 Python 的格式化語義；
在目前模組宣告或明確匯入的同名使用者 `str` 類別或函式仍可正常呼叫。

類別本體的欄位預設值接受不可變純量字面值、受支援的純常數運算式及已知整數列舉成員；
共用容器、物件、任意名稱參照和函式呼叫會被明確拒絕。請在明確的 `__init__` 中初始化每個實例的值；
容器先建立為帶註解的區域變數，再指定給實例欄位。

```powershell
python -m veroscript_python_transpiler input.py `
  -o input.vs --source-map input.vs.map.json `
  --veroscript veroscript
```

`--timeout <seconds>` 用來限制外部編譯器驗證時間，必須是有限的正數。函式庫 API
在兩種選項型別中都使用 `timeout_seconds` 表示相同設定。

預設使用真正的 VeroScript 編譯器驗證產生的原始碼。專案模式解析提供的相對
import，保留輸出模組結構，產生 source map 與 `transpile.manifest.json`，並
驗證完整模組圖。API 提供 `transpile`、`transpile_source`、`transpile_file`、
`transpile_files`。

使用 `as` 為專案模組或宣告改名的匯入會被明確拒絕；請使用原名。
`import typing as t` 等中繼資料匯入仍受支援。

## 驗證與專案模式

預設啟用外部編譯器驗證；只有另一個環節明確負責驗證產生的原始碼時，才使用 `--no-validate`。`--timeout` 預設為 `120.0` 秒，限制每次外部呼叫，而非整個專案的總耗時。原始碼、來源對應檔和資訊清單在取代前統一暫存，寫入失敗時嘗試還原；無法解析或存在歧義的匯入會被拒絕。

```powershell
python -m veroscript_python_transpiler package/models.py package/service.py `
  --project-root . `
  --output-dir generated/veroscript `
  --veroscript veroscript
```

`len(str)` 依 Unicode 碼點計數（補充平面字元和組合標記各依碼點計入），不傳回 UTF-8 位元組數。字串字面值長度不增加執行階段相依性；其他已宣告的字串值使用線性的 Core UTF-8 緩衝區輔助邏輯，並匯入標準程式庫 `string.vs`。該字串路徑需要 O(n) 時間及 O(n) 暫存空間，並非 Python 的 O(1) 長度查詢；它需要 Core 執行階段，目前獨立 Python AOT 會明確拒絕；產生的檔案或既有 `string.vs` 不得遮蔽該相依性。容器長度保持直接 Core 操作。無法確定運算式的字串／容器型別時，先將值放入具有型別註記的區域變數。

當 Python 的全域、匯入或區域繫結名稱為 `print`、`len`、`abs`、`min` 或 `max`，而 Core 會將呼叫解釋為內建函式時，轉譯會明確拒絕。一般方法和已宣告的 `str` 仍可呼叫。僅有註記、沒有預設值的欄位必須在讀取前明確初始化；本子集不模擬 Python 缺少屬性或完整的確定賦值語義。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](transpiler.md)
