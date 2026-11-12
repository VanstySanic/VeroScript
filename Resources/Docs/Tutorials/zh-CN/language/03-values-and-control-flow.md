# 03：变量、条件和循环

本篇用一个小型成绩统计程序，把变量、静态类型、条件分支和循环放在一起。重点不是记住某个例子，而是学会把一个问题拆成“状态、判断和重复动作”。

## 第一步：准备数据

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	print("total={}", total);
	return 0;
}
```

`total` 和 `count` 都是 `int`。`i` 只在 `for` 循环内部有效，这种局部作用域可以减少变量互相干扰。

## 第二步：加入判断

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	if (total >= 15)
	{
		print("pass: {}", total);
	}
	else
	{
		print("retry: {}", total);
	}
	return 0;
}
```

`if` 的条件必须能得到布尔结果。比较表达式会产生 `bool`；不要把整数当成条件使用来代替明确判断。

## 第三步：让数据来自数组

```VeroScript
int main()
{
	int scores[4] = {72, 88, 91, 65};
	int total = 0;

	for (int i = 0; i < len(scores); i = i + 1)
	{
		total = total + scores[i];
	}

	double average = to_double(total) / len(scores);
	print("average={}", average);
	return 0;
}
```

`scores` 是固定长度原生数组。`len(scores)` 返回它的长度，索引从 `0` 开始。需要动态增加元素时，可以改用 `array<int>`。

## 第四步：使用浮点字面量

当 `.` 后面紧跟十进制数字时，可以省略浮点字面量的整数部分。后缀仍然决定存储类型：`.5f` 是 `float`，而 `.5` 和 `.5e2` 是 `double` 值。

```VeroScript
int main()
{
	float ratio = .5f;
	double half = .5;
	double fifty = .5e2;

	print("ratio={}, half={}, fifty={}", ratio, half, fifty);
	return 0;
}
```

`0.5f` 和 `.5f` 的值与类型相同。小数点后必须保留数字；单独的 `.` 仍然用于成员访问。

## 第五步：处理脚本异常

对于脚本能够描述并恢复的失败，可以使用 `throw`。异常值可以跨越函数调用传播，并由匹配的
`catch` 处理。无论操作成功还是因异常离开，都必须执行的清理工作应放在 `finally` 中：

```VeroScript
void validate_score(int score)
{
	if (score < 0)
	{
		throw "score must not be negative";
	}
}

int main()
{
	try
	{
		validate_score(-1);
	}
	catch (const string& message)
	{
		print("handled: {}", message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

这段程序会先输出处理后的消息，再输出 `cleanup`。异常类型重要时使用类型处理器；`catch (...)`
只应作为最后的兜底处理。处理器内不带表达式的 `throw;` 会重新抛出原始值。

完整异常示例请参阅 [`examples/exceptions.vs`](../../../../../VeroScript/Resources/Scripts/examples/exceptions.vs)。

## 第六步：使用 `assert` 检查不变量

`assert(condition[, message])` 是运行时检查。条件必须是 `bool`，可选消息必须是 `string`：

```VeroScript
int main()
{
	int total = 15;
	assert(total >= 0, "total must be non-negative");
	print("ready");
	return 0;
}
```

条件为真时继续执行且不产生输出。条件为假时报告运行时错误并停止当前执行；它不是脚本异常，
因此不能由 `catch (...)` 处理。

## 你已经用到的控制流概念

- `if`：根据条件选择一条路径。
- `for`：在明确的初始化、条件和递增规则下重复执行。
- `try`/`catch`：处理 `throw` 抛出的脚本值，`finally`：执行清理工作。
- `assert`：检查布尔不变量，并在失败时报告运行时错误。
- `return`：在函数中提前返回结果，下一篇会详细使用。

## 练习

1. 在循环中计算最大分数和最小分数。
2. 只统计大于等于 `60` 的分数。
3. 把固定数组改成 `array<int>`，并使用范围遍历。
4. 把 `0.5` 这样的十进制字面量改写为 `.5`，并在需要 `float` 值的地方使用 `.5f`。
5. 为拒绝负分的函数添加类型 `catch` 和 `finally` 块。
6. 为分数计算中的不变量添加带消息的 `assert`。

如果想对照完整数组示例，可以阅读 [`examples/arrays.vs`](../../../../../VeroScript/Resources/Scripts/examples/arrays.vs)。语言细节见[变量](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/variables.md)、[控制流](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/control-flow.md)和[数组](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/arrays.md)。

下一篇会把重复逻辑抽成函数，并使用动态容器：[函数和容器](04-functions-and-collections.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/language/03-values-and-control-flow.md) · [fr-FR](../../fr-FR/language/03-values-and-control-flow.md) · [ja-JP](../../ja-JP/language/03-values-and-control-flow.md) · [ko-KR](../../ko-KR/language/03-values-and-control-flow.md) · [zh-CN](03-values-and-control-flow.md) · [zh-TW](../../zh-TW/language/03-values-and-control-flow.md)
