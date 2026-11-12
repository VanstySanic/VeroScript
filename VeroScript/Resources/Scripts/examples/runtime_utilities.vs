// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Standard runtime helpers: range, filesystem, and clock access.
// Run from this directory: veroscript runtime_utilities.vs
// Requires file-read permission for cwd(); the directory and clock are host-dependent.

import "io.vs";

void main()
{
	for (int i : std::range(3))
	{
		std::print(i);
	}
	std::print(std::filesystem::cwd());
	std::print(std::chrono::now() > 0.0);
}
