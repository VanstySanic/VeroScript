# 函数

函数是带有名称、参数列表和返回类型的可调用声明。函数可以定义在模块作用域，也可以作为结构体、类或接口的成员。

## 函数声明

函数声明包含返回类型、名称、参数列表和函数体：

```VeroScript
int add(int left, int right)
{
	return left + right;
}
```

`void` 表示函数不返回值：

```VeroScript
void print_message(string message)
{
	print(message);
}
```

## `main` 入口

应用脚本的入口函数可以返回 `int` 或 `void`，最多接收一个 `string[]` 参数。返回 `int` 时，返回值成为脚本的进程退出码；返回 `void` 时按 `0` 处理。入口参数不能是引用、可变参数或带默认值的参数：

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

入口 `main` 只能来自入口模块；库模块不能提供应用入口。

`int main` 正常返回或执行到函数末尾时，未显式调用 `exit` 的结果码分别来自 `return` 表达式或 `0`。`exit(n)` 的结果优先于 `main` 的返回值，并且 `exit(0)` 仍然表示脚本明确请求退出。对引擎 API 而言，非零的 `ExitCode` 不一定表示引擎失败；宿主应同时检查 `bSuccess`、`ExitCode` 和 `bExitRequested`。

函数体中的参数和局部变量遵循函数和代码块的作用域规则。

## 参数和返回值

参数按声明顺序接收调用方传入的实参，调用时实参类型必须满足参数类型的转换规则：

```VeroScript
double average(double total, int count)
{
	return total / count;
}
```

`return` 表达式的类型必须与函数返回类型兼容。`void` 函数可以使用不带表达式的 `return` 提前结束执行。

## 默认参数

参数可以声明默认值。调用方省略该参数时，使用声明中的默认表达式：

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

默认参数应从参数列表的末尾开始连续声明，不能在必选参数之后再出现省略位置不明确的默认参数。

## 函数重载

同一作用域中可以声明多个同名函数，只要参数列表能够区分：

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

调用函数时，编译器根据函数名、参数数量、参数类型和可用的类型转换选择重载。存在多个同样匹配的候选时，编译器会报告重载歧义。

## 引用参数

引用参数允许函数修改调用方的对象。公共示例推荐使用 `T&` 声明；`ref T` 是等价的关键字式写法：

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

调用引用参数时，实参也使用匹配的 `&` 或 `ref` 标记。VeroScript 没有通用的脚本指针类型，引用不支持指针运算。

## 可变参数

函数可以使用 `T... name` 声明一组同类型的可变参数：

```VeroScript
void print_all(string... values)
{
	for (string value : values)
	{
		print(value);
	}
}
```

可变参数在函数体内表现为对应元素类型的数组。可变参数必须位于参数列表末尾。

## 函数值和 Lambda

函数可以作为值传递，也可以通过 Lambda 创建匿名函数。函数类型使用参数列表和返回类型描述：

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

`fn` 是 `function` 的简写形式。函数值和 Lambda 的捕获规则、泛型函数和成员函数将在专门文档中说明。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/functions.md) · [fr-FR](../../fr-FR/language-basics/functions.md) · [ja-JP](../../ja-JP/language-basics/functions.md) · [ko-KR](../../ko-KR/language-basics/functions.md) · [zh-CN](functions.md) · [zh-TW](../../zh-TW/language-basics/functions.md)
