# 17: Optional values, weak references, and shared struct handles

This tutorial builds a small state-sharing example with VeroScript's compiler-provided utility types. You will learn when to use `optional<T>`, `weak<T>`, `handle<T>`, and `pair<T, U>`, and how their value and ownership rules differ.

`shared<T>` is the recommended spelling of `handle<T>`; both names denote exactly the same type, including inside containers, generic functions and `weak<T>`. `std::shared<T>` and `std::handle<T>` use that same identity. Explicit `shared<T>()` constructs a non-null wrapper; assigning `nullptr` clears a handle. For a struct payload, construction copies the value into the wrapper, while copying a shared handle preserves identity. `weak<shared<T>>` observes the wrapper; `lock()` returns a strong handle to that same object or `nullptr` after expiration. Check the lock result before accessing `.value`. The spelling `shared` is a reserved built-in type alias, not a syntax keyword. Like the existing soft generic rules, a user-declared `handle<T>` with matching arity supplies the type behind both spellings.

## Goal

By the end, you should be able to:

- represent a result that may be absent without using a sentinel value;
- observe a class without keeping it alive;
- share a struct payload through a class handle;
- distinguish map-entry snapshots from writable entry views.

The complete reference is [Value utility types](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/value-utility-types.md). Run each example with `--check` before executing it.

## Step 1: represent an optional result

Use `optional<T>()` for an empty result and `optional<T>(value)` for a result that contains a value. Check `has_value()` before calling `get()`:

```VeroScript
optional<int> find_score(bool available)
{
	if (available)
	{
		return optional<int>(95);
	}
	return optional<int>();
}

int main()
{
	optional<int> missing = find_score(false);
	print(missing.value_or(0));

	optional<int> found = find_score(true);
	if (found.has_value())
	{
		print(found.get());
	}

	return 0;
}
```

`value_or` is useful when a default is enough. `get()` is useful after an explicit state check; calling it on an empty optional fails through `assert`. `try_cast<T>` uses the same result pattern for fallible conversions.

## Step 2: observe a class without owning it

A class variable is a strong handle. A `weak<T>` value observes that object but does not keep it alive:

```VeroScript
class Session
{
public:
	int id = 0;
};

int main()
{
	Session owner = Session();
	owner.id = 42;

	weak<Session> watcher = weak<Session>(owner);
	if (!watcher.expired())
	{
		Session locked = watcher.lock();
		print(locked.id);
	}

	owner = nullptr;
	print(watcher.expired());
	return 0;
}
```

`lock()` returns a strong handle when the object is still alive and a null handle otherwise. Always check `expired()` or check the result of `lock()` before accessing members. Copying a weak value creates another observation; resetting one copy does not reset another.

## Step 3: share a struct payload

Struct assignment normally copies a value. When several parts of a program must mutate one shared struct payload, put it in `handle<T>`:

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void move_right(handle<Point> point)
{
	point.value.x += 1;
}

int main()
{
	shared<Point> first = shared<Point>(Point());
	handle<Point> second = first;

	first.value.x = 10;
	move_right(second);
	print(first.value.x);
	print(second.value.x);

	return 0;
}
```

Assigning one `handle<T>` to another copies the class handle, so both variables see the same `value`. `handle<T>(source)` instead copies the payload into a new shared object. Assigning `nullptr` clears the handle. The complete sample is [examples/handle_struct.vs](../../../../../VeroScript/Resources/Scripts/examples/handle_struct.vs).

## Step 4: choose a `pair` view

Map APIs use `pair<K, V>`. The result of `entries()` is an array of value snapshots, so changing a snapshot does not change the map. Iterating the map directly with `pair<K, V>&` gives a writable entry view:

```VeroScript
int main()
{
	ordered_map<string, int> scores = ordered_map<string, int>();
	scores["alice"] = 95;
	scores["bob"] = 88;

	for (pair<string, int> snapshot : scores.entries())
	{
		print("{} -> {}", snapshot.first, snapshot.second);
	}

	for (pair<string, int>& entry : scores)
	{
		entry.second += 1;
	}

	for (auto& [key, value] : scores)
	{
		print("{} -> {}", key, value);
	}

	return 0;
}
```

Use `m.entries()` when you need a collection of independent entry values. Use direct map iteration when you need to write through to an entry. The complete example is [examples/pair_map.vs](../../../../../VeroScript/Resources/Scripts/examples/pair_map.vs).

## Exercises

1. Change `find_score` to return an `optional<string>` message instead of an integer.
2. Add a second `weak<Session>` watcher and verify that resetting one watcher does not reset the other.
3. Add a `y`-movement function to the `handle<Point>` example and call it through the second handle.
4. Add a `map<string, Point>` and update a nested field through `pair<string, Point>&`.

## What to remember

- `optional<T>` represents an empty-or-present value and is a value type.
- `weak<T>` observes a class without extending its lifetime.
- `handle<T>` shares a class object whose public `value` field contains the payload.
- `pair<K, V>` from `entries()` is a value snapshot; a pair reference from direct map iteration writes through.

Next: [Use the std standard namespace](14-standard-namespace.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](17-value-utility-types.md) · [fr-FR](../../fr-FR/advanced/17-value-utility-types.md) · [ja-JP](../../ja-JP/advanced/17-value-utility-types.md) · [ko-KR](../../ko-KR/advanced/17-value-utility-types.md) · [zh-CN](../../zh-CN/advanced/17-value-utility-types.md) · [zh-TW](../../zh-TW/advanced/17-value-utility-types.md)
