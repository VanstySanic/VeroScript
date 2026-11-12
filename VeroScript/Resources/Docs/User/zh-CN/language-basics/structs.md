# 结构体

`struct` 用于定义具有值语义的用户类型。结构体可以包含字段、方法、构造函数和析构函数，也可以继承一个基类型或实现接口。结构体对象本身按值复制；字段的具体复制行为仍遵循字段类型自身的语义。

## 声明和成员

结构体声明由名称和成员列表组成。结构体成员默认是 `public`：

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	void translate(int dx, int dy)
	{
		this.x += dx;
		this.y += dy;
	}
};
```

在成员函数中，`this` 表示当前结构体对象。能够通过名称直接访问的成员，也可以使用 `this.member` 的形式访问。

结构体支持 `public`、`protected` 和 `private` 访问控制。虽然结构体默认为公开成员，但需要限制访问时应显式写出访问说明符：

```VeroScript
struct Account
{
public:
	int id = 0;

private:
	int balance = 0;
};
```

## 构造和初始化

字段可以提供默认初始化表达式。结构体也可以声明构造函数，构造时按照参数列表选择对应的重载：

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point()
	{
	}

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}
};

Point origin = Point();
Point point = Point(3, 4);
Point literal = Point{7, 8};
```

构造初始化列表可以在构造函数体之前初始化当前类型直接声明的实例字段。列表中的字段项按照字段声明顺序初始化。已有声明初始化器的字段不能再次出现在列表中；`const` 字段的完整规则参见[常量](constants.md)。

花括号形式使用类型明确的初始化语法，并遵循相同的构造函数匹配规则。没有显式构造函数时，字段默认值仍然参与默认构造。

## 值语义

结构体赋值会复制结构体值。修改复制结果的直接字段，不会改变原结构体：

```VeroScript
struct Counter
{
	int value = 0;
};

Counter first = Counter();
Counter second = first;
second.value = 10;

print(first.value);  // 0
print(second.value); // 10
```

值语义只描述结构体对象本身。若字段是 `array<T>`、`map<K, V>` 或其他共享句柄类型，复制结构体时该字段仍遵循共享句柄语义；需要独立容器时应显式复制容器内容。

## 方法和引用

结构体方法可以读取或修改当前对象。需要让函数直接修改调用方的结构体时，应使用引用参数：

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}

	void translate(int dx, int dy)
	{
		x += dx;
		y += dy;
	}
};

void reset(ref Point point)
{
	point.x = 0;
	point.y = 0;
}

int main()
{
	Point point = Point{3, 4};
	point.translate(1, 2);
	reset(ref point);
	print("{}, {}", point.x, point.y);
	return 0;
}
```

普通参数和按值范围遍历会产生结构体值副本；引用参数和引用范围遍历才会写回原对象。详细规则见[引用](references.md)。

## `mutable` 实现字段

非静态实例字段在表示可由 `const` 查询更新的实现状态时，可以标记为 `mutable`，最常见的用途是缓存：

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

此例外只适用于被标记的字段。它不会让 const 对象整体可写，也不会保证并发访问安全。`mutable` 字段不能同时是 `const`、`static` 或引用字段。

## 继承和接口

结构体可以继承一个结构体基类，也可以实现一个或多个接口：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Counter : IReadable
{
	int value = 0;

	int read() const
	{
		return value;
	}
};
```

将结构体转换为接口类型时，会产生接口所需的值副本；通过接口修改该副本不会回写原结构体。接口继承见[接口](interfaces.md)专题，泛型约束见[泛型](generics.md)专题。

## 默认特殊成员

```VeroScript
struct Record
{
	int value = 0;
	Record() = default;
	Record(const Record& other) = default;
	Record& operator=(const Record& other) = default;
	bool operator==(const Record& other) const = default;
	bool operator!=(const Record& other) const = default;
};
```

结构体可以显式请求逐成员复制构造、赋值和相等比较。复制构造不重新执行字段初始化器；赋值更新已有子对象，并返回原对象的引用。const 或引用字段可能使默认赋值不可用；复制与比较仍分别判断。

类可以请求默认相等比较，普通复制仍共享句柄。删除或不可访问的操作不会退回原始复制。显式复制构造可以用于直接构造，不能用于隐式初始化；新构造结果不需要额外复制。不新增脚本移动操作。

上述声明使用核心执行语义，包括混合／即时编译策略下的虚拟机回退与核心预编译执行。独立的 C++、C#、Python 源码导出会拒绝尚未支持的默认成员。示例：`default_special_members.vs`。

嵌套类型、局部声明、访问规则和分段泛型实参请参见[类型作用域](scope.md)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/structs.md) · [fr-FR](../../fr-FR/language-basics/structs.md) · [ja-JP](../../ja-JP/language-basics/structs.md) · [ko-KR](../../ko-KR/language-basics/structs.md) · [zh-CN](structs.md) · [zh-TW](../../zh-TW/language-basics/structs.md)
