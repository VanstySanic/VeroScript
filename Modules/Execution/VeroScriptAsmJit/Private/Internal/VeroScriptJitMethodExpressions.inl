// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in the typed-loop planner's private namespace. This-only receiver
// tokens never enter the scalar kernel, so spelling a field explicitly through
// this, or applying an existing bytecode peephole, preserves the same proof.

bool TryBuildFieldI32MethodExpression(
	const FBytecodeModule& Module, const std::uint16_t FunctionIndex,
	FTypedLoopPlan::FFieldI32MethodExpression& Out)
{
	if (FunctionIndex >= Module.Functions.size()) return false;
	const auto& Function = Module.Functions[FunctionIndex];
	JitIr::FSignature Signature;
	if (!Function.bIsMethod || Function.bReturnsVoid || Function.bReturnsRef || Function.bDeleted || Function.bIsAbstract ||
		!Function.UnavailableReason.empty() || !Function.Captures.empty() || Function.ParamCount > 8u ||
		Function.LocalCount < Function.ParamCount || Function.LocalCount > 16u || BytecodeFunctionHasRefParams(Function) ||
		!Function.ExceptionRegions.empty() || !JitIr::ParseSignature(Function.ClosureTypeName, Signature) || !Signature.bValid ||
		Signature.ReturnType != JitIr::EValueType::I32 || Signature.Parameters.size() != Function.ParamCount ||
		!std::all_of(Signature.Parameters.begin(), Signature.Parameters.end(), [](const auto Type)
			{
				return Type == JitIr::EValueType::I32;
			}))
	{
		return false;
	}

	FTypedLoopPlan::FFieldI32MethodExpression Candidate;
	Candidate.FunctionIndex = FunctionIndex;
	Candidate.ArgumentCount = static_cast<std::uint8_t>(Function.ParamCount);
	std::array<bool, 16> Initialized{};
	std::fill_n(Initialized.begin(), Function.ParamCount, true);
	// false denotes an i32 value; true denotes exactly the current this receiver.
	std::vector<bool> Stack;
	std::size_t Depth = 0;
	bool Returned = false;
	const auto PushScalar = [&]()
	{
		Stack.push_back(false);
		++Depth;
	};
	const auto PopScalar = [&]()
	{
		if (Stack.empty() || Stack.back()) return false;
		Stack.pop_back();
		--Depth;
		return true;
	};
	const auto PopThis = [&]()
	{
		if (Stack.empty() || !Stack.back()) return false;
		Stack.pop_back();
		return true;
	};
	const auto Field = [&](const std::uint16_t Name, std::uint16_t& Slot)
	{
		if (Name >= Module.StringPool.size()) return false;
		const auto Found = std::find(Candidate.FieldNameIndices.begin(), Candidate.FieldNameIndices.end(), Name);
		Slot = static_cast<std::uint16_t>(Found - Candidate.FieldNameIndices.begin());
		if (Found == Candidate.FieldNameIndices.end())
		{
			Candidate.FieldNameIndices.push_back(Name);
		}
		return Candidate.FieldNameIndices.size() <= 32u;
	};
	const auto Append = [&](const FTypedLoopInstruction& Instruction)
	{
		if (Candidate.Instructions.size() >= 256u) return false;
		Candidate.Instructions.push_back(Instruction);
		return true;
	};
	for (std::size_t Ip = 0; Ip < Function.Code.size();)
	{
		const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
		const auto Size = Bytecode::InstructionSize(Op);
		if (Returned || Size == 0u || Size > Function.Code.size() - Ip) return false;
		FTypedLoopInstruction I;
		I.Opcode = Op;
		I.BytecodeOffset = static_cast<std::uint32_t>(Ip);
		bool Emit = true;
		switch (Op)
		{
		case EOpcode::Nop:
			{
				Emit = false;
				break;
			}
		case EOpcode::LoadThis:
			{
				Stack.push_back(true);
				Emit = false;
				break;
			}
		case EOpcode::LoadLocal:
		case EOpcode::StoreLocal:
			{
				I.Operand0 = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
				if (I.Operand0 >= Function.LocalCount) return false;
				if (Op == EOpcode::LoadLocal)
				{
					if (!Initialized[I.Operand0]) return false;
					PushScalar();
				}
				else
				{
					if (!PopScalar()) return false;
					Initialized[I.Operand0] = true;
				}
				break;
			}
		case EOpcode::LoadThisField:
		case EOpcode::StoreThisField:
		case EOpcode::LoadField:
		case EOpcode::LoadFieldKeep:
		case EOpcode::StoreField:
		case EOpcode::IncField:
			{
				if (!Field(Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u), I.Operand0)) return false;
				if (Op == EOpcode::LoadThisField)
				{
					PushScalar();
				}
				else if (Op == EOpcode::StoreThisField)
				{
					if (!PopScalar()) return false;
				}
				else if (Op == EOpcode::LoadField || Op == EOpcode::LoadFieldKeep)
				{
					if (Stack.empty() || !Stack.back()) return false;
					if (Op == EOpcode::LoadField)
					{
						Stack.pop_back();
					}
					PushScalar();
					I.Opcode = EOpcode::LoadThisField;
				}
				else if (Op == EOpcode::StoreField)
				{
					if (!PopScalar() || !PopThis() || Depth + 2u > 32u) return false;
					PushScalar();
					auto Dup = I;
					Dup.Opcode = EOpcode::Dup;
					if (!Append(Dup)) return false;
					I.Opcode = EOpcode::StoreThisField;
				}
				else
				{
					if (!PopThis() || Depth + 2u > 32u) return false;
					auto Part = I;
					Part.Opcode = EOpcode::LoadThisField;
					if (!Append(Part)) return false;
					Part.Opcode = EOpcode::ConstI32;
					Part.Immediate = 1;
					if (!Append(Part)) return false;
					Part.Opcode = EOpcode::Add;
					if (!Append(Part)) return false;
					I.Opcode = EOpcode::StoreThisField;
				}
				break;
			}
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
		case EOpcode::AddLocalLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
				if (Slot >= Function.LocalCount || !Initialized[Slot] || Depth + 2u > 32u) return false;
				auto Part = I;
				Part.Opcode = EOpcode::LoadLocal;
				Part.Operand0 = Slot;
				if (!Append(Part)) return false;
				if (Op == EOpcode::AddLocalLocal)
				{
					Part.Operand0 = Bytecode::ReadU16LE(Function.Code.data() + Ip + 3u);
					if (Part.Operand0 >= Function.LocalCount || !Initialized[Part.Operand0]) return false;
				}
				else
				{
					Part.Opcode = EOpcode::ConstI32;
					Part.Immediate = Op == EOpcode::IncLocal ? 1 : Bytecode::ReadI32LE(Function.Code.data() + Ip + 3u);
				}
				if (!Append(Part)) return false;
				Part.Opcode = EOpcode::Add;
				if (!Append(Part)) return false;
				I.Opcode = EOpcode::StoreLocal;
				I.Operand0 = Slot;
				break;
			}
		case EOpcode::ConstI32:
			{
				if (!TryReadI32Constant(Module, Function.Code, Ip, I.Immediate)) return false;
				PushScalar();
				break;
			}
		case EOpcode::Neg:
		case EOpcode::BitNot:
			{
				if (Stack.empty() || Stack.back()) return false;
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
				if (!PopScalar() || !PopScalar()) return false;
				PushScalar();
				break;
			}
		case EOpcode::Dup:
			{
				if (Stack.empty()) return false;
				if (Stack.back())
				{
					Stack.push_back(true);
					Emit = false;
				}
				else
				{
					PushScalar();
				}
				break;
			}
		case EOpcode::Pop:
			{
				if (Stack.empty()) return false;
				if (Stack.back())
				{
					Stack.pop_back();
					Emit = false;
				}
				else if (!PopScalar())
				{
					return false;
				}
				break;
			}
		case EOpcode::Return:
			{
				if (Stack.size() != 1u || Stack.back() || Depth != 1u || Ip + Size != Function.Code.size()) return false;
				Returned = true;
				Emit = false;
				break;
			}
		default:
			return false;
		}
		if (Depth > 32u || Stack.size() > 64u || (Emit && !Append(I))) return false;
		Ip += Size;
	}
	if (!Returned || Candidate.FieldNameIndices.empty()) return false;
	Out = std::move(Candidate);
	return true;
}
