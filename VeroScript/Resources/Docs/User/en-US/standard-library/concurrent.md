# Concurrency module

Module file: `concurrent.vs`

Import it with:

```VeroScript
import "concurrent.vs";
```

## Channels

`channel<T>` is a shared container used for communication between tasks. The standard library exposes generic operations for every transferable channel element type:

| Function | Description |
| --- | --- |
| `std::concurrent::channel_send<T>(channel<T> ch, T value)` | Sends one value to a channel. |
| `std::concurrent::channel_recv<T>(channel<T> ch)` | Receives one value from a channel. |
| `std::concurrent::channel_close<T>(channel<T> ch)` | Closes a channel. |
| `std::concurrent::make_channel<T>(int64 capacity = 0)` | Creates an unbounded channel (`0`) or a bounded channel. |
| `ch.try_send(value)` / `ch.send_for(value, timeout_ms)` | Sends without blocking or with a timeout; returns `false` when it cannot send. |
| `ch.try_recv()` / `ch.recv_for(timeout_ms)` | Returns `optional<T>`; empty means empty, timeout, or closed. |
| `ch.pending()` / `ch.capacity()` / `ch.is_closed()` | Queue depth and capacity return non-negative `int64` (`0` capacity is unbounded); closed state returns `bool`. |

The shorter `send`, `recv`, and `close` forms support both namespace calls and extension syntax: `ch.send(value)`, `ch.recv()`, and `ch.close()`. `T` may be any channel-supported scalar (signed/unsigned integer widths, `float`, `double`, `bool`, `string`, or `wstring`) or another `channel<U>`. Nested channels safely pass synchronized capabilities between isolated workers without sharing ordinary mutable objects. Receiving from an open channel may wait; do not continue operations that require a valid send or receive after closing a channel.

Channel values use shared-handle semantics.

## Isolated tasks

`std::concurrent::spawn` starts an isolated task and returns a task ID. The worker entry point must match the channel type:

```VeroScript
import "concurrent.vs";

void worker(channel<int> ch)
{
	int value = ch.recv();
	ch.send(value + 1);
}

int main()
{
	channel<int> ch = channel<int>();
	int task_id = std::concurrent::spawn("worker", ch);
	ch.send(41);
	std::concurrent::join_task(task_id);
	int result = ch.recv();
	print(result);
	return 0;
}
```

`std::concurrent::join_task` waits for a task to finish. Tasks should exchange supported values through channels instead of directly sharing ordinary script locals.

`std::concurrent::spawn<T>` accepts any supported `channel<T>` when the named worker has the exact matching entry signature:

```text
int std::concurrent::spawn<T>(string name, channel<T> ch);
```

## Typed task results

`std::concurrent::spawn_task<T>` returns a `task<T>` for workers that publish one result through a result channel:

```VeroScript
import "concurrent.vs";

void compute(channel<int> output)
{
	output.send(42);
}

int main()
{
	std::task<int> result = std::concurrent::spawn_task<int>("compute");
	int value = result.await();
	print(value);
	return 0;
}
```

A string entry names a worker with the exact signature `void compute(channel<T> output)`. Send exactly one result before returning. Missing or extra results are errors after completion; a worker blocked before completion still needs cooperative cancellation.

`task<T>` provides these members for every supported transferable channel type:

| Member | Description |
| --- | --- |
| `valid()` | Tests whether the task is still active. |
| `done()` | Tests completion without waiting. |
| `await()` | Waits for the task, receives one result, and marks it inactive. |
| `await_for(timeout_ms)` | Only a completion timeout returns an empty optional and keeps the task active. Completion must provide exactly one result; failure is an error. |
| `cancel()` | Cooperatively cancels the task and closes its result channel. The VM retains the worker until it exits. |

Cooperative cancellation does not forcibly terminate or detach a thread. A worker used with `std::concurrent::spawn_task<T>` should observe channel closure and exit; VM/session teardown waits for workers that ignore cancellation so host callback lifetimes remain safe.

`task_group<T>` collects homogeneous tasks and provides `add`, `await_all`, and
`cancel_all`. The equivalent free helpers are `std::concurrent::when_all` and
`std::concurrent::cancel_all`.

Ordinary VM execution uses an isolated worker VM per task. It does not make one
session or arbitrary script objects concurrently mutable. Channels intentionally
accept only transferable scalar values and channel handles. The C++ AOT backend preserves the public
API using native threads and synchronized channels.

## Callable task entries

After importing `concurrent.vs`, a resolved script function, static method, or
function value (including a lambda) can be passed directly to `std::concurrent::spawn` or
`std::concurrent::spawn_task<T>`. Arguments may have different types and are evaluated once,
left to right. The callable's fixed parameters and the result `T` must match
exactly after normal argument conversion. `std::concurrent::spawn` requires a `void` result;
`std::concurrent::spawn_task<T>` requires a transferable `T` result.

```VeroScript
import "concurrent.vs";

int twice(int value)
{
	return value * 2;
}

int main()
{
	std::task<int> work = std::concurrent::spawn_task<int>(twice, 21);
	int factor = 3;
	std::task<int> captured = std::concurrent::spawn_task<int>(
		[factor](int value) -> int => value * factor, 7);
	print(work.await());
	print(captured.await());
	return 0;
}
```

Arguments and lambda captures may be transferable scalars, strings, or channel
handles. Reference and `this` captures, instance methods, mutable objects,
arrays, `ref` parameters and reference returns cannot cross the worker boundary.
The worker gets an isolated VM and an input snapshot. `await()` remains a
blocking wait; it is not an `async` language keyword. A callable worker can
poll `std::concurrent::cancel_requested()` after `std::concurrent::cancel_task(id)` or `task<T>.cancel()`.
Cancellation never forcibly stops the worker. The C++, Python and C# AOT
transpilers currently reject callable entries at compile time; string entries
remain available according to each backend's existing support.

## Synchronization capabilities

`std::concurrent::mutex` and `std::concurrent::counting_semaphore` are explicit
aliases of synchronized channel handles. They can therefore be sent through a
nested channel and used by multiple isolated worker VMs.

| Function | Description |
| --- | --- |
| `std::concurrent::make_mutex()` | Creates an unlocked, non-recursive mutex. |
| `std::concurrent::mutex_lock`, `std::concurrent::mutex_try_lock`, `std::concurrent::mutex_lock_for`, `std::concurrent::mutex_unlock` | Acquires or releases a mutex. Double unlock is a runtime error. |
| `std::concurrent::make_semaphore(initial, maximum)` | Creates a bounded counting semaphore. |
| `std::concurrent::semaphore_acquire`, `std::concurrent::semaphore_try_acquire`, `std::concurrent::semaphore_acquire_for` | Acquires one permit. |
| `std::concurrent::semaphore_release(value)` | Releases one permit; exceeding the maximum is a runtime error. |

For shared transferable state, `std::concurrent::make_synchronized<T>(initial)` creates a capacity-one
channel cell. `std::concurrent::synchronized_load`, `std::concurrent::synchronized_store`,
`std::concurrent::synchronized_exchange`, and `std::concurrent::synchronized_fetch_add` perform serialized
operations on it. These are portable lock-based operations; they do not claim
the lock-free guarantees of a hardware `atomic<T>`.

## Task cancellation requests

`std::concurrent::cancel_task(int id)` requests cooperative cancellation of a valid task ID without consuming it; `std::concurrent::join_task(id)` can still wait afterward. An invalid ID is a runtime error. `std::concurrent::cancel_requested()` is `true` inside that worker after the request and `false` outside a worker. A worker should poll it around bounded waits; it does not interrupt a blocked operation or terminate the thread. `task<T>.cancel()` also closes its result channel.

`pending(ch)`, `capacity(ch)` and `task_group<T>.size()` return nonnegative `int64`. `std::concurrent::make_channel<T>` accepts an `int64` capacity: `0` is unbounded and positive values select a bounded channel. Negative or unrepresentable capacities are errors; actual allocation remains subject to host limits. Task IDs retain type `int`.

- [Standard library foundation](foundation.md)

<a id="concurrency-aot-boundary"></a>

Standalone AOT boundary: task<T>, task_group, when_all and mutex/semaphore/synchronized now require Core capability identity, ownership and consumption checks, including spawn_task with a string entry. C++/Python/C# standalone AOT rejects these operations before output; run them through Core VM/Mixed/JIT. Existing ordinary channel, string spawn and join paths remain available in C++/Python.

When an owning Core VM finishes, it interrupts Core channel waits in its owned workers before joining them. Existing direct-argument channel closure still applies; the shutdown notification does not close other channels. Explicit cancellation remains cooperative, detached workers remain independent, and blocking host callbacks require host cooperation.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](concurrent.md) · [fr-FR](../../fr-FR/standard-library/concurrent.md) · [ja-JP](../../ja-JP/standard-library/concurrent.md) · [ko-KR](../../ko-KR/standard-library/concurrent.md) · [zh-CN](../../zh-CN/standard-library/concurrent.md) · [zh-TW](../../zh-TW/standard-library/concurrent.md)
