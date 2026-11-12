// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"
#include "VeroScriptVmBackend.h"

#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	namespace DebugEval
	{
		// Runtime tag → script type name for debug expression binding.
		std::string TypeNameOfValue(const FValue& Value);

		struct FEvalResult
		{
			bool bOk = false;
			FValue Value = std::int32_t{0};
			std::string Error;
		};

		/**
		 * Compile `return (Expr);` as `__vs_dbg_eval(Params...)` on a scratch module
		 * and run it with ArgumentValues (by-value copies of frame locals).
		 * When ExpectedReturnType is non-empty, only that return type is tried
		 * (so SetLocal cannot silently coerce via the first successful candidate).
		 */
		FEvalResult EvaluateExpression(
			std::string_view Expression,
			const std::vector<std::string>& ParamNames,
			const std::vector<std::string>& ParamTypes,
			const std::vector<FValue>& ArgumentValues,
			std::string_view ExpectedReturnType = {},
			std::string_view DeclarationPrelude = {},
			std::string_view FileLabel = "<debug-eval>",
			std::shared_ptr<const IVmBackendFactory> VmFactory = {});
	}
}
