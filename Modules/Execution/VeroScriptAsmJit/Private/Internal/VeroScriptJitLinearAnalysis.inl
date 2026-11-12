// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		// Internal fragment: Linear eligibility, ownership and CFG analysis. Included by the owning translation unit.

		enum class EStackTag : std::uint8_t
		{
			I32,
			U32,
			I64,
			U64,
			F32,
			F64,
			Bool,
			String,
			WString,
			Enum,
			Any,
		};

		bool TagFromTypeName(const std::string_view Name, EStackTag& Out)
		{
			if (Name == "int8" || Name == "char" || Name == "int16" || Name == "short")
			{
				Out = EStackTag::I32;
				return true;
			}
			if (Name == "uint8" || Name == "unsigned char" || Name == "uint16" || Name == "unsigned short")
			{
				Out = EStackTag::U32;
				return true;
			}
			if (Name == "int")
			{
				Out = EStackTag::I32;
				return true;
			}
			if (Name == "uint" || Name == "unsigned int" || Name == "unsigned")
			{
				Out = EStackTag::U32;
				return true;
			}
			if (Name == "double")
			{
				Out = EStackTag::F64;
				return true;
			}
			if (Name == "float")
			{
				Out = EStackTag::F32;
				return true;
			}
			if (Name == "int64" || Name == "long")
			{
				Out = EStackTag::I64;
				return true;
			}
			if (Name == "uint64" || Name == "unsigned long")
			{
				Out = EStackTag::U64;
				return true;
			}
			if (Name == "bool")
			{
				Out = EStackTag::Bool;
				return true;
			}
			if (Name == "string")
			{
				Out = EStackTag::String;
				return true;
			}
			if (Name == "wstring")
			{
				Out = EStackTag::WString;
				return true;
			}
			if (!Name.empty())
			{
				Out = EStackTag::Any;
				return true;
			}
			return false;
		}

		bool TagsCompatible(const EStackTag A, const EStackTag B)
		{
			return A == B || A == EStackTag::Any || B == EStackTag::Any;
		}

		bool ParseClosureType(
		    const std::string_view TypeName,
		    std::vector<EStackTag>& OutParams,
		    EStackTag& OutReturn,
		    bool& bOutVoidReturn)
		{
			OutParams.clear();
			bOutVoidReturn = false;
			if (TypeName.size() < 6 || TypeName.substr(0, 3) != "fn(")
			{
				return false;
			}
			std::size_t Depth = 0;
			std::size_t Angle = 0;
			std::size_t Close = std::string_view::npos;
			for (std::size_t I = 2; I < TypeName.size(); ++I)
			{
				const char C = TypeName[I];
				if (C == '<')
				{
					++Angle;
				}
				else if (C == '>' && Angle > 0 && (I == 0 || TypeName[I - 1] != '-'))
				{
					--Angle;
				}
				else if (Angle == 0 && C == '(')
				{
					++Depth;
				}
				else if (Angle == 0 && C == ')')
				{
					if (Depth == 0)
					{
						return false;
					}
					--Depth;
					if (Depth == 0)
					{
						Close = I;
						break;
					}
				}
			}
			if (Close == std::string_view::npos || Close + 2 >= TypeName.size() || TypeName.substr(Close + 1, 2) != "->")
			{
				return false;
			}
			const std::string_view Params = TypeName.substr(3, Close - 3);
			const std::string_view Ret = TypeName.substr(Close + 3);
			if (Ret == "void")
			{
				bOutVoidReturn = true;
			}
			else if (!TagFromTypeName(Ret, OutReturn))
			{
				return false;
			}
			if (Params.empty())
			{
				return true;
			}
			Depth = 0;
			Angle = 0;
			std::size_t Begin = 0;
			auto Flush = [&](const std::size_t End)
			{
				std::string_view Piece = Params.substr(Begin, End - Begin);
				while (!Piece.empty() && (Piece.front() == ' ' || Piece.front() == '\t'))
				{
					Piece.remove_prefix(1);
				}
				while (!Piece.empty() && (Piece.back() == ' ' || Piece.back() == '\t'))
				{
					Piece.remove_suffix(1);
				}
				EStackTag Tag{};
				if (!TagFromTypeName(Piece, Tag))
				{
					return false;
				}
				OutParams.push_back(Tag);
				return true;
			};
			for (std::size_t I = 0; I < Params.size(); ++I)
			{
				const char C = Params[I];
				if (C == '<')
				{
					++Angle;
				}
				else if (C == '>' && Angle > 0 && (I == 0 || Params[I - 1] != '-'))
				{
					--Angle;
				}
				else if (Angle == 0 && C == '(')
				{
					++Depth;
				}
				else if (Angle == 0 && C == ')' && Depth > 0)
				{
					--Depth;
				}
				else if (Angle == 0 && Depth == 0 && C == ',')
				{
					if (!Flush(I))
					{
						return false;
					}
					Begin = I + 1;
				}
			}
			return Flush(Params.size());
		}

		bool IsNumeric(const EStackTag Tag)
		{
			return Tag == EStackTag::I32 || Tag == EStackTag::U32 || Tag == EStackTag::I64 || Tag == EStackTag::U64 || Tag == EStackTag::F32 || Tag == EStackTag::F64 || Tag == EStackTag::Any;
		}
		bool IsInteger(const EStackTag Tag)
		{
			return Tag == EStackTag::I32 || Tag == EStackTag::U32 || Tag == EStackTag::I64 || Tag == EStackTag::U64 || Tag == EStackTag::Any;
		}
		bool IsJumpTargetInRange(
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

		/**
		 * Conservative profitability filter for ownership-aware Linear loops.
		 *
		 * This is deliberately structural rather than benchmark-name or constant
		 * based. Linear remains the correct executor for all of these bodies, but a
		 * loop that repeatedly crosses multiple boxed containers, selects among many
		 * object fields, or constructs dynamically-selected receivers pays more helper
		 * overhead than the VM's single dispatch loop. Known compact typed/fused shapes
		 * do not satisfy these conditions and retain their existing fast paths.
		 */
		bool HasCanonicalBorrowedStringCompareLoopShape(
		    const FBytecodeFunction& Function)
		{
			const auto& Code = Function.Code;
			constexpr std::size_t ShapeSize = 54u;
			for (std::size_t Ip = 0; Ip + ShapeSize <= Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Code.size() - Ip)
				{
					return false;
				}
				if (Op == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 3u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 6u]) == EOpcode::Mul && static_cast<EOpcode>(Code[Ip + 7u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 10u]) == EOpcode::Add && static_cast<EOpcode>(Code[Ip + 11u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 14u]) == EOpcode::Mod && static_cast<EOpcode>(Code[Ip + 15u]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[Ip + 18u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 21u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 24u]) == EOpcode::Mod && static_cast<EOpcode>(Code[Ip + 25u]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[Ip + 28u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 31u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 34u]) == EOpcode::GetIndexCopy && static_cast<EOpcode>(Code[Ip + 35u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 38u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 41u]) == EOpcode::GetIndexCopy && static_cast<EOpcode>(Code[Ip + 42u]) == EOpcode::Eq && static_cast<EOpcode>(Code[Ip + 43u]) == EOpcode::JmpIfFalse && static_cast<EOpcode>(Code[Ip + 48u]) == EOpcode::IncLocal && static_cast<EOpcode>(Code[Ip + 51u]) == EOpcode::IncLocal)
				{
					return true;
				}
				Ip += Size;
			}
			return false;
		}

		bool HasAssignedObjectProjectionScalarReplacementShape(
		    const FBytecodeFunction& Function)
		{
			const auto& Code = Function.Code;
			const auto IsPureScalarExpressionOpcode = [](const EOpcode Op)
			{
				switch (Op)
				{
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
				case EOpcode::LoadLocal:
				case EOpcode::LoadGlobal:
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
				case EOpcode::CheckedCast:
				case EOpcode::GetIndexCopy:
					return true;
				default:
					return false;
				}
			};
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Code.size() - Ip)
				{
					return false;
				}
				if (Op != EOpcode::NewObject || Ip + Size + Bytecode::InstructionSize(EOpcode::StoreLocal) > Code.size() || static_cast<EOpcode>(Code[Ip + Size]) != EOpcode::StoreLocal)
				{
					Ip += Size;
					continue;
				}
				const std::size_t StoreObjectIp = Ip + Size;
				const std::uint16_t ObjectSlot =
				    Bytecode::ReadU16LE(Code.data() + StoreObjectIp + 1u);
				std::size_t Cursor = StoreObjectIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
				std::size_t AssignmentCount = 0;
				while (Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal) <= Code.size() && static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + Cursor + 1u) == ObjectSlot)
				{
					std::size_t Scan = Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal);
					std::size_t OperationCount = 0;
					while (Scan < Code.size() && static_cast<EOpcode>(Code[Scan]) != EOpcode::StoreField && static_cast<EOpcode>(Code[Scan]) != EOpcode::StoreLocal && OperationCount++ < 32u)
					{
						const EOpcode ExpressionOp = static_cast<EOpcode>(Code[Scan]);
						const std::size_t ExpressionSize = Bytecode::InstructionSize(ExpressionOp);
						const bool bSafeBuiltin = ExpressionOp == EOpcode::CallBuiltin && Scan + 3u <= Code.size() && ExpressionSize == Bytecode::InstructionSize(EOpcode::CallBuiltin) && Code[Scan + 2u] == 1u && (static_cast<EBuiltinId>(Code[Scan + 1u]) == EBuiltinId::ToInt || static_cast<EBuiltinId>(Code[Scan + 1u]) == EBuiltinId::ToUnsigned || static_cast<EBuiltinId>(Code[Scan + 1u]) == EBuiltinId::ToFloat || static_cast<EBuiltinId>(Code[Scan + 1u]) == EBuiltinId::ToDouble);
						if ((!IsPureScalarExpressionOpcode(ExpressionOp) || (ExpressionOp == EOpcode::CallBuiltin && !bSafeBuiltin)) || ExpressionSize == 0 || ExpressionSize > Code.size() - Scan)
						{
							Scan = Code.size();
							break;
						}
						Scan += ExpressionSize;
					}
					std::size_t FieldStoreIp = Scan;
					std::size_t NextAssignmentIp = Code.size();
					if (Scan < Code.size() && static_cast<EOpcode>(Code[Scan]) == EOpcode::StoreLocal)
					{
						// A value-producing member assignment is emitted through an internal
						// temporary even when the surrounding expression statement discards it:
						//   StoreLocal t; LoadLocal t; StoreField f; Pop; LoadLocal t; Pop.
						// Treat that compiler-owned temporary as part of the same assignment.
						constexpr std::size_t TempAssignmentTailSize = 14u;
						if (Scan + TempAssignmentTailSize > Code.size() || static_cast<EOpcode>(Code[Scan + 3u]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[Scan + 6u]) != EOpcode::StoreField || static_cast<EOpcode>(Code[Scan + 9u]) != EOpcode::Pop || static_cast<EOpcode>(Code[Scan + 10u]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[Scan + 13u]) != EOpcode::Pop || Bytecode::ReadU16LE(Code.data() + Scan + 1u) != Bytecode::ReadU16LE(Code.data() + Scan + 4u) || Bytecode::ReadU16LE(Code.data() + Scan + 1u) != Bytecode::ReadU16LE(Code.data() + Scan + 11u))
						{
							break;
						}
						FieldStoreIp = Scan + 6u;
						NextAssignmentIp = Scan + TempAssignmentTailSize;
					}
					else
					{
						if (Scan + Bytecode::InstructionSize(EOpcode::StoreField) + Bytecode::InstructionSize(EOpcode::Pop) > Code.size() || static_cast<EOpcode>(Code[Scan]) != EOpcode::StoreField || static_cast<EOpcode>(Code[Scan + Bytecode::InstructionSize(EOpcode::StoreField)]) != EOpcode::Pop)
						{
							break;
						}
						NextAssignmentIp = Scan + Bytecode::InstructionSize(EOpcode::StoreField) + Bytecode::InstructionSize(EOpcode::Pop);
					}
					++AssignmentCount;
					(void)FieldStoreIp;
					Cursor = NextAssignmentIp;
				}
				if (AssignmentCount == 0 || Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal) * 2u + Bytecode::InstructionSize(EOpcode::CallBoundMethod) + Bytecode::InstructionSize(EOpcode::Add) > Code.size())
				{
					Ip += Size;
					continue;
				}
				const std::size_t ReceiverIp = Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal);
				std::size_t CallIp = ReceiverIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
				std::size_t ArgumentCount = 0;
				while (CallIp < Code.size() && static_cast<EOpcode>(Code[CallIp]) != EOpcode::CallBoundMethod && ArgumentCount < 4u)
				{
					const EOpcode ArgumentOp = static_cast<EOpcode>(Code[CallIp]);
					if (ArgumentOp != EOpcode::LoadLocal && ArgumentOp != EOpcode::ConstI32 && ArgumentOp != EOpcode::ConstF64 && ArgumentOp != EOpcode::ConstBool)
					{
						break;
					}
					CallIp += Bytecode::InstructionSize(ArgumentOp);
					++ArgumentCount;
				}
				const std::size_t AddIp = CallIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod);
				std::size_t TailIp = AddIp + Bytecode::InstructionSize(EOpcode::Add);
				if (CallIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod) > Code.size() || AddIp + Bytecode::InstructionSize(EOpcode::Add) > Code.size() || static_cast<EOpcode>(Code[Cursor]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[ReceiverIp]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + ReceiverIp + 1u) != ObjectSlot || static_cast<EOpcode>(Code[CallIp]) != EOpcode::CallBoundMethod || Code[CallIp + 3u] != ArgumentCount || static_cast<EOpcode>(Code[AddIp]) != EOpcode::Add)
				{
					Ip += Size;
					continue;
				}
				if (TailIp < Code.size() && static_cast<EOpcode>(Code[TailIp]) != EOpcode::StoreLocal)
				{
					const EOpcode Operand = static_cast<EOpcode>(Code[TailIp]);
					if (Operand != EOpcode::ConstI32 && Operand != EOpcode::LoadLocal)
					{
						Ip += Size;
						continue;
					}
					TailIp += Bytecode::InstructionSize(Operand);
					if (TailIp >= Code.size() || static_cast<EOpcode>(Code[TailIp]) != EOpcode::Mod)
					{
						Ip += Size;
						continue;
					}
					TailIp += Bytecode::InstructionSize(EOpcode::Mod);
				}
				if (TailIp + Bytecode::InstructionSize(EOpcode::StoreLocal) <= Code.size() && static_cast<EOpcode>(Code[TailIp]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + Cursor + 1u) == Bytecode::ReadU16LE(Code.data() + TailIp + 1u))
				{
					return true;
				}
				Ip += Size;
			}
			return false;
		}

		bool HasCanonicalTypedAffineMapValueUpdate(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			const auto& Code = Function.Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Code.size() - Ip)
				{
					return false;
				}
				if (Op == EOpcode::LoadLocal)
				{
					constexpr std::size_t ConstantSize = 3u;
					const std::size_t AfterIp = Ip + Size;
					const std::size_t MulIp = AfterIp + ConstantSize;
					const std::size_t AddConstantIp = MulIp + 1u;
					const std::size_t AddIp = AddConstantIp + ConstantSize;
					const std::size_t ModulusIp = AddIp + 1u;
					const std::size_t ModIp = ModulusIp + ConstantSize;
					const std::size_t StateStoreIp = ModIp + 1u;
					const std::size_t BodyIp = StateStoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
					constexpr std::size_t BodySize = 31u;
					if (BodyIp + BodySize + Bytecode::InstructionSize(EOpcode::IncLocal) <= Code.size() && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul && static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add && static_cast<EOpcode>(Code[ModulusIp]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[ModIp]) == EOpcode::Mod && static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Bytecode::ReadU16LE(Code.data() + Ip + 1u) && static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 12u]) == EOpcode::GetIndexCopy && static_cast<EOpcode>(Code[BodyIp + 13u]) == EOpcode::ConstF64 && static_cast<EOpcode>(Code[BodyIp + 16u]) == EOpcode::Add && static_cast<EOpcode>(Code[BodyIp + 17u]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[BodyIp + 20u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 23u]) == EOpcode::SetIndex && static_cast<EOpcode>(Code[BodyIp + 24u]) == EOpcode::StoreLocal && static_cast<EOpcode>(Code[BodyIp + 27u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 30u]) == EOpcode::Pop)
					{
						const std::uint16_t DeltaIndex = Bytecode::ReadU16LE(
						    Code.data() + BodyIp + 14u);
						if (DeltaIndex < Module.Constants.size() && Module.Constants[DeltaIndex].Kind == EConstantKind::F64)
						{
							return true;
						}
					}
				}
				Ip += Size;
			}
			return false;
		}

		bool HasCanonicalAffineF64ArrayRecurrence(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			const auto& Code = Function.Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Code.size() - Ip)
				{
					return false;
				}
				if (Op == EOpcode::LoadLocal)
				{
					constexpr std::size_t ConstantSize = 3u;
					const std::size_t AfterIp = Ip + Size;
					const std::size_t MulIp = AfterIp + ConstantSize;
					const std::size_t AddConstantIp = MulIp + 1u;
					const std::size_t AddIp = AddConstantIp + ConstantSize;
					const std::size_t StateModulusIp = AddIp + 1u;
					const std::size_t StateModIp = StateModulusIp + ConstantSize;
					const std::size_t StateStoreIp = StateModIp + 1u;
					const std::size_t BodyIp = StateStoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
					constexpr std::size_t BodySize = 26u;
					if (BodyIp + BodySize + Bytecode::InstructionSize(EOpcode::IncLocal) <= Code.size() && static_cast<EOpcode>(Code[AfterIp]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[MulIp]) == EOpcode::Mul && static_cast<EOpcode>(Code[AddConstantIp]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[AddIp]) == EOpcode::Add && static_cast<EOpcode>(Code[StateModulusIp]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[StateModIp]) == EOpcode::Mod && static_cast<EOpcode>(Code[StateStoreIp]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + StateStoreIp + 1u) == Bytecode::ReadU16LE(Code.data() + Ip + 1u) && static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::ConstF64 && static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::Mul && static_cast<EOpcode>(Code[BodyIp + 7u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 10u]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + BodyIp + 11u) == Bytecode::ReadU16LE(Code.data() + Ip + 1u) && static_cast<EOpcode>(Code[BodyIp + 13u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[BodyIp + 16u]) == EOpcode::Mod && static_cast<EOpcode>(Code[BodyIp + 17u]) == EOpcode::GetIndexCopy && static_cast<EOpcode>(Code[BodyIp + 18u]) == EOpcode::ConstF64 && static_cast<EOpcode>(Code[BodyIp + 21u]) == EOpcode::Mul && static_cast<EOpcode>(Code[BodyIp + 22u]) == EOpcode::Add && static_cast<EOpcode>(Code[BodyIp + 23u]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + BodyIp + 24u) == Bytecode::ReadU16LE(Code.data() + BodyIp + 1u))
					{
						const std::uint16_t AccumulatorMultiplier =
						    Bytecode::ReadU16LE(Code.data() + BodyIp + 4u);
						const std::uint16_t ElementMultiplier =
						    Bytecode::ReadU16LE(Code.data() + BodyIp + 19u);
						if (AccumulatorMultiplier < Module.Constants.size() && ElementMultiplier < Module.Constants.size() && Module.Constants[AccumulatorMultiplier].Kind == EConstantKind::F64 && Module.Constants[ElementMultiplier].Kind == EConstantKind::F64)
						{
							return true;
						}
					}
				}
				Ip += Size;
			}
			return false;
		}

		bool HasGeneralF64MapReadModifyWrite(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			bool bHasF64Map = false;
			bool bHasGet = false;
			bool bHasSet = false;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Function.Code.size() - Ip)
				{
					return false;
				}
				if (Op == EOpcode::NewMap)
				{
					const std::uint16_t TypeIndex = Bytecode::ReadU16LE(
					    Function.Code.data() + Ip + 1u);
					std::string KeyType;
					std::string ValueType;
					bHasF64Map = bHasF64Map || (TypeIndex < Module.StringPool.size() && JitHost::Services().SplitMapType(Module.StringPool[TypeIndex], KeyType, ValueType) && (KeyType == "int" || KeyType == "int32") && (ValueType == "double" || ValueType == "float64"));
				}
				else if (Op == EOpcode::GetIndexCopy)
				{
					bHasGet = true;
				}
				else if (Op == EOpcode::SetIndex)
				{
					bHasSet = true;
				}
				Ip += Size;
			}
			return bHasF64Map && bHasGet && bHasSet;
		}

		bool TryValidateRetainedPairProjection(
		    const FBytecodeModule& Module,
		    const std::uint16_t TypeIndex,
		    const std::uint16_t ArrayTypeName,
		    const std::uint16_t FirstName,
		    const std::uint16_t SecondName,
		    const std::uint16_t PushName,
		    const std::uint16_t ProjectionFunction,
		    FRetainedObjectArrayProjectionShape& Out)
		{
			if (TypeIndex >= Module.Types.size() || ArrayTypeName >= Module.StringPool.size() || FirstName >= Module.StringPool.size() || SecondName >= Module.StringPool.size() || PushName >= Module.StringPool.size() || ProjectionFunction >= Module.Functions.size() || Module.StringPool[PushName] != "push")
			{
				return false;
			}
			const std::array<std::uint16_t, 5> RequiredSlots{{Out.ParameterSlot, Out.ArraySlot, Out.ObjectSlot,
			    Out.CounterSlot, Out.TotalSlot}};
			for (std::size_t Left = 0; Left < RequiredSlots.size(); ++Left)
			{
				if (RequiredSlots[Left] == 0xFFFFu)
				{
					return false;
				}
				for (std::size_t Right = Left + 1u; Right < RequiredSlots.size(); ++Right)
				{
					if (RequiredSlots[Left] == RequiredSlots[Right])
					{
						return false;
					}
				}
			}
			if (Out.ReverseSlot != 0xFFFFu && std::find(RequiredSlots.begin(), RequiredSlots.end(), Out.ReverseSlot) != RequiredSlots.end())
			{
				return false;
			}
			std::string ElementType;
			if (!JitHost::Services().ParseDynamicArrayElementType(
			        Module.StringPool[ArrayTypeName], ElementType))
			{
				return false;
			}
			const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
			if (ElementType != Type.Name || Type.bIsStatic || Type.bIsAbstract || !Type.BaseTypeIndices.empty() || !Type.HostBaseTypeName.empty() || Type.DestructorFunctionIndex != 0xFFFFu || Type.Fields.size() != 2u)
			{
				return false;
			}
			for (const FBytecodeFieldDesc& Field : Type.Fields)
			{
				if (!Field.bHasDefault || Field.bHasInitCode || (Field.TypeName != "int" && Field.TypeName != "int32"))
				{
					return false;
				}
			}
			if (Type.Fields[0].Name != Module.StringPool[FirstName] || Type.Fields[1].Name != Module.StringPool[SecondName])
			{
				return false;
			}
			const FBytecodeFunction& Projection = Module.Functions[ProjectionFunction];
			if (!Projection.bIsMethod || Projection.bReturnsVoid || Projection.bReturnsRef || Projection.bIsAbstract || Projection.bDeleted || Projection.ParamCount != 0u || Projection.OwnerTypeIndex != TypeIndex || !Projection.ExceptionRegions.empty() || Projection.Code.size() != 8u || static_cast<EOpcode>(Projection.Code[0]) != EOpcode::LoadThisField || static_cast<EOpcode>(Projection.Code[3]) != EOpcode::LoadThisField || static_cast<EOpcode>(Projection.Code[6]) != EOpcode::Add || static_cast<EOpcode>(Projection.Code[7]) != EOpcode::Return || Bytecode::ReadU16LE(Projection.Code.data() + 1) != FirstName || Bytecode::ReadU16LE(Projection.Code.data() + 4) != SecondName)
			{
				return false;
			}
			Out.ProjectionFunctionIndex = ProjectionFunction;
			Out.ProjectionAddOffset = 6u;
			return true;
		}

		bool TryDescribeRetainedObjectArrayProjectionShape(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    FRetainedObjectArrayProjectionShape& Out)
		{
			Out = {};
			const auto& C = Function.Code;
			// Safe periodic forward construction: field0 = i % M0 and
			// field1 = (i % M1) * K, followed by a complete forward projection.
			// Positive moduli, a non-negative multiplier and a proven bounded
			// product make the construction exception-free; projection and total
			// additions remain checked by the execution path.
			if (!Function.bIsMethod && !Function.bReturnsVoid && Function.ParamCount == 1u && Function.LocalCount >= 7u && Function.ExceptionRegions.empty() && C.size() == 135u)
			{
				const auto Op = [&](const std::size_t Ip)
				{
					return static_cast<EOpcode>(C[Ip]);
				};
				const std::array<std::pair<std::size_t, EOpcode>, 47> Expected{{{0, EOpcode::NewDynamicArray}, {3, EOpcode::StoreLocal}, {6, EOpcode::ConstI32},
				    {9, EOpcode::StoreLocal}, {12, EOpcode::LoadLocal}, {15, EOpcode::LoadLocal},
				    {18, EOpcode::Lt}, {19, EOpcode::JmpIfFalse}, {24, EOpcode::NewObject},
				    {27, EOpcode::StoreLocal}, {30, EOpcode::LoadLocal}, {33, EOpcode::LoadLocal},
				    {36, EOpcode::ConstI32}, {39, EOpcode::Mod}, {40, EOpcode::StoreField},
				    {43, EOpcode::Pop}, {44, EOpcode::LoadLocal}, {47, EOpcode::LoadLocal},
				    {50, EOpcode::ConstI32}, {53, EOpcode::Mod}, {54, EOpcode::ConstI32},
				    {57, EOpcode::Mul}, {58, EOpcode::StoreField}, {61, EOpcode::Pop},
				    {62, EOpcode::LoadLocal}, {65, EOpcode::LoadLocal}, {68, EOpcode::CallMethod},
				    {72, EOpcode::Pop}, {73, EOpcode::IncLocal}, {76, EOpcode::Jmp},
				    {81, EOpcode::ConstI32}, {84, EOpcode::StoreLocal}, {87, EOpcode::ConstI32},
				    {90, EOpcode::StoreLocal}, {93, EOpcode::LoadLocal}, {96, EOpcode::LoadLocal},
				    {99, EOpcode::Lt}, {100, EOpcode::JmpIfFalse}, {105, EOpcode::LoadLocal},
				    {108, EOpcode::LoadLocal}, {111, EOpcode::LoadLocal},
				    {114, EOpcode::GetIndexCopy}, {115, EOpcode::CallBoundMethod},
				    {119, EOpcode::Add}, {120, EOpcode::StoreLocal}, {123, EOpcode::IncLocal},
				    {126, EOpcode::Jmp}}};
				for (const auto& [Ip, ExpectedOp] : Expected)
				{
					if (Op(Ip) != ExpectedOp)
					{
						return false;
					}
				}
				if (Op(131) != EOpcode::LoadLocal || Op(134) != EOpcode::Return || Bytecode::ReadI32LE(C.data() + 20) != 57 || Bytecode::ReadI32LE(C.data() + 77) != -69 || Bytecode::ReadI32LE(C.data() + 101) != 26 || Bytecode::ReadI32LE(C.data() + 127) != -38 || C[71] != 1u || C[118] != 0u)
				{
					return false;
				}
				const auto ReadI32Constant = [&](const std::size_t Ip, std::int32_t& Value)
				{
					const std::uint16_t Index = Bytecode::ReadU16LE(C.data() + Ip + 1u);
					if (Index >= Module.Constants.size() || Module.Constants[Index].Kind != EConstantKind::I32)
					{
						return false;
					}
					Value = Module.Constants[Index].I32;
					return true;
				};
				std::int32_t FirstCounter = 0, FirstModulus = 0;
				std::int32_t SecondModulus = 0, SecondMultiplier = 0;
				std::int32_t InitialTotal = 0, TraversalCounter = 0;
				if (!ReadI32Constant(6u, FirstCounter) || !ReadI32Constant(36u, FirstModulus) || !ReadI32Constant(50u, SecondModulus) || !ReadI32Constant(54u, SecondMultiplier) || !ReadI32Constant(81u, InitialTotal) || !ReadI32Constant(87u, TraversalCounter) || FirstCounter != 0 || InitialTotal != 0 || TraversalCounter != 0 || FirstModulus <= 0 || SecondModulus <= 0 || SecondMultiplier < 0 || static_cast<std::int64_t>(SecondModulus - 1) * SecondMultiplier > std::numeric_limits<std::int32_t>::max())
				{
					return false;
				}
				const std::uint16_t Parameter = Bytecode::ReadU16LE(C.data() + 16);
				const std::uint16_t Array = Bytecode::ReadU16LE(C.data() + 4);
				const std::uint16_t Counter = Bytecode::ReadU16LE(C.data() + 10);
				const std::uint16_t Object = Bytecode::ReadU16LE(C.data() + 28);
				const std::uint16_t Total = Bytecode::ReadU16LE(C.data() + 85);
				const std::uint16_t Traversal = Bytecode::ReadU16LE(C.data() + 91);
				if (Parameter >= Function.ParamCount || Array >= Function.LocalCount || Counter >= Function.LocalCount || Object >= Function.LocalCount || Total >= Function.LocalCount || Traversal >= Function.LocalCount || Counter == Traversal || Bytecode::ReadU16LE(C.data() + 13) != Counter || Bytecode::ReadU16LE(C.data() + 31) != Object || Bytecode::ReadU16LE(C.data() + 34) != Counter || Bytecode::ReadU16LE(C.data() + 45) != Object || Bytecode::ReadU16LE(C.data() + 48) != Counter || Bytecode::ReadU16LE(C.data() + 63) != Array || Bytecode::ReadU16LE(C.data() + 66) != Object || Bytecode::ReadU16LE(C.data() + 74) != Counter || Bytecode::ReadU16LE(C.data() + 94) != Traversal || Bytecode::ReadU16LE(C.data() + 97) != Parameter || Bytecode::ReadU16LE(C.data() + 106) != Total || Bytecode::ReadU16LE(C.data() + 109) != Array || Bytecode::ReadU16LE(C.data() + 112) != Traversal || Bytecode::ReadU16LE(C.data() + 121) != Total || Bytecode::ReadU16LE(C.data() + 124) != Traversal || Bytecode::ReadU16LE(C.data() + 132) != Total)
				{
					return false;
				}
				const std::uint16_t TypeIndex = Bytecode::ReadU16LE(C.data() + 25);
				const std::uint16_t ArrayTypeName = Bytecode::ReadU16LE(C.data() + 1);
				const std::uint16_t FirstName = Bytecode::ReadU16LE(C.data() + 41);
				const std::uint16_t SecondName = Bytecode::ReadU16LE(C.data() + 59);
				const std::uint16_t PushName = Bytecode::ReadU16LE(C.data() + 69);
				const std::uint16_t ProjectionFunction = Bytecode::ReadU16LE(C.data() + 116);
				Out.ParameterSlot = Parameter;
				Out.ArraySlot = Array;
				Out.ObjectSlot = Object;
				Out.CounterSlot = Counter;
				Out.TotalSlot = Total;
				Out.ReverseSlot = Traversal;
				Out.FirstModulus = FirstModulus;
				Out.SecondModulus = SecondModulus;
				Out.SecondPostMultiplier = SecondMultiplier;
				Out.ProjectionCallOffset = 115u;
				Out.TotalAddOffset = 119u;
				Out.bForwardModuloMultiply = true;
				return TryValidateRetainedPairProjection(
				    Module, TypeIndex, ArrayTypeName, FirstName, SecondName,
				    PushName, ProjectionFunction, Out);
			}
			if (!Function.bIsMethod && !Function.bReturnsVoid && Function.ParamCount == 1u && Function.LocalCount >= 6u && Function.ExceptionRegions.empty() && C.size() == 123u)
			{
				const auto Op = [&](const std::size_t Ip)
				{
					return static_cast<EOpcode>(C[Ip]);
				};
				const std::array<std::pair<std::size_t, EOpcode>, 37> Expected{{{0, EOpcode::NewDynamicArray}, {3, EOpcode::StoreLocal}, {6, EOpcode::ConstI32},
				    {9, EOpcode::StoreLocal}, {12, EOpcode::LoadLocal}, {15, EOpcode::LoadLocal},
				    {18, EOpcode::Lt}, {19, EOpcode::JmpIfFalse}, {24, EOpcode::NewObject},
				    {27, EOpcode::StoreLocal}, {30, EOpcode::LoadLocal}, {33, EOpcode::LoadLocal},
				    {36, EOpcode::StoreField}, {39, EOpcode::Pop}, {40, EOpcode::LoadLocal},
				    {43, EOpcode::ConstI32}, {46, EOpcode::StoreField}, {49, EOpcode::Pop},
				    {50, EOpcode::LoadLocal}, {53, EOpcode::LoadLocal}, {56, EOpcode::CallMethod},
				    {60, EOpcode::Pop}, {61, EOpcode::IncLocal}, {64, EOpcode::Jmp},
				    {69, EOpcode::ConstI32}, {72, EOpcode::StoreLocal}, {75, EOpcode::ConstI32},
				    {78, EOpcode::StoreLocal}, {81, EOpcode::LoadLocal}, {84, EOpcode::LoadLocal},
				    {87, EOpcode::Lt}, {88, EOpcode::JmpIfFalse}, {93, EOpcode::LoadLocal},
				    {96, EOpcode::LoadLocal}, {99, EOpcode::LoadLocal}, {102, EOpcode::GetIndexCopy},
				    {103, EOpcode::CallBoundMethod}}};
				for (const auto& [Ip, ExpectedOp] : Expected)
				{
					if (Op(Ip) != ExpectedOp)
					{
						return false;
					}
				}
				if (Op(107) != EOpcode::Add || Op(108) != EOpcode::StoreLocal || Op(111) != EOpcode::IncLocal || Op(114) != EOpcode::Jmp || Op(119) != EOpcode::LoadLocal || Op(122) != EOpcode::Return || Bytecode::ReadI32LE(C.data() + 20) != 45 || Bytecode::ReadI32LE(C.data() + 65) != -57 || Bytecode::ReadI32LE(C.data() + 89) != 26 || Bytecode::ReadI32LE(C.data() + 115) != -38 || C[59] != 1u || C[106] != 0u)
				{
					return false;
				}
				const auto ReadI32Constant = [&](const std::size_t Ip, std::int32_t& Value)
				{
					const std::uint16_t Index = Bytecode::ReadU16LE(C.data() + Ip + 1u);
					if (Index >= Module.Constants.size() || Module.Constants[Index].Kind != EConstantKind::I32)
					{
						return false;
					}
					Value = Module.Constants[Index].I32;
					return true;
				};
				std::int32_t FirstCounter = 0, SecondConstant = 0;
				std::int32_t InitialTotal = 0, SecondCounter = 0;
				if (!ReadI32Constant(6u, FirstCounter) || !ReadI32Constant(43u, SecondConstant) || !ReadI32Constant(69u, InitialTotal) || !ReadI32Constant(75u, SecondCounter) || FirstCounter != 0 || InitialTotal != 0 || SecondCounter != 0)
				{
					return false;
				}
				const std::uint16_t Parameter = Bytecode::ReadU16LE(C.data() + 16);
				const std::uint16_t Array = Bytecode::ReadU16LE(C.data() + 4);
				const std::uint16_t Counter = Bytecode::ReadU16LE(C.data() + 10);
				const std::uint16_t Object = Bytecode::ReadU16LE(C.data() + 28);
				const std::uint16_t Total = Bytecode::ReadU16LE(C.data() + 73);
				if (Parameter >= Function.ParamCount || Array >= Function.LocalCount || Counter >= Function.LocalCount || Object >= Function.LocalCount || Total >= Function.LocalCount || Bytecode::ReadU16LE(C.data() + 13) != Counter || Bytecode::ReadU16LE(C.data() + 31) != Object || Bytecode::ReadU16LE(C.data() + 34) != Counter || Bytecode::ReadU16LE(C.data() + 41) != Object || Bytecode::ReadU16LE(C.data() + 51) != Array || Bytecode::ReadU16LE(C.data() + 54) != Object || Bytecode::ReadU16LE(C.data() + 62) != Counter || Bytecode::ReadU16LE(C.data() + 79) != Counter || Bytecode::ReadU16LE(C.data() + 82) != Counter || Bytecode::ReadU16LE(C.data() + 85) != Parameter || Bytecode::ReadU16LE(C.data() + 94) != Total || Bytecode::ReadU16LE(C.data() + 97) != Array || Bytecode::ReadU16LE(C.data() + 100) != Counter || Bytecode::ReadU16LE(C.data() + 109) != Total || Bytecode::ReadU16LE(C.data() + 112) != Counter || Bytecode::ReadU16LE(C.data() + 120) != Total)
				{
					return false;
				}
				const std::uint16_t TypeIndex = Bytecode::ReadU16LE(C.data() + 25);
				const std::uint16_t ArrayTypeName = Bytecode::ReadU16LE(C.data() + 1);
				const std::uint16_t FirstName = Bytecode::ReadU16LE(C.data() + 37);
				const std::uint16_t SecondName = Bytecode::ReadU16LE(C.data() + 47);
				const std::uint16_t PushName = Bytecode::ReadU16LE(C.data() + 57);
				const std::uint16_t ProjectionFunction = Bytecode::ReadU16LE(C.data() + 104);
				Out.ParameterSlot = Parameter;
				Out.ArraySlot = Array;
				Out.ObjectSlot = Object;
				Out.CounterSlot = Counter;
				Out.TotalSlot = Total;
				Out.SecondConstant = SecondConstant;
				Out.ProjectionCallOffset = 103u;
				Out.TotalAddOffset = 107u;
				Out.bForwardDirectConstant = true;
				return TryValidateRetainedPairProjection(
				    Module, TypeIndex, ArrayTypeName, FirstName, SecondName,
				    PushName, ProjectionFunction, Out);
			}
			// Canonical lowering of two loops: construct a local array of trivial
			// two-field objects, then traverse it backwards through `field0+field1`.
			// The exact sequence is intentionally strict because this proof removes
			// both allocation and object identity.
			if (Function.bIsMethod || Function.bReturnsVoid || Function.ParamCount != 1u || Function.LocalCount < 7u || !Function.ExceptionRegions.empty() || C.size() != 147u)
			{
				return false;
			}
			const auto Op = [&](const std::size_t Ip)
			{
				return static_cast<EOpcode>(C[Ip]);
			};
			const std::array<std::pair<std::size_t, EOpcode>, 35> Expected{{{0, EOpcode::NewDynamicArray}, {3, EOpcode::StoreLocal}, {6, EOpcode::ConstI32},
			    {9, EOpcode::StoreLocal}, {12, EOpcode::LoadLocal}, {15, EOpcode::LoadLocal},
			    {18, EOpcode::Lt}, {19, EOpcode::JmpIfFalse}, {24, EOpcode::NewObject},
			    {27, EOpcode::StoreLocal}, {30, EOpcode::LoadLocal}, {33, EOpcode::LoadLocal},
			    {36, EOpcode::ConstI32}, {39, EOpcode::Add}, {40, EOpcode::ConstI32},
			    {43, EOpcode::BitAnd}, {44, EOpcode::StoreField}, {47, EOpcode::Pop},
			    {48, EOpcode::LoadLocal}, {51, EOpcode::LoadLocal}, {54, EOpcode::ConstI32},
			    {57, EOpcode::Mul}, {58, EOpcode::ConstI32}, {61, EOpcode::BitAnd},
			    {62, EOpcode::StoreField}, {65, EOpcode::Pop}, {66, EOpcode::LoadLocal},
			    {69, EOpcode::LoadLocal}, {72, EOpcode::CallMethod}, {76, EOpcode::Pop},
			    {77, EOpcode::IncLocal}, {80, EOpcode::Jmp}, {85, EOpcode::ConstI32},
			    {88, EOpcode::StoreLocal}, {91, EOpcode::LoadLocal}}};
			for (const auto& [Ip, ExpectedOp] : Expected)
			{
				if (Op(Ip) != ExpectedOp)
				{
					return false;
				}
			}
			const std::array<std::pair<std::size_t, EOpcode>, 17> Tail{{{94, EOpcode::ConstI32}, {97, EOpcode::Sub}, {98, EOpcode::StoreLocal},
			    {101, EOpcode::LoadLocal}, {104, EOpcode::ConstI32}, {107, EOpcode::Ge},
			    {108, EOpcode::JmpIfFalse}, {113, EOpcode::LoadLocal}, {116, EOpcode::LoadLocal},
			    {119, EOpcode::LoadLocal}, {122, EOpcode::GetIndexCopy},
			    {123, EOpcode::CallBoundMethod}, {127, EOpcode::Add}, {128, EOpcode::StoreLocal},
			    {131, EOpcode::AddLocalImm}, {138, EOpcode::Jmp}, {143, EOpcode::LoadLocal}}};
			for (const auto& [Ip, ExpectedOp] : Tail)
			{
				if (Op(Ip) != ExpectedOp)
				{
					return false;
				}
			}
			const auto ReadI32Constant = [&](const std::size_t Ip, std::int32_t& Value)
			{
				const std::uint16_t Index = Bytecode::ReadU16LE(C.data() + Ip + 1u);
				if (Index >= Module.Constants.size() || Module.Constants[Index].Kind != EConstantKind::I32)
				{
					return false;
				}
				Value = Module.Constants[Index].I32;
				return true;
			};
			std::int32_t InitialCounter = 0, FirstAddend = 0, FirstMask = 0;
			std::int32_t SecondMultiplier = 0, SecondMask = 0, InitialTotal = 0;
			std::int32_t ReverseSubtrahend = 0, ReverseBound = 0;
			if (!ReadI32Constant(6u, InitialCounter) || !ReadI32Constant(36u, FirstAddend) || !ReadI32Constant(40u, FirstMask) || !ReadI32Constant(54u, SecondMultiplier) || !ReadI32Constant(58u, SecondMask) || !ReadI32Constant(85u, InitialTotal) || !ReadI32Constant(94u, ReverseSubtrahend) || !ReadI32Constant(104u, ReverseBound))
			{
				return false;
			}
			if (Op(146) != EOpcode::Return || Bytecode::ReadI32LE(C.data() + 20) != 61 || Bytecode::ReadI32LE(C.data() + 81) != -73 || Bytecode::ReadI32LE(C.data() + 109) != 30 || Bytecode::ReadI32LE(C.data() + 139) != -42 || InitialCounter != 0 || InitialTotal != 0 || ReverseSubtrahend != 1 || ReverseBound != 0 || Bytecode::ReadI32LE(C.data() + 134) != -1 || C[75] != 1u || C[126] != 0u)
			{
				return false;
			}
			const std::uint16_t Parameter = Bytecode::ReadU16LE(C.data() + 16);
			const std::uint16_t Array = Bytecode::ReadU16LE(C.data() + 4);
			const std::uint16_t Counter = Bytecode::ReadU16LE(C.data() + 10);
			const std::uint16_t Object = Bytecode::ReadU16LE(C.data() + 28);
			const std::uint16_t Total = Bytecode::ReadU16LE(C.data() + 89);
			const std::uint16_t Reverse = Bytecode::ReadU16LE(C.data() + 99);
			if (Parameter >= Function.ParamCount || Array >= Function.LocalCount || Counter >= Function.LocalCount || Object >= Function.LocalCount || Total >= Function.LocalCount || Reverse >= Function.LocalCount || Bytecode::ReadU16LE(C.data() + 13) != Counter || Bytecode::ReadU16LE(C.data() + 34) != Counter || Bytecode::ReadU16LE(C.data() + 52) != Counter || Bytecode::ReadU16LE(C.data() + 78) != Counter || Bytecode::ReadU16LE(C.data() + 31) != Object || Bytecode::ReadU16LE(C.data() + 49) != Object || Bytecode::ReadU16LE(C.data() + 67) != Array || Bytecode::ReadU16LE(C.data() + 70) != Object || Bytecode::ReadU16LE(C.data() + 92) != Parameter || Bytecode::ReadU16LE(C.data() + 102) != Reverse || Bytecode::ReadU16LE(C.data() + 114) != Total || Bytecode::ReadU16LE(C.data() + 117) != Array || Bytecode::ReadU16LE(C.data() + 120) != Reverse || Bytecode::ReadU16LE(C.data() + 129) != Total || Bytecode::ReadU16LE(C.data() + 132) != Reverse || Bytecode::ReadU16LE(C.data() + 144) != Total)
			{
				return false;
			}
			const std::uint16_t TypeIndex = Bytecode::ReadU16LE(C.data() + 25);
			const std::uint16_t ArrayTypeName = Bytecode::ReadU16LE(C.data() + 1);
			const std::uint16_t FirstName = Bytecode::ReadU16LE(C.data() + 45);
			const std::uint16_t SecondName = Bytecode::ReadU16LE(C.data() + 63);
			const std::uint16_t PushName = Bytecode::ReadU16LE(C.data() + 73);
			const std::uint16_t ProjectionFunction = Bytecode::ReadU16LE(C.data() + 124);
			Out.ParameterSlot = Parameter;
			Out.ArraySlot = Array;
			Out.ObjectSlot = Object;
			Out.CounterSlot = Counter;
			Out.TotalSlot = Total;
			Out.ReverseSlot = Reverse;
			Out.FirstAddend = FirstAddend;
			Out.FirstMask = FirstMask;
			Out.SecondMultiplier = SecondMultiplier;
			Out.SecondMask = SecondMask;
			Out.FirstAddOffset = 39u;
			Out.SecondMultiplyOffset = 57u;
			Out.ProjectionAddOffset = 6u;
			Out.ProjectionCallOffset = 123u;
			Out.TotalAddOffset = 127u;
			Out.ReverseInitializeOffset = 97u;
			return TryValidateRetainedPairProjection(
			    Module, TypeIndex, ArrayTypeName, FirstName, SecondName,
			    PushName, ProjectionFunction, Out);
		}

		bool PreferBoundedReceiverExpression(const FBytecodeFunction& Function,
		    const std::vector<EStackTag>& Parameters, const EStackTag Return)
		{
			if (!Function.bIsMethod || Function.bReturnsVoid || Function.bReturnsRef || Return != EStackTag::I32 || Function.LocalCount > 32 || Function.Code.size() > 512 || !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function) || std::any_of(Parameters.begin(), Parameters.end(), [](auto Tag)
			{
				return Tag != EStackTag::I32;
			}))
			{
				return false;
			}
			bool Receiver = false;
			std::size_t Count = 0, Depth = 0, Writes = 0;
			for (std::size_t Offset = 0; Offset < Function.Code.size();)
			{
				if (++Count > 64) return false;
				const auto Op = static_cast<EOpcode>(Function.Code[Offset]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (!Size || Size > Function.Code.size() - Offset) return false;
				switch (Op)
				{
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
					{
						Receiver = true;
						if ((Op == EOpcode::StoreThisField || Op == EOpcode::InitializeThisField) && ++Writes > 16) return false;
						break;
					}
				case EOpcode::Nop:
				case EOpcode::ConstI32:
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
				case EOpcode::Dup:
				case EOpcode::Pop:
					{
						break;
					}
				case EOpcode::Return:
					return Receiver && Depth == 1 && Offset + Size == Function.Code.size();
				default:
					return false;
				}
				if (Op == EOpcode::ConstI32 || Op == EOpcode::LoadLocal || Op == EOpcode::LoadThisField || Op == EOpcode::Dup)
				{
					if (Op == EOpcode::Dup && Depth == 0) return false;
					if (++Depth > 16) return false;
				}
				else if (Op == EOpcode::StoreLocal || Op == EOpcode::StoreThisField || Op == EOpcode::InitializeThisField || Op == EOpcode::Pop)
				{
					if (Depth == 0) return false;
					--Depth;
				}
				else if (Op == EOpcode::Add || Op == EOpcode::Sub || Op == EOpcode::Mul || Op == EOpcode::Div || Op == EOpcode::Mod || Op == EOpcode::BitAnd || Op == EOpcode::BitOr || Op == EOpcode::BitXor)
				{
					if (Depth < 2) return false;
					--Depth;
				}
				Offset += Size;
			}
			return false;
		}

		bool IsBoxedServiceForwarder(const FBytecodeFunction& Function)
		{
			if (Function.bIsMethod || Function.bReturnsRef || !Function.ExceptionRegions.empty() || Function.Code.empty() || Function.Code.size() > 128u)
			{
				return false;
			}
			bool Called = false;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (Size == 0u || Size > Function.Code.size() - Ip) return false;
				if (Op == EOpcode::Return) return Called && Ip + Size == Function.Code.size();
				if (Called) return false;
				if (Op == EOpcode::CallBuiltin || Op == EOpcode::CallNative)
				{
					Called = true;
				}
				else if (Op != EOpcode::LoadLocal && Op != EOpcode::ConstI32 && Op != EOpcode::ConstF64 && Op != EOpcode::ConstBool && Op != EOpcode::ConstStr && Op != EOpcode::ConstNull)
				{
					return false;
				}
				Ip += Size;
			}
			return false;
		}

		bool HasUnloweredMethodCallWithoutContainerWork(const FBytecodeFunction& Function)
		{
			if (!Function.bIsMethod) return false;
			bool Called = false;
			bool HasMethodCall = false;
			bool HasIndirectCall = false;
			bool HasCheckedBuiltin = false;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (Size == 0u || Size > Function.Code.size() - Ip) return false;
				if (Op == EOpcode::NewDynamicArray || Op == EOpcode::NewMap || Op == EOpcode::NewSet || Op == EOpcode::GetIndexCopy || Op == EOpcode::SetIndex)
				{
					return false;
				}
				HasMethodCall |= Op == EOpcode::CallMethod;
				HasIndirectCall |= Op == EOpcode::CallIndirect;
				Called |= Op == EOpcode::Call || Op == EOpcode::CallBoundMethod || Op == EOpcode::CallIndirect;
				if (Op == EOpcode::CallBuiltin)
				{
					const auto Builtin = static_cast<EBuiltinId>(Function.Code[Ip + 1u]);
					HasCheckedBuiltin |= Builtin == EBuiltinId::WeakLock || Builtin == EBuiltinId::Assert;
					Called |= HasCheckedBuiltin;
				}
				Ip += Size;
			}
			// Optional/callable wrappers have no scalar lowering for their dynamic
			// invocation. Keep independent container-only helper kernels eligible.
			return Called && (!HasMethodCall || HasIndirectCall || HasCheckedBuiltin);
		}

		bool ShouldPreferVmForComplexOwnedLoop(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			std::size_t BackEdges = 0;
			std::size_t ConditionalBranches = 0;
			std::size_t ContainerCreations = 0;
			std::size_t UnloweredContainerCreations = 0;
			std::size_t IndirectCalls = 0;
			std::size_t DynamicArrayCreations = 0;
			bool bHasStringArray = false;
			std::size_t MapCreations = 0;
			std::size_t SetCreations = 0;
			std::size_t ObjectCreations = 0;
			std::size_t FieldOperations = 0;
			std::size_t IncrementFieldOperations = 0;
			std::size_t MethodCalls = 0;
			std::size_t ModOperations = 0;
			std::size_t EqualityOperations = 0;
			std::size_t BitwiseOrShiftOperations = 0;
			std::size_t StringConstants = 0;
			std::vector<std::size_t> StringConstantOffsets;
			std::vector<std::size_t> StringConversionOffsets;
			std::vector<std::size_t> ManagedBuiltinOffsets;
			std::size_t FirstObjectCreationIp = std::numeric_limits<std::size_t>::max();
			std::size_t EarliestBackEdgeTarget = std::numeric_limits<std::size_t>::max();
			std::size_t LastMethodCallIp = 0;
			bool bHasCheckedAffineI32StateUpdate = false;
			std::vector<std::size_t> IndexedReadOffsets;
			std::vector<std::size_t> FieldOperationOffsets;
			std::vector<std::size_t> MethodCallOffsets;
			std::vector<std::size_t> DynamicMethodCallOffsets;
			std::vector<std::pair<std::size_t, std::size_t>> BackwardLoopRanges;
			const std::vector<std::uint8_t>& Code = Function.Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Code.size() - Ip)
				{
					return false;
				}
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse)
				{
					if (Op == EOpcode::JmpIfFalse)
					{
						++ConditionalBranches;
					}
					std::size_t Target = 0;
					if (Size >= 5 && IsJumpTargetInRange(Ip + Size, Bytecode::ReadI32LE(Code.data() + Ip + 1), Code.size(), Target) && Target < Ip)
					{
						++BackEdges;
						EarliestBackEdgeTarget = std::min(EarliestBackEdgeTarget, Target);
						BackwardLoopRanges.emplace_back(Target, Ip);
					}
				}
				else if (Op == EOpcode::JmpIfLocalGeImm)
				{
					++ConditionalBranches;
					std::size_t Target = 0;
					if (Size >= 11 && IsJumpTargetInRange(Ip + Size, Bytecode::ReadI32LE(Code.data() + Ip + 7), Code.size(), Target) && Target < Ip)
					{
						++BackEdges;
						EarliestBackEdgeTarget = std::min(EarliestBackEdgeTarget, Target);
						BackwardLoopRanges.emplace_back(Target, Ip);
					}
				}

				switch (Op)
				{
				case EOpcode::NewDynamicArray:
					{
						++DynamicArrayCreations;
						++ContainerCreations;
						const auto TypeIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						std::string ElementType;
						if (TypeIndex < Module.StringPool.size() && JitHost::Services().ParseDynamicArrayElementType(Module.StringPool[TypeIndex], ElementType))
						{
							bHasStringArray |= ElementType == "string" || ElementType == "wstring";
						}
						break;
					}
				case EOpcode::NewNativeArray:
				case EOpcode::NewQueue:
				case EOpcode::NewStack:
				case EOpcode::NewOrderedMap:
				case EOpcode::NewDeque:
				case EOpcode::NewChannel:
					{
						++ContainerCreations;
						++UnloweredContainerCreations;
						break;
					}
				case EOpcode::CallIndirect:
					{
						++IndirectCalls;
						break;
					}
				case EOpcode::NewMap:
					{
						++MapCreations;
						++ContainerCreations;
						break;
					}
				case EOpcode::NewSet:
					{
						++SetCreations;
						++ContainerCreations;
						break;
					}
				case EOpcode::NewObject:
				case EOpcode::NewUninitializedObject:
					{
						++ObjectCreations;
						FirstObjectCreationIp = std::min(FirstObjectCreationIp, Ip);
						break;
					}
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::StoreField:
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
				case EOpcode::IncField:
					{
						++FieldOperations;
						FieldOperationOffsets.push_back(Ip);
						if (Op == EOpcode::IncField)
						{
							++IncrementFieldOperations;
						}
						break;
					}
				case EOpcode::CallMethod:
					{
						++MethodCalls;
						MethodCallOffsets.push_back(Ip);
						DynamicMethodCallOffsets.push_back(Ip);
						LastMethodCallIp = Ip;
						break;
					}
				case EOpcode::CallBoundMethod:
					{
						++MethodCalls;
						MethodCallOffsets.push_back(Ip);
						LastMethodCallIp = Ip;
						break;
					}
				case EOpcode::GetIndexCopy:
					{
						IndexedReadOffsets.push_back(Ip);
						break;
					}
				case EOpcode::Mod:
					{
						++ModOperations;
						break;
					}
				case EOpcode::Eq:
				case EOpcode::Ne:
					{
						++EqualityOperations;
						break;
					}
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::BitNot:
				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						++BitwiseOrShiftOperations;
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (static_cast<EBuiltinId>(Code[Ip + 1u]) == EBuiltinId::WeakLock)
						{
							ManagedBuiltinOffsets.push_back(Ip);
						}
						if (static_cast<EBuiltinId>(Code[Ip + 1u]) == EBuiltinId::ToString)
						{
							StringConversionOffsets.push_back(Ip);
						}
						break;
					}
				case EOpcode::ConstStr:
					{
						++StringConstants;
						StringConstantOffsets.push_back(Ip);
						break;
					}
				default:
					{
						break;
					}
				}
				// Checked signed affine state update emitted for
				// `state = (state * a + b) % m`. The exact local slot is deliberately
				// unconstrained here; the runtime matcher validates aliases and values.
				if (Op == EOpcode::LoadLocal && Ip + 18u <= Code.size() && static_cast<EOpcode>(Code[Ip + 3u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 6u]) == EOpcode::Mul && static_cast<EOpcode>(Code[Ip + 7u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 10u]) == EOpcode::Add && static_cast<EOpcode>(Code[Ip + 11u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 14u]) == EOpcode::Mod && static_cast<EOpcode>(Code[Ip + 15u]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + Ip + 1u) == Bytecode::ReadU16LE(Code.data() + Ip + 16u))
				{
					bHasCheckedAffineI32StateUpdate = true;
				}
				Ip += Size;
			}

			if (BackEdges == 0)
			{
				return false;
			}
			if (std::any_of(BackwardLoopRanges.begin(), BackwardLoopRanges.end(), [&](const auto& Range)
			{
				return std::any_of(ManagedBuiltinOffsets.begin(), ManagedBuiltinOffsets.end(), [&](const auto Offset)
				{
					return Offset >= Range.first && Offset <= Range.second;
				});
			}))
			{
				return true;
			}
			// Complete profitable typed plans are filtered by the caller. An unfused
			// loop calling one receiver constructed before the loop otherwise pays boxed
			// Linear dispatch at every iteration; scalar-loop fallback excludes owned bodies.
			if (ObjectCreations == 1 && ContainerCreations == 0 && FirstObjectCreationIp < EarliestBackEdgeTarget && std::any_of(BackwardLoopRanges.begin(), BackwardLoopRanges.end(), [&MethodCallOffsets](const auto& Range)
			{
				const auto Call = std::lower_bound(
				    MethodCallOffsets.begin(), MethodCallOffsets.end(), Range.first);
				return Call != MethodCallOffsets.end() && *Call <= Range.second;
			}))
			{
				return true;
			}
			if ((UnloweredContainerCreations != 0 && MethodCalls != 0) || IndirectCalls != 0)
			{
				return true;
			}
			// Require text work inside a loop. An unrelated string initialized outside
			// an integer loop must not discard that loop's native execution plan.
			if (StringConstants != 0 && ObjectCreations == 0 && MapCreations == 0 && SetCreations == 0 && DynamicArrayCreations <= 1 && !HasCanonicalBorrowedStringCompareLoopShape(Function) && std::any_of(BackwardLoopRanges.begin(), BackwardLoopRanges.end(), [&](const auto& Range)
			{
				const auto InLoop = [&Range](const std::size_t Offset)
				{
					return Offset >= Range.first && Offset <= Range.second;
				};
				return std::any_of(StringConstantOffsets.begin(), StringConstantOffsets.end(), InLoop) || (bHasStringArray && std::any_of(IndexedReadOffsets.begin(), IndexedReadOffsets.end(), InLoop));
			}))
			{
				return true;
			}
			// Dynamic string-key construction cannot use the borrowed-key kernels.
			if (BackEdges != 0 && MapCreations != 0 && std::all_of(BackwardLoopRanges.begin(), BackwardLoopRanges.end(), [&](const auto& Range)
			{
				return std::any_of(StringConversionOffsets.begin(), StringConversionOffsets.end(), [&](const auto Offset)
				{
					return Offset >= Range.first && Offset <= Range.second;
				});
			}))
			{
				return true;
			}
			const auto HasIndexedReadInOperationLoop = [&BackwardLoopRanges, &IndexedReadOffsets](const auto& OperationOffsets)
			{
				return std::any_of(
				    BackwardLoopRanges.begin(), BackwardLoopRanges.end(),
				    [&IndexedReadOffsets, &OperationOffsets](const auto& Range)
				{
					const auto IsInRange = [&Range](const std::size_t Offset)
					{
						return Offset >= Range.first && Offset <= Range.second;
					};
					return std::any_of(
					           IndexedReadOffsets.begin(), IndexedReadOffsets.end(), IsInRange) &&
					    std::any_of(
					        OperationOffsets.begin(), OperationOffsets.end(), IsInRange);
				});
			};
			const bool bHasIndexedReadInFieldLoop =
			    HasIndexedReadInOperationLoop(FieldOperationOffsets);
			const bool bHasIndexedReadInMethodLoop =
			    HasIndexedReadInOperationLoop(MethodCallOffsets);
			const bool bHasIndexedReadInDynamicMethodLoop =
			    HasIndexedReadInOperationLoop(DynamicMethodCallOffsets);
			// Raw int->double map storage alone is not enough to make a general
			// read/modify/write loop profitable: every double value still crosses the
			// boxed Linear arithmetic bridge. Keep the verified affine constant-delta
			// kernel enabled, but route all other such loops to the bytecode VM until
			// the typed executor can carry F64 values end-to-end. This decision is made
			// before execution and therefore cannot partially mutate the frame.
			if (HasGeneralF64MapReadModifyWrite(Module, Function) && !HasCanonicalTypedAffineMapValueUpdate(Module, Function))
			{
				return true;
			}
			// Multiple construction/traversal loops over multiple containers are the
			// characteristic indirect-index/key path. One-container affine loops and
			// one-loop string Set tables remain eligible for their fused reductions.
			if (BackEdges >= 2 && ContainerCreations >= 2)
			{
				return true;
			}
			// A single frame-local container is not automatically profitable in Linear.
			// The existing fused kernels cover affine/canonical counters, but an indirect
			// key derived through a general bitwise/shift expression otherwise crosses
			// the FValue/container bridge for every read and write. Until that loop has a
			// complete typed IR, the bytecode VM is consistently cheaper. String-key
			// tables are excluded: their borrowed-key Linear probes already outperform VM.
			const bool bHasNumericContainer =
			    DynamicArrayCreations != 0 || MapCreations != 0 || SetCreations != 0;
			if (BackEdges >= 2 && bHasNumericContainer && BitwiseOrShiftOperations >= 3 && StringConstants == 0)
			{
				return true;
			}
			// Branch-selected fields defeat the consecutive-field transformer and pay
			// a shape/PIC bridge at each selected arm.
			// An else-if chain is commonly lowered with shared jump tails, so four
			// bytecode branches already represent a materially polymorphic field site.
			// Complete typed-loop coverage is filtered by the caller. An indirect selector
			// read is a concrete sign that the direct field transformer cannot collapse the
			// branch chain, even if the surrounding state update looks canonical.
			if (FieldOperations != 0 && ConditionalBranches >= 4)
			{
				const bool bStableStateDrivenFieldShape =
				    !bHasIndexedReadInFieldLoop && bHasCheckedAffineI32StateUpdate && IncrementFieldOperations == 0 && ObjectCreations == 1 && ModOperations >= 2 && (MethodCalls == 0 || LastMethodCallIp < EarliestBackEdgeTarget) && FirstObjectCreationIp < EarliestBackEdgeTarget;
				if (!bStableStateDrivenFieldShape)
				{
					return true;
				}
			}
			// A polymorphic receiver selected through a loop-local indexed lookup is
			// materially different from the direct state selector handled by the typed
			// virtual transformer. Without complete profitable typed-loop coverage, each
			// iteration otherwise crosses the boxed Linear method bridge. Defer the
			// module-wide target check until the cheap loop-shape filters pass so ordinary
			// CallMethod bodies do not pay this analysis cost.
			if (bHasIndexedReadInDynamicMethodLoop && ConditionalBranches >= 4)
			{
				const auto IsIndexedPolymorphicCall = [&BackwardLoopRanges, &IndexedReadOffsets, &Code, &Module](
				                                          const std::size_t CallIp)
				{
					const bool bSharesIndexedLoop = std::any_of(
					    BackwardLoopRanges.begin(), BackwardLoopRanges.end(),
					    [&IndexedReadOffsets, CallIp](const auto& Range)
					{
						return CallIp >= Range.first && CallIp <= Range.second && std::any_of(IndexedReadOffsets.begin(), IndexedReadOffsets.end(), [&Range](const std::size_t ReadIp)
						{
							return ReadIp >= Range.first && ReadIp <= Range.second;
						});
					});
					if (!bSharesIndexedLoop)
					{
						return false;
					}
					const std::uint16_t MethodNameIndex =
					    Bytecode::ReadU16LE(Code.data() + CallIp + 1u);
					if (MethodNameIndex >= Module.StringPool.size())
					{
						return false;
					}
					const std::uint8_t ArgumentCount = Code[CallIp + 3u];
					const std::string_view MethodName =
					    Module.StringPool[MethodNameIndex];
					std::size_t ConcreteVirtualTargets = 0;
					for (const FBytecodeFunction& Candidate : Module.Functions)
					{
						if (Candidate.bIsMethod && Candidate.bIsVirtual && !Candidate.bIsAbstract && !Candidate.bDeleted && !Candidate.Code.empty() && Candidate.ParamCount == ArgumentCount && MethodLinkageLeafMatches(Candidate.Name, MethodName) && ++ConcreteVirtualTargets >= 2)
						{
							return true;
						}
					}
					return false;
				};
				if (std::any_of(DynamicMethodCallOffsets.begin(),
				        DynamicMethodCallOffsets.end(), IsIndexedPolymorphicCall))
				{
					return true;
				}
			}
			// Receivers created and selected within the same loop-heavy function do not
			// benefit from the parameter-based polymorphic PIC chain when the receiver is
			// first selected through an unplanned container lookup. Keep direct bounded
			// selector chains eligible for Linear's existing direct polymorphic transform.
			if (ObjectCreations >= 2 && MethodCalls != 0)
			{
				const bool bStateDrivenTransformShape =
				    !bHasIndexedReadInMethodLoop && ObjectCreations <= 8 && bHasCheckedAffineI32StateUpdate && ConditionalBranches >= 2 && FirstObjectCreationIp < EarliestBackEdgeTarget;
				if (!bStateDrivenTransformShape)
				{
					return true;
				}
			}
			const bool bHasScalarReplaceableAssignedObject =
			    HasAssignedObjectProjectionScalarReplacementShape(Function);
			// A loop retaining newly allocated objects in a local array does not
			// benefit from Linear's scalar projection elimination, even if the
			// function returns that array or its length without a second loop.
			// In the absence of complete profitable typed coverage (checked by the
			// caller), construction retains boxed ownership/container bridges. Choose
			// the authoritative VM frame independently of arithmetic spellings in
			// field initializers. This is an execution choice, not an escape proof;
			// nested callees remain independently eligible for native dispatch.
			if (DynamicArrayCreations != 0 && ObjectCreations == 1 && FirstObjectCreationIp >= EarliestBackEdgeTarget && !bHasScalarReplaceableAssignedObject)
			{
				return true;
			}
			// Construction after the earliest loop header means a fresh script object is
			// allocated on each iteration. Unless the conservative projection matcher can
			// remove that allocation completely, Linear adds ownership/trampoline work on
			// top of the same object semantics and is slower than the VM.
			if (ObjectCreations == 1 && BitwiseOrShiftOperations >= 3 && FirstObjectCreationIp >= EarliestBackEdgeTarget && !bHasScalarReplaceableAssignedObject)
			{
				return true;
			}
			// Short-lived objects are admitted here when the Linear executor can perform a
			// conservative per-iteration escape proof before eliminating allocation.
			// Unsupported/observable shapes retain the faster ordinary VM ownership path.
			// Several independently constructed receiver objects instead describe a
			// polymorphic dispatch workload, not an allocation candidate; until its
			// complete receiver selection is fused, the VM remains the faster executor.
			if (ObjectCreations == 1 && MethodCalls != 0 && ModOperations >= 3 && FirstObjectCreationIp >= EarliestBackEdgeTarget && !bHasScalarReplaceableAssignedObject)
			{
				return true;
			}
			// Equality over two independently boxed string arrays is helper dominated;
			// scalar/string Set membership uses CallMethod and is intentionally excluded.
			if (DynamicArrayCreations >= 2 && EqualityOperations != 0 && StringConstants >= 2)
			{
				return !HasCanonicalBorrowedStringCompareLoopShape(Function);
			}
			return false;
		}

		[[maybe_unused]] bool CanSuppressNestedJitForScalarCallees(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			// The fallback flag is inherited by every VM descendant. Preserve independent
			// dispatch when a known callee can own or process typed containers, including
			// calls reached through scalar wrappers. Bound the proof for large call graphs.
			std::vector<const FBytecodeFunction*> Pending{&Function};
			std::unordered_set<const FBytecodeFunction*> Visited{&Function};
			std::unordered_set<std::uint32_t> CheckedDynamicCalls;
			std::size_t InstructionsRemaining = 16384u;
			const auto Enqueue = [&Pending, &Visited](const FBytecodeFunction* Callee)
			{
				if (Visited.insert(Callee).second)
				{
					if (Visited.size() > 128u) return false;
					Pending.push_back(Callee);
				}
				return true;
			};
			while (!Pending.empty())
			{
				const auto* const Current = Pending.back();
				Pending.pop_back();
				const bool bNested = Current != &Function;
				if (bNested && (Current->Code.size() > 65536u || JitLowering::RequiresVmOwnership(*Current)))
				{
					return false;
				}
				for (std::size_t Ip = 0; Ip < Current->Code.size();)
				{
					if (InstructionsRemaining == 0u) return false;
					--InstructionsRemaining;
					const auto Op = static_cast<EOpcode>(Current->Code[Ip]);
					const auto Size = Bytecode::InstructionSize(Op);
					if (Size == 0u || Size > Current->Code.size() - Ip) return false;
					// Runtime callables and host callbacks can reach owned script work
					// outside this static call graph, including named host reentry.
					if (Op == EOpcode::CallIndirect || Op == EOpcode::CallNative) return false;
					if (bNested && (Op == EOpcode::GetIndexCopy || Op == EOpcode::SetIndex || Op == EOpcode::CallMethod || Op == EOpcode::ConstStr))
					{
						return false;
					}
					if (Op == EOpcode::Call || Op == EOpcode::CallBoundMethod)
					{
						const auto CalleeIndex = Bytecode::ReadU16LE(Current->Code.data() + Ip + 1u);
						if (CalleeIndex >= Module.Functions.size()) return false;
						const auto* const Callee = &Module.Functions[CalleeIndex];
						if (Callee->bIsVirtual) return false;
						if (!Enqueue(Callee)) return false;
					}
					else if (Op == EOpcode::CallMethod)
					{
						const auto NameIndex = Bytecode::ReadU16LE(Current->Code.data() + Ip + 1u);
						if (NameIndex >= Module.StringPool.size()) return false;
						const auto ArgumentCount = Current->Code[Ip + 3u];
						const std::uint32_t CallKey = (std::uint32_t{NameIndex} << 8u) | ArgumentCount;
						if (CheckedDynamicCalls.insert(CallKey).second)
						{
							// Interface calls encode a method leaf, not a function index. Every
							// matching script implementation must satisfy the same scalar proof.
							for (const auto& Candidate : Module.Functions)
							{
								if (InstructionsRemaining == 0u) return false;
								--InstructionsRemaining;
								if (Candidate.bIsMethod && !Candidate.bIsStaticMethod && !Candidate.bIsAbstract && !Candidate.bDeleted && !Candidate.Code.empty() && Candidate.ParamCount == ArgumentCount && MethodLinkageLeafMatches(Candidate.Name, Module.StringPool[NameIndex]) && !Enqueue(&Candidate))
								{
									return false;
								}
							}
						}
					}
					Ip += Size;
				}
			}
			return true;
		}

		bool IsLocalScalarLinearOpcode(EOpcode Op)
		{
			switch (Op)
			{
			case EOpcode::Nop:
			case EOpcode::ConstI32:
			case EOpcode::ConstF64:
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
				return true;
			default:
				return false;
			}
		}

		struct FLinearLoopSummary
		{
			bool bHasLoop = false;
			bool bAllLoopsUseLocalScalarOpcodes = true;
			bool bHasTextArithmeticLoop = false;
		};

		FLinearLoopSummary SummarizeLinearLoops(
		    const FBytecodeFunction& Function,
		    const std::vector<std::size_t>& TextAddOffsets)
		{
			// Classify the back-edge ranges independently of boxed setup and
			// teardown. Neither makes the local arithmetic loop profitable.
			const auto& Code = Function.Code;
			std::optional<std::size_t> LastNonScalar;
			FLinearLoopSummary Summary;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Code[Ip]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Code.size() - Ip) return {};
				if (Summary.bAllLoopsUseLocalScalarOpcodes && !IsLocalScalarLinearOpcode(Op))
				{
					LastNonScalar = Ip;
				}
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse || Op == EOpcode::JmpIfLocalGeImm)
				{
					const std::size_t RelativeOffset = Op == EOpcode::JmpIfLocalGeImm ? 7u : 1u;
					std::size_t Target = 0;
					if (IsJumpTargetInRange(Ip + Size, Bytecode::ReadI32LE(Code.data() + Ip + RelativeOffset), Code.size(), Target) && Target < Ip)
					{
						Summary.bHasLoop = true;
						Summary.bAllLoopsUseLocalScalarOpcodes &= !LastNonScalar || *LastNonScalar < Target;
						Summary.bHasTextArithmeticLoop = Summary.bHasTextArithmeticLoop || std::any_of(
						    TextAddOffsets.begin(), TextAddOffsets.end(),
						    [Target, Ip](const std::size_t Offset)
						    {
							    return Offset >= Target && Offset <= Ip;
						    });
					}
				}
				Ip += Size;
			}
			return Summary;
		}

		bool IsScalarNativeLinearCandidate(
		    const FBytecodeFunction& Function,
		    std::vector<std::uint32_t>& OutGuards)
		{
			OutGuards.clear();
			bool bSawNative = false;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				switch (Op)
				{
				case EOpcode::ConstStr:
					{
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (Function.Code[Ip + 2u] != 1u)
						{
							return false;
						}
						const EBuiltinId Builtin = static_cast<EBuiltinId>(
						    Function.Code[Ip + 1u]);
						// Widening float conversions are total. Checked integer/narrowing
						// conversions are deliberately not duplicated here: a failing
						// inlined builtin must retain the callee's diagnostic identity and
						// exact source position, which the scalar call boundary already does.
						if (Builtin != EBuiltinId::ToFloat && Builtin != EBuiltinId::ToDouble)
						{
							return false;
						}
						break;
					}
				case EOpcode::CallNative:
					{
						if (Ip + Bytecode::InstructionSize(Op) > Function.Code.size())
						{
							OutGuards.clear();
							return false;
						}
						bSawNative = true;
						OutGuards.push_back(
						    (static_cast<std::uint32_t>(Bytecode::ReadU16LE(
						         Function.Code.data() + Ip + 1))
						        << 8u) |
						    Function.Code[Ip + 3]);
						break;
					}
				default:
					// Globals, fields, and other owned operations can introduce
					// values that the native scalar call boundary cannot carry.
					{
						if (!IsLocalScalarLinearOpcode(Op))
						{
							OutGuards.clear();
							return false;
						}
						break;
					}
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					OutGuards.clear();
					return false;
				}
				Ip += Size;
			}
			return bSawNative && !OutGuards.empty();
		}

		bool IsOwnershipAwareLinearCandidate(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			bool bSawSupportedOwner = false;
			bool bSawCaptureFreeClosure = false;
			bool bSawIndirectCall = false;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				switch (Op)
				{
				case EOpcode::NewObject:
				case EOpcode::NewUninitializedObject:
				case EOpcode::NewDynamicArray:
				case EOpcode::NewMap:
				case EOpcode::NewSet:
				case EOpcode::NewQueue:
				case EOpcode::NewStack:
				case EOpcode::NewOrderedMap:
				case EOpcode::NewDeque:
				case EOpcode::NewChannel:
					{
						bSawSupportedOwner = true;
						break;
					}
				case EOpcode::MakeClosure:
					{
						const std::size_t Size = Bytecode::InstructionSize(Op);
						if (Size == 0 || Ip + Size > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t Target = Bytecode::ReadU16LE(
						    Function.Code.data() + Ip + 1);
						const std::uint8_t CaptureCount = Function.Code[Ip + 3];
						if (Target >= Module.Functions.size() || CaptureCount != 0 || !Module.Functions[Target].Captures.empty())
						{
							// Captures can promote frame-local references into cells. Keep
							// those closures on the authoritative VM until escape ownership
							// is represented in the JIT plan. A capture-free closure owns no
							// frame storage and is safe in the FValue Linear executor.
							return false;
						}
						bSawSupportedOwner = true;
						bSawCaptureFreeClosure = true;
						break;
					}
				case EOpcode::CallIndirect:
					// The Linear executor retains the callable and arguments across
					// the nested call. Admission still requires a capture-free
					// MakeClosure in this body through bSawSupportedOwner.
					{
						bSawIndirectCall = true;
						break;
					}
				case EOpcode::NewNativeArray:
				case EOpcode::CloneStruct:
				case EOpcode::MakeLocalRef:
				case EOpcode::MakeGlobalRef:
				case EOpcode::MakeThisFieldRef:
				case EOpcode::MakeFieldRef:
				case EOpcode::GetIndexRef:
				case EOpcode::GetIndexPathRef:
				case EOpcode::GetMapEntryRef:
				case EOpcode::BindLocal:
				case EOpcode::DerefRef:
				case EOpcode::CheckedCast:
				case EOpcode::TryCast:
					// Native arrays have value ownership, while refs/closures can
					// outlive the current frame. Keep those on the interpreter until
					// their alias/capture ownership is represented in the JIT IR.
					return false;
				default:
					{
						break;
					}
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					return false;
				}
				Ip += Size;
			}
			// Do not let an unused closure literal opt an otherwise unrelated body
			// into Linear execution. Besides keeping the ownership proof local, this
			// preserves VM stack-governance semantics for functions whose actual call
			// path may clear the complete operand stack (for example exit()).
			return bSawSupportedOwner && (!bSawCaptureFreeClosure || bSawIndirectCall);
		}

		bool IsFusibleI32CatchAccumulatorLoop(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			if (Function.ExceptionRegions.size() != 1)
			{
				return false;
			}
			const FBytecodeExceptionRegion& Region = Function.ExceptionRegions.front();
			if (Region.FinallyOffset != std::numeric_limits<std::uint32_t>::max() || Region.Catches.size() != 1 || (Region.Catches.front().TypeName != "int" && Region.Catches.front().TypeName != "int32"))
			{
				return false;
			}
			const auto& Code = Function.Code;
			const std::size_t CodeSize = Code.size();
			constexpr std::size_t LoadSize = 3;
			constexpr std::size_t StoreSize = 3;
			constexpr std::size_t JmpSize = 5;
			if (Region.BeginOffset > CodeSize || LoadSize + 1u > CodeSize - Region.BeginOffset || static_cast<EOpcode>(Code[Region.BeginOffset]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[Region.BeginOffset + LoadSize]) != EOpcode::Throw)
			{
				return false;
			}
			const std::uint16_t CounterSlot =
			    Bytecode::ReadU16LE(Code.data() + Region.BeginOffset + 1);
			const FBytecodeCatchHandler& Handler = Region.Catches.front();
			std::size_t Ip = Handler.HandlerOffset;
			constexpr std::size_t HandlerPrefixSize = LoadSize * 2u + 1u + StoreSize;
			if (Ip > CodeSize || HandlerPrefixSize + JmpSize > CodeSize - Ip || static_cast<EOpcode>(Code[Ip]) != EOpcode::LoadLocal)
			{
				return false;
			}
			const std::uint16_t AccumulatorSlot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (AccumulatorSlot == CounterSlot || static_cast<EOpcode>(Code[Ip + 3]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + Ip + 4) != Handler.LocalSlot || static_cast<EOpcode>(Code[Ip + 6]) != EOpcode::Add || static_cast<EOpcode>(Code[Ip + 7]) != EOpcode::StoreLocal || Bytecode::ReadU16LE(Code.data() + Ip + 8) != AccumulatorSlot)
			{
				return false;
			}
			Ip += HandlerPrefixSize;
			if (LoadSize + 1u <= CodeSize - Ip && static_cast<EOpcode>(Code[Ip]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + Ip + 1) == AccumulatorSlot && static_cast<EOpcode>(Code[Ip + LoadSize]) == EOpcode::Pop)
			{
				Ip += LoadSize + 1u;
			}
			if (JmpSize > CodeSize - Ip || static_cast<EOpcode>(Code[Ip]) != EOpcode::Jmp)
			{
				return false;
			}
			std::size_t TailStart = 0;
			if (!IsJumpTargetInRange(
			        Ip + JmpSize, Bytecode::ReadI32LE(Code.data() + Ip + 1),
			        CodeSize, TailStart))
			{
				return false;
			}

			std::size_t BackJumpIp = 0;
			if (3u + JmpSize <= CodeSize - TailStart && static_cast<EOpcode>(Code[TailStart]) == EOpcode::IncLocal && Bytecode::ReadU16LE(Code.data() + TailStart + 1) == CounterSlot && static_cast<EOpcode>(Code[TailStart + 3]) == EOpcode::Jmp)
			{
				BackJumpIp = TailStart + 3;
			}
			else
			{
				constexpr std::size_t VerboseTailSize = 25;
				if (VerboseTailSize > CodeSize - TailStart || static_cast<EOpcode>(Code[TailStart]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + TailStart + 1) != CounterSlot || static_cast<EOpcode>(Code[TailStart + 3]) != EOpcode::StoreLocal)
				{
					return false;
				}
				const std::uint16_t TemporarySlot = Bytecode::ReadU16LE(Code.data() + TailStart + 4);
				const std::uint16_t ConstantIndex = Bytecode::ReadU16LE(Code.data() + TailStart + 10);
				if (TemporarySlot == CounterSlot || static_cast<EOpcode>(Code[TailStart + 6]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + TailStart + 7) != TemporarySlot || static_cast<EOpcode>(Code[TailStart + 9]) != EOpcode::ConstI32 || ConstantIndex >= Module.Constants.size() || Module.Constants[ConstantIndex].Kind != EConstantKind::I32 || Module.Constants[ConstantIndex].I32 != 1 || static_cast<EOpcode>(Code[TailStart + 12]) != EOpcode::Add || static_cast<EOpcode>(Code[TailStart + 13]) != EOpcode::StoreLocal || Bytecode::ReadU16LE(Code.data() + TailStart + 14) != CounterSlot || static_cast<EOpcode>(Code[TailStart + 16]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + TailStart + 17) != TemporarySlot || static_cast<EOpcode>(Code[TailStart + 19]) != EOpcode::Pop || static_cast<EOpcode>(Code[TailStart + 20]) != EOpcode::Jmp)
				{
					return false;
				}
				BackJumpIp = TailStart + 20;
			}
			const std::size_t BackJumpEnd = BackJumpIp + JmpSize;
			std::size_t HeaderIp = 0;
			if (!IsJumpTargetInRange(
			        BackJumpEnd, Bytecode::ReadI32LE(Code.data() + BackJumpIp + 1),
			        CodeSize, HeaderIp))
			{
				return false;
			}
			std::size_t ExitIp = 0;
			if (11u <= CodeSize - HeaderIp && static_cast<EOpcode>(Code[HeaderIp]) == EOpcode::JmpIfLocalGeImm && Bytecode::ReadU16LE(Code.data() + HeaderIp + 1) == CounterSlot && HeaderIp + 11u == Region.BeginOffset && IsJumpTargetInRange(HeaderIp + 11u, Bytecode::ReadI32LE(Code.data() + HeaderIp + 7), CodeSize, ExitIp))
			{
				return ExitIp == BackJumpEnd;
			}
			constexpr std::size_t HeaderSize = 12;
			if (HeaderSize > CodeSize - HeaderIp || static_cast<EOpcode>(Code[HeaderIp]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + HeaderIp + 1) != CounterSlot || (static_cast<EOpcode>(Code[HeaderIp + 3]) != EOpcode::LoadLocal && static_cast<EOpcode>(Code[HeaderIp + 3]) != EOpcode::ConstI32) || static_cast<EOpcode>(Code[HeaderIp + 6]) != EOpcode::Lt || static_cast<EOpcode>(Code[HeaderIp + 7]) != EOpcode::JmpIfFalse || HeaderIp + HeaderSize != Region.BeginOffset || !IsJumpTargetInRange(HeaderIp + HeaderSize, Bytecode::ReadI32LE(Code.data() + HeaderIp + 8), CodeSize, ExitIp))
			{
				return false;
			}
			return ExitIp == BackJumpEnd;
		}

		bool HasFusibleU32AffineRecurrenceLoop(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			const auto& Code = Function.Code;
			const auto IsCanonicalTail = [&](
			                                 const std::size_t BodyIp,
			                                 const std::size_t TailIp) -> bool
			{
				if (TailIp >= Code.size())
				{
					return false;
				}
				std::uint16_t CounterSlot = 0xFFFF;
				std::size_t BackJumpIp = 0;
				if (TailIp + 8u <= Code.size() && static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal && static_cast<EOpcode>(Code[TailIp + 3u]) == EOpcode::Jmp)
				{
					CounterSlot = Bytecode::ReadU16LE(Code.data() + TailIp + 1u);
					BackJumpIp = TailIp + 3u;
				}
				else
				{
					constexpr std::size_t VerboseTailSize = 25u;
					if (TailIp + VerboseTailSize > Code.size() || static_cast<EOpcode>(Code[TailIp]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[TailIp + 3u]) != EOpcode::StoreLocal || static_cast<EOpcode>(Code[TailIp + 6u]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[TailIp + 9u]) != EOpcode::ConstI32 || static_cast<EOpcode>(Code[TailIp + 12u]) != EOpcode::Add || static_cast<EOpcode>(Code[TailIp + 13u]) != EOpcode::StoreLocal || static_cast<EOpcode>(Code[TailIp + 16u]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[TailIp + 19u]) != EOpcode::Pop || static_cast<EOpcode>(Code[TailIp + 20u]) != EOpcode::Jmp)
					{
						return false;
					}
					CounterSlot = Bytecode::ReadU16LE(Code.data() + TailIp + 1u);
					const std::uint16_t TemporarySlot =
					    Bytecode::ReadU16LE(Code.data() + TailIp + 4u);
					const std::uint16_t OneIndex =
					    Bytecode::ReadU16LE(Code.data() + TailIp + 10u);
					if (TemporarySlot == CounterSlot || Bytecode::ReadU16LE(Code.data() + TailIp + 7u) != TemporarySlot || Bytecode::ReadU16LE(Code.data() + TailIp + 14u) != CounterSlot || Bytecode::ReadU16LE(Code.data() + TailIp + 17u) != TemporarySlot || OneIndex >= Module.Constants.size() || Module.Constants[OneIndex].Kind != EConstantKind::I32 || Module.Constants[OneIndex].I32 != 1)
					{
						return false;
					}
					BackJumpIp = TailIp + 20u;
				}
				std::size_t HeaderIp = 0;
				if (!IsJumpTargetInRange(
				        BackJumpIp + Bytecode::InstructionSize(EOpcode::Jmp),
				        Bytecode::ReadI32LE(Code.data() + BackJumpIp + 1u),
				        Code.size(), HeaderIp))
				{
					return false;
				}
				if (HeaderIp + Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm) <= Code.size() && static_cast<EOpcode>(Code[HeaderIp]) == EOpcode::JmpIfLocalGeImm && Bytecode::ReadU16LE(Code.data() + HeaderIp + 1u) == CounterSlot && HeaderIp + Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm) == BodyIp)
				{
					return true;
				}
				constexpr std::size_t HeaderSize = 12u;
				return HeaderIp + HeaderSize <= Code.size() && static_cast<EOpcode>(Code[HeaderIp]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + HeaderIp + 1u) == CounterSlot && (static_cast<EOpcode>(Code[HeaderIp + 3u]) == EOpcode::LoadLocal || static_cast<EOpcode>(Code[HeaderIp + 3u]) == EOpcode::ConstI32) && static_cast<EOpcode>(Code[HeaderIp + 6u]) == EOpcode::Lt && static_cast<EOpcode>(Code[HeaderIp + 7u]) == EOpcode::JmpIfFalse && HeaderIp + HeaderSize == BodyIp;
			};

			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				constexpr std::size_t RecurrenceSize = 14u;
				if (Ip > Code.size() || RecurrenceSize > Code.size() - Ip || static_cast<EOpcode>(Code[Ip]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[Ip + 3u]) != EOpcode::ConstI32 || static_cast<EOpcode>(Code[Ip + 6u]) != EOpcode::Mul || static_cast<EOpcode>(Code[Ip + 7u]) != EOpcode::ConstI32 || static_cast<EOpcode>(Code[Ip + 10u]) != EOpcode::Add || static_cast<EOpcode>(Code[Ip + 11u]) != EOpcode::StoreLocal || Bytecode::ReadU16LE(Code.data() + Ip + 1u) != Bytecode::ReadU16LE(Code.data() + Ip + 12u))
				{
					const std::size_t Size = Bytecode::InstructionSize(
					    static_cast<EOpcode>(Code[Ip]));
					if (Size == 0 || Size > Code.size() - Ip)
					{
						return false;
					}
					Ip += Size;
					continue;
				}
				const std::uint16_t Multiplier =
				    Bytecode::ReadU16LE(Code.data() + Ip + 4u);
				const std::uint16_t Increment =
				    Bytecode::ReadU16LE(Code.data() + Ip + 8u);
				if (Multiplier >= Module.Constants.size() || Increment >= Module.Constants.size() || Module.Constants[Multiplier].Kind != EConstantKind::U32 || Module.Constants[Increment].Kind != EConstantKind::U32)
				{
					Ip += Bytecode::InstructionSize(static_cast<EOpcode>(Code[Ip]));
					continue;
				}
				std::size_t Cursor = Ip + RecurrenceSize;
				if (Cursor + 4u <= Code.size() && static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + Cursor + 1u) == Bytecode::ReadU16LE(Code.data() + Ip + 1u) && static_cast<EOpcode>(Code[Cursor + 3u]) == EOpcode::Pop)
				{
					Cursor += 4u;
				}
				std::size_t Tail = Cursor;
				if (Cursor + Bytecode::InstructionSize(EOpcode::Jmp) <= Code.size() && static_cast<EOpcode>(Code[Cursor]) == EOpcode::Jmp)
				{
					if (!IsJumpTargetInRange(
					        Cursor + Bytecode::InstructionSize(EOpcode::Jmp),
					        Bytecode::ReadI32LE(Code.data() + Cursor + 1u),
					        Code.size(), Tail))
					{
						Tail = Cursor;
					}
				}
				if (IsCanonicalTail(Ip, Tail))
				{
					return true;
				}
				Ip += Bytecode::InstructionSize(static_cast<EOpcode>(Code[Ip]));
			}
			return false;
		}

		bool HasFusibleI32ModuloAccumulatorLoop(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			const auto& Code = Function.Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				constexpr std::size_t PrefixSize = 18u;
				constexpr std::size_t BodySize = 18u;
				if (Ip + PrefixSize + BodySize <= Code.size() && static_cast<EOpcode>(Code[Ip]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[Ip + 3u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 6u]) == EOpcode::Mul && static_cast<EOpcode>(Code[Ip + 7u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 10u]) == EOpcode::Add && static_cast<EOpcode>(Code[Ip + 11u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[Ip + 14u]) == EOpcode::Mod && static_cast<EOpcode>(Code[Ip + 15u]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + Ip + 1u) == Bytecode::ReadU16LE(Code.data() + Ip + 16u))
				{
					const std::size_t BodyIp = Ip + PrefixSize;
					const std::uint16_t StateSlot =
					    Bytecode::ReadU16LE(Code.data() + Ip + 1u);
					if (static_cast<EOpcode>(Code[BodyIp]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[BodyIp + 3u]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + BodyIp + 4u) == StateSlot && static_cast<EOpcode>(Code[BodyIp + 6u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[BodyIp + 9u]) == EOpcode::Mod && static_cast<EOpcode>(Code[BodyIp + 10u]) == EOpcode::Add && static_cast<EOpcode>(Code[BodyIp + 11u]) == EOpcode::ConstI32 && static_cast<EOpcode>(Code[BodyIp + 14u]) == EOpcode::Mod && static_cast<EOpcode>(Code[BodyIp + 15u]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + BodyIp + 1u) == Bytecode::ReadU16LE(Code.data() + BodyIp + 16u))
					{
						const std::uint16_t ConstantOffsets[] = {4u, 8u, 12u};
						bool bValidConstants = true;
						for (const std::uint16_t Offset : ConstantOffsets)
						{
							const std::uint16_t Index =
							    Bytecode::ReadU16LE(Code.data() + Ip + Offset);
							bValidConstants = bValidConstants && Index < Module.Constants.size() && Module.Constants[Index].Kind == EConstantKind::I32;
						}
						for (const std::uint16_t Offset : {std::uint16_t{7u}, std::uint16_t{12u}})
						{
							const std::uint16_t Index =
							    Bytecode::ReadU16LE(Code.data() + BodyIp + Offset);
							bValidConstants = bValidConstants && Index < Module.Constants.size() && Module.Constants[Index].Kind == EConstantKind::I32;
						}
						const std::size_t TailIp = BodyIp + BodySize;
						if (bValidConstants && TailIp + Bytecode::InstructionSize(EOpcode::IncLocal) + Bytecode::InstructionSize(EOpcode::Jmp) <= Code.size() && static_cast<EOpcode>(Code[TailIp]) == EOpcode::IncLocal && static_cast<EOpcode>(Code[TailIp + 3u]) == EOpcode::Jmp)
						{
							const std::uint16_t CounterSlot =
							    Bytecode::ReadU16LE(Code.data() + TailIp + 1u);
							const std::size_t BackEnd = TailIp + 3u + Bytecode::InstructionSize(EOpcode::Jmp);
							std::size_t HeaderIp = 0;
							std::size_t ExitIp = 0;
							if (IsJumpTargetInRange(
							        BackEnd, Bytecode::ReadI32LE(Code.data() + TailIp + 4u),
							        Code.size(), HeaderIp))
							{
								if (HeaderIp + Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm) <= Code.size() && static_cast<EOpcode>(Code[HeaderIp]) == EOpcode::JmpIfLocalGeImm && Bytecode::ReadU16LE(Code.data() + HeaderIp + 1u) == CounterSlot && HeaderIp + Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm) == Ip && IsJumpTargetInRange(HeaderIp + Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm), Bytecode::ReadI32LE(Code.data() + HeaderIp + 7u), Code.size(), ExitIp) && ExitIp == BackEnd)
								{
									return true;
								}
								constexpr std::size_t HeaderSize = 12u;
								if (HeaderIp + HeaderSize <= Code.size() && static_cast<EOpcode>(Code[HeaderIp]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + HeaderIp + 1u) == CounterSlot && static_cast<EOpcode>(Code[HeaderIp + 3u]) == EOpcode::LoadLocal && static_cast<EOpcode>(Code[HeaderIp + 6u]) == EOpcode::Lt && static_cast<EOpcode>(Code[HeaderIp + 7u]) == EOpcode::JmpIfFalse && HeaderIp + HeaderSize == Ip && IsJumpTargetInRange(HeaderIp + HeaderSize, Bytecode::ReadI32LE(Code.data() + HeaderIp + 8u), Code.size(), ExitIp) && ExitIp == BackEnd)
								{
									return true;
								}
							}
						}
					}
				}
				const std::size_t Size = Bytecode::InstructionSize(
				    static_cast<EOpcode>(Code[Ip]));
				if (Size == 0 || Size > Code.size() - Ip)
				{
					return false;
				}
				Ip += Size;
			}
			return false;
		}

		bool IsScalarGlobalLoad(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip)
		{
			if (Ip + Bytecode::InstructionSize(EOpcode::LoadGlobal) > Code.size())
			{
				return false;
			}
			const std::uint16_t Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (Slot >= Module.GlobalTypeNames.size())
			{
				return false;
			}
			const std::string& TypeName = Module.GlobalTypeNames[Slot];
			return JitHost::Services().IsPrimitiveValueTypeName(TypeName) && TypeName != "string" && TypeName != "wstring";
		}

		bool IsNonEscapingScalarArrayCandidate(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			if (BytecodeFunctionHasRefParams(Function))
			{
				return false;
			}
			std::vector<EStackTag> ParameterTags;
			EStackTag ReturnTag = EStackTag::Any;
			bool bVoidReturn = false;
			const auto IsScalar = [](const EStackTag Tag)
			{
				return Tag == EStackTag::I32 || Tag == EStackTag::U32 || Tag == EStackTag::I64 || Tag == EStackTag::U64 || Tag == EStackTag::F32 || Tag == EStackTag::F64 || Tag == EStackTag::Bool;
			};
			if (!ParseClosureType(
			        Function.ClosureTypeName,
			        ParameterTags,
			        ReturnTag,
			        bVoidReturn) ||
			    !std::all_of(ParameterTags.begin(), ParameterTags.end(), IsScalar) || (!bVoidReturn && !IsScalar(ReturnTag)))
			{
				return false;
			}
			const auto HasScalarCallSignature = [&](
			                                        const FBytecodeFunction& Callee,
			                                        const std::uint8_t ArgumentCount)
			{
				std::vector<EStackTag> Params;
				EStackTag Result = EStackTag::Any;
				bool bVoid = false;
				return !Callee.bIsMethod && !BytecodeFunctionHasRefParams(Callee) && Callee.ParamCount == ArgumentCount && ParseClosureType(Callee.ClosureTypeName, Params, Result, bVoid) && Callee.bReturnsVoid == bVoid && Params.size() == ArgumentCount && std::all_of(Params.begin(), Params.end(), IsScalar) && (bVoid || IsScalar(Result));
			};

			bool bOnlyStringArrays = true;
			bool bSawStringArray = false;
			for (std::size_t ScanIp = 0; ScanIp < Function.Code.size();)
			{
				const EOpcode ScanOp = static_cast<EOpcode>(Function.Code[ScanIp]);
				const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
				if (ScanSize == 0 || ScanIp + ScanSize > Function.Code.size())
				{
					return false;
				}
				if (ScanOp == EOpcode::NewDynamicArray)
				{
					const std::uint16_t TypeIndex =
					    Bytecode::ReadU16LE(Function.Code.data() + ScanIp + 1);
					std::string ElementType;
					if (TypeIndex >= Module.StringPool.size() || !JitHost::Services().ParseDynamicArrayElementType(Module.StringPool[TypeIndex], ElementType))
					{
						return false;
					}
					const bool bString = ElementType == "string" || ElementType == "wstring";
					bSawStringArray = bSawStringArray || bString;
					bOnlyStringArrays = bOnlyStringArrays && bString;
				}
				ScanIp += ScanSize;
			}

			bool bSawArray = false;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				switch (Op)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
				case EOpcode::ConstStr:
					{
						break;
					}
				case EOpcode::TakeLocal:
					// The compiler transfers an owned construction temporary directly
					// into its final local. Other consuming shapes stay conservative.
					{
						if (Ip + 6 > Function.Code.size() || static_cast<EOpcode>(Function.Code[Ip + 3]) != EOpcode::StoreLocal || Bytecode::ReadU16LE(Function.Code.data() + Ip + 1) == Bytecode::ReadU16LE(Function.Code.data() + Ip + 4))
						{
							return false;
						}
						break;
					}
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
				case EOpcode::Lt:
				case EOpcode::Le:
				case EOpcode::Gt:
				case EOpcode::Ge:
				case EOpcode::Not:
				case EOpcode::Neg:
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
				case EOpcode::GetIndexCopy:
				case EOpcode::SetIndex:
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
				case EOpcode::LoadGlobal:
					{
						if (!IsScalarGlobalLoad(Module, Function.Code, Ip))
						{
							return false;
						}
						break;
					}
				case EOpcode::Eq:
				case EOpcode::Ne:
					{
						// Numeric-array functions retain the conservative historical gate:
						// several scalar reductions treat equality operands as raw locals.
						// Read-only string-array functions instead use the explicit borrowed
						// element comparison, whose operand provenance is fully checked.
						if (!bSawStringArray || !bOnlyStringArrays)
						{
							return false;
						}
						break;
					}
				case EOpcode::NewDynamicArray:
					{
						if (Ip + 3 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t TypeIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						if (TypeIndex >= Module.StringPool.size())
						{
							return false;
						}
						std::string ElementType;
						if (!JitHost::Services().ParseDynamicArrayElementType(
						        Module.StringPool[TypeIndex], ElementType) ||
						    !JitHost::Services().IsPrimitiveValueTypeName(ElementType))
						{
							return false;
						}
						bSawArray = true;
						break;
					}
				case EOpcode::CallMethod:
					{
						if (Ip + 4 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t NameIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
						if (NameIndex >= Module.StringPool.size() || ArgumentCount != 1)
						{
							return false;
						}
						const std::string& Name = Module.StringPool[NameIndex];
						if (Name != "reserve" && Name != "push")
						{
							return false;
						}
						break;
					}
				case EOpcode::Call:
					{
						if (Ip + 4 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t CalleeIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
						if (CalleeIndex >= Module.Functions.size() || !HasScalarCallSignature(Module.Functions[CalleeIndex], ArgumentCount))
						{
							return false;
						}
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (Ip + 3 > Function.Code.size())
						{
							return false;
						}
						const EBuiltinId Builtin =
						    static_cast<EBuiltinId>(Function.Code[Ip + 1]);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 2];
						const bool bSafe = ArgumentCount == 0 || (ArgumentCount == 1 && (Builtin == EBuiltinId::Len || Builtin == EBuiltinId::ToInt || Builtin == EBuiltinId::ToUnsigned || Builtin == EBuiltinId::ToInt64 || Builtin == EBuiltinId::ToUInt64 || Builtin == EBuiltinId::ToFloat || Builtin == EBuiltinId::ToDouble || Builtin == EBuiltinId::ToInt8 || Builtin == EBuiltinId::ToUInt8 || Builtin == EBuiltinId::ToInt16 || Builtin == EBuiltinId::ToUInt16));
						if (!bSafe)
						{
							return false;
						}
						break;
					}
				default:
					return false;
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					return false;
				}
				Ip += Size;
			}
			return bSawArray;
		}

		bool IsNonEscapingScalarMapCandidate(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			if (BytecodeFunctionHasRefParams(Function))
			{
				// StoreLocal writes through ref parameters, which could publish the
				// JIT-only representation outside this frame.
				return false;
			}
			std::vector<EStackTag> ParameterTags;
			EStackTag ReturnTag = EStackTag::Any;
			bool bVoidReturn = false;
			if (!ParseClosureType(
			        Function.ClosureTypeName,
			        ParameterTags,
			        ReturnTag,
			        bVoidReturn) ||
			    !std::all_of(
			        ParameterTags.begin(),
			        ParameterTags.end(),
			        [](const EStackTag Tag)
			{
				return Tag == EStackTag::I32 || Tag == EStackTag::U32 || Tag == EStackTag::I64 || Tag == EStackTag::U64 || Tag == EStackTag::F32 || Tag == EStackTag::F64 || Tag == EStackTag::Bool;
			}) ||
			    (!bVoidReturn && ReturnTag != EStackTag::I32 && ReturnTag != EStackTag::U32 && ReturnTag != EStackTag::I64 && ReturnTag != EStackTag::U64 && ReturnTag != EStackTag::F32 && ReturnTag != EStackTag::F64 && ReturnTag != EStackTag::Bool))
			{
				// Only a void/scalar result proves that a newly created map cannot
				// leave the frame through Return. `any` is deliberately rejected.
				return false;
			}
			const auto HasScalarCallSignature = [](const FBytecodeFunction& Callee,
			                                        const std::uint8_t ArgumentCount)
			{
				std::vector<EStackTag> Params;
				EStackTag Result = EStackTag::Any;
				bool bVoid = false;
				const auto IsScalar = [](const EStackTag Tag)
				{
					return Tag == EStackTag::I32 || Tag == EStackTag::U32 || Tag == EStackTag::I64 || Tag == EStackTag::U64 || Tag == EStackTag::F32 || Tag == EStackTag::F64 || Tag == EStackTag::Bool;
				};
				return !Callee.bIsMethod && !BytecodeFunctionHasRefParams(Callee) && Callee.ParamCount == ArgumentCount && ParseClosureType(Callee.ClosureTypeName, Params, Result, bVoid) && Callee.bReturnsVoid == bVoid && Params.size() == ArgumentCount && std::all_of(Params.begin(), Params.end(), IsScalar) && (bVoid || IsScalar(Result));
			};
			bool bSawMap = false;
			std::size_t PreviousIp = Function.Code.size();
			std::size_t PreviousPreviousIp = Function.Code.size();
			std::unordered_set<std::uint16_t> MapLocalSlots;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				switch (Op)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
				case EOpcode::ConstStr:
				case EOpcode::LoadLocal:
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
				case EOpcode::Lt:
				case EOpcode::Le:
				case EOpcode::Gt:
				case EOpcode::Ge:
				case EOpcode::Not:
				case EOpcode::Neg:
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
				case EOpcode::GetIndexCopy:
				case EOpcode::SetIndex:
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
				case EOpcode::LoadGlobal:
					{
						if (!IsScalarGlobalLoad(Module, Function.Code, Ip))
						{
							return false;
						}
						break;
					}
				case EOpcode::StoreLocal:
					{
						if (Ip + 3 > Function.Code.size())
						{
							return false;
						}
						if (PreviousIp < Function.Code.size() && static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::NewMap)
						{
							MapLocalSlots.insert(
							    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1));
						}
						else if (PreviousIp + 3 <= Function.Code.size() && static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::TakeLocal)
						{
							const auto Source = Bytecode::ReadU16LE(Function.Code.data() + PreviousIp + 1);
							if (MapLocalSlots.erase(Source))
							{
								MapLocalSlots.insert(Bytecode::ReadU16LE(Function.Code.data() + Ip + 1));
							}
						}
						break;
					}
				case EOpcode::Eq:
				case EOpcode::Ne:
					// Map equality observes canonical entries. Without typed
					// stack dataflow we cannot prove that these compare scalars.
					return false;
				case EOpcode::TakeLocal:
					// The compiler transfers an owned construction temporary directly
					// into its final local. Other consuming shapes stay conservative.
					{
						if (Ip + 6 > Function.Code.size() || static_cast<EOpcode>(Function.Code[Ip + 3]) != EOpcode::StoreLocal || Bytecode::ReadU16LE(Function.Code.data() + Ip + 1) == Bytecode::ReadU16LE(Function.Code.data() + Ip + 4))
						{
							return false;
						}
						break;
					}
				case EOpcode::NewMap:
					{
						if (Ip + 3 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t TypeIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						if (TypeIndex >= Module.StringPool.size())
						{
							return false;
						}
						std::string KeyType;
						std::string ValueType;
						if (!JitHost::Services().SplitMapType(
						        Module.StringPool[TypeIndex], KeyType, ValueType))
						{
							return false;
						}
						const bool bSupportedKey = KeyType == "int" || KeyType == "int32" || KeyType == "unsigned int" || KeyType == "uint" || KeyType == "unsigned" || KeyType == "uint32" || KeyType == "int64" || KeyType == "long" || KeyType == "uint64" || KeyType == "unsigned long" || KeyType == "string" || KeyType == "wstring";
						const bool bSupportedValue =
						    JitHost::Services().IsPrimitiveValueTypeName(ValueType) && ValueType != "string" && ValueType != "wstring";
						if (!bSupportedKey || !bSupportedValue)
						{
							return false;
						}
						bSawMap = true;
						break;
					}
				case EOpcode::CallMethod:
					{
						if (Ip + 4 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t NameIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
						const bool bConstantCapacity = PreviousIp < Function.Code.size() && static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::ConstI32;
						bool bScalarLocalCapacity = false;
						if (PreviousIp + 3 <= Function.Code.size() && PreviousPreviousIp + 3 <= Function.Code.size() && static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::LoadLocal && static_cast<EOpcode>(Function.Code[PreviousPreviousIp]) == EOpcode::LoadLocal)
						{
							const std::uint16_t ReceiverSlot = Bytecode::ReadU16LE(
							    Function.Code.data() + PreviousPreviousIp + 1);
							const std::uint16_t CapacitySlot = Bytecode::ReadU16LE(
							    Function.Code.data() + PreviousIp + 1);
							bScalarLocalCapacity = MapLocalSlots.count(ReceiverSlot) != 0 && MapLocalSlots.count(CapacitySlot) == 0;
						}
						// A literal capacity cannot publish a handle. A local capacity is
						// accepted only for the compiler's explicit `map-local, scalar-local,
						// reserve` stack shape, keeping custom reserve(map) calls conservative.
						if (NameIndex >= Module.StringPool.size() || Module.StringPool[NameIndex] != "reserve" || ArgumentCount != 1 || (!bConstantCapacity && !bScalarLocalCapacity))
						{
							return false;
						}
						break;
					}
				case EOpcode::Call:
					{
						if (Ip + 4 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t CalleeIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
						if (CalleeIndex >= Module.Functions.size() || !HasScalarCallSignature(Module.Functions[CalleeIndex], ArgumentCount))
						{
							return false;
						}
						break;
					}
				case EOpcode::CallBoundMethod:
				case EOpcode::CallNative:
					{
						// With no explicit arguments a call cannot observe or publish the
						// local map handle. Its return value remains independently boxed.
						if (Ip + 4 > Function.Code.size() || Function.Code[Ip + 3] != 0)
						{
							return false;
						}
						break;
					}
				case EOpcode::CallBuiltin:
					{
						// Zero-argument builtins such as now() cannot make a local map
						// escape. Primitive conversions and owned text snapshots never
						// publish the source handle; container formatting reads type/size
						// through the authoritative boxed or scalar representation.
						if (Ip + 3 > Function.Code.size())
						{
							return false;
						}
						if (Function.Code[Ip + 2] != 0)
						{
							const EBuiltinId Builtin =
							    static_cast<EBuiltinId>(Function.Code[Ip + 1]);
							const bool bPrimitiveConversion = Function.Code[Ip + 2] == 1 && (Builtin == EBuiltinId::ToString || Builtin == EBuiltinId::ToInt || Builtin == EBuiltinId::ToUnsigned || Builtin == EBuiltinId::ToInt64 || Builtin == EBuiltinId::ToUInt64 || Builtin == EBuiltinId::ToFloat || Builtin == EBuiltinId::ToDouble || Builtin == EBuiltinId::ToInt8 || Builtin == EBuiltinId::ToUInt8 || Builtin == EBuiltinId::ToInt16 || Builtin == EBuiltinId::ToUInt16);
							if (!bPrimitiveConversion)
							{
								return false;
							}
						}
						break;
					}
				default:
					// Calls, natives, globals, fields, refs, closures and other
					// containers can publish the handle. Keep their canonical
					// FValue-backed map representation.
					return false;
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					return false;
				}
				// The validated primitive conversion above keeps the producer and
				// receiver identity. Retain reserve's shape without removing its checks.
				const bool bScalarConversion = Op == EOpcode::CallBuiltin && Function.Code[Ip + 2] == 1;
				if (!bScalarConversion)
				{
					PreviousPreviousIp = PreviousIp;
					PreviousIp = Ip;
				}
				Ip += Size;
			}
			return bSawMap;
		}

		bool IsNonEscapingScalarSetCandidate(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			if (BytecodeFunctionHasRefParams(Function))
			{
				return false;
			}
			std::vector<EStackTag> Parameters;
			EStackTag ReturnTag = EStackTag::Any;
			bool bVoidReturn = false;
			const auto IsScalar = [](const EStackTag Tag)
			{
				return Tag == EStackTag::I32 || Tag == EStackTag::U32 || Tag == EStackTag::I64 || Tag == EStackTag::U64 || Tag == EStackTag::F32 || Tag == EStackTag::F64 || Tag == EStackTag::Bool;
			};
			if (!ParseClosureType(
			        Function.ClosureTypeName, Parameters, ReturnTag, bVoidReturn) ||
			    !std::all_of(Parameters.begin(), Parameters.end(), IsScalar) || (!bVoidReturn && !IsScalar(ReturnTag)))
			{
				return false;
			}
			const auto HasScalarCallSignature = [&](
			                                        const FBytecodeFunction& Callee,
			                                        const std::uint8_t ArgumentCount)
			{
				std::vector<EStackTag> Params;
				EStackTag Result = EStackTag::Any;
				bool bVoid = false;
				return !Callee.bIsMethod && !BytecodeFunctionHasRefParams(Callee) && Callee.ParamCount == ArgumentCount && ParseClosureType(Callee.ClosureTypeName, Params, Result, bVoid) && Callee.bReturnsVoid == bVoid && Params.size() == ArgumentCount && std::all_of(Params.begin(), Params.end(), IsScalar) && (bVoid || IsScalar(Result));
			};

			bool bSawSet = false;
			std::size_t PreviousIp = Function.Code.size();
			std::unordered_set<std::uint16_t> SetLocalSlots;
			std::vector<std::size_t> SetReceiverLoadIps;
			const auto IsOneScalarArgumentAfter = [&](
			                                          const std::size_t Begin,
			                                          const std::size_t End) -> bool
			{
				std::size_t Depth = 0;
				for (std::size_t ScanIp = Begin; ScanIp < End;)
				{
					const EOpcode ScanOp = static_cast<EOpcode>(Function.Code[ScanIp]);
					const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
					if (ScanSize == 0 || ScanSize > End - ScanIp)
					{
						return false;
					}
					if (ScanOp == EOpcode::ConstI32 || ScanOp == EOpcode::ConstF64 || ScanOp == EOpcode::ConstBool || ScanOp == EOpcode::ConstStr || ScanOp == EOpcode::LoadLocal || (ScanOp == EOpcode::LoadGlobal && IsScalarGlobalLoad(Module, Function.Code, ScanIp)))
					{
						++Depth;
					}
					else if (ScanOp == EOpcode::Add || ScanOp == EOpcode::Sub || ScanOp == EOpcode::Mul || ScanOp == EOpcode::Div || ScanOp == EOpcode::Mod || ScanOp == EOpcode::BitAnd || ScanOp == EOpcode::BitOr || ScanOp == EOpcode::BitXor || ScanOp == EOpcode::Shl || ScanOp == EOpcode::Shr)
					{
						if (Depth < 2)
						{
							return false;
						}
						--Depth;
					}
					else if (ScanOp == EOpcode::Neg || ScanOp == EOpcode::Not || ScanOp == EOpcode::BitNot || ScanOp == EOpcode::CheckedCast)
					{
						if (Depth == 0)
						{
							return false;
						}
					}
					else if (ScanOp == EOpcode::CallBuiltin)
					{
						if (Depth == 0 || Function.Code[ScanIp + 2] != 1)
						{
							return false;
						}
						const EBuiltinId Builtin =
						    static_cast<EBuiltinId>(Function.Code[ScanIp + 1]);
						if (Builtin != EBuiltinId::ToInt && Builtin != EBuiltinId::ToUnsigned && Builtin != EBuiltinId::ToInt64 && Builtin != EBuiltinId::ToUInt64 && Builtin != EBuiltinId::ToFloat && Builtin != EBuiltinId::ToDouble && Builtin != EBuiltinId::ToInt8 && Builtin != EBuiltinId::ToUInt8 && Builtin != EBuiltinId::ToInt16 && Builtin != EBuiltinId::ToUInt16)
						{
							return false;
						}
					}
					else
					{
						return false;
					}
					ScanIp += ScanSize;
				}
				return Depth == 1;
			};
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				switch (Op)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
				case EOpcode::ConstStr:
				case EOpcode::LoadLocal:
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
				case EOpcode::LoadGlobal:
					{
						if (!IsScalarGlobalLoad(Module, Function.Code, Ip))
						{
							return false;
						}
						break;
					}
				case EOpcode::StoreLocal:
					{
						if (Ip + 3 > Function.Code.size())
						{
							return false;
						}
						if (PreviousIp < Function.Code.size() && static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::NewSet)
						{
							SetLocalSlots.insert(Bytecode::ReadU16LE(Function.Code.data() + Ip + 1));
						}
						else if (PreviousIp + 3 <= Function.Code.size() && (static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::LoadLocal || static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::TakeLocal))
						{
							const std::uint16_t SourceSlot =
							    Bytecode::ReadU16LE(Function.Code.data() + PreviousIp + 1);
							if (SetLocalSlots.count(SourceSlot) != 0)
							{
								// Brace initializers are assembled in a compiler temporary and
								// finally copied into the declared local. Both slots share the
								// same frame-owned handle, so propagating the identity preserves
								// the non-escape proof and enables the same typed Set path as an
								// explicit `set<T>(); add(...)` construction.
								SetLocalSlots.insert(
								    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1));
								if (static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::TakeLocal)
								{
									SetLocalSlots.erase(SourceSlot);
								}
							}
						}
						break;
					}
				case EOpcode::TakeLocal:
					// The compiler transfers an owned construction temporary directly
					// into its final local. Other consuming shapes stay conservative.
					{
						if (Ip + 6 > Function.Code.size() || static_cast<EOpcode>(Function.Code[Ip + 3]) != EOpcode::StoreLocal || Bytecode::ReadU16LE(Function.Code.data() + Ip + 1) == Bytecode::ReadU16LE(Function.Code.data() + Ip + 4))
						{
							return false;
						}
						break;
					}
				case EOpcode::NewSet:
					{
						if (Ip + 3 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t TypeIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						if (TypeIndex >= Module.StringPool.size())
						{
							return false;
						}
						std::string ElementType;
						if (!JitHost::Services().SplitSetElementType(
						        Module.StringPool[TypeIndex], ElementType))
						{
							return false;
						}
						const bool bSupported = ElementType == "int" || ElementType == "int32" || ElementType == "unsigned int" || ElementType == "uint" || ElementType == "unsigned" || ElementType == "uint32" || ElementType == "int64" || ElementType == "long" || ElementType == "uint64" || ElementType == "unsigned long" || ElementType == "string" || ElementType == "wstring";
						if (!bSupported)
						{
							return false;
						}
						bSawSet = true;
						break;
					}
				case EOpcode::CallMethod:
					{
						if (Ip + 4 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t NameIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
						if (NameIndex >= Module.StringPool.size())
						{
							return false;
						}
						const std::string& Name = Module.StringPool[NameIndex];
						bool bKnownSetReceiver = false;
						for (auto It = SetReceiverLoadIps.rbegin();
						    It != SetReceiverLoadIps.rend(); ++It)
						{
							if (IsOneScalarArgumentAfter(
							        *It + Bytecode::InstructionSize(EOpcode::LoadLocal), Ip))
							{
								bKnownSetReceiver = true;
								break;
							}
						}
						if ((Name != "reserve" && Name != "add" && Name != "has" && Name != "remove") || ArgumentCount != 1 || !bKnownSetReceiver)
						{
							return false;
						}
						break;
					}
				case EOpcode::Call:
					{
						if (Ip + 4 > Function.Code.size())
						{
							return false;
						}
						const std::uint16_t CalleeIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
						if (CalleeIndex >= Module.Functions.size() || !HasScalarCallSignature(Module.Functions[CalleeIndex], ArgumentCount))
						{
							return false;
						}
						break;
					}
				case EOpcode::CallBoundMethod:
				case EOpcode::CallNative:
					{
						// A zero-argument call cannot observe or publish the local Set
						// handle. Keep this aligned with the Array/Map escape proofs.
						if (Ip + 4 > Function.Code.size() || Function.Code[Ip + 3] != 0)
						{
							return false;
						}
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (Ip + 3 > Function.Code.size())
						{
							return false;
						}
						const std::uint8_t ArgumentCount = Function.Code[Ip + 2];
						if (ArgumentCount == 0)
						{
							break;
						}
						if (ArgumentCount != 1)
						{
							return false;
						}
						const EBuiltinId Builtin = static_cast<EBuiltinId>(Function.Code[Ip + 1]);
						if (Builtin == EBuiltinId::Len)
						{
							if (PreviousIp + 3 > Function.Code.size() || static_cast<EOpcode>(Function.Code[PreviousIp]) != EOpcode::LoadLocal || SetLocalSlots.count(Bytecode::ReadU16LE(Function.Code.data() + PreviousIp + 1)) == 0)
							{
								return false;
							}
						}
						else if (Builtin != EBuiltinId::ToInt && Builtin != EBuiltinId::ToUnsigned && Builtin != EBuiltinId::ToInt64 && Builtin != EBuiltinId::ToUInt64 && Builtin != EBuiltinId::ToFloat && Builtin != EBuiltinId::ToDouble && Builtin != EBuiltinId::ToInt8 && Builtin != EBuiltinId::ToUInt8 && Builtin != EBuiltinId::ToInt16 && Builtin != EBuiltinId::ToUInt16)
						{
							return false;
						}
						break;
					}
				default:
					return false;
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					return false;
				}
				if (Op == EOpcode::LoadLocal && SetLocalSlots.count(Bytecode::ReadU16LE(Function.Code.data() + Ip + 1)) != 0)
				{
					SetReceiverLoadIps.push_back(Ip);
				}
				PreviousIp = Ip;
				Ip += Size;
			}
			return bSawSet;
		}

		bool IsInlineScalarTag(const EStackTag Tag)
		{
			return Tag == EStackTag::I32 || Tag == EStackTag::U32 || Tag == EStackTag::I64 || Tag == EStackTag::U64 || Tag == EStackTag::F32 || Tag == EStackTag::F64 || Tag == EStackTag::Bool;
		}

		inline constexpr std::size_t ScalarInlineSitesPerPass = 8u;
		inline constexpr std::size_t ScalarInlineAddedBytesPerPass = 512u;

		struct FScalarStackPoint
		{
			std::size_t Depth = std::numeric_limits<std::size_t>::max();
			EStackTag Top = EStackTag::Any;
		};

		bool ValidateCfg(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::vector<EStackTag>& ParamTags,
		    EStackTag ReturnTag,
		    bool bVoidReturn,
		    std::uint16_t* OutMaxStackSlots = nullptr,
		    std::uint16_t* OutMaxBoxedStackSlots = nullptr,
		    std::vector<std::size_t>* OutTextAddOffsets = nullptr,
		    std::vector<FScalarStackPoint>* OutStackPoints = nullptr,
		    std::vector<std::size_t>* OutUnsupportedTextFormatOffsets = nullptr);

		bool IsInlineableScalarCallee(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::uint8_t ArgumentCount,
		    const std::size_t InlineDepth = 0)
		{
			// The total expansion budget below remains authoritative.  A 96-byte
			// per-callee ceiling excluded small branchy arithmetic leaves after debug
			// and checked-operation bytecode expansion, even though they remain cheap
			// and profitable to inline.
			constexpr std::size_t kMaxInlineCalleeBytes = 192;
			constexpr std::size_t kMaxInlineChainDepth = 4;
			if (InlineDepth > kMaxInlineChainDepth || Function.bIsMethod || Function.bReturnsVoid || Function.ParamCount != ArgumentCount || Function.LocalCount < Function.ParamCount || Function.Code.empty() || Function.Code.size() > kMaxInlineCalleeBytes
			    // Exception regions are function-relative metadata. They cannot be
			    // copied into a caller until the inliner also remaps protected ranges,
			    // handlers and finally continuations into the expanded instruction
			    // stream. Reject them even when the current opcode subset happens not
			    // to raise, so later runtime-error/exception unification cannot silently
			    // change the meaning of an already-inlineable leaf.
			    || !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function) || JitLowering::RequiresVmOwnership(Function))
			{
				return false;
			}
			std::vector<EStackTag> Params;
			EStackTag ReturnTag = EStackTag::Any;
			bool bVoidReturn = false;
			if (!ParseClosureType(
			        Function.ClosureTypeName, Params, ReturnTag, bVoidReturn) ||
			    bVoidReturn || !IsInlineScalarTag(ReturnTag) || Params.size() != Function.ParamCount || !std::all_of(Params.begin(), Params.end(), IsInlineScalarTag))
			{
				return false;
			}
			std::size_t ReturnCount = 0;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					return false;
				}
				// Keep loop bodies as independently optimized functions. Inlining a
				// backedge into a caller loses whole-function recurrence/container
				// lowering, duplicates code, and adds no useful call-site information;
				// the one native call at the outer boundary is cheaper in every case.
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse || Op == EOpcode::JmpIfLocalGeImm)
				{
					const std::size_t RelativeOffset =
					    Op == EOpcode::JmpIfLocalGeImm ? 7u : 1u;
					std::size_t Target = 0;
					if (!IsJumpTargetInRange(
					        Ip + Size,
					        Bytecode::ReadI32LE(Function.Code.data() + Ip + RelativeOffset),
					        Function.Code.size(), Target) ||
					    Target <= Ip)
					{
						return false;
					}
				}
				switch (Op)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
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
				case EOpcode::Dup:
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
				case EOpcode::Call:
					{
						const std::uint16_t CalleeIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
						const std::uint8_t NestedArgumentCount = Function.Code[Ip + 3u];
						if (CalleeIndex >= Module.Functions.size())
						{
							return false;
						}
						const FBytecodeFunction& Nested = Module.Functions[CalleeIndex];
						// Name equality catches direct recursion, while the depth limit above
						// also terminates mutually recursive graphs.  Only a fully verified
						// scalar chain may be exposed to the caller's next inline pass.
						if (Nested.Name == Function.Name || !IsInlineableScalarCallee(Module, Nested, NestedArgumentCount, InlineDepth + 1u))
						{
							return false;
						}
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (Ip + Bytecode::InstructionSize(Op) > Function.Code.size() || Function.Code[Ip + 2u] != 1u)
						{
							return false;
						}
						const EBuiltinId Builtin = static_cast<EBuiltinId>(
						    Function.Code[Ip + 1u]);
						// These scalar conversions either cannot fail or return a typed
						// encoded kernel status.  The inliner materializes the callee debug
						// map below, so their error identity and source position remain exact.
						if (Builtin != EBuiltinId::ToInt && Builtin != EBuiltinId::ToUnsigned && Builtin != EBuiltinId::ToFloat && Builtin != EBuiltinId::ToDouble)
						{
							return false;
						}
						break;
					}
				case EOpcode::Return:
					{
						++ReturnCount;
						if (Ip + Bytecode::InstructionSize(Op) != Function.Code.size())
						{
							return false;
						}
						break;
					}
				default:
					// No nested calls, globals, fields, objects, containers or
					// observable dispatch points are duplicated.
					return false;
				}
				Ip += Size;
			}
			// Validate with an empty callee operand stack before removing its frame.
			// Checking only the expanded caller would let malformed callees borrow
			// operands that the caller retained below the call's arguments.
			return ReturnCount == 1 && ValidateCfg(Module, Function, Params, ReturnTag, bVoidReturn);
		}

		bool CanElideCatchOnlyMetadataAfterScalarInlining(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			if (Function.ExceptionRegions.empty())
			{
				return false;
			}
			for (const FBytecodeExceptionRegion& Region : Function.ExceptionRegions)
			{
				if (Region.FinallyOffset != std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}
			}
			const auto IsProtectedOffset = [&](const std::size_t Offset)
			{
				return std::any_of(
				    Function.ExceptionRegions.begin(), Function.ExceptionRegions.end(),
				    [Offset](const FBytecodeExceptionRegion& Region)
				{
					return Offset >= Region.BeginOffset && Offset < Region.EndOffset;
				});
			};
			bool bHasProtectedScalarCall = false;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Function.Code.size() - Ip)
				{
					return false;
				}
				if (Op == EOpcode::Throw || Op == EOpcode::EndFinally)
				{
					return false;
				}
				if (IsProtectedOffset(Ip))
				{
					if (Op == EOpcode::Call)
					{
						if (Size < 4)
						{
							return false;
						}
						const std::uint16_t CalleeIndex =
						    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 3u];
						if (CalleeIndex >= Module.Functions.size() || !IsInlineableScalarCallee(Module, Module.Functions[CalleeIndex], ArgumentCount))
						{
							return false;
						}
						bHasProtectedScalarCall = true;
					}
					else if (Op == EOpcode::CallMethod || Op == EOpcode::CallBuiltin || Op == EOpcode::CallNative || Op == EOpcode::CallBoundMethod || Op == EOpcode::CallIndirect)
					{
						return false;
					}
				}
				Ip += Size;
			}
			// Clearing a catch table is useful only when the protected call will be
			// removed. A no-call catch loop already uses the existing Linear path.
			return bHasProtectedScalarCall;
		}

		bool CanForwardSingleScalarArgument(const FBytecodeFunction& Function)
		{
			// A straight-line scalar expression that reads its sole parameter once
			// can consume the caller's already-evaluated stack argument directly.
			// This avoids a synthetic StoreLocal/LoadLocal pair and exposes the same
			// expression shape to later native loop reductions as source-written code.
			if (Function.ParamCount != 1 || Function.LocalCount != 1 || Function.Code.size() < Bytecode::InstructionSize(EOpcode::LoadLocal) || static_cast<EOpcode>(Function.Code[0]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Function.Code.data() + 1) != 0)
			{
				return false;
			}
			for (std::size_t Ip = Bytecode::InstructionSize(EOpcode::LoadLocal);
			    Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				switch (Op)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
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
				case EOpcode::Dup:
				case EOpcode::Pop:
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::BitNot:
				case EOpcode::Shl:
				case EOpcode::Shr:
				case EOpcode::Return:
					{
						break;
					}
				default:
					return false;
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Function.Code.size() - Ip)
				{
					return false;
				}
				Ip += Size;
			}
			return true;
		}

		bool CanForwardDirectScalarArguments(const FBytecodeFunction& Function)
		{
			// Directly substituting caller-local loads is valid only when every callee
			// local is a by-value parameter and the callee never assigns to it. A write
			// would otherwise mutate the caller instead of the callee's private copy.
			if (Function.ParamCount == 0u || Function.LocalCount != Function.ParamCount || Function.ParamCount > 8u)
			{
				return false;
			}
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0u || Size > Function.Code.size() - Ip)
				{
					return false;
				}
				switch (Op)
				{
				case EOpcode::StoreLocal:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::AddLocalLocal:
				case EOpcode::BindLocal:
				case EOpcode::MakeLocalRef:
					return false;
				default:
					{
						break;
					}
				}
				Ip += Size;
			}
			return true;
		}

		bool IsInlineableBoundThisCallee(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Caller,
		    const FBytecodeFunction& Function,
		    const std::uint8_t ArgumentCount,
		    const bool bCanNormalizeF64VoidSentinel)
		{
			// A CallBoundMethod whose receiver is the caller's current `this` can be
			// expanded without changing dispatch semantics. Keep this intentionally
			// narrow: only a receiver followed by no argument or one integer literal,
			// and no callee locals beyond parameter slots.
			constexpr std::size_t kMaxInlineCalleeBytes = 96;
			// A void script method uses the bytecode's integer return sentinel. Do
			// not splice that sentinel into a homogeneous F64 caller because that
			// would widen its CFG to Mixed. I32/U32/bool use the sentinel directly;
			// I64/U64/F32 callers already use the heterogeneous Mixed ABI, where the
			// sentinel and their wider scalar values retain independent stack tags. A
			// homogeneous F64 caller is admitted only when its inlined void sentinel
			// can be rewritten to an existing F64 zero constant without changing the
			// module constant pool.
			std::vector<EStackTag> CallerParameters;
			EStackTag CallerReturn = EStackTag::Any;
			bool bCallerVoid = false;
			if (!ParseClosureType(
			        Caller.ClosureTypeName,
			        CallerParameters,
			        CallerReturn,
			        bCallerVoid) ||
			    bCallerVoid || !std::all_of(CallerParameters.begin(), CallerParameters.end(), IsInlineScalarTag) || (CallerReturn != EStackTag::I32 && CallerReturn != EStackTag::U32 && CallerReturn != EStackTag::I64 && CallerReturn != EStackTag::U64 && CallerReturn != EStackTag::F32 && CallerReturn != EStackTag::F64 && CallerReturn != EStackTag::Bool))
			{
				return false;
			}
			std::vector<EStackTag> Parameters;
			EStackTag ReturnTag = EStackTag::Any;
			bool bVoidReturn = false;
			if (!ParseClosureType(
			        Function.ClosureTypeName, Parameters, ReturnTag, bVoidReturn) ||
			    Parameters.size() != Function.ParamCount || bVoidReturn != Function.bReturnsVoid || (!bVoidReturn && !IsInlineScalarTag(ReturnTag)) || !std::all_of(Parameters.begin(), Parameters.end(), IsInlineScalarTag))
			{
				return false;
			}
			if (CallerReturn == EStackTag::F64 && bVoidReturn)
			{
				const std::size_t SentinelOffset = Function.Code.size() >= 4
				    ? Function.Code.size() - 4
				    : Function.Code.size();
				if (!bCanNormalizeF64VoidSentinel || SentinelOffset >= Function.Code.size() || static_cast<EOpcode>(Function.Code[SentinelOffset]) != EOpcode::ConstI32 || static_cast<EOpcode>(Function.Code[SentinelOffset + 3]) != EOpcode::Return)
				{
					return false;
				}
				const std::uint16_t ConstantIndex =
				    Bytecode::ReadU16LE(Function.Code.data() + SentinelOffset + 1);
				if (ConstantIndex >= Module.Constants.size() || Module.Constants[ConstantIndex].Kind != EConstantKind::I32 || Module.Constants[ConstantIndex].I32 != 0)
				{
					return false;
				}
			}
			if (!Function.bIsMethod || Function.ParamCount != ArgumentCount || Function.LocalCount != Function.ParamCount || Function.Code.empty() || Function.Code.size() > kMaxInlineCalleeBytes || !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function) || JitLowering::RequiresVmOwnership(Function))
			{
				return false;
			}

			std::size_t ReturnCount = 0;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				switch (Op)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::AddLocalLocal:
				case EOpcode::LoadThis:
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
				case EOpcode::IncField:
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
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::BitNot:
				case EOpcode::Shl:
				case EOpcode::Shr:
				case EOpcode::Dup:
				case EOpcode::Pop:
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
				case EOpcode::CallBoundMethod:
					{
						break;
					}
				case EOpcode::Return:
					{
						++ReturnCount;
						if (Ip + Bytecode::InstructionSize(Op) != Function.Code.size())
						{
							return false;
						}
						break;
					}
				default:
					return false;
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					return false;
				}
				Ip += Size;
			}
			return ReturnCount == 1 && ValidateCfg(Module, Function, Parameters, ReturnTag, bVoidReturn);
		}

		bool MatchDirectScalarLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::uint8_t ArgumentCount,
		    std::optional<EOpcode>& OutOperator,
		    std::size_t& OutOperatorOffset)
		{
			if (!IsInlineableScalarCallee(Module, Function, ArgumentCount))
			{
				return false;
			}
			OutOperator.reset();
			OutOperatorOffset = std::numeric_limits<std::size_t>::max();
			const auto IsDirectOperator = [](const EOpcode Op)
			{
				switch (Op)
				{
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
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::BitNot:
				case EOpcode::Shl:
				case EOpcode::Shr:
					return true;
				default:
					return false;
				}
			};
			if (ArgumentCount == 1 && Function.Code.size() == 4 && static_cast<EOpcode>(Function.Code[0]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Function.Code.data() + 1) == 0 && static_cast<EOpcode>(Function.Code[3]) == EOpcode::Return)
			{
				// Identity: the argument is already on the caller's operand stack.
				return true;
			}
			if (ArgumentCount == 1 && Function.Code.size() == 5 && static_cast<EOpcode>(Function.Code[0]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Function.Code.data() + 1) == 0 && IsDirectOperator(static_cast<EOpcode>(Function.Code[3])) && static_cast<EOpcode>(Function.Code[4]) == EOpcode::Return)
			{
				OutOperator = static_cast<EOpcode>(Function.Code[3]);
				OutOperatorOffset = 3;
				return true;
			}
			if (ArgumentCount == 2 && Function.Code.size() == 8 && static_cast<EOpcode>(Function.Code[0]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Function.Code.data() + 1) == 0 && static_cast<EOpcode>(Function.Code[3]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Function.Code.data() + 4) == 1 && IsDirectOperator(static_cast<EOpcode>(Function.Code[6])) && static_cast<EOpcode>(Function.Code[7]) == EOpcode::Return)
			{
				OutOperator = static_cast<EOpcode>(Function.Code[6]);
				OutOperatorOffset = 6;
				return true;
			}
			return false;
		}

		bool TryRewriteDirectScalarLeafCalls(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    FBytecodeFunction& OutFunction,
		    std::uint16_t& OutInlineSites)
		{
			OutFunction = Function;
			std::size_t Sites = 0;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					return false;
				}
				if (Op == EOpcode::Call && Sites < ScalarInlineSitesPerPass)
				{
					const std::uint16_t CalleeIndex =
					    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
					const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
					if (CalleeIndex < Module.Functions.size())
					{
						const FBytecodeFunction& Callee = Module.Functions[CalleeIndex];
						std::optional<EOpcode> Operator;
						std::size_t OperatorOffset = std::numeric_limits<std::size_t>::max();
						if (Callee.Name != Function.Name && MatchDirectScalarLeaf(Module, Callee, ArgumentCount, Operator, OperatorOffset))
						{
							// Preserve the four-byte Call footprint so all caller
							// branch offsets and debug locations remain unchanged.
							OutFunction.Code[Ip] = static_cast<std::uint8_t>(
							    Operator.value_or(EOpcode::Nop));
							for (std::size_t I = 1; I < Size; ++I)
							{
								OutFunction.Code[Ip + I] =
								    static_cast<std::uint8_t>(EOpcode::Nop);
							}
							// A rewritten arithmetic/checking operation must retain the
							// callee's source location. Otherwise an overflow, division by
							// zero or invalid shift would be reported at the caller's Call.
							if (Operator && OperatorOffset < Callee.DebugLines.size())
							{
								if (OutFunction.DebugLines.size() < OutFunction.Code.size())
								{
									OutFunction.DebugLines.resize(OutFunction.Code.size());
								}
								OutFunction.DebugLines[Ip] = Callee.DebugLines[OperatorOffset];
							}
							++Sites;
						}
					}
				}
				Ip += Size;
			}
			OutInlineSites = static_cast<std::uint16_t>(Sites);
			return Sites != 0;
		}

		void PatchI32(
		    std::vector<std::uint8_t>& Bytes,
		    const std::size_t Offset,
		    const std::int32_t Value)
		{
			const std::uint32_t Bits = static_cast<std::uint32_t>(Value);
			Bytes[Offset] = static_cast<std::uint8_t>(Bits & 0xFFu);
			Bytes[Offset + 1] = static_cast<std::uint8_t>((Bits >> 8u) & 0xFFu);
			Bytes[Offset + 2] = static_cast<std::uint8_t>((Bits >> 16u) & 0xFFu);
			Bytes[Offset + 3] = static_cast<std::uint8_t>((Bits >> 24u) & 0xFFu);
		}

		void PatchU16(
		    std::vector<std::uint8_t>& Bytes,
		    const std::size_t Offset,
		    const std::uint16_t Value)
		{
			Bytes[Offset] = static_cast<std::uint8_t>(Value & 0xFFu);
			Bytes[Offset + 1] = static_cast<std::uint8_t>((Value >> 8u) & 0xFFu);
		}

		bool TryNormalizeInlineReturns(
		    const FBytecodeFunction& Function,
		    FBytecodeFunction& OutFunction)
		{
			const std::size_t CodeSize = Function.Code.size();
			if (CodeSize == 0)
			{
				return false;
			}
			std::vector<std::size_t> InstructionIps;
			std::vector<std::size_t> ReturnIps;
			for (std::size_t Ip = 0; Ip < CodeSize;)
			{
				InstructionIps.push_back(Ip);
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > CodeSize - Ip)
				{
					return false;
				}
				if (Op == EOpcode::Return)
				{
					ReturnIps.push_back(Ip);
				}
				Ip += Size;
			}
			if (ReturnIps.size() < 2 || ReturnIps.back() + 1u != CodeSize)
			{
				return false;
			}

			constexpr std::size_t Invalid = std::numeric_limits<std::size_t>::max();
			std::vector<std::size_t> NewOffset(CodeSize + 1u, Invalid);
			std::size_t NewSize = 0;
			for (const std::size_t Ip : InstructionIps)
			{
				NewOffset[Ip] = NewSize;
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				NewSize += Op == EOpcode::Return && Ip != ReturnIps.back()
				    ? Bytecode::InstructionSize(EOpcode::Jmp)
				    : Bytecode::InstructionSize(Op);
			}
			NewOffset[CodeSize] = NewSize;
			const std::size_t NewFinalReturn = NewOffset[ReturnIps.back()];

			OutFunction = Function;
			OutFunction.Code.clear();
			OutFunction.Code.reserve(NewSize);
			OutFunction.DebugLines.assign(NewSize, {});
			for (const std::size_t Ip : InstructionIps)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				const std::size_t Begin = OutFunction.Code.size();
				if (Op == EOpcode::Return && Ip != ReturnIps.back())
				{
					OutFunction.Code.push_back(static_cast<std::uint8_t>(EOpcode::Jmp));
					Bytecode::WriteI32LE(OutFunction.Code, static_cast<std::int32_t>(NewFinalReturn - (Begin + Bytecode::InstructionSize(EOpcode::Jmp))));
				}
				else
				{
					OutFunction.Code.insert(OutFunction.Code.end(),
					    Function.Code.begin() + Ip, Function.Code.begin() + Ip + Size);
				}
				if (Ip < Function.DebugLines.size())
				{
					OutFunction.DebugLines[Begin] =
					    Function.DebugLines[Ip];
				}
				std::size_t RelativeOffset = Invalid;
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse)
				{
					RelativeOffset = 1u;
				}
				else if (Op == EOpcode::JmpIfLocalGeImm)
				{
					RelativeOffset = 7u;
				}
				if (RelativeOffset != Invalid)
				{
					const std::int64_t OldTarget = static_cast<std::int64_t>(Ip + Size) + Bytecode::ReadI32LE(Function.Code.data() + Ip + RelativeOffset);
					if (OldTarget < 0 || static_cast<std::size_t>(OldTarget) > CodeSize || NewOffset[static_cast<std::size_t>(OldTarget)] == Invalid)
					{
						return false;
					}
					const std::int64_t NewRelative = static_cast<std::int64_t>(
					                                     NewOffset[static_cast<std::size_t>(OldTarget)]) -
					    static_cast<std::int64_t>(Begin + Size);
					if (NewRelative < std::numeric_limits<std::int32_t>::min() || NewRelative > std::numeric_limits<std::int32_t>::max())
					{
						return false;
					}
					PatchI32(OutFunction.Code, Begin + RelativeOffset,
					    static_cast<std::int32_t>(NewRelative));
				}
			}
			return OutFunction.Code.size() == NewSize;
		}

		struct FInlineExpansion
		{
			const FBytecodeFunction* Callee = nullptr;
			std::uint16_t LocalBase = 0;
			std::optional<std::uint16_t> F64VoidSentinelConstant;
			bool bForwardSingleStackArgument = false;
			std::array<std::uint16_t, 8> ForwardedParamSlots{};
			std::uint8_t ForwardedParamCount = 0;
		};

		bool TryBuildInlinedScalarFunction(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    FBytecodeFunction& OutFunction,
		    std::uint16_t& OutInlineSites)
		{
			const std::size_t CodeSize = Function.Code.size();
			std::vector<FInlineExpansion> Expansions(CodeSize);
			std::array<FBytecodeFunction, ScalarInlineSitesPerPass> NormalizedCallees{};
			std::size_t NormalizedCalleeCount = 0;
			std::vector<bool> BoundThisReceivers(CodeSize, false);
			std::vector<bool> ElidedCallerLoads(CodeSize, false);
			std::vector<bool> CallerJumpTargets(CodeSize, false);
			for (std::size_t ScanIp = 0; ScanIp < CodeSize;)
			{
				const EOpcode ScanOp = static_cast<EOpcode>(Function.Code[ScanIp]);
				const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
				if (ScanSize == 0u || ScanSize > CodeSize - ScanIp)
				{
					return false;
				}
				std::size_t Target = 0;
				std::size_t RelativeOffset = std::numeric_limits<std::size_t>::max();
				if (ScanOp == EOpcode::Jmp || ScanOp == EOpcode::JmpIfFalse)
				{
					RelativeOffset = 1u;
				}
				else if (ScanOp == EOpcode::JmpIfLocalGeImm)
				{
					RelativeOffset = 7u;
				}
				if (RelativeOffset != std::numeric_limits<std::size_t>::max())
				{
					const std::int64_t SignedTarget = static_cast<std::int64_t>(
					                                      ScanIp + ScanSize) +
					    Bytecode::ReadI32LE(
					        Function.Code.data() + ScanIp + RelativeOffset);
					if (SignedTarget < 0 || SignedTarget > static_cast<std::int64_t>(CodeSize))
					{
						return false;
					}
					Target = static_cast<std::size_t>(SignedTarget);
					if (Target < CodeSize)
					{
						CallerJumpTargets[Target] = true;
					}
				}
				ScanIp += ScanSize;
			}
			std::vector<EStackTag> CallerParameters;
			EStackTag CallerReturn = EStackTag::Any;
			bool bCallerVoid = false;
			const bool bF64Caller = ParseClosureType(
			                            Function.ClosureTypeName, CallerParameters, CallerReturn, bCallerVoid) &&
			    !bCallerVoid && CallerReturn == EStackTag::F64;
			std::optional<std::uint16_t> F64ZeroConstant;
			if (bF64Caller)
			{
				for (std::size_t Index = 0; Index < Module.Constants.size(); ++Index)
				{
					if (Module.Constants[Index].Kind == EConstantKind::F64 && Module.Constants[Index].F64 == 0.0 && Index <= std::numeric_limits<std::uint16_t>::max())
					{
						F64ZeroConstant = static_cast<std::uint16_t>(Index);
						break;
					}
				}
			}
			std::uint32_t NextLocal = Function.LocalCount;
			std::size_t AddedBytes = 0;
			std::size_t InlineSites = 0;
			std::size_t PreviousIp = std::numeric_limits<std::size_t>::max();
			std::size_t PreviousPreviousIp = std::numeric_limits<std::size_t>::max();
			std::vector<std::size_t> InstructionHistory;
			InstructionHistory.reserve(8u);
			for (std::size_t Ip = 0; Ip < CodeSize;)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > CodeSize)
				{
					return false;
				}
				if (Op == EOpcode::Call && InlineSites < ScalarInlineSitesPerPass)
				{
					const std::uint16_t CalleeIndex =
					    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
					const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
					if (CalleeIndex < Module.Functions.size())
					{
						const FBytecodeFunction& OriginalCallee = Module.Functions[CalleeIndex];
						const FBytecodeFunction* Callee = &OriginalCallee;
						if (NormalizedCalleeCount < NormalizedCallees.size() && TryNormalizeInlineReturns(OriginalCallee, NormalizedCallees[NormalizedCalleeCount]))
						{
							Callee = &NormalizedCallees[NormalizedCalleeCount++];
						}
						const bool bForwardSingleStackArgument =
						    CanForwardSingleScalarArgument(*Callee);
						std::array<std::uint16_t, 8> ForwardedParamSlots{};
						std::uint8_t ForwardedParamCount = 0;
						if (!bForwardSingleStackArgument && CanForwardDirectScalarArguments(*Callee) && ArgumentCount <= InstructionHistory.size())
						{
							const std::size_t Begin =
							    InstructionHistory.size() - ArgumentCount;
							bool bAllDirect = true;
							for (std::uint8_t Argument = 0;
							    bAllDirect && Argument < ArgumentCount; ++Argument)
							{
								const std::size_t LoadIp = InstructionHistory[Begin + Argument];
								bAllDirect = !CallerJumpTargets[LoadIp] && !ElidedCallerLoads[LoadIp] && static_cast<EOpcode>(Function.Code[LoadIp]) == EOpcode::LoadLocal;
								if (bAllDirect)
								{
									ForwardedParamSlots[Argument] = Bytecode::ReadU16LE(
									    Function.Code.data() + LoadIp + 1u);
								}
							}
							if (bAllDirect)
							{
								ForwardedParamCount = ArgumentCount;
							}
						}
						const std::size_t ExpandedSize =
						    (bForwardSingleStackArgument || ForwardedParamCount != 0u
						            ? 0u
						            : static_cast<std::size_t>(Callee->ParamCount) * 3u) +
						    Callee->Code.size() - (bForwardSingleStackArgument ? Bytecode::InstructionSize(EOpcode::LoadLocal) : 0u);
						const std::size_t SiteAdded =
						    ExpandedSize > Size ? ExpandedSize - Size : 0;
						if (Callee->Name != Function.Name && IsInlineableScalarCallee(Module, *Callee, ArgumentCount) && NextLocal + Callee->LocalCount <= std::numeric_limits<std::uint16_t>::max() && AddedBytes + SiteAdded <= ScalarInlineAddedBytesPerPass)
						{
							Expansions[Ip] = {
							    Callee,
							    static_cast<std::uint16_t>(NextLocal),
							    std::nullopt,
							    bForwardSingleStackArgument,
							    ForwardedParamSlots,
							    ForwardedParamCount};
							NextLocal += bForwardSingleStackArgument || ForwardedParamCount != 0u
							    ? 0u
							    : Callee->LocalCount;
							if (ForwardedParamCount != 0u)
							{
								const std::size_t Begin =
								    InstructionHistory.size() - ForwardedParamCount;
								for (std::size_t Argument = 0;
								    Argument < ForwardedParamCount; ++Argument)
								{
									ElidedCallerLoads[InstructionHistory[Begin + Argument]] = true;
								}
							}
							AddedBytes += SiteAdded;
							++InlineSites;
						}
					}
				}
				else if (Op == EOpcode::CallBoundMethod && InlineSites < ScalarInlineSitesPerPass && Function.bIsMethod)
				{
					const std::uint16_t CalleeIndex =
					    Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
					const std::uint8_t ArgumentCount = Function.Code[Ip + 3];
					std::size_t ReceiverIp = std::numeric_limits<std::size_t>::max();
					if (ArgumentCount == 0 && PreviousIp != std::numeric_limits<std::size_t>::max() && static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::LoadThis)
					{
						ReceiverIp = PreviousIp;
					}
					else if (ArgumentCount == 1 && PreviousIp != std::numeric_limits<std::size_t>::max() && PreviousPreviousIp != std::numeric_limits<std::size_t>::max() && static_cast<EOpcode>(Function.Code[PreviousPreviousIp]) == EOpcode::LoadThis && (static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::ConstI32 || static_cast<EOpcode>(Function.Code[PreviousIp]) == EOpcode::ConstF64))
					{
						// Keep this expansion deliberately narrow. It covers the dominant
						// `this.add(1)` scalar chains without attempting to duplicate arbitrary
						// argument evaluation or observable call expressions.
						ReceiverIp = PreviousPreviousIp;
					}
					if (ReceiverIp != std::numeric_limits<std::size_t>::max() && CalleeIndex < Module.Functions.size())
					{
						const FBytecodeFunction& Callee = Module.Functions[CalleeIndex];
						const std::size_t ExpandedSize = Callee.Code.size();
						const std::size_t SiteAdded =
						    ExpandedSize > Size ? ExpandedSize - Size : 0;
						if (Callee.Name != Function.Name && IsInlineableBoundThisCallee(Module, Function, Callee, ArgumentCount, F64ZeroConstant.has_value()) && NextLocal + Callee.LocalCount <= std::numeric_limits<std::uint16_t>::max() && AddedBytes + SiteAdded <= ScalarInlineAddedBytesPerPass)
						{
							Expansions[Ip] = {
							    &Callee,
							    static_cast<std::uint16_t>(NextLocal),
							    bF64Caller && Callee.bReturnsVoid
							        ? F64ZeroConstant
							        : std::nullopt};
							NextLocal += Callee.LocalCount;
							BoundThisReceivers[ReceiverIp] = true;
							AddedBytes += SiteAdded;
							++InlineSites;
						}
					}
				}
				PreviousPreviousIp = PreviousIp;
				PreviousIp = Ip;
				InstructionHistory.push_back(Ip);
				if (InstructionHistory.size() > 8u)
				{
					InstructionHistory.erase(InstructionHistory.begin());
				}
				Ip += Size;
			}
			if (InlineSites == 0 || CodeSize + AddedBytes > 65536)
			{
				return false;
			}

			constexpr std::size_t kInvalidOffset = std::numeric_limits<std::size_t>::max();
			std::vector<std::size_t> NewOffset(CodeSize + 1, kInvalidOffset);
			std::size_t NewSize = 0;
			for (std::size_t Ip = 0; Ip < CodeSize;)
			{
				NewOffset[Ip] = NewSize;
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Expansions[Ip].Callee)
				{
					NewSize += (Expansions[Ip].bForwardSingleStackArgument || Expansions[Ip].ForwardedParamCount != 0u
					                   ? 0u
					                   : static_cast<std::size_t>(
					                         Expansions[Ip].Callee->ParamCount) *
					                       3u) +
					    Expansions[Ip].Callee->Code.size() - (Expansions[Ip].bForwardSingleStackArgument ? Bytecode::InstructionSize(EOpcode::LoadLocal) : 0u);
				}
				else
				{
					NewSize += Size;
				}
				Ip += Size;
			}
			NewOffset[CodeSize] = NewSize;

			OutFunction = Function;
			OutFunction.Code.clear();
			OutFunction.Code.reserve(NewSize);
			OutFunction.DebugLines.assign(NewSize, {});
			OutFunction.LocalCount = static_cast<std::uint16_t>(NextLocal);
			OutFunction.LocalNames.resize(OutFunction.LocalCount);
			for (std::size_t Ip = 0; Ip < CodeSize;)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				const FInlineExpansion& Expansion = Expansions[Ip];
				if (Expansion.Callee)
				{
					const FDebugLine CallSiteLine = Ip < Function.DebugLines.size()
					    ? Function.DebugLines[Ip]
					    : FDebugLine{};
					const auto CalleeDebugLineAt = [&](const std::size_t CalleeIp)
					{
						const std::size_t End = std::min(
						    CalleeIp + 1u, Expansion.Callee->DebugLines.size());
						for (std::size_t Cursor = End; Cursor > 0u; --Cursor)
						{
							const FDebugLine& Line =
							    Expansion.Callee->DebugLines[Cursor - 1u];
							if (Line.Line != 0u)
							{
								return Line;
							}
						}
						return CallSiteLine;
					};
					if (!Expansion.bForwardSingleStackArgument && Expansion.ForwardedParamCount == 0u)
					{
						for (std::uint16_t Param = Expansion.Callee->ParamCount;
						    Param > 0; --Param)
						{
							const std::size_t StoreOffset = OutFunction.Code.size();
							OutFunction.Code.push_back(
							    static_cast<std::uint8_t>(EOpcode::StoreLocal));
							Bytecode::WriteU16LE(
							    OutFunction.Code,
							    static_cast<std::uint16_t>(
							        Expansion.LocalBase + Param - 1));
							OutFunction.DebugLines[StoreOffset] = CallSiteLine;
						}
					}
					for (std::size_t CalleeIp = 0;
					    CalleeIp < Expansion.Callee->Code.size();)
					{
						const EOpcode CalleeOp = static_cast<EOpcode>(
						    Expansion.Callee->Code[CalleeIp]);
						const std::size_t CalleeSize =
						    Bytecode::InstructionSize(CalleeOp);
						if (Expansion.bForwardSingleStackArgument && CalleeIp == 0)
						{
							CalleeIp += CalleeSize;
							continue;
						}
						const std::size_t Begin = OutFunction.Code.size();
						OutFunction.Code.insert(
						    OutFunction.Code.end(),
						    Expansion.Callee->Code.begin() + CalleeIp,
						    Expansion.Callee->Code.begin() + CalleeIp + CalleeSize);
						// Debug tables are sparse. Materialize the callee's effective
						// source position for every expanded instruction so a native
						// failure at a builtin/arithmetic opcode reports the same column
						// as an ordinary VM call instead of the function declaration.
						OutFunction.DebugLines[Begin] = CalleeDebugLineAt(CalleeIp);
						if (Expansion.F64VoidSentinelConstant && CalleeOp == EOpcode::ConstI32 && CalleeIp + 4 == Expansion.Callee->Code.size() && static_cast<EOpcode>(Expansion.Callee->Code[CalleeIp + 3]) == EOpcode::Return)
						{
							OutFunction.Code[Begin] =
							    static_cast<std::uint8_t>(EOpcode::ConstF64);
							PatchU16(
							    OutFunction.Code,
							    Begin + 1,
							    *Expansion.F64VoidSentinelConstant);
						}
						else if (CalleeOp == EOpcode::Return)
						{
							OutFunction.Code[Begin] =
							    static_cast<std::uint8_t>(EOpcode::Nop);
						}
						else if (CalleeOp == EOpcode::LoadLocal || CalleeOp == EOpcode::StoreLocal || CalleeOp == EOpcode::IncLocal || CalleeOp == EOpcode::AddLocalImm || CalleeOp == EOpcode::JmpIfLocalGeImm)
						{
							const std::uint16_t CalleeSlot = Bytecode::ReadU16LE(
							    Expansion.Callee->Code.data() + CalleeIp + 1);
							const std::uint16_t TargetSlot =
							    Expansion.ForwardedParamCount != 0u
							    ? Expansion.ForwardedParamSlots[CalleeSlot]
							    : static_cast<std::uint16_t>(
							          CalleeSlot + Expansion.LocalBase);
							PatchU16(
							    OutFunction.Code, Begin + 1, TargetSlot);
						}
						else if (CalleeOp == EOpcode::AddLocalLocal)
						{
							const auto MapLocal = [&](const std::size_t OperandOffset)
							{
								const std::uint16_t CalleeSlot = Bytecode::ReadU16LE(
								    Expansion.Callee->Code.data() + CalleeIp + OperandOffset);
								return Expansion.ForwardedParamCount != 0u
								    ? Expansion.ForwardedParamSlots[CalleeSlot]
								    : static_cast<std::uint16_t>(
								          CalleeSlot + Expansion.LocalBase);
							};
							PatchU16(
							    OutFunction.Code, Begin + 1, MapLocal(1u));
							PatchU16(
							    OutFunction.Code, Begin + 3, MapLocal(3u));
						}
						CalleeIp += CalleeSize;
					}
				}
				else
				{
					const std::size_t Begin = OutFunction.Code.size();
					OutFunction.Code.insert(
					    OutFunction.Code.end(),
					    Function.Code.begin() + Ip,
					    Function.Code.begin() + Ip + Size);
					if (Ip < Function.DebugLines.size())
					{
						OutFunction.DebugLines[Begin] = Function.DebugLines[Ip];
					}
					if (Op == EOpcode::LoadThis && BoundThisReceivers[Ip])
					{
						// The expanded callee keeps its own LoadThis instructions. Remove
						// the receiver consumed by the original CallBoundMethod so the
						// scalar emitter's pending-this accounting stays balanced.
						OutFunction.Code[Begin] = static_cast<std::uint8_t>(EOpcode::Nop);
					}
					else if (Op == EOpcode::LoadLocal && ElidedCallerLoads[Ip])
					{
						// The expanded read-only callee loads the exact caller slot directly.
						// Replace every byte with a one-byte Nop so the decoded stream keeps the
						// original three-byte width and all caller branch mapping stays valid.
						for (std::size_t Byte = 0; Byte < Size; ++Byte)
						{
							OutFunction.Code[Begin + Byte] =
							    static_cast<std::uint8_t>(EOpcode::Nop);
						}
					}
					std::size_t RelativeOffset = kInvalidOffset;
					if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse)
					{
						RelativeOffset = 1;
					}
					else if (Op == EOpcode::JmpIfLocalGeImm)
					{
						RelativeOffset = 7;
					}
					if (RelativeOffset != kInvalidOffset)
					{
						const std::int64_t OldTarget =
						    static_cast<std::int64_t>(Ip + Size) + Bytecode::ReadI32LE(Function.Code.data() + Ip + RelativeOffset);
						if (OldTarget < 0 || static_cast<std::size_t>(OldTarget) > CodeSize || NewOffset[static_cast<std::size_t>(OldTarget)] == kInvalidOffset)
						{
							return false;
						}
						const std::int64_t NewRelative =
						    static_cast<std::int64_t>(
						        NewOffset[static_cast<std::size_t>(OldTarget)]) -
						    static_cast<std::int64_t>(Begin + Size);
						if (NewRelative < std::numeric_limits<std::int32_t>::min() || NewRelative > std::numeric_limits<std::int32_t>::max())
						{
							return false;
						}
						PatchI32(
						    OutFunction.Code, Begin + RelativeOffset,
						    static_cast<std::int32_t>(NewRelative));
					}
				}
				Ip += Size;
			}
			OutInlineSites = static_cast<std::uint16_t>(InlineSites);
			return OutFunction.Code.size() == NewSize;
		}

		bool BuiltinResultTag(const EBuiltinId B, EStackTag& Out)
		{
			const Builtins::FDescriptor* const Desc = JitHost::Services().LookupBuiltin(B);
			if (!Desc)
			{
				return false;
			}
			if (Desc->bPushVoidResult)
			{
				Out = EStackTag::Any; // unused; caller checks void
				return true;
			}
			switch (B)
			{
			case EBuiltinId::ConsoleWidth:
			case EBuiltinId::ConsoleHeight:
			case EBuiltinId::ToInt:
			case EBuiltinId::ToInt8:
			case EBuiltinId::ToInt16:
			case EBuiltinId::CharCode:
			case EBuiltinId::ReadInt:
			case EBuiltinId::ReflectionInt:
			case EBuiltinId::SourceMetadataInt:
				{
					Out = EStackTag::I32;
					return true;
				}
			case EBuiltinId::ReflectionInt64:
			case EBuiltinId::SourceMetadataInt64:
			case EBuiltinId::ArrayPrepareSort:
			case EBuiltinId::Len:
			case EBuiltinId::Find:
			case EBuiltinId::ToInt64:
			case EBuiltinId::StringLastFind:
			case EBuiltinId::RandomIndex:
				{
					Out = EStackTag::I64;
					return true;
				}
			case EBuiltinId::ToUInt64:
			case EBuiltinId::SourceMetadataUInt64:
				{
					Out = EStackTag::U64;
					return true;
				}
			case EBuiltinId::ToUnsigned:
			case EBuiltinId::RandomNextUInt:
			case EBuiltinId::ToUInt8:
			case EBuiltinId::ToUInt16:
				{
					Out = EStackTag::U32;
					return true;
				}
			case EBuiltinId::ToDouble:
			case EBuiltinId::Sqrt:
			case EBuiltinId::Sin:
			case EBuiltinId::Cos:
			case EBuiltinId::Tan:
			case EBuiltinId::Floor:
			case EBuiltinId::Ceil:
			case EBuiltinId::Round:
			case EBuiltinId::Trunc:
			case EBuiltinId::Pow:
			case EBuiltinId::Exp:
			case EBuiltinId::Log:
			case EBuiltinId::Log2:
			case EBuiltinId::Asin:
			case EBuiltinId::Acos:
			case EBuiltinId::Atan2:
			case EBuiltinId::ReadDouble:
			case EBuiltinId::Now:
			case EBuiltinId::SteadyNow:
			case EBuiltinId::SourceMetadataFloat:
				{
					Out = EStackTag::F64;
					return true;
				}
			case EBuiltinId::ToFloat:
				{
					Out = EStackTag::F32;
					return true;
				}
			case EBuiltinId::ToString:
			case EBuiltinId::ReadFile:
			case EBuiltinId::Substr:
			case EBuiltinId::Join:
			case EBuiltinId::ToLower:
			case EBuiltinId::ToUpper:
			case EBuiltinId::Format:
			case EBuiltinId::FormatPrintf:
			case EBuiltinId::GetEnv:
			case EBuiltinId::Cwd:
			case EBuiltinId::ReadLine:
			case EBuiltinId::ReadToken:
			case EBuiltinId::ReadString:
			case EBuiltinId::ReflectionString:
			case EBuiltinId::SourceMetadataString:
			case EBuiltinId::StringRepeat:
			case EBuiltinId::StringReplace:
			case EBuiltinId::StringTrim:
			case EBuiltinId::StringDecodeUtf8:
			case EBuiltinId::PathRoot:
			case EBuiltinId::PathNormalize:
				{
					Out = EStackTag::String;
					return true;
				}
			case EBuiltinId::ToWString:
				{
					Out = EStackTag::WString;
					return true;
				}
			case EBuiltinId::ReadBool:
			case EBuiltinId::Exists:
			case EBuiltinId::DeleteFile:
			case EBuiltinId::ConsoleClear:
			case EBuiltinId::ConsoleIsInteractive:
			case EBuiltinId::ConsoleSupportsColor:
			case EBuiltinId::ConsoleSupportsCursor:
			case EBuiltinId::ConsoleSupportsClear:
			case EBuiltinId::ConsoleIsOutputTerminal:
			case EBuiltinId::ConsoleSetColor:
			case EBuiltinId::ConsoleResetStyle:
			case EBuiltinId::ConsoleClearLine:
			case EBuiltinId::ConsoleMoveCursor:
			case EBuiltinId::ConsoleSetCursorVisible:
			case EBuiltinId::IsFinite:
			case EBuiltinId::IsNan:
			case EBuiltinId::ReflectionBool:
			case EBuiltinId::SourceMetadataBool:
			case EBuiltinId::ScalarTryParse:
			case EBuiltinId::StringStartsWith:
			case EBuiltinId::StringEndsWith:
			case EBuiltinId::StringIsValidUtf8:
			case EBuiltinId::PathIsAbsolute:
			case EBuiltinId::MutexTryLock:
			case EBuiltinId::MutexLockFor:
			case EBuiltinId::SemaphoreTryAcquire:
			case EBuiltinId::SemaphoreAcquireFor:
			case EBuiltinId::TypedTaskValid:
			case EBuiltinId::TypedTaskDone:
				{
					Out = EStackTag::Bool;
					return true;
				}
			case EBuiltinId::IsFile:
				{
					Out = EStackTag::Bool;
					return true;
				}
			case EBuiltinId::IsDirectory:
				{
					Out = EStackTag::Bool;
					return true;
				}
			case EBuiltinId::RemoveDir:
				{
					Out = EStackTag::Bool;
					return true;
				}
			case EBuiltinId::CancelRequested:
				{
					Out = EStackTag::Bool;
					return true;
				}
			case EBuiltinId::Spawn:
				{
					Out = EStackTag::I32;
					return true;
				}
			default:
				{
					Out = EStackTag::Any;
					return true;
				}
			}
		}

		bool ValidateCfg(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::vector<EStackTag>& ParamTags,
		    const EStackTag ReturnTag,
		    const bool bVoidReturn,
		    std::uint16_t* const OutMaxStackSlots,
		    std::uint16_t* const OutMaxBoxedStackSlots,
		    std::vector<std::size_t>* const OutTextAddOffsets,
		    std::vector<FScalarStackPoint>* const OutStackPoints,
		    std::vector<std::size_t>* const OutUnsupportedTextFormatOffsets)
		{
			if (OutUnsupportedTextFormatOffsets)
			{
				OutUnsupportedTextFormatOffsets->clear();
			}
			if (OutTextAddOffsets)
			{
				OutTextAddOffsets->clear();
			}
			if (OutStackPoints)
			{
				OutStackPoints->clear();
			}
			if (OutMaxStackSlots)
			{
				*OutMaxStackSlots = 0;
			}
			if (OutMaxBoxedStackSlots)
			{
				*OutMaxBoxedStackSlots = 0;
			}
			if (Function.ParamCount != ParamTags.size() || Function.LocalCount < Function.ParamCount || Function.Code.size() > 65536)
			{
				return false;
			}
			// Empty void bodies are valid (fall off immediately); non-void needs code.
			if (Function.Code.empty())
			{
				return bVoidReturn;
			}
			struct FState
			{
				std::vector<EStackTag> Stack;
				std::vector<EStackTag> Locals;
			};
			std::size_t MaxStackSlots = 0;
			std::size_t MaxBoxedStackSlots = 0;
			auto ObserveStack = [&](const FState& State)
			{
				MaxStackSlots = std::max(MaxStackSlots, State.Stack.size());
				const std::size_t BoxedSlots = static_cast<std::size_t>(std::count_if(
				    State.Stack.begin(), State.Stack.end(), [](const EStackTag Tag)
				{
					return Tag == EStackTag::Any || Tag == EStackTag::String || Tag == EStackTag::WString;
				}));
				MaxBoxedStackSlots = std::max(MaxBoxedStackSlots, BoxedSlots);
			};
			const auto& Code = Function.Code;
			const std::size_t CodeSize = Code.size();
			std::vector<std::optional<FState>> Entries(CodeSize);
			std::queue<std::size_t> Work;
			FState Initial;
			Initial.Locals.assign(Function.LocalCount, EStackTag::Any);
			for (std::size_t I = 0; I < ParamTags.size(); ++I)
			{
				Initial.Locals[I] = ParamTags[I];
			}
			Entries[0] = Initial;
			Work.push(0);
			bool bSawReturn = false;
			std::size_t Budget = CodeSize * 8 + 64;
			auto JoinTag = [](const EStackTag A, const EStackTag B) -> std::optional<EStackTag>
			{
				if (A == B)
				{
					return A;
				}
				if (A == EStackTag::Any || B == EStackTag::Any)
				{
					return EStackTag::Any;
				}
				// Branch joins that only differ by numeric width stay on the linear path.
				if (IsNumeric(A) && IsNumeric(B))
				{
					return EStackTag::Any;
				}
				if ((A == EStackTag::Bool || A == EStackTag::I32 || A == EStackTag::U32) && (B == EStackTag::Bool || B == EStackTag::I32 || B == EStackTag::U32))
				{
					return EStackTag::Any;
				}
				return std::nullopt;
			};
			auto Enqueue = [&](const std::size_t Target, const FState& State)
			{
				if (Target >= CodeSize)
				{
					return false;
				}
				ObserveStack(State);
				if (!Entries[Target])
				{
					Entries[Target] = State;
					Work.push(Target);
					return true;
				}
				FState& Existing = *Entries[Target];
				if (Existing.Stack.size() != State.Stack.size() || Existing.Locals.size() != State.Locals.size())
				{
					return false;
				}
				bool bChanged = false;
				for (std::size_t I = 0; I < Existing.Stack.size(); ++I)
				{
					const auto Joined = JoinTag(Existing.Stack[I], State.Stack[I]);
					if (!Joined)
					{
						return false;
					}
					if (*Joined != Existing.Stack[I])
					{
						Existing.Stack[I] = *Joined;
						bChanged = true;
					}
				}
				for (std::size_t I = 0; I < Existing.Locals.size(); ++I)
				{
					const auto Joined = JoinTag(Existing.Locals[I], State.Locals[I]);
					if (!Joined)
					{
						return false;
					}
					if (*Joined != Existing.Locals[I])
					{
						Existing.Locals[I] = *Joined;
						bChanged = true;
					}
				}
				if (bChanged)
				{
					Work.push(Target);
				}
				return true;
			};
			auto Need = [](const FState& S, const std::size_t N)
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
				FState State = *Entries[Ip];
				ObserveStack(State);
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
						if (K == EConstantKind::I8 || K == EConstantKind::I16 || K == EConstantKind::I32)
						{
							State.Stack.push_back(EStackTag::I32);
						}
						else if (K == EConstantKind::U8 || K == EConstantKind::U16 || K == EConstantKind::U32)
						{
							State.Stack.push_back(EStackTag::U32);
						}
						else if (K == EConstantKind::I64)
						{
							State.Stack.push_back(EStackTag::I64);
						}
						else if (K == EConstantKind::U64)
						{
							State.Stack.push_back(EStackTag::U64);
						}
						else if (K == EConstantKind::Enum)
						{
							State.Stack.push_back(EStackTag::Enum);
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
						if (I >= Module.Constants.size() || (Module.Constants[I].Kind != EConstantKind::F64 && Module.Constants[I].Kind != EConstantKind::F32))
						{
							return false;
						}
						State.Stack.push_back(Module.Constants[I].Kind == EConstantKind::F32
						        ? EStackTag::F32
						        : EStackTag::F64);
						break;
					}
				case EOpcode::ConstBool:
					{
						State.Stack.push_back(EStackTag::Bool);
						break;
					}
				case EOpcode::ConstStr:
					{
						const auto I = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (I >= Module.Constants.size())
						{
							return false;
						}
						const auto K = Module.Constants[I].Kind;
						if (K == EConstantKind::Str)
						{
							State.Stack.push_back(EStackTag::String);
						}
						else if (K == EConstantKind::WStr)
						{
							State.Stack.push_back(EStackTag::WString);
						}
						else
						{
							return false;
						}
						break;
					}
				case EOpcode::LoadLocal:
				case EOpcode::TakeLocal:
					{
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (S >= State.Locals.size())
						{
							return false;
						}
						State.Stack.push_back(State.Locals[S]);
						if (Op == EOpcode::TakeLocal)
						{
							State.Locals[S] = EStackTag::I32;
						}
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
						EStackTag T = EStackTag::Any;
						if (S >= Module.GlobalTypeNames.size() || Module.GlobalTypeNames[S].empty() || !TagFromTypeName(Module.GlobalTypeNames[S], T))
						{
							return false;
						}
						State.Stack.push_back(T);
						break;
					}
				case EOpcode::StoreGlobal:
					{
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						EStackTag T = EStackTag::Any;
						if (S >= Module.GlobalTypeNames.size() || Module.GlobalTypeNames[S].empty() || !TagFromTypeName(Module.GlobalTypeNames[S], T) || !Need(State, 1) || !TagsCompatible(State.Stack.back(), T))
						{
							return false;
						}
						State.Stack.pop_back();
						break;
					}
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
					{
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (S >= State.Locals.size() || !IsNumeric(State.Locals[S]))
						{
							return false;
						}
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						const auto D = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 3);
						if (D >= State.Locals.size() || S >= State.Locals.size() || (!IsNumeric(State.Locals[D]) && State.Locals[D] != EStackTag::String && State.Locals[D] != EStackTag::WString) || !TagsCompatible(State.Locals[D], State.Locals[S]))
						{
							return false;
						}
						if (OutTextAddOffsets && (State.Locals[D] == EStackTag::String || State.Locals[D] == EStackTag::WString))
						{
							OutTextAddOffsets->push_back(Ip);
						}
						break;
					}
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
					{
						if (!Need(State, 2))
						{
							return false;
						}
						const EStackTag R = State.Stack.back();
						const EStackTag L = State.Stack[State.Stack.size() - 2];
						if (Op == EOpcode::Add && (L == EStackTag::String || R == EStackTag::String || L == EStackTag::WString || R == EStackTag::WString || L == EStackTag::Any || R == EStackTag::Any))
						{
							if (OutTextAddOffsets && (L == EStackTag::String || R == EStackTag::String || L == EStackTag::WString || R == EStackTag::WString))
							{
								OutTextAddOffsets->push_back(Ip);
							}
							State.Stack.pop_back();
							State.Stack.back() = (L == EStackTag::WString || R == EStackTag::WString)
							    ? EStackTag::WString
							    : (L == EStackTag::String || R == EStackTag::String
							              ? EStackTag::String
							              : EStackTag::Any);
							break;
						}
						if (!IsNumeric(L) || !IsNumeric(R) || (Op == EOpcode::Mod && (L == EStackTag::F32 || L == EStackTag::F64)))
						{
							return false;
						}
						State.Stack.pop_back();
						if (L == EStackTag::Any || R == EStackTag::Any)
						{
							State.Stack.back() = EStackTag::Any;
						}
						else if (L == EStackTag::F64 || R == EStackTag::F64)
						{
							State.Stack.back() = EStackTag::F64;
						}
						else if (L == EStackTag::F32 || R == EStackTag::F32)
						{
							State.Stack.back() = EStackTag::F32;
						}
						else if (L == EStackTag::U64 || R == EStackTag::U64)
						{
							State.Stack.back() = EStackTag::U64;
						}
						else if (L == EStackTag::I64 || R == EStackTag::I64)
						{
							State.Stack.back() = EStackTag::I64;
						}
						else if (L == EStackTag::U32 || R == EStackTag::U32)
						{
							State.Stack.back() = EStackTag::U32;
						}
						else
						{
							State.Stack.back() = EStackTag::I32;
						}
						break;
					}
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						if (!Need(State, 2) || !IsInteger(State.Stack.back()) || !IsInteger(State.Stack[State.Stack.size() - 2]))
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
						State.Stack.pop_back();
						State.Stack.back() = EStackTag::Bool;
						break;
					}
				case EOpcode::Lt:
				case EOpcode::Le:
				case EOpcode::Gt:
				case EOpcode::Ge:
					{
						if (!Need(State, 2))
						{
							return false;
						}
						State.Stack.pop_back();
						State.Stack.back() = EStackTag::Bool;
						break;
					}
				case EOpcode::Neg:
					{
						if (!Need(State, 1) || !IsNumeric(State.Stack.back()))
						{
							return false;
						}
						break;
					}
				case EOpcode::BitNot:
					{
						if (!Need(State, 1) || !IsInteger(State.Stack.back()))
						{
							return false;
						}
						break;
					}
				case EOpcode::Not:
					{
						if (!Need(State, 1) || (State.Stack.back() != EStackTag::Bool && State.Stack.back() != EStackTag::Any))
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
						const auto& F = Module.Functions[C];
						std::vector<EStackTag> P;
						EStackTag R = EStackTag::Any;
						bool V = false;
						if (F.bIsMethod)
						{
							return false;
						}
						if (!F.ClosureTypeName.empty())
						{
							if (!ParseClosureType(F.ClosureTypeName, P, R, V) || F.bReturnsVoid != V || F.ParamCount != N || P.size() != N || !Need(State, N))
							{
								return false;
							}
							for (std::size_t I = 0; I < N; ++I)
							{
								if (!TagsCompatible(State.Stack[State.Stack.size() - N + I], P[I]))
								{
									return false;
								}
							}
						}
						else if (F.ParamCount != N || !Need(State, N))
						{
							return false;
						}
						else
						{
							V = F.bReturnsVoid;
							R = EStackTag::Any;
						}
						State.Stack.resize(State.Stack.size() - N);
						// Void calls leave a Pop-discarded sentinel (same as the VM).
						State.Stack.push_back(V ? EStackTag::Any : R);
						break;
					}
				case EOpcode::CallBoundMethod:
					{
						const auto C = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto N = Code[Ip + 3];
						if (C >= Module.Functions.size() || !Need(State, static_cast<std::size_t>(N) + 1) || Module.Functions[C].ParamCount != N)
						{
							return false;
						}
						State.Stack.resize(State.Stack.size() - static_cast<std::size_t>(N) - 1);
						// Void bound calls also leave a Pop-discarded sentinel.
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::NewObject:
				case EOpcode::NewUninitializedObject:
					{
						const auto T = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (T >= Module.Types.size())
						{
							return false;
						}
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::ConstNull:
					{
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::LoadThis:
					{
						if (!Function.bIsMethod)
						{
							return false;
						}
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::LoadThisField:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (!Function.bIsMethod || Name >= Module.StringPool.size())
						{
							return false;
						}
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (!Function.bIsMethod || Name >= Module.StringPool.size() || !Need(State, 1))
						{
							return false;
						}
						State.Stack.pop_back();
						break;
					}
				case EOpcode::LoadField:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (Name >= Module.StringPool.size() || !Need(State, 1))
						{
							return false;
						}
						State.Stack.back() = EStackTag::Any;
						break;
					}
				case EOpcode::LoadFieldKeep:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (Name >= Module.StringPool.size() || !Need(State, 1))
						{
							return false;
						}
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::StoreField:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (Name >= Module.StringPool.size() || !Need(State, 2))
						{
							return false;
						}
						State.Stack.pop_back(); // value
						State.Stack.pop_back(); // object
						State.Stack.push_back(EStackTag::Any); // stored value
						break;
					}
				case EOpcode::IncField:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (Name >= Module.StringPool.size() || !Need(State, 1))
						{
							return false;
						}
						State.Stack.pop_back();
						break;
					}
				case EOpcode::CloneStruct:
					{
						if (!Need(State, 1))
						{
							return false;
						}
						break;
					}
				case EOpcode::NewDynamicArray:
				case EOpcode::NewMap:
				case EOpcode::NewSet:
				case EOpcode::NewQueue:
				case EOpcode::NewStack:
				case EOpcode::NewOrderedMap:
				case EOpcode::NewDeque:
				case EOpcode::NewChannel:
				case EOpcode::NewNativeArray:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (Name >= Module.StringPool.size())
						{
							return false;
						}
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::GetIndexCopy:
					{
						if (!Need(State, 2))
						{
							return false;
						}
						State.Stack.pop_back();
						State.Stack.back() = EStackTag::Any;
						break;
					}
				case EOpcode::SetIndex:
					{
						if (!Need(State, 3))
						{
							return false;
						}
						State.Stack.pop_back();
						State.Stack.pop_back();
						State.Stack.back() = EStackTag::Any;
						break;
					}
				case EOpcode::CallMethod:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto N = Code[Ip + 3];
						if (Name >= Module.StringPool.size() || !Need(State, static_cast<std::size_t>(N) + 1))
						{
							return false;
						}
						State.Stack.resize(State.Stack.size() - static_cast<std::size_t>(N) - 1);
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::CallNative:
					{
						const auto N = Code[Ip + 3];
						if (!Need(State, N))
						{
							return false;
						}
						State.Stack.resize(State.Stack.size() - N);
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::MakeClosure:
					{
						const auto C = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						const auto N = Code[Ip + 3];
						if (C >= Module.Functions.size() || !Need(State, N))
						{
							return false;
						}
						State.Stack.resize(State.Stack.size() - N);
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::CallIndirect:
					{
						const auto N = Code[Ip + 1];
						if (!Need(State, static_cast<std::size_t>(N) + 1))
						{
							return false;
						}
						State.Stack.resize(State.Stack.size() - static_cast<std::size_t>(N) - 1);
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::MakeLocalRef:
				case EOpcode::MakeGlobalRef:
				case EOpcode::MakeThisFieldRef:
					{
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (Op == EOpcode::MakeLocalRef && S >= State.Locals.size())
						{
							return false;
						}
						if (Op == EOpcode::MakeThisFieldRef && (!Function.bIsMethod || S >= Module.StringPool.size()))
						{
							return false;
						}
						if (Op == EOpcode::MakeGlobalRef && S >= Module.GlobalTypeNames.size())
						{
							return false;
						}
						State.Stack.push_back(EStackTag::Any);
						break;
					}
				case EOpcode::MakeFieldRef:
					{
						const auto Name = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (Name >= Module.StringPool.size() || !Need(State, 1))
						{
							return false;
						}
						State.Stack.back() = EStackTag::Any;
						break;
					}
				case EOpcode::GetIndexRef:
				case EOpcode::GetIndexPathRef:
				case EOpcode::GetMapEntryRef:
					{
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						if (S >= State.Locals.size() || !Need(State, 1))
						{
							return false;
						}
						State.Stack.back() = EStackTag::Any;
						break;
					}
				case EOpcode::BindLocal:
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
				case EOpcode::CallBuiltin:
					{
						const auto B = static_cast<EBuiltinId>(Code[Ip + 1]);
						const auto N = Code[Ip + 2];
						const Builtins::FDescriptor* const Desc = JitHost::Services().LookupBuiltin(B);
						if (!Desc || !Desc->AcceptsArgCount(N) || !Need(State, N))
						{
							return false;
						}
						State.Stack.resize(State.Stack.size() - N);
						if (!Desc->bPushVoidResult)
						{
							EStackTag ResultTag = EStackTag::Any;
							if (!BuiltinResultTag(B, ResultTag))
							{
								return false;
							}
							State.Stack.push_back(ResultTag);
						}
						break;
					}
				case EOpcode::Jmp:
					{
						std::size_t T = 0;
						if (!IsJumpTargetInRange(AfterIp, Bytecode::ReadI32LE(Code.data() + Ip + 1), CodeSize, T) || !Enqueue(T, State))
						{
							return false;
						}
						continue;
					}
				case EOpcode::JmpIfFalse:
					{
						std::size_t T = 0;
						if (!Need(State, 1) || (State.Stack.back() != EStackTag::Bool && State.Stack.back() != EStackTag::Any))
						{
							return false;
						}
						State.Stack.pop_back();
						if (!IsJumpTargetInRange(AfterIp, Bytecode::ReadI32LE(Code.data() + Ip + 1), CodeSize, T) || !Enqueue(T, State) || !Enqueue(AfterIp, State))
						{
							return false;
						}
						continue;
					}
				case EOpcode::JmpIfLocalGeImm:
					{
						const auto S = Bytecode::ReadU16LE(Code.data() + Ip + 1);
						std::size_t T = 0;
						if (S >= State.Locals.size() || !IsNumeric(State.Locals[S]) || !IsJumpTargetInRange(AfterIp, Bytecode::ReadI32LE(Code.data() + Ip + 7), CodeSize, T) || !Enqueue(T, State) || !Enqueue(AfterIp, State))
						{
							return false;
						}
						continue;
					}
				case EOpcode::Throw:
					{
						if (!Need(State, 1))
						{
							return false;
						}
						State.Stack.pop_back();
						for (const FBytecodeExceptionRegion& Region : Function.ExceptionRegions)
						{
							if (Ip < Region.BeginOffset || Ip >= Region.EndOffset)
							{
								continue;
							}
							for (const FBytecodeCatchHandler& Handler : Region.Catches)
							{
								if (!Enqueue(Handler.HandlerOffset, State))
								{
									return false;
								}
							}
						}
						continue;
					}
				case EOpcode::Return:
					{
						if (bVoidReturn)
						{
							if (State.Stack.size() > 1)
							{
								return false;
							}
						}
						else if (State.Stack.size() != 1 || !TagsCompatible(State.Stack.back(), ReturnTag))
						{
							return false;
						}
						bSawReturn = true;
						continue;
					}
				default:
					return false;
				}
				// Void functions may fall off the end (no trailing Return); the VM accepts that.
				if (AfterIp >= CodeSize)
				{
					if (bVoidReturn && State.Stack.size() <= 1)
					{
						bSawReturn = true;
						continue;
					}
					return false;
				}
				if (!Enqueue(AfterIp, State))
				{
					return false;
				}
			}
			if (!bSawReturn)
			{
				return false;
			}
			if (OutMaxStackSlots)
			{
				*OutMaxStackSlots = static_cast<std::uint16_t>(std::min<std::size_t>(
				    MaxStackSlots, std::numeric_limits<std::uint16_t>::max()));
			}
			if (OutMaxBoxedStackSlots)
			{
				*OutMaxBoxedStackSlots = static_cast<std::uint16_t>(std::min<std::size_t>(
				    MaxBoxedStackSlots, std::numeric_limits<std::uint16_t>::max()));
			}
			if (OutStackPoints)
			{
				OutStackPoints->resize(CodeSize);
				for (std::size_t Ip = 0; Ip < CodeSize; ++Ip)
				{
					if (const auto& State = Entries[Ip])
					{
						(*OutStackPoints)[Ip] = {State->Stack.size(),
						    State->Stack.empty() ? EStackTag::Any : State->Stack.back()};
					}
				}
			}
			if (OutUnsupportedTextFormatOffsets)
			{
				// Only a converged, concrete input kind can rule out the native
				// integer formatter. Unknown container reads keep runtime admission.
				for (std::size_t Ip = 0; Ip < CodeSize; ++Ip)
				{
					const auto& State = Entries[Ip];
					if (State && !State->Stack.empty() &&
						static_cast<EOpcode>(Code[Ip]) == EOpcode::CallBuiltin &&
						static_cast<EBuiltinId>(Code[Ip + 1u]) == EBuiltinId::ToString && Code[Ip + 2u] == 1u &&
						State->Stack.back() != EStackTag::Any && !IsInteger(State->Stack.back()))
					{
						OutUnsupportedTextFormatOffsets->push_back(Ip);
					}
				}
			}
			return true;
		}

		bool TryBuildTransitivelyInlinedScalarFunction(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::vector<EStackTag>& ParamTags,
		    const EStackTag ReturnTag,
		    const bool bVoidReturn,
		    const std::size_t MaxPasses,
		    const std::size_t MaxAdditionalBytes,
		    const std::uint16_t MaxInlineSites,
		    FBytecodeFunction& OutFunction,
		    std::uint16_t& OutInlineSites)
		{
			// Inlining one level exposes calls that were nested in the callee.  The
			// old caller ran exactly two passes, which left otherwise eligible three-
			// and four-level scalar chains behind a VM/JIT bridge.  Iterate to a
			// bounded fixed point instead.  Every candidate is re-verified as a full
			// bytecode CFG before it becomes the source of the next pass, and both
			// code growth and site count are global budgets rather than per-pass
			// loopholes.  Recursion remains excluded by the ordinary inliner.
			OutInlineSites = 0;
			const std::size_t OriginalBytes = Function.Code.size();
			const FBytecodeFunction* Current = &Function;
			bool bChanged = false;
			for (std::size_t Pass = 0; Pass < MaxPasses; ++Pass)
			{
				FBytecodeFunction Candidate;
				std::uint16_t PassSites = 0;
				if (!TryBuildInlinedScalarFunction(
				        Module, *Current, Candidate, PassSites) ||
				    PassSites == 0)
				{
					break;
				}
				if (PassSites > MaxInlineSites - OutInlineSites || Candidate.Code.size() > OriginalBytes + MaxAdditionalBytes || !ValidateCfg(Module, Candidate, ParamTags, ReturnTag, bVoidReturn))
				{
					break;
				}
				OutFunction = std::move(Candidate);
				Current = &OutFunction;
				OutInlineSites = static_cast<std::uint16_t>(
				    OutInlineSites + PassSites);
				bChanged = true;
			}
			return bChanged;
		}
