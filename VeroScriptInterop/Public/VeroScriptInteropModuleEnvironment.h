// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptInterop.h"
#include "VeroScriptModuleEnvironment.h"

namespace VeroScript::Interop
{
	// The environment is resolved/published by the matching native Core SDK.
	// C callers retain opaque handles; no STL, provider or registry layout crosses C.
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL CreateModuleEnvironment(
		const std::shared_ptr<const FModuleEnvironment>& Environment,
		vs_interop_module_environment** OutEnvironment);
}
