// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Directly test the production outlined kernel, not a look-alike test algorithm.
{
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.NativeArena().Reset();
	if (!Expect(Cache.NativeArena().ReservedBytes() == 0, "empty arena reset remains empty"))
	{
		return EXIT_FAILURE;
	}
	auto Module = CompileSource("int add(int a,int b){return a+b;}void main(){print(add(1,2));}");
	if (!Expect(static_cast<bool>(Module), "arena reset source"))
	{
		return EXIT_FAILURE;
	}
	for (int Round = 0; Round < 3; ++Round)
	{
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		#if VEROSCRIPT_HAS_ASMJIT
		if (!Expect(Cache.NativeArena().ReservedBytes() > 0, "compiled arena owns native pages"))
		{
			return EXIT_FAILURE;
		}
		#endif
		Cache.InvalidateAll();
		if (!Expect(Cache.NativeArena().ReservedBytes() == 0 && Cache.CompiledCount() == 0,
				"nonempty invalidation releases pages and entries"))
		{
			return EXIT_FAILURE;
		}
	}
}
#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
{
	using namespace VeroScript::AsmJit::JitLinear;
	using I32 = std::int32_t;
	constexpr I32 Min = std::numeric_limits<I32>::min();
	constexpr I32 Max = std::numeric_limits<I32>::max();
	VeroScript::FSet::FIntScalarElements Values;
	for (I32 N = -32; N <= 32; ++N)
	{
		Values.Insert(N);
	}
	FTypedLoopPlan::FDirectAffineSetI32 Plan;
	Plan.StateMulInstruction = 11;
	Plan.StateAddInstruction = 22;
	Plan.StateModInstruction = 33;
	Plan.TotalIncrementInstruction = 44;
	Plan.CounterInstruction = 55;
	const auto Reference = [&](I32 Counter, I32 Limit, I32 State, I32 Total)
	{
		const auto Fail = [&](std::uint16_t Ip, const char* Error)
		{
			return FAffineSetResult{Counter, State, Total, Ip, Error};
		};
		while (Counter < Limit)
		{
			const std::int64_t Product = std::int64_t{State} * Plan.StateMultiplier;
			if (Product < Min || Product > Max)
			{
				return Fail(11, "integer overflow");
			}
			const std::int64_t Sum = Product + Plan.StateIncrement;
			if (Sum < Min || Sum > Max)
			{
				return Fail(22, "integer overflow");
			}
			if (Plan.StateModulus == 0)
			{
				return Fail(33, "division by zero");
			}
			if (Sum == Min && Plan.StateModulus == -1)
			{
				return Fail(33, "integer overflow");
			}
			State = static_cast<I32>(Sum % Plan.StateModulus);
			if (State >= -32 && State <= 32)
			{
				if (Total == Max)
				{
					return Fail(44, "integer overflow");
				}
				++Total;
			}
			++Counter; // Counter < int32 Limit proves this increment cannot overflow.
		}
		return FAffineSetResult{Counter, State, Total, 0, nullptr};
	};
	const auto Verify = [&](I32 Counter, I32 Limit, I32 State, I32 Total)
	{
		const auto Expected = Reference(Counter, Limit, State, Total);
		const auto Actual = TestExecuteAffineSetKernel(Plan, Values.BorrowReadOnlyProbe(),
			Counter, Limit, State, Total);
		return Expect(Actual.Counter == Expected.Counter && Actual.State == Expected.State
			&& Actual.Total == Expected.Total && Actual.FailureInstruction == Expected.FailureInstruction
			&& std::string(Actual.Error ? Actual.Error : "") == (Expected.Error ? Expected.Error : ""),
			"affine kernel must preserve exact state and first failing instruction");
	};
	for (const I32 Divisor : {Min, -257, -1, 0, 1, 2, 7, 256, 257, Max})
	{
		for (const I32 Multiplier : {-3, 0, 1, 2, 7, Max})
		{
			for (const I32 Increment : {Min, -3, 0, 2, Max})
			{
				for (const I32 State : {Min, -1073741824, -1, 0, 1, 1073741823, Max})
				{
					Plan.StateModulus = Divisor;
					Plan.StateMultiplier = Multiplier;
					Plan.StateIncrement = Increment;
					if (!Verify(0, 17, State, 0) || !Verify(0, 2, State, Max)
									|| !Verify(Max, Max, State, Max) || !Verify(Max - 1, Max, State, 0))
					{
						return EXIT_FAILURE;
					}
				}
			}
		}
	}
	// Fail on a later iteration, after successful state/total/counter commits.
	// Increment 1 would reach Max exactly and wrap to zero before overflowing.
	// Increment 2 instead exceeds the multiplication bound on a later iteration.
	Plan.StateMultiplier = 2;
	Plan.StateIncrement = 2;
	Plan.StateModulus = Max;
	const auto LaterFailure = Reference(0, 40, 1, 0);
	if (!Expect(LaterFailure.Error != nullptr && LaterFailure.Counter > 0
			&& LaterFailure.FailureInstruction == Plan.StateMulInstruction,
			"later-failure fixture must actually fail after committed iterations"))
	{
		return EXIT_FAILURE;
	}
	if (!Verify(0, 40, 1, 0))
	{
		return EXIT_FAILURE;
	}
}
#endif
