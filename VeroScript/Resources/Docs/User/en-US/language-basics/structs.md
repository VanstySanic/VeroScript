# Structs

`struct` defines a user type with value semantics. A struct can contain fields, methods, constructors, and destructors. It can also inherit from one base type or implement interfaces. The struct object itself is copied by value; each field still follows the semantics of its own type.

## Declaration and members

A struct declaration consists of a name and a member list. Struct members are `public` by default:

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

Inside a member function, `this` refers to the current struct object. A member that can be accessed by its bare name can also be accessed as `this.member`.

Structs support `public`, `protected`, and `private` access control. Although members are public by default, use an explicit access specifier when access should be restricted:

```VeroScript
struct Account
{
public:
	int id = 0;

private:
	int balance = 0;
};
```

## Construction and initialization

Fields can provide default initialization expressions. A struct can also declare constructors; construction selects an overload from the argument list:

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

A constructor initializer list can initialize direct instance fields before the constructor body. Field entries in the list are initialized in field declaration order. A field with a declaration initializer cannot also appear in this list; see [Constants](constants.md) for `const` field rules.

The braced form is typed initialization and follows the same constructor matching rules. When no explicit constructor is declared, field defaults still participate in default construction.

## Value semantics

Assigning a struct copies its value. Modifying a direct field on the copy does not change the original struct:

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

Value semantics describe the struct object itself. If a field is an `array<T>`, `map<K, V>`, or another shared-handle type, that field retains its own shared-handle behavior when the struct is copied. Explicitly copy container contents when an independent container is required.

## Methods and references

A struct method can read or modify the current object. Use a reference parameter when a function must modify the caller's struct directly:

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

Ordinary parameters and by-value range iteration create struct value copies. Reference parameters and reference range iteration write through to the original object. See [References](references.md) for the detailed rules.

## Mutable implementation fields

A non-static struct field may be marked `mutable` when a `const` member function needs to update implementation state such as a cache:

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

Only the marked field receives this exception. `mutable` cannot be combined with `const`, `static`, or a reference field, and it is not a synchronization mechanism.

## Inheritance and interfaces

A struct can inherit from one struct base and implement one or more interfaces:

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

Converting a struct to an interface type creates the value representation required by the interface. Mutating that interface value does not write back to the original struct. Interface inheritance is covered in the [Interfaces](interfaces.md) page; generic constraints are covered in the [Generics](generics.md) page.

## Defaulted special members

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

A struct can request memberwise copy construction, assignment and equality. Copy construction does not rerun field initializers. Assignment updates existing subobjects and returns the original object by reference. A const or reference field can make default assignment unavailable; copying and equality remain separate capabilities.

Classes may request default equality while retaining shared handle copying. Deleted or inaccessible operations never fall back to raw copying. Direct construction can select an explicit copy constructor; implicit initialization cannot. Fresh constructor results do not need an extra copy. Script move operations are not introduced.

These declarations use Core execution, including VM fallback under Mixed/JIT and Core AOT. Standalone C++, C# and Python source export rejects unsupported default members. Example: `default_special_members.vs`.

Nested types, local declarations, access rules and segmented generic arguments are described in [Type scopes](scope.md).

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](structs.md) · [fr-FR](../../fr-FR/language-basics/structs.md) · [ja-JP](../../ja-JP/language-basics/structs.md) · [ko-KR](../../ko-KR/language-basics/structs.md) · [zh-CN](../../zh-CN/language-basics/structs.md) · [zh-TW](../../zh-TW/language-basics/structs.md)
