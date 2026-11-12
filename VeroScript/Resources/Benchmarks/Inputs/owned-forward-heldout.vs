// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Record
{
public:
	int sum()
	{
		return x + y;
	}

	int x = 0;
	int y = 0;
};

int work(int n)
{
	array<Record> values = array<Record>();
	int i = 0;
	while (i < n)
	{
		Record p = Record();
		p.x = i % 97;
		p.y = (i % 13) * 3;
		values.push(p);
		i++;
	}

	int total = 0;
	for (int j = 0; j < n; j++)
	{
		total += values[j].sum();
	}
	return total;
}

void main()
{
	int checksum = 0;
	for (int r = 0; r < 5; r++)
	{
		checksum = (checksum + work(30000)) % 1000000007;
	}
	print(checksum);
}
