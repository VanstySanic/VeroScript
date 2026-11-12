// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Reach the same aliases through an export import facade.
import "alias_exports_facade.vs";

int main()
{
	ExportedAliases::Count total = ExportedAliases::add(40, 2);
	ExportedAliases::Label label = "facade";
	assert(total == 42 && label == "facade");
	print("alias reexports: {} {}", label, total);
	return 0;
}
