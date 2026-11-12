// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"

#include <stdexcept>

namespace VeroScript
{
	FNumericOperationContext VeroScriptVm::GetNumericOperationContext()
	{
		auto Work = GetMemoryWork();
		return {NumericLimits, {Work.Check, Work.UserData}};
	}
	void VeroScriptVm::SetNumericLimits(FNumericLimits Limits)
	{
		if (bMemoryExecutionActive)
		{
			throw std::logic_error("cannot change active numeric limits");
		}
		FNumericLimits Normalized;
		FNumericError E;
		if (!Numerics::ValidateLimits(Limits, Normalized, E))
		{
			throw std::invalid_argument(E.MessageId);
		}
		NumericLimits = Normalized;
	}
	FNumericLimits VeroScriptVm::GetNumericLimits() const
	{
		return NumericLimits;
	}
} // namespace VeroScript
