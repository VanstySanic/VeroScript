# Events module

Module file: `delegate.vs`

Import it with:

```VeroScript
import "delegate.vs";
```

Use `std::delegate` and `std::multicast_delegate` in public scripts. Their unqualified spellings name the same types; importing `delegate.vs` is required for both. All members use lowercase names.

## Unicast delegates

`delegate` stores one handler with up to three arguments. Its first type parameter is the return type, followed by the call-argument types:

```VeroScript
std::delegate<int, int> mitigate = std::delegate<int, int>();
mitigate.bind(function(int value) -> int => value / 2);
int result = mitigate.execute(10);
```

The module provides `delegate<R>`, `delegate<R, A>`, `delegate<R, A, B>`, and `delegate<R, A, B, C>`. Members include:

| Member | Description |
| --- | --- |
| `bind(handler)` | Binds or replaces the handler. |
| `unbind()` | Removes the current handler. |
| `is_bound()` | Tests whether a handler is bound. |
| `execute(...)` | Executes the handler; asserts when unbound. |
| `execute_if_bound(...)` | Executes only when bound and returns whether it ran. |

`execute_if_bound` reports whether execution occurred; it does not return the handler's result.

The handler slot is private. `unbind()` clears the delegate's stored handler reference; use `is_bound()` to inspect the state.

## Multicast delegates

`multicast_delegate` stores a list of `void` handlers and supports zero to three arguments:

```VeroScript
std::multicast_delegate<int> on_score = std::multicast_delegate<int>();
on_score.add(function(int value) -> void => print(value));
on_score.broadcast(42);
```

Members include:

| Member | Description |
| --- | --- |
| `add(handler)` | Adds a handler. |
| `remove(handler)` | Removes a matching handler. |
| `broadcast(...)` | Calls all handlers in registration order. |
| `count()` | Returns the number of handlers as nonnegative `int64`. |
| `clear()` | Removes all handlers. |

Broadcasting first takes a snapshot of the handler list, so changes made during a broadcast do not change the call set for that broadcast.

The `handlers` field of `multicast_delegate` is public in the source module. Prefer `add`, `remove`, and `clear` when registration order and snapshot semantics must be preserved.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](events.md) · [fr-FR](../../fr-FR/standard-library/events.md) · [ja-JP](../../ja-JP/standard-library/events.md) · [ko-KR](../../ko-KR/standard-library/events.md) · [zh-CN](../../zh-CN/standard-library/events.md) · [zh-TW](../../zh-TW/standard-library/events.md)
