# 06：引用、引用返回和 Lambda 捕获

本篇通过几个可以直接运行的小程序，理解 VeroScript 中“同一个值的别名”和“闭包共享外层状态”之间的关系。引用适合原地修改已有对象或元素；Lambda 捕获则决定闭包如何保存外层绑定。

## 第一步：创建局部引用别名

```VeroScript
int main()
{
	int score = 40;
	int& alias = score;
	alias += 2;

	print("score={}, alias={}", score, alias);
	return 0;
}
```

`alias` 不是一份新的整数，而是 `score` 的引用别名。通过 `alias` 写入，会直接改变 `score`。引用绑定后不能改为引用另一个整数；如果你需要独立的值，应使用普通变量。

## 第二步：把引用传给函数

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	int value = 40;
	add_one(&value);
	print(value);
	return 0;
}
```

`int&` 表示可写引用参数，调用时使用 `&value` 明确表示传递别名。只读场景可以使用 `const int&`；它避免复制，同时禁止函数通过该引用修改对象。

## 第三步：返回一个可写引用

引用也可以作为函数返回值，但返回的对象必须在调用结束后仍然存在：

```VeroScript
int counter = 1;

int& counter_ref()
{
	return counter;
}

int main()
{
	counter_ref() += 4;
	int& alias = counter_ref();
	alias += 1;
	print(counter);
	return 0;
}
```

这里返回的是全局变量 `counter`，所以调用者可以继续通过返回值修改它。不要返回普通函数局部变量的引用，因为函数返回后该局部变量已经离开作用域。容器元素和对象成员也可以在其所有者仍然有效时作为引用返回。

## 第四步：用引用遍历原地修改容器

```VeroScript
int main()
{
	array<int> values = array<int>();
	values.push(1);
	values.push(2);

	for (int& value : values)
	{
		value += 10;
	}

	print(values[0]);
	print(values[1]);
	return 0;
}
```

`for (int& value : values)` 让 `value` 成为数组元素的别名，因此循环内的赋值会写回数组。去掉 `&` 后，循环变量按值取得元素，修改不会写回原容器。通过索引传递元素时也使用同样的显式形式，例如 `add_one(&values[0])`。

## 第五步：让 Lambda 捕获外层状态

```VeroScript
function() -> int make_counter()
{
	int running = 40;
	return [&]() -> int
	{
		running++;
		return running;
	};
}

int main()
{
	function() -> int next = make_counter();
	print(next());
	print(next());
	return 0;
}
```

`[&]` 表示按引用捕获使用到的外层局部变量。VeroScript 会把被捕获的局部绑定提升到安全的共享存储中，所以 `make_counter` 返回后，`next` 仍然可以安全地访问并修改 `running`。

如果只有一个变量需要共享修改，也可以写成 `[&running]`；需要混合策略时，可以使用 `[=, &running]`，让其他变量按值捕获而只让 `running` 按引用捕获。

## 第六步：捕获成员函数的接收者

接收者捕获遵循其类型本身的语义：

```VeroScript
class Counter
{
public:
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

struct Snapshot
{
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

int main()
{
	Counter counter = Counter();
	counter.value = 1;
	function() -> int live = counter.getter();
	counter.value = 2;

	Snapshot snapshot = Snapshot();
	snapshot.value = 3;
	function() -> int copied = snapshot.getter();
	snapshot.value = 4;

	print(live());
	print(copied());
	return 0;
}
```

类闭包会保留对象，因此输出 `2`；结构体闭包会保留值快照，因此输出 `3`。
没有捕获的 `[]` Lambda 不能访问 `this`。

## 什么时候使用哪一种

- 需要独立副本：使用普通值变量或按值捕获。
- 需要在函数中修改调用者已有的值：使用可写引用参数。
- 需要遍历并原地修改容器元素：使用引用范围变量。
- 需要让返回的闭包继续共享外层可变状态：使用按引用捕获。
- 只读且不希望复制较大对象：考虑 `const T&`，但确保被引用对象的生命周期足够长。

## 练习

1. 编写 `swap_values(int& left, int& right)`，交换两个整数，并用 `swap_values(&a, &b)` 调用。
2. 为 `array<int>` 编写一个引用范围循环，把所有元素乘以 `2`。
3. 修改 `make_counter`，增加一个按值捕获的 `step`，实现 `[=, &running]` 的计数器。
4. 尝试返回普通局部变量的引用，观察 `--check` 给出的诊断，并改为返回全局变量或仍然有效的成员。
5. 在创建闭包后修改 `Counter` 的字段，并与 `Snapshot` 的结果进行比较。

完整规则见[引用和引用语义](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/references.md)和[Lambda 和函数值](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/lambdas.md)。可运行示例见 [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs)、[`examples/reference_returns.vs`](../../../../../VeroScript/Resources/Scripts/examples/reference_returns.vs) 和 [`examples/lambda.vs`](../../../../../VeroScript/Resources/Scripts/examples/lambda.vs)。

下一篇学习如何拆分源文件：[模块、导入和预处理器](07-modules-and-preprocessor.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/language/06-references-and-captures.md) · [fr-FR](../../fr-FR/language/06-references-and-captures.md) · [ja-JP](../../ja-JP/language/06-references-and-captures.md) · [ko-KR](../../ko-KR/language/06-references-and-captures.md) · [zh-CN](06-references-and-captures.md) · [zh-TW](../../zh-TW/language/06-references-and-captures.md)
