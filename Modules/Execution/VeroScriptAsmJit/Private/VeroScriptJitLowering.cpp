// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitLowering.h"

#include <algorithm>
#include <utility>

namespace VeroScript::AsmJit::JitLowering
{
	bool RequiresVmOwnership(const FBytecodeFunction& Function)
	{
		if (!Function.UnavailableReason.empty() || Function.bReturnsRef
			|| BytecodeFunctionHasRefParams(Function)
			|| !Function.ExceptionRegions.empty())
		{
			return true;
		}
		// Signature Any/object types alone do not force VmTrampoline — LinearTrampoline
		// handles CallNative / object values via VM helpers. NativeCfg still gates on
		// precise scalar signatures separately (bSignatureHasBoxedValues).
		const std::vector<std::uint8_t>& Code = Function.Code;
		for (std::size_t Ip = 0; Ip < Code.size();)
		{
			const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
			switch (Op)
			{
			case EOpcode::NewObject:
			case EOpcode::NewUninitializedObject:
			case EOpcode::DefaultCopyConstruct:
			case EOpcode::TakeLocal:
			case EOpcode::SameObject:
			case EOpcode::AssignHostBase:
			case EOpcode::EqualHostBase:
			case EOpcode::AssignCoreMetadata:
			case EOpcode::CloneValueOfType:
			case EOpcode::InitIndex:
			case EOpcode::NewNativeArray:
			case EOpcode::NewUninitializedNativeArray:
			case EOpcode::NewDynamicArray:
			case EOpcode::NewMap:
			case EOpcode::NewSet:
			case EOpcode::NewQueue:
			case EOpcode::NewStack:
			case EOpcode::NewOrderedMap:
			case EOpcode::NewDeque:
			case EOpcode::NewChannel:
			case EOpcode::CloneStruct:
			case EOpcode::MakeLocalRef:
			case EOpcode::MakeGlobalRef:
			case EOpcode::MakeThisFieldRef:
			case EOpcode::MakeFieldRef:
			case EOpcode::GetIndexRef:
			case EOpcode::GetIndexPathRef:
			case EOpcode::GetMapEntryRef:
			case EOpcode::BindLocal:
			case EOpcode::MakeClosure:
			case EOpcode::CallIndirect:
			case EOpcode::DerefRef:
			case EOpcode::CheckedCast:
			case EOpcode::TryCast:
			case EOpcode::BoxVariant:
			case EOpcode::VariantIs:
			case EOpcode::Throw:
			case EOpcode::EndFinally:
				return true;
			default:
				{
					break;
				}
			}
			const std::size_t Size = Bytecode::InstructionSize(Op);
			if (Size == 0 || Ip + Size > Code.size())
			{
				return true;
			}
			Ip += Size;
		}
		return false;
	}

	namespace
	{
		EKind Classify(const EOpcode Opcode)
		{
			switch (Opcode)
			{
			case EOpcode::Nop:
				return EKind::Nop;
			case EOpcode::LoadThis:
				return EKind::Nop;
			case EOpcode::ConstI32:
			case EOpcode::ConstF64:
			case EOpcode::ConstBool:
			case EOpcode::ConstStr:
			case EOpcode::ConstNull:
				return EKind::Constant;
			case EOpcode::LoadLocal:
			case EOpcode::TakeLocal:
				return EKind::LocalLoad;
			case EOpcode::StoreLocal:
			case EOpcode::BindLocal:
				return EKind::LocalStore;
			case EOpcode::LoadGlobal:
				return EKind::GlobalLoad;
			case EOpcode::StoreGlobal:
				return EKind::GlobalStore;
			case EOpcode::LoadThisField:
				return EKind::FieldLoad;
			case EOpcode::StoreThisField:
			case EOpcode::InitializeThisField:
				return EKind::FieldStore;
			case EOpcode::Add:
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
			case EOpcode::BitAnd:
			case EOpcode::BitOr:
			case EOpcode::BitXor:
			case EOpcode::Shl:
			case EOpcode::Shr:
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
			case EOpcode::IncField:
				return EKind::Arithmetic;
			case EOpcode::Eq:
			case EOpcode::Ne:
			case EOpcode::Lt:
			case EOpcode::Le:
			case EOpcode::Gt:
			case EOpcode::Ge:
				return EKind::Compare;
			case EOpcode::Neg:
			case EOpcode::Not:
			case EOpcode::BitNot:
				return EKind::Unary;
			case EOpcode::Jmp:
			case EOpcode::JmpIfFalse:
			case EOpcode::JmpIfLocalGeImm:
				return EKind::Branch;
			case EOpcode::Call:
			case EOpcode::CallMethod:
			case EOpcode::CallBoundMethod:
			case EOpcode::CallBuiltin:
			case EOpcode::CallNative:
				return EKind::Call;
			case EOpcode::Return:
				return EKind::Return;
			default:
				return EKind::RuntimeFallback;
			}
		}

		JitIr::EValueType ResultType(const EOpcode Opcode, const JitIr::EValueType Previous)
		{
			switch (Opcode)
			{
			case EOpcode::Eq:
			case EOpcode::Ne:
			case EOpcode::Lt:
			case EOpcode::Le:
			case EOpcode::Gt:
			case EOpcode::Ge:
			case EOpcode::Not:
				return JitIr::EValueType::Bool;
			case EOpcode::Return:
			case EOpcode::StoreLocal:
			case EOpcode::BindLocal:
			case EOpcode::StoreGlobal:
			case EOpcode::StoreThisField:
			case EOpcode::InitializeThisField:
			case EOpcode::Jmp:
			case EOpcode::JmpIfFalse:
			case EOpcode::JmpIfLocalGeImm:
				return JitIr::EValueType::Void;
			default:
				return Previous;
			}
		}

		std::uint16_t ReadU16(const FBytecodeFunction& Function, const std::size_t Offset)
		{
			if (Offset + 2 > Function.Code.size())
			{
				return 0;
			}
			return Bytecode::ReadU16LE(Function.Code.data() + Offset);
		}

		FValueSlot::EStorage StorageFor(const JitIr::EValueType Type)
		{
			switch (Type)
			{
			case JitIr::EValueType::I8:
			case JitIr::EValueType::U8:
			case JitIr::EValueType::I16:
			case JitIr::EValueType::U16:
			case JitIr::EValueType::I32:
			case JitIr::EValueType::U32:
			case JitIr::EValueType::F32:
			case JitIr::EValueType::Bool:
				return FValueSlot::EStorage::Scalar32;
			case JitIr::EValueType::I64:
			case JitIr::EValueType::U64:
			case JitIr::EValueType::F64:
			case JitIr::EValueType::Enum:
				return FValueSlot::EStorage::Scalar64;
			default:
				return FValueSlot::EStorage::Boxed;
			}
		}

		std::size_t CountBoxed(const std::vector<JitIr::EValueType>& Values)
		{
			return static_cast<std::size_t>(std::count_if(
					Values.begin(), Values.end(), [](const JitIr::EValueType Type)
					{
						return StorageFor(Type) == FValueSlot::EStorage::Boxed;
					}));
		}
	}

	bool BuildScalarPlan(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FPlan& OutPlan,
		std::string* const OutError)
	{
		OutPlan = {};
		OutPlan.bRequiresVmOwnership = RequiresVmOwnership(Function);
		JitIr::FFunction Ir;
		if (!JitIr::BuildFunction(Function, Ir, OutError)
			|| !JitIr::AnalyzeTypes(Module, Function, Ir, OutError))
		{
			return false;
		}
		OutPlan.Signature = Ir.Signature;
		OutPlan.LocalCount = Function.LocalCount;
		OutPlan.LocalSlots.resize(Function.LocalCount);
		for (std::uint16_t Slot = 0; Slot < Function.LocalCount; ++Slot)
		{
			OutPlan.LocalSlots[Slot].Index = Slot;
			OutPlan.LocalSlots[Slot].Type = Slot < Ir.LocalTypes.size()
				? Ir.LocalTypes[Slot]
				: JitIr::EValueType::Unknown;
			OutPlan.LocalSlots[Slot].Storage = StorageFor(OutPlan.LocalSlots[Slot].Type);
			// Usage facts are complete only for a precise reachable replay. When
			// analysis encounters a fallback opcode, fail closed and retain the prior
			// boxed classification for every unknown/owned slot.
			const bool bUsed = !Ir.bTypeAnalysisPrecise
				|| (Slot < Ir.LocalReadCounts.size()
					&& Ir.LocalReadCounts[Slot] != 0)
				|| (Slot < Ir.LocalWriteCounts.size()
					&& Ir.LocalWriteCounts[Slot] != 0)
				|| (Slot < Ir.LocalAddressEscapes.size()
					&& Ir.LocalAddressEscapes[Slot]);
			OutPlan.bHasBoxedValues = OutPlan.bHasBoxedValues
				|| (bUsed
					&& OutPlan.LocalSlots[Slot].Storage == FValueSlot::EStorage::Boxed);
		}
		OutPlan.Blocks.reserve(Ir.Blocks.size());
		for (std::size_t BlockIndex = 0; BlockIndex < Ir.Blocks.size(); ++BlockIndex)
		{
			const JitIr::FBasicBlock& IrBlock = Ir.Blocks[BlockIndex];
			if (BlockIndex < Ir.BlockEntryStates.size())
			{
				OutPlan.MaxStackSlots = static_cast<std::uint16_t>(std::max<std::size_t>(
						OutPlan.MaxStackSlots, Ir.BlockEntryStates[BlockIndex].Stack.size()));
				OutPlan.MaxBoxedStackSlots = static_cast<std::uint16_t>(std::max<std::size_t>(
						OutPlan.MaxBoxedStackSlots, CountBoxed(Ir.BlockEntryStates[BlockIndex].Stack)));
			}
			FBlock Block;
			Block.StartOffset = IrBlock.StartOffset;
			Block.EndOffset = IrBlock.EndOffset;
			Block.Successors = IrBlock.Successors;
			for (std::size_t InstructionIndex = 0; InstructionIndex < IrBlock.Instructions.size(); ++InstructionIndex)
			{
				const JitIr::FInstruction& IrInstruction = IrBlock.Instructions[InstructionIndex];
				FInstruction Instruction;
				Instruction.Offset = IrInstruction.Offset;
				Instruction.Opcode = IrInstruction.Opcode;
				Instruction.Kind = Classify(IrInstruction.Opcode);
				if (IrInstruction.Opcode == EOpcode::CallBuiltin
					&& IrInstruction.Offset + 1 < Function.Code.size()
					&& static_cast<EBuiltinId>(Function.Code[IrInstruction.Offset + 1]) == EBuiltinId::Exit)
				{
					// exit changes VM control flow rather than merely returning a value;
					// scalar helpers must not continue with following instructions.
					Instruction.Kind = EKind::RuntimeFallback;
				}
				const JitIr::FTypeState* EntryStatePtr = nullptr;
				if (BlockIndex < Ir.InstructionEntryStates.size()
					&& InstructionIndex < Ir.InstructionEntryStates[BlockIndex].size())
				{
					EntryStatePtr = &Ir.InstructionEntryStates[BlockIndex][InstructionIndex];
				}
				const JitIr::FTypeState EmptyState;
				const JitIr::FTypeState& EntryState = EntryStatePtr ? *EntryStatePtr : EmptyState;
				const JitIr::FTypeState* ExitStatePtr = nullptr;
				if (BlockIndex < Ir.InstructionExitStates.size()
					&& InstructionIndex < Ir.InstructionExitStates[BlockIndex].size())
				{
					ExitStatePtr = &Ir.InstructionExitStates[BlockIndex][InstructionIndex];
				}
				const JitIr::FTypeState& ExitState = ExitStatePtr ? *ExitStatePtr : EmptyState;
				Instruction.InputType = EntryState.Stack.empty()
					? JitIr::EValueType::Void
					: EntryState.Stack.back();
				Instruction.ResultType = Instruction.InputType;
				if (IrInstruction.Opcode == EOpcode::LoadLocal || IrInstruction.Opcode == EOpcode::TakeLocal
					|| IrInstruction.Opcode == EOpcode::StoreLocal
					|| IrInstruction.Opcode == EOpcode::BindLocal
					|| IrInstruction.Opcode == EOpcode::LoadGlobal
					|| IrInstruction.Opcode == EOpcode::StoreGlobal
					|| IrInstruction.Opcode == EOpcode::LoadThisField
					|| IrInstruction.Opcode == EOpcode::StoreThisField
					|| IrInstruction.Opcode == EOpcode::InitializeThisField
					|| IrInstruction.Opcode == EOpcode::IncLocal
					|| IrInstruction.Opcode == EOpcode::AddLocalImm)
				{
					Instruction.Operand0 = ReadU16(Function, IrInstruction.Offset + 1);
				}
				if (IrInstruction.Opcode == EOpcode::StoreLocal
					&& BlockIndex < Ir.DeadLocalStores.size()
					&& InstructionIndex < Ir.DeadLocalStores[BlockIndex].size())
				{
					Instruction.bElideLocalStore =
						Ir.DeadLocalStores[BlockIndex][InstructionIndex];
					if (Instruction.bElideLocalStore)
					{
						OutPlan.ElidedLocalStoreOffsets.push_back(IrInstruction.Offset);
					}
				}
				if (!ExitState.Stack.empty())
				{
					Instruction.ResultType = ExitState.Stack.back();
				}
				else
				{
					Instruction.ResultType = ResultType(IrInstruction.Opcode, Instruction.ResultType);
				}
				if (ExitStatePtr)
				{
					OutPlan.MaxStackSlots = static_cast<std::uint16_t>(std::max<std::size_t>(
							OutPlan.MaxStackSlots, ExitState.Stack.size()));
					OutPlan.MaxBoxedStackSlots = static_cast<std::uint16_t>(std::max<std::size_t>(
							OutPlan.MaxBoxedStackSlots, CountBoxed(ExitState.Stack)));
					OutPlan.bHasBoxedValues = OutPlan.bHasBoxedValues
						|| CountBoxed(ExitState.Stack) != 0;
				}
				if (Instruction.Kind == EKind::LocalLoad && Instruction.Operand0 < EntryState.Locals.size())
				{
					Instruction.ResultType = EntryState.Locals[Instruction.Operand0];
				}
				if (Instruction.Kind == EKind::RuntimeFallback)
				{
					OutPlan.bHasRuntimeFallback = true;
					if (std::find(OutPlan.RuntimeFallbackOpcodes.begin(),
							OutPlan.RuntimeFallbackOpcodes.end(), Instruction.Opcode)
								== OutPlan.RuntimeFallbackOpcodes.end())
					{
						OutPlan.RuntimeFallbackOpcodes.push_back(Instruction.Opcode);
					}
				}
				Block.Instructions.push_back(Instruction);
			}
			OutPlan.Blocks.push_back(std::move(Block));
		}
		std::sort(
			OutPlan.ElidedLocalStoreOffsets.begin(),
			OutPlan.ElidedLocalStoreOffsets.end());
		OutPlan.ElidedLocalStoreOffsets.erase(
			std::unique(
				OutPlan.ElidedLocalStoreOffsets.begin(),
				OutPlan.ElidedLocalStoreOffsets.end()),
			OutPlan.ElidedLocalStoreOffsets.end());
		OutPlan.bScalarEligible = Ir.bTypeAnalysisPrecise && !OutPlan.bHasRuntimeFallback;
		return true;
	}
}
