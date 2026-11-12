// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Direct import of export using / export typedef declarations.
import "alias_exports_module.vs";

// This lookup directive belongs to this module and is not exported.
using namespace ExportedAliases;

int main()
{
	Count total = add(20, 22);
	Label label = "direct";
	assert(total == 42 && label == "direct");
	print("alias exports: {} {}", label, total);
	return 0;
}
