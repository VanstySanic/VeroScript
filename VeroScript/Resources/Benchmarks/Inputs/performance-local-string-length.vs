// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int work(int n, int seed)
{
	string s = "x";
	while (len(s) < seed)
	{
		s += s;
	}
	int total = 0;
	for (int i = 0; i < n; i++)
	{
		total = to_int((total + len(s)) % 1000003);
	}
	return total;
}

void main()
{
	print(work(20000, 65536));
}
