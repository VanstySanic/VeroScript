# 05：結構體、類別和介面

本篇透過一個「可讀取物件」的小例子來理解三種使用者類型：結構體適合值，類別適合共享身份，介面適合描述可替換的行為。

## 第一步：用結構體表示值

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	int distance_score() const
	{
		return x * x + y * y;
	}
};

int main()
{
	Point point = Point();
	point.x = 3;
	point.y = 4;
	print(point.distance_score());
	return 0;
}
```

結構體通常表示一組值。把結構體賦給另一個變數時，預設關注的是值複製；這適合座標、設定和小型資料記錄。

## 第二步：用類別表示共享對象

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}

	void add(int amount)
	{
		value = value + amount;
	}
};

int main()
{
	Counter first = Counter(10);
	Counter second = first;
	second.add(5);
	print(first.value);
	return 0;
}
```

這個程式輸出 `15`，因為 `first` 和 `second` 指向同一個類別物件。類別變數複製的是物件身份，而不是獨立物件。需要另一個獨立計數器時，應再次呼叫建構函式。

類別成員預設是私有的，所以範例使用 `public:` 暴露建構子和成員。對外只需要提供操作時，可以把欄位留在私有區域，避免外部隨意修改狀態。

### 轉換建構函式與 `explicit`

只接收一個值參數且沒有 `explicit` 的建構函式可以參與隱式轉換。如果只允許直接建構或透過 `cast<T>` 建構，應將它標記為 `explicit`：

```VeroScript
struct UserId
{
	int value;
	explicit UserId(int input)
	{
		value = input;
	}
};

UserId direct(7);
UserId converted = cast<UserId>(8);
```

`UserId invalid = 9;` 會被拒絕。這能讓多載解析維持可預測，並避免意外建構包裝類型。

`(T)運算式` 是 `cast<T>(運算式)` 的語法糖，並嚴格遵循相同的轉換規則：

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);
```

當簡單使用者型別名稱可能與同名值運算式混淆時，建議使用 `cast<T>`。`(void)運算式;` 是只能出現在陳述式位置的明確結果捨棄語法。它會執行運算式一次，保留副作用和執行階段錯誤，只捨棄運算式結果；一般的 `運算式;` 具有相同的執行階段行為。`for` 初始化陳述式也接受這種形式，但 `(void)運算式` 不能用作巢狀值或函式引數。

```VeroScript
int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print(calls);
	return 0;
}
```

### `mutable` 快取欄位

只有當 `const` 方法確實需要更新快取等實作狀態時，才應在非靜態的 `struct` 或 `class` 實例欄位上使用上下文相關的 `mutable`。它不會讓整個物件都可寫，也不會讓程式碼自動具備執行緒安全性，並且不能與 `const`、`static` 或參考欄位組合使用。

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

### 可攜式唯讀承載值

`readonly<T>` 將 `T` 儲存為一般值，因此包裝物件本身仍可複製或放入容器，但 `get()` 只公開 `const T&`。建構 `readonly<Struct>` 會擷取該結構體的值；建構 `readonly<Class>` 則保留類別控制代碼。

只有當來源運算式是 const，且其規範執行階段類型與 `T` 相同時，才應使用 `const_cast<T>(value)`。它不會執行轉換或執行階段類型檢查，只會移除編譯期的 const 存取能力。一般 `cast<T>` 會刻意保留 const 存取能力。

保留上方的 `Label` 宣告，在本步驟使用以下 `main`。它先建立 `label` 再建構唯讀包裝，第一項輸出為 `source`。

```VeroScript
int main()
{
	Label label = Label();
	label.source = "source";
	readonly<Label> published = readonly<Label>(label);
	print(published.get().text());

	// Deliberate escape: mutate the payload stored by the wrapper.
	const_cast<Label>(published.get()).cached = "recomputed";
	return 0;
}
```

### 在 `cast` 與 `try_cast` 之間選擇

當轉換必須成功、檢查型參考轉換失敗應該報錯時，使用 `cast<T>`。當關係可能不成立、呼叫端應檢查 `optional<T>` 時，使用 `try_cast<T>`：

```VeroScript
class Base
{
};

class Derived : public Base
{
};

class Other
{
};

int main()
{
	Base value = Derived();
	optional<Derived> found = try_cast<Derived>(value);
	optional<Other> missing = try_cast<Other>(value);

	if (found.has_value())
	{
		print("derived");
	}
	if (!missing.has_value())
	{
		print("not compatible");
	}
	return 0;
}
```

`try_cast<T>` 處理基本型別、作用域列舉和參考/介面轉換，但不會呼叫 `explicit` 建構函式。完整規則請參閱[型別轉換](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-reference/type-conversions.md)。


## 第三步：用介面抽象行為

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Sensor : public IReadable
{
public:
	int value = 42;

	int read() const override
	{
		return value;
	}
};

int read_twice(IReadable value)
{
	return value.read() + value.read();
}

int main()
{
	Sensor sensor = Sensor();
	print(read_twice(sensor));
	return 0;
}
```

介面不負責建立對象，它只規定實作必須提供什麼方法。 `read_twice` 不需要知道傳入的是哪一個具體類型，只依賴 `IReadable` 契約。

## 第四步：定義靜態工具類型

當一個類型只是操作或常數的命名容器時，可以使用 `static class` 或 `static struct`。其中宣告的所有成員都會按靜態成員處理，因此呼叫時不需要建立物件，只要使用類型名稱和 `::`：

```VeroScript
public static class Math
{
	public int Base = 40;
	public int Add(int value)
	{
		return Math::Base + value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Add(2));
	print(Limits::MaxRetries);
}
```

靜態工具類型不能被具現化、繼承，也不能宣告為泛型類型。它們不能包含建構函式、解構函式、`using` 成員、預設初始化區塊、`mutable` 欄位或 `virtual`、`const` 等實例修飾詞。如果類型需要物件層級狀態或繼承，請使用一般的 `class` 或 `struct`。

## 如何選擇

- 資料需要獨立複製：優先考慮 `struct`。
- 多處程式碼需要共用同一個可變物件：考慮 `class`。
- 呼叫者只需要知道「能做什麼」：用 `interface` 參數隔離具體實作。
- 不需要物件身分或實例狀態時，使用靜態工具類型。

## 練習

1. 為 `Point` 增加一個 `move` 方法。
2. 為 `Counter` 增加 `reset`，並讓 `value` 不再公開。
3. 建立第二個實作 `FixedSensor`，讓它也實作 `IReadable`。

完整語意見[結構體](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/structs.md)、[類別](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/classes.md)和[介面](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/interfaces.md)。綜合範例請見 [`examples/object_basics.vs`](../../../../../VeroScript/Resources/Scripts/examples/object_basics.vs)。

下一篇學習引用和閉包共享狀態：[引用、引用回傳和 Lambda 擷取](06-references-and-captures.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/language/05-structs-classes-and-interfaces.md) · [fr-FR](../../fr-FR/language/05-structs-classes-and-interfaces.md) · [ja-JP](../../ja-JP/language/05-structs-classes-and-interfaces.md) · [ko-KR](../../ko-KR/language/05-structs-classes-and-interfaces.md) · [zh-CN](../../zh-CN/language/05-structs-classes-and-interfaces.md) · [zh-TW](05-structs-classes-and-interfaces.md)
