// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included inside VeroScript::AsmJit::JitNative anonymous asmjit namespace.
// Mixed scalar CFG with typed payload slots and primitive leaf lowering.

enum class EMixTag : std::uint8_t
{
	I32 = 0,
	U32 = 1,
	F64 = 2,
	Bool = 3,
	I64 = 4,
	U64 = 5,
	F32 = 6,
	Uninitialized = 7,
	Enum = 8,
	I8 = 9,
	U8 = 10,
	I16 = 11,
	U16 = 12,
};

bool MixTagFromName(const std::string_view Name, EMixTag& Out)
{
	// Narrow integers retain their exact FValue kind at ABI/storage boundaries,
	// but the generated arithmetic slots use the language's integral promotion.
	if (Name == "int8" || Name == "char" || Name == "int16" || Name == "short")
	{
		Out = EMixTag::I32;
		return true;
	}
	if (Name == "uint8" || Name == "unsigned char"
		|| Name == "uint16" || Name == "unsigned short")
	{
		Out = EMixTag::U32;
		return true;
	}
	if (Name == "int")
	{
		Out = EMixTag::I32;
		return true;
	}
	if (Name == "uint" || Name == "unsigned int" || Name == "unsigned")
	{
		Out = EMixTag::U32;
		return true;
	}
	if (Name == "double")
	{
		Out = EMixTag::F64;
		return true;
	}
	if (Name == "float")
	{
		Out = EMixTag::F32;
		return true;
	}
	if (Name == "int64" || Name == "long")
	{
		Out = EMixTag::I64;
		return true;
	}
	if (Name == "uint64" || Name == "unsigned long")
	{
		Out = EMixTag::U64;
		return true;
	}
	if (Name == "bool")
	{
		Out = EMixTag::Bool;
		return true;
	}
	return false;
}

bool IsMixInteger(const EMixTag Tag)
{
	return Tag == EMixTag::I32 || Tag == EMixTag::U32
		|| Tag == EMixTag::I64 || Tag == EMixTag::U64;
}

bool IsMixNumeric(const EMixTag Tag)
{
	return IsMixInteger(Tag) || Tag == EMixTag::F32 || Tag == EMixTag::F64;
}

// Resolve the language's primitive arithmetic promotion, including same-sign
// integer widening, without materializing
// FValue operands.  The VM promotes an integer operand to the floating operand's
// width; keeping that rule in the CFG analysis lets ordinary expressions such as
// `doubleValue * intValue` remain in native code.  U64 is intentionally excluded
// from the x86 fast conversion because cvtsi2sd/cvtsi2ss interpret a 64-bit source
// as signed.  That uncommon high-bit-sensitive case retains the authoritative VM
// path rather than silently changing its value.
bool TryMixNumericPromotion(
	const EMixTag Left,
	const EMixTag Right,
	EMixTag& Out)
{
	if (!IsMixNumeric(Left) || !IsMixNumeric(Right))
	{
		return false;
	}
	if (Left == Right)
	{
		Out = Left;
		return true;
	}
	if ((Left == EMixTag::I32 && Right == EMixTag::I64)
		|| (Left == EMixTag::I64 && Right == EMixTag::I32))
	{
		Out = EMixTag::I64;
		return true;
	}
	if ((Left == EMixTag::U32 && Right == EMixTag::U64)
		|| (Left == EMixTag::U64 && Right == EMixTag::U32))
	{
		Out = EMixTag::U64;
		return true;
	}
	const bool bHasFloat = Left == EMixTag::F32 || Left == EMixTag::F64
		|| Right == EMixTag::F32 || Right == EMixTag::F64;
	if (!bHasFloat || Left == EMixTag::U64 || Right == EMixTag::U64)
	{
		return false;
	}
	Out = Left == EMixTag::F64 || Right == EMixTag::F64
		? EMixTag::F64
		: EMixTag::F32;
	return true;
}

bool MethodFieldMixTag(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::uint16_t NameIndex,
	EMixTag& Out)
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
	return MixTagFromName(FieldType, Out);
}

bool StorageMixTagFromName(const std::string_view Name, EMixTag& Out)
{
	if (Name == "int8" || Name == "char")
	{
		Out = EMixTag::I8;
		return true;
	}
	if (Name == "uint8" || Name == "unsigned char")
	{
		Out = EMixTag::U8;
		return true;
	}
	if (Name == "int16" || Name == "short")
	{
		Out = EMixTag::I16;
		return true;
	}
	if (Name == "uint16" || Name == "unsigned short")
	{
		Out = EMixTag::U16;
		return true;
	}
	return MixTagFromName(Name, Out);
}

bool MethodFieldStorageMixTag(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::uint16_t NameIndex,
	EMixTag& Out)
{
	if (!Function.bIsMethod
		|| Function.OwnerTypeIndex >= Module.Types.size()
		|| NameIndex >= Module.StringPool.size())
	{
		return false;
	}
	std::string FieldType;
	return FindFieldTypeOnTypeOrBases(
			Module, Function.OwnerTypeIndex, Module.StringPool[NameIndex], FieldType)
		&& StorageMixTagFromName(FieldType, Out);
}

struct FMixCfgState
{
	std::vector<EMixTag> Stack;
	std::vector<EMixTag> Locals;
	std::size_t PendingThis = 0;
};

bool ParseMixClosure(
	const std::string_view TypeName,
	std::vector<EMixTag>& OutParams,
	EMixTag& OutRet,
	bool& bVoid)
{
	OutParams.clear();
	bVoid = false;
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
	if (Ret == "void")
	{
		bVoid = true;
	}
	else if (!MixTagFromName(Ret, OutRet))
	{
		return false;
	}
	for (std::size_t Begin = 0; Begin < Params.size();)
	{
		const std::size_t Comma = Params.find(',', Begin);
		EMixTag Tag{};
		if (!MixTagFromName(
				Params.substr(
					Begin,
					Comma == std::string_view::npos ? std::string_view::npos : Comma - Begin),
				Tag))
		{
			return false;
		}
		OutParams.push_back(Tag);
		if (Comma == std::string_view::npos)
		{
			break;
		}
		Begin = Comma + 1;
	}
	return true;
}

bool AnalyzeMixStates(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::vector<EMixTag>& ParamTags,
	const EMixTag ReturnTag,
	const bool bVoidReturn,
	std::vector<std::optional<FMixCfgState>>& OutEntries)
{
	if (Function.ParamCount != ParamTags.size()
		|| Function.LocalCount < Function.ParamCount
		|| Function.LocalCount > kMaxNativeCfgLocals
		|| Function.Code.size() > kMaxNativeCfgBytecodeBytes)
	{
		return false;
	}
	if (Function.Code.empty())
	{
		OutEntries.clear();
		return bVoidReturn;
	}
	const auto& Code = Function.Code;
	const std::size_t CodeSize = Code.size();
	// Narrow signed locals are stored sign-extended in the raw I32 slot. Most
	// operators intentionally promote them to int, but abs() is different: the
	// VM preserves int8/int16 and must diagnose abs(min). Until the mixed state
	// carries exact local widths, conservatively keep only that width-sensitive
	// combination on the ownership-safe path. Other narrow scalar code remains
	// eligible for NativeCfg.
	const auto HasSignedTypeWord = [&](const std::string_view Word)
	{
		const std::string_view TypeName = Function.ClosureTypeName;
		for (std::size_t Pos = TypeName.find(Word); Pos != std::string_view::npos;
			Pos = TypeName.find(Word, Pos + 1))
		{
			const bool bWordStart = Pos == 0
				|| !(std::isalnum(static_cast<unsigned char>(TypeName[Pos - 1]))
					|| TypeName[Pos - 1] == '_');
			const std::size_t End = Pos + Word.size();
			const bool bWordEnd = End == TypeName.size()
				|| !(std::isalnum(static_cast<unsigned char>(TypeName[End]))
					|| TypeName[End] == '_');
			const bool bUnsignedPrefix = Pos >= 9
				&& TypeName.substr(Pos - 9, 9) == "unsigned ";
			if (bWordStart && bWordEnd && !bUnsignedPrefix)
			{
				return true;
			}
		}
		return false;
	};
	bool bMayProduceNarrowSigned = HasSignedTypeWord("int8")
		|| HasSignedTypeWord("int16") || HasSignedTypeWord("char")
		|| HasSignedTypeWord("short");
	for (std::size_t ScanIp = 0; ScanIp < CodeSize;)
	{
		const EOpcode ScanOp = static_cast<EOpcode>(Code[ScanIp]);
		const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
		if (ScanSize == 0 || ScanIp + ScanSize > CodeSize)
		{
			return false;
		}
		if (ScanOp == EOpcode::ConstI32)
		{
			const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + ScanIp + 1);
			if (Index >= Module.Constants.size())
			{
				return false;
			}
			const EConstantKind Kind = Module.Constants[Index].Kind;
			bMayProduceNarrowSigned = bMayProduceNarrowSigned
				|| Kind == EConstantKind::I8 || Kind == EConstantKind::I16;
		}
		else if (ScanOp == EOpcode::CallBuiltin)
		{
			const EBuiltinId Id = static_cast<EBuiltinId>(Code[ScanIp + 1]);
			bMayProduceNarrowSigned = bMayProduceNarrowSigned
				|| Id == EBuiltinId::ToInt8 || Id == EBuiltinId::ToInt16;
		}
		ScanIp += ScanSize;
	}
	OutEntries.assign(CodeSize, std::nullopt);
	std::queue<std::size_t> Work;
	FMixCfgState Initial;
	Initial.Stack.clear();
	// Locals outside the parameter prefix do not have a scalar kind until their
	// first StoreLocal.  Treating them as I32 made a loop back-edge disagree with
	// the entry edge whenever a block-scoped float/i64 local was initialized in
	// the loop body, rejecting the complete Mixed CFG.  Keep a real bottom value
	// instead; a LoadLocal from bottom is still rejected below.
	Initial.Locals.assign(Function.LocalCount, EMixTag::Uninitialized);
	Initial.PendingThis = 0;
	for (std::size_t I = 0; I < ParamTags.size(); ++I)
	{
		Initial.Locals[I] = ParamTags[I];
	}
	OutEntries[0] = Initial;
	Work.push(0);
	bool bSawReturn = false;
	std::size_t Budget = CodeSize * 8 + 64;
	auto Enqueue = [&](const std::size_t Target, const FMixCfgState& State)
	{
		if (Target >= CodeSize)
		{
			return false;
		}
		if (!OutEntries[Target])
		{
			OutEntries[Target] = State;
			Work.push(Target);
			return true;
		}
		FMixCfgState& Existing = *OutEntries[Target];
		if (Existing.Stack != State.Stack
			|| Existing.PendingThis != State.PendingThis
			|| Existing.Locals.size() != State.Locals.size())
		{
			return false;
		}
		bool bChanged = false;
		for (std::size_t Slot = 0; Slot < Existing.Locals.size(); ++Slot)
		{
			const EMixTag Incoming = State.Locals[Slot];
			EMixTag& Current = Existing.Locals[Slot];
			if (Current == Incoming)
			{
				continue;
			}
			// A local which is not initialized on every incoming edge remains
			// unavailable at the merge.  StoreLocal can establish its kind again in
			// the dominated block; LoadLocal will reject it.  Two concrete, unequal
			// kinds remain an invalid mixed CFG.
			if (Current == EMixTag::Uninitialized)
			{
				continue;
			}
			if (Incoming == EMixTag::Uninitialized)
			{
				Current = EMixTag::Uninitialized;
				bChanged = true;
				continue;
			}
			return false;
		}
		if (bChanged)
		{
			Work.push(Target);
		}
		return true;
	};
	auto Need = [](const FMixCfgState& S, const std::size_t N)
	{
		return S.Stack.size() >= N;
	};
	while (!Work.empty())
	{
		if (Budget-- == 0)
		{
			return false;
		}
		const std::size_t Ip = Work.front();
		Work.pop();
		FMixCfgState State = *OutEntries[Ip];
		const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
		const std::size_t Size = Bytecode::InstructionSize(Op);
		if (Size == 0 || Ip + Size > CodeSize)
		{
			return false;
		}
		const std::size_t AfterIp = Ip + Size;
		switch (Op)
		{
		case EOpcode::Nop:
			{
				break;
			}
		case EOpcode::ConstI32:
			{
				const auto I = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (I >= Module.Constants.size())
				{
					return false;
				}
				const auto K = Module.Constants[I].Kind;
				if (K == EConstantKind::I8 || K == EConstantKind::I16
					|| K == EConstantKind::I32)
				{
					State.Stack.push_back(EMixTag::I32);
				}
				else if (K == EConstantKind::U8 || K == EConstantKind::U16
					|| K == EConstantKind::U32)
				{
					State.Stack.push_back(EMixTag::U32);
				}
				else if (K == EConstantKind::I64)
				{
					State.Stack.push_back(EMixTag::I64);
				}
				else if (K == EConstantKind::U64)
				{
					State.Stack.push_back(EMixTag::U64);
				}
				else if (K == EConstantKind::Enum)
				{
					State.Stack.push_back(EMixTag::Enum);
				}
				else
				{
					return false;
				}
				break;
			}
		case EOpcode::ConstF64:
			{
				const auto I = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (I >= Module.Constants.size()
					|| (Module.Constants[I].Kind != EConstantKind::F64
						&& Module.Constants[I].Kind != EConstantKind::F32))
				{
					return false;
				}
				State.Stack.push_back(Module.Constants[I].Kind == EConstantKind::F32
					? EMixTag::F32
						: EMixTag::F64);
				break;
			}
		case EOpcode::ConstBool:
			{
				State.Stack.push_back(EMixTag::Bool);
				break;
			}
		case EOpcode::LoadLocal:
			{
				const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (S >= State.Locals.size()
					|| State.Locals[S] == EMixTag::Uninitialized)
				{
					return false;
				}
				State.Stack.push_back(State.Locals[S]);
				break;
			}
		case EOpcode::StoreLocal:
			{
				const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (S >= State.Locals.size() || !Need(State, 1))
				{
					return false;
				}
				State.Locals[S] = State.Stack.back();
				State.Stack.pop_back();
				break;
			}
		case EOpcode::LoadGlobal:
			{
				const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				EMixTag T{};
				if (S >= Module.GlobalTypeNames.size() || !MixTagFromName(Module.GlobalTypeNames[S], T))
				{
					return false;
				}
				State.Stack.push_back(T);
				break;
			}
		case EOpcode::StoreGlobal:
			{
				const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				EMixTag T{};
				if (S >= Module.GlobalTypeNames.size() || !MixTagFromName(Module.GlobalTypeNames[S], T)
					|| !Need(State, 1) || State.Stack.back() != T)
				{
					return false;
				}
				State.Stack.pop_back();
				break;
			}
		case EOpcode::LoadThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				EMixTag T{};
				if (!MethodFieldMixTag(Module, Function, Name, T))
				{
					return false;
				}
				State.Stack.push_back(T);
				break;
			}
		case EOpcode::StoreThisField:
		case EOpcode::InitializeThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				EMixTag T{};
				if (!MethodFieldMixTag(Module, Function, Name, T)
					|| !Need(State, 1) || State.Stack.back() != T)
				{
					return false;
				}
				State.Stack.pop_back();
				break;
			}
		case EOpcode::LoadThis:
			{
				if (!Function.bIsMethod)
				{
					return false;
				}
				++State.PendingThis;
				break;
			}
		case EOpcode::CallBoundMethod:
			{
				const auto FnIdx = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto N = Code[Ip + 3];
				if (!Function.bIsMethod || State.PendingThis == 0
					|| FnIdx >= Module.Functions.size()
					|| !Module.Functions[FnIdx].bIsMethod)
				{
					return false;
				}
				std::vector<EMixTag> P;
				EMixTag R = EMixTag::I32;
				bool V = false;
				if (!ParseMixClosure(Module.Functions[FnIdx].ClosureTypeName, P, R, V)
					|| Module.Functions[FnIdx].bReturnsVoid != V
					|| Module.Functions[FnIdx].ParamCount != N
					|| P.size() != N
					|| !Need(State, N))
				{
					return false;
				}
				for (std::size_t I = 0; I < N; ++I)
				{
					if (State.Stack[State.Stack.size() - N + I] != P[I])
					{
						return false;
					}
				}
				State.Stack.resize(State.Stack.size() - N);
				State.Stack.push_back(V ? EMixTag::I32 : R);
				--State.PendingThis;
				break;
			}
		case EOpcode::CallMethod:
			{
				const auto NameIdx = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto N = Code[Ip + 3];
				if (!Function.bIsMethod || State.PendingThis == 0
					|| NameIdx >= Module.StringPool.size())
				{
					return false;
				}
				const std::string& LeafName = Module.StringPool[NameIdx];
				std::vector<EMixTag> P;
				EMixTag R = EMixTag::I32;
				bool V = false;
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
					std::vector<EMixTag> CandP;
					EMixTag CandR = EMixTag::I32;
					bool CandV = false;
					if (!ParseMixClosure(Candidate.ClosureTypeName, CandP, CandR, CandV)
						|| Candidate.bReturnsVoid != CandV
						|| CandP.size() != N)
					{
						continue;
					}
					if (bFound && (CandP != P || CandR != R || CandV != V))
					{
						return false;
					}
					P = std::move(CandP);
					R = CandR;
					V = CandV;
					bFound = true;
				}
				if (!bFound || !Need(State, N))
				{
					return false;
				}
				for (std::size_t I = 0; I < N; ++I)
				{
					if (State.Stack[State.Stack.size() - N + I] != P[I])
					{
						return false;
					}
				}
				State.Stack.resize(State.Stack.size() - N);
				State.Stack.push_back(V ? EMixTag::I32 : R);
				--State.PendingThis;
				break;
			}
		case EOpcode::IncField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				EMixTag T{};
				if (!MethodFieldMixTag(Module, Function, Name, T)
					|| State.PendingThis == 0
					|| !IsMixNumeric(T))
				{
					return false;
				}
				--State.PendingThis;
				break;
			}
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
			{
				const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (S >= State.Locals.size()
					|| !IsMixNumeric(State.Locals[S]))
				{
					return false;
				}
				break;
			}
		case EOpcode::AddLocalLocal:
			{
				const auto D = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 3);
				if (D >= State.Locals.size() || S >= State.Locals.size()
					|| State.Locals[D] != State.Locals[S]
					|| !IsMixNumeric(State.Locals[D]))
				{
					return false;
				}
				break;
			}
		case EOpcode::Add:
		case EOpcode::Sub:
		case EOpcode::Mul:
		case EOpcode::Div:
		case EOpcode::Mod:
			{
				EMixTag ResultTag = EMixTag::Uninitialized;
				if (!Need(State, 2)
				|| !TryMixNumericPromotion(
					State.Stack[State.Stack.size() - 2], State.Stack.back(), ResultTag)
				|| (Op == EOpcode::Mod
					&& (ResultTag == EMixTag::F32 || ResultTag == EMixTag::F64)))
				{
					return false;
				}
				State.Stack.pop_back();
				State.Stack.back() = ResultTag;
				break;
			}
		case EOpcode::BitAnd:
		case EOpcode::BitOr:
		case EOpcode::BitXor:
		case EOpcode::Shl:
		case EOpcode::Shr:
			{
				if (!Need(State, 2) || State.Stack.back() != State.Stack[State.Stack.size() - 2]
					|| !IsMixInteger(State.Stack.back()))
				{
					return false;
				}
				State.Stack.pop_back();
				break;
			}
		case EOpcode::Eq:
		case EOpcode::Ne:
			{
				if (!Need(State, 2))
				{
					return false;
				}
				if (State.Stack.back() != State.Stack[State.Stack.size() - 2])
				{
					EMixTag CommonTag = EMixTag::Uninitialized;
					if (!TryMixNumericPromotion(
							State.Stack[State.Stack.size() - 2], State.Stack.back(), CommonTag))
					{
						return false;
					}
				}
				State.Stack.pop_back();
				State.Stack.back() = EMixTag::Bool;
				break;
			}
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				EMixTag CommonTag = EMixTag::Uninitialized;
				if (!Need(State, 2)
				|| !TryMixNumericPromotion(
					State.Stack[State.Stack.size() - 2], State.Stack.back(), CommonTag))
				{
					return false;
				}
				State.Stack.pop_back();
				State.Stack.back() = EMixTag::Bool;
				break;
			}
		case EOpcode::Neg:
			{
				if (!Need(State, 1)
					|| !IsMixNumeric(State.Stack.back()))
				{
					return false;
				}
				break;
			}
		case EOpcode::BitNot:
			{
				if (!Need(State, 1)
					|| !IsMixInteger(State.Stack.back()))
				{
					return false;
				}
				break;
			}
		case EOpcode::Not:
			{
				if (!Need(State, 1) || State.Stack.back() != EMixTag::Bool)
				{
					return false;
				}
				break;
			}
		case EOpcode::Dup:
			{
				if (!Need(State, 1))
				{
					return false;
				}
				State.Stack.push_back(State.Stack.back());
				break;
			}
		case EOpcode::Pop:
			{
				if (!Need(State, 1))
				{
					return false;
				}
				State.Stack.pop_back();
				break;
			}
		case EOpcode::Call:
			{
				const auto C = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto N = Code[Ip + 3];
				if (C >= Module.Functions.size())
				{
					return false;
				}
				std::vector<EMixTag> P;
				EMixTag R = EMixTag::I32;
				bool V = false;
				if (Module.Functions[C].bIsMethod
					|| !ParseMixClosure(Module.Functions[C].ClosureTypeName, P, R, V)
					|| Module.Functions[C].bReturnsVoid != V
					|| Module.Functions[C].ParamCount != N
					|| P.size() != N
					|| !Need(State, N))
				{
					return false;
				}
				for (std::size_t I = 0; I < N; ++I)
				{
					if (State.Stack[State.Stack.size() - N + I] != P[I])
					{
						return false;
					}
				}
				State.Stack.resize(State.Stack.size() - N);
				State.Stack.push_back(V ? EMixTag::I32 : R);
				break;
			}
		case EOpcode::CallBuiltin:
			{
				const auto B = static_cast<EBuiltinId>(Code[Ip + 1]);
				const auto N = Code[Ip + 2];
				if (!Need(State, N))
				{
					return false;
				}
				if (B == EBuiltinId::Now || B == EBuiltinId::SteadyNow
					|| B == EBuiltinId::ReadDouble)
				{
					if (N != 0)
					{
						return false;
					}
					State.Stack.push_back(EMixTag::F64);
				}
				else if (B == EBuiltinId::ReadInt || B == EBuiltinId::ReadBool)
				{
					if (N != 0)
					{
						return false;
					}
					State.Stack.push_back(
						B == EBuiltinId::ReadInt ? EMixTag::I32 : EMixTag::Bool);
				}
				else if (B == EBuiltinId::Exit)
				{
					if (N != 1 || State.Stack.back() != EMixTag::I32)
					{
						return false;
					}
					State.Stack.pop_back();
					// exit() terminates the VM call stack; no bytecode fallthrough exists.
					bSawReturn = true;
					continue;
				}
				else if (B == EBuiltinId::Assert || B == EBuiltinId::Sleep)
				{
					if (N != 1
						|| (B == EBuiltinId::Assert
							? State.Stack.back() != EMixTag::Bool
							: State.Stack.back() != EMixTag::I32))
					{
						return false;
					}
					State.Stack.pop_back();
				}
				else if (B == EBuiltinId::Abs)
				{
					if (N != 1 || !IsMixNumeric(State.Stack.back()))
					{
						return false;
					}
					if (bMayProduceNarrowSigned && State.Stack.back() == EMixTag::I32)
					{
						return false;
					}
				}
				else if (B == EBuiltinId::Print || B == EBuiltinId::Eprint)
				{
					if (N != 1
						|| (!IsMixNumeric(State.Stack.back())
							&& State.Stack.back() != EMixTag::Bool))
					{
						return false;
					}
					// Void builtins consume their arguments. The compiler emits the
					// expression-result sentinel as a following ConstI32 instruction.
					State.Stack.pop_back();
				}
				else if (B == EBuiltinId::Min || B == EBuiltinId::Max)
				{
					if (N != 2 || State.Stack.back() != State.Stack[State.Stack.size() - 2]
						|| !IsMixNumeric(State.Stack.back()))
					{
						return false;
					}
					State.Stack.pop_back();
				}
				else if (B == EBuiltinId::Sqrt || B == EBuiltinId::Sin || B == EBuiltinId::Cos
					|| B == EBuiltinId::Tan || B == EBuiltinId::Floor || B == EBuiltinId::Ceil
					|| B == EBuiltinId::Round || B == EBuiltinId::Trunc || B == EBuiltinId::Exp
					|| B == EBuiltinId::Log || B == EBuiltinId::Log2 || B == EBuiltinId::Asin
					|| B == EBuiltinId::Acos)
				{
					if (N != 1 || !IsMixNumeric(State.Stack.back()))
					{
						return false;
					}
					State.Stack.back() = EMixTag::F64;
				}
				else if (B == EBuiltinId::ToInt
					|| B == EBuiltinId::ToInt8 || B == EBuiltinId::ToInt16)
				{
					if (N != 1)
					{
						return false;
					}
					State.Stack.back() = EMixTag::I32;
				}
				else if (B == EBuiltinId::ToDouble)
				{
					if (N != 1)
					{
						return false;
					}
					State.Stack.back() = EMixTag::F64;
				}
				else if (B == EBuiltinId::ToFloat)
				{
					if (N != 1)
					{
						return false;
					}
					State.Stack.back() = EMixTag::F32;
				}
				else if (B == EBuiltinId::ToInt64)
				{
					if (N != 1)
					{
						return false;
					}
					State.Stack.back() = EMixTag::I64;
				}
				else if (B == EBuiltinId::ToUInt64)
				{
					if (N != 1)
					{
						return false;
					}
					State.Stack.back() = EMixTag::U64;
				}
				else if (B == EBuiltinId::ToUnsigned
					|| B == EBuiltinId::ToUInt8 || B == EBuiltinId::ToUInt16)
				{
					if (N != 1)
					{
						return false;
					}
					State.Stack.back() = EMixTag::U32;
				}
				else
				{
					return false;
				}
				break;
			}
		case EOpcode::Jmp:
			{
				std::size_t T = 0;
				if (!IsJumpTargetInRangeLocal(
						AfterIp,
						Bytecode::ReadI32LE(Code.data() + Ip + 1),
						CodeSize,
						T)
							|| !Enqueue(T, State))
				{
					return false;
				}
				continue;
			}
		case EOpcode::JmpIfFalse:
			{
				std::size_t T = 0;
				if (!Need(State, 1) || State.Stack.back() != EMixTag::Bool)
				{
					return false;
				}
				State.Stack.pop_back();
				if (!IsJumpTargetInRangeLocal(
						AfterIp,
						Bytecode::ReadI32LE(Code.data() + Ip + 1),
						CodeSize,
						T)
							|| !Enqueue(T, State)
							|| !Enqueue(AfterIp, State))
				{
					return false;
				}
				continue;
			}
		case EOpcode::JmpIfLocalGeImm:
			{
				const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				std::size_t T = 0;
				if (S >= State.Locals.size()
					|| !IsMixNumeric(State.Locals[S])
						|| !IsJumpTargetInRangeLocal(
							AfterIp,
							Bytecode::ReadI32LE(Code.data() + Ip + 7),
							CodeSize,
							T)
								|| !Enqueue(T, State)
								|| !Enqueue(AfterIp, State))
				{
					return false;
				}
				continue;
			}
		case EOpcode::Return:
			{
				if (State.PendingThis != 0
					|| (bVoidReturn && State.Stack.size() > 1)
					|| (!bVoidReturn && (State.Stack.size() != 1 || State.Stack.back() != ReturnTag)))
				{
					return false;
				}
				bSawReturn = true;
				continue;
			}
		default:
			return false;
		}
		if (AfterIp >= CodeSize)
		{
			// Void bodies may fall off without a trailing Return (VM accepts that).
			if (State.PendingThis != 0 || !bVoidReturn || State.Stack.size() > 1)
			{
				return false;
			}
			bSawReturn = true;
			continue;
		}
		if (!Enqueue(AfterIp, State))
		{
			return false;
		}
	}
	return bSawReturn;
}

struct FX64F32ClosedRecurrence
{
	std::uint16_t SourceSlot = 0;
	std::uint16_t AccumulatorSlot = 0;
	std::uint16_t CounterSlot = 0;
	std::uint64_t IterationCount = 0;
	std::int64_t Delta = 0;
};

bool TryGetExactI32ConstantLocal(
	const FBytecodeConstant& Constant,
	std::int32_t& Out)
{
	switch (Constant.Kind)
	{
	case EConstantKind::I32:
		{
			Out = Constant.I32;
			return true;
		}
	case EConstantKind::F32:
		{
			if (std::isfinite(Constant.F32)
			&& static_cast<double>(Constant.F32)
				>= static_cast<double>(std::numeric_limits<std::int32_t>::min())
			&& static_cast<double>(Constant.F32)
				<= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
			{
				const auto Candidate = static_cast<std::int32_t>(Constant.F32);
				if (static_cast<float>(Candidate) == Constant.F32)
				{
					Out = Candidate;
					return true;
				}
			}
			return false;
		}
	case EConstantKind::F64:
		{
			if (std::isfinite(Constant.F64)
			&& Constant.F64 >= static_cast<double>(std::numeric_limits<std::int32_t>::min())
			&& Constant.F64 <= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
			{
				const auto Candidate = static_cast<std::int32_t>(Constant.F64);
				if (static_cast<double>(Candidate) == Constant.F64)
				{
					Out = Candidate;
					return true;
				}
			}
			return false;
		}
	default:
		return false;
	}
}

bool TryMatchX64F32ClosedRecurrence(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::vector<EMixTag>& Params,
	const EMixTag ReturnTag,
	const std::vector<std::optional<FMixCfgState>>& Entries,
	FX64F32ClosedRecurrence& Out)
{
	Out = {};
	const auto& Code = Function.Code;
	if (Function.bReturnsVoid || ReturnTag != EMixTag::F32
		|| Function.ParamCount == 0 || Code.empty())
	{
		return false;
	}
	std::size_t Ip = 0;
	const auto Consume = [&](const EOpcode Expected, std::size_t& InOutIp)
	{
		if (InOutIp >= Code.size()
			|| static_cast<EOpcode>(Code[InOutIp]) != Expected)
		{
			return false;
		}
		const std::size_t Size = Bytecode::InstructionSize(Expected);
		if (Size == 0 || Size > Code.size() - InOutIp)
		{
			return false;
		}
		InOutIp += Size;
		return true;
	};
	const std::size_t SourceIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip))
	{
		return false;
	}
	Out.SourceSlot = Bytecode::ReadU16LE(Code.data() + SourceIp + 1);
	if (Out.SourceSlot >= Function.ParamCount || Out.SourceSlot >= Params.size()
		|| Params[Out.SourceSlot] != EMixTag::F32)
	{
		return false;
	}
	const std::size_t AccumulatorStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal, Ip))
	{
		return false;
	}
	Out.AccumulatorSlot = Bytecode::ReadU16LE(Code.data() + AccumulatorStoreIp + 1);

	const std::size_t CounterConstantIp = Ip;
	if (Ip >= Code.size())
	{
		return false;
	}
	const EOpcode CounterConstantOp = static_cast<EOpcode>(Code[Ip]);
	if (CounterConstantOp != EOpcode::ConstI32
		&& CounterConstantOp != EOpcode::ConstF64)
	{
		return false;
	}
	Ip += Bytecode::InstructionSize(CounterConstantOp);
	const std::uint16_t StartConstantIndex =
		Bytecode::ReadU16LE(Code.data() + CounterConstantIp + 1);
	std::int32_t Start = 0;
	if (StartConstantIndex >= Module.Constants.size()
		|| !TryGetExactI32ConstantLocal(Module.Constants[StartConstantIndex], Start))
	{
		return false;
	}
	const std::size_t CounterStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal, Ip))
	{
		return false;
	}
	Out.CounterSlot = Bytecode::ReadU16LE(Code.data() + CounterStoreIp + 1);
	if (Out.CounterSlot == Out.AccumulatorSlot
		|| Out.AccumulatorSlot >= Function.LocalCount
		|| Out.CounterSlot >= Function.LocalCount)
	{
		return false;
	}

	const std::size_t HeaderIp = Ip;
	if (!Consume(EOpcode::JmpIfLocalGeImm, Ip)
		|| Bytecode::ReadU16LE(Code.data() + HeaderIp + 1) != Out.CounterSlot)
	{
		return false;
	}
	const std::int32_t Limit = Bytecode::ReadI32LE(Code.data() + HeaderIp + 3);
	std::size_t ExitIp = 0;
	if (!IsJumpTargetInRangeLocal(
			Ip, Bytecode::ReadI32LE(Code.data() + HeaderIp + 7), Code.size(), ExitIp))
	{
		return false;
	}
	if (HeaderIp >= Entries.size() || !Entries[HeaderIp]
		|| Out.AccumulatorSlot >= Entries[HeaderIp]->Locals.size()
		|| Out.CounterSlot >= Entries[HeaderIp]->Locals.size()
		|| Entries[HeaderIp]->Locals[Out.AccumulatorSlot] != EMixTag::F32
		|| Entries[HeaderIp]->Locals[Out.CounterSlot] != EMixTag::F32)
	{
		return false;
	}

	if (Ip >= Code.size())
	{
		return false;
	}
	const EOpcode AccumulatorUpdate = static_cast<EOpcode>(Code[Ip]);
	if (AccumulatorUpdate != EOpcode::IncLocal
		&& AccumulatorUpdate != EOpcode::AddLocalImm)
	{
		return false;
	}
	if (Bytecode::ReadU16LE(Code.data() + Ip + 1) != Out.AccumulatorSlot)
	{
		return false;
	}
	const std::int32_t Addend = AccumulatorUpdate == EOpcode::IncLocal
		? 1
		: Bytecode::ReadI32LE(Code.data() + Ip + 3);
	Ip += Bytecode::InstructionSize(AccumulatorUpdate);
	const std::size_t CounterUpdateIp = Ip;
	if (!Consume(EOpcode::IncLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + CounterUpdateIp + 1) != Out.CounterSlot)
	{
		return false;
	}
	const std::size_t BackJumpIp = Ip;
	if (!Consume(EOpcode::Jmp, Ip))
	{
		return false;
	}
	std::size_t BackTarget = 0;
	if (!IsJumpTargetInRangeLocal(
			Ip, Bytecode::ReadI32LE(Code.data() + BackJumpIp + 1), Code.size(), BackTarget)
		|| BackTarget != HeaderIp || ExitIp != Ip)
	{
		return false;
	}
	const std::size_t ReturnLoadIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + ReturnLoadIp + 1) != Out.AccumulatorSlot
		|| !Consume(EOpcode::Return, Ip) || Ip != Code.size())
	{
		return false;
	}

	if (Start >= Limit)
	{
		Out.IterationCount = 0;
		Out.Delta = 0;
		return true;
	}
	// A float counter advanced by one is guaranteed to visit the same integer
	// sequence only inside the contiguous exact-integer range of binary32.
	constexpr std::int32_t MaxExactF32Integer = 1 << 24;
	if (Start < -MaxExactF32Integer || Limit > MaxExactF32Integer)
	{
		return false;
	}
	Out.IterationCount = static_cast<std::uint64_t>(
		static_cast<std::int64_t>(Limit) - Start);
	Out.Delta = static_cast<std::int64_t>(Out.IterationCount)
		* static_cast<std::int64_t>(Addend);
	return true;
}

bool CanEmitMixFloatConversionBuiltin(const EBuiltinId Builtin, const EMixTag Tag)
{
	if (Builtin != EBuiltinId::ToDouble && Builtin != EBuiltinId::ToFloat)
	{
		return false;
	}
	return Tag == EMixTag::I32 || Tag == EMixTag::U32 || Tag == EMixTag::I64
		|| Tag == EMixTag::F32 || Tag == EMixTag::F64 || Tag == EMixTag::Bool;
}

struct FMixF64LocalCachePlan
{
	std::vector<int> RegisterIndices;
	std::vector<std::uint16_t> Slots;
	bool bNoHelperLoop = false;
};

FMixF64LocalCachePlan PlanMixF64LocalCache(
	const FBytecodeFunction& Function,
	const std::vector<std::optional<FMixCfgState>>& Entries)
{
	FMixF64LocalCachePlan Result;
	Result.RegisterIndices.assign(Function.LocalCount, -1);
	// These kernels have a separate governed/observed entry path. Keep this
	// cache restricted to primitive free leaves: no alias, receiver or callback
	// can observe a local before the shared native epilogue commits its payload.
	if (Function.bIsMethod || Function.bReturnsRef
		|| BytecodeFunctionHasRefParams(Function) || !Function.Captures.empty()
		|| Function.bUsesLocalStaticStorage || !Function.ExceptionRegions.empty())
	{
		return Result;
	}
	std::vector<bool> SeenF64(Function.LocalCount, false);
	std::vector<bool> UniformF64(Function.LocalCount, true);
	std::vector<std::size_t> Uses(Function.LocalCount, 0u);
	bool bHasLoop = false;
	for (std::size_t Ip = 0; Ip < Function.Code.size();)
	{
		const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
		const auto Size = Bytecode::InstructionSize(Op);
		if (!Entries[Ip])
		{
			Ip += Size;
			continue;
		}
		const auto& State = *Entries[Ip];
		for (std::size_t Slot = 0; Slot < State.Locals.size(); ++Slot)
		{
			const auto Tag = State.Locals[Slot];
			SeenF64[Slot] = SeenF64[Slot] || Tag == EMixTag::F64;
			UniformF64[Slot] = UniformF64[Slot]
				&& (Tag == EMixTag::Uninitialized || Tag == EMixTag::F64);
		}
		switch (Op)
		{
		case EOpcode::LoadLocal:
		case EOpcode::StoreLocal:
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
		case EOpcode::AddLocalLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
				++Uses[Slot];
				if (Op == EOpcode::StoreLocal)
				{
					UniformF64[Slot] = UniformF64[Slot] && State.Stack.back() == EMixTag::F64;
				}
				else if (Op == EOpcode::AddLocalLocal)
				{
					++Uses[Bytecode::ReadU16LE(Function.Code.data() + Ip + 3u)];
				}
				break;
			}
		case EOpcode::Jmp:
		case EOpcode::JmpIfFalse:
			{
				bHasLoop = bHasLoop || Bytecode::ReadI32LE(Function.Code.data() + Ip + 1u) < 0;
				break;
			}
		case EOpcode::JmpIfLocalGeImm:
			{
				bHasLoop = bHasLoop || Bytecode::ReadI32LE(Function.Code.data() + Ip + 7u) < 0;
				++Uses[Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u)];
				break;
			}
		case EOpcode::CallBuiltin:
			{
				if (Function.Code[Ip + 2u] != 1u || State.Stack.empty()
					|| !CanEmitMixFloatConversionBuiltin(
						static_cast<EBuiltinId>(Function.Code[Ip + 1u]), State.Stack.back()))
				{
					return Result;
				}
				break;
			}
		case EOpcode::Nop:
		case EOpcode::ConstI32:
		case EOpcode::ConstF64:
		case EOpcode::ConstBool:
		case EOpcode::Add:
		case EOpcode::Sub:
		case EOpcode::Mul:
		case EOpcode::Div:
		case EOpcode::Mod:
		case EOpcode::BitAnd:
		case EOpcode::BitOr:
		case EOpcode::BitXor:
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
		case EOpcode::Return:
			{
				break;
			}
		default:
			return Result;
		}
		Ip += Size;
	}
	if (!bHasLoop)
	{
		return Result;
	}
	Result.bNoHelperLoop = true;
	for (std::uint16_t Slot = 0; Slot < Function.LocalCount; ++Slot)
	{
		if (SeenF64[Slot] && UniformF64[Slot] && Uses[Slot] >= 2u)
		{
			Result.Slots.push_back(Slot);
		}
	}
	std::stable_sort(Result.Slots.begin(), Result.Slots.end(),
		[&](const std::uint16_t Left, const std::uint16_t Right)
		{
			return Uses[Left] > Uses[Right];
		});
	// xmm0-3 are existing emitter scratch; Win64 preserves all 128 bits of
	// xmm6-15. Bound the cache so larger functions keep ordinary memory slots.
	if (Result.Slots.size() > 10u)
	{
		Result.Slots.resize(10u);
	}
	for (std::size_t Index = 0; Index < Result.Slots.size(); ++Index)
	{
		Result.RegisterIndices[Result.Slots[Index]] = static_cast<int>(Index);
	}
	return Result;
}

FLinearMixedKernel EmitLinearMixedCfgKernel(
	JitRuntime& Runtime,
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const JitLowering::FPlan* const Plan = nullptr,
	const bool bEnableF64StackRegisters = true)
{
	std::vector<EMixTag> Params;
	EMixTag ReturnTag = EMixTag::I32;
	bool bVoid = false;
	if (!ParseMixClosure(Function.ClosureTypeName, Params, ReturnTag, bVoid)
		|| Function.bReturnsVoid != bVoid)
	{
		return nullptr;
	}
	std::vector<std::optional<FMixCfgState>> Entries;
	if (!AnalyzeMixStates(Module, Function, Params, ReturnTag, bVoid, Entries))
	{
		return nullptr;
	}
	const auto F64LocalCache = PlanMixF64LocalCache(Function, Entries);
	const bool bUseF64StackRegisters = bEnableF64StackRegisters && F64LocalCache.bNoHelperLoop;
	const auto CachedLocalRegister = [&](const std::size_t Slot)
	{
		return x86::xmm(static_cast<std::uint32_t>(6 + F64LocalCache.RegisterIndices[Slot]));
	};
	const std::uint32_t NativeFrameBytes =
		64u + static_cast<std::uint32_t>(F64LocalCache.Slots.size()) * 16u;
	struct FDirectI32Callee : FExternalDirectI32Callee
	{
		bool bEuclideanTailRecurrence = false;
		std::uint32_t EuclideanModOffset = InvalidJitInstructionOffset;
		std::uint32_t EuclideanCallOffset = InvalidJitInstructionOffset;
	};
	std::uint16_t MixedFunctionIndex = 0xFFFFu;
	for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
	{
		if (&Module.Functions[Index] == &Function)
		{
			MixedFunctionIndex = static_cast<std::uint16_t>(Index);
			break;
		}
	}
	std::vector<std::optional<FDirectI32Callee>> DirectI32Callees(
		Module.Functions.size());
	const auto GetDirectI32Callee = [&](const std::uint16_t FunctionIndex)
		-> const FDirectI32Callee*
	{
		if (FunctionIndex >= Module.Functions.size())
		{
			return nullptr;
		}
		auto& Cached = DirectI32Callees[FunctionIndex];
		if (!Cached)
		{
			Cached.emplace();
			FDirectI32Callee& Candidate = *Cached;
			const FBytecodeFunction& Callee = Module.Functions[FunctionIndex];
			if (!AnalyzeExternalDirectI32Callee(
					Module, MixedFunctionIndex, FunctionIndex, Candidate))
			{
				return nullptr;
			}
			std::vector<std::optional<FI32CfgTagState>> CalleeEntries;
			if (!AnalyzeI32BoolCfgTags(Module, Callee, CalleeEntries))
			{
				return nullptr;
			}
			FX64I32EuclideanTailRecurrence Euclidean;
			Candidate.bEuclideanTailRecurrence =
				TryMatchX64I32EuclideanTailRecurrence(
					Module, Callee, FunctionIndex, CalleeEntries, Euclidean);
			if (Candidate.bEuclideanTailRecurrence)
			{
				Candidate.EuclideanModOffset = Euclidean.ModInstructionOffset;
				Candidate.EuclideanCallOffset = Euclidean.CallInstructionOffset;
			}
		}
		return Cached->bEligible ? &*Cached : nullptr;
	};
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	bool bEmittedCall = false;
	const auto EmitCallRax = [&]()
	{
		bEmittedCall = true;
		A.call(x86::rax);
	};
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
	Label CachedReturn = A.new_label();
	Label RestoreRegisters = A.new_label();
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

	A.push(x86::rbx);
	A.push(x86::r12);
	A.push(x86::r13);
	A.push(x86::r14);
	A.push(x86::r15);
	A.sub(x86::rsp, NativeFrameBytes);
	A.mov(x86::r15, x86::rcx); // Vm
	A.mov(x86::rbx, x86::rdx); // Locals
	A.mov(x86::r12, x86::r8); // OpStack
	A.mov(x86::r14, x86::r9); // Out
	A.xor_(x86::r13d, x86::r13d);
	for (std::size_t Index = 0; Index < F64LocalCache.Slots.size(); ++Index)
	{
		const auto Slot = F64LocalCache.Slots[Index];
		const auto Reg = CachedLocalRegister(Slot);
		A.movdqu(x86::ptr(x86::rsp, static_cast<std::int32_t>(64u + Index * 16u)), Reg);
		if (Slot < Function.ParamCount)
		{
			A.movq(Reg, x86::qword_ptr(x86::rbx, static_cast<std::int32_t>(Slot * 8u)));
			A.or_(x86::r13d, 1u << Index);
		}
	}
	std::size_t CurrentInstructionOffset = InvalidJitInstructionOffset;
	const auto MarkF64LocalInitialized = [&](const std::size_t Slot, const int CacheIndex)
	{
		// The existing CFG proof establishes a prior write on every entry edge.
		if (CurrentInstructionOffset < Entries.size() && Entries[CurrentInstructionOffset]
			&& Entries[CurrentInstructionOffset]->Locals[Slot] == EMixTag::F64)
		{
			return;
		}
		A.or_(x86::r13d, 1u << CacheIndex);
	};
	const auto LoadLocalRaw = [&](const x86::Gp Reg, const std::size_t Slot)
	{
		if (F64LocalCache.RegisterIndices[Slot] >= 0)
		{
			A.movq(Reg, CachedLocalRegister(Slot));
		}
		else
		{
			A.mov(Reg, x86::qword_ptr(x86::rbx, static_cast<std::int32_t>(Slot * 8u)));
		}
	};
	const auto StoreRaxToLocal = [&](const std::size_t Slot)
	{
		const auto CacheIndex = F64LocalCache.RegisterIndices[Slot];
		if (CacheIndex >= 0)
		{
			A.movq(CachedLocalRegister(Slot), x86::rax);
			MarkF64LocalInitialized(Slot, CacheIndex);
		}
		else
		{
			A.mov(x86::qword_ptr(x86::rbx, static_cast<std::int32_t>(Slot * 8u)), x86::rax);
		}
	};
	const auto LoadF64Local = [&](const x86::Vec Reg, const std::size_t Slot)
	{
		if (F64LocalCache.RegisterIndices[Slot] >= 0)
		{
			A.movapd(Reg, CachedLocalRegister(Slot));
		}
		else
		{
			A.movsd(Reg, x86::qword_ptr(x86::rbx, static_cast<std::int32_t>(Slot * 8u)));
		}
	};
	const auto StoreF64Local = [&](const std::size_t Slot, const x86::Vec Reg)
	{
		const auto CacheIndex = F64LocalCache.RegisterIndices[Slot];
		if (CacheIndex >= 0)
		{
			A.movapd(CachedLocalRegister(Slot), Reg);
			MarkF64LocalInitialized(Slot, CacheIndex);
		}
		else
		{
			A.movsd(x86::qword_ptr(x86::rbx, static_cast<std::int32_t>(Slot * 8u)), Reg);
		}
	};
	const auto FlushF64Locals = [&]()
	{
		for (std::size_t Index = 0; Index < F64LocalCache.Slots.size(); ++Index)
		{
			const auto Slot = F64LocalCache.Slots[Index];
			const auto SkipUninitialized = A.new_label();
			// A branch or an earlier checked failure may bypass the first store.
			// Commit only values initialized along the actual execution path.
			A.test(x86::r13d, 1u << Index);
			A.jz(SkipUninitialized);
			A.movq(x86::qword_ptr(x86::rbx, static_cast<std::int32_t>(Slot * 8u)), CachedLocalRegister(Slot));
			A.bind(SkipUninitialized);
		}
	};

	FX64F32ClosedRecurrence ClosedF32Recurrence;
	if (TryMatchX64F32ClosedRecurrence(
			Module, Function, Params, ReturnTag, Entries, ClosedF32Recurrence))
	{
		Label FallbackToOrdinaryCfg = At[0];
		if (ClosedF32Recurrence.IterationCount == 0)
		{
			A.mov(x86::rax, x86::qword_ptr(
				x86::rbx,
				static_cast<std::int32_t>(ClosedF32Recurrence.SourceSlot * 8u)));
		}
		else
		{
			constexpr std::int32_t MaxExactF32Integer = 1 << 24;
			A.mov(x86::eax, x86::dword_ptr(
				x86::rbx,
				static_cast<std::int32_t>(ClosedF32Recurrence.SourceSlot * 8u)));
			A.movd(x86::xmm0, x86::eax);
			A.cvttss2si(x86::ecx, x86::xmm0);
			A.cvtsi2ss(x86::xmm1, x86::ecx);
			A.ucomiss(x86::xmm0, x86::xmm1);
			A.jp(FallbackToOrdinaryCfg);
			A.jne(FallbackToOrdinaryCfg);
			A.cmp(x86::ecx, MaxExactF32Integer);
			A.jg(FallbackToOrdinaryCfg);
			A.cmp(x86::ecx, -MaxExactF32Integer);
			A.jl(FallbackToOrdinaryCfg);
			A.movsxd(x86::rdx, x86::ecx);
			if (ClosedF32Recurrence.Delta != 0)
			{
				A.mov(x86::rax, ClosedF32Recurrence.Delta);
				A.add(x86::rdx, x86::rax);
			}
			A.cmp(x86::rdx, MaxExactF32Integer);
			A.jg(FallbackToOrdinaryCfg);
			A.cmp(x86::rdx, -MaxExactF32Integer);
			A.jl(FallbackToOrdinaryCfg);
			A.cvtsi2ss(x86::xmm0, x86::edx);
			A.movd(x86::eax, x86::xmm0);
		}
		Label NoClosedF32Out = A.new_label();
		A.test(x86::r14, x86::r14);
		A.jz(NoClosedF32Out);
		A.mov(x86::qword_ptr(x86::r14), x86::rax);
		A.bind(NoClosedF32Out);
		A.xor_(x86::eax, x86::eax);
		A.jmp(Epilogue);
	}

	std::size_t NativeStackDepth = 0;
	// A stack position has the same type on every incoming edge. Only F64
	// payloads use xmm4/xmm5; integer, bool and F32 operands retain memory slots.
	// The initial stack is empty and AnalyzeMixStates proves write-before-read.
	const auto IsF64OperandAt = [&](const std::size_t Ip, const std::size_t Depth)
	{
		return Ip < Entries.size() && Entries[Ip] && Depth < Entries[Ip]->Stack.size()
			&& Entries[Ip]->Stack[Depth] == EMixTag::F64;
	};
	const auto LoadOperandSlotToRax = [&](const std::size_t Depth)
	{
		if (bUseF64StackRegisters && Depth < 2u && IsF64OperandAt(CurrentInstructionOffset, Depth))
		{
			A.movq(x86::rax, x86::xmm(static_cast<std::uint32_t>(4u + Depth)));
		}
		else
		{
			A.mov(x86::rax, x86::qword_ptr(x86::r12, static_cast<std::int32_t>(Depth * 8u)));
		}
	};
	auto PushQwordFromRax = [&]()
	{
		const auto NextIp = CurrentInstructionOffset + Bytecode::InstructionSize(static_cast<EOpcode>(Bytes[CurrentInstructionOffset]));
		if (bUseF64StackRegisters && NativeStackDepth < 2u && IsF64OperandAt(NextIp, NativeStackDepth))
		{
			A.movq(x86::xmm(static_cast<std::uint32_t>(4u + NativeStackDepth)), x86::rax);
		}
		else
		{
			A.mov(x86::qword_ptr(x86::r12,
				static_cast<std::int32_t>(NativeStackDepth * 8u)),
				x86::rax);
		}
		++NativeStackDepth;
	};
	auto PopToRax = [&]()
	{
		--NativeStackDepth;
		LoadOperandSlotToRax(NativeStackDepth);
	};
	const auto PopF64 = [&](const x86::Vec Reg)
	{
		--NativeStackDepth;
		if (NativeStackDepth < 2u)
		{
			A.movapd(Reg, x86::xmm(static_cast<std::uint32_t>(4u + NativeStackDepth)));
		}
		else
		{
			A.movsd(Reg, x86::qword_ptr(x86::r12, static_cast<std::int32_t>(NativeStackDepth * 8u)));
		}
	};
	const auto PushF64 = [&](const x86::Vec Reg)
	{
		if (NativeStackDepth < 2u)
		{
			A.movapd(x86::xmm(static_cast<std::uint32_t>(4u + NativeStackDepth)), Reg);
		}
		else
		{
			A.movsd(x86::qword_ptr(x86::r12, static_cast<std::int32_t>(NativeStackDepth * 8u)), Reg);
		}
		++NativeStackDepth;
	};
	const auto EmitF64StackArithmetic = [&](const EOpcode Op)
	{
		if (Op != EOpcode::Add && Op != EOpcode::Sub && Op != EOpcode::Mul && Op != EOpcode::Div)
		{
			return false;
		}
		PopF64(x86::xmm1);
		PopF64(x86::xmm0);
		if (Op == EOpcode::Div)
		{
			const auto Divide = A.new_label();
			A.xorpd(x86::xmm2, x86::xmm2);
			A.ucomisd(x86::xmm1, x86::xmm2);
			A.jp(Divide);
			A.je(NewFailureSite(EJitRuntimeStatus::DivisionByZero, CurrentInstructionOffset));
			A.bind(Divide);
			A.divsd(x86::xmm0, x86::xmm1);
		}
		else if (Op == EOpcode::Add)
		{
			A.addsd(x86::xmm0, x86::xmm1);
		}
		else if (Op == EOpcode::Sub)
		{
			A.subsd(x86::xmm0, x86::xmm1);
		}
		else
		{
			A.mulsd(x86::xmm0, x86::xmm1);
		}
		PushF64(x86::xmm0);
		return true;
	};
	auto CallHelperAndCheck = [&](const std::uint64_t AbsAddr)
	{
		A.mov(x86::rax, AbsAddr);
		EmitCallRax();
		A.test(x86::eax, x86::eax);
		A.jnz(NewDynamicFailureSite(CurrentInstructionOffset));
	};
	// Inputs: r8/r9 contain raw left/right scalar payloads. Result: rax.
	// Keep the common Add operation inside the generated kernel; this removes a
	// helper call from int64/uint64/float loops while preserving signed overflow.
	auto EmitAdd = [&](const EMixTag Tag) -> bool
	{
		switch (Tag)
		{
		case EMixTag::I32:
			{
				A.add(x86::r8d, x86::r9d);
				A.jo(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
				A.mov(x86::eax, x86::r8d);
				return true;
			}
		case EMixTag::U32:
			{
				A.add(x86::r8d, x86::r9d);
				A.mov(x86::eax, x86::r8d);
				return true;
			}
		case EMixTag::I64:
			{
				A.add(x86::r8, x86::r9);
				A.jo(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
				A.mov(x86::rax, x86::r8);
				return true;
			}
		case EMixTag::U64:
			{
				A.add(x86::r8, x86::r9);
				A.mov(x86::rax, x86::r8);
				return true;
			}
		case EMixTag::F32:
			{
				A.movd(x86::xmm0, x86::r8d);
				A.movd(x86::xmm1, x86::r9d);
				A.addss(x86::xmm0, x86::xmm1);
				A.movd(x86::eax, x86::xmm0);
				return true;
			}
		case EMixTag::F64:
			{
				A.movq(x86::xmm0, x86::r8);
				A.movq(x86::xmm1, x86::r9);
				A.addsd(x86::xmm0, x86::xmm1);
				A.movq(x86::rax, x86::xmm0);
				return true;
			}
		default:
			return false;
		}
	};
	// Inputs: r8/r9 contain raw left/right scalar payloads. Result: rax.
	// Mixed CFGs are common even for otherwise primitive loops (for example an
	// int induction variable beside a uint or double accumulator).  Calling the
	// generic typed helper for every primitive operation made those loops pay a
	// C++ ABI transition per opcode. Keep operations whose required checks are
	// representable directly in the generated kernel; the helper remains
	// authoritative for modulo, shifts, and any operation with richer semantics.
	auto EmitSimpleBinary = [&](const EMixTag Tag, const EOpcode Op) -> bool
	{
		if (Op == EOpcode::Add)
		{
			return EmitAdd(Tag);
		}
		const bool bComparison = Op == EOpcode::Eq || Op == EOpcode::Ne
			|| Op == EOpcode::Lt || Op == EOpcode::Le
			|| Op == EOpcode::Gt || Op == EOpcode::Ge;
		if (Tag == EMixTag::F32 || Tag == EMixTag::F64)
		{
			if (bComparison)
			{
				return false;
			}
			if (Op != EOpcode::Sub && Op != EOpcode::Mul && Op != EOpcode::Div)
			{
				return false;
			}
			if (Tag == EMixTag::F32)
			{
				A.movd(x86::xmm0, x86::r8d);
				A.movd(x86::xmm1, x86::r9d);
				if (Op == EOpcode::Sub)
				{
					A.subss(x86::xmm0, x86::xmm1);
				}
				else if (Op == EOpcode::Mul)
				{
					A.mulss(x86::xmm0, x86::xmm1);
				}
				else
				{
					A.xorps(x86::xmm2, x86::xmm2);
					A.ucomiss(x86::xmm1, x86::xmm2);
					Label DoDiv = A.new_label();
					A.jp(DoDiv);
					A.je(NewFailureSite(
						EJitRuntimeStatus::DivisionByZero, CurrentInstructionOffset));
					A.bind(DoDiv);
					A.divss(x86::xmm0, x86::xmm1);
				}
				A.movd(x86::eax, x86::xmm0);
			}
			else
			{
				A.movq(x86::xmm0, x86::r8);
				A.movq(x86::xmm1, x86::r9);
				if (Op == EOpcode::Sub)
				{
					A.subsd(x86::xmm0, x86::xmm1);
				}
				else if (Op == EOpcode::Mul)
				{
					A.mulsd(x86::xmm0, x86::xmm1);
				}
				else
				{
					A.xorpd(x86::xmm2, x86::xmm2);
					A.ucomisd(x86::xmm1, x86::xmm2);
					Label DoDiv = A.new_label();
					A.jp(DoDiv);
					A.je(NewFailureSite(
						EJitRuntimeStatus::DivisionByZero, CurrentInstructionOffset));
					A.bind(DoDiv);
					A.divsd(x86::xmm0, x86::xmm1);
				}
				A.movq(x86::rax, x86::xmm0);
			}
			return true;
		}

		const bool b32 = Tag == EMixTag::I32 || Tag == EMixTag::U32
			|| Tag == EMixTag::Bool;
		const bool b64 = Tag == EMixTag::I64 || Tag == EMixTag::U64
			|| Tag == EMixTag::Enum;
		if (!b32 && !b64)
		{
			return false;
		}
		const bool bSigned = Tag == EMixTag::I32 || Tag == EMixTag::I64
			|| Tag == EMixTag::Enum;
		if (bComparison)
		{
			if ((Tag == EMixTag::Bool || Tag == EMixTag::Enum)
				&& Op != EOpcode::Eq && Op != EOpcode::Ne)
			{
				return false;
			}
			if (b32)
			{
				A.cmp(x86::r8d, x86::r9d);
			}
			else
			{
				A.cmp(x86::r8, x86::r9);
			}
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
					if (bSigned)
					{
						A.setl(x86::al);
					}
					else
					{
						A.setb(x86::al);
					}
					break;
				}
			case EOpcode::Le:
				{
					if (bSigned)
					{
						A.setle(x86::al);
					}
					else
					{
						A.setbe(x86::al);
					}
					break;
				}
			case EOpcode::Gt:
				{
					if (bSigned)
					{
						A.setg(x86::al);
					}
					else
					{
						A.seta(x86::al);
					}
					break;
				}
			case EOpcode::Ge:
				{
					if (bSigned)
					{
						A.setge(x86::al);
					}
					else
					{
						A.setae(x86::al);
					}
					break;
				}
			default:
				return false;
			}
			A.movzx(x86::eax, x86::al);
			return true;
		}
		if (Tag == EMixTag::Bool || Tag == EMixTag::Enum)
		{
			return false;
		}
		if (Op == EOpcode::Sub)
		{
			if (b32)
			{
				A.sub(x86::r8d, x86::r9d);
			}
			else
			{
				A.sub(x86::r8, x86::r9);
			}
			if (bSigned)
			{
				A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			}
		}
		else if (Op == EOpcode::Mul)
		{
			if (b32)
			{
				A.imul(x86::r8d, x86::r9d);
			}
			else
			{
				A.imul(x86::r8, x86::r9);
			}
			if (bSigned)
			{
				A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			}
		}
		else if (Op == EOpcode::BitAnd)
		{
			if (b32)
			{
				A.and_(x86::r8d, x86::r9d);
			}
			else
			{
				A.and_(x86::r8, x86::r9);
			}
		}
		else if (Op == EOpcode::BitOr)
		{
			if (b32)
			{
				A.or_(x86::r8d, x86::r9d);
			}
			else
			{
				A.or_(x86::r8, x86::r9);
			}
		}
		else if (Op == EOpcode::BitXor)
		{
			if (b32)
			{
				A.xor_(x86::r8d, x86::r9d);
			}
			else
			{
				A.xor_(x86::r8, x86::r9);
			}
		}
		else
		{
			return false;
		}
		if (b32)
		{
			A.mov(x86::eax, x86::r8d);
		}
		else
		{
			A.mov(x86::rax, x86::r8);
		}
		return true;
	};
	// Inputs: r8/r9 contain raw left/right integer payloads. Result: rax.
	// x86 div/idiv provides the language's truncation and remainder rules; the
	// two exceptional cases are checked before issuing the instruction so the
	// native kernel reports them through the normal VM diagnostic path.
	auto EmitIntegerDivOrMod = [&](const EMixTag Tag, const EOpcode Op) -> bool
	{
		if (Op != EOpcode::Div && Op != EOpcode::Mod)
		{
			return false;
		}
		const bool bMod = Op == EOpcode::Mod;
		if (Tag == EMixTag::I32 || Tag == EMixTag::U32)
		{
			A.test(x86::r9d, x86::r9d);
			A.jz(NewFailureSite(
				EJitRuntimeStatus::DivisionByZero, CurrentInstructionOffset));
			A.mov(x86::eax, x86::r8d);
			if (Tag == EMixTag::I32)
			{
				Label DoDiv = A.new_label();
				A.cmp(x86::eax, static_cast<std::int32_t>(0x80000000u));
				A.jne(DoDiv);
				A.cmp(x86::r9d, -1);
				A.je(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
				A.bind(DoDiv);
				A.cdq();
				A.idiv(x86::r9d);
			}
			else
			{
				A.xor_(x86::edx, x86::edx);
				A.div(x86::r9d);
			}
			if (bMod)
			{
				A.mov(x86::eax, x86::edx);
			}
			return true;
		}
		if (Tag == EMixTag::I64 || Tag == EMixTag::U64)
		{
			A.test(x86::r9, x86::r9);
			A.jz(NewFailureSite(
				EJitRuntimeStatus::DivisionByZero, CurrentInstructionOffset));
			if (Tag == EMixTag::I64)
			{
				Label DoDiv = A.new_label();
				A.cmp(x86::r9, -1);
				A.jne(DoDiv);
				A.mov(x86::rax, static_cast<std::int64_t>(0x8000000000000000ULL));
				A.cmp(x86::r8, x86::rax);
				A.je(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
				A.bind(DoDiv);
				A.mov(x86::rax, x86::r8);
				A.cqo();
				A.idiv(x86::r9);
			}
			else
			{
				A.mov(x86::rax, x86::r8);
				A.xor_(x86::edx, x86::edx);
				A.div(x86::r9);
			}
			if (bMod)
			{
				A.mov(x86::rax, x86::rdx);
			}
			return true;
		}
		return false;
	};
	// Strength-reduce signed i32 division/remainder when the right operand is a
	// compile-time constant. Unlike idiv this uses one multiply-high plus shifts,
	// while preserving division-by-zero and INT_MIN / -1 failures exactly.
	auto EmitI32DivOrModImmediate = [&](const EOpcode Op,
		const std::int32_t Divisor) -> bool
	{
		if (Op != EOpcode::Div && Op != EOpcode::Mod) return false;
		EmitSignedDivOrModImmediate32(A, Op, Divisor, [&](const EJitRuntimeStatus Status)
		{
			return NewFailureSite(Status, CurrentInstructionOffset);
		});
		return true;
	};

	// Exact scalar widening/narrowing between the common signed/32-bit/float
	// forms has no language-visible failure path. Keep it inside the kernel so a
	// numeric loop does not allocate an FValue or cross the native helper ABI for
	// each explicit conversion. Unsigned 64-bit conversions intentionally retain
	// the helper path because x86 scalar conversion treats its integer source as
	// signed and needs a separate high-bit algorithm.
	auto EmitFloatConversionBuiltin = [&](const EBuiltinId Builtin, const EMixTag Tag, const bool bXmmResult = false) -> bool
	{
		if (!CanEmitMixFloatConversionBuiltin(Builtin, Tag))
		{
			return false;
		}
		if (Builtin == EBuiltinId::ToDouble)
		{
			if (Tag == EMixTag::F64)
			{
				A.mov(x86::rax, x86::r8);
				return true;
			}
			if (Tag == EMixTag::F32)
			{
				A.movd(x86::xmm0, x86::r8d);
				A.cvtss2sd(x86::xmm0, x86::xmm0);
			}
			else if (Tag == EMixTag::I32)
			{
				A.movsxd(x86::rax, x86::r8d);
				A.cvtsi2sd(x86::xmm0, x86::rax);
			}
			else if (Tag == EMixTag::U32)
			{
				A.mov(x86::eax, x86::r8d);
				A.cvtsi2sd(x86::xmm0, x86::rax);
			}
			else if (Tag == EMixTag::I64)
			{
				A.cvtsi2sd(x86::xmm0, x86::r8);
			}
			else
			{
				A.mov(x86::eax, x86::r8d);
				A.and_(x86::eax, 1);
				A.cvtsi2sd(x86::xmm0, x86::eax);
			}
			if (!bXmmResult)
			{
				A.movq(x86::rax, x86::xmm0);
			}
			return true;
		}

		if (Tag == EMixTag::F32)
		{
			A.mov(x86::eax, x86::r8d);
			return true;
		}
		if (Tag == EMixTag::F64)
		{
			A.movq(x86::xmm0, x86::r8);
			A.cvtsd2ss(x86::xmm0, x86::xmm0);
		}
		else if (Tag == EMixTag::I32)
		{
			A.movsxd(x86::rax, x86::r8d);
			A.cvtsi2ss(x86::xmm0, x86::rax);
		}
		else if (Tag == EMixTag::U32)
		{
			A.mov(x86::eax, x86::r8d);
			A.cvtsi2ss(x86::xmm0, x86::rax);
		}
		else if (Tag == EMixTag::I64)
		{
			A.cvtsi2ss(x86::xmm0, x86::r8);
		}
		else
		{
			A.mov(x86::eax, x86::r8d);
			A.and_(x86::eax, 1);
			A.cvtsi2ss(x86::xmm0, x86::eax);
		}
		A.movd(x86::eax, x86::xmm0);
		return true;
	};
	// Integral conversions below either preserve every source value or use the
	// existing kernel status codes for their only checked boundary. Keep wider
	// unsigned and narrow exact-width conversions in the helper: those require
	// additional range/diagnostic distinctions that are not free to duplicate.
	auto CanEmitIntegralConversionBuiltin = [](const EBuiltinId Builtin, const EMixTag Tag)
	{
		switch (Builtin)
		{
		case EBuiltinId::ToInt:
			return Tag == EMixTag::I32 || Tag == EMixTag::U32 || Tag == EMixTag::Bool;
		case EBuiltinId::ToUnsigned:
			return Tag == EMixTag::I32 || Tag == EMixTag::U32 || Tag == EMixTag::Bool;
		case EBuiltinId::ToInt64:
			return Tag == EMixTag::I32 || Tag == EMixTag::U32
				|| Tag == EMixTag::I64 || Tag == EMixTag::Bool;
		case EBuiltinId::ToUInt64:
			return Tag == EMixTag::U32 || Tag == EMixTag::Bool;
		default:
			return false;
		}
	};
	auto EmitIntegralConversionBuiltin = [&](const EBuiltinId Builtin, const EMixTag Tag) -> bool
	{
		if (!CanEmitIntegralConversionBuiltin(Builtin, Tag))
		{
			return false;
		}
		if (Builtin == EBuiltinId::ToInt)
		{
			if (Tag == EMixTag::U32)
			{
				A.cmp(x86::r8d, std::numeric_limits<std::int32_t>::max());
				A.ja(NewFailureSite(
					EJitRuntimeStatus::ToIntOverflow, CurrentInstructionOffset));
			}
			A.mov(x86::eax, x86::r8d);
			if (Tag == EMixTag::Bool)
			{
				A.and_(x86::eax, 1);
			}
			return true;
		}
		if (Builtin == EBuiltinId::ToUnsigned)
		{
			if (Tag == EMixTag::I32)
			{
				A.test(x86::r8d, x86::r8d);
				A.js(NewFailureSite(
					EJitRuntimeStatus::ToUnsignedOverflow, CurrentInstructionOffset));
			}
			A.mov(x86::eax, x86::r8d);
			if (Tag == EMixTag::Bool)
			{
				A.and_(x86::eax, 1);
			}
			return true;
		}
		if (Builtin == EBuiltinId::ToInt64)
		{
			if (Tag == EMixTag::I32)
			{
				A.movsxd(x86::rax, x86::r8d);
			}
			else if (Tag == EMixTag::U32)
			{
				A.mov(x86::eax, x86::r8d);
			}
			else if (Tag == EMixTag::I64)
			{
				A.mov(x86::rax, x86::r8);
			}
			else
			{
				A.mov(x86::eax, x86::r8d);
				A.and_(x86::eax, 1);
			}
			return true;
		}
		A.mov(x86::eax, x86::r8d);
		if (Tag == EMixTag::Bool)
		{
			A.and_(x86::eax, 1);
		}
		return true;
	};
	// Inputs: r8/r9 contain raw left/right numeric payloads. Result: rax.
	// The builtin is specified as a conditional expression, not an IEEE min/max
	// instruction: when values compare equal or unordered it must return the
	// right operand. Keeping that rule also preserves signed-zero and NaN bits.
	auto EmitMinMaxBuiltin = [&](const EMixTag Tag, const bool bMin) -> bool
	{
		if (Tag == EMixTag::I32 || Tag == EMixTag::U32)
		{
			A.mov(x86::eax, x86::r9d);
			A.cmp(x86::r8d, x86::r9d);
			if (bMin)
			{
				if (Tag == EMixTag::I32)
				{
					A.cmovl(x86::eax, x86::r8d);
				}
				else
				{
					A.cmovb(x86::eax, x86::r8d);
				}
			}
			else
			{
				if (Tag == EMixTag::I32)
				{
					A.cmovg(x86::eax, x86::r8d);
				}
				else
				{
					A.cmova(x86::eax, x86::r8d);
				}
			}
			return true;
		}
		if (Tag == EMixTag::I64 || Tag == EMixTag::U64)
		{
			A.mov(x86::rax, x86::r9);
			A.cmp(x86::r8, x86::r9);
			if (bMin)
			{
				if (Tag == EMixTag::I64)
				{
					A.cmovl(x86::rax, x86::r8);
				}
				else
				{
					A.cmovb(x86::rax, x86::r8);
				}
			}
			else
			{
				if (Tag == EMixTag::I64)
				{
					A.cmovg(x86::rax, x86::r8);
				}
				else
				{
					A.cmova(x86::rax, x86::r8);
				}
			}
			return true;
		}
		if (Tag == EMixTag::F32)
		{
			Label TakeLeft = A.new_label();
			Label Done = A.new_label();
			A.movd(x86::xmm0, x86::r8d);
			A.movd(x86::xmm1, x86::r9d);
			A.ucomiss(x86::xmm0, x86::xmm1);
			A.mov(x86::eax, x86::r9d);
			A.jp(Done);
			if (bMin)
			{
				A.jb(TakeLeft);
			}
			else
			{
				A.ja(TakeLeft);
			}
			A.jmp(Done);
			A.bind(TakeLeft);
			A.mov(x86::eax, x86::r8d);
			A.bind(Done);
			return true;
		}
		if (Tag == EMixTag::F64)
		{
			Label TakeLeft = A.new_label();
			Label Done = A.new_label();
			A.movq(x86::xmm0, x86::r8);
			A.movq(x86::xmm1, x86::r9);
			A.ucomisd(x86::xmm0, x86::xmm1);
			A.mov(x86::rax, x86::r9);
			A.jp(Done);
			if (bMin)
			{
				A.jb(TakeLeft);
			}
			else
			{
				A.ja(TakeLeft);
			}
			A.jmp(Done);
			A.bind(TakeLeft);
			A.mov(x86::rax, x86::r8);
			A.bind(Done);
			return true;
		}
		return false;
	};
	// Inputs: r8 contains a raw numeric payload. Result: rax. Preserve the
	// standard-library abs semantics: floating variants clear their sign bit,
	// including negative zero and signed NaNs, while signed-integer minimum
	// values still report overflow instead of relying on the processor's
	// wrapping negation.
	auto EmitAbsBuiltin = [&](const EMixTag Tag) -> bool
	{
		if (Tag == EMixTag::I32)
		{
			Label Done = A.new_label();
			A.mov(x86::eax, x86::r8d);
			A.test(x86::eax, x86::eax);
			A.jns(Done);
			A.neg(x86::eax);
			A.jo(NewFailureSite(
				EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			A.bind(Done);
			return true;
		}
		if (Tag == EMixTag::U32)
		{
			A.mov(x86::eax, x86::r8d);
			return true;
		}
		if (Tag == EMixTag::I64)
		{
			Label Done = A.new_label();
			A.mov(x86::rax, x86::r8);
			A.test(x86::rax, x86::rax);
			A.jns(Done);
			A.neg(x86::rax);
			A.jo(NewFailureSite(
				EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			A.bind(Done);
			return true;
		}
		if (Tag == EMixTag::U64)
		{
			A.mov(x86::rax, x86::r8);
			return true;
		}
		if (Tag == EMixTag::F32)
		{
			A.mov(x86::eax, x86::r8d);
			A.and_(x86::eax, 0x7fffffff);
			return true;
		}
		if (Tag == EMixTag::F64)
		{
			A.mov(x86::rax, static_cast<std::int64_t>(0x7fffffffffffffffULL));
			A.and_(x86::rax, x86::r8);
			return true;
		}
		return false;
	};
	// Inputs: r8 contains a raw scalar payload. Result: rax.  These forms are
	// frequent in mixed numeric kernels and their checked semantics map exactly
	// to a native instruction plus the existing overflow status path.
	auto EmitSimpleUnary = [&](const EMixTag Tag, const EOpcode Op) -> bool
	{
		if (Op == EOpcode::Not && Tag == EMixTag::Bool)
		{
			A.mov(x86::eax, x86::r8d);
			A.and_(x86::eax, 1);
			A.xor_(x86::eax, 1);
			return true;
		}
		if (Op == EOpcode::BitNot)
		{
			if (Tag == EMixTag::I32 || Tag == EMixTag::U32)
			{
				A.mov(x86::eax, x86::r8d);
				A.not_(x86::eax);
				return true;
			}
			if (Tag == EMixTag::I64 || Tag == EMixTag::U64)
			{
				A.mov(x86::rax, x86::r8);
				A.not_(x86::rax);
				return true;
			}
			return false;
		}
		if (Op != EOpcode::Neg)
		{
			return false;
		}
		if (Tag == EMixTag::I32)
		{
			A.mov(x86::eax, x86::r8d);
			A.neg(x86::eax);
			A.jo(NewFailureSite(
				EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			return true;
		}
		if (Tag == EMixTag::U32)
		{
			A.mov(x86::eax, x86::r8d);
			A.neg(x86::eax);
			return true;
		}
		if (Tag == EMixTag::I64)
		{
			A.mov(x86::rax, x86::r8);
			A.neg(x86::rax);
			A.jo(NewFailureSite(
				EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			return true;
		}
		if (Tag == EMixTag::U64)
		{
			A.mov(x86::rax, x86::r8);
			A.neg(x86::rax);
			return true;
		}
		if (Tag == EMixTag::F32)
		{
			A.movd(x86::xmm0, x86::r8d);
			A.mov(x86::eax, static_cast<std::int32_t>(0x80000000u));
			A.movd(x86::xmm1, x86::eax);
			A.xorps(x86::xmm0, x86::xmm1);
			A.movd(x86::eax, x86::xmm0);
			return true;
		}
		if (Tag == EMixTag::F64)
		{
			A.movq(x86::xmm0, x86::r8);
			A.mov(x86::rax, static_cast<std::int64_t>(0x8000000000000000ULL));
			A.movq(x86::xmm1, x86::rax);
			A.xorpd(x86::xmm0, x86::xmm1);
			A.movq(x86::rax, x86::xmm0);
			return true;
		}
		return false;
	};
	// Inputs: r8/r9 contain raw left/right float payloads. Result: eax bool.
	// C++ comparison semantics require every ordered relation to be false for NaN,
	// while != is true. Emit that unordered branch explicitly instead of entering
	// BinOpMixed on every iteration of a float-controlled loop.
	auto EmitFloatComparison = [&](const EMixTag Tag, const EOpcode Op) -> bool
	{
		if ((Tag != EMixTag::F32 && Tag != EMixTag::F64)
			|| (Op != EOpcode::Eq && Op != EOpcode::Ne && Op != EOpcode::Lt
				&& Op != EOpcode::Le && Op != EOpcode::Gt && Op != EOpcode::Ge))
		{
			return false;
		}
		if (Tag == EMixTag::F32)
		{
			A.movd(x86::xmm0, x86::r8d);
			A.movd(x86::xmm1, x86::r9d);
			A.ucomiss(x86::xmm0, x86::xmm1);
		}
		else
		{
			A.movq(x86::xmm0, x86::r8);
			A.movq(x86::xmm1, x86::r9);
			A.ucomisd(x86::xmm0, x86::xmm1);
		}
		Label TrueResult = A.new_label();
		Label FalseResult = A.new_label();
		Label Done = A.new_label();
		if (Op == EOpcode::Ne)
		{
			A.jp(TrueResult);
			A.jne(TrueResult);
		}
		else
		{
			A.jp(FalseResult);
			switch (Op)
			{
			case EOpcode::Eq:
				{
					A.je(TrueResult);
					break;
				}
			case EOpcode::Lt:
				{
					A.jb(TrueResult);
					break;
				}
			case EOpcode::Le:
				{
					A.jbe(TrueResult);
					break;
				}
			case EOpcode::Gt:
				{
					A.ja(TrueResult);
					break;
				}
			case EOpcode::Ge:
				{
					A.jae(TrueResult);
					break;
				}
			default:
				return false;
			}
		}
		A.bind(FalseResult);
		A.xor_(x86::eax, x86::eax);
		A.jmp(Done);
		A.bind(TrueResult);
		A.mov(x86::eax, 1);
		A.bind(Done);
		return true;
	};
	// Inputs are the raw left/right payloads in r8/r9. Convert unlike primitive
	// operands directly to their language-promoted floating type and perform the
	// operation in the kernel. This is the non-boxing counterpart of
	// FExecutionContext::EvalArithmetic/JitCompare.
	auto EmitPromotedFloatBinary = [&] (
		const EMixTag LeftTag,
		const EMixTag RightTag,
		const EMixTag ResultTag,
		const EOpcode Op) -> bool
	{
		if ((ResultTag != EMixTag::F32 && ResultTag != EMixTag::F64)
			|| LeftTag == RightTag || Op == EOpcode::Mod)
		{
			return false;
		}
		const auto Convert = [&](const EMixTag SourceTag,
			const x86::Gp& Source64, const x86::Gp& Source32,
			const x86::Vec& Destination) -> bool
		{
			if (ResultTag == EMixTag::F64)
			{
				if (SourceTag == EMixTag::F64)
				{
					A.movq(Destination, Source64);
				}
				else if (SourceTag == EMixTag::F32)
				{
					A.movd(Destination, Source32);
					A.cvtss2sd(Destination, Destination);
				}
				else if (SourceTag == EMixTag::I32)
				{
					A.cvtsi2sd(Destination, Source32);
				}
				else if (SourceTag == EMixTag::U32)
				{
					A.mov(x86::eax, Source32);
					A.cvtsi2sd(Destination, x86::rax);
				}
				else if (SourceTag == EMixTag::I64)
				{
					A.cvtsi2sd(Destination, Source64);
				}
				else
				{
					return false;
				}
				return true;
			}
			if (SourceTag == EMixTag::F32)
			{
				A.movd(Destination, Source32);
			}
			else if (SourceTag == EMixTag::I32)
			{
				A.cvtsi2ss(Destination, Source32);
			}
			else if (SourceTag == EMixTag::U32)
			{
				A.mov(x86::eax, Source32);
				A.cvtsi2ss(Destination, x86::rax);
			}
			else if (SourceTag == EMixTag::I64)
			{
				A.cvtsi2ss(Destination, Source64);
			}
			else
			{
				return false;
			}
			return true;
		};
		if (!Convert(LeftTag, x86::r8, x86::r8d, x86::xmm0)
			|| !Convert(RightTag, x86::r9, x86::r9d, x86::xmm1))
		{
			return false;
		}
		const bool bComparison = Op == EOpcode::Eq || Op == EOpcode::Ne
			|| Op == EOpcode::Lt || Op == EOpcode::Le
			|| Op == EOpcode::Gt || Op == EOpcode::Ge;
		if (bComparison)
		{
			if (ResultTag == EMixTag::F32)
			{
				A.ucomiss(x86::xmm0, x86::xmm1);
			}
			else
			{
				A.ucomisd(x86::xmm0, x86::xmm1);
			}
			Label TrueResult = A.new_label();
			Label FalseResult = A.new_label();
			Label Done = A.new_label();
			if (Op == EOpcode::Ne)
			{
				A.jp(TrueResult);
				A.jne(TrueResult);
			}
			else
			{
				A.jp(FalseResult);
				switch (Op)
				{
				case EOpcode::Eq:
					{
						A.je(TrueResult);
						break;
					}
				case EOpcode::Lt:
					{
						A.jb(TrueResult);
						break;
					}
				case EOpcode::Le:
					{
						A.jbe(TrueResult);
						break;
					}
				case EOpcode::Gt:
					{
						A.ja(TrueResult);
						break;
					}
				case EOpcode::Ge:
					{
						A.jae(TrueResult);
						break;
					}
				default:
					return false;
				}
			}
			A.bind(FalseResult);
			A.xor_(x86::eax, x86::eax);
			A.jmp(Done);
			A.bind(TrueResult);
			A.mov(x86::eax, 1);
			A.bind(Done);
			return true;
		}
		if (Op != EOpcode::Add && Op != EOpcode::Sub
			&& Op != EOpcode::Mul && Op != EOpcode::Div)
		{
			return false;
		}
		if (Op == EOpcode::Div)
		{
			if (ResultTag == EMixTag::F32)
			{
				A.xorps(x86::xmm2, x86::xmm2);
				A.ucomiss(x86::xmm1, x86::xmm2);
			}
			else
			{
				A.xorpd(x86::xmm2, x86::xmm2);
				A.ucomisd(x86::xmm1, x86::xmm2);
			}
			Label DoDiv = A.new_label();
			A.jp(DoDiv);
			A.je(NewFailureSite(
				EJitRuntimeStatus::DivisionByZero, CurrentInstructionOffset));
			A.bind(DoDiv);
		}
		if (ResultTag == EMixTag::F32)
		{
			if (Op == EOpcode::Add)
			{
				A.addss(x86::xmm0, x86::xmm1);
			}
			else if (Op == EOpcode::Sub)
			{
				A.subss(x86::xmm0, x86::xmm1);
			}
			else if (Op == EOpcode::Mul)
			{
				A.mulss(x86::xmm0, x86::xmm1);
			}
			else
			{
				A.divss(x86::xmm0, x86::xmm1);
			}
			A.movd(x86::eax, x86::xmm0);
		}
		else
		{
			if (Op == EOpcode::Add)
			{
				A.addsd(x86::xmm0, x86::xmm1);
			}
			else if (Op == EOpcode::Sub)
			{
				A.subsd(x86::xmm0, x86::xmm1);
			}
			else if (Op == EOpcode::Mul)
			{
				A.mulsd(x86::xmm0, x86::xmm1);
			}
			else
			{
				A.divsd(x86::xmm0, x86::xmm1);
			}
			A.movq(x86::rax, x86::xmm0);
		}
		return true;
	};

	// Record branch destinations before applying local expression fusions. A
	// sequence may only be collapsed when no instruction inside it is externally
	// reachable; this also keeps hand-authored/validated bytecode semantics exact.
	std::vector<bool> JumpTargets(CodeSize, false);
	if (!JumpTargets.empty())
	{
		JumpTargets[0] = true;
	}
	for (std::size_t ScanIp = 0; ScanIp < CodeSize;)
	{
		const EOpcode ScanOp = static_cast<EOpcode>(Bytes[ScanIp]);
		const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
		if (ScanSize == 0 || ScanIp + ScanSize > CodeSize)
		{
			return nullptr;
		}
		std::size_t Target = 0;
		if ((ScanOp == EOpcode::Jmp || ScanOp == EOpcode::JmpIfFalse)
			&& IsJumpTargetInRangeLocal(
				ScanIp + ScanSize,
				Bytecode::ReadI32LE(Bytes.data() + ScanIp + 1),
				CodeSize,
				Target))
		{
			JumpTargets[Target] = true;
		}
		else if (ScanOp == EOpcode::JmpIfLocalGeImm
			&& IsJumpTargetInRangeLocal(
				ScanIp + ScanSize,
				Bytecode::ReadI32LE(Bytes.data() + ScanIp + 7),
				CodeSize,
				Target))
		{
			JumpTargets[Target] = true;
		}
		ScanIp += ScanSize;
	}

	struct FLocalAffineUpdate
	{
		std::uint16_t SourceSlot = 0;
		std::uint16_t DestinationSlot = 0;
		EMixTag Tag = EMixTag::Uninitialized;
		std::uint64_t Multiplier = 0;
		std::uint64_t Addend = 0;
		std::size_t EndIp = 0;
	};
	auto ConstantBitsForTag = [&](const std::size_t ConstIp, const EMixTag Tag,
		std::uint64_t& OutBits) -> bool
	{
		const EOpcode ConstOp = static_cast<EOpcode>(Bytes[ConstIp]);
		if ((ConstOp != EOpcode::ConstI32 && ConstOp != EOpcode::ConstF64)
			|| ConstIp + 3 > CodeSize)
		{
			return false;
		}
		const std::uint16_t Index = Bytecode::ReadU16LE(Bytes.data() + ConstIp + 1);
		if (Index >= Module.Constants.size())
		{
			return false;
		}
		const auto& C = Module.Constants[Index];
		switch (Tag)
		{
		case EMixTag::I32:
			{
				if (C.Kind != EConstantKind::I32)
				{
					return false;
				}
				OutBits = static_cast<std::uint32_t>(C.I32);
				return true;
			}
		case EMixTag::U32:
			{
				if (C.Kind != EConstantKind::U32)
				{
					return false;
				}
				OutBits = C.U32;
				return true;
			}
		case EMixTag::I64:
			{
				if (C.Kind != EConstantKind::I64)
				{
					return false;
				}
				OutBits = static_cast<std::uint64_t>(C.I64);
				return true;
			}
		case EMixTag::U64:
			{
				if (C.Kind != EConstantKind::U64)
				{
					return false;
				}
				OutBits = C.U64;
				return true;
			}
		case EMixTag::F32:
			{
				if (C.Kind != EConstantKind::F32)
				{
					return false;
				}
				{
					std::uint32_t Bits = 0;
					std::memcpy(&Bits, &C.F32, sizeof(Bits));
					OutBits = Bits;
				}
				return true;
			}
		case EMixTag::F64:
			{
				if (C.Kind != EConstantKind::F64)
				{
					return false;
				}
				std::memcpy(&OutBits, &C.F64, sizeof(OutBits));
				return true;
			}
		default:
			return false;
		}
	};
	struct FMaskedConditionalIncrement
	{
		std::uint16_t SourceSlot = 0;
		std::uint16_t DestinationSlot = 0;
		EMixTag DestinationTag = EMixTag::Uninitialized;
		std::uint32_t Mask = 0;
		std::uint32_t ComparedValue = 0;
		bool bEqual = true;
		std::size_t EndIp = 0;
	};
	auto MatchMaskedConditionalIncrement = [&](const std::size_t BeginIp,
		FMaskedConditionalIncrement& Out) -> bool
	{
		// if ((local & constant) ==/!= constant) otherLocal++
		const std::size_t MaskConstantIp = BeginIp
			+ Bytecode::InstructionSize(EOpcode::LoadLocal);
		const std::size_t BitAndIp = MaskConstantIp
			+ Bytecode::InstructionSize(EOpcode::ConstI32);
		const std::size_t CompareConstantIp = BitAndIp
			+ Bytecode::InstructionSize(EOpcode::BitAnd);
		const std::size_t CompareIp = CompareConstantIp
			+ Bytecode::InstructionSize(EOpcode::ConstI32);
		const std::size_t BranchIp = CompareIp + Bytecode::InstructionSize(EOpcode::Eq);
		const std::size_t IncrementIp = BranchIp
			+ Bytecode::InstructionSize(EOpcode::JmpIfFalse);
		const std::size_t EndIp = IncrementIp
			+ Bytecode::InstructionSize(EOpcode::IncLocal);
		if (EndIp > CodeSize
			|| static_cast<EOpcode>(Bytes[BeginIp]) != EOpcode::LoadLocal
			|| static_cast<EOpcode>(Bytes[MaskConstantIp]) != EOpcode::ConstI32
			|| static_cast<EOpcode>(Bytes[BitAndIp]) != EOpcode::BitAnd
			|| static_cast<EOpcode>(Bytes[CompareConstantIp]) != EOpcode::ConstI32
			|| (static_cast<EOpcode>(Bytes[CompareIp]) != EOpcode::Eq
				&& static_cast<EOpcode>(Bytes[CompareIp]) != EOpcode::Ne)
			|| static_cast<EOpcode>(Bytes[BranchIp]) != EOpcode::JmpIfFalse
			|| static_cast<EOpcode>(Bytes[IncrementIp]) != EOpcode::IncLocal)
		{
			return false;
		}
		std::size_t BranchTarget = 0;
		if (!IsJumpTargetInRangeLocal(
				IncrementIp,
				Bytecode::ReadI32LE(Bytes.data() + BranchIp + 1),
				CodeSize,
				BranchTarget)
			|| BranchTarget != EndIp || !Entries[BeginIp])
		{
			return false;
		}
		const std::uint16_t SourceSlot =
			Bytecode::ReadU16LE(Bytes.data() + BeginIp + 1);
		const std::uint16_t DestinationSlot =
			Bytecode::ReadU16LE(Bytes.data() + IncrementIp + 1);
		if (SourceSlot >= Entries[BeginIp]->Locals.size()
			|| DestinationSlot >= Entries[BeginIp]->Locals.size())
		{
			return false;
		}
		const EMixTag SourceTag = Entries[BeginIp]->Locals[SourceSlot];
		const EMixTag DestinationTag = Entries[BeginIp]->Locals[DestinationSlot];
		if ((SourceTag != EMixTag::I32 && SourceTag != EMixTag::U32)
			|| (DestinationTag != EMixTag::I32 && DestinationTag != EMixTag::U32))
		{
			return false;
		}
		std::uint64_t Mask = 0;
		std::uint64_t ComparedValue = 0;
		if (!ConstantBitsForTag(MaskConstantIp, SourceTag, Mask)
			|| !ConstantBitsForTag(CompareConstantIp, SourceTag, ComparedValue))
		{
			return false;
		}
		for (std::size_t Interior = MaskConstantIp; Interior < EndIp;)
		{
			if (JumpTargets[Interior])
			{
				return false;
			}
			const std::size_t Size = Bytecode::InstructionSize(
				static_cast<EOpcode>(Bytes[Interior]));
			if (Size == 0 || Interior + Size > EndIp)
			{
				return false;
			}
			Interior += Size;
		}
		Out = {
			SourceSlot,
			DestinationSlot,
			DestinationTag,
			static_cast<std::uint32_t>(Mask),
			static_cast<std::uint32_t>(ComparedValue),
			static_cast<EOpcode>(Bytes[CompareIp]) == EOpcode::Eq,
			EndIp};
		return true;
	};
	auto EmitMaskedConditionalIncrement = [&] (
		const FMaskedConditionalIncrement& Update)
	{
		A.mov(x86::eax, x86::dword_ptr(
			x86::rbx, static_cast<std::int32_t>(Update.SourceSlot * 8u)));
		const bool bSingleBitTest = Update.Mask != 0u
			&& (Update.Mask & (Update.Mask - 1u)) == 0u
			&& (Update.ComparedValue == 0u
				|| Update.ComparedValue == Update.Mask);
		if (bSingleBitTest)
		{
			const bool bTrueWhenSet =
				(Update.ComparedValue == Update.Mask) == Update.bEqual;
			if (Update.Mask == 1u)
			{
				// Parity tests are common in PRNG and hashing loops. The low
				// bit is already a normalized 0/1 value, so avoid materializing
				// flags through setcc on this dependency chain.
				if (!bTrueWhenSet)
				{
					A.not_(x86::eax);
				}
				A.and_(x86::eax, 1u);
			}
			else
			{
				A.test(x86::eax, Update.Mask);
				if (bTrueWhenSet)
				{
					A.setne(x86::al);
				}
				else
				{
					A.sete(x86::al);
				}
			}
		}
		else
		{
			A.and_(x86::eax, Update.Mask);
			A.cmp(x86::eax, Update.ComparedValue);
			if (Update.bEqual)
			{
				A.sete(x86::al);
			}
			else
			{
				A.setne(x86::al);
			}
		}
		if (!bSingleBitTest || Update.Mask != 1u)
		{
			A.movzx(x86::eax, x86::al);
		}
		const std::int32_t DestinationOffset =
			static_cast<std::int32_t>(Update.DestinationSlot * 8u);
		A.mov(x86::r9d, x86::dword_ptr(x86::rbx, DestinationOffset));
		A.add(x86::r9d, x86::eax);
		if (Update.DestinationTag == EMixTag::I32)
		{
			A.jo(NewFailureSite(
				EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
		}
		A.mov(x86::qword_ptr(x86::rbx, DestinationOffset), x86::r9);
	};
	auto MatchLocalAffineUpdate = [&](const std::size_t BeginIp,
		FLocalAffineUpdate& Out) -> bool
	{
		// load local; constant; mul; constant; add; [nop]; store local
		//
		// Scalar call expansion replaces the callee's terminal Return with Nop.
		// Accept that semantic no-op so an otherwise identical affine update keeps
		// the same native path after a pure scalar function has been inlined.
		constexpr std::size_t MinimumSequenceBytes = 14;
		if (BeginIp + MinimumSequenceBytes > CodeSize
			|| static_cast<EOpcode>(Bytes[BeginIp]) != EOpcode::LoadLocal
			|| static_cast<EOpcode>(Bytes[BeginIp + 6]) != EOpcode::Mul
			|| static_cast<EOpcode>(Bytes[BeginIp + 10]) != EOpcode::Add)
		{
			return false;
		}
		std::size_t StoreIp = BeginIp + 11;
		if (static_cast<EOpcode>(Bytes[StoreIp]) == EOpcode::Nop)
		{
			++StoreIp;
		}
		if (StoreIp + 3 > CodeSize
			|| static_cast<EOpcode>(Bytes[StoreIp]) != EOpcode::StoreLocal)
		{
			return false;
		}
		const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + BeginIp + 1);
		const std::uint16_t DestinationSlot =
			Bytecode::ReadU16LE(Bytes.data() + StoreIp + 1);
		if (Slot >= Function.LocalCount || DestinationSlot >= Function.LocalCount
			|| !Entries[BeginIp] || Slot >= Entries[BeginIp]->Locals.size()
			|| DestinationSlot >= Entries[BeginIp]->Locals.size())
		{
			return false;
		}
		for (std::size_t Interior = BeginIp + 3; Interior <= StoreIp;)
		{
			if (JumpTargets[Interior])
			{
				return false;
			}
			const std::size_t Size = Bytecode::InstructionSize(
				static_cast<EOpcode>(Bytes[Interior]));
			if (Size == 0 || Interior + Size > StoreIp + 3)
			{
				return false;
			}
			Interior += Size;
		}
		const EMixTag Tag = Entries[BeginIp]->Locals[Slot];
		std::uint64_t Multiplier = 0;
		std::uint64_t Addend = 0;
		if (!ConstantBitsForTag(BeginIp + 3, Tag, Multiplier)
			|| !ConstantBitsForTag(BeginIp + 7, Tag, Addend))
		{
			return false;
		}
		Out = {Slot, DestinationSlot, Tag, Multiplier, Addend, StoreIp + 3};
		return true;
	};
	auto EmitLocalAffineUpdate = [&](const FLocalAffineUpdate& Update) -> bool
	{
		const std::int32_t SourceOffset =
			static_cast<std::int32_t>(Update.SourceSlot * 8u);
		const std::int32_t DestinationOffset =
			static_cast<std::int32_t>(Update.DestinationSlot * 8u);
		if (Update.Tag == EMixTag::F32)
		{
			A.movss(x86::xmm0, x86::dword_ptr(x86::rbx, SourceOffset));
			A.mov(x86::eax, static_cast<std::uint32_t>(Update.Multiplier));
			A.movd(x86::xmm1, x86::eax);
			A.mulss(x86::xmm0, x86::xmm1);
			A.mov(x86::eax, static_cast<std::uint32_t>(Update.Addend));
			A.movd(x86::xmm1, x86::eax);
			A.addss(x86::xmm0, x86::xmm1);
			A.movss(x86::dword_ptr(x86::rbx, DestinationOffset), x86::xmm0);
			return true;
		}
		if (Update.Tag == EMixTag::F64)
		{
			LoadF64Local(x86::xmm0, Update.SourceSlot);
			A.mov(x86::rax, static_cast<std::int64_t>(Update.Multiplier));
			A.movq(x86::xmm1, x86::rax);
			A.mulsd(x86::xmm0, x86::xmm1);
			A.mov(x86::rax, static_cast<std::int64_t>(Update.Addend));
			A.movq(x86::xmm1, x86::rax);
			A.addsd(x86::xmm0, x86::xmm1);
			StoreF64Local(Update.DestinationSlot, x86::xmm0);
			return true;
		}
		if (Update.Tag == EMixTag::I32 || Update.Tag == EMixTag::U32)
		{
			A.mov(x86::eax, x86::dword_ptr(x86::rbx, SourceOffset));
			A.mov(x86::r9d, static_cast<std::uint32_t>(Update.Multiplier));
			A.imul(x86::eax, x86::r9d);
			if (Update.Tag == EMixTag::I32)
			{
				A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			}
			A.mov(x86::r9d, static_cast<std::uint32_t>(Update.Addend));
			A.add(x86::eax, x86::r9d);
			if (Update.Tag == EMixTag::I32)
			{
				A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			}
			A.mov(x86::qword_ptr(x86::rbx, DestinationOffset), x86::rax);
			return true;
		}
		if (Update.Tag == EMixTag::I64 || Update.Tag == EMixTag::U64)
		{
			A.mov(x86::rax, x86::qword_ptr(x86::rbx, SourceOffset));
			A.mov(x86::r9, static_cast<std::int64_t>(Update.Multiplier));
			A.imul(x86::rax, x86::r9);
			if (Update.Tag == EMixTag::I64)
			{
				A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			}
			A.mov(x86::r9, static_cast<std::int64_t>(Update.Addend));
			A.add(x86::rax, x86::r9);
			if (Update.Tag == EMixTag::I64)
			{
				A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
			}
			A.mov(x86::qword_ptr(x86::rbx, DestinationOffset), x86::rax);
			return true;
		}
		return false;
	};
	struct FClosedU32AffineLoop
	{
		std::uint16_t CounterSlot = 0;
		std::uint16_t ValueSlot = 0;
		std::int32_t Limit = 0;
		std::uint32_t Multiplier = 0;
		std::uint32_t Addend = 0;
		std::size_t ExitIp = 0;
	};
	auto MatchClosedU32AffineLoop = [&](const std::size_t BeginIp,
		FClosedU32AffineLoop& Out) -> bool
	{
		// Canonical `for` loop whose only body effect is
		// `u32 = u32 * constant + constant`. This shape also appears after a
		// pure scalar call has been expanded, with an optional Nop before the
		// store. Unsigned arithmetic is exactly affine modulo 2^32, so every
		// iteration can be composed without changing overflow semantics.
		if (static_cast<EOpcode>(Bytes[BeginIp]) != EOpcode::JmpIfLocalGeImm
			|| !Entries[BeginIp] || !Entries[BeginIp]->Stack.empty())
		{
			return false;
		}
		const std::size_t HeaderEnd = BeginIp
			+ Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm);
		const std::uint16_t CounterSlot =
			Bytecode::ReadU16LE(Bytes.data() + BeginIp + 1);
		if (HeaderEnd >= CodeSize
			|| CounterSlot >= Entries[BeginIp]->Locals.size()
			|| Entries[BeginIp]->Locals[CounterSlot] != EMixTag::I32
			|| JumpTargets[HeaderEnd])
		{
			return false;
		}
		std::size_t ExitIp = 0;
		if (!IsJumpTargetInRangeLocal(
				HeaderEnd,
				Bytecode::ReadI32LE(Bytes.data() + BeginIp + 7),
				CodeSize,
				ExitIp)
			|| ExitIp >= CodeSize)
		{
			return false;
		}
		FLocalAffineUpdate Affine;
		if (!MatchLocalAffineUpdate(HeaderEnd, Affine)
			|| Affine.Tag != EMixTag::U32
			|| Affine.SourceSlot != Affine.DestinationSlot
			|| Affine.SourceSlot == CounterSlot)
		{
			return false;
		}
		const std::size_t IncrementIp = Affine.EndIp;
		const std::size_t BackJumpIp = IncrementIp
			+ Bytecode::InstructionSize(EOpcode::IncLocal);
		const std::size_t LoopEnd = BackJumpIp
			+ Bytecode::InstructionSize(EOpcode::Jmp);
		if (LoopEnd > CodeSize
			|| static_cast<EOpcode>(Bytes[IncrementIp]) != EOpcode::IncLocal
			|| Bytecode::ReadU16LE(Bytes.data() + IncrementIp + 1) != CounterSlot
			|| static_cast<EOpcode>(Bytes[BackJumpIp]) != EOpcode::Jmp
			|| JumpTargets[IncrementIp] || JumpTargets[BackJumpIp]
			|| ExitIp != LoopEnd)
		{
			return false;
		}
		std::size_t BackTarget = 0;
		if (!IsJumpTargetInRangeLocal(
				LoopEnd,
				Bytecode::ReadI32LE(Bytes.data() + BackJumpIp + 1),
				CodeSize,
				BackTarget)
			|| BackTarget != BeginIp)
		{
			return false;
		}
		Out = {
			CounterSlot,
			Affine.SourceSlot,
			Bytecode::ReadI32LE(Bytes.data() + BeginIp + 3),
			static_cast<std::uint32_t>(Affine.Multiplier),
			static_cast<std::uint32_t>(Affine.Addend),
			ExitIp};
		return true;
	};
	auto EmitClosedU32AffineLoop = [&](const FClosedU32AffineLoop& Loop)
	{
		const std::int32_t CounterOffset =
			static_cast<std::int32_t>(Loop.CounterSlot * 8u);
		const std::int32_t ValueOffset =
			static_cast<std::int32_t>(Loop.ValueSlot * 8u);
		A.cmp(x86::dword_ptr(x86::rbx, CounterOffset), Loop.Limit);
		A.jge(At[Loop.ExitIp]);

		// ecx = positive iteration count. The signed loop comparison above
		// guarantees that the modulo-2^32 subtraction represents its exact
		// mathematical distance even across the full int32 range.
		A.mov(x86::ecx, static_cast<std::uint32_t>(Loop.Limit));
		A.sub(x86::ecx, x86::dword_ptr(x86::rbx, CounterOffset));
		A.mov(x86::eax, static_cast<std::uint32_t>(Loop.Limit));
		A.mov(x86::qword_ptr(x86::rbx, CounterOffset), x86::rax);
		A.mov(x86::r8d, x86::dword_ptr(x86::rbx, ValueOffset));
		A.mov(x86::r9d, 1u); // Result multiplier.
		A.xor_(x86::r10d, x86::r10d); // Result addend.
		A.mov(x86::r11d, Loop.Multiplier); // Power multiplier.
		A.mov(x86::edx, Loop.Addend); // Power addend.
		Label ExponentLoop = A.new_label();
		Label SkipComposition = A.new_label();
		Label ExponentDone = A.new_label();
		A.bind(ExponentLoop);
		A.test(x86::ecx, 1u);
		A.jz(SkipComposition);
		A.imul(x86::r10d, x86::r11d);
		A.add(x86::r10d, x86::edx);
		A.imul(x86::r9d, x86::r11d);
		A.bind(SkipComposition);
		A.shr(x86::ecx, 1u);
		A.jz(ExponentDone);
		A.mov(x86::eax, x86::r11d);
		A.imul(x86::eax, x86::edx);
		A.add(x86::eax, x86::edx);
		A.mov(x86::edx, x86::eax);
		A.imul(x86::r11d, x86::r11d);
		A.jmp(ExponentLoop);
		A.bind(ExponentDone);
		A.imul(x86::r8d, x86::r9d);
		A.add(x86::r8d, x86::r10d);
		A.mov(x86::qword_ptr(x86::rbx, ValueOffset), x86::r8);
	};
	struct FRegisterFloatAffineLoop
	{
		std::uint16_t CounterSlot = 0;
		std::uint16_t LimitSlot = 0;
		std::uint16_t ValueSlot = 0;
		EMixTag Tag = EMixTag::Uninitialized;
		std::int32_t Limit = 0;
		bool bLocalLimit = false;
		std::uint64_t Multiplier = 0;
		std::uint64_t Addend = 0;
		std::size_t ExitIp = 0;
	};
	auto MatchRegisterFloatAffineLoop = [&](const std::size_t BeginIp,
		FRegisterFloatAffineLoop& Out) -> bool
	{
		// Keep a canonical floating-point recurrence in registers for the whole
		// loop. The generated code deliberately retains one multiply followed by
		// one add per source iteration (no reassociation or FMA), so rounding,
		// NaN propagation and the active MXCSR mode remain bytecode-equivalent.
		if (!Entries[BeginIp] || !Entries[BeginIp]->Stack.empty())
		{
			return false;
		}
		std::size_t HeaderEnd = 0;
		std::size_t BranchEnd = 0;
		std::uint16_t CounterSlot = 0;
		std::uint16_t LimitSlot = 0;
		std::int32_t ImmediateLimit = 0;
		bool bLocalLimit = false;
		std::size_t ExitIp = 0;
		if (static_cast<EOpcode>(Bytes[BeginIp]) == EOpcode::JmpIfLocalGeImm)
		{
			HeaderEnd = BeginIp + Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm);
			BranchEnd = HeaderEnd;
			CounterSlot = Bytecode::ReadU16LE(Bytes.data() + BeginIp + 1);
			ImmediateLimit = Bytecode::ReadI32LE(Bytes.data() + BeginIp + 3);
			if (!IsJumpTargetInRangeLocal(
					BranchEnd, Bytecode::ReadI32LE(Bytes.data() + BeginIp + 7),
					CodeSize, ExitIp))
			{
				return false;
			}
		}
		else
		{
			// Runtime-bound canonical counted loop:
			//   load counter; load limit; lt; jump-if-false exit
			constexpr std::size_t HeaderSize = 12u;
			if (BeginIp + HeaderSize > CodeSize
				|| static_cast<EOpcode>(Bytes[BeginIp]) != EOpcode::LoadLocal
				|| static_cast<EOpcode>(Bytes[BeginIp + 3u]) != EOpcode::LoadLocal
				|| static_cast<EOpcode>(Bytes[BeginIp + 6u]) != EOpcode::Lt
				|| static_cast<EOpcode>(Bytes[BeginIp + 7u]) != EOpcode::JmpIfFalse)
			{
				return false;
			}
			CounterSlot = Bytecode::ReadU16LE(Bytes.data() + BeginIp + 1u);
			LimitSlot = Bytecode::ReadU16LE(Bytes.data() + BeginIp + 4u);
			HeaderEnd = BeginIp + HeaderSize;
			BranchEnd = HeaderEnd;
			bLocalLimit = true;
			if (!IsJumpTargetInRangeLocal(
					BranchEnd, Bytecode::ReadI32LE(Bytes.data() + BeginIp + 8u),
					CodeSize, ExitIp))
			{
				return false;
			}
		}
		if (HeaderEnd >= CodeSize || ExitIp >= CodeSize
			|| CounterSlot >= Entries[BeginIp]->Locals.size()
			|| Entries[BeginIp]->Locals[CounterSlot] != EMixTag::I32
			|| (bLocalLimit && (LimitSlot >= Entries[BeginIp]->Locals.size()
				|| Entries[BeginIp]->Locals[LimitSlot] != EMixTag::I32
				|| LimitSlot == CounterSlot))
			|| JumpTargets[HeaderEnd])
		{
			return false;
		}
		FLocalAffineUpdate Affine;
		bool bMatchedAffine = MatchLocalAffineUpdate(HeaderEnd, Affine);
		if (!bMatchedAffine)
		{
			// The compiler emits the compact local-immediate form for the common
			// `value += integral_constant` spelling.  Normalize it to the same
			// affine description so it receives the register loop and exact-integer
			// endpoint optimization instead of falling back to per-opcode stores.
			const EOpcode UpdateOp = static_cast<EOpcode>(Bytes[HeaderEnd]);
			if (UpdateOp == EOpcode::IncLocal || UpdateOp == EOpcode::AddLocalImm)
			{
				const std::uint16_t Slot =
					Bytecode::ReadU16LE(Bytes.data() + HeaderEnd + 1u);
				if (Slot < Entries[BeginIp]->Locals.size())
				{
					const EMixTag Tag = Entries[BeginIp]->Locals[Slot];
					const std::int32_t IntegerAddend = UpdateOp == EOpcode::IncLocal
						? 1
						: Bytecode::ReadI32LE(Bytes.data() + HeaderEnd + 3u);
					if (Tag == EMixTag::F32)
					{
						const float One = 1.0f;
						const float Add = static_cast<float>(IntegerAddend);
						std::uint32_t OneBits = 0;
						std::uint32_t AddBits = 0;
						std::memcpy(&OneBits, &One, sizeof(OneBits));
						std::memcpy(&AddBits, &Add, sizeof(AddBits));
						Affine = {Slot, Slot, Tag, OneBits, AddBits,
							HeaderEnd + Bytecode::InstructionSize(UpdateOp)};
						bMatchedAffine = true;
					}
					else if (Tag == EMixTag::F64)
					{
						const double One = 1.0;
						const double Add = static_cast<double>(IntegerAddend);
						std::uint64_t OneBits = 0;
						std::uint64_t AddBits = 0;
						std::memcpy(&OneBits, &One, sizeof(OneBits));
						std::memcpy(&AddBits, &Add, sizeof(AddBits));
						Affine = {Slot, Slot, Tag, OneBits, AddBits,
							HeaderEnd + Bytecode::InstructionSize(UpdateOp)};
						bMatchedAffine = true;
					}
				}
			}
		}
		if (!bMatchedAffine
			|| (Affine.Tag != EMixTag::F32 && Affine.Tag != EMixTag::F64)
			|| Affine.SourceSlot != Affine.DestinationSlot
			|| Affine.SourceSlot == CounterSlot)
		{
			return false;
		}
		const std::size_t IncrementIp = Affine.EndIp;
		const std::size_t BackJumpIp = IncrementIp
			+ Bytecode::InstructionSize(EOpcode::IncLocal);
		const std::size_t LoopEnd = BackJumpIp
			+ Bytecode::InstructionSize(EOpcode::Jmp);
		if (LoopEnd > CodeSize
			|| static_cast<EOpcode>(Bytes[IncrementIp]) != EOpcode::IncLocal
			|| Bytecode::ReadU16LE(Bytes.data() + IncrementIp + 1) != CounterSlot
			|| static_cast<EOpcode>(Bytes[BackJumpIp]) != EOpcode::Jmp
			|| JumpTargets[IncrementIp] || JumpTargets[BackJumpIp]
			|| ExitIp != LoopEnd)
		{
			return false;
		}
		std::size_t BackTarget = 0;
		if (!IsJumpTargetInRangeLocal(
				LoopEnd,
				Bytecode::ReadI32LE(Bytes.data() + BackJumpIp + 1),
				CodeSize,
				BackTarget)
			|| BackTarget != BeginIp)
		{
			return false;
		}
		Out = {
			CounterSlot,
			LimitSlot,
			Affine.SourceSlot,
			Affine.Tag,
			ImmediateLimit,
			bLocalLimit,
			Affine.Multiplier,
			Affine.Addend,
			ExitIp};
		return true;
	};
	auto EmitRegisterFloatAffineLoop = [&](const FRegisterFloatAffineLoop& Loop)
	{
		const std::int32_t CounterOffset =
			static_cast<std::int32_t>(Loop.CounterSlot * 8u);
		const std::int32_t ValueOffset =
			static_cast<std::int32_t>(Loop.ValueSlot * 8u);
		if (Loop.bLocalLimit)
		{
			const std::int32_t LimitOffset =
				static_cast<std::int32_t>(Loop.LimitSlot * 8u);
			A.mov(x86::ecx, x86::dword_ptr(x86::rbx, LimitOffset));
			A.cmp(x86::dword_ptr(x86::rbx, CounterOffset), x86::ecx);
			A.jge(At[Loop.ExitIp]);
			A.mov(x86::eax, x86::ecx);
			A.sub(x86::ecx, x86::dword_ptr(x86::rbx, CounterOffset));
			A.mov(x86::qword_ptr(x86::rbx, CounterOffset), x86::rax);
		}
		else
		{
			A.cmp(x86::dword_ptr(x86::rbx, CounterOffset), Loop.Limit);
			A.jge(At[Loop.ExitIp]);
			A.mov(x86::ecx, static_cast<std::uint32_t>(Loop.Limit));
			A.sub(x86::ecx, x86::dword_ptr(x86::rbx, CounterOffset));
			A.mov(x86::eax, static_cast<std::uint32_t>(Loop.Limit));
			A.mov(x86::qword_ptr(x86::rbx, CounterOffset), x86::rax);
		}
		Label NativeLoop = A.new_label();
		if (Loop.Tag == EMixTag::F32)
		{
			A.movss(x86::xmm0, x86::dword_ptr(x86::rbx, ValueOffset));
			A.mov(x86::eax, static_cast<std::uint32_t>(Loop.Multiplier));
			A.movd(x86::xmm1, x86::eax);
			A.mov(x86::eax, static_cast<std::uint32_t>(Loop.Addend));
			A.movd(x86::xmm2, x86::eax);
			A.bind(NativeLoop);
			A.mulss(x86::xmm0, x86::xmm1);
			A.addss(x86::xmm0, x86::xmm2);
			A.dec(x86::ecx);
			A.jnz(NativeLoop);
			A.movss(x86::dword_ptr(x86::rbx, ValueOffset), x86::xmm0);
		}
		else
		{
			double Multiplier = 0.0;
			double Addend = 0.0;
			static_assert(sizeof(Multiplier) == sizeof(Loop.Multiplier));
			std::memcpy(&Multiplier, &Loop.Multiplier, sizeof(Multiplier));
			std::memcpy(&Addend, &Loop.Addend, sizeof(Addend));
			const bool bExactIntegerInduction = Multiplier == 1.0
				&& std::isfinite(Addend) && std::trunc(Addend) == Addend
				&& std::abs(Addend) <= 9007199254740992.0;
			Label OrdinaryRecurrence = A.new_label();
			Label RecurrenceDone = A.new_label();
			if (bExactIntegerInduction)
			{
				// Integer-valued binary64 addition is exact while every value remains
				// inside [-2^53, 2^53].  Prove that condition at run time, then replace
				// the counted recurrence with its mathematically identical endpoint.
				// Compute that endpoint as an integer: a floating multiply may round when
				// count * addend lies outside the exact binary64 integer range even though
				// both recurrence endpoints (and therefore every monotonic intermediate)
				// remain exact. All unproven/overflowing cases retain the original loop.
				LoadF64Local(x86::xmm0, Loop.ValueSlot);
				A.cvttsd2si(x86::rax, x86::xmm0);
				A.cvtsi2sd(x86::xmm3, x86::rax);
				A.ucomisd(x86::xmm0, x86::xmm3);
				A.jne(OrdinaryRecurrence);
				A.jp(OrdinaryRecurrence);
				A.mov(x86::rdx, x86::rax); // exact integral starting value
				A.mov(x86::eax, x86::ecx); // zero-extend the positive iteration count
				A.mov(x86::r10, static_cast<std::int64_t>(Addend));
				A.imul(x86::rax, x86::r10);
				A.jo(OrdinaryRecurrence);
				A.add(x86::rax, x86::rdx);
				A.jo(OrdinaryRecurrence);
				A.mov(x86::r10, std::int64_t{9007199254740992ll});
				A.cmp(x86::rax, x86::r10);
				A.jg(OrdinaryRecurrence);
				A.mov(x86::r10, std::int64_t{-9007199254740992ll});
				A.cmp(x86::rax, x86::r10);
				A.jl(OrdinaryRecurrence);
				A.cvtsi2sd(x86::xmm3, x86::rax);
				StoreF64Local(Loop.ValueSlot, x86::xmm3);
				A.jmp(RecurrenceDone);
				A.bind(OrdinaryRecurrence);
			}
			LoadF64Local(x86::xmm0, Loop.ValueSlot);
			A.mov(x86::rax, static_cast<std::int64_t>(Loop.Multiplier));
			A.movq(x86::xmm1, x86::rax);
			A.mov(x86::rax, static_cast<std::int64_t>(Loop.Addend));
			A.movq(x86::xmm2, x86::rax);
			A.bind(NativeLoop);
			A.mulsd(x86::xmm0, x86::xmm1);
			A.addsd(x86::xmm0, x86::xmm2);
			A.dec(x86::ecx);
			A.jnz(NativeLoop);
			StoreF64Local(Loop.ValueSlot, x86::xmm0);
			if (bExactIntegerInduction)
			{
				A.bind(RecurrenceDone);
			}
		}
	};
	struct FRegisterU32AffineConditionalLoop
	{
		std::uint16_t CounterSlot = 0;
		std::uint16_t ValueSlot = 0;
		std::uint16_t DestinationSlot = 0;
		EMixTag DestinationTag = EMixTag::Uninitialized;
		std::int32_t Limit = 0;
		std::uint32_t Multiplier = 0;
		std::uint32_t Addend = 0;
		std::uint32_t Mask = 0;
		std::uint32_t ComparedValue = 0;
		bool bEqual = true;
		std::size_t ExitIp = 0;
	};
	auto MatchRegisterU32AffineConditionalLoop = [&](const std::size_t BeginIp,
		FRegisterU32AffineConditionalLoop& Out) -> bool
	{
		// Canonical LCG-style loop: update one u32 recurrence, test its masked
		// value, conditionally increment another scalar, then advance the loop
		// counter. Keeping all three locals in registers removes the per-opcode
		// memory traffic without speculating about values or branch direction.
		if (static_cast<EOpcode>(Bytes[BeginIp]) != EOpcode::JmpIfLocalGeImm
			|| !Entries[BeginIp] || !Entries[BeginIp]->Stack.empty())
		{
			return false;
		}
		const std::size_t HeaderEnd = BeginIp
			+ Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm);
		const std::uint16_t CounterSlot =
			Bytecode::ReadU16LE(Bytes.data() + BeginIp + 1);
		if (HeaderEnd >= CodeSize
			|| CounterSlot >= Entries[BeginIp]->Locals.size()
			|| Entries[BeginIp]->Locals[CounterSlot] != EMixTag::I32
			|| JumpTargets[HeaderEnd])
		{
			return false;
		}
		std::size_t ExitIp = 0;
		if (!IsJumpTargetInRangeLocal(
				HeaderEnd,
				Bytecode::ReadI32LE(Bytes.data() + BeginIp + 7),
				CodeSize,
				ExitIp)
			|| ExitIp >= CodeSize)
		{
			return false;
		}
		FLocalAffineUpdate Affine;
		if (!MatchLocalAffineUpdate(HeaderEnd, Affine)
			|| Affine.Tag != EMixTag::U32
			|| Affine.SourceSlot != Affine.DestinationSlot
			|| Affine.SourceSlot == CounterSlot)
		{
			return false;
		}
		FMaskedConditionalIncrement Conditional;
		if (!MatchMaskedConditionalIncrement(Affine.EndIp, Conditional)
			|| Conditional.SourceSlot != Affine.SourceSlot
			|| Conditional.DestinationSlot == CounterSlot
			|| Conditional.DestinationSlot == Affine.SourceSlot)
		{
			return false;
		}
		const std::size_t IncrementIp = Conditional.EndIp;
		const std::size_t BackJumpIp = IncrementIp
			+ Bytecode::InstructionSize(EOpcode::IncLocal);
		const std::size_t LoopEnd = BackJumpIp
			+ Bytecode::InstructionSize(EOpcode::Jmp);
		if (LoopEnd > CodeSize
			|| static_cast<EOpcode>(Bytes[IncrementIp]) != EOpcode::IncLocal
			|| Bytecode::ReadU16LE(Bytes.data() + IncrementIp + 1) != CounterSlot
			|| static_cast<EOpcode>(Bytes[BackJumpIp]) != EOpcode::Jmp
			|| JumpTargets[IncrementIp] || JumpTargets[BackJumpIp]
			|| ExitIp != LoopEnd)
		{
			return false;
		}
		std::size_t BackTarget = 0;
		if (!IsJumpTargetInRangeLocal(
				LoopEnd,
				Bytecode::ReadI32LE(Bytes.data() + BackJumpIp + 1),
				CodeSize,
				BackTarget)
			|| BackTarget != BeginIp)
		{
			return false;
		}
		Out = {
			CounterSlot,
			Affine.SourceSlot,
			Conditional.DestinationSlot,
			Conditional.DestinationTag,
			Bytecode::ReadI32LE(Bytes.data() + BeginIp + 3),
			static_cast<std::uint32_t>(Affine.Multiplier),
			static_cast<std::uint32_t>(Affine.Addend),
			Conditional.Mask,
			Conditional.ComparedValue,
			Conditional.bEqual,
			ExitIp};
		return true;
	};
	auto EmitRegisterU32AffineConditionalLoop = [&] (
		const FRegisterU32AffineConditionalLoop& Loop)
	{
		const std::int32_t CounterOffset =
			static_cast<std::int32_t>(Loop.CounterSlot * 8u);
		const std::int32_t ValueOffset =
			static_cast<std::int32_t>(Loop.ValueSlot * 8u);
		const std::int32_t DestinationOffset =
			static_cast<std::int32_t>(Loop.DestinationSlot * 8u);
		A.cmp(x86::dword_ptr(x86::rbx, CounterOffset), Loop.Limit);
		A.jge(At[Loop.ExitIp]);
		A.mov(x86::ecx, static_cast<std::uint32_t>(Loop.Limit));
		A.sub(x86::ecx, x86::dword_ptr(x86::rbx, CounterOffset));
		A.mov(x86::r8d, x86::dword_ptr(x86::rbx, ValueOffset));
		A.mov(x86::r9d, x86::dword_ptr(x86::rbx, DestinationOffset));
		A.mov(x86::r10d, Loop.Multiplier);
		A.mov(x86::r11d, Loop.Addend);
		Label NativeLoop = A.new_label();
		Label IncrementOverflow = A.new_label();
		Label Done = A.new_label();
		A.bind(NativeLoop);
		A.imul(x86::r8d, x86::r10d);
		A.add(x86::r8d, x86::r11d);
		const bool bSingleBitTest = Loop.Mask != 0u
			&& (Loop.Mask & (Loop.Mask - 1u)) == 0u
			&& (Loop.ComparedValue == 0u || Loop.ComparedValue == Loop.Mask);
		if (bSingleBitTest)
		{
			const bool bTrueWhenSet =
				(Loop.ComparedValue == Loop.Mask) == Loop.bEqual;
			if (Loop.Mask == 1u)
			{
				A.mov(x86::eax, x86::r8d);
				if (!bTrueWhenSet)
				{
					A.not_(x86::eax);
				}
				A.and_(x86::eax, 1u);
			}
			else
			{
				A.test(x86::r8d, Loop.Mask);
				if (bTrueWhenSet)
				{
					A.setne(x86::al);
				}
				else
				{
					A.sete(x86::al);
				}
			}
		}
		else
		{
			A.mov(x86::eax, x86::r8d);
			A.and_(x86::eax, Loop.Mask);
			A.cmp(x86::eax, Loop.ComparedValue);
			if (Loop.bEqual)
			{
				A.sete(x86::al);
			}
			else
			{
				A.setne(x86::al);
			}
		}
		if (!bSingleBitTest || Loop.Mask != 1u)
		{
			A.movzx(x86::eax, x86::al);
		}
		A.add(x86::r9d, x86::eax);
		if (Loop.DestinationTag == EMixTag::I32)
		{
			A.jo(IncrementOverflow);
		}
		A.dec(x86::ecx);
		A.jnz(NativeLoop);
		A.mov(x86::eax, static_cast<std::uint32_t>(Loop.Limit));
		A.mov(x86::qword_ptr(x86::rbx, CounterOffset), x86::rax);
		A.mov(x86::qword_ptr(x86::rbx, ValueOffset), x86::r8);
		A.mov(x86::qword_ptr(x86::rbx, DestinationOffset), x86::r9);
		A.jmp(Done);
		if (Loop.DestinationTag == EMixTag::I32)
		{
			// Match bytecode ordering on overflow: the recurrence has completed,
			// but the conditional increment and loop-counter increment have not.
			A.bind(IncrementOverflow);
			A.sub(x86::r9d, x86::eax);
			A.mov(x86::qword_ptr(x86::rbx, ValueOffset), x86::r8);
			A.mov(x86::qword_ptr(x86::rbx, DestinationOffset), x86::r9);
			A.mov(x86::eax, static_cast<std::uint32_t>(Loop.Limit));
			A.sub(x86::eax, x86::ecx);
			A.mov(x86::qword_ptr(x86::rbx, CounterOffset), x86::rax);
			A.jmp(NewFailureSite(
				EJitRuntimeStatus::IntegerOverflow, CurrentInstructionOffset));
		}
		A.bind(Done);
	};

	for (std::size_t Ip = 0; Ip < CodeSize;)
	{
		if (!Entries[Ip])
		{
			Ip += Bytecode::InstructionSize(static_cast<EOpcode>(Bytes[Ip]));
			continue;
		}
		A.bind(At[Ip]);
		CurrentInstructionOffset = Ip;
		const EOpcode Op = static_cast<EOpcode>(Bytes[Ip]);
		const std::size_t AfterIp = Ip + Bytecode::InstructionSize(Op);
		const auto& StackTags = Entries[Ip]->Stack;
		const auto& LocalTags = Entries[Ip]->Locals;
		NativeStackDepth = StackTags.size();
		FRegisterU32AffineConditionalLoop RegisterConditionalLoop;
		if (MatchRegisterU32AffineConditionalLoop(Ip, RegisterConditionalLoop))
		{
			EmitRegisterU32AffineConditionalLoop(RegisterConditionalLoop);
			Ip = RegisterConditionalLoop.ExitIp;
			continue;
		}
		FClosedU32AffineLoop ClosedAffine;
		if (MatchClosedU32AffineLoop(Ip, ClosedAffine))
		{
			EmitClosedU32AffineLoop(ClosedAffine);
			Ip = ClosedAffine.ExitIp;
			continue;
		}
		FRegisterFloatAffineLoop RegisterFloatLoop;
		if (MatchRegisterFloatAffineLoop(Ip, RegisterFloatLoop))
		{
			EmitRegisterFloatAffineLoop(RegisterFloatLoop);
			Ip = RegisterFloatLoop.ExitIp;
			continue;
		}
		FMaskedConditionalIncrement ConditionalIncrement;
		if (MatchMaskedConditionalIncrement(Ip, ConditionalIncrement))
		{
			EmitMaskedConditionalIncrement(ConditionalIncrement);
			Ip = ConditionalIncrement.EndIp;
			continue;
		}
		FLocalAffineUpdate Affine;
		if (MatchLocalAffineUpdate(Ip, Affine) && EmitLocalAffineUpdate(Affine))
		{
			Ip = Affine.EndIp;
			continue;
		}

		switch (Op)
		{
		case EOpcode::Nop:
			{
				break;
			}
		case EOpcode::ConstI32:
			{
				const auto Idx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto& Const = Module.Constants[Idx];
				if (Const.Kind == EConstantKind::I64
					|| Const.Kind == EConstantKind::Enum)
				{
					A.mov(x86::rax, Const.I64);
				}
				else if (Const.Kind == EConstantKind::U64)
				{
					A.mov(x86::rax, static_cast<std::int64_t>(Const.U64));
				}
				else if (Const.Kind == EConstantKind::U32)
				{
					A.mov(x86::eax, Const.U32);
					A.mov(x86::rax, x86::eax);
				}
				else if (Const.Kind == EConstantKind::I8)
				{
					A.mov(x86::eax, static_cast<std::int32_t>(Const.I8));
					A.mov(x86::rax, x86::eax);
				}
				else if (Const.Kind == EConstantKind::U8)
				{
					A.mov(x86::eax, static_cast<std::uint32_t>(Const.U8));
					A.mov(x86::rax, x86::eax);
				}
				else if (Const.Kind == EConstantKind::I16)
				{
					A.mov(x86::eax, static_cast<std::int32_t>(Const.I16));
					A.mov(x86::rax, x86::eax);
				}
				else if (Const.Kind == EConstantKind::U16)
				{
					A.mov(x86::eax, static_cast<std::uint32_t>(Const.U16));
					A.mov(x86::rax, x86::eax);
				}
				else
				{
					A.mov(x86::eax, Const.I32);
					A.mov(x86::rax, x86::eax);
				}
				PushQwordFromRax();
				break;
			}
		case EOpcode::ConstF64:
			{
				const auto Idx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto& Const = Module.Constants[Idx];
				std::uint64_t Bits = 0;
				if (Const.Kind == EConstantKind::F32)
				{
					std::uint32_t Narrow = 0;
					std::memcpy(&Narrow, &Const.F32, sizeof(Narrow));
					Bits = Narrow;
				}
				else
				{
					std::memcpy(&Bits, &Const.F64, sizeof(Bits));
				}
				A.mov(x86::rax, static_cast<std::int64_t>(Bits));
				PushQwordFromRax();
				break;
			}
		case EOpcode::ConstBool:
			{
				A.mov(x86::eax, Bytes[Ip + 1] != 0 ? 1 : 0);
				A.mov(x86::rax, x86::eax);
				PushQwordFromRax();
				break;
			}
		case EOpcode::LoadLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (bUseF64StackRegisters && LocalTags[Slot] == EMixTag::F64)
				{
					const auto Reg = NativeStackDepth < 2u ? x86::xmm(static_cast<std::uint32_t>(4u + NativeStackDepth)) : x86::xmm0;
					LoadF64Local(Reg, Slot);
					if (NativeStackDepth >= 2u)
					{
						A.movsd(x86::qword_ptr(x86::r12, static_cast<std::int32_t>(NativeStackDepth * 8u)), Reg);
					}
					++NativeStackDepth; break;
				}
				LoadLocalRaw(x86::rax, Slot);
				PushQwordFromRax();
				break;
			}
		case EOpcode::StoreLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (bUseF64StackRegisters && StackTags.back() == EMixTag::F64)
				{
					PopF64(x86::xmm0);
					if (!CanElideLocalStore(Ip))
					{
						StoreF64Local(Slot, x86::xmm0);
					}
					break;
				}
				PopToRax();
				if (!CanElideLocalStore(Ip))
				{
					StoreRaxToLocal(Slot);
				}
				break;
			}
		case EOpcode::LoadGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Slot);
				A.lea(x86::r8, x86::qword_ptr(x86::rsp, 40));
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalMixed));
				A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
				PushQwordFromRax();
				break;
			}
		case EOpcode::StoreGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				PopToRax();
				A.mov(x86::r8, x86::rax);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Slot);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalMixed));
				break;
			}
		case EOpcode::LoadThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				EMixTag FieldTag{};
				if (!MethodFieldStorageMixTag(Module, Function, Name, FieldTag))
				{
					return nullptr;
				}
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				A.mov(x86::r8d, static_cast<std::uint32_t>(FieldTag));
				A.lea(x86::r9, x86::qword_ptr(x86::rsp, 40));
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::LoadThisFieldMixed));
				A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
				PushQwordFromRax();
				break;
			}
		case EOpcode::StoreThisField:
		case EOpcode::InitializeThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				EMixTag FieldTag{};
				if (!MethodFieldStorageMixTag(Module, Function, Name, FieldTag))
				{
					return nullptr;
				}
				PopToRax();
				A.mov(x86::r9, x86::rax);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				A.mov(x86::r8d, static_cast<std::uint32_t>(FieldTag));
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
					Op == EOpcode::InitializeThisField
						? &VeroScript::AsmJit::JitNative::LinearHelpers::InitializeThisFieldMixed
						: &VeroScript::AsmJit::JitNative::LinearHelpers::StoreThisFieldMixed));
				break;
			}
		case EOpcode::LoadThis:
			{
				break;
			}
		case EOpcode::CallBoundMethod:
			{
				const auto FnIdx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto N = Bytes[Ip + 3];
				NativeStackDepth -= N;
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, FnIdx);
				A.lea(x86::r8, x86::qword_ptr(x86::r12,
					static_cast<std::int32_t>(NativeStackDepth * 8u)));
				A.mov(x86::r9d, N);
				A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisMixed));
				if (!Module.Functions[FnIdx].bReturnsVoid)
				{
					A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
					PushQwordFromRax();
				}
				else
				{
					A.xor_(x86::eax, x86::eax);
					PushQwordFromRax();
				}
				break;
			}
		case EOpcode::CallMethod:
			{
				const auto NameIdx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto N = Bytes[Ip + 3];
				bool bCallVoid = false;
				bool bSaw = false;
				bool bAmbiguous = false;
				std::uint16_t ResolvedFnIdx = 0;
				const std::string& LeafName = Module.StringPool[NameIdx];
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
					std::vector<EMixTag> CandP;
					EMixTag CandR = EMixTag::I32;
					bool CandV = false;
					if (!ParseMixClosure(Candidate.ClosureTypeName, CandP, CandR, CandV)
						|| Candidate.bReturnsVoid != CandV
						|| CandP.size() != N)
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
						bCallVoid = CandV;
						bSaw = true;
					}
				}
				NativeStackDepth -= N;
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, (!bAmbiguous && bSaw) ? ResolvedFnIdx : NameIdx);
				A.lea(x86::r8, x86::qword_ptr(x86::r12,
					static_cast<std::int32_t>(NativeStackDepth * 8u)));
				A.mov(x86::r9d, N);
				A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						(!bAmbiguous && bSaw)
						? &VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisMixed
						: &VeroScript::AsmJit::JitNative::LinearHelpers::CallMethodFromThisMixed));
				if (bSaw && !bCallVoid)
				{
					A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
					PushQwordFromRax();
				}
				else
				{
					A.xor_(x86::eax, x86::eax);
					PushQwordFromRax();
				}
				break;
			}
		case EOpcode::IncField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::IncThisFieldMixed));
				break;
			}
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::int32_t Imm = Op == EOpcode::IncLocal
					? 1
					: Bytecode::ReadI32LE(Bytes.data() + Ip + 3);
				const EMixTag Tag = LocalTags[Slot];
				if (Tag == EMixTag::F64 && F64LocalCache.RegisterIndices[Slot] >= 0)
				{
					const double ImmF = static_cast<double>(Imm);
					std::uint64_t Bits = 0;
					std::memcpy(&Bits, &ImmF, sizeof(Bits));
					A.mov(x86::rax, static_cast<std::int64_t>(Bits));
					A.movq(x86::xmm0, x86::rax);
					A.addsd(CachedLocalRegister(Slot), x86::xmm0);
					break;
				}
				LoadLocalRaw(x86::r8, Slot);
				if (Tag == EMixTag::F64)
				{
					const double ImmF = static_cast<double>(Imm);
					std::uint64_t Bits = 0;
					std::memcpy(&Bits, &ImmF, sizeof(Bits));
					A.mov(x86::rax, static_cast<std::int64_t>(Bits));
				}
				else if (Tag == EMixTag::F32)
				{
					const float ImmF = static_cast<float>(Imm);
					std::uint32_t Bits = 0;
					std::memcpy(&Bits, &ImmF, sizeof(Bits));
					A.mov(x86::eax, Bits);
					A.mov(x86::rax, x86::eax);
				}
				else if (Tag == EMixTag::I64)
				{
					A.mov(x86::rax, static_cast<std::int64_t>(Imm));
				}
				else
				{
					A.mov(x86::eax, Imm);
					A.mov(x86::rax, x86::eax);
				}
				A.mov(x86::r9, x86::rax); // Right
				if (!EmitAdd(Tag))
				{
					return nullptr;
				}
				StoreRaxToLocal(Slot);
				break;
			}
		case EOpcode::AddLocalLocal:
			{
				const auto D = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto S = Bytecode::ReadU16LE(Bytes.data() + Ip + 3);
				const EMixTag Tag = LocalTags[D];
				if (Tag == EMixTag::F64 && F64LocalCache.RegisterIndices[D] >= 0)
				{
					const auto Destination = CachedLocalRegister(D);
					if (F64LocalCache.RegisterIndices[S] >= 0)
					{
						A.addsd(Destination, CachedLocalRegister(S));
					}
					else
					{
						A.addsd(Destination, x86::qword_ptr(x86::rbx, static_cast<std::int32_t>(S * 8u)));
					}
					break;
				}
				LoadLocalRaw(x86::r8, D);
				LoadLocalRaw(x86::r9, S);
				if (!EmitAdd(Tag))
				{
					return nullptr;
				}
				StoreRaxToLocal(D);
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
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				const EMixTag LeftTag = StackTags[StackTags.size() - 2];
				const EMixTag RightTag = StackTags.back();
				if (bUseF64StackRegisters && LeftTag == EMixTag::F64 && RightTag == EMixTag::F64 && EmitF64StackArithmetic(Op))
				{
					break;
				}
				EMixTag Tag = RightTag;
				if (LeftTag != RightTag
					&& !TryMixNumericPromotion(LeftTag, RightTag, Tag))
				{
					return nullptr;
				}
				std::int32_t ImmediateRightI32 = 0;
				bool bHasImmediateRightI32 = false;
				std::optional<std::int64_t> ImmediateRightI64;
				constexpr std::size_t ConstI32Size = 3u;
				if ((Tag == EMixTag::I32 || Tag == EMixTag::I64)
					&& Ip >= ConstI32Size && !JumpTargets[Ip])
				{
					const std::size_t ConstantIp = Ip - ConstI32Size;
					if (Entries[ConstantIp]
						&& static_cast<EOpcode>(Bytes[ConstantIp]) == EOpcode::ConstI32)
					{
						const std::uint16_t ConstantIndex =
							Bytecode::ReadU16LE(Bytes.data() + ConstantIp + 1u);
						if (Tag == EMixTag::I32 && ConstantIndex < Module.Constants.size()
							&& Module.Constants[ConstantIndex].Kind == EConstantKind::I32)
						{
							ImmediateRightI32 = Module.Constants[ConstantIndex].I32;
							bHasImmediateRightI32 = true;
						}
						if (Tag == EMixTag::I64 && ConstantIndex < Module.Constants.size())
						{
							const auto& Constant = Module.Constants[ConstantIndex];
							if (RightTag == EMixTag::I64 && Constant.Kind == EConstantKind::I64)
							{
								ImmediateRightI64 = Constant.I64;
							}
							else if (RightTag == EMixTag::I32 && Constant.Kind == EConstantKind::I32)
							{
								ImmediateRightI64 = Constant.I32;
							}
						}
					}
				}
				PopToRax(); // Right
				A.mov(x86::r9, x86::rax);
				PopToRax(); // Left
				A.mov(x86::r8, x86::rax);
				// Widen each operand before a mixed-width integral operation. I32
				// slots carry only their low bits; reading them as a qword would
				// otherwise turn negative values into large positive integers.
				if (Tag == EMixTag::I64)
				{
					if (LeftTag == EMixTag::I32)
					{
						A.movsxd(x86::r8, x86::r8d);
					}
					if (RightTag == EMixTag::I32)
					{
						A.movsxd(x86::r9, x86::r9d);
					}
				}
				if (ImmediateRightI64 && (Op == EOpcode::Div || Op == EOpcode::Mod))
				{
					// No branch enters this instruction, so the preceding constant is
					// its sole right operand. Signed I32 promotion was applied above.
					EmitSignedDivOrModImmediate64(A, Op, *ImmediateRightI64,
						[&](const EJitRuntimeStatus Status)
						{
							return NewFailureSite(Status, CurrentInstructionOffset);
						});
					PushQwordFromRax();
					break;
				}
				if ((LeftTag != RightTag
						&& EmitPromotedFloatBinary(LeftTag, RightTag, Tag, Op))
					|| (bHasImmediateRightI32
						&& EmitI32DivOrModImmediate(Op, ImmediateRightI32))
					|| EmitIntegerDivOrMod(Tag, Op)
					|| EmitSimpleBinary(Tag, Op)
					|| EmitFloatComparison(Tag, Op))
				{
					PushQwordFromRax();
					break;
				}
				A.mov(x86::ecx, static_cast<std::uint32_t>(Op));
				A.mov(x86::edx, static_cast<std::uint32_t>(Tag));
				A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::BinOpMixed));
				A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
				PushQwordFromRax();
				break;
			}
		case EOpcode::Neg:
		case EOpcode::BitNot:
		case EOpcode::Not:
			{
				const EMixTag Tag = StackTags.back();
				PopToRax();
				A.mov(x86::r8, x86::rax);
				if (EmitSimpleUnary(Tag, Op))
				{
					PushQwordFromRax();
					break;
				}
				A.mov(x86::ecx, static_cast<std::uint32_t>(Op));
				A.mov(x86::edx, static_cast<std::uint32_t>(Tag));
				A.lea(x86::r9, x86::qword_ptr(x86::rsp, 40));
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::UnaryOpMixed));
				A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
				PushQwordFromRax();
				break;
			}
		case EOpcode::Dup:
			{
				LoadOperandSlotToRax(NativeStackDepth - 1u);
				PushQwordFromRax();
				break;
			}
		case EOpcode::Pop:
			{
				--NativeStackDepth;
				break;
			}
		case EOpcode::Call:
			{
				const auto FnIdx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto N = Bytes[Ip + 3];
				NativeStackDepth -= N;
				const FDirectI32Callee* const Direct = GetDirectI32Callee(FnIdx);
				bool bDirectTagsMatch = Direct && StackTags.size() >= N
					&& N == Module.Functions[FnIdx].ParamCount;
				for (std::size_t Arg = 0; Arg < N && bDirectTagsMatch; ++Arg)
				{
					const EMixTag Tag = StackTags[StackTags.size() - N + Arg];
					bDirectTagsMatch = Direct->bUnsigned
						? Tag == EMixTag::U32
						: (Tag == EMixTag::I32 || Tag == EMixTag::Bool);
				}
				if (bDirectTagsMatch)
				{
					if (Direct->bEuclideanTailRecurrence)
					{
						const Label Loop = A.new_label();
						const Label FastPositiveLoop = A.new_label();
						const Label Done = A.new_label();
						const Label ArithmeticOverflow = A.new_label();
						const Label DepthOverflow = A.new_label();
						const Label FallbackCall = A.new_label();
						const Label CallDone = A.new_label();
						const Label FailureOwnerKnown = A.new_label();
						const Label DepthFailureOwnerKnown = A.new_label();

						A.mov(x86::r10, x86::qword_ptr(x86::rsp, 144));
						A.test(x86::r10, x86::r10);
						A.jz(FallbackCall);
						A.mov(x86::r11, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, ActiveDepth))));
						A.mov(x86::rax, x86::qword_ptr(x86::r11));
						A.cmp(x86::rax, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, NativeDepthLimit))));
						A.jae(FallbackCall);
						A.mov(x86::rax, x86::qword_ptr(x86::r11));
						A.mov(x86::qword_ptr(x86::rsp, 48), x86::rax);
						A.xor_(x86::ecx, x86::ecx);
						A.mov(x86::r8d, x86::dword_ptr(x86::r12,
							static_cast<std::int32_t>(NativeStackDepth * 8u)));
						A.mov(x86::r9d, x86::dword_ptr(x86::r12,
							static_cast<std::int32_t>((NativeStackDepth + 1u) * 8u)));
						// For non-negative int32 operands, Euclid needs fewer than 64
						// invocations. Prove the complete call chain fits once and keep
						// the hot recurrence free of depth loads and signed-overflow guards.
						A.mov(x86::rax, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, LanguageDepthLimit))));
						A.cmp(x86::qword_ptr(x86::rsp, 48), x86::rax);
						A.jae(DepthOverflow);
						A.sub(x86::rax, x86::qword_ptr(x86::rsp, 48));
						A.cmp(x86::rax, kMaxNonNegativeI32EuclideanInvocations);
						A.jb(Loop);
						A.mov(x86::eax, x86::r8d);
						A.or_(x86::eax, x86::r9d);
						A.js(Loop);
						A.jmp(FastPositiveLoop);

						A.bind(FastPositiveLoop);
						A.inc(x86::rcx);
						A.test(x86::r9d, x86::r9d);
						A.jz(Done);
						A.mov(x86::eax, x86::r8d);
						A.xor_(x86::edx, x86::edx);
						A.div(x86::r9d);
						A.mov(x86::r8d, x86::r9d);
						A.mov(x86::r9d, x86::edx);
						A.jmp(FastPositiveLoop);

						A.bind(Loop);
						A.mov(x86::rax, x86::qword_ptr(x86::rsp, 48));
						A.add(x86::rax, x86::rcx);
						A.cmp(x86::rax, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, LanguageDepthLimit))));
						A.jae(DepthOverflow);
						A.inc(x86::rcx);
						A.test(x86::r9d, x86::r9d);
						A.jz(Done);
						A.cmp(x86::r8d, std::numeric_limits<std::int32_t>::min());
						A.jne(FailureOwnerKnown);
						A.cmp(x86::r9d, -1);
						A.je(ArithmeticOverflow);
						A.bind(FailureOwnerKnown);
						A.mov(x86::eax, x86::r8d);
						A.cdq();
						A.idiv(x86::r9d);
						A.mov(x86::r8d, x86::r9d);
						A.mov(x86::r9d, x86::edx);
						A.jmp(Loop);

						A.bind(Done);
						A.add(x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, DirectCalls))),
							x86::rcx);
						A.mov(x86::eax, x86::r8d);
						A.mov(x86::qword_ptr(x86::r12,
							static_cast<std::int32_t>(NativeStackDepth * 8u)),
							x86::rax);
						A.jmp(CallDone);

						A.bind(ArithmeticOverflow);
						A.cmp(x86::word_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, FailureFunctionIndex))),
							0xFFFFu);
						A.jne(DepthFailureOwnerKnown);
						A.mov(x86::word_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, FailureFunctionIndex))),
							FnIdx);
						A.bind(DepthFailureOwnerKnown);
						A.add(x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, DirectCalls))),
							x86::rcx);
						A.mov(x86::eax, EncodeJitKernelFailure(
							EJitRuntimeStatus::IntegerOverflow,
							Direct->EuclideanModOffset));
						A.jmp(Epilogue);

						A.bind(DepthOverflow);
						A.add(x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, DirectCalls))),
							x86::rcx);
						A.cmp(x86::word_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, FailureFunctionIndex))),
							0xFFFFu);
						{
							const Label OwnerAlreadyKnown = A.new_label();
							A.jne(OwnerAlreadyKnown);
							A.mov(x86::word_ptr(x86::r10,
								static_cast<std::int32_t>(offsetof(
									FNativeRecursionState, FailureFunctionIndex))),
								FnIdx);
							A.bind(OwnerAlreadyKnown);
						}
						A.mov(x86::rax, x86::qword_ptr(x86::rsp, 48));
						A.add(x86::rax, x86::rcx);
						A.mov(x86::qword_ptr(x86::r11), x86::rax);
						A.mov(x86::rcx, x86::r15);
						A.mov(x86::edx, static_cast<std::uint32_t>(
							EJitScalarValueKind::I32));
						A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
						EmitCallRax();
						A.mov(x86::dword_ptr(x86::rsp, 40), x86::eax);
						A.mov(x86::r10, x86::qword_ptr(x86::rsp, 144));
						A.mov(x86::r11, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, ActiveDepth))));
						A.mov(x86::rax, x86::qword_ptr(x86::rsp, 48));
						A.mov(x86::qword_ptr(x86::r11), x86::rax);
						A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
						A.jmp(Epilogue);

						A.bind(FallbackCall);
						A.mov(x86::rcx, x86::r15);
						A.mov(x86::edx, FnIdx);
						A.lea(x86::r8, x86::qword_ptr(x86::r12,
							static_cast<std::int32_t>(NativeStackDepth * 8u)));
						A.mov(x86::r9d, N);
						A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
						A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
						CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallMixed));
						A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
						A.mov(x86::qword_ptr(x86::r12,
							static_cast<std::int32_t>(NativeStackDepth * 8u)),
							x86::rax);
						A.bind(CallDone);
						++NativeStackDepth;
						break;
					}
					constexpr std::size_t OutOffset = 40u;
					constexpr std::size_t StatusOffset = 44u;
					constexpr std::size_t StateOffset = 48u;
					constexpr std::size_t LocalsOffset = 56u;
					const std::size_t StackOffset = LocalsOffset
						+ Direct->LocalCount * 4u;
					const std::size_t FrameBytes =
						(StackOffset + Direct->StackSlots * 4u + 15u) & ~15u;
					const Label FallbackCall = A.new_label();
					const Label LanguageOverflow = A.new_label();
					const Label CallDone = A.new_label();

					// Five non-volatile pushes plus 64 local bytes move the fifth kernel
					// argument from entry rsp+40 to the current rsp+144.
					A.mov(x86::r10, x86::qword_ptr(x86::rsp, 144));
					A.test(x86::r10, x86::r10);
					A.jz(FallbackCall);
					A.cmp(x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectI32TargetCount))),
						FnIdx);
					A.jbe(FallbackCall);
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectI32Targets))));
					A.test(x86::r11, x86::r11);
					A.jz(FallbackCall);
					A.mov(x86::rax, x86::qword_ptr(x86::r11,
						static_cast<std::int32_t>(FnIdx * sizeof(void*))));
					A.test(x86::rax, x86::rax);
					A.jz(FallbackCall);
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, ActiveDepth))));
					A.mov(x86::rcx, x86::qword_ptr(x86::r11));
					A.cmp(x86::rcx, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, LanguageDepthLimit))));
					A.jae(LanguageOverflow);
					A.cmp(x86::rcx, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, NativeDepthLimit))));
					A.jae(FallbackCall);
					A.inc(x86::qword_ptr(x86::r11));
					A.inc(x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectCalls))));

					A.sub(x86::rsp, static_cast<std::uint32_t>(FrameBytes));
					A.mov(x86::qword_ptr(x86::rsp,
						static_cast<std::int32_t>(StateOffset)),
						x86::r10);
					for (std::size_t Arg = 0; Arg < N; ++Arg)
					{
						A.mov(x86::eax, x86::dword_ptr(x86::r12,
							static_cast<std::int32_t>(
								(NativeStackDepth + Arg) * 8u)));
						A.mov(x86::dword_ptr(x86::rsp,
							static_cast<std::int32_t>(LocalsOffset + Arg * 4u)),
							x86::eax);
					}
					A.xor_(x86::eax, x86::eax);
					for (std::size_t Local = N; Local < Direct->LocalCount; ++Local)
					{
						A.mov(x86::dword_ptr(x86::rsp,
							static_cast<std::int32_t>(LocalsOffset + Local * 4u)),
							x86::eax);
					}
					A.mov(x86::rcx, x86::r15);
					A.lea(x86::rdx, x86::dword_ptr(x86::rsp,
						static_cast<std::int32_t>(LocalsOffset)));
					A.lea(x86::r8, x86::dword_ptr(x86::rsp,
						static_cast<std::int32_t>(StackOffset)));
					A.lea(x86::r9, x86::dword_ptr(x86::rsp,
						static_cast<std::int32_t>(OutOffset)));
					A.mov(x86::r10, x86::qword_ptr(x86::rsp,
						static_cast<std::int32_t>(StateOffset)));
					A.mov(x86::qword_ptr(x86::rsp, 32), x86::r10);
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectI32Targets))));
					A.mov(x86::rax, x86::qword_ptr(x86::r11,
						static_cast<std::int32_t>(FnIdx * sizeof(void*))));
					EmitCallRax();
					A.mov(x86::dword_ptr(x86::rsp,
						static_cast<std::int32_t>(StatusOffset)),
						x86::eax);
					A.mov(x86::r10, x86::qword_ptr(x86::rsp,
						static_cast<std::int32_t>(StateOffset)));
					{
						const Label FailureOwnerKnown = A.new_label();
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
					}
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, ActiveDepth))));
					A.dec(x86::qword_ptr(x86::r11));
					A.mov(x86::r10d, x86::dword_ptr(x86::rsp,
						static_cast<std::int32_t>(StatusOffset)));
					if (!Module.Functions[FnIdx].bReturnsVoid)
					{
						A.mov(x86::r11d, x86::dword_ptr(x86::rsp,
							static_cast<std::int32_t>(OutOffset)));
					}
					A.add(x86::rsp, static_cast<std::uint32_t>(FrameBytes));
					A.test(x86::r10d, x86::r10d);
					A.mov(x86::eax, x86::r10d);
					A.jnz(Epilogue);
					if (!Module.Functions[FnIdx].bReturnsVoid)
					{
						A.mov(x86::eax, x86::r11d);
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
					}
					A.mov(x86::qword_ptr(x86::r12,
						static_cast<std::int32_t>(NativeStackDepth * 8u)),
						x86::rax);
					A.jmp(CallDone);

					A.bind(LanguageOverflow);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, static_cast<std::uint32_t>(Direct->bUnsigned
						? EJitScalarValueKind::U32 : EJitScalarValueKind::I32));
					A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
					EmitCallRax();
					A.jmp(NewDynamicFailureSite(Ip));

					A.bind(FallbackCall);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, FnIdx);
					A.lea(x86::r8, x86::qword_ptr(x86::r12,
						static_cast<std::int32_t>(NativeStackDepth * 8u)));
					A.mov(x86::r9d, N);
					A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
					A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
					CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallMixed));
					if (!Module.Functions[FnIdx].bReturnsVoid)
					{
						A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
					}
					A.mov(x86::qword_ptr(x86::r12,
						static_cast<std::int32_t>(NativeStackDepth * 8u)),
						x86::rax);
					A.bind(CallDone);
					++NativeStackDepth;
					break;
				}
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, FnIdx);
				A.lea(x86::r8, x86::qword_ptr(x86::r12,
					static_cast<std::int32_t>(NativeStackDepth * 8u)));
				A.mov(x86::r9d, N);
				A.lea(x86::rax, x86::qword_ptr(x86::rsp, 40));
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallMixed));
				if (!Module.Functions[FnIdx].bReturnsVoid)
				{
					A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
					PushQwordFromRax();
				}
				else
				{
					A.xor_(x86::eax, x86::eax);
					PushQwordFromRax();
				}
				break;
			}
		case EOpcode::CallBuiltin:
			{
				const auto B = static_cast<EBuiltinId>(Bytes[Ip + 1]);
				const auto N = Bytes[Ip + 2];
				EMixTag ArgTag = EMixTag::I32;
				if (B == EBuiltinId::Now || B == EBuiltinId::SteadyNow
					|| B == EBuiltinId::ReadDouble)
				{
					ArgTag = EMixTag::F64;
				}
				else if (B == EBuiltinId::ReadBool)
				{
					ArgTag = EMixTag::Bool;
				}
				else if (B == EBuiltinId::Min || B == EBuiltinId::Max)
				{
					if (StackTags.size() < 2)
					{
						return nullptr;
					}
					ArgTag = StackTags[StackTags.size() - 2];
				}
				else if (N != 0)
				{
					if (StackTags.empty())
					{
						return nullptr;
					}
					ArgTag = StackTags.back();
				}
				if (B == EBuiltinId::Abs && N == 1)
				{
					PopToRax();
					A.mov(x86::r8, x86::rax);
					if (!EmitAbsBuiltin(ArgTag))
					{
						return nullptr;
					}
					PushQwordFromRax();
					break;
				}
				if (N == 1 && CanEmitMixFloatConversionBuiltin(B, ArgTag))
				{
					const bool bXmmResult = bUseF64StackRegisters && B == EBuiltinId::ToDouble && ArgTag != EMixTag::F64;
					PopToRax();
					A.mov(x86::r8, x86::rax);
					if (!EmitFloatConversionBuiltin(B, ArgTag, bXmmResult))
					{
						return nullptr;
					}
					if (bXmmResult)
					{
						PushF64(x86::xmm0);
					}
					else
					{
						PushQwordFromRax();
					}
					break;
				}
				if (N == 1 && CanEmitIntegralConversionBuiltin(B, ArgTag))
				{
					PopToRax();
					A.mov(x86::r8, x86::rax);
					if (!EmitIntegralConversionBuiltin(B, ArgTag))
					{
						return nullptr;
					}
					PushQwordFromRax();
					break;
				}
				if ((B == EBuiltinId::Min || B == EBuiltinId::Max) && N == 2)
				{
					PopToRax();
					A.mov(x86::r9, x86::rax);
					PopToRax();
					A.mov(x86::r8, x86::rax);
					if (!EmitMinMaxBuiltin(ArgTag, B == EBuiltinId::Min))
					{
						return nullptr;
					}
					PushQwordFromRax();
					break;
				}
				if (N == 2)
				{
					// Pair at stack top-1, top: copy to [rsp+40], [rsp+48]
					A.mov(x86::rax, x86::qword_ptr(
						x86::r12, static_cast<std::int32_t>(
							(NativeStackDepth - 2u) * 8u)));
					A.mov(x86::qword_ptr(x86::rsp, 40), x86::rax);
					A.mov(x86::rax, x86::qword_ptr(
						x86::r12, static_cast<std::int32_t>(
							(NativeStackDepth - 1u) * 8u)));
					A.mov(x86::qword_ptr(x86::rsp, 48), x86::rax);
					NativeStackDepth -= 2u;
				}
				else if (N == 1)
				{
					PopToRax();
					A.mov(x86::qword_ptr(x86::rsp, 40), x86::rax);
				}
				else
				{
					// Zero-argument scalar builtins write their result into this slot.
					A.xor_(x86::eax, x86::eax);
					A.mov(x86::qword_ptr(x86::rsp, 40), x86::rax);
				}
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, static_cast<std::uint32_t>(B));
				A.mov(x86::r8d, static_cast<std::uint32_t>(ArgTag));
				A.lea(x86::r9, x86::qword_ptr(x86::rsp, 40));
				A.mov(x86::eax, N);
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::BuiltinMixed));
				if (B == EBuiltinId::Exit)
				{
					// The VM helper has applied the requested exit and cleared VM frames.
					A.xor_(x86::eax, x86::eax);
					A.jmp(Epilogue);
					break;
				}
				if (B != EBuiltinId::Print && B != EBuiltinId::Eprint
					&& B != EBuiltinId::Assert && B != EBuiltinId::Sleep)
				{
					A.mov(x86::rax, x86::qword_ptr(x86::rsp, 40));
					PushQwordFromRax();
				}
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
				PopToRax();
				A.test(x86::eax, x86::eax);
				A.jz(At[Target]);
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
				const EMixTag Tag = LocalTags[Slot];
				if (Tag == EMixTag::I32)
				{
					A.cmp(x86::dword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)), Imm);
					A.jge(At[Target]);
				}
				else if (Tag == EMixTag::U32)
				{
					A.cmp(
						x86::dword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)),
						static_cast<std::uint32_t>(Imm));
					A.jae(At[Target]);
				}
				else if (Tag == EMixTag::I64)
				{
					A.mov(x86::rax, static_cast<std::int64_t>(Imm));
					A.cmp(x86::qword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)), x86::rax);
					A.jge(At[Target]);
				}
				else if (Tag == EMixTag::U64)
				{
					A.mov(x86::rax, static_cast<std::uint64_t>(static_cast<std::uint32_t>(Imm)));
					A.cmp(x86::qword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)), x86::rax);
					A.jae(At[Target]);
				}
				else if (Tag == EMixTag::F32)
				{
					A.movss(x86::xmm0, x86::dword_ptr(x86::rbx, static_cast<int32_t>(Slot * 8)));
					const float ImmF = static_cast<float>(Imm);
					std::uint32_t Bits = 0;
					std::memcpy(&Bits, &ImmF, sizeof(Bits));
					A.mov(x86::eax, Bits);
					A.movd(x86::xmm1, x86::eax);
					// JmpIfLocalGeImm means !(local < imm), not merely local >= imm:
					// unordered NaN comparisons must also take the jump. Keep both tests
					// in generated code so a hot float loop does not call a helper on
					// every back edge.
					A.ucomiss(x86::xmm0, x86::xmm1);
					A.jp(At[Target]);
					A.jae(At[Target]);
				}
				else
				{
					// !(local < Imm) for f64
					LoadF64Local(x86::xmm0, Slot);
					const double ImmF = static_cast<double>(Imm);
					std::uint64_t Bits = 0;
					std::memcpy(&Bits, &ImmF, sizeof(Bits));
					A.mov(x86::rax, static_cast<std::int64_t>(Bits));
					A.movq(x86::xmm1, x86::rax);
					A.ucomisd(x86::xmm0, x86::xmm1);
					A.jp(At[Target]);
					A.jae(At[Target]);
				}
				break;
			}
		case EOpcode::Return:
			{
				if (!Function.bReturnsVoid)
				{
					PopToRax();
					if (!F64LocalCache.Slots.empty() || bUseF64StackRegisters)
					{
						A.jmp(CachedReturn);
						break;
					}
					A.test(x86::r14, x86::r14);
					Label Skip = A.new_label();
					A.jz(Skip);
					A.mov(x86::qword_ptr(x86::r14), x86::rax);
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
	if (!Function.bReturnsVoid && (!F64LocalCache.Slots.empty() || bUseF64StackRegisters))
	{
		A.bind(CachedReturn);
		FlushF64Locals();
		// Locals are committed before the return store, preserving the raw ABI's
		// existing write order even if the caller aliases Out with a local slot.
		Label SkipOut = A.new_label();
		A.test(x86::r14, x86::r14);
		A.jz(SkipOut);
		A.mov(x86::qword_ptr(x86::r14), x86::rax);
		A.bind(SkipOut);
		A.xor_(x86::eax, x86::eax);
		A.jmp(RestoreRegisters);
	}
	A.bind(Epilogue);
	FlushF64Locals();
	A.bind(RestoreRegisters);
	for (std::size_t Index = 0; Index < F64LocalCache.Slots.size(); ++Index)
	{
		const auto Slot = F64LocalCache.Slots[Index];
		const auto Reg = CachedLocalRegister(Slot);
		A.movdqu(Reg, x86::ptr(x86::rsp, static_cast<std::int32_t>(64u + Index * 16u)));
	}
	A.add(x86::rsp, NativeFrameBytes);
	A.pop(x86::r15);
	A.pop(x86::r14);
	A.pop(x86::r13);
	A.pop(x86::r12);
	A.pop(x86::rbx);
	A.ret();

	// The opcode/type proof intentionally admits only existing inline emitters.
	// If a helper/direct call was nevertheless emitted, discard this unpublished
	// CodeHolder and retry the original memory operand stack. Keep existing native
	// coverage and the independently callee-saved F64 local cache. Passing false
	// bounds the retry to one additional emission and never publishes volatile
	// cached operands across a generated call.
	if (bUseF64StackRegisters && bEmittedCall)
	{
		return EmitLinearMixedCfgKernel(Runtime, Module, Function, Plan, false);
	}
	void* Fn = nullptr;
	if (!FinalizeKernel(Runtime, Code, &Fn))
	{
		return nullptr;
	}
	return reinterpret_cast<FLinearMixedKernel>(Fn);
}
