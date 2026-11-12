// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNativeExtension.h"
namespace VeroScript::Curl
{
	const char* GetImplementationIdentity() noexcept;
	std::uint32_t VEROSCRIPT_EXTENSION_CALL QueryModule(std::uint32_t Version, std::uint32_t Size, void* Descriptor);
}
extern "C" std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_curl_query(std::uint32_t, std::uint32_t, void*);
