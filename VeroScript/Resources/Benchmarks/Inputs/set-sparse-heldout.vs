// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int work()
{
	set<int> values = set<int>();
	values.reserve(20001);
	int i = 0;
	while (i < 100003)
	{
		if ((i % 5) == 0)
		{
			values.add(i);
		}
		i++;
	}
	int state = 1;
	int total = 0;
	i = 0;
	while (i < 200000)
	{
		state = (state * 7 + 3) % 100003;
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
