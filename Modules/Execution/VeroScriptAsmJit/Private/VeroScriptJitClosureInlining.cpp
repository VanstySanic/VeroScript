// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitLinear.h"

#include <algorithm>
#include <limits>

namespace VeroScript::AsmJit::JitLinear
{
	namespace
	{
		bool IsExpressionOpcode(const EOpcode Op)
		{
			switch (Op)
			{
			case EOpcode::Nop:
			case EOpcode::ConstI32:
			case EOpcode::LoadLocal:
			case EOpcode::Add:
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
			case EOpcode::Neg:
			case EOpcode::BitAnd:
			case EOpcode::BitOr:
			case EOpcode::BitXor:
			case EOpcode::BitNot:
			case EOpcode::Shl:
			case EOpcode::Shr:
			case EOpcode::Return:
				{
					return true;
				}
			default:
				{
					return false;
				}
			}
		}

		std::size_t RelativeOperand(const EOpcode Op)
		{
			if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse) return 1u;
			if (Op == EOpcode::JmpIfLocalGeImm) return 7u;
			return 0u;
		}
	}

	bool TryInlineLocalScalarClosure(const FBytecodeModule& Module,
	    const FBytecodeFunction& Function, FBytecodeFunction& Out, std::uint16_t& OutSites)
	{
		OutSites = 0;
		// A single non-escaping value capture needs no closure object. Its existing
		// local becomes the capture snapshot or mutable state, and a scalar argument
		// can be read directly because the callee can only write its own capture.
		if (Function.bIsMethod || !Function.Captures.empty() || !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function) || Function.Code.size() > 4096u)
		{
			return false;
		}
		const auto& Code = Function.Code;
		std::vector<std::size_t> Offsets;
		std::size_t Creation = Code.size();
		bool SeenBranch = false;
		for (std::size_t Pc = 0; Pc < Code.size();)
		{
			const auto Op = static_cast<EOpcode>(Code[Pc]);
			const auto Size = Bytecode::InstructionSize(Op);
			if (Size == 0 || Size > Code.size() - Pc) return false;
			Offsets.push_back(Pc);
			if (Op == EOpcode::MakeClosure)
			{
				if (Creation != Code.size() || SeenBranch || Code[Pc + 3u] != 1u) return false;
				Creation = Pc;
			}
			else if (RelativeOperand(Op) != 0u)
			{
				SeenBranch = true;
			}
			else if (!IsExpressionOpcode(Op) && Op != EOpcode::StoreLocal && Op != EOpcode::CallIndirect && Op != EOpcode::Lt && Op != EOpcode::Le && Op != EOpcode::Gt && Op != EOpcode::Ge && Op != EOpcode::Eq && Op != EOpcode::Ne && Op != EOpcode::ConstBool && Op != EOpcode::IncLocal && Op != EOpcode::AddLocalImm && Op != EOpcode::AddLocalLocal)
			{
				return false;
			}
			Pc += Size;
		}
		if (Creation == Code.size()) return false;
		const auto Definition = Creation + Bytecode::InstructionSize(EOpcode::MakeClosure);
		if (Code.size() - Definition < 3u || static_cast<EOpcode>(Code[Definition]) != EOpcode::StoreLocal) return false;
		const auto Slot = Bytecode::ReadU16LE(Code.data() + Definition + 1u);
		const auto Index = Bytecode::ReadU16LE(Code.data() + Creation + 1u);
		if (Slot >= Function.LocalCount || Index >= Module.Functions.size()) return false;
		const auto& Callee = Module.Functions[Index];
		if (Callee.ClosureTypeName != "fn(int)->int" || Callee.LocalCount != 2u || Callee.ParamCount != 1u || Callee.Captures.size() != 1u || !Callee.ExceptionRegions.empty() || Callee.bReturnsRef || Callee.bReturnsVoid || Callee.bIsMethod || Callee.bIsAbstract || Callee.bDeleted || !Callee.UnavailableReason.empty() || BytecodeFunctionHasRefParams(Callee) || Callee.Code.empty() || Callee.Code.size() > 192u)
		{
			return false;
		}
		const auto& Capture = Callee.Captures[0];
		const bool bCanWriteCapture = Capture.Mode == EClosureCaptureMode::MutableValue && Capture.bWritable;
		if (Capture.TypeName != "int" || Capture.bStableStorage || (Capture.Mode != EClosureCaptureMode::Value && !bCanWriteCapture)) return false;
		bool Returned = false;
		for (std::size_t Pc = 0; Pc < Callee.Code.size();)
		{
			const auto Op = static_cast<EOpcode>(Callee.Code[Pc]);
			const auto Size = Bytecode::InstructionSize(Op);
			if (Size == 0 || Size > Callee.Code.size() - Pc || (!IsExpressionOpcode(Op) && Op != EOpcode::StoreLocal) || (Op == EOpcode::Return && Pc + Size != Callee.Code.size()))
			{
				return false;
			}
			Returned = Returned || Op == EOpcode::Return;
			if (Op == EOpcode::LoadLocal && Bytecode::ReadU16LE(Callee.Code.data() + Pc + 1u) >= 2u) return false;
			if (Op == EOpcode::StoreLocal && (!bCanWriteCapture || Bytecode::ReadU16LE(Callee.Code.data() + Pc + 1u) != 1u)) return false;
			if (Op == EOpcode::ConstI32)
			{
				const auto Constant = Bytecode::ReadU16LE(Callee.Code.data() + Pc + 1u);
				if (Constant >= Module.Constants.size() || Module.Constants[Constant].Kind != EConstantKind::I32) return false;
			}
			Pc += Size;
		}
		if (!Returned) return false;
		std::vector<std::uint16_t> Arguments(Code.size(), 0xFFFFu);
		std::vector<bool> Elided(Code.size(), false);
		std::size_t Sites = 0;
		for (std::size_t At = 0; At < Offsets.size(); ++At)
		{
			const auto Pc = Offsets[At];
			const auto Op = static_cast<EOpcode>(Code[Pc]);
			const auto Relative = RelativeOperand(Op);
			if (Relative != 0u)
			{
				const auto Target = static_cast<std::int64_t>(Pc + Bytecode::InstructionSize(Op)) + Bytecode::ReadI32LE(Code.data() + Pc + Relative);
				// Creation must dominate every use and must not be revisited by a loop.
				if (Target <= static_cast<std::int64_t>(Definition) || Target > static_cast<std::int64_t>(Code.size())) return false;
			}
			if (Op == EOpcode::CallIndirect)
			{
				if (At < 2u || Code[Pc + 1u] != 1u || Pc < Definition) return false;
				const auto Receiver = Offsets[At - 1u];
				const auto Argument = Offsets[At - 2u];
				if (static_cast<EOpcode>(Code[Receiver]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + Receiver + 1u) != Slot || static_cast<EOpcode>(Code[Argument]) != EOpcode::LoadLocal)
				{
					return false;
				}
				const auto ArgSlot = Bytecode::ReadU16LE(Code.data() + Argument + 1u);
				if (ArgSlot == Slot || ArgSlot >= Function.LocalCount || ++Sites > 8u) return false;
				Arguments[Pc] = ArgSlot;
				Elided[Argument] = Elided[Receiver] = true;
			}
			if (Op == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + Pc + 1u) == Slot)
			{
				if (At + 1u >= Offsets.size() || static_cast<EOpcode>(Code[Offsets[At + 1u]]) != EOpcode::CallIndirect) return false;
			}
			if ((Op == EOpcode::StoreLocal || Op == EOpcode::IncLocal || Op == EOpcode::AddLocalImm || Op == EOpcode::AddLocalLocal || Op == EOpcode::JmpIfLocalGeImm) && Bytecode::ReadU16LE(Code.data() + Pc + 1u) == Slot && Pc != Definition)
			{
				return false;
			}
			if (Op == EOpcode::AddLocalLocal && Bytecode::ReadU16LE(Code.data() + Pc + 3u) == Slot) return false;
		}
		if (Sites == 0u) return false;
		// No branch may enter the middle of an expression/receiver sequence.
		for (const auto Pc : Offsets)
		{
			const auto Op = static_cast<EOpcode>(Code[Pc]);
			const auto Relative = RelativeOperand(Op);
			if (Relative == 0u)
			{
				continue;
			}
			const auto Target = static_cast<std::size_t>(static_cast<std::int64_t>(Pc + Bytecode::InstructionSize(Op)) + Bytecode::ReadI32LE(Code.data() + Pc + Relative));
			if (Target < Code.size() && (Elided[Target] || Arguments[Target] != 0xFFFFu)) return false;
		}
		Out = Function;
		Out.Code.clear();
		Out.DebugLines.clear();
		std::vector<std::size_t> Remap(Code.size() + 1u, std::numeric_limits<std::size_t>::max());
		for (const auto Pc : Offsets)
		{
			Remap[Pc] = Out.Code.size();
			const auto Op = static_cast<EOpcode>(Code[Pc]);
			const auto Size = Bytecode::InstructionSize(Op);
			if (Arguments[Pc] != 0xFFFFu)
			{
				FDebugLine Line = Pc < Function.DebugLines.size() ? Function.DebugLines[Pc] : FDebugLine{};
				for (std::size_t Inner = 0; Inner + 1u < Callee.Code.size();)
				{
					const auto InnerOp = static_cast<EOpcode>(Callee.Code[Inner]);
					const auto InnerSize = Bytecode::InstructionSize(InnerOp);
					const auto Begin = Out.Code.size();
					Out.Code.insert(Out.Code.end(), Callee.Code.begin() + Inner, Callee.Code.begin() + Inner + InnerSize);
					if (InnerOp == EOpcode::LoadLocal || InnerOp == EOpcode::StoreLocal)
					{
						const auto Mapped = Bytecode::ReadU16LE(Callee.Code.data() + Inner + 1u) == 0u ? Arguments[Pc] : Slot;
						Out.Code[Begin + 1u] = static_cast<std::uint8_t>(Mapped);
						Out.Code[Begin + 2u] = static_cast<std::uint8_t>(Mapped >> 8u);
					}
					if (Inner < Callee.DebugLines.size() && Callee.DebugLines[Inner].Line != 0u)
					{
						Line = Callee.DebugLines[Inner];
					}
					Out.DebugLines.resize(Out.Code.size());
					Out.DebugLines[Begin] = Line;
					Inner += InnerSize;
				}
			}
			else
			{
				const auto Begin = Out.Code.size();
				if (Pc == Creation || Elided[Pc])
				{
					Out.Code.insert(Out.Code.end(), Size, static_cast<std::uint8_t>(EOpcode::Nop));
				}
				else
				{
					Out.Code.insert(Out.Code.end(), Code.begin() + Pc, Code.begin() + Pc + Size);
				}
				Out.DebugLines.resize(Out.Code.size());
				if (Pc < Function.DebugLines.size())
				{
					Out.DebugLines[Begin] = Function.DebugLines[Pc];
				}
			}
		}
		Remap[Code.size()] = Out.Code.size();
		for (const auto Pc : Offsets)
		{
			const auto Op = static_cast<EOpcode>(Code[Pc]);
			const auto Relative = RelativeOperand(Op);
			if (Relative == 0u)
			{
				continue;
			}
			const auto Size = Bytecode::InstructionSize(Op);
			const auto Target = static_cast<std::size_t>(static_cast<std::int64_t>(Pc + Size) + Bytecode::ReadI32LE(Code.data() + Pc + Relative));
			if (Remap[Target] == std::numeric_limits<std::size_t>::max()) return false;
			const auto Delta = static_cast<std::int32_t>(Remap[Target]) - static_cast<std::int32_t>(Remap[Pc] + Size);
			for (unsigned Byte = 0; Byte < 4u; ++Byte)
			{
				Out.Code[Remap[Pc] + Relative + Byte] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(Delta) >> (Byte * 8u));
			}
		}
		if (Out.Code.size() > 65536u) return false;
		OutSites = static_cast<std::uint16_t>(Sites);
		return true;
	}
}
