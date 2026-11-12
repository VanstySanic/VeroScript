# 引用和引用語意

引用是對已有儲存位置或控制代碼槽位的別名，不會建立新的獨立對象，也不擁有被引用對象。 VeroScript 的引用用於參數、局部別名、範圍遍歷、元素存取、引用傳回和 Lambda 引用擷取；它不是通用腳本指針，不支援指針運算。

## 引用參數

建議使用帶尾隨 `&` 的寫法宣告引用參數：

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
	print(value);
	return 0;
}
```

`ref T` 和 `const ref T` 是等價的關鍵字式寫法：

```VeroScript
void reset(ref int value)
{
	value = 0;
}

int main()
{
	int value = 1;
	reset(ref value);
	return 0;
}
```

兩種宣告風格必須與對應的呼叫標記相符：`T&` 使用 `&expression`，`ref T` 使用 `ref expression`。引用實參必須是可定位的儲存位置，例如變數、陣列元素、映射元素或物件欄位。

## 唯讀引用

只讀參考可以使用 `const T&` 或 `const ref T`。它可以讀取目標，但不能透過該別名修改目標：

```VeroScript
int read_value(const int& value)
{
	return value;
}

int main()
{
	const int value = 42;
	print(read_value(&value));
	return 0;
}
```

可變引用不能綁定到只讀物件或只讀引用回傳值。如果函式只需要讀取對象，應優先使用唯讀參考。

## 局部引用別名

局部變數可以宣告為已有物件的引用別名：

```VeroScript
int main()
{
	int value = 1;
	int& alias = value;
	const int& view = value;

	alias = 2;
	print(view);
	return 0;
}
```

`alias` 和 `view` 都指向 `value` 的儲存位置。引用變數不能脫離目標獨立存在，也不會因為宣告引用而複製目標物件。

## 範圍遍歷中的引用

普通範圍遍歷變數會以元素值接收資料。對於結構體等值類型，循環體內的修改不會寫回原容器：

```VeroScript
for (Point point : points)
{
	point.x = 0;
}
```

使用 `T&` 可以讓循環變數直接別名到原元素：

```VeroScript
for (Point& point : points)
{
	point.x = 0;
}
```

陣列、固定長度原生陣列、雙端佇列和映射值都支援引用範圍遍歷。對唯讀集合，應使用 `const T&`。

當映射遍歷需要同時存取鍵和值時，可以使用 `pair<K, V>&`；只需要值時可以遍歷 `V&`：

```VeroScript
for (pair<string, int>& entry : counts)
{
	entry.second += 1;
}
```

## 元素引用

陣列或映射元素可以直接作為引用實參：

```VeroScript
void bump(int& value)
{
	value += 1;
}

bump(&values[0]);
bump(&counts["total"]);
```

元素引用不會複製元素，函式傳回後透過引用產生的修改仍然保留在原始容器中。嵌套欄位和容器元素也可以形成引用路徑，但路徑中的每一層物件都必須仍然有效。

## 引用傳回

函式可以傳回可寫引用或只讀引用。引用回傳值是一個可繼續參與賦值、遞增或綁定的左值：

```VeroScript
int Counter = 1;
array<int> Values = array<int>();

int& counter_ref()
{
	return Counter;
}

int& first_value()
{
	return Values[0];
}

const int& read_counter()
{
	return Counter;
}

int main()
{
	Values.push(40);
	counter_ref() += 4;
	first_value() += 2;
	print(read_counter());
	return 0;
}
```

引用傳回必須指向仍然存在的儲存位置。可以傳回全域變數、容器元素、物件欄位或已有參考；不能傳回普通函式局部變數，因為函式退出後該堆疊儲存已經失效。可寫引用不能綁定到 `const` 引用回傳值。

類別控制代碼變數也可以透過引用回傳進行重新綁定：引用指向的是控制代碼槽位，而不是複製一個新物件。這個行為與類別本身的共享身分語意不同，應在 API 中明確說明。

## 引用與函式類型

引用方向是函式類型的一部分。函式值和 Lambda 的參數類型可以使用 `int&` 或 `const int&`，呼叫時仍必須傳入 `&` 的實參：

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	function(int&) -> void mutate = add_one;
	int value = 3;
	mutate(&value);
	print(value);
	return 0;
}
```

## 限制和生命週期

- 可寫入參考參數不能宣告預設值；唯讀參考參數可使用支援的常值預設值，綁定呼叫端保存的暫存值。參考參數不能是可變參數。
- 引用不擁有對象，不進行指針運算，也不會自動延長一般對象的生命週期。
- `ref`、`const ref`、`T&` 和 `const T&` 會影響函式類型、重載和綁定檢查。
- 普通引用必須在目標儲存位置有效期間使用。
- Lambda 的引用擷取使用受管理的共享引用單元，可以安全地讓閉包離開定義它的函式；具體規則見[Lambda 和函式值](lambdas.md)。

完整的可執行範例請參閱[範例索引](../examples.md)中的參考範例。

容器元素參考借用原容器和原項目；重新綁定容器變數不會改變參考目標。原地賦值、尾端新增及插入其他鍵會保留目標；項目已失效或原位置已被其他元素佔用時，後續存取會回報無效參考。受管理的參考回傳與 Lambda 擷取繼續遵循既有生命週期規則。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/references.md) · [fr-FR](../../fr-FR/language-basics/references.md) · [ja-JP](../../ja-JP/language-basics/references.md) · [ko-KR](../../ko-KR/language-basics/references.md) · [zh-CN](../../zh-CN/language-basics/references.md) · [zh-TW](references.md)
