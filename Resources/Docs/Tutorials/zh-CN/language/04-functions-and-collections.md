# 04：函数和容器

当 `main` 开始变长，最有效的整理方式通常不是增加注释，而是把有明确职责的代码提取为函数。本篇会写一个求和函数，并用 `array<int>` 保存可变数量的数据。

## 第一步：写一个求和函数

```VeroScript
int sum(array<int> values)
{
	int result = 0;
	for (int value : values)
	{
		result = result + value;
	}
	return result;
}

int main()
{
	array<int> values = array<int>();
	values.push(10);
	values.push(20);
	values.push(30);

	print("sum={}", sum(values));
	return 0;
}
```

函数声明先写返回类型，再写名称和参数。`sum` 接收 `array<int>`，最后返回一个 `int`。

## 第二步：让容器增长

`array<int>()` 创建空动态数组，`push` 在末尾添加元素。你也可以使用花括号初始化：

```VeroScript
int main()
{
	array<int> values = array<int>{10, 20, 30};

	values.push(40);
	for (int& value : values)
	{
		value = value + 1;
	}

	print("size={}", len(values));
	print("first={}", values[0]);
	return 0;
}
```

普通范围遍历 `for (int value : values)` 得到元素副本；加上 `&` 后，循环变量成为元素别名，对它的修改会写回容器。

## 第三步：使用默认参数

```VeroScript
void print_values(array<int> values, string label = "values")
{
	print(label);
	for (int value : values)
	{
		print(value);
	}
}

int main()
{
	array<int> values = array<int>{1, 2, 3};
	print_values(values);
	print_values(values, "numbers");
	return 0;
}
```

默认参数让调用者可以省略常用配置。默认值必须与参数类型兼容，调用时仍可以显式传入不同值。

## 选择容器

先用 `array<T>` 表示有顺序的动态数据；需要按键查找时考虑 `map<K, V>`，需要去重时考虑 `set<T>`。不要因为容器名称很多就提前复杂化设计，先根据访问方式选择。

## 练习

1. 写一个 `average` 函数，处理空数组时返回 `0.0`。
2. 写一个函数，把数组中所有负数改成 `0`，使用引用范围遍历。
3. 将成绩按名字保存到 `map<string, int>` 中，并输出一个指定名字的成绩。

动态数组的完整行为见 [`examples/dynamic_array.vs`](../../../../../VeroScript/Resources/Scripts/examples/dynamic_array.vs)，引用遍历见 [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs)。容器总览见[数据类型](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/data-types.md)。

下一篇将把数据和行为组织成自己的类型：[结构体、类和接口](05-structs-classes-and-interfaces.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/language/04-functions-and-collections.md) · [fr-FR](../../fr-FR/language/04-functions-and-collections.md) · [ja-JP](../../ja-JP/language/04-functions-and-collections.md) · [ko-KR](../../ko-KR/language/04-functions-and-collections.md) · [zh-CN](04-functions-and-collections.md) · [zh-TW](../../zh-TW/language/04-functions-and-collections.md)
