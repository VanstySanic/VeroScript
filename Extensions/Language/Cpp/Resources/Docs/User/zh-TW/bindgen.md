# C++ BindGen

`VeroScriptCppBindGen` 將支援的 C++ 宣告轉換為 VeroScript 繫結目錄。繫結目錄是編譯器和工具的中繼資料，不是執行階段實作；產生的 callback stub 也不是原生呼叫 thunk，真實 C++ 函式仍需由宿主配接器註冊。

`GenerateBindings` 接收 AST；`GenerateBindingsFromSource`、`GenerateBindingsFromFile`、`GenerateBindingsFromFiles` 接收原始碼；`GenerateBindingProject` 合併多個輸入並回傳一個權威 `CatalogJson`、可選的 `ApiReferences`、確定性的 `ManifestJson`、診斷以及 `Succeeded()`。`FBindGenOptions::bEmitApiReference` 控制是否產生面向人的 API 參考。

`FBindGenProjectLayout` 指定合併目錄、API 參考目錄及專案清單的輸出位置。編譯器和 IDE 應使用合併後的權威目錄；逐輸入參考只是文件檢視，不能取代合併後的宣告集合。

專案 manifest 的穩定欄位包括 `version`、`kind`、`catalogRole`、`catalog`、`inputs` 和 `apiReferences`。`apiReferences` 的每個元素包含 `input` 和 `path`。合併後的 catalog 只產生一次；reference 是可選的面向使用者的可讀檢視。

專案模式會將合併後的 catalog、每個輸入的 API reference 和 manifest 作為確定性
結果集寫出。manifest 的範例如下：

```json
{
	"version": 1,
	"kind": "veroscript.cpp-binding-project",
	"catalogRole": "merged",
	"catalog": "bindings.json",
	"inputs": ["Include/Types.hpp"],
	"apiReferences": [
		{
			"input": "Include/Types.hpp",
			"path": "api/Types.vs.txt"
		}
	]
}
```

```powershell
VeroScriptCppBindGen Resources/Examples/BindingInput.hpp Resources/Examples/BindingExtras.hpp `
  --catalog build/BindingInput.bindings.json `
  --api-reference build/BindingInput.vs.txt
```

若要為每個輸入產生 API reference 並同時寫出專案 manifest：

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

CLI 選項包括必需的 `--catalog <output.json>`、`--api-reference <output.vs.txt>`、`--api-reference-dir <directory>`、`--manifest <output.json>`、`--version` 和 `--help`。兩個 reference 選項不能同時使用。

工具保留限定名稱、列舉身分和底層型別、成員類別、建構函式、運算子、const 方法、`ref`/`const ref` 參數、普通型別別名和帶型別參數的別名樣板。別名預設參數無法表示在目錄模式中，因此會被拒絕。型別、列舉、欄位、函式和參數上的 C++ 屬性會寫入結構化註記；無命名空間屬性以及 `veroscript::`/`vero::` 保留命名空間會被接受，其他廠商命名空間需要對映。列舉初始化器支援整數、可移植的單字元字面值、先前的列舉項、括號、一元、算術、移位、位元、布林、邏輯、關係和相等運算，並會檢查 `int64` 溢位。無值屬性仍作為旗標參數保留，具名屬性值會在目錄中往返保存。

原始指標、回傳參照、宿主到宿主繼承、刪除成員、參照欄位、自由全域變數、具名方法重載、自由函式重載、預設參數、內部連結自由函式以及超出底層型別範圍的列舉值會被拒絕。支援的列舉初始化器會經過檢查後求值。完整目錄欄位與執行階段註冊邊界見[宿主繫結](bindings.md)。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](bindgen.md)
