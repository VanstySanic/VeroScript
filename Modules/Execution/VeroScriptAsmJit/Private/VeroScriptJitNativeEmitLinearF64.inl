// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included inside VeroScript::AsmJit::JitNative anonymous asmjit namespace.
// Homogeneous double scalar CFG (locals/globals/calls are double; bools only as cmp temps).

bool ClosureTypeIsDoubleOnlyLocal(const std::string_view TypeName)
{
	if (TypeName.size() < 6 || TypeName.substr(0, 3) != "fn(")
	{
		return false;
	}
	const std::size_t Close = TypeName.find(')');
	if (Close == std::string_view::npos || Close + 2 >= TypeName.size()
		|| TypeName.substr(Close + 1, 2) != "->")
	{
		return false;
	}
	const std::string_view Params = TypeName.substr(3, Close - 3);
	const std::string_view Ret = TypeName.substr(Close + 3);
	auto Ok = [](const std::string_view T)
	{
		return T.empty() || T == "double" || T == "void";
	};
	if (!Ok(Ret))
	{
		return false;
	}
	for (std::size_t Begin = 0; Begin < Params.size();)
	{
		const std::size_t Comma = Params.find(',', Begin);
		if (!Ok(Params.substr(
					Begin,
					Comma == std::string_view::npos ? std::string_view::npos : Comma - Begin)))
		{
			return false;
		}
		if (Comma == std::string_view::npos)
		{
			break;
		}
		Begin = Comma + 1;
	}
	return true;
}

bool MethodFieldMatchesF64(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::uint16_t NameIndex)
{
	if (!Function.bIsMethod
		|| Function.OwnerTypeIndex >= Module.Types.size()
		|| NameIndex >= Module.StringPool.size())
	{
		return false;
	}
	std::string FieldType;
	if (!FindFieldTypeOnTypeOrBases(
			Module, Function.OwnerTypeIndex, Module.StringPool[NameIndex], FieldType))
	{
		return false;
	}
	return FieldType == "double";
}

bool IsF64LinearCandidate(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function)
{
	if (Function.ClosureTypeName.empty())
	{
		if (!Function.bIsMethod)
		{
			return false;
		}
	}
	else if (!ClosureTypeIsDoubleOnlyLocal(Function.ClosureTypeName))
	{
		return false;
	}
	if (Function.LocalCount > kMaxNativeCfgLocals
		|| Function.Code.size() > kMaxNativeCfgBytecodeBytes)
	{
		return false;
	}
	const auto& Code = Function.Code;
	// A predicate in the F64 scratch stack is encoded as raw 0/1 bits. It is
	// therefore only valid when the immediately following JmpIfFalse is reached
	// by fallthrough from that comparison. Do not let a separately targeted
	// branch enter such an instruction with an arbitrary double in its slot.
	std::vector<bool> IsBranchTarget(Code.size(), false);
	for (std::size_t ScanIp = 0; ScanIp < Code.size();)
	{
		const EOpcode ScanOp = static_cast<EOpcode>(Code[ScanIp]);
		const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
		if (ScanSize == 0 || ScanIp + ScanSize > Code.size())
		{
			return false;
		}
		std::size_t Target = 0;
		const auto MarkBranchTarget = [&](const std::int32_t Offset)
		{
			if (!IsJumpTargetInRangeLocal(ScanIp + ScanSize, Offset, Code.size(), Target)
				|| Target >= IsBranchTarget.size())
			{
				return false;
			}
			IsBranchTarget[Target] = true;
			return true;
		};
		switch (ScanOp)
		{
		case EOpcode::Jmp:
		case EOpcode::JmpIfFalse:
			{
				if (!MarkBranchTarget(Bytecode::ReadI32LE(Code.data() + ScanIp + 1)))
				{
					return false;
				}
				break;
			}
		case EOpcode::JmpIfLocalGeImm:
			{
				if (!MarkBranchTarget(Bytecode::ReadI32LE(Code.data() + ScanIp + 7)))
				{
					return false;
				}
				break;
			}
		default:
			{
				break;
			}
		}
		ScanIp += ScanSize;
	}
	// LoadThis is a phantom receiver (not on the scalar stack). Track Depth the same
	// way Mixed CFG does so Dup/Pop/etc. cannot treat this as a real slot.
	std::size_t PendingThis = 0;
	std::size_t Depth = 0;
	EOpcode PreviousOpcode = EOpcode::Nop;
	auto NeedDepth = [&](const std::size_t N)
	{
		return Depth >= N;
	};
	auto PopDepth = [&](const std::size_t N) -> bool
	{
		if (!NeedDepth(N))
		{
			return false;
		}
		Depth -= N;
		return true;
	};
	for (std::size_t Ip = 0; Ip < Code.size();)
	{
		const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
		const std::size_t Size = Bytecode::InstructionSize(Op);
		if (Ip + Size > Code.size())
		{
			return false;
		}
		switch (Op)
		{
		case EOpcode::Nop:
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
		case EOpcode::AddLocalLocal:
		case EOpcode::Jmp:
		case EOpcode::JmpIfLocalGeImm:
		case EOpcode::Return:
			{
				break;
			}
		case EOpcode::ConstI32:
			{
				// The compiler terminates void functions with `ConstI32(0); Return`.
				// It is a VM return sentinel, not an integer participating in the body.
				// A discarded void builtin expression similarly materializes an integer
				// zero only to keep the bytecode expression stack balanced.
				if (IsVoidReturnSentinelI32(Module, Function, Ip))
				{
					break;
				}
				if ((PreviousOpcode != EOpcode::CallBuiltin)
					|| Ip < Bytecode::InstructionSize(EOpcode::CallBuiltin)
					|| (static_cast<EBuiltinId>(Code[Ip - Bytecode::InstructionSize(EOpcode::CallBuiltin) + 1])
						!= EBuiltinId::Print
						&& static_cast<EBuiltinId>(Code[Ip - Bytecode::InstructionSize(EOpcode::CallBuiltin) + 1])
							!= EBuiltinId::Eprint)
					|| Bytecode::ReadU16LE(Code.data() + Ip + 1) >= Module.Constants.size()
					|| Module.Constants[Bytecode::ReadU16LE(Code.data() + Ip + 1)].Kind != EConstantKind::I32
					|| Module.Constants[Bytecode::ReadU16LE(Code.data() + Ip + 1)].I32 != 0)
				{
					return false;
				}
				++Depth;
				break;
			}
		case EOpcode::ConstF64:
		case EOpcode::LoadLocal:
			{
				++Depth;
				break;
				// F64 CFG stores every operand in a raw double slot.  It cannot preserve
				// an observable bool result without a parallel tag stack, so only a
				// comparison consumed immediately by JmpIfFalse is admissible below.
			}
		case EOpcode::ConstBool:
		case EOpcode::Not:
			return false;
		case EOpcode::StoreLocal:
			{
				if (!PopDepth(1))
				{
					return false;
				}
				break;
			}
		case EOpcode::Add:
		case EOpcode::Sub:
		case EOpcode::Mul:
		case EOpcode::Div:
			{
				if (!PopDepth(2))
				{
					return false;
				}
				++Depth;
				break;
			}
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				// The emitter represents predicates as raw 0/1 bits in its double
				// scratch buffer. They are sound only as an immediate branch condition;
				// any other use would lose the language-level bool type.
				if (Ip + Size >= Code.size()
					|| static_cast<EOpcode>(Code[Ip + Size]) != EOpcode::JmpIfFalse
					|| !PopDepth(2))
				{
					return false;
				}
				++Depth;
				break;
			}
		case EOpcode::Neg:
			{
				if (!NeedDepth(1))
				{
					return false;
				}
				break;
			}
		case EOpcode::Dup:
			{
				if (!NeedDepth(1))
				{
					return false;
				}
				++Depth;
				break;
			}
		case EOpcode::Pop:
			{
				if (!PopDepth(1))
				{
					return false;
				}
				break;
			}
		case EOpcode::JmpIfFalse:
			{
				if ((PreviousOpcode != EOpcode::Eq
						&& PreviousOpcode != EOpcode::Ne
						&& PreviousOpcode != EOpcode::Lt
						&& PreviousOpcode != EOpcode::Le
						&& PreviousOpcode != EOpcode::Gt
						&& PreviousOpcode != EOpcode::Ge)
					|| IsBranchTarget[Ip]
					|| !PopDepth(1))
				{
					return false;
				}
				break;
			}
		case EOpcode::LoadThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (!MethodFieldMatchesF64(Module, Function, Name))
				{
					return false;
				}
				++Depth;
				break;
			}
		case EOpcode::StoreThisField:
		case EOpcode::InitializeThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (!MethodFieldMatchesF64(Module, Function, Name) || !PopDepth(1))
				{
					return false;
				}
				break;
			}
		case EOpcode::LoadThis:
			{
				if (!Function.bIsMethod)
				{
					return false;
				}
				++PendingThis;
				break;
			}
		case EOpcode::CallBoundMethod:
			{
				const auto FnIdx = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto N = Code[Ip + 3];
				if (!Function.bIsMethod
					|| PendingThis == 0
					|| FnIdx >= Module.Functions.size()
					|| !Module.Functions[FnIdx].bIsMethod
					|| Module.Functions[FnIdx].ParamCount != N
					|| !ClosureTypeIsDoubleOnlyLocal(Module.Functions[FnIdx].ClosureTypeName)
					|| !PopDepth(N))
				{
					return false;
				}
				++Depth;
				--PendingThis;
				break;
			}
		case EOpcode::CallMethod:
			{
				const auto NameIdx = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto N = Code[Ip + 3];
				if (!Function.bIsMethod || PendingThis == 0
					|| NameIdx >= Module.StringPool.size())
				{
					return false;
				}
				const std::string& LeafName = Module.StringPool[NameIdx];
				const std::string* FoundClosure = nullptr;
				bool bFoundVoid = false;
				bool bFound = false;
				for (const auto& Candidate : Module.Functions)
				{
					if (!Candidate.bIsMethod || Candidate.ParamCount != N)
					{
						continue;
					}
					if (!MethodLinkageLeafMatches(Candidate.Name, LeafName))
					{
						continue;
					}
					if (!ClosureTypeIsDoubleOnlyLocal(Candidate.ClosureTypeName))
					{
						continue;
					}
					if (bFound
						&& (*FoundClosure != Candidate.ClosureTypeName
							|| bFoundVoid != Candidate.bReturnsVoid))
					{
						return false;
					}
					FoundClosure = &Candidate.ClosureTypeName;
					bFoundVoid = Candidate.bReturnsVoid;
					bFound = true;
				}
				if (!bFound || !PopDepth(N))
				{
					return false;
				}
				++Depth;
				--PendingThis;
				break;
			}
		case EOpcode::IncField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (!Function.bIsMethod
					|| PendingThis == 0
					|| !MethodFieldMatchesF64(Module, Function, Name))
				{
					return false;
				}
				--PendingThis;
				break;
			}
		case EOpcode::LoadGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (Slot >= Module.GlobalTypeNames.size()
					|| Module.GlobalTypeNames[Slot] != "double")
				{
					return false;
				}
				++Depth;
				break;
			}
		case EOpcode::StoreGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (Slot >= Module.GlobalTypeNames.size()
					|| Module.GlobalTypeNames[Slot] != "double"
					|| !PopDepth(1))
				{
					return false;
				}
				break;
			}
		case EOpcode::Call:
			{
				const auto Idx = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto N = Code[Ip + 3];
				if (Idx >= Module.Functions.size()
					|| !ClosureTypeIsDoubleOnlyLocal(Module.Functions[Idx].ClosureTypeName)
					|| Module.Functions[Idx].ParamCount != N
					|| !PopDepth(N))
				{
					return false;
				}
				++Depth;
				break;
			}
		case EOpcode::CallBuiltin:
			{
				const auto B = static_cast<EBuiltinId>(Code[Ip + 1]);
				const auto N = Code[Ip + 2];
				if (B == EBuiltinId::Now || B == EBuiltinId::SteadyNow)
				{
					if (N != 0)
					{
						return false;
					}
					++Depth;
					break;
				}
				if (B == EBuiltinId::Print || B == EBuiltinId::Eprint)
				{
					if (N != 1 || !PopDepth(1))
					{
						return false;
					}
					break;
				}
				if (B != EBuiltinId::Abs && B != EBuiltinId::Min && B != EBuiltinId::Max
					&& B != EBuiltinId::Sqrt && B != EBuiltinId::Sin && B != EBuiltinId::Cos
					&& B != EBuiltinId::Tan && B != EBuiltinId::Floor && B != EBuiltinId::Ceil
					&& B != EBuiltinId::Round && B != EBuiltinId::Trunc && B != EBuiltinId::Exp
					&& B != EBuiltinId::Log && B != EBuiltinId::Log2 && B != EBuiltinId::Asin
					&& B != EBuiltinId::Acos)
				{
					return false;
				}
				if (B == EBuiltinId::Min || B == EBuiltinId::Max)
				{
					if (!PopDepth(2))
					{
						return false;
					}
					++Depth;
				}
				else if (!NeedDepth(1))
				{
					return false;
				}
				break;
			}
		default:
			return false;
		}
		Ip += Size;
		PreviousOpcode = Op;
	}
	return PendingThis == 0;
}

#if VEROSCRIPT_ASMJIT_X64
FLinearF64Kernel EmitLinearF64CfgKernel(
	JitRuntime& Runtime,
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const JitLowering::FPlan* const Plan = nullptr)
{
	if (Plan && (!Plan->bScalarEligible || Plan->LocalCount != Function.LocalCount
			|| Plan->MaxStackSlots > kMaxNativeCfgStackSlots))
	{
		return nullptr;
	}
	if (!IsF64LinearCandidate(Module, Function))
	{
		return nullptr;
	}
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);

	const auto& Bytes = Function.Code;
	const std::size_t CodeSize = Bytes.size();
	const auto CanElideLocalStore = [&](const std::size_t Offset)
	{
		return Plan && std::binary_search(
			Plan->ElidedLocalStoreOffsets.begin(),
			Plan->ElidedLocalStoreOffsets.end(), Offset);
	};
	std::vector<Label> At(CodeSize);
	for (std::size_t Ip = 0; Ip < CodeSize;)
	{
		At[Ip] = A.new_label();
		Ip += Bytecode::InstructionSize(static_cast<EOpcode>(Bytes[Ip]));
	}
	Label Epilogue = A.new_label();
	struct FEncodedFailureSite
	{
		Label Target;
		std::int32_t EncodedFailure = 0;
		bool bPreserveStatus = false;
	};
	std::vector<FEncodedFailureSite> EncodedFailureSites;
	const auto NewFailureSite = [&](const EJitRuntimeStatus Status, const std::size_t Ip)
	{
		const Label Target = A.new_label();
		EncodedFailureSites.push_back(FEncodedFailureSite{
			Target,
			EncodeJitKernelFailure(Status, static_cast<std::uint32_t>(Ip)),
			false});
		return Target;
	};
	const auto NewDynamicFailureSite = [&](const std::size_t Ip)
	{
		const Label Target = A.new_label();
		EncodedFailureSites.push_back(FEncodedFailureSite{
			Target,
			EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure,
				static_cast<std::uint32_t>(Ip)) &
				~static_cast<std::int32_t>(JitRuntimeStatusMask),
			true});
		return Target;
	};
	std::uint16_t SelfFunctionIndex = 0xFFFFu;
	for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
	{
		if (&Module.Functions[Index] == &Function)
		{
			SelfFunctionIndex = static_cast<std::uint16_t>(Index);
			break;
		}
	}
	std::vector<std::optional<FExternalDirectF64Callee>> ExternalDirectCallees(
		Module.Functions.size());
	const auto GetExternalDirectF64Callee = [&](const std::uint16_t CalleeIndex)
		-> const FExternalDirectF64Callee*
	{
		if (CalleeIndex >= Module.Functions.size()
			|| CalleeIndex == SelfFunctionIndex)
		{
			return nullptr;
		}
		auto& Cached = ExternalDirectCallees[CalleeIndex];
		if (!Cached)
		{
			Cached.emplace();
			(void)AnalyzeExternalDirectF64Callee(
				Module, SelfFunctionIndex, CalleeIndex, *Cached);
		}
		return Cached->bEligible ? &*Cached : nullptr;
	};

	// Win64: rcx=Vm, rdx=Locals, r8=OpStack, r9=Out
	A.push(x86::rbx);
	A.push(x86::r12);
	A.push(x86::r13);
	A.push(x86::r14);
	A.push(x86::r15);
	A.sub(x86::rsp, 48);
	A.mov(x86::r15, x86::rcx);
	A.mov(x86::rbx, x86::rdx);
	A.mov(x86::r12, x86::r8);
	A.mov(x86::r14, x86::r9);
	A.xor_(x86::r13d, x86::r13d);
	constexpr std::int32_t RecursionStateStackOffset = 128;

	auto PushXmm0 = [&]()
	{
		A.mov(x86::rax, x86::r13);
		A.shl(x86::rax, 3);
		A.movsd(x86::qword_ptr(x86::r12, x86::rax), x86::xmm0);
		A.inc(x86::r13d);
	};
	auto PopXmm0 = [&]()
	{
		A.dec(x86::r13d);
		A.mov(x86::rax, x86::r13);
		A.shl(x86::rax, 3);
		A.movsd(x86::xmm0, x86::qword_ptr(x86::r12, x86::rax));
	};
	auto PopXmm1 = [&]()
	{
		A.dec(x86::r13d);
		A.mov(x86::rax, x86::r13);
		A.shl(x86::rax, 3);
		A.movsd(x86::xmm1, x86::qword_ptr(x86::r12, x86::rax));
	};
	auto PushBoolAl = [&]()
	{
		A.movzx(x86::eax, x86::al);
		A.mov(x86::rdx, x86::r13);
		A.shl(x86::rdx, 3);
		A.mov(x86::qword_ptr(x86::r12, x86::rdx), x86::rax);
		A.inc(x86::r13d);
	};
	std::size_t CurrentInstructionOffset = InvalidJitInstructionOffset;
	auto CallHelperAndCheck = [&](const std::uint64_t AbsAddr)
	{
		A.mov(x86::rax, AbsAddr);
		A.call(x86::rax);
		A.test(x86::eax, x86::eax);
		A.jnz(NewDynamicFailureSite(CurrentInstructionOffset));
	};

	for (std::size_t Ip = 0; Ip < CodeSize;)
	{
		A.bind(At[Ip]);
		CurrentInstructionOffset = Ip;
		const EOpcode Op = static_cast<EOpcode>(Bytes[Ip]);
		const std::size_t AfterIp = Ip + Bytecode::InstructionSize(Op);
		switch (Op)
		{
		case EOpcode::Nop:
			{
				break;
			}
		case EOpcode::ConstI32:
			{
				// The terminal void sentinel is not a stack value.  A preceding
				// print/eprint expression sentinel is consumed by the following Pop;
				// represent it as a harmless F64 zero to preserve that bytecode shape.
				if (!IsVoidReturnSentinelI32(Module, Function, Ip))
				{
					A.xor_(x86::eax, x86::eax);
					A.movq(x86::xmm0, x86::rax);
					PushXmm0();
				}
				break;
			}
		case EOpcode::ConstF64:
			{
				const auto Idx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Idx >= Module.Constants.size()
					|| Module.Constants[Idx].Kind != EConstantKind::F64)
				{
					return nullptr;
				}
				std::uint64_t Bits = 0;
				std::memcpy(&Bits, &Module.Constants[Idx].F64, sizeof(Bits));
				A.mov(x86::rax, static_cast<std::int64_t>(Bits));
				A.movq(x86::xmm0, x86::rax);
				PushXmm0();
				break;
			}
		case EOpcode::ConstBool:
			{
				A.mov(x86::eax, Bytes[Ip + 1] != 0 ? 1 : 0);
				PushBoolAl();
				break;
			}
		case EOpcode::LoadLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.movsd(x86::xmm0, x86::qword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)));
				PushXmm0();
				break;
			}
		case EOpcode::StoreLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				PopXmm0();
				if (!CanElideLocalStore(Ip))
				{
					A.movsd(x86::qword_ptr(
						x86::rbx, static_cast<int32_t>(Slot * 8)),
						x86::xmm0);
				}
				break;
			}
		case EOpcode::LoadThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				A.lea(x86::r8, x86::qword_ptr(x86::rsp, 40));
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::LoadThisFieldF64));
				A.movsd(x86::xmm0, x86::qword_ptr(x86::rsp, 40));
				PushXmm0();
				break;
			}
		case EOpcode::StoreThisField:
		case EOpcode::InitializeThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				PopXmm0();
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				A.movaps(x86::xmm2, x86::xmm0);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
					Op == EOpcode::InitializeThisField
						? &VeroScript::AsmJit::JitNative::LinearHelpers::InitializeThisFieldF64
						: &VeroScript::AsmJit::JitNative::LinearHelpers::StoreThisFieldF64));
				break;
			}
		case EOpcode::LoadThis:
			{
				break;
			}
		case EOpcode::CallBoundMethod:
		case EOpcode::CallMethod:
			{
				const auto Idx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto N = Bytes[Ip + 3];
				A.sub(x86::r13d, N);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Idx);
				A.mov(x86::rax, x86::r13);
				A.shl(x86::rax, 3);
				A.lea(x86::r8, x86::qword_ptr(x86::r12, x86::rax));
				A.mov(x86::r9d, N);
				A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				if (Op == EOpcode::CallBoundMethod)
				{
					CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisF64));
					if (!Module.Functions[Idx].bReturnsVoid)
					{
						A.movsd(x86::xmm0, x86::qword_ptr(x86::rsp, 40));
						PushXmm0();
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
						A.movq(x86::xmm0, x86::rax);
						PushXmm0();
					}
				}
				else
				{
					bool bCallVoid = false;
					bool bSaw = false;
					bool bAmbiguous = false;
					std::uint16_t ResolvedFnIdx = 0;
					const std::string& LeafName = Module.StringPool[Idx];
					std::uint16_t CandidateIndex = 0;
					for (const auto& Candidate : Module.Functions)
					{
						const std::uint16_t CurrentCandidateIndex = CandidateIndex++;
						if (!Candidate.bIsMethod || Candidate.ParamCount != N)
						{
							continue;
						}
						if (!MethodLinkageLeafMatches(Candidate.Name, LeafName))
						{
							continue;
						}
						if (!ClosureTypeIsDoubleOnlyLocal(Candidate.ClosureTypeName))
						{
							continue;
						}
						if (bSaw)
						{
							bAmbiguous = true;
						}
						else
						{
							ResolvedFnIdx = CurrentCandidateIndex;
							bCallVoid = Candidate.bReturnsVoid;
							bSaw = true;
						}
					}
					A.mov(x86::edx, (!bAmbiguous && bSaw) ? ResolvedFnIdx : Idx);
					CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
							(!bAmbiguous && bSaw)
							? &VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisF64
							: &VeroScript::AsmJit::JitNative::LinearHelpers::CallMethodFromThisF64));
					if (bSaw && !bCallVoid)
					{
						A.movsd(x86::xmm0, x86::qword_ptr(x86::rsp, 40));
						PushXmm0();
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
						A.movq(x86::xmm0, x86::rax);
						PushXmm0();
					}
				}
				break;
			}
		case EOpcode::IncField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::IncThisFieldF64));
				break;
			}
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::int32_t Imm = Op == EOpcode::IncLocal
					? 1
					: Bytecode::ReadI32LE(Bytes.data() + Ip + 3);
				const double ImmF64 = static_cast<double>(Imm);
				std::uint64_t Bits = 0;
				std::memcpy(&Bits, &ImmF64, sizeof(Bits));
				A.movsd(x86::xmm0, x86::qword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)));
				A.mov(x86::rax, static_cast<std::int64_t>(Bits));
				A.movq(x86::xmm1, x86::rax);
				A.addsd(x86::xmm0, x86::xmm1);
				A.movsd(x86::qword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)), x86::xmm0);
				break;
			}
		case EOpcode::AddLocalLocal:
			{
				const auto D = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto S = Bytecode::ReadU16LE(Bytes.data() + Ip + 3);
				A.movsd(x86::xmm0, x86::qword_ptr(x86::rbx, static_cast<int32_t>(D * 8)));
				A.addsd(x86::xmm0, x86::qword_ptr(x86::rbx, static_cast<int32_t>(S * 8)));
				A.movsd(x86::qword_ptr(x86::rbx, static_cast<int32_t>(D * 8)), x86::xmm0);
				break;
			}
		case EOpcode::LoadGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Slot);
				A.lea(x86::r8, x86::qword_ptr(x86::rsp, 40));
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalF64));
				A.movsd(x86::xmm0, x86::qword_ptr(x86::rsp, 40));
				PushXmm0();
				break;
			}
		case EOpcode::StoreGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				PopXmm0();
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Slot);
				// Win64: 3rd arg is double → xmm2 (rcx/rdx already used by pointer/integer args).
				A.movaps(x86::xmm2, x86::xmm0);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalF64));
				break;
			}
		case EOpcode::Call:
			{
				const auto FnIdx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto N = Bytes[Ip + 3];
				A.sub(x86::r13d, N);
				const FExternalDirectF64Callee* const ExternalCallee =
					GetExternalDirectF64Callee(FnIdx);
				if (ExternalCallee && N == Module.Functions[FnIdx].ParamCount)
				{
					const std::size_t OutOffset = 40u;
					const std::size_t StatusOffset = 48u;
					const std::size_t LocalsOffset = 64u;
					const std::size_t StackOffset = LocalsOffset
						+ ExternalCallee->LocalCount * sizeof(double);
					const std::size_t FrameBytes =
						(StackOffset + ExternalCallee->StackSlots * sizeof(double) + 15u)
						& ~15u;
					Label FallbackCall = A.new_label();
					Label LanguageOverflow = A.new_label();
					Label FailureOwnerKnown = A.new_label();
					Label CallDone = A.new_label();

					A.mov(x86::r10, x86::qword_ptr(
						x86::rsp, RecursionStateStackOffset));
					A.test(x86::r10, x86::r10);
					A.jz(FallbackCall);
					A.cmp(x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectF64TargetCount))),
						FnIdx);
					A.jbe(FallbackCall);
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectF64Targets))));
					A.test(x86::r11, x86::r11);
					A.jz(FallbackCall);
					A.mov(x86::r11, x86::qword_ptr(
						x86::r11, static_cast<std::int32_t>(FnIdx * sizeof(void*))));
					A.test(x86::r11, x86::r11);
					A.jz(FallbackCall);

					A.mov(x86::rax, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, ActiveDepth))));
					A.mov(x86::rax, x86::qword_ptr(x86::rax));
					A.cmp(x86::rax, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, LanguageDepthLimit))));
					A.jae(LanguageOverflow);
					A.cmp(x86::rax, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, NativeDepthLimit))));
					A.jae(FallbackCall);
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, ActiveDepth))));
					A.inc(x86::qword_ptr(x86::r11));
					A.inc(x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectCalls))));

					A.sub(x86::rsp, static_cast<std::uint32_t>(FrameBytes));
					A.mov(x86::qword_ptr(x86::rsp, 32), x86::r10);
					for (std::size_t Arg = 0; Arg < N; ++Arg)
					{
						A.movsd(x86::xmm0, x86::qword_ptr(
							x86::r12, x86::r13, 3,
							static_cast<std::int32_t>(Arg * sizeof(double))));
						A.movsd(x86::qword_ptr(x86::rsp,
							static_cast<std::int32_t>(
								LocalsOffset + Arg * sizeof(double))),
							x86::xmm0);
					}
					A.xor_(x86::eax, x86::eax);
					A.movq(x86::xmm0, x86::rax);
					for (std::size_t Local = N;
						Local < ExternalCallee->LocalCount; ++Local)
					{
						A.movsd(x86::qword_ptr(x86::rsp,
							static_cast<std::int32_t>(
								LocalsOffset + Local * sizeof(double))),
							x86::xmm0);
					}
					A.mov(x86::rcx, x86::r15);
					A.lea(x86::rdx, x86::qword_ptr(x86::rsp,
						static_cast<std::int32_t>(LocalsOffset)));
					A.lea(x86::r8, x86::qword_ptr(x86::rsp,
						static_cast<std::int32_t>(StackOffset)));
					A.lea(x86::r9, x86::qword_ptr(x86::rsp,
						static_cast<std::int32_t>(OutOffset)));
					A.mov(x86::r10, x86::qword_ptr(x86::rsp, 32));
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectF64Targets))));
					A.mov(x86::rax, x86::qword_ptr(
						x86::r11, static_cast<std::int32_t>(FnIdx * sizeof(void*))));
					A.call(x86::rax);
					A.mov(x86::dword_ptr(x86::rsp,
						static_cast<std::int32_t>(StatusOffset)),
						x86::eax);
					A.mov(x86::r10, x86::qword_ptr(x86::rsp, 32));
					A.test(x86::eax, x86::eax);
					A.jz(FailureOwnerKnown);
					A.cmp(x86::word_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, FailureFunctionIndex))),
						0xFFFFu);
					A.jne(FailureOwnerKnown);
					A.mov(x86::word_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, FailureFunctionIndex))),
						FnIdx);
					A.bind(FailureOwnerKnown);
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, ActiveDepth))));
					A.dec(x86::qword_ptr(x86::r11));
					A.mov(x86::r10d, x86::dword_ptr(x86::rsp,
						static_cast<std::int32_t>(StatusOffset)));
					if (!Module.Functions[FnIdx].bReturnsVoid)
					{
						A.movsd(x86::xmm0, x86::qword_ptr(x86::rsp,
							static_cast<std::int32_t>(OutOffset)));
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
						A.movq(x86::xmm0, x86::rax);
					}
					A.add(x86::rsp, static_cast<std::uint32_t>(FrameBytes));
					A.test(x86::r10d, x86::r10d);
					A.mov(x86::eax, x86::r10d);
					A.jnz(Epilogue);
					PushXmm0();
					A.jmp(CallDone);

					A.bind(LanguageOverflow);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, static_cast<std::uint32_t>(
						EJitScalarValueKind::F64));
					A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
					A.call(x86::rax);
					A.jmp(NewDynamicFailureSite(Ip));

					A.bind(FallbackCall);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, FnIdx);
					A.mov(x86::rax, x86::r13);
					A.shl(x86::rax, 3);
					A.lea(x86::r8, x86::qword_ptr(x86::r12, x86::rax));
					A.mov(x86::r9d, N);
					A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
					A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
					CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallF64));
					if (!Module.Functions[FnIdx].bReturnsVoid)
					{
						A.movsd(x86::xmm0, x86::qword_ptr(x86::rsp, 40));
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
						A.movq(x86::xmm0, x86::rax);
					}
					PushXmm0();
					A.bind(CallDone);
					break;
				}
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, FnIdx);
				A.mov(x86::rax, x86::r13);
				A.shl(x86::rax, 3);
				A.lea(x86::r8, x86::qword_ptr(x86::r12, x86::rax));
				A.mov(x86::r9d, N);
				A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallF64));
				if (!Module.Functions[FnIdx].bReturnsVoid)
				{
					A.movsd(x86::xmm0, x86::qword_ptr(x86::rsp, 40));
					PushXmm0();
				}
				else
				{
					A.xor_(x86::eax, x86::eax);
					A.movq(x86::xmm0, x86::rax);
					PushXmm0();
				}
				break;
			}
		case EOpcode::CallBuiltin:
			{
				const auto B = static_cast<EBuiltinId>(Bytes[Ip + 1]);
				if (B == EBuiltinId::Now || B == EBuiltinId::SteadyNow)
				{
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, static_cast<std::uint32_t>(B));
					A.lea(x86::r8, x86::qword_ptr(x86::rsp, 40));
					CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::ClockNowF64));
					A.movsd(x86::xmm0, x86::qword_ptr(x86::rsp, 40));
					PushXmm0();
					break;
				}
				if (B == EBuiltinId::Print || B == EBuiltinId::Eprint)
				{
					PopXmm0();
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, static_cast<std::uint32_t>(B));
					A.movaps(x86::xmm2, x86::xmm0);
					CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::OutputF64Builtin));
					break;
				}
				if (B == EBuiltinId::Min || B == EBuiltinId::Max)
				{
					PopXmm1();
					PopXmm0();
					if (B == EBuiltinId::Min)
					{
						A.minsd(x86::xmm0, x86::xmm1);
					}
					else
					{
						A.maxsd(x86::xmm0, x86::xmm1);
					}
					PushXmm0();
					break;
				}
				// Unary math / Abs: operate in-place on stack top via helper.
				A.mov(x86::rax, x86::r13);
				A.shl(x86::rax, 3);
				A.lea(x86::r8, x86::qword_ptr(x86::r12, x86::rax, 0, -8));
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, static_cast<std::uint32_t>(B));
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::MathUnaryF64));
				break;
			}
		case EOpcode::Add:
			{
				PopXmm1();
				PopXmm0();
				A.addsd(x86::xmm0, x86::xmm1);
				PushXmm0();
				break;
			}
		case EOpcode::Sub:
			{
				PopXmm1();
				PopXmm0();
				A.subsd(x86::xmm0, x86::xmm1);
				PushXmm0();
				break;
			}
		case EOpcode::Mul:
			{
				PopXmm1();
				PopXmm0();
				A.mulsd(x86::xmm0, x86::xmm1);
				PushXmm0();
				break;
			}
		case EOpcode::Div:
			{
				PopXmm1();
				PopXmm0();
				A.xorpd(x86::xmm2, x86::xmm2);
				A.ucomisd(x86::xmm1, x86::xmm2);
				Label DoDiv = A.new_label();
				A.jp(DoDiv);
				A.je(NewFailureSite(EJitRuntimeStatus::DivisionByZero, Ip));
				A.bind(DoDiv);
				A.divsd(x86::xmm0, x86::xmm1);
				PushXmm0();
				break;
			}
		case EOpcode::Neg:
			{
				PopXmm0();
				A.mov(x86::rax, static_cast<std::int64_t>(0x8000000000000000ULL));
				A.movq(x86::xmm1, x86::rax);
				A.xorpd(x86::xmm0, x86::xmm1);
				PushXmm0();
				break;
			}
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				PopXmm1();
				PopXmm0();
				const void* Helper = nullptr;
				switch (Op)
				{
				case EOpcode::Eq:
					{
						Helper = reinterpret_cast<const void*>(&VeroScript::AsmJit::JitNative::LinearHelpers::CmpEqF64);
						break;
					}
				case EOpcode::Ne:
					{
						Helper = reinterpret_cast<const void*>(&VeroScript::AsmJit::JitNative::LinearHelpers::CmpNeF64);
						break;
					}
				case EOpcode::Lt:
					{
						Helper = reinterpret_cast<const void*>(&VeroScript::AsmJit::JitNative::LinearHelpers::CmpLtF64);
						break;
					}
				case EOpcode::Le:
					{
						Helper = reinterpret_cast<const void*>(&VeroScript::AsmJit::JitNative::LinearHelpers::CmpLeF64);
						break;
					}
				case EOpcode::Gt:
					{
						Helper = reinterpret_cast<const void*>(&VeroScript::AsmJit::JitNative::LinearHelpers::CmpGtF64);
						break;
					}
				default:
					{
						Helper = reinterpret_cast<const void*>(&VeroScript::AsmJit::JitNative::LinearHelpers::CmpGeF64);
						break;
					}
				}
				A.mov(x86::rax, reinterpret_cast<std::uint64_t>(Helper));
				A.call(x86::rax);
				A.mov(x86::rdx, x86::r13);
				A.shl(x86::rdx, 3);
				A.mov(x86::qword_ptr(x86::r12, x86::rdx), x86::rax);
				A.inc(x86::r13d);
				break;
			}
		case EOpcode::Not:
			{
				A.dec(x86::r13d);
				A.mov(x86::rdx, x86::r13);
				A.shl(x86::rdx, 3);
				A.mov(x86::eax, x86::dword_ptr(x86::r12, x86::rdx));
				A.xor_(x86::eax, 1);
				A.and_(x86::eax, 1);
				A.mov(x86::qword_ptr(x86::r12, x86::rdx), x86::rax);
				A.inc(x86::r13d);
				break;
			}
		case EOpcode::Dup:
			{
				A.mov(x86::rax, x86::r13);
				A.shl(x86::rax, 3);
				A.movsd(x86::xmm0, x86::qword_ptr(x86::r12, x86::rax, 0, -8));
				PushXmm0();
				break;
			}
		case EOpcode::Pop:
			{
				A.dec(x86::r13d);
				break;
			}
		case EOpcode::Jmp:
			{
				std::size_t Target = 0;
				if (!IsJumpTargetInRangeLocal(
						AfterIp,
						Bytecode::ReadI32LE(Bytes.data() + Ip + 1),
						CodeSize,
						Target))
				{
					return nullptr;
				}
				A.jmp(At[Target]);
				break;
			}
		case EOpcode::JmpIfFalse:
			{
				std::size_t Target = 0;
				if (!IsJumpTargetInRangeLocal(
						AfterIp,
						Bytecode::ReadI32LE(Bytes.data() + Ip + 1),
						CodeSize,
						Target))
				{
					return nullptr;
				}
				A.dec(x86::r13d);
				A.mov(x86::rax, x86::r13);
				A.shl(x86::rax, 3);
				A.cmp(x86::dword_ptr(x86::r12, x86::rax), 0);
				A.je(At[Target]);
				break;
			}
		case EOpcode::JmpIfLocalGeImm:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto Imm = Bytecode::ReadI32LE(Bytes.data() + Ip + 3);
				std::size_t Target = 0;
				if (!IsJumpTargetInRangeLocal(
						AfterIp,
						Bytecode::ReadI32LE(Bytes.data() + Ip + 7),
						CodeSize,
						Target))
				{
					return nullptr;
				}
				// Match linear: !(ToDouble(local) < Imm) — true for NaN or local >= Imm.
				A.movsd(x86::xmm0, x86::qword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)));
				const double ImmF64 = static_cast<double>(Imm);
				std::uint64_t Bits = 0;
				std::memcpy(&Bits, &ImmF64, sizeof(Bits));
				A.mov(x86::rax, static_cast<std::int64_t>(Bits));
				A.movq(x86::xmm1, x86::rax);
				A.ucomisd(x86::xmm0, x86::xmm1);
				A.jp(At[Target]);
				A.jae(At[Target]);
				break;
			}
		case EOpcode::Return:
			{
				if (!Function.bReturnsVoid)
				{
					PopXmm0();
					A.test(x86::r14, x86::r14);
					Label Skip = A.new_label();
					A.jz(Skip);
					A.movsd(x86::qword_ptr(x86::r14), x86::xmm0);
					A.bind(Skip);
				}
				A.xor_(x86::eax, x86::eax);
				A.jmp(Epilogue);
				break;
			}
		default:
			return nullptr;
		}
		Ip = AfterIp;
	}

	if (Function.bReturnsVoid)
	{
		A.xor_(x86::eax, x86::eax);
	}
	else
	{
		A.mov(x86::eax, kStatusGenericFailure);
	}
	A.jmp(Epilogue);
	for (const FEncodedFailureSite& FailureSite : EncodedFailureSites)
	{
		A.bind(FailureSite.Target);
		if (FailureSite.bPreserveStatus)
		{
			A.and_(x86::eax, JitRuntimeStatusMask);
			A.or_(x86::eax, FailureSite.EncodedFailure);
		}
		else
		{
			A.mov(x86::eax, FailureSite.EncodedFailure);
		}
		A.jmp(Epilogue);
	}
	A.bind(Epilogue);
	A.add(x86::rsp, 48);
	A.pop(x86::r15);
	A.pop(x86::r14);
	A.pop(x86::r13);
	A.pop(x86::r12);
	A.pop(x86::rbx);
	A.ret();

	void* Fn = nullptr;
	if (!FinalizeKernel(Runtime, Code, &Fn))
	{
		return nullptr;
	}
	return reinterpret_cast<FLinearF64Kernel>(Fn);
}
#endif // VEROSCRIPT_ASMJIT_X64
