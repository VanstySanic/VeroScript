# Data Types

Types determine how a value is represented, which operations can be applied to it, and which conversions are allowed. VeroScript types are grouped into built-in, container, and user-defined types.

## Built-in types

| Category | Types |
| --- | --- |
| No value | `void` |
| Boolean | `bool` |
| Signed integers | `char`, `short`, `int`, `long` |
| Unsigned integers | `unsigned char`, `unsigned short`, `unsigned int`, `unsigned long` |
| Fixed-width integers | `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64` |
| Floating point | `float` (binary32), `double` (binary64) |
| Text | `string`, `wstring` |

`void` can only be used as a function return type; it cannot be used for variables. Fixed-width integers and `float`/`double` are distinct language types rather than documentation-only aliases. They are useful for interfaces and data structures that require an explicit width or floating-point precision.

The fixed-width integer ranges are `-128` to `127` for `int8`, `0` to `255` for `uint8`, `-32768` to `32767` for `int16`, and `0` to `65535` for `uint16`; `int32`, `uint32`, `int64`, and `uint64` use the corresponding signed and unsigned 32-bit and 64-bit ranges. A literal outside its target range is an error; do not rely on truncation.

```VeroScript
int8 signed_byte = -128;
uint8 byte = 255u;
float single = 2.05f;
double precise = 2.05;
```

A floating-point literal with an `f`/`F` suffix is stored as `float`; an unsuffixed floating-point literal is stored as `double`. Their rounding behavior can therefore differ.

## Container types

Container types use type arguments to describe their element or key/value types:

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
set<string> names = set<string>();
queue<int> pending = queue<int>();
```

VeroScript provides these container categories:

- Sequence containers: `array`, `deque`.
- Associative containers: `map`, `multimap`, `ordered_map`, `ordered_multimap`.
- Set containers: `set`, `multiset`, `ordered_multiset`.
- Adapters: `queue`, `priority_queue`, `stack`.
- Concurrent containers: `channel`.

Container operations, element semantics, and concurrency rules belong in the container and concurrency references.

## Native arrays

Fixed-size native arrays put their dimensions after the element type:

```VeroScript
int numbers[3] = {10, 20, 30};
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
```

`T[N]` includes its length in the type, while `array<T>` is a dynamic array container. They can both be indexed, but they are different types.

## User-defined types

Users can define new types with `struct`, `class`, `interface`, and `enum`:

```VeroScript
struct Point
{
	int x;
	int y;
};

enum class Color
{
	Red,
	Green,
	Blue
};
```

`struct` has value semantics, while `class` has shared identity. See [Structs](structs.md) and [Classes](classes.md) for fields, methods, and object semantics; see [Interfaces](interfaces.md) and [Enums](enums.md) for those type forms, while generics are covered separately.

## `auto`

`auto` infers a variable's type from its initializer:

```VeroScript
auto count = 3;
auto values = array<int>{1, 2, 3};
```

An `auto` declaration must have an initializer. Inference occurs at the declaration; the variable still has a definite static type afterward.

## Read-only wrappers

`readonly<T>` is a standard value wrapper. It stores a `T` by value, can itself be copied or placed in a container, and exposes the stored value through `const ref T get() const`:

```VeroScript
readonly<int> count = readonly<int>(41);
const ref int view = count.get();
print(view);
```

The wrapper has no implicit default value; construct it with a `T`. For a struct type, construction stores a struct value. For a class type, the stored value is the class handle, so the class object's shared identity is preserved. See [Type conversions](../language-reference/type-conversions.md) for the deliberate `const_cast<T>` escape.

The built-in type names `optional`, `readonly`, `weak`, `shared`, `type`, `field_info`, `method_info`, `module_info`, `function_info`, `global_info`, and `enum_member_info` are reserved and cannot be redeclared by a script.

## Compiler-provided utility types

VeroScript also provides `optional<T>`, `weak<T>`, `pair<T, U>`, and `shared<T>` / `handle<T>`. Their constructors, state rules, ownership behavior, and map-entry semantics are documented in [Value utility types](value-utility-types.md).

`pair<T, U>` and `handle<T>` are soft generic built-ins: a user declaration with the same name and generic arity takes precedence over the compiler-provided form. This differs from the hard-reserved names listed above.

## Type conversions

Compatible numeric types can be converted according to the language rules. `string` and `wstring` cannot be mixed implicitly; use the appropriate explicit conversion function when moving between text types.

Whether a conversion is allowed depends on the source type, target type, and expression context. Host bindings and generic interfaces should use explicit types instead of relying on implicit conversions where possible.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](data-types.md) · [fr-FR](../../fr-FR/language-basics/data-types.md) · [ja-JP](../../ja-JP/language-basics/data-types.md) · [ko-KR](../../ko-KR/language-basics/data-types.md) · [zh-CN](../../zh-CN/language-basics/data-types.md) · [zh-TW](../../zh-TW/language-basics/data-types.md)
