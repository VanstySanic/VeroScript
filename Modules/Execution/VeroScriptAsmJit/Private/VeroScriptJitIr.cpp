// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitHost.h"
#include "VeroScriptJitIr.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptMethodKey.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript::AsmJit::JitIr
{
	namespace
	{
		bool ParseValueType(const std::string_view Name, EValueType& OutType)
		{
			if (Name == "int8" || Name == "char")
			{
				OutType = EValueType::I8;
				return true;
			}
			if (Name == "uint8" || Name == "unsigned char")
			{
				OutType = EValueType::U8;
				return true;
			}
			if (Name == "int16" || Name == "short")
			{
				OutType = EValueType::I16;
				return true;
			}
			if (Name == "uint16" || Name == "unsigned short")
			{
				OutType = EValueType::U16;
				return true;
			}
			if (Name == "int")
			{
				OutType = EValueType::I32;
				return true;
			}
			if (Name == "uint" || Name == "unsigned" || Name == "unsigned int")
			{
				OutType = EValueType::U32;
				return true;
			}
			if (Name == "double")
			{
				OutType = EValueType::F64;
				return true;
			}
			if (Name == "float")
			{
				OutType = EValueType::F32;
				return true;
			}
			if (Name == "int64" || Name == "long")
			{
				OutType = EValueType::I64;
				return true;
			}
			if (Name == "uint64" || Name == "unsigned long")
			{
				OutType = EValueType::U64;
				return true;
			}
			if (Name == "bool")
			{
				OutType = EValueType::Bool;
				return true;
			}
			if (Name == "string")
			{
				OutType = EValueType::String;
				return true;
			}
			if (Name == "wstring")
			{
				OutType = EValueType::WString;
				return true;
			}
			if (!Name.empty())
			{
				OutType = EValueType::Any;
				return true;
			}
			return false;
		}

		std::string_view Trim(std::string_view Text)
		{
			while (!Text.empty() && (Text.front() == ' ' || Text.front() == '\t'))
			{
				Text.remove_prefix(1);
			}
			while (!Text.empty() && (Text.back() == ' ' || Text.back() == '\t'))
			{
				Text.remove_suffix(1);
			}
			return Text;
		}

		bool IsConditionalJump(const EOpcode Opcode)
		{
			return Opcode == EOpcode::JmpIfFalse || Opcode == EOpcode::JmpIfLocalGeImm;
		}

		bool IsJump(const EOpcode Opcode)
		{
			return Opcode == EOpcode::Jmp || IsConditionalJump(Opcode);
		}

		bool ReadJumpTarget(
		    const std::vector<std::uint8_t>& Code,
		    const FInstruction& Instruction,
		    std::size_t& OutTarget)
		{
			const std::size_t OperandOffset = Instruction.Offset + 1;
			if (Instruction.Opcode == EOpcode::JmpIfLocalGeImm)
			{
				if (OperandOffset + 10 > Code.size())
				{
					return false;
				}
				const std::int32_t Relative = Bytecode::ReadI32LE(Code.data() + OperandOffset + 6);
				const std::int64_t Next =
				    static_cast<std::int64_t>(Instruction.Offset + Instruction.Size) + Relative;
				if (Next < 0 || static_cast<std::size_t>(Next) >= Code.size())
				{
					return false;
				}
				OutTarget = static_cast<std::size_t>(Next);
				return true;
			}

			if (OperandOffset + 4 > Code.size())
			{
				return false;
			}
			const std::int32_t Relative = Bytecode::ReadI32LE(Code.data() + OperandOffset);
			const std::int64_t Next =
			    static_cast<std::int64_t>(Instruction.Offset + Instruction.Size) + Relative;
			if (Next < 0 || static_cast<std::size_t>(Next) >= Code.size())
			{
				return false;
			}
			OutTarget = static_cast<std::size_t>(Next);
			return true;
		}

		void SetError(std::string* const OutError, const char* const Message)
		{
			if (OutError)
			{
				*OutError = Message;
			}
		}

		bool IsNumeric(const EValueType Type)
		{
			return Type == EValueType::I8 || Type == EValueType::U8 || Type == EValueType::I16 || Type == EValueType::U16 || Type == EValueType::I32 || Type == EValueType::U32 || Type == EValueType::I64 || Type == EValueType::U64 || Type == EValueType::F32 || Type == EValueType::F64 || Type == EValueType::Any;
		}

		EValueType JoinType(const EValueType A, const EValueType B)
		{
			if (A == B)
			{
				return A;
			}
			// Every predecessor passed here is reachable. Unknown therefore means
			// that the slot is not initialized on that path, rather than an absent
			// data-flow fact. Joining it with a concrete value must remain boxed;
			// otherwise a hand-authored module could enter native code with an empty
			// FValue on one branch and a scalar on another.
			if (A == EValueType::Unknown || B == EValueType::Unknown)
			{
				return EValueType::Any;
			}
			if (A == EValueType::Any || B == EValueType::Any)
			{
				return EValueType::Any;
			}
			if (IsNumeric(A) && IsNumeric(B))
			{
				return EValueType::Any;
			}
			if ((A == EValueType::Bool || A == EValueType::I32 || A == EValueType::U32) && (B == EValueType::Bool || B == EValueType::I32 || B == EValueType::U32))
			{
				return EValueType::Any;
			}
			return EValueType::Any;
		}

		EValueType TypeFromName(const std::string_view Name)
		{
			EValueType Type = EValueType::Unknown;
			return ParseValueType(Name, Type) ? Type : EValueType::Any;
		}

		bool FindMethodFieldType(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::uint16_t NameIndex,
		    EValueType& OutType)
		{
			if (!Function.bIsMethod || Function.OwnerTypeIndex >= Module.Types.size() || NameIndex >= Module.StringPool.size())
			{
				return false;
			}
			const std::string& FieldName = Module.StringPool[NameIndex];
			std::vector<std::uint16_t> Pending{Function.OwnerTypeIndex};
			std::vector<bool> Visited(Module.Types.size(), false);
			while (!Pending.empty())
			{
				const std::uint16_t TypeIndex = Pending.back();
				Pending.pop_back();
				if (TypeIndex >= Module.Types.size() || Visited[TypeIndex])
				{
					continue;
				}
				Visited[TypeIndex] = true;
				const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
				for (const FBytecodeFieldDesc& Field : Type.Fields)
				{
					if (Field.Name == FieldName)
					{
						OutType = TypeFromName(Field.TypeName);
						return OutType != EValueType::Unknown;
					}
				}
				Pending.insert(Pending.end(), Type.BaseTypeIndices.begin(), Type.BaseTypeIndices.end());
			}
			return false;
		}

		bool PopTypes(FTypeState& State, const std::size_t Count)
		{
			if (State.Stack.size() < Count)
			{
				return false;
			}
			State.Stack.resize(State.Stack.size() - Count);
			return true;
		}

		bool PopBinaryTypes(
		    FTypeState& State,
		    EValueType& OutLeft,
		    EValueType& OutRight)
		{
			if (State.Stack.size() < 2)
			{
				return false;
			}
			OutRight = State.Stack.back();
			State.Stack.pop_back();
			OutLeft = State.Stack.back();
			State.Stack.pop_back();
			return true;
		}

		EValueType NumericResultType(const EValueType Left, const EValueType Right)
		{
			if (!IsNumeric(Left) || !IsNumeric(Right) || Left == EValueType::Any || Right == EValueType::Any)
			{
				return EValueType::Any;
			}
			if (Left == EValueType::F64 || Right == EValueType::F64)
			{
				return EValueType::F64;
			}
			if (Left == EValueType::F32 || Right == EValueType::F32)
			{
				return EValueType::F32;
			}
			if (Left == EValueType::U64 || Right == EValueType::U64)
			{
				return EValueType::U64;
			}
			if (Left == EValueType::I64 || Right == EValueType::I64)
			{
				return EValueType::I64;
			}
			if (Left == EValueType::U32 || Right == EValueType::U32 || Left == EValueType::U8 || Right == EValueType::U8 || Left == EValueType::U16 || Right == EValueType::U16)
			{
				return EValueType::U32;
			}
			return EValueType::I32;
		}

		EValueType AddResultType(const EValueType Left, const EValueType Right)
		{
			if (Left == EValueType::String || Right == EValueType::String)
			{
				return EValueType::String;
			}
			if (Left == EValueType::WString || Right == EValueType::WString)
			{
				return EValueType::WString;
			}
			return NumericResultType(Left, Right);
		}

		bool ReadU16Operand(
		    const std::vector<std::uint8_t>& Code,
		    const FInstruction& Instruction,
		    const std::size_t RelativeOffset,
		    std::uint16_t& OutValue)
		{
			const std::size_t Offset = Instruction.Offset + RelativeOffset;
			if (Offset + 2 > Code.size())
			{
				return false;
			}
			OutValue = Bytecode::ReadU16LE(Code.data() + Offset);
			return true;
		}

		struct FLocalEffects
		{
			std::array<std::uint16_t, 2> Reads{0xFFFFu, 0xFFFFu};
			std::uint16_t Write = 0xFFFFu;
			std::uint16_t Escapes = 0xFFFFu;
		};

		FLocalEffects DecodeLocalEffects(
		    const FBytecodeFunction& Function,
		    const FInstruction& Instruction)
		{
			FLocalEffects Result;
			std::uint16_t First = 0;
			const bool bHasFirst = ReadU16Operand(
			                           Function.Code, Instruction, 1, First) &&
			    First < Function.LocalCount;
			switch (Instruction.Opcode)
			{
			case EOpcode::LoadLocal:
			case EOpcode::JmpIfLocalGeImm:
				{
					if (bHasFirst)
					{
						Result.Reads[0] = First;
					}
					break;
				}
			case EOpcode::TakeLocal:
				{
					if (bHasFirst)
					{
						Result.Reads[0] = First;
						Result.Write = First;
					}
					break;
				}
			case EOpcode::StoreLocal:
			case EOpcode::BindLocal:
				{
					if (bHasFirst)
					{
						Result.Write = First;
					}
					break;
				}
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
				{
					if (bHasFirst)
					{
						Result.Reads[0] = First;
						Result.Write = First;
					}
					break;
				}
			case EOpcode::AddLocalLocal:
				{
					if (bHasFirst)
					{
						Result.Reads[0] = First;
						Result.Write = First;
						std::uint16_t Second = 0;
						if (ReadU16Operand(Function.Code, Instruction, 3, Second) && Second < Function.LocalCount)
						{
							Result.Reads[1] = Second;
						}
					}
					break;
				}
			case EOpcode::MakeLocalRef:
			case EOpcode::GetIndexRef:
			case EOpcode::GetIndexPathRef:
			case EOpcode::GetMapEntryRef:
				{
					if (bHasFirst)
					{
						Result.Reads[0] = First;
						Result.Escapes = First;
					}
					break;
				}
			default:
				{
					break;
				}
			}
			return Result;
		}

		bool ApplyScalarInstruction(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const FInstruction& Instruction,
		    FTypeState& State)
		{
			const auto& Code = Function.Code;
			const auto PushAny = [&]()
			{
				State.Stack.push_back(EValueType::Any);
			};
			switch (Instruction.Opcode)
			{
			case EOpcode::Nop:
			case EOpcode::Jmp:
			case EOpcode::JmpIfLocalGeImm:
				return true;
			case EOpcode::ConstI32:
				{
					std::uint16_t Index = 0;
					if (!ReadU16Operand(Code, Instruction, 1, Index) || Index >= Module.Constants.size())
					{
						return false;
					}
					const auto Kind = Module.Constants[Index].Kind;
					if (Kind == EConstantKind::I8 || Kind == EConstantKind::I16 || Kind == EConstantKind::I32)
					{
						State.Stack.push_back(EValueType::I32);
					}
					else if (Kind == EConstantKind::U8 || Kind == EConstantKind::U16 || Kind == EConstantKind::U32)
					{
						State.Stack.push_back(EValueType::U32);
					}
					else if (Kind == EConstantKind::I64)
					{
						State.Stack.push_back(EValueType::I64);
					}
					else if (Kind == EConstantKind::U64)
					{
						State.Stack.push_back(EValueType::U64);
					}
					else if (Kind == EConstantKind::Enum)
					{
						State.Stack.push_back(EValueType::Enum);
					}
					else
					{
						return false;
					}
					return true;
				}
			case EOpcode::ConstF64:
				{
					std::uint16_t Index = 0;
					if (!ReadU16Operand(Code, Instruction, 1, Index) || Index >= Module.Constants.size())
					{
						return false;
					}
					const auto Kind = Module.Constants[Index].Kind;
					if (Kind == EConstantKind::F32)
					{
						State.Stack.push_back(EValueType::F32);
					}
					else if (Kind == EConstantKind::F64)
					{
						State.Stack.push_back(EValueType::F64);
					}
					else
					{
						return false;
					}
					return true;
				}
			case EOpcode::ConstBool:
				{
					State.Stack.push_back(EValueType::Bool);
					return true;
				}
			case EOpcode::ConstStr:
				{
					std::uint16_t Index = 0;
					if (!ReadU16Operand(Code, Instruction, 1, Index) || Index >= Module.Constants.size())
					{
						return false;
					}
					const auto Kind = Module.Constants[Index].Kind;
					if (Kind == EConstantKind::Str)
					{
						State.Stack.push_back(EValueType::String);
					}
					else if (Kind == EConstantKind::WStr)
					{
						State.Stack.push_back(EValueType::WString);
					}
					else
					{
						return false;
					}
					return true;
				}
			case EOpcode::LoadLocal:
			case EOpcode::TakeLocal:
				{
					std::uint16_t Slot = 0;
					if (!ReadU16Operand(Code, Instruction, 1, Slot) || Slot >= State.Locals.size()) return false;
					State.Stack.push_back(State.Locals[Slot]);
					if (Instruction.Opcode == EOpcode::TakeLocal)
					{
						State.Locals[Slot] = EValueType::I32;
					}
					return true;
				}
			case EOpcode::StoreLocal:
				{
					std::uint16_t Slot = 0;
					if (!ReadU16Operand(Code, Instruction, 1, Slot) || Slot >= State.Locals.size() || State.Stack.empty())
					{
						return false;
					}
					State.Locals[Slot] = State.Stack.back();
					State.Stack.pop_back();
					return true;
				}
			case EOpcode::LoadGlobal:
				{
					std::uint16_t Slot = 0;
					if (!ReadU16Operand(Code, Instruction, 1, Slot))
					{
						return false;
					}
					State.Stack.push_back(Slot < Module.GlobalTypeNames.size()
					        ? TypeFromName(Module.GlobalTypeNames[Slot])
					        : EValueType::Any);
					return true;
				}
			case EOpcode::StoreGlobal:
				return PopTypes(State, 1);
			case EOpcode::LoadThisField:
				{
					std::uint16_t NameIndex = 0;
					EValueType FieldType = EValueType::Unknown;
					if (!ReadU16Operand(Code, Instruction, 1, NameIndex) || !FindMethodFieldType(Module, Function, NameIndex, FieldType))
					{
						return false;
					}
					State.Stack.push_back(FieldType);
					return true;
				}
			case EOpcode::StoreThisField:
			case EOpcode::InitializeThisField:
				{
					std::uint16_t NameIndex = 0;
					EValueType FieldType = EValueType::Unknown;
					if (!ReadU16Operand(Code, Instruction, 1, NameIndex) || !FindMethodFieldType(Module, Function, NameIndex, FieldType) || State.Stack.empty())
					{
						return false;
					}
					const EValueType ValueType = State.Stack.back();
					if (FieldType != EValueType::Any && ValueType != EValueType::Any && FieldType != ValueType)
					{
						return false;
					}
					return PopTypes(State, 1);
				}
			case EOpcode::Dup:
				{
					if (State.Stack.empty())
					{
						return false;
					}
					State.Stack.push_back(State.Stack.back());
					return true;
				}
			case EOpcode::Pop:
				return PopTypes(State, 1);
			case EOpcode::Add:
				{
					EValueType Left = EValueType::Unknown;
					EValueType Right = EValueType::Unknown;
					if (!PopBinaryTypes(State, Left, Right))
					{
						return false;
					}
					State.Stack.push_back(AddResultType(Left, Right));
					return true;
				}
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					EValueType Left = EValueType::Unknown;
					EValueType Right = EValueType::Unknown;
					if (!PopBinaryTypes(State, Left, Right))
					{
						return false;
					}
					State.Stack.push_back(NumericResultType(Left, Right));
					return true;
				}
			case EOpcode::Eq:
			case EOpcode::Ne:
			case EOpcode::Lt:
			case EOpcode::Le:
			case EOpcode::Gt:
			case EOpcode::Ge:
				{
					if (!PopTypes(State, 2))
					{
						return false;
					}
					State.Stack.push_back(EValueType::Bool);
					return true;
				}
			case EOpcode::Neg:
				{
					if (State.Stack.empty())
					{
						return false;
					}
					const EValueType Operand = State.Stack.back();
					State.Stack.pop_back();
					const EValueType Promoted =
					    Operand == EValueType::I8 || Operand == EValueType::I16
					    ? EValueType::I32
					    : Operand == EValueType::U8 || Operand == EValueType::U16
					    ? EValueType::U32
					    : Operand;
					State.Stack.push_back(IsNumeric(Promoted) ? Promoted : EValueType::Any);
					return true;
				}
			case EOpcode::BitNot:
				{
					if (State.Stack.empty())
					{
						return false;
					}
					const EValueType Operand = State.Stack.back();
					State.Stack.pop_back();
					State.Stack.push_back(
					    Operand == EValueType::I8 || Operand == EValueType::I16
					        ? EValueType::I32
					        : Operand == EValueType::U8 || Operand == EValueType::U16
					        ? EValueType::U32
					        : Operand == EValueType::I32 || Operand == EValueType::U32 || Operand == EValueType::I64 || Operand == EValueType::U64
					        ? Operand
					        : EValueType::Any);
					return true;
				}
			case EOpcode::Not:
				{
					if (!PopTypes(State, 1))
					{
						return false;
					}
					State.Stack.push_back(EValueType::Bool);
					return true;
				}
			case EOpcode::JmpIfFalse:
				return PopTypes(State, 1);
			case EOpcode::ConstNull:
				{
					PushAny();
					return true;
				}
			case EOpcode::CheckedCast:
				{
					std::uint16_t TypeNameIndex = 0;
					if (!ReadU16Operand(Code, Instruction, 1, TypeNameIndex) || TypeNameIndex >= Module.StringPool.size() || State.Stack.empty())
					{
						return false;
					}
					State.Stack.back() = TypeFromName(Module.StringPool[TypeNameIndex]);
					return true;
				}
			case EOpcode::TryCast:
				{
					if (State.Stack.empty())
					{
						return false;
					}
					State.Stack.back() = EValueType::Any;
					return true;
				}
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
				{
					std::uint16_t Slot = 0;
					return ReadU16Operand(Code, Instruction, 1, Slot) && Slot < State.Locals.size();
				}
			case EOpcode::IncField:
				{
					std::uint16_t NameIndex = 0;
					EValueType FieldType = EValueType::Unknown;
					return ReadU16Operand(Code, Instruction, 1, NameIndex) && FindMethodFieldType(Module, Function, NameIndex, FieldType) && FieldType != EValueType::Any && IsNumeric(FieldType);
				}
			case EOpcode::Return:
				// The VM discards one compiler-emitted sentinel for void returns. Keep
				// the architecture-neutral stack model identical so otherwise pure F64
				// methods do not become imprecise solely because the sentinel is i32.
				return Function.bReturnsVoid
				    ? (State.Stack.empty() || PopTypes(State, 1))
				    : PopTypes(State, 1);
			case EOpcode::LoadThis:
				// Scalar native CFG keeps the receiver in VM call context; it is not
				// materialized in the scalar operand stack.
				return Function.bIsMethod;
			case EOpcode::Call:
				{
					std::uint16_t Index = 0;
					if (!ReadU16Operand(Code, Instruction, 1, Index) || Instruction.Offset + 4 > Code.size())
					{
						return false;
					}
					const std::uint8_t Count = Code[Instruction.Offset + 3];
					if (!PopTypes(State, Count) || Index >= Module.Functions.size())
					{
						return false;
					}
					const auto& Callee = Module.Functions[Index];
					if (!Callee.bReturnsVoid)
					{
						FSignature Signature;
						State.Stack.push_back(
						    ParseSignature(Callee.ClosureTypeName, Signature) && Signature.bValid
						        ? Signature.ReturnType
						        : EValueType::Any);
					}
					return true;
				}
			case EOpcode::CallBoundMethod:
				{
					if (Instruction.Offset + 3 >= Code.size())
					{
						return false;
					}
					const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + Instruction.Offset + 1);
					const std::size_t Count = Code[Instruction.Offset + 3];
					if (Index >= Module.Functions.size() || !PopTypes(State, Count))
					{
						return false;
					}
					FSignature Signature;
					if (!ParseSignature(Module.Functions[Index].ClosureTypeName, Signature) || !Signature.bValid)
					{
						return false;
					}
					if (!Module.Functions[Index].bReturnsVoid)
					{
						State.Stack.push_back(Signature.ReturnType);
					}
					return true;
				}
			case EOpcode::CallMethod:
				{
					if (Instruction.Offset + 3 >= Code.size())
					{
						return false;
					}
					const std::uint16_t NameIndex = Bytecode::ReadU16LE(Code.data() + Instruction.Offset + 1);
					const std::size_t Count = Code[Instruction.Offset + 3];
					if (NameIndex >= Module.StringPool.size() || !PopTypes(State, Count))
					{
						return false;
					}
					const std::string& LeafName = Module.StringPool[NameIndex];
					const FBytecodeFunction* Match = nullptr;
					for (const FBytecodeFunction& Candidate : Module.Functions)
					{
						if (!Candidate.bIsMethod || Candidate.ParamCount != Count || !MethodLinkageLeafMatches(Candidate.Name, LeafName))
						{
							continue;
						}
						if (Match != nullptr)
						{
							return false;
						}
						Match = &Candidate;
					}
					if (Match == nullptr)
					{
						return false;
					}
					FSignature Signature;
					if (!ParseSignature(Match->ClosureTypeName, Signature) || !Signature.bValid)
					{
						return false;
					}
					if (!Match->bReturnsVoid)
					{
						State.Stack.push_back(Signature.ReturnType);
					}
					return true;
				}
			case EOpcode::CallIndirect:
				{
					const std::size_t CountOffset = Instruction.Opcode == EOpcode::CallIndirect ? 1 : 3;
					if (Instruction.Offset + CountOffset >= Code.size())
					{
						return false;
					}
					const std::size_t Count = Code[Instruction.Offset + CountOffset] + (Instruction.Opcode == EOpcode::CallIndirect ? 1u : 1u);
					if (!PopTypes(State, Count))
					{
						return false;
					}
					PushAny();
					return true;
				}
			case EOpcode::CallBuiltin:
				{
					if (Instruction.Offset + 2 >= Code.size())
					{
						return false;
					}
					const auto Builtin = static_cast<EBuiltinId>(Code[Instruction.Offset + 1]);
					const Builtins::FDescriptor* const Descriptor = JitHost::Services().LookupBuiltin(Builtin);
					const std::uint8_t ArgumentCount = Code[Instruction.Offset + 2];
					if (!Descriptor || !Descriptor->AcceptsArgCount(ArgumentCount) || !PopTypes(State, ArgumentCount))
					{
						return false;
					}
					if (!Descriptor->bPushVoidResult)
					{
						// Successful wide conversions and length queries have a fixed
						// result type, including when consumed by a later loop local.
						State.Stack.push_back(Builtin == EBuiltinId::ToInt64 || Builtin == EBuiltinId::Len || Builtin == EBuiltinId::ArrayPrepareSort
						        ? EValueType::I64
						        : Builtin == EBuiltinId::ToUInt64 ? EValueType::U64
						                                          : EValueType::Any);
					}
					return true;
				}
			case EOpcode::CallNative:
				{
					const std::size_t CountOffset = 3;
					if (Instruction.Offset + CountOffset >= Code.size())
					{
						return false;
					}
					if (!PopTypes(State, Code[Instruction.Offset + CountOffset]))
					{
						return false;
					}
					PushAny();
					return true;
				}
			default:
				return false;
			}
		}
	}

	bool ParseSignature(const std::string_view TypeName, FSignature& OutSignature,
	    std::vector<std::string_view>* const OutParameterNames)
	{
		OutSignature = {};
		if (OutParameterNames)
		{
			OutParameterNames->clear();
		}
		if (TypeName.size() < 6 || TypeName.substr(0, 3) != "fn(")
		{
			return false;
		}
		std::size_t Depth = 0;
		std::size_t Angle = 0;
		std::size_t Close = std::string_view::npos;
		for (std::size_t Index = 2; Index < TypeName.size(); ++Index)
		{
			const char Character = TypeName[Index];
			if (Character == '<')
			{
				++Angle;
			}
			else if (Character == '>' && Angle > 0 && (Index == 0 || TypeName[Index - 1] != '-'))
			{
				--Angle;
			}
			else if (Angle == 0 && Character == '(')
			{
				++Depth;
			}
			else if (Angle == 0 && Character == ')')
			{
				if (Depth == 0)
				{
					return false;
				}
				--Depth;
				if (Depth == 0)
				{
					Close = Index;
					break;
				}
			}
		}
		if (Close == std::string_view::npos || Close + 2 >= TypeName.size() || TypeName.substr(Close + 1, 2) != "->")
		{
			return false;
		}

		const std::string_view Parameters = TypeName.substr(3, Close - 3);
		const std::string_view Return = Trim(TypeName.substr(Close + 3));
		if (Return == "void")
		{
			OutSignature.ReturnType = EValueType::Void;
		}
		else if (!ParseValueType(Return, OutSignature.ReturnType))
		{
			return false;
		}

		if (!Parameters.empty())
		{
			std::size_t Begin = 0;
			Depth = 0;
			Angle = 0;
			for (std::size_t Index = 0; Index <= Parameters.size(); ++Index)
			{
				const char Character = Index < Parameters.size() ? Parameters[Index] : ',';
				if (Character == '<')
				{
					++Angle;
				}
				else if (Character == '>' && Angle > 0 && (Index == 0 || Parameters[Index - 1] != '-'))
				{
					--Angle;
				}
				else if (Character == '(')
				{
					++Depth;
				}
				else if (Character == ')' && Depth > 0)
				{
					--Depth;
				}
				else if (Character == ',' && Angle == 0 && Depth == 0)
				{
					EValueType ParameterType = EValueType::Unknown;
					if (!ParseValueType(Trim(Parameters.substr(Begin, Index - Begin)), ParameterType))
					{
						return false;
					}
					OutSignature.Parameters.push_back(ParameterType);
					if (OutParameterNames)
					{
						OutParameterNames->push_back(Trim(Parameters.substr(Begin, Index - Begin)));
					}
					Begin = Index + 1;
				}
			}
		}
		OutSignature.bValid = true;
		return true;
	}

	bool BuildFunction(
	    const FBytecodeFunction& Function,
	    FFunction& OutFunction,
	    std::string* const OutError)
	{
		OutFunction = {};
		if (!Function.ClosureTypeName.empty())
		{
			if (!ParseSignature(Function.ClosureTypeName, OutFunction.Signature))
			{
				SetError(OutError, "invalid function signature");
				return false;
			}
		}
		else
		{
			OutFunction.Signature.Parameters.assign(Function.ParamCount, EValueType::Any);
			OutFunction.Signature.ReturnType =
			    Function.bReturnsVoid ? EValueType::Void : EValueType::Any;
			OutFunction.Signature.bValid = Function.bIsMethod;
		}
		if (Function.Code.empty())
		{
			return Function.bReturnsVoid;
		}

		const auto& Code = Function.Code;
		std::vector<FInstruction> Instructions;
		std::unordered_set<std::size_t> InstructionOffsets;
		for (std::size_t Offset = 0; Offset < Code.size();)
		{
			const EOpcode Opcode = static_cast<EOpcode>(Code[Offset]);
			const std::size_t Size = Bytecode::InstructionSize(Opcode);
			if (Size == 0 || Offset + Size > Code.size())
			{
				SetError(OutError, "invalid bytecode instruction size");
				return false;
			}
			Instructions.push_back({Offset, Opcode, Size});
			InstructionOffsets.insert(Offset);
			Offset += Size;
		}

		std::vector<std::size_t> Leaders{0};
		for (const FInstruction& Instruction : Instructions)
		{
			if (!IsJump(Instruction.Opcode))
			{
				continue;
			}
			std::size_t Target = 0;
			if (!ReadJumpTarget(Code, Instruction, Target) || InstructionOffsets.find(Target) == InstructionOffsets.end())
			{
				SetError(OutError, "jump target is not an instruction boundary");
				return false;
			}
			Leaders.push_back(Target);
			const std::size_t Fallthrough = Instruction.Offset + Instruction.Size;
			if (IsConditionalJump(Instruction.Opcode) && Fallthrough < Code.size())
			{
				if (InstructionOffsets.find(Fallthrough) == InstructionOffsets.end())
				{
					SetError(OutError, "conditional fallthrough is not an instruction boundary");
					return false;
				}
				Leaders.push_back(Fallthrough);
			}
		}

		std::sort(Leaders.begin(), Leaders.end());
		Leaders.erase(std::unique(Leaders.begin(), Leaders.end()), Leaders.end());
		std::unordered_map<std::size_t, std::size_t> BlockByOffset;
		for (std::size_t Index = 0; Index < Leaders.size(); ++Index)
		{
			BlockByOffset.emplace(Leaders[Index], Index);
			FBasicBlock Block;
			Block.StartOffset = Leaders[Index];
			Block.EndOffset = Index + 1 < Leaders.size() ? Leaders[Index + 1] : Code.size();
			for (const FInstruction& Instruction : Instructions)
			{
				if (Instruction.Offset >= Block.StartOffset && Instruction.Offset < Block.EndOffset)
				{
					Block.Instructions.push_back(Instruction);
				}
			}
			if (Block.Instructions.empty())
			{
				SetError(OutError, "empty basic block");
				return false;
			}
			OutFunction.Blocks.push_back(std::move(Block));
		}

		for (std::size_t BlockIndex = 0; BlockIndex < OutFunction.Blocks.size(); ++BlockIndex)
		{
			FBasicBlock& Block = OutFunction.Blocks[BlockIndex];
			const FInstruction& Last = Block.Instructions.back();
			const auto AddSuccessor = [&](const std::size_t Target)
			{
				const auto It = BlockByOffset.find(Target);
				if (It == BlockByOffset.end())
				{
					return false;
				}
				if (std::find(Block.Successors.begin(), Block.Successors.end(), It->second) == Block.Successors.end())
				{
					Block.Successors.push_back(It->second);
				}
				if (It->second <= BlockIndex)
				{
					OutFunction.bHasLoop = true;
				}
				return true;
			};

			if (IsJump(Last.Opcode))
			{
				std::size_t Target = 0;
				if (!ReadJumpTarget(Code, Last, Target) || !AddSuccessor(Target))
				{
					SetError(OutError, "missing jump successor block");
					return false;
				}
				if (IsConditionalJump(Last.Opcode))
				{
					const std::size_t Fallthrough = Last.Offset + Last.Size;
					if (Fallthrough < Code.size() && !AddSuccessor(Fallthrough))
					{
						SetError(OutError, "missing conditional successor block");
						return false;
					}
				}
			}
			else if (Last.Opcode != EOpcode::Return)
			{
				const std::size_t Fallthrough = Last.Offset + Last.Size;
				if (Fallthrough < Code.size() && !AddSuccessor(Fallthrough))
				{
					SetError(OutError, "missing fallthrough block");
					return false;
				}
			}
		}

		for (std::size_t BlockIndex = 0; BlockIndex < OutFunction.Blocks.size(); ++BlockIndex)
		{
			for (const std::size_t Successor : OutFunction.Blocks[BlockIndex].Successors)
			{
				OutFunction.Blocks[Successor].Predecessors.push_back(BlockIndex);
			}
		}
		return true;
	}

	bool AnalyzeTypes(
	    const FBytecodeModule& Module,
	    const FBytecodeFunction& Function,
	    FFunction& InOutFunction,
	    std::string* const OutError)
	{
		if (InOutFunction.Blocks.empty() && !BuildFunction(Function, InOutFunction, OutError))
		{
			return false;
		}
		InOutFunction.BlockEntryStates.assign(InOutFunction.Blocks.size(), {});
		InOutFunction.InstructionEntryStates.assign(InOutFunction.Blocks.size(), {});
		InOutFunction.InstructionExitStates.assign(InOutFunction.Blocks.size(), {});
		InOutFunction.LocalTypes.assign(Function.LocalCount, EValueType::Unknown);
		InOutFunction.LocalReadCounts.assign(Function.LocalCount, 0u);
		InOutFunction.LocalWriteCounts.assign(Function.LocalCount, 0u);
		InOutFunction.LocalAddressEscapes.assign(Function.LocalCount, false);
		InOutFunction.ReachableBlocks.assign(InOutFunction.Blocks.size(), false);
		InOutFunction.BlockLiveInLocals.assign(InOutFunction.Blocks.size(), {});
		InOutFunction.BlockLiveOutLocals.assign(InOutFunction.Blocks.size(), {});
		InOutFunction.DeadLocalStores.assign(InOutFunction.Blocks.size(), {});
		if (InOutFunction.Blocks.empty())
		{
			InOutFunction.bTypeAnalysisPrecise = Function.bReturnsVoid;
			return true;
		}

		std::vector<bool> Reached(InOutFunction.Blocks.size(), false);
		std::deque<std::size_t> Work;
		FTypeState Initial;
		// Non-parameter locals have no value at function entry. A dominating
		// StoreLocal overwrites Unknown with a concrete type, while a CFG join with
		// a reachable uninitialized path remains conservative. Seeding every local
		// as Any permanently poisoned loop joins and incorrectly forced otherwise
		// scalar functions into boxed fallback paths.
		Initial.Locals.assign(Function.LocalCount, EValueType::Unknown);
		for (std::size_t Index = 0; Index < InOutFunction.Signature.Parameters.size() && Index < Initial.Locals.size(); ++Index)
		{
			Initial.Locals[Index] = InOutFunction.Signature.Parameters[Index];
		}
		InOutFunction.BlockEntryStates[0] = Initial;
		Reached[0] = true;
		Work.push_back(0);
		bool bPrecise = InOutFunction.Signature.bValid;
		while (!Work.empty())
		{
			const std::size_t BlockIndex = Work.front();
			Work.pop_front();
			FTypeState State = InOutFunction.BlockEntryStates[BlockIndex];
			for (const FInstruction& Instruction : InOutFunction.Blocks[BlockIndex].Instructions)
			{
				if (!ApplyScalarInstruction(Module, Function, Instruction, State))
				{
					InOutFunction.bHasUnsupportedOperations = true;
					bPrecise = false;
					break;
				}
			}
			if (!bPrecise)
			{
				continue;
			}
			for (const std::size_t Successor : InOutFunction.Blocks[BlockIndex].Successors)
			{
				FTypeState& Existing = InOutFunction.BlockEntryStates[Successor];
				bool bChanged = !Reached[Successor];
				if (!Reached[Successor])
				{
					Existing = State;
					Reached[Successor] = true;
				}
				else if (Existing.Stack.size() != State.Stack.size() || Existing.Locals.size() != State.Locals.size())
				{
					bPrecise = false;
					break;
				}
				else
				{
					for (std::size_t Index = 0; Index < Existing.Stack.size(); ++Index)
					{
						const EValueType Joined = JoinType(Existing.Stack[Index], State.Stack[Index]);
						bChanged = bChanged || Joined != Existing.Stack[Index];
						Existing.Stack[Index] = Joined;
					}
					for (std::size_t Index = 0; Index < Existing.Locals.size(); ++Index)
					{
						const EValueType Joined = JoinType(Existing.Locals[Index], State.Locals[Index]);
						bChanged = bChanged || Joined != Existing.Locals[Index];
						Existing.Locals[Index] = Joined;
					}
				}
				if (bChanged)
				{
					Work.push_back(Successor);
				}
			}
		}
		InOutFunction.ReachableBlocks = Reached;
		InOutFunction.bTypeAnalysisPrecise = bPrecise;
		if (bPrecise)
		{
			const auto RecordLocalUse = [&](const FInstruction& Instruction)
			{
				const FLocalEffects Effects = DecodeLocalEffects(Function, Instruction);
				for (const std::uint16_t Slot : Effects.Reads)
				{
					if (Slot < InOutFunction.LocalReadCounts.size())
					{
						++InOutFunction.LocalReadCounts[Slot];
					}
				}
				if (Effects.Write < InOutFunction.LocalWriteCounts.size())
				{
					++InOutFunction.LocalWriteCounts[Effects.Write];
				}
				if (Effects.Escapes < InOutFunction.LocalAddressEscapes.size())
				{
					InOutFunction.LocalAddressEscapes[Effects.Escapes] = true;
				}
			};
			const auto MergeLocalTypes = [&](const FTypeState& State)
			{
				for (std::size_t Index = 0;
				    Index < State.Locals.size() && Index < InOutFunction.LocalTypes.size();
				    ++Index)
				{
					const EValueType Observed = State.Locals[Index];
					// The aggregate starts at Unknown as an analysis sentinel. Do not let
					// states before a local's dominating store erase its later concrete
					// representation. CFG joins above still treat a reachable uninitialized
					// path conservatively as Any.
					if (Observed == EValueType::Unknown)
					{
						continue;
					}
					EValueType& Aggregate = InOutFunction.LocalTypes[Index];
					Aggregate = Aggregate == EValueType::Unknown
					    ? Observed
					    : JoinType(Aggregate, Observed);
				}
			};
			// Replaying after convergence gives lowering a stable, final state even
			// when a block was visited several times while joining loop types.
			for (std::size_t BlockIndex = 0; BlockIndex < InOutFunction.Blocks.size(); ++BlockIndex)
			{
				if (!Reached[BlockIndex])
				{
					continue;
				}
				FTypeState State = InOutFunction.BlockEntryStates[BlockIndex];
				MergeLocalTypes(State);
				auto& States = InOutFunction.InstructionEntryStates[BlockIndex];
				auto& ExitStates = InOutFunction.InstructionExitStates[BlockIndex];
				States.clear();
				ExitStates.clear();
				States.reserve(InOutFunction.Blocks[BlockIndex].Instructions.size());
				ExitStates.reserve(InOutFunction.Blocks[BlockIndex].Instructions.size());
				for (const FInstruction& Instruction : InOutFunction.Blocks[BlockIndex].Instructions)
				{
					RecordLocalUse(Instruction);
					States.push_back(State);
					if (!ApplyScalarInstruction(Module, Function, Instruction, State))
					{
						InOutFunction.bTypeAnalysisPrecise = false;
						break;
					}
					ExitStates.push_back(State);
					MergeLocalTypes(State);
				}
			}

			if (InOutFunction.bTypeAnalysisPrecise)
			{
				const std::size_t BlockCount = InOutFunction.Blocks.size();
				const std::size_t LocalCount = Function.LocalCount;
				std::vector<std::vector<bool>> BlockUse(
				    BlockCount, std::vector<bool>(LocalCount, false));
				std::vector<std::vector<bool>> BlockDef(
				    BlockCount, std::vector<bool>(LocalCount, false));
				for (std::size_t BlockIndex = 0; BlockIndex < BlockCount; ++BlockIndex)
				{
					InOutFunction.BlockLiveInLocals[BlockIndex].assign(LocalCount, false);
					InOutFunction.BlockLiveOutLocals[BlockIndex].assign(LocalCount, false);
					if (!Reached[BlockIndex])
					{
						continue;
					}
					for (const FInstruction& Instruction :
					    InOutFunction.Blocks[BlockIndex].Instructions)
					{
						const FLocalEffects Effects = DecodeLocalEffects(Function, Instruction);
						for (const std::uint16_t Slot : Effects.Reads)
						{
							if (Slot < LocalCount && !BlockDef[BlockIndex][Slot])
							{
								BlockUse[BlockIndex][Slot] = true;
							}
						}
						if (Effects.Write < LocalCount)
						{
							BlockDef[BlockIndex][Effects.Write] = true;
						}
					}
				}

				bool bChanged = true;
				while (bChanged)
				{
					bChanged = false;
					for (std::size_t Reverse = BlockCount; Reverse > 0; --Reverse)
					{
						const std::size_t BlockIndex = Reverse - 1u;
						if (!Reached[BlockIndex])
						{
							continue;
						}
						std::vector<bool> NewOut(LocalCount, false);
						for (const std::size_t Successor :
						    InOutFunction.Blocks[BlockIndex].Successors)
						{
							if (Successor >= BlockCount || !Reached[Successor])
							{
								continue;
							}
							for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
							{
								NewOut[Slot] = NewOut[Slot] || InOutFunction.BlockLiveInLocals[Successor][Slot];
							}
						}
						std::vector<bool> NewIn = BlockUse[BlockIndex];
						for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
						{
							NewIn[Slot] = NewIn[Slot] || (NewOut[Slot] && !BlockDef[BlockIndex][Slot]) || InOutFunction.LocalAddressEscapes[Slot];
							NewOut[Slot] = NewOut[Slot] || InOutFunction.LocalAddressEscapes[Slot];
						}
						bChanged = bChanged || NewOut != InOutFunction.BlockLiveOutLocals[BlockIndex] || NewIn != InOutFunction.BlockLiveInLocals[BlockIndex];
						InOutFunction.BlockLiveOutLocals[BlockIndex] = std::move(NewOut);
						InOutFunction.BlockLiveInLocals[BlockIndex] = std::move(NewIn);
					}
				}

				for (std::size_t BlockIndex = 0; BlockIndex < BlockCount; ++BlockIndex)
				{
					const auto& Instructions = InOutFunction.Blocks[BlockIndex].Instructions;
					auto& DeadStores = InOutFunction.DeadLocalStores[BlockIndex];
					DeadStores.assign(Instructions.size(), false);
					if (!Reached[BlockIndex])
					{
						continue;
					}
					std::vector<bool> Live = InOutFunction.BlockLiveOutLocals[BlockIndex];
					for (std::size_t Reverse = Instructions.size(); Reverse > 0; --Reverse)
					{
						const std::size_t InstructionIndex = Reverse - 1u;
						const FLocalEffects Effects = DecodeLocalEffects(
						    Function, Instructions[InstructionIndex]);
						DeadStores[InstructionIndex] =
						    Instructions[InstructionIndex].Opcode == EOpcode::StoreLocal && Effects.Write < LocalCount && !Live[Effects.Write] && !InOutFunction.LocalAddressEscapes[Effects.Write];
						if (Effects.Write < LocalCount && !InOutFunction.LocalAddressEscapes[Effects.Write])
						{
							Live[Effects.Write] = false;
						}
						for (const std::uint16_t Slot : Effects.Reads)
						{
							if (Slot < LocalCount)
							{
								Live[Slot] = true;
							}
						}
					}
				}
			}
		}
		return true;
	}
}
