// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitHost.h"
#include "VeroScriptJitLinear.h"
#include "VeroScriptJitText.h"
#include "VeroScriptJitTextPatterns.h"

#include "VeroScriptJitIr.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptAsmJitExecutionContext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

namespace VeroScript::AsmJit::JitLinear
{
	namespace
	{
		constexpr std::uint32_t InvalidInstruction = 0xFFFFFFFFu;
		constexpr std::size_t MaxTypedLoopInstructions = 2048u;

		bool CanBindArrayPushAtEntry(const FTypedLoopPlan& Plan)
		{
			if (Plan.FusedKind != FTypedLoopPlan::EFusedKind::None || Plan.bWideCandidate ||
				Plan.bUsesDirectScalarFields || Plan.bOwnsStringTemporaries)
			{
				return false;
			}

			bool bHasArrayPush = false;
			for (const FTypedLoopInstruction& Instruction : Plan.Instructions)
			{
				if (Instruction.Opcode == EOpcode::CallBoundMethod ||
				    Instruction.Opcode == EOpcode::GetIndexCopy || Instruction.Opcode == EOpcode::SetIndex)
				{
					return false;
				}
				if (Instruction.Opcode == EOpcode::CallMethod)
				{
					if (Instruction.Immediate != 2 || Instruction.ArgumentCount != 1u) return false;
					bHasArrayPush = true;
				}
			}
			return bHasArrayPush;
		}

		bool TryJumpTarget(
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    const EOpcode Opcode,
		    std::size_t& OutTarget)
		{
			const std::size_t Size = Bytecode::InstructionSize(Opcode);
			const std::size_t OffsetPosition = Opcode == EOpcode::JmpIfLocalGeImm
			    ? 7u
			    : 1u;
			if (Size == 0 || Size > Code.size() - Ip || OffsetPosition + sizeof(std::int32_t) > Size)
			{
				return false;
			}
			const std::int64_t Target = static_cast<std::int64_t>(Ip + Size) + static_cast<std::int64_t>(Bytecode::ReadI32LE(Code.data() + Ip + OffsetPosition));
			if (Target < 0 || Target > static_cast<std::int64_t>(Code.size()))
			{
				return false;
			}
			OutTarget = static_cast<std::size_t>(Target);
			return true;
		}

		bool IsTypedLoopOpcode(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    const EOpcode Opcode,
		    bool& bInOutUsesContainer)
		{
			switch (Opcode)
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
			case EOpcode::Jmp:
			case EOpcode::JmpIfFalse:
			case EOpcode::JmpIfLocalGeImm:
				return true;
			case EOpcode::ConstStr:
				{
					const auto Constant = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
					return Constant < Module.Constants.size() && Module.Constants[Constant].Kind == EConstantKind::Str
						&& Module.Constants[Constant].StringIndex < Module.StringPool.size();
				}
			case EOpcode::Call:
				{
					const auto Index = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
					FPureTextProjection Projection;
					return Index < Module.Functions.size() && Code[Ip + 3u] == Module.Functions[Index].ParamCount
						&& DescribePureTextProjection(Module.Functions[Index], Projection)
						&& Projection.Kind != EPureTextProjection::IdentityWString;
				}
			case EOpcode::CallBuiltin:
				{
					if (Code[Ip + 2u] == 2u && static_cast<EBuiltinId>(Code[Ip + 1u]) == EBuiltinId::CharCode) return true;
					if (Code[Ip + 2u] != 1u)
					{
						return false;
					}
					const EBuiltinId Builtin = static_cast<EBuiltinId>(Code[Ip + 1u]);
					return Builtin == EBuiltinId::Len || Builtin == EBuiltinId::ToString || Builtin == EBuiltinId::ToInt || Builtin == EBuiltinId::ToInt64 || Builtin == EBuiltinId::ToUInt64 || Builtin == EBuiltinId::ToUnsigned || Builtin == EBuiltinId::ToFloat || Builtin == EBuiltinId::ToDouble;
				}
			case EOpcode::GetIndexCopy:
			case EOpcode::SetIndex:
				{
					bInOutUsesContainer = true;
					return true;
				}
			case EOpcode::LoadField:
			case EOpcode::StoreField:
			case EOpcode::LoadFieldKeep:
			case EOpcode::IncField:
				{
					bInOutUsesContainer = true;
					return true;
				}
			case EOpcode::CallMethod:
				{
					const std::uint16_t NameIndex =
					    Bytecode::ReadU16LE(Code.data() + Ip + 1u);
					const std::uint8_t ArgumentCount = Code[Ip + 3u];
					if (NameIndex >= Module.StringPool.size() || (ArgumentCount != 1u && ArgumentCount != 2u))
					{
						return false;
					}
					// One-argument calls may be admitted only for a subsequently proven
					// `has`/`push`/`add` call or for a subsequently proven fused scalar
					// virtual-transform plan. TryBuildPlan assigns the container operation
					// tag and rejects every other method unless the complete virtual shape
					// is proven, before a plan can execute or mutate state.
					bInOutUsesContainer = true;
					return true;
				}
			case EOpcode::CallBoundMethod:
				{
					bInOutUsesContainer = true;
					return Code[Ip + 3u] <= 8u;
				}
			default:
				return false;
			}
		}

		bool TryReadI32Constant(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    std::int32_t& OutValue)
		{
			const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
			if (Index >= Module.Constants.size())
			{
				return false;
			}
			const auto& Constant = Module.Constants[Index];
			switch (Constant.Kind)
			{
			case EConstantKind::I8:
				{
					OutValue = Constant.I8;
					return true;
				}
			case EConstantKind::U8:
				{
					OutValue = Constant.U8;
					return true;
				}
			case EConstantKind::I16:
				{
					OutValue = Constant.I16;
					return true;
				}
			case EConstantKind::U16:
				{
					OutValue = Constant.U16;
					return true;
				}
			case EConstantKind::I32:
				{
					OutValue = Constant.I32;
					return true;
				}
			default:
				return false;
			}
		}

		bool TryReadF64Constant(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    double& OutValue,
		    bool* const bOutF32 = nullptr)
		{
			const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
			if (Index >= Module.Constants.size())
			{
				return false;
			}
			const auto& Constant = Module.Constants[Index];
			if (Constant.Kind == EConstantKind::F64)
			{
				OutValue = Constant.F64;
				if (bOutF32)
				{
					*bOutF32 = false;
				}
				return true;
			}
			if (Constant.Kind == EConstantKind::F32)
			{
				OutValue = static_cast<double>(Constant.F32);
				if (bOutF32)
				{
					*bOutF32 = true;
				}
				return true;
			}
			return false;
		}

		bool TryReadTypedIntegerConstant(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    std::int32_t& OutBits,
		    bool& bOutU32)
		{
			bOutU32 = false;
			const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
			if (Index >= Module.Constants.size())
			{
				return false;
			}
			const auto& Constant = Module.Constants[Index];
			if (Constant.Kind == EConstantKind::U32)
			{
				OutBits = static_cast<std::int32_t>(Constant.U32);
				bOutU32 = true;
				return true;
			}
			return TryReadI32Constant(Module, Code, Ip, OutBits);
		}

		bool TryBuildPureI32MethodExpression(
		    const FBytecodeModule& Module,
		    const std::uint16_t FunctionIndex,
		    FTypedLoopPlan::FPureI32MethodExpression& Out)
		{
			if (FunctionIndex >= Module.Functions.size())
			{
				return false;
			}
			const FBytecodeFunction& Function = Module.Functions[FunctionIndex];
			if (!Function.bIsMethod || Function.bReturnsVoid || Function.bReturnsRef || Function.bDeleted || Function.bIsAbstract || (Function.ParamCount != 1u && Function.ParamCount != 2u) || Function.LocalCount != Function.ParamCount || BytecodeFunctionHasRefParams(Function) || !Function.ExceptionRegions.empty() || Function.Code.empty())
			{
				return false;
			}

			FTypedLoopPlan::FPureI32MethodExpression Candidate;
			Candidate.FunctionIndex = FunctionIndex;
			std::int32_t StackDepth = 0;
			std::int32_t MaxStackDepth = 0;
			bool bReturned = false;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if (Size == 0u || Size > Function.Code.size() - Ip || bReturned)
				{
					return false;
				}
				FTypedLoopInstruction Instruction;
				Instruction.Opcode = Opcode;
				Instruction.BytecodeOffset = static_cast<std::uint32_t>(Ip);
				switch (Opcode)
				{
				case EOpcode::Nop:
					{
						break;
					}
				case EOpcode::LoadLocal:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(
						    Function.Code.data() + Ip + 1u);
						if (Instruction.Operand0 >= Function.ParamCount)
						{
							return false;
						}
						Candidate.Instructions.push_back(Instruction);
						++StackDepth;
						break;
					}
				case EOpcode::ConstI32:
					{
						if (!TryReadI32Constant(
						        Module, Function.Code, Ip, Instruction.Immediate))
						{
							return false;
						}
						Candidate.Instructions.push_back(Instruction);
						++StackDepth;
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
					{
						if (StackDepth < 2)
						{
							return false;
						}
						Candidate.Instructions.push_back(Instruction);
						--StackDepth;
						break;
					}
				case EOpcode::Return:
					{
						if (StackDepth != 1 || Ip + Size != Function.Code.size())
						{
							return false;
						}
						bReturned = true;
						break;
					}
				default:
					return false;
				}
				MaxStackDepth = std::max(MaxStackDepth, StackDepth);
				Ip += Size;
			}
			if (!bReturned || Candidate.Instructions.empty() || MaxStackDepth <= 0 || MaxStackDepth > 32)
			{
				return false;
			}
			Candidate.MaxStackDepth = static_cast<std::uint8_t>(MaxStackDepth);
			Out = std::move(Candidate);
			return true;
		}

		#include "Internal/VeroScriptJitMethodExpressions.inl"

		// Track the receiver's stack position through its scalar arguments. An
		// argument expression is not necessarily a single preceding instruction.
		std::uint16_t FindRootedStackLocal(
			const std::vector<FTypedLoopInstruction>& Instructions, const std::size_t EndIndex,
			const std::size_t Depth, std::size_t* const OutProducer = nullptr)
		{
			for (std::size_t Index = EndIndex; Index != 0u;)
			{
				const auto& Previous = Instructions[--Index];
				if (Previous.Opcode == EOpcode::LoadLocal && Previous.EntryStackDepth == Depth)
				{
					for (const auto& Branch : Instructions)
					{
						if ((Branch.Opcode == EOpcode::Jmp || Branch.Opcode == EOpcode::JmpIfFalse || Branch.Opcode == EOpcode::JmpIfLocalGeImm) && Branch.BranchInstruction > Index && Branch.BranchInstruction <= EndIndex) return 0xFFFFu;
					}
					return Previous.Operand0;
				}
				if (Previous.EntryStackDepth <= Depth || Previous.Opcode == EOpcode::Jmp || Previous.Opcode == EOpcode::JmpIfFalse || Previous.Opcode == EOpcode::JmpIfLocalGeImm || Previous.Opcode == EOpcode::StoreLocal || Previous.Opcode == EOpcode::CallMethod || Previous.Opcode == EOpcode::CallBoundMethod) return 0xFFFFu;
				std::size_t Pops = 0;
				switch (Previous.Opcode)
				{
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
				case EOpcode::GetIndexCopy:
				case EOpcode::StoreField:
					{
						Pops = 2u;
						break;
					}
				case EOpcode::SetIndex:
					{
						Pops = 3u;
						break;
					}
				case EOpcode::CallBuiltin:
					{
						Pops = Previous.ArgumentCount;
						break;
					}
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::Neg:
				case EOpcode::Not:
				case EOpcode::BitNot:
				case EOpcode::Dup:
				case EOpcode::Pop:
				case EOpcode::IncField:
					{
						Pops = 1u;
						break;
					}
				default:
					{
						break;
					}
				}
				if (Pops > Previous.EntryStackDepth || (Pops != 0u && Previous.EntryStackDepth - Pops <= Depth))
				{
					if (OutProducer)
					{
						*OutProducer = Index;
					}
					return 0xFFFFu;
				}
			}
			return 0xFFFFu;
		}

		std::uint16_t FindRootedMethodReceiver(
			const std::vector<FTypedLoopInstruction>& Instructions, const std::size_t CallIndex,
			std::uint16_t& OutArraySlot)
		{
			const auto& Call = Instructions[CallIndex];
			if (Call.EntryStackDepth <= Call.ArgumentCount) return 0xFFFFu;
			const auto Depth = Call.EntryStackDepth - Call.ArgumentCount - 1u;
			std::size_t Producer = Instructions.size();
			const auto Slot = FindRootedStackLocal(Instructions, CallIndex, Depth, &Producer);
			if (Slot == 0xFFFFu && Producer < Instructions.size() && Instructions[Producer].Opcode == EOpcode::GetIndexCopy && Instructions[Producer].EntryStackDepth == Depth + 2u)
			{
				OutArraySlot = FindRootedStackLocal(Instructions, Producer, Depth);
			}
			return Slot;
		}

		bool CanUseFusedScalarContainerStorage(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			if (Function.bReturnsRef || BytecodeFunctionHasRefParams(Function) || !Function.ExceptionRegions.empty())
			{
				return false;
			}
			const auto IsScalarType = [](const JitIr::EValueType Type)
			{
				return Type == JitIr::EValueType::I32 || Type == JitIr::EValueType::U32 || Type == JitIr::EValueType::I64 || Type == JitIr::EValueType::U64 || Type == JitIr::EValueType::F32 || Type == JitIr::EValueType::F64 || Type == JitIr::EValueType::Bool;
			};
			const auto IsI32Name = [](const std::string_view Type)
			{
				return Type == "int" || Type == "int32";
			};
			const auto IsU32Name = [](const std::string_view Type)
			{
				return Type == "unsigned int" || Type == "uint" || Type == "unsigned" || Type == "uint32";
			};
			const auto IsStringName = [](const std::string_view Type)
			{
				return Type == "string" || Type == "wstring";
			};
			const auto IsTypedArrayElement = [&](const std::string_view Type)
			{
				return IsI32Name(Type) || IsU32Name(Type) || Type == "float" || Type == "double" || IsStringName(Type);
			};
			JitIr::FSignature Signature;
			if (!JitIr::ParseSignature(Function.ClosureTypeName, Signature) || !Signature.bValid || Function.bReturnsVoid != (Signature.ReturnType == JitIr::EValueType::Void) || (Signature.ReturnType != JitIr::EValueType::Void && !IsScalarType(Signature.ReturnType)) || !std::all_of(Signature.Parameters.begin(), Signature.Parameters.end(), IsScalarType))
			{
				return false;
			}
			const auto HasScalarCallSignature = [&](
			                                        const FBytecodeFunction& Callee,
			                                        const std::uint8_t ArgumentCount)
			{
				JitIr::FSignature CalleeSignature;
				return !Callee.bIsMethod && !BytecodeFunctionHasRefParams(Callee) && Callee.ParamCount == ArgumentCount && JitIr::ParseSignature(Callee.ClosureTypeName, CalleeSignature) && CalleeSignature.bValid && Callee.bReturnsVoid == (CalleeSignature.ReturnType == JitIr::EValueType::Void) && CalleeSignature.Parameters.size() == ArgumentCount && std::all_of(CalleeSignature.Parameters.begin(), CalleeSignature.Parameters.end(), IsScalarType) && (CalleeSignature.ReturnType == JitIr::EValueType::Void || IsScalarType(CalleeSignature.ReturnType));
			};

			// This proof deliberately admits only frame-local primitive construction
			// and scalar/container operations. A free call is safe only when its complete
			// signature is scalar and has no ref parameters, so no frame-local container
			// handle can cross that boundary. Globals, fields, refs, closures and every
			// other call shape remain rejected.
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Function.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if (Size == 0 || Size > Function.Code.size() - Ip)
				{
					return false;
				}
				switch (Opcode)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
				case EOpcode::ConstStr:
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
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
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::AddLocalLocal:
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
				case EOpcode::JmpIfLocalGeImm:
				case EOpcode::GetIndexCopy:
				case EOpcode::SetIndex:
				case EOpcode::Return:
					{
						break;
					}
				case EOpcode::TakeLocal:
					{
						// An owned construction temporary stays inside this frame when
						// transferred directly to a distinct final local.
						const auto Next = Ip + Size;
						if (Next + Bytecode::InstructionSize(EOpcode::StoreLocal) > Function.Code.size() || static_cast<EOpcode>(Function.Code[Next]) != EOpcode::StoreLocal || Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u) == Bytecode::ReadU16LE(Function.Code.data() + Next + 1u))
						{
							return false;
						}
						break;
					}
				case EOpcode::NewDynamicArray:
					{
						const std::uint16_t TypeIndex = Bytecode::ReadU16LE(
						    Function.Code.data() + Ip + 1u);
						std::string ElementType;
						if (TypeIndex >= Module.StringPool.size() || !JitHost::Services().ParseDynamicArrayElementType(Module.StringPool[TypeIndex], ElementType) || !IsTypedArrayElement(ElementType))
						{
							return false;
						}
						break;
					}
				case EOpcode::NewMap:
					{
						const std::uint16_t TypeIndex = Bytecode::ReadU16LE(
						    Function.Code.data() + Ip + 1u);
						std::string KeyType;
						std::string ValueType;
						if (TypeIndex >= Module.StringPool.size() || !JitHost::Services().SplitMapType(Module.StringPool[TypeIndex], KeyType, ValueType) || (!(IsI32Name(KeyType) || IsU32Name(KeyType)) && !IsStringName(KeyType)) || (IsStringName(KeyType) ? !IsI32Name(ValueType) : !(IsI32Name(ValueType) || IsU32Name(ValueType) || ValueType == "float" || ValueType == "double")))
						{
							return false;
						}
						break;
					}
				case EOpcode::NewSet:
					{
						const std::uint16_t TypeIndex = Bytecode::ReadU16LE(
						    Function.Code.data() + Ip + 1u);
						std::string ElementType;
						if (TypeIndex >= Module.StringPool.size() || !JitHost::Services().SplitSetElementType(Module.StringPool[TypeIndex], ElementType) || !(IsI32Name(ElementType) || IsU32Name(ElementType) || IsStringName(ElementType)))
						{
							return false;
						}
						break;
					}
				case EOpcode::CallMethod:
					{
						const std::uint16_t NameIndex = Bytecode::ReadU16LE(
						    Function.Code.data() + Ip + 1u);
						if (NameIndex >= Module.StringPool.size() || Function.Code[Ip + 3u] != 1u)
						{
							return false;
						}
						const std::string& Name = Module.StringPool[NameIndex];
						if (Name != "reserve" && Name != "push" && Name != "add" && Name != "has" && Name != "remove")
						{
							return false;
						}
						break;
					}
				case EOpcode::Call:
					{
						const std::uint16_t CalleeIndex = Bytecode::ReadU16LE(
						    Function.Code.data() + Ip + 1u);
						const std::uint8_t ArgumentCount = Function.Code[Ip + 3u];
						if (CalleeIndex >= Module.Functions.size() || !HasScalarCallSignature(Module.Functions[CalleeIndex], ArgumentCount))
						{
							return false;
						}
						break;
					}
				case EOpcode::CallBuiltin:
					{
						const std::uint8_t ArgumentCount = Function.Code[Ip + 2u];
						if (ArgumentCount == 0u)
						{
							break;
						}
						const EBuiltinId Builtin = static_cast<EBuiltinId>(
						    Function.Code[Ip + 1u]);
						const bool bPrimitiveConversion = ArgumentCount == 1u && (Builtin == EBuiltinId::Len || Builtin == EBuiltinId::ToString || Builtin == EBuiltinId::ToInt || Builtin == EBuiltinId::ToUnsigned || Builtin == EBuiltinId::ToInt64 || Builtin == EBuiltinId::ToUInt64 || Builtin == EBuiltinId::ToFloat || Builtin == EBuiltinId::ToDouble || Builtin == EBuiltinId::ToInt8 || Builtin == EBuiltinId::ToUInt8 || Builtin == EBuiltinId::ToInt16 || Builtin == EBuiltinId::ToUInt16);
						if (!bPrimitiveConversion)
						{
							return false;
						}
						break;
					}
				default:
					return false;
				}
				Ip += Size;
			}
			return true;
		}

		bool MatchOpcodes(
		    const std::vector<FTypedLoopInstruction>& Instructions,
		    const std::initializer_list<EOpcode> Opcodes)
		{
			if (Instructions.size() != Opcodes.size())
			{
				return false;
			}
			return std::equal(
			    Instructions.begin(), Instructions.end(), Opcodes.begin(),
			    [](const FTypedLoopInstruction& Instruction, const EOpcode Opcode)
			{
				return Instruction.Opcode == Opcode;
			});
		}

		bool MatchOpcodesWithWildcard(
		    const std::vector<FTypedLoopInstruction>& Instructions,
		    const std::initializer_list<EOpcode> Opcodes,
		    const std::size_t WildcardIndex)
		{
			if (Instructions.size() != Opcodes.size() || WildcardIndex >= Instructions.size())
			{
				return false;
			}
			std::size_t Index = 0;
			for (const EOpcode Opcode : Opcodes)
			{
				if (Index != WildcardIndex && Instructions[Index].Opcode != Opcode)
				{
					return false;
				}
				++Index;
			}
			return true;
		}

		bool InstructionWritesLocal(
		    const FTypedLoopInstruction& Instruction,
		    const std::uint16_t Slot) noexcept
		{
			switch (Instruction.Opcode)
			{
			case EOpcode::StoreLocal:
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
				return Instruction.Operand0 == Slot;
			default:
				return false;
			}
		}

		#include "Internal/VeroScriptJitObjectPrefixes.inl"

		bool FusedPlanSnapshotsMutatedLimit(const FTypedLoopPlan& Plan)
		{
			std::uint16_t LimitSlot = 0xFFFFu;
			switch (Plan.FusedKind)
			{
			case FTypedLoopPlan::EFusedKind::None:
				return false;
			case FTypedLoopPlan::EFusedKind::DirectAffineIndexedUpdate:
				{
					if (Plan.DirectIndexed.bLimitImmediate)
					{
						return false;
					}
					LimitSlot = Plan.DirectIndexed.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::DirectConstantScalarIndexedUpdate:
				return false;
			case FTypedLoopPlan::EFusedKind::DirectAffineSetContains:
				{
					if (Plan.DirectSet.bLimitImmediate)
					{
						return false;
					}
					LimitSlot = Plan.DirectSet.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::DirectFieldAccumulate:
				{
					LimitSlot = Plan.DirectFieldAccumulator.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::DirectFieldBatchUpdate:
				{
					LimitSlot = Plan.DirectFieldBatch.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::DirectStateFieldAccumulate:
				{
					LimitSlot = Plan.DirectFields.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::StateIndirectFieldUpdate:
				{
					LimitSlot = Plan.StateFields.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::StateIndirectStringLookupCompare:
			case FTypedLoopPlan::EFusedKind::StateStringMapLookup:
				{
					LimitSlot = Plan.StateStrings.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::StateDualStringArrayCompare:
				{
					LimitSlot = Plan.StateStringArrays.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::StateIndirectVirtualTransform:
				{
					if (Plan.StateVirtual.bLimitImmediate)
					{
						return false;
					}
					LimitSlot = Plan.StateVirtual.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::StateArrayVirtualTransform:
				{
					LimitSlot = Plan.StateArrayVirtual.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::StateMapF64Update:
				{
					LimitSlot = Plan.StateMapF64.LimitSlot;
					break;
				}
			case FTypedLoopPlan::EFusedKind::DirectIndexedUpdate:
			case FTypedLoopPlan::EFusedKind::DirectIndexedSetContains:
			case FTypedLoopPlan::EFusedKind::StateIndirectIndexedUpdate:
			case FTypedLoopPlan::EFusedKind::StateIndirectSetContains:
			case FTypedLoopPlan::EFusedKind::IndirectArrayUpdate:
			case FTypedLoopPlan::EFusedKind::IndirectMapUpdate:
			case FTypedLoopPlan::EFusedKind::IndirectSetContains:
				{
					LimitSlot = Plan.Indirect.LimitSlot;
					break;
				}
			default:
				{
					break;
				}
			}
			return LimitSlot != 0xFFFFu && std::any_of(Plan.Instructions.begin(), Plan.Instructions.end(), [LimitSlot](const FTypedLoopInstruction& Instruction)
			{
				return InstructionWritesLocal(Instruction, LimitSlot);
			});
		}

		bool HasDuplicateValidSlots(
		    const std::initializer_list<std::uint16_t> Slots) noexcept
		{
			for (auto Left = Slots.begin(); Left != Slots.end(); ++Left)
			{
				if (*Left == 0xFFFFu)
				{
					continue;
				}
				for (auto Right = std::next(Left); Right != Slots.end(); ++Right)
				{
					if (*Left == *Right)
					{
						return true;
					}
				}
			}
			return false;
		}

		bool SlotAliasesAny(const std::uint16_t Slot,
		    const std::initializer_list<std::uint16_t> Others) noexcept
		{
			if (Slot == 0xFFFFu)
			{
				return false;
			}
			return std::any_of(Others.begin(), Others.end(),
			    [Slot](const std::uint16_t Other)
			{
				return Other != 0xFFFFu && Slot == Other;
			});
		}

		// Fused executors deliberately keep loop-carried state in independent native
		// variables and commit it at semantic exits. Source locals are nevertheless
		// allowed to alias those roles (for example, one local can be both the loop
		// counter and a Set hit counter). Such bytecode is valid, but fusion would
		// change its write ordering. Keep all alias-sensitive plans on the ordinary
		// typed opcode path. Scratch locals may alias one another where the executor
		// preserves their bytecode order, but never a cached loop-carried local.
		bool FusedPlanHasUnsafeLocalAliases(const FTypedLoopPlan& Plan) noexcept
		{
			using EKind = FTypedLoopPlan::EFusedKind;
			switch (Plan.FusedKind)
			{
			case EKind::None:
				return false;
			case EKind::DirectConstantScalarIndexedUpdate:
				return HasDuplicateValidSlots({Plan.DirectIndexed.CounterSlot,
				    Plan.DirectIndexed.StateSlot,
				    Plan.DirectIndexed.StoredValueSlot});
			case EKind::DirectAffineIndexedUpdate:
				{
					const auto& D = Plan.DirectIndexed;
					const std::uint16_t Limit = D.bLimitImmediate
					    ? 0xFFFFu
					    : D.LimitSlot;
					const auto Cached = {D.CounterSlot, Limit, D.StateSlot, D.TotalSlot};
					if (HasDuplicateValidSlots(Cached))
					{
						return true;
					}
					return (!D.bDeltaConstant && SlotAliasesAny(D.DeltaSlot, Cached)) || SlotAliasesAny(D.StoredValueSlot, Cached);
				}
			case EKind::DirectAffineSetContains:
				{
					const auto& D = Plan.DirectSet;
					return HasDuplicateValidSlots({D.CounterSlot,
					    D.bLimitImmediate ? std::uint16_t{0xFFFFu} : D.LimitSlot,
					    D.StateSlot, D.TotalSlot});
				}
			case EKind::DirectFieldAccumulate:
				return HasDuplicateValidSlots({Plan.DirectFieldAccumulator.CounterSlot,
				    Plan.DirectFieldAccumulator.LimitSlot,
				    Plan.DirectFieldAccumulator.TotalSlot});
			case EKind::DirectFieldBatchUpdate:
				return HasDuplicateValidSlots({Plan.DirectFieldBatch.CounterSlot,
				    Plan.DirectFieldBatch.LimitSlot});
			case EKind::DirectStateFieldAccumulate:
				return HasDuplicateValidSlots({Plan.DirectFields.CounterSlot,
				    Plan.DirectFields.LimitSlot, Plan.DirectFields.StateSlot,
				    Plan.DirectFields.ChoiceSlot, Plan.DirectFields.TotalSlot});
			case EKind::StateIndirectFieldUpdate:
				{
					const auto& D = Plan.StateFields;
					const auto Cached = {D.CounterSlot, D.LimitSlot, D.StateSlot,
					    D.PositionSlot, D.ChoiceSlot, D.DeltaSlot};
					if (HasDuplicateValidSlots(Cached))
					{
						return true;
					}
					for (std::size_t Index = 0; Index < D.FieldCount; ++Index)
					{
						if (SlotAliasesAny(D.TemporarySlots[Index], Cached))
						{
							return true;
						}
					}
					return false;
				}
			case EKind::StateIndirectStringLookupCompare:
				return HasDuplicateValidSlots({Plan.StateStrings.CounterSlot,
				    Plan.StateStrings.LimitSlot, Plan.StateStrings.StateSlot,
				    Plan.StateStrings.PositionSlot, Plan.StateStrings.TotalSlot});
			case EKind::StateStringMapLookup:
				return HasDuplicateValidSlots({Plan.StateStrings.CounterSlot,
				    Plan.StateStrings.LimitSlot, Plan.StateStrings.StateSlot,
				    Plan.StateStrings.TotalSlot});
			case EKind::StateDualStringArrayCompare:
				return HasDuplicateValidSlots({Plan.StateStringArrays.CounterSlot,
				    Plan.StateStringArrays.LimitSlot, Plan.StateStringArrays.StateSlot,
				    Plan.StateStringArrays.LeftIndexSlot,
				    Plan.StateStringArrays.RightIndexSlot,
				    Plan.StateStringArrays.TotalSlot});
			case EKind::StateIndirectVirtualTransform:
				return HasDuplicateValidSlots({Plan.StateVirtual.CounterSlot,
				    Plan.StateVirtual.LimitSlot, Plan.StateVirtual.StateSlot,
				    Plan.StateVirtual.PositionSlot, Plan.StateVirtual.ChoiceSlot,
				    Plan.StateVirtual.AccumulatorSlot});
			case EKind::StateArrayVirtualTransform:
				return HasDuplicateValidSlots({Plan.StateArrayVirtual.CounterSlot,
				    Plan.StateArrayVirtual.LimitSlot, Plan.StateArrayVirtual.StateSlot,
				    Plan.StateArrayVirtual.IndexSlot,
				    Plan.StateArrayVirtual.AccumulatorSlot});
			case EKind::StateMapF64Update:
				return HasDuplicateValidSlots({Plan.StateMapF64.CounterSlot,
				    Plan.StateMapF64.LimitSlot, Plan.StateMapF64.StateSlot,
				    Plan.StateMapF64.SizeSlot, Plan.StateMapF64.KeySlot,
				    Plan.StateMapF64.ValueSlot, Plan.StateMapF64.StoredValueSlot,
				    Plan.StateMapF64.TotalSlot});
			case EKind::DirectIndexedUpdate:
			case EKind::DirectIndexedSetContains:
			case EKind::StateIndirectIndexedUpdate:
			case EKind::StateIndirectSetContains:
			case EKind::IndirectArrayUpdate:
			case EKind::IndirectMapUpdate:
			case EKind::IndirectSetContains:
				{
					const auto& D = Plan.Indirect;
					const auto Cached = {
					    D.CounterSlot, D.LimitSlot, D.CursorSlot, D.TotalSlot};
					if (HasDuplicateValidSlots(Cached) || SlotAliasesAny(D.IndexOrKeySlot, Cached) || SlotAliasesAny(D.DeltaOrValueSlot, Cached) || SlotAliasesAny(D.StoredValueSlot, Cached))
					{
						return true;
					}
					// The state-indirect forms materialize a position local that is not
					// otherwise needed by the compact descriptor.
					if (Plan.FusedKind == EKind::StateIndirectIndexedUpdate)
					{
						const std::size_t PositionInstruction =
						    D.bStateUpdateHasNop ? 22u : 21u;
						return PositionInstruction < Plan.Instructions.size() && SlotAliasesAny(Plan.Instructions[PositionInstruction].Operand0, Cached);
					}
					if (Plan.FusedKind == EKind::StateIndirectSetContains)
					{
						return Plan.Instructions.size() > 22u && SlotAliasesAny(Plan.Instructions[22u].Operand0, Cached);
					}
					return false;
				}
			default:
				{
					break;
				}
			}
			return true;
		}

		void DescribeFusedIndirectLoop(
		    const FBytecodeModule& Module,
		    FTypedLoopPlan& Plan)
		{
			using EKind = FTypedLoopPlan::EFusedKind;
			using I = EOpcode;
			const auto& V = Plan.Instructions;
			const auto SameSlot = [&](const std::size_t A, const std::size_t B)
			{
				return V[A].Operand0 == V[B].Operand0;
			};
			// Read-only stable object field accumulation. Field identity and receiver
			// root are proven by the typed-plan builder, so the runtime can borrow the
			// slot once and keep both scalar operands unboxed for the whole loop.
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::LoadLocal, I::LoadField, I::Add, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 9) && SameSlot(4, 8) && V[6].Operand1 == V[5].Operand0)
			{
				Plan.FusedKind = EKind::DirectFieldAccumulate;
				auto& D = Plan.DirectFieldAccumulator;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.ObjectSlot = V[5].Operand0;
				D.TotalSlot = V[4].Operand0;
				D.FieldNameIndex = V[6].Operand0;
				D.FieldInstruction = 6u;
				D.AddInstruction = 7u;
				D.CounterInstruction = 9u;
				return;
			}

			// A straight-line batch of scalar field compound assignments has one
			// stable receiver shape for the complete loop.  Normalize any 1..32 field
			// sequence and arbitrary i32 immediates; this is deliberately structural,
			// not tied to field names, local numbers, or a benchmark iteration count.
			constexpr std::size_t BatchHeaderCount = 4u;
			constexpr std::size_t BatchFieldInstructionCount = 10u;
			constexpr std::size_t BatchTailCount = 2u;
			if (V.size() >= BatchHeaderCount + BatchFieldInstructionCount + BatchTailCount && (V.size() - BatchHeaderCount - BatchTailCount) % BatchFieldInstructionCount == 0u)
			{
				const std::size_t FieldCount =
				    (V.size() - BatchHeaderCount - BatchTailCount) / BatchFieldInstructionCount;
				const std::size_t Tail = V.size() - BatchTailCount;
				bool bShape = FieldCount >= 1u && FieldCount <= 32u && V[0].Opcode == I::LoadLocal && V[1].Opcode == I::LoadLocal && V[2].Opcode == I::Lt && V[3].Opcode == I::JmpIfFalse && V[Tail].Opcode == I::IncLocal && V[Tail + 1u].Opcode == I::Jmp && SameSlot(0, Tail);
				FTypedLoopPlan::FDirectFieldBatchI32 D;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.CounterInstruction = static_cast<std::uint16_t>(Tail);
				D.FieldCount = static_cast<std::uint8_t>(FieldCount);
				std::uint16_t ObjectSlot = 0xFFFFu;
				for (std::size_t Field = 0; bShape && Field < FieldCount; ++Field)
				{
					const std::size_t Base = BatchHeaderCount + Field * BatchFieldInstructionCount;
					bShape = V[Base].Opcode == I::LoadLocal && V[Base + 1u].Opcode == I::LoadFieldKeep && V[Base + 2u].Opcode == I::ConstI32 && V[Base + 3u].Opcode == I::Add && V[Base + 4u].Opcode == I::StoreLocal && V[Base + 5u].Opcode == I::LoadLocal && V[Base + 6u].Opcode == I::StoreField && V[Base + 7u].Opcode == I::Pop && V[Base + 8u].Opcode == I::LoadLocal && V[Base + 9u].Opcode == I::Pop && V[Base + 1u].Operand0 == V[Base + 6u].Operand0 && V[Base].Operand0 == V[Base + 1u].Operand1 && V[Base].Operand0 == V[Base + 6u].Operand1 && V[Base + 4u].Operand0 == V[Base + 5u].Operand0 && V[Base + 4u].Operand0 == V[Base + 8u].Operand0;
					if (!bShape)
					{
						break;
					}
					if (ObjectSlot == 0xFFFFu)
					{
						ObjectSlot = V[Base].Operand0;
					}
					else
					{
						bShape = ObjectSlot == V[Base].Operand0;
					}
					D.FieldNameIndices[Field] = V[Base + 1u].Operand0;
					D.Deltas[Field] = V[Base + 2u].Immediate;
					D.AdditionInstructions[Field] =
					    static_cast<std::uint16_t>(Base + 3u);
				}
				if (bShape)
				{
					D.ObjectSlot = ObjectSlot;
					Plan.FusedKind = EKind::DirectFieldBatchUpdate;
					Plan.DirectFieldBatch = D;
					return;
				}
			}
			// General direct indexed read/modify/write loop. Array and Map share the
			// same bytecode; the bound local kind selects the raw container at runtime.
			// Constants, slots, bounds and failure sites remain entirely data driven.
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::LoadLocal, I::ConstI32, I::Mod, I::Add, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::SetIndex, I::StoreLocal, I::LoadLocal, I::Pop, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 43) && SameSlot(4, 11) && SameSlot(4, 12) && SameSlot(4, 21) && SameSlot(15, 17) && SameSlot(15, 19) && SameSlot(15, 37) && SameSlot(16, 18) && SameSlot(16, 32) && SameSlot(16, 36) && SameSlot(29, 30) && SameSlot(29, 33) && SameSlot(35, 42))
			{
				Plan.FusedKind = EKind::DirectIndexedUpdate;
				auto& D = Plan.Indirect;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.CursorSlot = V[4].Operand0;
				D.ContainerSlot = V[16].Operand0;
				D.IndexOrKeySlot = V[15].Operand0;
				D.StoredValueSlot = V[29].Operand0;
				D.TotalSlot = V[35].Operand0;
				D.CursorMultiplier = V[5].Immediate;
				D.CursorIncrement = V[7].Immediate;
				D.CursorModulus = V[9].Immediate;
				D.SecondaryIndexModulus = V[13].Immediate;
				D.DeltaModulus = V[22].Immediate;
				D.DeltaAddend = V[25].Immediate;
				D.ElementModulus = V[27].Immediate;
				D.TotalModulus = V[40].Immediate;
				return;
			}

			// Normalize the compiler's ordinary compound indexed assignment lowering.
			// Unlike the legacy canonical shape above, this form materializes the delta,
			// index, receiver and stored value in temporaries.  The descriptor records
			// semantic slots and source failure sites, so Array and Map use one proven
			// typed loop regardless of local numbering or arithmetic constants.
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::ConstI32, I::Mod, I::ConstI32, I::Add, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::StoreLocal, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::LoadLocal, I::Add, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::SetIndex, I::StoreLocal, I::LoadLocal, I::Pop, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 43) && SameSlot(4, 11) && SameSlot(4, 12) && SameSlot(4, 19) && SameSlot(4, 37) && SameSlot(17, 25) && SameSlot(18, 32) && SameSlot(18, 36) && SameSlot(20, 23) && SameSlot(20, 29) && SameSlot(21, 22) && SameSlot(21, 28) && SameSlot(27, 30) && SameSlot(27, 33) && SameSlot(35, 42))
			{
				Plan.FusedKind = EKind::DirectAffineIndexedUpdate;
				auto& D = Plan.DirectIndexed;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.StateSlot = V[4].Operand0;
				D.DeltaSlot = V[17].Operand0;
				D.ContainerSlot = V[18].Operand0;
				D.StoredValueSlot = V[27].Operand0;
				D.TotalSlot = V[35].Operand0;
				D.StateMultiplier = V[5].Immediate;
				D.StateIncrement = V[7].Immediate;
				D.StateModulus = V[9].Immediate;
				D.DeltaModulus = V[13].Immediate;
				D.DeltaAddend = V[15].Immediate;
				D.TotalModulus = V[40].Immediate;
				D.StateMulInstruction = 6u;
				D.StateAddInstruction = 8u;
				D.StateModInstruction = 10u;
				D.DeltaModInstruction = 14u;
				D.DeltaAddInstruction = 16u;
				D.LookupInstruction = 24u;
				D.ElementAddInstruction = 26u;
				D.TotalAddInstruction = 39u;
				D.TotalModInstruction = 41u;
				D.CounterInstruction = 43u;
				Plan.Indirect.CounterSlot = D.CounterSlot;
				Plan.Indirect.LimitSlot = D.LimitSlot;
				Plan.Indirect.CursorSlot = D.StateSlot;
				Plan.Indirect.DeltaOrValueSlot = D.DeltaSlot;
				Plan.Indirect.ContainerSlot = D.ContainerSlot;
				Plan.Indirect.StoredValueSlot = D.StoredValueSlot;
				Plan.Indirect.TotalSlot = D.TotalSlot;
				return;
			}

			// Constant-bound primitive map/array read-modify-write. Integer and
			// floating constants share two bytecode load opcodes; their exact scalar
			// identity remains attached to the decoded typed instruction. Fuse the
			// complete semantic CFG for every supported primitive value type.
			if (MatchOpcodesWithWildcard(V, {I::JmpIfLocalGeImm, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::Nop, I::Add, I::StoreLocal, I::LoadLocal, I::SetIndex, I::StoreLocal, I::LoadLocal, I::Pop, I::IncLocal, I::Jmp}, 14u) && (V[14].Opcode == I::ConstI32 || V[14].Opcode == I::ConstF64) && SameSlot(0, 22) && SameSlot(1, 8) && SameSlot(1, 10) && SameSlot(1, 12) && SameSlot(9, 11) && SameSlot(9, 19) && SameSlot(16, 17) && SameSlot(16, 20))
			{
				Plan.FusedKind = EKind::DirectConstantScalarIndexedUpdate;
				auto& D = Plan.DirectIndexed;
				D.CounterSlot = V[0].Operand0;
				D.bLimitImmediate = true;
				D.LimitImmediate = V[0].Immediate;
				D.StateSlot = V[1].Operand0;
				D.ContainerSlot = V[9].Operand0;
				D.StoredValueSlot = V[16].Operand0;
				D.bDeltaConstant = true;
				D.DeltaAddend = V[14].Immediate;
				D.StateMultiplier = V[2].Immediate;
				D.StateIncrement = V[4].Immediate;
				D.StateModulus = V[6].Immediate;
				D.StateMulInstruction = 3u;
				D.StateAddInstruction = 5u;
				D.StateModInstruction = 7u;
				D.LookupInstruction = 13u;
				D.ElementAddInstruction = 15u;
				D.CounterInstruction = 22u;
				Plan.Indirect.CounterSlot = D.CounterSlot;
				Plan.Indirect.CursorSlot = D.StateSlot;
				Plan.Indirect.ContainerSlot = D.ContainerSlot;
				return;
			}

			// Constant-bound loops use JmpIfLocalGeImm and the non-compound spelling
			// `container[key] = container[key] + constant`.  Normalize that lowering to
			// the same direct affine descriptor instead of forcing the generic opcode
			// dispatcher to execute dozens of typed instructions per iteration.
			if (MatchOpcodes(V, {I::JmpIfLocalGeImm, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::ConstI32, I::Add, I::StoreLocal, I::LoadLocal, I::SetIndex, I::StoreLocal, I::LoadLocal, I::Pop, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 30) && SameSlot(1, 8) && SameSlot(1, 10) && SameSlot(1, 12) && SameSlot(1, 24) && SameSlot(9, 11) && SameSlot(9, 19) && SameSlot(9, 23) && SameSlot(16, 17) && SameSlot(16, 20) && SameSlot(22, 29))
			{
				Plan.FusedKind = EKind::DirectAffineIndexedUpdate;
				auto& D = Plan.DirectIndexed;
				D.CounterSlot = V[0].Operand0;
				D.bLimitImmediate = true;
				D.LimitImmediate = V[0].Immediate;
				D.StateSlot = V[1].Operand0;
				D.ContainerSlot = V[9].Operand0;
				D.StoredValueSlot = V[16].Operand0;
				D.TotalSlot = V[22].Operand0;
				D.bDeltaConstant = true;
				D.DeltaAddend = V[14].Immediate;
				D.StateMultiplier = V[2].Immediate;
				D.StateIncrement = V[4].Immediate;
				D.StateModulus = V[6].Immediate;
				D.TotalModulus = V[27].Immediate;
				D.StateMulInstruction = 3u;
				D.StateAddInstruction = 5u;
				D.StateModInstruction = 7u;
				D.LookupInstruction = 13u;
				D.ElementAddInstruction = 15u;
				D.TotalAddInstruction = 26u;
				D.TotalModInstruction = 28u;
				D.CounterInstruction = 30u;
				Plan.Indirect.CounterSlot = D.CounterSlot;
				Plan.Indirect.CursorSlot = D.StateSlot;
				Plan.Indirect.ContainerSlot = D.ContainerSlot;
				Plan.Indirect.StoredValueSlot = D.StoredValueSlot;
				Plan.Indirect.TotalSlot = D.TotalSlot;
				return;
			}

			// Direct state-keyed Set membership is the same semantic loop as the
			// modulo-key form below, but the natural source spelling uses the state
			// itself as the key.  Keep it as a separate normalized descriptor so no
			// sentinel modulus or fabricated failure site is required.
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::CallMethod, I::JmpIfFalse, I::IncLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 17) && SameSlot(4, 11) && SameSlot(4, 13) && V[14].Operand0 < Module.StringPool.size() && Module.StringPool[V[14].Operand0] == "has")
			{
				Plan.FusedKind = EKind::DirectAffineSetContains;
				auto& D = Plan.DirectSet;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.StateSlot = V[4].Operand0;
				D.SetSlot = V[12].Operand0;
				D.TotalSlot = V[16].Operand0;
				D.StateMultiplier = V[5].Immediate;
				D.StateIncrement = V[7].Immediate;
				D.StateModulus = V[9].Immediate;
				D.StateMulInstruction = 6u;
				D.StateAddInstruction = 8u;
				D.StateModInstruction = 10u;
				D.TotalIncrementInstruction = 16u;
				D.CounterInstruction = 17u;
				Plan.Indirect.CounterSlot = D.CounterSlot;
				Plan.Indirect.LimitSlot = D.LimitSlot;
				Plan.Indirect.CursorSlot = D.StateSlot;
				Plan.Indirect.ContainerSlot = D.SetSlot;
				Plan.Indirect.TotalSlot = D.TotalSlot;
				return;
			}
			if (MatchOpcodes(V, {I::JmpIfLocalGeImm, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::CallMethod, I::JmpIfFalse, I::IncLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 14) && SameSlot(1, 8) && SameSlot(1, 10) && V[11].Operand0 < Module.StringPool.size() && Module.StringPool[V[11].Operand0] == "has")
			{
				Plan.FusedKind = EKind::DirectAffineSetContains;
				auto& D = Plan.DirectSet;
				D.CounterSlot = V[0].Operand0;
				D.bLimitImmediate = true;
				D.LimitImmediate = V[0].Immediate;
				D.StateSlot = V[1].Operand0;
				D.SetSlot = V[9].Operand0;
				D.TotalSlot = V[13].Operand0;
				D.StateMultiplier = V[2].Immediate;
				D.StateIncrement = V[4].Immediate;
				D.StateModulus = V[6].Immediate;
				D.StateMulInstruction = 3u;
				D.StateAddInstruction = 5u;
				D.StateModInstruction = 7u;
				D.TotalIncrementInstruction = 13u;
				D.CounterInstruction = 14u;
				Plan.Indirect.CounterSlot = D.CounterSlot;
				Plan.Indirect.CursorSlot = D.StateSlot;
				Plan.Indirect.ContainerSlot = D.SetSlot;
				Plan.Indirect.TotalSlot = D.TotalSlot;
				return;
			}

			// Stable-shape field dispatch generated from an arbitrary selector chain.
			// The field count, selector values, names, object slot and all arithmetic
			// constants are data driven.  Resolution is performed once per loop and the
			// runtime mutation callback guard below preserves observable write barriers.
			constexpr std::size_t DirectFieldPrefixCount = 16u;
			constexpr std::size_t DirectComparedFieldArmCount = 12u;
			constexpr std::size_t DirectFallbackFieldArmCount = 7u;
			constexpr std::size_t DirectFieldTailCount = 6u;
			if (V.size() >= DirectFieldPrefixCount + DirectComparedFieldArmCount + DirectFallbackFieldArmCount + DirectFieldTailCount && (V.size() - 17u) % DirectComparedFieldArmCount == 0u)
			{
				const std::size_t FieldCount =
				    (V.size() - 17u) / DirectComparedFieldArmCount;
				const auto OpAt = [&](const std::size_t Index, const I Opcode)
				{
					return Index < V.size() && V[Index].Opcode == Opcode;
				};
				bool bShape = FieldCount >= 2u && FieldCount <= 32u && OpAt(0, I::LoadLocal) && OpAt(1, I::LoadLocal) && OpAt(2, I::Lt) && OpAt(3, I::JmpIfFalse) && OpAt(4, I::LoadLocal) && OpAt(5, I::ConstI32) && OpAt(6, I::Mul) && OpAt(7, I::ConstI32) && OpAt(8, I::Add) && OpAt(9, I::ConstI32) && OpAt(10, I::Mod) && OpAt(11, I::StoreLocal) && OpAt(12, I::LoadLocal) && OpAt(13, I::ConstI32) && OpAt(14, I::Mod) && OpAt(15, I::StoreLocal) && SameSlot(0, V.size() - 2u) && SameSlot(4, 11) && SameSlot(4, 12);
				FTypedLoopPlan::FDirectStateFieldsI32 D;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.StateSlot = V[4].Operand0;
				D.ChoiceSlot = V[15].Operand0;
				D.FieldCount = static_cast<std::uint8_t>(FieldCount);
				D.StateMultiplier = V[5].Immediate;
				D.StateIncrement = V[7].Immediate;
				D.StateModulus = V[9].Immediate;
				D.ChoiceModulus = V[13].Immediate;
				D.StateMulInstruction = 6u;
				D.StateAddInstruction = 8u;
				D.StateModInstruction = 10u;
				D.ChoiceModInstruction = 14u;
				std::uint16_t ObjectSlot = 0xFFFFu;
				std::uint16_t TotalSlot = 0xFFFFu;
				const std::size_t Tail = V.size() - DirectFieldTailCount;
				for (std::size_t Field = 0; bShape && Field + 1u < FieldCount; ++Field)
				{
					const std::size_t Base = DirectFieldPrefixCount + Field * DirectComparedFieldArmCount;
					bShape = OpAt(Base, I::LoadLocal) && OpAt(Base + 1u, I::ConstI32) && OpAt(Base + 2u, I::Eq) && OpAt(Base + 3u, I::JmpIfFalse) && OpAt(Base + 4u, I::LoadLocal) && OpAt(Base + 5u, I::IncField) && OpAt(Base + 6u, I::LoadLocal) && OpAt(Base + 7u, I::LoadLocal) && OpAt(Base + 8u, I::LoadField) && OpAt(Base + 9u, I::Add) && OpAt(Base + 10u, I::StoreLocal) && OpAt(Base + 11u, I::Jmp) && V[Base].Operand0 == D.ChoiceSlot && V[Base + 3u].BranchInstruction == Base + DirectComparedFieldArmCount && V[Base + 11u].BranchInstruction == Tail && V[Base + 4u].Operand0 == V[Base + 7u].Operand0 && V[Base + 5u].Operand0 == V[Base + 8u].Operand0 && V[Base + 6u].Operand0 == V[Base + 10u].Operand0;
					if (!bShape)
					{
						break;
					}
					if (ObjectSlot == 0xFFFFu)
					{
						ObjectSlot = V[Base + 4u].Operand0;
					}
					else
					{
						bShape = ObjectSlot == V[Base + 4u].Operand0;
					}
					if (TotalSlot == 0xFFFFu)
					{
						TotalSlot = V[Base + 6u].Operand0;
					}
					else
					{
						bShape = TotalSlot == V[Base + 6u].Operand0;
					}
					D.SelectorValues[Field] = V[Base + 1u].Immediate;
					D.FieldNameIndices[Field] = V[Base + 5u].Operand0;
					D.FieldIncrementInstructions[Field] =
					    static_cast<std::uint16_t>(Base + 5u);
					D.TotalAddInstructions[Field] =
					    static_cast<std::uint16_t>(Base + 9u);
				}
				const std::size_t Fallback = DirectFieldPrefixCount + (FieldCount - 1u) * DirectComparedFieldArmCount;
				const std::size_t Last = FieldCount - 1u;
				bShape = bShape && OpAt(Fallback, I::LoadLocal) && OpAt(Fallback + 1u, I::IncField) && OpAt(Fallback + 2u, I::LoadLocal) && OpAt(Fallback + 3u, I::LoadLocal) && OpAt(Fallback + 4u, I::LoadField) && OpAt(Fallback + 5u, I::Add) && OpAt(Fallback + 6u, I::StoreLocal) && V[Fallback].Operand0 == V[Fallback + 3u].Operand0 && V[Fallback + 1u].Operand0 == V[Fallback + 4u].Operand0 && V[Fallback + 2u].Operand0 == V[Fallback + 6u].Operand0 && V[Fallback].Operand0 == ObjectSlot && V[Fallback + 2u].Operand0 == TotalSlot && OpAt(Tail, I::LoadLocal) && OpAt(Tail + 1u, I::ConstI32) && OpAt(Tail + 2u, I::Mod) && OpAt(Tail + 3u, I::StoreLocal) && OpAt(Tail + 4u, I::IncLocal) && OpAt(Tail + 5u, I::Jmp) && V[Tail].Operand0 == TotalSlot && V[Tail + 3u].Operand0 == TotalSlot && V[Tail + 4u].Operand0 == D.CounterSlot;
				if (bShape)
				{
					D.ObjectSlot = ObjectSlot;
					D.TotalSlot = TotalSlot;
					D.TotalModulus = V[Tail + 1u].Immediate;
					D.TotalModInstruction = static_cast<std::uint16_t>(Tail + 2u);
					D.CounterInstruction = static_cast<std::uint16_t>(Tail + 4u);
					D.FieldNameIndices[Last] = V[Fallback + 1u].Operand0;
					D.FieldIncrementInstructions[Last] =
					    static_cast<std::uint16_t>(Fallback + 1u);
					D.TotalAddInstructions[Last] =
					    static_cast<std::uint16_t>(Fallback + 5u);
					Plan.FusedKind = EKind::DirectStateFieldAccumulate;
					Plan.DirectFields = D;
					return;
				}
			}

			// State/position driven bounded polymorphic transform.  Parse the branch
			// chain structurally instead of pinning it to exactly four receiver types.
			// Two through eight receivers share one generation-qualified PIC protocol;
			// selector constants, receiver slots and the join instruction are all read
			// from the typed CFG.  The method is admitted only when runtime resolution
			// proves every target to be the same pure i32 transform shape.
			const auto TryDescribeStateVirtualChain = [&](
			                                              const std::size_t PrefixCount,
			                                              const auto& Initialize) -> bool
			{
				for (std::size_t ReceiverCount = 2u; ReceiverCount <= 8u; ++ReceiverCount)
				{
					const std::size_t FinalReceiver = PrefixCount + 7u * (ReceiverCount - 1u);
					const std::size_t CallBase = FinalReceiver + 2u;
					if (V.size() != CallBase + 6u)
					{
						continue;
					}
					const std::array<I, 2> FinalOpcodes = {I::LoadLocal, I::StoreLocal};
					const std::array<I, 6> CallOpcodes = {I::LoadLocal, I::LoadLocal,
					    I::CallMethod, I::StoreLocal, I::IncLocal, I::Jmp};
					if (!std::equal(FinalOpcodes.begin(), FinalOpcodes.end(),
					        V.begin() + static_cast<std::ptrdiff_t>(FinalReceiver),
					        [](const I Opcode, const FTypedLoopInstruction& Instruction)
					{
						return Opcode == Instruction.Opcode;
					}) ||
					    !std::equal(CallOpcodes.begin(), CallOpcodes.end(),
					        V.begin() + static_cast<std::ptrdiff_t>(CallBase),
					        [](const I Opcode, const FTypedLoopInstruction& Instruction)
					{
						return Opcode == Instruction.Opcode;
					}))
					{
						continue;
					}

					const std::uint16_t ChoiceSlot = V[PrefixCount - 1u].Operand0;
					const std::uint16_t SelectedSlot = V[FinalReceiver + 1u].Operand0;
					bool bValidChain = V[CallBase].Operand0 == SelectedSlot && V[CallBase + 1u].Operand0 == V[CallBase + 3u].Operand0 && V[CallBase + 2u].ArgumentCount == 1u && V[CallBase + 2u].Operand0 < Module.StringPool.size() && V[CallBase + 4u].Operand0 == V[0].Operand0;
					for (std::size_t Receiver = 0;
					    bValidChain && Receiver + 1u < ReceiverCount; ++Receiver)
					{
						const std::size_t Base = PrefixCount + 7u * Receiver;
						const std::array<I, 7> ArmOpcodes = {I::LoadLocal, I::ConstI32,
						    I::Eq, I::JmpIfFalse, I::LoadLocal, I::StoreLocal, I::Jmp};
						bValidChain = std::equal(ArmOpcodes.begin(), ArmOpcodes.end(),
						                  V.begin() + static_cast<std::ptrdiff_t>(Base),
						                  [](const I Opcode, const FTypedLoopInstruction& Instruction)
						{
							return Opcode == Instruction.Opcode;
						}) &&
						    V[Base].Operand0 == ChoiceSlot && V[Base + 5u].Operand0 == SelectedSlot && V[Base + 3u].BranchInstruction == Base + 7u && V[Base + 6u].BranchInstruction == CallBase;
					}
					if (!bValidChain)
					{
						continue;
					}

					Plan.FusedKind = EKind::StateIndirectVirtualTransform;
					auto& D = Plan.StateVirtual;
					D.ChoiceSlot = ChoiceSlot;
					D.SelectedReceiverSlot = SelectedSlot;
					D.AccumulatorSlot = V[CallBase + 1u].Operand0;
					D.MethodNameIndex = V[CallBase + 2u].Operand0;
					D.ReceiverCount = static_cast<std::uint8_t>(ReceiverCount);
					D.CallInstructionIndex = static_cast<std::uint16_t>(CallBase + 2u);
					D.CounterInstruction = static_cast<std::uint16_t>(CallBase + 4u);
					Initialize(D);
					for (std::size_t Receiver = 0; Receiver + 1u < ReceiverCount; ++Receiver)
					{
						const std::size_t Base = PrefixCount + 7u * Receiver;
						D.SelectorValues[Receiver] = V[Base + 1u].Immediate;
						D.ReceiverSlots[Receiver] = V[Base + 4u].Operand0;
					}
					D.ReceiverSlots[ReceiverCount - 1u] = V[FinalReceiver].Operand0;
					return true;
				}
				return false;
			};
			constexpr std::array<I, 27> StateVirtualPrefix = {
			    I::LoadLocal,
			    I::LoadLocal,
			    I::Lt,
			    I::JmpIfFalse,
			    I::LoadLocal,
			    I::ConstI32,
			    I::Mul,
			    I::ConstI32,
			    I::Add,
			    I::ConstI32,
			    I::Mod,
			    I::Nop,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::ConstI32,
			    I::Mod,
			    I::ConstI32,
			    I::Mul,
			    I::Add,
			    I::ConstI32,
			    I::Mod,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::GetIndexCopy,
			    I::StoreLocal,
			};
			const bool bStateVirtualPrefix = V.size() >= StateVirtualPrefix.size() && std::equal(StateVirtualPrefix.begin(), StateVirtualPrefix.end(), V.begin(), [](const I Opcode, const FTypedLoopInstruction& Instruction)
			{
				return Opcode == Instruction.Opcode;
			});
			if (bStateVirtualPrefix && SameSlot(0, 14) && SameSlot(4, 12) && SameSlot(4, 13) && SameSlot(22, 24))
			{
				if (TryDescribeStateVirtualChain(27u,
				        [&](FTypedLoopPlan::FStateIndirectVirtualI32& D)
				{
					D.CounterSlot = V[0].Operand0;
					D.LimitSlot = V[1].Operand0;
					D.StateSlot = V[4].Operand0;
					D.PositionSlot = V[22].Operand0;
					D.ChoicesSlot = V[23].Operand0;
					D.StateMultiplier = V[5].Immediate;
					D.StateIncrement = V[7].Immediate;
					D.StateModulus = V[9].Immediate;
					D.CounterModulus = V[15].Immediate;
					D.PositionMultiplier = V[17].Immediate;
					D.PositionModulus = V[20].Immediate;
					D.StateMulInstruction = 6u;
					D.StateAddInstruction = 8u;
					D.StateModInstruction = 10u;
					D.CounterModInstruction = 16u;
					D.PositionMulInstruction = 18u;
					D.PositionAddInstruction = 19u;
					D.PositionModInstruction = 21u;
					D.LookupInstruction = 25u;
				}))
				{
					return;
				}
			}

			// Constant-limit loops often fold the four-instruction header into
			// JmpIfLocalGeImm. When the choice table is indexed directly by the checked
			// affine state, retain that compact form rather than manufacturing a
			// position temporary and modulo work that do not exist in source semantics.
			constexpr std::array<I, 13> DirectStateVirtualPrefix = {
			    I::JmpIfLocalGeImm,
			    I::LoadLocal,
			    I::ConstI32,
			    I::Mul,
			    I::ConstI32,
			    I::Add,
			    I::ConstI32,
			    I::Mod,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::GetIndexCopy,
			    I::StoreLocal,
			};
			const bool bDirectStateVirtualPrefix = V.size() >= DirectStateVirtualPrefix.size() && std::equal(DirectStateVirtualPrefix.begin(), DirectStateVirtualPrefix.end(), V.begin(), [](const I Opcode, const FTypedLoopInstruction& Instruction)
			{
				return Opcode == Instruction.Opcode;
			}) &&
			    SameSlot(1, 8) && SameSlot(1, 10);
			if (bDirectStateVirtualPrefix && TryDescribeStateVirtualChain(13u, [&](FTypedLoopPlan::FStateIndirectVirtualI32& D)
			{
				D.CounterSlot = V[0].Operand0;
				D.LimitImmediate = V[0].Immediate;
				D.bLimitImmediate = true;
				D.StateSlot = V[1].Operand0;
				D.bDirectStateIndex = true;
				D.ChoicesSlot = V[9].Operand0;
				D.StateMultiplier = V[2].Immediate;
				D.StateIncrement = V[4].Immediate;
				D.StateModulus = V[6].Immediate;
				D.StateMulInstruction = 3u;
				D.StateAddInstruction = 5u;
				D.StateModInstruction = 7u;
				D.LookupInstruction = 11u;
			}))
			{
				return;
			}

			// Indirect receiver selection through a typed object array followed by a
			// two-argument pure scalar virtual call.  Receiver types and method bodies
			// are not fixed here: normal runtime dispatch supplies a generation-scoped
			// function identity, which is then matched against the verified expression
			// table built below.  This covers Array/complex-expression receivers without
			// bypassing interception, inheritance, overload resolution or hot reload.
			const auto CollectArrayMethods = [&](FTypedLoopPlan::FStateArrayVirtualI32& D)
			{
				const auto& Name = Module.StringPool[D.MethodNameIndex];
				for (std::size_t Index = 0; Index < Module.Functions.size() && Index < 0xFFFFu; ++Index)
				{
					if (!MethodLinkageLeafMatches(Module.Functions[Index].Name, Name))
					{
						continue;
					}
					FTypedLoopPlan::FPureI32MethodExpression Expression;
					if (TryBuildPureI32MethodExpression(Module, static_cast<std::uint16_t>(Index), Expression))
					{
						D.Methods.push_back(std::move(Expression));
					}
				}
			};
			if (V.size() == 25u || (V.size() == 26u && V[11].Opcode == I::Nop))
			{
				const std::size_t Padding = V.size() - 25u;
				const auto At = [&](const std::size_t Index) -> const FTypedLoopInstruction&
				{
					return V[Index + (Index >= 11u ? Padding : 0u)];
				};
				const std::array<I, 25> Shape = {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse,
				    I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod,
				    I::StoreLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Mod, I::GetIndexCopy,
				    I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::CallMethod,
				    I::StoreLocal, I::IncLocal, I::Jmp};
				bool Matches = true;
				for (std::size_t Index = 0; Index < Shape.size(); ++Index)
				{
					Matches = Matches && At(Index).Opcode == Shape[Index];
				}
				if (Matches && At(0).Operand0 == At(23).Operand0 && At(4).Operand0 == At(11).Operand0 && At(4).Operand0 == At(13).Operand0 && At(4).Operand0 == At(20).Operand0 && At(17).Operand0 == At(18).Operand0 && At(19).Operand0 == At(22).Operand0 && At(21).ArgumentCount == 2u && At(21).Operand0 < Module.StringPool.size())
				{
					FTypedLoopPlan::FStateArrayVirtualI32 D;
					D.CounterSlot = At(0).Operand0;
					D.LimitSlot = At(1).Operand0;
					D.StateSlot = At(4).Operand0;
					D.ReceiverArraySlot = At(12).Operand0;
					D.SelectedReceiverSlot = At(17).Operand0;
					D.AccumulatorSlot = At(19).Operand0;
					D.MethodNameIndex = At(21).Operand0;
					D.CallInstructionIndex = static_cast<std::uint16_t>(21u + Padding);
					D.StateMultiplier = At(5).Immediate;
					D.StateIncrement = At(7).Immediate;
					D.StateModulus = At(9).Immediate;
					D.PositionOperand = At(14).Immediate;
					D.bDirectReceiverIndex = true;
					D.PositionInstruction = static_cast<std::uint16_t>(15u + Padding);
					D.ReceiverLookupInstruction = static_cast<std::uint16_t>(16u + Padding);
					D.CounterInstruction = static_cast<std::uint16_t>(23u + Padding);
					CollectArrayMethods(D);
					if (!D.Methods.empty())
					{
						Plan.FusedKind = EKind::StateArrayVirtualTransform;
						Plan.StateArrayVirtual = std::move(D);
						return;
					}
				}
			}
			if (V.size() == 32u && MatchOpcodesWithWildcard(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::Nop, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::Add, I::ConstI32, I::BitAnd, I::GetIndexCopy, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::CallMethod, I::StoreLocal, I::IncLocal, I::Jmp}, 18u) && (V[18].Opcode == I::BitAnd || V[18].Opcode == I::Mod) && SameSlot(0, 30) && SameSlot(4, 12) && SameSlot(4, 14) && V[15].Operand0 == V[26].Operand0 && V[15].Operand0 == V[29].Operand0 && V[20].Operand0 == V[22].Operand0 && V[24].Operand0 == V[25].Operand0 && V[28].ArgumentCount == 2u && V[28].Operand0 < Module.StringPool.size())
			{
				FTypedLoopPlan::FStateArrayVirtualI32 D;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.StateSlot = V[4].Operand0;
				D.OrderSlot = V[13].Operand0;
				D.ReceiverArraySlot = V[21].Operand0;
				D.IndexSlot = V[20].Operand0;
				D.SelectedReceiverSlot = V[24].Operand0;
				D.AccumulatorSlot = V[15].Operand0;
				D.MethodNameIndex = V[28].Operand0;
				D.CallInstructionIndex = 28u;
				D.StateMultiplier = V[5].Immediate;
				D.StateIncrement = V[7].Immediate;
				D.StateModulus = V[9].Immediate;
				D.PositionOperand = V[17].Immediate;
				D.bPositionUsesMask = V[18].Opcode == I::BitAnd;

				CollectArrayMethods(D);
				if (!D.Methods.empty())
				{
					Plan.FusedKind = EKind::StateArrayVirtualTransform;
					Plan.StateArrayVirtual = std::move(D);
					return;
				}
			}

			// Type-stable map<int,double> read/modify/write with an affine integer
			// selector and a conditional floating transform. This is normalized from
			// bytecode slots/constants (not source names) and keeps the flat-map lookup,
			// f64 arithmetic and accumulation unboxed for the complete natural loop.
			const bool bStateMapF64HasInlinedCallNop = MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::Nop, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Mul, I::Add, I::LoadLocal, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::ConstF64, I::Mul, I::LoadLocal, I::ConstI32, I::Mod, I::ConstF64, I::Mul, I::Add, I::StoreLocal, I::LoadLocal, I::ConstF64, I::Gt, I::JmpIfFalse, I::LoadLocal, I::ConstF64, I::Mul, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::StoreLocal, I::LoadLocal, I::SetIndex, I::StoreLocal, I::LoadLocal, I::Pop, I::AddLocalLocal, I::IncLocal, I::Jmp});
			const bool bStateMapF64DirectRecurrence = MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Mul, I::Add, I::LoadLocal, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::ConstF64, I::Mul, I::LoadLocal, I::ConstI32, I::Mod, I::ConstF64, I::Mul, I::Add, I::StoreLocal, I::LoadLocal, I::ConstF64, I::Gt, I::JmpIfFalse, I::LoadLocal, I::ConstF64, I::Mul, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::StoreLocal, I::LoadLocal, I::SetIndex, I::StoreLocal, I::LoadLocal, I::Pop, I::AddLocalLocal, I::IncLocal, I::Jmp});
			if (bStateMapF64HasInlinedCallNop || bStateMapF64DirectRecurrence)
			{
				// The inliner preserves a source-position Nop after the state
				// recurrence. Directly written recurrences omit it. Normalize both
				// equivalent CFGs before checking slot flow and extracting constants.
				const auto PhysicalIndex = [&](const std::size_t LogicalIndex)
				{
					return !bStateMapF64HasInlinedCallNop && LogicalIndex > 11u
					    ? LogicalIndex - 1u
					    : LogicalIndex;
				};
				const auto& At = [&](const std::size_t LogicalIndex)
				    -> const FTypedLoopInstruction&
				{
					return V[PhysicalIndex(LogicalIndex)];
				};
				const auto SameLogicalSlot = [&](const std::size_t Left,
				                                 const std::size_t Right)
				{
					return At(Left).Operand0 == At(Right).Operand0;
				};
				const bool bSlotFlowMatches = SameLogicalSlot(0, 14) && SameLogicalSlot(0, 51) && SameLogicalSlot(4, 12) && SameLogicalSlot(4, 13) && SameLogicalSlot(4, 26) && SameLogicalSlot(20, 22) && SameLogicalSlot(20, 42) && SameLogicalSlot(21, 41) && SameLogicalSlot(21, 47) && SameLogicalSlot(32, 33) && SameLogicalSlot(32, 37) && SameLogicalSlot(32, 40) && SameLogicalSlot(32, 43) && SameLogicalSlot(44, 45) && SameLogicalSlot(44, 48) && At(50).Operand1 == At(32).Operand0 && At(36).BranchInstruction == PhysicalIndex(41u);
				if (bSlotFlowMatches)
				{
					Plan.FusedKind = EKind::StateMapF64Update;
					auto& D = Plan.StateMapF64;
					D.CounterSlot = At(0).Operand0;
					D.LimitSlot = At(1).Operand0;
					D.StateSlot = At(4).Operand0;
					D.SizeSlot = At(18).Operand0;
					D.MapSlot = At(21).Operand0;
					D.KeySlot = At(20).Operand0;
					D.ValueSlot = At(32).Operand0;
					D.StoredValueSlot = At(44).Operand0;
					D.TotalSlot = At(50).Operand0;
					D.StateMultiplier = At(5).Immediate;
					D.StateIncrement = At(7).Immediate;
					D.StateModulus = At(9).Immediate;
					D.KeyMultiplier = At(15).Immediate;
					D.ValueMultiplier = At(24).ImmediateF64;
					D.StateValueModulus = At(27).Immediate;
					D.StateValueScale = At(29).ImmediateF64;
					D.Threshold = At(34).ImmediateF64;
					D.ConditionalScale = At(38).ImmediateF64;
					D.bHasInlinedCallNop = bStateMapF64HasInlinedCallNop;
					return;
				}
			}

			// Two read-only string arrays indexed by independent scalar expressions.
			// Borrow the compact string storage once, cache hashes at the loop boundary,
			// and keep indexing/comparison out of the FValue/native-helper bridge.
			const bool bDualStringsHasInlinedCallNop = MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::Nop, I::StoreLocal, I::LoadLocal, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::BitAnd, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::Eq, I::JmpIfFalse, I::IncLocal, I::IncLocal, I::Jmp});
			const bool bDualStringsDirectRecurrence = MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::BitAnd, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::Eq, I::JmpIfFalse, I::IncLocal, I::IncLocal, I::Jmp});
			if (bDualStringsHasInlinedCallNop || bDualStringsDirectRecurrence)
			{
				const auto PhysicalIndex = [&](const std::size_t LogicalIndex)
				{
					return !bDualStringsHasInlinedCallNop && LogicalIndex > 11u
					    ? LogicalIndex - 1u
					    : LogicalIndex;
				};
				const auto At = [&](const std::size_t LogicalIndex)
				    -> const FTypedLoopInstruction&
				{
					return V[PhysicalIndex(LogicalIndex)];
				};
				const auto SameLogicalSlot = [&](const std::size_t Left,
				                                 const std::size_t Right)
				{
					return At(Left).Operand0 == At(Right).Operand0;
				};
				const bool bSlotFlowMatches = SameLogicalSlot(0, 18) && SameLogicalSlot(0, 34) && SameLogicalSlot(4, 12) && SameLogicalSlot(4, 13) && SameLogicalSlot(4, 17) && SameLogicalSlot(16, 26) && SameLogicalSlot(24, 29) && At(32).BranchInstruction == PhysicalIndex(34u);
				if (bSlotFlowMatches)
				{
					Plan.FusedKind = EKind::StateDualStringArrayCompare;
					auto& D = Plan.StateStringArrays;
					D.CounterSlot = At(0).Operand0;
					D.LimitSlot = At(1).Operand0;
					D.StateSlot = At(4).Operand0;
					D.LeftIndexSlot = At(16).Operand0;
					D.RightIndexSlot = At(24).Operand0;
					D.LeftArraySlot = At(25).Operand0;
					D.RightArraySlot = At(28).Operand0;
					D.TotalSlot = At(33).Operand0;
					D.StateMultiplier = At(5).Immediate;
					D.StateIncrement = At(7).Immediate;
					D.StateModulus = At(9).Immediate;
					D.LeftIndexModulus = At(14).Immediate;
					D.CounterMask = At(19).Immediate;
					D.RightIndexModulus = At(22).Immediate;
					D.bHasInlinedCallNop = bDualStringsHasInlinedCallNop;
					return;
				}
			}

			// Direct typed Set lookup driven by a checked affine state recurrence.
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Mod, I::CallMethod, I::JmpIfFalse, I::IncLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 19) && SameSlot(4, 11) && SameSlot(4, 13) && V[16].Operand0 < Module.StringPool.size() && Module.StringPool[V[16].Operand0] == "has")
			{
				Plan.FusedKind = EKind::DirectIndexedSetContains;
				auto& D = Plan.Indirect;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.CursorSlot = V[4].Operand0;
				D.ContainerSlot = V[12].Operand0;
				D.TotalSlot = V[18].Operand0;
				D.CursorMultiplier = V[5].Immediate;
				D.CursorIncrement = V[7].Immediate;
				D.CursorModulus = V[9].Immediate;
				D.SecondaryIndexModulus = V[14].Immediate;
				return;
			}

			// State-driven indirect Array/Map read-modify-write.  This is described
			// from the typed bytecode rather than source spelling: the state, counter,
			// order table, target container, temporaries and all arithmetic constants
			// may use arbitrary local slots and values.  Array and Map intentionally
			// share the descriptor because their typed indexing semantics are equal;
			// the runtime local kind selects the storage implementation.
			constexpr std::array<I, 64> StateIndirectIndexedOpcodes = {
			    I::LoadLocal,
			    I::LoadLocal,
			    I::Lt,
			    I::JmpIfFalse,
			    I::LoadLocal,
			    I::ConstI32,
			    I::Mul,
			    I::ConstI32,
			    I::Add,
			    I::ConstI32,
			    I::Mod,
			    I::Nop,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::ConstI32,
			    I::Mod,
			    I::ConstI32,
			    I::Mul,
			    I::Add,
			    I::ConstI32,
			    I::Mod,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::GetIndexCopy,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::ConstI32,
			    I::Div,
			    I::ConstI32,
			    I::Mod,
			    I::ConstI32,
			    I::Add,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::GetIndexCopy,
			    I::LoadLocal,
			    I::Add,
			    I::ConstI32,
			    I::Mod,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::SetIndex,
			    I::StoreLocal,
			    I::LoadLocal,
			    I::Pop,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::LoadLocal,
			    I::GetIndexCopy,
			    I::Add,
			    I::LoadLocal,
			    I::ConstI32,
			    I::BitAnd,
			    I::Add,
			    I::ConstI32,
			    I::Mod,
			    I::StoreLocal,
			    I::IncLocal,
			    I::Jmp,
			};
			const auto TryDescribeStateIndirectIndexed = [&](const bool bHasNop)
			{
				const std::size_t ExpectedSize = StateIndirectIndexedOpcodes.size() - (bHasNop ? 0u : 1u);
				if (V.size() != ExpectedSize)
				{
					return false;
				}
				const auto ActualIndex = [bHasNop](const std::size_t Canonical)
				{
					return Canonical - (!bHasNop && Canonical > 11u ? 1u : 0u);
				};
				for (std::size_t Canonical = 0;
				    Canonical < StateIndirectIndexedOpcodes.size(); ++Canonical)
				{
					if (!bHasNop && Canonical == 11u)
					{
						continue;
					}
					if (V[ActualIndex(Canonical)].Opcode != StateIndirectIndexedOpcodes[Canonical])
					{
						return false;
					}
				}
				const auto& A = [&](const std::size_t Canonical)
				    -> const FTypedLoopInstruction&
				{
					return V[ActualIndex(Canonical)];
				};
				const auto SameCanonicalSlot = [&](const std::size_t Left,
				                                   const std::size_t Right)
				{
					return A(Left).Operand0 == A(Right).Operand0;
				};
				if (!SameCanonicalSlot(0, 14) || !SameCanonicalSlot(0, 62) || !SameCanonicalSlot(4, 12) || !SameCanonicalSlot(4, 13) || !SameCanonicalSlot(4, 27) || !SameCanonicalSlot(22, 24) || !SameCanonicalSlot(22, 55) || !SameCanonicalSlot(35, 37) || !SameCanonicalSlot(35, 47) || !SameCanonicalSlot(35, 51) || !SameCanonicalSlot(26, 36) || !SameCanonicalSlot(26, 38) || !SameCanonicalSlot(26, 52) || !SameCanonicalSlot(34, 40) || !SameCanonicalSlot(44, 45) || !SameCanonicalSlot(44, 48) || !SameCanonicalSlot(50, 61))
				{
					return false;
				}

				Plan.FusedKind = EKind::StateIndirectIndexedUpdate;
				auto& D = Plan.Indirect;
				D.CounterSlot = A(0).Operand0;
				D.LimitSlot = A(1).Operand0;
				D.CursorSlot = A(4).Operand0;
				D.OrderSlot = A(23).Operand0;
				D.ContainerSlot = A(35).Operand0;
				D.IndexOrKeySlot = A(26).Operand0;
				D.DeltaOrValueSlot = A(34).Operand0;
				D.StoredValueSlot = A(44).Operand0;
				D.TotalSlot = A(50).Operand0;
				D.CursorMultiplier = A(5).Immediate;
				D.CursorIncrement = A(7).Immediate;
				D.CursorModulus = A(9).Immediate;
				D.SecondaryIndexModulus = A(15).Immediate;
				D.PositionMultiplier = A(17).Immediate;
				D.PositionModulus = A(20).Immediate;
				D.DeltaDivisor = A(28).Immediate;
				D.DeltaModulus = A(30).Immediate;
				D.DeltaAddend = A(32).Immediate;
				D.ElementModulus = A(42).Immediate;
				D.PositionMask = A(56).Immediate;
				D.TotalModulus = A(59).Immediate;
				D.bStateUpdateHasNop = bHasNop;
				return true;
			};
			if (TryDescribeStateIndirectIndexed(true) || TryDescribeStateIndirectIndexed(false))
			{
				return;
			}

			// The corresponding state-driven indirect Set lookup.  Both branch
			// increments and the optional key adjustment are retained in the plan so
			// the fused executor preserves source semantics for arbitrary constants.
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::Nop, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Mod, I::ConstI32, I::Mul, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::StoreLocal, I::LoadLocal, I::ConstI32, I::BitAnd, I::ConstI32, I::Ne, I::JmpIfFalse, I::IncLocal, I::LoadLocal, I::LoadLocal, I::CallMethod, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::Jmp, I::LoadLocal, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 14) && SameSlot(0, 51) && SameSlot(4, 12) && SameSlot(4, 13) && SameSlot(4, 27) && SameSlot(22, 24) && SameSlot(26, 33) && SameSlot(26, 35) && SameSlot(38, 43) && SameSlot(38, 45) && SameSlot(38, 50) && V[30].Immediate == 0 && V[41].Immediate == V[48].Immediate && V[36].Operand0 < Module.StringPool.size() && Module.StringPool[V[36].Operand0] == "has")
			{
				Plan.FusedKind = EKind::StateIndirectSetContains;
				auto& D = Plan.Indirect;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.CursorSlot = V[4].Operand0;
				D.OrderSlot = V[23].Operand0;
				D.ContainerSlot = V[34].Operand0;
				D.IndexOrKeySlot = V[26].Operand0;
				D.TotalSlot = V[38].Operand0;
				D.CursorMultiplier = V[5].Immediate;
				D.CursorIncrement = V[7].Immediate;
				D.CursorModulus = V[9].Immediate;
				D.SecondaryIndexModulus = V[15].Immediate;
				D.PositionMultiplier = V[17].Immediate;
				D.PositionModulus = V[20].Immediate;
				D.PositionMask = V[28].Immediate;
				D.TrueIncrement = V[39].Immediate;
				D.FalseIncrement = V[46].Immediate;
				D.TotalModulus = V[41].Immediate;
				return;
			}

			// State-driven indirect stable-field update.  The number and names of
			// fields, selector values, object/temporary slots and arithmetic constants
			// are derived from the typed CFG.  Each arm must join the same loop tail;
			// this proves that resolving the object shape and field slots once preserves
			// the source branch and mutation order.
			const auto OpAt = [&](const std::size_t Index, const I Opcode)
			{
				return Index < V.size() && V[Index].Opcode == Opcode;
			};
			constexpr std::size_t FieldPrefixCount = 35u;
			constexpr std::size_t ComparedFieldArmCount = 15u;
			constexpr std::size_t FallbackFieldArmCount = 10u;
			constexpr std::size_t FieldLoopTailCount = 2u;
			if (V.size() >= FieldPrefixCount + FallbackFieldArmCount + FieldLoopTailCount && (V.size() - FieldPrefixCount - FallbackFieldArmCount - FieldLoopTailCount) % ComparedFieldArmCount == 0u)
			{
				const std::size_t FieldCount =
				    (V.size() - FieldPrefixCount - FallbackFieldArmCount - FieldLoopTailCount) / ComparedFieldArmCount + 1u;
				bool bShape = FieldCount >= 2u && FieldCount <= 16u && OpAt(0, I::LoadLocal) && OpAt(1, I::LoadLocal) && OpAt(2, I::Lt) && OpAt(3, I::JmpIfFalse) && OpAt(4, I::LoadLocal) && OpAt(5, I::ConstI32) && OpAt(6, I::Mul) && OpAt(7, I::ConstI32) && OpAt(8, I::Add) && OpAt(9, I::ConstI32) && OpAt(10, I::Mod) && OpAt(11, I::Nop) && OpAt(12, I::StoreLocal) && OpAt(13, I::LoadLocal) && OpAt(14, I::LoadLocal) && OpAt(15, I::ConstI32) && OpAt(16, I::Mod) && OpAt(17, I::ConstI32) && OpAt(18, I::Mul) && OpAt(19, I::Add) && OpAt(20, I::ConstI32) && OpAt(21, I::Mod) && OpAt(22, I::StoreLocal) && OpAt(23, I::LoadLocal) && OpAt(24, I::LoadLocal) && OpAt(25, I::GetIndexCopy) && OpAt(26, I::StoreLocal) && OpAt(27, I::LoadLocal) && OpAt(28, I::ConstI32) && OpAt(29, I::Div) && OpAt(30, I::ConstI32) && OpAt(31, I::Mod) && OpAt(32, I::ConstI32) && OpAt(33, I::Add) && OpAt(34, I::StoreLocal) && SameSlot(0, V.size() - 2u) && SameSlot(4, 12) && SameSlot(4, 13) && SameSlot(4, 27) && SameSlot(0, 14) && SameSlot(22, 24);
				FTypedLoopPlan::FStateIndirectFieldsI32 Description;
				Description.CounterSlot = V[0].Operand0;
				Description.LimitSlot = V[1].Operand0;
				Description.StateSlot = V[4].Operand0;
				Description.PositionSlot = V[22].Operand0;
				Description.ChoicesSlot = V[23].Operand0;
				Description.ChoiceSlot = V[26].Operand0;
				Description.DeltaSlot = V[34].Operand0;
				Description.FieldCount = static_cast<std::uint8_t>(FieldCount);
				Description.StateMultiplier = V[5].Immediate;
				Description.StateIncrement = V[7].Immediate;
				Description.StateModulus = V[9].Immediate;
				Description.CounterModulus = V[15].Immediate;
				Description.PositionMultiplier = V[17].Immediate;
				Description.PositionModulus = V[20].Immediate;
				Description.DeltaDivisor = V[28].Immediate;
				Description.DeltaModulus = V[30].Immediate;
				Description.DeltaAddend = V[32].Immediate;
				const std::size_t LoopTail = V.size() - FieldLoopTailCount;
				std::uint16_t CommonObjectSlot = 0xFFFFu;
				for (std::size_t Arm = 0; bShape && Arm + 1u < FieldCount; ++Arm)
				{
					const std::size_t Base = FieldPrefixCount + Arm * ComparedFieldArmCount;
					bShape = OpAt(Base, I::LoadLocal) && OpAt(Base + 1u, I::ConstI32) && OpAt(Base + 2u, I::Eq) && OpAt(Base + 3u, I::JmpIfFalse) && OpAt(Base + 4u, I::LoadLocal) && OpAt(Base + 5u, I::LoadFieldKeep) && OpAt(Base + 6u, I::LoadLocal) && OpAt(Base + 7u, I::Add) && OpAt(Base + 8u, I::StoreLocal) && OpAt(Base + 9u, I::LoadLocal) && OpAt(Base + 10u, I::StoreField) && OpAt(Base + 11u, I::Pop) && OpAt(Base + 12u, I::LoadLocal) && OpAt(Base + 13u, I::Pop) && OpAt(Base + 14u, I::Jmp) && V[Base].Operand0 == Description.ChoiceSlot && V[Base + 3u].BranchInstruction == Base + ComparedFieldArmCount && V[Base + 14u].BranchInstruction == LoopTail && V[Base + 6u].Operand0 == Description.DeltaSlot && V[Base + 8u].Operand0 == V[Base + 9u].Operand0 && V[Base + 8u].Operand0 == V[Base + 12u].Operand0 && V[Base + 5u].Operand0 == V[Base + 10u].Operand0;
					if (!bShape)
					{
						break;
					}
					if (CommonObjectSlot == 0xFFFFu)
					{
						CommonObjectSlot = V[Base + 4u].Operand0;
					}
					else
					{
						bShape = V[Base + 4u].Operand0 == CommonObjectSlot;
					}
					Description.SelectorValues[Arm] = V[Base + 1u].Immediate;
					Description.FieldNameIndices[Arm] = V[Base + 5u].Operand0;
					Description.TemporarySlots[Arm] = V[Base + 8u].Operand0;
					Description.AdditionInstructionIndices[Arm] =
					    static_cast<std::uint16_t>(Base + 7u);
				}
				const std::size_t Fallback = FieldPrefixCount + (FieldCount - 1u) * ComparedFieldArmCount;
				const std::size_t Last = FieldCount - 1u;
				bShape = bShape && OpAt(Fallback, I::LoadLocal) && OpAt(Fallback + 1u, I::LoadFieldKeep) && OpAt(Fallback + 2u, I::LoadLocal) && OpAt(Fallback + 3u, I::Add) && OpAt(Fallback + 4u, I::StoreLocal) && OpAt(Fallback + 5u, I::LoadLocal) && OpAt(Fallback + 6u, I::StoreField) && OpAt(Fallback + 7u, I::Pop) && OpAt(Fallback + 8u, I::LoadLocal) && OpAt(Fallback + 9u, I::Pop) && OpAt(LoopTail, I::IncLocal) && OpAt(LoopTail + 1u, I::Jmp) && V[Fallback].Operand0 == CommonObjectSlot && V[Fallback + 2u].Operand0 == Description.DeltaSlot && V[Fallback + 4u].Operand0 == V[Fallback + 5u].Operand0 && V[Fallback + 4u].Operand0 == V[Fallback + 8u].Operand0 && V[Fallback + 1u].Operand0 == V[Fallback + 6u].Operand0;
				if (bShape)
				{
					Description.ObjectSlot = CommonObjectSlot;
					Description.FieldNameIndices[Last] = V[Fallback + 1u].Operand0;
					Description.TemporarySlots[Last] = V[Fallback + 4u].Operand0;
					Description.AdditionInstructionIndices[Last] =
					    static_cast<std::uint16_t>(Fallback + 3u);
					Plan.FusedKind = EKind::StateIndirectFieldUpdate;
					Plan.StateFields = Description;
					return;
				}
			}

			// Borrowed string Array/Map pipeline driven by an indirect position table.
			// Keeping key references borrowed across the native hash/equality operations
			// removes both FValue boxing and per-iteration string copies.
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::Nop, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Mod, I::Add, I::ConstI32, I::Mod, I::GetIndexCopy, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::GetIndexCopy, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::Eq, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 15) && SameSlot(0, 47) && SameSlot(4, 12) && SameSlot(4, 14) && SameSlot(22, 26) && SameSlot(22, 34) && SameSlot(22, 37) && SameSlot(25, 33) && SameSlot(23, 32) && SameSlot(23, 41) && SameSlot(23, 46) && V[30].Immediate == V[44].Immediate)
			{
				Plan.FusedKind = EKind::StateIndirectStringLookupCompare;
				auto& D = Plan.StateStrings;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.StateSlot = V[4].Operand0;
				D.OrderSlot = V[13].Operand0;
				D.PositionSlot = V[22].Operand0;
				D.TotalSlot = V[23].Operand0;
				D.MapSlot = V[24].Operand0;
				D.KeysSlot = V[25].Operand0;
				D.OtherSlot = V[36].Operand0;
				D.StateMultiplier = V[5].Immediate;
				D.StateIncrement = V[7].Immediate;
				D.StateModulus = V[9].Immediate;
				D.CounterModulus = V[16].Immediate;
				D.PositionModulus = V[19].Immediate;
				D.EqualityIncrement = V[42].Immediate;
				D.TotalModulus = V[30].Immediate;
				return;
			}

			// Direct state-indexed string Map lookup. The key remains a borrowed array
			// element through hashing and equality; only the scalar mapped value enters
			// the accumulator.
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Mod, I::GetIndexCopy, I::GetIndexCopy, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(0, 24) && SameSlot(4, 11) && SameSlot(4, 15) && SameSlot(12, 23))
			{
				Plan.FusedKind = EKind::StateStringMapLookup;
				auto& D = Plan.StateStrings;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.StateSlot = V[4].Operand0;
				D.TotalSlot = V[12].Operand0;
				D.MapSlot = V[13].Operand0;
				D.KeysSlot = V[14].Operand0;
				D.StateMultiplier = V[5].Immediate;
				D.StateIncrement = V[7].Immediate;
				D.StateModulus = V[9].Immediate;
				D.PositionModulus = V[16].Immediate;
				D.TotalModulus = V[21].Immediate;
				return;
			}
			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::GetIndexCopy, I::ConstI32, I::Mod, I::ConstI32, I::Add, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::StoreLocal, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::LoadLocal, I::Add, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::SetIndex, I::StoreLocal, I::LoadLocal, I::Pop, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(4, 11) && SameSlot(4, 13) && SameSlot(4, 17) && SameSlot(12, 16) && SameSlot(15, 29) && SameSlot(15, 47) && SameSlot(27, 35) && SameSlot(28, 42) && SameSlot(28, 46) && SameSlot(30, 33) && SameSlot(30, 39) && SameSlot(31, 32) && SameSlot(31, 38) && SameSlot(37, 40) && SameSlot(37, 43) && SameSlot(45, 52) && SameSlot(0, 53))
			{
				Plan.FusedKind = EKind::IndirectArrayUpdate;
				auto& D = Plan.Indirect;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.CursorSlot = V[4].Operand0;
				D.OrderSlot = V[12].Operand0;
				D.ContainerSlot = V[28].Operand0;
				D.IndexOrKeySlot = V[15].Operand0;
				D.DeltaOrValueSlot = V[27].Operand0;
				D.StoredValueSlot = V[37].Operand0;
				D.TotalSlot = V[45].Operand0;
				D.CursorMultiplier = V[5].Immediate;
				D.CursorIncrement = V[7].Immediate;
				D.CursorModulus = V[9].Immediate;
				D.SecondaryOffset = V[18].Immediate;
				D.SecondaryIndexModulus = V[20].Immediate;
				D.DeltaModulus = V[23].Immediate;
				D.DeltaAddend = V[25].Immediate;
				D.TotalModulus = V[50].Immediate;
				return;
			}

			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::GetIndexCopy, I::ConstI32, I::Mod, I::ConstI32, I::Add, I::Add, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::StoreLocal, I::LoadLocal, I::SetIndex, I::StoreLocal, I::LoadLocal, I::Pop, I::LoadLocal, I::LoadLocal, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::IncLocal, I::Jmp}) && SameSlot(4, 11) && SameSlot(4, 13) && SameSlot(4, 22) && SameSlot(12, 21) && SameSlot(15, 17) && SameSlot(15, 35) && SameSlot(16, 34) && SameSlot(16, 40) && SameSlot(19, 20) && SameSlot(19, 36) && SameSlot(19, 44) && SameSlot(37, 38) && SameSlot(37, 41) && SameSlot(43, 48) && SameSlot(0, 49))
			{
				Plan.FusedKind = EKind::IndirectMapUpdate;
				auto& D = Plan.Indirect;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.CursorSlot = V[4].Operand0;
				D.OrderSlot = V[12].Operand0;
				D.ContainerSlot = V[16].Operand0;
				D.IndexOrKeySlot = V[15].Operand0;
				D.DeltaOrValueSlot = V[19].Operand0;
				D.StoredValueSlot = V[19].Operand0;
				D.TotalSlot = V[43].Operand0;
				D.CursorMultiplier = V[5].Immediate;
				D.CursorIncrement = V[7].Immediate;
				D.CursorModulus = V[9].Immediate;
				D.SecondaryOffset = V[23].Immediate;
				D.SecondaryIndexModulus = V[25].Immediate;
				D.DeltaModulus = V[28].Immediate;
				D.DeltaAddend = V[30].Immediate;
				D.TotalModulus = V[46].Immediate;
				return;
			}

			if (MatchOpcodes(V, {I::LoadLocal, I::LoadLocal, I::Lt, I::JmpIfFalse, I::LoadLocal, I::ConstI32, I::Mul, I::ConstI32, I::Add, I::ConstI32, I::Mod, I::StoreLocal, I::LoadLocal, I::LoadLocal, I::LoadLocal, I::GetIndexCopy, I::CallMethod, I::JmpIfFalse, I::IncLocal, I::IncLocal, I::Jmp}) && SameSlot(4, 11) && SameSlot(4, 14) && SameSlot(0, 19))
			{
				Plan.FusedKind = EKind::IndirectSetContains;
				auto& D = Plan.Indirect;
				D.CounterSlot = V[0].Operand0;
				D.LimitSlot = V[1].Operand0;
				D.CursorSlot = V[4].Operand0;
				D.ContainerSlot = V[12].Operand0;
				D.OrderSlot = V[13].Operand0;
				D.TotalSlot = V[18].Operand0;
				D.CursorMultiplier = V[5].Immediate;
				D.CursorIncrement = V[7].Immediate;
				D.CursorModulus = V[9].Immediate;
			}
		}

		bool TryComputeTypedLoopStackDepth(
		    std::vector<FTypedLoopInstruction>& Instructions,
		    std::uint16_t& OutMaximumDepth, bool& bOutOwnsStringTemporaries)
		{
			bOutOwnsStringTemporaries = std::any_of(Instructions.begin(), Instructions.end(),
			    [](const FTypedLoopInstruction& Instruction)
			{
				return Instruction.Opcode == EOpcode::CallBuiltin && static_cast<EBuiltinId>(Instruction.Immediate) == EBuiltinId::ToString;
			});
			OutMaximumDepth = 0;
			if (Instructions.empty())
			{
				return false;
			}
			constexpr std::int32_t UnknownDepth = -1;
			std::vector<std::int32_t> EntryDepth(
			    Instructions.size(), UnknownDepth);
			std::vector<std::uint32_t> Worklist;
			Worklist.reserve(Instructions.size());
			EntryDepth[0] = 0;
			Worklist.push_back(0u);
			std::uint32_t MaximumDepth = 0;

			const auto StackEffect = [](const FTypedLoopInstruction& Instruction,
			                             std::int32_t& OutPops, std::int32_t& OutPushes)
			{
				OutPops = 0;
				OutPushes = 0;
				switch (Instruction.Opcode)
				{
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
				case EOpcode::ConstStr:
				case EOpcode::LoadLocal:
					{
						OutPushes = 1;
						return true;
					}
				case EOpcode::CallBuiltin:
					{
						OutPops = static_cast<std::int32_t>(Instruction.ArgumentCount);
						OutPushes = 1;
						return true;
					}
				case EOpcode::StoreLocal:
				case EOpcode::Pop:
				case EOpcode::IncField:
				case EOpcode::JmpIfFalse:
					{
						OutPops = 1;
						return true;
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
				case EOpcode::GetIndexCopy:
					{
						OutPops = 2;
						OutPushes = 1;
						return true;
					}
				case EOpcode::SetIndex:
					{
						OutPops = 3;
						OutPushes = 1;
						return true;
					}
				case EOpcode::StoreField:
					{
						OutPops = 2;
						OutPushes = 1;
						return true;
					}
				case EOpcode::CallMethod:
				case EOpcode::CallBoundMethod:
					{
						OutPops = static_cast<std::int32_t>(Instruction.ArgumentCount) + 1;
						OutPushes = 1;
						return true;
					}
				case EOpcode::LoadField:
					{
						OutPops = 1;
						OutPushes = 1;
						return true;
					}
				case EOpcode::LoadFieldKeep:
					{
						OutPops = 1;
						OutPushes = 2;
						return true;
					}
				case EOpcode::Dup:
					{
						OutPops = 1;
						OutPushes = 2;
						return true;
					}
				case EOpcode::Neg:
				case EOpcode::BitNot:
				case EOpcode::Not:
					{
						OutPops = 1;
						OutPushes = 1;
						return true;
					}
				case EOpcode::Nop:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::AddLocalLocal:
				case EOpcode::Jmp:
				case EOpcode::JmpIfLocalGeImm:
					return true;
				default:
					return false;
				}
			};

			const auto MergeDepth = [&](const std::uint32_t Target,
			                            const std::int32_t Depth) -> bool
			{
				if (Target >= EntryDepth.size())
				{
					return false;
				}
				if (EntryDepth[Target] == UnknownDepth)
				{
					EntryDepth[Target] = Depth;
					Worklist.push_back(Target);
					return true;
				}
				return EntryDepth[Target] == Depth;
			};

			while (!Worklist.empty())
			{
				const std::uint32_t Index = Worklist.back();
				Worklist.pop_back();
				FTypedLoopInstruction& Instruction = Instructions[Index];
				std::int32_t Pops = 0;
				std::int32_t Pushes = 0;
				if (!StackEffect(Instruction, Pops, Pushes) || EntryDepth[Index] < Pops)
				{
					return false;
				}
				Instruction.EntryStackDepth = static_cast<std::uint16_t>(
				    EntryDepth[Index]);
				const std::int32_t ExitDepth = EntryDepth[Index] - Pops + Pushes;
				if (ExitDepth < 0 || ExitDepth > static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max()))
				{
					return false;
				}
				MaximumDepth = std::max(
				    MaximumDepth, static_cast<std::uint32_t>(ExitDepth));

				const auto MergeBranch = [&]() -> bool
				{
					if (Instruction.BranchInstruction != InvalidInstruction)
					{
						// An owning conversion slot may be reused only after every previous
						// operand has been consumed, including inner-loop back edges.
						if (bOutOwnsStringTemporaries && Instruction.BranchInstruction <= Index && ExitDepth != 0)
						{
							return false;
						}
						return MergeDepth(Instruction.BranchInstruction, ExitDepth);
					}
					// The typed executor owns a private operand stack. An exit may
					// transfer control back to Linear only when the loop leaves it empty.
					return ExitDepth == 0;
				};
				if (Instruction.Opcode == EOpcode::Jmp)
				{
					if (!MergeBranch())
					{
						return false;
					}
					continue;
				}
				if (Instruction.Opcode == EOpcode::JmpIfFalse || Instruction.Opcode == EOpcode::JmpIfLocalGeImm)
				{
					if (!MergeBranch())
					{
						return false;
					}
				}
				if (Instruction.NextInstruction == InvalidInstruction)
				{
					return false;
				}
				if (!MergeDepth(Instruction.NextInstruction, ExitDepth))
				{
					return false;
				}
			}

			OutMaximumDepth = static_cast<std::uint16_t>(MaximumDepth);
			return true;
		}

		bool MayUseWideIntegers(const FBytecodeModule& Module, const FBytecodeFunction& Function)
		{
			JitIr::FSignature Signature;
			const auto IsWide = [](const JitIr::EValueType Type)
			{
				return Type == JitIr::EValueType::I64 || Type == JitIr::EValueType::U64;
			};
			if (JitIr::ParseSignature(Function.ClosureTypeName, Signature) && (IsWide(Signature.ReturnType) || std::any_of(Signature.Parameters.begin(), Signature.Parameters.end(), IsWide)))
			{
				return true;
			}
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Function.Code.size() - Ip) return false;
				// A wide argument consumed immediately by an instance call does not
				// establish a wide local for a later loop (for example reserve(size)).
				// Producers inside the actual loop are checked independently below.
				const auto Next = Ip + Size;
				const bool bConsumedByMethod = Next < Function.Code.size() && static_cast<EOpcode>(Function.Code[Next]) == EOpcode::CallMethod;
				const bool bNarrowed = Next + 3u <= Function.Code.size() && static_cast<EOpcode>(Function.Code[Next]) == EOpcode::CallBuiltin && (static_cast<EBuiltinId>(Function.Code[Next + 1u]) == EBuiltinId::ToInt || static_cast<EBuiltinId>(Function.Code[Next + 1u]) == EBuiltinId::ToUnsigned || static_cast<EBuiltinId>(Function.Code[Next + 1u]) == EBuiltinId::ToFloat || static_cast<EBuiltinId>(Function.Code[Next + 1u]) == EBuiltinId::ToDouble);
				if (Op == EOpcode::CallBuiltin)
				{
					const auto Id = static_cast<EBuiltinId>(Function.Code[Ip + 1u]);
					if (!bConsumedByMethod && !bNarrowed && (Id == EBuiltinId::ToInt64 || Id == EBuiltinId::ToUInt64 || Id == EBuiltinId::Len))
					{
						return true;
					}
				}
				if (Op == EOpcode::ConstI32)
				{
					const auto Index = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
					if (!bConsumedByMethod && !bNarrowed && Index < Module.Constants.size() && (Module.Constants[Index].Kind == EConstantKind::I64 || Module.Constants[Index].Kind == EConstantKind::U64))
					{
						return true;
					}
				}
				Ip += Size;
			}
			return false;
		}

		bool TryBuildPlan(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::size_t Header,
		    const std::size_t BackEdgeIp,
		    const bool bFunctionMayUseWideIntegers,
		    const std::vector<JitIr::EValueType>* const LocalTypes,
		    const std::vector<bool>& UnsupportedWideLocals,
		    const std::vector<std::size_t>* const TextAddOffsets,
		    const std::vector<std::size_t>* const UnsupportedTextFormatOffsets,
		    FTypedLoopPlan& OutPlan)
		{
			const auto& Code = Function.Code;
			const EOpcode BackEdgeOpcode = static_cast<EOpcode>(Code[BackEdgeIp]);
			std::size_t BackTarget = 0;
			if (BackEdgeOpcode != EOpcode::Jmp || !TryJumpTarget(Code, BackEdgeIp, BackEdgeOpcode, BackTarget) || BackTarget != Header)
			{
				return false;
			}
			const std::size_t RegionEnd = BackEdgeIp + Bytecode::InstructionSize(BackEdgeOpcode);
			if (Header >= RegionEnd || RegionEnd > Code.size())
			{
				return false;
			}

			std::vector<std::size_t> Offsets;
			std::unordered_map<std::size_t, std::uint32_t> OffsetToInstruction;
			bool bUsesContainer = false;
			bool bUsesDirectField = false;
			bool bUsesWide = bFunctionMayUseWideIntegers && !LocalTypes;
			bool bUsesUnsupportedWideValues = false;
			bool bUsesStringConversions = false;
			bool bUsesText = false;
			std::size_t ExitOffset = 0;
			for (std::size_t Ip = Header; Ip < RegionEnd;)
			{
				if (Offsets.size() >= MaxTypedLoopInstructions)
				{
					return false;
				}
				const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if (Size == 0 || Size > RegionEnd - Ip || !IsTypedLoopOpcode(Module, Code, Ip, Opcode, bUsesContainer))
				{
					return false;
				}
				bUsesText = bUsesText || Opcode == EOpcode::ConstStr || Opcode == EOpcode::Call
					|| (Opcode == EOpcode::CallBuiltin && static_cast<EBuiltinId>(Code[Ip + 1u]) == EBuiltinId::CharCode)
					|| ((Opcode == EOpcode::Add || Opcode == EOpcode::AddLocalLocal) && TextAddOffsets && std::find(TextAddOffsets->begin(), TextAddOffsets->end(), Ip) != TextAddOffsets->end());
				if (Opcode == EOpcode::ConstI32)
				{
					std::int32_t Ignored = 0;
					bool bIgnoredU32 = false;
					const auto ConstantIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
					const bool Wide = ConstantIndex < Module.Constants.size() && (Module.Constants[ConstantIndex].Kind == EConstantKind::I64 || Module.Constants[ConstantIndex].Kind == EConstantKind::U64);
					bUsesWide = bUsesWide || Wide;
					if (!Wide && !TryReadTypedIntegerConstant(Module, Code, Ip, Ignored, bIgnoredU32))
					{
						return false;
					}
				}
				if (Opcode == EOpcode::CallBuiltin)
				{
					const auto Id = static_cast<EBuiltinId>(Code[Ip + 1u]);
					if (Id == EBuiltinId::ToString && UnsupportedTextFormatOffsets &&
						std::binary_search(UnsupportedTextFormatOffsets->begin(), UnsupportedTextFormatOffsets->end(), Ip))
					{
						return false;
					}
					bUsesStringConversions = bUsesStringConversions || Id == EBuiltinId::ToString;
					bUsesWide = bUsesWide || Id == EBuiltinId::ToInt64 || Id == EBuiltinId::ToUInt64 || Id == EBuiltinId::Len;
					bUsesUnsupportedWideValues = bUsesUnsupportedWideValues || Id == EBuiltinId::ToFloat || Id == EBuiltinId::ToDouble;
				}
				if (LocalTypes && (Opcode == EOpcode::LoadLocal || Opcode == EOpcode::StoreLocal || Opcode == EOpcode::IncLocal || Opcode == EOpcode::AddLocalImm || Opcode == EOpcode::AddLocalLocal || Opcode == EOpcode::JmpIfLocalGeImm))
				{
					const auto IsWideLocal = [&](const std::size_t Operand)
					{
						const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + Operand);
						return Slot < LocalTypes->size() && ((*LocalTypes)[Slot] == JitIr::EValueType::I64 || (*LocalTypes)[Slot] == JitIr::EValueType::U64);
					};
					bUsesWide = bUsesWide || IsWideLocal(1u) || (Opcode == EOpcode::AddLocalLocal && IsWideLocal(3u));
				}
				if (Opcode == EOpcode::LoadLocal || Opcode == EOpcode::StoreLocal || Opcode == EOpcode::IncLocal || Opcode == EOpcode::AddLocalImm || Opcode == EOpcode::AddLocalLocal || Opcode == EOpcode::JmpIfLocalGeImm)
				{
					const auto Slot = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
					bUsesUnsupportedWideValues = bUsesUnsupportedWideValues || (Slot < UnsupportedWideLocals.size() && UnsupportedWideLocals[Slot]);
					if (Opcode == EOpcode::AddLocalLocal)
					{
						const auto Source = Bytecode::ReadU16LE(Code.data() + Ip + 3u);
						bUsesUnsupportedWideValues = bUsesUnsupportedWideValues || (Source < UnsupportedWideLocals.size() && UnsupportedWideLocals[Source]);
					}
				}
				if (Opcode == EOpcode::LoadField || Opcode == EOpcode::LoadFieldKeep || Opcode == EOpcode::StoreField || Opcode == EOpcode::IncField)
				{
					bUsesDirectField = true;
				}
				else if (Opcode == EOpcode::ConstF64)
				{
					bUsesUnsupportedWideValues = true;
					double Ignored = 0.0;
					if (!TryReadF64Constant(Module, Code, Ip, Ignored))
					{
						return false;
					}
				}
				if (Opcode == EOpcode::Jmp || Opcode == EOpcode::JmpIfFalse || Opcode == EOpcode::JmpIfLocalGeImm)
				{
					std::size_t Target = 0;
					if (!TryJumpTarget(Code, Ip, Opcode, Target))
					{
						return false;
					}
					if (Target < Header)
					{
						return false;
					}
					if (Target >= RegionEnd)
					{
						if (Opcode == EOpcode::Jmp && Ip != BackEdgeIp)
						{
							return false;
						}
						if (ExitOffset != 0 && ExitOffset != Target)
						{
							return false;
						}
						ExitOffset = Target;
					}
				}
				OffsetToInstruction.emplace(Ip, static_cast<std::uint32_t>(Offsets.size()));
				Offsets.push_back(Ip);
				Ip += Size;
			}
			if ((!bUsesContainer && !bUsesDirectField && !bUsesWide && !bUsesText) || ExitOffset == 0 || ((bUsesWide || bUsesStringConversions) && bUsesUnsupportedWideValues))
			{
				return false;
			}

			OutPlan = {};
			OutPlan.bWideCandidate = bUsesWide;
			OutPlan.bUsesTextOperations = bUsesText;
			OutPlan.HeaderOffset = static_cast<std::uint32_t>(Header);
			OutPlan.ExitOffset = static_cast<std::uint32_t>(ExitOffset);
			OutPlan.LocalCount = Function.LocalCount;
			OutPlan.Instructions.reserve(Offsets.size());
			for (std::size_t Index = 0; Index < Offsets.size(); ++Index)
			{
				const std::size_t Ip = Offsets[Index];
				const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
				FTypedLoopInstruction Instruction;
				Instruction.Opcode = Opcode;
				Instruction.BytecodeOffset = static_cast<std::uint32_t>(Ip);
				Instruction.NextInstruction = Index + 1u < Offsets.size()
				    ? static_cast<std::uint32_t>(Index + 1u)
				    : InvalidInstruction;
				Instruction.bTextAdd = (Opcode == EOpcode::Add || Opcode == EOpcode::AddLocalLocal) && TextAddOffsets
					&& std::find(TextAddOffsets->begin(), TextAddOffsets->end(), Ip) != TextAddOffsets->end();
				switch (Opcode)
				{
				case EOpcode::ConstStr:
					{
						const auto& Constant = Module.Constants[Bytecode::ReadU16LE(Code.data() + Ip + 1u)];
						const auto& Value = Module.StringPool[Constant.StringIndex];
						Instruction.TextValueSlot = static_cast<std::uint16_t>(OutPlan.TextConstants.size());
						OutPlan.TextConstants.push_back({&Value, Value.data(), Value.size(), std::hash<std::string>{}(Value)});
						break;
					}
				case EOpcode::Call:
					{
						Instruction.TextFunctionIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						FPureTextProjection Projection;
						if (!DescribePureTextProjection(Module.Functions[Instruction.TextFunctionIndex], Projection)) return false;
						Instruction.Opcode = EOpcode::CallBuiltin;
						Instruction.ArgumentCount = Code[Ip + 3u];
						Instruction.bTextIdentity = Projection.Kind == EPureTextProjection::IdentityString;
						Instruction.Immediate = static_cast<std::int32_t>(EBuiltinId::CharCode);
						Instruction.TextCalleeOffset = Projection.InstructionOffset;
						break;
					}
				case EOpcode::ConstI32:
					{
						const auto ConstantIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						const auto& Constant = Module.Constants[ConstantIndex];
						if (Constant.Kind == EConstantKind::I64 || Constant.Kind == EConstantKind::U64)
						{
							Instruction.WideImmediateKind = Constant.Kind == EConstantKind::I64
							    ? ETypedLoopLocalKind::I64
							    : ETypedLoopLocalKind::U64;
							Instruction.Immediate64 = Constant.Kind == EConstantKind::I64
							    ? static_cast<std::uint64_t>(Constant.I64)
							    : Constant.U64;
						}
						else if (!TryReadTypedIntegerConstant(
						             Module, Code, Ip, Instruction.Immediate,
						             Instruction.bImmediateU32))
						{
							return false;
						}
						break;
					}
				case EOpcode::ConstF64:
					{
						if (!TryReadF64Constant(Module, Code, Ip, Instruction.ImmediateF64,
						        &Instruction.bImmediateF32))
						{
							return false;
						}
						break;
					}
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::IncField:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(
						    Code.data() + Ip + 1u);
						if (Index == 0u || OutPlan.Instructions.back().Opcode != EOpcode::LoadLocal)
						{
							// Direct local receivers give the plan a stable rooted object and
							// make it possible to resolve a field slot once before mutation.
							return false;
						}
						Instruction.Operand1 = OutPlan.Instructions.back().Operand0;
						OutPlan.bUsesDirectScalarFields = true;
						OutPlan.bMutatesDirectScalarFields =
						    OutPlan.bMutatesDirectScalarFields || Opcode == EOpcode::IncField;
						break;
					}
				case EOpcode::StoreField:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(
						    Code.data() + Ip + 1u);
						// Both compiler assignment forms are direct and stable here:
						// LoadFieldKeep retains the original receiver under the value, while a
						// normalized read/modify/write reloads `receiver, temporary` immediately
						// before StoreField. Prove the latter stack shape explicitly and require
						// every matching field read in the loop to use the same rooted local.
						std::optional<std::uint16_t> SourceSlot;
						if (Index >= 2u && OutPlan.Instructions[Index - 2u].Opcode == EOpcode::LoadLocal && OutPlan.Instructions[Index - 1u].Opcode == EOpcode::LoadLocal)
						{
							SourceSlot = OutPlan.Instructions[Index - 2u].Operand0;
						}
						else
						{
							for (std::size_t CandidateIndex = 0;
							    CandidateIndex < OutPlan.Instructions.size(); ++CandidateIndex)
							{
								const FTypedLoopInstruction& Candidate =
								    OutPlan.Instructions[CandidateIndex];
								if (Candidate.Opcode != EOpcode::LoadFieldKeep && Candidate.Opcode != EOpcode::LoadField)
								{
									continue;
								}
								if (Candidate.Operand0 != Instruction.Operand0)
								{
									continue;
								}
								if (Candidate.Opcode == EOpcode::LoadField && (CandidateIndex < 2u || OutPlan.Instructions[CandidateIndex - 2u].Opcode != EOpcode::LoadLocal || OutPlan.Instructions[CandidateIndex - 1u].Opcode != EOpcode::LoadLocal || OutPlan.Instructions[CandidateIndex - 2u].Operand0 != Candidate.Operand1 || OutPlan.Instructions[CandidateIndex - 1u].Operand0 != Candidate.Operand1))
								{
									continue;
								}
								if (SourceSlot && *SourceSlot != Candidate.Operand1)
								{
									return false;
								}
								SourceSlot = Candidate.Operand1;
							}
						}
						if (!SourceSlot)
						{
							return false;
						}
						bool bFoundMatchingRead = false;
						for (const FTypedLoopInstruction& Candidate : OutPlan.Instructions)
						{
							if ((Candidate.Opcode != EOpcode::LoadFieldKeep && Candidate.Opcode != EOpcode::LoadField) || Candidate.Operand0 != Instruction.Operand0)
							{
								continue;
							}
							if (*SourceSlot != Candidate.Operand1)
							{
								return false;
							}
							bFoundMatchingRead = true;
						}
						if (!bFoundMatchingRead)
						{
							return false;
						}
						Instruction.Operand1 = *SourceSlot;
						OutPlan.bUsesDirectScalarFields = true;
						OutPlan.bMutatesDirectScalarFields = true;
						break;
					}
				case EOpcode::ConstBool:
					{
						Instruction.Immediate = Code[Ip + 1u] != 0 ? 1 : 0;
						break;
					}
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::IncLocal:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						break;
					}
				case EOpcode::AddLocalImm:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						Instruction.Immediate = Bytecode::ReadI32LE(Code.data() + Ip + 3u);
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						Instruction.Operand1 = Bytecode::ReadU16LE(Code.data() + Ip + 3u);
						if (Instruction.bTextAdd)
						{
							// The shared text-arithmetic proof also identifies wstring.
							// This kernel owns UTF-8 strings only; retain VM selection for
							// known unsupported parameter/producer kinds before advertising
							// native coverage for the compact operation.
							if (Instruction.Operand0 >= UnsupportedWideLocals.size() || Instruction.Operand1 >= UnsupportedWideLocals.size() ||
								UnsupportedWideLocals[Instruction.Operand0] || UnsupportedWideLocals[Instruction.Operand1])
							{
								return false;
							}
							Instruction.bTextConcatStore = true;
							Instruction.Immediate = static_cast<std::int32_t>(Ip);
						}
						break;
					}
				case EOpcode::CallBuiltin:
					{
						Instruction.Immediate = Code[Ip + 1u];
						Instruction.ArgumentCount = Code[Ip + 2u];
						break;
					}
				case EOpcode::CallMethod:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						Instruction.Operand1 = 0xFFFFu;
						Instruction.ArgumentCount = Code[Ip + 3u];
						if (Module.StringPool[Instruction.Operand0] == "has")
						{
							Instruction.Immediate = 1;
						}
						else if (Module.StringPool[Instruction.Operand0] == "push")
						{
							Instruction.Immediate = 2;
						}
						else if (Module.StringPool[Instruction.Operand0] == "add")
						{
							Instruction.Immediate = 3;
						}
						else if (Index >= static_cast<std::size_t>(Instruction.ArgumentCount) + 1u && OutPlan.Instructions[Index - static_cast<std::size_t>(Instruction.ArgumentCount) - 1u].Opcode == EOpcode::LoadLocal)
						{
							// A direct rooted receiver can be generation/shape-guarded once at
							// loop entry. More complex receiver expressions retain normal PIC
							// dispatch until their value provenance is represented in typed IR.
							Instruction.Operand1 = OutPlan.Instructions[Index - static_cast<std::size_t>(Instruction.ArgumentCount) - 1u]
							                           .Operand0;
						}
						break;
					}
				case EOpcode::CallBoundMethod:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						Instruction.Operand1 = 0xFFFFu;
						Instruction.ArgumentCount = Code[Ip + 3u];
						if (Index >= static_cast<std::size_t>(Instruction.ArgumentCount) + 1u && OutPlan.Instructions[Index - static_cast<std::size_t>(Instruction.ArgumentCount) - 1u].Opcode == EOpcode::LoadLocal)
						{
							Instruction.Operand1 = OutPlan.Instructions[Index - static_cast<std::size_t>(Instruction.ArgumentCount) - 1u]
							                           .Operand0;
						}
						break;
					}
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
				case EOpcode::JmpIfLocalGeImm:
					{
						std::size_t Target = 0;
						if (!TryJumpTarget(Code, Ip, Opcode, Target))
						{
							return false;
						}
						const auto Found = OffsetToInstruction.find(Target);
						if (Found != OffsetToInstruction.end())
						{
							Instruction.BranchInstruction = Found->second;
							Instruction.ExternalBranchOffset = 0;
						}
						else
						{
							Instruction.BranchInstruction = InvalidInstruction;
							Instruction.ExternalBranchOffset = static_cast<std::uint32_t>(Target);
						}
						if (Opcode == EOpcode::JmpIfLocalGeImm)
						{
							Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
							Instruction.Immediate = Bytecode::ReadI32LE(Code.data() + Ip + 3u);
						}
						break;
					}
				default:
					{
						break;
					}
				}
				if (Instruction.Operand0 >= Function.LocalCount && (Opcode == EOpcode::LoadLocal || Opcode == EOpcode::StoreLocal || Opcode == EOpcode::IncLocal || Opcode == EOpcode::AddLocalImm || Opcode == EOpcode::JmpIfLocalGeImm))
				{
					return false;
				}
				if (Opcode == EOpcode::AddLocalLocal && (Instruction.Operand0 >= Function.LocalCount || Instruction.Operand1 >= Function.LocalCount))
				{
					return false;
				}
				OutPlan.Instructions.push_back(Instruction);
			}
			if (!TryComputeTypedLoopStackDepth(
			        OutPlan.Instructions, OutPlan.MaxStackDepth, OutPlan.bOwnsStringTemporaries))
			{
				return false;
			}
			// Defer only a complete adjacent Add/Store pair. There is no owning
			// unbounded backend string: the final result belongs to the core frame.
			const auto HasAlternateEntry = [&](const std::size_t Pc)
			{
				return std::any_of(OutPlan.Instructions.begin(), OutPlan.Instructions.end(),
					[&](const auto& I) {return I.BranchInstruction == Pc;});
			};
			for (std::size_t Pc = 0; Pc < OutPlan.Instructions.size(); ++Pc)
			{
				auto& I = OutPlan.Instructions[Pc];
				if (I.Opcode == EOpcode::Add && I.bTextAdd)
				{
					if (Pc + 1u >= OutPlan.Instructions.size() || I.NextInstruction != Pc + 1u ||
						OutPlan.Instructions[Pc + 1u].Opcode != EOpcode::StoreLocal || HasAlternateEntry(Pc + 1u))
					{
						return false;
					}
					auto& Store = OutPlan.Instructions[Pc + 1u];
					Store.bTextConcatStore = true;
					Store.Immediate = static_cast<std::int32_t>(I.BytecodeOffset);
				}
			}
			for (std::size_t Pc = 0; Pc < OutPlan.Instructions.size(); ++Pc)
			{
				auto& I = OutPlan.Instructions[Pc];
				if (I.Opcode != EOpcode::CallBuiltin || I.bTextIdentity ||
					static_cast<EBuiltinId>(I.Immediate) != EBuiltinId::ToString)
				{
					continue;
				}
				std::size_t StorePc = Pc + 1u;
				if (StorePc < OutPlan.Instructions.size() && OutPlan.Instructions[StorePc].bTextAdd && !HasAlternateEntry(StorePc))
				{
					++StorePc;
				}
				if (StorePc < OutPlan.Instructions.size() && OutPlan.Instructions[StorePc].Opcode == EOpcode::StoreLocal
					&& !HasAlternateEntry(Pc + 1u) && !HasAlternateEntry(StorePc))
				{
					I.TextFormatInstruction = static_cast<std::uint16_t>(Pc);
					OutPlan.Instructions[StorePc].TextFormatInstruction = static_cast<std::uint16_t>(Pc);
				}
				else
				{
					I.TextValueSlot = OutPlan.TextTemporaryCount++;
				}
			}
			if (OutPlan.TextTemporaryCount != 0u)
			{
				OutPlan.TextBuffers = std::make_shared<FTypedLoopTextBufferPool>(OutPlan.TextTemporaryCount);
			}
			OutPlan.bOwnsStringTemporaries = OutPlan.bOwnsStringTemporaries || OutPlan.bUsesTextOperations;
			for (std::size_t Index = 0; Index < OutPlan.Instructions.size(); ++Index)
			{
				auto& Instruction = OutPlan.Instructions[Index];
				if ((Instruction.Opcode == EOpcode::CallMethod || Instruction.Opcode == EOpcode::CallBoundMethod) && Instruction.Immediate == 0)
				{
					Instruction.Operand1 = FindRootedMethodReceiver(OutPlan.Instructions, Index, Instruction.ReceiverArraySlot);
				}
			}
			if (OutPlan.bUsesDirectScalarFields)
			{
				for (const FTypedLoopInstruction& Field : OutPlan.Instructions)
				{
					if (Field.Opcode != EOpcode::LoadField && Field.Opcode != EOpcode::StoreField && Field.Opcode != EOpcode::LoadFieldKeep && Field.Opcode != EOpcode::IncField)
					{
						continue;
					}
					if (std::any_of(
					        OutPlan.Instructions.begin(), OutPlan.Instructions.end(),
					        [&](const FTypedLoopInstruction& Candidate)
					{
						return Candidate.Opcode == EOpcode::StoreLocal && Candidate.Operand0 == Field.Operand1;
					}))
					{
						return false;
					}
				}
			}
			if (!OutPlan.bWideCandidate && !OutPlan.bUsesTextOperations)
			{
				DescribeFusedIndirectLoop(Module, OutPlan);
			}
			for (FTypedLoopInstruction& Method : OutPlan.Instructions)
			{
				if ((Method.Opcode != EOpcode::CallMethod && Method.Opcode != EOpcode::CallBoundMethod) || Method.Immediate != 0 || OutPlan.FusedKind != FTypedLoopPlan::EFusedKind::None)
				{
					continue;
				}
				if (Method.Opcode == EOpcode::CallBoundMethod && Method.Operand1 != 0xFFFFu)
				{
					FTypedLoopPlan::FFieldI32MethodExpression Expression;
					if (TryBuildFieldI32MethodExpression(Module, Method.Operand0, Expression) &&
						Expression.ArgumentCount == Method.ArgumentCount &&
						!HasDirectI32FieldSummary(Module, Module.Functions[Method.Operand0]))
					{
						const auto Found = std::find_if(OutPlan.FieldMethods.begin(), OutPlan.FieldMethods.end(),
							[&](const auto& Existing) { return Existing.FunctionIndex == Method.Operand0; });
						Method.FieldMethodIndex = static_cast<std::uint16_t>(Found - OutPlan.FieldMethods.begin());
						if (Found == OutPlan.FieldMethods.end())
						{
							OutPlan.FieldMethods.push_back(std::move(Expression));
						}
					}
				}
				// Multi-argument calls require a complete native field kernel before any
				// execution; every other method retains its authoritative call path.
				if (Method.ArgumentCount != 1u && Method.FieldMethodIndex == 0xFFFFu) return false;
				if (Method.Operand1 == 0xFFFFu)
				{
					if (Method.Opcode != EOpcode::CallMethod || Method.Operand0 >= Module.StringPool.size())
					{
						return false;
					}
					const std::string& MethodName = Module.StringPool[Method.Operand0];
					bool bFoundPureTarget = false;
					for (std::size_t FunctionIndex = 0;
					    FunctionIndex < Module.Functions.size() && FunctionIndex < 0xFFFFu;
					    ++FunctionIndex)
					{
						const FBytecodeFunction& CandidateFunction =
						    Module.Functions[FunctionIndex];
						if (!CandidateFunction.bIsMethod || !MethodLinkageLeafMatches(CandidateFunction.Name, MethodName) || CandidateFunction.ParamCount != 1u || CandidateFunction.bIsAbstract || CandidateFunction.bDeleted)
						{
							continue;
						}
						FTypedLoopPlan::FPureI32MethodExpression Expression;
						if (!TryBuildPureI32MethodExpression(Module,
						        static_cast<std::uint16_t>(FunctionIndex), Expression))
						{
							FTypedLoopPlan::FFieldI32MethodExpression FieldExpression;
							if (Method.ReceiverArraySlot != 0xFFFFu && TryBuildFieldI32MethodExpression(
								Module, static_cast<std::uint16_t>(FunctionIndex), FieldExpression) &&
								std::none_of(FieldExpression.Instructions.begin(), FieldExpression.Instructions.end(),
									[](const auto& I) {return I.Opcode == EOpcode::StoreThisField;}) &&
								std::none_of(OutPlan.Instructions.begin(), OutPlan.Instructions.end(),
									[&](const auto& I) {return I.Opcode == EOpcode::StoreLocal && I.Operand0 == Method.ReceiverArraySlot;}))
							{
								Expression.FunctionIndex = FieldExpression.FunctionIndex;
								Expression.Instructions = std::move(FieldExpression.Instructions);
								Expression.FieldNameIndices = std::move(FieldExpression.FieldNameIndices);
							}
							else
							{
							// Bytecode dispatch is name/arity based. If any concrete target
							// with that dispatch identity cannot be proven side-effect-free,
							// the receiver chain must stay on the authoritative VM path; a
							// late miss could otherwise happen after loop-local updates.
							return false;
							}
						}
						bFoundPureTarget = true;
						if (std::none_of(
						        OutPlan.GenericPureMethods.begin(),
						        OutPlan.GenericPureMethods.end(),
						        [FunctionIndex](const auto& Existing)
						{
							return Existing.FunctionIndex == FunctionIndex;
						}))
						{
							OutPlan.GenericPureMethods.push_back(std::move(Expression));
						}
					}
					if (!bFoundPureTarget)
					{
						return false;
					}
					continue;
				}
				if (std::any_of(
				        OutPlan.Instructions.begin(), OutPlan.Instructions.end(),
				        [&](const FTypedLoopInstruction& Candidate)
				{
					return Candidate.Opcode == EOpcode::StoreLocal && Candidate.Operand0 == Method.Operand1;
				}))
				{
					return false;
				}
			}
			if (OutPlan.bWideCandidate)
			{
				OutPlan.FusedKind = FTypedLoopPlan::EFusedKind::None;
				return true;
			}
			// Fused executors keep the loop limit in a native scalar for the complete
			// batch.  That is valid only when the body does not write the same local.
			// Preserve legal self-shrinking/self-growing loops by retaining the general
			// typed opcode plan whenever alias analysis finds such a write.
			if (FusedPlanSnapshotsMutatedLimit(OutPlan) || FusedPlanHasUnsafeLocalAliases(OutPlan))
			{
				OutPlan.FusedKind = FTypedLoopPlan::EFusedKind::None;
			}
			DescribeObjectArrayPrefix(OutPlan);
			return true;
		}

		bool TryBuildHoistedNonEscapingObjectPlan(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::size_t Header,
		    const std::size_t BackEdgeIp,
		    FTypedLoopPlan& OutPlan)
		{
			// This is a conservative loop-level escape/scalarization proof.  It is
			// intentionally independent of a particular state recurrence: the scalar
			// prefix remains ordinary typed CFG (including uint shifts/conversions),
			// while only an unobservable allocation and a pure zero-argument projection
			// are rewritten.  Unsupported identity, lifetime or call semantics fail the
			// proof before any source-visible mutation.
			const auto& Code = Function.Code;
			if (Header >= BackEdgeIp || BackEdgeIp >= Code.size() || static_cast<EOpcode>(Code[BackEdgeIp]) != EOpcode::Jmp)
			{
				return false;
			}
			std::size_t BackTarget = 0;
			if (!TryJumpTarget(Code, BackEdgeIp, EOpcode::Jmp, BackTarget) || BackTarget != Header)
			{
				return false;
			}

			std::size_t ObjectIp = Code.size();
			for (std::size_t Ip = Header; Ip <= BackEdgeIp;)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if (Size == 0u || Size > Code.size() - Ip)
				{
					return false;
				}
				if (Opcode == EOpcode::NewObject)
				{
					if (ObjectIp != Code.size())
					{
						return false;
					}
					ObjectIp = Ip;
				}
				Ip += Size;
			}
			if (ObjectIp == Code.size())
			{
				return false;
			}
			const std::size_t ObjectStoreIp = ObjectIp + Bytecode::InstructionSize(EOpcode::NewObject);
			if (ObjectStoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal) > Code.size() || static_cast<EOpcode>(Code[ObjectStoreIp]) != EOpcode::StoreLocal)
			{
				return false;
			}
			const std::uint16_t TypeIndex = Bytecode::ReadU16LE(
			    Code.data() + ObjectIp + 1u);
			const std::uint16_t ObjectSlot = Bytecode::ReadU16LE(
			    Code.data() + ObjectStoreIp + 1u);
			if (TypeIndex >= Module.Types.size() || ObjectSlot >= Function.LocalCount)
			{
				return false;
			}
			const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
			const bool bHasClassDefaults = std::any_of(
			    Type.DeclMeta.begin(), Type.DeclMeta.end(),
			    [](const auto& Entry)
			{
				return Entry.first == ClassDefaultsLinkageMetaKey;
			});
			if (Type.Kind != EScriptObjectKind::Class || Type.bIsAbstract || Type.bIsStatic || !Type.BaseTypeIndices.empty() || !Type.HostBaseTypeName.empty() || Type.DestructorFunctionIndex != 0xFFFFu || bHasClassDefaults || std::any_of(Type.Fields.begin(), Type.Fields.end(), [](const FBytecodeFieldDesc& Field)
			{
				return Field.bHasInitCode;
			}))
			{
				return false;
			}

			std::vector<std::uint16_t> AssignedFieldNames;
			std::vector<std::size_t> AllowedObjectAccesses{ObjectStoreIp};
			std::size_t Cursor = ObjectStoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
			const auto IsScalarExpressionOpcode = [](const EOpcode Opcode)
			{
				switch (Opcode)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstBool:
				case EOpcode::LoadLocal:
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
					return true;
				default:
					return false;
				}
			};
			while (Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal) <= BackEdgeIp && static_cast<EOpcode>(Code[Cursor]) == EOpcode::LoadLocal && Bytecode::ReadU16LE(Code.data() + Cursor + 1u) == ObjectSlot)
			{
				AllowedObjectAccesses.push_back(Cursor);
				std::size_t Scan = Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal);
				std::int32_t Depth = 0;
				std::size_t Operations = 0;
				for (; Scan < BackEdgeIp && Operations++ < 64u;)
				{
					const EOpcode Opcode = static_cast<EOpcode>(Code[Scan]);
					if (Opcode == EOpcode::StoreField)
					{
						break;
					}
					const std::size_t Size = Bytecode::InstructionSize(Opcode);
					if (Size == 0u || Size > Code.size() - Scan)
					{
						return false;
					}
					if (Opcode == EOpcode::CallBuiltin)
					{
						if (Code[Scan + 2u] != 1u)
						{
							return false;
						}
						const EBuiltinId Builtin = static_cast<EBuiltinId>(Code[Scan + 1u]);
						if (Builtin != EBuiltinId::ToInt && Builtin != EBuiltinId::ToUnsigned && Builtin != EBuiltinId::ToFloat && Builtin != EBuiltinId::ToDouble)
						{
							return false;
						}
						if (Depth < 1)
						{
							return false;
						}
					}
					else if (!IsScalarExpressionOpcode(Opcode))
					{
						return false;
					}
					else if (Opcode == EOpcode::ConstI32 || Opcode == EOpcode::ConstBool || Opcode == EOpcode::LoadLocal)
					{
						++Depth;
					}
					else if (Opcode == EOpcode::Add || Opcode == EOpcode::Sub || Opcode == EOpcode::Mul || Opcode == EOpcode::Div || Opcode == EOpcode::Mod || Opcode == EOpcode::BitAnd || Opcode == EOpcode::BitOr || Opcode == EOpcode::BitXor || Opcode == EOpcode::Shl || Opcode == EOpcode::Shr)
					{
						if (Depth < 2)
						{
							return false;
						}
						--Depth;
					}
					else if ((Opcode == EOpcode::Neg || Opcode == EOpcode::BitNot || Opcode == EOpcode::CheckedCast) && Depth < 1)
					{
						return false;
					}
					Scan += Size;
				}
				if (Scan + Bytecode::InstructionSize(EOpcode::StoreField) + Bytecode::InstructionSize(EOpcode::Pop) > BackEdgeIp || static_cast<EOpcode>(Code[Scan]) != EOpcode::StoreField || Depth != 1 || static_cast<EOpcode>(Code[Scan + Bytecode::InstructionSize(EOpcode::StoreField)]) != EOpcode::Pop)
				{
					return false;
				}
				const std::uint16_t FieldName = Bytecode::ReadU16LE(Code.data() + Scan + 1u);
				if (FieldName >= Module.StringPool.size())
				{
					return false;
				}
				const std::string& Name = Module.StringPool[FieldName];
				const auto Field = std::find_if(Type.Fields.begin(), Type.Fields.end(),
				    [&](const FBytecodeFieldDesc& Candidate)
				{
					return Candidate.Name == Name;
				});
				if (Field == Type.Fields.end() || Field->bHasInitCode || (Field->TypeName != "int" && Field->TypeName != "int32"))
				{
					return false;
				}
				if (std::find(AssignedFieldNames.begin(), AssignedFieldNames.end(), FieldName) == AssignedFieldNames.end())
				{
					AssignedFieldNames.push_back(FieldName);
				}
				Cursor = Scan + Bytecode::InstructionSize(EOpcode::StoreField) + Bytecode::InstructionSize(EOpcode::Pop);
			}
			if (AssignedFieldNames.empty())
			{
				return false;
			}

			// The projection must be the next object use.  Zero-argument methods cover
			// the common value-projection case while avoiding argument/alias materialization.
			const std::size_t ReceiverIp = Cursor + Bytecode::InstructionSize(EOpcode::LoadLocal);
			const std::size_t CallIp = ReceiverIp + Bytecode::InstructionSize(EOpcode::LoadLocal);
			if (CallIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod) > BackEdgeIp || static_cast<EOpcode>(Code[Cursor]) != EOpcode::LoadLocal || static_cast<EOpcode>(Code[ReceiverIp]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + ReceiverIp + 1u) != ObjectSlot || static_cast<EOpcode>(Code[CallIp]) != EOpcode::CallBoundMethod || Code[CallIp + 3u] != 0u)
			{
				return false;
			}
			AllowedObjectAccesses.push_back(ReceiverIp);
			const std::uint16_t MethodIndex = Bytecode::ReadU16LE(Code.data() + CallIp + 1u);
			if (MethodIndex >= Module.Functions.size())
			{
				return false;
			}
			const FBytecodeFunction& Method = Module.Functions[MethodIndex];
			if (!Method.bIsMethod || Method.OwnerTypeIndex != TypeIndex || Method.ParamCount != 0u || Method.LocalCount != 0u || Method.bReturnsVoid || Method.bReturnsRef || Method.bIsVirtual || Method.bIsAbstract || Method.bDeleted || BytecodeFunctionHasRefParams(Method) || !Method.ExceptionRegions.empty() || Method.Code.empty())
			{
				return false;
			}

			std::int32_t MethodDepth = 0;
			bool bMethodReturned = false;
			for (std::size_t MethodIp = 0; MethodIp < Method.Code.size();)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Method.Code[MethodIp]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if (Size == 0u || Size > Method.Code.size() - MethodIp || bMethodReturned)
				{
					return false;
				}
				if (Opcode == EOpcode::LoadThisField)
				{
					const std::uint16_t Name = Bytecode::ReadU16LE(
					    Method.Code.data() + MethodIp + 1u);
					if (std::find(AssignedFieldNames.begin(), AssignedFieldNames.end(), Name) == AssignedFieldNames.end())
					{
						return false;
					}
					++MethodDepth;
				}
				else if (Opcode == EOpcode::ConstI32)
				{
					std::int32_t Ignored = 0;
					bool bIgnoredU32 = false;
					if (!TryReadTypedIntegerConstant(
					        Module, Method.Code, MethodIp, Ignored, bIgnoredU32))
					{
						return false;
					}
					++MethodDepth;
				}
				else if (Opcode == EOpcode::Add || Opcode == EOpcode::Sub || Opcode == EOpcode::Mul || Opcode == EOpcode::Div || Opcode == EOpcode::Mod || Opcode == EOpcode::BitAnd || Opcode == EOpcode::BitOr || Opcode == EOpcode::BitXor || Opcode == EOpcode::Shl || Opcode == EOpcode::Shr)
				{
					if (MethodDepth < 2)
					{
						return false;
					}
					--MethodDepth;
				}
				else if (Opcode == EOpcode::Neg || Opcode == EOpcode::BitNot)
				{
					if (MethodDepth < 1)
					{
						return false;
					}
				}
				else if (Opcode == EOpcode::Return)
				{
					bMethodReturned = MethodDepth == 1 && MethodIp + Size == Method.Code.size();
					if (!bMethodReturned)
					{
						return false;
					}
				}
				else if (Opcode != EOpcode::Nop)
				{
					return false;
				}
				MethodIp += Size;
			}
			if (!bMethodReturned)
			{
				return false;
			}

			std::sort(AllowedObjectAccesses.begin(), AllowedObjectAccesses.end());
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if (Size == 0u || Size > Code.size() - Ip)
				{
					return false;
				}
				if ((Opcode == EOpcode::LoadLocal || Opcode == EOpcode::StoreLocal || Opcode == EOpcode::MakeLocalRef || Opcode == EOpcode::BindLocal) && Size >= 3u && Bytecode::ReadU16LE(Code.data() + Ip + 1u) == ObjectSlot && !std::binary_search(AllowedObjectAccesses.begin(), AllowedObjectAccesses.end(), Ip))
				{
					return false;
				}
				Ip += Size;
			}

			std::uint16_t CounterSlot = 0xFFFFu;
			const EOpcode HeaderOpcode = static_cast<EOpcode>(Code[Header]);
			if (HeaderOpcode == EOpcode::JmpIfLocalGeImm)
			{
				CounterSlot = Bytecode::ReadU16LE(Code.data() + Header + 1u);
			}
			else if (HeaderOpcode == EOpcode::LoadLocal)
			{
				CounterSlot = Bytecode::ReadU16LE(Code.data() + Header + 1u);
			}
			if (CounterSlot >= Function.LocalCount || CounterSlot == ObjectSlot)
			{
				return false;
			}
			bool bCounterAdvanced = false;
			for (std::size_t Ip = Header; Ip < BackEdgeIp;)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if ((Opcode == EOpcode::IncLocal || Opcode == EOpcode::AddLocalImm) && Bytecode::ReadU16LE(Code.data() + Ip + 1u) == CounterSlot)
				{
					bCounterAdvanced = true;
				}
				Ip += Size;
			}
			if (!bCounterAdvanced)
			{
				return false;
			}

			FTypedLoopPlan Plan;
			Plan.HeaderOffset = static_cast<std::uint32_t>(Header);
			Plan.LocalCount = Function.LocalCount;
			Plan.bUsesDirectScalarFields = true;
			Plan.bMutatesDirectScalarFields = true;
			Plan.bHoistsNonEscapingObject = true;
			Plan.HoistedObjectTypeIndex = TypeIndex;
			Plan.HoistedObjectSlot = ObjectSlot;
			Plan.HoistedObjectCounterSlot = CounterSlot;
			Plan.HoistedObjectInstructionOffset = static_cast<std::uint32_t>(ObjectIp);
			std::vector<std::size_t> BranchTargets;
			std::vector<std::uint32_t> RawToInstruction(Code.size(), InvalidInstruction);
			std::size_t ExitOffset = 0u;

			auto AppendInstruction = [&](FTypedLoopInstruction Instruction,
			                             const std::size_t RawIp, const std::size_t BranchTarget)
			{
				if (Plan.Instructions.size() >= MaxTypedLoopInstructions)
				{
					return false;
				}
				if (RawIp < RawToInstruction.size() && RawToInstruction[RawIp] == InvalidInstruction)
				{
					RawToInstruction[RawIp] = static_cast<std::uint32_t>(
					    Plan.Instructions.size());
				}
				Plan.Instructions.push_back(Instruction);
				BranchTargets.push_back(BranchTarget);
				return true;
			};
			auto AppendRaw = [&](const std::size_t Ip)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
				FTypedLoopInstruction Instruction;
				Instruction.Opcode = Opcode;
				Instruction.BytecodeOffset = static_cast<std::uint32_t>(Ip);
				std::size_t BranchTarget = Code.size() + 1u;
				switch (Opcode)
				{
				case EOpcode::Nop:
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
				case EOpcode::Pop:
					{
						break;
					}
				case EOpcode::ConstI32:
					{
						if (!TryReadTypedIntegerConstant(Module, Code, Ip,
						        Instruction.Immediate, Instruction.bImmediateU32))
						{
							return false;
						}
						break;
					}
				case EOpcode::ConstBool:
					{
						Instruction.Immediate = Code[Ip + 1u] != 0u ? 1 : 0;
						break;
					}
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::IncLocal:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						break;
					}
				case EOpcode::AddLocalImm:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						Instruction.Immediate = Bytecode::ReadI32LE(Code.data() + Ip + 3u);
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						Instruction.Operand1 = Bytecode::ReadU16LE(Code.data() + Ip + 3u);
						break;
					}
				case EOpcode::CallBuiltin:
					{
						Instruction.Immediate = Code[Ip + 1u];
						Instruction.ArgumentCount = Code[Ip + 2u];
						if (Instruction.ArgumentCount != 1u)
						{
							return false;
						}
						break;
					}
				case EOpcode::StoreField:
					{
						Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
						Instruction.Operand1 = ObjectSlot;
						break;
					}
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
				case EOpcode::JmpIfLocalGeImm:
					{
						if (!TryJumpTarget(Code, Ip, Opcode, BranchTarget))
						{
							return false;
						}
						if (Opcode == EOpcode::JmpIfLocalGeImm)
						{
							Instruction.Operand0 = Bytecode::ReadU16LE(Code.data() + Ip + 1u);
							Instruction.Immediate = Bytecode::ReadI32LE(Code.data() + Ip + 3u);
						}
						break;
					}
				default:
					return false;
				}
				return AppendInstruction(Instruction, Ip, BranchTarget);
			};

			for (std::size_t Ip = Header; Ip <= BackEdgeIp;)
			{
				if (Ip == ObjectIp)
				{
					Ip = ObjectStoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
					continue;
				}
				if (Ip == ReceiverIp)
				{
					const std::size_t FirstSynthetic = Plan.Instructions.size();
					RawToInstruction[ReceiverIp] = static_cast<std::uint32_t>(FirstSynthetic);
					RawToInstruction[CallIp] = static_cast<std::uint32_t>(FirstSynthetic);
					for (std::size_t MethodIp = 0; MethodIp < Method.Code.size();)
					{
						const EOpcode Opcode = static_cast<EOpcode>(Method.Code[MethodIp]);
						const std::size_t Size = Bytecode::InstructionSize(Opcode);
						if (Opcode == EOpcode::Return || Opcode == EOpcode::Nop)
						{
							MethodIp += Size;
							continue;
						}
						if (Opcode == EOpcode::LoadThisField)
						{
							FTypedLoopInstruction LoadObject;
							LoadObject.Opcode = EOpcode::LoadLocal;
							LoadObject.BytecodeOffset = static_cast<std::uint32_t>(CallIp);
							LoadObject.Operand0 = ObjectSlot;
							if (!AppendInstruction(LoadObject, ReceiverIp, Code.size() + 1u))
							{
								return false;
							}
							FTypedLoopInstruction LoadField;
							LoadField.Opcode = EOpcode::LoadField;
							LoadField.BytecodeOffset = static_cast<std::uint32_t>(CallIp);
							LoadField.Operand0 = Bytecode::ReadU16LE(
							    Method.Code.data() + MethodIp + 1u);
							LoadField.Operand1 = ObjectSlot;
							if (!AppendInstruction(LoadField, ReceiverIp, Code.size() + 1u))
							{
								return false;
							}
						}
						else
						{
							FTypedLoopInstruction Synthetic;
							Synthetic.Opcode = Opcode;
							Synthetic.BytecodeOffset = static_cast<std::uint32_t>(CallIp);
							if (Opcode == EOpcode::ConstI32 && !TryReadTypedIntegerConstant(Module, Method.Code, MethodIp, Synthetic.Immediate, Synthetic.bImmediateU32))
							{
								return false;
							}
							if (!AppendInstruction(Synthetic, CallIp, Code.size() + 1u))
							{
								return false;
							}
						}
						MethodIp += Size;
					}
					Ip = CallIp + Bytecode::InstructionSize(EOpcode::CallBoundMethod);
					continue;
				}
				if (!AppendRaw(Ip))
				{
					return false;
				}
				Ip += Bytecode::InstructionSize(static_cast<EOpcode>(Code[Ip]));
			}

			if (Plan.Instructions.empty())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Plan.Instructions.size(); ++Index)
			{
				FTypedLoopInstruction& Instruction = Plan.Instructions[Index];
				Instruction.NextInstruction = Index + 1u < Plan.Instructions.size()
				    ? static_cast<std::uint32_t>(Index + 1u)
				    : InvalidInstruction;
				const std::size_t Target = BranchTargets[Index];
				if (Target == Code.size() + 1u)
				{
					continue;
				}
				if (Target >= Header && Target <= BackEdgeIp)
				{
					if (Target >= RawToInstruction.size() || RawToInstruction[Target] == InvalidInstruction)
					{
						return false;
					}
					Instruction.BranchInstruction = RawToInstruction[Target];
				}
				else
				{
					if (Instruction.Opcode == EOpcode::Jmp)
					{
						return false;
					}
					if (ExitOffset != 0u && ExitOffset != Target)
					{
						return false;
					}
					ExitOffset = Target;
					Instruction.BranchInstruction = InvalidInstruction;
					Instruction.ExternalBranchOffset = static_cast<std::uint32_t>(Target);
				}
			}
			if (ExitOffset == 0u || !TryComputeTypedLoopStackDepth(Plan.Instructions, Plan.MaxStackDepth, Plan.bOwnsStringTemporaries))
			{
				return false;
			}
			const bool bHasSupportedEntryGuard =
			    (!Plan.Instructions.empty() && Plan.Instructions[0].Opcode == EOpcode::JmpIfLocalGeImm && Plan.Instructions[0].Operand0 == CounterSlot) || (Plan.Instructions.size() >= 4u && Plan.Instructions[0].Opcode == EOpcode::LoadLocal && Plan.Instructions[0].Operand0 == CounterSlot && (Plan.Instructions[1].Opcode == EOpcode::LoadLocal || (Plan.Instructions[1].Opcode == EOpcode::ConstI32 && !Plan.Instructions[1].bImmediateU32)) && Plan.Instructions[2].Opcode == EOpcode::Lt && Plan.Instructions[3].Opcode == EOpcode::JmpIfFalse);
			if (!bHasSupportedEntryGuard)
			{
				return false;
			}
			Plan.ExitOffset = static_cast<std::uint32_t>(ExitOffset);
			OutPlan = std::move(Plan);
			return true;
		}

		using ELocalKind = ETypedLoopLocalKind;
		using FLocalBinding = FTypedLoopNativeBinding;

	}

	std::shared_ptr<FTypedLoopPlanSet> BuildTypedLoopPlans(
	    const FBytecodeModule& Module,
	    const FBytecodeFunction& Function,
	    JitNative::FArena& Arena,
	    const bool bAllowScalarArrayStorage,
	    const bool bAllowScalarMapStorage,
	    const bool bAllowScalarSetStorage,
	    const std::vector<std::size_t>* const TextAddOffsets,
	    const std::vector<std::size_t>* const UnsupportedTextFormatOffsets)
	{
		if (!Function.ExceptionRegions.empty() || Function.Code.empty())
		{
			return {};
		}
		auto Plans = std::make_shared<FTypedLoopPlanSet>();
		Plans->PlanAtOffset.assign(Function.Code.size(), 0u);
		std::vector<bool> SeenBackwardLoopHeader(Function.Code.size(), false);
		const bool bFusedScalarStorageSafe =
		    CanUseFusedScalarContainerStorage(Module, Function);
		const bool bFunctionMayUseWideIntegers = MayUseWideIntegers(Module, Function);
		// Width admission is a cost decision, not a replacement for runtime type
		// validation. Retain scalar/array producers when the scalar IR cannot analyze
		// an owning prefix. Unsupported values affect only loops that reference them;
		// unrelated floating locals must not disable an integer plan.
		std::vector<bool> UnsupportedWideLocals(Function.LocalCount, false);
		const auto IsUnsupportedArray = [](const std::string_view Type)
		{
			std::string Element;
			return !JitHost::Services().ParseDynamicArrayElementType(Type, Element) || (Element != "int" && Element != "uint32" && Element != "uint" && Element != "unsigned" && Element != "unsigned int");
		};
		JitIr::FSignature Signature;
		std::vector<std::string_view> ParameterNames;
		if (JitIr::ParseSignature(Function.ClosureTypeName, Signature, &ParameterNames))
		{
			for (std::size_t Slot = 0; Slot < Signature.Parameters.size() && Slot < UnsupportedWideLocals.size(); ++Slot)
			{
				UnsupportedWideLocals[Slot] = Signature.Parameters[Slot] == JitIr::EValueType::F32 || Signature.Parameters[Slot] == JitIr::EValueType::F64 || Signature.Parameters[Slot] == JitIr::EValueType::WString || (Signature.Parameters[Slot] == JitIr::EValueType::Any && IsUnsupportedArray(ParameterNames[Slot]));
			}
		}
		std::size_t Previous = Function.Code.size();
		for (std::size_t Ip = 0; Ip < Function.Code.size();)
		{
			const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
			const auto Size = Bytecode::InstructionSize(Op);
			if (Size == 0 || Size > Function.Code.size() - Ip) return {};
			if (Op == EOpcode::StoreLocal && Previous < Function.Code.size())
			{
				const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
				const auto Producer = static_cast<EOpcode>(Function.Code[Previous]);
				bool bUnsupportedWide = Producer == EOpcode::ConstF64;
				if (Producer == EOpcode::ConstStr)
				{
					const auto Constant = Bytecode::ReadU16LE(Function.Code.data() + Previous + 1u);
					bUnsupportedWide = Constant >= Module.Constants.size() || Module.Constants[Constant].Kind != EConstantKind::Str;
				}
				if (Producer == EOpcode::NewDynamicArray)
				{
					const auto Index = Bytecode::ReadU16LE(Function.Code.data() + Previous + 1u);
					bUnsupportedWide = Index < Module.StringPool.size() && IsUnsupportedArray(Module.StringPool[Index]);
				}
				if (Producer == EOpcode::CallBuiltin)
				{
					const auto Id = static_cast<EBuiltinId>(Function.Code[Previous + 1u]);
					bUnsupportedWide = Id == EBuiltinId::ToFloat || Id == EBuiltinId::ToDouble;
				}
				else if (Producer == EOpcode::LoadLocal)
				{
					const auto Source = Bytecode::ReadU16LE(Function.Code.data() + Previous + 1u);
					bUnsupportedWide = Source < UnsupportedWideLocals.size() && UnsupportedWideLocals[Source];
				}
				else if (Producer == EOpcode::Call || Producer == EOpcode::CallBoundMethod)
				{
					const auto Callee = Bytecode::ReadU16LE(Function.Code.data() + Previous + 1u);
					JitIr::FSignature Result;
					if (Callee < Module.Functions.size() && JitIr::ParseSignature(Module.Functions[Callee].ClosureTypeName, Result))
					{
						bUnsupportedWide = Result.ReturnType == JitIr::EValueType::F32 || Result.ReturnType == JitIr::EValueType::F64;
					}
				}
				if (Slot < UnsupportedWideLocals.size())
				{
					UnsupportedWideLocals[Slot] = UnsupportedWideLocals[Slot] || bUnsupportedWide;
				}
			}
			Previous = Ip;
			Ip += Size;
		}
		JitIr::FFunction TypeInfo;
		const std::vector<JitIr::EValueType>* LocalTypes = nullptr;
		if (bFunctionMayUseWideIntegers && JitIr::BuildFunction(Function, TypeInfo) && JitIr::AnalyzeTypes(Module, Function, TypeInfo) && TypeInfo.bTypeAnalysisPrecise)
		{
			LocalTypes = &TypeInfo.LocalTypes;
		}
		const auto LoopContainsComplexOperation = [&Function](
		                                              const std::size_t Header,
		                                              const std::size_t BackEdge) -> bool
		{
			for (std::size_t LoopIp = Header; LoopIp <= BackEdge;)
			{
				const EOpcode LoopOpcode = static_cast<EOpcode>(Function.Code[LoopIp]);
				const std::size_t LoopSize = Bytecode::InstructionSize(LoopOpcode);
				if (LoopSize == 0 || LoopSize > Function.Code.size() - LoopIp)
				{
					return true;
				}
				switch (LoopOpcode)
				{
				case EOpcode::GetIndexCopy:
				case EOpcode::SetIndex:
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::StoreField:
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
				case EOpcode::IncField:
				case EOpcode::CallMethod:
				case EOpcode::CallBoundMethod:
				case EOpcode::NewObject:
					return true;
				default:
					{
						break;
					}
				}
				LoopIp += LoopSize;
			}
			return false;
		};
		for (std::size_t ScanIp = 0; ScanIp < Function.Code.size();)
		{
			const EOpcode ScanOpcode = static_cast<EOpcode>(Function.Code[ScanIp]);
			const std::size_t ScanSize = Bytecode::InstructionSize(ScanOpcode);
			if (ScanSize == 0 || ScanSize > Function.Code.size() - ScanIp)
			{
				return {};
			}
			ScanIp += ScanSize;
		}
		const auto& Code = Function.Code;
		for (std::size_t Ip = 0; Ip < Code.size();)
		{
			const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
			const std::size_t Size = Bytecode::InstructionSize(Opcode);
			if (Size == 0 || Size > Code.size() - Ip)
			{
				return {};
			}
			if (Opcode == EOpcode::Jmp)
			{
				std::size_t Target = 0;
				if (TryJumpTarget(Code, Ip, Opcode, Target) && Target < Ip && Target < Plans->PlanAtOffset.size() && !SeenBackwardLoopHeader[Target])
				{
					SeenBackwardLoopHeader[Target] = true;
					++Plans->BackwardLoopCount;
					FTypedLoopPlan Plan;
					const bool bBuiltTypedPlan =
					    TryBuildPlan(Module, Function, Target, Ip, bFunctionMayUseWideIntegers, LocalTypes, UnsupportedWideLocals, TextAddOffsets, UnsupportedTextFormatOffsets, Plan) || TryBuildHoistedNonEscapingObjectPlan(Module, Function, Target, Ip, Plan);
					const bool bHasStorageProof = bBuiltTypedPlan && TypedContainerMethodsHaveProvenStorage(
					    Plan, bFusedScalarStorageSafe, bAllowScalarArrayStorage, bAllowScalarMapStorage, bAllowScalarSetStorage);
					// A call outside the loop can defeat whole-function escape analysis
					// even though this construction loop has no user calls. Admit only a
					// native kernel whose complete operand kinds are checked before entry.
					Plan.bRuntimeBoundArrayPush = bBuiltTypedPlan && !bHasStorageProof && CanBindArrayPushAtEntry(Plan);
					if ((bHasStorageProof || Plan.bRuntimeBoundArrayPush) && Plans->Plans.size() < 0xFFFEu)
					{
						// Accumulate requirements and profitability per natural loop. Using the
						// PlanSet booleans directly while describing a plan lets an earlier hot
						// loop make every later plan look profitable, and can also overwrite a
						// storage requirement established by an earlier plan.
						const bool bPreviousProfitable = Plans->bHasProfitablePlan;
						const bool bPreviousArray = Plans->bRequiresScalarArrayStorage;
						const bool bPreviousMap = Plans->bRequiresScalarMapStorage;
						const bool bPreviousSet = Plans->bRequiresScalarSetStorage;
						Plans->bHasProfitablePlan = false;
						Plans->bRequiresScalarArrayStorage = false;
						Plans->bRequiresScalarMapStorage = false;
						Plans->bRequiresScalarSetStorage = false;
						if (Plan.bWideCandidate || Plan.bUsesTextOperations)
						{
							Plans->bHasProfitablePlan = true;
						}
						if (Plan.bUsesDirectScalarFields)
						{
							Plans->bHasProfitablePlan = true;
						}
						if (!Plan.GenericPureMethods.empty())
						{
							Plans->bHasProfitablePlan = true;
						}
						if (std::any_of(
						        Plan.Instructions.begin(), Plan.Instructions.end(),
						        [](const FTypedLoopInstruction& Instruction)
						{
							return (Instruction.Opcode == EOpcode::CallMethod || Instruction.Opcode == EOpcode::CallBoundMethod) && Instruction.Immediate == 0 && Instruction.Operand1 != 0xFFFFu;
						}))
						{
							Plans->bHasProfitablePlan = true;
						}
						// The general typed-loop executor is not limited to the fused
						// canonical shapes below.  Once whole-function escape analysis has
						// proved that primitive container storage cannot be published, any
						// supported index/method instruction can operate directly on raw
						// storage.  Mark those plans profitable as well; otherwise harmless
						// changes such as introducing an index temporary disable the typed
						// loop and send every operation back through FValue/Linear.
						if (bFusedScalarStorageSafe || bAllowScalarArrayStorage || bAllowScalarMapStorage || bAllowScalarSetStorage)
						{
							for (const FTypedLoopInstruction& Instruction : Plan.Instructions)
							{
								if (Instruction.Opcode == EOpcode::GetIndexCopy || Instruction.Opcode == EOpcode::SetIndex)
								{
									// Indexing is shared by Array and Map bytecode. Candidate
									// discovery still limits storage conversion to actual local
									// declarations, so enabling both requirements is conservative.
									Plans->bRequiresScalarArrayStorage =
									    bFusedScalarStorageSafe || bAllowScalarArrayStorage;
									Plans->bRequiresScalarMapStorage =
									    bFusedScalarStorageSafe || bAllowScalarMapStorage;
									Plans->bHasProfitablePlan = Plans->bHasProfitablePlan || Plans->bRequiresScalarArrayStorage || Plans->bRequiresScalarMapStorage;
								}
								else if (Instruction.Opcode == EOpcode::CallMethod)
								{
									if (Instruction.Immediate == 1)
									{
										// `has` is shared by Map and Set. Candidate discovery
										// determines which declared local can actually use scalar
										// storage; retaining both requirements here lets the runtime
										// typed binding select the authoritative receiver kind.
										Plans->bRequiresScalarMapStorage =
										    bFusedScalarStorageSafe || bAllowScalarMapStorage;
										Plans->bRequiresScalarSetStorage =
										    bFusedScalarStorageSafe || bAllowScalarSetStorage;
									}
									else if (Instruction.Immediate == 3)
									{
										Plans->bRequiresScalarSetStorage =
										    bFusedScalarStorageSafe || bAllowScalarSetStorage;
									}
									if (Instruction.Immediate == 2)
									{
										Plans->bRequiresScalarArrayStorage =
										    bFusedScalarStorageSafe || bAllowScalarArrayStorage;
									}
									Plans->bHasProfitablePlan = Plans->bHasProfitablePlan || (Instruction.Immediate == 2 ? Plans->bRequiresScalarArrayStorage : Plans->bRequiresScalarMapStorage || Plans->bRequiresScalarSetStorage);
								}
							}
						}
						const bool bSelfContainedObjectLoopProof =
						    Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectFieldUpdate || Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectFieldAccumulate || Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectFieldBatchUpdate || Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectStateFieldAccumulate || Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectVirtualTransform || Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateArrayVirtualTransform;
						switch (bFusedScalarStorageSafe || bSelfContainedObjectLoopProof
						        ? Plan.FusedKind
						        : FTypedLoopPlan::EFusedKind::None)
						{
						case FTypedLoopPlan::EFusedKind::DirectIndexedUpdate:
						case FTypedLoopPlan::EFusedKind::DirectConstantScalarIndexedUpdate:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bRequiresScalarMapStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::DirectIndexedSetContains:
							{
								Plans->bRequiresScalarSetStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::DirectAffineIndexedUpdate:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bRequiresScalarMapStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::DirectAffineSetContains:
							{
								Plans->bRequiresScalarSetStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::DirectFieldAccumulate:
							{
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::DirectFieldBatchUpdate:
							{
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::DirectStateFieldAccumulate:
							{
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::StateIndirectIndexedUpdate:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bRequiresScalarMapStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::StateIndirectSetContains:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bRequiresScalarSetStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::StateIndirectFieldUpdate:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::StateIndirectStringLookupCompare:
						case FTypedLoopPlan::EFusedKind::StateStringMapLookup:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bRequiresScalarMapStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::StateDualStringArrayCompare:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::StateIndirectVirtualTransform:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::StateArrayVirtualTransform:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::StateMapF64Update:
							{
								Plans->bRequiresScalarMapStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::IndirectArrayUpdate:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::IndirectMapUpdate:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bRequiresScalarMapStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::IndirectSetContains:
							{
								Plans->bRequiresScalarArrayStorage = true;
								Plans->bRequiresScalarSetStorage = true;
								Plans->bHasProfitablePlan = true;
								break;
							}
						case FTypedLoopPlan::EFusedKind::None:
							{
								break;
							}
						default:
							{
								break;
							}
						}
						bool bAdmitPlan = true;
						if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::None)
						{
							for (auto& Method : Plan.FieldMethods)
							{
								Method.NativeCode = reinterpret_cast<void*>(Arena.EmitFieldI32Expression(Method.Instructions, Method.ArgumentCount));
							}
							for (auto& Method : Plan.GenericPureMethods)
							{
								Method.NativeCode = Method.FieldNameIndices.empty()
									? reinterpret_cast<void*>(Arena.EmitPureI32Expression(Method.Instructions))
									: reinterpret_cast<void*>(Arena.EmitFieldI32Expression(Method.Instructions));
							}
							bAdmitPlan = std::all_of(Plan.FieldMethods.begin(), Plan.FieldMethods.end(),
								[](const auto& Method) { return Method.NativeCode != nullptr; }) && std::all_of(
							    Plan.GenericPureMethods.begin(),
							    Plan.GenericPureMethods.end(),
							    [](const auto& Method)
							{
								return Method.NativeCode != nullptr;
							});
							if (bAdmitPlan)
							{
								Plan.NativeI32Code = reinterpret_cast<void*>(
								    Arena.EmitTypedLoopI32(Plan));
								if (Plan.bRuntimeBoundArrayPush && !Plan.NativeI32Code)
								{
									bAdmitPlan = false;
								}
								if (Plan.bWideCandidate)
								{
									Plan.NativeWideCode = reinterpret_cast<void*>(Arena.EmitTypedLoopWide(Plan));
								}
								if (Plan.bOwnsStringTemporaries && !Plan.NativeI32Code && !Plan.NativeWideCode)
								{
									// Owning string conversions require the complete native lifetime path.
									// Other architectures retain the authoritative VM loop without first
									// entering a partial typed execution or retaining its scratch.
									bAdmitPlan = false;
								}
								if (Plan.bHoistsNonEscapingObject && Plan.NativeI32Code == nullptr)
								{
									// Hoisting changes the loop body from allocation/call bytecode
									// into stable field-slot operations.  Only admit it when that
									// transformed body has a native lowering; the portable Linear
									// executor would otherwise retain its dispatch cost while also
									// making fallback ownership unnecessarily complicated.
									bAdmitPlan = false;
								}
								if ((!Plan.GenericPureMethods.empty() || !Plan.FieldMethods.empty()) && Plan.NativeI32Code == nullptr)
								{
									// A dynamic pure-method plan is only profitable and safe when
									// both halves of the native chain exist. Retaining the portable
									// typed-loop executor here would reintroduce a per-instruction
									// receiver bridge and can be slower than the bytecode VM.
									bAdmitPlan = false;
								}
							}
						}
						else if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectVirtualTransform)
						{
// This fusion batches resolved direct NativeCfg summaries. Without a
// supported native backend the runtime preflight necessarily declines,
// leaving the boxed Linear loop substantially slower than the VM.
							#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
							bAdmitPlan = true;
							#else
							bAdmitPlan = false;
							#endif
						}
						else if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateArrayVirtualTransform)
						{
							for (auto& Method : Plan.StateArrayVirtual.Methods)
							{
								Method.NativeCode = reinterpret_cast<void*>(
								    Arena.EmitPureI32Expression(Method.Instructions));
							}
							// The portable expression interpreter is a semantic fallback, not
							// a profitable loop kernel: invoking it for every virtual call is
							// measurably slower than the bytecode VM when asmjit/x64 is absent
							// or an expression cannot be emitted. Reject the complete fusion in
							// that case so the function-level cost model selects VM instead of
							// retaining a negatively profitable Linear plan.
							bAdmitPlan = std::all_of(
							    Plan.StateArrayVirtual.Methods.begin(),
							    Plan.StateArrayVirtual.Methods.end(),
							    [](const FTypedLoopPlan::FPureI32MethodExpression& Method)
							{
								return Method.NativeCode != nullptr;
							});
						}
						const bool bPlanProfitable = Plans->bHasProfitablePlan;
						const bool bPlanRequiresArray =
						    Plans->bRequiresScalarArrayStorage;
						const bool bPlanRequiresMap =
						    Plans->bRequiresScalarMapStorage;
						const bool bPlanRequiresSet =
						    Plans->bRequiresScalarSetStorage;
						Plans->bHasProfitablePlan = bPreviousProfitable;
						Plans->bRequiresScalarArrayStorage = bPreviousArray;
						Plans->bRequiresScalarMapStorage = bPreviousMap;
						Plans->bRequiresScalarSetStorage = bPreviousSet;
						if (bAdmitPlan)
						{
							Plan.bProfitable = bPlanProfitable || Plan.bRuntimeBoundArrayPush;
							Plans->bHasProfitablePlan =
							    Plans->bHasProfitablePlan || bPlanProfitable;
							Plans->bRequiresScalarArrayStorage =
							    Plans->bRequiresScalarArrayStorage || bPlanRequiresArray;
							Plans->bRequiresScalarMapStorage =
							    Plans->bRequiresScalarMapStorage || bPlanRequiresMap;
							Plans->bRequiresScalarSetStorage =
							    Plans->bRequiresScalarSetStorage || bPlanRequiresSet;
							if (!bPlanProfitable && LoopContainsComplexOperation(Target, Ip))
							{
								// An optional storage probe must leave the function's existing
								// backend choice intact when its runtime receiver is incompatible.
								if (Plan.bRuntimeBoundArrayPush)
								{
									Plans->bHasUnplannedComplexLoop = true;
								}
								else
								{
									Plans->bHasUnprofitableComplexPlan = true;
								}
							}
							Plans->Plans.push_back(std::move(Plan));
							Plans->PlanAtOffset[Target] = static_cast<std::uint16_t>(
							    Plans->Plans.size());
							++Plans->PlannedBackwardLoopCount;
						}
						else
						{
							Plans->bHasUnplannedComplexLoop = true;
							Plans->bNativeOnlyPlanUnavailable |= !Plan.bRuntimeBoundArrayPush;
						}
					}
					else if (LoopContainsComplexOperation(Target, Ip))
					{
						Plans->bHasUnplannedComplexLoop = true;
					}
				}
			}
			Ip += Size;
		}
		// Preserve coverage metadata even when no plan was admitted.  The caller
		// can then make a deliberate VM-vs-Linear decision for complex loops.
		return Plans->Plans.empty() && !Plans->bHasUnplannedComplexLoop
		    ? std::shared_ptr<FTypedLoopPlanSet>{}
		    : std::move(Plans);
	}

}
