// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// lambda.vs - first-class function values and capture syntax.
// `function` is the readable spelling; `fn` remains its compact alias.

int g_base = 40;

namespace NamedCallbacks
{
	int answer()
	{
		return 42;
	}
}

int apply(function(int) -> int f, int x)
{
	return f(x);
}

void add_to(int& value, int amount)
{
	value = value + amount;
}

class Counter
{
public:
	Counter(int v)
	{
		value = v;
	}

	function() -> int make_getter()
	{
		return function() -> int => value;
	}

	int value;
};

function() -> int make_counter()
{
	return [count = 0]() mutable -> int
	{
		return++count;
	};
}

int remember_first(int seed)
{
	static int value = seed;
	return++value;
}

void main()
{
	function(int) -> int inc = function(int x) -> int => x + 1;
	print(inc(41));

	int doubled = apply(function(int x) -> int
	{
		return x * 2;
	}, 21);
	print(doubled);

	function(int &, int) -> void mutate = add_to;
	int mutableValue = 40;
	mutate(&mutableValue, 2);
	print(mutableValue);

	int base = 40;

	// function(...) and [=](...) both auto-capture by value.
	function(int) -> int add = function(int x) -> int => x + base;
	print(add(2));
	fn(int) -> int add2 = [=](int x) -> int => x + base;
	print(add2(2));

	// Explicit list: only listed locals may be captured; globals may be listed too
	fn(int) -> int add3 = [base](int x) -> int => x + base;
	print(add3(2));
	fn(int) -> int addg = [g_base](int x) -> int => x + g_base;
	print(addg(2));

	// [&] captures used locals by reference. The VM promotes captured locals to
	// safe shared cells, so a returned closure never retains a dangling stack slot.
	int running = 40;
	function() -> int next = [&]() -> int
	{
		running++;
		return running;
	};
	print(next());
	print(running);

	// Mixed defaults match familiar C++ capture-list spelling.
	int step = 2;
	function() -> int advance = [=, &running]() -> int
	{
		running += step;
		return running;
	};
	print(advance());

	// [] = no outer local captures (globals still visible)
	print(([](int x) -> int => x + 1)(41));

	Counter c = Counter(42);
	function() -> int get = c.make_getter();
	print(get());

	// The compact `fn` spelling is fully interchangeable.
	print((fn(int x) -> int => x + 1)(41));

	// Function values share their mutable environment. A new factory call is independent.
	auto first = make_counter();
	auto alias = first;
	auto other = make_counter();
	assert(first() == 1 && alias() == 2 && other() == 1);
	// Initializers run once in the enclosing scope; reference captures keep their owner.
	auto initialized = [&target = running, amount = 3]() -> int
	{
		target += amount;
		return target;
	};
	assert(initialized() == 46 && running == 46);
	// A local static keeps the first initialization within this program state.
	assert(remember_first(10) == 11 && remember_first(99) == 12);

	// Capture a namespace-qualified function value and invoke it later.
	auto callback = [answer = NamedCallbacks::answer]() -> int
	{
		return answer();
	};
	assert(callback() == 42);
}
