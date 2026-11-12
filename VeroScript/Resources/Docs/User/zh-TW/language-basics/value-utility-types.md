# 值工具類型

VeroScript 提供一組由編譯器提供的泛型類型，用於表示可能不存在的值、對類別物件的非擁有觀察、共享結構體承載值以及映射項目。這些類型不需要匯入即可使用。[std 標準命名空間](standard-namespace.md)中說明的 `std::` 寫法，是指向相同類型的相容別名。

## `optional<T>`

`optional<T>` 是一種值類型，可以不包含值，也可以包含一個 `T` 類型的值。它的狀態是明確的；預設建構的 optional 為空。

### 建構和方法

| 形式 | 意義 |
| --- | --- |
| `optional<T>()` | 建構空的 optional。 |
| `optional<T>(value)` | 建構包含 `value` 的 engaged optional。 |
| `has_value() const` | 返回是否存在值。 |
| `get() const` | 返回儲存的值。為空時呼叫會透過 `assert` 失敗。 |
| `value_or(fallback) const` | 有值時返回儲存值，否則返回 `fallback`。 |
| `reset()` | 將 optional 設為空。 |

```VeroScript
optional<int> score = optional<int>();
print(score.has_value());
print(score.value_or(0));

score = optional<int>(95);
if (score.has_value())
{
	print(score.get());
}

score.reset();
```

optional 保留 `T` 的值語意。對於結構體承載值，複製有值的 optional 會複製結構體；對於類別承載值，複製的是類別控制代碼，因此會保留共享身分。`reset()` 也會釋放其中儲存的類別控制代碼（如果有）。

對於可能失敗的轉換，`try_cast<T>` 會返回 `optional<T>`。呼叫 `get()` 前應先檢查 `has_value()`；完整規則請參閱[型別轉換](../language-reference/type-conversions.md)。

## `weak<T>`

`weak<T>` 是對類別物件的非擁有觀察。`T` 必須是腳本類別、介面或支援的宿主類別型別；基本型別和結構體不能作為目標。weak 值不會延長目標的生命週期。

### 弱參考方法

| 形式 | 意義 |
| --- | --- |
| `weak<T>()` | 建構空的弱引用。 |
| `weak<T>(target)` | 觀察 `target` 中的類別控制代碼。 |
| `lock() const` | 目標仍存在時返回強 `T` 控制代碼，否則返回空控制代碼。 |
| `expired() const` | 返回目標是否已無法鎖定。 |
| `reset()` | 清除此弱引用。 |

```VeroScript
class Session
{
public:
	int id = 0;
};

Session owner = Session();
weak<Session> observer = weak<Session>(owner);

Session locked = observer.lock();
if (locked != nullptr)
{
	print(locked.id);
}
```

複製 weak 值會建立另一個觀察相同目標的弱引用。重設其中一個副本不會重設其他副本。當所有強類別控制代碼都消失後，`expired()` 會變成 `true`，`lock()` 會返回空控制代碼。存取成員前必須檢查鎖定結果。

## `shared<T>` / `handle<T>`

`handle<T>` 是編譯器提供的單一型別參數泛型類別，包含名為 `value` 的公用欄位。當結構體值需要放在共享的類別身分後方時，可以使用它。預設建構函式會建立控制代碼，並在 `T` 為值型別時預設建構 `value`；如果 `T` 是類別，承載值則遵循類別控制代碼語意，預設值為空控制代碼。`handle<T>(value)` 會將承載值複製到新的共享物件。

`shared<T>` 是 `handle<T>` 的建議寫法；兩者是完全相同的型別，在容器、泛型函式及 `weak<T>` 中也可混用。`std::shared<T>` 與 `std::handle<T>` 使用相同型別身分。明確呼叫 `shared<T>()` 會建立非空包裝物件；指定 `nullptr` 會清空控制代碼。結構體承載值在建構時複製到包裝物件，之後複製共享控制代碼會保留相同物件身分。`weak<shared<T>>` 觀察此包裝物件；`lock()` 回傳相同物件的強控制代碼，失效後回傳 `nullptr`，存取 `.value` 前應檢查鎖定結果。`shared` 是保留的內建型別別名，不是語法關鍵字。依既有軟泛型規則，若使用者宣告同名且泛型參數數量相符的 `handle<T>`，兩種寫法皆指向該使用者型別。

帶一個承載值參數時，`auto h = shared(value)` 可以推斷 `T`；無參數建構需要明確的目標型別或型別引數。

原始碼匯出有後端限制：C++ 匯出支援共享包裝和弱參考；C# 匯出支援共享包裝，但結構體載荷或開放泛型載荷的建構需要明確傳入值，例如 `shared<Point>(Point())`，且暫不支援 `weak<T>`。獨立 Python 原始碼匯出暫不支援共享包裝和弱參考；Python 執行時繫結透過核心支援兩者。完整的生命週期範例可在 VM、Mixed 和 JIT 後端執行。

### 建構和共享

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void bump_x(handle<Point> point)
{
	point.value.x += 1;
}

shared<Point> first = shared<Point>(Point());
handle<Point> second = first;
first.value.x = 10;
bump_x(second);
print(first.value.x);
```

將 `first` 賦值給 `second` 會複製類別控制代碼，因此兩個變數會觀察並修改相同的 `value`。賦值 `nullptr` 會清空控制代碼。當 `T` 為結構體時，建構 `handle<T>(source)` 會建立新的承載值副本；之後修改 `source` 不會影響這個副本。

`handle<T>` 是共享包裝器，不是一般類別的替代品。將 `h.value` 中的結構體轉換為介面時，會裝箱一個值副本。如果介面需要公開共享的可變身分，請使用實作該介面的類別。

完整的可執行範例見 [examples/handle_struct.vs](../../../../Scripts/examples/handle_struct.vs)。

## `pair<T, U>`

`pair<T, U>` 是編譯器提供的泛型結構體，包含兩個公用欄位：

| 欄位 | 型別 |
| --- | --- |
| `first` | `T` |
| `second` | `U` |

映射 API 使用 `pair` 表示鍵和值組成的項目。`m.entries()` 返回值型別的 `pair<K, V>` 快照陣列：

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (pair<string, int> entry : scores.entries())
{
	print("{} -> {}", entry.first, entry.second);
}
```

直接遍歷映射本身時，可以取得可寫入的項目檢視。`pair<K, V>&` 會將修改寫回映射，而 `pair<K, V>` 是副本：

```VeroScript
for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}

for (auto& [key, value] : scores)
{
	value += 1;
}
```

不要把 `entries()` 返回的 `pair` 當作原始映射項目的引用。完整的範圍遍歷規則請參閱[引用](references.md)和[容器](containers.md)。可執行的映射範例見 [examples/pair_map.vs](../../../../Scripts/examples/pair_map.vs)。

## 命名和相容性

`optional`、`readonly`、`weak` 以及反射資訊類型是嚴格保留的內建類型名稱，腳本不能重新宣告。

`pair<T, U>` 和 `handle<T>` 是軟泛型內建類型。只有在不存在同名且泛型參數數量相同的使用者宣告時，編譯器才會提供雙參數的 `pair` 和單參數的 `handle`。非泛型的 `pair` 或 `handle`，以及參數數量不同的宣告，可以與對應的軟泛型形式共存。

標準命名空間別名為 `std::optional`、`std::weak`、`std::shared`, `std::handle` 和 `std::pair`。這些別名不會建立不同的型別。

## 相關參考

- [資料型別](data-types.md)：內建型別、容器和唯讀包裝器。
- [泛型](generics.md)：使用者定義的泛型型別和軟內建型別。
- [型別轉換](../language-reference/type-conversions.md)：`cast`、`try_cast`、`const_cast` 以及轉換結果。
- [容器](containers.md)：映射操作和範圍遍歷。

指令碼物件開始解構時，其弱參考生命週期即結束：解構期間及之後，lock() 回傳空，expired() 回傳 true。在解構函式中讓 this 逸出也不會使物件復活，後續欄位存取與方法呼叫會被拒絕。宿主物件的弱參考繼續遵循宿主生命週期契約。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/value-utility-types.md) · [fr-FR](../../fr-FR/language-basics/value-utility-types.md) · [ja-JP](../../ja-JP/language-basics/value-utility-types.md) · [ko-KR](../../ko-KR/language-basics/value-utility-types.md) · [zh-CN](../../zh-CN/language-basics/value-utility-types.md) · [zh-TW](value-utility-types.md)
