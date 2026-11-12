// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpModule.h"

extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(
	std::uint32_t Version, std::uint32_t Size, void* Descriptor)
{
	return VeroScript::Http::QueryModule(Version, Size, Descriptor);
}
