// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// ref_foreach.vs — range-for aliases and index alias arguments
// Run from this directory: veroscript ref_foreach.vs
//
// for (T x : xs)     — by-value (structs are cloned; mutation is local)
// `&` declares an alias binding:
//   T& x
//   const T& x
//   bump(&xs[i])
// for (T& x : xs)  — in-place alias into array / T[N] / deque / map values
// bump(&xs[i])      — pass an element by alias to an alias parameter
// bump(&m[k])       — pass a map entry by alias

struct Point
{
	int x = 0;
};

void bump(Point& p)
{
	p.x = p.x + 1;
}

void bump_int(int& x)
{
	x = x + 1;
}

void main()
{
	int source = 1;
	int& alias = source;
	alias = 2;
	const int& readonly = source;
	print("local aliases: {}, {}", source, readonly);

	array<Point> pts = array<Point>();
	pts.push(Point());
	pts.push(Point());

	// Alias iteration mutates each element in place.
	for (Point& p : pts)
	{
		p.x = p.x + 3;
	}
	print("after alias range-for: {}, {}", pts[0].x, pts[1].x);

	// Index alias argument (same alias mechanism as range-for).
	bump(&pts[0]);
	print("after bump(&pts[0]): {}", pts[0].x);

	// By-value range-for still copies; assignment does not write back.
	for (Point p : pts)
	{
		p.x = 99;
	}
	print("after value range-for: {}", pts[0].x);

	// Native T[N]: alias range-for aliases the local array (not a temporary copy).
	int nums[2] = {1, 2};
	for (int& n : nums)
	{
		n = n + 10;
	}
	print("native T[N] alias range-for: {}, {}", nums[0], nums[1]);

	// deque: alias range-for aliases elements.
	deque<int> d = deque<int>();
	d.push_back(1);
	d.push_back(2);
	for (int& x : d)
	{
		x = x + 10;
	}
	print("deque alias range-for: {}, {}", d[0], d[1]);

	// map: &m[k] / for (V& v : m) write through; see pair_map.vs for entries.
	map<string, int> m = map<string, int>();
	m["a"] = 40;
	bump_int(&m["a"]);
	for (int& v : m)
	{
		v = v + 1;
	}
	print("map alias entry + foreach: {}", m["a"]);

	// keys + alias when both key and value are needed
	for (string k : m.keys())
	{
		bump_int(&m[k]);
	}
	print("map keys + &m[k]: {}", m["a"]);
}
