// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in the x64 emitter namespace. Locals and operand slots remain raw
// 64-bit payloads; integer and bounds helpers never enter the FValue bridge.
FTypedLoopI32Kernel EmitTypedLoopWideKernel(JitRuntime &Runtime, const JitLinear::FTypedLoopPlan &Plan)
{
	using namespace JitLinear;
	if (Plan.Instructions.empty()) return nullptr;
	for (const auto &I : Plan.Instructions)
	{
		if (I.Opcode == EOpcode::ConstF64 ||
		    ((I.Opcode == EOpcode::CallMethod || I.Opcode == EOpcode::CallBoundMethod) && I.Immediate != 2) || I.Opcode == EOpcode::LoadField ||
		    I.Opcode == EOpcode::LoadFieldKeep || I.Opcode == EOpcode::StoreField ||
		    I.Opcode == EOpcode::IncField)
		{
			return nullptr;
		}
	}
	CodeHolder Code;
	if (!InitCode(Runtime, Code)) return nullptr;
	x86::Assembler A(&Code);
	std::vector<Label> At;
	for (std::size_t Pc = 0; Pc < Plan.Instructions.size(); ++Pc)
	{
		At.push_back(A.new_label());
	}
	const auto Done = A.new_label();
	const auto Exit = A.new_label();
	std::vector<std::pair<Label, std::uint32_t>> BoundsFailures;
	A.push(x86::rbx);
	A.push(x86::r12);
	A.push(x86::r15);
	A.sub(x86::rsp, 32);
	A.mov(x86::r15, x86::rcx);
	A.mov(x86::rbx,
	    x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Locals))));
	A.mov(x86::r12,
	    x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Stack))));
	const auto Stack = [&](const std::size_t Slot)
	{
		return x86::qword_ptr(x86::r12, static_cast<std::int32_t>(Slot * 8u));
	};
	const auto Local = [&](const std::uint16_t Slot)
	{
		return x86::qword_ptr(x86::rbx, static_cast<std::int32_t>(Slot * 8u));
	};
	const auto Scratch =
	    x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch)));
	const auto Call = [&](const std::size_t Pc)
	{
		A.mov(x86::rcx, x86::r15);
		A.mov(x86::edx, static_cast<std::uint32_t>(Pc));
		A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&TypedLoopWideOperation));
		A.call(x86::rax);
		A.test(x86::eax, x86::eax);
		A.jnz(Exit);
		A.mov(x86::rax, Scratch);
	};
	const auto Arithmetic = [&](const EOpcode Op, const std::size_t Pc)
	{
		const bool Compare = Op >= EOpcode::Eq && Op <= EOpcode::Ge;
		if (!Compare && Op != EOpcode::Add && Op != EOpcode::Sub && Op != EOpcode::Mul &&
		    Op != EOpcode::Div && Op != EOpcode::Mod)
		{
			Call(Pc);
			return;
		}
		const auto Slow = A.new_label();
		const auto Wide = A.new_label();
		const auto After = A.new_label();
		A.mov(x86::r10, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, InstructionKinds))));
		A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)),
		    static_cast<std::uint32_t>(ETypedLoopLocalKind::I32));
		const auto Narrow = A.new_label();
		A.je(Narrow);
		A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)),
		    static_cast<std::uint32_t>(ETypedLoopLocalKind::I64));
		A.je(Wide);
		A.jmp(Slow);
		std::optional<std::int64_t> ImmediateDivisor;
		std::optional<std::int32_t> ImmediateNarrowDivisor;
		if ((Op == EOpcode::Div || Op == EOpcode::Mod) && Pc != 0u)
		{
			const auto &Previous = Plan.Instructions[Pc - 1u];
			const bool bAlternateEntry = std::any_of(Plan.Instructions.begin(), Plan.Instructions.end(),
			    [Pc](const FTypedLoopInstruction &Candidate)
			{
				return Candidate.BranchInstruction == Pc;
			});
			if (!bAlternateEntry && Previous.Opcode == EOpcode::ConstI32 && Previous.NextInstruction == Pc)
			{
				if (Previous.WideImmediateKind == ETypedLoopLocalKind::I64)
				{
					std::int64_t Value = 0;
					std::memcpy(&Value, &Previous.Immediate64, sizeof(Value));
					ImmediateDivisor = Value;
				}
				else if (Previous.WideImmediateKind == ETypedLoopLocalKind::Unsupported && !Previous.bImmediateU32)
				{
					ImmediateDivisor = Previous.Immediate;
					ImmediateNarrowDivisor = Previous.Immediate;
				}
			}
		}
		const auto Emit = [&](const bool IsWide)
		{
			const auto Left = IsWide ? x86::rax : x86::eax;
			const auto Right = IsWide ? x86::r9 : x86::r9d;
			A.mov(Left, IsWide ? x86::r8 : x86::r8d);
			if (Compare)
			{
				A.cmp(Left, Right);
				switch (Op)
				{
				case EOpcode::Eq:
					{
						A.sete(x86::al);
						break;
					}
				case EOpcode::Ne:
					{
						A.setne(x86::al);
						break;
					}
				case EOpcode::Lt:
					{
						A.setl(x86::al);
						break;
					}
				case EOpcode::Le:
					{
						A.setle(x86::al);
						break;
					}
				case EOpcode::Gt:
					{
						A.setg(x86::al);
						break;
					}
				case EOpcode::Ge:
					{
						A.setge(x86::al);
						break;
					}
				default:
					{
						break;
					}
				}
				A.movzx(x86::eax, x86::al);
			}
			else if (Op == EOpcode::Div || Op == EOpcode::Mod)
			{
				if (!IsWide && ImmediateNarrowDivisor)
				{
					EmitSignedDivOrModImmediate32(A, Op, *ImmediateNarrowDivisor,
					    [&](const EJitRuntimeStatus)
					{
						return Slow;
					});
					A.movsxd(x86::rax, x86::eax);
					A.jmp(After);
					return;
				}
				if (IsWide && ImmediateDivisor)
				{
					EmitSignedDivOrModImmediate64(A, Op, *ImmediateDivisor,
					    [&](const EJitRuntimeStatus)
					{
						return Slow;
					});
					A.jmp(After);
					return;
				}
				A.test(Right, Right);
				A.jz(Slow);
				const auto Divide = A.new_label();
				A.cmp(Right, -1);
				A.jne(Divide);
				if (IsWide)
				{
					A.mov(x86::r10, std::uint64_t{1} << 63u);
					A.cmp(Left, x86::r10);
				}
				else
				{
					A.cmp(Left, INT32_MIN);
				}
				A.je(Slow);
				A.bind(Divide);
				if (IsWide)
				{
					A.cqo();
				}
				else
				{
					A.cdq();
				}
				A.idiv(Right);
				if (Op == EOpcode::Mod)
				{
					A.mov(Left, IsWide ? x86::rdx : x86::edx);
				}
			}
			else
			{
				if (Op == EOpcode::Add)
				{
					A.add(Left, Right);
				}
				else if (Op == EOpcode::Sub)
				{
					A.sub(Left, Right);
				}
				else
				{
					A.imul(Left, Right);
				}
				A.jo(Slow);
			}
			if (!IsWide && !Compare)
			{
				A.movsxd(x86::rax, x86::eax);
			}
			A.jmp(After);
		};
		A.bind(Narrow);
		Emit(false);
		A.bind(Wide);
		Emit(true);
		A.bind(Slow);
		// Inputs remain in r8/r9. The authoritative helper handles unsupported
		// kinds and every failure, preserving diagnostics and the partial prefix.
		Call(Pc);
		A.bind(After);
	};
	for (std::size_t Pc = 0; Pc < Plan.Instructions.size(); ++Pc)
	{
		const auto &I = Plan.Instructions[Pc];
		const auto Depth = I.EntryStackDepth;
		A.bind(At[Pc]);
		const auto Branch = I.BranchInstruction < At.size() ? At[I.BranchInstruction] : Done;
		switch (I.Opcode)
		{
		case EOpcode::Nop:
		case EOpcode::Pop:
			{
				break;
			}
		case EOpcode::ConstI32:
		case EOpcode::ConstBool:
			{
				const auto Bits = I.Opcode == EOpcode::ConstBool              ? static_cast<std::uint64_t>(I.Immediate != 0)
				    : I.WideImmediateKind != ETypedLoopLocalKind::Unsupported ? I.Immediate64
				    : I.bImmediateU32
				    ? static_cast<std::uint32_t>(I.Immediate)
				    : static_cast<std::uint64_t>(static_cast<std::int64_t>(I.Immediate));
				A.mov(x86::rax, Bits);
				A.mov(Stack(Depth), x86::rax);
				break;
			}
		case EOpcode::ConstStr:
			{
				A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&Plan.TextConstants[I.TextValueSlot]) | TypedLoopNativeStringTag);
				A.mov(Stack(Depth), x86::rax);
				break;
			}
		case EOpcode::LoadLocal:
			{
				A.mov(x86::rax, Local(I.Operand0));
				A.mov(Stack(Depth), x86::rax);
				break;
			}
		case EOpcode::StoreLocal:
			{
				A.mov(x86::rax, Stack(Depth - 1u));
				if (Plan.bOwnsStringTemporaries)
				{
					const auto Store = A.new_label();
					A.mov(x86::r10, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, InstructionKinds))));
					A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)),
					    static_cast<std::uint32_t>(ETypedLoopLocalKind::String));
					A.jne(Store);
					A.mov(x86::r8, x86::rax);
					if (I.bTextConcatStore)
					{
						A.mov(x86::r9, Stack(Depth));
					}
					else
					{
						A.xor_(x86::r9d, x86::r9d);
					}
					Call(Pc);
					A.bind(Store);
				}
				A.mov(Local(I.Operand0), x86::rax);
				break;
			}
		case EOpcode::Dup:
			{
				A.mov(x86::rax, Stack(Depth - 1u));
				A.mov(Stack(Depth), x86::rax);
				break;
			}
		case EOpcode::Jmp:
			{
				A.jmp(Branch);
				continue;
			}
		case EOpcode::JmpIfFalse:
			{
				A.cmp(Stack(Depth - 1u), 0);
				A.je(Branch);
				break;
			}
		case EOpcode::JmpIfLocalGeImm:
			{
				// Preserve signed/unsigned width in the compact loop guard.
				A.mov(x86::rax, Local(I.Operand0));
				A.mov(x86::r10, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, InstructionKinds))));
				const auto Signed = A.new_label();
				const auto NarrowUnsigned = A.new_label();
				const auto After = A.new_label();
				A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)),
				    static_cast<std::uint32_t>(ETypedLoopLocalKind::U32));
				A.je(NarrowUnsigned);
				A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)),
				    static_cast<std::uint32_t>(ETypedLoopLocalKind::U64));
				A.jne(Signed);
				A.mov(x86::r9d, static_cast<std::uint32_t>(I.Immediate));
				A.cmp(x86::rax, x86::r9);
				A.jae(Branch);
				A.jmp(After);
				A.bind(NarrowUnsigned);
				A.cmp(x86::eax, I.Immediate);
				A.jae(Branch);
				A.jmp(After);
				A.bind(Signed);
				A.cmp(x86::rax, I.Immediate);
				A.jge(Branch);
				A.bind(After);
				break;
			}
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
		case EOpcode::AddLocalLocal:
			{
				A.mov(x86::r8, Local(I.Operand0));
				if (I.Opcode == EOpcode::AddLocalLocal)
				{
					A.mov(x86::r9, Local(I.Operand1));
					if (I.bTextAdd)
					{
						Call(Pc);
						A.mov(Local(I.Operand0), x86::rax);
						break;
					}
				}
				else
				{
					A.mov(x86::r9, static_cast<std::int64_t>(I.Opcode == EOpcode::IncLocal ? 1 : I.Immediate));
				}
				Arithmetic(EOpcode::Add, Pc);
				A.mov(Local(I.Operand0), x86::rax);
				break;
			}
		case EOpcode::GetIndexCopy:
		case EOpcode::SetIndex:
			{
				const bool Store = I.Opcode == EOpcode::SetIndex;
				const auto Base = Depth - (Store ? 3u : 2u);
				const auto Failure = A.new_label();
				BoundsFailures.emplace_back(Failure, I.BytecodeOffset);
				A.mov(x86::r8, Stack(Base));
				A.mov(x86::r9, Stack(Base + 1u));
				// Check all 64 index bits before any scaled addressing or narrowing.
				A.cmp(x86::r9, x86::qword_ptr(x86::r8, static_cast<std::int32_t>(offsetof(FTypedLoopNativeBinding, NativeSize))));
				A.jae(Failure);
				A.mov(x86::r10, x86::qword_ptr(x86::r8, static_cast<std::int32_t>(offsetof(FTypedLoopNativeBinding, NativeData))));
				if (Store)
				{
					A.mov(x86::rax, Stack(Depth - 1u));
					A.mov(x86::dword_ptr(x86::r10, x86::r9, 2), x86::eax);
					A.mov(x86::rax, x86::r8);
					A.inc(x86::qword_ptr(
					    x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, ArraySets))));
				}
				else
				{
					const auto Unsigned = A.new_label();
					A.mov(x86::eax, x86::dword_ptr(x86::r10, x86::r9, 2));
					A.cmp(x86::byte_ptr(x86::r8,
					          static_cast<std::int32_t>(offsetof(FTypedLoopNativeBinding, Kind))),
					    static_cast<std::uint32_t>(ETypedLoopLocalKind::ArrayI32));
					A.jne(Unsigned);
					A.movsxd(x86::rax, x86::eax);
					A.bind(Unsigned);
					A.inc(x86::qword_ptr(
					    x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, ArrayGets))));
				}
				A.mov(Stack(Base), x86::rax);
				break;
			}
		case EOpcode::CallMethod:
		case EOpcode::CallBoundMethod:
			{
				A.mov(x86::r8, Stack(Depth - 2u));
				A.mov(x86::r9, Stack(Depth - 1u));
				Call(Pc);
				A.mov(Stack(Depth - 2u), x86::rax);
				break;
			}
		case EOpcode::CallBuiltin:
			{
				if (I.bTextIdentity || I.TextFormatInstruction != 0xFFFFu)
				{
					break;
				}
				if (static_cast<EBuiltinId>(I.Immediate) == EBuiltinId::CharCode)
				{
					const auto NarrowSigned = A.new_label();
					const auto Check = A.new_label();
					const auto Failure = A.new_label();
					const auto Complete = A.new_label();
					A.mov(x86::r8, Stack(Depth - 2u));
					A.mov(x86::r9, Stack(Depth - 1u));
					A.mov(x86::r10, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, InstructionKinds))));
					A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)), static_cast<std::uint32_t>(ETypedLoopLocalKind::I32));
					A.je(NarrowSigned);
					A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)), static_cast<std::uint32_t>(ETypedLoopLocalKind::U32));
					A.jne(Check);
					A.mov(x86::r9d, x86::r9d);
					A.jmp(Check);
					A.bind(NarrowSigned);
					A.movsxd(x86::r9, x86::r9d);
					A.bind(Check);
					A.btr(x86::r8, 63);
					A.cmp(x86::r9, x86::qword_ptr(x86::r8, static_cast<std::int32_t>(offsetof(FTypedLoopNativeStringView, Size))));
					A.jae(Failure);
					A.mov(x86::r8, x86::qword_ptr(x86::r8, static_cast<std::int32_t>(offsetof(FTypedLoopNativeStringView, Data))));
					A.movzx(x86::eax, x86::byte_ptr(x86::r8, x86::r9));
					A.jmp(Complete);
					A.bind(Failure);
					A.mov(x86::r8, Stack(Depth - 2u));
					A.mov(x86::r9, Stack(Depth - 1u));
					Call(Pc);
					A.bind(Complete);
					A.mov(Stack(Depth - 2u), x86::rax);
					break;
				}
				A.mov(x86::r8, Stack(Depth - 1u));
				A.xor_(x86::r9d, x86::r9d);
				const auto After = A.new_label();
				if (static_cast<EBuiltinId>(I.Immediate) == EBuiltinId::Len)
				{
					const auto ArrayLength = A.new_label();
					const auto CheckLength = A.new_label();
					const auto Slow = A.new_label();
					A.mov(x86::r10, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, InstructionKinds))));
					A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)),
					    static_cast<std::uint32_t>(ETypedLoopLocalKind::String));
					A.jne(ArrayLength);
					A.mov(x86::r10, x86::r8);
					A.btr(x86::r10, 63);
					A.mov(x86::rax, x86::qword_ptr(x86::r10, static_cast<std::int32_t>(offsetof(FTypedLoopNativeStringView, Size))));
					A.jmp(CheckLength);
					A.bind(ArrayLength);
					A.mov(x86::rax, x86::qword_ptr(x86::r8, static_cast<std::int32_t>(offsetof(FTypedLoopNativeBinding, NativeSize))));
					A.bind(CheckLength);
					A.test(x86::rax, x86::rax);
					A.js(Slow);
					A.jmp(After);
					A.bind(Slow);
				}
				if (static_cast<EBuiltinId>(I.Immediate) == EBuiltinId::ToInt64)
				{
					A.mov(x86::r10, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, InstructionKinds))));
					A.mov(x86::rax, x86::r8);
					for (const auto Kind :
					    {ETypedLoopLocalKind::I32, ETypedLoopLocalKind::U32, ETypedLoopLocalKind::I64})
					{
						A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Pc)),
						    static_cast<std::uint32_t>(Kind));
						A.je(After);
					}
				}
				Call(Pc);
				A.bind(After);
				A.mov(Stack(Depth - 1u), x86::rax);
				break;
			}
		case EOpcode::Neg:
		case EOpcode::BitNot:
		case EOpcode::Not:
			{
				A.mov(x86::r8, Stack(Depth - 1u));
				A.xor_(x86::r9d, x86::r9d);
				Call(Pc);
				A.mov(Stack(Depth - 1u), x86::rax);
				break;
			}
		default:
			{
				if (I.bTextAdd)
				{
					break;
				}
				A.mov(x86::r8, Stack(Depth - 2u));
				A.mov(x86::r9, Stack(Depth - 1u));
				Arithmetic(I.Opcode, Pc);
				A.mov(Stack(Depth - 2u), x86::rax);
				break;
			}
		}
		if (I.NextInstruction < At.size())
		{
			A.jmp(At[I.NextInstruction]);
		}
		else
		{
			A.jmp(Done);
		}
	}
	for (const auto &Failure : BoundsFailures)
	{
		A.bind(Failure.first);
		A.mov(x86::dword_ptr(x86::r15,
		          static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, FailureOffset))),
		    Failure.second);
		A.mov(x86::rax, reinterpret_cast<std::uint64_t>("array index out of bounds"));
		A.mov(x86::qword_ptr(x86::r15,
		          static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, FailureMessage))),
		    x86::rax);
		A.mov(x86::eax, static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure));
		A.jmp(Exit);
	}
	A.bind(Done);
	A.xor_(x86::eax, x86::eax);
	A.bind(Exit);
	A.add(x86::rsp, 32);
	A.pop(x86::r15);
	A.pop(x86::r12);
	A.pop(x86::rbx);
	A.ret();
	void *Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FTypedLoopI32Kernel>(Fn) : nullptr;
}
