// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "core.vs";

int fib(int n)
{
	if (n <= 1)
	{
		return n;
	}
	return fib(n - 1) + fib(n - 2);
}
class Counter
{
public:
	void inc()
	{
		value = value + 1;
	}

	int get()
	{
		return value;
	}

	int value = 0;
};

void main()
{
	auto c = Counter();
	c.inc();
	print("count = " + to_string(c.get()));
	print("fib(10) = " + to_string(fib(10)));
}
