// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <iostream>

extern "C" bool CheckInstalledSharedGui();

int main()
{
	if (!CheckInstalledSharedGui())
	{
		std::cerr << "Installed shared GUI host failed\n";
		return 1;
	}
	std::cout << "Installed shared GUI host passed\n";
	return 0;
}
