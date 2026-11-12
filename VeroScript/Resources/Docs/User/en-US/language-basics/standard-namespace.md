# The `std` standard namespace

VeroScript provides a language-reserved `std` namespace for built-in containers, utility value types, reflection types, and public runtime functions. New code should use the qualified `std::` spelling. Module utilities use responsibility-based child namespaces; obsolete flat forms such as `std::contains` and `std::read_file` are not aliases.

## Resolution rules

Both `std::name` and `std.name` are accepted. Public code should use `std::name`. The compiler resolves these names to the existing language or standard-library declarations, so the aliases do not create a second container ABI or add runtime dispatch. Formatting functions are public compiler-provided entry points; other source standard-library functions still follow ordinary import rules.

```VeroScript
import "math.vs";

int main()
{
	std::array<int> values = {2, 3, 5};
	std::optional<int> selected = std::optional<int>(values[0]);

	std::print("selected={}, root={}", selected.get(), std::sqrt(81.0));
	return 0;
}
```

Core language types and direct runtime functions retain their canonical unqualified spellings: `array<int>` and `std::array<int>`, or `print(...)` and `std::print(...)`, refer to the same language capability. Module-owned APIs are intentionally namespace-only; use names such as `std::algorithm::sort` and `std::strings::contains` after importing their module. This is not a compatibility forwarding layer for old `std` names; removed flat `std` spellings do not resolve.

`std` is reserved. A script cannot add declarations to `std` or one of its child namespaces. Namespace aliases also do not change module visibility: an alias to a source standard-library function still requires importing the module that provides it.

## Type aliases

The following table lists the public type aliases. The right-hand side is the existing language or reflection type identity.

| `std::` spelling | Canonical name |
| --- | --- |
| `string`, `wstring` | `string`, `wstring` |
| `array`, `map`, `multimap` | Same-named containers |
| `ordered_map`, `ordered_multimap` | Same-named ordered maps |
| `set`, `multiset`, `ordered_multiset` | Same-named sets |
| `queue`, `priority_queue`, `stack`, `deque` | Same-named sequence containers |
| `channel`, `task`, `concurrent::task_group`, `concurrent::mutex`, `concurrent::counting_semaphore` | Channel, concurrent-task, mutex, and semaphore types |
| `pair` | `pair` |
| `optional`, `weak` | Same-named optional and weak types |
| `readonly` | Read-only value wrapper |
| `shared`, `handle` | `handle` |
| `delegate`, `multicast_delegate` | `delegate`, `multicast_delegate` |
| `type`, `field_info`, `method_info` | Reflection information types |
| `module_info`, `function_info`, `global_info` | Module, function, and global information types |
| `enum_member_info` | Enum-member information type |

The aliases change only source spelling. Generic arguments are still checked using the original type rules, so `std::map<string, int>` remains a two-parameter map.

## Conversion spellings

`std::readonly<T>` is the standard-namespace spelling of `readonly<T>`. The conversion built-ins remain global: use `cast<T>`, `try_cast<T>`, `const_cast<T>`, and parenthesized `(T)expression` without a `std::` prefix. Forms such as `std::cast<T>` and `std::try_cast<T>` are not documented aliases. See [Type conversions](../language-reference/type-conversions.md) for their behavior and failure rules.

```VeroScript
std::readonly<int> published = std::readonly<int>(41);
double precise = cast<double>(published.get());
optional<int> narrowed = try_cast<int>(precise);
```

## Function aliases

### Direct runtime functions

These names refer to built-ins or functions supplied directly by the runtime:

| Category | Functions |
| --- | --- |
| Output and formatting | `print`, `eprint`, `printf`, `format`, `formatf`, `assert`, `std::console::{write, error, flush, clear, is_interactive}` |
| Length and conversion | `len`, `to_string`, `to_wstring`, `to_int`, `to_unsigned`, `to_int64`, `to_uint64`, `to_double`, `to_float` |
| Numeric | `abs`, `min`, `max` |
| Runtime utilities | `range`, `exit` |

Time, console, thread, and environment functions use responsibility-based
names: `std::chrono::now`, `std::chrono::steady_now`,
`std::this_thread::sleep`, `std::environment::getenv`, and
`std::environment::setenv`. No corresponding flat `std` aliases are retained:

```VeroScript
int main()
{
	std::print("elapsed={}", std::chrono::now() > 0.0);
	string mode = std::environment::getenv("MODE");
	std::this_thread::sleep(1);
	return 0;
}
```

### Module functions

The following names are standard-namespace spellings of exported standard-library functions. They still follow ordinary `import` rules:

| Module | Functions |
| --- | --- |
| `io.vs` | `std::io::{read_file, write_file, append_file, read_line, pause, read_token, read_string, read_int, read_double, read_bool}` |
| `io.vs` | `std::filesystem::{delete_file, exists, is_file, is_directory, remove_dir, list_dir, mkdir, cwd, chdir}` |
| `string.vs` | `std::strings::{substring, find, last_find, char_code, contains, starts_with, ends_with, trim_left, trim_right, trim, split, join, to_lower, to_upper, replace, repeat, remove_prefix, remove_suffix}` |
| `path.vs` | `std::filesystem::{basename, dirname, join_path, extension, change_extension}` |
| `concurrent.vs` | `std::concurrent::{channel_send, channel_recv, channel_close, send, recv, close, make_channel, try_send, send_for, try_recv, recv_for, is_closed, pending, capacity, spawn, spawn_task, join_task, cancel_task, cancel_requested, when_all, cancel_all, make_mutex, mutex_lock, mutex_try_lock, mutex_lock_for, mutex_unlock, make_semaphore, semaphore_acquire, semaphore_try_acquire, semaphore_acquire_for, semaphore_release, make_synchronized, synchronized_load, synchronized_store, synchronized_exchange, synchronized_fetch_add}` |
| `math.vs` | Flat numeric names: `std::{sqrt, sin, cos, tan, floor, ceil, round, trunc, pow, exp, log, log2, asin, acos, atan2, isfinite, isnan, pi, e, clamp, clamp_int}` |
| `algorithm.vs` | `std::algorithm::{sort, reverse, find_index, count, lower_bound, binary_search, fill, copy, contains, equal, is_sorted, min_index, max_index, rotate, shuffle}` |
| `numeric.vs` | `std::numeric::{accumulate, sum, average}` |
| `random.vs` | `std::random::{make_engine, seed, next_uint, next_bounded_uint, next_int, next_double, next_double_range, next_bool}` |

For example, `std::sqrt` is available in the preceding sample because it imports `math.vs`. Writing `std::sqrt` alone does not load a standard-library module; `std::format` and `std::formatf` are compiler-provided formatting entry points and do not require `string.vs` just to resolve.

## Uniform extension calls

A visible free function can be called with its first argument as the receiver:

```VeroScript
int grow(ref Counter self, int amount)
{
	/* ... */
}

counter.grow(2); // grow(ref counter, 2)
```

Real script, virtual, host, and built-in members always win. Only when no real member matches does the compiler try the equivalent free-function call. Generic inference, overload resolution, imports, package visibility, defaults, conversions, and `ref`/`const ref` rules are shared with ordinary calls. A reference receiver binds implicitly; if both forms exist, a compatible reference-receiver overload is preferred over copying the receiver. This lowering is compile-time only.

`concurrent.vs` uses this mechanism for channels:

```VeroScript
import "concurrent.vs";

channel<int> messages = channel<int>();
messages.send(42);
int value = messages.recv();
messages.close();
```

## Boundaries

- A script cannot declare `namespace std` or register a user type or function under the `std` name tree.
- `std::` aliases are not module imports; modules remain controlled by `import`, `export`, and package visibility rules.
- Do not depend on names beginning with `__`. The documented `std::` spellings and standard-library pages are the stable public interface.

The complete alias table is maintained by the runtime standard-library registry. See [Namespaces](namespaces.md) for ordinary namespace rules and [Standard Library Reference](../standard-library/index.md) for module signatures.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](standard-namespace.md) · [fr-FR](../../fr-FR/language-basics/standard-namespace.md) · [ja-JP](../../ja-JP/language-basics/standard-namespace.md) · [ko-KR](../../ko-KR/language-basics/standard-namespace.md) · [zh-CN](../../zh-CN/language-basics/standard-namespace.md) · [zh-TW](../../zh-TW/language-basics/standard-namespace.md)
