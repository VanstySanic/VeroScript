// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Imported by macro_scope.vs. Its macro expands here, but is not exported.
// Run macro_scope.vs to see the module boundary in action.

#ifdef CALLER_ONLY
#error a macro from the importing file leaked into this module
#endif
#define MODULE_ONLY_VALUE 42
export int module_value()
{
	return MODULE_ONLY_VALUE;
}
