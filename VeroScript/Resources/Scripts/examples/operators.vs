// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Operator overloads: compound assignment, increment, indexing, calls, and conversion.
// Run from this directory: veroscript operators.vs

class Vec
{
public:
	Vec(int v)
	{
		x = v;
	}

	Vec operator+(const Vec& other) const
	{
		return Vec(x + other.x);
	}
	Vec & operator+=(const Vec& other)
	{
		x = x + other.x;
		return this;
	}
	bool operator==(const Vec& other) const
	{
		return x == other.x;
	}
	bool operator<(const Vec& other) const
	{
		return x < other.x;
	}
	Vec & operator++()
	{
		x = x + 1;
		return this;
	}
	Vec operator++(int tag)
	{
		Vec old = Vec(x);
		x = x + 1;
		return old;
	}
	int operator[](int i)
	{
		return x + i;
	}
	bool operator bool()
	{
		return x != 0;
	}

	int x;
};

Vec operator+(int n, const Vec& v)
{
	return Vec(n + v.x);
}

class Adder
{
public:
	Adder(int b)
	{
		base = b;
	}
	int operator()(int n)
	{
		return base + n;
	}
	void operator()(int& target, const int& delta)
	{
		target += delta;
	}

	int base;
};

class Cell
{
public:

	int & operator[](int index)
	{
		return value;
	}
	const int & operator[](int index) const
	{
		return value;
	}
	Cell & operator=(const int& next)
	{
		value = next;
		return this;
	}

	int value = 0;
};

void main()
{
	Vec a = Vec(3);
	Vec b = Vec(4);
	print("a+b = {}", (a + b).x);
	a += b;
	print("a+=b → {}", a.x);
	print("1+Vec(2) = {}", (1 + Vec(2)).x);
	print("a!=b = {}", a != b);
	print("b>a = {}", b > a);
	++a;
	print("++a → {}", a.x);
	print("a[1] = {}", a[1]);
	Adder f = Adder(10);
	print("f(3) = {}", f(3));
	bool ok = a;
	print("bool(a) = {}", ok);

	// Operator reference parameters bind implicitly; ordinary calls retain
	// their explicit & / ref argument markers.
	int target = 1;
	f(target, 3);
	assert(target == 4);
	print("f(target, 3) → {}", target);

	Cell cell = Cell();
	cell[0] = 5;
	cell[0] += 2;
	int& alias = cell[0];
	alias++;
	const Cell view = cell;
	assert(view[0] == 8);
	print("writable index → {}", view[0]);
	(cell = 9)[0] = 10;
	assert(cell.value == 10);
	print("assignment reference → {}", cell.value);

	int before = a.x;
	Vec old = a++;
	assert(old.x == before && a.x == before + 1);
	print("postfix old/new = {}/{}", old.x, a.x);
}
