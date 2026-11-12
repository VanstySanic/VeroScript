# 03：變數、條件和循環

本篇用一個小型成績統計程序，把變數、靜態型別、條件分支和迴圈放在一起。重點不是記住某個例子，而是學會把一個問題拆成「狀態、判斷和重複動作」。

## 第一步：準備數據

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	print("total={}", total);
	return 0;
}
```

`total` 和 `count` 都是 `int`。 `i` 只在 `for` 迴圈內部有效，這種局部作用域可以減少變數互相干擾。

## 第二步：加入判斷

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	if (total >= 15)
	{
		print("pass: {}", total);
	}
	else
	{
		print("retry: {}", total);
	}
	return 0;
}
```

`if` 的條件必須能得到布林結果。比較運算式會產生 `bool`；不要把整數當成條件使用來代替明確判斷。

## 第三步：讓資料來自陣列

```VeroScript
int main()
{
	int scores[4] = {72, 88, 91, 65};
	int total = 0;

	for (int i = 0; i < len(scores); i = i + 1)
	{
		total = total + scores[i];
	}

	double average = to_double(total) / len(scores);
	print("average={}", average);
	return 0;
}
```

`scores` 是固定長度原生陣列。 `len(scores)` 傳回它的長度，索引從 `0` 開始。需要動態增加元素時，可以改用 `array<int>`。

## 第四步：使用浮點字面量

當 `.` 後面緊跟十進位數字時，可以省略浮點字面量的整數部分。後綴仍然決定儲存類型：`.5f` 是 `float`，而 `.5` 和 `.5e2` 是 `double` 值。

```VeroScript
int main()
{
	float ratio = .5f;
	double half = .5;
	double fifty = .5e2;

	print("ratio={}, half={}, fifty={}", ratio, half, fifty);
	return 0;
}
```

`0.5f` 和 `.5f` 的值與類型相同。小數點後必須保留數字；單獨的 `.` 仍然用於成員存取。

## 第五步：處理腳本例外

對於腳本能夠描述並復原的失敗，可以使用 `throw`。例外值可以跨越函式呼叫傳播，並由相符的
`catch` 處理。無論操作成功或因例外離開，都必須執行的清理工作應放在 `finally` 中：

```VeroScript
void validate_score(int score)
{
	if (score < 0)
	{
		throw "score must not be negative";
	}
}

int main()
{
	try
	{
		validate_score(-1);
	}
	catch (const string& message)
	{
		print("handled: {}", message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

這段程式會先輸出處理後的訊息，再輸出 `cleanup`。例外型別重要時使用型別處理器；`catch (...)`
只應作為最後的萬用處理。處理器內不帶運算式的 `throw;` 會重新擲回原始值。

完整例外範例請參閱 [`examples/exceptions.vs`](../../../../../VeroScript/Resources/Scripts/examples/exceptions.vs)。

## 第六步：使用 `assert` 檢查不變條件

`assert(condition[, message])` 是執行階段檢查。條件必須是 `bool`，可選訊息必須是 `string`：

```VeroScript
int main()
{
	int total = 15;
	assert(total >= 0, "total must be non-negative");
	print("ready");
	return 0;
}
```

條件為真時會繼續執行且不產生輸出。條件為假時會報告執行階段錯誤並停止目前執行；它不是腳本
例外，因此不能由 `catch (...)` 處理。

## 你已經用到的控制流概念

- `if`：根據條件選擇一條路徑。
- `for`：在明確的初始化、條件和遞增規則下重複執行。
- `try`/`catch`：處理 `throw` 擲回的腳本值，`finally`：執行清理工作。
- `assert`：檢查布林不變條件，並在失敗時報告執行階段錯誤。
- `return`：在函式中提前傳回結果，下一篇會詳細使用。

## 練習

1. 在循環中計算最大分數和最小分數。
2. 只統計大於等於 `60` 的分數。
3. 把固定陣列改成 `array<int>`，並使用範圍遍歷。
4. 把 `0.5` 這樣的十進位字面量改寫為 `.5`，並在需要 `float` 值的地方使用 `.5f`。
5. 為拒絕負分的函式新增型別 `catch` 和 `finally` 區塊。
6. 為分數計算中的不變條件新增帶訊息的 `assert`。

如果想要對照完整陣列範例，可以閱讀 [`examples/arrays.vs`](../../../../../VeroScript/Resources/Scripts/examples/arrays.vs)。語言細節請見[變數](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/variables.md)、[控制流](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/control-flow.md)和[陣列](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/arrays.md)。

下一篇會把重複邏輯抽成函式，並使用動態容器：[函式和容器](04-functions-and-collections.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/language/03-values-and-control-flow.md) · [fr-FR](../../fr-FR/language/03-values-and-control-flow.md) · [ja-JP](../../ja-JP/language/03-values-and-control-flow.md) · [ko-KR](../../ko-KR/language/03-values-and-control-flow.md) · [zh-CN](../../zh-CN/language/03-values-and-control-flow.md) · [zh-TW](03-values-and-control-flow.md)
