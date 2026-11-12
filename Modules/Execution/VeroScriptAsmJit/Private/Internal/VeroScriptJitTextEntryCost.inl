// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// This is a cost proof, never a substitute for the kernel's runtime guards.
// Unknown roots, suffixes and unbounded loop relationships retain ordinary selection.
constexpr std::uint32_t NativeTextStartupWork = 512u;
constexpr std::uint32_t NativeTextBytesPerWorkUnit = 256u;

std::uint32_t BoundedTextFormatBytes(const FTypedLoopPlan &Plan, const std::size_t FormatPc,
	const std::vector<std::uint8_t> &BranchTargets)
{
	const auto &V = Plan.Instructions;
	if (FormatPc == 0u || FormatPc >= V.size() || V[FormatPc - 1u].Opcode != EOpcode::LoadLocal) return 11u;
	const auto Slot = V[FormatPc - 1u].Operand0;
	const auto IsTarget = [&](const std::size_t Pc)
	{
		return Pc < BranchTargets.size() && BranchTargets[Pc] != 0u;
	};
	// Only a dominating write in the same straight-line block narrows the
	// formatted value. A join, intervening write or unknown producer keeps the
	// full signed-I32 bound, including the possible minus sign.
	for (std::size_t Pc = FormatPc - 1u; Pc > 0u;)
	{
		if (IsTarget(Pc)) return 11u;
		--Pc;
		const auto &I = V[Pc];
		if (I.NextInstruction != Pc + 1u || I.Opcode == EOpcode::Jmp ||
			I.Opcode == EOpcode::JmpIfFalse || I.Opcode == EOpcode::JmpIfLocalGeImm)
		{
			return 11u;
		}
		if (I.Operand0 != Slot || (I.Opcode != EOpcode::StoreLocal && I.Opcode != EOpcode::IncLocal &&
									  I.Opcode != EOpcode::AddLocalImm && I.Opcode != EOpcode::AddLocalLocal))
		{
			continue;
		}
		if (I.Opcode != EOpcode::StoreLocal || Pc < 2u || IsTarget(Pc) || IsTarget(Pc - 1u) || IsTarget(Pc - 2u)) return 11u;
		const auto &Divisor = V[Pc - 2u];
		if (V[Pc - 1u].Opcode != EOpcode::Mod || V[Pc - 1u].NextInstruction != Pc ||
			Divisor.Opcode != EOpcode::ConstI32 || Divisor.NextInstruction != Pc - 1u ||
			Divisor.WideImmediateKind != ETypedLoopLocalKind::Unsupported || Divisor.bImmediateU32 ||
			Divisor.bImmediateF32 || Divisor.Immediate <= 0)
		{
			return 11u;
		}
		auto Maximum = static_cast<std::uint32_t>(Divisor.Immediate - 1);
		if (Maximum == 0u) return 1u;
		std::uint32_t Digits = 1u;
		while (Maximum >= 10u)
		{
			Maximum /= 10u;
			++Digits;
		}
		return std::min(11u, Digits + 1u);
	}
	return 11u;
}

bool AddFollowingTextScanCost(const FBytecodeModule &Module, const FBytecodeFunction &Function,
	const FTypedLoopPlan &Builder, const FTypedLoopPlan &Scan,
	const std::uint16_t StringSlot, const std::vector<std::uint8_t> &StringLocals, std::uint16_t &Work)
{
	const auto &V = Scan.Instructions;
	if (Builder.ExitOffset > Scan.HeaderOffset || Scan.HeaderOffset >= Scan.ExitOffset || Scan.ExitOffset > Function.Code.size() ||
		V.size() < 8u || !Scan.GenericPureMethods.empty() ||
		Scan.bUsesDirectScalarFields || V[0].Opcode != EOpcode::LoadLocal)
	{
		return false;
	}
	const auto Counter = V[0].Operand0;
	if (Counter < Function.ParamCount || Counter >= Function.LocalCount || Counter == StringSlot) return false;
	std::size_t Guard = 1u;
	if (V[Guard].Opcode == EOpcode::CallBuiltin && static_cast<EBuiltinId>(V[Guard].Immediate) == EBuiltinId::ToInt64 && V[Guard].ArgumentCount == 1u)
	{
		++Guard;
	}
	if (Guard + 3u >= V.size() || V[Guard].Opcode != EOpcode::LoadLocal || V[Guard].Operand0 != StringSlot ||
		V[Guard + 1u].Opcode != EOpcode::CallBuiltin || static_cast<EBuiltinId>(V[Guard + 1u].Immediate) != EBuiltinId::Len ||
		V[Guard + 1u].ArgumentCount != 1u || V[Guard + 2u].Opcode != EOpcode::Lt ||
		V[Guard + 3u].Opcode != EOpcode::JmpIfFalse)
	{
		return false;
	}
	Guard += 3u;
	if (V[Guard].BranchInstruction != 0xFFFFFFFFu || V[Guard].ExternalBranchOffset != Scan.ExitOffset) return false;
	for (std::size_t Pc = 0; Pc < Guard; ++Pc)
	{
		if (V[Pc].NextInstruction != Pc + 1u)
		{
			return false;
		}
	}
	const auto Tail = V.size() - 2u;
	if (V[Tail].Opcode != EOpcode::IncLocal || V[Tail].Operand0 != Counter ||
		V[Tail].NextInstruction != Tail + 1u || V.back().Opcode != EOpcode::Jmp || V.back().BranchInstruction != 0u)
	{
		return false;
	}
	bool Initialized = false;
	const auto &Code = Function.Code;
	std::size_t Previous = Code.size();
	for (std::size_t Ip = 0u; Ip < Scan.ExitOffset;)
	{
		const auto Op = static_cast<EOpcode>(Code[Ip]);
		const auto Size = Bytecode::InstructionSize(Op);
		if (!Size || Size > Code.size() - Ip) return false;
		const auto Slot = Size >= 3u ? Bytecode::ReadU16LE(Code.data() + Ip + 1u) : 0xFFFFu;
		if (Ip >= Builder.ExitOffset && Ip < Scan.HeaderOffset)
		{
			if (Op != EOpcode::Nop && Op != EOpcode::ConstI32 && Op != EOpcode::LoadLocal && Op != EOpcode::StoreLocal) return false;
			if (Slot < StringLocals.size() && StringLocals[Slot] && (Op == EOpcode::LoadLocal || Op == EOpcode::StoreLocal)) return false;
		}
		if (Slot == Counter && Ip >= Builder.ExitOffset && (Op == EOpcode::StoreLocal || Op == EOpcode::IncLocal || Op == EOpcode::AddLocalImm || Op == EOpcode::AddLocalLocal))
		{
			if (Ip == V[Tail].BytecodeOffset)
			{
			}
			else if (!Initialized && Ip < Scan.HeaderOffset && Op == EOpcode::StoreLocal && Previous < Ip && static_cast<EOpcode>(Code[Previous]) == EOpcode::ConstI32)
			{
				const auto Constant = Bytecode::ReadU16LE(Code.data() + Previous + 1u);
				if (Constant >= Module.Constants.size() || Module.Constants[Constant].Kind != EConstantKind::I32 || Module.Constants[Constant].I32 != 0) return false;
				Initialized = true;
			}
			else
			{
				return false;
			}
		}
		if (Op != EOpcode::Nop)
		{
			Previous = Ip;
		}
		Ip += Size;
	}
	if (!Initialized) return false;
	for (std::size_t Pc = 0u; Pc < V.size(); ++Pc)
	{
		const auto &I = V[Pc];
		if (I.bTextAdd || I.bTextConcatStore) return false;
		if (I.Opcode == EOpcode::LoadLocal && I.Operand0 < StringLocals.size() &&
			StringLocals[I.Operand0] && I.Operand0 != StringSlot)
		{
			return false;
		}
		if ((I.Opcode == EOpcode::StoreLocal || I.Opcode == EOpcode::IncLocal || I.Opcode == EOpcode::AddLocalImm || I.Opcode == EOpcode::AddLocalLocal) &&
			((I.Operand0 < StringLocals.size() && StringLocals[I.Operand0]) || (I.Operand0 == Counter && Pc != Tail) || I.bTextAdd))
		{
			return false;
		}
		switch (I.Opcode)
		{
		case EOpcode::Jmp:
		case EOpcode::JmpIfFalse:
		case EOpcode::JmpIfLocalGeImm:
			{
				if (Pc != Guard && Pc != V.size() - 1u && (I.BranchInstruction <= Pc || I.BranchInstruction > Tail)) return false;
				break;
			}
		case EOpcode::CallBuiltin:
			{
				if (static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::Len && static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::CharCode &&
					static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::ToInt && static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::ToInt64 &&
					static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::ToUnsigned)
				{
					return false;
				}
				break;
			}
		case EOpcode::Nop:
		case EOpcode::ConstI32:
		case EOpcode::ConstBool:
		case EOpcode::LoadLocal:
		case EOpcode::StoreLocal:
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
		case EOpcode::AddLocalLocal:
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
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
		case EOpcode::Neg:
		case EOpcode::BitNot:
		case EOpcode::Not:
		case EOpcode::Dup:
		case EOpcode::Pop:
			{
				break;
			}
		default:
			return false;
		}
	}
	std::uint64_t BytesPerTrip = 0u;
	std::vector<std::uint8_t> BuilderTargets(Builder.Instructions.size(), 0u);
	for (const auto &I : Builder.Instructions)
	{
		if ((I.Opcode == EOpcode::Jmp || I.Opcode == EOpcode::JmpIfFalse || I.Opcode == EOpcode::JmpIfLocalGeImm) &&
			I.BranchInstruction < BuilderTargets.size())
		{
			BuilderTargets[I.BranchInstruction] = 1u;
		}
	}
	for (std::size_t Pc = 0u; Pc < Builder.Instructions.size(); ++Pc)
	{
		const auto &I = Builder.Instructions[Pc];
		if (I.Opcode != EOpcode::StoreLocal || !I.bTextConcatStore)
		{
			continue;
		}
		std::uint64_t Bytes = 0u;
		if (I.TextFormatInstruction != 0xFFFFu)
		{
			Bytes = BoundedTextFormatBytes(Builder, I.TextFormatInstruction, BuilderTargets);
		}
		else if (Pc >= 2u && Builder.Instructions[Pc - 2u].Opcode == EOpcode::ConstStr &&
				 Builder.Instructions[Pc - 2u].TextValueSlot < Builder.TextConstants.size())
		{
			Bytes = Builder.TextConstants[Builder.Instructions[Pc - 2u].TextValueSlot].Size;
		}
		else
		{
			return false;
		}
		if (Bytes > 0xFFFFu || BytesPerTrip > 0xFFFFu - Bytes) return false;
		BytesPerTrip += Bytes;
	}
	const auto Combined = static_cast<std::uint64_t>(Work) + BytesPerTrip * V.size();
	if (Combined > 0xFFFFu) return false;
	Work = static_cast<std::uint16_t>(Combined);
	return true;
}

void DescribeTextEntryCost(const FBytecodeModule &Module, const FBytecodeFunction &Function,
	const std::vector<EStackTag> &Parameters, FJitFunctionEntry &Entry)
{
	if (Entry.bPreferVmForExecution || Function.bIsMethod || BytecodeFunctionHasRefParams(Function) ||
		!Function.Captures.empty() || !Entry.TypedLoopPlans || Entry.TypedLoopPlans->BackwardLoopCount < 1u ||
		Entry.TypedLoopPlans->BackwardLoopCount > 2u || Entry.TypedLoopPlans->Plans.size() != Entry.TypedLoopPlans->BackwardLoopCount || Entry.TypedLoopPlans->bHasUnplannedComplexLoop ||
		std::any_of(Parameters.begin(), Parameters.end(), [](const auto Kind)
			{
				return Kind != EStackTag::I32;
			}))
	{
		return;
	}
	const auto &Plan = Entry.TypedLoopPlans->Plans.front();
	const auto &V = Plan.Instructions;
	if (!Plan.bUsesTextOperations || !Plan.GenericPureMethods.empty() ||
		Plan.bUsesDirectScalarFields || V.size() < 7u || V.size() > 0xFFFFu)
	{
		return;
	}
	const auto IsExit = [&](const FTypedLoopInstruction &I)
	{
		return I.BranchInstruction == 0xFFFFFFFFu && I.ExternalBranchOffset == Plan.ExitOffset;
	};
	std::uint16_t Counter = 0xFFFFu, Limit = 0xFFFFu;
	std::int32_t Immediate = 0;
	std::size_t Guard = 0u;
	if (V[0].Opcode == EOpcode::JmpIfLocalGeImm && IsExit(V[0]))
	{
		Counter = V[0].Operand0;
		Immediate = V[0].Immediate;
	}
	else if (V[0].Opcode == EOpcode::LoadLocal && V[1].Opcode == EOpcode::LoadLocal &&
			 V[2].Opcode == EOpcode::Lt && V[3].Opcode == EOpcode::JmpIfFalse && IsExit(V[3]) &&
			 V[0].NextInstruction == 1u && V[1].NextInstruction == 2u && V[2].NextInstruction == 3u)
	{
		Counter = V[0].Operand0;
		Limit = V[1].Operand0;
		Guard = 3u;
		if (Limit >= Function.ParamCount || Limit >= Parameters.size()) return;
	}
	else
	{
		return;
	}
	if (Counter >= Function.LocalCount || Counter < Function.ParamCount || Counter == Limit) return;
	const auto Tail = V.size() - 2u;
	if (V[Tail].Opcode != EOpcode::IncLocal || V[Tail].Operand0 != Counter ||
		V[Tail].NextInstruction != Tail + 1u || V.back().Opcode != EOpcode::Jmp || V.back().BranchInstruction != 0u)
	{
		return;
	}
	const auto &Code = Function.Code;
	std::vector<std::uint8_t> StringLocals(Function.LocalCount), EmptyStrings(Function.LocalCount), StringTargets(Function.LocalCount);
	bool CounterInitialized = false;
	bool HasReadonlyQuery = false;
	std::uint64_t LiteralBytes = 0u;
	std::size_t Previous = Code.size();
	for (std::size_t Ip = 0u; Ip < Code.size();)
	{
		const auto Op = static_cast<EOpcode>(Code[Ip]);
		const auto Size = Bytecode::InstructionSize(Op);
		if (!Size || Size > Code.size() - Ip) return;
		const auto Slot = Size >= 3u ? Bytecode::ReadU16LE(Code.data() + Ip + 1u) : 0xFFFFu;
		if (Op == EOpcode::MakeLocalRef || Op == EOpcode::BindLocal || Op == EOpcode::TakeLocal ||
			Op == EOpcode::MakeClosure || Op == EOpcode::LoadGlobal || Op == EOpcode::StoreGlobal)
		{
			return;
		}
		if (Op == EOpcode::CallNative || Op == EOpcode::CallIndirect || Op == EOpcode::CallMethod || Op == EOpcode::CallBoundMethod) return;
		if (Op == EOpcode::Call)
		{
			FPureTextProjection Projection;
			FReadonlyTextBuiltinForwarder Forwarder;
			if (Slot >= Module.Functions.size()) return;
			const bool IsQuery = DescribeReadonlyTextBuiltinForwarder(Module.Functions[Slot], Forwarder);
			if (!IsQuery && !DescribePureTextProjection(Module.Functions[Slot], Projection)) return;
			HasReadonlyQuery = HasReadonlyQuery || IsQuery;
		}
		if (Op == EOpcode::ConstStr)
		{
			if (Slot >= Module.Constants.size()) return;
			const auto &Constant = Module.Constants[Slot];
			std::uint64_t Bytes = 0u;
			if (Constant.Kind == EConstantKind::Str && Constant.StringIndex < Module.StringPool.size())
			{
				Bytes = Module.StringPool[Constant.StringIndex].size();
			}
			else if (Constant.Kind == EConstantKind::WStr && Constant.StringIndex < Module.WStringPool.size())
			{
				if (Module.WStringPool[Constant.StringIndex].size() > NativeTextStartupWork * NativeTextBytesPerWorkUnit / 2u) return;
				Bytes = Module.WStringPool[Constant.StringIndex].size() * 2u;
			}
			else
			{
				return;
			}
			constexpr std::uint64_t LiteralBudget = NativeTextStartupWork * NativeTextBytesPerWorkUnit;
			if (Bytes > LiteralBudget || LiteralBytes > LiteralBudget - Bytes) return;
			LiteralBytes += Bytes;
		}
		const bool Writes = Op == EOpcode::StoreLocal || Op == EOpcode::IncLocal ||
							Op == EOpcode::AddLocalImm || Op == EOpcode::AddLocalLocal;
		if (Writes && Slot == Limit) return;
		if (Writes && Slot == Counter && Ip < Plan.ExitOffset)
		{
			if (Ip == V[Tail].BytecodeOffset)
			{
			}
			else if (!CounterInitialized && Ip < Plan.HeaderOffset && Op == EOpcode::StoreLocal && Previous < Ip &&
					 static_cast<EOpcode>(Code[Previous]) == EOpcode::ConstI32)
			{
				const auto Constant = Bytecode::ReadU16LE(Code.data() + Previous + 1u);
				if (Constant >= Module.Constants.size() || Module.Constants[Constant].Kind != EConstantKind::I32 ||
					Module.Constants[Constant].I32 != 0)
				{
					return;
				}
				CounterInitialized = true;
			}
			else
			{
				return;
			}
		}
		if (Ip < Plan.HeaderOffset)
		{
			// A straight-line scalar/literal prefix cannot mutate the loop bound
			// through a callback, or introduce an unknown large borrowed suffix.
			if (Op != EOpcode::Nop && Op != EOpcode::ConstI32 && Op != EOpcode::ConstStr &&
				Op != EOpcode::LoadLocal && Op != EOpcode::StoreLocal)
			{
				return;
			}
			if (Op == EOpcode::LoadLocal && (Slot >= StringLocals.size() || StringLocals[Slot])) return;
			if (Op == EOpcode::ConstI32 && (Slot >= Module.Constants.size() ||
											   (Module.Constants[Slot].Kind != EConstantKind::I32 && Module.Constants[Slot].Kind != EConstantKind::U32)))
			{
				return;
			}
			if (Op == EOpcode::StoreLocal && Previous < Ip && static_cast<EOpcode>(Code[Previous]) == EOpcode::ConstStr)
			{
				const auto Constant = Bytecode::ReadU16LE(Code.data() + Previous + 1u);
				if (Slot >= StringLocals.size() || Constant >= Module.Constants.size() ||
					Module.Constants[Constant].Kind != EConstantKind::Str)
				{
					return;
				}
				StringLocals[Slot] = 1u;
				if (Module.Constants[Constant].StringIndex >= Module.StringPool.size()) return;
				EmptyStrings[Slot] = Module.StringPool[Module.Constants[Constant].StringIndex].empty();
			}
		}
		if (Op != EOpcode::Nop)
		{
			Previous = Ip;
		}
		Ip += Size;
	}
	if (!CounterInitialized) return;
	for (const auto &I : V)
	{
		if ((I.Opcode == EOpcode::StoreLocal && I.bTextConcatStore) || (I.Opcode == EOpcode::AddLocalLocal && I.bTextAdd))
		{
			if (I.Operand0 >= StringTargets.size() || !StringLocals[I.Operand0]) return;
			StringTargets[I.Operand0] = 1u;
		}
	}
	std::vector<std::uint8_t> LeftReads(V.size(), 0u);
	for (std::size_t Pc = 0u; Pc < V.size(); ++Pc)
	{
		const auto &I = V[Pc];
		if (I.Opcode == EOpcode::ConstI32 && (I.WideImmediateKind != ETypedLoopLocalKind::Unsupported || I.bImmediateF32)) return;
		if (I.Opcode == EOpcode::AddLocalLocal && I.bTextAdd) return;
		if (I.Opcode == EOpcode::StoreLocal && I.Operand0 < StringLocals.size() && StringLocals[I.Operand0])
		{
			if (!I.bTextConcatStore || Pc < 3u || V[Pc - 1u].Opcode != EOpcode::Add) return;
			std::size_t Left = Pc - 1u;
			while (Left && V[Left].EntryStackDepth != 0u)
			{
				--Left;
			}
			if (V[Left].Opcode != EOpcode::LoadLocal || V[Left].Operand0 != I.Operand0) return;
			LeftReads[Left] = 1u;
			for (std::size_t Rhs = Left + 1u; Rhs + 1u < Pc; ++Rhs)
			{
				if (V[Rhs].Opcode == EOpcode::LoadLocal && V[Rhs].Operand0 < StringLocals.size() && StringLocals[V[Rhs].Operand0]) return;
			}
			if (std::any_of(V.begin(), V.end(), [&](const auto &Branch)
					{
						return (Branch.Opcode == EOpcode::Jmp || Branch.Opcode == EOpcode::JmpIfFalse || Branch.Opcode == EOpcode::JmpIfLocalGeImm) &&
							   Branch.BranchInstruction > Left && Branch.BranchInstruction <= Pc;
					}))
			{
				return;
			}
		}
		if (I.Opcode == EOpcode::LoadLocal && I.Operand0 < StringLocals.size() &&
			StringLocals[I.Operand0] && !StringTargets[I.Operand0])
		{
			return;
		}
		switch (I.Opcode)
		{
		case EOpcode::Jmp:
		case EOpcode::JmpIfFalse:
		case EOpcode::JmpIfLocalGeImm:
			{
				if (Pc != Guard && Pc != V.size() - 1u && (I.BranchInstruction <= Pc || I.BranchInstruction > Tail)) return;
				break;
			}
		case EOpcode::CallBuiltin:
			{
				if (I.ArgumentCount != 1u ||
					(static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::ToString &&
						static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::ToInt))
				{
					return;
				}
				break;
			}
		case EOpcode::Nop:
		case EOpcode::ConstI32:
		case EOpcode::ConstStr:
		case EOpcode::ConstBool:
		case EOpcode::LoadLocal:
		case EOpcode::StoreLocal:
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
		case EOpcode::AddLocalLocal:
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
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
		case EOpcode::Neg:
		case EOpcode::BitNot:
		case EOpcode::Not:
		case EOpcode::Dup:
		case EOpcode::Pop:
			{
				break;
			}
		default:
			return;
		}
	}
	std::uint64_t WorkUnits = V.size();
	for (std::size_t Pc = 0u; Pc < V.size(); ++Pc)
	{
		const auto &I = V[Pc];
		if (I.Opcode == EOpcode::LoadLocal && I.Operand0 < StringLocals.size() && StringLocals[I.Operand0] && !LeftReads[Pc]) return;
		if (I.Opcode != EOpcode::ConstStr)
		{
			continue;
		}
		if (Pc + 2u >= V.size() || V[Pc + 1u].Opcode != EOpcode::Add || !V[Pc + 1u].bTextAdd ||
			V[Pc + 2u].Opcode != EOpcode::StoreLocal || !V[Pc + 2u].bTextConcatStore || I.TextValueSlot >= Plan.TextConstants.size())
		{
			return;
		}
		const auto Bytes = Plan.TextConstants[I.TextValueSlot].Size;
		// Appending the literal copies the same bytes in either executor. Only a
		// proved readonly query can amortize that byte work through its borrowed
		// call boundary; large pure appends must not select native prematurely.
		if (HasReadonlyQuery)
		{
			WorkUnits += Bytes / NativeTextBytesPerWorkUnit + (Bytes % NativeTextBytesPerWorkUnit != 0u ? 1u : 0u);
		}
	}
	if (WorkUnits > 0xFFFFu) return;
	std::uint16_t Work = static_cast<std::uint16_t>(WorkUnits);
	if (Entry.TypedLoopPlans->Plans.size() == 2u)
	{
		std::uint16_t Root = 0xFFFFu;
		for (std::size_t Slot = 0u; Slot < StringTargets.size(); ++Slot)
		{
			if (StringTargets[Slot])
			{
				if (Root != 0xFFFFu || !EmptyStrings[Slot]) return;
				Root = static_cast<std::uint16_t>(Slot);
			}
		}
		if (Root == 0xFFFFu || !AddFollowingTextScanCost(Module, Function, Plan,
								   Entry.TypedLoopPlans->Plans[1], Root, StringLocals, Work))
		{
			return;
		}
	}
	const auto StartupWork = NativeTextStartupWork * Entry.TypedLoopPlans->Plans.size();
	Entry.TextEntryVmTripThreshold = static_cast<std::uint16_t>(StartupWork / Work + 1u);
	Entry.TextEntryLimitArgument = Limit;
	Entry.TextEntryLimitImmediate = Immediate;
}
