# 控制流程

控制流語句決定語句的執行順序。 VeroScript 支援條件語句、循環語句、`switch`、跳轉和函式傳回。

## `if`

`if` 依條件選擇代碼區塊，`else if` 和 `else` 可選：

```VeroScript
if (score >= 60)
{
	print("pass");
}
else if (score >= 0)
{
	print("fail");
}
else
{
	print("invalid score");
}
```

條件式必須是可以依布林規則判斷的運算式。使用花括號可以明確分支範圍，即使分支中只有一條語句也建議保留花括號。

## `while`

`while` 在每次執行迴圈體前檢查條件：

```VeroScript
int count = 3;
while (count > 0)
{
	print(count);
	count -= 1;
}
```

如果條件第一次判斷為假，則循環體不會執行。

## `do ... while`

`do` 會先執行一次循環體，再檢查條件；末尾分號不可省略：

```VeroScript
int count = 0;
do
{
	count += 1;
}
while (count < 3);
```

條件必須是 `bool`。循環體中的 `continue` 會轉到條件求值位置，`break` 會退出循環。

## `for`

傳統 `for` 語句包含初始化、條件和迭代運算式：

```VeroScript
for (int i = 0; i < 10; ++i)
{
	print(i);
}
```

初始化部分可以宣告局部變數，該變數的作用域限於 `for` 語句。條件和迭代運算式可以省略，但必須保證循環能夠在預期位置結束。

初始化子句和迭代子句都可以包含逗號分隔的表達式清單，並嚴格從左到右求值。該語法僅屬於 `for` 子句；VeroScript 不提供通用逗號運算子。初始化子句還可使用 `int index = 0, end = 3` 這樣的同型別宣告群組；名稱從左到右變得可見，作用域限於該 `for` 陳述式。

## 範圍循環

範圍循環使用 `:` 遍歷陣列或容器：

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

使用引用循環變數可以直接修改元素：

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

映射容器也可以使用鍵值對形式的循環變數；具體形式見容器檔案。

## `break` 和 `continue`

`break` 結束最近的外圍循環或 `switch`。`continue` 跳過目前迭代的剩餘部分並轉到最近的外圍循環；`switch` 不是 `continue` 的目標：

```VeroScript
for (int value : values)
{
	if (value < 0)
	{
		continue;
	}
	if (value == 100)
	{
		break;
	}
	print(value);
}
```

## `switch`

`switch` 根據一個運算式的值選擇 `case` 分支。目前實作支援整數、枚舉、字串和寬字串作為判定值：

```VeroScript
switch (state)
{
case 0:
	{
		print("off");
		break;
	}
case 1:
	{
		print("on");
		break;
	}
default:
	{
		print("unknown");
		break;
	}
}
```

VeroScript 不允許未標記的隱含貫穿。需要繼續執行下一個 `case` 時，必須明確寫出 `fallthrough`：

```VeroScript
switch (value)
{
case 1:
	{
		print("one");
	}
	fallthrough;
case 2:
	{
		print("one or two");
		break;
	}
default:
	{
		break;
	}
}
```

`fallthrough;` 必須是 `case` 下最後一條直接語句。使用花括號組織分支內容時，將它放在該區塊之後，不要放進巢狀區塊、條件或迴圈。

## 例外

VeroScript 會區分腳本例外與引擎或執行階段錯誤。`throw expression;` 可以擲回任何非
`void` 的腳本值。`try` 陳述式必須位於函式內，並且至少包含一個 `catch` 或 `finally` 子句：

```VeroScript
int main()
{
	try
	{
		throw "failed";
	}
	catch (const string& message)
	{
		print(message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

`catch` 子句依原始碼順序檢查。型別處理器可以寫成 `catch (Type name)`、
`catch (const Type& name)`、`catch (Type& name)` 或 `catch (ref Type name)`。也可以省略
名稱，寫成 `catch (Type)`。`catch (...)` 會接收任意腳本例外、不建立繫結，並且必須是最後一個
`catch` 子句。型別處理器會配對擲回值的型別；對類別或介面，也會配對可指派的衍生物件。為了
讓處理器配對，不會執行數值轉換。

依值繫結得到的是副本。參考繫結會觀察原始值，因此可寫參考的修改也會改變後續傳播的值。沒有
運算式的 `throw;` 只允許出現在 `catch` 處理器內，並會重新擲回原始例外物件，而不是已被依值
繫結修改的副本。擲回的值會跨越函式呼叫，直到找到相符的處理器。

`finally` 可以放在處理器之後，也可以不寫 `catch` 而單獨使用。當控制流程離開受保護的 `try`
或 `catch` 時，它都會執行，包括正常結束、`return`、迴圈跳轉、腳本例外傳播和執行階段錯誤
傳播。`return`、`goto` 或跳出到外層迴圈的 `break`/`continue` 等離開 `finally` 的非區域跳轉
會被編譯器拒絕。

`finally` 內可以使用迴圈和巢狀 `try`/`catch`。在內部捕獲的異常不會改變待處理的外層回傳值或異常；未捕獲的指令碼異常離開 `finally` 時，會取代待處理的轉移並傳播到外圍處理常式。清理呼叫解構函式並再次執行指令碼時，也遵循這些規則。

引擎/執行階段錯誤不是腳本值，不能被 `catch (...)` 吞掉。失敗的 `assert` 也屬於此類執行階段
錯誤；這類錯誤傳播時 `finally` 仍會執行。

## `goto` 和標籤

標籤由名稱和冒號組成，`goto` 跳到指定標籤：

```VeroScript
int main()
{
	goto done;

	print("not reached");

	done:
	print("done");
}
```

`goto` 應限制在清晰、局部的控制流中使用，避免讓函式形成難以分析的跳躍圖。

## `return`

`return` 結束目前函式。非 `void` 函式必須傳回符合宣告類型的值，`void` 函式可以不帶回傳值：

```VeroScript
int square(int value)
{
	return value * value;
}

void log_done()
{
	print("done");
	return;
}
```

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/control-flow.md) · [fr-FR](../../fr-FR/language-basics/control-flow.md) · [ja-JP](../../ja-JP/language-basics/control-flow.md) · [ko-KR](../../ko-KR/language-basics/control-flow.md) · [zh-CN](../../zh-CN/language-basics/control-flow.md) · [zh-TW](control-flow.md)
