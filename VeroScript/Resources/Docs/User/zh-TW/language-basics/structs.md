# 結構體

`struct` 用於定義具有值語意的使用者類型。結構體可以包含欄位、方法、建構子和析構函式，也可以繼承一個基底型別或實作介面。結構體物件本身以值複製；欄位的特定複製行為仍遵循欄位類型自身的語意。

## 宣告和成員

結構體宣告由名稱和成員清單組成。結構體成員預設為 `public`：

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	void translate(int dx, int dy)
	{
		this.x += dx;
		this.y += dy;
	}
};
```

在成員函式中，`this` 表示目前結構體物件。能夠透過名稱直接存取的成員，也可以使用 `this.member` 的形式存取。

結構體支援 `public`、`protected` 和 `private` 存取控制。雖然結構體預設為公開成員，但需要限制存取時應明確寫出存取說明符：

```VeroScript
struct Account
{
public:
	int id = 0;

private:
	int balance = 0;
};
```

## 建構與初始化

欄位可以提供預設初始化運算式。結構體也可以宣告建構函式，建構時依照參數清單選擇對應的重載：

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point()
	{
	}

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}
};

Point origin = Point();
Point point = Point(3, 4);
Point literal = Point{7, 8};
```

建構初始化列表可以在建構函式主體之前初始化目前型別直接宣告的執行個體欄位。列表中的欄位項目會依欄位宣告順序初始化。已有宣告初始化器的欄位不能再次出現在列表中；`const` 欄位的完整規則請參閱[常數](constants.md)。

花括號形式使用型別明確的初始化語法，並遵循相同的建構子匹配規則。沒有顯式建構函式時，欄位預設值仍然參與預設建構。

## 值語意

結構體賦值會複製結構體值。修改複製結果的直接欄位，不會改變原結構體：

```VeroScript
struct Counter
{
	int value = 0;
};

Counter first = Counter();
Counter second = first;
second.value = 10;

print(first.value);  // 0
print(second.value); // 10
```

值語意只描述結構體物件本身。若欄位是 `array<T>`、`map<K, V>` 或其他共用控制代碼類型，複製結構體時該欄位仍遵循共用控制代碼語意；需要獨立容器時應明確複製容器內容。

## 方法和引用

結構體方法可以讀取或修改目前物件。需要讓函式直接修改呼叫方的結構體時，應使用參考參數：

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}

	void translate(int dx, int dy)
	{
		x += dx;
		y += dy;
	}
};

void reset(ref Point point)
{
	point.x = 0;
	point.y = 0;
}

int main()
{
	Point point = Point{3, 4};
	point.translate(1, 2);
	reset(ref point);
	print("{}, {}", point.x, point.y);
	return 0;
}
```

普通參數和按值範圍遍歷會產生結構體值副本；引用參數和引用範圍遍歷才會寫回原始物件。詳細規則請見[引用](references.md)。

## `mutable` 實作欄位

非靜態實例欄位在表示可由 `const` 查詢更新的實作狀態時，可以標記為 `mutable`，最常見的用途是快取：

```VeroScript
struct Label
{
	string source;
	mutable string cached;

	string text() const
	{
		if (cached == "")
		{
			cached = source;
		}
		return cached;
	}
};
```

此例外只適用於標記的欄位。它不會讓 const 物件整體可寫，也不會保證並行存取安全。`mutable` 欄位不能同時是 `const`、`static` 或參考欄位。

## 繼承和介面

結構體可以繼承一個結構體基底類，也可以實作一個或多個介面：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Counter : IReadable
{
	int value = 0;

	int read() const
	{
		return value;
	}
};
```

將結構體轉換為介面類型時，會產生介面所需的值副本；透過介面修改該副本不會回寫原結構體。介面繼承見[介面](interfaces.md)專題，泛型限制見[泛型](generics.md)專題。

## 預設特殊成員

```VeroScript
struct Record
{
	int value = 0;
	Record() = default;
	Record(const Record& other) = default;
	Record& operator=(const Record& other) = default;
	bool operator==(const Record& other) const = default;
	bool operator!=(const Record& other) const = default;
};
```

結構體可以明確要求逐成員複製建構、賦值及相等比較。複製建構不會重新執行欄位初始化器；賦值更新既有子物件，並傳回原物件的參考。const 或參考欄位可能使預設賦值無法使用；複製與比較仍分別判斷。

類別可以要求預設相等比較，一般複製仍共用控制代碼。刪除或無法存取的操作不會退回原始複製。明確複製建構可用於直接建構，不能用於隱含初始化；新建構結果不需要額外複製。不新增指令碼移動操作。

上述宣告採用核心執行語意，包括混合／即時編譯策略下的虛擬機回退與核心預先編譯執行。獨立的 C++、C#、Python 原始碼匯出會拒絕尚未支援的預設成員。範例：`default_special_members.vs`。

巢狀型別、區域宣告、存取規則與分段泛型實參請參閱[型別範圍](scope.md)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/structs.md) · [fr-FR](../../fr-FR/language-basics/structs.md) · [ja-JP](../../ja-JP/language-basics/structs.md) · [ko-KR](../../ko-KR/language-basics/structs.md) · [zh-CN](../../zh-CN/language-basics/structs.md) · [zh-TW](structs.md)
