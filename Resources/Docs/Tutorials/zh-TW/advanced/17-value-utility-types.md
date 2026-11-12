# 17：可選值、弱引用和共享結構體控制代碼

本教學使用 VeroScript 編譯器提供的工具類型，建立一個小型狀態共享範例。你將學習何時使用 `optional<T>`、`weak<T>`、`handle<T>` 和 `pair<T, U>`，以及它們在值語意和擁有權規則上的差異。

`shared<T>` 是 `handle<T>` 的建議寫法；兩者是完全相同的型別，在容器、泛型函式及 `weak<T>` 中也可混用。`std::shared<T>` 與 `std::handle<T>` 使用相同型別身分。明確呼叫 `shared<T>()` 會建立非空包裝物件；指定 `nullptr` 會清空控制代碼。結構體承載值在建構時複製到包裝物件，之後複製共享控制代碼會保留相同物件身分。`weak<shared<T>>` 觀察此包裝物件；`lock()` 回傳相同物件的強控制代碼，失效後回傳 `nullptr`，存取 `.value` 前應檢查鎖定結果。`shared` 是保留的內建型別別名，不是語法關鍵字。依既有軟泛型規則，若使用者宣告同名且泛型參數數量相符的 `handle<T>`，兩種寫法皆指向該使用者型別。

## 目標

完成本教學後，你應該能夠：

- 不使用哨兵值，表示可能不存在的結果；
- 在不延長物件生命週期的情況下觀察類別；
- 透過類別控制代碼共享結構體承載值；
- 區分映射項目的值快照和可寫入檢視。

完整參考請參閱[值工具類型](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/value-utility-types.md)。每個範例執行前都應先使用 `--check` 檢查。

## 第一步：表示可選結果

空結果使用 `optional<T>()`，包含值的結果使用 `optional<T>(value)`。呼叫 `get()` 前先檢查 `has_value()`：

```VeroScript
optional<int> find_score(bool available)
{
	if (available)
	{
		return optional<int>(95);
	}
	return optional<int>();
}

int main()
{
	optional<int> missing = find_score(false);
	print(missing.value_or(0));

	optional<int> found = find_score(true);
	if (found.has_value())
	{
		print(found.get());
	}

	return 0;
}
```

只需要預設值時，`value_or` 很方便；明確檢查狀態後，可以使用 `get()`。對空 optional 呼叫 `get()` 會透過 `assert` 失敗。`try_cast<T>` 也使用相同的結果模式表示可能失敗的轉換。

## 第二步：不擁有物件地觀察類別

類別變數是強控制代碼。`weak<T>` 值只觀察物件，不會延長物件的生命週期：

```VeroScript
class Session
{
public:
	int id = 0;
};

int main()
{
	Session owner = Session();
	owner.id = 42;

	weak<Session> watcher = weak<Session>(owner);
	if (!watcher.expired())
	{
		Session locked = watcher.lock();
		print(locked.id);
	}

	owner = nullptr;
	print(watcher.expired());
	return 0;
}
```

物件仍存在時，`lock()` 返回強控制代碼；否則返回空控制代碼。存取成員前，必須檢查 `expired()`，或檢查 `lock()` 的結果。複製 weak 值會建立另一個觀察，重設一個副本不會重設另一個副本。

## 第三步：共享結構體承載值

結構體賦值通常會複製值。如果程式的多個部分必須修改同一個結構體承載值，可以將它放進 `handle<T>`：

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void move_right(handle<Point> point)
{
	point.value.x += 1;
}

int main()
{
	shared<Point> first = shared<Point>(Point());
	handle<Point> second = first;

	first.value.x = 10;
	move_right(second);
	print(first.value.x);
	print(second.value.x);

	return 0;
}
```

將一個 `handle<T>` 賦值給另一個控制代碼時，會複製類別控制代碼，因此兩個變數看到的是相同的 `value`。而 `handle<T>(source)` 會把承載值複製到新的共享物件。賦值 `nullptr` 會清空控制代碼。完整範例見 [examples/handle_struct.vs](../../../../../VeroScript/Resources/Scripts/examples/handle_struct.vs)。

## 第四步：選擇 `pair` 檢視

映射 API 使用 `pair<K, V>`。`entries()` 的結果是值快照陣列，因此修改快照不會修改映射。使用 `pair<K, V>&` 直接遍歷映射時，可以取得可寫入的項目檢視：

```VeroScript
int main()
{
	ordered_map<string, int> scores = ordered_map<string, int>();
	scores["alice"] = 95;
	scores["bob"] = 88;

	for (pair<string, int> snapshot : scores.entries())
	{
		print("{} -> {}", snapshot.first, snapshot.second);
	}

	for (pair<string, int>& entry : scores)
	{
		entry.second += 1;
	}

	for (auto& [key, value] : scores)
	{
		print("{} -> {}", key, value);
	}

	return 0;
}
```

需要獨立項目值集合時使用 `m.entries()`；需要將修改寫回項目時，直接遍歷映射。完整範例見 [examples/pair_map.vs](../../../../../VeroScript/Resources/Scripts/examples/pair_map.vs)。

## 練習

1. 修改 `find_score`，讓它返回 `optional<string>` 訊息，而不是整數。
2. 新增第二個 `weak<Session>` 觀察者，並驗證重設一個觀察者不會重設另一個。
3. 為 `handle<Point>` 範例新增沿 `y` 軸移動的函式，並透過第二個控制代碼呼叫。
4. 新增 `map<string, Point>`，透過 `pair<string, Point>&` 更新巢狀欄位。

## 需要記住的內容

- `optional<T>` 表示空值或有值的狀態，是值類型。
- `weak<T>` 觀察類別，但不會延長類別的生命週期。
- `handle<T>` 共享一個包含公用 `value` 欄位的類別物件，承載值保存在該欄位中。
- `entries()` 返回的 `pair<K, V>` 是值快照；直接遍歷映射取得的 pair 引用會寫回映射。

下一篇：[使用 std 標準命名空間](14-standard-namespace.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/advanced/17-value-utility-types.md) · [fr-FR](../../fr-FR/advanced/17-value-utility-types.md) · [ja-JP](../../ja-JP/advanced/17-value-utility-types.md) · [ko-KR](../../ko-KR/advanced/17-value-utility-types.md) · [zh-CN](../../zh-CN/advanced/17-value-utility-types.md) · [zh-TW](17-value-utility-types.md)
