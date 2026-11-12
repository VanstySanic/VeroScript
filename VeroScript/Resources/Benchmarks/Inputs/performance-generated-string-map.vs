// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int work(int n, int seed)
{
	map<string, int> a = map<string, int>();
	for (int i = 0; i < 1021; i++)
	{
		a[to_string(i)] = i % 97;
	}
	int s = seed;
	int total = 0;
	for (int i = 0; i < n; i++)
	{
		s = (s * 75 + 74) % 65521;
		string k = to_string(s % 1021);
		int v = (a[k] + s) % 1009;
		a[k] = v;
		total += v;
	}
	return total;
}

void main()
{
	print(work(30000, 17));
}
