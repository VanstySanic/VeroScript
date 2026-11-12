// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitLinear.h"
#include "VeroScriptJitText.h"
#include "VeroScriptJitNativeLinearHelpers.h"

#include <limits>

namespace VeroScript::AsmJit::JitLinear
{
	namespace
	{
		using K = ETypedLoopLocalKind;
		using T = JitNative::LinearHelpers::EMixedScalarTag;

		T ScalarTag(const K Kind)
		{
			switch (Kind)
			{
			case K::U32:
				{
					return T::U32;
				}
			case K::I64:
				{
					return T::I64;
				}
			case K::U64:
				{
					return T::U64;
				}
			case K::Bool:
				{
					return T::Bool;
				}
			default:
				{
					return T::I32;
				}
			}
		}

		std::uint64_t Normalize(const std::uint64_t Value, const K Kind)
		{
			if (Kind == K::I32) return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(Value)));
			if (Kind == K::U32) return static_cast<std::uint32_t>(Value);
			return Value;
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopWideOperation(
		FTypedLoopNativeContext* const Context, const std::uint32_t InstructionIndex,
		const std::uint64_t Left, const std::uint64_t Right) noexcept
	{
		using namespace JitNative::LinearHelpers;
		const auto& Instruction = Context->Instructions[InstructionIndex];
		const auto Op = Instruction.Opcode;
		const auto Kind = Context->InstructionKinds[InstructionIndex];
		Context->FailureOffset = Instruction.BytecodeOffset;
		std::int32_t Status = 0;
		if (Op == EOpcode::AddLocalLocal && Instruction.bTextAdd)
		{
			return TypedLoopNativeStoreText(Context, InstructionIndex, Left, Right);
		}
		if (Op == EOpcode::StoreLocal && Kind == K::String)
		{
			if (Instruction.bTextConcatStore || Instruction.TextFormatInstruction != 0xFFFFu)
			{
				return TypedLoopNativeStoreText(Context, InstructionIndex, Left, Right);
			}
			return TypedLoopNativeStoreStringLocal(Context, Instruction.Operand0,
				Left, Instruction.BytecodeOffset);
		}
		if (Op == EOpcode::Not)
		{
			Context->Scratch = Left == 0u;
			return 0;
		}
		if (Kind == K::String && (Op == EOpcode::Eq || Op == EOpcode::Ne))
		{
			return TypedLoopNativeCompareI32OrString(Context, Left, Right,
				Op == EOpcode::Eq ? 0u : 1u, Instruction.BytecodeOffset);
		}
		if (Op == EOpcode::GetIndexCopy || Op == EOpcode::SetIndex)
		{
			// The kind preflight proves the token is a rooted Array binding. Compare
			// the complete index before converting it to a host storage offset.
			auto* const Binding = reinterpret_cast<FTypedLoopNativeBinding*>(static_cast<std::uintptr_t>(Left));
			if (Right >= Binding->NativeSize || Right > std::numeric_limits<std::size_t>::max())
			{
				Context->FailureMessage = "array index out of bounds";
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			const auto Index = static_cast<std::size_t>(Right);
			if (Op == EOpcode::GetIndexCopy)
			{
				Context->Scratch = Binding->Kind == K::ArrayI32
					? static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t*>(Binding->NativeData)[Index]))
					: static_cast<std::uint32_t*>(Binding->NativeData)[Index];
				++Context->ArrayGets;
			}
			else
			{
				if (Binding->Kind == K::ArrayI32)
				{
					static_cast<std::int32_t*>(Binding->NativeData)[Index] = static_cast<std::int32_t>(Context->Scratch);
				}
				else
				{
					static_cast<std::uint32_t*>(Binding->NativeData)[Index] = static_cast<std::uint32_t>(Context->Scratch);
				}
				Context->Scratch = Left;
				++Context->ArraySets;
			}
			return 0;
		}
		if (Op == EOpcode::CallMethod || Op == EOpcode::CallBoundMethod)
		{
			return TypedLoopNativeContainerMethodI32(Context, Left, Right,
				static_cast<std::uint32_t>(Instruction.Immediate), Instruction.BytecodeOffset);
		}
		if (Op == EOpcode::CallBuiltin)
		{
			const auto Id = static_cast<EBuiltinId>(Instruction.Immediate);
			if (Id == EBuiltinId::CharCode) return TypedLoopNativeTextOperation(Context, InstructionIndex, Left, Right);
			if (Id == EBuiltinId::ToString)
			{
				return TypedLoopNativeToString(Context, Instruction.TextValueSlot, Left, Instruction.BytecodeOffset);
			}
			if (Id == EBuiltinId::Len)
			{
				// Complete kind preflight proves a rooted string view or scalar array.
				const auto Length = Kind == K::String
					? reinterpret_cast<const FTypedLoopNativeStringView*>(static_cast<std::uintptr_t>(Left & TypedLoopNativePayloadMask))->Size
					: reinterpret_cast<const FTypedLoopNativeBinding*>(static_cast<std::uintptr_t>(Left))->NativeSize;
				if (Length > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
				{
					Context->FailureMessage = "len result exceeds int64 range";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				Context->Scratch = Length;
				return 0;
			}
			const bool Unsigned = Kind == K::U32 || Kind == K::U64;
			const auto Signed = Kind == K::I32 ? static_cast<std::int64_t>(static_cast<std::int32_t>(Left)) : static_cast<std::int64_t>(Left);
			const auto UnsignedValue = Kind == K::U32 ? static_cast<std::uint64_t>(static_cast<std::uint32_t>(Left)) : Left;
			if (Id == EBuiltinId::ToInt || Id == EBuiltinId::ToUnsigned)
			{
				const bool SignedTarget = Id == EBuiltinId::ToInt;
				const auto Maximum = SignedTarget ? static_cast<std::uint64_t>(INT32_MAX) : static_cast<std::uint64_t>(UINT32_MAX);
				if ((Unsigned && UnsignedValue > Maximum)
					|| (!Unsigned && (Signed < (SignedTarget ? static_cast<std::int64_t>(INT32_MIN) : 0)
						|| (Signed >= 0 && static_cast<std::uint64_t>(Signed) > Maximum))))
				{
					return static_cast<std::int32_t>(SignedTarget ? EJitRuntimeStatus::ToIntOverflow : EJitRuntimeStatus::ToUnsignedOverflow);
				}
				Context->Scratch = Normalize(Left, SignedTarget ? K::I32 : K::U32);
				return 0;
			}
			if ((Id == EBuiltinId::ToInt64 && Unsigned && UnsignedValue > static_cast<std::uint64_t>(INT64_MAX))
				|| (Id == EBuiltinId::ToUInt64 && !Unsigned && Signed < 0))
			{
				Context->FailureMessage = Id == EBuiltinId::ToInt64 ? "to_int64 overflow" : "to_uint64 overflow";
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			Context->Scratch = Unsigned ? UnsignedValue : static_cast<std::uint64_t>(Signed);
			return 0;
		}
		if ((Op == EOpcode::Shl || Op == EOpcode::Shr)
			&& Right >= (Kind == K::I64 || Kind == K::U64 ? 64u : 32u))
		{
			return static_cast<std::int32_t>(EJitRuntimeStatus::ShiftOutOfRange);
		}
		if (Op == EOpcode::Neg || Op == EOpcode::BitNot || Op == EOpcode::Not)
		{
			Status = UnaryOpMixed(static_cast<std::uint8_t>(Op),
				static_cast<std::uint8_t>(ScalarTag(Kind)), Left, &Context->Scratch);
		}
		else
		{
			const auto Binary = Op == EOpcode::IncLocal || Op == EOpcode::AddLocalImm
				|| Op == EOpcode::AddLocalLocal ? EOpcode::Add : Op;
			Status = BinOpMixed(static_cast<std::uint8_t>(Binary),
				static_cast<std::uint8_t>(ScalarTag(Kind)), Left, Right, &Context->Scratch);
		}
		if (Status == 0)
		{
			Context->Scratch = Normalize(Context->Scratch, Kind);
		}
		return Status;
	}

	std::int32_t ExecuteWideTypedLoop(FTypedLoopNativeContext& Context, const FTypedLoopPlan& Plan) noexcept
	{
		std::size_t Pc = 0;
		while (Pc < Plan.Instructions.size())
		{
			const auto& I = Plan.Instructions[Pc];
			const auto Kind = Context.InstructionKinds[Pc];
			auto* const Stack = Context.Stack;
			const auto Depth = I.EntryStackDepth;
			std::size_t Next = I.NextInstruction;
			std::int32_t Status = 0;
			switch (I.Opcode)
			{
			case EOpcode::Nop:
				{
					break;
				}
			case EOpcode::ConstI32:
				{
					Stack[Depth] = I.WideImmediateKind != K::Unsupported ? I.Immediate64
						: I.bImmediateU32 ? static_cast<std::uint32_t>(I.Immediate)
						: static_cast<std::uint64_t>(static_cast<std::int64_t>(I.Immediate));
					break;
				}
			case EOpcode::ConstBool:
				{
					Stack[Depth] = I.Immediate != 0;
					break;
				}
			case EOpcode::LoadLocal:
				{
					Stack[Depth] = Context.Locals[I.Operand0];
					break;
				}
			case EOpcode::StoreLocal:
				{
					Context.Locals[I.Operand0] = Stack[Depth - 1u];
					break;
				}
			case EOpcode::Dup:
				{
					Stack[Depth] = Stack[Depth - 1u];
					break;
				}
			case EOpcode::Pop:
				{
					break;
				}
			case EOpcode::Jmp:
				{
					Next = I.BranchInstruction;
					break;
				}
			case EOpcode::JmpIfFalse:
				{
					if (Stack[Depth - 1u] == 0u)
					{
						Next = I.BranchInstruction;
					}
					break;
				}
			case EOpcode::JmpIfLocalGeImm:
				{
					const auto Value = Context.Locals[I.Operand0];
					const bool Taken = Kind == K::I64 ? static_cast<std::int64_t>(Value) >= I.Immediate
						: Kind == K::I32 ? static_cast<std::int32_t>(Value) >= I.Immediate
						: Kind == K::U64 ? Value >= static_cast<std::uint32_t>(I.Immediate)
						: static_cast<std::uint32_t>(Value) >= static_cast<std::uint32_t>(I.Immediate);
					if (Taken)
					{
						Next = I.BranchInstruction;
					}
					break;
				}
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
				{
					const auto Right = I.Opcode == EOpcode::AddLocalLocal ? Context.Locals[I.Operand1]
						: static_cast<std::uint64_t>(static_cast<std::int64_t>(I.Opcode == EOpcode::IncLocal ? 1 : I.Immediate));
					Status = TypedLoopWideOperation(&Context, static_cast<std::uint32_t>(Pc), Context.Locals[I.Operand0], Right);
					if (Status == 0)
					{
						Context.Locals[I.Operand0] = Context.Scratch;
					}
					break;
				}
			case EOpcode::CallMethod:
			case EOpcode::CallBoundMethod:
				{
					Status = TypedLoopWideOperation(&Context, static_cast<std::uint32_t>(Pc), Stack[Depth - 2u], Stack[Depth - 1u]);
					Stack[Depth - 2u] = Context.Scratch;
					break;
				}
			case EOpcode::SetIndex:
				{
					Context.Scratch = Stack[Depth - 1u];
					Status = TypedLoopWideOperation(&Context, static_cast<std::uint32_t>(Pc), Stack[Depth - 3u], Stack[Depth - 2u]);
					Stack[Depth - 3u] = Context.Scratch;
					break;
				}
			case EOpcode::CallBuiltin:
			case EOpcode::Neg:
			case EOpcode::BitNot:
			case EOpcode::Not:
				{
					Status = TypedLoopWideOperation(&Context, static_cast<std::uint32_t>(Pc), Stack[Depth - 1u], 0u);
					Stack[Depth - 1u] = Context.Scratch;
					break;
				}
			default:
				{
					Status = TypedLoopWideOperation(&Context, static_cast<std::uint32_t>(Pc), Stack[Depth - 2u], Stack[Depth - 1u]);
					Stack[Depth - 2u] = Context.Scratch;
					break;
				}
			}
			if (Status != 0) return Status;
			Pc = Next;
		}
		return 0;
	}
}
