# Static members

A member declared with `static` belongs to the type itself rather than to one object instance. A static field has one type-level storage location, and a static method does not need an object receiver.

## Static utility types

`static class` and `static struct` declare non-instantiable utility containers. Every field and method declared inside is treated as static and is accessed through the type name and `::`:

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

A static class or static struct cannot be constructed or used as a base type. It cannot be generic, declare constructors or destructors, declare `using` members, or contain a default-initialization block. Its fields cannot be `mutable`, and its methods cannot use instance modifiers such as `virtual`, `override`, `final`, pure virtual, or `const`. Access sections still apply: a static class follows class default access, while a static struct follows struct default access. Use an ordinary `class` or `struct` when the type needs instances, inheritance, or instance state.

## Static fields

Access a static field through the type name and `::`. The `Total` field below is shared by every `Counter` instance:

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

Static fields are still subject to access control. Declare a field in a `public` section when code outside the type must access it.

## Static methods

Call a static method through the type name without creating an object for the call:

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

A static method has no implicit instance object, so it cannot directly access non-static fields or call instance-dependent members. Pass an object explicitly when an instance member is needed:

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

## Static members in generic types

Each concrete type instance of a generic type has independent static storage. For example, `Box<int>::Count` and `Box<string>::Count` are separate values:

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

When using a static member, prefer qualifying it with the complete type name. This makes it clear that the member belongs to the type rather than to an object instance.

## Member defaults and initialization

An instance field default is evaluated during that instance’s normal construction. It may call a static method declared later in its owner, including a generic owner, and reads the current static value rather than a snapshot captured at compilation. Constructing another instance does not reset the type’s static fields. Access control, constness and constructor initialization rules still apply.

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

The output is `7`, `11`, `7`, on separate lines.

Within a generic or nested type, its lexical self name refers to the current specialization and retains enclosing type/value arguments. Explicit enclosing arguments, such as `Outer<int>::Inner`, remain explicit. An alias of the same concrete type shares its static storage. A local value with the same name remains a value in expressions, and a leading `::` still selects root scope; see [Scope](scope.md).

Type static fields initialize as part of module startup in each execution state; this differs from a function-local static’s first-reach initialization. Identifiable generic static dependencies are prepared before their consuming initializer. Merely storing a lambda does not call its body, and a nullable field or empty container does not construct its element values. Do not treat this as arbitrary lazy initialization or a total ordering of unrelated types: keep dependencies simple and perform order-sensitive side effects explicitly in `main`.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](static-members.md) · [fr-FR](../../fr-FR/language-basics/static-members.md) · [ja-JP](../../ja-JP/language-basics/static-members.md) · [ko-KR](../../ko-KR/language-basics/static-members.md) · [zh-CN](../../zh-CN/language-basics/static-members.md) · [zh-TW](../../zh-TW/language-basics/static-members.md)
