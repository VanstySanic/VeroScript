# C++ Transpiler

`VeroScriptCppTranspiler` 將能夠保持語意的嚴格 C++17 子集轉換為 VeroScript 原始碼。它共用 [C++ 前端](frontend.md) 的 AST 和型別對映，但獨立於 BindGen 與語言無關的 Interop ABI。

```powershell
VeroScriptCppTranspiler Resources/Examples/Basic.cpp `
  -o build/Basic.vs `
  --source-map build/Basic.vs.map.json
```

產生結果包含 VeroScript 原始碼和可選的宣告級原始碼對映。預設會由 VeroScript 編譯器校驗，只有解析、降級和校驗全部成功後才寫出結果。專案模式先解析並合併所有輸入，再為每個輸入產生 `.vs` 與原始碼對映，並產生確定性的 manifest；manifest 將產生模組列為隱式匯入並啟用專案級存取。

專案 manifest 使用穩定欄位 `version`、`kind`、`allowProjectWideAccess`、`modulePaths`、`implicitImportPaths` 和 `files`。`files` 的每個元素包含 `input`、`source` 和 `sourceMap`。manifest、產生原始碼和原始碼對映必須來自同一次轉換；產生檔案欄位中的路徑為相對於專案的路徑。

專案模式範例：

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp `
  --output-dir build/generated `
  --manifest build/generated/transpile.manifest.json
```

manifest 結構範例如下：

```json
{
	"version": 1,
	"kind": "veroscript.transpiled-project",
	"allowProjectWideAccess": true,
	"modulePaths": ["Types.vs"],
	"implicitImportPaths": ["Types.vs"],
	"files": [
		{
			"input": "Source/Types.cpp",
			"source": "Types.vs",
			"sourceMap": "Types.vs.map.json"
		}
	]
}
```

CLI 也接受可重複的 `--attribute-map cpp=vero` 對映。`veroscript::` 和 `vero::` 命名空間中的屬性會直接轉換；其他屬性命名空間需要明確對映。預設會校驗產生原始碼；只有在由獨立驗證階段負責校驗時才使用 `--no-validate`。

CLI 選項包括單檔輸出的 `-o`/`--output`、原始碼對映的 `--source-map`、專案模式的 `--output-dir`、manifest 的 `--manifest`、可重複的 `--attribute-map`、`--no-validate`、`--version` 和 `--help`。專案模式會為每個輸入產生原始碼和原始碼對映。

C++ API 中，`FTranspileOptions` 也控制產生原始碼校驗、記錄在原始碼對映和診斷中的產生原始碼路徑、額外的已知宿主型別和列舉型別、已知型別別名以及明確屬性對映。專案選項會將校驗策略和屬性對映套用於專案輸出。

支援按值程式碼、命名空間、列舉、類別/結構體、建構函式、方法、全域變數、容器、`this->member`、純量 `static_cast`、`typedef`/`using` 別名、普通和 `u8` 原始字串，以及型別化 `throw`、`try`/`catch`、全捕獲和重新拋出。別名樣板會在結構化宣告型別中展開。只使用 `typename`/`class` 型別參數的類別、結構體和自由函式樣板（包括末尾預設型別參數）會轉換為 VeroScript 開放泛型。宣告屬性會轉換為 VeroScript 註解；參數屬性由 BindGen 保留，因為原始碼轉換沒有對應的參數註解槽位。專案模式會匹配類別外定義及靜態資料定義。

所有權操作、任意指標、RTTI、協程、高階 C++ 樣板特性、共用所有權、未解析的 `extern`、配置/儲存構造、參照欄位或全域變數、無法表示的 `constexpr`/`consteval`/`constinit`/`noexcept`、`goto` 以及不支援的屬性或 friend 形式會被拒絕；工具不會靜默刪除無法表達的語意。

非型別樣板參數、參數包、特化、SFINAE、concept、相依編譯期表達式、方法區域樣板和明確函式樣板實參呼叫不在支援範圍內。泛型別名若出現在不透明函式本體、friend token 序列或初始化器中，也會被拒絕。

專案模式中，普通方法的宣告保留在宣告模組，本體保留在實作模組；建構／解構與靜態資料仍依核心的同檔案要求合併。預設驗證會依 manifest 的可見性策略編譯最終模組圖，並將錯誤映射回原 C++ 檔案。無法保留建構／解構查找作用域的合併會被拒絕。泛型類別的類別外定義仍不支援，前端會明確報錯。

專案合併不是連結器。限定的命名空間定義仍屬於該命名空間；內部連結函式或變數發生衝突時回報 `cpp.merge.internal_linkage_collision`，不會重新命名實體或依輸入順序選擇定義。多個輸出一起提交，避免產生的原始碼與原始碼對映不同步。

## 數值型別與整數字面值

區域變數及 `static_cast` 的基本數值型別使用與宣告簽名相同的平台映射。整數字面值依工具編譯平台的 C++17 資料模型選擇型別，再轉換為腳本後綴；八進位、二進位及數字分隔符保留其數值。十六進位浮點與 `long double` 字面值會被明確拒絕。數值轉換要求輸入值位於目標型別範圍內，超出範圍依腳本規則報錯；依賴 C++ 窄化截斷或混合有號提升的程式碼需要明確改寫。

`std::pair<T,U>` 映射至核心的軟內建 `pair<T,U>`。可見的同名同參數個數使用者模板會捕獲該型別時，轉譯以 `cpp.transpile.standard_pair_shadowed` 明確失敗，包括專案模式和關閉產生原始碼驗證的情況。應重新命名使用者模板或提供明確適配器；不同參數個數或不相關命名空間中的模板仍可共存。

VeroScript 樣板參數不能遮蔽全域型別或普通別名。這類 C++ 宣告會在輸出前回報 `cpp.transpile.template_parameter_shadow_unsupported`，包括專案模式及關閉產生原始碼驗證的情況。轉譯前請重新命名 C++ 樣板參數。別名樣板按結構解析，並保留自身參數的繫結關係。

結構化宣告中的使用者型別採用絕對限定名稱，包括巢狀樣板參數，避免在輸出命名空間內重新繫結。基底類別保留原始查找作用域。目標繼承語法無法表示被區域同名型別遮蔽的全域基底類別，此時會在輸出前回報 `cpp.transpile.global_base_shadowed`；關閉產生原始碼驗證時也會拒絕。
內建容器基底類別和相依型別參數本身的基底類別會回報 `cpp.transpile.base_type_unsupported`；繼承要求已知的類別或結構型別。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](transpiler.md)
