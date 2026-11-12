# 05：结构体、类和接口

本篇通过一个“可读取对象”的小例子理解三种用户类型：结构体适合值，类适合共享身份，接口适合描述可替换的行为。

## 第一步：用结构体表示值

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	int distance_score() const
	{
		return x * x + y * y;
	}
};

int main()
{
	Point point = Point();
	point.x = 3;
	point.y = 4;
	print(point.distance_score());
	return 0;
}
```

结构体通常表示一组值。把结构体赋给另一个变量时，默认关注的是值复制；这适合坐标、配置和小型数据记录。

## 第二步：用类表示共享对象

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}

	void add(int amount)
	{
		value = value + amount;
	}
};

int main()
{
	Counter first = Counter(10);
	Counter second = first;
	second.add(5);
	print(first.value);
	return 0;
}
```

这个程序输出 `15`，因为 `first` 和 `second` 指向同一个类对象。类变量复制的是对象身份，而不是独立对象。需要另一个独立计数器时，应再次调用构造函数。

类成员默认是私有的，所以示例使用 `public:` 暴露构造函数和成员。对外只需要提供操作时，可以把字段留在私有区域，避免外部随意修改状态。

### 转换构造函数与 `explicit`

只接收一个值参数且没有 `explicit` 的构造函数可以参与隐式转换。如果只允许直接构造或通过 `cast<T>` 构造，应将它标记为 `explicit`：

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
```

`UserId invalid = 9;` 会被拒绝。这样可以让重载解析保持可预测，并避免意外构造包装类型。

`(T)表达式` 是 `cast<T>(表达式)` 的语法糖，并严格遵循相同的转换规则：

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);
```

当简单用户类型名可能与同名值表达式混淆时，建议使用 `cast<T>`。`(void)表达式;` 是只能出现在语句位置的显式结果丢弃语法。它会执行表达式一次，保留副作用和运行时错误，只丢弃表达式结果；普通的 `表达式;` 具有相同的运行时行为。`for` 初始化语句也接受这种形式，但 `(void)表达式` 不能用作嵌套值或函数参数。

```VeroScript
int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print(calls);
	return 0;
}
```

### `mutable` 缓存字段

只有当 `const` 方法确实需要更新缓存等实现状态时，才应在非静态的 `struct` 或 `class` 实例字段上使用上下文相关的 `mutable`。它不会让整个对象都可写，也不会使代码自动获得线程安全性，并且不能与 `const`、`static` 或引用字段组合使用。

```VeroScript
struct Label
{
	string source;
	mutable string cached;

	string text() const
	{
		if (cached == "")
		{
			cached = source;
		}
		return cached;
	}
};
```

### 可移植的只读载荷

`readonly<T>` 把 `T` 保存为普通值，因此包装对象本身仍可复制或放入容器，但 `get()` 只暴露 `const T&`。构造 `readonly<Struct>` 会捕获该结构体的值；构造 `readonly<Class>` 则保留类句柄。

仅当源表达式是 const，并且其规范运行时类型与 `T` 相同时，才应使用 `const_cast<T>(value)`。它不会执行转换或运行时类型检查，只会移除编译期的 const 访问能力。普通 `cast<T>` 会有意保留 const 访问能力。

保留上面的 `Label` 声明，在本步骤使用以下 `main`。它先创建 `label`，再构造只读包装；第一项输出为 `source`。

```VeroScript
int main()
{
	Label label = Label();
	label.source = "source";
	readonly<Label> published = readonly<Label>(label);
	print(published.get().text());

	// Deliberate escape: mutate the payload stored by the wrapper.
	const_cast<Label>(published.get()).cached = "recomputed";
	return 0;
}
```

### 在 `cast` 与 `try_cast` 之间选择

当转换必须成功、检查型引用转换失败应当报错时，使用 `cast<T>`。当关系可能不成立、调用方应检查 `optional<T>` 时，使用 `try_cast<T>`：

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

int main()
{
	Base value = Derived();
	optional<Derived> found = try_cast<Derived>(value);
	optional<Other> missing = try_cast<Other>(value);

	if (found.has_value())
	{
		print("derived");
	}
	if (!missing.has_value())
	{
		print("not compatible");
	}
	return 0;
}
```

`try_cast<T>` 处理基本类型、作用域枚举和引用/接口转换，但不会调用 `explicit` 构造函数。完整规则请参阅[类型转换](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-reference/type-conversions.md)。


## 第三步：用接口抽象行为

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Sensor : public IReadable
{
public:
	int value = 42;

	int read() const override
	{
		return value;
	}
};

int read_twice(IReadable value)
{
	return value.read() + value.read();
}

int main()
{
	Sensor sensor = Sensor();
	print(read_twice(sensor));
	return 0;
}
```

接口不负责创建对象，它只规定实现必须提供什么方法。`read_twice` 不需要知道传入的是哪一个具体类型，只依赖 `IReadable` 契约。

## 第四步：定义静态工具类型

当一个类型只是操作或常量的命名容器时，可以使用 `static class` 或 `static struct`。其中声明的所有成员都会按静态成员处理，因此调用时无需创建对象，只需使用类型名称和 `::`：

```VeroScript
public static class Math
{
	public int Base = 40;
	public int Add(int value)
	{
		return Math::Base + value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Add(2));
	print(Limits::MaxRetries);
}
```

静态工具类型不能被构造、继承，也不能声明为泛型类型。它们不能包含构造函数、析构函数、`using` 成员、默认初始化块、`mutable` 字段或 `virtual`、`const` 等实例修饰符。如果类型需要对象级状态或继承，请使用普通的 `class` 或 `struct`。

## 如何选择

- 数据需要独立复制：优先考虑 `struct`。
- 多处代码需要共享同一个可变对象：考虑 `class`。
- 调用者只需要知道“能做什么”：用 `interface` 参数隔离具体实现。
- 不需要对象身份或实例状态时，使用静态工具类型。

## 练习

1. 为 `Point` 增加一个 `move` 方法。
2. 为 `Counter` 增加 `reset`，并让 `value` 不再公开。
3. 创建第二个实现 `FixedSensor`，让它也实现 `IReadable`。

完整语义见[结构体](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/structs.md)、[类](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/classes.md)和[接口](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/interfaces.md)。综合示例见 [`examples/object_basics.vs`](../../../../../VeroScript/Resources/Scripts/examples/object_basics.vs)。

下一篇学习引用和闭包共享状态：[引用、引用返回和 Lambda 捕获](06-references-and-captures.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/language/05-structs-classes-and-interfaces.md) · [fr-FR](../../fr-FR/language/05-structs-classes-and-interfaces.md) · [ja-JP](../../ja-JP/language/05-structs-classes-and-interfaces.md) · [ko-KR](../../ko-KR/language/05-structs-classes-and-interfaces.md) · [zh-CN](05-structs-classes-and-interfaces.md) · [zh-TW](../../zh-TW/language/05-structs-classes-and-interfaces.md)
