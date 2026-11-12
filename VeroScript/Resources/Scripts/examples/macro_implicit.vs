// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Run: veroscript --implicit-import macro_exports_facade.vs macro_implicit.vs
// The host supplies the import; this consumer needs no import declaration.
#ifdef LOCAL_ONLY
#error private module macro leaked
#endif
int main()
{
	TRACE("public={} private-result={}", TWICE(PUBLIC_VALUE), module_value());
	return 0;
}
