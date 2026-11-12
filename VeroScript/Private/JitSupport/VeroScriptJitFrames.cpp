// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptVmCleanup.h"

#include <algorithm>
#include <utility>

namespace VeroScript
{
	void VeroScriptVm::PopRuntimeFrame() noexcept
	{
		if (!Frames.empty() && Frames.back().DiagnosticToken)
		{
			EndFunctionDiagnostic(Frames.back().DiagnosticToken);
			Frames.back().DiagnosticToken = 0;
		}
		ExecutionDetail::PopFrame(Frames, ActiveBackendFrameCredits);
	}

	void VeroScriptVm::ClearRuntimeFrames() noexcept
	{
		AbortLocalStaticInitializers(0);
		for (auto Frame = Frames.rbegin(); Frame != Frames.rend(); ++Frame)
		{
			EndFunctionDiagnostic(Frame->DiagnosticToken);
		}
		Frames.clear();
		ActiveBackendFrameCredits = 0;
	}

	std::size_t VeroScriptVm::GetLogicalCallDepth() const noexcept
	{
		// A backend invocation and its materialized language frame represent one call.
		return Frames.size() + ActiveJitCallDepth - ActiveBackendFrameCredits;
	}

	bool VeroScriptVm::EnterJitRuntimeFrame(const FBytecodeFunction* const Function,
		const std::size_t LocalCount, const FValue* const Arguments,
		const std::uint8_t ArgumentCount, const FObjectHandle& ThisObject,
		const std::uint64_t Identity)
	{
		bJitErrorLocationPinned = false;
		FCallFrame Frame;
		if (!FreeFrames.empty())
		{
			Frame = std::move(FreeFrames.back());
			FreeFrames.pop_back();
			++JitLinearFrameReuses;
		}
		Frame.Function = Function;
		Frame.DiagnosticToken = 0;
		Frame.JitRuntimeFrameIdentity = Identity;
		Frame.InstructionPointer = 0;
		Frame.LastInstructionOffset = 0;
		Frame.StackBase = OperandStack.size();
		Frame.FinallyContinuations.clear();
		if (Frame.Locals.size() == LocalCount)
		{
			std::fill(
				Frame.Locals.begin(), Frame.Locals.end(), FValue{std::int32_t{0}});
		}
		else
		{
			Frame.Locals.assign(LocalCount, std::int32_t{0});
		}
		for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
		{
			Frame.Locals[Index] = Arguments[Index];
		}
		Frame.ThisObject = ThisObject;
		Frame.ReturnValue = std::int32_t{0};
		Frame.bHasReturn = false;
		Frame.bExpressionEval = false;
		Frame.bDisableNestedJit = false;
		Frame.PrepareInlineCaches(ActiveModule, Function, ProgramGeneration);
		Frames.push_back(std::move(Frame));
		if (Identity != 0)
		{
			++ActiveBackendFrameCredits;
		}
		return true;
	}

	bool VeroScriptVm::LeaveJitRuntimeFrame(const FBytecodeFunction* const Expected)
	{
		// Nested Call/Run may leave extra frames after a failed StopFrameDepth unwind.
		// Tear them down through the language ownership path; raw pop_back() would
		// skip script destructors and any values parked by a finally continuation.
		std::size_t ExpectedDepth = Frames.size();
		while (ExpectedDepth != 0
			&& Frames[ExpectedDepth - 1].Function != Expected)
		{
			--ExpectedDepth;
		}
		if (ExpectedDepth == 0)
		{
			ReportError("JIT linear frame: leave mismatch");
			return false;
		}
		bool bNestedCleanupOk = true;
		if (Frames.size() > ExpectedDepth)
		{
			bNestedCleanupOk = DiscardExecutionAbove(
				ExpectedDepth, Frames[ExpectedDepth - 1].StackBase, {});
		}
		if (Frames.empty() || !Frames.back().Function || Frames.back().Function != Expected)
		{
			ReportError("JIT linear frame: leave mismatch");
			return false;
		}
		// Move locals out before nested destructor Run() can reallocate Frames.
		FCallFrame Finished = std::move(Frames.back());
		PopRuntimeFrame();
		const bool bPrevTeardownActive = bTeardownErrorActive;
		const FBytecodeFunction* const PrevTeardownFunction = TeardownErrorFunction;
		const std::size_t PrevTeardownIp = TeardownErrorIp;
		const std::size_t PrevTeardownDepth = TeardownFrameDepth;
		bTeardownErrorActive = true;
		TeardownErrorFunction = Finished.Function;
		TeardownErrorIp = ActiveInstructionStart;
		TeardownFrameDepth = Frames.size();
		const bool bHadPendingError = bHasError;
		FScriptError PendingError = Error;
		bHasError = false;
		Error = {};
		bool bCleanupOk = bNestedCleanupOk;
		FScriptError FirstCleanupError;
		const auto RecordCleanupFailure = [&]()
		{
			VmDetail::RecordCleanupFailure(bCleanupOk, FirstCleanupError, bHasError, Error);
		};
		std::vector<FValue>& LocalsToRelease = Finished.Locals;
		if (!ReleaseFinallyContinuations(Finished, {}))
		{
			RecordCleanupFailure();
		}
		if (!ReleaseValueList(LocalsToRelease.data(), LocalsToRelease.size(), {}))
		{
			RecordCleanupFailure();
		}
		if (Finished.ThisObject)
		{
			if ((!bCleanupOk || bHadPendingError || bHasError || bHasPendingScriptException)
				&& !DestroyAbortedConstruction(Finished, {}))
			{
				RecordCleanupFailure();
			}
			FValue ReceiverOwner = std::move(Finished.ThisObject);
			Finished.ThisObject.reset();
			if (!ReleaseValue(ReceiverOwner, {}))
			{
				RecordCleanupFailure();
			}
		}
		bCleanupOk = VmDetail::FinishCleanup(bCleanupOk, bHadPendingError, std::move(PendingError),
											 std::move(FirstCleanupError), bExitRequested, bHasError, Error);
		bTeardownErrorActive = bPrevTeardownActive;
		TeardownErrorFunction = PrevTeardownFunction;
		TeardownErrorIp = PrevTeardownIp;
		TeardownFrameDepth = PrevTeardownDepth;
		Finished.Function = nullptr;
		Finished.JitRuntimeFrameIdentity = 0;
		Finished.ThisObject.reset();
		Finished.bDisableNestedJit = false;
		// Retain the zeroed local slots and their capacity for the next VM/linear
		// frame. Avoid pinning an exceptional giant frame for the VM lifetime.
		if (VmDetail::CanRecycleFrame(FreeFrames.size(), Finished.Locals.capacity()))
		{
			FreeFrames.push_back(std::move(Finished));
		}
		return bCleanupOk;
	}
}
