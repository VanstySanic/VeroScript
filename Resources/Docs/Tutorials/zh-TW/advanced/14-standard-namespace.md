# 14：使用 `std` 標準命名空間

本篇把一個使用未限定模組宣告的小腳本整理為建議的公開 `std` 寫法。 `std::` 是原始碼層面的標準命名空間別名；它不會建立新的容器類型，也不會改變執行階段行為。

## 第一步：準備一個評分腳本

建立 `scores.vs`：

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	array<int> scores = {81, 92, 88};
	optional<int> selected = optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	print(format("selected={}, root={}", selected.get(), root));
	return 0;
}
```

先檢查並執行它：

```powershell
veroscript --check scores.vs
veroscript scores.vs
```

第一個版本已經限定了模組所屬的數學函式。模組所屬的標準函式庫函式只能透過其公開的 `std` 命名空間存取；只有精簡的核心內建 API 保持全域可用。

## 第二步：遷移到 `std::`

只修改名稱限定，不改變演算法：

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	std::array<int> scores = {81, 92, 88};
	std::optional<int> selected = std::optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	std::print(std::format("selected={}, root={}", selected.get(), root));
	std::print(std::strings::contains("scores", "core"));
	return 0;
}
```

再次執行相同的檢查和執行命令。 `std::array<int>` 與 `array<int>` 指向相同的語言類型；`std::print` 與 `print` 也指向相同的執行階段函式。 `std::` 在編譯階段解析，不會增加額外的執行階段分派。

數學、I/O、檔案系統、字串、路徑、並行、時間、執行緒、環境和強型別輸入入口均只能透過對應命名空間存取。不提供它們的標準函式庫未限定寫法，也不提供 `std::now` 這類扁平相容名稱；使用者程式碼仍可定義同名但無關的未限定函式。

## 第三步：理解匯入邊界

標準命名空間別名不等於模組匯入。原始碼標準庫函式仍然需要匯入提供它們的模組：

- `math.vs` 提供 `sqrt`、`sin`、`cos`、`tan`、`floor`、`ceil`、`round`、`trunc`、`pow`、`exp`、`log`、`log2`、`asin`、`acos`、`atan2`、`isfinite`、`isnan`、`pi`、`e`、`clamp` 和 `clamp_int`。
- `string.vs` 透過 `std::strings` 提供字串查詢，包括 `last_find`、`repeat`、`remove_prefix` 和 `remove_suffix`。
- `io.vs` 透過 `std::io` 提供流操作，透過 `std::filesystem` 提供檔案系統操作。
- `path.vs` 向 `std::filesystem` 補充路徑操作；`concurrent.vs` 使用 `std::concurrent`。

執行階段主控台函式不需要匯入模組。無換行輸出使用
`std::console::write` 和 `std::console::error`，發布等待中的輸出使用
`std::console::flush()`，偵測互動式終端使用 `std::console::is_interactive()`。

例如，刪除 `import "math.vs";` 後，`std::sqrt` 不會因為帶有 `std::` 限定而自動載入數學模組。先匯入模組，再使用限定名稱，能讓依賴關係保持清晰。

## 第四步：遷移容器和通用值類型

標準命名空間也覆蓋映射、集合、佇列、通道、任務和反射資訊類型：

```VeroScript
int main()
{
	std::map<string, int> totals = {"math": 2, "io": 1};

	std::print("math={}, io={}", totals["math"], totals["io"]);
	return 0;
}
```

新代碼應統一使用分組後的 `std` 寫法。實作宣告仍保留在各自模組內部的未限定名稱，使命名空間圖層能讓所有後端解析到相同符號身分。

## 第五步：明確值保護和轉換

目前範例還把 `std::readonly<T>` 別名與全域轉換內建函式組合使用。`std::` 並不是所有內建函式的前綴，因此 `cast<T>`、`try_cast<T>` 和 `const_cast<T>` 應保持未限定寫法。

```VeroScript
std::readonly<int> published_score = std::readonly<int>(selected.get());
double exact_score = cast<double>(scores[1]);
std::optional<int> narrowed = try_cast<int>(exact_score);

const std::array<int> explicitly_unlocked = {13};
// 只有確定底層儲存可寫時，才使用這個有意的 const 解除。
const_cast<std::array<int>>(explicitly_unlocked).push(published_score.get());
```

`cast<T>` 要求執行轉換；`try_cast<T>` 返回可由呼叫端檢查的 `optional<T>`。`const_cast<T>` 不會轉換值，也不會執行執行階段類型檢查，只會移除編譯期的 const 存取能力，因此必須在明確且受控的情境中使用。完整規則請參閱[型別轉換](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-reference/type-conversions.md)。

## 根限定名稱

開頭的 `::` 會從全域/根作用域開始查找，跳過目前命名空間、本地別名和 `using namespace` 的回退結果：

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

需要明確引用根層級宣告時使用 `::Name`。`std::readonly<T>` 是 `readonly<T>` 包裝器的標準命名空間寫法，是原始碼別名，不是第二個執行階段型別。


## 練習

1. 把範例中的 `std::map` 換成 `std::unordered_map` 不適用；請查閱完整別名表，選擇目前支援的容器並說明原因。
2. 增加一個 `std::set<int>`，把重複分數過濾掉。
3. 匯入 `string.vs`，呼叫 `std::strings::contains` 或 `std::strings::starts_with`。

完整別名表請見[`std` 標準命名空間](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/standard-namespace.md)，可執行範例請參考 [`examples/standard_namespace.vs`](../../../../../VeroScript/Resources/Scripts/examples/standard_namespace.vs)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/advanced/14-standard-namespace.md) · [fr-FR](../../fr-FR/advanced/14-standard-namespace.md) · [ja-JP](../../ja-JP/advanced/14-standard-namespace.md) · [ko-KR](../../ko-KR/advanced/14-standard-namespace.md) · [zh-CN](../../zh-CN/advanced/14-standard-namespace.md) · [zh-TW](14-standard-namespace.md)
