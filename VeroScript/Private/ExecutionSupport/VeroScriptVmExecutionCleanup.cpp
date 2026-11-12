// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptExecutionCleanup.h"

#include <algorithm>

namespace VeroScript
{
	const FScriptError& VeroScriptVm::GetError() const
	{
		if (Error.MessageId == "diag.execution_state_reset_required") return Error;
		return ExecutionCleanup ? ExecutionCleanup->Primary : Error;
	}

	const std::vector<FScriptError>& VeroScriptVm::GetCleanupDiagnostics() const
	{
		static const std::vector<FScriptError> Empty;
		return ExecutionCleanup ? ExecutionCleanup->Diagnostics : Empty;
	}

	bool VeroScriptVm::RequiresReset() const noexcept
	{
		return bExecutionResetRequired || (ExternalExecutionResetRequired && *ExternalExecutionResetRequired) || (ExecutionCleanup && ExecutionCleanup->bRequiresReset);
	}

	void VeroScriptVm::BeginExecutionCleanup()
	{
		if (ExecutionCleanup || !bHasError) return;
		try
		{
			auto Cleanup = std::make_shared<FExecutionCleanupState>();
			Cleanup->Primary = Error;
			bool bFoundPrimary = false;
			for (const auto& Frame : Frames)
			{
				for (const auto& Continuation : Frame.FinallyContinuations)
				{
					if (Continuation.Kind == FCallFrame::EFinallyContinuationKind::FatalError)
					{
						Cleanup->Primary = Continuation.Error;
						bFoundPrimary = true;
						break;
					}
				}
				if (bFoundPrimary)
				{
					break;
				}
			}
			Cleanup->Governance.Begin(
			    {FExecutionCleanupState::MaxInstructions, FExecutionCleanupState::TimeoutMilliseconds},
			    std::chrono::steady_clock::time_point::max());
			ExecutionCleanup = std::move(Cleanup);
			if (ExternalExecutionCleanup)
			{
				*ExternalExecutionCleanup = ExecutionCleanup;
			}
			RecordExecutionCleanupFailure(Error);
			for (const auto& Frame : Frames)
			{
				for (const auto& Continuation : Frame.FinallyContinuations)
				{
					if (Frame.LastInstructionOffset >= Continuation.FinallyBeginOffset && Frame.LastInstructionOffset < Continuation.FinallyEndOffset)
					{
						// An already-running finally was interrupted; restarting its
						// current operation could replay effects or resume a partial call.
						MarkExecutionResetRequired();
					}
				}
			}
		}
		catch (...)
		{
			// No reserve means script cleanup cannot be promised, even if diagnostic
			// storage is unavailable. The first termination remains authoritative.
			MarkExecutionResetRequired();
		}
	}

	void VeroScriptVm::PreserveExecutionCleanupError(const FScriptError& Earlier)
	{
		if (!ExecutionCleanup || Earlier.Message.empty() || Earlier.bUncaughtScriptException) return;
		const auto& Id = ExecutionCleanup->Primary.MessageId;
		if (Id != "diag.runtime_execution_instruction_limit_exceeded" && Id != "diag.execution_time_limit_exceeded" && Id != "diag.execution_cancelled_by_host")
		{
			return;
		}
		const FScriptError Interruption = ExecutionCleanup->Primary;
		ExecutionCleanup->Primary = Earlier;
		RecordExecutionCleanupFailure(Interruption);
	}

	bool VeroScriptVm::CheckExecutionCleanup()
	{
		if (ExecutionCleanup->bExhausted) return false;
		if (std::chrono::steady_clock::now() < ExecutionCleanup->Governance.Deadline) return true;
		ExecutionCleanup->bExhausted = true;
		MarkExecutionResetRequired();
		ReportErrorId("diag.execution_cleanup_time_limit_exceeded");
		return false;
	}

	bool VeroScriptVm::ChargeExecutionCleanup(const std::uint64_t Units)
	{
		if (!CheckExecutionCleanup()) return false;
		if (ExecutionCleanup->Governance.Charge(Units, FExecutionCleanupState::MaxInstructions)) return true;
		ExecutionCleanup->bExhausted = true;
		MarkExecutionResetRequired();
		ReportErrorId("diag.execution_cleanup_instruction_limit_exceeded");
		return false;
	}

	void VeroScriptVm::RecordExecutionCleanupFailure(const FScriptError& Failure)
	{
		if (!ExecutionCleanup || Failure.Message.empty()) return;
		const auto& Primary = ExecutionCleanup->Primary;
		if (Failure.MessageId == Primary.MessageId && Failure.Message == Primary.Message) return;
		MarkExecutionResetRequired();
		try
		{
			auto& Diagnostics = ExecutionCleanup->Diagnostics;
			if (Diagnostics.size() >= FExecutionCleanupState::MaxDiagnostics) return;
			if (std::any_of(Diagnostics.begin(), Diagnostics.end(), [&](const FScriptError& Existing)
			{
				return Existing.MessageId == Failure.MessageId && Existing.Message == Failure.Message;
			}))
			{
				return;
			}
			Diagnostics.push_back(Failure);
		}
		catch (...)
		{
			// The reset latch also covers a secondary diagnostic that could not fit.
		}
	}

	void VeroScriptVm::MarkExecutionResetRequired() noexcept
	{
		bExecutionResetRequired = true;
		if (ExternalExecutionResetRequired)
		{
			*ExternalExecutionResetRequired = true;
		}
		if (ExecutionCleanup)
		{
			ExecutionCleanup->bRequiresReset = true;
		}
	}

	bool VeroScriptVm::SkipScriptCleanup() const noexcept
	{
		return bExecutionStateInvalid || (ExecutionCleanup ? ExecutionCleanup->bExhausted : RequiresReset());
	}

	bool VeroScriptVm::RejectExecutionReuse()
	{
		if (!RequiresReset()) return false;
		bHasError = false;
		Error = {};
		ReportErrorId("diag.execution_state_reset_required");
		return true;
	}
}
