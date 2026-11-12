# 函式

函式是帶有名稱、參數列表和傳回類型的可呼叫宣告。函式可以定義在模組作用域，也可以作為結構體、類別或介面的成員。

## 函式宣告

函式宣告包含傳回類型、名稱、參數清單和函式體：

```VeroScript
int add(int left, int right)
{
	return left + right;
}
```

`void` 表示函式不傳回值：

```VeroScript
void print_message(string message)
{
	print(message);
}
```

## `main` 入口

應用程式腳本的入口函式可以傳回 `int` 或 `void`，最多接收一個 `string[]` 參數。傳回 `int` 時，傳回值成為腳本的處理程序退出碼；傳回 `void` 時按 `0` 處理。入口參數不能是引用、可變參數或帶預設值的參數：

```VeroScript
int main(string[] args)
{
	if (len(args) == 0)
	{
		return 2;
	}
	print(args[0]);
	return 0;
}
```

入口 `main` 只能來自入口模組；庫模組不能提供應用入口。

`int main` 正常回傳或執行到函式末端時，未明確呼叫 `exit` 的結果碼分別來自 `return` 運算式或 `0`。 `exit(n)` 的結果優先於 `main` 的回傳值，且 `exit(0)` 仍表示腳本明確要求退出。對引擎 API 而言，非零的 `ExitCode` 不一定表示引擎失敗；宿主應同時檢查 `bSuccess`、`ExitCode` 和 `bExitRequested`。

函式體中的參數和局部變數遵循函式和程式碼區塊的作用域規則。

## 參數和回傳值

參數依宣告順序接收呼叫方傳入的實參，呼叫時實參類型必須符合參數類型的轉換規則：

```VeroScript
double average(double total, int count)
{
	return total / count;
}
```

`return` 運算式的類型必須與函式傳回類型相容。 `void` 函式可以使用不含運算式的 `return` 提前結束執行。

## 預設參數

參數可以宣告預設值。當呼叫方省略此參數時，使用宣告中的預設運算式：

```VeroScript
int clamp(int value, int minimum = 0, int maximum = 100)
{
	if (value < minimum)
	{
		return minimum;
	}
	if (value > maximum)
	{
		return maximum;
	}
	return value;
}

int main()
{
	print(clamp(120));
	print(clamp(-1, -10, 10));
}
```

預設參數應從參數清單的末端開始連續宣告，不能在必選參數之後再出現省略位置不明確的預設參數。

## 函式重載

在同一作用域中可以宣告多個同名函式，只要參數清單能區分：

```VeroScript
int abs_value(int value)
{
	return value < 0 ? -value : value;
}

double abs_value(double value)
{
	return value < 0.0 ? -value : value;
}
```

呼叫函式時，編譯器會根據函式名稱、參數數量、參數類型和可用的類型轉換選擇重載。當存在多個同樣匹配的候選時，編譯器會報告重載歧義。

## 引用參數

引用參數允許函式修改呼叫方的物件。公共範例建議使用 `T&` 宣告；`ref T` 是等價的關鍵字式寫法：

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
}
```

呼叫參考參數時，實參也使用匹配的 `&` 或 `ref` 標記。 VeroScript 沒有通用的腳本指標類型，引用不支援指標運算。

## 可變參數

函式可以使用 `T... name` 宣告一組同類型的可變參數：

```VeroScript
void print_all(string... values)
{
	for (string value : values)
	{
		print(value);
	}
}
```

可變參數在函式體內表現為對應元素類型的陣列。可變參數必須位於參數清單末端。

## 函式值和 Lambda

函式可以作為值傳遞，也可以透過 Lambda 建立匿名函式。函式類型使用參數列表和傳回類型描述：

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

int main()
{
	function(int) -> int increment = function(int value) -> int => value + 1;
	print(apply(increment, 41));
}
```

`fn` 是 `function` 的簡寫形式。函式值和 Lambda 的擷取規則、泛型函式和成員函式將在專門檔案中說明。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/functions.md) · [fr-FR](../../fr-FR/language-basics/functions.md) · [ja-JP](../../ja-JP/language-basics/functions.md) · [ko-KR](../../ko-KR/language-basics/functions.md) · [zh-CN](../../zh-CN/language-basics/functions.md) · [zh-TW](functions.md)
