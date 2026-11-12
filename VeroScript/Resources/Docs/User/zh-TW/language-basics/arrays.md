# 陣列

VeroScript 有兩種陣列形式：動態陣列 `array<T>` 和固定長度原生陣列 `T[N]`。兩者都支援索引和範圍遍歷，但型別和執行階段語意不同。

## 動態陣列

`array<T>` 是由元素類型 `T` 參數化的動態容器：

```VeroScript
array<int> values = array<int>();
values.push(10);
values.push(20);

print(values[0]);
print(len(values));
```

也可以使用花括號初始化：

```VeroScript
array<int> values = array<int>{1, 2, 3};
```

動態陣列的常用操作包括：

| 操作 | 說明 |
| --- | --- |
| `values[index]` | 讀取或修改指定索引的元素 |
| `values.push(value)` | 在末尾追加一個元素，並回傳陣列本身 |
| `values.remove(value)` | 刪除第一個與值相等的元素，傳回是否刪除成功 |
| `values.remove_at(index)` | 刪除指定索引的元素 |
| `values.clear()` | 清空陣列，並傳回陣列本身 |
| `values.reserve(count)` | 預留容量，並傳回陣列本身 |
| `values.capacity()` | 傳回目前容量 |
| `len(values)` | 傳回元素數量 |

`array<T>` 的元素必須與 `T` 相符。在陣列中加入結構體或固定陣列時，執行階段會按值語意複製這些元素；類別和其他控制代碼型別仍保持其控制代碼語意。

動態陣列值透過共享容器控制代碼儲存。將一個 `array<T>` 賦給另一個變數時，兩個變數指向同一個動態陣列；如果需要獨立副本，應明確建構並複製元素。

## 固定長度原生陣列

原生陣列把長度寫在元素類型後面：

```VeroScript
int numbers[3] = {10, 20, 30};
```

陣列長度屬於型別的一部分。`int[3]`、`int[4]` 和 `array<int>` 是不同的型別。原生陣列可以作為結構體欄位，也可以宣告為多維陣列：

```VeroScript
struct GridRow
{
	int cells[3] = {1, 2, 3};
};

int matrix[2][3] = {
	{1, 2, 3},
	{4, 5, 6}
};
```

多維原生陣列由嵌套的原生陣列組成。 `len(matrix)` 傳回第一維的長度，`len(matrix[0])` 傳回第二維的長度。

原生陣列型別最多包含 64 個維度。每個維度最多包含 65,536 個元素，所有維度攤平後的元素數量乘積最多為 1,048,576。原始碼超過任一上限時會在編譯階段被拒絕。

## 索引

陣列索引從 `0` 開始：

```VeroScript
array<string> names = array<string>{"Ada", "Linus"};
string first = names[0];
names[1] = "Grace";
```

索引運算式的結果類型是元素類型。索引必須是整數，且必須在目前陣列的有效範圍內；越界存取會產生執行階段錯誤。

## 範圍遍歷

使用 `for` 和冒號可以依序遍歷陣列。普通循環變數以元素值接收資料：

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

使用引用循環變數可以直接修改原始陣列元素：

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

對於結構體陣列，以值遍歷時修改循環變數不會寫回陣列；使用 `T&` 才會修改原元素。原生陣列和動態陣列都支援這種引用遍歷。

## 類型選擇

選擇陣列形式時，長度固定且屬於資料佈局的一部分時使用 `T[N]`；長度在執行階段變化，或需要使用 `push`、`remove_at` 等容器操作時使用 `array<T>`；需要在函式之間共用同一個可變容器時使用 `array<T>`，需要獨立的固定佈局資料時使用原生陣列。

`len(values)` 和 `values.capacity()` 傳回非負 `int64`。線性索引、`remove_at(index)` 和 `reserve(count)` 接收 `int64`；索引須符合 `0 <= index < len(values)`，預留容量須非負且能由宿主平台表示。實際配置仍受平台限制。數值範圍檢查規則請見[內建函式](../standard-library/builtins.md)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/arrays.md) · [fr-FR](../../fr-FR/language-basics/arrays.md) · [ja-JP](../../ja-JP/language-basics/arrays.md) · [ko-KR](../../ko-KR/language-basics/arrays.md) · [zh-CN](../../zh-CN/language-basics/arrays.md) · [zh-TW](arrays.md)
