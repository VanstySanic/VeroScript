// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int work(int n, int seed)
{
	int64 state = seed;
	int64 total = 0;
	for (int i = 0; i < n; i++)
	{
		state = (state * 75 + 74) % 65521;
		total = (total + state) % 1000003;
	}
	return to_int(total + state);
}

void main()
{
	print(work(200000, 17));
}
