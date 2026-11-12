// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
{
	using namespace VeroScript;
	using namespace VeroScript::AsmJit;
	using namespace VeroScript::AsmJit::JitLinear;
	constexpr auto Minimum = std::numeric_limits<std::int64_t>::min();
	constexpr auto Maximum = std::numeric_limits<std::int64_t>::max();
	const auto SignedBits = [](const std::uint64_t Bits)
	{
		std::int64_t Value = 0;
		std::memcpy(&Value, &Bits, sizeof(Value));
		return Value;
	};
	std::uint64_t Random = 0x1F0D3B7A951C826Eull;
	const auto Next = [&]()
	{
		Random ^= Random >> 12u;
		Random ^= Random << 25u;
		Random ^= Random >> 27u;
		return Random * 2685821657736338717ull;
	};
	std::vector<std::int64_t> Divisors{
	    0, 1, -1, 2, -2, 3, -3, 7, -7, 17, -17, 65521, -65521,
	    2147483647, -2147483648ll, 4294967291ll, -4294967291ll,
	    std::int64_t{1} << 32u, -(std::int64_t{1} << 32u),
	    std::int64_t{1} << 62u, -(std::int64_t{1} << 62u),
	    Minimum, Minimum + 1, Maximum - 1, Maximum};
	for (unsigned Index = 0; Index < 128u; ++Index)
	{
		Divisors.push_back(SignedBits(Next()));
	}
	for (const auto Divisor : Divisors)
	{
		std::vector<std::int64_t> Numerators{Minimum, Minimum + 1, INT32_MIN, INT32_MAX,
		    -1, 0, 1, Maximum - 1, Maximum, Divisor};
		if (Divisor != Minimum)
		{
			Numerators.push_back(Divisor - 1);
		}
		if (Divisor != Maximum)
		{
			Numerators.push_back(Divisor + 1);
		}
		for (unsigned Index = 0; Index < 24u; ++Index)
		{
			Numerators.push_back(SignedBits(Next()));
			const auto Bits = static_cast<std::uint32_t>(Next());
			std::int32_t Narrow = 0;
			std::memcpy(&Narrow, &Bits, sizeof(Narrow));
			Numerators.push_back(Narrow);
		}
		for (const auto Op : {EOpcode::Div, EOpcode::Mod})
		{
			for (const unsigned Mode : {0u, 1u, 2u})
			{
				const bool bPromoted = Mode != 0u;
				const bool bNarrow = Mode == 2u;
				if (bPromoted && (Divisor < INT32_MIN || Divisor > INT32_MAX))
				{
					continue;
				}
				FBytecodeModule Module;
				FBytecodeConstant Constant;
				Constant.Kind = bPromoted ? EConstantKind::I32 : EConstantKind::I64;
				if (bPromoted)
				{
					Constant.I32 = static_cast<std::int32_t>(Divisor);
				}
				else
				{
					Constant.I64 = Divisor;
				}
				Module.Constants.push_back(Constant);
				FBytecodeFunction Function;
				Function.Name = "divide";
				Function.ClosureTypeName = bNarrow ? "fn(int)->int" : "fn(int64)->int64";
				Function.ParamCount = 1;
				Function.LocalCount = 1;
				Function.bReturnsVoid = false;
				Function.Code = {static_cast<std::uint8_t>(EOpcode::LoadLocal), 0, 0,
				    static_cast<std::uint8_t>(EOpcode::ConstI32), 0, 0,
				    static_cast<std::uint8_t>(Op), static_cast<std::uint8_t>(EOpcode::Return)};
				Module.Functions.push_back(std::move(Function));
				FTypedLoopPlan Plan;
				Plan.MaxStackDepth = 2;
				Plan.Instructions.resize(4);
				for (std::uint32_t Index = 0; Index < 4u; ++Index)
				{
					Plan.Instructions[Index].NextInstruction = Index + 1u;
					Plan.Instructions[Index].BranchInstruction = 4u;
				}
				Plan.Instructions[0].Opcode = EOpcode::LoadLocal;
				Plan.Instructions[1].Opcode = EOpcode::ConstI32;
				Plan.Instructions[1].EntryStackDepth = 1;
				if (bPromoted)
				{
					Plan.Instructions[1].Immediate = static_cast<std::int32_t>(Divisor);
				}
				else
				{
					Plan.Instructions[1].WideImmediateKind = ETypedLoopLocalKind::I64;
					Plan.Instructions[1].Immediate64 = static_cast<std::uint64_t>(Divisor);
				}
				Plan.Instructions[2].Opcode = Op;
				Plan.Instructions[2].BytecodeOffset = 6;
				Plan.Instructions[2].EntryStackDepth = 2;
				Plan.Instructions[3].Opcode = EOpcode::StoreLocal;
				Plan.Instructions[3].EntryStackDepth = 1;
				Plan.Instructions[3].Operand0 = 1;
				JitNative::FArena Arena;
				const auto Scalar = Arena.EmitLinearMixedCfg(Module, Module.Functions[0]);
				const auto Wide = Arena.EmitTypedLoopWide(Plan);
				if (!Expect(Scalar && Wide, "emit native I32/I64 constant division paths")) return EXIT_FAILURE;
				const auto ArithmeticKind = bNarrow ? ETypedLoopLocalKind::I32 : ETypedLoopLocalKind::I64;
				const ETypedLoopLocalKind Kinds[]{ArithmeticKind,
				    bPromoted ? ETypedLoopLocalKind::I32 : ETypedLoopLocalKind::I64,
				    ArithmeticKind, ArithmeticKind};
				for (const auto Numerator : Numerators)
				{
					if (bNarrow && (Numerator < INT32_MIN || Numerator > INT32_MAX))
					{
						continue;
					}
					const auto ExpectedStatus = Divisor == 0                            ? EJitRuntimeStatus::DivisionByZero
					    : Numerator == (bNarrow ? INT32_MIN : Minimum) && Divisor == -1 ? EJitRuntimeStatus::IntegerOverflow
					                                                                    : EJitRuntimeStatus::Ok;
					const auto Expected = ExpectedStatus != EJitRuntimeStatus::Ok ? 0u
					                                                              : static_cast<std::uint64_t>(Op == EOpcode::Div ? Numerator / Divisor : Numerator % Divisor);
					std::uint64_t Locals[]{static_cast<std::uint64_t>(Numerator), 0};
					std::uint64_t Stack[2]{};
					std::uint64_t Result = 0;
					const auto Failure = DecodeJitKernelFailure(Scalar(nullptr, Locals, Stack, &Result, nullptr));
					FTypedLoopNativeContext Context;
					Context.InstructionKinds = Kinds;
					Context.Instructions = Plan.Instructions.data();
					Context.Locals = Locals;
					Context.Stack = Stack;
					const auto WideStatus = static_cast<EJitRuntimeStatus>(Wide(&Context));
					if (!Expect(Failure.Status == ExpectedStatus && WideStatus == ExpectedStatus && (ExpectedStatus == EJitRuntimeStatus::Ok ? Result == (bNarrow ? static_cast<std::uint32_t>(Expected) : Expected) && Locals[1] == Expected : Failure.InstructionOffset == 6u && Context.FailureOffset == 6u),
					        "native I32/I64 quotient/remainder match independent C++ boundary and random oracle"))
					{
						std::cerr << " numerator=" << Numerator << " divisor=" << Divisor
						          << " op=" << static_cast<unsigned>(Op) << " promoted=" << bPromoted << " narrow=" << bNarrow
						          << " scalar_status=" << static_cast<std::int32_t>(Failure.Status)
						          << " wide_status=" << static_cast<std::int32_t>(WideStatus)
						          << " expected=" << Expected << " scalar=" << Result << " wide=" << Locals[1] << '\n';
						return EXIT_FAILURE;
					}
				}
			}
		}
	}

	// A jump can bypass the preceding constant and carry a live divisor instead.
	// The optimization must use the CFG predecessor proof, not byte adjacency.
	for (const bool bNarrow : {false, true})
	{
		FBytecodeModule Module;
		FBytecodeConstant Constant;
		Constant.Kind = bNarrow ? EConstantKind::I32 : EConstantKind::I64;
		if (bNarrow)
		{
			Constant.I32 = 65521;
		}
		else
		{
			Constant.I64 = 65521;
		}
		Module.Constants.push_back(Constant);
		FBytecodeFunction Function;
		Function.Name = "alternate";
		Function.ClosureTypeName = bNarrow ? "fn(int,int,bool)->int" : "fn(int64,int64,bool)->int64";
		Function.ParamCount = 3;
		Function.LocalCount = 3;
		Function.bReturnsVoid = false;
		Function.Code = {static_cast<std::uint8_t>(EOpcode::LoadLocal), 0, 0,
		    static_cast<std::uint8_t>(EOpcode::LoadLocal), 1, 0,
		    static_cast<std::uint8_t>(EOpcode::LoadLocal), 2, 0,
		    static_cast<std::uint8_t>(EOpcode::JmpIfFalse), 4, 0, 0, 0,
		    static_cast<std::uint8_t>(EOpcode::Pop), static_cast<std::uint8_t>(EOpcode::ConstI32), 0, 0,
		    static_cast<std::uint8_t>(EOpcode::Div), static_cast<std::uint8_t>(EOpcode::Return)};
		Module.Functions.push_back(std::move(Function));
		JitNative::FArena Arena;
		const auto Kernel = Arena.EmitLinearMixedCfg(Module, Module.Functions[0]);
		if (!Expect(Kernel != nullptr, "emit alternate-entry division CFG")) return EXIT_FAILURE;
		FTypedLoopPlan Plan;
		Plan.MaxStackDepth = 3;
		Plan.Instructions.resize(8);
		const EOpcode Opcodes[]{EOpcode::LoadLocal, EOpcode::LoadLocal, EOpcode::LoadLocal,
		    EOpcode::JmpIfFalse, EOpcode::Pop, EOpcode::ConstI32, EOpcode::Div, EOpcode::StoreLocal};
		const std::uint16_t Depths[]{0, 1, 2, 3, 2, 1, 2, 1};
		const auto IntegerKind = bNarrow ? ETypedLoopLocalKind::I32 : ETypedLoopLocalKind::I64;
		const ETypedLoopLocalKind Kinds[]{IntegerKind, IntegerKind,
		    ETypedLoopLocalKind::Bool, ETypedLoopLocalKind::Bool, IntegerKind,
		    IntegerKind, IntegerKind, IntegerKind};
		for (std::uint32_t Index = 0; Index < 8u; ++Index)
		{
			Plan.Instructions[Index].Opcode = Opcodes[Index];
			Plan.Instructions[Index].EntryStackDepth = Depths[Index];
			Plan.Instructions[Index].NextInstruction = Index + 1u;
			Plan.Instructions[Index].BranchInstruction = 8u;
		}
		Plan.Instructions[1].Operand0 = 1;
		Plan.Instructions[2].Operand0 = 2;
		Plan.Instructions[3].BranchInstruction = 6;
		if (bNarrow)
		{
			Plan.Instructions[5].Immediate = 65521;
		}
		else
		{
			Plan.Instructions[5].WideImmediateKind = ETypedLoopLocalKind::I64;
			Plan.Instructions[5].Immediate64 = 65521;
		}
		Plan.Instructions[7].Operand0 = 3;
		const auto Wide = Arena.EmitTypedLoopWide(Plan);
		if (!Expect(Wide != nullptr, "emit alternate-entry typed-wide division")) return EXIT_FAILURE;
		for (const bool bConstant : {false, true})
		{
			std::uint64_t Locals[]{987654321, 17, static_cast<std::uint64_t>(bConstant), 0};
			std::uint64_t Stack[3]{};
			std::uint64_t Result = 0;
			FTypedLoopNativeContext Context;
			Context.Instructions = Plan.Instructions.data();
			Context.InstructionKinds = Kinds;
			Context.Locals = Locals;
			Context.Stack = Stack;
			if (!Expect(Kernel(nullptr, Locals, Stack, &Result, nullptr) == 0 && Wide(&Context) == 0 && Result == 987654321u / (bConstant ? 65521u : 17u) && Locals[3] == Result,
			        "alternate CFG entry retains its dynamic divisor"))
			{
				return EXIT_FAILURE;
			}
		}
	}
}
#endif

{
	const std::string Source =
	    "int64 scalar(int count, int64 seed) { int64 state = seed; for (int i = 0; i < count; i++) { "
	    "state = (state * 75i64 + 74i64) % 65521i64; } return state; } "
	    "int64 owned(int count, int64 seed) { array<int> values = array<int>(); values.push(0); "
	    "int64 state = seed; for (int i = 0; i < count; i++) { "
	    "state = (state * 75i64 + 74i64) % 65521; values[0] = to_int(state); } return state; } "
	    "void main() { print(scalar(257,17i64)); print(owned(311,-29i64)); }";
	std::string Expected;
	for (const auto Input : {std::pair<int, std::int64_t>{257, 17}, std::pair<int, std::int64_t>{311, -29}})
	{
		auto State = Input.second;
		for (int Index = 0; Index < Input.first; ++Index)
		{
			State = (State * 75 + 74) % 65521;
		}
		Expected += std::to_string(State) + '\n';
	}
	const auto Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile scalar and owned I64 constant-divisor loops")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Module, Backend);
		if (!Expect(Capture.bSuccess && Capture.Output == Expected,
		        "signed I64 constant-divisor loops preserve backend results"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.ScalarFunctionFastPathMixed != 0u && Capture.JitStats.NativeTypedLoopExecutions != 0u, "both scalar CFG and typed-wide loops execute native code"))
		{
			std::cerr << " mixed_calls=" << Capture.JitStats.ScalarFunctionFastPathMixed
			          << " typed_native=" << Capture.JitStats.NativeTypedLoopExecutions << '\n';
			return EXIT_FAILURE;
		}
		#endif
	}
}

{
	const auto Module = CompileSource(
	    "int work(int count, int seed) { array<int> values = {1,2,3,4,5}; int state = seed; int total = 0; "
	    "for (int i = 0; i < count; i++) { state = (state * 75 + 74) % 65521; int64 key = state % 5; "
	    "int value = (values[key] + state) % 997; values[key] = value; total = (total + value) % 1000003; } "
	    "return total + state; } void main() { print(work(199,23)); }");
	if (!Expect(static_cast<bool>(Module), "compile I32 arithmetic loop with full-width indices")) return EXIT_FAILURE;
	std::array<int, 5> Values{1, 2, 3, 4, 5};
	int State = 23;
	int Total = 0;
	for (int Index = 0; Index < 199; ++Index)
	{
		State = (State * 75 + 74) % 65521;
		auto& Value = Values[static_cast<std::size_t>(State % 5)];
		Value = (Value + State) % 997;
		Total = (Total + Value) % 1000003;
	}
	const auto Expected = std::to_string(Total + State) + '\n';
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Module, Backend);
		if (!Expect(Capture.bSuccess && Capture.Output == Expected,
		        "I32 constant division with full-width indices preserves backend results"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.NativeTypedLoopExecutions != 0u, "I32 arithmetic in a wide-index loop executes the actual native kernel"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
}

for (const auto Source : {
         "int64 fail(int64 value) { return value / 0i64; } void main() { print(fail(1i64)); }",
         "int64 fail(int64 value) { return value % -1i64; } void main() { "
         "int64 value = -9223372036854775807i64; value -= 1i64; print(fail(value)); }"})
{
	if (!ExpectDifferentialRun(Source, "constant I64 division errors retain VM source diagnostics")) return EXIT_FAILURE;
}
