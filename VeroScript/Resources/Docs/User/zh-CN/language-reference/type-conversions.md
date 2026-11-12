# 类型转换

类型转换决定一个表达式能否作为另一个类型的值使用。转换是否成立取决于源类型、目标类型以及表达式所在的上下文。

## 数值类型

兼容的整数和浮点类型可以按照语言的数值转换规则参与赋值、参数传递和运算。固定宽度整数用于需要明确数据宽度的接口；转换到更窄或无符号类型时，应确认取值范围不会被截断或改变。

```VeroScript
int count = 10;
double total = count;
int rounded = to_int(total);
```

需要明确表达意图时，使用目标类型构造或语言支持的显式转换形式；不要通过字符串拼接或隐式转换替代数值转换。

## 转换上下文

VeroScript 区分自动允许的隐式转换与必须显式请求的转换：

| 上下文 | 用途 | 主要规则 |
| --- | --- | --- |
| 赋值、传参和返回值 | 隐式转换 | 会考虑兼容的数值转换以及兼容的基类/接口转换；没有 `explicit` 的单值构造函数也可以参与。 |
| `cast<T>(value)` | 显式转换或构造 | 支持数值、作用域枚举、引用、接口和显式构造相关的转换，也可显式请求匹配的用户自定义转换运算符；检查型引用转换失败会产生运行时错误。 |
| `try_cast<T>(value)` | 可失败转换 | 返回 `optional<T>`；检查型引用转换失败时返回空 optional，且不会调用 `explicit` 构造函数。 |
| `const_cast<T>(value)` | 移除 const 访问能力 | 源表达式必须是 const，且与 `T` 的规范类型完全一致；它不是值转换，也不执行运行时类型检查。 |

编译器不会串联用户自定义转换。即使存在 `A` 到 `B`、再到 `C` 的转换，也不会把 `A` 隐式地连续转换为 `C`。

## 文本类型

`string` 和 `wstring` 是不同的类型，不能直接混合赋值、比较或拼接。使用标准库提供的显式转换函数：

```VeroScript
string text = "hello";
wstring wide = to_wstring(text);
string restored = to_string(wide);
```

数值和布尔值转为文本时使用 `to_string`，不要依赖混合类型的隐式拼接。

## 转换构造函数与转换运算符

接收一个值参数、且没有标记为 `explicit` 的构造函数可以参与隐式转换：

```VeroScript
struct Meter
{
	int value;

	Meter(int input)
	{
		value = input;
	}
};

Meter assigned = 7;
```

如果只允许直接构造或显式 `cast<T>`，应标记为 `explicit`：

```VeroScript
struct UserId
{
	int value;

	explicit UserId(int input)
	{
		value = input;
	}
};

UserId direct(7);
UserId converted = cast<UserId>(8);
// UserId invalid = 9; // Compile error.
```

`operator bool()` 这样的用户自定义转换运算符，也可以在语言允许的转换上下文中参与转换。应谨慎使用，保证隐式转换在调用点仍然直观。
`cast<T>` 还可以显式请求匹配的用户自定义转换运算符。`try_cast<T>` 仅限于文档所述的基本类型、作用域枚举和引用/接口场景，不会调用用户自定义构造函数或转换运算符。

## `cast<T>`

`cast<T>(value)` 请求显式转换。它可用于兼容的数值转换、作用域枚举与底层整数之间的转换、检查型类/接口引用转换，以及目标 struct/class 的显式构造。

```VeroScript
double precise = cast<double>(7);
int narrowed = cast<int>(2.9);

enum class Mode
{
	Idle = 3
};

Mode mode = cast<Mode>(3);
int raw_mode = cast<int>(mode);
```

对于类和接口引用，在需要向下转换或交叉转换时会检查运行时关系；失败会产生运行时错误。struct 转为接口时会形成接口所需的值表示，class 转为接口时保留类对象的共享身份。`cast<T>` 不会移除 const 访问能力；const 源得到的结果仍按 const 访问。需要有意移除 const 时才使用 `const_cast<T>`。

转换为结构体值成功后遵循值语义。

## 括号式转换语法

`(T)表达式` 是 `cast<T>(表达式)` 的另一种写法。它遵循完全相同的显式转换规则，包括数值、作用域枚举、引用/接口、显式构造以及匹配的用户自定义转换运算符：

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);

int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print("a={}, calls={}", a, calls);
	return 0;
}
```

这种形式在一元表达式位置被识别。简单用户类型名可能与值表达式产生歧义，因此当目标类型不够明确时，应使用 `cast<T>`。

`(void)表达式;` 是只能出现在语句位置的显式结果丢弃语法。它会准确执行表达式一次，保留副作用和运行时错误，只丢弃表达式结果。`for` 初始化语句也接受这种形式。它不是值表达式，不能用作参数、赋值结果、算术操作数或嵌套表达式。普通的 `表达式;` 具有相同的运行时行为；`(void)` 前缀用于明确表达丢弃结果的意图。

## `try_cast<T>`

`try_cast<T>(value)` 是可失败版本，适用于基本类型、作用域枚举以及引用/接口转换：

```VeroScript
class Base
{
};

class Derived : public Base
{
};

class Other
{
};

Base base = Derived();
optional<Derived> found = try_cast<Derived>(base);
optional<Other> missing = try_cast<Other>(base);

if (found.has_value())
{
	print("derived");
}

if (!missing.has_value())
{
	print("not compatible");
}
```

`try_cast` 在转换成功时返回 engaged optional。检查型引用转换失败时返回空 optional。null 引用转换仍然是到可空目标的成功转换，因此可能返回包含 `nullptr` 的 engaged optional。数值转换返回转换后的值，例如 `try_cast<int>(2.9)` 包含 `2`。

`try_cast<T>` 不会选择 `explicit` 构造函数，需要显式构造时应使用 `cast<T>`。

## `const_cast<T>`

`const_cast<T>(value)` 是范围很窄的 const 逃逸。源表达式必须是 const，并且解析别名后的规范类型必须与 `T` 完全一致：

```VeroScript
class Counter
{
public:
	int value = 41;
};

int main()
{
	readonly<Counter> stored = Counter();
	const_cast<Counter>(stored.get()).value = 42;
	print(stored.get().value);
	return 0;
}
```

本例修改共享类对象的字段。`const_cast<int>(view)` 得到的是标量值，不是可赋值的引用，因此 `const_cast<int>(view) = 42` 是无效写法。转换不会将标量副本变为原存储的别名。

它只移除编译期的 const 访问能力，不执行值转换、运行时类型改变或检查型转换。只应对明确允许修改的存储（例如受控的 `readonly<T>` 包装器）使用；应用于真正不可变的存储会破坏 `const` 原本提供的保护。

以下形式会被拒绝：

```VeroScript
const int number = 1;
// const_cast<double>(number); // The target type must be exactly int.
// const_cast<int>(mutable_value); // The source must be const.
```

## 结构体、类、接口和容器

结构体转换遵循值语义，复制会产生结构体值的副本；结构体字段如果本身是共享句柄，字段仍保留自己的句柄语义。类转换保留对象的共享身份：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Point : IReadable
{
	int value;
	int read() const override
	{
		return value;
	}
};

Point point = Point();
IReadable readable = point;
```

将结构体转换为接口会形成接口所需的值副本；将类转换为接口会保留类对象的共享身份。接口转换要求实际类型满足接口的全部纯虚方法。

容器的元素类型、键类型和值类型必须满足容器声明的类型规则。泛型函数和类型只有在实参或类型参数满足声明约束时才能实例化；`where` 约束不会自动放宽其他类型转换规则。

## 不应依赖的转换

`nullptr` 只适用于可以为空的句柄类型；文本类型不能通过普通赋值互换；结构体值和类句柄也不能因为字段形状相似而自动互换。宿主绑定和接口边界应优先使用明确的目标类型。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-reference/type-conversions.md) · [fr-FR](../../fr-FR/language-reference/type-conversions.md) · [ja-JP](../../ja-JP/language-reference/type-conversions.md) · [ko-KR](../../ko-KR/language-reference/type-conversions.md) · [zh-CN](type-conversions.md) · [zh-TW](../../zh-TW/language-reference/type-conversions.md)
