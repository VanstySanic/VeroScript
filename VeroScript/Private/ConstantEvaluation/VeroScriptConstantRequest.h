// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTemplate.h"

namespace VeroScript
{
	std::shared_ptr<FConstantEvaluationState> CurrentConstantEvaluationRequest();

	/** Preparation, binding and code generation consume one request budget. */
	class FConstantEvaluationRequestScope
	{
	public:
		explicit FConstantEvaluationRequestScope(const FConstantEvaluationLimits &);
		~FConstantEvaluationRequestScope();
		FConstantEvaluationRequestScope(const FConstantEvaluationRequestScope &) = delete;
		FConstantEvaluationRequestScope &operator=(const FConstantEvaluationRequestScope &) = delete;
	private:
		std::shared_ptr<FConstantEvaluationState> Previous;
	};
}
