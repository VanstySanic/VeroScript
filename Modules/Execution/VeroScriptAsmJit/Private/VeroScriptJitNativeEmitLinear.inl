// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included inside VeroScript::AsmJit::JitNative anonymous asmjit namespace.
// Emits I32/bool scalar CFG kernels (Call/globals/Abs/Min/Max/casts via helpers or inline).

#include "VeroScriptJitNativeEmitLinearI32Shared.inl"

struct FX64I32ClosedRecurrence
{
	std::uint16_t SourceSlot = 0;
	std::uint16_t AccumulatorSlot = 0;
	std::uint16_t CounterSlot = 0;
	std::uint64_t IterationCount = 0;
	std::int32_t Addend = 0;
	std::uint32_t FailureInstructionOffset = InvalidJitInstructionOffset;
};

bool TryMatchX64I32CounterModuloAccumulation(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	std::int32_t& OutValue)
{
	// Exact closed form for a pure counted remainder accumulation:
	//   accumulator = (accumulator + counter % D) % M
	// The admitted non-negative range proves every checked source operation and
	// lets modulo associativity replace the loop without changing diagnostics.
	OutValue = 0;
	if (Function.bIsMethod || Function.bReturnsVoid || Function.bReturnsRef
		|| Function.ParamCount != 0u || Function.LocalCount < 2u
		|| !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function))
	{
		return false;
	}
	const auto& Code = Function.Code;
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
	const auto ReadConstant = [&](const std::size_t ConstIp,
		std::int32_t& OutConstant)
	{
		const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + ConstIp + 1u);
		if (Index >= Module.Constants.size()
			|| Module.Constants[Index].Kind != EConstantKind::I32)
		{
			return false;
		}
		OutConstant = Module.Constants[Index].I32;
		return true;
	};
	std::int32_t InitialAccumulator = 0;
	std::int32_t Start = 0;
	std::int32_t TermModulus = 0;
	std::int32_t AccumulatorModulus = 0;
	const std::size_t AccumulatorConstIp = Ip;
	if (!Consume(EOpcode::ConstI32, Ip)
		|| !ReadConstant(AccumulatorConstIp, InitialAccumulator))
	{
		return false;
	}
	const std::size_t AccumulatorStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal, Ip))
	{
		return false;
	}
	const std::uint16_t AccumulatorSlot =
		Bytecode::ReadU16LE(Code.data() + AccumulatorStoreIp + 1u);
	const std::size_t CounterConstIp = Ip;
	if (!Consume(EOpcode::ConstI32, Ip)
		|| !ReadConstant(CounterConstIp, Start))
	{
		return false;
	}
	const std::size_t CounterStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal, Ip))
	{
		return false;
	}
	const std::uint16_t CounterSlot =
		Bytecode::ReadU16LE(Code.data() + CounterStoreIp + 1u);
	if (AccumulatorSlot >= Function.LocalCount || CounterSlot >= Function.LocalCount
		|| AccumulatorSlot == CounterSlot)
	{
		return false;
	}
	const std::size_t HeaderIp = Ip;
	if (!Consume(EOpcode::JmpIfLocalGeImm, Ip)
		|| Bytecode::ReadU16LE(Code.data() + HeaderIp + 1u) != CounterSlot)
	{
		return false;
	}
	const std::int32_t Limit = Bytecode::ReadI32LE(Code.data() + HeaderIp + 3u);
	std::size_t ExitIp = 0;
	if (!IsJumpTargetInRangeLocal(Ip,
			Bytecode::ReadI32LE(Code.data() + HeaderIp + 7u), Code.size(), ExitIp))
	{
		return false;
	}
	const std::size_t LoadAccumulatorIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + LoadAccumulatorIp + 1u) != AccumulatorSlot)
	{
		return false;
	}
	const std::size_t LoadCounterIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + LoadCounterIp + 1u) != CounterSlot)
	{
		return false;
	}
	const std::size_t TermModulusIp = Ip;
	if (!Consume(EOpcode::ConstI32, Ip)
		|| !ReadConstant(TermModulusIp, TermModulus)
		|| !Consume(EOpcode::Mod, Ip)
		|| !Consume(EOpcode::Add, Ip))
	{
		return false;
	}
	const std::size_t AccumulatorModulusIp = Ip;
	if (!Consume(EOpcode::ConstI32, Ip)
		|| !ReadConstant(AccumulatorModulusIp, AccumulatorModulus)
		|| !Consume(EOpcode::Mod, Ip))
	{
		return false;
	}
	const std::size_t ResultStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + ResultStoreIp + 1u) != AccumulatorSlot)
	{
		return false;
	}
	const std::size_t IncrementIp = Ip;
	if (!Consume(EOpcode::IncLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + IncrementIp + 1u) != CounterSlot)
	{
		return false;
	}
	const std::size_t BackJumpIp = Ip;
	if (!Consume(EOpcode::Jmp, Ip))
	{
		return false;
	}
	std::size_t BackTarget = 0;
	if (!IsJumpTargetInRangeLocal(Ip,
			Bytecode::ReadI32LE(Code.data() + BackJumpIp + 1u), Code.size(), BackTarget)
		|| BackTarget != HeaderIp || ExitIp != Ip)
	{
		return false;
	}
	const std::size_t ReturnLoadIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + ReturnLoadIp + 1u) != AccumulatorSlot
		|| !Consume(EOpcode::Return, Ip) || Ip != Code.size())
	{
		return false;
	}

	if (Start >= Limit)
	{
		OutValue = InitialAccumulator;
		return true;
	}
	if (Start < 0 || TermModulus <= 0 || AccumulatorModulus <= 0
		|| InitialAccumulator < 0 || InitialAccumulator >= AccumulatorModulus)
	{
		return false;
	}
	const std::int64_t MaxTerm = std::min<std::int64_t>(
		static_cast<std::int64_t>(Limit) - 1, TermModulus - 1);
	if (static_cast<std::int64_t>(AccumulatorModulus - 1) + MaxTerm
		> std::numeric_limits<std::int32_t>::max())
	{
		return false;
	}
	const auto SumRemainders = [](const std::uint64_t End,
		const std::uint64_t Divisor)
	{
		const std::uint64_t Cycles = End / Divisor;
		const std::uint64_t Tail = End % Divisor;
		std::uint64_t Left = Divisor;
		std::uint64_t Right = Divisor - 1u;
		if ((Left & 1u) == 0u)
		{
			Left /= 2u;
		}
		else
		{
			Right /= 2u;
		}
		return Cycles * Left * Right + Tail * (Tail - 1u) / 2u;
	};
	const std::uint64_t Divisor = static_cast<std::uint64_t>(TermModulus);
	const std::uint64_t Sum = SumRemainders(static_cast<std::uint64_t>(Limit), Divisor)
		- SumRemainders(static_cast<std::uint64_t>(Start), Divisor);
	OutValue = static_cast<std::int32_t>(
		(static_cast<std::uint64_t>(InitialAccumulator)
			+ Sum % static_cast<std::uint64_t>(AccumulatorModulus))
		% static_cast<std::uint64_t>(AccumulatorModulus));
	return true;
}

bool TryMatchX64I32ConstantLocalRecurrence(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	std::int32_t& OutValue)
{
	// Exact closed form for a complete, side-effect-free counted loop whose only
	// loop-carried values are an i32 counter and an accumulator changed by a
	// constant. This also covers a tiny additive leaf after scalar inlining.
	OutValue = 0;
	if (Function.bIsMethod || Function.bReturnsVoid || Function.bReturnsRef
		|| Function.ParamCount != 0u || Function.LocalCount < 2u
		|| !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function))
	{
		return false;
	}
	const auto& Code = Function.Code;
	std::size_t Ip = 0;
	const auto SkipNops = [&]()
	{
		while (Ip < Code.size() && static_cast<EOpcode>(Code[Ip]) == EOpcode::Nop)
		{
			++Ip;
		}
	};
	const auto Consume = [&](const EOpcode Expected)
	{
		SkipNops();
		if (Ip >= Code.size() || static_cast<EOpcode>(Code[Ip]) != Expected)
		{
			return false;
		}
		const std::size_t Size = Bytecode::InstructionSize(Expected);
		if (Size == 0u || Size > Code.size() - Ip)
		{
			return false;
		}
		Ip += Size;
		return true;
	};
	const auto ReadI32Constant = [&](const std::size_t ConstIp,
		std::int32_t& OutConstant)
	{
		if (ConstIp + Bytecode::InstructionSize(EOpcode::ConstI32) > Code.size())
		{
			return false;
		}
		const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + ConstIp + 1u);
		if (Index >= Module.Constants.size()
			|| Module.Constants[Index].Kind != EConstantKind::I32)
		{
			return false;
		}
		OutConstant = Module.Constants[Index].I32;
		return true;
	};

	SkipNops();
	const std::size_t InitialIp = Ip;
	std::int32_t Initial = 0;
	if (!Consume(EOpcode::ConstI32) || !ReadI32Constant(InitialIp, Initial))
	{
		return false;
	}
	SkipNops();
	const std::size_t AccumulatorStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal))
	{
		return false;
	}
	const std::uint16_t AccumulatorSlot =
		Bytecode::ReadU16LE(Code.data() + AccumulatorStoreIp + 1u);
	SkipNops();
	const std::size_t StartIp = Ip;
	std::int32_t Start = 0;
	if (!Consume(EOpcode::ConstI32) || !ReadI32Constant(StartIp, Start))
	{
		return false;
	}
	SkipNops();
	const std::size_t CounterStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal))
	{
		return false;
	}
	const std::uint16_t CounterSlot =
		Bytecode::ReadU16LE(Code.data() + CounterStoreIp + 1u);
	if (AccumulatorSlot >= Function.LocalCount || CounterSlot >= Function.LocalCount
		|| AccumulatorSlot == CounterSlot)
	{
		return false;
	}

	SkipNops();
	const std::size_t HeaderIp = Ip;
	if (!Consume(EOpcode::JmpIfLocalGeImm)
		|| Bytecode::ReadU16LE(Code.data() + HeaderIp + 1u) != CounterSlot)
	{
		return false;
	}
	const std::int32_t Limit = Bytecode::ReadI32LE(Code.data() + HeaderIp + 3u);
	std::size_t ExitIp = 0;
	if (!IsJumpTargetInRangeLocal(
			HeaderIp + Bytecode::InstructionSize(EOpcode::JmpIfLocalGeImm),
			Bytecode::ReadI32LE(Code.data() + HeaderIp + 7u), Code.size(), ExitIp))
	{
		return false;
	}

	SkipNops();
	std::int64_t Delta = 0;
	const EOpcode Update = Ip < Code.size()
		? static_cast<EOpcode>(Code[Ip])
		: EOpcode::Nop;
	if (Update == EOpcode::IncLocal)
	{
		if (Bytecode::ReadU16LE(Code.data() + Ip + 1u) != AccumulatorSlot
			|| !Consume(EOpcode::IncLocal))
		{
			return false;
		}
		Delta = 1;
	}
	else if (Update == EOpcode::AddLocalImm)
	{
		if (Bytecode::ReadU16LE(Code.data() + Ip + 1u) != AccumulatorSlot)
		{
			return false;
		}
		Delta = Bytecode::ReadI32LE(Code.data() + Ip + 3u);
		if (!Consume(EOpcode::AddLocalImm))
		{
			return false;
		}
	}
	else
	{
		const std::size_t LoadAccumulatorIp = Ip;
		if (!Consume(EOpcode::LoadLocal)
			|| Bytecode::ReadU16LE(Code.data() + LoadAccumulatorIp + 1u)
				!= AccumulatorSlot)
		{
			return false;
		}
		SkipNops();
		const std::size_t DeltaIp = Ip;
		std::int32_t EncodedDelta = 0;
		if (!Consume(EOpcode::ConstI32)
			|| !ReadI32Constant(DeltaIp, EncodedDelta))
		{
			return false;
		}
		SkipNops();
		const EOpcode Arithmetic = Ip < Code.size()
			? static_cast<EOpcode>(Code[Ip])
			: EOpcode::Nop;
		if (Arithmetic != EOpcode::Add && Arithmetic != EOpcode::Sub)
		{
			return false;
		}
		if (!Consume(Arithmetic))
		{
			return false;
		}
		Delta = Arithmetic == EOpcode::Sub
			? -static_cast<std::int64_t>(EncodedDelta)
			: EncodedDelta;
		SkipNops();
		const std::size_t ResultStoreIp = Ip;
		if (!Consume(EOpcode::StoreLocal)
			|| Bytecode::ReadU16LE(Code.data() + ResultStoreIp + 1u)
				!= AccumulatorSlot)
		{
			return false;
		}
	}

	SkipNops();
	const std::size_t CounterUpdateIp = Ip;
	if (!Consume(EOpcode::IncLocal)
		|| Bytecode::ReadU16LE(Code.data() + CounterUpdateIp + 1u) != CounterSlot)
	{
		return false;
	}
	SkipNops();
	const std::size_t BackJumpIp = Ip;
	if (!Consume(EOpcode::Jmp))
	{
		return false;
	}
	std::size_t BackTarget = 0;
	if (!IsJumpTargetInRangeLocal(
			BackJumpIp + Bytecode::InstructionSize(EOpcode::Jmp),
			Bytecode::ReadI32LE(Code.data() + BackJumpIp + 1u), Code.size(), BackTarget)
		|| BackTarget != HeaderIp || ExitIp != Ip)
	{
		return false;
	}
	SkipNops();
	const std::size_t ReturnLoadIp = Ip;
	if (!Consume(EOpcode::LoadLocal)
		|| Bytecode::ReadU16LE(Code.data() + ReturnLoadIp + 1u) != AccumulatorSlot
		|| !Consume(EOpcode::Return))
	{
		return false;
	}
	SkipNops();
	if (Ip != Code.size())
	{
		return false;
	}

	const std::uint64_t Count = Start < Limit
		? static_cast<std::uint64_t>(static_cast<std::int64_t>(Limit) - Start)
		: 0u;
	if ((Delta > 0 && Count > static_cast<std::uint64_t>(
			(static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())
				- Initial) / Delta))
		|| (Delta < 0 && Count > static_cast<std::uint64_t>(
			(static_cast<std::int64_t>(Initial)
				- std::numeric_limits<std::int32_t>::min()) / -Delta)))
	{
		return false;
	}
	OutValue = static_cast<std::int32_t>(static_cast<std::int64_t>(Initial)
		+ static_cast<std::int64_t>(Count) * Delta);
	return true;
}

bool TryMatchX64I32ConstantCallRecurrence(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	std::int32_t& OutValue)
{
	// Pure unary additive callees are already legal scalar-inline candidates.
	// Recognize the surrounding canonical counted recurrence before expansion so
	// it can use the same exact closed-form proof as an equivalent local update.
	OutValue = 0;
	if (Function.bIsMethod || Function.bReturnsVoid || Function.bReturnsRef
		|| Function.ParamCount != 0u || Function.LocalCount < 2u
		|| !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function))
	{
		return false;
	}
	const auto& Code = Function.Code;
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
	const auto ReadConstant = [&](const std::size_t ConstIp,
		std::int32_t& OutConstant)
	{
		const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + ConstIp + 1u);
		if (Index >= Module.Constants.size()
			|| Module.Constants[Index].Kind != EConstantKind::I32)
		{
			return false;
		}
		OutConstant = Module.Constants[Index].I32;
		return true;
	};
	std::int32_t Initial = 0;
	std::int32_t Start = 0;
	const std::size_t InitialIp = Ip;
	if (!Consume(EOpcode::ConstI32, Ip) || !ReadConstant(InitialIp, Initial))
	{
		return false;
	}
	const std::size_t AccumulatorStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal, Ip))
	{
		return false;
	}
	const std::uint16_t AccumulatorSlot =
		Bytecode::ReadU16LE(Code.data() + AccumulatorStoreIp + 1u);
	const std::size_t StartIp = Ip;
	if (!Consume(EOpcode::ConstI32, Ip) || !ReadConstant(StartIp, Start))
	{
		return false;
	}
	const std::size_t CounterStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal, Ip))
	{
		return false;
	}
	const std::uint16_t CounterSlot =
		Bytecode::ReadU16LE(Code.data() + CounterStoreIp + 1u);
	if (AccumulatorSlot >= Function.LocalCount || CounterSlot >= Function.LocalCount
		|| AccumulatorSlot == CounterSlot)
	{
		return false;
	}
	const std::size_t HeaderIp = Ip;
	if (!Consume(EOpcode::JmpIfLocalGeImm, Ip)
		|| Bytecode::ReadU16LE(Code.data() + HeaderIp + 1u) != CounterSlot)
	{
		return false;
	}
	const std::int32_t Limit = Bytecode::ReadI32LE(Code.data() + HeaderIp + 3u);
	std::size_t ExitIp = 0;
	if (!IsJumpTargetInRangeLocal(Ip,
			Bytecode::ReadI32LE(Code.data() + HeaderIp + 7u), Code.size(), ExitIp))
	{
		return false;
	}
	const std::size_t LoadIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + LoadIp + 1u) != AccumulatorSlot)
	{
		return false;
	}
	const std::size_t CallIp = Ip;
	if (!Consume(EOpcode::Call, Ip) || Code[CallIp + 3u] != 1u)
	{
		return false;
	}
	const std::uint16_t CalleeIndex = Bytecode::ReadU16LE(Code.data() + CallIp + 1u);
	if (CalleeIndex >= Module.Functions.size())
	{
		return false;
	}
	const std::size_t ResultStoreIp = Ip;
	if (!Consume(EOpcode::StoreLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + ResultStoreIp + 1u) != AccumulatorSlot)
	{
		return false;
	}
	const std::size_t IncrementIp = Ip;
	if (!Consume(EOpcode::IncLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + IncrementIp + 1u) != CounterSlot)
	{
		return false;
	}
	const std::size_t BackJumpIp = Ip;
	if (!Consume(EOpcode::Jmp, Ip))
	{
		return false;
	}
	std::size_t BackTarget = 0;
	if (!IsJumpTargetInRangeLocal(Ip,
			Bytecode::ReadI32LE(Code.data() + BackJumpIp + 1u), Code.size(), BackTarget)
		|| BackTarget != HeaderIp || ExitIp != Ip)
	{
		return false;
	}
	const std::size_t ReturnLoadIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + ReturnLoadIp + 1u) != AccumulatorSlot
		|| !Consume(EOpcode::Return, Ip) || Ip != Code.size())
	{
		return false;
	}

	const FBytecodeFunction& Callee = Module.Functions[CalleeIndex];
	if (Callee.bIsMethod || Callee.bReturnsVoid || Callee.bReturnsRef
		|| Callee.ParamCount != 1u || Callee.LocalCount != 1u
		|| !Callee.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Callee))
	{
		return false;
	}
	const auto& CalleeCode = Callee.Code;
	if (CalleeCode.size() != 8u
		|| static_cast<EOpcode>(CalleeCode[0]) != EOpcode::LoadLocal
		|| Bytecode::ReadU16LE(CalleeCode.data() + 1u) != 0u
		|| static_cast<EOpcode>(CalleeCode[3]) != EOpcode::ConstI32
		|| (static_cast<EOpcode>(CalleeCode[6]) != EOpcode::Add
			&& static_cast<EOpcode>(CalleeCode[6]) != EOpcode::Sub)
		|| static_cast<EOpcode>(CalleeCode[7]) != EOpcode::Return)
	{
		return false;
	}
	const std::uint16_t DeltaIndex = Bytecode::ReadU16LE(CalleeCode.data() + 4u);
	if (DeltaIndex >= Module.Constants.size()
		|| Module.Constants[DeltaIndex].Kind != EConstantKind::I32)
	{
		return false;
	}
	std::int64_t Delta = Module.Constants[DeltaIndex].I32;
	if (static_cast<EOpcode>(CalleeCode[6]) == EOpcode::Sub)
	{
		Delta = -Delta;
	}
	const std::uint64_t Count = Start < Limit
		? static_cast<std::uint64_t>(static_cast<std::int64_t>(Limit) - Start)
		: 0u;
	if ((Delta > 0 && Count > static_cast<std::uint64_t>(
			(static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())
				- Initial) / Delta))
		|| (Delta < 0 && Count > static_cast<std::uint64_t>(
			(static_cast<std::int64_t>(Initial)
				- std::numeric_limits<std::int32_t>::min()) / -Delta)))
	{
		return false;
	}
	const std::int64_t Final = static_cast<std::int64_t>(Initial)
		+ static_cast<std::int64_t>(Count) * Delta;
	OutValue = static_cast<std::int32_t>(Final);
	return true;
}

struct FX64I32BinarySelfRecurrence
{
	std::int32_t BaseLimit = 0;
	std::int32_t FirstDelta = 0;
	std::int32_t SecondDelta = 0;
};

struct FX64I32EuclideanTailRecurrence
{
	std::uint32_t ModInstructionOffset = InvalidJitInstructionOffset;
	std::uint32_t CallInstructionOffset = InvalidJitInstructionOffset;
};

// Consecutive Euclidean remainders decrease at least as quickly as adjacent
// Fibonacci numbers. Sixty-four is therefore a conservative bound for the full
// call chain over non-negative signed 32-bit operands (including the terminal
// invocation), and leaves ample room above the exact worst case.
constexpr std::uint64_t kMaxNonNegativeI32EuclideanInvocations = 64u;

bool TryMatchX64I32EuclideanTailRecurrence(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::uint16_t SelfFunctionIndex,
	const std::vector<std::optional<FI32CfgTagState>>& Entries,
	FX64I32EuclideanTailRecurrence& Out)
{
	// Exact, side-effect-free Euclidean tail recurrence:
	//   if (right == 0) return left;
	//   tail-call self(right, left % right);
	// Replacing only this complete function preserves checked remainder errors,
	// recursion-depth accounting and the observable recursive-call count while
	// avoiding a native/VM frame transition for every Euclidean step.
	Out = {};
	if (SelfFunctionIndex == 0xFFFFu || Function.bIsMethod
		|| Function.bReturnsVoid || Function.bReturnsRef
		|| Function.ParamCount != 2u || Function.LocalCount != 2u
		|| !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function)
		|| Function.ClosureTypeName != "fn(int,int)->int"
		|| Entries.empty() || !Entries[0] || Entries[0]->Locals.size() != 2u
		|| Entries[0]->Locals[0] != kI32MixI32
		|| Entries[0]->Locals[1] != kI32MixI32)
	{
		return false;
	}
	const auto& Code = Function.Code;
	std::size_t Ip = 0;
	const auto Consume = [&](const EOpcode Expected, std::size_t& InOutIp)
	{
		if (InOutIp >= Code.size()
			|| static_cast<EOpcode>(Code[InOutIp]) != Expected)
		{
			return false;
		}
		const std::size_t Size = Bytecode::InstructionSize(Expected);
		if (Size == 0u || Size > Code.size() - InOutIp)
		{
			return false;
		}
		InOutIp += Size;
		return true;
	};
	const std::size_t ConditionLoadIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + ConditionLoadIp + 1u) != 1u)
	{
		return false;
	}
	const std::size_t ZeroIp = Ip;
	if (!Consume(EOpcode::ConstI32, Ip))
	{
		return false;
	}
	const std::uint16_t ZeroIndex = Bytecode::ReadU16LE(Code.data() + ZeroIp + 1u);
	if (ZeroIndex >= Module.Constants.size()
		|| Module.Constants[ZeroIndex].Kind != EConstantKind::I32
		|| Module.Constants[ZeroIndex].I32 != 0
		|| !Consume(EOpcode::Eq, Ip))
	{
		return false;
	}
	const std::size_t BranchIp = Ip;
	if (!Consume(EOpcode::JmpIfFalse, Ip))
	{
		return false;
	}
	std::size_t RecursiveBodyIp = 0;
	if (!IsJumpTargetInRangeLocal(
			Ip, Bytecode::ReadI32LE(Code.data() + BranchIp + 1u),
			Code.size(), RecursiveBodyIp))
	{
		return false;
	}
	const std::size_t BaseLoadIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + BaseLoadIp + 1u) != 0u
		|| !Consume(EOpcode::Return, Ip) || Ip != RecursiveBodyIp)
	{
		return false;
	}
	const std::size_t RightArgumentIp = Ip;
	const std::size_t LeftModuloIp = Ip + Bytecode::InstructionSize(EOpcode::LoadLocal);
	const std::size_t RightModuloIp = LeftModuloIp
		+ Bytecode::InstructionSize(EOpcode::LoadLocal);
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + RightArgumentIp + 1u) != 1u
		|| !Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + LeftModuloIp + 1u) != 0u
		|| !Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + RightModuloIp + 1u) != 1u)
	{
		return false;
	}
	Out.ModInstructionOffset = static_cast<std::uint32_t>(Ip);
	if (!Consume(EOpcode::Mod, Ip))
	{
		return false;
	}
	Out.CallInstructionOffset = static_cast<std::uint32_t>(Ip);
	if (!Consume(EOpcode::Call, Ip)
		|| Bytecode::ReadU16LE(Code.data() + Out.CallInstructionOffset + 1u)
			!= SelfFunctionIndex
		|| Code[Out.CallInstructionOffset + 3u] != 2u
		|| !Consume(EOpcode::Return, Ip) || Ip != Code.size())
	{
		return false;
	}
	return true;
}

bool TryMatchX64I32BinarySelfRecurrence(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::uint16_t SelfFunctionIndex,
	const std::vector<std::optional<FI32CfgTagState>>& Entries,
	FX64I32BinarySelfRecurrence& Out)
{
	// Exact pure scalar shape:
	//   if (n < base) return n;
	//   the second branch calls self(n - first) + self(n - second);
	//
	// A compact internal ABI can preserve every recursive edge, checked integer
	// operation and depth transition without constructing a full locals/operand
	// kernel frame for each call. Requiring the complete bytecode shape prevents
	// side effects, exception regions or externally observable intermediate state
	// from being elided.
	Out = {};
	if (SelfFunctionIndex == 0xFFFFu || Function.bIsMethod
		|| Function.bReturnsVoid || Function.bReturnsRef
		|| Function.ParamCount != 1u
		|| Function.LocalCount != 1u || !Function.ExceptionRegions.empty()
		|| BytecodeFunctionHasRefParams(Function)
		|| Entries.empty() || !Entries[0]
		|| Entries[0]->Locals.size() != 1u
		|| Entries[0]->Locals[0] != kI32MixI32
		|| Function.ClosureTypeName != "fn(int)->int")
	{
		return false;
	}
	const auto& Code = Function.Code;
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
	const auto ReadI32Constant = [&](const std::size_t ConstIp,
		std::int32_t& OutValue)
	{
		if (ConstIp + Bytecode::InstructionSize(EOpcode::ConstI32) > Code.size())
		{
			return false;
		}
		const std::uint16_t Index = Bytecode::ReadU16LE(Code.data() + ConstIp + 1);
		if (Index >= Module.Constants.size()
			|| Module.Constants[Index].Kind != EConstantKind::I32)
		{
			return false;
		}
		OutValue = Module.Constants[Index].I32;
		return true;
	};

	const std::size_t HeaderIp = Ip;
	if (!Consume(EOpcode::JmpIfLocalGeImm, Ip)
		|| Bytecode::ReadU16LE(Code.data() + HeaderIp + 1) != 0u)
	{
		return false;
	}
	Out.BaseLimit = Bytecode::ReadI32LE(Code.data() + HeaderIp + 3);
	std::size_t RecursiveBodyIp = 0;
	if (!IsJumpTargetInRangeLocal(
			Ip,
			Bytecode::ReadI32LE(Code.data() + HeaderIp + 7),
			Code.size(),
			RecursiveBodyIp))
	{
		return false;
	}
	const std::size_t BaseLoadIp = Ip;
	if (!Consume(EOpcode::LoadLocal, Ip)
		|| Bytecode::ReadU16LE(Code.data() + BaseLoadIp + 1) != 0u
		|| !Consume(EOpcode::Return, Ip)
		|| Ip != RecursiveBodyIp)
	{
		return false;
	}
	const auto ConsumeRecursiveArgument = [&](std::size_t& InOutIp,
		std::int32_t& OutDelta)
	{
		const std::size_t LoadIp = InOutIp;
		if (!Consume(EOpcode::LoadLocal, InOutIp)
			|| Bytecode::ReadU16LE(Code.data() + LoadIp + 1) != 0u)
		{
			return false;
		}
		const std::size_t ConstantIp = InOutIp;
		if (!Consume(EOpcode::ConstI32, InOutIp)
			|| !ReadI32Constant(ConstantIp, OutDelta)
			|| !Consume(EOpcode::Sub, InOutIp))
		{
			return false;
		}
		const std::size_t CallIp = InOutIp;
		return Consume(EOpcode::Call, InOutIp)
			&& Bytecode::ReadU16LE(Code.data() + CallIp + 1) == SelfFunctionIndex
			&& Code[CallIp + 3] == 1u;
	};
	if (!ConsumeRecursiveArgument(Ip, Out.FirstDelta)
		|| !ConsumeRecursiveArgument(Ip, Out.SecondDelta)
		|| !Consume(EOpcode::Add, Ip)
		|| !Consume(EOpcode::Return, Ip)
		|| Ip != Code.size())
	{
		return false;
	}
	return true;
}

bool TryMatchX64I32ClosedRecurrence(
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function,
	const std::vector<std::optional<FI32CfgTagState>>& Entries,
	FX64I32ClosedRecurrence& Out)
{
	Out = {};
	const auto& Code = Function.Code;
	if (Function.bReturnsVoid || Function.ParamCount == 0 || Code.empty())
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
	if (Out.SourceSlot >= Function.ParamCount || Entries.empty() || !Entries[0]
		|| Out.SourceSlot >= Entries[0]->Locals.size()
		|| Entries[0]->Locals[Out.SourceSlot] != kI32MixI32)
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
	if (!Consume(EOpcode::ConstI32, Ip))
	{
		return false;
	}
	const std::uint16_t StartConstantIndex =
		Bytecode::ReadU16LE(Code.data() + CounterConstantIp + 1);
	if (StartConstantIndex >= Module.Constants.size())
	{
		return false;
	}
	const FBytecodeConstant& StartConstant = Module.Constants[StartConstantIndex];
	if (StartConstant.Kind != EConstantKind::I32)
	{
		return false;
	}
	const std::int32_t SignedStart = StartConstant.I32;
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
	const std::int32_t EncodedLimit = Bytecode::ReadI32LE(Code.data() + HeaderIp + 3);
	std::size_t ExitIp = 0;
	if (!IsJumpTargetInRangeLocal(
			Ip, Bytecode::ReadI32LE(Code.data() + HeaderIp + 7), Code.size(), ExitIp))
	{
		return false;
	}
	if (HeaderIp >= Entries.size() || !Entries[HeaderIp]
		|| Out.AccumulatorSlot >= Entries[HeaderIp]->Locals.size()
		|| Out.CounterSlot >= Entries[HeaderIp]->Locals.size()
		|| Entries[HeaderIp]->Locals[Out.AccumulatorSlot] != kI32MixI32
		|| Entries[HeaderIp]->Locals[Out.CounterSlot] != kI32MixI32)
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
	Out.Addend = AccumulatorUpdate == EOpcode::IncLocal
		? 1
		: Bytecode::ReadI32LE(Code.data() + Ip + 3);
	Out.FailureInstructionOffset = static_cast<std::uint32_t>(Ip);
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

	Out.IterationCount = SignedStart < EncodedLimit
		? static_cast<std::uint64_t>(
			static_cast<std::int64_t>(EncodedLimit) - SignedStart)
		: 0u;
	return true;
}

FLinearI32Kernel EmitLinearI32CfgKernel(
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
	std::vector<std::optional<FI32CfgTagState>> Entries;
	if (!AnalyzeI32BoolCfgTags(Module, Function, Entries))
	{
		return nullptr;
	}
	const bool bUnsigned =
		InferI32FunctionModeLocal(Module, Function)
		== EI32FunctionModeLocal::Unsigned;
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
	std::vector<bool> JumpTargets(CodeSize, false);
	if (!JumpTargets.empty())
	{
		JumpTargets[0] = true;
	}
	for (std::size_t ScanIp = 0; ScanIp < CodeSize;)
	{
		const EOpcode ScanOp = static_cast<EOpcode>(Bytes[ScanIp]);
		const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
		if (ScanSize == 0 || ScanSize > CodeSize - ScanIp)
		{
			return nullptr;
		}
		std::size_t Target = 0;
		if ((ScanOp == EOpcode::Jmp || ScanOp == EOpcode::JmpIfFalse)
			&& IsJumpTargetInRangeLocal(ScanIp + ScanSize,
				Bytecode::ReadI32LE(Bytes.data() + ScanIp + 1u), CodeSize, Target))
		{
			JumpTargets[Target] = true;
		}
		else if (ScanOp == EOpcode::JmpIfLocalGeImm
			&& IsJumpTargetInRangeLocal(ScanIp + ScanSize,
				Bytecode::ReadI32LE(Bytes.data() + ScanIp + 7u), CodeSize, Target))
		{
			JumpTargets[Target] = true;
		}
		ScanIp += ScanSize;
	}
	Label Epilogue = A.new_label();
	Label NativeEntry = A.new_label();
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
	A.bind(NativeEntry);

	std::uint16_t SelfFunctionIndex = 0xFFFFu;
	for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
	{
		if (&Module.Functions[Index] == &Function)
		{
			SelfFunctionIndex = static_cast<std::uint16_t>(Index);
			break;
		}
	}
	std::size_t SelfStackSlots = Plan
		? std::max<std::size_t>(1u, Plan->MaxStackSlots)
		: 1u;
	if (!Plan)
	{
		for (const auto& Entry : Entries)
		{
			if (Entry)
			{
				SelfStackSlots = std::max(SelfStackSlots, Entry->Stack.size());
			}
		}
	}
	const bool bCanDirectSelfRecurse = SelfFunctionIndex != 0xFFFFu
		&& Function.LocalCount <= 64u && SelfStackSlots <= 64u;
	std::vector<std::optional<FExternalDirectI32Callee>> ExternalDirectCallees(
		Module.Functions.size());
	const auto GetExternalDirectCallee = [&](const std::uint16_t CalleeIndex)
		-> const FExternalDirectI32Callee*
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
			(void)AnalyzeExternalDirectI32Callee(
				Module, SelfFunctionIndex, CalleeIndex, *Cached);
		}
		return Cached->bEligible ? &*Cached : nullptr;
	};
	bool bHasTailSelfCall = false;
	// The in-place tail-loop rewrite is currently proven only for the single-
	// parameter scalar ABI. Multi-parameter recursion retains the ordinary direct
	// recursive kernel/VM fallback: rewriting several live arguments in place can
	// otherwise violate the caller-established recursion-state ABI when the callee
	// is entered from another NativeCfg function with dynamic arguments.
	if (bCanDirectSelfRecurse && Function.ParamCount == 1u
		&& !Function.bReturnsVoid)
	{
		for (std::size_t ScanIp = 0; ScanIp < CodeSize;)
		{
			const EOpcode ScanOp = static_cast<EOpcode>(Bytes[ScanIp]);
			const std::size_t ScanSize = Bytecode::InstructionSize(ScanOp);
			if (ScanSize == 0 || ScanSize > CodeSize - ScanIp)
			{
				return nullptr;
			}
			const std::size_t NextIp = ScanIp + ScanSize;
			if (ScanOp == EOpcode::Call
				&& Bytecode::ReadU16LE(Bytes.data() + ScanIp + 1u) == SelfFunctionIndex
				&& Bytes[ScanIp + 3u] == Function.ParamCount
				&& NextIp < CodeSize
				&& static_cast<EOpcode>(Bytes[NextIp]) == EOpcode::Return)
			{
				bHasTailSelfCall = true;
				break;
			}
			ScanIp = NextIp;
		}
	}
	FX64I32BinarySelfRecurrence BinarySelfRecurrence;
	const bool bCanCompactBinarySelfRecurse = !bUnsigned
		&& TryMatchX64I32BinarySelfRecurrence(
			Module, Function, SelfFunctionIndex, Entries, BinarySelfRecurrence);
	FX64I32EuclideanTailRecurrence EuclideanTailRecurrence;
	const bool bCanCompactEuclideanTailRecurse = !bUnsigned
		&& TryMatchX64I32EuclideanTailRecurrence(
			Module, Function, SelfFunctionIndex, Entries, EuclideanTailRecurrence);
	const bool bUsesTailDepth = bHasTailSelfCall
		|| bCanCompactEuclideanTailRecurse;
	const std::int32_t RecursionStateStackOffset = bUsesTailDepth ? 144 : 128;
	Label CompactUncheckedRecursiveEntry = A.new_label();

	A.push(x86::rbx);
	A.push(x86::r12);
	A.push(x86::r13);
	A.push(x86::r14);
	A.push(x86::r15);
	if (bUsesTailDepth)
	{
		A.push(x86::rsi);
		A.sub(x86::rsp, 56);
		A.xor_(x86::esi, x86::esi);
	}
	else
	{
		A.sub(x86::rsp, 48);
	}
	A.mov(x86::r15, x86::rcx);
	A.mov(x86::rbx, x86::rdx);
	A.mov(x86::r12, x86::r8);
	A.mov(x86::r14, x86::r9);
	A.xor_(x86::r13d, x86::r13d);
	std::int32_t ClosedModuloAccumulation = 0;
	if (!bUnsigned && TryMatchX64I32CounterModuloAccumulation(
			Module, Function, ClosedModuloAccumulation))
	{
		Label NoClosedModuloOut = A.new_label();
		A.test(x86::r14, x86::r14);
		A.jz(NoClosedModuloOut);
		A.mov(x86::dword_ptr(x86::r14), ClosedModuloAccumulation);
		A.bind(NoClosedModuloOut);
		A.xor_(x86::eax, x86::eax);
		A.jmp(Epilogue);
	}
	std::int32_t ClosedLocalRecurrence = 0;
	if (!bUnsigned && TryMatchX64I32ConstantLocalRecurrence(
			Module, Function, ClosedLocalRecurrence))
	{
		Label NoClosedLocalOut = A.new_label();
		A.test(x86::r14, x86::r14);
		A.jz(NoClosedLocalOut);
		A.mov(x86::dword_ptr(x86::r14), ClosedLocalRecurrence);
		A.bind(NoClosedLocalOut);
		A.xor_(x86::eax, x86::eax);
		A.jmp(Epilogue);
	}
	std::int32_t ClosedCallRecurrence = 0;
	if (!bUnsigned && TryMatchX64I32ConstantCallRecurrence(
			Module, Function, ClosedCallRecurrence))
	{
		Label NoClosedCallOut = A.new_label();
		A.test(x86::r14, x86::r14);
		A.jz(NoClosedCallOut);
		A.mov(x86::dword_ptr(x86::r14), ClosedCallRecurrence);
		A.bind(NoClosedCallOut);
		A.xor_(x86::eax, x86::eax);
		A.jmp(Epilogue);
	}
	if (bCanCompactEuclideanTailRecurse)
	{
		Label EuclideanLoop = A.new_label();
		Label EuclideanFastPositiveLoop = A.new_label();
		Label EuclideanDone = A.new_label();
		Label EuclideanNoOut = A.new_label();
		Label EuclideanModuloSafe = A.new_label();
		Label EuclideanArithmeticOverflow = A.new_label();
		Label EuclideanDepthOverflow = A.new_label();
		A.mov(x86::r10, x86::qword_ptr(
			x86::rsp, RecursionStateStackOffset));
		// A native kernel is normally entered with a recursion state. Retain the
		// authoritative CFG when embedded by a future caller that omits it.
		A.test(x86::r10, x86::r10);
		A.jz(At[0]);
		A.mov(x86::r11, x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(FNativeRecursionState, ActiveDepth))));
		A.mov(x86::rax, x86::qword_ptr(x86::r11));
		A.mov(x86::qword_ptr(x86::rsp, 48), x86::rax);
		A.mov(x86::r8d, x86::dword_ptr(x86::rbx));
		A.mov(x86::r9d, x86::dword_ptr(x86::rbx, 4));
		A.test(x86::r9d, x86::r9d);
		A.jz(EuclideanDone);
		A.mov(x86::rax, x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(
				FNativeRecursionState, LanguageDepthLimit))));
		A.cmp(x86::qword_ptr(x86::rsp, 48), x86::rax);
		A.jae(EuclideanDepthOverflow);
		A.sub(x86::rax, x86::qword_ptr(x86::rsp, 48));
		A.cmp(x86::rax, kMaxNonNegativeI32EuclideanInvocations);
		A.jb(EuclideanLoop);
		A.mov(x86::eax, x86::r8d);
		A.or_(x86::eax, x86::r9d);
		A.js(EuclideanLoop);
		A.jmp(EuclideanFastPositiveLoop);

		A.bind(EuclideanFastPositiveLoop);
		A.test(x86::r9d, x86::r9d);
		A.jz(EuclideanDone);
		A.mov(x86::eax, x86::r8d);
		A.xor_(x86::edx, x86::edx);
		A.div(x86::r9d);
		A.inc(x86::rsi);
		A.mov(x86::r8d, x86::r9d);
		A.mov(x86::r9d, x86::edx);
		A.jmp(EuclideanFastPositiveLoop);

		A.bind(EuclideanLoop);
		A.test(x86::r9d, x86::r9d);
		A.jz(EuclideanDone);
		A.cmp(x86::r8d, std::numeric_limits<std::int32_t>::min());
		A.jne(EuclideanModuloSafe);
		A.cmp(x86::r9d, -1);
		A.je(EuclideanArithmeticOverflow);
		A.bind(EuclideanModuloSafe);
		A.mov(x86::eax, x86::r8d);
		A.cdq();
		A.idiv(x86::r9d);

		A.mov(x86::r10, x86::qword_ptr(
			x86::rsp, RecursionStateStackOffset));
		A.mov(x86::rax, x86::qword_ptr(x86::rsp, 48));
		A.add(x86::rax, x86::rsi);
		A.cmp(x86::rax, x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(
				FNativeRecursionState, LanguageDepthLimit))));
		A.jae(EuclideanDepthOverflow);
		A.inc(x86::rsi);
		A.mov(x86::r8d, x86::r9d);
		A.mov(x86::r9d, x86::edx);
		A.jmp(EuclideanLoop);

		A.bind(EuclideanArithmeticOverflow);
		A.mov(x86::r10, x86::qword_ptr(
			x86::rsp, RecursionStateStackOffset));
		A.add(x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(
				FNativeRecursionState, DirectCalls))),
			x86::rsi);
		A.xor_(x86::esi, x86::esi);
		A.jmp(NewFailureSite(EJitRuntimeStatus::IntegerOverflow,
			EuclideanTailRecurrence.ModInstructionOffset));

		A.bind(EuclideanDepthOverflow);
		A.add(x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(
				FNativeRecursionState, DirectCalls))),
			x86::rsi);
		A.mov(x86::r11, x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(FNativeRecursionState, ActiveDepth))));
		A.mov(x86::rax, x86::qword_ptr(x86::rsp, 48));
		A.add(x86::rax, x86::rsi);
		A.mov(x86::qword_ptr(x86::r11), x86::rax);
		A.mov(x86::rcx, x86::r15);
		A.mov(x86::edx, static_cast<std::uint32_t>(EJitScalarValueKind::I32));
		A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
			&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
		A.call(x86::rax);
		A.mov(x86::dword_ptr(x86::rsp, 40), x86::eax);
		A.mov(x86::r10, x86::qword_ptr(
			x86::rsp, RecursionStateStackOffset));
		A.mov(x86::r11, x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(FNativeRecursionState, ActiveDepth))));
		A.mov(x86::rax, x86::qword_ptr(x86::rsp, 48));
		A.mov(x86::qword_ptr(x86::r11), x86::rax);
		A.xor_(x86::esi, x86::esi);
		A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
		A.jmp(NewDynamicFailureSite(
			EuclideanTailRecurrence.CallInstructionOffset));

		A.bind(EuclideanDone);
		A.add(x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(
				FNativeRecursionState, DirectCalls))),
			x86::rsi);
		A.xor_(x86::esi, x86::esi);
		A.test(x86::r14, x86::r14);
		A.jz(EuclideanNoOut);
		A.mov(x86::dword_ptr(x86::r14), x86::r8d);
		A.bind(EuclideanNoOut);
		A.xor_(x86::eax, x86::eax);
		A.jmp(Epilogue);
	}

	FX64I32ClosedRecurrence ClosedRecurrence;
	if (!bUnsigned && TryMatchX64I32ClosedRecurrence(
			Module, Function, Entries, ClosedRecurrence))
	{
		const std::int64_t Delta = static_cast<std::int64_t>(
			ClosedRecurrence.IterationCount)
			*
			static_cast<std::int64_t>(ClosedRecurrence.Addend);
		A.movsxd(x86::rax, x86::dword_ptr(
			x86::rbx, static_cast<std::int32_t>(ClosedRecurrence.SourceSlot * 4u)));
		if (Delta != 0)
		{
			A.mov(x86::rcx, Delta);
			A.add(x86::rax, x86::rcx);
		}
		A.cmp(x86::rax, std::numeric_limits<std::int32_t>::max());
		A.jg(NewFailureSite(EJitRuntimeStatus::IntegerOverflow,
			ClosedRecurrence.FailureInstructionOffset));
		A.cmp(x86::rax, std::numeric_limits<std::int32_t>::min());
		A.jl(NewFailureSite(EJitRuntimeStatus::IntegerOverflow,
			ClosedRecurrence.FailureInstructionOffset));
		Label NoClosedRecurrenceOut = A.new_label();
		A.test(x86::r14, x86::r14);
		A.jz(NoClosedRecurrenceOut);
		A.mov(x86::dword_ptr(x86::r14), x86::eax);
		A.bind(NoClosedRecurrenceOut);
		A.xor_(x86::eax, x86::eax);
		A.jmp(Epilogue);
	}

	auto PushEax = [&]()
	{
		A.mov(x86::dword_ptr(x86::r12, x86::r13, 2), x86::eax);
		A.inc(x86::r13d);
	};
	auto PopEax = [&]()
	{
		A.dec(x86::r13d);
		A.mov(x86::eax, x86::dword_ptr(x86::r12, x86::r13, 2));
	};
	auto PopEcx = [&]()
	{
		A.dec(x86::r13d);
		A.mov(x86::ecx, x86::dword_ptr(x86::r12, x86::r13, 2));
	};
	std::size_t CurrentInstructionOffset = InvalidJitInstructionOffset;
	auto CallHelperAndCheck = [&](const std::uint64_t AbsAddr)
	{
		A.mov(x86::rax, AbsAddr);
		A.call(x86::rax);
		A.test(x86::eax, x86::eax);
		A.jnz(NewDynamicFailureSite(CurrentInstructionOffset));
	};
	const auto EmitSignedDivOrModImmediate = [&](const EOpcode Op,
		const std::int32_t Divisor)
	{
		const bool bMod = Op == EOpcode::Mod;
		if (Divisor == 0)
		{
			A.jmp(NewFailureSite(EJitRuntimeStatus::DivisionByZero,
				CurrentInstructionOffset));
			return;
		}
		if (Divisor == 1)
		{
			if (bMod)
			{
				A.xor_(x86::eax, x86::eax);
			}
			return;
		}
		if (Divisor == -1)
		{
			A.cmp(x86::eax, static_cast<std::int32_t>(0x80000000u));
			A.je(NewFailureSite(EJitRuntimeStatus::IntegerOverflow,
				CurrentInstructionOffset));
			if (bMod)
			{
				A.xor_(x86::eax, x86::eax);
			}
			else
			{
				A.neg(x86::eax);
			}
			return;
		}
		const FSignedDivMagic32 Magic = ComputeSignedDivMagic32(Divisor);
		A.mov(x86::r8d, x86::eax); // preserve numerator for remainder
		A.mov(x86::ecx, Magic.Multiplier);
		A.imul(x86::ecx);
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
	};
	const auto ReadI32Immediate = [&](const std::size_t ConstIp,
		std::int32_t& OutSigned, std::uint32_t& OutBits)
	{
		if (ConstIp >= CodeSize
			|| static_cast<EOpcode>(Bytes[ConstIp]) != EOpcode::ConstI32
			|| Bytecode::InstructionSize(EOpcode::ConstI32) > CodeSize - ConstIp)
		{
			return false;
		}
		const std::uint16_t Index = Bytecode::ReadU16LE(Bytes.data() + ConstIp + 1u);
		if (Index >= Module.Constants.size())
		{
			return false;
		}
		const auto& Constant = Module.Constants[Index];
		if (bUnsigned)
		{
			if (Constant.Kind == EConstantKind::U32)
			{
				OutBits = Constant.U32;
				OutSigned = static_cast<std::int32_t>(OutBits);
				return true;
			}
			if (Constant.Kind != EConstantKind::I32)
			{
				return false;
			}
		}
		else if (Constant.Kind != EConstantKind::I32)
		{
			return false;
		}
		OutSigned = Constant.I32;
		OutBits = static_cast<std::uint32_t>(Constant.I32);
		return true;
	};
	const auto IsImmediateI32Operator = [](const EOpcode Op)
	{
		switch (Op)
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
			return true;
		default:
			return false;
		}
	};
	const auto EmitImmediateI32Operator = [&](const EOpcode Op,
		const std::int32_t SignedImmediate, const std::uint32_t ImmediateBits,
		const std::size_t OperatorIp)
	{
		CurrentInstructionOffset = OperatorIp;
		switch (Op)
		{
		case EOpcode::Add:
			{
				A.add(x86::eax, SignedImmediate);
				if (!bUnsigned)
				{
					A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, OperatorIp));
				}
				return true;
			}
		case EOpcode::Sub:
			{
				A.sub(x86::eax, SignedImmediate);
				if (!bUnsigned)
				{
					A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, OperatorIp));
				}
				return true;
			}
		case EOpcode::Mul:
			{
				A.imul(x86::eax, x86::eax, SignedImmediate);
				if (!bUnsigned)
				{
					A.jo(NewFailureSite(
					EJitRuntimeStatus::IntegerOverflow, OperatorIp));
				}
				return true;
			}
		case EOpcode::Div:
		case EOpcode::Mod:
			{
				if (!bUnsigned)
				{
					EmitSignedDivOrModImmediate(Op, SignedImmediate);
					return true;
				}
				if (ImmediateBits == 0u)
				{
					A.jmp(NewFailureSite(EJitRuntimeStatus::DivisionByZero, OperatorIp));
					return true;
				}
				A.mov(x86::ecx, static_cast<std::int32_t>(ImmediateBits));
				A.xor_(x86::edx, x86::edx);
				A.div(x86::ecx);
				if (Op == EOpcode::Mod)
				{
					A.mov(x86::eax, x86::edx);
				}
				return true;
			}
		case EOpcode::BitAnd:
			{
				A.and_(x86::eax, SignedImmediate);
				return true;
			}
		case EOpcode::BitOr:
			{
				A.or_(x86::eax, SignedImmediate);
				return true;
			}
		case EOpcode::BitXor:
			{
				A.xor_(x86::eax, SignedImmediate);
				return true;
			}
		case EOpcode::Shl:
		case EOpcode::Shr:
			{
				if ((!bUnsigned && (SignedImmediate < 0 || SignedImmediate > 31))
				|| (bUnsigned && ImmediateBits > 31u))
				{
					A.jmp(NewFailureSite(EJitRuntimeStatus::ShiftOutOfRange, OperatorIp));
					return true;
				}
				if (Op == EOpcode::Shl)
				{
					A.shl(x86::eax, ImmediateBits & 31u);
				}
				else if (bUnsigned)
				{
					A.shr(x86::eax, ImmediateBits & 31u);
				}
				else
				{
					A.sar(x86::eax, ImmediateBits & 31u);
				}
				return true;
			}
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				A.cmp(x86::eax, SignedImmediate);
				A.mov(x86::eax, 0);
				if (Op == EOpcode::Eq)
				{
					A.sete(x86::al);
				}
				else if (Op == EOpcode::Ne)
				{
					A.setne(x86::al);
				}
				else if (Op == EOpcode::Lt)
				{
					if (bUnsigned)
					{
						A.setb(x86::al);
					}
					else
					{
						A.setl(x86::al);
					}
				}
				else if (Op == EOpcode::Le)
				{
					if (bUnsigned)
					{
						A.setbe(x86::al);
					}
					else
					{
						A.setle(x86::al);
					}
				}
				else if (Op == EOpcode::Gt)
				{
					if (bUnsigned)
					{
						A.seta(x86::al);
					}
					else
					{
						A.setg(x86::al);
					}
				}
				else
				{
					if (bUnsigned)
					{
						A.setae(x86::al);
					}
					else
					{
						A.setge(x86::al);
					}
				}
				return true;
			}
		default:
			return false;
		}
	};
	const auto HasInteriorJumpTarget = [&](const std::size_t Begin,
		const std::size_t End)
	{
		for (std::size_t Scan = Begin; Scan < End;)
		{
			if (Scan < JumpTargets.size() && JumpTargets[Scan])
			{
				return true;
			}
			const std::size_t Size = Bytecode::InstructionSize(
				static_cast<EOpcode>(Bytes[Scan]));
			if (Size == 0u || Size > End - Scan)
			{
				return true;
			}
			Scan += Size;
		}
		return false;
	};
	struct FImmediateChainStep
	{
		EOpcode Operator = EOpcode::Nop;
		std::int32_t SignedImmediate = 0;
		std::uint32_t ImmediateBits = 0;
		std::size_t OperatorIp = 0;
	};
	const auto ParseImmediateChain = [&](std::size_t& InOutCursor,
		std::vector<FImmediateChainStep>& OutSteps)
	{
		while (InOutCursor < CodeSize)
		{
			while (InOutCursor < CodeSize
				&& static_cast<EOpcode>(Bytes[InOutCursor]) == EOpcode::Nop
				&& !JumpTargets[InOutCursor])
			{
				++InOutCursor;
			}
			if (InOutCursor >= CodeSize
				|| static_cast<EOpcode>(Bytes[InOutCursor]) != EOpcode::ConstI32)
			{
				break;
			}
			std::int32_t SignedImmediate = 0;
			std::uint32_t ImmediateBits = 0;
			if (!ReadI32Immediate(InOutCursor, SignedImmediate, ImmediateBits))
			{
				break;
			}
			const std::size_t OperatorIp = InOutCursor
				+ Bytecode::InstructionSize(EOpcode::ConstI32);
			if (OperatorIp >= CodeSize || JumpTargets[OperatorIp])
			{
				break;
			}
			const EOpcode Operator = static_cast<EOpcode>(Bytes[OperatorIp]);
			if (!IsImmediateI32Operator(Operator))
			{
				break;
			}
			OutSteps.push_back(FImmediateChainStep{
				Operator, SignedImmediate, ImmediateBits, OperatorIp});
			InOutCursor = OperatorIp + Bytecode::InstructionSize(Operator);
		}
		return !OutSteps.empty();
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
		// The bytecode stack remains the authoritative interchange format, but the
		// common local scalar chains below have no observable intermediate stack
		// state. Lower them as a single register expression. This is deliberately a
		// structural peephole rather than a benchmark-specific loop recognizer: it
		// applies to every straight-line i32/u32 CFG and preserves each checked
		// operator's original bytecode offset for diagnostics.
		if (Op == EOpcode::LoadLocal && StackTags.empty())
		{
			const std::uint16_t FirstSlot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1u);
			if (FirstSlot >= Function.LocalCount)
			{
				return nullptr;
			}

			// accumulator = accumulator op (source immediate-op ...)
			std::size_t Cursor = AfterIp;
			if (Cursor < CodeSize && !JumpTargets[Cursor]
				&& static_cast<EOpcode>(Bytes[Cursor]) == EOpcode::LoadLocal)
			{
				const std::uint16_t SourceSlot =
					Bytecode::ReadU16LE(Bytes.data() + Cursor + 1u);
				std::size_t SourceCursor = Cursor
					+ Bytecode::InstructionSize(EOpcode::LoadLocal);
				std::vector<FImmediateChainStep> SourceSteps;
				(void)ParseImmediateChain(SourceCursor, SourceSteps);
				while (SourceCursor < CodeSize
					&& static_cast<EOpcode>(Bytes[SourceCursor]) == EOpcode::Nop
					&& !JumpTargets[SourceCursor])
				{
					++SourceCursor;
				}
				if (SourceSlot < Function.LocalCount && SourceCursor < CodeSize
					&& !JumpTargets[SourceCursor])
				{
					const EOpcode Combine = static_cast<EOpcode>(Bytes[SourceCursor]);
					std::size_t StoreIp = SourceCursor
						+ Bytecode::InstructionSize(Combine);
					std::vector<FImmediateChainStep> ResultSteps;
					(void)ParseImmediateChain(StoreIp, ResultSteps);
					while (StoreIp < CodeSize
						&& static_cast<EOpcode>(Bytes[StoreIp]) == EOpcode::Nop
						&& !JumpTargets[StoreIp])
					{
						++StoreIp;
					}
					const bool bSupportedCombine = Combine == EOpcode::Add
						|| Combine == EOpcode::Sub || Combine == EOpcode::Mul
						|| Combine == EOpcode::BitAnd || Combine == EOpcode::BitOr
						|| Combine == EOpcode::BitXor;
					if (bSupportedCombine && StoreIp < CodeSize
						&& !JumpTargets[StoreIp]
						&& static_cast<EOpcode>(Bytes[StoreIp]) == EOpcode::StoreLocal
						&& !HasInteriorJumpTarget(AfterIp, StoreIp + 3u))
					{
						const std::uint16_t Destination =
							Bytecode::ReadU16LE(Bytes.data() + StoreIp + 1u);
						if (Destination >= Function.LocalCount)
						{
							return nullptr;
						}
						A.mov(x86::eax, x86::dword_ptr(x86::rbx,
							static_cast<std::int32_t>(SourceSlot * 4u)));
						for (const FImmediateChainStep& Step : SourceSteps)
						{
							if (!EmitImmediateI32Operator(Step.Operator,
								Step.SignedImmediate, Step.ImmediateBits,
								Step.OperatorIp))
							{
								return nullptr;
							}
						}
						A.mov(x86::ecx, x86::dword_ptr(x86::rbx,
							static_cast<std::int32_t>(FirstSlot * 4u)));
						if (Combine == EOpcode::Add)
						{
							A.add(x86::ecx, x86::eax);
						}
						else if (Combine == EOpcode::Sub)
						{
							A.sub(x86::ecx, x86::eax);
						}
						else if (Combine == EOpcode::Mul)
						{
							A.imul(x86::ecx, x86::eax);
						}
						else if (Combine == EOpcode::BitAnd)
						{
							A.and_(x86::ecx, x86::eax);
						}
						else if (Combine == EOpcode::BitOr)
						{
							A.or_(x86::ecx, x86::eax);
						}
						else
						{
							A.xor_(x86::ecx, x86::eax);
						}
						if (!bUnsigned && (Combine == EOpcode::Add
							|| Combine == EOpcode::Sub || Combine == EOpcode::Mul))
						{
							A.jo(NewFailureSite(EJitRuntimeStatus::IntegerOverflow,
								SourceCursor));
						}
						A.mov(x86::eax, x86::ecx);
						for (const FImmediateChainStep& Step : ResultSteps)
						{
							if (!EmitImmediateI32Operator(Step.Operator,
								Step.SignedImmediate, Step.ImmediateBits,
								Step.OperatorIp))
							{
								return nullptr;
							}
						}
						if (!CanElideLocalStore(StoreIp))
						{
							A.mov(x86::dword_ptr(x86::rbx,
								static_cast<std::int32_t>(Destination * 4u)),
								x86::eax);
						}
						Ip = StoreIp + Bytecode::InstructionSize(EOpcode::StoreLocal);
						continue;
					}
				}
			}

			// local = source immediate-op ...
			Cursor = AfterIp;
			std::vector<FImmediateChainStep> Steps;
			if (ParseImmediateChain(Cursor, Steps))
			{
				while (Cursor < CodeSize
					&& static_cast<EOpcode>(Bytes[Cursor]) == EOpcode::Nop
					&& !JumpTargets[Cursor])
				{
					++Cursor;
				}
				if (Cursor < CodeSize && !JumpTargets[Cursor]
					&& static_cast<EOpcode>(Bytes[Cursor]) == EOpcode::StoreLocal
					&& !HasInteriorJumpTarget(AfterIp, Cursor + 3u))
				{
					const std::uint16_t Destination =
						Bytecode::ReadU16LE(Bytes.data() + Cursor + 1u);
					if (Destination >= Function.LocalCount)
					{
						return nullptr;
					}
					A.mov(x86::eax, x86::dword_ptr(x86::rbx,
						static_cast<std::int32_t>(FirstSlot * 4u)));
					for (const FImmediateChainStep& Step : Steps)
					{
						if (!EmitImmediateI32Operator(Step.Operator,
							Step.SignedImmediate, Step.ImmediateBits,
							Step.OperatorIp))
						{
							return nullptr;
						}
					}
					if (!CanElideLocalStore(Cursor))
					{
						A.mov(x86::dword_ptr(x86::rbx,
							static_cast<std::int32_t>(Destination * 4u)),
							x86::eax);
					}
					Ip = Cursor + Bytecode::InstructionSize(EOpcode::StoreLocal);
					continue;
				}
				if (Cursor < CodeSize && !JumpTargets[Cursor]
					&& static_cast<EOpcode>(Bytes[Cursor]) == EOpcode::JmpIfFalse
					&& !HasInteriorJumpTarget(AfterIp,
						Cursor + Bytecode::InstructionSize(EOpcode::JmpIfFalse)))
				{
					std::size_t Target = 0;
					if (!IsJumpTargetInRangeLocal(
							Cursor + Bytecode::InstructionSize(EOpcode::JmpIfFalse),
							Bytecode::ReadI32LE(Bytes.data() + Cursor + 1u),
							CodeSize, Target))
					{
						return nullptr;
					}
					A.mov(x86::eax, x86::dword_ptr(x86::rbx,
						static_cast<std::int32_t>(FirstSlot * 4u)));
					for (const FImmediateChainStep& Step : Steps)
					{
						if (!EmitImmediateI32Operator(Step.Operator,
							Step.SignedImmediate, Step.ImmediateBits,
							Step.OperatorIp))
						{
							return nullptr;
						}
					}
					A.test(x86::eax, x86::eax);
					A.jz(At[Target]);
					Ip = Cursor + Bytecode::InstructionSize(EOpcode::JmpIfFalse);
					continue;
				}
			}
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
				if (Idx >= Module.Constants.size())
				{
					return nullptr;
				}
				const auto& Const = Module.Constants[Idx];
				if (bUnsigned)
				{
					if (Const.Kind != EConstantKind::U32 && Const.Kind != EConstantKind::I32)
					{
						return nullptr;
					}
					A.mov(x86::eax, Const.Kind == EConstantKind::U32 ? static_cast<std::int32_t>(Const.U32) : Const.I32);
				}
				else if (Const.Kind != EConstantKind::I32)
				{
					return nullptr;
				}
				else
				{
					A.mov(x86::eax, Const.I32);
				}
				PushEax();
				break;
			}
		case EOpcode::ConstBool:
			{
				A.mov(x86::eax, Bytes[Ip + 1] != 0 ? 1 : 0);
				PushEax();
				break;
			}
		case EOpcode::LoadLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Function.LocalCount)
				{
					return nullptr;
				}
				A.mov(x86::eax, x86::dword_ptr(x86::rbx, static_cast<int32_t>(Slot * 4)));
				PushEax();
				break;
			}
		case EOpcode::StoreLocal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Function.LocalCount)
				{
					return nullptr;
				}
				PopEax();
				if (!CanElideLocalStore(Ip))
				{
					A.mov(x86::dword_ptr(
						x86::rbx, static_cast<int32_t>(Slot * 4)),
						x86::eax);
				}
				break;
			}
		case EOpcode::LoadThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				A.lea(x86::r8, x86::dword_ptr(x86::rsp, 40));
				CallHelperAndCheck(bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadThisFieldU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadThisFieldI32));
				A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
				PushEax();
				break;
			}
		case EOpcode::StoreThisField:
		case EOpcode::InitializeThisField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				PopEax();
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				A.mov(x86::r8d, x86::eax);
				CallHelperAndCheck(bUnsigned
					? reinterpret_cast<std::uint64_t>(Op == EOpcode::InitializeThisField
						? &VeroScript::AsmJit::JitNative::LinearHelpers::InitializeThisFieldU32
						: &VeroScript::AsmJit::JitNative::LinearHelpers::StoreThisFieldU32)
					: reinterpret_cast<std::uint64_t>(Op == EOpcode::InitializeThisField
						? &VeroScript::AsmJit::JitNative::LinearHelpers::InitializeThisFieldI32
						: &VeroScript::AsmJit::JitNative::LinearHelpers::StoreThisFieldI32));
				break;
			}
		case EOpcode::LoadThis:
			{
				// No-op: this stays in GetJitThisObject(); never pushed on the scalar stack.
				break;
			}
		case EOpcode::CallBoundMethod:
		case EOpcode::CallMethod:
			{
				const auto Idx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto N = Bytes[Ip + 3];
				if (StackTags.size() < N)
				{
					return nullptr;
				}
				A.sub(x86::r13d, N);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Idx);
				A.lea(x86::r8, x86::dword_ptr(x86::r12, x86::r13, 2));
				A.mov(x86::r9d, N);
				A.lea(x86::rax, x86::dword_ptr(x86::rsp, 40));
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				if (Op == EOpcode::CallBoundMethod)
				{
					CallHelperAndCheck(bUnsigned
						? reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisU32)
							: reinterpret_cast<std::uint64_t>(
								&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisI32));
					if (!Module.Functions[Idx].bReturnsVoid)
					{
						A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
						PushEax();
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
						PushEax();
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
						if (!ClosureTypeMatchesLocal(Candidate.ClosureTypeName, bUnsigned))
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
					CallHelperAndCheck(bUnsigned
						? reinterpret_cast<std::uint64_t>((!bAmbiguous && bSaw)
							? &VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisU32
							: &VeroScript::AsmJit::JitNative::LinearHelpers::CallMethodFromThisU32)
							: reinterpret_cast<std::uint64_t>((!bAmbiguous && bSaw)
								? &VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisI32
								: &VeroScript::AsmJit::JitNative::LinearHelpers::CallMethodFromThisI32));
					if (bSaw && !bCallVoid)
					{
						A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
						PushEax();
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
						PushEax();
					}
				}
				break;
			}
		case EOpcode::IncField:
			{
				const auto Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Name);
				CallHelperAndCheck(bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::IncThisFieldU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::IncThisFieldI32));
				break;
			}
		case EOpcode::LoadGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Slot);
				A.lea(x86::r8, x86::dword_ptr(x86::rsp, 40));
				CallHelperAndCheck(bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalI32));
				A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
				PushEax();
				break;
			}
		case EOpcode::StoreGlobal:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				PopEax();
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, Slot);
				A.mov(x86::r8d, x86::eax);
				CallHelperAndCheck(bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalI32));
				break;
			}
		case EOpcode::Call:
			{
				const auto FnIdx = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto N = Bytes[Ip + 3];
				if (StackTags.size() < N)
				{
					return nullptr;
				}
				A.sub(x86::r13d, N);
				const bool bTailSelfCall = bHasTailSelfCall
					&& FnIdx == SelfFunctionIndex
					&& N == Function.ParamCount
					&& AfterIp < CodeSize
					&& static_cast<EOpcode>(Bytes[AfterIp]) == EOpcode::Return;
				if (bTailSelfCall)
				{
					Label TailDepthOverflow = A.new_label();
					A.mov(x86::r10, x86::qword_ptr(
						x86::rsp, RecursionStateStackOffset));
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(FNativeRecursionState, ActiveDepth))));
					A.mov(x86::rax, x86::qword_ptr(x86::r11));
					A.cmp(x86::rax, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, LanguageDepthLimit))));
					A.jae(TailDepthOverflow);
					A.inc(x86::rax);
					A.mov(x86::qword_ptr(x86::r11), x86::rax);
					A.inc(x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(FNativeRecursionState, DirectCalls))));
					A.inc(x86::rsi);
					for (std::size_t Arg = 0; Arg < N; ++Arg)
					{
						A.mov(x86::eax, x86::dword_ptr(
							x86::r12, x86::r13, 2,
							static_cast<std::int32_t>(Arg * 4u)));
						A.mov(x86::dword_ptr(x86::rbx,
							static_cast<std::int32_t>(Arg * 4u)),
							x86::eax);
					}
					A.xor_(x86::eax, x86::eax);
					for (std::size_t Local = N; Local < Function.LocalCount; ++Local)
					{
						A.mov(x86::dword_ptr(x86::rbx,
							static_cast<std::int32_t>(Local * 4u)),
							x86::eax);
					}
					A.xor_(x86::r13d, x86::r13d);
					A.jmp(At[0]);

					A.bind(TailDepthOverflow);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, static_cast<std::uint32_t>(bUnsigned
						? EJitScalarValueKind::U32 : EJitScalarValueKind::I32));
					A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
					A.call(x86::rax);
					A.jmp(NewDynamicFailureSite(Ip));
				}
				const FExternalDirectI32Callee* const ExternalCallee =
					GetExternalDirectCallee(FnIdx);
				if (ExternalCallee && ExternalCallee->bUnsigned == bUnsigned
					&& N == Module.Functions[FnIdx].ParamCount)
				{
					// A generation-owned dense target table lets an already compiled scalar
					// callee be entered without cache lookup, FValue packing, or a C++ bridge.
					// The first on-demand call normally takes FallbackCall and publishes the
					// target; subsequent iterations load it directly from the same stable table.
					const std::size_t OutOffset = 40u;
					const std::size_t StatusOffset = 44u;
					const std::size_t LocalsOffset = 48u;
					const std::size_t StackOffset = LocalsOffset
						+ ExternalCallee->LocalCount * 4u;
					const std::size_t FrameBytes =
						(StackOffset + ExternalCallee->StackSlots * 4u + 15u) & ~15u;
					Label FallbackCall = A.new_label();
					Label LanguageOverflow = A.new_label();
					Label CallDone = A.new_label();

					A.mov(x86::r10, x86::qword_ptr(
						x86::rsp, RecursionStateStackOffset));
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
						A.mov(x86::eax, x86::dword_ptr(
							x86::r12, x86::r13, 2, static_cast<std::int32_t>(Arg * 4u)));
						A.mov(x86::dword_ptr(x86::rsp,
							static_cast<std::int32_t>(LocalsOffset + Arg * 4u)),
							x86::eax);
					}
					A.xor_(x86::eax, x86::eax);
					for (std::size_t Local = N; Local < ExternalCallee->LocalCount; ++Local)
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
					A.mov(x86::r10, x86::qword_ptr(x86::rsp, 32));
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(
							FNativeRecursionState, DirectI32Targets))));
					A.mov(x86::rax, x86::qword_ptr(
						x86::r11, static_cast<std::int32_t>(FnIdx * sizeof(void*))));
					A.call(x86::rax);
					A.mov(x86::dword_ptr(x86::rsp,
						static_cast<std::int32_t>(StatusOffset)),
						x86::eax);
					A.mov(x86::r10, x86::qword_ptr(x86::rsp, 32));
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
					PushEax();
					A.jmp(CallDone);

					A.bind(LanguageOverflow);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, static_cast<std::uint32_t>(ExternalCallee->bUnsigned
						? EJitScalarValueKind::U32 : EJitScalarValueKind::I32));
					A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
					A.call(x86::rax);
					A.jmp(NewDynamicFailureSite(Ip));

					A.bind(FallbackCall);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, FnIdx);
					A.lea(x86::r8, x86::dword_ptr(x86::r12, x86::r13, 2));
					A.mov(x86::r9d, N);
					A.lea(x86::rax, x86::dword_ptr(x86::rsp, 40));
					A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
					CallHelperAndCheck(bUnsigned
						? reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallU32)
						: reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCall));
					if (!Module.Functions[FnIdx].bReturnsVoid)
					{
						A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
					}
					PushEax();
					A.bind(CallDone);
					break;
				}
				if (bCanDirectSelfRecurse && FnIdx == SelfFunctionIndex)
				{
					// 0..31 is Win64 shadow space and offset 32 carries the fifth
					// (recursion-state) argument to the nested kernel.
					const std::size_t OutOffset = 40u;
					const std::size_t StatusOffset = 44u;
					const std::size_t LocalsOffset = 48u;
					const std::size_t StackOffset = LocalsOffset + Function.LocalCount * 4u;
					const std::size_t FrameBytes = (StackOffset + SelfStackSlots * 4u + 15u) & ~15u;
					Label DirectCall = A.new_label();
					Label FallbackCall = A.new_label();
					Label LanguageOverflow = A.new_label();
					Label CallDone = A.new_label();

					// Fifth Win64 argument at entry rsp+40; this prologue consumes 88 bytes.
					A.mov(x86::r10, x86::qword_ptr(
						x86::rsp, RecursionStateStackOffset));
					A.test(x86::r10, x86::r10);
					A.jz(FallbackCall);
					A.mov(x86::r11, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(FNativeRecursionState, ActiveDepth))));
					A.mov(x86::rax, x86::qword_ptr(x86::r11));
					A.cmp(x86::rax, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(FNativeRecursionState, LanguageDepthLimit))));
					A.jae(LanguageOverflow);
					A.cmp(x86::rax, x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(FNativeRecursionState, NativeDepthLimit))));
					A.jae(FallbackCall);
					A.inc(x86::rax);
					A.mov(x86::qword_ptr(x86::r11), x86::rax);
					A.inc(x86::qword_ptr(x86::r10,
						static_cast<std::int32_t>(offsetof(FNativeRecursionState, DirectCalls))));
					A.jmp(DirectCall);

					A.bind(LanguageOverflow);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, static_cast<std::uint32_t>(bUnsigned
						? EJitScalarValueKind::U32 : EJitScalarValueKind::I32));
					A.mov(x86::rax, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
					A.call(x86::rax);
					A.jmp(NewDynamicFailureSite(Ip));

					A.bind(DirectCall);
					if (bCanCompactBinarySelfRecurse
						&& BinarySelfRecurrence.FirstDelta > 0
						&& BinarySelfRecurrence.SecondDelta > 0)
					{
						// Compact ABI: rcx=VM, edx=argument, r8=recursion state;
						// eax=result, edx=status. The current kernel's shadow/scratch
						// area preserves both return registers while depth is restored.
						A.mov(x86::edx, x86::dword_ptr(x86::r12, x86::r13, 2));
						Label UseFullKernelEntry = A.new_label();
						Label UseUncheckedCompactEntry = A.new_label();
						// A conservative upper bound assumes each edge decreases by only
						// one. If even that bound fits both limits, the pure subtree cannot
						// observe per-edge ActiveDepth changes and may use the lean ABI.
						A.cmp(x86::edx, BinarySelfRecurrence.BaseLimit);
						A.jl(UseUncheckedCompactEntry);
						A.movsxd(x86::r11, x86::edx);
						A.mov(x86::r9, static_cast<std::int64_t>(
							BinarySelfRecurrence.BaseLimit) - 1ll);
						A.sub(x86::r11, x86::r9);
						A.add(x86::r11, x86::rax);
						A.cmp(x86::r11, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, LanguageDepthLimit))));
						A.ja(UseFullKernelEntry);
						A.cmp(x86::r11, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(
								FNativeRecursionState, NativeDepthLimit))));
						A.ja(UseFullKernelEntry);
						A.bind(UseUncheckedCompactEntry);
						A.mov(x86::r8, x86::r10);
						A.call(CompactUncheckedRecursiveEntry);
						A.mov(x86::dword_ptr(x86::rsp, 40), x86::eax);
						A.mov(x86::dword_ptr(x86::rsp, 44), x86::edx);
						A.mov(x86::r10, x86::qword_ptr(
							x86::rsp, RecursionStateStackOffset));
						A.mov(x86::r11, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(FNativeRecursionState, ActiveDepth))));
						A.dec(x86::qword_ptr(x86::r11));
						A.mov(x86::edx, x86::dword_ptr(x86::rsp, 44));
						A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
						Label CompactCallSucceeded = A.new_label();
						A.test(x86::edx, x86::edx);
						A.jz(CompactCallSucceeded);
						A.mov(x86::eax, x86::edx);
						A.jmp(Epilogue);
						A.bind(CompactCallSucceeded);
						PushEax();
						A.jmp(CallDone);
						A.bind(UseFullKernelEntry);
					}
					{
						A.mov(x86::r10, x86::qword_ptr(
							x86::rsp, RecursionStateStackOffset));
						A.sub(x86::rsp, static_cast<std::uint32_t>(FrameBytes));
						A.mov(x86::qword_ptr(x86::rsp, 32), x86::r10);
						for (std::size_t Arg = 0; Arg < N; ++Arg)
						{
							A.mov(x86::eax, x86::dword_ptr(
								x86::r12, x86::r13, 2, static_cast<std::int32_t>(Arg * 4u)));
							A.mov(x86::dword_ptr(x86::rsp,
								static_cast<std::int32_t>(LocalsOffset + Arg * 4u)),
								x86::eax);
						}
						A.xor_(x86::eax, x86::eax);
						for (std::size_t Local = N; Local < Function.LocalCount; ++Local)
						{
							A.mov(x86::dword_ptr(x86::rsp,
								static_cast<std::int32_t>(LocalsOffset + Local * 4u)),
								x86::eax);
						}
						A.mov(x86::rcx, x86::r15);
						A.lea(x86::rdx, x86::dword_ptr(x86::rsp, static_cast<std::int32_t>(LocalsOffset)));
						A.lea(x86::r8, x86::dword_ptr(x86::rsp, static_cast<std::int32_t>(StackOffset)));
						A.lea(x86::r9, x86::dword_ptr(x86::rsp, static_cast<std::int32_t>(OutOffset)));
						A.call(NativeEntry);
						A.mov(x86::dword_ptr(x86::rsp, static_cast<std::int32_t>(StatusOffset)), x86::eax);
						A.mov(x86::r10, x86::qword_ptr(x86::rsp, 32));
						A.mov(x86::r11, x86::qword_ptr(x86::r10,
							static_cast<std::int32_t>(offsetof(FNativeRecursionState, ActiveDepth))));
						A.dec(x86::qword_ptr(x86::r11));
						A.mov(x86::r10d, x86::dword_ptr(x86::rsp, static_cast<std::int32_t>(StatusOffset)));
						if (!Function.bReturnsVoid)
						{
							A.mov(x86::r11d, x86::dword_ptr(x86::rsp, static_cast<std::int32_t>(OutOffset)));
						}
						A.add(x86::rsp, static_cast<std::uint32_t>(FrameBytes));
						A.test(x86::r10d, x86::r10d);
						A.mov(x86::eax, x86::r10d);
						A.jnz(Epilogue);
						if (!Function.bReturnsVoid)
						{
							A.mov(x86::eax, x86::r11d);
						}
						else
						{
							A.xor_(x86::eax, x86::eax);
						}
						PushEax();
						A.jmp(CallDone);
					}

					A.bind(FallbackCall);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, FnIdx);
					A.lea(x86::r8, x86::dword_ptr(x86::r12, x86::r13, 2));
					A.mov(x86::r9d, N);
					A.lea(x86::rax, x86::dword_ptr(x86::rsp, 40));
					A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
					CallHelperAndCheck(bUnsigned
						? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallU32)
						: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCall));
					if (!Function.bReturnsVoid)
					{
						A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
					}
					else
					{
						A.xor_(x86::eax, x86::eax);
					}
					PushEax();
					A.bind(CallDone);
					break;
				}
				A.mov(x86::rcx, x86::r15);
				A.mov(x86::edx, FnIdx);
				A.lea(x86::r8, x86::dword_ptr(x86::r12, x86::r13, 2));
				A.mov(x86::r9d, N);
				A.lea(x86::rax, x86::dword_ptr(x86::rsp, 40));
				A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
				CallHelperAndCheck(bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCall));
				if (!Module.Functions[FnIdx].bReturnsVoid)
				{
					A.mov(x86::eax, x86::dword_ptr(x86::rsp, 40));
					PushEax();
				}
				else
				{
					// Match VM Call+Pop: void calls leave a discarded sentinel.
					A.xor_(x86::eax, x86::eax);
					PushEax();
				}
				break;
			}
		case EOpcode::CallBuiltin:
			{
				const auto B = static_cast<EBuiltinId>(Bytes[Ip + 1]);
				const auto N = Bytes[Ip + 2];
				if (B == EBuiltinId::Print || B == EBuiltinId::Eprint)
				{
					if (N != 1 || StackTags.empty())
					{
						return nullptr;
					}
					// Win64: BuiltinMixed(Vm, Id, Tag, Slot*, ArgCount).
					// Tag from CFG entry state (avoids linear OpTags join desync).
					const std::uint8_t ArgTag = StackTags.back();
					PopEax();
					A.mov(x86::qword_ptr(x86::rsp, 40), x86::rax);
					A.mov(x86::rcx, x86::r15);
					A.mov(x86::edx, static_cast<std::uint32_t>(B));
					A.mov(x86::r8d, static_cast<std::uint32_t>(ArgTag));
					A.lea(x86::r9, x86::qword_ptr(x86::rsp, 40));
					A.mov(x86::eax, 1);
					A.mov(x86::qword_ptr(x86::rsp, 32), x86::rax);
					CallHelperAndCheck(reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::BuiltinMixed));
					break;
				}
				if (B == EBuiltinId::Abs)
				{
					if (bUnsigned)
					{
						break; // uint abs is identity
					}
					PopEax();
					A.cmp(x86::eax, static_cast<std::int32_t>(0x80000000u));
					A.je(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
					Label AbsSkip = A.new_label();
					A.test(x86::eax, x86::eax);
					A.jns(AbsSkip);
					A.neg(x86::eax);
					A.bind(AbsSkip);
					PushEax();
					break;
				}
				if (B == EBuiltinId::Min || B == EBuiltinId::Max)
				{
					PopEcx();
					PopEax();
					A.cmp(x86::eax, x86::ecx);
					if (B == EBuiltinId::Min)
					{
						if (bUnsigned)
						{
							A.cmova(x86::eax, x86::ecx);
						}
						else
						{
							A.cmovg(x86::eax, x86::ecx);
						}
					}
					else
					{
						if (bUnsigned)
						{
							A.cmovb(x86::eax, x86::ecx);
						}
						else
						{
							A.cmovl(x86::eax, x86::ecx);
						}
					}
					PushEax();
					break;
				}
				if (B == EBuiltinId::ToInt)
				{
					if (bUnsigned)
					{
						A.mov(x86::eax, x86::dword_ptr(
							x86::r12, x86::r13, 2, -4));
						A.cmp(x86::eax, static_cast<std::int32_t>(0x80000000u));
						A.jae(NewFailureSite(EJitRuntimeStatus::ToIntOverflow, Ip));
					}
					break;
				}
				if (B == EBuiltinId::ToUnsigned)
				{
					if (!bUnsigned)
					{
						A.mov(x86::eax, x86::dword_ptr(
							x86::r12, x86::r13, 2, -4));
						A.test(x86::eax, x86::eax);
						A.js(NewFailureSite(EJitRuntimeStatus::ToUnsignedOverflow, Ip));
					}
					break;
				}
				return nullptr;
			}
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
			{
				const auto Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::int32_t Imm = Op == EOpcode::IncLocal
					? 1
					: Bytecode::ReadI32LE(Bytes.data() + Ip + 3);
				A.mov(x86::eax, x86::dword_ptr(x86::rbx, static_cast<int32_t>(Slot * 4)));
				A.add(x86::eax, Imm);
				if (!bUnsigned)
				{
					A.jo(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
				}
				A.mov(x86::dword_ptr(x86::rbx, static_cast<int32_t>(Slot * 4)), x86::eax);
				break;
			}
		case EOpcode::AddLocalLocal:
			{
				const auto D = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const auto S = Bytecode::ReadU16LE(Bytes.data() + Ip + 3);
				A.mov(x86::eax, x86::dword_ptr(x86::rbx, static_cast<int32_t>(D * 4)));
				A.add(x86::eax, x86::dword_ptr(x86::rbx, static_cast<int32_t>(S * 4)));
				if (!bUnsigned)
				{
					A.jo(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
				}
				A.mov(x86::dword_ptr(x86::rbx, static_cast<int32_t>(D * 4)), x86::eax);
				break;
			}
		case EOpcode::Add:
		case EOpcode::Sub:
		case EOpcode::Mul:
			{
				PopEcx();
				PopEax();
				if (Op == EOpcode::Add)
				{
					A.add(x86::eax, x86::ecx);
				}
				else if (Op == EOpcode::Sub)
				{
					A.sub(x86::eax, x86::ecx);
				}
				else
				{
					A.imul(x86::eax, x86::ecx);
				}
				if (!bUnsigned)
				{
					A.jo(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
				}
				PushEax();
				break;
			}
		case EOpcode::Div:
		case EOpcode::Mod:
			{
				std::int32_t ImmediateDivisor = 0;
				bool bHasSignedImmediate = false;
				constexpr std::size_t ConstI32Size = 3u;
				if (!bUnsigned && Ip >= ConstI32Size && !JumpTargets[Ip])
				{
					const std::size_t ConstantIp = Ip - ConstI32Size;
					if (Entries[ConstantIp]
						&& static_cast<EOpcode>(Bytes[ConstantIp]) == EOpcode::ConstI32)
					{
						const std::uint16_t ConstantIndex =
							Bytecode::ReadU16LE(Bytes.data() + ConstantIp + 1u);
						if (ConstantIndex < Module.Constants.size()
							&& Module.Constants[ConstantIndex].Kind == EConstantKind::I32)
						{
							ImmediateDivisor = Module.Constants[ConstantIndex].I32;
							bHasSignedImmediate = true;
						}
					}
				}
				PopEcx();
				PopEax();
				if (bHasSignedImmediate)
				{
					EmitSignedDivOrModImmediate(Op, ImmediateDivisor);
				}
				else if (bUnsigned)
				{
					A.test(x86::ecx, x86::ecx);
					A.jz(NewFailureSite(EJitRuntimeStatus::DivisionByZero, Ip));
					A.xor_(x86::edx, x86::edx);
					A.div(x86::ecx);
				}
				else
				{
					A.test(x86::ecx, x86::ecx);
					A.jz(NewFailureSite(EJitRuntimeStatus::DivisionByZero, Ip));
					A.cmp(x86::eax, static_cast<std::int32_t>(0x80000000u));
					Label DoDiv = A.new_label();
					A.jne(DoDiv);
					A.cmp(x86::ecx, -1);
					A.je(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
					A.bind(DoDiv);
					A.cdq();
					A.idiv(x86::ecx);
				}
				if (!bHasSignedImmediate && Op == EOpcode::Mod)
				{
					A.mov(x86::eax, x86::edx);
				}
				PushEax();
				break;
			}
		case EOpcode::BitAnd:
		case EOpcode::BitOr:
		case EOpcode::BitXor:
			{
				PopEcx();
				PopEax();
				if (Op == EOpcode::BitAnd)
				{
					A.and_(x86::eax, x86::ecx);
				}
				else if (Op == EOpcode::BitOr)
				{
					A.or_(x86::eax, x86::ecx);
				}
				else
				{
					A.xor_(x86::eax, x86::ecx);
				}
				PushEax();
				break;
			}
		case EOpcode::Shl:
		case EOpcode::Shr:
			{
				PopEcx();
				PopEax();
				if (bUnsigned)
				{
					A.cmp(x86::ecx, 31);
					A.ja(NewFailureSite(EJitRuntimeStatus::ShiftOutOfRange, Ip));
				}
				else
				{
					A.cmp(x86::ecx, 0);
					A.jl(NewFailureSite(EJitRuntimeStatus::ShiftOutOfRange, Ip));
					A.cmp(x86::ecx, 31);
					A.jg(NewFailureSite(EJitRuntimeStatus::ShiftOutOfRange, Ip));
				}
				if (Op == EOpcode::Shl)
				{
					A.shl(x86::eax, x86::cl);
				}
				else if (bUnsigned)
				{
					A.shr(x86::eax, x86::cl);
				}
				else
				{
					A.sar(x86::eax, x86::cl);
				}
				PushEax();
				break;
			}
		case EOpcode::Neg:
			{
				PopEax();
				A.neg(x86::eax);
				if (!bUnsigned)
				{
					A.jo(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
				}
				PushEax();
				break;
			}
		case EOpcode::BitNot:
			{
				PopEax();
				A.not_(x86::eax);
				PushEax();
				break;
			}
		case EOpcode::Not:
			{
				(void)PopEax();
				A.xor_(x86::eax, 1);
				A.and_(x86::eax, 1);
				PushEax();
				break;
			}
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				PopEcx();
				(void)PopEax();
				A.cmp(x86::eax, x86::ecx);
				A.mov(x86::eax, 0);
				if (Op == EOpcode::Eq)
				{
					A.sete(x86::al);
				}
				else if (Op == EOpcode::Ne)
				{
					A.setne(x86::al);
				}
				else if (Op == EOpcode::Lt)
				{
					if (bUnsigned)
					{
						A.setb(x86::al);
					}
					else
					{
						A.setl(x86::al);
					}
				}
				else if (Op == EOpcode::Le)
				{
					if (bUnsigned)
					{
						A.setbe(x86::al);
					}
					else
					{
						A.setle(x86::al);
					}
				}
				else if (Op == EOpcode::Gt)
				{
					if (bUnsigned)
					{
						A.seta(x86::al);
					}
					else
					{
						A.setg(x86::al);
					}
				}
				else
				{
					if (bUnsigned)
					{
						A.setae(x86::al);
					}
					else
					{
						A.setge(x86::al);
					}
				}
				PushEax();
				break;
			}
		case EOpcode::Dup:
			{
				A.mov(x86::eax, x86::dword_ptr(x86::r12, x86::r13, 2, -4));
				PushEax();
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
				PopEax();
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
				A.cmp(x86::dword_ptr(x86::rbx, static_cast<int32_t>(Slot * 4)), Imm);
				if (bUnsigned)
				{
					A.jae(At[Target]);
				}
				else
				{
					A.jge(At[Target]);
				}
				break;
			}
		case EOpcode::Return:
			{
				if (!Function.bReturnsVoid)
				{
					PopEax();
					A.test(x86::r14, x86::r14);
					Label Skip = A.new_label();
					A.jz(Skip);
					A.mov(x86::dword_ptr(x86::r14), x86::eax);
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

	// Void bodies may fall off the end with no trailing Return (VM / linear accept that).
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
	if (bUsesTailDepth)
	{
		Label NoTailDepth = A.new_label();
		A.test(x86::rsi, x86::rsi);
		A.jz(NoTailDepth);
		A.mov(x86::r10, x86::qword_ptr(
			x86::rsp, RecursionStateStackOffset));
		A.mov(x86::r11, x86::qword_ptr(x86::r10,
			static_cast<std::int32_t>(offsetof(FNativeRecursionState, ActiveDepth))));
		A.sub(x86::qword_ptr(x86::r11), x86::rsi);
		A.bind(NoTailDepth);
		A.add(x86::rsp, 56);
		A.pop(x86::rsi);
	}
	else
	{
		A.add(x86::rsp, 48);
	}
	A.pop(x86::r15);
	A.pop(x86::r14);
	A.pop(x86::r13);
	A.pop(x86::r12);
	A.pop(x86::rbx);
	A.ret();

	if (bCanCompactBinarySelfRecurse)
	{
		if (BinarySelfRecurrence.FirstDelta > 0
			&& BinarySelfRecurrence.SecondDelta > 0)
		{
			// The caller selects this entry only after proving that the complete
			// strictly-decreasing subtree fits both recursion limits. It therefore
			// needs no depth bridge, VM pointer or helper-call shadow space. Actual
			// recursive calls and DirectCalls accounting remain one-for-one.
			A.bind(CompactUncheckedRecursiveEntry);
			Label UncheckedBase = A.new_label();
			Label UncheckedOverflow = A.new_label();
			Label UncheckedFrameReturn = A.new_label();
			A.cmp(x86::edx, BinarySelfRecurrence.BaseLimit);
			A.jl(UncheckedBase);

			constexpr std::int32_t UncheckedFrameBytes = 24;
			constexpr std::int32_t UncheckedOriginalArgumentOffset = 0;
			constexpr std::int32_t UncheckedFirstResultOffset = 4;
			constexpr std::int32_t UncheckedStateOffset = 8;
			A.sub(x86::rsp, UncheckedFrameBytes);
			A.mov(x86::dword_ptr(
				x86::rsp, UncheckedOriginalArgumentOffset),
				x86::edx);
			A.mov(x86::qword_ptr(x86::rsp, UncheckedStateOffset), x86::r8);

			const auto EmitUncheckedChild = [&](const std::int32_t Delta)
			{
				A.mov(x86::edx, x86::dword_ptr(
					x86::rsp, UncheckedOriginalArgumentOffset));
				A.sub(x86::edx, static_cast<std::uint32_t>(Delta));
				A.jo(UncheckedOverflow);
				A.mov(x86::r8, x86::qword_ptr(x86::rsp, UncheckedStateOffset));
				A.inc(x86::qword_ptr(x86::r8,
					static_cast<std::int32_t>(offsetof(
						FNativeRecursionState, DirectCalls))));
				A.call(CompactUncheckedRecursiveEntry);
			};

			EmitUncheckedChild(BinarySelfRecurrence.FirstDelta);
			A.test(x86::edx, x86::edx);
			A.jnz(UncheckedFrameReturn);
			A.mov(x86::dword_ptr(
				x86::rsp, UncheckedFirstResultOffset),
				x86::eax);
			EmitUncheckedChild(BinarySelfRecurrence.SecondDelta);
			A.test(x86::edx, x86::edx);
			A.jnz(UncheckedFrameReturn);
			A.add(x86::eax, x86::dword_ptr(
				x86::rsp, UncheckedFirstResultOffset));
			A.jo(UncheckedOverflow);
			A.xor_(x86::edx, x86::edx);
			A.jmp(UncheckedFrameReturn);

			A.bind(UncheckedOverflow);
			A.xor_(x86::eax, x86::eax);
			A.mov(x86::edx, kStatusOverflow);
			A.bind(UncheckedFrameReturn);
			A.add(x86::rsp, UncheckedFrameBytes);
			A.ret();

			A.bind(UncheckedBase);
			A.mov(x86::eax, x86::edx);
			A.xor_(x86::edx, x86::edx);
			A.ret();
		}
	}

	void* Fn = nullptr;
	if (!FinalizeKernel(Runtime, Code, &Fn))
	{
		return nullptr;
	}
	return reinterpret_cast<FLinearI32Kernel>(Fn);
}
