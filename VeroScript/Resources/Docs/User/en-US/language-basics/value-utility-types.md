# Value utility types

VeroScript provides a small group of compiler-supplied generic types for values that may be absent, non-owning observations of class objects, shared struct payloads, and map entries. They are available without an import. The `std::` spellings described in [The `std` namespace](standard-namespace.md) are compatibility aliases for the same types.

## `optional<T>`

`optional<T>` is a value type that stores either no value or one value of type `T`. Its state is explicit; a default-constructed optional is empty.

### Construction and methods

| Form | Meaning |
| --- | --- |
| `optional<T>()` | Construct an empty optional. |
| `optional<T>(value)` | Construct an engaged optional containing `value`. |
| `has_value() const` | Return whether a value is present. |
| `get() const` | Return the stored value. Calling it while empty fails through `assert`. |
| `value_or(fallback) const` | Return the stored value or `fallback` when empty. |
| `reset()` | Make the optional empty. |

```VeroScript
optional<int> score = optional<int>();
print(score.has_value());
print(score.value_or(0));

score = optional<int>(95);
if (score.has_value())
{
	print(score.get());
}

score.reset();
```

An optional copies the value semantics of `T`. For a struct payload, copying an engaged optional copies the struct. For a class payload, it copies the class handle and therefore preserves shared identity. `reset()` also releases the stored class handle, if any.

`try_cast<T>` returns `optional<T>` for a fallible conversion. Check `has_value()` before calling `get()`; see [Type conversions](../language-reference/type-conversions.md).

## `weak<T>`

`weak<T>` is a non-owning observation of a class object. `T` must be a script class, interface, or supported host class type; primitive types and structs are not valid targets. A weak value does not keep its target alive.

### Weak-reference methods

| Form | Meaning |
| --- | --- |
| `weak<T>()` | Construct an empty weak reference. |
| `weak<T>(target)` | Observe the class handle in `target`. |
| `lock() const` | Return a strong `T` handle when the target is alive, or a null handle otherwise. |
| `expired() const` | Return whether the target can no longer be locked. |
| `reset()` | Clear this weak reference. |

```VeroScript
class Session
{
public:
	int id = 0;
};

Session owner = Session();
weak<Session> observer = weak<Session>(owner);

Session locked = observer.lock();
if (locked != nullptr)
{
	print(locked.id);
}
```

Copying a weak value creates another weak observation of the same target. Resetting one copy does not reset the others. Once every strong class handle is gone, `expired()` becomes true and `lock()` returns a null handle. Always check the result before accessing members.

## `shared<T>` / `handle<T>`

`handle<T>` is a compiler-supplied generic class with one type parameter and a public field named `value`. It is useful when a struct value must be placed behind shared class identity. The default constructor creates the handle and default-constructs `value` for a value type; for a class `T`, the payload follows class-handle semantics and defaults to a null handle. `handle<T>(value)` copies the payload into a new shared object.

`shared<T>` is the recommended spelling of `handle<T>`; both names denote exactly the same type, including inside containers, generic functions and `weak<T>`. `std::shared<T>` and `std::handle<T>` use that same identity. Explicit `shared<T>()` constructs a non-null wrapper; assigning `nullptr` clears a handle. For a struct payload, construction copies the value into the wrapper, while copying a shared handle preserves identity. `weak<shared<T>>` observes the wrapper; `lock()` returns a strong handle to that same object or `nullptr` after expiration. Check the lock result before accessing `.value`. The spelling `shared` is a reserved built-in type alias, not a syntax keyword. Like the existing soft generic rules, a user-declared `handle<T>` with matching arity supplies the type behind both spellings.

With one payload argument, `auto h = shared(value)` infers `T`; a zero-argument call requires an explicit target type or type arguments.

Source emission has backend limits: C++ emission supports shared wrappers and weak references. C# emission supports shared wrappers, but struct or open generic payload construction requires an explicit value, such as `shared<Point>(Point())`, and `weak<T>` is currently unsupported. Standalone Python source emission does not support shared wrappers or weak references; the Python runtime binding supports both through the core. The complete lifetime examples run on the VM, Mixed, and JIT backends.

### Construction and sharing

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void bump_x(handle<Point> point)
{
	point.value.x += 1;
}

shared<Point> first = shared<Point>(Point());
handle<Point> second = first;
first.value.x = 10;
bump_x(second);
print(first.value.x);
```

Assigning `first` to `second` copies the class handle, so both variables observe and mutate the same `value`. Assigning `nullptr` clears the handle. For a struct `T`, constructing `handle<T>(source)` creates a new payload copy; later changes to `source` do not change that copied payload.

`handle<T>` is a shared wrapper, not a general substitute for a class. Converting `h.value` from a struct to an interface boxes a value copy. If an interface must expose shared mutable identity, use a class that implements the interface.

The complete runnable example is [examples/handle_struct.vs](../../../../Scripts/examples/handle_struct.vs).

## `pair<T, U>`

`pair<T, U>` is a compiler-supplied generic struct with two public fields:

| Field | Type |
| --- | --- |
| `first` | `T` |
| `second` | `U` |

Map APIs use `pair` to represent a key/value entry. `m.entries()` returns an array of value `pair<K, V>` snapshots:

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (pair<string, int> entry : scores.entries())
{
	print("{} -> {}", entry.first, entry.second);
}
```

Iterating the map itself can expose a writable entry view. `pair<K, V>&` writes through to the map, while `pair<K, V>` is a copy:

```VeroScript
for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}

for (auto& [key, value] : scores)
{
	value += 1;
}
```

Do not treat a `pair` returned by `entries()` as a reference to the original map entry. For the full range-iteration rules, see [References](references.md) and [Containers](containers.md). The runnable mapping example is [examples/pair_map.vs](../../../../Scripts/examples/pair_map.vs).

## Naming and compatibility

`optional`, `readonly`, `weak`, `shared`, and the reflection information types are hard-reserved built-in type names. A script cannot redeclare them.

`pair<T, U>` and `handle<T>` are soft generic built-ins. The compiler supplies `pair` with two type parameters and `handle` with one type parameter only when a user declaration with the same name and generic arity is not present. A non-generic `pair` or `handle`, or a declaration with another arity, can coexist with the corresponding soft generic form.

The standard-namespace aliases are `std::optional`, `std::weak`, `std::shared`, `std::handle`, and `std::pair`. These aliases do not create distinct types.

## Related references

- [Data Types](data-types.md): built-in types, containers, and read-only wrappers.
- [Generics](generics.md): user-defined generic types and soft built-ins.
- [Type conversions](../language-reference/type-conversions.md): `cast`, `try_cast`, `const_cast`, and conversion results.
- [Containers](containers.md): map operations and range iteration.

For script objects, destruction starts the end of the weak-reference lifetime: lock() returns null and expired() returns true during or after destruction. Escaping this from a destructor does not revive the object; subsequent field and method access is rejected. Host-object weak references keep their host lifetime contract.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](value-utility-types.md) · [fr-FR](../../fr-FR/language-basics/value-utility-types.md) · [ja-JP](../../ja-JP/language-basics/value-utility-types.md) · [ko-KR](../../ko-KR/language-basics/value-utility-types.md) · [zh-CN](../../zh-CN/language-basics/value-utility-types.md) · [zh-TW](../../zh-TW/language-basics/value-utility-types.md)
