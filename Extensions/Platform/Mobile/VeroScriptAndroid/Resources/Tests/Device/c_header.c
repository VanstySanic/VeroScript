// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <VeroScriptInterop.h>
_Static_assert(sizeof(void*) == 8, "The mobile release uses a 64-bit ABI");
uint32_t device_c_abi_version(void)
{
	return vs_interop_get_abi_version();
}
