// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "string.vs";
int work(int n, int seed)
{
	string s = "";
	for (int i = 0; i < n; i++)
	{
		s += "abc-";
	}
	return to_int(len(s) + std::strings::find(s, "z") + seed);
}

void main()
{
	print(work(12000, 17));
}
