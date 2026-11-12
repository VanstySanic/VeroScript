# 06：引用、引用傳回和 Lambda 擷取

本篇透過幾個可以直接執行的小程式，理解 VeroScript 中「同一個值的別名」和「閉包共享外層狀態」之間的關係。引用適合原地修改已有物件或元素；Lambda 擷取則決定閉包如何儲存外層綁定。

## 第一步：建立局部引用別名

```VeroScript
int main()
{
	int score = 40;
	int& alias = score;
	alias += 2;

	print("score={}, alias={}", score, alias);
	return 0;
}
```

`alias` 不是一份新的整數，而是 `score` 的引用別名。透過 `alias` 寫入，會直接改變 `score`。引用綁定後不能改為引用另一個整數；如果你需要獨立的值，應使用普通變數。

## 第二步：把引用傳給函式

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	int value = 40;
	add_one(&value);
	print(value);
	return 0;
}
```

`int&` 表示可寫參考參數，呼叫時使用 `&value` 明確表示傳遞別名。只讀場景可以使用 `const int&`；它避免複製，同時禁止函式透過該參考修改物件。

## 第三步：回傳一個可寫引用

引用也可以作為函式傳回值，但傳回的物件必須在呼叫結束後仍然存在：

```VeroScript
int counter = 1;

int& counter_ref()
{
	return counter;
}

int main()
{
	counter_ref() += 4;
	int& alias = counter_ref();
	alias += 1;
	print(counter);
	return 0;
}
```

這裡回傳的是全域變數 `counter`，所以呼叫者可以繼續透過回傳值來修改它。不要傳回對普通函式局部變數的引用，因為函式傳回後該局部變數已經離開作用域。容器元素和物件成員也可以在其擁有者仍然有效時作為參考傳回。

## 第四步：用引用遍歷原地修改容器

```VeroScript
int main()
{
	array<int> values = array<int>();
	values.push(1);
	values.push(2);

	for (int& value : values)
	{
		value += 10;
	}

	print(values[0]);
	print(values[1]);
	return 0;
}
```

`for (int& value : values)` 讓 `value` 成為陣列元素的別名，因此迴圈內的賦值會寫回陣列。去掉 `&` 後，循環變數會依值取得元素，修改不會寫回原容器。透過索引傳遞元素時也使用相同的顯式形式，例如 `add_one(&values[0])`。

## 第五步：讓 Lambda 捕捉外層狀態

```VeroScript
function() -> int make_counter()
{
	int running = 40;
	return [&]() -> int
	{
		running++;
		return running;
	};
}

int main()
{
	function() -> int next = make_counter();
	print(next());
	print(next());
	return 0;
}
```

`[&]` 表示以引用方式擷取使用到的外層局部變數。 VeroScript 會把被擷取的局部綁定提升到安全的共享儲存中，所以 `make_counter` 回傳後，`next` 仍然可以安全地存取並修改 `running`。

如果只有一個變數需要共享修改，也可以寫成 `[&running]`；需要混合策略時，可以使用 `[=, &running]`，讓其他變數以值擷取而只讓 `running` 按引用擷取。

## 第六步：擷取成員函式的接收者

接收者擷取遵循其型別本身的語意：

```VeroScript
class Counter
{
public:
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

struct Snapshot
{
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

int main()
{
	Counter counter = Counter();
	counter.value = 1;
	function() -> int live = counter.getter();
	counter.value = 2;

	Snapshot snapshot = Snapshot();
	snapshot.value = 3;
	function() -> int copied = snapshot.getter();
	snapshot.value = 4;

	print(live());
	print(copied());
	return 0;
}
```

類別閉包會保留物件，因此輸出 `2`；結構閉包會保留值快照，因此輸出 `3`。
沒有擷取的 `[]` Lambda 不能存取 `this`。

## 什麼時候使用哪一種

- 需要獨立副本：使用普通值變數或按值擷取。
- 需要在函式中修改呼叫者已有的值：使用可寫引用參數。
- 需要遍歷並原地修改容器元素：使用引用範圍變數。
- 需要讓傳回的閉包繼續共用外層可變狀態：使用按引用擷取。
- 唯讀且不希望複製較大物件：考慮 `const T&`，但確保被引用物件的生命週期足夠長。

## 練習

1. 寫 `swap_values(int& left, int& right)`，交換兩個整數，並用 `swap_values(&a, &b)` 呼叫。
2. 為 `array<int>` 寫一個引用範圍循環，把所有元素乘以 `2`。
3. 修改 `make_counter`，增加一個值擷取的 `step`，實作 `[=, &running]` 的計數器。
4. 嘗試傳回普通局部變數的引用，觀察 `--check` 給出的診斷，並改為傳回全域變數或仍有效的成員。
5. 在建立閉包後修改 `Counter` 的欄位，並與 `Snapshot` 的結果比較。

完整規則請見[引用和引用語意](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/references.md)和[Lambda 和函式值](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/lambdas.md)。可執行範例請參考 [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs)、[`examples/reference_returns.vs`](../../../../../VeroScript/Resources/Scripts/examples/reference_returns.vs) 和 [`examples/lambda.vs`](../../../../../VeroScript/Resources/Scripts/examples/lambda.vs)。

下一篇學習如何分割原始檔：[模組、匯入和預處理器](07-modules-and-preprocessor.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/language/06-references-and-captures.md) · [fr-FR](../../fr-FR/language/06-references-and-captures.md) · [ja-JP](../../ja-JP/language/06-references-and-captures.md) · [ko-KR](../../ko-KR/language/06-references-and-captures.md) · [zh-CN](../../zh-CN/language/06-references-and-captures.md) · [zh-TW](06-references-and-captures.md)
