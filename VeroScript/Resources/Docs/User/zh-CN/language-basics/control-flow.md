# 控制流

控制流语句决定语句的执行顺序。VeroScript 支持条件语句、循环语句、`switch`、跳转和函数返回。

## `if`

`if` 根据条件选择代码块，`else if` 和 `else` 可选：

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

条件表达式必须是可以按布尔规则判断的表达式。使用花括号可以明确分支范围，即使分支中只有一条语句也建议保留花括号。

## `while`

`while` 在每次执行循环体前检查条件：

```VeroScript
int count = 3;
while (count > 0)
{
	print(count);
	count -= 1;
}
```

如果条件第一次判断为假，循环体不会执行。

## `do ... while`

`do` 会先执行一次循环体，再检查条件；末尾分号不可省略：

```VeroScript
int count = 0;
do
{
	count += 1;
}
while (count < 3);
```

条件必须是 `bool`。循环体中的 `continue` 会转到条件求值位置，`break` 会退出循环。

## `for`

传统 `for` 语句包含初始化、条件和迭代表达式：

```VeroScript
for (int i = 0; i < 10; ++i)
{
	print(i);
}
```

初始化部分可以声明局部变量，该变量的作用域限于 `for` 语句。条件和迭代表达式可以省略，但必须保证循环能够在预期位置结束。

初始化子句和迭代子句都可以包含逗号分隔的表达式列表，并严格从左到右求值。该语法仅属于 `for` 子句；VeroScript 不提供通用逗号运算符。初始化子句还可使用 `int index = 0, end = 3` 这样的同类型声明组；名称从左到右变得可见，作用域限于该 `for` 语句。

## 范围循环

范围循环使用 `:` 遍历数组或容器：

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

使用引用循环变量可以直接修改元素：

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

映射容器还可以使用键值对形式的循环变量；具体形式见容器文档。

## `break` 和 `continue`

`break` 结束最近的外围循环或 `switch`。`continue` 跳过当前迭代的剩余部分并转到最近的外围循环；`switch` 不是 `continue` 的目标：

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

`switch` 根据一个表达式的值选择 `case` 分支。当前实现支持整数、枚举、字符串和宽字符串作为判定值：

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

VeroScript 不允许未标记的隐式贯穿。需要继续执行下一个 `case` 时，必须显式写出 `fallthrough`：

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

`fallthrough;` 必须是 `case` 下最后一条直接语句。使用花括号组织分支内容时，将它放在该块之后，不要放进嵌套块、条件或循环。

## 异常

VeroScript 区分脚本异常与引擎或运行时故障。`throw expression;` 可以抛出任何非
`void` 的脚本值。`try` 语句必须位于函数内，并且至少包含一个 `catch` 或 `finally` 子句：

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

`catch` 子句按源码顺序检查。类型处理器可以写成 `catch (Type name)`、
`catch (const Type& name)`、`catch (Type& name)` 或 `catch (ref Type name)`。也可以省略
名称，写成 `catch (Type)`。`catch (...)` 接收任意脚本异常、不创建绑定，并且必须是最后一个
`catch` 子句。类型处理器匹配抛出值的类型；对于类或接口，也会匹配可赋值的派生对象。为了
让处理器匹配，不会执行数值转换。

按值绑定得到的是副本。引用绑定观察原始值，因此可写引用的修改也会改变后续传播的值。无
表达式的 `throw;` 只允许出现在 `catch` 处理器内，并会重新抛出原始异常对象，而不是已被
按值绑定修改的副本。抛出的值会跨越函数调用，直到找到匹配的处理器。

`finally` 可以放在处理器之后，也可以单独使用而不写 `catch`。当控制流离开受保护的 `try`
或 `catch` 时，它都会执行，包括正常结束、`return`、循环跳转、脚本异常传播和运行时故障
传播。`return`、`goto` 或跳出到外层循环的 `break`/`continue` 等离开 `finally` 的非局部转移
会被编译器拒绝。

`finally` 内可以使用循环和嵌套 `try`/`catch`。在内部捕获的异常不会改变待处理的外层返回值或异常；未捕获的脚本异常离开 `finally` 时，会替换待处理的转移并传播到外围处理器。清理调用析构函数并再次执行脚本代码时，也遵循这些规则。

引擎/运行时故障不是脚本值，不能被 `catch (...)` 吞掉。失败的 `assert` 也属于此类运行时
错误；这类故障传播时 `finally` 仍会执行。

## `goto` 和标签

标签由名称和冒号组成，`goto` 跳转到指定标签：

```VeroScript
int main()
{
	goto done;

	print("not reached");

	done:
	print("done");
}
```

`goto` 应限制在清晰、局部的控制流中使用，避免让函数形成难以分析的跳转图。

## `return`

`return` 结束当前函数。非 `void` 函数必须返回符合声明类型的值，`void` 函数可以不带返回值：

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

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/control-flow.md) · [fr-FR](../../fr-FR/language-basics/control-flow.md) · [ja-JP](../../ja-JP/language-basics/control-flow.md) · [ko-KR](../../ko-KR/language-basics/control-flow.md) · [zh-CN](control-flow.md) · [zh-TW](../../zh-TW/language-basics/control-flow.md)
