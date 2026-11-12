// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Run from this directory: veroscript macro_scope.vs
// Private macros remain local; imports expose only explicitly public definitions.

#define CALLER_ONLY 1
import "macro_scope_module.vs";
#ifdef MODULE_ONLY_VALUE
#error a macro from the imported module leaked into this file
#endif
int main()
{
	print("module value: {}", module_value());
	return 0;
}
