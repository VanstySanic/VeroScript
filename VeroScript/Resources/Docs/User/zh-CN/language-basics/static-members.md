# 静态成员

使用 `static` 声明的成员属于类型本身，而不属于某一个对象实例。静态字段只有一份类型级存储；静态方法不需要通过对象调用。

## 静态工具类型

`static class` 和 `static struct` 用于声明不可实例化的工具容器。内部声明的所有字段和方法都会按静态成员处理，并通过类型名称和 `::` 访问：

```VeroScript
public static class Math
{
	public double Square(double value)
	{
		return value * value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Square(4.0));
	print(Limits::MaxRetries);
}
```

静态类或静态结构体不能被构造，也不能作为基类使用。它们不能是泛型，不能声明构造函数、析构函数、`using` 成员或默认初始化块。字段不能使用 `mutable`，方法不能使用 `virtual`、`override`、`final`、纯虚或 `const` 等实例修饰符。访问区域仍然有效：静态类遵循类的默认访问权限，静态结构体遵循结构体的默认访问权限。如果类型需要实例、继承或实例状态，应使用普通的 `class` 或 `struct`。

## 静态字段

静态字段可以使用类型名称和 `::` 访问。下面的 `Total` 会被 `Counter` 的所有实例共享：

```VeroScript
class Counter
{
public:
	static int Total = 0;
	int id;

	Counter()
	{
		Counter::Total = Counter::Total + 1;
		id = Counter::Total;
	}
};

Counter first = Counter();
Counter second = Counter();
print("{} {}", first.id, Counter::Total);
```

静态字段仍然受到访问说明符限制。需要从类型外部访问时，应将字段声明在 `public` 区域中。

## 静态方法

静态方法通过类型名称调用，不依赖某个对象：

```VeroScript
class Counter
{
public:
	static int Total = 0;

	static int Peek()
	{
		return Counter::Total;
	}
};

int current = Counter::Peek();
```

静态方法没有隐含的实例对象，因此不能直接访问非静态字段或调用依赖实例的成员方法。需要访问实例成员时，应把对象作为参数传入：

```VeroScript
class Formatter
{
public:
	string prefix;

	static string with_prefix(Formatter formatter, string value)
	{
		return formatter.prefix + value;
	}
};
```

## 泛型类型中的静态成员

泛型类型的每个具体类型实例拥有独立的静态存储。例如，`Box<int>::Count` 与 `Box<string>::Count` 互不共享：

```VeroScript
class Box<T>
{
public:
	static int Count = 0;
	T value;
};

Box<int>::Count = Box<int>::Count + 1;
Box<string>::Count = Box<string>::Count + 1;
```

使用静态成员时，建议始终通过完整的类型名称限定成员，以明确表达它属于类型而不是对象实例。

## 成员默认值与初始化

实例字段默认值在该实例的正常构造过程中求值，可以调用所属类型中稍后声明的静态方法，泛型类型也适用。它读取当时的静态值，而不是编译时冻结的快照；再创建实例不会重新初始化类型的静态字段。访问控制、const 与构造初始化规则仍然适用。

```VeroScript
class Sample<T>
{
public:
	static int Seed = 7;
	T snapshot = Sample<T>::Read();

	static T Read()
	{
		return cast<T>(Sample<T>::Seed);
	}
};

int main()
{
	Sample<int> first = Sample<int>();
	Sample<int>::Seed = 11;
	Sample<int> second = Sample<int>();
	print(first.snapshot);
	print(second.snapshot);
	print(first.snapshot);
	return 0;
}
```

输出依次为 `7`、`11`、`7`，各占一行。

泛型或嵌套类型内部的自身词法名称指向当前特化，保留外层类型及值参数；`Outer<int>::Inner` 这样的显式外层实参仍按显式指定解析。同一具体类型的别名共享静态存储。表达式中的同名局部值仍是值，前导 `::` 仍选择根作用域；参见[作用域](scope.md)。

类型静态字段在各执行状态的模块启动过程中初始化，区别于函数局部 static 的首次到达初始化。可确定的泛型静态依赖会在使用它的初始化表达式前准备；只保存 Lambda 不会调用其正文，可空字段或空容器也不会构造其中的元素。不要将此理解为任意惰性初始化或无关类型之间的全局总顺序；保持依赖简单，需要严格副作用顺序时在 `main` 中显式安排。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/static-members.md) · [fr-FR](../../fr-FR/language-basics/static-members.md) · [ja-JP](../../ja-JP/language-basics/static-members.md) · [ko-KR](../../ko-KR/language-basics/static-members.md) · [zh-CN](static-members.md) · [zh-TW](../../zh-TW/language-basics/static-members.md)
