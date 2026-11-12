// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitBackend.h"
#include "VeroScriptPlatform.h"
#include <string_view>

namespace VeroScript::Test
{
	inline bool SupportsAsmJitControlFlow(const FJitBackendDescription& Description) noexcept
	{
		// The x86 backend provides scalar leaves. Controlled CFG emitters belong
		// to the x64/ARM64 profiles, including when the factory is loaded dynamically.
		const std::string_view Architecture = GetHostPlatformInfo().Architecture;
		return Description.Id == "asmjit" && Description.bNativeCode &&
		    (Architecture == "x64" || Architecture == "arm64");
	}
}
