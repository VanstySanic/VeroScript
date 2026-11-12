// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"

namespace VeroScript
{
	bool VeroScriptVm::PrepareGlobalInitialization()
	{
		if (!bDemandGlobalInitializing || Frames.empty() || !ActiveModule)
		{
			return true;
		}
		const auto FrameIndex = Frames.size() - 1;
		if (!Frames[FrameIndex].Function)
		{
			auto& Frame = Frames[FrameIndex];
			if (Frame.InitializingGlobalSlot != 0xFFFF)
			{
				const auto Slot = Frame.InitializingGlobalSlot;
				if (Slot < DemandGlobalInitializers.size() && DemandGlobalInitializers[Slot]
				    && Frame.InstructionPointer >= DemandGlobalInitializers[Slot]->EndOffset)
				{
					DemandGlobalStates[Slot] = 2;
					Frame.InitializingGlobalSlot = 0xFFFF;
				}
			}
			for (;;)
			{
				const auto Found = DemandGlobalStarts.find(Frame.InstructionPointer);
				if (Found == DemandGlobalStarts.end())
				{
					break;
				}
				const auto Slot = Found->second;
				if (DemandGlobalStates[Slot] == 2)
				{
					Frame.InstructionPointer = DemandGlobalInitializers[Slot]->EndOffset;
					continue;
				}
				DemandGlobalStates[Slot] = 1;
				Frame.InitializingGlobalSlot = Slot;
				break;
			}
		}
		const auto& Frame = Frames[FrameIndex];
		const auto& Code = Frame.Function ? Frame.Function->Code : ActiveModule->InitCode;
		const auto Offset = Frame.InstructionPointer;
		if (Offset < Code.size())
		{
			const auto Opcode = static_cast<EOpcode>(Code[Offset]);
			if ((Opcode == EOpcode::LoadGlobal || Opcode == EOpcode::StoreGlobal || Opcode == EOpcode::MakeGlobalRef)
			    && Code.size() - Offset >= Bytecode::InstructionSize(Opcode))
			{
				return EnsureGlobalInitialized(Bytecode::ReadU16LE(Code.data() + Offset + 1));
			}
		}
		return true;
	}

	bool VeroScriptVm::EnsureGlobalInitialized(const std::uint16_t Slot)
	{
		if (!bDemandGlobalInitializing || Slot >= DemandGlobalInitializers.size()
		    || !DemandGlobalInitializers[Slot] || DemandGlobalStates[Slot] != 0)
		{
			return true;
		}
		const auto Range = *DemandGlobalInitializers[Slot];
		if (!ActiveModule || Range.BeginOffset >= Range.EndOffset || Range.EndOffset > ActiveModule->InitCode.size())
		{
			ReportError("invalid deferred global initialization range");
			return false;
		}
		if (GetLogicalCallDepth() >= CallDepthLimit)
		{
			ReportErrorId("diag.call_stack_overflow");
			return false;
		}
		// Relative bytecode branches and file-backed debug locations retain their
		// meaning when the exact initializer span runs as a bounded expression frame.
		// The context owns the code even if an exceptional host callback unwinds
		// this synchronous entry before the interpreter has discarded its frame.
		DemandGlobalCode[Slot] = std::make_unique<FBytecodeFunction>();
		auto& Initializer = *DemandGlobalCode[Slot];
		Initializer.Name = "<global initializer>";
		Initializer.bReturnsVoid = true;
		Initializer.LocalCount = ActiveModule->InitLocalCount;
		Initializer.Code.assign(ActiveModule->InitCode.begin() + Range.BeginOffset,
		    ActiveModule->InitCode.begin() + Range.EndOffset);
		if (Range.EndOffset <= ActiveModule->InitDebugLines.size())
		{
			Initializer.DebugLines.assign(ActiveModule->InitDebugLines.begin() + Range.BeginOffset,
			    ActiveModule->InitDebugLines.begin() + Range.EndOffset);
		}
		if (Slot < ActiveModule->GlobalMetadata.size())
		{
			Initializer.ModulePath = ActiveModule->GlobalMetadata[Slot].ModulePath;
		}
		Initializer.Location.FileLabel = Initializer.ModulePath;
		const auto Depth = Frames.size();
		const auto Stack = OperandStack.size();
		const auto PreviousStop = StopFrameDepth;
		const auto PreviousOffset = ActiveInstructionStart;
		FCallFrame Frame;
		Frame.Function = &Initializer;
		Frame.bExpressionEval = true;
		Frame.bDisableNestedJit = true;
		Frame.StackBase = Stack;
		Frame.Locals.resize(Initializer.LocalCount);
		Frames.push_back(std::move(Frame));
		DemandGlobalStates[Slot] = 1;
		StopFrameDepth = Depth;
		bool bRan;
		try
		{
			bRan = Run();
		}
		catch (...)
		{
			StopFrameDepth = PreviousStop;
			ActiveInstructionStart = PreviousOffset;
			(void)DiscardExecutionAbove(Depth, Stack, {});
			DemandGlobalStates[Slot] = 0;
			throw;
		}
		StopFrameDepth = PreviousStop;
		ActiveInstructionStart = PreviousOffset;
		const bool bBalanced = Frames.size() == Depth && OperandStack.size() == Stack;
		if (!bRan || !bBalanced || bHasError || bHasPendingScriptException || bExitRequested)
		{
			if (bRan && !bBalanced && !bHasError && !bExitRequested)
			{
				ReportError("unbalanced deferred global initialization");
			}
			(void)DiscardExecutionAbove(Depth, Stack, {});
			// Keep the recursion guard until its frame and temporary owners are gone.
			// A caught failure must retry, without resetting completed dependencies.
			DemandGlobalStates[Slot] = 0;
			return false;
		}
		DemandGlobalStates[Slot] = 2;
		return true;
	}
}
