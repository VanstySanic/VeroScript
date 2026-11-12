// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// constructor_initializers.vs — constructor initializer lists and const fields
// Run from this directory: veroscript constructor_initializers.vs

int initialization_trace = 0;

int mark_initialization(int digit)
{
	initialization_trace = initialization_trace * 10 + digit;
	return digit;
}

struct Point
{
	Point(int initialX, int initialY);

	int sum() const
	{
		return x + y;
	}

	const int x;
	const int y;
};

Point::Point(int initialX, int initialY) : x(initialX), y(initialY)
{
}

class BaseSettings
{
public:
	BaseSettings(int initialValue) : base_value(initialValue)
	{
	}

	int base() const
	{
		return base_value;
	}

protected:
	const int base_value;
};

class Settings : public BaseSettings
{
public:
	Settings() // The written order is intentionally different. The direct base runs
	// first, then fields run in declaration order, and the body runs last.
	: third(mark_initialization(3)), BaseSettings(mark_initialization(1)), second(mark_initialization(2))
	{
		initialization_trace = initialization_trace * 10 + 4;
	}

	int total() const
	{
		return first + second + third + base();
	}

	// A const field with a declaration initializer is already complete and
	// must not appear again in a constructor initializer list.
	const int first = 1;

	// A const instance field without a declaration initializer is initialized
	// exactly once by every constructor that names it in its initializer list.
	const int second;
	int third;
};

int main()
{
	Point point = Point(5, 7);
	Settings settings = Settings();

	assert(point.sum() == 12);
	assert(settings.total() == 7);
	assert(initialization_trace == 1234);

	print("point sum: {}", point.sum());
	print("settings total: {}", settings.total());
	print("initialization order: {}", initialization_trace);
	return 0;
}
