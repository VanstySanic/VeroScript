# 預處理器

預處理器在詞法分析和語意分析之前處理原始檔。它支援條件編譯、物件巨集、函式巨集、字串化、標記拼接和模組包含。

## 物件宏

物件巨集把名稱替換為預先定義的標記序列：

```VeroScript
#define VERSION 2
#define MAX_COUNT 64

int version = VERSION;
int capacity = MAX_COUNT;
```

巨集替換發生在編譯器進行類型檢查之前。巨集本身沒有 VeroScript 的靜態型別，替換後的運算式必須能夠通過正常的語法和型別檢查。

## 函式巨集

函式巨集可以接收參數。複雜運算式中的參數應使用括號，避免替換後的運算優先權改變：

```VeroScript
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int value = MAX(3, 8);
```

巨集適合表達簡單的詞法替換；需要型別檢查、重載或除錯能力時，應優先使用函式或泛型函式。

## 字串化與標記拼接

在函式巨集中，`#` 將參數轉換為字串，`##` 將相鄰標記拼接為一個標記：

```VeroScript
#define STR(x) #x
#define CAT(a, b) a##b

int VALUE = 9;
string name = STR(hello);
int result = CAT(VAL, UE);
```

字串化和標記拼接都在巨集展開階段執行。拼接後的結果仍必須是合法的 VeroScript 標記。

## 可變參數宏

函式巨集可以使用 `...` 接收可變參數，並透過 `__VA_ARGS__` 展開：

```VeroScript
#define LOG(fmt, ...) print(fmt, ##__VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

目前實作支援使用 `##__VA_ARGS__` 消除沒有可變參數時多餘的逗號，也支援 `__VA_OPT__(tokens)`：當可變參數非空時展開括號內的標記，否則展開為空白：

```VeroScript
#define LOG(fmt, ...) print(fmt __VA_OPT__(,) __VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

`__VA_OPT__` 只能出現在可變參數巨集的替換清單中，且必須跟隨一對括號。

## 動態內建宏

編譯器也提供兩個以目前來源模組動態產生的巨集：

| 宏 | 展開結果 |
| --- | --- |
| `__FILE__` | 目前模組的規範邏輯路徑；記憶體原始碼沒有路徑時為 `<memory>`。 |
| `__LINE__` | 巨集呼叫所在實體來源行號，從 `1` 開始。 |

它們在每個來源模組的預處理上下文中計算，而不是普通的全域巨集：

```VeroScript
#define REPORT(name) print("{}:{} {}", __FILE__, __LINE__, name)

int main()
{
	REPORT("ready");
	return 0;
}
```

`__FILE__`、`__LINE__`、`__VA_ARGS__` 和 `__VA_OPT__` 是保留名稱，不能被腳本、命令列初始定義、宿主預定義巨集或函式巨集參數重新定義。

## 條件編譯

使用 `#if`、`#ifdef`、`#ifndef`、`#elif`、`#else` 和 `#endif` 控制原始碼是否參與後續編譯：

```VeroScript
#define VERSION 2

#if VERSION >= 2
int feature_level = 2;
#else
int feature_level = 1;
#endif
```

完整的原始碼指令集合是 `#include`、`#define`、`#define_export`、`#undef`、`#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif` 和 `#error`。條件中可用 `defined(NAME)` 或 `defined NAME` 判斷巨集是否存在。運算式會先展開巨集；仍未定義的識別碼按零處理。

`#if` 運算式使用 64 位元整數常量（十進位、C 風格前導零八進位或十六進位）、括號、一元 `+ - ! ~`、算術 `* / % + -`、位移、比較、相等、位元運算 `& ^ |`、短路 `&& ||` 和 `?:`。它不使用腳本字面量中的二進位前綴或數字分隔符規則。實際求值分支中的除零和溢位是錯誤；短路分支不會求值。

作用中分支中的 `#error message` 會立即停止預處理，並在診斷中保留裁剪首尾空白後的訊息；非作用中條件分支會忽略它。訊息不會再次進行巨集展開。`#warning`、`#line` 和 `#pragma`（包括 `#pragma once`）不受支援，並會按具體指令名稱報告。

命令列工具可以使用 `-D` 或 `--define` 提供簡單物件巨集；原始碼檔案仍可定義前文所述的函式式巨集和可變參數巨集：

```powershell
veroscript -D ENABLE_LOGGING=1 script.vs
```

## 包含模組

`#include` 是 `export import` 的別名：匯入並轉匯出公開宣告與公開巨集，不進行文字包含。

```VeroScript
#include "math.vs"

int main()
{
	print(std::clamp_int(15, 0, 10));
}
```

需要表達模組依賴和匯出關係時，建議使用 `import` 和 `export import`，而不是把 `#include` 當作 C++ 文字包含機制使用。

## 公開巨集

<a id="exported-macro-contract"></a>

使用 `#define_export`，定義語法與 `#define` 相同，支援物件巨集、函式巨集及可變參數巨集。普通定義保持私有。顯式 `import` 在路徑字串結束後立即引入目標模組最終公開的巨集；`export import` 同時再匯出它們。`#include` 是 `export import` 的別名，包含公開宣告及公開巨集。`.vslib` 程式庫遵循相同規則。宿主隱式匯入也從業務原始碼開頭引入已確定的公開巨集，但不自動轉匯出。巨集提供模組及其顯式相依先獨立建立介面；提供模組之間的巨集相依仍使用普通 `import`。私有巨集保持私有，衝突沿用普通匯入規則。例如，`veroscript --implicit-import settings.vs main.vs` 讓 `main.vs` 不必撰寫匯入即可使用 `settings.vs` 的公開巨集。

```VeroScript
// settings.vs
#define PRIVATE_VALUE 7
#define_export VALUE 21
#define_export DOUBLE(x) ((x) * 2)

```

```VeroScript
// main.vs
import "settings.vs";
int main()
{
	print(DOUBLE(VALUE));
	return 0;
}
```

替換體在呼叫方目前的巨集環境中展開，不捕獲私有輔助巨集；需要時明確匯出輔助巨集。`__FILE__` 及 `__LINE__` 指向呼叫位置。同名的不同原始定義會衝突，即使替換體相同；替換匯入的定義前先 `#undef`。重複匯入相同原始定義具冪等性，`#undef` 後再次匯入會恢復定義。普通 `#define` 重新定義後，最終定義為私有；`#undef` 從公開介面移除定義。

沒有公開巨集定義的循環依賴仍受支援。循環原始碼閉包內若出現任何 `#define_export`（包含非活動指令），公開介面無法確定，會拒絕載入；需要解除原始碼匯入環。程式庫在建置時確定條件匯出的結果；應用套件不攜帶公開巨集介面。

<a id="preprocess-resource-limits"></a>

## 前置處理資源限制

預設每個檔案的展開輸出（含中間取代文字）最多為 32 MiB；同一次編譯的所有檔案共用 268,435,456 個內部計量單位的工作預算。超過任一上限會回報前置處理錯誤並拒絕編譯。內部單位不是 VM 指令數或執行時間保證。原生嵌入宿主可透過 `FPreprocessContext::Budget` 提供 `FPreprocessBudget`；零表示上限為零，不表示無限。這些限制與腳本編譯擴充的執行預算分別計算。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/preprocessor.md) · [fr-FR](../../fr-FR/language-basics/preprocessor.md) · [ja-JP](../../ja-JP/language-basics/preprocessor.md) · [ko-KR](../../ko-KR/language-basics/preprocessor.md) · [zh-CN](../../zh-CN/language-basics/preprocessor.md) · [zh-TW](preprocessor.md)
