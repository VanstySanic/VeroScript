// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptInterop.h"
#include "VeroScriptCompileProcessing.h"

namespace VeroScript::Interop
{
	// Matching native Core/Interop SDKs are required. C callers only pass the
	// opaque service; AST, registry and STL callbacks remain in native code.
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL CreateCompileProcessingService(
		const FCompileProcessingOptions& Options, vs_interop_compile_processing_service** OutService);
}
