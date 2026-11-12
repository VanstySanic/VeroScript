// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int main(string[] args)
{
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); i = i + 1)
	{
		print(args[i]);
	}
	return 0;
}
