// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptInterop.h"
#include "VeroScriptAnnotationProcessing.h"

namespace VeroScript::Interop
{
	// Native SDK bridge. The service retains the frozen registry and its factories;
	// C/managed consumers pass only the opaque handle, never AST or STL callbacks.
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL CreateAnnotationProcessingService(
		const FAnnotationProcessingOptions&, vs_interop_annotation_service**);
}
