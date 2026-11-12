// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// path_helpers.vs - path standard-library helpers

import "path.vs";

void main()
{
	print(std::filesystem::basename("a/b/c.txt"));
	print(std::filesystem::dirname("a/b/c.txt"));
	print(std::filesystem::join_path("a/b", "c.txt"));
	assert(std::filesystem::join_path("a", "/b") == "/b");
	assert(std::filesystem::join_path("C:", "file") == "C:file");
}
