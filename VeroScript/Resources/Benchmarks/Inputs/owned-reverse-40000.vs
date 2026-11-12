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
	for (int i = 0; i < n; i++)
	{
		Record p = Record();
		p.x = (i + 17) & 255;
		p.y = (i * 7) & 127;
		values.push(p);
	}

	int total = 0;
	for (int i = n - 1; i >= 0; i--)
	{
		total += values[i].sum();
	}
	return total;
}

void main()
{
	int checksum = 0;
	for (int r = 0; r < 5; r++)
	{
		checksum = (checksum + work(40000)) % 1000000007;
	}
	print(checksum);
}
