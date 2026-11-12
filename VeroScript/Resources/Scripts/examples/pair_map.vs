// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// pair_map.vs — pair, entries, entry views, and destructuring range-for
// Run from this directory: veroscript pair_map.vs

struct Point
{
	int x = 0;
	int y = 0;
};

class Box
{
public:
	int get() const
	{
		return n;
	}

	void set(int v)
	{
		n = v;
	}

	int n = 0;
};

class WatchedObject
{
};

void bump(int& x)
{
	x = x + 1;
}

void main()
{
	const Box b = Box();
	print("const Box.get: {}", b.get());

	Box mutableBox = Box();
	mutableBox.set(41);
	print("Box.set then get: {}", mutableBox.get());

	ordered_map<string, int> m = ordered_map<string, int>();
	m["a"] = 10;
	m["b"] = 20;

	for (pair<string, int> p : m.entries())
	{
		print("entry {} -> {}", p.first, p.second);
		bump(&m[p.first]);
	}
	print("after bump via p.first: {}, {}", m["a"], m["b"]);

	// Value pair foreach (entries sugar) — copies only.
	for (pair<string, int> p : m)
	{
		print("pair foreach: {}={}", p.first, p.second);
	}

	// An alias entry view writes through its value, including nested fields.
	for (pair<string, int>& p : m)
	{
		print("alias pair {}={}", p.first, p.second);
		bump(&p.second);
	}
	print("after alias pair bump: {}, {}", m["a"], m["b"]);

	// Typed destructure + C++-ish auto& [k, v]
	for ((string k, int& v) : m)
	{
		print("destructure {}={}", k, v);
		v = v + 1;
	}
	print("after typed destructure: {}, {}", m["a"], m["b"]);

	for (auto& [k, v] : m)
	{
		print("auto& [{}, {}]", k, v);
		bump(&v);
	}
	print("after auto& destructure: {}, {}", m["a"], m["b"]);

	map<string, Point> points = map<string, Point>();
	points["o"] = Point();
	assert(points["o"].x == 0 && points["o"].y == 0);
	for (pair<string, Point>& p : points)
	{
		p.second.x = 3;
		bump(&p.second.y);
	}
	assert(points["o"].x == 3 && points["o"].y == 1);
	print("entry view nested field: ({}, {})", points["o"].x, points["o"].y);

	// A first assignment inserts the key. A weak value does not retain its target.
	WatchedObject owner = WatchedObject();
	map<int64, weak<WatchedObject>> observers = map<int64, weak<WatchedObject>>();
	observers[1i64] = weak<WatchedObject>(owner);
	assert(observers[1i64].lock() == owner);
	owner = nullptr;
	assert(observers[1i64].expired());

	for (int v : m)
	{
		print("value foreach: {}", v);
	}
}
