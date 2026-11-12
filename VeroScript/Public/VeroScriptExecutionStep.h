// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptExecutionGovernance.h"
#include <algorithm>

namespace VeroScript::ExecutionDetail
{
	/** One authoritative instruction boundary for Core helpers and VM backends. */
	template <typename TFrames, typename TPoll, typename TCleanup, typename TQuota>
	bool CheckInstruction(const FBytecodeModule& Module, const FBytecodeFunction* Function,
	    std::size_t Offset, std::size_t& ActiveOffset, TFrames& Frames,
	    FExecutionGovernanceState& Governance, const FExecutionLimits& Limits, bool bCleanup,
	    TPoll&& Poll, TCleanup&& Cleanup, TQuota&& Quota)
	{
		const auto& Code = Function ? Function->Code : Module.InitCode;
		// Visiting the end to release a frame is a poll, not an instruction.
		if (Offset >= Code.size()) return Poll();
		const auto& Markers = Function ? Function->PauseMarkerOffsets : Module.InitPauseMarkerOffsets;
		const bool Pause = static_cast<EOpcode>(Code[Offset]) == EOpcode::Nop && std::binary_search(Markers.begin(), Markers.end(), Offset);
		ActiveOffset = Offset;
		if (!Frames.empty())
		{
			Frames.back().LastInstructionOffset = Offset;
		}
		if (bCleanup) return Cleanup(Pause);
		if (!Pause && !Governance.Charge(1, Limits.MaxInstructions))
		{
			Quota();
			return false;
		}
		return Poll();
	}
}
