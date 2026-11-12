// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptConstantRequest.h"

namespace VeroScript
{
	namespace
	{
		thread_local std::shared_ptr<FConstantEvaluationState> ActiveRequest;
	}
	std::shared_ptr<FConstantEvaluationState> CurrentConstantEvaluationRequest()
	{
		return ActiveRequest;
	}
	FConstantEvaluationRequestScope::FConstantEvaluationRequestScope(const FConstantEvaluationLimits &Limits)
	    : Previous(ActiveRequest)
	{
		if (!ActiveRequest)
		{
			ActiveRequest = std::make_shared<FConstantEvaluationState>();
			ActiveRequest->Limits = Limits;
		}
	}
	FConstantEvaluationRequestScope::~FConstantEvaluationRequestScope()
	{
		ActiveRequest = std::move(Previous);
	}
}
