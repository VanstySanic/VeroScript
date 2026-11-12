# Classes

`class` defines a user type with shared identity. Class values are handles, can be null, and support fields, methods, constructors, destructors, inheritance, and virtual methods. Unlike a struct, assigning a class variable normally copies the object handle rather than the object itself.

## Declaration and access control

A class can contain fields and member functions. Class members are `private` by default, so use an explicit `public` section for the interface exposed to callers:

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

Members can also be `protected` or `private`. An access specifier controls which members are visible to the class, derived classes, and outside code. `friend` can grant additional access to a specified function or class.

## Construction and null objects

A class can declare one or more constructors. A construction expression creates a new class object:

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

A constructor initializer list can call direct base constructors and initialize direct instance fields before the constructor body. Field entries in the list are initialized in field declaration order. A field with a declaration initializer cannot also appear in this list; see [Constants](constants.md) for `const` field rules.

During construction or destruction, virtual calls on the object use the implementation belonging to the type whose constructor or destructor is currently executing, including calls through helpers or bound methods. A base constructor does not call an override on the still uninitialized derived part. Calling a pure virtual method without an implementation at that stage produces a runtime error. Once construction completes, ordinary dynamic dispatch uses the actual derived type.

If construction fails, the failed type's destructor does not run. Initialized members and completed bases are released in reverse construction order.

A class handle can contain `nullptr`:

```VeroScript
Counter empty = nullptr;
if (empty == nullptr)
{
	print("empty");
}
```

Calling an instance member through a null handle produces a runtime error. Ensure that a handle refers to a valid object before using it.

## Shared identity

Assigning one class variable to another makes both variables refer to the same object:

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

Construct another object when independent identity is required; do not copy the handle. Class fields, class values in containers, and class parameters follow the same shared-identity semantics.

## Const objects and query methods

A member function can be qualified with `const`, which means it cannot modify members through the current object. A const object can only call methods that are valid for const objects:

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

Prefer `const` member functions for operations that only inspect object state. A non-`const` member function cannot be called through a `const` object.

### Mutable implementation fields

`mutable` is a contextual field specifier for a non-static instance field of a `struct` or `class`. It permits that one field to be updated from a `const` member function:

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

`mutable` does not make the whole object writable, does not provide thread safety, and cannot be combined with `const`, `static`, or a reference field. It is contextual rather than globally reserved: outside a field declaration, `mutable` may still be used as an ordinary identifier where the grammar permits it.

## Inheritance and virtual methods

A class can inherit from one or more base classes and implement interfaces. Use `virtual` for dynamically dispatched methods and `override` to make a base-method override explicit:

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

An abstract class can declare a pure virtual method such as `virtual int value() const = 0;`. A class with an unimplemented pure virtual slot cannot be instantiated. A virtual destructor ensures the correct destruction path when a derived object is destroyed through a base-class identity.

The complete rules for interfaces, inheritance access, and multiple inheritance are covered in the [Interfaces](interfaces.md) documentation.

## Destructors

A class can declare a destructor as `~TypeName()`, which runs when the object lifetime ends. A destructor can also be virtual or pure virtual. When inheritance is involved, choose the virtual-destructor design according to how the object will be destroyed.


## Definitions outside the class

Declare members in the class and define them later in the same source module using `Type::member`. The definition must match the declared parameter and return types, reference qualifiers and `const`. Constructors have no return type. These are source definitions; do not split declarations and implementations into independently linked script modules.

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

Nested types, local declarations, access rules and segmented generic arguments are described in [Type scopes](scope.md).

## Self-referencing fields

A generic class can store a handle to its own specialization. The field can start as `nullptr`; assigning another object preserves its identity.

```VeroScript
class Node<T>
{
public:
	T value;
	Node<T> next = nullptr;
};
```

Direct or mutual recursive struct fields stored by value remain invalid. Cyclic inheritance and unbounded template expansion are also rejected; compiler depth limits still apply.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](classes.md) · [fr-FR](../../fr-FR/language-basics/classes.md) · [ja-JP](../../ja-JP/language-basics/classes.md) · [ko-KR](../../ko-KR/language-basics/classes.md) · [zh-CN](../../zh-CN/language-basics/classes.md) · [zh-TW](../../zh-TW/language-basics/classes.md)
