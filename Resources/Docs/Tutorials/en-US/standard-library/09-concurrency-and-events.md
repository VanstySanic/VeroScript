# 09: Concurrency and events

Shared state is where concurrency becomes difficult. This tutorial sends a value through a channel instead of sharing an ordinary local variable, then uses a multicast delegate to notify multiple handlers.

## Step 1: let a task process a value

```VeroScript
import "concurrent.vs";

void worker(channel<int> input)
{
	int value = input.recv();
	input.send(value + 1);
}

int twice(int value)
{
	return value * 2;
}

int main()
{
	channel<int> pipe = channel<int>();
	int task_id = std::concurrent::spawn(worker, pipe);

	pipe.send(41);
	std::concurrent::join_task(task_id);

	int result = pipe.recv();
	assert(result == 42);
	task<int> doubled = std::concurrent::spawn_task<int>(twice, 21);
	assert(doubled.await() == 42);
	print(result);
	return 0;
}
```

The task receives and sends values through `channel<int>`. `std::concurrent::spawn` accepts a resolved script function, static method, or function value and returns a task ID; `join_task` waits for completion. `spawn_task<T>` accepts the same callable forms and returns a typed result through `task<T>.await()`. Callable arguments and lambda captures must be transferable values such as scalars, strings, or channel handles; each worker runs in an isolated VM. `send`, `recv`, and `close` are ordinary visible free functions exposed through VeroScript's uniform extension-call syntax. Ordinary local variables are not a substitute for a task communication channel. Callable task support depends on the execution backend; see the concurrency reference for its limits.

Cancellation is cooperative. A running worker can poll `std::concurrent::cancel_requested()` after another part of the program calls `std::concurrent::cancel_task(id)`. The request does not interrupt a blocked operation or force the worker to stop; `task<T>.cancel()` also closes that task's result channel.

## Step 2: broadcast a result

```VeroScript
import "delegate.vs";

int main()
{
	multicast_delegate<int> on_result = multicast_delegate<int>();
	on_result.add(function(int value) -> void => print("log={}", value));
	on_result.add(function(int value) -> void => print("double={}", value * 2));

	on_result.broadcast(21);
	print("listeners={}", on_result.count());
	return 0;
}
```

A multicast delegate stores several `void` handlers. `broadcast` calls them in their current registration order, which is useful when one result should trigger multiple independent responses.

## Choosing a mechanism

- A function call: the caller knows the function and needs an immediate result.
- `delegate`: one replaceable handler.
- `multicast_delegate`: notification for multiple handlers.
- `channel<T>`: values exchanged between tasks.
- `spawn_task<T>`: a callable task that produces a typed result you can await.
- `make_synchronized<T>`, mutexes, and semaphores: explicit synchronized handles for transferable shared state.

Pay attention to waiting order. A sender waiting for a receiver while the receiver waits for the sender is the most common deadlock pattern. Design the message flow before adding `spawn` and `join_task`.

## Exercises

1. Change `worker` to receive two integers and return their sum.
2. add a handler that prints only when the result is greater than `40`.
3. Send a status string through `channel<string>` and close the channel in the main function.

Runnable examples: [`examples/concurrent_demo.vs`](../../../../../VeroScript/Resources/Scripts/examples/concurrent_demo.vs) and [`examples/delegates.vs`](../../../../../VeroScript/Resources/Scripts/examples/delegates.vs). See [Concurrency](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/concurrent.md) and [Events](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/events.md) for the full rules.

Next: [Building packages and distributing an application](../toolchain/10-packages-and-distribution.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](09-concurrency-and-events.md) · [fr-FR](../../fr-FR/standard-library/09-concurrency-and-events.md) · [ja-JP](../../ja-JP/standard-library/09-concurrency-and-events.md) · [ko-KR](../../ko-KR/standard-library/09-concurrency-and-events.md) · [zh-CN](../../zh-CN/standard-library/09-concurrency-and-events.md) · [zh-TW](../../zh-TW/standard-library/09-concurrency-and-events.md)
