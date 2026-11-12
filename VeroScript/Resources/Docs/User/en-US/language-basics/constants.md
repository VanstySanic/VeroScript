# Constants

A constant is a variable or member that cannot be modified through its declared name. VeroScript uses `const` for constants.

`const` provides read-only access; it does not require compile-time initialization. Use [constexpr](../language-reference/constexpr.md) when a supported scalar value must be known during compilation.

## Constant variables

`const` can be used for local and module-level variables. A constant must be initialized at its declaration:

```VeroScript
const int max_count = 100;

int main()
{
	const string product_name = "VeroScript";
	print(product_name);
}
```

After initialization, a constant cannot be assigned through that name:

```VeroScript
const int limit = 10;
// limit = 20; // Compile error.
```

The constant still has the type written in its declaration. `const` does not convert the value or change its scope.

## Constant members

A struct or class can declare a constant field:

```VeroScript
class Settings
{
public:
	const int version = 1;
};
```

A non-static field without a declaration initializer can instead be initialized in the constructor initializer list:

```VeroScript
class Settings
{
public:
	const int version;

	Settings(int initial_version)
		: version(initial_version)
	{
	}
};
```

The list can name only direct instance fields. Direct base entries in the constructor initializer list run first; field entries in that list then run in field declaration order, before the constructor body. A field with a declaration initializer cannot also appear in the constructor initializer list, and a field cannot appear there twice. Assignment in the constructor body remains ordinary assignment and therefore cannot modify a `const` field. Declaration initializers and fields omitted from the list continue to follow the existing default-construction rules.

## `const` member functions

A member function can use `const` after its parameter list to indicate that it cannot modify the current object's mutable state:

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
```

Const member functions are appropriate for queries. Whether a member function can be called depends on the object's constness and the function's `const` qualifier.

### The `mutable` exception

A non-static instance field may be marked `mutable` when it represents implementation state that a `const` query is allowed to refresh, most commonly a cache:

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

This exception applies only to the marked field. It does not make a const object generally writable or make concurrent access safe. A mutable field cannot also be `const`, `static`, or a reference field.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](constants.md) · [fr-FR](../../fr-FR/language-basics/constants.md) · [ja-JP](../../ja-JP/language-basics/constants.md) · [ko-KR](../../ko-KR/language-basics/constants.md) · [zh-CN](../../zh-CN/language-basics/constants.md) · [zh-TW](../../zh-TW/language-basics/constants.md)
