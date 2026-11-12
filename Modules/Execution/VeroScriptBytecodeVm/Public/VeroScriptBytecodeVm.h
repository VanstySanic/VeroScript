// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVmBackend.h"
#include "VeroScriptExecutionServices.h"

namespace VeroScript::BytecodeVm
{
	// Explicit services bind static factories to the actual owning Core.
	std::shared_ptr<const IVmBackendFactory> CreateFactory(const FVmHostServices& Host);
}
