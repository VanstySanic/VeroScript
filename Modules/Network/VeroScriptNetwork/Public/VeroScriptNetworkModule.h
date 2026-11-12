// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNativeExtension.h"

namespace VeroScript::Network
{
	const char* GetImplementationIdentity() noexcept;
	std::uint32_t VEROSCRIPT_EXTENSION_CALL QueryModule(std::uint32_t Version, std::uint32_t Size, void* Descriptor);
} // namespace VeroScript::Network
