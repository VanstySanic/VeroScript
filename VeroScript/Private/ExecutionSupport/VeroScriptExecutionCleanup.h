// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptExecutionGovernance.h"

namespace VeroScript
{
	// Shared only by synchronous cleanup helpers, never by independent workers.
	struct FExecutionCleanupState
	{
		static constexpr std::uint64_t MaxInstructions = 16384;
		static constexpr std::uint64_t TimeoutMilliseconds = 50;
		static constexpr std::size_t MaxDiagnostics = 8;

		FScriptError Primary;
		std::vector<FScriptError> Diagnostics;
		FExecutionGovernanceState Governance;
		bool bExhausted = false;
		bool bRequiresReset = false;
	};
}
