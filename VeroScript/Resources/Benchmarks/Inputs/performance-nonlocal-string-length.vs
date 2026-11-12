// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Text
{
public:
	string value = "";
};

int work(int n, int seed)
{
	string value = "x";
	while (len(value) < seed)
	{
		value += value;
	}
	Text text = Text();
	text.value = value;
	int total = 0;
	for (int index = 0; index < n; ++index)
	{
		total = to_int((total + len(text.value)) % 1000003);
	}
	return total;
}

void main()
{
	print(work(20000, 65536));
}
