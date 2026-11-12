// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included inside VeroScript::AsmJit::JitNative anonymous asmjit namespace.
// Shared I32/bool CFG tag analysis (x64 + A64). Emit kernels live in sibling .inl files.

bool IsJumpTargetInRangeLocal(
	const std::size_t AfterIp,
	const std::int32_t Offset,
	const std::size_t CodeSize,
	std::size_t& OutTarget)
{
	const std::int64_t Next = static_cast<std::int64_t>(AfterIp) + Offset;
	if (Next < 0 || static_cast<std::size_t>(Next) >= CodeSize)
	{
		return false;
	}
	OutTarget = static_cast<std::size_t>(Next);
	return true;
}

bool ClosureTypeMatchesLocal(
	const std::string_view TypeName,
	const bool bUnsigned)
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
	auto Ok = [bUnsigned](const std::string_view T)
	{
		if (T.empty() || T == "void")
		{
			return true;
		}
		if (bUnsigned)
		{
			return T == "uint" || T == "unsigned int" || T == "unsigned";
		}
		return T == "int" || T == "bool";
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

bool IsUnsignedClosureLocal(const std::string_view TypeName)
{
	return TypeName.find("uint") != std::string_view::npos
		|| TypeName.find("unsigned") != std::string_view::npos;
}

enum class EI32FunctionModeLocal : std::uint8_t
{
	Signed,
	Unsigned,
	Mixed,
};

EI32FunctionModeLocal InferI32FunctionModeLocal(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function)
{
	if (IsUnsignedClosureLocal(Function.ClosureTypeName))
	{
		return EI32FunctionModeLocal::Unsigned;
	}
	// A typed signed/bool boundary fixes the raw mode. Only neutral void()
	// methods need body-based inference (e.g. bump() forwarding to add(uint)).
	if (Function.ClosureTypeName != "fn()->void")
	{
		return EI32FunctionModeLocal::Signed;
	}

	bool bSawUnsigned = false;
	bool bSawSigned = false;
	const std::size_t OwnerSeparator = FindMethodLinkageSeparator(Function.Name);
	const std::string_view OwnerName = OwnerSeparator == std::string::npos
		? std::string_view{}
		: std::string_view(Function.Name).substr(0, OwnerSeparator);
	const FBytecodeTypeDesc* OwnerType = nullptr;
	for (const FBytecodeTypeDesc& Type : Module.Types)
	{
		if (Type.Name == OwnerName)
		{
			OwnerType = &Type;
			break;
		}
	}

	const auto MarkType = [&](const std::string_view TypeName)
	{
		if (IsUnsignedClosureLocal(TypeName))
		{
			bSawUnsigned = true;
		}
		else if (TypeName == "int" || TypeName == "bool")
		{
			bSawSigned = true;
		}
	};
	const auto& Code = Function.Code;
	for (std::size_t Ip = 0; Ip < Code.size();)
	{
		const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
		const std::size_t Size = Bytecode::InstructionSize(Op);
		if (Size == 0 || Ip + Size > Code.size())
		{
			break;
		}
		if (Op == EOpcode::CallBoundMethod && Size >= 4)
		{
			const std::uint16_t Target = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (Target < Module.Functions.size())
			{
				MarkType(Module.Functions[Target].ClosureTypeName);
			}
		}
		else if (OwnerType
			&& (Op == EOpcode::LoadThisField
				|| Op == EOpcode::StoreThisField
				|| Op == EOpcode::InitializeThisField
				|| Op == EOpcode::IncField)
			&& Size >= 3)
		{
			const std::uint16_t NameIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (NameIndex < Module.StringPool.size())
			{
				const std::string& FieldName = Module.StringPool[NameIndex];
				for (const FBytecodeFieldDesc& Field : OwnerType->Fields)
				{
					if (Field.Name == FieldName)
					{
						MarkType(Field.TypeName);
						break;
					}
				}
			}
		}
		Ip += Size;
	}
	if (bSawUnsigned && bSawSigned)
	{
		return EI32FunctionModeLocal::Mixed;
	}
	return bSawUnsigned
		? EI32FunctionModeLocal::Unsigned
		: EI32FunctionModeLocal::Signed;
}

bool ClosureReturnsBoolLocal(const std::string_view TypeName)
{
	const auto Arrow = TypeName.rfind("->");
	return Arrow != std::string_view::npos && TypeName.substr(Arrow + 2) == "bool";
}

// Seed parameter slots that are bool (so LoadLocal+print keeps bool formatting).
void SeedBoolParamSlots(
	const std::string_view TypeName,
	const std::uint16_t LocalCount,
	std::vector<char>& OutLocalIsBool)
{
	OutLocalIsBool.assign(LocalCount, 0);
	if (TypeName.size() < 6 || TypeName.substr(0, 3) != "fn(")
	{
		return;
	}
	const std::size_t Close = TypeName.find(')');
	if (Close == std::string_view::npos)
	{
		return;
	}
	const std::string_view Params = TypeName.substr(3, Close - 3);
	std::uint16_t Slot = 0;
	for (std::size_t Begin = 0; Begin < Params.size() && Slot < LocalCount;)
	{
		const std::size_t Comma = Params.find(',', Begin);
		const std::string_view Param = Params.substr(
			Begin,
			Comma == std::string_view::npos ? std::string_view::npos : Comma - Begin);
		if (Param == "bool")
		{
			OutLocalIsBool[Slot] = 1;
		}
		++Slot;
		if (Comma == std::string_view::npos)
		{
			break;
		}
		Begin = Comma + 1;
	}
}

bool FindFieldTypeOnTypeOrBases(
	const FBytecodeModule& Module,
	const std::uint16_t TypeIndex,
	const std::string& FieldName,
	std::string& OutTypeName)
{
	if (TypeIndex >= Module.Types.size())
	{
		return false;
	}
	std::vector<std::uint16_t> Queue{TypeIndex};
	std::vector<bool> Visited(Module.Types.size(), false);
	for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
	{
		const std::uint16_t Current = Queue[Qi];
		if (Current >= Module.Types.size() || Visited[Current])
		{
			continue;
		}
		Visited[Current] = true;
		for (const FBytecodeFieldDesc& Field : Module.Types[Current].Fields)
		{
			if (Field.Name == FieldName)
			{
				OutTypeName = Field.TypeName;
				return true;
			}
		}
		for (const std::uint16_t BaseIdx : Module.Types[Current].BaseTypeIndices)
		{
			if (BaseIdx < Module.Types.size() && !Visited[BaseIdx])
			{
				Queue.push_back(BaseIdx);
			}
		}
	}
	return false;
}

bool MethodFieldMatchesI32(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::uint16_t NameIndex,
	const bool bUnsignedFn)
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
	if (bUnsignedFn)
	{
		return FieldType == "uint"
			|| FieldType == "unsigned int"
			|| FieldType == "unsigned";
	}
	return FieldType == "int" || FieldType == "bool";
}

// Match LinearHelpers::EMixTag: I32=0, U32=1, Bool=3.
constexpr std::uint8_t kI32MixI32 = 0;
constexpr std::uint8_t kI32MixU32 = 1;
constexpr std::uint8_t kI32MixBool = 3;

struct FI32CfgTagState
{
	bool operator==(const FI32CfgTagState& Other) const
	{
		return Stack == Other.Stack
			&& Locals == Other.Locals
			&& PendingThis == Other.PendingThis;
	}

	std::vector<std::uint8_t> Stack;
	std::vector<std::uint8_t> Locals;
	std::size_t PendingThis = 0;

};

// CFG-aware Mix-tag dataflow (same join rules as Mixed CFG).
bool AnalyzeI32BoolCfgTags(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	std::vector<std::optional<FI32CfgTagState>>& OutEntries)
{
	const EI32FunctionModeLocal FunctionMode =
		InferI32FunctionModeLocal(Module, Function);
	if (FunctionMode == EI32FunctionModeLocal::Mixed)
	{
		// A homogeneous 32-bit kernel cannot preserve a neutral void method that
		// touches both signed and unsigned values. Let Mixed/VM handle it.
		return false;
	}
	const bool bUnsignedFn = FunctionMode == EI32FunctionModeLocal::Unsigned;
	const std::uint8_t NumTag = bUnsignedFn ? kI32MixU32 : kI32MixI32;
	if (Function.ClosureTypeName.empty())
	{
		if (!Function.bIsMethod)
		{
			return false;
		}
	}
	else if (!ClosureTypeMatchesLocal(Function.ClosureTypeName, bUnsignedFn))
	{
		return false;
	}
	if (Function.LocalCount > kMaxNativeCfgLocals
		|| Function.Code.size() > kMaxNativeCfgBytecodeBytes)
	{
		return false;
	}
	const auto& Code = Function.Code;
	const std::size_t CodeSize = Code.size();
	if (CodeSize == 0)
	{
		OutEntries.clear();
		return Function.bReturnsVoid;
	}
	OutEntries.assign(CodeSize, std::nullopt);
	std::queue<std::size_t> Work;
	FI32CfgTagState Initial;
	Initial.Locals.assign(Function.LocalCount, NumTag);
	std::vector<char> LocalIsBool;
	SeedBoolParamSlots(Function.ClosureTypeName, Function.LocalCount, LocalIsBool);
	for (std::uint16_t Slot = 0; Slot < Function.LocalCount; ++Slot)
	{
		if (LocalIsBool[Slot])
		{
			Initial.Locals[Slot] = kI32MixBool;
		}
	}
	OutEntries[0] = Initial;
	Work.push(0);
	std::size_t Budget = CodeSize * 8 + 64;
	auto Enqueue = [&](const std::size_t Target, const FI32CfgTagState& State)
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
		return *OutEntries[Target] == State;
	};
	auto Need = [](const FI32CfgTagState& S, const std::size_t N)
	{
		return S.Stack.size() >= N;
	};
	auto PopN = [&](FI32CfgTagState& S, const std::size_t N) -> bool
	{
		if (!Need(S, N))
		{
			return false;
		}
		S.Stack.resize(S.Stack.size() - N);
		return true;
	};
	auto PushTag = [&](FI32CfgTagState& S, const std::uint8_t Tag)
	{
		S.Stack.push_back(Tag);
	};
	while (!Work.empty())
	{
		if (Budget-- == 0)
		{
			return false;
		}
		const std::size_t Ip = Work.front();
		Work.pop();
		FI32CfgTagState State = *OutEntries[Ip];
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
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (Slot >= State.Locals.size())
				{
					return false;
				}
				State.Locals[Slot] = NumTag;
				break;
			}
		case EOpcode::AddLocalLocal:
			{
				const auto Dst = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto Src = Bytecode::ReadU16LE(Code.data() + Ip + 3);
				if (Dst >= State.Locals.size() || Src >= State.Locals.size())
				{
					return false;
				}
				State.Locals[Dst] = NumTag;
				break;
			}
		case EOpcode::ConstI32:
			{
				const auto Index = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (Index >= Module.Constants.size())
				{
					return false;
				}
				const EConstantKind Kind = Module.Constants[Index].Kind;
				const bool bUnsignedConstant = Kind == EConstantKind::U8
					|| Kind == EConstantKind::U16 || Kind == EConstantKind::U32;
				const bool bSignedConstant = Kind == EConstantKind::I8
					|| Kind == EConstantKind::I16 || Kind == EConstantKind::I32;
				if (((!bUnsignedConstant && !bSignedConstant)
						|| bUnsignedConstant != bUnsignedFn)
					&& !IsVoidReturnSentinelI32(Module, Function, Ip))
				{
					// Homogeneous I32 gives every numeric local one signedness. Mixed CFG
					// must retain the per-local type when the body contains the other kind.
					return false;
				}
				PushTag(State, NumTag);
				break;
			}
		case EOpcode::ConstBool:
			{
				PushTag(State, kI32MixBool);
				break;
			}
		case EOpcode::LoadLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (Slot >= State.Locals.size())
				{
					return false;
				}
				PushTag(State, State.Locals[Slot]);
				break;
			}
		case EOpcode::StoreLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (Slot >= State.Locals.size() || !Need(State, 1))
				{
					return false;
				}
				State.Locals[Slot] = State.Stack.back();
				State.Stack.pop_back();
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
				if (!PopN(State, 2))
				{
					return false;
				}
				PushTag(State, NumTag);
				break;
			}
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				if (!PopN(State, 2))
				{
					return false;
				}
				PushTag(State, kI32MixBool);
				break;
			}
		case EOpcode::Neg:
		case EOpcode::BitNot:
			{
				if (!Need(State, 1))
				{
					return false;
				}
				State.Stack.back() = NumTag;
				break;
			}
		case EOpcode::Not:
			{
				if (!Need(State, 1))
				{
					return false;
				}
				State.Stack.back() = kI32MixBool;
				break;
			}
		case EOpcode::Dup:
			{
				if (!Need(State, 1))
				{
					return false;
				}
				PushTag(State, State.Stack.back());
				break;
			}
		case EOpcode::Pop:
			{
				if (!PopN(State, 1))
				{
					return false;
				}
				break;
			}
		case EOpcode::LoadThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (!MethodFieldMatchesI32(Module, Function, Name, bUnsignedFn))
				{
					return false;
				}
				std::string FieldType;
				const bool bBoolField = FindFieldTypeOnTypeOrBases(
					Module, Function.OwnerTypeIndex, Module.StringPool[Name], FieldType)
						&& FieldType == "bool";
				PushTag(State, bBoolField ? kI32MixBool : NumTag);
				break;
			}
		case EOpcode::StoreThisField:
		case EOpcode::InitializeThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (!MethodFieldMatchesI32(Module, Function, Name, bUnsignedFn)
					|| !PopN(State, 1))
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
				++State.PendingThis;
				break;
			}
		case EOpcode::CallBoundMethod:
			{
				const auto FnIdx = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				const auto N = Code[Ip + 3];
				if (!Function.bIsMethod
					|| State.PendingThis == 0
					|| FnIdx >= Module.Functions.size()
					|| !Module.Functions[FnIdx].bIsMethod
					|| Module.Functions[FnIdx].ParamCount != N
					|| !ClosureTypeMatchesLocal(
						Module.Functions[FnIdx].ClosureTypeName, bUnsignedFn)
							|| !PopN(State, N))
				{
					return false;
				}
				PushTag(State,
					(!Module.Functions[FnIdx].bReturnsVoid
						&& ClosureReturnsBoolLocal(Module.Functions[FnIdx].ClosureTypeName))
					? kI32MixBool
					: NumTag);
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
					if (!ClosureTypeMatchesLocal(Candidate.ClosureTypeName, bUnsignedFn))
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
				if (!bFound || !PopN(State, N))
				{
					return false;
				}
				PushTag(State,
					(FoundClosure && !bFoundVoid && ClosureReturnsBoolLocal(*FoundClosure))
					? kI32MixBool
					: NumTag);
				--State.PendingThis;
				break;
			}
		case EOpcode::IncField:
			{
				const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (!Function.bIsMethod
					|| State.PendingThis == 0
					|| !MethodFieldMatchesI32(Module, Function, Name, bUnsignedFn))
				{
					return false;
				}
				--State.PendingThis;
				break;
			}
		case EOpcode::LoadGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (Slot >= Module.GlobalTypeNames.size())
				{
					return false;
				}
				const std::string& T = Module.GlobalTypeNames[Slot];
				if (bUnsignedFn)
				{
					if (T != "uint" && T != "unsigned int" && T != "unsigned")
					{
						return false;
					}
				}
				else if (T != "int" && T != "bool")
				{
					return false;
				}
				PushTag(State, T == "bool" ? kI32MixBool : NumTag);
				break;
			}
		case EOpcode::StoreGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				if (Slot >= Module.GlobalTypeNames.size())
				{
					return false;
				}
				const std::string& T = Module.GlobalTypeNames[Slot];
				if (bUnsignedFn)
				{
					if (T != "uint" && T != "unsigned int" && T != "unsigned")
					{
						return false;
					}
				}
				else if (T != "int" && T != "bool")
				{
					return false;
				}
				if (!PopN(State, 1))
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
					|| !ClosureTypeMatchesLocal(Module.Functions[Idx].ClosureTypeName, bUnsignedFn)
					|| Module.Functions[Idx].ParamCount != N
					|| !PopN(State, N))
				{
					return false;
				}
				PushTag(State,
					(!Module.Functions[Idx].bReturnsVoid
						&& ClosureReturnsBoolLocal(Module.Functions[Idx].ClosureTypeName))
					? kI32MixBool
					: NumTag);
				break;
			}
		case EOpcode::CallBuiltin:
			{
				const auto B = static_cast<EBuiltinId>(Code[Ip + 1]);
				const auto N = Code[Ip + 2];
				if (B == EBuiltinId::Print || B == EBuiltinId::Eprint)
				{
					if (N != 1 || !PopN(State, 1))
					{
						return false;
					}
					break;
				}
				if (B != EBuiltinId::Abs && B != EBuiltinId::Min && B != EBuiltinId::Max
					&& B != EBuiltinId::ToInt && B != EBuiltinId::ToUnsigned)
				{
					return false;
				}
				if (B == EBuiltinId::Min || B == EBuiltinId::Max)
				{
					if (!PopN(State, 2))
					{
						return false;
					}
					PushTag(State, NumTag);
				}
				else if (!Need(State, 1))
				{
					return false;
				}
				else if (B == EBuiltinId::ToInt)
				{
					if (bUnsignedFn)
					{
						return false;
					}
					State.Stack.back() = kI32MixI32;
				}
				else if (B == EBuiltinId::ToUnsigned)
				{
					if (!bUnsignedFn)
					{
						return false;
					}
					State.Stack.back() = kI32MixU32;
				}
				// Abs keeps numeric tag on the existing slot.
				break;
			}
		case EOpcode::Jmp:
			{
				std::size_t Target = 0;
				if (!IsJumpTargetInRangeLocal(
						AfterIp,
						Bytecode::ReadI32LE(Code.data() + Ip + 1),
						CodeSize,
						Target)
							|| !Enqueue(Target, State))
				{
					return false;
				}
				continue;
			}
		case EOpcode::JmpIfFalse:
			{
				std::size_t Target = 0;
				if (!Need(State, 1)
					|| !IsJumpTargetInRangeLocal(
						AfterIp,
						Bytecode::ReadI32LE(Code.data() + Ip + 1),
						CodeSize,
						Target))
				{
					return false;
				}
				State.Stack.pop_back();
				if (!Enqueue(Target, State) || !Enqueue(AfterIp, State))
				{
					return false;
				}
				continue;
			}
		case EOpcode::JmpIfLocalGeImm:
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
				std::size_t Target = 0;
				if (Slot >= State.Locals.size()
					|| !IsJumpTargetInRangeLocal(
						AfterIp,
						Bytecode::ReadI32LE(Code.data() + Ip + 7),
						CodeSize,
						Target)
							|| !Enqueue(Target, State)
							|| !Enqueue(AfterIp, State))
				{
					return false;
				}
				continue;
			}
		case EOpcode::Return:
			{
				if (!Function.bReturnsVoid)
				{
					if (!PopN(State, 1) || State.PendingThis != 0)
					{
						return false;
					}
				}
				else if (State.PendingThis != 0)
				{
					return false;
				}
				continue;
			}
		default:
			return false;
		}
		if (AfterIp >= CodeSize)
		{
			// Void bodies may fall off without a trailing Return (VM accepts that).
			if (State.PendingThis != 0 || !Function.bReturnsVoid || !State.Stack.empty())
			{
				return false;
			}
			continue;
		}
		if (!Enqueue(AfterIp, State))
		{
			return false;
		}
	}
	return true;
}

struct FExternalDirectI32Callee
{
	bool bEligible = false;
	bool bUnsigned = false;
	std::uint16_t LocalCount = 0;
	std::uint16_t StackSlots = 0;
};

bool AnalyzeExternalDirectI32Callee(
	const FBytecodeModule& Module,
	const std::uint16_t FunctionIndex,
	const std::uint16_t CalleeIndex,
	FExternalDirectI32Callee& OutCallee)
{
	OutCallee = {};
	if (CalleeIndex >= Module.Functions.size() || CalleeIndex == FunctionIndex)
	{
		return false;
	}
	const FBytecodeFunction& Callee = Module.Functions[CalleeIndex];
	if (Callee.bIsMethod || Callee.bReturnsRef
		|| Callee.LocalCount > 64u || !Callee.ExceptionRegions.empty()
		|| BytecodeFunctionHasRefParams(Callee))
	{
		return false;
	}
	const EI32FunctionModeLocal CalleeMode =
		InferI32FunctionModeLocal(Module, Callee);
	if (CalleeMode == EI32FunctionModeLocal::Mixed
		|| !ClosureTypeMatchesLocal(Callee.ClosureTypeName,
			CalleeMode == EI32FunctionModeLocal::Unsigned))
	{
		return false;
	}
	// A bridge-free target cannot depend on PendingJitFunctionIndex or any
	// caller-owned VM call context. Keep this list identical on x64 and AArch64;
	// architecture-specific emitters may only enter a callee accepted here.
	for (std::size_t Ip = 0; Ip < Callee.Code.size();)
	{
		const EOpcode Opcode = static_cast<EOpcode>(Callee.Code[Ip]);
		const std::size_t Size = Bytecode::InstructionSize(Opcode);
		if (Size == 0u || Size > Callee.Code.size() - Ip)
		{
			return false;
		}
		if (Opcode == EOpcode::Call
			&& Bytecode::ReadU16LE(Callee.Code.data() + Ip + 1u) != CalleeIndex)
		{
			return false;
		}
		switch (Opcode)
		{
		case EOpcode::Nop:
		case EOpcode::ConstI32:
		case EOpcode::ConstBool:
		case EOpcode::LoadLocal:
		case EOpcode::StoreLocal:
		case EOpcode::Add:
		case EOpcode::Sub:
		case EOpcode::Mul:
		case EOpcode::Div:
		case EOpcode::Mod:
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
		case EOpcode::Not:
		case EOpcode::Neg:
		case EOpcode::Jmp:
		case EOpcode::JmpIfFalse:
		case EOpcode::Call:
		case EOpcode::Dup:
		case EOpcode::Return:
		case EOpcode::Pop:
		case EOpcode::BitAnd:
		case EOpcode::BitOr:
		case EOpcode::BitXor:
		case EOpcode::BitNot:
		case EOpcode::Shl:
		case EOpcode::Shr:
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
		case EOpcode::AddLocalLocal:
		case EOpcode::JmpIfLocalGeImm:
			{
				break;
			}
		default:
			return false;
		}
		Ip += Size;
	}
	std::vector<std::optional<FI32CfgTagState>> Entries;
	if (!AnalyzeI32BoolCfgTags(Module, Callee, Entries))
	{
		return false;
	}
	std::size_t MaxStackSlots = 1u;
	for (const auto& Entry : Entries)
	{
		if (Entry)
		{
			MaxStackSlots = std::max(MaxStackSlots, Entry->Stack.size());
		}
	}
	if (MaxStackSlots > 64u)
	{
		return false;
	}
	OutCallee.bUnsigned = CalleeMode == EI32FunctionModeLocal::Unsigned;
	OutCallee.LocalCount = Callee.LocalCount;
	OutCallee.StackSlots = static_cast<std::uint16_t>(MaxStackSlots);
	OutCallee.bEligible = true;
	return true;
}

bool IsI32BoolLinearCandidate(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function)
{
	std::vector<std::optional<FI32CfgTagState>> Entries;
	return AnalyzeI32BoolCfgTags(Module, Function, Entries);
}
