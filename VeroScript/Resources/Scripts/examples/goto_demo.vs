// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// goto_demo.vs — labels and goto (same scope depth only)

void main()
{
	int n = 0;
	again : n = n + 1;
	print(n);
	if (n >= 3)
	{
		return;
	}
	goto again;
}
