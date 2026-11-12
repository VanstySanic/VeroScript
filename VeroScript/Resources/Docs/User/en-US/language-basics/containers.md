# Containers

VeroScript container types use generic arguments to describe their element or key/value types. Except for fixed-size native arrays, containers are generally represented by shared handles; assigning a container to another variable can make both variables refer to the same container object.

## Common rules

Container types use angle brackets for type arguments, and `len` returns their element or entry count:

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();

print(len(values));
print(len(counts));
```

Inserted elements must match the declared element type. Value types such as structs are copied on insertion; classes and other handle types retain their handle semantics.

Most containers support range iteration. A value loop variable receives an element copy; a `T&` loop variable can modify the element in the container. Maps also support value, key, and key/value-entry iteration.

## `map` and `ordered_map`

`map<K, V>` associates keys with values and uses indexing to access a value:

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;
scores["bob"] = 88;

print(scores["alice"]);
print(scores.has("bob"));
```

Common operations include:

| Operation | Description |
| --- | --- |
| `m[key]` | Read or write the value for a key |
| `m.has(key)` | Test whether a key exists |
| `m.remove(key)` | Remove a key and return whether it was removed |
| `m.keys()` | Return the keys as `array<K>` |
| `m.values()` | Return the values as `array<V>` |
| `m.entries()` | Return the key/value entries as an array |
| `m.clear()` | Clear the map |
| `len(m)` | Return the number of keys |

`map` is unordered; the order returned by `keys()` is unspecified. `ordered_map` preserves the first-insertion order of distinct keys. Updating an existing key does not move it.

## `multimap` and `ordered_multimap`

`multimap<K, V>` allows one key to have multiple values. Use `add` to append a value; indexing returns the `array<V>` associated with the key:

```VeroScript
multimap<string, int> grouped = multimap<string, int>();
grouped.add("alpha", 1).add("alpha", 2);

print(grouped["alpha"][0]);
print(len(grouped["alpha"]));
```

`remove(key)` removes all values stored under that key. `ordered_multimap` additionally preserves the first-insertion order of distinct keys.

## `set` and `multiset`

`set<T>` stores unique elements. Adding an existing element does not create a second element:

```VeroScript
set<string> names = set<string>();
names.add("alice");
names.add("alice");
print(names.has("alice"));
```

`remove(value)` removes an element, and `clear()` empties the set. `multiset<T>` permits repeated values and provides `count(value)`:

```VeroScript
multiset<int> numbers = {2, 1, 2, 3, 2};
print(numbers.count(2));
numbers.remove(2);
```

For a `multiset`, `remove(value)` removes one occurrence. `ordered_multiset` preserves the first-insertion order of distinct values.

## Queues, stacks, priority queues, and deques

| Type | Semantics | Common operations |
| --- | --- | --- |
| `queue<T>` | First in, first out | `push`, `pop`, `front` |
| `stack<T>` | Last in, first out | `push`, `pop`, `top` |
| `priority_queue<T>` | Removes by priority; the current implementation returns the maximum first | `push`, `pop`, `top` |
| `deque<T>` | Insertion and removal at both ends, plus integer indexing | `push_front`, `push_back`, `pop_front`, `pop_back` |

For example, `queue<T>` adds at the back and removes from the front, while `stack<T>` adds and removes at the top. Use `len` to obtain the current number of elements.

## Map iteration

A map can be iterated by value or by key/value entry:

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (int value : scores)
{
	print(value);
}

for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}
```

You can also iterate over `scores.keys()` and access each value with `scores[key]`. The order of an unordered map must not be used as program logic.

### Snapshots and entry views

`m.entries()` returns an `array<pair<K, V>>` of value snapshots. A `pair<K, V>&` obtained by iterating `m` directly is a writable view of the map entry. Do not use a reference to an element of `m.entries()` as if it were a reference into the map; modify the map through direct entry iteration or `m[key]`.

## `channel`

`channel<T>` is a shared handle used to communicate between concurrent tasks. The `concurrent.vs` standard library provides `channel_send`, `channel_recv`, and `channel_close` functions:

```VeroScript
import "concurrent.vs";

channel<int> values = channel<int>();
std::concurrent::channel_send(values, 42);
int result = std::concurrent::channel_recv(values);
std::concurrent::channel_close(values);
```

Channels are normally used with `spawn`, `join`, and task functions. Blocking behavior, transferable types, and task lifetime belong in the concurrency runtime reference rather than the general container reference.

Container lengths and element counts, including `multiset.count(value)`, return nonnegative `int64`. Deque indices use `int64`, while map keys retain their declared key type. See [Built-in functions](../standard-library/builtins.md) for length conversion and range rules, and [Concurrency](../standard-library/concurrent.md) for channel counts and capacities.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](containers.md) · [fr-FR](../../fr-FR/language-basics/containers.md) · [ja-JP](../../ja-JP/language-basics/containers.md) · [ko-KR](../../ko-KR/language-basics/containers.md) · [zh-CN](../../zh-CN/language-basics/containers.md) · [zh-TW](../../zh-TW/language-basics/containers.md)
