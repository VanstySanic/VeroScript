// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// concurrent_demo.vs - isolated tasks, bounded channels, and timed waits
// Requires task-spawn permission and a host with isolated VM workers.

import "concurrent.vs";

void worker(channel<int> ch)
{
	int x = ch.recv();
	ch.send(x + 1);
}

export void answer(channel<int> output)
{
	std::this_thread::sleep(10);
	output.send(42);
}

export void increment_shared(channel<channel<int>> setup)
{
	channel<int> value = setup.recv();
	std::concurrent::synchronized_fetch_add(value, 1);
}

export void cooperative(channel<int> unused)
{
	for (int i = 0; i < 5000 && !std::concurrent::cancel_requested(); i++)
	{
		std::this_thread::sleep(1);
	}
	assert(std::concurrent::cancel_requested());
}

int double_value(int value)
{
	return value * 2;
}

struct TaskMath
{
	static int add(int left, int right)
	{
		return left + right;
	}
};

void send_value(channel<int> output, int value)
{
	output.send(value);
}

void main()
{
	channel<int> ch = std::concurrent::make_channel<int>(1);
	assert(ch.capacity() == 1);
	assert(ch.try_send(41));
	assert(!ch.try_send(99));
	int t = std::concurrent::spawn("worker", ch);
	std::concurrent::join_task(t);
	int y = ch.recv();
	assert(y == 42);
	print(y);

	channel<string> messages = channel<string>();
	messages.send("hi");
	string message = messages.recv();
	assert(message == "hi");
	messages.close();
	assert(messages.is_closed());
	print(message);

	task<int> delayed = std::concurrent::spawn_task<int>("answer");
	optional<int> early = delayed.await_for(0);
	// Scheduling can finish the worker before this non-blocking wait.
	// A successful await_for consumes the result; await only after a timeout.
	if (early.has_value())
	{
		assert(early.get() == 42);
	}
	else
	{
		assert(delayed.await() == 42);
	}

	// Callable workers return directly; by-value captures become input snapshots.
	task<int> direct = std::concurrent::spawn_task<int>(double_value, 21);
	int factor = 3;
	task<int> captured = std::concurrent::spawn_task<int>([factor](int value) -> int => value * factor, 7);
	task<int> staticMethod = std::concurrent::spawn_task<int>(TaskMath::add, 20, 22);
	assert(direct.await() == 42);
	assert(captured.await() == 21);
	assert(staticMethod.await() == 42);

	// A void callable returns a task ID; join before reading its channel result.
	channel<int> delivered = channel<int>();
	int delivery = std::concurrent::spawn(send_value, delivered, 42);
	std::concurrent::join_task(delivery);
	assert(delivered.recv() == 42);

	// Nested channels pass synchronized capabilities between isolated VMs.
	channel<channel<int>> setup = channel<channel<int>>();
	channel<int> counter = std::concurrent::make_synchronized<int>(0);
	int first = std::concurrent::spawn("increment_shared", setup);
	int second = std::concurrent::spawn("increment_shared", setup);
	setup.send(counter);
	setup.send(counter);
	std::concurrent::join_task(first);
	std::concurrent::join_task(second);
	assert(std::concurrent::synchronized_load(counter) == 2);

	assert(!std::concurrent::cancel_requested());
	channel<int> cancellation = channel<int>();
	int cancellable = std::concurrent::spawn("cooperative", cancellation);
	std::concurrent::cancel_task(cancellable);
	std::concurrent::join_task(cancellable);

	std::concurrent::mutex gate = std::concurrent::make_mutex();
	assert(std::concurrent::mutex_try_lock(gate));
	std::concurrent::mutex_unlock(gate);

	std::concurrent::counting_semaphore permits = std::concurrent::make_semaphore(2, 4);
	std::concurrent::semaphore_acquire(permits);
	std::concurrent::semaphore_release(permits);
}
