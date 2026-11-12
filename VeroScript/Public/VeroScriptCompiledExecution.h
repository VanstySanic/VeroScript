// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitBackend.h"
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace VeroScript
{
	inline constexpr std::uint64_t CompiledInstructionServiceCapability = 1ull << 4;
	struct FCompiledInstruction
	{
		std::uint32_t Offset = 0;
		std::uint32_t Target = 0;
		EOpcode Opcode = EOpcode::Nop;
	};

	// Core owns language values, frames, calls and errors. Generated native code
	// owns the function's control flow; it never enters an interpreter Run loop.
	class ICompiledFunctionServices
	{
	public:
		virtual ~ICompiledFunctionServices() = default;
		virtual bool Begin(FJitCallContext& Context) noexcept = 0;
		// Optional primitive operand blocks. Core keeps locals, accounting and
		// diagnostics authoritative. Unsupported values resume ordinary instruction
		// services without replay; arbitrary host checks decline before entry.
		virtual bool EnableScalarBlocks() noexcept
		{
			return false;
		}
		// -1 failure/control transfer, 0 fallthrough, 1 taken branch, 2 return.
		virtual int Execute(std::size_t InstructionOffset) noexcept = 0;
		// A straight-line block still checks and charges every instruction. Native
		// backends retain branch ownership while amortizing the service boundary.
		virtual int ExecuteBlock(const FCompiledInstruction* Instructions, std::size_t Count) noexcept
		{
			if (!Instructions || Count == 0) return -1;
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				const int Result = Execute(Instructions[Index].Offset);
				if (Result != 0) return Result;
			}
			return 0;
		}
		virtual bool Finish(FJitCallContext& Context) noexcept = 0;
	};
	using FCompiledFunctionEntry = int (*)(void*);

	// Header-only decoding is shared by host generators and independent backends.
	// Unsupported semantics are rejected for the whole function before entry.
	inline bool BuildCompiledFunctionPlan(const FBytecodeFunction& Function,
	    std::vector<FCompiledInstruction>& Out, std::string& Reason, FExecutionCheck Check = {})
	{
		Out.clear();
		if (!Function.ExceptionRegions.empty() || Function.Code.empty() || Function.Code.size() > 65536)
		{
			Reason = "empty body, exception regions or a body beyond the bounded native planning size require the VM";
			return false;
		}
		for (std::size_t Ip = 0; Ip < Function.Code.size();)
		{
			if ((Out.size() & 255u) == 0 && !Check.Poll())
			{
				Reason = "compilation stopped by host";
				return false;
			}
			const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
			switch (Op)
			{
			case EOpcode::Nop:
			case EOpcode::ConstI32:
			case EOpcode::ConstF64:
			case EOpcode::ConstBool:
			case EOpcode::ConstStr:
			case EOpcode::ConstNull:
			case EOpcode::LoadLocal:
			case EOpcode::StoreLocal:
			case EOpcode::LoadGlobal:
			case EOpcode::StoreGlobal:
			case EOpcode::LoadField:
			case EOpcode::LoadFieldKeep:
			case EOpcode::StoreField:
			case EOpcode::LoadThis:
			case EOpcode::LoadThisField:
			case EOpcode::StoreThisField:
			case EOpcode::IncField:
			case EOpcode::MakeLocalRef:
			case EOpcode::MakeGlobalRef:
			case EOpcode::MakeThisFieldRef:
			case EOpcode::MakeFieldRef:
			case EOpcode::CloneStruct:
			case EOpcode::BeginLocalStatic:
			case EOpcode::CommitLocalStatic:
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
			case EOpcode::BitNot:
			case EOpcode::BitAnd:
			case EOpcode::BitOr:
			case EOpcode::BitXor:
			case EOpcode::Shl:
			case EOpcode::Shr:
			case EOpcode::Jmp:
			case EOpcode::JmpIfFalse:
			case EOpcode::Dup:
			case EOpcode::Pop:
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
			case EOpcode::JmpIfLocalGeImm:
			case EOpcode::Call:
			case EOpcode::CallMethod:
			case EOpcode::CallBoundMethod:
			case EOpcode::CallBuiltin:
			case EOpcode::CallNative:
			case EOpcode::Return:
				{
					break;
				}
			default:
				{
					Reason = "opcode " + std::to_string(static_cast<unsigned>(Op)) + " requires the VM";
					return false;
				}
			}
			const auto Size = Bytecode::InstructionSize(Op);
			if (!Size || Size > Function.Code.size() - Ip)
			{
				Reason = "truncated instruction";
				return false;
			}
			FCompiledInstruction Instruction{static_cast<std::uint32_t>(Ip), 0, Op};
			if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse || Op == EOpcode::JmpIfLocalGeImm || Op == EOpcode::BeginLocalStatic)
			{
				const auto Target = static_cast<std::int64_t>(Ip + Size) + Bytecode::ReadI32LE(Function.Code.data() + Ip + Size - 4);
				if (Target < 0 || static_cast<std::uint64_t>(Target) >= Function.Code.size())
				{
					Reason = "branch target requires the VM";
					return false;
				}
				Instruction.Target = static_cast<std::uint32_t>(Target);
			}
			Out.push_back(Instruction);
			Ip += Size;
		}
		for (const auto& I : Out)
		{
			if ((I.Opcode == EOpcode::Jmp || I.Opcode == EOpcode::JmpIfFalse || I.Opcode == EOpcode::JmpIfLocalGeImm || I.Opcode == EOpcode::BeginLocalStatic) &&
			    !std::binary_search(Out.begin(), Out.end(), FCompiledInstruction{I.Target, 0, EOpcode::Nop},
			        [](const auto& A, const auto& B)
			{
				return A.Offset < B.Offset;
			}))
			{
				Reason = "branch target is not an instruction boundary";
				return false;
			}
		}
		if (Out.back().Opcode != EOpcode::Return && Out.back().Opcode != EOpcode::Jmp)
		{
			Reason = "implicit fallthrough requires the VM";
			return false;
		}
		Reason.clear();
		return true;
	}

	inline EJitInvocationResult InvokeCompiledFunction(FJitCallContext& Context,
	    const FCompiledFunctionEntry Entry, std::size_t* ActualEntries = nullptr,
	    EJitExecutionKind Kind = EJitExecutionKind::Aot) noexcept
	{
		if (!Context.Services || !Entry) return EJitInvocationResult::NotEntered;
		auto* Services = Context.Services->GetCompiledFunctionServices();
		if (!Services) return EJitInvocationResult::NotEntered;
		Context.MarkEntered();
		const bool Began = Services->Begin(Context);
		int Result = -1;
		try
		{
			if (Began)
			{
				Context.ExecutionKind = Kind;
				if (ActualEntries)
				{
					++*ActualEntries;
				}
				Result = Entry(Services);
			}
		}
		catch (...)
		{
			Context.Services->ReportError("native compiled entry raised an unexpected exception");
		}
		const bool Finished = Services->Finish(Context);
		if (Context.Services->HasPendingControlTransfer()) return EJitInvocationResult::ControlTransfer;
		if (!Began || Result != 2 || !Finished || Context.Services->HasError())
		{
			Context.Services->ReportError("compiled function did not complete normally");
			return EJitInvocationResult::Failed;
		}
		return EJitInvocationResult::Returned;
	}
}
