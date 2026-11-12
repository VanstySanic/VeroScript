# C# 轉換器

`VeroScriptCSharpTranspiler` 將嚴格且可進行靜態分析的 C# 12 子集轉換為
VeroScript 原始碼。它結合 `VeroScriptCSharpFrontend` 使用 Roslyn 的語意資訊，
不會進行文字 Token 替換。它是獨立的原始碼轉換工具，與 C# 執行時期 SDK、
BindGen 以及 VeroScript 到 C# 的 AOT 後端分開。

## 單一檔案轉換

```powershell
veroscript-csharp-transpiler Program.cs -o Program.vs `
	--source-map Program.vs.map.json `
	--veroscript veroscript
```

`--veroscript` 指定用來驗證產生原始碼的 VeroScript 可執行檔或 SDK 根目錄。預設
會啟用驗證。只有解析、降級、驗證和輸出暫存全部成功後，才會寫入 VeroScript
檔案與 source map；轉換失敗時不會取代既有輸出。

公開函式庫 API 提供 `CSharpTranspiler.TranspileSource` 和
`CSharpTranspiler.TranspileFile`。結果包含產生的原始碼、source-map JSON 和
`CSharpDiagnostic` 診斷。`CSharpTranspileOptions` 可控制驗證、驗證器、產生路徑、
逾時時間和 C# 解析選項。

## 專案轉換

```powershell
veroscript-csharp-transpiler Models/User.cs Services/UserService.cs `
	--project-root . `
	--output-dir generated/veroscript `
	--veroscript veroscript
```

專案模式會將所有輸入作為單一 C# 編譯單元解析，解析跨檔案符號，產生明確的匯入，
並保留相對於 `--project-root` 的路徑。每個輸入會產生一個 VeroScript 原始碼檔案
和一個 `.map.json` source map。預設清單為 `transpile.manifest.json`，也可以用
`--manifest` 指定路徑。所有產生檔案與清單會作為一個輸出集合提交；暫存失敗時會
回復。

函式庫 API 的專案入口是 `CSharpTranspiler.TranspileProject`，使用
`CSharpTranspileProjectOptions` 設定。結果包含產生檔案記錄、具決定性的清單 JSON
和診斷。每個輸入都必須位於專案根目錄內，且不能重複提供相同路徑。

如果兩個輸入會產生相同的相對輸出名稱，後一個名稱會加入具決定性的路徑雜湊後綴。

## 命令列選項

| 選項 | 範圍與意義 |
| --- | --- |
| `-o`、`--output` | 單一檔案輸出路徑。單一檔案模式必須提供，專案模式不能使用。 |
| `-h`、`--help` | 輸出使用方式並結束。 |
| `--version` | 輸出轉換器版本並結束。 |
| `--source-map` | 單一檔案 source map 路徑。預設為 `<output>.map.json`，專案模式不能使用。 |
| `--output-dir` | 啟用專案模式並選擇產生輸出目錄。 |
| `--manifest` | 專案清單路徑。預設為輸出目錄中的 `transpile.manifest.json`。 |
| `--project-root` | 用於計算專案相對輸出名稱的根目錄。所有輸入都必須位於其中。 |
| `--reference` | 新增中繼資料組件；多個組件需要重複此選項。 |
| `--language-version` | 選擇解析時使用的 C# 語言版本。 |
| `--veroscript` | 選擇作為驗證器的 VeroScript 可執行檔或 SDK 根目錄。 |
| `--timeout` | 設定產生原始碼驗證逾時時間（秒）；預設值為 120。 |
| `--no-validate` | 略過編譯器驗證；僅在另有明確環節驗證產生的原始碼時使用。 |

當輸入超過一個，或提供 `--output-dir` 或 `--manifest` 時，會選擇專案模式。
專案模式一定需要 `--output-dir`。

## 輸出中繼資料

source map 會記錄原始碼路徑、產生路徑和對應陣列。每個對應會記錄產生的行，
以及對應原始碼的路徑、行和欄：

```json
{
	"version": 1,
	"source": "Program.cs",
	"generated": "Program.vs",
	"mappings": [
		{
			"generatedLine": 1,
			"sourcePath": "Program.cs",
			"sourceLine": 1,
			"sourceColumn": 1
		}
	]
}
```

專案清單使用以下穩定欄位：

```json
{
	"version": 1,
	"kind": "veroscript.csharp-transpiled-project",
	"projectWideAccess": true,
	"files": [
		{
			"input": "Models/User.cs",
			"output": "Models/User.vs",
			"sourceMap": "Models/User.vs.map.json"
		}
	]
}
```

清單中的路徑是專案相對路徑，並統一使用 `/` 作為分隔符號。應將同一次轉換產生的
清單、原始碼和 source map 一起保留。

## 支援的 C# 範圍

目前子集包括：

- 命名空間、列舉、類別、結構，以及作為命名空間層級宣告使用的 static class；
- public、protected、private 欄位、對稱的自動屬性、建構函式、靜態成員、執行個體
  成員和方法；
- 無約束泛型型別和自由函式、基本型別以及 VeroScript 標準容器型別；
- 區域型別推斷、呼叫、物件和集合建立、索引、轉型、算術、指定、條件、迴圈、
  包括 `do`/`while` 與具有多個宣告或反覆運算式的傳統 `for`、`foreach` 和
  `switch`；
- 一般的 `throw`、`try`、`catch` 和 `finally`；
- 型別別名、常數區域變數、`params`、委派/函式型別，以及運算式或區塊 lambda；
- 回傳參考的方法、參考區域變數、標籤和 `goto`，並降級為對應的 VeroScript
  別名與控制流程形式。

`Console.Write` 與 `Console.WriteLine` 的換行行為差異會被保留。對於支援的 lambda，
C# 以參考捕獲的行為也會保留。
字串常值會保留 NUL 與其他支援的控制字元。未配對的 UTF-16 代理項沒有無損的
VeroScript UTF-8 表示，因此會安全地被拒絕。

可表達的明確泛型成員呼叫會保留具體型別引數。例如
`Helpers.Pick<long>(1)` 會產生 `Helpers::Pick<int64>(1)`。巢狀型別宣告會以
診斷拒絕，不會產生語意不明或無效的 VeroScript；需要轉換時請將宣告移到
namespace 範圍。

## 明確邊界

無法忠實表示的功能會回報 `VSCT2001` 診斷並拒絕，而不會被靜默近似。目前包括：

- `async`/`await`、工作、迭代器和 LINQ 查詢語法；
- 委派宣告和事件；
- 可為 null 的參考語意、unsafe/指標程式碼、反射和 `dynamic`；
- 泛型約束和介面；
- `catch` 篩選器；
- 非對稱屬性或計算屬性；
- 建構函式鏈；
- 分散在多個檔案中的 partial 型別。

`--reference` 可重複新增中繼資料組件，`--language-version` 選擇 C# 語法版本，兩者都不會擴充受支援的 VeroScript 語意。`--timeout` 限制產生原始碼的驗證時間。只有另一個明確環節負責編譯器驗證時，才使用 `--no-validate`；單純產生成功不代表輸出能夠執行。

## 整數轉譯與數值支援邊界

Transpiler 保留 C# 預設 unchecked 整數語意：加減乘、取負、遞增遞減、複合指定和整數強制轉型按解析後的位元寬度環繞；位移計數以 31 或 63 遮罩。複合指定在右運算元之前僅求值一次目標及其舊值；自動屬性輸出為欄位。整數除法與餘數在除數為零或有號最小值/-1 溢位時擲回可捕捉的指令碼例外。整數值的 `double` 常數仍保留浮點型別。

<a id="numeric-and-console-boundaries"></a>

明確的 `checked`/`unchecked` 內容、非常數浮點除法與餘數、非常數浮點轉整數，以及非有限浮點常數均以 `VSCT2001` 拒絕，包括關閉驗證時。有限的編譯期數值常數由 Roslyn 摺疊。解析使用預設 unchecked 編譯設定，不匯入取用專案的溢位檢查編譯選項。

列舉的整數強制轉型按宣告的底層位元寬度環繞。無號 64 位元列舉、列舉算術或位元運算變更、非常數 `double` 轉 `float` 窄化，以及非常數 64 位元整數轉 `float` 均以 `VSCT2001` 拒絕。Console 支援單一字串、整數或布林參數，以及無參數 `Console.WriteLine()`；`Console.Write` 使用無換行的 `std::console::write`，`Console.WriteLine` 使用帶換行的 `std::print`。布林文字使用 `True`/`False`，字串串接及 `+=` 同樣如此。標準函式使用限定名稱，防止原始碼中的同名函式攔截輸出或整數文字轉換。格式化/多參數 Console 多載，以及浮點、列舉、物件或陣列的格式化均明確拒絕，不會悄然改變 CLR 文字語意。

C# 轉譯器在整數與浮點數的混合比較前套用 Roslyn 的運算元提升，確保進入核心比較前已經完成 C# 的 float 捨入。

數值條件分支套用 Roslyn 選擇的隱式轉換，保留整數位寬與 float 捨入，執行時只求值選中的分支。條件分支中的非常量 64 位元整數轉 float 同樣明確拒絕。

## 連結輸出路徑

Windows 的 `\\?\`、`\\.\` 和磁碟區 GUID 路徑別名會依最終檔案系統位置比較。這些別名同樣不能略過驗證；相異的輸出路徑仍受支援。

BindGen 與 Transpiler CLI 在暫存輸出前解析既有的目錄與檔案連結。symbolic link 或 Windows junction 路徑不能覆寫輸入原始碼，也不能重複指向同一輸出；連結目錄內相異的輸出路徑仍受支援。

`--timeout` 和 `ValidationTimeout` 對每次驗證呼叫同時限制驗證器結束及標準輸出、標準錯誤的讀取。即使啟動器已結束、子行程仍持有重新導向管道，到期也會以 `VSCT2004` 失敗。驗證失敗不會替換 CLI 輸出檔案。

靜態欄位與自動屬性只接受省略或編譯期常數初始化器。非常數靜態初始化器及靜態建構函式會以 `VSCT2001` 拒絕，即使關閉驗證也是如此，因為 VeroScript 全域初始化無法保持 CLR 型別初始化的時機、副作用及相依順序。請從 `Main` 或應用程式控制的方法中明確初始化動態靜態狀態。

字串、陣列和已映射集合的欄位與自動屬性必須具有明確的非 null 初始化器。已映射集合包括 `List<T>`、`Dictionary<TKey, TValue>`、`SortedDictionary<TKey, TValue>`、`HashSet<T>`、`Queue<T>` 和 `Stack<T>`。這些型別省略初始化或賦值為 null 會以 `VSCT2001` 拒絕，即使關閉驗證也是如此，因為把 CLR null 參考變成空字串或空容器會改變相等比較和例外行為。支援的非 null 陣列、已映射集合與 null 進行相等或不等比較仍受支援；字串與 null 的比較不屬於支援子集。

## 輸出檔名衝突

Transpiler 的專案輸出檔名在所有主機上都會忽略大小寫，並按 Unicode NFC 正規化後比較。名稱按輸入路徑順序分配；衝突時加上確定性的路徑雜湊後綴，該名稱仍被占用時再加上數字後綴。請使用傳回記錄或資訊清單中的實際檔名，不要自行推算。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](transpiler.md)
