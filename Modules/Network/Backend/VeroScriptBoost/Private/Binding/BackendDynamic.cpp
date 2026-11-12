// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoostModule.h"
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(std::uint32_t V, std::uint32_t S, void* O)
{
	return VeroScript::Boost::QueryModule(V, S, O);
}
