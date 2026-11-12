// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int work()
{
	set<int> values = set<int>();
	int state = -1;
	int total = 0;
	int i = 0;
	while (i < 200000)
	{
		state = (state * 7 + -3) % 257;
		if (values.has(state))
		{
			total++;
		}
		i++;
	}
	return total;
}
void main()
{
	int total = 0;
	for (int i = 0; i < 5; i++)
	{
		total += work();
	}
	print(total);
}
