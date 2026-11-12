// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptExecutionGovernance.h"
#include <algorithm>
#include <limits>

namespace VeroScript
{
	void FExecutionGovernanceState::Begin(const FExecutionLimits& Limits,
	    const std::chrono::steady_clock::time_point ExternalDeadline)
	{
		ExecutedInstructions = 0;
		Deadline = ExternalDeadline;
		if (Limits.TimeoutMilliseconds == 0) return;
		const auto Now = std::chrono::steady_clock::now();
		const auto Remaining = std::chrono::steady_clock::time_point::max() - Now;
		const auto MaximumMs = std::chrono::duration<long double, std::milli>(Remaining).count();
		if (Limits.TimeoutMilliseconds > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
		    MaximumMs <= 0 || static_cast<long double>(Limits.TimeoutMilliseconds) >= MaximumMs)
		{
			return;
		}
		Deadline = std::min(Deadline,
		    Now + std::chrono::milliseconds(static_cast<std::int64_t>(Limits.TimeoutMilliseconds)));
	}

}
