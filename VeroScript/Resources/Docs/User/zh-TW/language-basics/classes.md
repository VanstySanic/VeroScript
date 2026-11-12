# 類

`class` 用於定義具有共享身分的使用者類型。類別物件透過控制代碼訪問，可以為空，並支援欄位、方法、建構子、析構函式、繼承和虛方法。與結構體不同，複製類別變數通常只是複製物件控制代碼，不會複製物件本身。

## 宣告與存取控制

類別支援欄位和成員函式。類別成員預設為 `private`，需要對外提供介面時應明確使用 `public`：

```VeroScript
class Counter
{
	int value = 0;

public:
	void add(int amount)
	{
		value += amount;
	}

	int get() const
	{
		return value;
	}
};
```

成員訪問也支援 `protected` 和 `private`。存取說明符會影響目前類別、衍生類別以及類別外程式碼能夠存取的成員。 `friend` 可以為指定的函式或類別授予額外存取權限。

## 建構與空對象

類別可以宣告一個或多個建構函式。建構運算式會建立新的類別物件：

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}
};

Counter counter = Counter(10);
```

建構初始化列表可以在建構函式主體之前呼叫直接基底類別建構函式，並初始化目前型別直接宣告的執行個體欄位。列表中的欄位項目會依欄位宣告順序初始化。已有宣告初始化器的欄位不能再次出現在列表中；`const` 欄位的完整規則請參閱[常數](constants.md)。

建構或解構期間，對目前物件的虛擬呼叫使用正在執行的建構函式或解構函式所屬型別的實作，透過輔助函式或繫結方法發起的呼叫也遵循此規則。基底建構函式不會呼叫尚未初始化的衍生部分中的覆寫方法。該階段呼叫沒有實作的純虛擬方法會產生執行階段錯誤。建構完成後，一般動態分派使用實際衍生型別。

建構失敗時，不執行建構失敗型別的解構函式；已初始化成員和已完成建構的基底按照建構的逆序釋放。

類別控制代碼可以儲存 `nullptr`：

```VeroScript
Counter empty = nullptr;
if (empty == nullptr)
{
	print("empty");
}
```

對空控制代碼呼叫實例成員會產生執行階段錯誤。使用物件前，應先確保控制代碼指向有效物件。

## 共享身份

將一個類別變數賦給另一個變數時，兩個變數指向同一個物件：

```VeroScript
class Box
{
public:
	int value = 0;

	void set(int next)
	{
		value = next;
	}
};

Box first = Box();
Box second = first;
second.set(10);

print(first.value);  // 10
```

如果需要獨立物件，應再次呼叫建構函式，而不是直接複製控制代碼。類別欄位、容器中的類別值和函式參數中的類別值都遵循這種共享身分語意。

## 常數物件和查詢方法

成員函式可以使用 `const` 修飾，表示函式不能透過目前物件修改成員。常量物件只能呼叫適用於常數物件的成員函式：

```VeroScript
class Counter
{
public:
	int value = 0;

	int get() const
	{
		return value;
	}
};

const Counter counter = Counter();
print(counter.get());
```

如果一個函式只讀取物件狀態，應優先宣告為 `const` 成員函式。可變成員函式不能透過 `const` 物件呼叫。

### `mutable` 實作欄位

`mutable` 是 `struct` 或 `class` 非靜態實例欄位的內容相關說明符。它允許 `const` 成員函式只更新這個欄位：

```VeroScript
class Label
{
	string source;
	mutable string cached;

public:
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

`mutable` 不會讓整個物件可寫，也不提供執行緒安全，並且不能與 `const`、`static` 或參考欄位組合使用。它是內容相關說明符，而不是全域保留字；在欄位宣告之外，只要語法允許，仍可作為一般識別字使用。

## 繼承和虛方法

類別可以繼承一個或多個基類，並實作介面。使用 `virtual` 宣告可動態分派的方法，使用 `override` 明確覆寫基底類別方法：

```VeroScript
class Base
{
public:
	virtual int value() const
	{
		return 1;
	}
};

class Derived : public Base
{
public:
	int value() const override
	{
		return 2;
	}
};

int main()
{
	Base object = Derived();
	print(object.value());
}
```

抽象類別可以宣告純虛方法，例如 `virtual int value() const = 0;`。包含仍未實作的純虛槽位的類別不能被實例化。虛析構函式用於確保透過基底類別身分銷毀派生物件時執行正確的析構過程。

介面、繼承存取方式和多重繼承的完整規則請見[介面](interfaces.md)專題。

## 析構函式

類別可以宣告析構函式 `~TypeName()`，在物件生命週期結束時執行清理邏輯。析構函式也可以宣告為 `virtual` 或純虛析構函式；涉及繼承時，應根據物件的銷毀方式選擇合適的虛析構設計。


## 類別外定義

可以在類別內宣告成員，再於同一原始碼模組中使用 `Type::member` 定義。參數型別、回傳型別、參考限定和 `const` 必須與宣告相符；建構函式不寫回傳型別。這是原始碼中的定義形式，請勿把宣告與實作拆成獨立連結的指令碼模組。

```VeroScript
class Counter
{
public:
	Counter(int seed);
	int Add(int delta) const;
	int value;
};

Counter::Counter(int seed)
{
	this.value = seed;
}

int Counter::Add(int delta) const
{
	return this.value + delta;
}

void main()
{
	Counter counter = Counter(6);
	print(counter.Add(1));
}
```

巢狀型別、區域宣告、存取規則與分段泛型實參請參閱[型別範圍](scope.md)。

## 自我參照欄位

泛型類別可以在欄位中保存自身實例型別的控制代碼。欄位可以初始化為 `nullptr`；指定其他物件時會保留其物件身分。

```VeroScript
class Node<T>
{
public:
	T value;
	Node<T> next = nullptr;
};
```

結構的欄位直接或互相以值遞迴仍不合法。循環繼承和無限展開的範本也會被拒絕，編譯深度限制仍然有效。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/classes.md) · [fr-FR](../../fr-FR/language-basics/classes.md) · [ja-JP](../../ja-JP/language-basics/classes.md) · [ko-KR](../../ko-KR/language-basics/classes.md) · [zh-CN](../../zh-CN/language-basics/classes.md) · [zh-TW](classes.md)
