# Type conversions

Type conversion determines whether an expression can be used as a value of another type. Whether a conversion is valid depends on the source type, target type, and expression context.

## Numeric types

Compatible integer and floating-point types can participate in assignment, argument passing, and operations according to the language's numeric-conversion rules. Fixed-width integers are useful at explicit data-width boundaries; when converting to a narrower or unsigned type, verify that the value range will not be truncated or changed unexpectedly.

```VeroScript
int count = 10;
double total = count;
int rounded = to_int(total);
```

Use an explicit target-type construction or conversion form when the intent matters. Do not use string concatenation or implicit conversion as a substitute for numeric conversion.

## Conversion contexts

VeroScript distinguishes conversions that are allowed automatically from conversions that must be requested explicitly:

| Context | Purpose | Important rules |
| --- | --- | --- |
| Assignment, argument passing, and return values | Implicit conversion | Compatible numeric conversions and compatible base/interface conversions are considered. A non-`explicit` single-value constructor may also participate. |
| `cast<T>(value)` | Explicit conversion or construction | Supports explicit numeric, scoped-enum, reference, interface, constructor, and user-defined conversion-operator cases. A failed checked reference cast raises a runtime error. |
| `try_cast<T>(value)` | Fallible conversion | Returns `optional<T>`. A failed runtime reference conversion becomes an empty optional. It does not call an `explicit` constructor. |
| `const_cast<T>(value)` | Remove const access capability | The source must be const and have the same canonical type as `T`. This is not a value conversion and performs no runtime type check. |

The compiler does not chain user-defined conversions. It will not silently convert `A` to `B` and then `B` to `C` merely because both individual conversions exist.

## Text types

`string` and `wstring` are distinct types and cannot be directly mixed in assignment, comparison, or concatenation. Use the explicit standard-library conversion functions:

```VeroScript
string text = "hello";
wstring wide = to_wstring(text);
string restored = to_string(wide);
```

Use `to_string` when converting numeric or boolean values to text instead of relying on mixed-type implicit concatenation.

## Converting constructors and conversion operators

A constructor that takes one value parameter can be used as a conversion constructor when it is not marked `explicit`:

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

Mark the constructor `explicit` when it should be callable only by direct construction or an explicit `cast<T>`:

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

A user-defined conversion operator such as `operator bool()` can also participate where the language permits a user-defined conversion. Use such operators sparingly so that implicit conversion remains unsurprising.

`cast<T>` can explicitly request a matching user-defined conversion operator. `try_cast<T>` is restricted to its documented primitive, scoped-enum, and reference/interface cases; it does not invoke user-defined constructors or conversion operators.

## `cast<T>`

`cast<T>(value)` requests an explicit conversion. It supports compatible numeric conversions, conversions to and from scoped-enum underlying integers, checked class and interface reference conversions, and explicit construction of a target struct or class.

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

For class and interface references, `cast<T>` checks the runtime relationship when a downcast or cross-cast is required. A failed checked reference cast is a runtime error. A struct-to-interface conversion uses the interface representation of the struct value; class-to-interface conversion preserves shared class identity. A successful cast to a struct value follows value semantics.

`cast<T>` does not remove const access capability. If the source is const, the result remains const-accessible. Use `const_cast<T>` only for the deliberate const escape described below.

## Parenthesized cast syntax

`(T)expression` is an alternate spelling of `cast<T>(expression)`. It uses the
same explicit conversion rules, including numeric, scoped-enum,
reference/interface, explicit-construction, and matching user-defined
conversion-operator cases:

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

The parser recognizes this form at unary-expression position. A simple user
type name can be ambiguous with a value expression, so prefer `cast<T>` when
the intended target type is not obvious.

`(void)expression;` is a statement-only explicit result discard. It evaluates
the expression exactly once, preserves its side effects and runtime failures,
and discards its result. The same prefix is accepted in a `for` initializer.
It is not a value expression: `(void)expr` cannot be used as an argument,
assignment value, arithmetic operand, or nested expression. An ordinary
`expression;` has the same runtime behavior; the `(void)` prefix documents the
intent to discard the result.

## `try_cast<T>`

`try_cast<T>(value)` is the fallible counterpart for primitive, scoped-enum, and reference/interface conversions. Its result is `optional<T>`:

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

`try_cast` returns an engaged optional for a successful conversion. A failed checked reference conversion returns an empty optional. A null reference conversion is still a successful conversion to a nullable target and can therefore produce an engaged optional containing `nullptr`. Numeric conversions produce the converted value; for example, `try_cast<int>(2.9)` contains `2`.

`try_cast<T>` does not select an `explicit` constructor. Use `cast<T>` when explicit construction is the intended operation.

## `const_cast<T>`

`const_cast<T>(value)` is a narrowly scoped const escape. The source expression must be const, and its canonical type must exactly match `T` after aliases are resolved:

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

This example changes a field of the shared class object. `const_cast<int>(view)` produces a scalar value, not an assignable reference, so `const_cast<int>(view) = 42` is invalid. The cast does not make a scalar copy into an alias of its original storage.

The operation removes only the compile-time const access capability. It does not convert the value, change its runtime type, or perform a checked cast. It is appropriate only when the underlying storage is intentionally writable, such as a controlled `readonly<T>` wrapper. Applying it to genuinely immutable storage defeats the protection that `const` was intended to provide.

The following forms are rejected:

```VeroScript
const int number = 1;
// const_cast<double>(number); // The target type must be exactly int.
// const_cast<int>(mutable_value); // The source must be const.
```

## Structs, classes, interfaces, and containers

Struct conversions follow value semantics and create a copy of the struct value. A struct field that is itself a shared handle still keeps that field's handle semantics. Class conversions preserve shared object identity:

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

Converting a struct to an interface creates the value representation required by the interface; converting a class to an interface preserves the class object's shared identity. The concrete type must implement all pure virtual methods required by the interface.

Container element, key, and value types must satisfy the rules of their declarations. A generic function or type can be instantiated only when its arguments satisfy its declared constraints; a `where` constraint does not otherwise relax conversion rules.

## Conversions not to rely on

`nullptr` applies only to nullable handle types. Text types cannot be exchanged through ordinary assignment, and a struct value cannot be exchanged with a class handle merely because their fields look similar. Prefer explicit target types at host-binding and interface boundaries.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](type-conversions.md) · [fr-FR](../../fr-FR/language-reference/type-conversions.md) · [ja-JP](../../ja-JP/language-reference/type-conversions.md) · [ko-KR](../../ko-KR/language-reference/type-conversions.md) · [zh-CN](../../zh-CN/language-reference/type-conversions.md) · [zh-TW](../../zh-TW/language-reference/type-conversions.md)
