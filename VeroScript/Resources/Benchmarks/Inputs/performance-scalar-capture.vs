// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int work(int n, int seed)
{
	int delta = seed % 7 + 1;
	auto getter = [delta](int value) -> int
	{
		return (value * 3 + delta) % 65521;
	};
	int value = seed;
	for (int index = 0; index < n; ++index)
	{
		value = getter(value);
	}
	return value;
}

void main()
{
	print(work(100000, 17));
}
