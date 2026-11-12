// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in the typed-loop planner after InstructionWritesLocal.
void DescribeObjectArrayPrefix(FTypedLoopPlan& Plan)
{
	if (Plan.FusedKind != FTypedLoopPlan::EFusedKind::None || Plan.bWideCandidate ||
		Plan.bUsesTextOperations || Plan.bHoistsNonEscapingObject || Plan.GenericPureMethods.empty())
	{
		return;
	}
	const auto& V = Plan.Instructions;
	if (V.size() < 6u) return;
	std::uint16_t Counter = 0xFFFFu, Limit = 0xFFFFu;
	std::int32_t Immediate = 0;
	std::size_t Guard = 0;
	const auto IsExit = [&](const FTypedLoopInstruction& I)
	{
		return I.BranchInstruction == InvalidInstruction && I.ExternalBranchOffset == Plan.ExitOffset;
	};
	if (V[0].Opcode == EOpcode::JmpIfLocalGeImm && IsExit(V[0]))
	{
		Counter = V[0].Operand0;
		Immediate = V[0].Immediate;
	}
	else if (V.size() >= 7u && V[0].Opcode == EOpcode::LoadLocal &&
		V[2].Opcode == EOpcode::Lt && V[3].Opcode == EOpcode::JmpIfFalse && IsExit(V[3]) &&
		V[0].NextInstruction == 1u && V[1].NextInstruction == 2u && V[2].NextInstruction == 3u)
	{
		Counter = V[0].Operand0;
		Guard = 3u;
		if (V[1].Opcode == EOpcode::LoadLocal)
		{
			Limit = V[1].Operand0;
		}
		else if (V[1].Opcode == EOpcode::ConstI32 && !V[1].bImmediateU32 &&
				 !V[1].bImmediateF32 && V[1].WideImmediateKind == ETypedLoopLocalKind::Unsupported)
		{
			Immediate = V[1].Immediate;
		}
		else
		{
			return;
		}
	}
	else
	{
		return;
	}
	if (Counter >= Plan.LocalCount || Counter == Limit || (Limit != 0xFFFFu && Limit >= Plan.LocalCount)) return;
	const auto Tail = V.size() - 2u;
	if (V[Tail].Opcode != EOpcode::IncLocal || V[Tail].Operand0 != Counter ||
		V[Tail].NextInstruction != Tail + 1u || V.back().Opcode != EOpcode::Jmp ||
		V.back().BranchInstruction != 0u)
	{
		return;
	}

	std::vector<std::uint16_t> ArrayRoots;
	for (std::size_t Pc = 0; Pc < V.size(); ++Pc)
	{
		const auto& I = V[Pc];
		if (InstructionWritesLocal(I, Counter) && Pc != Tail) return;
		if (Limit != 0xFFFFu && InstructionWritesLocal(I, Limit)) return;
		switch (I.Opcode)
		{
		case EOpcode::CallMethod:
		case EOpcode::CallBoundMethod:
			{
				// Every admitted instance call is independently resolved/proven as a
				// scalar field transform or a pure body before the kernel can start.
				// Container mutations and unknown call adapters retain full preparation.
				if (I.Immediate != 0) return;
				break;
			}
		case EOpcode::CallBuiltin:
			{
				if (static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::Len &&
					static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::ToInt &&
					static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::ToUnsigned)
				{
					return;
				}
				break;
			}
		case EOpcode::GetIndexCopy:
			{
				if (Pc < 2u || V[Pc - 2u].Opcode != EOpcode::LoadLocal ||
					V[Pc - 1u].Opcode != EOpcode::LoadLocal || V[Pc - 1u].Operand0 != Counter ||
					V[Pc - 2u].NextInstruction != Pc - 1u || V[Pc - 1u].NextInstruction != Pc ||
					V[Pc - 2u].Operand0 >= Plan.LocalCount)
				{
					return;
				}
				// No alternate edge may replace either of these actual stack operands.
				if (std::any_of(V.begin(), V.end(), [&](const auto &Branch)
						{
							return (Branch.Opcode == EOpcode::Jmp || Branch.Opcode == EOpcode::JmpIfFalse ||
									   Branch.Opcode == EOpcode::JmpIfLocalGeImm) &&
								   (Branch.BranchInstruction == Pc - 1u || Branch.BranchInstruction == Pc);
						}))
				{
					return;
				}
				ArrayRoots.push_back(V[Pc - 2u].Operand0);
				break;
			}
		case EOpcode::Jmp:
		case EOpcode::JmpIfFalse:
		case EOpcode::JmpIfLocalGeImm:
			{
				if (Pc == Guard || Pc == V.size() - 1u)
				{
					break;
				}
				// Only the canonical tail may go backwards; early exits and jumps into
				// the guard/tail body cannot bypass the monotonic induction proof.
				if (I.BranchInstruction <= Pc || I.BranchInstruction > Tail) return;
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
		case EOpcode::Neg:
		case EOpcode::BitNot:
		case EOpcode::Not:
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
		case EOpcode::Dup:
		case EOpcode::Pop:
		case EOpcode::LoadField:
		case EOpcode::LoadFieldKeep:
		case EOpcode::StoreField:
		case EOpcode::IncField:
			{
				break;
			}
		default:
			return;
		}
	}
	if (ArrayRoots.empty()) return;
	for (const auto &I : V)
	{
		for (const auto Root : ArrayRoots)
		{
			if (InstructionWritesLocal(I, Root)) return;
		}
	}
	Plan.ObjectArrayPrefixCounterSlot = Counter;
	Plan.ObjectArrayPrefixLimitSlot = Limit;
	Plan.ObjectArrayPrefixLimitImmediate = Immediate;
}

