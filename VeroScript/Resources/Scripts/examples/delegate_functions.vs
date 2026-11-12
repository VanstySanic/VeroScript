// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Named-function delegates; suitable for VM and C++/C#/Python AOT.
import "delegate.vs";

int zero()
{
	return 7;
}

int one(int a)
{
	return a + 1;
}

int two(int a, int b)
{
	return a + b;
}

int three(int a, int b, int c)
{
	return a + b + c;
}

void report_zero()
{
	print(0);
}

void report_one(int a)
{
	print(a);
}

void report_two(int a, int b)
{
	print(a + b);
}

void report_three(int a, int b, int c)
{
	print(a + b + c);
}

int main()
{
	std::delegate<int> d0 = std::delegate<int>();
	std::delegate<int, int> d1 = std::delegate<int, int>();
	std::delegate<int, int, int> d2 = std::delegate<int, int, int>();
	std::delegate<int, int, int, int> d3 = std::delegate<int, int, int, int>();
	d0.bind(zero);
	d1.bind(one);
	d2.bind(two);
	d3.bind(three);
	assert(d0.execute() == 7 && d1.execute(2) == 3);
	assert(d2.execute(2, 3) == 5 && d3.execute(1, 2, 3) == 6);
	assert(d0.is_bound());
	d0.unbind();
	assert(!d0.is_bound() && !d0.execute_if_bound());
	d0.bind(zero);
	assert(d0.execute() == 7);

	std::delegate<void> notification = std::delegate<void>();
	notification.bind(report_zero);
	notification.execute();
	notification.unbind();
	assert(!notification.execute_if_bound());

	std::multicast_delegate m0 = std::multicast_delegate();
	std::multicast_delegate<int> m1 = std::multicast_delegate<int>();
	std::multicast_delegate<int, int> m2 = std::multicast_delegate<int, int>();
	std::multicast_delegate<int, int, int> m3 = std::multicast_delegate<int, int, int>();
	m0 += report_zero;
	m1.add(report_one);
	m2.add(report_two);
	m3.add(report_three);
	m0.broadcast();
	m1.broadcast(1);
	m2.broadcast(1, 1);
	m3.broadcast(1, 1, 1);
	m0 -= report_zero;
	m1.remove(report_one);
	m2.clear();
	m3.clear();
	assert(m0.count() == 0 && m1.count() == 0 && m2.count() == 0 && m3.count() == 0);
	print("portable delegates ok");
	return 0;
}
