// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitNative.h"
#include "VeroScriptJitRawScratch.h"
#include "VeroScriptJitNativeLinearHelpers.h"
#include "VeroScriptAsmJitCapabilities.h"
#include "VeroScriptJitLinear.h"
#include "VeroScriptJitText.h"
#include "VeroScriptJitLowering.h"

#include "VeroScriptMethodKey.h"
#include "VeroScriptAsmJitExecutionContext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <queue>
#include <string_view>
#include <vector>

#if VEROSCRIPT_HAS_ASMJIT
#if VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_X86
#include <asmjit/x86.h>
#elif VEROSCRIPT_ASMJIT_A64
#include <asmjit/a64.h>
#endif
#endif

namespace VeroScript::AsmJit::JitNative
{
	namespace
	{
		// Raw kernels retain their compact integer ABI, while the public enum keeps
		// all VM/JIT reporting sites synchronized with that protocol.
		constexpr std::int32_t ToKernelStatus(const EJitRuntimeStatus Status)
		{
			return static_cast<std::int32_t>(Status);
		}
		constexpr std::int32_t kStatusOk = ToKernelStatus(EJitRuntimeStatus::Ok);
		#if VEROSCRIPT_HAS_ASMJIT
		constexpr std::int32_t kStatusOverflow = ToKernelStatus(EJitRuntimeStatus::IntegerOverflow);
		constexpr std::int32_t kStatusDivByZero = ToKernelStatus(EJitRuntimeStatus::DivisionByZero);
		constexpr std::int32_t kStatusShiftRange = ToKernelStatus(EJitRuntimeStatus::ShiftOutOfRange);
		constexpr std::int32_t kStatusGenericFailure = ToKernelStatus(EJitRuntimeStatus::GenericFailure);
		constexpr std::int32_t kStatusToUnsignedOverflow = ToKernelStatus(EJitRuntimeStatus::ToUnsignedOverflow);
		constexpr std::int32_t kStatusToIntOverflow = ToKernelStatus(EJitRuntimeStatus::ToIntOverflow);
		#endif

		#if VEROSCRIPT_HAS_ASMJIT
		// Native CFG locals use the VM-provided local array, not a generated native
		// stack frame. Keep analysis at the same bounded resource limits as the
		// validated Linear CFG instead of imposing the former artificial
		// 128-local/4096-byte deopt cliff.
		constexpr std::size_t kMaxNativeCfgLocals =
			std::numeric_limits<std::uint16_t>::max();
		constexpr std::size_t kMaxNativeCfgBytecodeBytes = 64u * 1024u;
		constexpr std::size_t kMaxNativeCfgStackSlots =
			std::numeric_limits<std::uint16_t>::max();

		using namespace asmjit;

		bool FinalizeKernel(JitRuntime& Runtime, CodeHolder& Code, void** OutFn)
		{
			const Error Err = Runtime.add(OutFn, &Code);
			return Err == kErrorOk && OutFn && *OutFn;
		}

		bool InitCode(JitRuntime& Runtime, CodeHolder& Code)
		{
			return Code.init(Runtime.environment(), Runtime.cpu_features()) == kErrorOk;
		}

		bool IsVoidReturnSentinelI32(
			const FBytecodeModule& Module,
			const FBytecodeFunction& Function,
			const std::size_t Offset)
		{
			const auto& Code = Function.Code;
			if (!Function.bReturnsVoid || Offset + 4 > Code.size()
				|| static_cast<EOpcode>(Code[Offset]) != EOpcode::ConstI32
				|| static_cast<EOpcode>(Code[Offset + 3]) != EOpcode::Return)
			{
				return false;
			}
			const std::uint16_t ConstantIndex = Bytecode::ReadU16LE(Code.data() + Offset + 1);
			return ConstantIndex < Module.Constants.size()
				&& Module.Constants[ConstantIndex].Kind == EConstantKind::I32
				&& Module.Constants[ConstantIndex].I32 == 0;
		}

		struct FExternalDirectF64Callee
		{
			bool bEligible = false;
			std::uint16_t LocalCount = 0;
			std::uint16_t StackSlots = 0;
		};

		bool AnalyzeExternalDirectF64Callee(
			const FBytecodeModule& Module,
			const std::uint16_t FunctionIndex,
			const std::uint16_t CalleeIndex,
			FExternalDirectF64Callee& OutCallee)
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
			JitLowering::FPlan Plan;
			if (!JitLowering::BuildScalarPlan(Module, Callee, Plan)
				|| !Plan.bScalarEligible || Plan.bHasBoxedValues
				|| Plan.LocalCount != Callee.LocalCount
				|| Plan.MaxStackSlots > 64u
				|| Plan.Signature.Parameters.size() != Callee.ParamCount
				|| !std::all_of(
					Plan.Signature.Parameters.begin(),
					Plan.Signature.Parameters.end(),
					[](const JitIr::EValueType Type)
					{
						return Type == JitIr::EValueType::F64;
					})
				|| (Plan.Signature.ReturnType != JitIr::EValueType::F64
					&& Plan.Signature.ReturnType != JitIr::EValueType::Void))
			{
				return false;
			}
			// Keep direct entry independent of bridge-owned pending-call state.
			// Arithmetic, locals and control flow are fully emitted on both x64 and
			// AArch64; calls, globals, objects and helper-backed builtins are not.
			for (std::size_t Ip = 0; Ip < Callee.Code.size();)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Callee.Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if (Size == 0u || Size > Callee.Code.size() - Ip)
				{
					return false;
				}
				if (Opcode == EOpcode::ConstI32
					&& !IsVoidReturnSentinelI32(Module, Callee, Ip))
				{
					return false;
				}
				switch (Opcode)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::AddLocalLocal:
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Eq:
				case EOpcode::Ne:
				case EOpcode::Lt:
				case EOpcode::Le:
				case EOpcode::Gt:
				case EOpcode::Ge:
				case EOpcode::Neg:
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
				case EOpcode::JmpIfLocalGeImm:
				case EOpcode::Dup:
				case EOpcode::Return:
				case EOpcode::Pop:
					{
						break;
					}
				default:
					return false;
				}
				Ip += Size;
			}
			OutCallee.bEligible = true;
			OutCallee.LocalCount = Callee.LocalCount;
			OutCallee.StackSlots = std::max<std::uint16_t>(1u, Plan.MaxStackSlots);
			return true;
		}

		enum class EBitOp : std::uint8_t
		{
			And,
			Or,
			Xor,
		};

		#if VEROSCRIPT_ASMJIT_X64
		FBinaryI32Kernel EmitAdd(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			Label Overflow = A.new_label();
			A.mov(x86::eax, x86::ecx);
			A.add(x86::eax, x86::edx);
			A.jo(Overflow);
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FBinaryI32Kernel EmitSub(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			Label Overflow = A.new_label();
			A.mov(x86::eax, x86::ecx);
			A.sub(x86::eax, x86::edx);
			A.jo(Overflow);
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FBinaryI32Kernel EmitMul(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			Label Overflow = A.new_label();
			A.mov(x86::eax, x86::ecx);
			A.imul(x86::eax, x86::edx);
			A.jo(Overflow);
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FBinaryI32Kernel EmitDivOrMod(JitRuntime& Runtime, const bool bDiv)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);

			Label DivByZero = A.new_label();
			Label Overflow = A.new_label();
			Label DoDiv = A.new_label();

			A.test(x86::edx, x86::edx);
			A.jz(DivByZero);
			A.cmp(x86::ecx, static_cast<std::int32_t>(0x80000000u));
			A.jne(DoDiv);
			A.cmp(x86::edx, -1);
			A.je(Overflow);
			A.bind(DoDiv);
			A.mov(x86::r9d, x86::edx);
			A.mov(x86::eax, x86::ecx);
			A.cdq();
			A.idiv(x86::r9d);
			if (bDiv)
			{
				A.mov(x86::dword_ptr(x86::r8), x86::eax);
			}
			else
			{
				A.mov(x86::dword_ptr(x86::r8), x86::edx);
			}
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(DivByZero);
			A.mov(x86::eax, kStatusDivByZero);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();

			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FBinaryI32Kernel EmitBit(JitRuntime& Runtime, const EBitOp Op)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.mov(x86::eax, x86::ecx);
			switch (Op)
			{
			case EBitOp::And:
				{
					A.and_(x86::eax, x86::edx);
					break;
				}
			case EBitOp::Or:
				{
					A.or_(x86::eax, x86::edx);
					break;
				}
			case EBitOp::Xor:
				{
					A.xor_(x86::eax, x86::edx);
					break;
				}
			default:
				{
					break;
				}
			}
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FBinaryI32Kernel EmitShift(JitRuntime& Runtime, const bool bShl)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			Label Bad = A.new_label();
			A.cmp(x86::edx, 0);
			A.jl(Bad);
			A.cmp(x86::edx, 31);
			A.jg(Bad);
			A.mov(x86::eax, x86::ecx);
			A.mov(x86::ecx, x86::edx);
			if (bShl)
			{
				A.shl(x86::eax, x86::cl);
			}
			else
			{
				A.sar(x86::eax, x86::cl);
			}
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Bad);
			A.mov(x86::eax, kStatusShiftRange);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FBinaryI32Kernel EmitCmp(JitRuntime& Runtime, const EOpcode Op)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.cmp(x86::ecx, x86::edx);
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
				return nullptr;
			}
			A.movzx(x86::eax, x86::al);
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FUnaryI32Kernel EmitNeg(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			Label Overflow = A.new_label();
			A.mov(x86::eax, x86::ecx);
			A.neg(x86::eax);
			A.jo(Overflow);
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
		}

		FUnaryI32Kernel EmitBitNot(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.mov(x86::eax, x86::ecx);
			A.not_(x86::eax);
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
		}

		FUnaryI32ImmKernel EmitUnaryAddSubImm(
			JitRuntime& Runtime,
			const std::int32_t Imm,
			const bool bAdd)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);

			Label Overflow = A.new_label();
			A.mov(x86::eax, x86::ecx);
			if (bAdd)
			{
				A.add(x86::eax, Imm);
			}
			else
			{
				A.sub(x86::eax, Imm);
			}
			A.jo(Overflow);
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();

			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
		}

		FUnaryI32ImmKernel EmitMulImm(JitRuntime& Runtime, const std::int32_t Imm)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			Label Overflow = A.new_label();
			A.mov(x86::eax, x86::ecx);
			A.imul(x86::eax, Imm);
			A.jo(Overflow);
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
		}

		FUnaryI32ImmKernel EmitBitImm(JitRuntime& Runtime, const std::int32_t Imm, const EBitOp Op)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.mov(x86::eax, x86::ecx);
			switch (Op)
			{
			case EBitOp::And:
				{
					A.and_(x86::eax, Imm);
					break;
				}
			case EBitOp::Or:
				{
					A.or_(x86::eax, Imm);
					break;
				}
			case EBitOp::Xor:
				{
					A.xor_(x86::eax, Imm);
					break;
				}
			default:
				{
					break;
				}
			}
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
		}

		FUnaryI32ImmKernel EmitShiftImm(JitRuntime& Runtime, const std::int32_t Imm, const bool bShl)
		{
			if (Imm < 0 || Imm > 31)
			{
				return nullptr;
			}
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.mov(x86::eax, x86::ecx);
			if (bShl)
			{
				A.shl(x86::eax, Imm);
			}
			else
			{
				A.sar(x86::eax, Imm);
			}
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
		}

		FUnaryI32ImmKernel EmitImmMinusValue(JitRuntime& Runtime, const std::int32_t Imm)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);

			Label Overflow = A.new_label();
			A.mov(x86::eax, Imm);
			A.sub(x86::eax, x86::ecx);
			A.jo(Overflow);
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();

			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
		}

		FUnaryI32ImmKernel EmitAlwaysStatus(JitRuntime& Runtime, const std::int32_t Status)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.mov(x86::eax, Status);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
		}

		FUnaryI32ImmKernel EmitDivOrModImm(
			JitRuntime& Runtime,
			const std::int32_t Imm,
			const bool bDiv)
		{
			if (Imm == 0)
			{
				return EmitAlwaysStatus(Runtime, kStatusDivByZero);
			}
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			Label Overflow = A.new_label();
			// Win64 unary: Value=ecx, Out=rdx. Save Out in r8 before cdq clobbers edx.
			A.mov(x86::r8, x86::rdx);
			if (Imm == -1)
			{
				A.cmp(x86::ecx, static_cast<std::int32_t>(0x80000000u));
				A.je(Overflow);
				if (bDiv)
				{
					A.mov(x86::eax, x86::ecx);
					A.neg(x86::eax);
				}
				else
				{
					A.xor_(x86::eax, x86::eax);
				}
			}
			else
			{
				A.mov(x86::eax, x86::ecx);
				A.cdq();
				A.mov(x86::r9d, Imm);
				A.idiv(x86::r9d);
				if (!bDiv)
				{
					A.mov(x86::eax, x86::edx);
				}
			}
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
		}

		FUnaryI32ImmKernel EmitDivOrModFromImm(
			JitRuntime& Runtime,
			const std::int32_t Imm,
			const bool bDiv)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			Label DivByZero = A.new_label();
			Label Overflow = A.new_label();
			A.mov(x86::r8, x86::rdx);
			A.test(x86::ecx, x86::ecx);
			A.jz(DivByZero);
			if (Imm == static_cast<std::int32_t>(0x80000000u))
			{
				A.cmp(x86::ecx, -1);
				A.je(Overflow);
			}
			A.mov(x86::eax, Imm);
			A.cdq();
			A.idiv(x86::ecx);
			if (!bDiv)
			{
				A.mov(x86::eax, x86::edx);
			}
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			A.bind(DivByZero);
			A.mov(x86::eax, kStatusDivByZero);
			A.ret();
			A.bind(Overflow);
			A.mov(x86::eax, kStatusOverflow);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
		}

		FBinaryI32Kernel EmitU32Arith(JitRuntime& Runtime, const EOpcode Op)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			if (Op == EOpcode::Add || Op == EOpcode::Sub || Op == EOpcode::Mul)
			{
				A.mov(x86::eax, x86::ecx);
				if (Op == EOpcode::Add)
				{
					A.add(x86::eax, x86::edx);
				}
				else if (Op == EOpcode::Sub)
				{
					A.sub(x86::eax, x86::edx);
				}
				else
				{
					A.imul(x86::eax, x86::edx);
				}
				A.mov(x86::dword_ptr(x86::r8), x86::eax);
				A.xor_(x86::eax, x86::eax);
				A.ret();
			}
			else if (Op == EOpcode::Div || Op == EOpcode::Mod)
			{
				Label DivByZero = A.new_label();
				A.test(x86::edx, x86::edx);
				A.jz(DivByZero);
				A.mov(x86::r9d, x86::edx);
				A.mov(x86::eax, x86::ecx);
				A.xor_(x86::edx, x86::edx);
				A.div(x86::r9d);
				if (Op == EOpcode::Div)
				{
					A.mov(x86::dword_ptr(x86::r8), x86::eax);
				}
				else
				{
					A.mov(x86::dword_ptr(x86::r8), x86::edx);
				}
				A.xor_(x86::eax, x86::eax);
				A.ret();
				A.bind(DivByZero);
				A.mov(x86::eax, kStatusDivByZero);
				A.ret();
			}
			else
			{
				return nullptr;
			}
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FBinaryI32Kernel EmitCmpU32Op(JitRuntime& Runtime, const EOpcode Op)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.cmp(x86::ecx, x86::edx);
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
					A.setb(x86::al);
					break;
				}
			case EOpcode::Le:
				{
					A.setbe(x86::al);
					break;
				}
			case EOpcode::Gt:
				{
					A.seta(x86::al);
					break;
				}
			case EOpcode::Ge:
				{
					A.setae(x86::al);
					break;
				}
			default:
				return nullptr;
			}
			A.movzx(x86::eax, x86::al);
			A.mov(x86::dword_ptr(x86::r8), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
		}

		FUnaryI32Kernel EmitNegU32Kernel(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.mov(x86::eax, x86::ecx);
			A.neg(x86::eax);
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
		}

		FUnaryI32Kernel EmitNotBoolKernel(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.mov(x86::eax, x86::ecx);
			A.xor_(x86::eax, 1);
			A.and_(x86::eax, 1);
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
		}

		FUnaryI32Kernel EmitIdentityI32Kernel(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.mov(x86::dword_ptr(x86::rdx), x86::ecx);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
		}

		FUnaryF64Kernel EmitIdentityF64Kernel(JitRuntime& Runtime)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			A.movsd(x86::ptr(x86::rdx), x86::xmm0);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryF64Kernel>(Fn) : nullptr;
		}

		struct FSignedDivMagic32
		{
			std::int32_t Multiplier = 0;
			std::uint8_t Shift = 0;
		};

		// Hacker's Delight signed-division magic. Divisors 0, 1 and -1 are
		// handled explicitly by each emitter and never reach this routine.
		FSignedDivMagic32 ComputeSignedDivMagic32(const std::int32_t Divisor)
		{
			const std::uint64_t AbsoluteDivisor = Divisor < 0
				? static_cast<std::uint64_t>(-static_cast<std::int64_t>(Divisor))
				: static_cast<std::uint64_t>(Divisor);
			constexpr std::uint64_t Two31 = std::uint64_t{1} << 31u;
			const std::uint64_t Sign = static_cast<std::uint32_t>(Divisor) >> 31u;
			const std::uint64_t T = Two31 + Sign;
			const std::uint64_t Anc = T - 1u - T % AbsoluteDivisor;
			std::uint32_t P = 31u;
			std::uint64_t Q1 = Two31 / Anc;
			std::uint64_t R1 = Two31 - Q1 * Anc;
			std::uint64_t Q2 = Two31 / AbsoluteDivisor;
			std::uint64_t R2 = Two31 - Q2 * AbsoluteDivisor;
			std::uint64_t Delta = 0;
			do
			{
				++P;
				Q1 *= 2u;
				R1 *= 2u;
				if (R1 >= Anc)
				{
					++Q1;
					R1 -= Anc;
				}
				Q2 *= 2u;
				R2 *= 2u;
				if (R2 >= AbsoluteDivisor)
				{
					++Q2;
					R2 -= AbsoluteDivisor;
				}
				Delta = AbsoluteDivisor - R2;
			}
			while (Q1 < Delta || (Q1 == Delta && R1 == 0u));
			std::int32_t Multiplier = static_cast<std::int32_t>(
				static_cast<std::uint32_t>(Q2 + 1u));
			if (Divisor < 0)
			{
				Multiplier = static_cast<std::int32_t>(
					0u - static_cast<std::uint32_t>(Multiplier));
			}
			return {Multiplier, static_cast<std::uint8_t>(P - 32u)};
		}

		template <typename FFailure>
		void EmitSignedDivOrModImmediate32(x86::Assembler& A, const EOpcode Op,
			const std::int32_t Divisor, const FFailure& Failure)
		{
			// The existing Mixed CFG sequence also serves typed-wide I32 arithmetic.
			// Preserve r8/r9 so a failing typed operation retains its original inputs.
			const bool bMod = Op == EOpcode::Mod;
			if (Divisor == 0)
			{
				A.jmp(Failure(EJitRuntimeStatus::DivisionByZero));
				return;
			}
			if (Divisor == 1)
			{
				if (bMod)
				{
					A.xor_(x86::eax, x86::eax);
				}
				else
				{
					A.mov(x86::eax, x86::r8d);
				}
				return;
			}
			if (Divisor == -1)
			{
				A.cmp(x86::r8d, static_cast<std::int32_t>(0x80000000u));
				A.je(Failure(EJitRuntimeStatus::IntegerOverflow));
				if (bMod)
				{
					A.xor_(x86::eax, x86::eax);
				}
				else
				{
					A.mov(x86::eax, x86::r8d);
					A.neg(x86::eax);
				}
				return;
			}
			const FSignedDivMagic32 Magic = ComputeSignedDivMagic32(Divisor);
			A.mov(x86::eax, x86::r8d);
			A.mov(x86::ecx, Magic.Multiplier);
			A.imul(x86::ecx); // edx:eax = signed eax * ecx
			if (Divisor > 0 && Magic.Multiplier < 0)
			{
				A.add(x86::edx, x86::r8d);
			}
			else if (Divisor < 0 && Magic.Multiplier > 0)
			{
				A.sub(x86::edx, x86::r8d);
			}
			if (Magic.Shift != 0u)
			{
				A.sar(x86::edx, Magic.Shift);
			}
			A.mov(x86::eax, x86::edx);
			A.shr(x86::eax, 31);
			A.add(x86::edx, x86::eax);
			if (!bMod)
			{
				A.mov(x86::eax, x86::edx);
				return;
			}
			A.imul(x86::edx, x86::edx, Divisor);
			A.mov(x86::eax, x86::r8d);
			A.sub(x86::eax, x86::edx);
		}

		struct FSignedDivMagic64
		{
			std::int64_t Multiplier = 0;
			std::uint8_t Shift = 0;
		};

		FSignedDivMagic64 ComputeSignedDivMagic64(const std::int64_t Divisor)
		{
			// The signed-division recurrence needs only unsigned 64-bit arithmetic.
			// In particular, neither abs(INT64_MIN) nor negating a multiplier uses
			// signed overflow, and the host compiler needs no 128-bit extension.
			const auto Bits = static_cast<std::uint64_t>(Divisor);
			const std::uint64_t AbsoluteDivisor = Divisor < 0 ? 0u - Bits : Bits;
			constexpr std::uint64_t Two63 = std::uint64_t{1} << 63u;
			const std::uint64_t T = Two63 + (Bits >> 63u);
			const std::uint64_t Anc = T - 1u - T % AbsoluteDivisor;
			std::uint32_t P = 63u;
			std::uint64_t Q1 = Two63 / Anc;
			std::uint64_t R1 = Two63 - Q1 * Anc;
			std::uint64_t Q2 = Two63 / AbsoluteDivisor;
			std::uint64_t R2 = Two63 - Q2 * AbsoluteDivisor;
			std::uint64_t Delta = 0;
			do
			{
				++P;
				Q1 *= 2u;
				R1 *= 2u;
				if (R1 >= Anc)
				{
					++Q1;
					R1 -= Anc;
				}
				Q2 *= 2u;
				R2 *= 2u;
				if (R2 >= AbsoluteDivisor)
				{
					++Q2;
					R2 -= AbsoluteDivisor;
				}
				Delta = AbsoluteDivisor - R2;
			}
			while (Q1 < Delta || (Q1 == Delta && R1 == 0u));
			const std::uint64_t MultiplierBits = Divisor < 0 ? 0u - (Q2 + 1u) : Q2 + 1u;
			std::int64_t Multiplier = 0;
			std::memcpy(&Multiplier, &MultiplierBits, sizeof(Multiplier));
			return {Multiplier, static_cast<std::uint8_t>(P - 64u)};
		}

		template <typename FFailure>
		void EmitSignedDivOrModImmediate64(x86::Assembler& A, const EOpcode Op,
			const std::int64_t Divisor, const FFailure& Failure)
		{
			// r8 retains the numerator for remainder reconstruction and the typed
			// loop's authoritative failure helper. Only rax/rdx/r10 are clobbered.
			const bool bMod = Op == EOpcode::Mod;
			if (Divisor == 0)
			{
				A.jmp(Failure(EJitRuntimeStatus::DivisionByZero));
				return;
			}
			if (Divisor == 1 || Divisor == -1)
			{
				if (Divisor == -1)
				{
					A.mov(x86::r10, std::uint64_t{1} << 63u);
					A.cmp(x86::r8, x86::r10);
					A.je(Failure(EJitRuntimeStatus::IntegerOverflow));
				}
				if (bMod)
				{
					A.xor_(x86::eax, x86::eax);
				}
				else
				{
					A.mov(x86::rax, x86::r8);
					if (Divisor == -1)
					{
						A.neg(x86::rax);
					}
				}
				return;
			}
			if (Divisor == std::numeric_limits<std::int64_t>::min())
			{
				A.mov(x86::r10, std::uint64_t{1} << 63u);
				if (bMod)
				{
					A.xor_(x86::eax, x86::eax);
					A.cmp(x86::r8, x86::r10);
					A.cmovne(x86::rax, x86::r8);
				}
				else
				{
					A.cmp(x86::r8, x86::r10);
					A.sete(x86::al);
					A.movzx(x86::eax, x86::al);
				}
				return;
			}
			const FSignedDivMagic64 Magic = ComputeSignedDivMagic64(Divisor);
			A.mov(x86::rax, x86::r8);
			A.mov(x86::r10, Magic.Multiplier);
			A.imul(x86::r10);
			if (Divisor > 0 && Magic.Multiplier < 0)
			{
				A.add(x86::rdx, x86::r8);
			}
			else if (Divisor < 0 && Magic.Multiplier > 0)
			{
				A.sub(x86::rdx, x86::r8);
			}
			if (Magic.Shift != 0u)
			{
				A.sar(x86::rdx, Magic.Shift);
			}
			A.mov(x86::rax, x86::rdx);
			A.shr(x86::rax, 63u);
			A.add(x86::rdx, x86::rax);
			if (!bMod)
			{
				A.mov(x86::rax, x86::rdx);
			}
			else
			{
				A.mov(x86::r10, Divisor);
				A.imul(x86::rdx, x86::r10);
				A.mov(x86::rax, x86::r8);
				A.sub(x86::rax, x86::rdx);
			}
		}

		#include "VeroScriptJitNativeEmitF64.inl"
		#include "VeroScriptJitNativeEmitLinear.inl"
		#include "VeroScriptJitNativeEmitLinearF64.inl"
		#include "VeroScriptJitNativeEmitLinearMixed.inl"

		FPureI32ExpressionKernel EmitPureI32ExpressionKernel(
			JitRuntime& Runtime,
			const std::vector<JitLinear::FTypedLoopInstruction>& Instructions,
			const bool bFieldMethod = false, const std::uint8_t FieldArgumentCount = 1)
		{
			using JitLinear::FTypedLoopInstruction;
			if (Instructions.empty() || (bFieldMethod && FieldArgumentCount > 8u))
			{
				return nullptr;
			}
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			struct FFailure
			{
				Label Target;
				EJitRuntimeStatus Status;
				std::uint32_t Offset;
			};
			std::vector<FFailure> Failures;
			const auto NewFailure = [&](const EJitRuntimeStatus Status,
				const std::uint32_t Offset)
			{
				const Label Target = A.new_label();
				Failures.push_back({Target, Status, Offset});
				return Target;
			};

			constexpr std::int32_t OutPointerOffset = 0;
			constexpr std::int32_t FailurePointerOffset = 8;
			constexpr std::int32_t FieldStateOffset = 16;
			const std::int32_t Argument0Offset = bFieldMethod ? 24 : 16;
			const std::int32_t Argument1Offset = bFieldMethod ? 28 : 20;
			const std::int32_t StackOffset = bFieldMethod ? 96 : 32;
			const std::int32_t FrameBytes = StackOffset + 32 * 4;
			A.sub(x86::rsp, FrameBytes);
			// VEROSCRIPT_JIT_CALL uses the Microsoft x64 ABI on every x64 host.
			if (bFieldMethod && FieldArgumentCount > 1u)
			{
				for (std::uint8_t Slot = 0; Slot < FieldArgumentCount; ++Slot)
				{
					A.mov(x86::eax, x86::dword_ptr(x86::rcx, Slot * 8u));
					A.mov(x86::dword_ptr(x86::rsp, Argument0Offset + Slot * 4u), x86::eax);
				}
			}
			else
			{
				A.mov(x86::dword_ptr(x86::rsp, Argument0Offset), x86::ecx);
			}
			if (bFieldMethod)
			{
				A.mov(x86::qword_ptr(x86::rsp, FieldStateOffset), x86::rdx);
			}
			else
			{
				A.mov(x86::dword_ptr(x86::rsp, Argument1Offset), x86::edx);
			}
			A.mov(x86::qword_ptr(x86::rsp, OutPointerOffset), x86::r8);
			A.mov(x86::qword_ptr(x86::rsp, FailurePointerOffset), x86::r9);

			std::size_t Depth = 0;
			for (std::size_t Index = 0; Index < Instructions.size(); ++Index)
			{
				const auto& Instruction = Instructions[Index];
				const auto StackSlot = [&](const std::size_t Index)
				{
					return x86::dword_ptr(x86::rsp,
						StackOffset + static_cast<std::int32_t>(Index * 4u));
				};
				switch (Instruction.Opcode)
				{
				case EOpcode::LoadLocal:
					{
						if (Instruction.Operand0 >= (bFieldMethod ? 16u : 2u) || Depth >= 32u)
						{
							return nullptr;
						}
						A.mov(x86::eax, x86::dword_ptr(x86::rsp,
							Argument0Offset + static_cast<std::int32_t>(Instruction.Operand0 * 4u)));
						A.mov(StackSlot(Depth++), x86::eax);
						break;
					}
				case EOpcode::ConstI32:
					{
						if (Depth >= 32u)
						{
							return nullptr;
						}
						A.mov(StackSlot(Depth++), Instruction.Immediate);
						break;
					}
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
					{
						if (!bFieldMethod || (Instruction.Opcode == EOpcode::LoadThisField ? Depth >= 32u : Depth == 0u))
						{
							return nullptr;
						}
						A.mov(x86::r10, x86::qword_ptr(x86::rsp, FieldStateOffset));
						A.mov(x86::r11, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(JitLinear::FTypedLoopNativeMethodBinding::FFieldState, Fields))));
						A.mov(x86::r11, x86::qword_ptr(x86::r11, Instruction.Operand0 * sizeof(void*)));
						if (Instruction.Opcode == EOpcode::LoadThisField)
						{
							A.mov(x86::eax, x86::dword_ptr(x86::r11));
							A.mov(StackSlot(Depth++), x86::eax);
						}
						else
						{
							A.mov(x86::eax, StackSlot(--Depth));
							A.mov(x86::dword_ptr(x86::r11), x86::eax);
							A.inc(x86::qword_ptr(x86::r10,
								static_cast<std::int32_t>(offsetof(JitLinear::FTypedLoopNativeMethodBinding::FFieldState, MutationCount))));
						}
						break;
					}
				case EOpcode::Dup:
					{
						if (Depth == 0u || Depth >= 32u) return nullptr;
						A.mov(x86::eax, StackSlot(Depth - 1u));
						A.mov(StackSlot(Depth++), x86::eax);
						break;
					}
				case EOpcode::StoreLocal:
					{
						if (!bFieldMethod || Instruction.Operand0 >= 16u || Depth == 0u) return nullptr;
						A.mov(x86::eax, StackSlot(--Depth));
						A.mov(x86::dword_ptr(x86::rsp,
							Argument0Offset + static_cast<std::int32_t>(Instruction.Operand0 * 4u)), x86::eax);
						break;
					}
				case EOpcode::Pop:
					{
						if (Depth == 0u) return nullptr;
						--Depth;
						break;
					}
				case EOpcode::Neg:
				case EOpcode::BitNot:
					{
						if (Depth == 0u) return nullptr;
						A.mov(x86::eax, StackSlot(Depth - 1u));
						if (Instruction.Opcode == EOpcode::Neg)
						{
							A.neg(x86::eax);
							A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow, Instruction.BytecodeOffset));
						}
						else
						{
							A.not_(x86::eax);
						}
						A.mov(StackSlot(Depth - 1u), x86::eax);
						break;
					}
				case EOpcode::Shl:
				case EOpcode::Shr:
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
					{
						if (Depth < 2u)
						{
							return nullptr;
						}
						A.mov(x86::eax, StackSlot(Depth - 2u));
						A.mov(x86::ecx, StackSlot(Depth - 1u));
						if (Instruction.Opcode == EOpcode::Add)
						{
							A.add(x86::eax, x86::ecx);
							A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
						}
						else if (Instruction.Opcode == EOpcode::Sub)
						{
							A.sub(x86::eax, x86::ecx);
							A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
						}
						else if (Instruction.Opcode == EOpcode::Mul)
						{
							A.imul(x86::eax, x86::ecx);
							A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
						}
						else if (Instruction.Opcode == EOpcode::Shl || Instruction.Opcode == EOpcode::Shr)
						{
							A.cmp(x86::ecx, 31u);
							A.ja(NewFailure(EJitRuntimeStatus::ShiftOutOfRange, Instruction.BytecodeOffset));
							if (Instruction.Opcode == EOpcode::Shl)
							{
								A.shl(x86::eax, x86::cl);
							}
							else
							{
								A.sar(x86::eax, x86::cl);
							}
						}
						else if ((Instruction.Opcode == EOpcode::Div || Instruction.Opcode == EOpcode::Mod) &&
							Index != 0u && Instructions[Index - 1u].Opcode == EOpcode::ConstI32)
						{
							// This expression has no control flow; the preceding constant
							// is exactly the right operand, including zero and negative values.
							A.mov(x86::r8d, x86::eax);
							EmitSignedDivOrModImmediate32(A, Instruction.Opcode, Instructions[Index - 1u].Immediate,
								[&](const auto Status) {return NewFailure(Status, Instruction.BytecodeOffset);});
						}
						else if (Instruction.Opcode == EOpcode::Div
							|| Instruction.Opcode == EOpcode::Mod)
						{
							A.test(x86::ecx, x86::ecx);
							A.jz(NewFailure(EJitRuntimeStatus::DivisionByZero,
								Instruction.BytecodeOffset));
							const Label Safe = A.new_label();
							A.cmp(x86::eax, std::numeric_limits<std::int32_t>::min());
							A.jne(Safe);
							A.cmp(x86::ecx, -1);
							A.je(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
							A.bind(Safe);
							A.cdq();
							A.idiv(x86::ecx);
							if (Instruction.Opcode == EOpcode::Mod)
							{
								A.mov(x86::eax, x86::edx);
							}
						}
						else if (Instruction.Opcode == EOpcode::BitAnd)
						{
							A.and_(x86::eax, x86::ecx);
						}
						else if (Instruction.Opcode == EOpcode::BitOr)
						{
							A.or_(x86::eax, x86::ecx);
						}
						else
						{
							A.xor_(x86::eax, x86::ecx);
						}
						A.mov(StackSlot(Depth - 2u), x86::eax);
						--Depth;
						break;
					}
				default:
					return nullptr;
				}
			}
			if (Depth != 1u)
			{
				return nullptr;
			}
			A.mov(x86::eax, x86::dword_ptr(x86::rsp, StackOffset));
			A.mov(x86::rdx, x86::qword_ptr(x86::rsp, OutPointerOffset));
			A.mov(x86::dword_ptr(x86::rdx), x86::eax);
			A.xor_(x86::eax, x86::eax);
			A.add(x86::rsp, FrameBytes);
			A.ret();

			for (const FFailure& Failure : Failures)
			{
				A.bind(Failure.Target);
				A.mov(x86::rdx, x86::qword_ptr(x86::rsp, FailurePointerOffset));
				const Label NoOffset = A.new_label();
				A.test(x86::rdx, x86::rdx);
				A.jz(NoOffset);
				A.mov(x86::dword_ptr(x86::rdx), Failure.Offset);
				A.bind(NoOffset);
				A.mov(x86::eax, static_cast<std::uint32_t>(Failure.Status));
				A.add(x86::rsp, FrameBytes);
				A.ret();
			}
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn)
				? reinterpret_cast<FPureI32ExpressionKernel>(Fn)
				: nullptr;
		}

		FTypedLoopI32Kernel EmitTypedLoopI32Kernel(
			JitRuntime& Runtime,
			const JitLinear::FTypedLoopPlan& Plan)
		{
			using namespace JitLinear;
			if (Plan.Instructions.empty() || Plan.LocalCount == 0u)
			{
				return nullptr;
			}
			for (const FTypedLoopInstruction& Instruction : Plan.Instructions)
			{
				switch (Instruction.Opcode)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstBool:
				case EOpcode::ConstStr:
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
				case EOpcode::GetIndexCopy:
				case EOpcode::SetIndex:
				case EOpcode::CallBuiltin:
				case EOpcode::CallMethod:
				case EOpcode::CallBoundMethod:
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::StoreField:
				case EOpcode::IncField:
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
				case EOpcode::JmpIfLocalGeImm:
					{
						break;
					}
				default:
					return nullptr;
				}
				if (Instruction.Opcode == EOpcode::CallBuiltin && !Instruction.bTextIdentity)
				{
					const auto Builtin = static_cast<EBuiltinId>(Instruction.Immediate);
					if (Builtin == EBuiltinId::CharCode ? Instruction.ArgumentCount != 2u
														: Instruction.ArgumentCount != 1u || (Builtin != EBuiltinId::ToInt && Builtin != EBuiltinId::ToUnsigned && Builtin != EBuiltinId::ToString))
					{
						return nullptr;
					}
				}
				if ((Instruction.Opcode == EOpcode::CallMethod
						|| Instruction.Opcode == EOpcode::CallBoundMethod)
					&& ((Instruction.FieldMethodIndex == 0xFFFFu ? Instruction.ArgumentCount != 1u : Instruction.ArgumentCount > 8u)
						|| (Instruction.Opcode == EOpcode::CallBoundMethod
							? Instruction.Operand1 == 0xFFFFu
							: Instruction.Immediate != 0
								&& (Instruction.Immediate < 1
									|| Instruction.Immediate > 3))))
				{
					return nullptr;
				}
			}
			constexpr std::size_t NoDirectMethod =
				std::numeric_limits<std::size_t>::max();
			constexpr std::size_t MultipleDirectMethods = NoDirectMethod - 1u;
			std::size_t SingleDirectMethodIndex = NoDirectMethod;
			std::uint16_t DirectMutationSourceSlot = 0xFFFFu;
			bool bHasMultipleMutationSources = false;
			for (std::size_t Index = 0; Index < Plan.Instructions.size(); ++Index)
			{
				const FTypedLoopInstruction& Instruction = Plan.Instructions[Index];
				if (Instruction.Opcode == EOpcode::StoreField
					|| Instruction.Opcode == EOpcode::IncField)
				{
					if (DirectMutationSourceSlot == 0xFFFFu)
					{
						DirectMutationSourceSlot = Instruction.Operand1;
					}
					else if (DirectMutationSourceSlot != Instruction.Operand1)
					{
						bHasMultipleMutationSources = true;
					}
				}
				if ((Instruction.Opcode != EOpcode::CallMethod
						&& Instruction.Opcode != EOpcode::CallBoundMethod)
					|| Instruction.Immediate != 0
					|| Instruction.Operand1 == 0xFFFFu
					|| Instruction.FieldMethodIndex != 0xFFFFu)
				{
					continue;
				}
				SingleDirectMethodIndex = SingleDirectMethodIndex == NoDirectMethod
					? Index
					: MultipleDirectMethods;
			}
			const bool bBatchDirectFieldMutations =
				Plan.bMutatesDirectScalarFields
				&& DirectMutationSourceSlot != 0xFFFFu
				&& !bHasMultipleMutationSources
				&& (SingleDirectMethodIndex == NoDirectMethod
					|| SingleDirectMethodIndex == MultipleDirectMethods);

			// Keep the two most frequently accessed loop locals in Microsoft-x64
			// nonvolatile registers. Unix x64 helpers and kernel typedefs deliberately
			// use VEROSCRIPT_JIT_CALL (ms_abi), so rdi/rsi have the same preservation
			// contract on every x64 host. The typed CFG gives every instruction a static local
			// slot, so this is a small, architecture-local register allocation rather
			// than a bytecode-shape specialization.  Stores remain write-through: helper
			// calls, failure reconstruction and the VM always observe authoritative
			// locals, while hot loads and read/modify/write operations avoid context
			// traffic.
			std::vector<std::uint32_t> LocalAccessCounts(Plan.LocalCount, 0u);
			const auto CountLocal = [&](const std::uint16_t Slot, const std::uint32_t Weight = 1u)
			{
				if (Slot < LocalAccessCounts.size())
				{
					LocalAccessCounts[Slot] += Weight;
				}
			};
			for (const FTypedLoopInstruction& Instruction : Plan.Instructions)
			{
				switch (Instruction.Opcode)
				{
				case EOpcode::LoadLocal:
					{
						CountLocal(Instruction.Operand0, 2u);
						break;
					}
				case EOpcode::StoreLocal:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::JmpIfLocalGeImm:
					{
						CountLocal(Instruction.Operand0, 3u);
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						CountLocal(Instruction.Operand0, 3u);
						CountLocal(Instruction.Operand1, 2u);
						break;
					}
				default:
					{
						break;
					}
				}
			}
			std::array<std::uint16_t, 2> CachedLocalSlots{0xFFFFu, 0xFFFFu};
			for (std::size_t CacheIndex = 0; CacheIndex < CachedLocalSlots.size(); ++CacheIndex)
			{
				std::uint32_t BestCount = 2u;
				for (std::uint16_t Slot = 0; Slot < LocalAccessCounts.size(); ++Slot)
				{
					const bool bAlreadyCached = std::find(
						CachedLocalSlots.begin(),
						CachedLocalSlots.begin() + static_cast<std::ptrdiff_t>(CacheIndex),
						Slot) != CachedLocalSlots.begin()
						+ static_cast<std::ptrdiff_t>(CacheIndex);
					if (bAlreadyCached || LocalAccessCounts[Slot] <= BestCount)
					{
						continue;
					}
					BestCount = LocalAccessCounts[Slot];
					CachedLocalSlots[CacheIndex] = Slot;
				}
			}

			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			std::vector<Label> At;
			At.reserve(Plan.Instructions.size());
			for (std::size_t Index = 0; Index < Plan.Instructions.size(); ++Index)
			{
				At.push_back(A.new_label());
			}
			const Label Epilogue = A.new_label();
			struct FFailureSite
			{
				Label Target;
				EJitRuntimeStatus Status = EJitRuntimeStatus::GenericFailure;
				std::uint32_t Offset = InvalidJitInstructionOffset;
			};
			std::vector<FFailureSite> Failures;
			const auto NewFailure = [&](const EJitRuntimeStatus Status,
				const std::uint32_t Offset)
			{
				const Label Target = A.new_label();
				Failures.push_back({Target, Status, Offset});
				return Target;
			};

			A.push(x86::rbx);
			A.push(x86::r12);
			A.push(x86::r13);
			A.push(x86::r14);
			A.push(x86::r15);
			A.push(x86::rbp);
			A.push(x86::rdi);
			A.push(x86::rsi);
			// 32-byte helper shadow space and one stack argument slot. 40 preserves
			// 16-byte call alignment after the eight GPR pushes above.
			constexpr std::int32_t KernelStackBytes = 40;
			A.sub(x86::rsp, KernelStackBytes);
			A.mov(x86::r15, x86::rcx);
			A.mov(x86::rbx, x86::qword_ptr(x86::r15,
				static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Locals))));
			A.mov(x86::r12, x86::qword_ptr(x86::r15,
				static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Stack))));
			A.xor_(x86::r13d, x86::r13d);
			A.xor_(x86::r14d, x86::r14d);
			A.xor_(x86::ebp, x86::ebp);
			if (CachedLocalSlots[0] != 0xFFFFu)
			{
				A.mov(x86::rdi, x86::qword_ptr(x86::rbx,
					static_cast<std::int32_t>(CachedLocalSlots[0] * 8u)));
			}
			if (CachedLocalSlots[1] != 0xFFFFu)
			{
				A.mov(x86::rsi, x86::qword_ptr(x86::rbx,
					static_cast<std::int32_t>(CachedLocalSlots[1] * 8u)));
			}
			A.jmp(At[0]);

			const auto CachedLocalIndex = [&](const std::uint16_t Slot) -> std::size_t
			{
				const auto It = std::find(CachedLocalSlots.begin(), CachedLocalSlots.end(), Slot);
				return It == CachedLocalSlots.end()
					? CachedLocalSlots.size()
					: static_cast<std::size_t>(It - CachedLocalSlots.begin());
			};
			const auto LoadLocalQword = [&](const std::uint16_t Slot, const x86::Gp& Out)
			{
				const std::size_t Cached = CachedLocalIndex(Slot);
				if (Cached == 0u)
				{
					A.mov(Out, x86::rdi);
				}
				else if (Cached == 1u)
				{
					A.mov(Out, x86::rsi);
				}
				else
				{
					A.mov(Out, x86::qword_ptr(x86::rbx,
						static_cast<std::int32_t>(Slot * 8u)));
				}
			};
			const auto StoreLocalQword = [&](const std::uint16_t Slot, const x86::Gp& Value)
			{
				const std::size_t Cached = CachedLocalIndex(Slot);
				if (Cached == 0u)
				{
					A.mov(x86::rdi, Value);
				}
				else if (Cached == 1u)
				{
					A.mov(x86::rsi, Value);
				}
				A.mov(x86::qword_ptr(x86::rbx,
					static_cast<std::int32_t>(Slot * 8u)),
					Value);
			};
			const auto LoadLocalDword = [&](const std::uint16_t Slot, const x86::Gp& Out)
			{
				const std::size_t Cached = CachedLocalIndex(Slot);
				if (Cached == 0u)
				{
					A.mov(Out.r32(), x86::edi);
				}
				else if (Cached == 1u)
				{
					A.mov(Out.r32(), x86::esi);
				}
				else
				{
					A.mov(Out.r32(), x86::dword_ptr(x86::rbx,
						static_cast<std::int32_t>(Slot * 8u)));
				}
			};
			const auto StoreLocalDword = [&](const std::uint16_t Slot, const x86::Gp& Value)
			{
				const std::size_t Cached = CachedLocalIndex(Slot);
				if (Cached == 0u)
				{
					A.mov(x86::edi, Value.r32());
				}
				else if (Cached == 1u)
				{
					A.mov(x86::esi, Value.r32());
				}
				A.mov(x86::dword_ptr(x86::rbx,
					static_cast<std::int32_t>(Slot * 8u)),
					Value.r32());
			};

			std::size_t NativeStackDepth = 0;
			const auto PushRax = [&]()
			{
				A.mov(x86::qword_ptr(x86::r12,
					static_cast<std::int32_t>(NativeStackDepth * 8u)),
					x86::rax);
				++NativeStackDepth;
			};
			const auto PopRax = [&]()
			{
				--NativeStackDepth;
				A.mov(x86::rax, x86::qword_ptr(x86::r12,
					static_cast<std::int32_t>(NativeStackDepth * 8u)));
			};
			const auto PopRcx = [&]()
			{
				--NativeStackDepth;
				A.mov(x86::rcx, x86::qword_ptr(x86::r12,
					static_cast<std::int32_t>(NativeStackDepth * 8u)));
			};
			const auto BranchTo = [&](const FTypedLoopInstruction& Instruction)
			{
				if (Instruction.BranchInstruction < At.size())
				{
					A.jmp(At[Instruction.BranchInstruction]);
				}
				else
				{
					A.xor_(x86::eax, x86::eax);
					A.jmp(Epilogue);
				}
			};
			const auto ContinueTo = [&](const FTypedLoopInstruction& Instruction)
			{
				if (Instruction.NextInstruction >= At.size())
				{
					return false;
				}
				A.jmp(At[Instruction.NextInstruction]);
				return true;
			};
			const auto JumpIfInstructionU32 = [&](const std::size_t InstructionIndex,
				const Label& Target)
			{
				A.mov(x86::r10, x86::qword_ptr(x86::r15,
					static_cast<std::int32_t>(offsetof(
						FTypedLoopNativeContext, InstructionKinds))));
				A.cmp(x86::byte_ptr(x86::r10,
					static_cast<std::int32_t>(InstructionIndex)),
					static_cast<std::uint32_t>(ETypedLoopLocalKind::U32));
				A.je(Target);
			};
			const auto LoadFieldBinding = [&](const std::size_t InstructionIndex)
			{
				A.mov(x86::r10, x86::qword_ptr(x86::r15,
					static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Fields))));
				const std::size_t ByteOffset = InstructionIndex
					* sizeof(FTypedLoopNativeFieldBinding);
				if (ByteOffset != 0u)
				{
					A.add(x86::r10, static_cast<std::uint32_t>(ByteOffset));
				}
			};
			const std::int32_t FieldStorageOffset = static_cast<std::int32_t>(
				offsetof(FTypedLoopNativeFieldBinding, Storage));
			const std::int32_t FieldObjectOffset = static_cast<std::int32_t>(
				offsetof(FTypedLoopNativeFieldBinding, Object));
			// This offset is resolved in the same runtime that emitted the kernel; it
			// does not become part of bytecode/package ABI.
			FScriptObject LayoutProbe;
			const std::int32_t MutationVersionOffset = static_cast<std::int32_t>(
				reinterpret_cast<const std::uint8_t*>(&LayoutProbe.MutationVersion)
					- reinterpret_cast<const std::uint8_t*>(&LayoutProbe));
			// Input/output: eax. r10 addresses one prevalidated method binding.
			// Runtime-resolved divisors are loop invariant, so binding precomputes the
			// same Hacker's Delight magic used by compile-time constant modulo. This
			// keeps hot monomorphic methods free of idiv without baking a script method
			// or divisor into the generic kernel.
			const auto EmitBoundSignedModulo = [&](const std::int32_t DivisorOffset,
				const std::int32_t MagicOffset, const std::int32_t ShiftOffset,
				const std::uint32_t FailureOffset)
			{
				const Label Complete = A.new_label();
				const Label NotOne = A.new_label();
				const Label General = A.new_label();
				A.mov(x86::r8d, x86::eax);
				A.mov(x86::r9d, x86::dword_ptr(x86::r10, DivisorOffset));
				A.test(x86::r9d, x86::r9d);
				A.jz(Complete); // zero denotes no transform, not `% 0`
				A.cmp(x86::r9d, 1);
				A.jne(NotOne);
				A.xor_(x86::eax, x86::eax);
				A.jmp(Complete);
				A.bind(NotOne);
				A.cmp(x86::r9d, -1);
				A.jne(General);
				A.cmp(x86::r8d, std::numeric_limits<std::int32_t>::min());
				A.je(NewFailure(EJitRuntimeStatus::IntegerOverflow, FailureOffset));
				A.xor_(x86::eax, x86::eax);
				A.jmp(Complete);
				A.bind(General);
				A.mov(x86::ecx, x86::dword_ptr(x86::r10, MagicOffset));
				A.mov(x86::eax, x86::r8d);
				A.imul(x86::ecx); // edx:eax = signed input * magic
				const Label NegativeDivisor = A.new_label();
				const Label CorrectionComplete = A.new_label();
				A.test(x86::r9d, x86::r9d);
				A.js(NegativeDivisor);
				A.test(x86::ecx, x86::ecx);
				A.jns(CorrectionComplete);
				A.add(x86::edx, x86::r8d);
				A.jmp(CorrectionComplete);
				A.bind(NegativeDivisor);
				A.test(x86::ecx, x86::ecx);
				A.jle(CorrectionComplete);
				A.sub(x86::edx, x86::r8d);
				A.bind(CorrectionComplete);
				A.movzx(x86::ecx, x86::byte_ptr(x86::r10, ShiftOffset));
				A.sar(x86::edx, x86::cl);
				A.mov(x86::eax, x86::edx);
				A.shr(x86::eax, 31);
				A.add(x86::edx, x86::eax); // quotient
				A.imul(x86::edx, x86::r9d);
				A.mov(x86::eax, x86::r8d);
				A.sub(x86::eax, x86::edx); // signed remainder
				A.bind(Complete);
			};

			for (std::size_t Index = 0; Index < Plan.Instructions.size(); ++Index)
			{
				const FTypedLoopInstruction& Instruction = Plan.Instructions[Index];
				A.bind(At[Index]);
				NativeStackDepth = Instruction.EntryStackDepth;
				switch (Instruction.Opcode)
				{
				case EOpcode::Nop:
					{
						break;
					}
				case EOpcode::ConstI32:
					{
						A.mov(x86::eax, Instruction.Immediate);
						PushRax();
						break;
					}
				case EOpcode::ConstStr:
					{
						A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&Plan.TextConstants[Instruction.TextValueSlot]) | TypedLoopNativeStringTag);
						PushRax();
						break;
					}
				case EOpcode::ConstBool:
					{
						A.mov(x86::eax, Instruction.Immediate != 0 ? 1 : 0);
						PushRax();
						break;
					}
				case EOpcode::LoadLocal:
					{
						LoadLocalQword(Instruction.Operand0, x86::rax);
						PushRax();
						break;
					}
				case EOpcode::StoreLocal:
					{
						if (Instruction.bTextConcatStore || Instruction.TextFormatInstruction != 0xFFFFu)
						{
							A.mov(x86::r8, x86::qword_ptr(x86::r12, static_cast<std::int32_t>((NativeStackDepth - 1u) * 8u)));
							if (Instruction.bTextConcatStore)
							{
								A.mov(x86::r9, x86::qword_ptr(x86::r12, static_cast<std::int32_t>(NativeStackDepth * 8u)));
							}
							else
							{
								A.xor_(x86::r9d, x86::r9d);
							}
							A.mov(x86::rcx, x86::r15);
							A.mov(x86::edx, static_cast<std::uint32_t>(Index));
							A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&TypedLoopNativeStoreText));
							A.call(x86::rax);
							A.test(x86::eax, x86::eax);
							A.jnz(Epilogue);
							A.mov(x86::rax, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
							--NativeStackDepth;
							StoreLocalQword(Instruction.Operand0, x86::rax);
							break;
						}
						PopRax();
						if (Plan.bOwnsStringTemporaries)
						{
							const Label Store = A.new_label();
							A.mov(x86::r10, x86::qword_ptr(x86::r15,
								static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, InstructionKinds))));
							A.cmp(x86::byte_ptr(x86::r10, static_cast<std::int32_t>(Index)),
								static_cast<std::uint32_t>(ETypedLoopLocalKind::String));
							A.jne(Store);
							A.mov(x86::r8, x86::rax);
							A.mov(x86::rcx, x86::r15);
							A.mov(x86::edx, Instruction.Operand0);
							A.mov(x86::r9d, Instruction.BytecodeOffset);
							A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&TypedLoopNativeStoreStringLocal));
							A.call(x86::rax);
							A.test(x86::eax, x86::eax);
							A.jnz(Epilogue);
							A.mov(x86::rax, x86::qword_ptr(x86::r15,
								static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
							A.bind(Store);
						}
						StoreLocalQword(Instruction.Operand0, x86::rax);
						break;
					}
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
					{
						const Label Unsigned = A.new_label();
						const Label Store = A.new_label();
						JumpIfInstructionU32(Index, Unsigned);
						LoadLocalDword(Instruction.Operand0, x86::rax);
						A.add(x86::eax, Instruction.Opcode == EOpcode::IncLocal
							? 1 : Instruction.Immediate);
						A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
							Instruction.BytecodeOffset));
						A.jmp(Store);
						A.bind(Unsigned);
						LoadLocalDword(Instruction.Operand0, x86::rax);
						A.add(x86::eax, Instruction.Opcode == EOpcode::IncLocal
							? 1 : Instruction.Immediate);
						A.bind(Store);
						StoreLocalDword(Instruction.Operand0, x86::rax);
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						if (Instruction.bTextAdd)
						{
							LoadLocalQword(Instruction.Operand0, x86::r8);
							LoadLocalQword(Instruction.Operand1, x86::r9);
							A.mov(x86::rcx, x86::r15);
							A.mov(x86::edx, static_cast<std::uint32_t>(Index));
							A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&TypedLoopNativeStoreText));
							A.call(x86::rax);
							A.test(x86::eax, x86::eax);
							A.jnz(Epilogue);
							A.mov(x86::rax, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
							StoreLocalQword(Instruction.Operand0, x86::rax);
							break;
						}
						const Label Unsigned = A.new_label();
						const Label Store = A.new_label();
						JumpIfInstructionU32(Index, Unsigned);
						LoadLocalDword(Instruction.Operand0, x86::rax);
						LoadLocalDword(Instruction.Operand1, x86::rcx);
						A.add(x86::eax, x86::ecx);
						A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
							Instruction.BytecodeOffset));
						A.jmp(Store);
						A.bind(Unsigned);
						LoadLocalDword(Instruction.Operand0, x86::rax);
						LoadLocalDword(Instruction.Operand1, x86::rcx);
						A.add(x86::eax, x86::ecx);
						A.bind(Store);
						StoreLocalDword(Instruction.Operand0, x86::rax);
						break;
					}
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
					{
						if (Instruction.bTextAdd)
						{
							// The adjacent Store consumes both original slots. No mutation
							// or allocation occurs before that source StoreLocal.
							--NativeStackDepth;
							break;
						}
						PopRcx();
						PopRax();
						const Label Unsigned = A.new_label();
						const Label Complete = A.new_label();
						JumpIfInstructionU32(Index, Unsigned);
						if (Instruction.Opcode == EOpcode::Add)
						{
							A.add(x86::eax, x86::ecx);
						}
						else if (Instruction.Opcode == EOpcode::Sub)
						{
							A.sub(x86::eax, x86::ecx);
						}
						else if (Instruction.Opcode == EOpcode::Mul)
						{
							A.imul(x86::eax, x86::ecx);
						}
						else
						{
							bool bConstantPositiveDivisor = false;
							std::int32_t ConstantDivisor = 0;
							if (Index != 0u)
							{
								const FTypedLoopInstruction& Previous =
									Plan.Instructions[Index - 1u];
								const bool bAlternateEntry = std::any_of(
									Plan.Instructions.begin(), Plan.Instructions.end(),
									[Index](const FTypedLoopInstruction& Candidate)
									{
										return Candidate.BranchInstruction == Index;
									});
								bConstantPositiveDivisor = !bAlternateEntry
									&& Previous.Opcode == EOpcode::ConstI32
									&& !Previous.bImmediateU32
									&& Previous.NextInstruction == Index
									&& Previous.Immediate > 1;
								ConstantDivisor = Previous.Immediate;
							}
							const Label GenericSignedDivision = A.new_label();
							const Label SignedDivisionComplete = A.new_label();
							if (bConstantPositiveDivisor)
							{
								// The preceding constant is the sole CFG predecessor, so the
								// divisor is loop invariant. Lower signed / and % with Hacker's
								// Delight magic instead of paying for idiv in every iteration.
								A.cmp(x86::ecx, ConstantDivisor);
								A.jne(GenericSignedDivision);
								const FSignedDivMagic32 Magic =
									ComputeSignedDivMagic32(ConstantDivisor);
								A.mov(x86::r8d, x86::eax);
								A.mov(x86::ecx, Magic.Multiplier);
								A.imul(x86::ecx);
								if (Magic.Multiplier < 0)
								{
									A.add(x86::edx, x86::r8d);
								}
								if (Magic.Shift != 0u)
								{
									A.sar(x86::edx, Magic.Shift);
								}
								A.mov(x86::eax, x86::edx);
								A.shr(x86::eax, 31);
								A.add(x86::edx, x86::eax);
								if (Instruction.Opcode == EOpcode::Mod)
								{
									A.imul(x86::edx, x86::edx, ConstantDivisor);
									A.mov(x86::eax, x86::r8d);
									A.sub(x86::eax, x86::edx);
								}
								else
								{
									A.mov(x86::eax, x86::edx);
								}
								A.jmp(SignedDivisionComplete);
							}
							A.bind(GenericSignedDivision);
							A.test(x86::ecx, x86::ecx);
							A.jz(NewFailure(EJitRuntimeStatus::DivisionByZero,
								Instruction.BytecodeOffset));
							const Label Safe = A.new_label();
							A.cmp(x86::eax, std::numeric_limits<std::int32_t>::min());
							A.jne(Safe);
							A.cmp(x86::ecx, -1);
							A.je(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
							A.bind(Safe);
							A.cdq();
							A.idiv(x86::ecx);
							if (Instruction.Opcode == EOpcode::Mod)
							{
								A.mov(x86::eax, x86::edx);
							}
							A.bind(SignedDivisionComplete);
						}
						if (Instruction.Opcode == EOpcode::Add
							|| Instruction.Opcode == EOpcode::Sub
							|| Instruction.Opcode == EOpcode::Mul)
						{
							A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
						}
						A.jmp(Complete);
						A.bind(Unsigned);
						if (Instruction.Opcode == EOpcode::Add)
						{
							A.add(x86::eax, x86::ecx);
						}
						else if (Instruction.Opcode == EOpcode::Sub)
						{
							A.sub(x86::eax, x86::ecx);
						}
						else if (Instruction.Opcode == EOpcode::Mul)
						{
							A.imul(x86::eax, x86::ecx);
						}
						else
						{
							A.test(x86::ecx, x86::ecx);
							A.jz(NewFailure(EJitRuntimeStatus::DivisionByZero,
								Instruction.BytecodeOffset));
							A.xor_(x86::edx, x86::edx);
							A.div(x86::ecx);
							if (Instruction.Opcode == EOpcode::Mod)
							{
								A.mov(x86::eax, x86::edx);
							}
						}
						A.bind(Complete);
						PushRax();
						break;
					}
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						PopRcx();
						PopRax();
						if (Instruction.Opcode == EOpcode::BitAnd)
						{
							A.and_(x86::eax, x86::ecx);
						}
						else if (Instruction.Opcode == EOpcode::BitOr)
						{
							A.or_(x86::eax, x86::ecx);
						}
						else if (Instruction.Opcode == EOpcode::BitXor)
						{
							A.xor_(x86::eax, x86::ecx);
						}
						else
						{
							A.cmp(x86::ecx, 32);
							A.jae(NewFailure(EJitRuntimeStatus::ShiftOutOfRange,
								Instruction.BytecodeOffset));
							if (Instruction.Opcode == EOpcode::Shl)
							{
								A.shl(x86::eax, x86::cl);
							}
							else
							{
								const Label Unsigned = A.new_label();
								const Label Complete = A.new_label();
								JumpIfInstructionU32(Index, Unsigned);
								A.sar(x86::eax, x86::cl);
								A.jmp(Complete);
								A.bind(Unsigned);
								A.shr(x86::eax, x86::cl);
								A.bind(Complete);
							}
						}
						PushRax();
						break;
					}
				case EOpcode::Eq:
				case EOpcode::Ne:
				case EOpcode::Lt:
				case EOpcode::Le:
				case EOpcode::Gt:
				case EOpcode::Ge:
					{
						PopRcx();
						PopRax();
						if (Instruction.Opcode == EOpcode::Eq
							|| Instruction.Opcode == EOpcode::Ne)
						{
							const Label IntegerCompare = A.new_label();
							const Label CompareComplete = A.new_label();
							A.mov(x86::r10, x86::rax);
							A.or_(x86::r10, x86::rcx);
							A.test(x86::r10, x86::r10);
							A.jns(IntegerCompare);
							// Both operands are invocation-local borrowed string views whose
							// lifetime was validated before entering the kernel. Compare their
							// lengths and bytes directly so a hot string loop does not cross a
							// C++ helper/FValue boundary on every equality operation.
							A.btr(x86::rax, 63);
							A.btr(x86::rcx, 63);
							const Label StringEqual = A.new_label();
							const Label StringUnequal = A.new_label();
							const Label StringCompareLoop = A.new_label();
							const Label StringResult = A.new_label();
							A.cmp(x86::rax, x86::rcx);
							A.je(StringEqual);
							// Hashes are computed once while the borrowed views are prepared. A
							// mismatch proves inequality and avoids touching either string's bytes;
							// equal hashes still fall through to the exact comparison, so collisions
							// cannot change language semantics.
							A.mov(x86::rdx, x86::qword_ptr(x86::rax,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeStringView, Hash))));
							A.cmp(x86::rdx, x86::qword_ptr(x86::rcx,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeStringView, Hash))));
							A.jne(StringUnequal);
							A.mov(x86::rdx, x86::qword_ptr(x86::rax,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeStringView, Size))));
							A.cmp(x86::rdx, x86::qword_ptr(x86::rcx,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeStringView, Size))));
							A.jne(StringUnequal);
							A.mov(x86::r8, x86::qword_ptr(x86::rax,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeStringView, Data))));
							A.mov(x86::r9, x86::qword_ptr(x86::rcx,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeStringView, Data))));
							A.xor_(x86::r10d, x86::r10d);
							A.bind(StringCompareLoop);
							A.cmp(x86::r10, x86::rdx);
							A.je(StringEqual);
							A.movzx(x86::r11d, x86::byte_ptr(x86::r8, x86::r10));
							A.cmp(x86::r11b, x86::byte_ptr(x86::r9, x86::r10));
							A.jne(StringUnequal);
							A.inc(x86::r10);
							A.jmp(StringCompareLoop);
							A.bind(StringEqual);
							A.mov(x86::eax, Instruction.Opcode == EOpcode::Eq ? 1u : 0u);
							A.jmp(StringResult);
							A.bind(StringUnequal);
							A.mov(x86::eax, Instruction.Opcode == EOpcode::Eq ? 0u : 1u);
							A.bind(StringResult);
							NativeStackDepth = Instruction.EntryStackDepth - 2u;
							PushRax();
							A.jmp(CompareComplete);
							A.bind(IntegerCompare);
							NativeStackDepth = Instruction.EntryStackDepth - 2u;
							A.cmp(x86::eax, x86::ecx);
							if (Instruction.Opcode == EOpcode::Eq)
							{
								A.sete(x86::al);
							}
							else
							{
								A.setne(x86::al);
							}
							A.movzx(x86::eax, x86::al);
							PushRax();
							A.bind(CompareComplete);
							break;
						}
						{
							const Label Unsigned = A.new_label();
							const Label Complete = A.new_label();
							JumpIfInstructionU32(Index, Unsigned);
							A.cmp(x86::eax, x86::ecx);
							if (Instruction.Opcode == EOpcode::Lt)
							{
								A.setl(x86::al);
							}
							else if (Instruction.Opcode == EOpcode::Le)
							{
								A.setle(x86::al);
							}
							else if (Instruction.Opcode == EOpcode::Gt)
							{
								A.setg(x86::al);
							}
							else
							{
								A.setge(x86::al);
							}
							A.jmp(Complete);
							A.bind(Unsigned);
							A.cmp(x86::eax, x86::ecx);
							if (Instruction.Opcode == EOpcode::Lt)
							{
								A.setb(x86::al);
							}
							else if (Instruction.Opcode == EOpcode::Le)
							{
								A.setbe(x86::al);
							}
							else if (Instruction.Opcode == EOpcode::Gt)
							{
								A.seta(x86::al);
							}
							else
							{
								A.setae(x86::al);
							}
							A.bind(Complete);
						}
						A.movzx(x86::eax, x86::al);
						PushRax();
						break;
					}
				case EOpcode::Neg:
					{
						PopRax();
						const Label Unsigned = A.new_label();
						const Label Complete = A.new_label();
						JumpIfInstructionU32(Index, Unsigned);
						A.neg(x86::eax);
						A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
							Instruction.BytecodeOffset));
						A.jmp(Complete);
						A.bind(Unsigned);
						A.neg(x86::eax);
						A.bind(Complete);
						PushRax();
						break;
					}
				case EOpcode::BitNot:
					{
						PopRax();
						A.not_(x86::eax);
						PushRax();
						break;
					}
				case EOpcode::Not:
					{
						PopRax();
						A.test(x86::eax, x86::eax);
						A.setz(x86::al);
						A.movzx(x86::eax, x86::al);
						PushRax();
						break;
					}
				case EOpcode::Dup:
					{
						A.mov(x86::rax, x86::qword_ptr(x86::r12,
							static_cast<std::int32_t>((NativeStackDepth - 1u) * 8u)));
						PushRax();
						break;
					}
				case EOpcode::Pop:
					{
						--NativeStackDepth;
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (Instruction.bTextIdentity || Instruction.TextFormatInstruction != 0xFFFFu)
						{
							break;
						}
						if (static_cast<EBuiltinId>(Instruction.Immediate) == EBuiltinId::CharCode)
						{
							const Label UnsignedIndex = A.new_label();
							const Label CheckIndex = A.new_label();
							const Label SlowFailure = A.new_label();
							const Label Complete = A.new_label();
							PopRcx();
							PopRax();
							A.mov(x86::r8, x86::rax);
							A.btr(x86::r8, 63);
							JumpIfInstructionU32(Index, UnsignedIndex);
							A.movsxd(x86::r9, x86::ecx);
							A.jmp(CheckIndex);
							A.bind(UnsignedIndex);
							A.mov(x86::r9d, x86::ecx);
							A.bind(CheckIndex);
							A.cmp(x86::r9, x86::qword_ptr(x86::r8, static_cast<std::int32_t>(offsetof(FTypedLoopNativeStringView, Size))));
							A.jae(SlowFailure);
							A.mov(x86::r8, x86::qword_ptr(x86::r8, static_cast<std::int32_t>(offsetof(FTypedLoopNativeStringView, Data))));
							A.movzx(x86::eax, x86::byte_ptr(x86::r8, x86::r9));
							A.jmp(Complete);
							A.bind(SlowFailure);
							A.mov(x86::r8, x86::qword_ptr(x86::r12, static_cast<std::int32_t>(NativeStackDepth * 8u)));
							A.mov(x86::r9, x86::qword_ptr(x86::r12, static_cast<std::int32_t>((NativeStackDepth + 1u) * 8u)));
							A.mov(x86::rcx, x86::r15);
							A.mov(x86::edx, static_cast<std::uint32_t>(Index));
							A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&TypedLoopNativeTextOperation));
							A.call(x86::rax);
							A.test(x86::eax, x86::eax);
							A.jnz(Epilogue);
							A.mov(x86::rax, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
							A.bind(Complete);
							PushRax();
							break;
						}
						PopRax();
						const EBuiltinId Builtin = static_cast<EBuiltinId>(
							Instruction.Immediate);
						if (Builtin == EBuiltinId::ToString)
						{
							A.mov(x86::r8, x86::rax);
							A.mov(x86::rcx, x86::r15);
							A.mov(x86::edx, Instruction.TextValueSlot);
							A.mov(x86::r9d, Instruction.BytecodeOffset);
							A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&TypedLoopNativeToString));
							A.call(x86::rax);
							A.test(x86::eax, x86::eax);
							A.jnz(Epilogue);
							A.mov(x86::rax, x86::qword_ptr(x86::r15,
								static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
							PushRax();
							break;
						}
						const Label UnsignedInput = A.new_label();
						const Label Complete = A.new_label();
						JumpIfInstructionU32(Index, UnsignedInput);
						if (Builtin == EBuiltinId::ToUnsigned)
						{
							A.test(x86::eax, x86::eax);
							A.js(NewFailure(EJitRuntimeStatus::ToUnsignedOverflow,
								Instruction.BytecodeOffset));
						}
						A.jmp(Complete);
						A.bind(UnsignedInput);
						if (Builtin == EBuiltinId::ToInt)
						{
							A.test(x86::eax, x86::eax);
							A.js(NewFailure(EJitRuntimeStatus::ToIntOverflow,
								Instruction.BytecodeOffset));
						}
						A.bind(Complete);
						PushRax();
						break;
					}
				case EOpcode::GetIndexCopy:
					{
						PopRax();
						A.mov(x86::r8, x86::rax);
						PopRax();
						A.mov(x86::rdx, x86::rax);
						const std::size_t IndexResultDepth = NativeStackDepth;
						const Label UseHelper = A.new_label();
						const Label ArrayString = A.new_label();
						const Label ArrayObject = A.new_label();
						const Label Complete = A.new_label();
						A.cmp(x86::byte_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, Kind))),
							static_cast<std::uint32_t>(ETypedLoopLocalKind::ArrayI32));
						A.je(Complete);
						A.cmp(x86::byte_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, Kind))),
							static_cast<std::uint32_t>(ETypedLoopLocalKind::ArrayU32));
						A.je(Complete);
						A.cmp(x86::byte_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, Kind))),
							static_cast<std::uint32_t>(ETypedLoopLocalKind::ArrayString));
						A.je(ArrayString);
						A.cmp(x86::byte_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, Kind))),
							static_cast<std::uint32_t>(ETypedLoopLocalKind::ArrayObject));
						A.je(ArrayObject);
						A.jmp(UseHelper);
						A.bind(Complete);
						{
							const Label UnsignedIndex = A.new_label();
							const Label IndexReady = A.new_label();
							JumpIfInstructionU32(Index, UnsignedIndex);
							A.movsxd(x86::r11, x86::r8d);
							A.jmp(IndexReady);
							A.bind(UnsignedIndex);
							A.mov(x86::r11d, x86::r8d);
							A.bind(IndexReady);
						}
						A.mov(x86::r10, x86::qword_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, NativeSize))));
						A.cmp(x86::r11, x86::r10);
						A.jae(UseHelper);
						A.mov(x86::rax, x86::qword_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, NativeData))));
						A.mov(x86::eax, x86::dword_ptr(x86::rax, x86::r11, 2));
						A.inc(x86::r13);
						NativeStackDepth = IndexResultDepth;
						PushRax();
						const Label Finish = A.new_label();
						A.jmp(Finish);
						A.bind(ArrayString);
						A.mov(x86::r10, x86::qword_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, NativeSize))));
						A.movsxd(x86::r11, x86::r8d);
						A.cmp(x86::r11, x86::r10);
						A.jae(UseHelper);
						A.mov(x86::rax, x86::qword_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, NativeData))));
						A.imul(x86::r11, x86::r11,
							static_cast<std::uint32_t>(sizeof(FTypedLoopNativeStringView)));
						A.add(x86::rax, x86::r11);
						A.mov(x86::r10, TypedLoopNativeStringTag);
						A.or_(x86::rax, x86::r10);
						A.inc(x86::r13);
						NativeStackDepth = IndexResultDepth;
						PushRax();
						A.jmp(Finish);
						A.bind(ArrayObject);
						{
							const Label UnsignedIndex = A.new_label();
							const Label IndexReady = A.new_label();
							JumpIfInstructionU32(Index, UnsignedIndex);
							A.movsxd(x86::r11, x86::r8d);
							A.jmp(IndexReady);
							A.bind(UnsignedIndex);
							A.mov(x86::r11d, x86::r8d);
							A.bind(IndexReady);
						}
						A.mov(x86::r10, x86::qword_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, NativeSize))));
						A.cmp(x86::r11, x86::r10);
						A.jae(UseHelper);
						A.imul(x86::r11, x86::r11,
							static_cast<std::uint32_t>(sizeof(FTypedLoopNativeObjectView)));
						A.mov(x86::rax, x86::qword_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, NativeData))));
						A.add(x86::rax, x86::r11);
						A.or_(x86::rax, TypedLoopNativeObjectViewTag);
						A.inc(x86::r13);
						NativeStackDepth = IndexResultDepth;
						PushRax();
						A.jmp(Finish);
						A.bind(UseHelper);
						A.mov(x86::rcx, x86::r15);
						A.mov(x86::r9d, Instruction.BytecodeOffset);
						A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
							&TypedLoopNativeGetIndexI32));
						A.call(x86::rax);
						A.test(x86::eax, x86::eax);
						A.jnz(Epilogue);
						A.mov(x86::rax, x86::qword_ptr(x86::r15,
							static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
						NativeStackDepth = IndexResultDepth;
						PushRax();
						A.bind(Finish);
						break;
					}
				case EOpcode::SetIndex:
					{
						PopRax();
						A.mov(x86::r9d, x86::eax);
						PopRax();
						A.mov(x86::r8, x86::rax);
						PopRax();
						A.mov(x86::rdx, x86::rax);
						const std::size_t SetIndexResultDepth = NativeStackDepth;
						const Label UseHelper = A.new_label();
						const Label DirectArray = A.new_label();
						const Label Complete = A.new_label();
						A.cmp(x86::byte_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, Kind))),
							static_cast<std::uint32_t>(ETypedLoopLocalKind::ArrayI32));
						A.je(DirectArray);
						A.cmp(x86::byte_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, Kind))),
							static_cast<std::uint32_t>(ETypedLoopLocalKind::ArrayU32));
						A.jne(UseHelper);
						A.bind(DirectArray);
						{
							const Label UnsignedIndex = A.new_label();
							const Label IndexReady = A.new_label();
							JumpIfInstructionU32(Index, UnsignedIndex);
							A.movsxd(x86::r11, x86::r8d);
							A.jmp(IndexReady);
							A.bind(UnsignedIndex);
							A.mov(x86::r11d, x86::r8d);
							A.bind(IndexReady);
						}
						A.mov(x86::r10, x86::qword_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, NativeSize))));
						A.cmp(x86::r11, x86::r10);
						A.jae(UseHelper);
						A.mov(x86::rax, x86::qword_ptr(x86::rdx,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeBinding, NativeData))));
						A.mov(x86::dword_ptr(x86::rax, x86::r11, 2), x86::r9d);
						A.inc(x86::r14);
						A.mov(x86::rax, x86::rdx);
						NativeStackDepth = SetIndexResultDepth;
						PushRax();
						A.jmp(Complete);
						A.bind(UseHelper);
						A.mov(x86::rcx, x86::r15);
						A.mov(x86::eax, Instruction.BytecodeOffset);
						A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
						A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
							&TypedLoopNativeSetIndexI32));
						A.call(x86::rax);
						A.test(x86::eax, x86::eax);
						A.jnz(Epilogue);
						A.mov(x86::rax, x86::qword_ptr(x86::r15,
							static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
						NativeStackDepth = SetIndexResultDepth;
						PushRax();
						A.bind(Complete);
						break;
					}
				case EOpcode::CallMethod:
				case EOpcode::CallBoundMethod:
					{
						if (Instruction.FieldMethodIndex != 0xFFFFu)
						{
							if (Instruction.ArgumentCount == 1u)
							{
								PopRax();
								A.mov(x86::ecx, x86::eax);
							}
							else if (Instruction.ArgumentCount > 1u)
							{
								NativeStackDepth -= Instruction.ArgumentCount;
								A.lea(x86::rcx, x86::qword_ptr(x86::r12, static_cast<std::int32_t>(NativeStackDepth * 8u)));
							}
							else
							{
								A.xor_(x86::ecx, x86::ecx);
							}
							--NativeStackDepth; // guarded receiver
							const auto LoadMethod = [&]()
							{
								A.mov(x86::r10, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Methods))));
								if (Index != 0u)
								{
									A.add(x86::r10, static_cast<std::uint32_t>(Index * sizeof(FTypedLoopNativeMethodBinding)));
								}
							};
							LoadMethod();
							A.lea(x86::rdx, x86::qword_ptr(x86::r10, static_cast<std::int32_t>(offsetof(FTypedLoopNativeMethodBinding, FieldState))));
							A.lea(x86::r8, x86::qword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
							A.lea(x86::r9, x86::dword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, FailureOffset))));
							A.call(x86::qword_ptr(x86::r10, static_cast<std::int32_t>(offsetof(FTypedLoopNativeMethodBinding, FieldCode))));
							const Label Succeeded = A.new_label();
							A.test(x86::eax, x86::eax);
							A.jz(Succeeded);
							// Report only: the kernel already committed every preceding field
							// store. Reinvoking it here would duplicate source-visible effects.
							A.mov(x86::r8d, x86::eax);
							A.mov(x86::rcx, x86::r15);
							A.mov(x86::edx, static_cast<std::uint32_t>(Index));
							A.mov(x86::r9d, Instruction.BytecodeOffset);
							A.mov(x86::rax, reinterpret_cast<std::uint64_t>(&TypedLoopNativeReportI32FieldMethodFailure));
							A.call(x86::rax);
							A.jmp(Epilogue);
							A.bind(Succeeded);
							LoadMethod();
							A.inc(x86::qword_ptr(x86::r10, static_cast<std::int32_t>(offsetof(FTypedLoopNativeMethodBinding, CallCount))));
							A.mov(x86::eax, x86::dword_ptr(x86::r15, static_cast<std::int32_t>(offsetof(FTypedLoopNativeContext, Scratch))));
							PushRax();
							break;
						}
						PopRax();
						if (Instruction.Opcode == EOpcode::CallBoundMethod
							|| (Instruction.Immediate == 0
								&& Instruction.Operand1 != 0xFFFFu))
						{
							// Binding preflight has already resolved generation, receiver,
							// method target and stable field slot. Keep the complete scalar
							// transform in machine code instead of crossing a C++ helper on
							// every iteration. CallCount is committed through the same
							// ownership/mutation path after the kernel returns.
							A.mov(x86::r8d, x86::eax);
							PopRax(); // discard guarded receiver token
							A.mov(x86::r10, x86::qword_ptr(x86::r15,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeContext, Methods))));
							const std::size_t MethodByteOffset = Index
								* sizeof(FTypedLoopNativeMethodBinding);
							if (MethodByteOffset != 0u)
							{
								A.add(x86::r10, static_cast<std::uint32_t>(MethodByteOffset));
							}
							A.mov(x86::r11, x86::qword_ptr(x86::r10,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeMethodBinding, Storage))));
							A.mov(x86::eax, x86::r8d);
							EmitBoundSignedModulo(
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeMethodBinding, ArgumentModulus)),
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeMethodBinding, ArgumentMagic)),
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeMethodBinding, ArgumentShift)),
								Instruction.BytecodeOffset);
							A.add(x86::eax, x86::dword_ptr(x86::r11));
							A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
							A.add(x86::eax, x86::dword_ptr(x86::r10,
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeMethodBinding, Addend))));
							A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
							EmitBoundSignedModulo(
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeMethodBinding, ResultModulus)),
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeMethodBinding, ResultMagic)),
								static_cast<std::int32_t>(offsetof(
									FTypedLoopNativeMethodBinding, ResultShift)),
								Instruction.BytecodeOffset);
							A.mov(x86::dword_ptr(x86::r11), x86::eax);
							if (Index == SingleDirectMethodIndex)
							{
								A.inc(x86::rbp);
							}
							else
							{
								A.inc(x86::qword_ptr(x86::r10,
										static_cast<std::int32_t>(offsetof(
											FTypedLoopNativeMethodBinding, CallCount))));
							}
							PushRax();
							break;
						}
						else if (Instruction.Immediate == 0)
						{
							A.mov(x86::r9d, x86::eax);
							PopRax();
							A.mov(x86::r8, x86::rax);
							A.mov(x86::rdx, Index);
							A.mov(x86::rcx, x86::r15);
							A.mov(x86::eax, Instruction.BytecodeOffset);
							A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
							A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
								&TypedLoopNativeInvokePureI32Method));
						}
						else
						{
							A.mov(x86::r8, x86::rax);
							PopRax();
							A.mov(x86::rdx, x86::rax);
							A.mov(x86::rcx, x86::r15);
							if (Instruction.Immediate == 1)
							{
								// The read-only operation is known at compile time. Its
								// four-argument entry keeps the source offset in r9d.
								A.mov(x86::r9d, Instruction.BytecodeOffset);
								A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
									&TypedLoopNativeContainsI32));
							}
							else
							{
								A.mov(x86::r9d, static_cast<std::uint32_t>(
									Instruction.Immediate));
								A.mov(x86::eax, Instruction.BytecodeOffset);
								A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
								A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
									&TypedLoopNativeContainerMethodI32));
							}
						}
						A.call(x86::rax);
						A.test(x86::eax, x86::eax);
						A.jnz(Epilogue);
						A.mov(x86::rax, x86::qword_ptr(x86::r15,
							static_cast<std::int32_t>(offsetof(
								FTypedLoopNativeContext, Scratch))));
						PushRax();
						break;
					}
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
					{
						if (Instruction.Opcode == EOpcode::LoadField)
						{
							--NativeStackDepth;
						}
						LoadFieldBinding(Index);
						A.mov(x86::r10, x86::qword_ptr(x86::r10, FieldStorageOffset));
						A.mov(x86::eax, x86::dword_ptr(x86::r10));
						PushRax();
						break;
					}
				case EOpcode::StoreField:
					{
						PopRax();
						--NativeStackDepth;
						LoadFieldBinding(Index);
						A.mov(x86::r10, x86::qword_ptr(x86::r10, FieldStorageOffset));
						A.mov(x86::dword_ptr(x86::r10), x86::eax);
						if (bBatchDirectFieldMutations)
						{
							A.inc(x86::rbp);
						}
						else
						{
							LoadFieldBinding(Index);
							A.mov(x86::r11, x86::qword_ptr(x86::r10, FieldObjectOffset));
							A.inc(x86::qword_ptr(x86::r11, MutationVersionOffset));
						}
						PushRax();
						break;
					}
				case EOpcode::IncField:
					{
						--NativeStackDepth;
						LoadFieldBinding(Index);
						if (!bBatchDirectFieldMutations)
						{
							A.mov(x86::r11, x86::qword_ptr(x86::r10, FieldObjectOffset));
						}
						// Keep the field address out of r10. JumpIfInstructionU32 uses
						// r10 as a scratch register to read the per-instruction kind; using
						// r10 for both silently redirected the final store into the kind table.
						A.mov(x86::rdx, x86::qword_ptr(x86::r10, FieldStorageOffset));
						// Check the increment before publishing it.  An in-memory add would
						// leave INT32_MIN behind when INT32_MAX overflows, whereas the VM
						// reports the error without mutating the field.
						A.mov(x86::eax, x86::dword_ptr(x86::rdx));
						A.add(x86::eax, 1);
						{
							const Label Unsigned = A.new_label();
							JumpIfInstructionU32(Index, Unsigned);
							A.jo(NewFailure(EJitRuntimeStatus::IntegerOverflow,
								Instruction.BytecodeOffset));
							A.bind(Unsigned);
						}
						A.mov(x86::dword_ptr(x86::rdx), x86::eax);
						if (bBatchDirectFieldMutations)
						{
							A.inc(x86::rbp);
						}
						else
						{
							A.inc(x86::qword_ptr(x86::r11, MutationVersionOffset));
						}
						break;
					}
				case EOpcode::Jmp:
					{
						BranchTo(Instruction);
						continue;
					}
				case EOpcode::JmpIfFalse:
					{
						PopRax();
						A.test(x86::eax, x86::eax);
						const Label Fallthrough = A.new_label();
						A.jnz(Fallthrough);
						BranchTo(Instruction);
						A.bind(Fallthrough);
						break;
					}
				case EOpcode::JmpIfLocalGeImm:
					{
						const Label Fallthrough = A.new_label();
						const Label Unsigned = A.new_label();
						JumpIfInstructionU32(Index, Unsigned);
						LoadLocalDword(Instruction.Operand0, x86::rax);
						A.cmp(x86::eax, Instruction.Immediate);
						A.jl(Fallthrough);
						BranchTo(Instruction);
						A.bind(Unsigned);
						LoadLocalDword(Instruction.Operand0, x86::rax);
						A.cmp(x86::eax,
							static_cast<std::uint32_t>(Instruction.Immediate));
						A.jb(Fallthrough);
						BranchTo(Instruction);
						A.bind(Fallthrough);
						break;
					}
				default:
					return nullptr;
				}
				// The plan is laid out in instruction order.  Falling through to the
				// immediately following label is equivalent to an explicit jump and
				// removes one taken branch per bytecode operation from hot typed loops.
				// Non-linear successors still use the explicit CFG edge below.
				if (Instruction.NextInstruction != Index + 1u
					&& !ContinueTo(Instruction))
				{
					return nullptr;
				}
			}

			for (const FFailureSite& Failure : Failures)
			{
				A.bind(Failure.Target);
				A.mov(x86::dword_ptr(x86::r15,
					static_cast<std::int32_t>(offsetof(
						FTypedLoopNativeContext, FailureOffset))),
					Failure.Offset);
				A.mov(x86::eax, static_cast<std::uint32_t>(Failure.Status));
				A.jmp(Epilogue);
			}
			A.bind(Epilogue);
			// Fast Array instructions can execute millions of times. Keep their
			// instrumentation in nonvolatile registers across helper calls and publish
			// it once at the kernel boundary; this preserves exact diagnostics without
			// introducing a loop-carried memory dependency for every element access.
			A.add(x86::qword_ptr(x86::r15,
				static_cast<std::int32_t>(offsetof(
					FTypedLoopNativeContext, ArrayGets))),
				x86::r13);
			A.add(x86::qword_ptr(x86::r15,
				static_cast<std::int32_t>(offsetof(
					FTypedLoopNativeContext, ArraySets))),
				x86::r14);
			if (SingleDirectMethodIndex != NoDirectMethod
				&& SingleDirectMethodIndex != MultipleDirectMethods)
			{
				A.mov(x86::r10, x86::qword_ptr(x86::r15,
					static_cast<std::int32_t>(offsetof(
						FTypedLoopNativeContext, Methods))));
				const std::size_t MethodByteOffset = SingleDirectMethodIndex
					* sizeof(FTypedLoopNativeMethodBinding)
					+ offsetof(FTypedLoopNativeMethodBinding, CallCount);
				if (MethodByteOffset != 0u)
				{
					A.add(x86::r10, static_cast<std::uint32_t>(MethodByteOffset));
				}
				A.add(x86::qword_ptr(x86::r10), x86::rbp);
			}
			if (bBatchDirectFieldMutations)
			{
				A.mov(x86::r10, x86::qword_ptr(x86::r15,
					static_cast<std::int32_t>(offsetof(
						FTypedLoopNativeContext, DirectFieldMutationObject))));
				A.add(x86::qword_ptr(x86::r10, MutationVersionOffset), x86::rbp);
			}
			A.add(x86::rsp, KernelStackBytes);
			A.pop(x86::rsi);
			A.pop(x86::rdi);
			A.pop(x86::rbp);
			A.pop(x86::r15);
			A.pop(x86::r14);
			A.pop(x86::r13);
			A.pop(x86::r12);
			A.pop(x86::rbx);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn)
				? reinterpret_cast<FTypedLoopI32Kernel>(Fn)
				: nullptr;
		}

		#include "VeroScriptJitNativeEmitWideLoop.inl"

		FConstI32Kernel EmitConst(JitRuntime& Runtime, const std::int32_t Imm)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			x86::Assembler A(&Code);
			// Win64: Out in rcx
			A.mov(x86::dword_ptr(x86::rcx), Imm);
			A.xor_(x86::eax, x86::eax);
			A.ret();
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FConstI32Kernel>(Fn) : nullptr;
		}
		#elif VEROSCRIPT_ASMJIT_X86
		#include "VeroScriptJitNativeEmitX86.inl"
		#elif VEROSCRIPT_ASMJIT_A64
		// Shared I32 CFG tag analysis (same helpers as x64 EmitLinear path).
		#include "VeroScriptJitNativeEmitLinearI32Shared.inl"
		#include "VeroScriptJitNativeEmitLinearF64.inl"
		#include "VeroScriptJitNativeEmitA64.inl"
		#endif // VEROSCRIPT_ASMJIT_X64 / X86 / A64
		#endif // VEROSCRIPT_HAS_ASMJIT
	}

	struct FArena::FState
	{
		#if VEROSCRIPT_HAS_ASMJIT
		asmjit::JitRuntime Runtime;
		#endif
	};

	FArena::FArena()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		State = new FState();
		#endif
	}

	FArena::~FArena()
	{
		delete State;
		State = nullptr;
	}

	void FArena::Reset()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		FState* const NewState = new FState();
		delete State;
		State = NewState;
		#else
		(void)State;
		#endif
	}

	std::size_t FArena::ReservedBytes() const
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? State->Runtime.allocator().statistics().reserved_size() : 0;
		#else
		return 0;
		#endif
	}

	bool FArena::IsAvailable() const
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State != nullptr;
		#else
		return false;
		#endif
	}

	#if VEROSCRIPT_HAS_ASMJIT
	#define VS_EMIT_BIN(Name, EmitCall) \
	FBinaryI32Kernel FArena::Name() \
	{                               \
		if (!State)                 \
		{                           \
			return nullptr;         \
		}                           \
		return EmitCall;            \
	}
	VS_EMIT_BIN(EmitAddI32, EmitAdd(State->Runtime))
	VS_EMIT_BIN(EmitSubI32, EmitSub(State->Runtime))
	VS_EMIT_BIN(EmitMulI32, EmitMul(State->Runtime))
	VS_EMIT_BIN(EmitDivI32, EmitDivOrMod(State->Runtime, true))
	VS_EMIT_BIN(EmitModI32, EmitDivOrMod(State->Runtime, false))
	VS_EMIT_BIN(EmitBitAndI32, EmitBit(State->Runtime, EBitOp::And))
	VS_EMIT_BIN(EmitBitOrI32, EmitBit(State->Runtime, EBitOp::Or))
	VS_EMIT_BIN(EmitBitXorI32, EmitBit(State->Runtime, EBitOp::Xor))
	VS_EMIT_BIN(EmitShlI32, EmitShift(State->Runtime, true))
	VS_EMIT_BIN(EmitShrI32, EmitShift(State->Runtime, false))
	VS_EMIT_BIN(EmitCmpEqI32, EmitCmp(State->Runtime, EOpcode::Eq))
	VS_EMIT_BIN(EmitCmpNeI32, EmitCmp(State->Runtime, EOpcode::Ne))
	VS_EMIT_BIN(EmitCmpLtI32, EmitCmp(State->Runtime, EOpcode::Lt))
	VS_EMIT_BIN(EmitCmpLeI32, EmitCmp(State->Runtime, EOpcode::Le))
	VS_EMIT_BIN(EmitCmpGtI32, EmitCmp(State->Runtime, EOpcode::Gt))
	VS_EMIT_BIN(EmitCmpGeI32, EmitCmp(State->Runtime, EOpcode::Ge))
	VS_EMIT_BIN(EmitAddU32, EmitU32Arith(State->Runtime, EOpcode::Add))
	VS_EMIT_BIN(EmitSubU32, EmitU32Arith(State->Runtime, EOpcode::Sub))
	VS_EMIT_BIN(EmitMulU32, EmitU32Arith(State->Runtime, EOpcode::Mul))
	VS_EMIT_BIN(EmitDivU32, EmitU32Arith(State->Runtime, EOpcode::Div))
	VS_EMIT_BIN(EmitModU32, EmitU32Arith(State->Runtime, EOpcode::Mod))
	VS_EMIT_BIN(EmitCmpEqU32, EmitCmpU32Op(State->Runtime, EOpcode::Eq))
	VS_EMIT_BIN(EmitCmpNeU32, EmitCmpU32Op(State->Runtime, EOpcode::Ne))
	VS_EMIT_BIN(EmitCmpLtU32, EmitCmpU32Op(State->Runtime, EOpcode::Lt))
	VS_EMIT_BIN(EmitCmpLeU32, EmitCmpU32Op(State->Runtime, EOpcode::Le))
	VS_EMIT_BIN(EmitCmpGtU32, EmitCmpU32Op(State->Runtime, EOpcode::Gt))
	VS_EMIT_BIN(EmitCmpGeU32, EmitCmpU32Op(State->Runtime, EOpcode::Ge))
	#undef VS_EMIT_BIN
	#else
	FBinaryI32Kernel FArena::EmitAddI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitSubI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitMulI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitDivI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitModI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitBitAndI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitBitOrI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitBitXorI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitShlI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitShrI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpEqI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpNeI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpLtI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpLeI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpGtI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpGeI32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitAddU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitSubU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitMulU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitDivU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitModU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpEqU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpNeU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpLtU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpLeU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpGtU32()
	{
		return nullptr;
	}
	FBinaryI32Kernel FArena::EmitCmpGeU32()
	{
		return nullptr;
	}
	#endif

	FUnaryI32Kernel FArena::EmitNegI32()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitNeg(State->Runtime) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FUnaryI32Kernel FArena::EmitBitNotI32()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitBitNot(State->Runtime) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitAddImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitUnaryAddSubImm(State->Runtime, Imm, true) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitSubImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitUnaryAddSubImm(State->Runtime, Imm, false) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitMulImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitMulImm(State->Runtime, Imm) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitDivImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitDivOrModImm(State->Runtime, Imm, true) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitModImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitDivOrModImm(State->Runtime, Imm, false) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitBitAndImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitBitImm(State->Runtime, Imm, EBitOp::And) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitBitOrImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitBitImm(State->Runtime, Imm, EBitOp::Or) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitBitXorImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitBitImm(State->Runtime, Imm, EBitOp::Xor) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitShlImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitShiftImm(State->Runtime, Imm, true) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitShrImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitShiftImm(State->Runtime, Imm, false) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitSubFromImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitImmMinusValue(State->Runtime, Imm) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitDivFromImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitDivOrModFromImm(State->Runtime, Imm, true) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32ImmKernel FArena::EmitModFromImmI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitDivOrModFromImm(State->Runtime, Imm, false) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FConstI32Kernel FArena::EmitConstI32(const std::int32_t Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitConst(State->Runtime, Imm) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FUnaryI32Kernel FArena::EmitNegU32()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitNegU32Kernel(State->Runtime) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FUnaryI32Kernel FArena::EmitBitNotU32()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		// Same bitwise not encoding as I32.
		return State ? EmitBitNot(State->Runtime) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FUnaryI32Kernel FArena::EmitNotBool()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitNotBoolKernel(State->Runtime) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FUnaryI32Kernel FArena::EmitIdentityI32()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitIdentityI32Kernel(State->Runtime) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FUnaryF64Kernel FArena::EmitIdentityF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitIdentityF64Kernel(State->Runtime) : nullptr;
		#else
		return nullptr;
		#endif
	}

	namespace
	{
		bool ReportKernelStatus(FExecutionContext* const Vm, const std::int32_t Status)
		{
			return Vm->JitHandleKernelFailure(Status);
		}

		void FillNativeEntry(FJitFunctionEntry& OutEntry, FJitNativeEntry Bridge, void* Kernel)
		{
			OutEntry = {};
			OutEntry.Entry = Bridge;
			OutEntry.NativeCode = Kernel;
			OutEntry.bNativeLeaf = true;
			OutEntry.Status = EJitCompileStatus::Ok;
			OutEntry.Kind = EJitEntryKind::NativeLeaf;
		}

		void FillNativeImmEntry(
			FJitFunctionEntry& OutEntry,
			FJitNativeEntry Bridge,
			void* Kernel,
			const std::int32_t Imm)
		{
			FillNativeEntry(OutEntry, Bridge, Kernel);
			OutEntry.PayloadI32 = Imm;
		}
	}

	bool BridgeBinaryI32(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FBinaryI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing binary kernel");
			return false;
		}
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
		std::int32_t Result = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(*Left, *Right, &Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result);
	}

	bool BridgeCmpI32(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FBinaryI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing cmp kernel");
			return false;
		}
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
		std::int32_t Result = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(*Left, *Right, &Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result != 0);
	}

	bool BridgeUnaryI32(void* const VmContext)
	{
		return BridgeUnaryImmI32(VmContext);
	}

	bool BridgeUnaryImmI32(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FUnaryI32ImmKernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing unary kernel");
			return false;
		}
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
		std::int32_t Result = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(*Value, &Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result);
	}

	bool BridgeConstI32(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FConstI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing const kernel");
			return false;
		}
		std::int32_t Result = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(&Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result);
	}

	bool TryBindBinaryI32(FArena& Arena, const EOpcode Op, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FBinaryI32Kernel Kernel = nullptr;
		switch (Op)
		{
		case EOpcode::Add:
			{
				Kernel = Arena.EmitAddI32();
				break;
			}
		case EOpcode::Sub:
			{
				Kernel = Arena.EmitSubI32();
				break;
			}
		case EOpcode::Mul:
			{
				Kernel = Arena.EmitMulI32();
				break;
			}
		case EOpcode::Div:
			{
				Kernel = Arena.EmitDivI32();
				break;
			}
		case EOpcode::Mod:
			{
				Kernel = Arena.EmitModI32();
				break;
			}
		case EOpcode::BitAnd:
			{
				Kernel = Arena.EmitBitAndI32();
				break;
			}
		case EOpcode::BitOr:
			{
				Kernel = Arena.EmitBitOrI32();
				break;
			}
		case EOpcode::BitXor:
			{
				Kernel = Arena.EmitBitXorI32();
				break;
			}
		case EOpcode::Shl:
			{
				Kernel = Arena.EmitShlI32();
				break;
			}
		case EOpcode::Shr:
			{
				Kernel = Arena.EmitShrI32();
				break;
			}
		default:
			return false;
		}
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeBinaryI32, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindCmpI32(FArena& Arena, const EOpcode Op, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FBinaryI32Kernel Kernel = nullptr;
		switch (Op)
		{
		case EOpcode::Eq:
			{
				Kernel = Arena.EmitCmpEqI32();
				break;
			}
		case EOpcode::Ne:
			{
				Kernel = Arena.EmitCmpNeI32();
				break;
			}
		case EOpcode::Lt:
			{
				Kernel = Arena.EmitCmpLtI32();
				break;
			}
		case EOpcode::Le:
			{
				Kernel = Arena.EmitCmpLeI32();
				break;
			}
		case EOpcode::Gt:
			{
				Kernel = Arena.EmitCmpGtI32();
				break;
			}
		case EOpcode::Ge:
			{
				Kernel = Arena.EmitCmpGeI32();
				break;
			}
		default:
			return false;
		}
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeCmpI32, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindNegI32(FArena& Arena, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32Kernel Kernel = Arena.EmitNegI32();
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeUnaryI32, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindBitNotI32(FArena& Arena, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32Kernel Kernel = Arena.EmitBitNotI32();
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeUnaryI32, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindConstI32(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FConstI32Kernel Kernel = Arena.EmitConstI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeConstI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0AddImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitAddImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0SubImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitSubImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0MulImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitMulImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0DivImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitDivImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0ModImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitModImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0BitAndImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitBitAndImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0BitOrImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitBitOrImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0BitXorImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitBitXorImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0ShlImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitShlImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindLocal0ShrImm(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitShrImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindImmSubLocal0(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitSubFromImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindImmDivLocal0(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitDivFromImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool TryBindImmModLocal0(FArena& Arena, const std::int32_t Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32ImmKernel Kernel = Arena.EmitModFromImmI32(Imm);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeUnaryImmI32, reinterpret_cast<void*>(Kernel), Imm);
		return true;
	}

	bool BridgeBinaryU32(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FBinaryI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing uint binary kernel");
			return false;
		}
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
		std::int32_t ResultBits = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(
			static_cast<std::int32_t>(*Left),
			static_cast<std::int32_t>(*Right),
				&ResultBits);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(static_cast<std::uint32_t>(ResultBits));
	}

	bool BridgeCmpU32(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FBinaryI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing uint cmp kernel");
			return false;
		}
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
		std::int32_t Result = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(
			static_cast<std::int32_t>(*Left),
			static_cast<std::int32_t>(*Right),
				&Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result != 0);
	}

	bool BridgeUnaryU32(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FUnaryI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing uint unary kernel");
			return false;
		}
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
		std::int32_t ResultBits = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(static_cast<std::int32_t>(*Value), &ResultBits);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(static_cast<std::uint32_t>(ResultBits));
	}

	bool BridgeNotBool(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FUnaryI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing not-bool kernel");
			return false;
		}
		if (Vm->GetPendingJitArgumentCount() != 1 || Vm->GetPendingJitArguments() == nullptr)
		{
			Vm->JitReportRuntimeError("JIT unary op expects 1 argument");
			return false;
		}
		const auto* const Value = std::get_if<bool>(&Vm->GetPendingJitArguments()[0]);
		if (!Value)
		{
			Vm->JitReportRuntimeError("JIT not expects bool argument");
			return false;
		}
		std::int32_t Result = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(*Value ? 1 : 0, &Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result != 0);
	}

	bool TryBindBinaryU32(FArena& Arena, const EOpcode Op, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FBinaryI32Kernel Kernel = nullptr;
		switch (Op)
		{
		case EOpcode::Add:
			{
				Kernel = Arena.EmitAddU32();
				break;
			}
		case EOpcode::Sub:
			{
				Kernel = Arena.EmitSubU32();
				break;
			}
		case EOpcode::Mul:
			{
				Kernel = Arena.EmitMulU32();
				break;
			}
		case EOpcode::Div:
			{
				Kernel = Arena.EmitDivU32();
				break;
			}
		case EOpcode::Mod:
			{
				Kernel = Arena.EmitModU32();
				break;
			}
		default:
			return false;
		}
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeBinaryU32, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindCmpU32(FArena& Arena, const EOpcode Op, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FBinaryI32Kernel Kernel = nullptr;
		switch (Op)
		{
		case EOpcode::Eq:
			{
				Kernel = Arena.EmitCmpEqU32();
				break;
			}
		case EOpcode::Ne:
			{
				Kernel = Arena.EmitCmpNeU32();
				break;
			}
		case EOpcode::Lt:
			{
				Kernel = Arena.EmitCmpLtU32();
				break;
			}
		case EOpcode::Le:
			{
				Kernel = Arena.EmitCmpLeU32();
				break;
			}
		case EOpcode::Gt:
			{
				Kernel = Arena.EmitCmpGtU32();
				break;
			}
		case EOpcode::Ge:
			{
				Kernel = Arena.EmitCmpGeU32();
				break;
			}
		default:
			return false;
		}
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeCmpU32, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindNegU32(FArena& Arena, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32Kernel Kernel = Arena.EmitNegU32();
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeUnaryU32, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindBitNotU32(FArena& Arena, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32Kernel Kernel = Arena.EmitBitNotU32();
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeUnaryU32, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindNotBool(FArena& Arena, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryI32Kernel Kernel = Arena.EmitNotBool();
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeNotBool, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool BridgeConstBool(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FConstI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing const-bool kernel");
			return false;
		}
		std::int32_t Result = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(&Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result != 0);
	}

	bool BridgeIdentity(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		if (Vm->GetPendingJitArgumentCount() != 1 || Vm->GetPendingJitArguments() == nullptr)
		{
			Vm->JitReportRuntimeError("JIT identity expects 1 argument");
			return false;
		}
		const FValue& Arg = Vm->GetPendingJitArguments()[0];
		if (const auto* IntValue = std::get_if<std::int32_t>(&Arg))
		{
			auto* const Kernel = reinterpret_cast<FUnaryI32Kernel>(Vm->GetPendingJitNativeCode());
			if (!Kernel)
			{
				return Vm->JitPushReturn(Arg);
			}
			std::int32_t Result = 0;
			Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
			if (!ReportKernelStatus(Vm, Kernel(*IntValue, &Result)))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}
		if (const auto* BoolValue = std::get_if<bool>(&Arg))
		{
			auto* const Kernel = reinterpret_cast<FUnaryI32Kernel>(Vm->GetPendingJitNativeCode());
			if (!Kernel)
			{
				return Vm->JitPushReturn(Arg);
			}
			std::int32_t Result = 0;
			Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
			if (!ReportKernelStatus(Vm, Kernel(*BoolValue ? 1 : 0, &Result)))
			{
				return false;
			}
			return Vm->JitPushReturn(Result != 0);
		}
		if (const auto* UIntValue = std::get_if<std::uint32_t>(&Arg))
		{
			auto* const Kernel = reinterpret_cast<FUnaryI32Kernel>(Vm->GetPendingJitNativeCode());
			if (!Kernel)
			{
				return Vm->JitPushReturn(Arg);
			}
			std::int32_t ResultBits = 0;
			Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
			if (!ReportKernelStatus(Vm, Kernel(static_cast<std::int32_t>(*UIntValue), &ResultBits)))
			{
				return false;
			}
			return Vm->JitPushReturn(static_cast<std::uint32_t>(ResultBits));
		}
		if (const auto* DoubleValue = std::get_if<double>(&Arg))
		{
			auto* const Kernel = reinterpret_cast<FUnaryF64Kernel>(Vm->GetPendingJitNativeCode());
			if (!Kernel)
			{
				return Vm->JitPushReturn(Arg);
			}
			double Result = 0.0;
			Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
			if (!ReportKernelStatus(Vm, Kernel(*DoubleValue, &Result)))
			{
				return false;
			}
			return Vm->JitPushReturn(Result);
		}
		return Vm->JitPushReturn(Arg);
	}

	bool TryBindConstBool(FArena& Arena, const bool Value, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FConstI32Kernel Kernel = Arena.EmitConstI32(Value ? 1 : 0);
		if (!Kernel)
		{
			return false;
		}
		FillNativeImmEntry(OutEntry, &BridgeConstBool, reinterpret_cast<void*>(Kernel), Value ? 1 : 0);
		return true;
	}

	bool TryBindIdentity(
		FArena& Arena,
		const FBytecodeFunction& Function,
		FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		const std::string_view Type = Function.ClosureTypeName;
		if (Type == "fn(int)->int" || Type == "fn(bool)->bool"
			|| Type == "fn(uint)->uint" || Type == "fn(unsigned int)->unsigned int")
		{
			FUnaryI32Kernel Kernel = Arena.EmitIdentityI32();
			if (!Kernel)
			{
				return false;
			}
			FillNativeEntry(OutEntry, &BridgeIdentity, reinterpret_cast<void*>(Kernel));
			return true;
		}
		if (Type == "fn(double)->double")
		{
			FUnaryF64Kernel Kernel = Arena.EmitIdentityF64();
			if (!Kernel)
			{
				return false;
			}
			FillNativeEntry(OutEntry, &BridgeIdentity, reinterpret_cast<void*>(Kernel));
			return true;
		}
		return false;
	}

	FBinaryF64Kernel FArena::EmitAddF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Arith(State->Runtime, EOpcode::Add) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FBinaryF64Kernel FArena::EmitSubF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Arith(State->Runtime, EOpcode::Sub) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FBinaryF64Kernel FArena::EmitMulF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Arith(State->Runtime, EOpcode::Mul) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FBinaryF64Kernel FArena::EmitDivF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Arith(State->Runtime, EOpcode::Div) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FCmpF64Kernel FArena::EmitCmpEqF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Cmp(State->Runtime, EOpcode::Eq) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FCmpF64Kernel FArena::EmitCmpNeF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Cmp(State->Runtime, EOpcode::Ne) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FCmpF64Kernel FArena::EmitCmpLtF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Cmp(State->Runtime, EOpcode::Lt) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FCmpF64Kernel FArena::EmitCmpLeF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Cmp(State->Runtime, EOpcode::Le) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FCmpF64Kernel FArena::EmitCmpGtF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Cmp(State->Runtime, EOpcode::Gt) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FCmpF64Kernel FArena::EmitCmpGeF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitF64Cmp(State->Runtime, EOpcode::Ge) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FUnaryF64Kernel FArena::EmitNegF64()
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitNegF64Kernel(State->Runtime) : nullptr;
		#else
		return nullptr;
		#endif
	}

	FConstF64Kernel FArena::EmitConstF64(const double Imm)
	{
		#if VEROSCRIPT_HAS_ASMJIT
		return State ? EmitConstF64Kernel(State->Runtime, Imm) : nullptr;
		#else
		(void)Imm;
		return nullptr;
		#endif
	}

	FLinearI32Kernel FArena::EmitLinearI32Cfg(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		const JitLowering::FPlan* const Plan)
	{
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (!State)
		{
			return nullptr;
		}
		JitLowering::FPlan LocalPlan;
		const JitLowering::FPlan* EffectivePlan = Plan;
		if (!EffectivePlan)
		{
			std::string Error;
			if (JitLowering::BuildScalarPlan(Module, Function, LocalPlan, &Error))
			{
				EffectivePlan = &LocalPlan;
			}
		}
		std::string Error;
		if (EffectivePlan
			&& JitBackend::ValidateScalarPlan(
				*EffectivePlan, JitBackend::EArchitecture::X64, &Error))
		{
			return EmitLinearI32CfgKernel(
				State->Runtime, Module, Function, EffectivePlan);
		}
		return EmitLinearI32CfgKernel(State->Runtime, Module, Function);
		#elif VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_A64
		(void)Plan;
		return State ? EmitLinearI32CfgA64Kernel(State->Runtime, Module, Function) : nullptr;
		#else
		(void)Module;
		(void)Function;
		(void)Plan;
		return nullptr;
		#endif
	}

	FLinearF64Kernel FArena::EmitLinearF64Cfg(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		const JitLowering::FPlan* const Plan)
	{
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (!State)
		{
			return nullptr;
		}
		JitLowering::FPlan LocalPlan;
		const JitLowering::FPlan* EffectivePlan = Plan;
		if (!EffectivePlan)
		{
			std::string Error;
			if (JitLowering::BuildScalarPlan(Module, Function, LocalPlan, &Error))
			{
				EffectivePlan = &LocalPlan;
			}
		}
		std::string Error;
		if (EffectivePlan
			&& JitBackend::ValidateScalarPlan(
				*EffectivePlan, JitBackend::EArchitecture::X64, &Error))
		{
			return EmitLinearF64CfgKernel(
				State->Runtime, Module, Function, EffectivePlan);
		}
		return EmitLinearF64CfgKernel(State->Runtime, Module, Function);
		#elif VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_A64
		(void)Plan;
		return State ? EmitLinearF64CfgA64Kernel(State->Runtime, Module, Function) : nullptr;
		#else
		(void)Module;
		(void)Function;
		(void)Plan;
		return nullptr;
		#endif
	}

	FLinearMixedKernel FArena::EmitLinearMixedCfg(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		const JitLowering::FPlan* const Plan)
	{
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (!State)
		{
			return nullptr;
		}
		JitLowering::FPlan LocalPlan;
		const JitLowering::FPlan* EffectivePlan = Plan;
		if (!EffectivePlan)
		{
			std::string Error;
			if (JitLowering::BuildScalarPlan(Module, Function, LocalPlan, &Error))
			{
				EffectivePlan = &LocalPlan;
			}
		}
		if (EffectivePlan && EffectivePlan->bScalarEligible
			&& !EffectivePlan->bRequiresVmOwnership)
		{
			return EmitLinearMixedCfgKernel(
				State->Runtime, Module, Function, EffectivePlan);
		}
		return EmitLinearMixedCfgKernel(State->Runtime, Module, Function);
		#else
		(void)Module;
		(void)Function;
		(void)Plan;
		return nullptr;
		#endif
	}

	FTypedLoopI32Kernel FArena::EmitTypedLoopI32(
		const JitLinear::FTypedLoopPlan& Plan)
	{
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		return State ? EmitTypedLoopI32Kernel(State->Runtime, Plan) : nullptr;
		#else
		(void)Plan;
		return nullptr;
		#endif
	}

	FTypedLoopI32Kernel FArena::EmitTypedLoopWide(const JitLinear::FTypedLoopPlan& Plan)
	{
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		return State ? EmitTypedLoopWideKernel(State->Runtime, Plan) : nullptr;
		#else
		(void)Plan;
		return nullptr;
		#endif
	}

	FPureI32ExpressionKernel FArena::EmitPureI32Expression(
		const std::vector<JitLinear::FTypedLoopInstruction>& Instructions)
	{
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		return State ? EmitPureI32ExpressionKernel(State->Runtime, Instructions) : nullptr;
		#else
		(void)Instructions;
		return nullptr;
		#endif
	}

	FFieldI32ExpressionKernel FArena::EmitFieldI32Expression(
		const std::vector<JitLinear::FTypedLoopInstruction>& Instructions,
		const std::uint8_t ArgumentCount)
	{
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		return State ? reinterpret_cast<FFieldI32ExpressionKernel>(
			EmitPureI32ExpressionKernel(State->Runtime, Instructions, true, ArgumentCount)) : nullptr;
		#else
		(void)Instructions;
		(void)ArgumentCount;
		return nullptr;
		#endif
	}

	bool BridgeBinaryF64(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FBinaryF64Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing f64 binary kernel");
			return false;
		}
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
		double Result = 0.0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(*Left, *Right, &Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result);
	}

	bool BridgeCmpF64(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FCmpF64Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing f64 cmp kernel");
			return false;
		}
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
		std::int32_t Result = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(*Left, *Right, &Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result != 0);
	}

	bool BridgeUnaryF64(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FUnaryF64Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing f64 unary kernel");
			return false;
		}
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
		double Result = 0.0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(*Value, &Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result);
	}

	bool BridgeConstF64(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FConstF64Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing f64 const kernel");
			return false;
		}
		double Result = 0.0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeLeaf);
		const std::int32_t Status = Kernel(&Result);
		if (!ReportKernelStatus(Vm, Status))
		{
			return false;
		}
		return Vm->JitPushReturn(Result);
	}

	bool BridgeLinearI32Cfg(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FLinearI32Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing linear kernel");
			return false;
		}
		const FBytecodeModule* const Module = Vm->DebugGetModule();
		const std::uint16_t FunctionIndex = Vm->GetPendingJitFunctionIndex();
		if (!Module || FunctionIndex >= Module->Functions.size())
		{
			Vm->JitReportRuntimeError("JIT linear native: invalid function");
			return false;
		}
		const auto& Function = Module->Functions[FunctionIndex];
		const auto LocalCount = std::max<std::size_t>(Function.LocalCount, Vm->GetPendingJitNativeLocalCount());
		const auto ArgCount = Vm->GetPendingJitArgumentCount();
		const FValue* const Args = Vm->GetPendingJitArguments();
		if (ArgCount != Function.ParamCount
			|| LocalCount < ArgCount
			|| (ArgCount && !Args))
		{
			Vm->JitReportRuntimeError("JIT linear native: argument count mismatch");
			return false;
		}
		const bool bUnsigned = Vm->GetPendingJitScalarAbi() == EJitScalarAbi::U32;
		constexpr std::size_t InlineArgumentSlots = 16;
		std::int32_t SignedArgumentScratch[InlineArgumentSlots];
		std::uint32_t UnsignedArgumentScratch[InlineArgumentSlots];
		JitScratch::TRawScratchLease<std::int32_t> SignedArgumentHeap(
			!bUnsigned && ArgCount > InlineArgumentSlots ? ArgCount : 0);
		JitScratch::TRawScratchLease<std::uint32_t> UnsignedArgumentHeap(
			bUnsigned && ArgCount > InlineArgumentSlots ? ArgCount : 0);
		std::int32_t* const SignedArguments = ArgCount > InlineArgumentSlots
			? SignedArgumentHeap.data()
			: SignedArgumentScratch;
		std::uint32_t* const UnsignedArguments = ArgCount > InlineArgumentSlots
			? UnsignedArgumentHeap.data()
			: UnsignedArgumentScratch;
		for (std::uint8_t I = 0; I < ArgCount; ++I)
		{
			std::int32_t SignedValue = 0;
			std::uint32_t UnsignedValue = 0;
			if (const auto* EnumValue = std::get_if<FEnumValue>(&Args[I]))
			{
				// Overload resolution may bind an enum to a function taking its exact
				// integral underlying type. At that point the callee ABI is scalar and
				// the enum identity is no longer part of the parameter value.
				SignedValue = static_cast<std::int32_t>(EnumValue->Value);
				UnsignedValue = static_cast<std::uint32_t>(EnumValue->Value);
			}
			else if (const auto* Int8Value = std::get_if<std::int8_t>(&Args[I]))
			{
				SignedValue = *Int8Value;
				UnsignedValue = static_cast<std::uint32_t>(static_cast<std::int32_t>(*Int8Value));
			}
			else if (const auto* Uint8Value = std::get_if<std::uint8_t>(&Args[I]))
			{
				SignedValue = *Uint8Value;
				UnsignedValue = *Uint8Value;
			}
			else if (const auto* Int16Value = std::get_if<std::int16_t>(&Args[I]))
			{
				SignedValue = *Int16Value;
				UnsignedValue = static_cast<std::uint32_t>(static_cast<std::int32_t>(*Int16Value));
			}
			else if (const auto* Uint16Value = std::get_if<std::uint16_t>(&Args[I]))
			{
				SignedValue = *Uint16Value;
				UnsignedValue = *Uint16Value;
			}
			else if (const auto* IntValue = std::get_if<std::int32_t>(&Args[I]))
			{
				SignedValue = *IntValue;
				UnsignedValue = static_cast<std::uint32_t>(*IntValue);
			}
			else if (const auto* BoolValue = std::get_if<bool>(&Args[I]))
			{
				SignedValue = *BoolValue ? 1 : 0;
				UnsignedValue = *BoolValue ? 1u : 0u;
			}
			else if (const auto* UintValue = std::get_if<std::uint32_t>(&Args[I]))
			{
				std::memcpy(&SignedValue, UintValue, sizeof(SignedValue));
				UnsignedValue = *UintValue;
			}
			else
			{
				Vm->JitReportRuntimeError("JIT linear native: expected int/bool/uint args");
				return false;
			}
			if (bUnsigned)
			{
				UnsignedArguments[I] = UnsignedValue;
			}
			else
			{
				SignedArguments[I] = SignedValue;
			}
		}
		if (!Function.bIsMethod)
		{
			bool bHandled = false;
			if (bUnsigned)
			{
				std::uint32_t FastOut = 0;
				const bool bOk = Vm->JitInvokeFunctionU32Fast(
					FunctionIndex,
					UnsignedArguments,
					ArgCount,
					Function.bReturnsVoid ? nullptr : &FastOut,
					bHandled, true);
				if (bHandled)
				{
					if (!bOk)
					{
						return false;
					}
					return Function.bReturnsVoid
						? true
						: Vm->JitPushReturn(FastOut);
				}
			}
			else
			{
				std::int32_t FastOut = 0;
				const bool bOk = Vm->JitInvokeFunctionI32Fast(
					FunctionIndex,
					SignedArguments,
					ArgCount,
					Function.bReturnsVoid ? nullptr : &FastOut,
					bHandled, true);
				if (bHandled)
				{
					if (!bOk)
					{
						return false;
					}
					if (Function.bReturnsVoid)
					{
						return true;
					}
					return Vm->GetPendingJitScalarReturnKind()
						== EJitScalarValueKind::Bool
						? Vm->JitPushReturn(FastOut != 0)
						: Vm->JitPushReturn(FastOut);
				}
			}
		}
		std::int32_t LocalScratch[64];
		JitScratch::TRawScratchLease<std::int32_t> LocalHeap(
			LocalCount > 64 ? LocalCount : 0);
		std::int32_t* const Locals = LocalCount > 64
			? LocalHeap.data()
			: LocalScratch;
		for (std::uint8_t I = 0; I < ArgCount; ++I)
		{
			if (bUnsigned)
			{
				std::memcpy(&Locals[I], &UnsignedArguments[I], sizeof(Locals[I]));
			}
			else
			{
				Locals[I] = SignedArguments[I];
			}
		}
		std::fill_n(Locals + ArgCount, LocalCount - ArgCount, 0);
		const std::uint16_t StackCapacityHint = Vm->GetPendingJitStackCapacityHint();
		const std::size_t OpStackSlots = StackCapacityHint != 0
			? StackCapacityHint
			: (Function.Code.size() + 16u > 64u ? Function.Code.size() + 16u : 64u);
		std::int32_t OpScratch[64];
		JitScratch::TRawScratchLease<std::int32_t> OpHeap(
			OpStackSlots > 64 ? OpStackSlots : 0);
		std::int32_t* const OpStack = OpStackSlots > 64
			? OpHeap.data()
			: OpScratch;
		// The validated native CFG writes every operand slot before reading it.
		// Unlike language locals, the transient operand stack has no observable
		// default value, so clearing all 64 inline slots on every call is wasted.
		std::int32_t Out = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeCfg);
		const std::int32_t Status = Kernel(
			Vm,
			Locals,
			OpStack,
			Function.bReturnsVoid ? nullptr : &Out,
			nullptr);
		if (Vm->WasExitRequested())
		{
			return !Vm->HasError();
		}
		if (Status != kStatusOk)
		{
			if (Vm->HasError())
			{
				return false;
			}
			if (!ReportKernelStatus(Vm, Status))
			{
				return false;
			}
		}
		if (Function.bReturnsVoid)
		{
			return true;
		}
		// Bool returns are still 0/1 int bits; the entry caches the parsed signature.
		if (Vm->GetPendingJitScalarReturnKind() == EJitScalarValueKind::Bool)
		{
			return Vm->JitPushReturn(Out != 0);
		}
		if (bUnsigned)
		{
			return Vm->JitPushReturn(static_cast<std::uint32_t>(Out));
		}
		return Vm->JitPushReturn(Out);
	}

	bool TryBindBinaryF64(FArena& Arena, const EOpcode Op, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FBinaryF64Kernel Kernel = nullptr;
		switch (Op)
		{
		case EOpcode::Add:
			{
				Kernel = Arena.EmitAddF64();
				break;
			}
		case EOpcode::Sub:
			{
				Kernel = Arena.EmitSubF64();
				break;
			}
		case EOpcode::Mul:
			{
				Kernel = Arena.EmitMulF64();
				break;
			}
		case EOpcode::Div:
			{
				Kernel = Arena.EmitDivF64();
				break;
			}
		default:
			return false;
		}
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeBinaryF64, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindCmpF64(FArena& Arena, const EOpcode Op, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FCmpF64Kernel Kernel = nullptr;
		switch (Op)
		{
		case EOpcode::Eq:
			{
				Kernel = Arena.EmitCmpEqF64();
				break;
			}
		case EOpcode::Ne:
			{
				Kernel = Arena.EmitCmpNeF64();
				break;
			}
		case EOpcode::Lt:
			{
				Kernel = Arena.EmitCmpLtF64();
				break;
			}
		case EOpcode::Le:
			{
				Kernel = Arena.EmitCmpLeF64();
				break;
			}
		case EOpcode::Gt:
			{
				Kernel = Arena.EmitCmpGtF64();
				break;
			}
		case EOpcode::Ge:
			{
				Kernel = Arena.EmitCmpGeF64();
				break;
			}
		default:
			return false;
		}
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeCmpF64, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindNegF64(FArena& Arena, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FUnaryF64Kernel Kernel = Arena.EmitNegF64();
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeUnaryF64, reinterpret_cast<void*>(Kernel));
		return true;
	}

	bool TryBindConstF64(FArena& Arena, const double Imm, FJitFunctionEntry& OutEntry)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FConstF64Kernel Kernel = Arena.EmitConstF64(Imm);
		if (!Kernel)
		{
			return false;
		}
		OutEntry = {};
		OutEntry.Entry = &BridgeConstF64;
		OutEntry.NativeCode = reinterpret_cast<void*>(Kernel);
		OutEntry.PayloadF64 = Imm;
		OutEntry.bNativeLeaf = true;
		OutEntry.Status = EJitCompileStatus::Ok;
		return true;
	}

	bool TryBindLinearI32Cfg(
		FArena& Arena,
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FJitFunctionEntry& OutEntry,
		const JitLowering::FPlan* const Plan)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FLinearI32Kernel Kernel = Arena.EmitLinearI32Cfg(Module, Function, Plan);
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeLinearI32Cfg, reinterpret_cast<void*>(Kernel));
		OutEntry.Kind = EJitEntryKind::NativeCfg;
		OutEntry.NativeLocalCount = Function.LocalCount;
		OutEntry.ScalarAbi =
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
			InferI32FunctionModeLocal(Module, Function)
				== EI32FunctionModeLocal::Unsigned
		#else
			Function.ClosureTypeName.find("uint") != std::string::npos
				|| Function.ClosureTypeName.find("unsigned") != std::string::npos
		#endif
			? EJitScalarAbi::U32
			: EJitScalarAbi::I32;
		if (Plan && Plan->bScalarEligible)
		{
			OutEntry.StackCapacityHint = Plan->MaxStackSlots;
			OutEntry.BoxedStackCapacityHint = Plan->MaxBoxedStackSlots;
		}
		return true;
	}

	bool BridgeLinearF64Cfg(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FLinearF64Kernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing f64 linear kernel");
			return false;
		}
		const FBytecodeModule* const Module = Vm->DebugGetModule();
		const std::uint16_t FunctionIndex = Vm->GetPendingJitFunctionIndex();
		if (!Module || FunctionIndex >= Module->Functions.size())
		{
			Vm->JitReportRuntimeError("JIT f64 linear native: invalid function");
			return false;
		}
		const auto& Function = Module->Functions[FunctionIndex];
		const auto LocalCount = std::max<std::size_t>(Function.LocalCount, Vm->GetPendingJitNativeLocalCount());
		const auto ArgCount = Vm->GetPendingJitArgumentCount();
		const FValue* const Args = Vm->GetPendingJitArguments();
		if (ArgCount != Function.ParamCount
			|| LocalCount < ArgCount
			|| (ArgCount && !Args))
		{
			Vm->JitReportRuntimeError("JIT f64 linear native: argument count mismatch");
			return false;
		}
		constexpr std::size_t InlineArgumentSlots = 16;
		double ArgumentScratch[InlineArgumentSlots];
		JitScratch::TRawScratchLease<double> ArgumentHeap(
			ArgCount > InlineArgumentSlots ? ArgCount : 0);
		double* const Arguments = ArgCount > InlineArgumentSlots
			? ArgumentHeap.data()
			: ArgumentScratch;
		for (std::uint8_t I = 0; I < ArgCount; ++I)
		{
			const auto* const D = std::get_if<double>(&Args[I]);
			if (!D)
			{
				Vm->JitReportRuntimeError("JIT f64 linear native: expected double args");
				return false;
			}
			Arguments[I] = *D;
		}
		if (!Function.bIsMethod)
		{
			bool bHandled = false;
			double FastOut = 0.0;
			const bool bOk = Vm->JitInvokeFunctionF64Fast(
				FunctionIndex,
				Arguments,
				ArgCount,
				Function.bReturnsVoid ? nullptr : &FastOut,
				bHandled, true);
			if (bHandled)
			{
				if (!bOk)
				{
					return false;
				}
				return Function.bReturnsVoid
					? true
					: Vm->JitPushReturn(FastOut);
			}
		}
		double LocalScratch[64];
		JitScratch::TRawScratchLease<double> LocalHeap(
			LocalCount > 64 ? LocalCount : 0);
		double* const Locals = LocalCount > 64
			? LocalHeap.data()
			: LocalScratch;
		std::copy_n(Arguments, ArgCount, Locals);
		std::fill_n(Locals + ArgCount, LocalCount - ArgCount, 0.0);
		const std::uint16_t StackCapacityHint = Vm->GetPendingJitStackCapacityHint();
		const std::size_t OpStackSlots = StackCapacityHint != 0
			? StackCapacityHint
			: (Function.Code.size() + 16u > 64u ? Function.Code.size() + 16u : 64u);
		double OpScratch[64];
		JitScratch::TRawScratchLease<double> OpHeap(
			OpStackSlots > 64 ? OpStackSlots : 0);
		double* const OpStack = OpStackSlots > 64
			? OpHeap.data()
			: OpScratch;
		// Native CFG validation guarantees write-before-read for operand slots.
		double Out = 0.0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeCfg);
		const std::int32_t Status = Kernel(
			Vm,
			Locals,
			OpStack,
			Function.bReturnsVoid ? nullptr : &Out,
			nullptr);
		if (Vm->WasExitRequested())
		{
			return !Vm->HasError();
		}
		if (Status != kStatusOk)
		{
			if (Vm->HasError())
			{
				return false;
			}
			if (!ReportKernelStatus(Vm, Status))
			{
				return false;
			}
		}
		if (Function.bReturnsVoid)
		{
			return true;
		}
		return Vm->JitPushReturn(Out);
	}

	bool TryBindLinearF64Cfg(
		FArena& Arena,
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FJitFunctionEntry& OutEntry,
		const JitLowering::FPlan* const Plan)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FLinearF64Kernel Kernel = Arena.EmitLinearF64Cfg(Module, Function, Plan);
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeLinearF64Cfg, reinterpret_cast<void*>(Kernel));
		OutEntry.Kind = EJitEntryKind::NativeCfg;
		OutEntry.NativeLocalCount = Function.LocalCount;
		OutEntry.ScalarAbi = EJitScalarAbi::F64;
		if (Plan && Plan->bScalarEligible)
		{
			OutEntry.StackCapacityHint = Plan->MaxStackSlots;
			OutEntry.BoxedStackCapacityHint = Plan->MaxBoxedStackSlots;
		}
		return true;
	}

	bool BridgeLinearMixedCfg(void* const VmContext)
	{
		auto* const Vm = static_cast<FExecutionContext*>(VmContext);
		auto* const Kernel = reinterpret_cast<FLinearMixedKernel>(Vm->GetPendingJitNativeCode());
		if (!Kernel)
		{
			Vm->JitReportRuntimeError("JIT native: missing mixed linear kernel");
			return false;
		}
		const FBytecodeModule* const Module = Vm->DebugGetModule();
		const std::uint16_t FunctionIndex = Vm->GetPendingJitFunctionIndex();
		if (!Module || FunctionIndex >= Module->Functions.size())
		{
			Vm->JitReportRuntimeError("JIT mixed native: invalid function");
			return false;
		}
		const auto& Function = Module->Functions[FunctionIndex];
		const auto LocalCount = std::max<std::size_t>(Function.LocalCount, Vm->GetPendingJitNativeLocalCount());
		std::uint64_t LocalScratch[64];
		JitScratch::TRawScratchLease<std::uint64_t> LocalHeap(
			LocalCount > 64 ? LocalCount : 0);
		std::uint64_t* const Locals = LocalCount > 64
			? LocalHeap.data()
			: LocalScratch;
		const std::uint16_t StackCapacityHint = Vm->GetPendingJitStackCapacityHint();
		const std::size_t OpStackSlots = StackCapacityHint != 0
			? StackCapacityHint
			: (Function.Code.size() + 16u > 64u ? Function.Code.size() + 16u : 64u);
		std::uint64_t OpScratch[64];
		JitScratch::TRawScratchLease<std::uint64_t> OpHeap(
			OpStackSlots > 64 ? OpStackSlots : 0);
		std::uint64_t* const OpStack = OpStackSlots > 64
			? OpHeap.data()
			: OpScratch;
		// Native CFG validation guarantees write-before-read for operand slots.
		const auto ArgCount = Vm->GetPendingJitArgumentCount();
		const FValue* const Args = Vm->GetPendingJitArguments();
		if (ArgCount != Function.ParamCount
			|| LocalCount < ArgCount
			|| (ArgCount && !Args))
		{
			Vm->JitReportRuntimeError("JIT mixed native: argument count mismatch");
			return false;
		}
		std::fill_n(
			Locals + ArgCount,
			LocalCount - ArgCount,
			std::uint64_t{0});
		for (std::uint8_t I = 0; I < ArgCount; ++I)
		{
			if (const auto* EnumValue = std::get_if<FEnumValue>(&Args[I]))
			{
				// An enum can bind to a function taking its underlying integral type.
				// The resolved callee no longer needs the enum's nominal identity.
				Locals[I] = static_cast<std::uint64_t>(EnumValue->Value);
			}
			else if (const auto* Int8Value = std::get_if<std::int8_t>(&Args[I]))
			{
				Locals[I] = static_cast<std::uint32_t>(static_cast<std::int32_t>(*Int8Value));
			}
			else if (const auto* Uint8Value = std::get_if<std::uint8_t>(&Args[I]))
			{
				Locals[I] = *Uint8Value;
			}
			else if (const auto* Int16Value = std::get_if<std::int16_t>(&Args[I]))
			{
				Locals[I] = static_cast<std::uint32_t>(static_cast<std::int32_t>(*Int16Value));
			}
			else if (const auto* Uint16Value = std::get_if<std::uint16_t>(&Args[I]))
			{
				Locals[I] = *Uint16Value;
			}
			else if (const auto* IntValue = std::get_if<std::int32_t>(&Args[I]))
			{
				Locals[I] = static_cast<std::uint32_t>(*IntValue);
			}
			else if (const auto* UintValue = std::get_if<std::uint32_t>(&Args[I]))
			{
				Locals[I] = *UintValue;
			}
			else if (const auto* BoolValue = std::get_if<bool>(&Args[I]))
			{
				Locals[I] = *BoolValue ? 1u : 0u;
			}
			else if (const auto* DoubleValue = std::get_if<double>(&Args[I]))
			{
				std::memcpy(&Locals[I], DoubleValue, sizeof(std::uint64_t));
			}
			else if (const auto* FloatValue = std::get_if<float>(&Args[I]))
			{
				std::uint32_t Bits = 0;
				std::memcpy(&Bits, FloatValue, sizeof(Bits));
				Locals[I] = Bits;
			}
			else if (const auto* Int64Value = std::get_if<std::int64_t>(&Args[I]))
			{
				Locals[I] = static_cast<std::uint64_t>(*Int64Value);
			}
			else if (const auto* Uint64Value = std::get_if<std::uint64_t>(&Args[I]))
			{
				Locals[I] = *Uint64Value;
			}
			else
			{
				Vm->JitReportRuntimeError("JIT mixed native: expected scalar args");
				return false;
			}
		}
		std::uint64_t Out = 0;
		Vm->RecordNativeExecution(EJitExecutionKind::NativeCfg);
		const std::int32_t Status = Kernel(
			Vm,
			Locals,
			OpStack,
			Function.bReturnsVoid ? nullptr : &Out,
			nullptr);
		if (Vm->WasExitRequested())
		{
			return !Vm->HasError();
		}
		if (Status != kStatusOk)
		{
			if (Vm->HasError())
			{
				return false;
			}
			if (!ReportKernelStatus(Vm, Status))
			{
				return false;
			}
		}
		if (Function.bReturnsVoid)
		{
			return true;
		}
		switch (Vm->GetPendingJitScalarReturnKind())
		{
		case EJitScalarValueKind::Bool:
			return Vm->JitPushReturn((Out & 1u) != 0);
		case EJitScalarValueKind::I8:
			return Vm->JitPushReturn(
				static_cast<std::int8_t>(static_cast<std::uint8_t>(Out)));
		case EJitScalarValueKind::U8:
			return Vm->JitPushReturn(static_cast<std::uint8_t>(Out));
		case EJitScalarValueKind::I16:
			return Vm->JitPushReturn(
				static_cast<std::int16_t>(static_cast<std::uint16_t>(Out)));
		case EJitScalarValueKind::U16:
			return Vm->JitPushReturn(static_cast<std::uint16_t>(Out));
		case EJitScalarValueKind::I32:
			return Vm->JitPushReturn(static_cast<std::int32_t>(Out));
		case EJitScalarValueKind::U32:
			return Vm->JitPushReturn(static_cast<std::uint32_t>(Out));
		case EJitScalarValueKind::I64:
			return Vm->JitPushReturn(static_cast<std::int64_t>(Out));
		case EJitScalarValueKind::U64:
			return Vm->JitPushReturn(Out);
		case EJitScalarValueKind::F32:
			{
				const std::uint32_t Bits = static_cast<std::uint32_t>(Out);
				float F = 0.0f;
				std::memcpy(&F, &Bits, sizeof(F));
				return Vm->JitPushReturn(F);
			}
		case EJitScalarValueKind::F64:
			{
				double D = 0.0;
				std::memcpy(&D, &Out, sizeof(D));
				return Vm->JitPushReturn(D);
			}
		case EJitScalarValueKind::None:
		case EJitScalarValueKind::Void:
			{
				break;
			}
		default:
			{
				break;
			}
		}
		Vm->JitReportRuntimeError("JIT mixed native: unsupported return type");
		return false;
	}

	bool TryBindLinearMixedCfg(
		FArena& Arena,
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		FJitFunctionEntry& OutEntry,
		const JitLowering::FPlan* const Plan)
	{
		if (!Arena.IsAvailable())
		{
			return false;
		}
		FLinearMixedKernel Kernel = Arena.EmitLinearMixedCfg(Module, Function, Plan);
		if (!Kernel)
		{
			return false;
		}
		FillNativeEntry(OutEntry, &BridgeLinearMixedCfg, reinterpret_cast<void*>(Kernel));
		OutEntry.Kind = EJitEntryKind::NativeCfg;
		OutEntry.NativeLocalCount = Function.LocalCount;
		OutEntry.ScalarAbi = EJitScalarAbi::Mixed;
		if (Plan && Plan->bScalarEligible)
		{
			OutEntry.StackCapacityHint = Plan->MaxStackSlots;
			OutEntry.BoxedStackCapacityHint = Plan->MaxBoxedStackSlots;
		}
		return true;
	}
}
