// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// concurrent.vs - isolated worker VMs and synchronized channel capabilities.
// Prefer these over raw __channel_* / __spawn builtins in user code.

/**
 * Synchronization capabilities are channel handles, so they can safely be sent
 * through another channel to an isolated worker. They do not make ordinary
 * objects or containers shared mutable state.
 */
export using mutex = channel<bool>;
export using counting_semaphore = channel<bool>;

export void __std_channel_send<T>(channel<T> ch, T value)
{
	__channel_send(ch, value);
}

export T __std_channel_recv<T>(channel<T> ch)
{
	return __channel_recv(ch);
}

export void __std_channel_close<T>(channel<T> ch)
{
	__channel_close(ch);
}

// Short names are designed for uniform extension-call syntax (`ch.send(value)`).
export void send<T>(channel<T> ch, T value)
{
	__channel_send(ch, value);
}

export T recv<T>(channel<T> ch)
{
	return __channel_recv(ch);
}

export void close<T>(channel<T> ch)
{
	__channel_close(ch);
}

/** Create an unbounded channel when capacity is 0, otherwise a bounded channel. */
export channel<T> __std_make_channel<T>(int64 capacity = 0)
{
	assert(capacity >= 0, "channel capacity must be non-negative");
	channel<T> result = channel<T>();
	if (capacity > 0)
	{
		__channel_set_capacity(result, capacity);
	}
	return result;
}

/** Non-blocking send. Returns false when the channel is full or closed. */
export bool try_send<T>(channel<T> ch, T value)
{
	return __channel_try_send(ch, value);
}

/** Timed send. Returns false when the timeout elapses or the channel closes. */
export bool send_for<T>(channel<T> ch, T value, int timeout_ms)
{
	assert(timeout_ms >= 0, "channel timeout must be non-negative");
	return __channel_send_for(ch, value, timeout_ms);
}

/** Non-blocking receive. An empty optional means empty or closed. */
export optional<T> try_recv<T>(channel<T> ch)
{
	array<T> received = __channel_try_recv(ch);
	if (len(received) == 0)
	{
		return optional<T>();
	}
	return optional<T>(received[0]);
}

/** Timed receive. An empty optional means timeout or closed. */
export optional<T> recv_for<T>(channel<T> ch, int timeout_ms)
{
	assert(timeout_ms >= 0, "channel timeout must be non-negative");
	array<T> received = __channel_recv_for(ch, timeout_ms);
	if (len(received) == 0)
	{
		return optional<T>();
	}
	return optional<T>(received[0]);
}

export bool is_closed<T>(channel<T> ch)
{
	return __channel_is_closed(ch);
}

export int64 pending<T>(channel<T> ch)
{
	return __channel_size(ch);
}

/** Zero denotes an unbounded channel. */
export int64 capacity<T>(channel<T> ch)
{
	return __channel_capacity(ch);
}

/** An owned non-recursive mutex; a foreign executor cannot unlock it. */
export mutex __std_make_mutex()
{
	return __sync_make_mutex();
}
export void mutex_lock(mutex value)
{
	__sync_mutex_lock(value);
}
export bool mutex_try_lock(mutex value)
{
	return __sync_mutex_try_lock(value);
}
export bool __std_mutex_lock_for(mutex value, int timeout_ms)
{
	return __sync_mutex_lock_for(value, timeout_ms);
}
export void mutex_unlock(mutex value)
{
	__sync_mutex_unlock(value);
}

/** Fixed-maximum counting semaphore. Permit storage is constant size. */
export counting_semaphore __std_make_semaphore(int initial, int maximum)
{
	return __sync_make_semaphore(initial, maximum);
}
export void semaphore_acquire(counting_semaphore value)
{
	__sync_semaphore_acquire(value);
}
export bool semaphore_try_acquire(counting_semaphore value)
{
	return __sync_semaphore_try_acquire(value);
}
export bool __std_semaphore_acquire_for(counting_semaphore value, int timeout_ms)
{
	return __sync_semaphore_acquire_for(value, timeout_ms);
}
export void semaphore_release(counting_semaphore value)
{
	__sync_semaphore_release(value);
}

/** Native snapshot/commit operations never leave the cell without a payload. */
export channel<T> __std_make_synchronized<T>(T initial)
{
	return __sync_make<T>(initial);
}
export T synchronized_load<T>(channel<T> value)
{
	return __sync_load<T>(value);
}
export void synchronized_store<T>(channel<T> value, T desired)
{
	__sync_store<T>(value, desired);
}
export T synchronized_exchange<T>(channel<T> value, T desired)
{
	return __sync_exchange<T>(value, desired);
}
export T synchronized_fetch_add<T>(channel<T> value, T delta)
{
	return __sync_fetch_add<T>(value, delta);
}

/** Spawn an isolated worker whose entry is `void name(channel<T> ch)`. Returns task id. */
export int __std_spawn<T>(string name, channel<T> ch)
{
	return __spawn(name, ch);
}

export void __std_join_task(int task_id)
{
	__join_task(task_id);
}

/** Request cooperative cancellation without consuming the task id. */
export void __std_cancel_task(int task_id)
{
	__task_done(task_id);
	__cancel_task(task_id);
}

/** True inside a worker after cancellation was requested; false elsewhere. */
export bool __std_cancel_requested()
{
	return __cancel_requested();
}

/**
 * Typed asynchronous result. Workers keep the existing isolation contract and
 * publish exactly one T to their channel. `cancel` is cooperative: it closes the
 * channel and requests worker completion; it never terminates or detaches a thread.
 */
export class task<T>
{
public:
	task(int id, channel<T> output)
	{
		__typed_task_bind(id, output);
		task_id = id;
		result = output;
	}
	bool valid() const
	{
		return __typed_task_valid(task_id, result);
	}
	bool done() const
	{
		return __typed_task_done(task_id, result);
	}
	T await()
	{
		return __typed_task_await<T>(task_id, result);
	}
	optional<T> await_for(int timeout_ms)
	{
		return __typed_task_await_for<T>(task_id, result, timeout_ms);
	}
	void cancel()
	{
		__typed_task_cancel(task_id, result);
	}
private:
	int task_id = -1;
	channel<T> result;
};

/**
 * Spawn exported `void name(channel<T>)`; the worker must send one result value.
 * T may be any transferable type accepted by the channel ABI.
 */
export task<T> __std_spawn_task<T>(string name)
{
	channel<T> output = channel<T>();
	int id = __spawn_result(name, output);
	return task<T>(id, output);
}

/** A failed await_all retains only unconsumed work in its pending range. */
export class task_group<T>
{
public:
	void add(task<T> work)
	{
		assert(!busy, "task_group cannot be modified while an operation is active");
		assert(work != nullptr, "task_group cannot contain null tasks");
		tasks.push(work);
	}
	int64 size() const
	{
		return len(tasks) - cursor;
	}
	array<T> await_all()
	{
		return __task_group_await_all<T>(this);
	}
	void cancel_all()
	{
		__task_group_cancel_all(this);
	}
private:
	array<task<T>> tasks = array<task<T>>();
	int64 cursor = 0;
	bool busy = false;
};

export array<T> __std_when_all<T>(array<task<T>> tasks)
{
	return __task_array_await_all<T>(tasks);
}

export void __std_cancel_all<T>(array<task<T>> tasks)
{
	__task_array_cancel_all(tasks);
}
