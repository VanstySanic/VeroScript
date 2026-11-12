// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNativeExtension.h"

namespace VeroScript::Json
{
	const char* GetImplementationIdentity() noexcept;
	// Unique static query; the dynamic wrapper exports the common loader symbol.
	std::uint32_t VEROSCRIPT_EXTENSION_CALL QueryModule(std::uint32_t Version, std::uint32_t Size, void* Descriptor);
} // namespace VeroScript::Json
