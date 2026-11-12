// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Completion handlers run only when the owning script calls dispatch.
class CompletionCount
{
public:
	int value = 0;
};

int main()
{
	Network::context context = Network::create_context(true);
	CompletionCount count = CompletionCount();
	Network::operation operation = Network::resolve(context, "localhost", 80, 5000);
	operation.on_complete([count](Network::operation completed) mutable -> void
	{
		assert(completed.done() && completed.error_code() == 0); assert(completed.address_count() > 0); count.value += 1;
	});
	for (int round = 0; round < 10000 && count.value == 0; ++round)
	{
		context.poll();
		context.dispatch(64);
		if (count.value == 0)
		{
			std::this_thread::sleep(1);
		}
	}
	assert(count.value == 1);
	assert(context.dispatch(64) == 0);
	context.shutdown();
	return 0;
}
