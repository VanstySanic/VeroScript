// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// brace_initialization.vs — constructor and typed container brace initialization
// Run from this directory: veroscript brace_initialization.vs

using Ints = array<int>;
using MoreInts = Ints;
using Scores = map<string, int>;

struct Point
{
	Point()
	{
	}

	Point(int initialX, int initialY)
	{
		x = initialX;
		y = initialY;
	}

	int sum()
	{
		return x + y;
	}

	int x = 0;
	int y = 0;
};

struct Defaults
{
	Ints values = {2, 3};
	Scores scores = {"answer": 42};
};

MoreInts global_values = {11, 13};
Scores global_scores = {"published": 1};

Point make_point()
{
	return Point{7, 8};
}

void main()
{
	// Variable direct initialization: both no-argument and argument forms use
	// the same constructor overloads as Point{} / Point{3, 4}.
	Point origin();
	Point point(3, 4);

	array<int> direct = array<int>{17, 19};
	Ints aliasBare = {23, 29};
	MoreInts aliasExplicit = MoreInts{31, 37};
	map<string, int> scores = map<string, int>{"high": 99};
	set<int> unique = set<int>{1, 1, 2};
	Defaults defaults = Defaults{};

	{
		using LocalInts = array<int>;
		LocalInts localBare = {41, 43};
		LocalInts localExplicit = LocalInts{47, 53};
		print("local aliases: {}, {}", localBare[1], localExplicit[0]);
	}

	print("default point: ({}, {})", origin.x, origin.y);
	print("constructed point sum: {}", point.sum());
	print("returned point sum: {}", make_point().sum());
	print("postfix chain sum: {}", Point{5, 6}.sum());
	print("typed array: {}, {}", direct[0], direct[1]);
	print("aliases: {}, {}", aliasBare[1], aliasExplicit[0]);
	print("typed map: {}", scores["high"]);
	print("set size: {}", len(unique));
	print("field defaults: {}, {}", defaults.values[1], defaults.scores["answer"]);
	print("globals: {}, {}", global_values[1], global_scores["published"]);
}
