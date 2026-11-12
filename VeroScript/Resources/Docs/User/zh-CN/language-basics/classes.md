# 类

`class` 用于定义具有共享身份的用户类型。类对象通过句柄访问，可以为空，并支持字段、方法、构造函数、析构函数、继承和虚方法。与结构体不同，复制类变量通常只是复制对象句柄，不会复制对象本身。

## 声明和访问控制

类支持字段和成员函数。类成员默认是 `private`，需要对外提供接口时应显式使用 `public`：

```VeroScript
class Counter
{
	int value = 0;

public:
	void add(int amount)
	{
		value += amount;
	}

	int get() const
	{
		return value;
	}
};
```

成员访问还支持 `protected` 和 `private`。访问说明符会影响当前类、派生类以及类外代码能够访问的成员。`friend` 可以为指定的函数或类授予额外访问权限。

## 构造和空对象

类可以声明一个或多个构造函数。构造表达式会创建新的类对象：

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}
};

Counter counter = Counter(10);
```

构造初始化列表可以在构造函数体之前调用直接基类构造函数，并初始化当前类型直接声明的实例字段。列表中的字段项按照字段声明顺序初始化。已有声明初始化器的字段不能再次出现在列表中；`const` 字段的完整规则参见[常量](constants.md)。

构造或析构期间，对当前对象的虚调用使用正在执行的构造函数或析构函数所属类型的实现，通过辅助函数或绑定方法发起的调用也遵循此规则。基类构造函数不会调用尚未初始化的派生部分中的覆盖方法。该阶段调用没有实现的纯虚方法会产生运行时错误。构造完成后，普通动态分派使用实际派生类型。

构造失败时，不执行构造失败类型的析构函数；已初始化成员和已完成构造的基类按照构造的逆序释放。

类句柄可以保存 `nullptr`：

```VeroScript
Counter empty = nullptr;
if (empty == nullptr)
{
	print("empty");
}
```

对空句柄调用实例成员会产生运行时错误。使用对象前，应先确保句柄指向有效对象。

## 共享身份

将一个类变量赋给另一个变量时，两个变量指向同一个对象：

```VeroScript
class Box
{
public:
	int value = 0;

	void set(int next)
	{
		value = next;
	}
};

Box first = Box();
Box second = first;
second.set(10);

print(first.value);  // 10
```

如果需要独立对象，应再次调用构造函数，而不是直接复制句柄。类字段、容器中的类值和函数参数中的类值都遵循这种共享身份语义。

## 常量对象和查询方法

成员函数可以使用 `const` 修饰，表示函数不能通过当前对象修改成员。常量对象只能调用适用于常量对象的成员函数：

```VeroScript
class Counter
{
public:
	int value = 0;

	int get() const
	{
		return value;
	}
};

const Counter counter = Counter();
print(counter.get());
```

如果一个函数只读取对象状态，应优先声明为 `const` 成员函数。可变成员函数不能通过 `const` 对象调用。

### `mutable` 实现字段

`mutable` 是 `struct` 或 `class` 非静态实例字段的上下文相关说明符。它允许 `const` 成员函数只更新这个字段：

```VeroScript
class Label
{
	string source;
	mutable string cached;

public:
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

`mutable` 不会让整个对象可写，也不提供线程安全，并且不能与 `const`、`static` 或引用字段组合使用。它是上下文相关说明符，而不是全局保留字；在字段声明之外，只要语法允许，仍可作为普通标识符使用。

## 继承和虚方法

类可以继承一个或多个基类，并实现接口。使用 `virtual` 声明可动态分派的方法，使用 `override` 明确覆盖基类方法：

```VeroScript
class Base
{
public:
	virtual int value() const
	{
		return 1;
	}
};

class Derived : public Base
{
public:
	int value() const override
	{
		return 2;
	}
};

int main()
{
	Base object = Derived();
	print(object.value());
}
```

抽象类可以声明纯虚方法，例如 `virtual int value() const = 0;`。包含仍未实现的纯虚槽位的类不能被实例化。虚析构函数用于保证通过基类身份销毁派生对象时执行正确的析构过程。

接口、继承访问方式和多重继承的完整规则见[接口](interfaces.md)专题。

## 析构函数

类可以声明析构函数 `~TypeName()`，在对象生命周期结束时执行清理逻辑。析构函数也可以声明为 `virtual` 或纯虚析构函数；涉及继承时，应根据对象的销毁方式选择合适的虚析构设计。


## 类外定义

可以在类内声明成员，再在同一源码模块中使用 `Type::member` 定义。参数类型、返回类型、引用限定和 `const` 必须与声明匹配；构造函数不写返回类型。这是源码中的定义形式，不要把声明和实现拆成独立链接的脚本模块。

```VeroScript
class Counter
{
public:
	Counter(int seed);
	int Add(int delta) const;
	int value;
};

Counter::Counter(int seed)
{
	this.value = seed;
}

int Counter::Add(int delta) const
{
	return this.value + delta;
}

void main()
{
	Counter counter = Counter(6);
	print(counter.Add(1));
}
```

嵌套类型、局部声明、访问规则和分段泛型实参请参见[类型作用域](scope.md)。

## 自引用字段

泛型类可以在字段中保存自身实例类型的句柄。字段可以初始化为 `nullptr`；赋入其他对象时保留其对象身份。

```VeroScript
class Node<T>
{
public:
	T value;
	Node<T> next = nullptr;
};
```

结构体字段直接或相互按值递归仍不合法。循环继承和无限展开的模板也会被拒绝，编译深度限制继续生效。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/classes.md) · [fr-FR](../../fr-FR/language-basics/classes.md) · [ja-JP](../../ja-JP/language-basics/classes.md) · [ko-KR](../../ko-KR/language-basics/classes.md) · [zh-CN](classes.md) · [zh-TW](../../zh-TW/language-basics/classes.md)
