// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "macro_exports_facade.vs";
#ifdef LOCAL_ONLY
#error private module macro leaked
#endif
int main()
{
	TRACE("public={} private-result={}", TWICE(PUBLIC_VALUE), module_value());
	return 0;
}
