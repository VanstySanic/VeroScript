// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptLocalStaticInternal.h"

#include <algorithm>
#include <limits>

namespace VeroScript
{
	std::vector<VeroScriptVm::FDebugLocalStaticView> VeroScriptVm::DebugGetLocalStatics(
		const std::size_t FrameIndex) const
	{
		std::vector<FDebugLocalStaticView> Result;
		if (FrameIndex >= Frames.size() || !Frames[FrameIndex].Function) return Result;
		const auto& Frame = Frames[FrameIndex];
		for (const auto& Initializer : Frame.Function->LocalStaticInitializers)
		{
			if (Frame.InstructionPointer < Initializer.BeginOffset
				|| Frame.InstructionPointer >= Initializer.ScopeEndOffset)
			{
				continue;
			}
			const auto Slot = Initializer.GlobalSlot;
			const auto State = LocalStatics && Slot < LocalStatics->States.size()
				? LocalStatics->States[Slot] : ELocalStaticInitialization::Uninitialized;
			Result.push_back({Initializer.Name,
				State == ELocalStaticInitialization::Initialized && Slot < Globals.size() ? &Globals[Slot] : nullptr,
				State == ELocalStaticInitialization::Initializing});
		}
		return Result;
	}

	bool VeroScriptVm::CheckLocalStaticAccess(const std::uint16_t Slot, const FSourceLocation& Location)
	{
		if (!ActiveModule || Slot >= ActiveModule->GlobalMetadata.size()
			|| !ActiveModule->GlobalMetadata[Slot].bLocalStatic)
		{
			return true;
		}
		if (!LocalStatics || Slot >= LocalStatics->States.size()
			|| LocalStatics->States[Slot] != ELocalStaticInitialization::Initialized)
		{
			ReportErrorId("diag.local_static_not_initialized", Location);
			return false;
		}
		return true;
	}

	bool VeroScriptVm::BeginLocalStatic(const std::uint16_t Slot, bool& bSkip,
		const FSourceLocation& Location)
	{
		bSkip = false;
		if (!ActiveModule || Frames.empty() || !Frames.back().Function || Slot >= Globals.size()
			|| Slot >= ActiveModule->GlobalMetadata.size() || !ActiveModule->GlobalMetadata[Slot].bLocalStatic)
		{
			ReportError("invalid local static initialization", Location);
			return false;
		}
		const auto& Function = *Frames.back().Function;
		if (LocalStatics && Slot < LocalStatics->States.size()
			&& LocalStatics->States[Slot] == ELocalStaticInitialization::Initialized)
		{
			bSkip = true;
			return true;
		}
		const auto Range = std::find_if(Function.LocalStaticInitializers.begin(), Function.LocalStaticInitializers.end(),
			[&](const FBytecodeLocalStaticInitializer& Candidate)
			{
				return Candidate.GlobalSlot == Slot && Candidate.BeginOffset == ActiveInstructionStart;
			});
		if (Range == Function.LocalStaticInitializers.end())
		{
			ReportError("missing local static initialization range", Location);
			return false;
		}
		if (!LocalStatics)
		{
			auto State = std::make_shared<FLocalStaticState>();
			State->States.resize(Globals.size(), ELocalStaticInitialization::Uninitialized);
			LocalStatics = std::move(State);
		}
		if (Slot >= LocalStatics->States.size())
		{
			ReportError("local static state does not match the active program", Location);
			return false;
		}
		const auto State = LocalStatics->States[Slot];
		if (State == ELocalStaticInitialization::Initializing)
		{
			ReportErrorId("diag.local_static_reentrant", Location, {Range->Name});
			return false;
		}
		if (LocalStatics->bDestroying || RequiresReset())
		{
			ReportErrorId("diag.local_static_initialization_unavailable", Location);
			return false;
		}
		// Register the guard before publishing Initializing; allocation failure cannot arm a partial guard.
		LocalStatics->Guards.push_back({this, Frames.size() - 1u, Slot, Range->BeginOffset, Range->EndOffset,
			OperandStack.size()});
		LocalStatics->States[Slot] = ELocalStaticInitialization::Initializing;
		return true;
	}

	bool VeroScriptVm::CommitLocalStatic(const std::uint16_t Slot, FValue Value,
		const FSourceLocation& Location)
	{
		if (!LocalStatics || Frames.empty() || Slot >= Globals.size()
			|| Slot >= LocalStatics->States.size()
			|| LocalStatics->States[Slot] != ELocalStaticInitialization::Initializing)
		{
			ReportError("local static commit has no active initializer", Location);
			(void)ReleaseValue(Value, Location);
			return false;
		}
		auto& Guards = LocalStatics->Guards;
		const auto Guard = std::find_if(Guards.begin(), Guards.end(), [&](const FLocalStaticGuard& Candidate)
		{
			return Candidate.Context == this && Candidate.FrameIndex == Frames.size() - 1u
				&& Candidate.GlobalSlot == Slot;
		});
		if (Guard == Guards.end() || Guard->StackDepth != OperandStack.size())
		{
			ReportError("local static commit belongs to another invocation", Location);
			(void)ReleaseValue(Value, Location);
			return false;
		}
		try
		{
			LocalStatics->Completed.reserve(LocalStatics->Completed.size() + 1u);
		}
		catch (...)
		{
			ReportError("cannot retain local static initialization order", Location);
			(void)ReleaseValue(Value, Location);
			return false;
		}
		Globals[Slot] = std::move(Value);
		LocalStatics->Completed.push_back(Slot);
		LocalStatics->States[Slot] = ELocalStaticInitialization::Initialized;
		Guards.erase(Guard);
		return true;
	}

	void VeroScriptVm::AbortLocalStaticInitializers(const std::size_t FrameBoundary,
		const std::size_t TargetOffset) noexcept
	{
		if (!LocalStatics) return;
		auto& Guards = LocalStatics->Guards;
		for (auto It = Guards.begin(); It != Guards.end();)
		{
			if (It->Context == this && (It->FrameIndex > FrameBoundary
				|| (It->FrameIndex == FrameBoundary && (TargetOffset < It->BeginOffset || TargetOffset >= It->EndOffset))))
			{
				LocalStatics->States[It->GlobalSlot] = ELocalStaticInitialization::Uninitialized;
				It = Guards.erase(It);
			}
			else
			{
				++It;
			}
		}
	}

	bool VeroScriptVm::ReleaseProgramGlobals(const FSourceLocation& Location)
	{
		const bool StaticsReleased = ReleaseLocalStaticRoots(Location);
		return ReleaseValueList(Globals, Location) && StaticsReleased;
	}

	bool VeroScriptVm::ReleaseLocalStaticRoots(const FSourceLocation& Location)
	{
		bool bOk = true;
		if (LocalStatics && !Globals.empty())
		{
			LocalStatics->bDestroying = true;
			for (auto It = LocalStatics->Completed.rbegin(); It != LocalStatics->Completed.rend(); ++It)
			{
				if (*It >= Globals.size())
				{
					continue;
				}
				FValue Value = std::move(Globals[*It]);
				Globals[*It] = std::int32_t{0};
				LocalStatics->States[*It] = ELocalStaticInitialization::Uninitialized;
				bOk = ReleaseValue(Value, Location) && bOk;
			}
			LocalStatics->Completed.clear();
		}
		return bOk;
	}
}
