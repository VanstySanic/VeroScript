// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

enum class Growth
{
	Adaptive,
	Fixed
};

template < typename T, int Count = 4, Growth Policy = Growth::Adaptive > struct Buffer
{
	T values[Count];

	int capacity() const
	{
		return Count;
	}

	Growth growth() const
	{
		return Policy;
	}
};

template<typename T, T Value>
T constant()
{
	return Value;
}

void main()
{
	const int Size = 2 + 3;
	Buffer<int, Size, Growth::Fixed> buffer = Buffer<int, Size, Growth::Fixed>();
	buffer.values[0] = 12;
	print(buffer.capacity());
	print(buffer.values[0]);
	print(buffer.growth() == Growth::Fixed);
	print(constant < uint64, 18446744073709551615u64 > ());
}
