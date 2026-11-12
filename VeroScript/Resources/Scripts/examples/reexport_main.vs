// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// reexport_main.vs - uses symbols re-exported through facade

import "reexport_facade.vs";

void main()
{
	print(std::strings::join(std::strings::split("a,b,c", ","), "-"));
}
