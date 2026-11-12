# 類型轉換

類型轉換決定一個運算式能否作為另一個類型的值。轉換是否成立取決於來源類型、目標類型以及運算式所在的上下文。

## 數值類型

相容的整數和浮點類型可以依照語言的數值轉換規則參與賦值、參數傳遞和運算。固定寬度整數用於需要明確資料寬度的介面；轉換到較窄或無符號類型時，應確認取值範圍不會被截斷或改變。

```VeroScript
int count = 10;
double total = count;
int rounded = to_int(total);
```

需要明確表達意圖時，使用目標類型建構或語言支援的明確轉換形式；不要透過字串拼接或隱式轉換來替代數值轉換。

## 轉換情境

VeroScript 區分自動允許的隱式轉換，以及必須明確要求的轉換：

| 情境 | 用途 | 主要規則 |
| --- | --- | --- |
| 指派、傳參和回傳值 | 隱式轉換 | 會考慮相容的數值轉換及相容的基底類別/介面轉換；沒有 `explicit` 的單值建構函式也可以參與。 |
| `cast<T>(value)` | 明確轉換或建構 | 支援數值、作用域列舉、參考、介面和明確建構相關的轉換，也可明確要求相符的使用者自訂轉換運算子；檢查型參考轉換失敗會產生執行階段錯誤。 |
| `try_cast<T>(value)` | 可失敗轉換 | 回傳 `optional<T>`；檢查型參考轉換失敗時回傳空 optional，且不會呼叫 `explicit` 建構函式。 |
| `const_cast<T>(value)` | 移除 const 存取能力 | 來源運算式必須是 const，且與 `T` 的規範型別完全一致；它不是值轉換，也不執行執行階段型別檢查。 |

編譯器不會串接使用者自訂轉換。即使存在 `A` 到 `B`、再到 `C` 的轉換，也不會把 `A` 隱式地連續轉換為 `C`。

## 文字類型

`string` 和 `wstring` 是不同的類型，不能直接混合賦值、比較或拼接。使用標準庫提供的明確轉換函式：

```VeroScript
string text = "hello";
wstring wide = to_wstring(text);
string restored = to_string(wide);
```

數值和布林值轉為文字時使用 `to_string`，不要依賴混合類型的隱式拼接。

## 轉換建構函式與轉換運算子

接收一個值參數且沒有標記為 `explicit` 的建構函式可以參與隱式轉換：

```VeroScript
struct Meter
{
	int value;

	Meter(int input)
	{
		value = input;
	}
};

Meter assigned = 7;
```

如果只允許直接建構或明確 `cast<T>`，應標記為 `explicit`：

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
// UserId invalid = 9; // Compile error.
```

`operator bool()` 這類使用者自訂轉換運算子，也可以在語言允許的轉換情境中參與轉換。應謹慎使用，確保隱式轉換在呼叫點仍然直觀。
`cast<T>` 也可以明確要求相符的使用者自訂轉換運算子。`try_cast<T>` 僅限於文件所述的基礎型別、作用域列舉和參考/介面情境，不會呼叫使用者自訂建構函式或轉換運算子。

## `cast<T>`

`cast<T>(value)` 要求明確轉換。它可用於相容的數值轉換、作用域列舉與底層整數之間的轉換、檢查型類別/介面參考轉換，以及目標 struct/class 的明確建構。

```VeroScript
double precise = cast<double>(7);
int narrowed = cast<int>(2.9);

enum class Mode
{
	Idle = 3
};

Mode mode = cast<Mode>(3);
int raw_mode = cast<int>(mode);
```

對於類別和介面參考，在需要向下轉型或交叉轉型時會檢查執行階段關係；失敗會產生執行階段錯誤。struct 轉為介面時會形成介面所需的值表示，class 轉為介面時保留類別物件的共享身分。`cast<T>` 不會移除 const 存取能力；const 來源得到的結果仍按 const 存取。只有刻意移除 const 時才使用 `const_cast<T>`。

轉換為結構體值成功後會遵循值語意。

## 括號式轉換語法

`(T)運算式` 是 `cast<T>(運算式)` 的另一種寫法。它遵循完全相同的明確轉換規則，包括數值、作用域列舉、參考/介面、明確建構以及相符的使用者自訂轉換運算子：

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);

int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print("a={}, calls={}", a, calls);
	return 0;
}
```

這種形式會在一元運算式位置被辨識。簡單使用者型別名稱可能與值運算式產生歧義，因此當目標型別不夠明確時，應使用 `cast<T>`。

`(void)運算式;` 是只能出現在陳述式位置的明確結果捨棄語法。它會準確執行運算式一次，保留副作用和執行階段錯誤，只捨棄運算式結果。`for` 初始化陳述式也接受這種形式。它不是值運算式，不能用作引數、指定結果、算術運算元或巢狀運算式。普通的 `運算式;` 具有相同的執行階段行為；`(void)` 前綴用來明確表達捨棄結果的意圖。

## `try_cast<T>`

`try_cast<T>(value)` 是可失敗版本，適用於基本型別、作用域列舉以及參考/介面轉換：

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

Base base = Derived();
optional<Derived> found = try_cast<Derived>(base);
optional<Other> missing = try_cast<Other>(base);

if (found.has_value())
{
	print("derived");
}

if (!missing.has_value())
{
	print("not compatible");
}
```

`try_cast` 在轉換成功時回傳 engaged optional。檢查型參考轉換失敗時回傳空 optional。null 參考轉換仍是到可為空目標的成功轉換，因此可能回傳包含 `nullptr` 的 engaged optional。數值轉換回傳轉換後的值，例如 `try_cast<int>(2.9)` 包含 `2`。

`try_cast<T>` 不會選擇 `explicit` 建構函式，需要明確建構時應使用 `cast<T>`。

## `const_cast<T>`

`const_cast<T>(value)` 是範圍很窄的 const 逃逸。來源運算式必須是 const，且解析別名後的規範型別必須與 `T` 完全一致：

```VeroScript
class Counter
{
public:
	int value = 41;
};

int main()
{
	readonly<Counter> stored = Counter();
	const_cast<Counter>(stored.get()).value = 42;
	print(stored.get().value);
	return 0;
}
```

本例修改共享類別物件的欄位。`const_cast<int>(view)` 得到的是純量值，不是可賦值的參考，因此 `const_cast<int>(view) = 42` 是無效寫法。轉換不會將純量副本變為原儲存空間的別名。

它只移除編譯期的 const 存取能力，不執行值轉換、執行階段型別變更或檢查型轉換。只應對明確允許修改的儲存（例如受控的 `readonly<T>` 包裝器）使用；應用於真正不可變的儲存會破壞 `const` 原本提供的保護。

以下形式會被拒絕：

```VeroScript
const int number = 1;
// const_cast<double>(number); // The target type must be exactly int.
// const_cast<int>(mutable_value); // The source must be const.
```

## 結構體、類別、介面和容器

結構體轉換遵循值語意，複製會產生結構體值的副本；結構體欄位如果本身是共享控制代碼，欄位仍保留自己的控制代碼語意。類別轉換保留物件的共享身分：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Point : IReadable
{
	int value;
	int read() const override
	{
		return value;
	}
};

Point point = Point();
IReadable readable = point;
```

將結構體轉換為介面會形成介面所需的值副本；將類別轉換為介面會保留類別物件的共用身分。介面轉換要求實際類型滿足介面的全部純虛方法。

容器的元素類型、鍵類型和值類型必須滿足容器宣告的類型規則。泛型函式和型別只有在實參或型別參數滿足宣告限制時才能實例化；`where` 限制不會自動放寬其他型別轉換規則。

## 不應依賴的轉換

`nullptr` 只適用於可以為空的控制代碼類型；文字類型不能透過普通賦值互換；結構體值和類別控制代碼也不能因為欄位形狀相似而自動互換。宿主綁定和介面邊界應優先使用明確的目標類型。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-reference/type-conversions.md) · [fr-FR](../../fr-FR/language-reference/type-conversions.md) · [ja-JP](../../ja-JP/language-reference/type-conversions.md) · [ko-KR](../../ko-KR/language-reference/type-conversions.md) · [zh-CN](../../zh-CN/language-reference/type-conversions.md) · [zh-TW](type-conversions.md)
