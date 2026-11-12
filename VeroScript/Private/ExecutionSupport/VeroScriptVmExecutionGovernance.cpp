// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptExecutionStep.h"
#include <algorithm>
#include <stdexcept>

namespace VeroScript
{
	void VeroScriptVm::RequireIdleExecutionConfiguration() const
	{
		if (ActiveModule || ActiveJitCallDepth || !Frames.empty())
		{
			throw std::logic_error("execution limits, permissions and control services are immutable while executing");
		}
	}

	bool VeroScriptVm::CheckExecutionStep(const FBytecodeFunction* Function, const std::size_t Offset)
	{
		return ExecutionDetail::CheckInstruction(*ActiveModule, Function, Offset,
		    ActiveInstructionStart, Frames, ExecutionGovernance, ExecutionLimits,
		    static_cast<bool>(ExecutionCleanup), [&]()
		{
			return CheckExecutionGovernance();
		}, [&](bool Pause)
		{
			return Pause ? CheckExecutionCleanup() : ChargeExecutionCleanup(1);
		}, [&]()
		{
			ReportErrorId("diag.runtime_execution_instruction_limit_exceeded");
			BeginExecutionCleanup();
		});
	}

	bool VeroScriptVm::ChargeExecutionWork(const std::uint64_t Units)
	{
		if (ExecutionCleanup) return ChargeExecutionCleanup(Units);
		if (!CheckExecutionGovernance()) return false;
		if (!ExecutionGovernance.Charge(Units, ExecutionLimits.MaxInstructions))
		{
			ReportErrorId("diag.runtime_execution_instruction_limit_exceeded");
			BeginExecutionCleanup();
			return false;
		}
		return true;
	}
}
