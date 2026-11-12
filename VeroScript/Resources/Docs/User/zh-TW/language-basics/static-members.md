# 靜態成員

使用 `static` 宣告的成員屬於型別本身，而不是某一個物件實例。靜態欄位只有一份類型級儲存；靜態方法不需要透過物件呼叫。

## 靜態工具類型

`static class` 和 `static struct` 用於宣告不可具現化的工具容器。內部宣告的所有欄位和方法都會按靜態成員處理，並透過類型名稱和 `::` 存取：

```VeroScript
public static class Math
{
	public double Square(double value)
	{
		return value * value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Square(4.0));
	print(Limits::MaxRetries);
}
```

靜態類別或靜態結構體不能被具現化，也不能作為基底類型使用。它們不能是泛型，不能宣告建構函式、解構函式、`using` 成員或預設初始化區塊。欄位不能使用 `mutable`，方法不能使用 `virtual`、`override`、`final`、純虛擬或 `const` 等實例修飾詞。存取區域仍然有效：靜態類別遵循類別的預設存取權限，靜態結構體遵循結構體的預設存取權限。如果類型需要實例、繼承或實例狀態，應使用一般的 `class` 或 `struct`。

## 靜態欄位

靜態欄位可以使用類型名稱和 `::` 存取。下面的 `Total` 會被 `Counter` 的所有實例共用：

```VeroScript
class Counter
{
public:
	static int Total = 0;
	int id;

	Counter()
	{
		Counter::Total = Counter::Total + 1;
		id = Counter::Total;
	}
};

Counter first = Counter();
Counter second = Counter();
print("{} {}", first.id, Counter::Total);
```

靜態欄位仍然受到存取說明符限制。需要從類型外部存取時，應將欄位宣告在 `public` 區域中。

## 靜態方法

靜態方法透過型別名稱呼叫，不依賴某個物件：

```VeroScript
class Counter
{
public:
	static int Total = 0;

	static int Peek()
	{
		return Counter::Total;
	}
};

int current = Counter::Peek();
```

靜態方法沒有隱含的實例對象，因此不能直接存取非靜態欄位或呼叫依賴實例的成員方法。需要存取實例成員時，應把物件當作參數傳入：

```VeroScript
class Formatter
{
public:
	string prefix;

	static string with_prefix(Formatter formatter, string value)
	{
		return formatter.prefix + value;
	}
};
```

## 泛型類型中的靜態成員

泛型類型的每個具體類型實例擁有獨立的靜態儲存。例如，`Box<int>::Count` 與 `Box<string>::Count` 互不共享：

```VeroScript
class Box<T>
{
public:
	static int Count = 0;
	T value;
};

Box<int>::Count = Box<int>::Count + 1;
Box<string>::Count = Box<string>::Count + 1;
```

使用靜態成員時，建議始終透過完整的類型名稱來限定成員，以明確表達它屬於類型而不是物件實例。

## 成員預設值與初始化

實例欄位預設值在該實例的正常建構過程中求值，可以呼叫所屬型別中稍後宣告的靜態方法，泛型型別亦適用。它讀取當時的靜態值，而非編譯時凍結的快照；再建立實例不會重新初始化型別的靜態欄位。存取控制、const 與建構初始化規則仍然適用。

```VeroScript
class Sample<T>
{
public:
	static int Seed = 7;
	T snapshot = Sample<T>::Read();

	static T Read()
	{
		return cast<T>(Sample<T>::Seed);
	}
};

int main()
{
	Sample<int> first = Sample<int>();
	Sample<int>::Seed = 11;
	Sample<int> second = Sample<int>();
	print(first.snapshot);
	print(second.snapshot);
	print(first.snapshot);
	return 0;
}
```

輸出依序為 `7`、`11`、`7`，各佔一行。

泛型或巢狀型別內部的自身詞法名稱指向目前特化，保留外層型別及值參數；`Outer<int>::Inner` 這類明確的外層實參仍依明確指定解析。同一具體型別的別名共用靜態儲存。運算式中的同名區域值仍是值，前導 `::` 仍選擇根範圍；參見[範圍](scope.md)。

型別靜態欄位在各執行狀態的模組啟動過程中初始化，與函式區域 static 的首次到達初始化不同。可確定的泛型靜態相依會在使用它的初始化運算式前準備；只儲存 Lambda 不會呼叫其主體，可為空的欄位或空容器也不會建構其中的元素。不要將此理解為任意延遲初始化或無關型別之間的全域總順序；保持相依簡單，需要嚴格副作用順序時在 `main` 中明確安排。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/static-members.md) · [fr-FR](../../fr-FR/language-basics/static-members.md) · [ja-JP](../../ja-JP/language-basics/static-members.md) · [ko-KR](../../ko-KR/language-basics/static-members.md) · [zh-CN](../../zh-CN/language-basics/static-members.md) · [zh-TW](static-members.md)
