// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCurlModule.h"
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(std::uint32_t V, std::uint32_t S, void* O)
{
	return VeroScript::Curl::QueryModule(V, S, O);
}
