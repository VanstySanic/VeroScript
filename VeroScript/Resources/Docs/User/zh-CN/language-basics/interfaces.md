# 接口

`interface` 用于声明一组可由结构体或类实现的成员契约。接口不能直接实例化，但可以作为参数、返回值和变量类型，用于以统一方式访问不同的实现类型。

## 接口声明

接口方法通常声明为纯虚方法。纯虚方法必须使用 `virtual`，并以 `= 0;` 结束：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};
```

纯虚方法必须是公开成员。接口方法的参数、返回类型和 `const` 修饰共同组成实现契约。

## 实现接口

结构体或类在继承列表中写出接口名称，并提供与接口签名匹配的实现：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Counter : public IReadable
{
public:
	int value = 0;

	int read() const override
	{
		return value;
	}
};
```

实现方法可以使用 `override` 明确表示其覆盖接口方法。缺少纯虚方法实现的类或结构体不能作为具体对象实例化。

一个类型可以实现多个接口：

```VeroScript
interface IId
{
	virtual int id() const = 0;
};

interface IName
{
	virtual string name() const = 0;
};

class Person : public IId, public IName
{
public:
	int id() const override
	{
		return 42;
	}

	string name() const override
	{
		return "Ada";
	}
};
```

## 默认实现

接口方法也可以提供方法体，形成默认实现。实现类型可以直接继承该实现，也可以使用 `override` 提供自己的版本：

```VeroScript
interface IDescribable
{
	virtual string name() const = 0;

	string describe() const
	{
		return "name=" + name();
	}
};
```

默认方法仍然属于接口契约的一部分；只有没有实现的纯虚方法会阻止具体类型实例化。

## 接口继承

接口可以继承一个或多个父接口：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

interface IResource : IReadable
{
	virtual void close() = 0;
};
```

实现 `IResource` 的类型必须满足它自身以及父接口的全部纯虚方法。接口继承列表中的每个父类型都必须是接口。

## 接口值和转换

结构体转换为接口类型时，会形成接口所需的结构体值副本；通过接口修改该副本不会回写原结构体。类转换为接口时保留类对象的共享身份：

```VeroScript
interface ICounter
{
	virtual void add(int amount) = 0;
};

struct ValueCounter : ICounter
{
	int value = 0;
	void add(int amount)
	{
		value += amount;
	}
};

class SharedCounter : ICounter
{
public:
	int value = 0;
	void add(int amount) override
	{
		value += amount;
	}
};
```

因此，接口变量的复制和调用行为取决于其实际对象来源：结构体接口值遵循值语义，类接口值遵循类句柄语义。接口变量本身不能用来构造一个新的实现对象。

嵌套类型、局部声明、访问规则和分段泛型实参请参见[类型作用域](scope.md)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/interfaces.md) · [fr-FR](../../fr-FR/language-basics/interfaces.md) · [ja-JP](../../ja-JP/language-basics/interfaces.md) · [ko-KR](../../ko-KR/language-basics/interfaces.md) · [zh-CN](interfaces.md) · [zh-TW](../../zh-TW/language-basics/interfaces.md)
