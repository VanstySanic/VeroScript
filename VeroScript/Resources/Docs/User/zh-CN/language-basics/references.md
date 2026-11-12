# 引用和引用语义

引用是对已有存储位置或句柄槽位的别名，不会创建新的独立对象，也不拥有被引用对象。VeroScript 的引用用于参数、局部别名、范围遍历、元素访问、引用返回和 Lambda 引用捕获；它不是通用脚本指针，不支持指针运算。

## 引用参数

推荐使用带尾随 `&` 的写法声明引用参数：

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

`ref T` 和 `const ref T` 是等价的关键字式写法：

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

两种声明风格必须与对应的调用标记匹配：`T&` 使用 `&expression`，`ref T` 使用 `ref expression`。引用实参必须是可定位的存储位置，例如变量、数组元素、映射元素或对象字段。

## 只读引用

只读引用可以使用 `const T&` 或 `const ref T`。它可以读取目标，但不能通过该别名修改目标：

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

可变引用不能绑定到只读对象或只读引用返回值。如果函数只需要读取对象，应优先使用只读引用。

## 局部引用别名

局部变量可以声明为已有对象的引用别名：

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

`alias` 和 `view` 都指向 `value` 的存储位置。引用变量不能脱离目标独立存在，也不会因为声明引用而复制目标对象。

## 范围遍历中的引用

普通范围遍历变量按元素值接收数据。对于结构体等值类型，循环体内的修改不会写回原容器：

```VeroScript
for (Point point : points)
{
	point.x = 0;
}
```

使用 `T&` 可以让循环变量直接别名到原元素：

```VeroScript
for (Point& point : points)
{
	point.x = 0;
}
```

数组、固定长度原生数组、双端队列和映射值都支持引用范围遍历。对只读集合，应使用 `const T&`。

当映射遍历需要同时访问键和值时，可以使用 `pair<K, V>&`；只需要值时可以遍历 `V&`：

```VeroScript
for (pair<string, int>& entry : counts)
{
	entry.second += 1;
}
```

## 元素引用

数组或映射元素可以直接作为引用实参：

```VeroScript
void bump(int& value)
{
	value += 1;
}

bump(&values[0]);
bump(&counts["total"]);
```

元素引用不会复制元素，函数返回后通过引用产生的修改仍然保留在原容器中。嵌套字段和容器元素也可以形成引用路径，但路径中的每一级对象都必须仍然有效。

## 引用返回

函数可以返回可写引用或只读引用。引用返回值是一个可继续参与赋值、递增或绑定的左值：

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

引用返回必须指向仍然存在的存储位置。可以返回全局变量、容器元素、对象字段或已有引用；不能返回普通函数局部变量，因为函数退出后该栈存储已经失效。可写引用不能绑定到 `const` 引用返回值。

类句柄变量也可以通过引用返回进行重新绑定：引用指向的是句柄槽位，而不是复制一个新对象。这个行为与类自身的共享身份语义不同，应在 API 中明确说明。

## 引用与函数类型

引用方向是函数类型的一部分。函数值和 Lambda 的参数类型可以使用 `int&` 或 `const int&`，调用时仍必须传入带 `&` 的实参：

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

## 限制和生命周期

- 可写引用参数不能声明默认值；只读引用参数可以使用受支持的字面量默认值，并绑定由调用方保存的临时值。引用参数不能声明为可变参数。
- 引用不拥有对象，不进行指针运算，也不会自动延长普通对象的生命周期。
- `ref`、`const ref`、`T&` 和 `const T&` 会影响函数类型、重载和绑定检查。
- 普通引用必须在目标存储位置有效期间使用。
- Lambda 的引用捕获使用受管理的共享引用单元，可以安全地让闭包离开定义它的函数；具体规则见[Lambda 和函数值](lambdas.md)。

完整的可运行示例见[示例索引](../examples.md)中的引用示例。

容器元素引用借用原容器和原条目；重新绑定容器变量不会改变引用目标。原地赋值、尾部追加及插入无关键会保留目标；条目已失效或当前位置已被其他元素占用时，后续访问报告无效引用。受管理的引用返回和 Lambda 捕获继续遵循现有生命周期规则。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/references.md) · [fr-FR](../../fr-FR/language-basics/references.md) · [ja-JP](../../ja-JP/language-basics/references.md) · [ko-KR](../../ko-KR/language-basics/references.md) · [zh-CN](references.md) · [zh-TW](../../zh-TW/language-basics/references.md)
