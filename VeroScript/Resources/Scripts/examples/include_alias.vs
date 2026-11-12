// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// include_alias.vs - #include is a re-exporting module-import spelling alias
// Run from this directory: veroscript include_alias.vs
//
// This does not copy source text or leak macros. It forwards only declarations
// exported by the target module. Module identity provides duplicate suppression.

#include "math.vs"
void main()
{
	print("clamped: {}", std::clamp_int(15, 0, 10));
}
