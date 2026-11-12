# Arrays

VeroScript has two array forms: the dynamic array `array<T>` and the fixed-size native array `T[N]`. Both support indexing and range iteration, but they are different types with different runtime semantics.

## Dynamic arrays

`array<T>` is a dynamic container parameterized by an element type `T`:

```VeroScript
array<int> values = array<int>();
values.push(10);
values.push(20);

print(values[0]);
print(len(values));
```

Brace initialization is also supported:

```VeroScript
array<int> values = array<int>{1, 2, 3};
```

Common operations include:

| Operation | Description |
| --- | --- |
| `values[index]` | Read or modify an element at an index |
| `values.push(value)` | Append an element and return the array |
| `values.remove(value)` | Remove the first equal element and return whether it was removed |
| `values.remove_at(index)` | Remove the element at an index |
| `values.clear()` | Clear the array and return the array |
| `values.reserve(count)` | Reserve capacity and return the array |
| `values.capacity()` | Return the current capacity |
| `len(values)` | Return the number of elements |

Every element inserted into an `array<T>` must match `T`. Structs and native arrays are copied according to value semantics when inserted; classes and other handle types retain their handle semantics.

Dynamic arrays are stored through a shared container handle. Assigning one `array<T>` variable to another makes both variables refer to the same dynamic array. Construct an independent array and copy its elements when an independent copy is required.

## Fixed-size native arrays

A native array puts its length after the element type:

```VeroScript
int numbers[3] = {10, 20, 30};
```

The length is part of the type. `int[3]`, `int[4]`, and `array<int>` are different types. Native arrays can be struct fields and can be multidimensional:

```VeroScript
struct GridRow
{
	int cells[3] = {1, 2, 3};
};

int matrix[2][3] = {
	{1, 2, 3},
	{4, 5, 6}
};
```

A multidimensional native array consists of nested native arrays. `len(matrix)` returns the length of the first dimension, and `len(matrix[0])` returns the length of the second dimension.

A native-array type may have at most 64 dimensions. Each dimension may contain at most 65,536 elements, and the flattened product of all dimensions may contain at most 1,048,576 elements. Source that exceeds any limit is rejected during compilation.

## Indexing

Array indices start at `0`:

```VeroScript
array<string> names = array<string>{"Ada", "Linus"};
string first = names[0];
names[1] = "Grace";
```

The result of an indexing expression has the element type. The index must be an integer within the current array's valid range; an out-of-range access produces a runtime error.

## Range iteration

Use `for` with a colon to iterate over an array. A regular loop variable receives the element value:

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

Use a reference loop variable to modify the original elements:

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

For an array of structs, modifying a by-value loop variable does not write back to the array. Use `T&` to modify the original element. Both dynamic and native arrays support reference iteration.

## Choosing an array form

Use `T[N]` when the length is fixed and is part of the data layout. Use `array<T>` when the length changes at runtime or when operations such as `push` and `remove_at` are needed. Use `array<T>` when a mutable container should be shared between functions, and a native array when a fixed-layout value is required.

`len(values)` and `values.capacity()` return nonnegative `int64`. Linear indices, `remove_at(index)` and `reserve(count)` accept `int64`; indices must satisfy `0 <= index < len(values)` and reserve counts must be nonnegative and fit the host platform. Allocation limits still apply. The checked conversion rules are described in [Built-in functions](../standard-library/builtins.md).

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](arrays.md) · [fr-FR](../../fr-FR/language-basics/arrays.md) · [ja-JP](../../ja-JP/language-basics/arrays.md) · [ko-KR](../../ko-KR/language-basics/arrays.md) · [zh-CN](../../zh-CN/language-basics/arrays.md) · [zh-TW](../../zh-TW/language-basics/arrays.md)
