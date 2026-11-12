# 資料類型

VeroScript 的型別用於決定值的表示方式、可執行的運算以及允許的轉換。類型可以分為內建類型、容器類型和使用者定義類型。

## 內建類型

| 類別 | 類型 |
| --- | --- |
| 無回傳值 | `void` |
| 布林 | `bool` |
| 有符號整數 | `char`、`short`、`int`、`long` |
| 無符號整數 | `unsigned char`、`unsigned short`、`unsigned int`、`unsigned long` |
| 固定寬度整數 | `int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32`、`int64`、`uint64` |
| 浮點數 | `float`、`double` |
| 文字 | `string`、`wstring` |

`void` 只能用作函式傳回類型，不能用於宣告變數。固定寬度整數和 `float`/`double` 是獨立的語言類型，不是僅用於檔案的別名；它們適合用於需要明確資料寬度或浮點精度的介面和資料結構。

固定寬度整數的取值範圍為：`int8` 為 `-128` 到 `127`，`uint8` 為 `0` 到 `255`；`int16` 為 `-32768` 到 `32767`，`uint16` 為 `0` 到 `65535`；`int32`、`uint32`、`int64` 和 `uint64` 分別使用對應的 32 位元和 64 位元有符號/無符號範圍。字面量超出目標類型範圍時應明確處理，無法依賴截斷。

```VeroScript
int8 signed_byte = -128;
uint8 byte = 255u;
float single = 2.05f;
double precise = 2.05;
```

帶有 `f`/`F` 的浮點字面量儲存為 `float`；沒有該後綴的浮點字面量儲存為 `double`。因此 `float` 的捨入行為可能與 `double` 不同。

## 容器類型

容器類型透過類型參數描述元素類型或鍵值類型：

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
set<string> names = set<string>();
queue<int> pending = queue<int>();
```

VeroScript 提供以下容器類別：

- 序列容器：`array`、`deque`。
- 關聯容器：`map`、`multimap`、`ordered_map`、`ordered_multimap`。
- 集合容器：`set`、`multiset`、`ordered_multiset`。
- 轉接器：`queue`、`priority_queue`、`stack`。
- 並發容器：`channel`。

容器的特定操作、元素語意和並發規則應參閱容器與並發檔案。

## 原生陣列

固定長度陣列把長度寫在元素類型後面：

```VeroScript
int numbers[3] = {10, 20, 30};
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
```

`T[N]` 的長度屬於類型的一部分，`array<T>` 則是動態陣列容器。兩者可以使用索引訪問，但不是同一種類型。

## 使用者定義類型

使用者可以使用 `struct`、`class`、`interface` 和 `enum` 定義新類型：

```VeroScript
struct Point
{
	int x;
	int y;
};

enum class Color
{
	Red,
	Green,
	Blue
};
```

`struct` 具有值語意，`class` 具有共享身分。具體的欄位、方法和物件語意見[結構體](structs.md)與[類別](classes.md)；介面和列舉見[介面](interfaces.md)與[列舉](enums.md)，泛型會在獨立的語言文件中說明。

## `auto`

`auto` 根據初始化運算式推導變數類型：

```VeroScript
auto count = 3;
auto values = array<int>{1, 2, 3};
```

`auto` 宣告必須包含初始化運算式。類型推導只發生在宣告處，變數宣告完成後仍具有確定的靜態型別。

## 類型轉換

相容的數值類型可以依語言規則進行轉換；`string` 和 `wstring` 之間不能隱含。需要在不同文字類型之間轉換時，應使用對應的明確轉換函式。

類型轉換是否允許取決於來源類型、目標類型和運算式上下文。對於宿主綁定和泛型介面，應盡量使用明確的類型，避免依賴隱式轉換。

## 唯讀包裝器

`readonly<T>` 是按值儲存 `T` 的標準包裝器。包裝器本身可以複製或放入容器，並透過 `const ref T get() const` 以唯讀方式公開內部值。

```VeroScript
readonly<int> count = readonly<int>(41);
const ref int view = count.get();
print(view);
```

它沒有隱式預設建構函式。對 struct 會儲存值副本；對 class 會儲存類別控制代碼，因此保留共享身分。如需了解刻意使用 `const_cast<T>` 逃逸的情況，請參閱[類型轉換](../language-reference/type-conversions.md)。

內建型別名稱 `optional`、`readonly`、`weak`、`shared`、`type`、`field_info`、`method_info`、`module_info`、`function_info`、`global_info` 和 `enum_member_info` 均為保留名稱，腳本不能重新宣告。

## 編譯器提供的工具類型

VeroScript 也提供 `optional<T>`、`weak<T>`、`pair<T, U>` 和 `shared<T>` / `handle<T>`。它們的建構函式、狀態規則、擁有權行為以及映射項目語意，統一說明在[值工具類型](value-utility-types.md)中。

`pair<T, U>` 和 `handle<T>` 是軟泛型內建類型：如果存在同名且泛型參數數量相同的使用者宣告，使用者宣告會優先於編譯器提供的形式。這一點不同於上面列出的嚴格保留名稱。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/data-types.md) · [fr-FR](../../fr-FR/language-basics/data-types.md) · [ja-JP](../../ja-JP/language-basics/data-types.md) · [ko-KR](../../ko-KR/language-basics/data-types.md) · [zh-CN](../../zh-CN/language-basics/data-types.md) · [zh-TW](data-types.md)
