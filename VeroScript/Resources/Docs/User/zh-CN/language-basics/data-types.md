# 数据类型

VeroScript 的类型用于确定值的表示方式、可执行的运算以及允许的转换。类型可以分为内置类型、容器类型和用户定义类型。

## 内置类型

| 类别 | 类型 |
| --- | --- |
| 无返回值 | `void` |
| 布尔 | `bool` |
| 有符号整数 | `char`、`short`、`int`、`long` |
| 无符号整数 | `unsigned char`、`unsigned short`、`unsigned int`、`unsigned long` |
| 固定宽度整数 | `int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32`、`int64`、`uint64` |
| 浮点数 | `float`、`double` |
| 文本 | `string`、`wstring` |

`void` 只能用作函数返回类型，不能用于声明变量。固定宽度整数和 `float`/`double` 是独立的语言类型，不是仅用于文档的别名；它们适合用于需要明确数据宽度或浮点精度的接口和数据结构。

固定宽度整数的取值范围为：`int8` 为 `-128` 到 `127`，`uint8` 为 `0` 到 `255`；`int16` 为 `-32768` 到 `32767`，`uint16` 为 `0` 到 `65535`；`int32`、`uint32`、`int64` 和 `uint64` 分别使用对应的 32 位和 64 位有符号/无符号范围。字面量超出目标类型范围时应显式处理，不能依赖截断。

```VeroScript
int8 signed_byte = -128;
uint8 byte = 255u;
float single = 2.05f;
double precise = 2.05;
```

带 `f`/`F` 的浮点字面量保存为 `float`；没有该后缀的浮点字面量保存为 `double`。因此 `float` 的舍入行为可能与 `double` 不同。

## 容器类型

容器类型通过类型参数描述元素类型或键值类型：

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
set<string> names = set<string>();
queue<int> pending = queue<int>();
```

VeroScript 提供以下容器类别：

- 序列容器：`array`、`deque`。
- 关联容器：`map`、`multimap`、`ordered_map`、`ordered_multimap`。
- 集合容器：`set`、`multiset`、`ordered_multiset`。
- 适配器：`queue`、`priority_queue`、`stack`。
- 并发容器：`channel`。

容器的具体操作、元素语义和并发规则应参阅容器与并发文档。

## 原生数组

固定长度数组把长度写在元素类型后面：

```VeroScript
int numbers[3] = {10, 20, 30};
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
```

`T[N]` 的长度属于类型的一部分，`array<T>` 则是动态数组容器。两者可以使用索引访问，但不是同一种类型。

## 用户定义类型

用户可以使用 `struct`、`class`、`interface` 和 `enum` 定义新类型：

```VeroScript
struct Point
{
	int x;
	int y;
};

enum class Color
{
	Red,
	Green,
	Blue
};
```

`struct` 具有值语义，`class` 具有共享身份。具体的字段、方法和对象语义见[结构体](structs.md)与[类](classes.md)；接口和枚举见[接口](interfaces.md)与[枚举](enums.md)，泛型会在独立的语言文档中说明。

## `auto`

`auto` 根据初始化表达式推导变量类型：

```VeroScript
auto count = 3;
auto values = array<int>{1, 2, 3};
```

`auto` 声明必须包含初始化表达式。类型推导只发生在声明处，变量声明完成后仍然具有确定的静态类型。

## 类型转换

兼容的数值类型可以按语言规则进行转换；`string` 和 `wstring` 之间不能隐式混用。需要在不同文本类型之间转换时，应使用相应的显式转换函数。

类型转换是否允许取决于源类型、目标类型和表达式上下文。对于宿主绑定和泛型接口，应尽量使用明确的类型，避免依赖隐式转换。

## 只读包装器

`readonly<T>` 是按值保存 `T` 的标准包装器。包装器本身可以复制或放入容器，并通过 `const ref T get() const` 只读暴露内部值。

```VeroScript
readonly<int> count = readonly<int>(41);
const ref int view = count.get();
print(view);
```

它没有隐式默认构造函数。对于 struct，它保存值副本；对于 class，它保存类句柄，因此保留共享身份。有关有意使用 `const_cast<T>` 逃逸的说明，请参见[类型转换](../language-reference/type-conversions.md)。

内置类型名 `optional`、`readonly`、`weak`、`shared`、`type`、`field_info`、`method_info`、`module_info`、`function_info`、`global_info` 和 `enum_member_info` 均为保留名称，脚本不能重新声明。

## 编译器提供的工具类型

VeroScript 还提供 `optional<T>`、`weak<T>`、`pair<T, U>` 和 `shared<T>` / `handle<T>`。它们的构造函数、状态规则、所有权行为以及映射条目语义，统一说明在[值工具类型](value-utility-types.md)中。

`pair<T, U>` 和 `handle<T>` 是软泛型内置类型：如果存在同名且泛型参数数量相同的用户声明，用户声明会优先于编译器提供的形式。这一点不同于上面列出的严格保留名称。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/data-types.md) · [fr-FR](../../fr-FR/language-basics/data-types.md) · [ja-JP](../../ja-JP/language-basics/data-types.md) · [ko-KR](../../ko-KR/language-basics/data-types.md) · [zh-CN](data-types.md) · [zh-TW](../../zh-TW/language-basics/data-types.md)
