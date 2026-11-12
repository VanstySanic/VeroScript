# 数组

VeroScript 有两种数组形式：动态数组 `array<T>` 和固定长度原生数组 `T[N]`。两者都支持索引和范围遍历，但类型和运行时语义不同。

## 动态数组

`array<T>` 是由元素类型 `T` 参数化的动态容器：

```VeroScript
array<int> values = array<int>();
values.push(10);
values.push(20);

print(values[0]);
print(len(values));
```

也可以使用花括号初始化：

```VeroScript
array<int> values = array<int>{1, 2, 3};
```

动态数组的常用操作包括：

| 操作 | 说明 |
| --- | --- |
| `values[index]` | 读取或修改指定索引的元素 |
| `values.push(value)` | 在末尾追加一个元素，并返回数组本身 |
| `values.remove(value)` | 删除第一个与值相等的元素，返回是否删除成功 |
| `values.remove_at(index)` | 删除指定索引的元素 |
| `values.clear()` | 清空数组，并返回数组本身 |
| `values.reserve(count)` | 预留容量，并返回数组本身 |
| `values.capacity()` | 返回当前容量 |
| `len(values)` | 返回元素数量 |

`array<T>` 的元素必须与 `T` 匹配。向数组中加入结构体或固定数组时，运行时会按值语义复制这些元素；类和其他句柄类型仍保持其句柄语义。

动态数组值通过共享容器句柄保存。将一个 `array<T>` 赋给另一个变量时，两个变量指向同一个动态数组；如果需要独立副本，应显式构造并复制元素。

## 固定长度原生数组

原生数组把长度写在元素类型后面：

```VeroScript
int numbers[3] = {10, 20, 30};
```

数组长度属于类型的一部分。`int[3]`、`int[4]` 和 `array<int>` 是不同的类型。原生数组可以作为结构体字段，也可以声明为多维数组：

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

多维原生数组由嵌套的原生数组组成。`len(matrix)` 返回第一维的长度，`len(matrix[0])` 返回第二维的长度。

原生数组类型最多包含 64 个维度。每个维度最多包含 65,536 个元素，所有维度展平后的元素数量乘积最多为 1,048,576。源码超过任一上限时会在编译阶段被拒绝。

## 索引

数组索引从 `0` 开始：

```VeroScript
array<string> names = array<string>{"Ada", "Linus"};
string first = names[0];
names[1] = "Grace";
```

索引表达式的结果类型是元素类型。索引必须是整数，并且必须处于当前数组的有效范围内；越界访问会产生运行时错误。

## 范围遍历

使用 `for` 和冒号可以按顺序遍历数组。普通循环变量按元素值接收数据：

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

使用引用循环变量可以直接修改原数组元素：

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

对于结构体数组，按值遍历时修改循环变量不会写回数组；使用 `T&` 才会修改原元素。原生数组和动态数组都支持这种引用遍历。

## 类型选择

选择数组形式时，长度固定且属于数据布局的一部分时使用 `T[N]`；长度在运行时变化，或需要使用 `push`、`remove_at` 等容器操作时使用 `array<T>`；需要在函数之间共享同一个可变容器时使用 `array<T>`，需要独立的固定布局数据时使用原生数组。

`len(values)` 和 `values.capacity()` 返回非负 `int64`。线性索引、`remove_at(index)` 和 `reserve(count)` 接收 `int64`；索引须满足 `0 <= index < len(values)`，预留容量须非负且能由宿主平台表示。实际分配仍受平台限制。数值范围检查规则见[内置函数](../standard-library/builtins.md)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/arrays.md) · [fr-FR](../../fr-FR/language-basics/arrays.md) · [ja-JP](../../ja-JP/language-basics/arrays.md) · [ko-KR](../../ko-KR/language-basics/arrays.md) · [zh-CN](arrays.md) · [zh-TW](../../zh-TW/language-basics/arrays.md)
