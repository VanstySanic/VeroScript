// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// constexpr values are checked at compile time; ordinary calls still run normally.
constexpr int triangular(int n)
{
	int result = 0;
	for (int i = 1; i <= n; ++i)
	{
		result += i;
	}
	return result;
}

struct Limits
{
	static constexpr int capacity = triangular(4);
};

template<int Count>
struct Buffer
{
	int values[Count];
};

// Each instance compiles only its selected branch; Count == 0 ends recursion.
template<int Count>
int static_triangular()
{
	if constexpr(Count == 0)
	{
		return 0;
	}
	else
	{
		return Count + static_triangular < Count - 1 > ();
	}
}

int main()
{
	constexpr int count = Limits::capacity;
	Buffer<count> buffer = Buffer<count>();
	assert(static_triangular < 4 > () == count);
	print(std::len(buffer.values)); // 10
	int input = 5;
	print(triangular(input)); // Ordinary runtime call: 15
	return 0;
}
