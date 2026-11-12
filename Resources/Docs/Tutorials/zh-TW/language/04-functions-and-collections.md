# 04：函式與容器

當 `main` 開始變長，最有效的整理方式通常不是增加註釋，而是把有明確職責的程式碼提取為函式。本篇會寫一個求和函式，並用 `array<int>` 儲存可變數量的資料。

## 第一步：寫一個求和函式

```VeroScript
int sum(array<int> values)
{
	int result = 0;
	for (int value : values)
	{
		result = result + value;
	}
	return result;
}

int main()
{
	array<int> values = array<int>();
	values.push(10);
	values.push(20);
	values.push(30);

	print("sum={}", sum(values));
	return 0;
}
```

函式宣告先寫回傳類型，再寫名稱和參數。 `sum` 接收 `array<int>`，最後回傳一個 `int`。

## 第二步：讓容器成長

`array<int>()` 建立空動態陣列，`push` 在末尾新增元素。你也可以使用花括號初始化：

```VeroScript
int main()
{
	array<int> values = array<int>{10, 20, 30};

	values.push(40);
	for (int& value : values)
	{
		value = value + 1;
	}

	print("size={}", len(values));
	print("first={}", values[0]);
	return 0;
}
```

普通範圍遍歷 `for (int value : values)` 得到元素副本；加上 `&` 後，循環變數成為元素別名，對它的修改會寫回容器。

## 第三步：使用預設參數

```VeroScript
void print_values(array<int> values, string label = "values")
{
	print(label);
	for (int value : values)
	{
		print(value);
	}
}

int main()
{
	array<int> values = array<int>{1, 2, 3};
	print_values(values);
	print_values(values, "numbers");
	return 0;
}
```

預設參數讓呼叫者可以省略常用設定。預設值必須與參數類型相容，呼叫時仍可明確傳入不同值。

## 選擇容器

先用 `array<T>` 表示有順序的動態資料；需要按鍵查找時考慮 `map<K, V>`，需要去重時考慮 `set<T>`。不要因為容器名稱很多就事先複雜化設計，先依照訪問方式選擇。

## 練習

1. 寫一個 `average` 函式，處理空陣列時回傳 `0.0`。
2. 寫一個函式，把陣列中所有負數改成 `0`，使用引用範圍遍歷。
3. 將成績依名字儲存到 `map<string, int>` 中，並輸出一個指定名字的成績。

動態陣列的完整行為請參考 [`examples/dynamic_array.vs`](../../../../../VeroScript/Resources/Scripts/examples/dynamic_array.vs)，引用遍歷見 [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs)。容器總覽請見[資料型別](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/data-types.md)。

下一篇將把資料和行為組織成自己的類型：[結構體、類別和介面](05-structs-classes-and-interfaces.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/language/04-functions-and-collections.md) · [fr-FR](../../fr-FR/language/04-functions-and-collections.md) · [ja-JP](../../ja-JP/language/04-functions-and-collections.md) · [ko-KR](../../ko-KR/language/04-functions-and-collections.md) · [zh-CN](../../zh-CN/language/04-functions-and-collections.md) · [zh-TW](04-functions-and-collections.md)
