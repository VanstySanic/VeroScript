// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmIndexedExpressions.h"

#include <algorithm>

#if defined(_MSC_VER) && defined(_M_X64)
#pragma code_seg(push, ".vstext")
#endif

namespace VeroScript::VmIndexedExpressions
{
	void FCache::DecodeConditional(FEntry& Entry, const FBytecodeModule& Module,
	    const FBytecodeFunction& Function, const std::size_t Begin)
	{
		Entry.Function = &Function;
		Entry.Begin = Begin;
		Entry.PeakDepth = 0;
		Entry.Kind = EEntryKind::None;
		if (!Function.ExceptionRegions.empty()) return;
		const auto& Code = Function.Code;
		std::size_t Cursor = Begin, Depth = 1, Peak = 1;
		for (std::size_t Index = 0; Index < MaxInstructions && Cursor < Code.size(); ++Index)
		{
			const auto Op = static_cast<EOpcode>(Code[Cursor]);
			const auto Size = Bytecode::InstructionSize(Op);
			if (!Size || Size > Code.size() - Cursor) return;
			if (Op == EOpcode::Eq || Op == EOpcode::Ne || Op == EOpcode::Lt ||
			    Op == EOpcode::Le || Op == EOpcode::Gt || Op == EOpcode::Ge)
			{
				const auto BranchIp = Cursor + Size;
				const auto BranchSize = Bytecode::InstructionSize(EOpcode::JmpIfFalse);
				if (Depth != 2 || Code.size() - BranchIp < BranchSize ||
					static_cast<EOpcode>(Code[BranchIp]) != EOpcode::JmpIfFalse)
				{
					return;
				}
				const auto End = BranchIp + BranchSize;
				const auto Offset = Bytecode::ReadI32LE(Code.data() + BranchIp + 1u);
				const auto Target = static_cast<std::int64_t>(End) + Offset;
				if (Target < 0 || static_cast<std::size_t>(Target) > Code.size()) return;
				// This is only a bounded optimization proof, not a replacement for
				// Core's bytecode validation or protected-control-transfer services.
				std::size_t Boundary = 0;
				for (unsigned Step = 0; Boundary < static_cast<std::size_t>(Target) && Step < 256u; ++Step)
				{
					const auto Width = Bytecode::InstructionSize(static_cast<EOpcode>(Code[Boundary]));
					if (!Width || Width > static_cast<std::size_t>(Target) - Boundary) return;
					Boundary += Width;
				}
				if (Boundary != static_cast<std::size_t>(Target)) return;
				Entry.End = End;
				Entry.BranchOffset = Offset;
				Entry.Comparison = Op;
				Entry.Count = static_cast<std::uint8_t>(Index);
				Entry.PeakDepth = static_cast<std::uint8_t>(Peak);
				Entry.Kind = EEntryKind::PureCondition;
				return;
			}
			std::int32_t Argument = 0;
			switch (Op)
			{
			case EOpcode::LoadLocal:
			case EOpcode::ConstI32:
				{
					if (Depth == MaxDepth) return;
					++Depth;
					Peak = std::max(Peak, Depth);
					const auto Operand = Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
					if (Op == EOpcode::LoadLocal)
					{
						if (Operand >= Function.LocalCount) return;
						Argument = Operand;
					}
					else
					{
						if (Operand >= Module.Constants.size() || Module.Constants[Operand].Kind != EConstantKind::I32) return;
						Argument = Module.Constants[Operand].I32;
					}
					break;
				}
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
				{
					if (Depth < 2) return;
					--Depth;
					break;
				}
			default:
				return;
			}
			Entry.Opcodes[Index] = Op;
			Entry.Arguments[Index] = Argument;
			Cursor += Size;
		}
	}

#if defined(_MSC_VER)
	__declspec(noinline)
#elif defined(__GNUC__)
	__attribute__((noinline))
#endif
	bool FCache::TryExecuteConditional(const std::int32_t First, const FBytecodeModule& Module,
	    const FBytecodeFunction& Function, const std::size_t Begin, const std::vector<FValue>& Locals,
	    const std::size_t StackRoom, FConditionalBranch& Branch)
	{
		auto& Entry = GetEntry(Module, Function, Begin);
		if (Entry.Kind != EEntryKind::PureCondition || !Entry.PeakDepth ||
			StackRoom < Entry.PeakDepth || Locals.size() < Function.LocalCount)
		{
			return false;
		}
		if (State && (State->GetFrames().empty() || !State->GetFrames().back().FinallyContinuations.empty() ||
						 State->GetExecutionCleanup() || State->GetResetRequiredFlag() || State->HasExternalExecutionCheck() ||
						 State->HasExecutionObservers() || State->GetDebugger() || State->IsCopyingLanguageValue() ||
						 !State->GetActiveContainerCopyReads().empty()))
		{
			return false;
		}
		std::array<std::int32_t, MaxDepth> Values;
		Values[0] = First;
		std::size_t Count = 1;
		for (std::size_t Index = 0; Index < Entry.Count; ++Index)
		{
			const auto Op = Entry.Opcodes[Index];
			const auto Argument = Entry.Arguments[Index];
			if (Op == EOpcode::LoadLocal)
			{
				const auto* Value = std::get_if<std::int32_t>(&Locals[static_cast<std::size_t>(Argument)]);
				if (!Value)
				{
					Entry.PeakDepth = 0;
					return false;
				}
				Values[Count++] = *Value;
			}
			else if (Op == EOpcode::ConstI32)
			{
				Values[Count++] = Argument;
			}
			else
			{
				const auto Right = Values[--Count];
				auto& Left = Values[Count - 1u];
				switch (Op)
				{
				case EOpcode::BitAnd:
					{
						Left &= Right;
						break;
					}
				case EOpcode::BitOr:
					{
						Left |= Right;
						break;
					}
				case EOpcode::BitXor:
					{
						Left ^= Right;
						break;
					}
				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						if (static_cast<std::uint32_t>(Right) > 31u)
						{
							Entry.PeakDepth = 0;
							return false;
						}
						Left = Op == EOpcode::Shl ? static_cast<std::int32_t>(static_cast<std::uint32_t>(Left) << Right) : Left >> Right;
						break;
					}
				default:
					{
						if (!VmDetail::TryEvalInt32Arithmetic(Op, Left, Right, Left))
						{
							Entry.PeakDepth = 0;
							return false;
						}
						break;
					}
				}
			}
		}
		const auto Left = Values[0], Right = Values[1];
		bool Condition = false;
		switch (Entry.Comparison)
		{
		case EOpcode::Eq:
			{
				Condition = Left == Right;
				break;
			}
		case EOpcode::Ne:
			{
				Condition = Left != Right;
				break;
			}
		case EOpcode::Lt:
			{
				Condition = Left < Right;
				break;
			}
		case EOpcode::Le:
			{
				Condition = Left <= Right;
				break;
			}
		case EOpcode::Gt:
			{
				Condition = Left > Right;
				break;
			}
		case EOpcode::Ge:
			{
				Condition = Left >= Right;
				break;
			}
		default:
			return false;
		}
		Branch = {Entry.End, Entry.BranchOffset, Condition};
		return true;
	}

	void FCache::Decode(FEntry& Entry, const FBytecodeModule& Module,
	    const FBytecodeFunction& Function, const std::size_t Begin)
	{
		Entry.Function = &Function;
		Entry.Begin = Begin;
		Entry.PeakDepth = 0;
		Entry.Kind = EEntryKind::None;
		const auto& Code = Function.Code;
		std::size_t Cursor = Begin;
		std::size_t Depth = 1;
		std::size_t Peak = 1;
		bool bReadArray = false;
		for (std::size_t Index = 0; Index < MaxInstructions && Cursor < Code.size(); ++Index)
		{
			const auto Opcode = static_cast<EOpcode>(Code[Cursor]);
			const auto Size = Bytecode::InstructionSize(Opcode);
			if (Size == 0 || Size > Code.size() - Cursor) return;
			if (Opcode == EOpcode::StoreLocal)
			{
				const auto Target = Bytecode::ReadU16LE(Code.data() + Cursor + 1);
				if (!bReadArray || Depth != 1 || Target >= Function.LocalCount) return;
				Entry.Target = Target;
				Entry.Count = static_cast<std::uint8_t>(Index);
				Entry.End = Cursor + Size;
				Entry.PeakDepth = static_cast<std::uint8_t>(Peak);
				Entry.Kind = EEntryKind::IndexedStore;
				return;
			}
			std::int32_t Argument = 0;
			switch (Opcode)
			{
			case EOpcode::LoadLocal:
			case EOpcode::ConstI32:
				{
					if (Depth == MaxDepth) return;
					++Depth;
					Peak = std::max(Peak, Depth);
					const auto Operand = Bytecode::ReadU16LE(Code.data() + Cursor + 1);
					if (Opcode == EOpcode::LoadLocal)
					{
						if (Operand >= Function.LocalCount) return;
						Argument = Operand;
					}
					else
					{
						if (Operand >= Module.Constants.size() || Module.Constants[Operand].Kind != EConstantKind::I32) return;
						Argument = Module.Constants[Operand].I32;
					}
					break;
				}
			case EOpcode::GetIndexCopy:
				{
					if (Depth < 2) return;
					--Depth;
					bReadArray = true;
					break;
				}
			case EOpcode::Add:
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
			case EOpcode::BitAnd:
			case EOpcode::BitOr:
			case EOpcode::BitXor:
				{
					if (Depth < 2) return;
					--Depth;
					break;
				}
			default:
				return;
			}
			Entry.Opcodes[Index] = Opcode;
			Entry.Arguments[Index] = Argument;
			Cursor += Size;
		}
	}

	bool FCache::CanFuseInFunction(const FBytecodeModule& Module, const FBytecodeFunction& Function,
	    const std::uint8_t* const LocalKinds, const std::size_t LocalCount)
	{
		const auto& Code = Function.Code;
		for (std::size_t Cursor = 0; Cursor < Code.size();)
		{
			const auto Opcode = static_cast<EOpcode>(Code[Cursor]);
			const auto Size = Bytecode::InstructionSize(Opcode);
			if (Size == 0 || Size > Code.size() - Cursor) return false;
			const auto FirstSlot = Size >= 3u ? Bytecode::ReadU16LE(Code.data() + Cursor + 1u) : 0u;
			if (Opcode == EOpcode::LoadLocal && FirstSlot < LocalCount && LocalKinds[FirstSlot] == 1u)
			{
				FEntry Entry;
				Decode(Entry, Module, Function, Cursor + Size);
				if (Entry.Kind == EEntryKind::IndexedStore && Entry.PeakDepth != 0 &&
				    Entry.Target < LocalCount && LocalKinds[Entry.Target] == 1u)
				{
					std::array<std::uint8_t, MaxDepth> Kinds;
					Kinds[0] = 1u;
					std::size_t Count = 1u;
					bool bKnown = true;
					for (std::size_t Index = 0; Index < Entry.Count && bKnown; ++Index)
					{
						const auto Operation = Entry.Opcodes[Index];
						if (Operation == EOpcode::LoadLocal)
						{
							const auto Slot = static_cast<std::size_t>(Entry.Arguments[Index]);
							Kinds[Count++] = Slot < LocalCount ? LocalKinds[Slot] : 0u;
						}
						else if (Operation == EOpcode::ConstI32)
						{
							Kinds[Count++] = 1u;
						}
						else
						{
							const auto Right = Kinds[--Count];
							const auto Left = Kinds[Count - 1u];
							bKnown = Right == 1u && Left == (Operation == EOpcode::GetIndexCopy ? 4u : 1u);
							Kinds[Count - 1u] = 1u;
						}
					}
					if (bKnown) return true;
				}
			}
			Cursor += Size;
		}
		return false;
	}

#if defined(_MSC_VER)
	__declspec(noinline)
#elif defined(__GNUC__)
	__attribute__((noinline))
#endif
	bool FCache::TryExecuteIndexed(const std::int32_t First, const FBytecodeModule& Module,
	    const FBytecodeFunction& Function, std::size_t& Next, std::vector<FValue>& Locals,
	    const std::size_t StackRoom)
	{
		auto& Entry = GetEntry(Module, Function, Next);
		if (Entry.Kind != EEntryKind::IndexedStore || Entry.PeakDepth == 0) return false;
		if (Execute(First, Function, Next, Locals, StackRoom, Entry)) return true;
		// A declined candidate stays canonical for the rest of this run. In
		// particular, maps and boxed/ref arrays must not pay a failed probe per read.
		Entry.PeakDepth = 0;
		return false;
	}

	bool FCache::Execute(const std::int32_t First, const FBytecodeFunction& Function,
	    std::size_t& Next, std::vector<FValue>& Locals, const std::size_t StackRoom, const FEntry& Entry)
	{
		if (!State || StackRoom < Entry.PeakDepth || Locals.size() < Function.LocalCount ||
			State->IsCopyingLanguageValue() || !State->GetActiveContainerCopyReads().empty())
		{
			return false;
		}
		auto* const Target = std::get_if<std::int32_t>(&Locals[Entry.Target]);
		if (!Target) return false;
		struct FOperand
		{
			const std::vector<std::int32_t>* Array = nullptr;
			std::int32_t Number = 0;
		};
		std::array<FOperand, MaxDepth> Values;
		Values[0].Number = First;
		std::size_t Count = 1;
		for (std::size_t Index = 0; Index < Entry.Count; ++Index)
		{
			const auto Opcode = Entry.Opcodes[Index];
			const auto Argument = Entry.Arguments[Index];
			if (Opcode == EOpcode::LoadLocal)
			{
				const auto& Local = Locals[static_cast<std::size_t>(Argument)];
				if (const auto* Number = std::get_if<std::int32_t>(&Local))
				{
					Values[Count++] = {nullptr, *Number};
					continue;
				}
				const auto* Handle = std::get_if<FDynamicArrayHandle>(&Local);
				if (!Handle || !*Handle || !(*Handle)->Elements.empty() || (*Handle)->ElementTypeName != "int") return false;
				const auto* Array = std::get_if<std::vector<std::int32_t>>(&(*Handle)->ScalarElements);
				if (!Array) return false;
				Values[Count++] = {Array, 0};
				continue;
			}
			if (Opcode == EOpcode::ConstI32)
			{
				Values[Count++] = {nullptr, Argument};
				continue;
			}
			const FOperand Right = Values[--Count];
			FOperand& Left = Values[Count - 1];
			if (Opcode == EOpcode::GetIndexCopy)
			{
				if (!Left.Array || Right.Array || Right.Number < 0 || static_cast<std::size_t>(Right.Number) >= Left.Array->size()) return false;
				Left = {nullptr, (*Left.Array)[static_cast<std::size_t>(Right.Number)]};
				continue;
			}
			if (Left.Array || Right.Array) return false;
			switch (Opcode)
			{
			case EOpcode::BitAnd:
				{
					Left.Number &= Right.Number;
					break;
				}
			case EOpcode::BitOr:
				{
					Left.Number |= Right.Number;
					break;
				}
			case EOpcode::BitXor:
				{
					Left.Number ^= Right.Number;
					break;
				}
			default:
				{
					if (!VmDetail::TryEvalInt32Arithmetic(Opcode, Left.Number, Right.Number, Left.Number)) return false;
					break;
				}
			}
		}
		if (Count != 1 || Values[0].Array) return false;
		*Target = Values[0].Number;
		Next = Entry.End;
		return true;
	}
}

#if defined(_MSC_VER) && defined(_M_X64)
#pragma code_seg(pop)
#endif
