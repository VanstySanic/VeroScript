// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTemplate.h"

namespace VeroScript
{
	using FConstantValue = FTemplateConstant;
	using FConstantEvaluationResult = FTemplateEvaluationResult;
	using FConstantEvaluationContext = FTemplateEvaluationContext;

	/** Same exact arithmetic, binding and budgets as value template arguments. */
	inline FConstantEvaluationResult EvaluateConstant(
		const FExpr &Expression, const FConstantEvaluationContext &Context = {})
	{
		return EvaluateTemplateConstant(Expression, Context);
	}

	/** Produces an exact typed literal for normal storage/backend lowering. */
	std::unique_ptr<FExpr> MakeConstantExpression(const FConstantValue &, const FSourceLocation & = {});

	/** Clone for standalone emitters; never mutates a prepared/shared program. */
	std::unique_ptr<struct FProgram> CloneProgramWithConstantValues(
		const struct FProgram &, const struct FBytecodeModule &, std::string &Error);
}
