# 05: Structs, classes, and interfaces

This tutorial uses a small readable-object example to compare three user-defined types: structs for values, classes for shared identity, and interfaces for replaceable behavior.

## Step 1: represent a value with a struct

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

A struct usually represents a group of values. Assigning a struct to another variable is normally understood as value copying, which works well for coordinates, settings, and small records.

## Step 2: represent shared identity with a class

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

This prints `15` because `first` and `second` refer to the same class object. A class assignment copies the object identity, not an independent object. Construct another object when you need separate state.

Class members are private by default, so the example uses `public:` for the constructor and members. In a larger program, keep fields private when callers should use controlled operations instead.

### Converting constructors and `explicit`

A non-`explicit` constructor with one value parameter can participate in an implicit conversion. Mark it `explicit` when construction should only happen through direct construction or `cast<T>`:

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

`UserId invalid = 9;` is rejected. This keeps overload resolution predictable and prevents accidental wrapper construction.

`(T)expression` is spelling sugar for `cast<T>(expression)` and follows exactly the same conversion rules:

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);
```

Prefer `cast<T>` when a simple user type name could be confused with a shadowing value expression. `(void)expression;` is a statement-only explicit result discard. It evaluates the expression once, preserves side effects and runtime failures, and discards the result; an ordinary `expression;` has the same runtime behavior. The form is also valid in a `for` initializer, but `(void)expression` cannot be used as a nested value or function argument.

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

### `mutable` cache fields

Use contextual `mutable` only on a non-static `struct` or `class` field when a `const` method must update implementation state such as a cache. It does not make the object generally writable, does not make code thread-safe, and cannot be combined with `const`, `static`, or a reference field.

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

### Portable read-only payloads

`readonly<T>` stores a `T` as an ordinary value that can itself be copied or
placed in containers, while `get()` exposes only `const T&`. Constructing a
`readonly<Struct>` captures that struct value; constructing a
`readonly<Class>` retains the class handle.

Use `const_cast<T>(value)` only when the source expression is const and its
canonical runtime type is the same as `T`. It removes the compile-time const access
capability without performing a conversion or a runtime type check. Ordinary
`cast<T>` deliberately preserves const access.

Keep the `Label` declaration above and use this `main` for the next step. It creates the `label` value before constructing the read-only wrapper; the first output is `source`.

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

### Choosing between `cast` and `try_cast`

Use `cast<T>` when a conversion is required and a failed checked reference conversion should be an error. Use `try_cast<T>` when the relationship may not hold and the caller should inspect an `optional<T>` instead:

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

`try_cast<T>` handles primitive, scoped-enum, and reference/interface conversions; it does not invoke an `explicit` constructor. See [Type conversions](../../../../../VeroScript/Resources/Docs/User/en-US/language-reference/type-conversions.md) for the complete conversion rules.

`cast<T>` may also request a matching user-defined conversion operator. `try_cast<T>` does not invoke user-defined constructors or conversion operators.

## Step 3: describe behavior with an interface

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

An interface does not create objects; it states what an implementation must provide. `read_twice` depends on the `IReadable` contract rather than on one concrete sensor type.

## Step 4: define a static utility type

Use `static class` or `static struct` when a type is only a named container for operations or constants. Every declared member is treated as static, so callers use the type name and `::` without creating an object:

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

Static utility types cannot be constructed, inherited, or declared as generic types. They cannot contain constructors, destructors, `using` members, a default-initialization block, mutable fields, or instance modifiers such as `virtual` and `const`. Use an ordinary `class` or `struct` when the type needs per-object state or inheritance.

## A practical choice

- Use a `struct` when independent value copies are useful.
- Use a `class` when several parts of a program should share mutable identity.
- Use an `interface` when callers should depend on behavior rather than a concrete implementation.
- Use a static utility type when no object identity or instance state is needed.

## Exercises

1. Add a `move` method to `Point`.
2. Add `reset` to `Counter`, then keep `value` private.
3. Create a second `IReadable` implementation named `FixedSensor`.

See [Structs](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/structs.md), [Classes](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/classes.md), and [Interfaces](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/interfaces.md). A combined example is [`examples/object_basics.vs`](../../../../../VeroScript/Resources/Scripts/examples/object_basics.vs).

Next: [References, reference returns, and lambda captures](06-references-and-captures.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](05-structs-classes-and-interfaces.md) · [fr-FR](../../fr-FR/language/05-structs-classes-and-interfaces.md) · [ja-JP](../../ja-JP/language/05-structs-classes-and-interfaces.md) · [ko-KR](../../ko-KR/language/05-structs-classes-and-interfaces.md) · [zh-CN](../../zh-CN/language/05-structs-classes-and-interfaces.md) · [zh-TW](../../zh-TW/language/05-structs-classes-and-interfaces.md)
