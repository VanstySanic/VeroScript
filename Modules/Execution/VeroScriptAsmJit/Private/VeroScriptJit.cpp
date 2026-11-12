// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptJitCacheInternal.h"

#include "VeroScriptJitLinear.h"
#include "VeroScriptJitTextPatterns.h"
#include "VeroScriptJitNative.h"
#include "VeroScriptJitRawScratch.h"
#include "VeroScriptJitLowering.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptAsmJitExecutionContext.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>

namespace VeroScript::AsmJit
{
	namespace
	{
		// Short field helpers cannot amortize a boxed JIT invocation.
		// Direct scalar kernels and loops retain their independent dispatch.
		bool ShouldPreferVmForShortMethod(const FBytecodeFunction& Function)
		{
			if (!Function.bIsMethod || Function.Code.empty() || Function.Code.size() > 160u)
			{
				return false;
			}
			bool bHasFields = false;
			unsigned Arithmetic = 0;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (!Size || Size > Function.Code.size() - Ip)
				{
					return false;
				}
				if ((Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse) &&
				    Bytecode::ReadI32LE(Function.Code.data() + Ip + 1) < 0)
				{
					return false;
				}
				switch (Op)
				{
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::StoreField:
					{
						bHasFields = true;
						break;
					}
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
				case EOpcode::IncField:
					{
						if (++Arithmetic > 4)
						{
							return false;
						}
						break;
					}
				case EOpcode::JmpIfLocalGeImm:
				case EOpcode::NewDynamicArray:
				case EOpcode::NewNativeArray:
				case EOpcode::NewUninitializedNativeArray:
				case EOpcode::NewMap:
				case EOpcode::NewSet:
				case EOpcode::NewOrderedMap:
				case EOpcode::GetIndexCopy:
				case EOpcode::SetIndex:
				case EOpcode::GetIndexRef:
				case EOpcode::GetIndexPathRef:
					return false;
				default:
					{
						break;
					}
				}
				Ip += Size;
			}
			return bHasFields || Function.Code.size() <= 4u;
		}

		bool RequiresJitThisObject(const FBytecodeFunction& Function)
		{
			if (!Function.bIsMethod)
			{
				return false;
			}
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				switch (Op)
				{
				case EOpcode::LoadThis:
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
				case EOpcode::MakeThisFieldRef:
				case EOpcode::MakeClosure:
				case EOpcode::CallBoundMethod:
				case EOpcode::CallNative:
					return true;
				default:
					{
						break;
					}
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Function.Code.size())
				{
					// Hand-authored/unvalidated bytecode must keep the conservative
					// receiver publication used by the generic bridge.
					return true;
				}
				Ip += Size;
			}
			return false;
		}

		bool MutatesScriptFields(const FBytecodeFunction& Function)
		{
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Function.Code[Ip]);
				if (Op == EOpcode::StoreField || Op == EOpcode::StoreThisField || Op == EOpcode::InitializeThisField || Op == EOpcode::IncField)
				{
					return true;
				}
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Function.Code.size() - Ip)
				{
					// Invalid bytecode is rejected before execution. Keep metadata
					// conservative if a hand-authored function reaches JIT analysis.
					return true;
				}
				Ip += Size;
			}
			return false;
		}

		void PopulateRuntimeErrorInstructionOffsets(
		    const FBytecodeFunction& Function,
		    FJitFunctionEntry& Entry)
		{
			std::array<bool, static_cast<std::size_t>(EJitRuntimeStatus::Count)>
			    bAmbiguous{};
			auto RecordUnique = [&Entry, &bAmbiguous](
			                        const EJitRuntimeStatus Status,
			                        const std::size_t InstructionOffset)
			{
				const std::size_t StatusIndex = static_cast<std::size_t>(Status);
				if (bAmbiguous[StatusIndex])
				{
					return;
				}
				std::uint32_t& RecordedOffset =
				    Entry.RuntimeErrorInstructionOffsets[StatusIndex];
				if (RecordedOffset != InvalidJitInstructionOffset)
				{
					// A native kernel reports only a status, not an instruction pointer.
					// Retaining an arbitrary one of two candidates would manufacture a
					// plausible but false diagnostic location.
					RecordedOffset = InvalidJitInstructionOffset;
					bAmbiguous[StatusIndex] = true;
					return;
				}
				RecordedOffset = static_cast<std::uint32_t>(InstructionOffset);
			};

			const std::vector<std::uint8_t>& Code = Function.Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				switch (Op)
				{
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Neg:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::AddLocalLocal:
					{
						RecordUnique(EJitRuntimeStatus::IntegerOverflow, Ip);
						break;
					}
				case EOpcode::Div:
				case EOpcode::Mod:
					{
						RecordUnique(EJitRuntimeStatus::DivisionByZero, Ip);
						RecordUnique(EJitRuntimeStatus::IntegerOverflow, Ip);
						break;
					}
				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						RecordUnique(EJitRuntimeStatus::ShiftOutOfRange, Ip);
						break;
					}
				case EOpcode::CallBuiltin:
					{
						RecordUnique(EJitRuntimeStatus::GenericFailure, Ip);
						if (Ip + 1u < Code.size())
						{
							switch (static_cast<EBuiltinId>(Code[Ip + 1u]))
							{
							case EBuiltinId::Abs:
								{
									RecordUnique(EJitRuntimeStatus::IntegerOverflow, Ip);
									break;
								}
							case EBuiltinId::ToInt:
								{
									RecordUnique(EJitRuntimeStatus::ToIntOverflow, Ip);
									break;
								}
							case EBuiltinId::ToUnsigned:
								{
									RecordUnique(EJitRuntimeStatus::ToUnsignedOverflow, Ip);
									break;
								}
							default:
								{
									break;
								}
							}
						}
						break;
					}
				case EOpcode::LoadGlobal:
				case EOpcode::StoreGlobal:
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
				case EOpcode::IncField:
				case EOpcode::Call:
				case EOpcode::CallMethod:
				case EOpcode::CallBoundMethod:
					{
						// New native CFG emitters return a packed exact offset. This
						// conservative map remains valuable for scalar leaves, portable
						// fallbacks, and manually supplied legacy kernels.
						RecordUnique(EJitRuntimeStatus::GenericFailure, Ip);
						break;
					}
				default:
					{
						break;
					}
				}

				const std::size_t InstructionSize = Bytecode::InstructionSize(Op);
				if (InstructionSize == 0 || Ip + InstructionSize > Code.size())
				{
					// Validated bytecode never reaches this case. Do not derive source
					// metadata from an invalid stream supplied through a public API.
					return;
				}
				Ip += InstructionSize;
			}
		}

		constexpr std::string_view kFnIntIntToInt = "fn(int,int)->int";
		constexpr std::string_view kFnIntToInt = "fn(int)->int";
		constexpr std::string_view kFnToInt = "fn()->int";
		constexpr std::string_view kFnIntIntToBool = "fn(int,int)->bool";
		constexpr std::string_view kFnBoolToBool = "fn(bool)->bool";
		constexpr std::string_view kFnToBool = "fn()->bool";
		constexpr std::string_view kFnUintUintToUint = "fn(unsigned int,unsigned int)->unsigned int";
		constexpr std::string_view kFnUintUintToBool = "fn(unsigned int,unsigned int)->bool";
		constexpr std::string_view kFnUintToUint = "fn(unsigned int)->unsigned int";
		constexpr std::string_view kFnUintToBool = "fn(unsigned int)->bool";
		constexpr std::string_view kFnDoubleDoubleToDouble = "fn(double,double)->double";
		constexpr std::string_view kFnDoubleDoubleToBool = "fn(double,double)->bool";
		constexpr std::string_view kFnDoubleToDouble = "fn(double)->double";
		constexpr std::string_view kFnDoubleToInt = "fn(double)->int";
		constexpr std::string_view kFnIntToDouble = "fn(int)->double";
		constexpr std::string_view kFnIntToUnsigned = "fn(int)->unsigned int";
		constexpr std::string_view kFnIntToUint = "fn(int)->uint";
		constexpr std::string_view kFnI64I64ToI64 = "fn(int64,int64)->int64";
		constexpr std::string_view kFnI64I64ToBool = "fn(int64,int64)->bool";
		constexpr std::string_view kFnI64ToI64 = "fn(int64)->int64";
		constexpr std::string_view kFnU64U64ToU64 = "fn(uint64,uint64)->uint64";
		constexpr std::string_view kFnU64U64ToBool = "fn(uint64,uint64)->bool";
		constexpr std::string_view kFnU64ToU64 = "fn(uint64)->uint64";
		constexpr std::string_view kFnF32F32ToF32 = "fn(float,float)->float";
		constexpr std::string_view kFnF32F32ToBool = "fn(float,float)->bool";
		constexpr std::string_view kFnF32ToF32 = "fn(float)->float";

		// Sentinel so cast trampolines count as native leaves without asmjit kernels.
		alignas(void*) char GCastNativeMarker = 0;

		std::size_t SkipNops(const std::vector<std::uint8_t>& Code, std::size_t Ip)
		{
			while (Ip < Code.size() && static_cast<EOpcode>(Code[Ip]) == EOpcode::Nop)
			{
				Ip += Bytecode::InstructionSize(EOpcode::Nop);
			}
			return Ip;
		}

		bool MatchAt(
		    const std::vector<std::uint8_t>& Code,
		    std::size_t Ip,
		    const EOpcode Opcode)
		{
			return Ip < Code.size() && static_cast<EOpcode>(Code[Ip]) == Opcode;
		}

		bool MatchLoadLocal(
		    const std::vector<std::uint8_t>& Code,
		    std::size_t Ip,
		    const std::uint16_t Slot)
		{
			return MatchAt(Code, Ip, EOpcode::LoadLocal) && Bytecode::ReadU16LE(Code.data() + Ip + 1) == Slot;
		}

		bool ReadConstI32(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    std::int32_t& OutValue)
		{
			if (!MatchAt(Code, Ip, EOpcode::ConstI32))
			{
				return false;
			}
			const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (ConstIndex >= Module.Constants.size())
			{
				return false;
			}
			const FBytecodeConstant& Constant = Module.Constants[ConstIndex];
			if (Constant.Kind != EConstantKind::I32)
			{
				return false;
			}
			OutValue = Constant.I32;
			return true;
		}

		bool ReadConstU32(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    std::uint32_t& OutValue)
		{
			if (!MatchAt(Code, Ip, EOpcode::ConstI32))
			{
				return false;
			}
			const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (ConstIndex >= Module.Constants.size())
			{
				return false;
			}
			const FBytecodeConstant& Constant = Module.Constants[ConstIndex];
			if (Constant.Kind != EConstantKind::U32)
			{
				return false;
			}
			OutValue = Constant.U32;
			return true;
		}

		bool ReadConstI64(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    std::int64_t& OutValue)
		{
			if (!MatchAt(Code, Ip, EOpcode::ConstI32))
			{
				return false;
			}
			const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (ConstIndex >= Module.Constants.size() || Module.Constants[ConstIndex].Kind != EConstantKind::I64)
			{
				return false;
			}
			OutValue = Module.Constants[ConstIndex].I64;
			return true;
		}

		bool ReadConstU64(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    std::uint64_t& OutValue)
		{
			if (!MatchAt(Code, Ip, EOpcode::ConstI32))
			{
				return false;
			}
			const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (ConstIndex >= Module.Constants.size() || Module.Constants[ConstIndex].Kind != EConstantKind::U64)
			{
				return false;
			}
			OutValue = Module.Constants[ConstIndex].U64;
			return true;
		}

		bool ReadConstF32(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::size_t Ip,
		    float& OutValue)
		{
			if (!MatchAt(Code, Ip, EOpcode::ConstF64))
			{
				return false;
			}
			const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (ConstIndex >= Module.Constants.size() || Module.Constants[ConstIndex].Kind != EConstantKind::F32)
			{
				return false;
			}
			OutValue = Module.Constants[ConstIndex].F32;
			return true;
		}

		bool HasExactClosureType(
		    const FBytecodeFunction& Function,
		    const std::string_view Expected)
		{
			return Function.ClosureTypeName == Expected;
		}

		bool IsSafeIdentityClosureType(const FBytecodeFunction& Function)
		{
			static constexpr std::string_view kAllowed[] = {
			    "fn(int)->int",
			    "fn(uint)->uint",
			    "fn(unsigned int)->unsigned int",
			    "fn(double)->double",
			    "fn(int64)->int64",
			    "fn(uint64)->uint64",
			    "fn(float)->float",
			    "fn(bool)->bool",
			    "fn(string)->string",
			    "fn(wstring)->wstring",
			};
			for (const std::string_view Allowed : kAllowed)
			{
				if (Function.ClosureTypeName == Allowed)
				{
					return true;
				}
			}
			return false;
		}

		bool IsTrivialVoidLeaf(const FBytecodeModule& Module, const FBytecodeFunction& Function)
		{
			if (!Function.bReturnsVoid)
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (Ip == Code.size())
			{
				return true;
			}
			if (MatchAt(Code, Ip, EOpcode::Return))
			{
				Ip += Bytecode::InstructionSize(EOpcode::Return);
				return SkipNops(Code, Ip) == Code.size();
			}
			std::int32_t Imm = 0;
			if (ReadConstI32(Module, Code, Ip, Imm) && Imm == 0)
			{
				Ip += Bytecode::InstructionSize(EOpcode::ConstI32);
				Ip = SkipNops(Code, Ip);
				if (!MatchAt(Code, Ip, EOpcode::Return))
				{
					return false;
				}
				Ip += Bytecode::InstructionSize(EOpcode::Return);
				return SkipNops(Code, Ip) == Code.size();
			}
			return false;
		}

		/**
		 * Binary leaf: LoadLocal a; LoadLocal b; Op; Return.
		 * Non-commutative ops must pass bAllowSwappedLoads=false (only 0 then 1).
		 */
		bool IsTypedBinaryLeaf(
		    const FBytecodeFunction& Function,
		    const EOpcode Op,
		    const std::string_view ClosureType,
		    const bool bAllowSwappedLoads)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 2 || Function.LocalCount < 2 || !HasExactClosureType(Function, ClosureType))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchAt(Code, Ip, EOpcode::LoadLocal))
			{
				return false;
			}
			const std::uint16_t First = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (First > 1)
			{
				return false;
			}
			if (!bAllowSwappedLoads && First != 0)
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			const std::uint16_t Second = static_cast<std::uint16_t>(1u - First);
			if (!MatchLoadLocal(Code, Ip, Second))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, Op))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(Op);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsBinaryI32Leaf(
		    const FBytecodeFunction& Function,
		    const EOpcode Op,
		    const bool bAllowSwappedLoads)
		{
			return IsTypedBinaryLeaf(Function, Op, kFnIntIntToInt, bAllowSwappedLoads);
		}

		bool IsNegI32Leaf(const FBytecodeFunction& Function)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, kFnIntToInt))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Neg))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Neg);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsBitNotI32Leaf(const FBytecodeFunction& Function)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, kFnIntToInt))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::BitNot))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::BitNot);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsNegDoubleLeaf(const FBytecodeFunction& Function)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, kFnDoubleToDouble))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Neg))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Neg);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsUnaryCallBuiltinLeaf(
		    const FBytecodeFunction& Function,
		    const EBuiltinId Builtin,
		    const std::string_view ClosureType)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, ClosureType))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::CallBuiltin) || Ip + 2 >= Code.size() || static_cast<EBuiltinId>(Code[Ip + 1]) != Builtin || Code[Ip + 2] != 1)
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::CallBuiltin);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsCastF64ToI32Leaf(const FBytecodeFunction& Function)
		{
			return IsUnaryCallBuiltinLeaf(Function, EBuiltinId::ToInt, kFnDoubleToInt);
		}

		bool IsCastI32ToF64Leaf(const FBytecodeFunction& Function)
		{
			return IsUnaryCallBuiltinLeaf(Function, EBuiltinId::ToDouble, kFnIntToDouble);
		}

		bool IsCastI32ToU32Leaf(const FBytecodeFunction& Function)
		{
			return IsUnaryCallBuiltinLeaf(Function, EBuiltinId::ToUnsigned, kFnIntToUnsigned) || IsUnaryCallBuiltinLeaf(Function, EBuiltinId::ToUnsigned, kFnIntToUint);
		}

		bool IsNotBoolLeaf(const FBytecodeFunction& Function)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, kFnBoolToBool))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Not))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Not);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsIdentityLocal0Leaf(const FBytecodeFunction& Function)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !IsSafeIdentityClosureType(Function))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsConstI32ReturnLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    std::int32_t& OutImm)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 0 || !HasExactClosureType(Function, kFnToInt))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!ReadConstI32(Module, Code, Ip, OutImm))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::ConstI32);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsConstBoolReturnLeaf(const FBytecodeFunction& Function, bool& OutValue)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 0 || !HasExactClosureType(Function, kFnToBool))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchAt(Code, Ip, EOpcode::ConstBool) || Ip + 1 >= Code.size())
			{
				return false;
			}
			OutValue = Code[Ip + 1] != 0;
			Ip += Bytecode::InstructionSize(EOpcode::ConstBool);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsConstF64ReturnLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    double& OutValue)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 0 || Function.ClosureTypeName != "fn()->double")
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchAt(Code, Ip, EOpcode::ConstF64))
			{
				return false;
			}
			const std::uint16_t ConstIndex = Bytecode::ReadU16LE(Code.data() + Ip + 1);
			if (ConstIndex >= Module.Constants.size() || Module.Constants[ConstIndex].Kind != EConstantKind::F64)
			{
				return false;
			}
			OutValue = Module.Constants[ConstIndex].F64;
			Ip += Bytecode::InstructionSize(EOpcode::ConstF64);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsNegUintLeaf(const FBytecodeFunction& Function)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, kFnUintToUint))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Neg))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Neg);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsBitNotUintLeaf(const FBytecodeFunction& Function)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, kFnUintToUint))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::BitNot))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::BitNot);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsLocal0ConstBinaryLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const EOpcode Op,
		    std::int32_t& OutImm)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, kFnIntToInt))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!ReadConstI32(Module, Code, Ip, OutImm))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::ConstI32);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, Op))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(Op);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsTypedUnaryLeaf(
		    const FBytecodeFunction& Function,
		    const EOpcode Op,
		    const std::string_view ClosureType)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, ClosureType))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, Op))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(Op);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsConstU32ReturnLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    std::uint32_t& OutValue)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 0 || (Function.ClosureTypeName != "fn()->unsigned int" && Function.ClosureTypeName != "fn()->uint"))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!ReadConstU32(Module, Code, Ip, OutValue))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::ConstI32);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool IsLocal0ConstU32BinaryLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const EOpcode Op,
		    std::uint32_t& OutImm)
		{
			const bool bComparison = Op == EOpcode::Eq || Op == EOpcode::Ne || Op == EOpcode::Lt || Op == EOpcode::Le || Op == EOpcode::Gt || Op == EOpcode::Ge;
			const bool bClosureMatches = bComparison
			    ? (Function.ClosureTypeName == kFnUintToBool || Function.ClosureTypeName == "fn(uint)->bool")
			    : (Function.ClosureTypeName == kFnUintToUint || Function.ClosureTypeName == "fn(uint)->uint");
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !bClosureMatches)
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!ReadConstU32(Module, Code, Ip, OutImm))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::ConstI32);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, Op))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(Op);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		template <typename TValue, typename TReader>
		bool IsLocal0ConstWideBinaryLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const EOpcode Op,
		    const std::string_view ValueClosure,
		    const std::string_view BoolClosure,
		    const EOpcode ConstOpcode,
		    TReader&& ReadConstant,
		    TValue& OutValue)
		{
			const bool bComparison = Op >= EOpcode::Eq && Op <= EOpcode::Ge;
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || Function.ClosureTypeName != (bComparison ? BoolClosure : ValueClosure))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!ReadConstant(Module, Code, Ip, OutValue))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(ConstOpcode);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, Op))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(Op);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		// ConstI32; LoadLocal 0; Op; Return — Imm is the left operand.
		bool IsConstLocal0BinaryLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const EOpcode Op,
		    std::int32_t& OutImm)
		{
			if (Function.bReturnsVoid || Function.ParamCount != 1 || Function.LocalCount < 1 || !HasExactClosureType(Function, kFnIntToInt))
			{
				return false;
			}
			const std::vector<std::uint8_t>& Code = Function.Code;
			std::size_t Ip = SkipNops(Code, 0);
			if (!ReadConstI32(Module, Code, Ip, OutImm))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::ConstI32);
			Ip = SkipNops(Code, Ip);
			if (!MatchLoadLocal(Code, Ip, 0))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::LoadLocal);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, Op))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(Op);
			Ip = SkipNops(Code, Ip);
			if (!MatchAt(Code, Ip, EOpcode::Return))
			{
				return false;
			}
			Ip += Bytecode::InstructionSize(EOpcode::Return);
			return SkipNops(Code, Ip) == Code.size();
		}

		bool EvalShift(
		    FExecutionContext* const Vm,
		    const std::int32_t Left,
		    const std::int32_t Right,
		    const bool bShl,
		    std::int32_t& OutResult)
		{
			if (Right < 0 || Right > 31)
			{
				Vm->JitReportRuntimeError(
				    "shift count out of range", EJitRuntimeStatus::ShiftOutOfRange);
				return false;
			}
			OutResult = bShl
			    ? static_cast<std::int32_t>(
			          static_cast<std::uint32_t>(Left) << static_cast<std::uint32_t>(Right))
			    : static_cast<std::int32_t>(Left >> Right);
			return true;
		}

		bool ReadTwoI32Args(FExecutionContext* const Vm, std::int32_t& OutLeft, std::int32_t& OutRight)
		{
			if (Vm->GetPendingJitArgumentCount() != 2 || Vm->GetPendingJitArguments() == nullptr)
			{
				Vm->JitReportRuntimeError("JIT binary op expects 2 arguments");
				return false;
			}
			const auto* const Left =
			    std::get_if<std::int32_t>(&Vm->GetPendingJitArguments()[0]);
			const auto* const Right =
			    std::get_if<std::int32_t>(&Vm->GetPendingJitArguments()[1]);
			if (!Left || !Right)
			{
				Vm->JitReportRuntimeError("JIT binary op requires int arguments");
				return false;
			}
			OutLeft = *Left;
			OutRight = *Right;
			return true;
		}

		bool ReadTwoU32Args(FExecutionContext* const Vm, std::uint32_t& OutLeft, std::uint32_t& OutRight)
		{
			if (Vm->GetPendingJitArgumentCount() != 2 || Vm->GetPendingJitArguments() == nullptr)
			{
				Vm->JitReportRuntimeError("JIT binary op expects 2 arguments");
				return false;
			}
			const auto* const Left =
			    std::get_if<std::uint32_t>(&Vm->GetPendingJitArguments()[0]);
			const auto* const Right =
			    std::get_if<std::uint32_t>(&Vm->GetPendingJitArguments()[1]);
			if (!Left || !Right)
			{
				Vm->JitReportRuntimeError("JIT binary op requires uint arguments");
				return false;
			}
			OutLeft = *Left;
			OutRight = *Right;
			return true;
		}

		bool ReadTwoF64Args(FExecutionContext* const Vm, double& OutLeft, double& OutRight)
		{
			if (Vm->GetPendingJitArgumentCount() != 2 || Vm->GetPendingJitArguments() == nullptr)
			{
				Vm->JitReportRuntimeError("JIT binary op expects 2 arguments");
				return false;
			}
			const auto* const Left = std::get_if<double>(&Vm->GetPendingJitArguments()[0]);
			const auto* const Right = std::get_if<double>(&Vm->GetPendingJitArguments()[1]);
			if (!Left || !Right)
			{
				Vm->JitReportRuntimeError("JIT binary op requires double arguments");
				return false;
			}
			OutLeft = *Left;
			OutRight = *Right;
			return true;
		}

		bool ReadOneU32Arg(FExecutionContext* const Vm, std::uint32_t& OutValue)
		{
			if (Vm->GetPendingJitArgumentCount() != 1 || Vm->GetPendingJitArguments() == nullptr)
			{
				Vm->JitReportRuntimeError("JIT unary op expects 1 argument");
				return false;
			}
			const auto* const Value =
			    std::get_if<std::uint32_t>(&Vm->GetPendingJitArguments()[0]);
			if (!Value)
			{
				Vm->JitReportRuntimeError("JIT unary op requires uint argument");
				return false;
			}
			OutValue = *Value;
			return true;
		}

		bool ReadOneF64Arg(FExecutionContext* const Vm, double& OutValue)
		{
			if (Vm->GetPendingJitArgumentCount() != 1 || Vm->GetPendingJitArguments() == nullptr)
			{
				Vm->JitReportRuntimeError("JIT unary op expects 1 argument");
				return false;
			}
			const auto* const Value = std::get_if<double>(&Vm->GetPendingJitArguments()[0]);
			if (!Value)
			{
				Vm->JitReportRuntimeError("JIT unary op requires double argument");
				return false;
			}
			OutValue = *Value;
			return true;
		}

		bool ReadOneI32Arg(FExecutionContext* const Vm, std::int32_t& OutValue)
		{
			if (Vm->GetPendingJitArgumentCount() != 1 || Vm->GetPendingJitArguments() == nullptr)
			{
				Vm->JitReportRuntimeError("JIT unary op expects 1 argument");
				return false;
			}
			const auto* const Value =
			    std::get_if<std::int32_t>(&Vm->GetPendingJitArguments()[0]);
			if (!Value)
			{
				Vm->JitReportRuntimeError("JIT unary op requires int argument");
				return false;
			}
			OutValue = *Value;
			return true;
		}

		bool ReadOneBoolArg(FExecutionContext* const Vm, bool& OutValue)
		{
			if (Vm->GetPendingJitArgumentCount() != 1 || Vm->GetPendingJitArguments() == nullptr)
			{
				Vm->JitReportRuntimeError("JIT not expects 1 argument");
				return false;
			}
			const auto* const Value = std::get_if<bool>(&Vm->GetPendingJitArguments()[0]);
			if (!Value)
			{
				Vm->JitReportRuntimeError("JIT not requires bool argument");
				return false;
			}
			OutValue = *Value;
			return true;
		}

		bool EvalDivOrMod(
		    FExecutionContext* const Vm,
		    const std::int32_t Left,
		    const std::int32_t Right,
		    const bool bDiv,
		    std::int32_t& OutResult)
		{
			if (Right == 0)
			{
				Vm->JitReportRuntimeError(
				    "division by zero", EJitRuntimeStatus::DivisionByZero);
				return false;
			}
			if (Left == std::numeric_limits<std::int32_t>::min() && Right == -1)
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			OutResult = bDiv
			    ? static_cast<std::int32_t>(Left / Right)
			    : static_cast<std::int32_t>(Left % Right);
			return true;
		}

		bool JitTrampoline_TrivialVoid(void* const VmContext)
		{
			(void)VmContext;
			return true;
		}

		bool JitTrampoline_AddI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!Runtime::CheckedAdd(Left, Right, Result))
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_SubI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!Runtime::CheckedSub(Left, Right, Result))
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_MulI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!Runtime::CheckedMul(Left, Right, Result))
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_DivI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalDivOrMod(Vm, Left, Right, true, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_ModI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalDivOrMod(Vm, Left, Right, false, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_BitAndI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(Left & Right);
		}

		bool JitTrampoline_BitOrI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(Left | Right);
		}

		bool JitTrampoline_BitXorI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(Left ^ Right);
		}

		bool JitTrampoline_ShlI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalShift(Vm, Left, Right, true, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_ShrI32I32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalShift(Vm, Left, Right, false, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_BitNotI32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::int32_t>(~Value));
		}

		bool JitTrampoline_AddU32U32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Left = 0;
			std::uint32_t Right = 0;
			if (!ReadTwoU32Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(Left + Right));
		}

		bool JitTrampoline_SubU32U32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Left = 0;
			std::uint32_t Right = 0;
			if (!ReadTwoU32Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(Left - Right));
		}

		bool JitTrampoline_MulU32U32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Left = 0;
			std::uint32_t Right = 0;
			if (!ReadTwoU32Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(Left * Right));
		}

		bool JitTrampoline_DivU32U32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Left = 0;
			std::uint32_t Right = 0;
			if (!ReadTwoU32Args(Vm, Left, Right))
			{
				return false;
			}
			if (Right == 0)
			{
				Vm->JitReportRuntimeError(
				    "division by zero", EJitRuntimeStatus::DivisionByZero);
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(Left / Right));
		}

		bool JitTrampoline_ModU32U32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Left = 0;
			std::uint32_t Right = 0;
			if (!ReadTwoU32Args(Vm, Left, Right))
			{
				return false;
			}
			if (Right == 0)
			{
				Vm->JitReportRuntimeError(
				    "division by zero", EJitRuntimeStatus::DivisionByZero);
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(Left % Right));
		}

		bool JitTrampoline_NegU32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Value = 0;
			if (!ReadOneU32Arg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(0u - Value));
		}

		bool JitTrampoline_BitNotU32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Value = 0;
			if (!ReadOneU32Arg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(~Value));
		}

		bool JitTrampoline_ConstF64(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitPushReturn(Vm->GetPendingJitPayloadF64());
		}

		bool JitTrampoline_AddF64F64(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			double Left = 0.0;
			double Right = 0.0;
			if (!ReadTwoF64Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(Left + Right);
		}

		bool JitTrampoline_SubF64F64(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			double Left = 0.0;
			double Right = 0.0;
			if (!ReadTwoF64Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(Left - Right);
		}

		bool JitTrampoline_MulF64F64(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			double Left = 0.0;
			double Right = 0.0;
			if (!ReadTwoF64Args(Vm, Left, Right))
			{
				return false;
			}
			return Vm->JitPushReturn(Left * Right);
		}

		bool JitTrampoline_DivF64F64(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			double Left = 0.0;
			double Right = 0.0;
			if (!ReadTwoF64Args(Vm, Left, Right))
			{
				return false;
			}
			if (Right == 0.0)
			{
				Vm->JitReportRuntimeError(
				    "division by zero", EJitRuntimeStatus::DivisionByZero);
				return false;
			}
			return Vm->JitPushReturn(Left / Right);
		}

		bool JitTrampoline_NegF64(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			double Value = 0.0;
			if (!ReadOneF64Arg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(-Value);
		}

		template <EOpcode Op>
		bool JitTrampoline_CmpU32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Left = 0;
			std::uint32_t Right = 0;
			if (!ReadTwoU32Args(Vm, Left, Right))
			{
				return false;
			}
			if constexpr (Op == EOpcode::Eq)
			{
				return Vm->JitPushReturn(Left == Right);
			}
			else if constexpr (Op == EOpcode::Ne)
			{
				return Vm->JitPushReturn(Left != Right);
			}
			else if constexpr (Op == EOpcode::Lt)
			{
				return Vm->JitPushReturn(Left < Right);
			}
			else if constexpr (Op == EOpcode::Le)
			{
				return Vm->JitPushReturn(Left <= Right);
			}
			else if constexpr (Op == EOpcode::Gt)
			{
				return Vm->JitPushReturn(Left > Right);
			}
			else
			{
				static_assert(Op == EOpcode::Ge, "unsupported uint comparison opcode");
				return Vm->JitPushReturn(Left >= Right);
			}
		}

		template <EOpcode Op>
		bool JitTrampoline_CmpF64(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			double Left = 0.0;
			double Right = 0.0;
			if (!ReadTwoF64Args(Vm, Left, Right))
			{
				return false;
			}
			if constexpr (Op == EOpcode::Eq)
			{
				return Vm->JitPushReturn(Left == Right);
			}
			else if constexpr (Op == EOpcode::Ne)
			{
				return Vm->JitPushReturn(Left != Right);
			}
			else if constexpr (Op == EOpcode::Lt)
			{
				return Vm->JitPushReturn(Left < Right);
			}
			else if constexpr (Op == EOpcode::Le)
			{
				return Vm->JitPushReturn(Left <= Right);
			}
			else if constexpr (Op == EOpcode::Gt)
			{
				return Vm->JitPushReturn(Left > Right);
			}
			else
			{
				static_assert(Op == EOpcode::Ge, "unsupported double comparison opcode");
				return Vm->JitPushReturn(Left >= Right);
			}
		}

		bool JitTrampoline_NegI32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			if (Value == std::numeric_limits<std::int32_t>::min())
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::int32_t>(-Value));
		}

		bool JitTrampoline_IdentityLocal0(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			if (Vm->GetPendingJitArgumentCount() != 1 || Vm->GetPendingJitArguments() == nullptr)
			{
				Vm->JitReportRuntimeError("JIT identity expects 1 argument");
				return false;
			}
			return Vm->JitPushReturn(Vm->GetPendingJitArguments()[0]);
		}

		bool JitTrampoline_ConstI32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitPushReturn(Vm->GetPendingJitPayloadI32());
		}

		bool JitTrampoline_ConstBool(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitPushReturn(Vm->GetPendingJitPayloadI32() != 0);
		}

		bool JitTrampoline_AddLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!Runtime::CheckedAdd(Value, Vm->GetPendingJitPayloadI32(), Result))
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_SubLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!Runtime::CheckedSub(Value, Vm->GetPendingJitPayloadI32(), Result))
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_MulLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!Runtime::CheckedMul(Value, Vm->GetPendingJitPayloadI32(), Result))
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_DivLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalDivOrMod(Vm, Value, Vm->GetPendingJitPayloadI32(), true, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_ModLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalDivOrMod(Vm, Value, Vm->GetPendingJitPayloadI32(), false, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_BitAndLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(Value & Vm->GetPendingJitPayloadI32());
		}

		bool JitTrampoline_BitOrLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(Value | Vm->GetPendingJitPayloadI32());
		}

		bool JitTrampoline_BitXorLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(Value ^ Vm->GetPendingJitPayloadI32());
		}

		bool JitTrampoline_ShlLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalShift(Vm, Value, Vm->GetPendingJitPayloadI32(), true, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_ShrLocal0Imm(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalShift(Vm, Value, Vm->GetPendingJitPayloadI32(), false, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_SubImmLocal0(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!Runtime::CheckedSub(Vm->GetPendingJitPayloadI32(), Value, Result))
			{
				Vm->JitReportRuntimeError(
				    "integer overflow", EJitRuntimeStatus::IntegerOverflow);
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_DivImmLocal0(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalDivOrMod(Vm, Vm->GetPendingJitPayloadI32(), Value, true, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_ModImmLocal0(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			std::int32_t Result = 0;
			if (!EvalDivOrMod(Vm, Vm->GetPendingJitPayloadI32(), Value, false, Result))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}

		bool JitTrampoline_NotBool(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			bool Value = false;
			if (!ReadOneBoolArg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(!Value);
		}

		bool JitTrampoline_CastF64ToI32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			double Value = 0.0;
			if (!ReadOneF64Arg(Vm, Value))
			{
				return false;
			}
			if (!std::isfinite(Value) || Value >= 2147483648.0 || Value <= -2147483649.0)
			{
				Vm->JitReportRuntimeError(
				    "to_int overflow", EJitRuntimeStatus::ToIntOverflow);
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::int32_t>(Value));
		}

		bool JitTrampoline_CastI32ToF64(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			return Vm->JitPushReturn(static_cast<double>(Value));
		}

		bool JitTrampoline_CastI32ToU32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Value = 0;
			if (!ReadOneI32Arg(Vm, Value))
			{
				return false;
			}
			if (Value < 0)
			{
				Vm->JitReportRuntimeError(
				    "to_unsigned overflow", EJitRuntimeStatus::ToUnsignedOverflow);
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(Value));
		}

		void BindCastNativeLeaf(FJitFunctionEntry& OutEntry, bool (*Bridge)(void*))
		{
			OutEntry = {};
			OutEntry.Entry = Bridge;
			OutEntry.NativeCode = &GCastNativeMarker;
			OutEntry.bNativeLeaf = true;
			OutEntry.Kind = EJitEntryKind::NativeLeaf;
			OutEntry.Status = EJitCompileStatus::Ok;
		}

		template <EOpcode Op>
		bool JitTrampoline_CmpI32(void* const VmContext)
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Left = 0;
			std::int32_t Right = 0;
			if (!ReadTwoI32Args(Vm, Left, Right))
			{
				return false;
			}
			if constexpr (Op == EOpcode::Eq)
			{
				return Vm->JitPushReturn(Left == Right);
			}
			else if constexpr (Op == EOpcode::Ne)
			{
				return Vm->JitPushReturn(Left != Right);
			}
			else if constexpr (Op == EOpcode::Lt)
			{
				return Vm->JitPushReturn(Left < Right);
			}
			else if constexpr (Op == EOpcode::Le)
			{
				return Vm->JitPushReturn(Left <= Right);
			}
			else if constexpr (Op == EOpcode::Gt)
			{
				return Vm->JitPushReturn(Left > Right);
			}
			else
			{
				static_assert(Op == EOpcode::Ge, "unsupported int comparison opcode");
				return Vm->JitPushReturn(Left >= Right);
			}
		}

		bool SignatureHasOwnedValues(const FBytecodeFunction& Function)
		{
			// The runtime closure type may be a primitive alias while FValue must
			// preserve a declared host wrapper/enum identity. Keep it boxed.
			if (HasDeclaredSignatureTypeMetadata(Function.DeclMeta))
			{
				return true;
			}
			if (Function.ClosureTypeName.empty())
			{
				return false;
			}
			JitIr::FSignature Signature;
			if (!JitIr::ParseSignature(Function.ClosureTypeName, Signature) || !Signature.bValid)
			{
				// Fail closed: unknown encodings must not enter native/trivial leaves.
				return true;
			}
			const auto IsOwned = [](const JitIr::EValueType Type)
			{
				return Type == JitIr::EValueType::Any || Type == JitIr::EValueType::Unknown || Type == JitIr::EValueType::String || Type == JitIr::EValueType::WString;
			};
			if (IsOwned(Signature.ReturnType))
			{
				return true;
			}
			return std::any_of(
			    Signature.Parameters.begin(),
			    Signature.Parameters.end(),
			    IsOwned);
		}

		EJitScalarValueKind ToScalarValueKind(const JitIr::EValueType Type)
		{
			switch (Type)
			{
			case JitIr::EValueType::I8:
				return EJitScalarValueKind::I8;
			case JitIr::EValueType::U8:
				return EJitScalarValueKind::U8;
			case JitIr::EValueType::I16:
				return EJitScalarValueKind::I16;
			case JitIr::EValueType::U16:
				return EJitScalarValueKind::U16;
			case JitIr::EValueType::I32:
				return EJitScalarValueKind::I32;
			case JitIr::EValueType::U32:
				return EJitScalarValueKind::U32;
			case JitIr::EValueType::I64:
				return EJitScalarValueKind::I64;
			case JitIr::EValueType::U64:
				return EJitScalarValueKind::U64;
			case JitIr::EValueType::F32:
				return EJitScalarValueKind::F32;
			case JitIr::EValueType::F64:
				return EJitScalarValueKind::F64;
			case JitIr::EValueType::Bool:
				return EJitScalarValueKind::Bool;
			case JitIr::EValueType::Void:
				return EJitScalarValueKind::Void;
			default:
				return EJitScalarValueKind::None;
			}
		}

		bool TryBuildScalarSignatureMetadata(
		    const FBytecodeFunction& Function,
		    std::uint64_t& OutPackedKinds,
		    std::uint8_t& OutParameterCount,
		    EJitScalarValueKind& OutReturnKind)
		{
			OutPackedKinds = 0;
			OutParameterCount = 0;
			OutReturnKind = EJitScalarValueKind::None;
			if (SignatureHasOwnedValues(Function))
			{
				return false;
			}
			JitIr::FSignature Signature;
			if (!JitIr::ParseSignature(Function.ClosureTypeName, Signature) || !Signature.bValid || Signature.Parameters.size() > 16)
			{
				return false;
			}
			const EJitScalarValueKind ReturnKind = ToScalarValueKind(Signature.ReturnType);
			if (ReturnKind == EJitScalarValueKind::None)
			{
				return false;
			}
			std::uint64_t PackedKinds = 0;
			for (std::size_t Index = 0; Index < Signature.Parameters.size(); ++Index)
			{
				const EJitScalarValueKind Kind = ToScalarValueKind(Signature.Parameters[Index]);
				if (Kind == EJitScalarValueKind::None || Kind == EJitScalarValueKind::Void)
				{
					return false;
				}
				PackedKinds |= static_cast<std::uint64_t>(Kind) << (Index * 4u);
			}
			OutPackedKinds = PackedKinds;
			OutParameterCount = static_cast<std::uint8_t>(Signature.Parameters.size());
			OutReturnKind = ReturnKind;
			return true;
		}

		void PopulateScalarSignatureMetadata(
		    const FBytecodeFunction& Function,
		    FJitFunctionEntry& Entry)
		{
			Entry.ScalarParameterKinds = 0;
			Entry.ScalarParameterCount = 0;
			Entry.ScalarReturnKind = EJitScalarValueKind::None;
			if (Entry.Kind == EJitEntryKind::NativeCfg && Entry.ScalarAbi != EJitScalarAbi::None)
			{
				(void)TryBuildScalarSignatureMetadata(
				    Function,
				    Entry.ScalarParameterKinds,
				    Entry.ScalarParameterCount,
				    Entry.ScalarReturnKind);
			}
		}

		bool TryAttachDirectScalarMethodLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    JitNative::FArena& Arena,
		    FJitFunctionEntry& Entry)
		{
			if (!Function.bIsMethod || Entry.Kind != EJitEntryKind::NativeCfg || Entry.bRequiresThisObject)
			{
				return false;
			}
			FJitFunctionEntry Direct;
			const auto Attach = [&](const EJitDirectScalarKind Kind)
			{
				if (!Direct.NativeCode)
				{
					return false;
				}
				Entry.DirectScalarCode = Direct.NativeCode;
				Entry.DirectScalarKind = Kind;
				return true;
			};
			const auto AttachPortable = [&](const EJitDirectScalarKind Kind,
			                                const EOpcode Op)
			{
				Entry.DirectScalarCode = nullptr;
				Entry.DirectScalarKind = Kind;
				Entry.DirectScalarOpcode = Op;
				return true;
			};

			std::int32_t Imm = 0;
			std::uint32_t U32Value = 0;
			bool BoolValue = false;
			double F64Value = 0.0;
			if (IsConstI32ReturnLeaf(Module, Function, Imm) && JitNative::TryBindConstI32(Arena, Imm, Direct))
			{
				Entry.DirectScalarOpcode = EOpcode::ConstI32;
				Entry.DirectScalarPayloadU32 = static_cast<std::uint32_t>(Imm);
				return Attach(EJitDirectScalarKind::NullaryI32);
			}
			if (IsConstBoolReturnLeaf(Function, BoolValue) && JitNative::TryBindConstBool(Arena, BoolValue, Direct))
			{
				Entry.DirectScalarOpcode = EOpcode::ConstBool;
				Entry.DirectScalarPayloadU32 = BoolValue ? 1u : 0u;
				return Attach(EJitDirectScalarKind::NullaryI32);
			}
			Direct = {};
			if (IsConstU32ReturnLeaf(Module, Function, U32Value))
			{
				const std::int32_t SignedBits = U32Value <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
				    ? static_cast<std::int32_t>(U32Value)
				    : -1 - static_cast<std::int32_t>(std::numeric_limits<std::uint32_t>::max() - U32Value);
				if (JitNative::TryBindConstI32(Arena, SignedBits, Direct))
				{
					return Attach(EJitDirectScalarKind::NullaryU32);
				}
			}
			if (IsConstF64ReturnLeaf(Module, Function, F64Value) && JitNative::TryBindConstF64(Arena, F64Value, Direct))
			{
				return Attach(EJitDirectScalarKind::NullaryF64);
			}
			Direct = {};
			if (IsIdentityLocal0Leaf(Function) && JitNative::TryBindIdentity(Arena, Function, Direct))
			{
				if (Function.ClosureTypeName == kFnDoubleToDouble)
				{
					return Attach(EJitDirectScalarKind::UnaryF64);
				}
				if (Function.ClosureTypeName == kFnUintToUint || Function.ClosureTypeName == "fn(uint)->uint")
				{
					return Attach(EJitDirectScalarKind::UnaryU32);
				}
				return Attach(EJitDirectScalarKind::UnaryI32);
			}

			const auto TryUnaryImm = [&](const EOpcode Op) -> bool
			{
				if (!IsLocal0ConstBinaryLeaf(Module, Function, Op, Imm))
				{
					return false;
				}
				switch (Op)
				{
				case EOpcode::Add:
					return JitNative::TryBindLocal0AddImm(Arena, Imm, Direct);
				case EOpcode::Sub:
					return JitNative::TryBindLocal0SubImm(Arena, Imm, Direct);
				case EOpcode::Mul:
					return JitNative::TryBindLocal0MulImm(Arena, Imm, Direct);
				case EOpcode::Div:
					return JitNative::TryBindLocal0DivImm(Arena, Imm, Direct);
				case EOpcode::Mod:
					return JitNative::TryBindLocal0ModImm(Arena, Imm, Direct);
				case EOpcode::BitAnd:
					return JitNative::TryBindLocal0BitAndImm(Arena, Imm, Direct);
				case EOpcode::BitOr:
					return JitNative::TryBindLocal0BitOrImm(Arena, Imm, Direct);
				case EOpcode::BitXor:
					return JitNative::TryBindLocal0BitXorImm(Arena, Imm, Direct);
				case EOpcode::Shl:
					return JitNative::TryBindLocal0ShlImm(Arena, Imm, Direct);
				case EOpcode::Shr:
					return JitNative::TryBindLocal0ShrImm(Arena, Imm, Direct);
				default:
					return false;
				}
			};
			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul,
			         EOpcode::Div, EOpcode::Mod, EOpcode::BitAnd, EOpcode::BitOr,
			         EOpcode::BitXor, EOpcode::Shl, EOpcode::Shr})
			{
				Direct = {};
				if (TryUnaryImm(Op))
				{
					return Attach(EJitDirectScalarKind::UnaryI32);
				}
			}
			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul,
			         EOpcode::Div, EOpcode::Mod})
			{
				Direct = {};
				const bool bCommutative = Op == EOpcode::Add || Op == EOpcode::Mul;
				if (IsBinaryI32Leaf(Function, Op, bCommutative) && JitNative::TryBindBinaryI32(Arena, Op, Direct))
				{
					return Attach(EJitDirectScalarKind::BinaryI32);
				}
			}
			for (const EOpcode Op : {EOpcode::Eq, EOpcode::Ne, EOpcode::Lt,
			         EOpcode::Le, EOpcode::Gt, EOpcode::Ge})
			{
				Direct = {};
				const bool bCommutative = Op == EOpcode::Eq || Op == EOpcode::Ne;
				if (IsTypedBinaryLeaf(Function, Op, kFnIntIntToBool, bCommutative) && JitNative::TryBindCmpI32(Arena, Op, Direct))
				{
					return Attach(EJitDirectScalarKind::BinaryI32);
				}
			}
			Direct = {};
			if (IsNegI32Leaf(Function) && JitNative::TryBindNegI32(Arena, Direct))
			{
				return Attach(EJitDirectScalarKind::UnaryI32);
			}
			Direct = {};
			if (IsBitNotI32Leaf(Function) && JitNative::TryBindBitNotI32(Arena, Direct))
			{
				return Attach(EJitDirectScalarKind::UnaryI32);
			}
			Direct = {};
			if (IsNotBoolLeaf(Function) && JitNative::TryBindNotBool(Arena, Direct))
			{
				return Attach(EJitDirectScalarKind::UnaryI32);
			}

			std::uint32_t UImm = 0;
			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul,
			         EOpcode::Div, EOpcode::Mod})
			{
				Direct = {};
				if (IsLocal0ConstU32BinaryLeaf(Module, Function, Op, UImm) && JitNative::TryBindBinaryU32(Arena, Op, Direct))
				{
					Entry.DirectScalarPayloadU32 = UImm;
					return Attach(EJitDirectScalarKind::UnaryU32RightImm);
				}
			}
			for (const EOpcode Op : {EOpcode::Eq, EOpcode::Ne, EOpcode::Lt,
			         EOpcode::Le, EOpcode::Gt, EOpcode::Ge})
			{
				Direct = {};
				if (IsLocal0ConstU32BinaryLeaf(Module, Function, Op, UImm) && JitNative::TryBindCmpU32(Arena, Op, Direct))
				{
					Entry.DirectScalarPayloadU32 = UImm;
					return Attach(EJitDirectScalarKind::UnaryU32RightImm);
				}
			}

			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul,
			         EOpcode::Div, EOpcode::Mod})
			{
				Direct = {};
				const bool bCommutative = Op == EOpcode::Add || Op == EOpcode::Mul;
				if (IsTypedBinaryLeaf(Function, Op, kFnUintUintToUint, bCommutative) && JitNative::TryBindBinaryU32(Arena, Op, Direct))
				{
					return Attach(EJitDirectScalarKind::BinaryU32);
				}
			}
			for (const EOpcode Op : {EOpcode::Eq, EOpcode::Ne, EOpcode::Lt,
			         EOpcode::Le, EOpcode::Gt, EOpcode::Ge})
			{
				Direct = {};
				const bool bCommutative = Op == EOpcode::Eq || Op == EOpcode::Ne;
				if (IsTypedBinaryLeaf(Function, Op, kFnUintUintToBool, bCommutative) && JitNative::TryBindCmpU32(Arena, Op, Direct))
				{
					return Attach(EJitDirectScalarKind::BinaryU32);
				}
			}
			Direct = {};
			if (IsNegUintLeaf(Function) && JitNative::TryBindNegU32(Arena, Direct))
			{
				return Attach(EJitDirectScalarKind::UnaryU32);
			}
			Direct = {};
			if (IsBitNotUintLeaf(Function) && JitNative::TryBindBitNotU32(Arena, Direct))
			{
				return Attach(EJitDirectScalarKind::UnaryU32);
			}

			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub,
			         EOpcode::Mul, EOpcode::Div})
			{
				Direct = {};
				const bool bCommutative = Op == EOpcode::Add || Op == EOpcode::Mul;
				if (IsTypedBinaryLeaf(Function, Op, kFnDoubleDoubleToDouble, bCommutative) && JitNative::TryBindBinaryF64(Arena, Op, Direct))
				{
					return Attach(EJitDirectScalarKind::BinaryF64);
				}
			}
			for (const EOpcode Op : {EOpcode::Eq, EOpcode::Ne, EOpcode::Lt,
			         EOpcode::Le, EOpcode::Gt, EOpcode::Ge})
			{
				Direct = {};
				const bool bCommutative = Op == EOpcode::Eq || Op == EOpcode::Ne;
				if (IsTypedBinaryLeaf(Function, Op, kFnDoubleDoubleToBool, bCommutative) && JitNative::TryBindCmpF64(Arena, Op, Direct))
				{
					return Attach(EJitDirectScalarKind::BinaryF64Bool);
				}
			}
			Direct = {};
			if (IsNegDoubleLeaf(Function) && JitNative::TryBindNegF64(Arena, Direct))
			{
				return Attach(EJitDirectScalarKind::UnaryF64);
			}

			// I64/U64/F32 already use the portable Mixed CFG representation. For
			// receiver-independent leaves retain the resolved method identity but
			// execute the raw operation directly, avoiding a complete nested CFG.
			if (IsIdentityLocal0Leaf(Function))
			{
				if (Function.ClosureTypeName == kFnI64ToI64)
				{
					return AttachPortable(EJitDirectScalarKind::UnaryI64, EOpcode::Nop);
				}
				if (Function.ClosureTypeName == kFnU64ToU64)
				{
					return AttachPortable(EJitDirectScalarKind::UnaryU64, EOpcode::Nop);
				}
				if (Function.ClosureTypeName == kFnF32ToF32)
				{
					return AttachPortable(EJitDirectScalarKind::UnaryF32, EOpcode::Nop);
				}
			}
			std::int64_t I64Imm = 0;
			std::uint64_t U64Imm = 0;
			float F32Imm = 0.0f;
			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul,
			         EOpcode::Div, EOpcode::Mod, EOpcode::BitAnd, EOpcode::BitOr,
			         EOpcode::BitXor, EOpcode::Shl, EOpcode::Shr,
			         EOpcode::Eq, EOpcode::Ne, EOpcode::Lt, EOpcode::Le,
			         EOpcode::Gt, EOpcode::Ge})
			{
				if (IsLocal0ConstWideBinaryLeaf(
				        Module, Function, Op, kFnI64ToI64, "fn(int64)->bool",
				        EOpcode::ConstI32, ReadConstI64, I64Imm))
				{
					Entry.DirectScalarPayloadU64 = static_cast<std::uint64_t>(I64Imm);
					return AttachPortable(EJitDirectScalarKind::UnaryI64RightImm, Op);
				}
				if (IsLocal0ConstWideBinaryLeaf(
				        Module, Function, Op, kFnU64ToU64, "fn(uint64)->bool",
				        EOpcode::ConstI32, ReadConstU64, U64Imm))
				{
					Entry.DirectScalarPayloadU64 = U64Imm;
					return AttachPortable(EJitDirectScalarKind::UnaryU64RightImm, Op);
				}
			}
			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul,
			         EOpcode::Div, EOpcode::Eq, EOpcode::Ne, EOpcode::Lt,
			         EOpcode::Le, EOpcode::Gt, EOpcode::Ge})
			{
				if (IsLocal0ConstWideBinaryLeaf(
				        Module, Function, Op, kFnF32ToF32, "fn(float)->bool",
				        EOpcode::ConstF64, ReadConstF32, F32Imm))
				{
					std::uint32_t Bits = 0;
					std::memcpy(&Bits, &F32Imm, sizeof(Bits));
					Entry.DirectScalarPayloadU64 = Bits;
					return AttachPortable(EJitDirectScalarKind::UnaryF32RightImm, Op);
				}
			}
			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul,
			         EOpcode::Div, EOpcode::Mod, EOpcode::BitAnd, EOpcode::BitOr,
			         EOpcode::BitXor, EOpcode::Shl, EOpcode::Shr,
			         EOpcode::Eq, EOpcode::Ne, EOpcode::Lt, EOpcode::Le,
			         EOpcode::Gt, EOpcode::Ge})
			{
				const bool bCommutative = Op == EOpcode::Add || Op == EOpcode::Mul || Op == EOpcode::BitAnd || Op == EOpcode::BitOr || Op == EOpcode::BitXor || Op == EOpcode::Eq || Op == EOpcode::Ne;
				const std::string_view I64Closure = Op >= EOpcode::Eq && Op <= EOpcode::Ge
				    ? kFnI64I64ToBool
				    : kFnI64I64ToI64;
				if (IsTypedBinaryLeaf(Function, Op, I64Closure, bCommutative))
				{
					return AttachPortable(EJitDirectScalarKind::BinaryI64, Op);
				}
				const std::string_view U64Closure = Op >= EOpcode::Eq && Op <= EOpcode::Ge
				    ? kFnU64U64ToBool
				    : kFnU64U64ToU64;
				if (IsTypedBinaryLeaf(Function, Op, U64Closure, bCommutative))
				{
					return AttachPortable(EJitDirectScalarKind::BinaryU64, Op);
				}
			}
			for (const EOpcode Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul,
			         EOpcode::Div, EOpcode::Eq, EOpcode::Ne, EOpcode::Lt,
			         EOpcode::Le, EOpcode::Gt, EOpcode::Ge})
			{
				const bool bCommutative = Op == EOpcode::Add || Op == EOpcode::Mul || Op == EOpcode::Eq || Op == EOpcode::Ne;
				const std::string_view Closure = Op >= EOpcode::Eq && Op <= EOpcode::Ge
				    ? kFnF32F32ToBool
				    : kFnF32F32ToF32;
				if (IsTypedBinaryLeaf(Function, Op, Closure, bCommutative))
				{
					return AttachPortable(EJitDirectScalarKind::BinaryF32, Op);
				}
			}
			if (IsTypedUnaryLeaf(Function, EOpcode::Neg, kFnI64ToI64))
			{
				return AttachPortable(EJitDirectScalarKind::UnaryI64, EOpcode::Neg);
			}
			if (IsTypedUnaryLeaf(Function, EOpcode::BitNot, kFnI64ToI64))
			{
				return AttachPortable(EJitDirectScalarKind::UnaryI64, EOpcode::BitNot);
			}
			if (IsTypedUnaryLeaf(Function, EOpcode::Neg, kFnU64ToU64))
			{
				return AttachPortable(EJitDirectScalarKind::UnaryU64, EOpcode::Neg);
			}
			if (IsTypedUnaryLeaf(Function, EOpcode::BitNot, kFnU64ToU64))
			{
				return AttachPortable(EJitDirectScalarKind::UnaryU64, EOpcode::BitNot);
			}
			if (IsTypedUnaryLeaf(Function, EOpcode::Neg, kFnF32ToF32))
			{
				return AttachPortable(EJitDirectScalarKind::UnaryF32, EOpcode::Neg);
			}
			return false;
		}

		bool TryCompileLeaf(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    JitNative::FArena& Arena,
		    FJitFunctionEntry& OutEntry)
		{
			OutEntry = {};
			// Address queries require the VM's actual reference storage. Decline before entry.
			for (std::size_t I = 0; I < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[I]);
				const auto N = Bytecode::InstructionSize(Op);
				if (!N || N > Function.Code.size() - I)
				{
					return false;
				}
				if (Op == EOpcode::NewUninitializedNativeArray || Op == EOpcode::DefaultCopyConstruct || Op == EOpcode::SameObject || Op == EOpcode::AssignHostBase || Op == EOpcode::EqualHostBase || Op == EOpcode::AssignCoreMetadata || Op == EOpcode::CloneValueOfType || Op == EOpcode::InitIndex)
				{
					return false;
				}
				if (Op == EOpcode::CallMemory)
				{
					// Core-relative standard calls (memory and i18n) use VM services.
					// Decline before entry so Mixed/JIT can fall back without replay.
					return false;
				}
				if (Op == EOpcode::CallBuiltin &&
				    Function.Code[I + 1] == static_cast<unsigned>(EBuiltinId::MemoryAddress))
				{
					return false;
				}
				I += N;
			}

			// Ownership opcodes (NewObject / refs / closures) and object signatures must
			// not enter early native/trivial leaves — Linear/Vm trampolines release values.
			if (JitLowering::RequiresVmOwnership(Function) || SignatureHasOwnedValues(Function))
			{
				return JitLinear::TryCompile(Module, Function, Arena, OutEntry);
			}
			if (IsTrivialVoidLeaf(Module, Function))
			{
				OutEntry.Entry = &JitTrampoline_TrivialVoid;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			// Instance calls can use the raw scalar ABI directly from both native and
			// boxed dynamic call sites. Prefer NativeCfg over a narrower leaf bridge so
			// constant/property-like and arithmetic methods share that fast path.
			if (Function.bIsMethod)
			{
				std::uint64_t PackedKinds = 0;
				std::uint8_t ParameterCount = 0;
				EJitScalarValueKind ReturnKind = EJitScalarValueKind::None;
				FJitFunctionEntry MethodCfg;
				if (TryBuildScalarSignatureMetadata(
				        Function, PackedKinds, ParameterCount, ReturnKind) &&
				    JitLinear::TryCompile(Module, Function, Arena, MethodCfg) && MethodCfg.Kind == EJitEntryKind::NativeCfg)
				{
					OutEntry = MethodCfg;
					OutEntry.bRequiresThisObject = RequiresJitThisObject(Function);
					(void)TryAttachDirectScalarMethodLeaf(
					    Module, Function, Arena, OutEntry);
					return true;
				}
			}
			if (IsBinaryI32Leaf(Function, EOpcode::Add, true))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::Add, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_AddI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::Sub, false))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::Sub, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_SubI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::Mul, true))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::Mul, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_MulI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::Div, false))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::Div, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_DivI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::Mod, false))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::Mod, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ModI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::BitAnd, true))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::BitAnd, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_BitAndI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::BitOr, true))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::BitOr, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_BitOrI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::BitXor, true))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::BitXor, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_BitXorI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::Shl, false))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::Shl, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ShlI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBinaryI32Leaf(Function, EOpcode::Shr, false))
			{
				if (JitNative::TryBindBinaryI32(Arena, EOpcode::Shr, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ShrI32I32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Add, kFnUintUintToUint, true))
			{
				if (JitNative::TryBindBinaryU32(Arena, EOpcode::Add, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_AddU32U32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Sub, kFnUintUintToUint, false))
			{
				if (JitNative::TryBindBinaryU32(Arena, EOpcode::Sub, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_SubU32U32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Mul, kFnUintUintToUint, true))
			{
				if (JitNative::TryBindBinaryU32(Arena, EOpcode::Mul, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_MulU32U32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Div, kFnUintUintToUint, false))
			{
				if (JitNative::TryBindBinaryU32(Arena, EOpcode::Div, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_DivU32U32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Mod, kFnUintUintToUint, false))
			{
				if (JitNative::TryBindBinaryU32(Arena, EOpcode::Mod, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ModU32U32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Add, kFnDoubleDoubleToDouble, true))
			{
				if (JitNative::TryBindBinaryF64(Arena, EOpcode::Add, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_AddF64F64;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Sub, kFnDoubleDoubleToDouble, false))
			{
				if (JitNative::TryBindBinaryF64(Arena, EOpcode::Sub, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_SubF64F64;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Mul, kFnDoubleDoubleToDouble, true))
			{
				if (JitNative::TryBindBinaryF64(Arena, EOpcode::Mul, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_MulF64F64;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Div, kFnDoubleDoubleToDouble, false))
			{
				if (JitNative::TryBindBinaryF64(Arena, EOpcode::Div, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_DivF64F64;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsNegI32Leaf(Function))
			{
				if (JitNative::TryBindNegI32(Arena, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_NegI32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBitNotI32Leaf(Function))
			{
				if (JitNative::TryBindBitNotI32(Arena, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_BitNotI32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsNegDoubleLeaf(Function))
			{
				if (JitNative::TryBindNegF64(Arena, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_NegF64;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsNegUintLeaf(Function))
			{
				if (JitNative::TryBindNegU32(Arena, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_NegU32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsBitNotUintLeaf(Function))
			{
				if (JitNative::TryBindBitNotU32(Arena, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_BitNotU32;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			std::int32_t Imm = 0;
			if (IsConstI32ReturnLeaf(Module, Function, Imm))
			{
				if (JitNative::TryBindConstI32(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ConstI32;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			bool bConstBool = false;
			if (IsConstBoolReturnLeaf(Function, bConstBool))
			{
				if (JitNative::TryBindConstBool(Arena, bConstBool, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ConstBool;
				OutEntry.PayloadI32 = bConstBool ? 1 : 0;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			double ConstF64 = 0.0;
			if (IsConstF64ReturnLeaf(Module, Function, ConstF64))
			{
				if (JitNative::TryBindConstF64(Arena, ConstF64, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ConstF64;
				OutEntry.PayloadF64 = ConstF64;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::Add, Imm))
			{
				if (JitNative::TryBindLocal0AddImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_AddLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::Sub, Imm))
			{
				if (JitNative::TryBindLocal0SubImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_SubLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::Mul, Imm) || IsConstLocal0BinaryLeaf(Module, Function, EOpcode::Mul, Imm))
			{
				if (JitNative::TryBindLocal0MulImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_MulLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::Div, Imm))
			{
				if (JitNative::TryBindLocal0DivImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_DivLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::Mod, Imm))
			{
				if (JitNative::TryBindLocal0ModImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ModLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::BitAnd, Imm) || IsConstLocal0BinaryLeaf(Module, Function, EOpcode::BitAnd, Imm))
			{
				if (JitNative::TryBindLocal0BitAndImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_BitAndLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::BitOr, Imm) || IsConstLocal0BinaryLeaf(Module, Function, EOpcode::BitOr, Imm))
			{
				if (JitNative::TryBindLocal0BitOrImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_BitOrLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::BitXor, Imm) || IsConstLocal0BinaryLeaf(Module, Function, EOpcode::BitXor, Imm))
			{
				if (JitNative::TryBindLocal0BitXorImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_BitXorLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::Shl, Imm))
			{
				if (JitNative::TryBindLocal0ShlImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ShlLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsLocal0ConstBinaryLeaf(Module, Function, EOpcode::Shr, Imm))
			{
				if (JitNative::TryBindLocal0ShrImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ShrLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsConstLocal0BinaryLeaf(Module, Function, EOpcode::Add, Imm))
			{
				if (JitNative::TryBindLocal0AddImm(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_AddLocal0Imm;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsConstLocal0BinaryLeaf(Module, Function, EOpcode::Sub, Imm))
			{
				if (JitNative::TryBindImmSubLocal0(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_SubImmLocal0;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsConstLocal0BinaryLeaf(Module, Function, EOpcode::Div, Imm))
			{
				if (JitNative::TryBindImmDivLocal0(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_DivImmLocal0;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsConstLocal0BinaryLeaf(Module, Function, EOpcode::Mod, Imm))
			{
				if (JitNative::TryBindImmModLocal0(Arena, Imm, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_ModImmLocal0;
				OutEntry.PayloadI32 = Imm;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Eq, kFnIntIntToBool, true))
			{
				if (JitNative::TryBindCmpI32(Arena, EOpcode::Eq, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpI32<EOpcode::Eq>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Ne, kFnIntIntToBool, true))
			{
				if (JitNative::TryBindCmpI32(Arena, EOpcode::Ne, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpI32<EOpcode::Ne>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Lt, kFnIntIntToBool, false))
			{
				if (JitNative::TryBindCmpI32(Arena, EOpcode::Lt, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpI32<EOpcode::Lt>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Le, kFnIntIntToBool, false))
			{
				if (JitNative::TryBindCmpI32(Arena, EOpcode::Le, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpI32<EOpcode::Le>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Gt, kFnIntIntToBool, false))
			{
				if (JitNative::TryBindCmpI32(Arena, EOpcode::Gt, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpI32<EOpcode::Gt>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Ge, kFnIntIntToBool, false))
			{
				if (JitNative::TryBindCmpI32(Arena, EOpcode::Ge, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpI32<EOpcode::Ge>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Eq, kFnUintUintToBool, true))
			{
				if (JitNative::TryBindCmpU32(Arena, EOpcode::Eq, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpU32<EOpcode::Eq>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Ne, kFnUintUintToBool, true))
			{
				if (JitNative::TryBindCmpU32(Arena, EOpcode::Ne, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpU32<EOpcode::Ne>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Lt, kFnUintUintToBool, false))
			{
				if (JitNative::TryBindCmpU32(Arena, EOpcode::Lt, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpU32<EOpcode::Lt>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Le, kFnUintUintToBool, false))
			{
				if (JitNative::TryBindCmpU32(Arena, EOpcode::Le, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpU32<EOpcode::Le>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Gt, kFnUintUintToBool, false))
			{
				if (JitNative::TryBindCmpU32(Arena, EOpcode::Gt, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpU32<EOpcode::Gt>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Ge, kFnUintUintToBool, false))
			{
				if (JitNative::TryBindCmpU32(Arena, EOpcode::Ge, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpU32<EOpcode::Ge>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Eq, kFnDoubleDoubleToBool, true))
			{
				if (JitNative::TryBindCmpF64(Arena, EOpcode::Eq, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpF64<EOpcode::Eq>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Ne, kFnDoubleDoubleToBool, true))
			{
				if (JitNative::TryBindCmpF64(Arena, EOpcode::Ne, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpF64<EOpcode::Ne>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Lt, kFnDoubleDoubleToBool, false))
			{
				if (JitNative::TryBindCmpF64(Arena, EOpcode::Lt, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpF64<EOpcode::Lt>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Le, kFnDoubleDoubleToBool, false))
			{
				if (JitNative::TryBindCmpF64(Arena, EOpcode::Le, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpF64<EOpcode::Le>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Gt, kFnDoubleDoubleToBool, false))
			{
				if (JitNative::TryBindCmpF64(Arena, EOpcode::Gt, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpF64<EOpcode::Gt>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsTypedBinaryLeaf(Function, EOpcode::Ge, kFnDoubleDoubleToBool, false))
			{
				if (JitNative::TryBindCmpF64(Arena, EOpcode::Ge, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_CmpF64<EOpcode::Ge>;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsNotBoolLeaf(Function))
			{
				if (JitNative::TryBindNotBool(Arena, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_NotBool;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			if (IsCastF64ToI32Leaf(Function))
			{
				BindCastNativeLeaf(OutEntry, &JitTrampoline_CastF64ToI32);
				return true;
			}
			if (IsCastI32ToF64Leaf(Function))
			{
				BindCastNativeLeaf(OutEntry, &JitTrampoline_CastI32ToF64);
				return true;
			}
			if (IsCastI32ToU32Leaf(Function))
			{
				BindCastNativeLeaf(OutEntry, &JitTrampoline_CastI32ToU32);
				return true;
			}
			if (IsIdentityLocal0Leaf(Function))
			{
				if (JitNative::TryBindIdentity(Arena, Function, OutEntry))
				{
					return true;
				}
				OutEntry.Entry = &JitTrampoline_IdentityLocal0;
				OutEntry.Status = EJitCompileStatus::Ok;
				return true;
			}
			// Jump-free int/bool functions (locals, bitwise, peephole Inc/AddLocal*).
			if (JitLinear::TryCompile(Module, Function, Arena, OutEntry))
			{
				return true;
			}
			return false;
		}
	}

	namespace
	{
// Keep scalar-analysis scratch and its unwind path out of cache hits.
// NativeCfg binders already prove their emitted stream's stack bound;
// other entries still require the original complete scalar analysis.
		#if defined(_MSC_VER)
		__declspec(noinline)
		#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
		#endif
		void PopulateVerifiedStackCapacity(
		    const FBytecodeModule& Module, const FBytecodeFunction& Function,
		    FJitFunctionEntry& Entry)
		{
			if (Entry.Kind == EJitEntryKind::NativeCfg && Entry.StackCapacityHint != 0)
			{
				return;
			}
			JitLowering::FPlan Plan;
			if (JitLowering::BuildScalarPlan(Module, Function, Plan) && Plan.bScalarEligible)
			{
				Entry.StackCapacityHint = std::max(Entry.StackCapacityHint, Plan.MaxStackSlots);
				Entry.BoxedStackCapacityHint = std::max(Entry.BoxedStackCapacityHint, Plan.MaxBoxedStackSlots);
			}
		}
	}

	EJitCompileStatus FJitCodeCache::EnsureCompiled(
	    const FBytecodeModule& Module,
	    const std::uint16_t FunctionIndex)
	{
		FImpl& Cache = *Impl;
		if (FunctionIndex < Module.Functions.size() && Module.Functions[FunctionIndex].bUsesLocalStaticStorage)
		{
			return EJitCompileStatus::Unsupported;
		}
		if (const auto* Entry = GetCandidate(FunctionIndex))
		{
			++Cache.CacheHits;
			return Entry->Status;
		}
		if (Cache.Policy.MaxCachedEntries != 0 && Cache.CachedEntryCount >= Cache.Policy.MaxCachedEntries)
		{
			++Cache.CacheCapacitySkips;
			return EJitCompileStatus::Failed;
		}
		if (Cache.Policy.MaxCompileAttempts != 0 && Cache.CompileAttempts >= Cache.Policy.MaxCompileAttempts)
		{
			++Cache.CompileBudgetSkips;
			return EJitCompileStatus::Failed;
		}
		++Cache.CompileAttempts;

		if (FunctionIndex >= Cache.Entries.size())
		{
			Cache.Entries.resize(static_cast<std::size_t>(FunctionIndex) + 1u);
		}
		auto Slot = std::make_unique<FJitFunctionEntry>();
		auto& Entry = *Slot;
		if (FunctionIndex < Module.Functions.size() && TryCompileLeaf(Module, Module.Functions[FunctionIndex], *Cache.Arena, Entry))
		{
			PopulateVerifiedStackCapacity(Module, Module.Functions[FunctionIndex], Entry);
			PopulateScalarSignatureMetadata(Module.Functions[FunctionIndex], Entry);
			JitLinear::FReadonlyTextBuiltinForwarder Forwarder;
			if (JitLinear::DescribeReadonlyTextBuiltinForwarder(Module.Functions[FunctionIndex], Forwarder))
			{
				Entry.ReadonlyTextBuiltin = static_cast<std::uint8_t>(Forwarder.Builtin);
				Entry.ReadonlyTextBuiltinOffset = Forwarder.InstructionOffset;
			}
			if (Entry.Kind != EJitEntryKind::NativeLeaf && Entry.DirectScalarKind == EJitDirectScalarKind::None &&
			    !Entry.bDirectThisFieldAddModuloI32 && ShouldPreferVmForShortMethod(Module.Functions[FunctionIndex]))
			{
				Entry.bPreferVmForExecution = true;
			}

			// Rewritten native CFG kernels return packed exact offsets into their own
			// stream.  Do not manufacture a legacy status-only fallback offset from
			// the original function: the two instruction spaces are unrelated.
			if (Entry.RuntimeDebugLines.empty())
			{
				PopulateRuntimeErrorInstructionOffsets(Module.Functions[FunctionIndex], Entry);
			}
			Entry.bRequiresThisObject =
			    RequiresJitThisObject(Module.Functions[FunctionIndex]);
			Entry.bMutatesScriptFields =
			    MutatesScriptFields(Module.Functions[FunctionIndex]);
			// Older leaf bridges initialize only Status/bNativeLeaf. Keep their
			// classification explicit for statistics without changing dispatch.
			if (Entry.Kind == EJitEntryKind::Unsupported)
			{
				Entry.Kind = Entry.bNativeLeaf
				    ? EJitEntryKind::NativeLeaf
				    : EJitEntryKind::BridgeTrampoline;
			}
			if (Entry.bNativeLeaf)
			{
				++Cache.NativeEntries;
			}
			switch (Entry.Kind)
			{
			case EJitEntryKind::NativeLeaf:
				{
					++Cache.NativeLeafEntries;
					break;
				}
			case EJitEntryKind::NativeCfg:
				{
					++Cache.NativeCfgEntries;
					break;
				}
			case EJitEntryKind::BridgeTrampoline:
				{
					++Cache.BridgeTrampolineEntries;
					break;
				}
			case EJitEntryKind::LinearTrampoline:
				{
					++Cache.LinearTrampolineEntries;
					break;
				}
			case EJitEntryKind::VmTrampoline:
				{
					++Cache.VmTrampolineEntries;
					break;
				}
			case EJitEntryKind::Unsupported:
				{
					break;
				}
			default:
				{
					break;
				}
			}
			const std::uint16_t CompiledInlineSites = Entry.InlinedCallSites;
			Cache.StoreEntry(FunctionIndex, std::move(Slot));
			++Cache.CachedEntryCount;
			Cache.InlinedCallSites += CompiledInlineSites;
			++Cache.CompiledEntries;
			return EJitCompileStatus::Ok;
		}

		Entry.Status = EJitCompileStatus::Unsupported;
		Entry.Kind = EJitEntryKind::Unsupported;
		Entry.Entry = nullptr;
		Cache.StoreEntry(FunctionIndex, std::move(Slot));
		++Cache.CachedEntryCount;
		++Cache.UnsupportedEntries;
		return EJitCompileStatus::Unsupported;
	}

}
