// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPackageManager.h"

int main()
{
	const auto transport = VeroScript::Packages::CreateHttpTransport();
	return transport ? 0 : 1;
}
