# 運算子

運算子用於計算、比較、組合或修改運算式的值。 VeroScript 支援算術、比較、邏輯、位元運算、賦值、索引、成員存取和條件運算子。

## 運算子分類

| 類別 | 運算子 |
| --- | --- |
| 算術 | `+`、`-`、`*`、`/`、`%` |
| 比較 | `==`、`!=`、`<`、`>`、`<=`、`>=` |
| 邏輯 | `!`、`&&`、`||` |
| 位元運算 | `&`、`|`、`^`、`<<`、`>>` |
| 賦值 | `=`、`+=`、`-=`、`*=`、`/=`、`%=`、`&=`、`|=`、`^=`、`<<=`、`>>=` |
| 自增與自減 | `++`、`--` |
| 條件 | `condition ? when_true : when_false` |
| 訪問 | `.`、`::`、`[]`、`()` |

## 優先權和結合性

下表從最低優先權排列到最高優先權：

| 層級 | 運算子 | 結合性 |
| --- | --- | --- |
| 賦值 | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` | 右結合 |
| 條件 | `?:` | 右結合 |
| 邏輯或 | `||` | 左結合 |
| 邏輯與 | `&&` | 左結合 |
| 位元或 | `|` | 左結合 |
| 位元互斥或 | `^` | 左結合 |
| 位元與 | `&` | 左結合 |
| 比較 | `==`, `!=`, `<`, `>`, `<=`, `>=` | 左結合 |
| 位移 | `<<`, `>>` | 左結合 |
| 加減 | `+`, `-` | 左結合 |
| 乘除餘 | `*`, `/`, `%` | 左結合 |
| 前綴 | `!`, `~`, 一元 `+`/`-`, 前綴 `++`/`--`, `new`, `delete`, 轉換和 `ref` | 右結合 |
| 後綴/存取 | 呼叫 `()`, 索引 `[]`, `.`, `::`, 後綴 `++`/`--` | 左結合 |

`&&` 和 `||` 先計算左運算元並短路；`?:` 先計算條件，隨後只計算一個分支。除此之外，當副作用順序會影響結果時，不應依賴運算元或函式引數之間的相對求值順序，應拆成獨立語句。運算子多載不會改變語法優先權或結合性。VeroScript 沒有通用逗號運算子；逗號分隔的運算式只允許出現在經典 `for` 的初始化和迭代子句中。

對於包含多個不同運算子的運算式，可以使用括號明確計算順序：

```VeroScript
int result = (a + b) * c;
bool valid = (value >= minimum) && (value <= maximum);
```

## 算術運算

`+`、`-`、`*`、`/` 和 `%` 用於數值運算。 `+` 也可以用於相同文字類型的字串拼接：

```VeroScript
int total = 10 + 2;
int remainder = total % 3;
string message = "value: " + to_string(total);
```

`string` 和 `wstring` 不能直接混合拼接。

## 比較與邏輯運算

比較運算子產生布林結果。邏輯運算子用於組合佈林運算式：

```VeroScript
bool in_range = value >= 0 && value <= 100;
bool invalid = value < 0 || value > 100;
bool enabled = !disabled;
```

上例中的 `empty` 是否可用取決於物件類型和已匯入的庫；對字串通常使用標準庫提供的字串函式完成檢查。

## 賦值運算

`=` 進行普通賦值，複合賦值運算子把運算與賦值合併：

```VeroScript
value = 10;
value += 2;
value *= 3;
```

賦值兩側必須滿足語言規定的型別轉換規則。賦值不會改變左側變數的靜態型別。

## 存取運算

索引運算子 `[]` 用於存取陣列或容器元素，成員存取運算子 `.` 用於存取物件成員：

```VeroScript
int first = values[0];
point.x = 10;
```

命名空間和類型成員也可以使用 `::`：

```VeroScript
Color color = Color::Red;
```

值成員存取使用 `.`；命名空間、型別、列舉成員和靜態成員優先使用 `::`。開頭的 `::` 會從全域/根作用域開始查找，並忽略目前命名空間、本地別名和 `using namespace` 的回退結果。

## 運算子重載

使用者定義類型可以宣告運算子方法，使類型參與特定運算。運算子重載必須滿足編譯器規定的參數和傳回類型規則；不應僅為了縮短程式碼而重載意義不直觀的運算子。

成員運算子以目前物件為隱含的左操作數；非成員運算子函式可以明確宣告多個運算元：

```VeroScript
class Vec
{
public:
	int x;

	Vec(int value)
	{
		x = value;
	}

	Vec operator+(Vec other)
	{
		return Vec(x + other.x);
	}

	void operator+=(Vec other)
	{
		x = x + other.x;
	}

	bool operator==(Vec other)
	{
		return x == other.x;
	}

	int operator[](int index)
	{
		return x + index;
	}
};

Vec operator+(int value, Vec vector)
{
	return Vec(value + vector.x);
}

Vec sum = Vec(3) + Vec(4);
Vec shifted = 1 + Vec(2);
int item = sum[0];
```

運算子重載可以覆寫二元運算、一元運算、複合賦值、索引和函式呼叫等形式。類型也可以宣告轉換運算子，例如 `bool operator bool()`，使物件能夠明確轉換為布林值：

```VeroScript
class Token
{
public:
	int value = 42;
	bool operator bool()
	{
		return value != 0;
	}
};

int main()
{
	Token token = Token();
	bool available = token;
	print(available);
	return 0;
}
```


內建布林運算子 `&&` 和 `||` 始終保持短路求值，不會被使用者自訂重載取代。只有當至少一個運算元屬於可重載的使用者型別或宿主型別時，才會考慮使用者自訂運算子候選；因此，需要先轉換兩個基礎型別運算元的運算子不會被選取。

某些比較運算可以根據已宣告的基礎運算子獲得鏡像行為，例如使用 `operator<` 支援相反方向的比較；程式碼仍應優先宣告能夠直接表達型別語意的運算子。運算子重載應保持型別的自然意義，避免讓相同符號承擔與常見語意無關的行為。

## 運算子重載中的參考

運算子實參依 C++ 風格隱式綁定參考參數：`a + b`、`a[i]` 與 `f(x)` 不必加上 `&` 或 `ref`。一般函式與方法呼叫仍須使用明確的參考實參標記。`T&` 只能綁定型別別名正規化後型別相同且可寫入的儲存位置，不能綁定常數、暫存值或需要數值轉換的實參；`const T&` 可以綁定唯讀儲存與轉換後的暫存值。

其他轉換相同時，可寫入左值優先匹配 `T&` 而非 `const T&`。按值與參考重載的轉換無法區分時，編譯器會回報歧義。

傳回 `T&` 的運算子運算式可以作為賦值目標或區域參考別名的來源；`const T&` 結果只能讀取。接收物件的 const 限制與成員存取權限同樣適用。參考傳回仍受生命週期檢查約束；`return this` 參考目前物件的控制代碼。

前置遞增/遞減的成員重載不帶明確參數，後置版本帶一個按值 `int` 標記參數。自由函式的前置版本接收運算元，後置版本再加上標記。明確重載決定傳回值：`void` 複合賦值不能當作物件值使用，非 void 結果也不會自動寫回目標。

`operator()` 支援預設實參與本語言的具型別可變參數，其他運算子不允許兩者。成員 `operator=` 接收一個參數，不支援自由函式 `operator=`。變數宣告中的初始化保留原有語義。

只有 `operator()` 和 `operator[]` 可以宣告為 `static`。繼承的運算子使用其宣告型別的泛型引數。靜態欄位和命名空間全域變數可以繫結參考參數；暫存結構體的欄位不能繫結可寫參考。

自由函式 `operator[]`/`operator()`、比較鏡像及合成複合賦值仍是 VeroScript 擴充。類別控制代碼與結構值保留原有語義；這不代表完整支援 C++ 指標、右值參考或移動語義。

本節新增能力適用於 VM、Mixed 與 JIT 執行後端。原始碼 AOT 匯出器有獨立的能力範圍：這些參考運算子組合、賦值重載、後置標記與呼叫運算子的預設/可變參數尚未適配，會明確拒絕匯出。

```VeroScript
class Cell
{
public:
	int value = 0;
	int& operator[](int index)
	{
		return value;
	}
	const int& operator[](int index) const { return value; }
	void operator()(int& target, const int& delta = 1) const { target += delta; }
	Cell& operator=(const int& next)
	{
		value = next;
		return this;
	}
	Cell& operator++()
	{
		value++;
		return this;
	}
	Cell operator++(int tag)
	{
		Cell old = Cell();
		old.value = value;
		value++;
		return old;
	}
};

void main()
{
	Cell cell = Cell();
	cell[0] = 3;
	cell[0] += 2;
	int number = 4;
	cell(number);             // number == 5
	cell(number, 2);          // number == 7
	print((cell++).value);    // 5
	print((++cell).value);    // 7
	(cell = 9)[0] = 10;
	const Cell view = cell;
	print(view[0]);           // 10
}
```

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/operators.md) · [fr-FR](../../fr-FR/language-basics/operators.md) · [ja-JP](../../ja-JP/language-basics/operators.md) · [ko-KR](../../ko-KR/language-basics/operators.md) · [zh-CN](../../zh-CN/language-basics/operators.md) · [zh-TW](operators.md)
