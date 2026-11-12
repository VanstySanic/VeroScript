# References and reference semantics

A reference aliases an existing storage location or handle slot. It does not create an independent object and does not own the object it aliases. VeroScript references are used for parameters, local aliases, range iteration, element access, reference returns, and lambda reference captures. They are not general-purpose script pointers and do not support pointer arithmetic.

## Reference parameters

The recommended spelling uses a trailing `&`:

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
	print(value);
	return 0;
}
```

`ref T` and `const ref T` are equivalent keyword-style spellings:

```VeroScript
void reset(ref int value)
{
	value = 0;
}

int main()
{
	int value = 1;
	reset(ref value);
	return 0;
}
```

The declaration and call markers should match: `T&` uses `&expression`, while `ref T` uses `ref expression`. A reference argument must identify storage, such as a variable, array element, map element, or object field.

## Read-only references

Use `const T&` or `const ref T` for a read-only reference. It can read the target but cannot modify it through the alias:

```VeroScript
int read_value(const int& value)
{
	return value;
}

int main()
{
	const int value = 42;
	print(read_value(&value));
	return 0;
}
```

A mutable reference cannot bind to a const object or a const reference return. Prefer a read-only reference when a function only needs to inspect an object.

## Local reference aliases

A local variable can be declared as an alias to an existing object:

```VeroScript
int main()
{
	int value = 1;
	int& alias = value;
	const int& view = value;

	alias = 2;
	print(view);
	return 0;
}
```

Both `alias` and `view` refer to `value`'s storage. A reference variable does not become an independent object and does not copy its target.

## References in range iteration

A normal range variable receives an element value. For value types such as structs, modifying the loop variable does not write back to the original container:

```VeroScript
for (Point point : points)
{
	point.x = 0;
}
```

Use `T&` to make the loop variable an alias to the original element:

```VeroScript
for (Point& point : points)
{
	point.x = 0;
}
```

Arrays, fixed-size native arrays, deques, and map values support reference range iteration. Use `const T&` for a read-only collection.

When map iteration needs both key and value, use `pair<K, V>&`; when only the value is needed, iterate over `V&`:

```VeroScript
for (pair<string, int>& entry : counts)
{
	entry.second += 1;
}
```

## Element references

An array or map element can be passed directly as a reference argument:

```VeroScript
void bump(int& value)
{
	value += 1;
}

bump(&values[0]);
bump(&counts["total"]);
```

An element reference does not copy the element, so changes made through it remain in the original container after the call. Nested fields and element paths can also produce references as long as every object in the path remains valid.

## Reference returns

A function can return a mutable or read-only reference. A reference-returning expression remains an assignable lvalue and can be incremented or bound to another reference:

```VeroScript
int Counter = 1;
array<int> Values = array<int>();

int& counter_ref()
{
	return Counter;
}

int& first_value()
{
	return Values[0];
}

const int& read_counter()
{
	return Counter;
}

int main()
{
	Values.push(40);
	counter_ref() += 4;
	first_value() += 2;
	print(read_counter());
	return 0;
}
```

A reference return must point to storage that remains valid. It may return a global, a container element, an object field, or an existing reference; it cannot return an ordinary function-local variable whose stack storage ends with the function. A mutable reference cannot bind to a const reference return.

A class-handle variable can also be rebound through a reference return: the reference points to the handle slot rather than copying a new object. This is different from the shared-identity behavior of the class itself and should be documented explicitly in an API.

## References and function types

Reference direction is part of a function type. Function values and lambdas can use `int&` or `const int&`, and the call still needs an `&` argument:

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	function(int&) -> void mutate = add_one;
	int value = 3;
	mutate(&value);
	print(value);
	return 0;
}
```

## Restrictions and lifetime

- Mutable reference parameters cannot have defaults. Read-only reference parameters may use supported literal defaults, which bind caller-owned temporaries. Reference parameters cannot be variadic.
- References do not own objects, support pointer arithmetic, or automatically extend the lifetime of ordinary objects.
- `ref`, `const ref`, `T&`, and `const T&` affect function types, overloads, and binding checks.
- An ordinary reference must be used while its target storage remains valid.
- Lambda reference captures use managed shared reference cells, so a closure can safely outlive the function that created it; see [Lambdas and function values](lambdas.md).

Runnable reference examples are listed in the [Examples index](../examples.md).

A container-element reference borrows the original container and entry. Rebinding the container variable does not retarget it. In-place assignment, tail append and unrelated map insertion preserve the target; an expired entry or a position occupied by another element produces an invalid-reference diagnostic. Managed reference returns and lambda captures retain their existing lifetime rules.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](references.md) · [fr-FR](../../fr-FR/language-basics/references.md) · [ja-JP](../../ja-JP/language-basics/references.md) · [ko-KR](../../ko-KR/language-basics/references.md) · [zh-CN](../../zh-CN/language-basics/references.md) · [zh-TW](../../zh-TW/language-basics/references.md)
