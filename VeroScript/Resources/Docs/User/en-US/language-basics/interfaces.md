# Interfaces

`interface` declares a set of member contracts that structs or classes can implement. An interface cannot be instantiated directly, but it can be used as a parameter, return type, or variable type to access different implementations uniformly.

## Interface declarations

Interface methods are commonly declared as pure virtual methods. A pure virtual method must use `virtual` and end with `= 0;`:

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};
```

A pure virtual method must be public. Its parameter list, return type, and `const` qualification together form the implementation contract.

## Implementing an interface

A struct or class names the interface in its inheritance list and provides a matching method implementation:

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

Use `override` to make an implementation of an interface method explicit. A class or struct that still has an unimplemented pure virtual method cannot be instantiated as a concrete object.

A type can implement multiple interfaces:

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

## Default implementations

An interface method can also provide a body, forming a default implementation. An implementing type can inherit that implementation or provide its own version with `override`:

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

A default method remains part of the interface contract; only an unimplemented pure virtual method prevents concrete instantiation.

## Interface inheritance

An interface can inherit from one or more parent interfaces:

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

A type implementing `IResource` must satisfy all pure virtual methods declared by it and its parent interfaces. Every type in an interface inheritance list must itself be an interface.

## Interface values and conversions

Converting a struct to an interface type creates the struct value representation required by the interface; accessing that interface value does not modify the original struct. Converting a class to an interface preserves the shared identity of the class object:

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

Copying and calling an interface value therefore depends on the underlying implementation: a struct interface value follows value semantics, while a class interface value follows class-handle semantics. An interface variable cannot be used to construct a new implementation object.

Nested types, local declarations, access rules and segmented generic arguments are described in [Type scopes](scope.md).

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](interfaces.md) · [fr-FR](../../fr-FR/language-basics/interfaces.md) · [ja-JP](../../ja-JP/language-basics/interfaces.md) · [ko-KR](../../ko-KR/language-basics/interfaces.md) · [zh-CN](../../zh-CN/language-basics/interfaces.md) · [zh-TW](../../zh-TW/language-basics/interfaces.md)
