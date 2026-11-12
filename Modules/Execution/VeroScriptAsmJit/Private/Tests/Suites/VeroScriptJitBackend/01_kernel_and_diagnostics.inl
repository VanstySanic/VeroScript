// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptJitBackendTests / kernel and diagnostics suite. Included by the owning translation unit.

	{
		using namespace VeroScript::AsmJit::JitLinear;
		using K = ETypedLoopLocalKind;
		std::vector<FTypedLoopInstruction> Instructions(5);
		Instructions[1].EntryStackDepth = 2;
		Instructions[2].EntryStackDepth = 2;
		Instructions[3].EntryStackDepth = 1;
		FPackedKindStates States(Instructions, 2);
		std::vector<K> Stack{K::String};
		bool Added = true;
		if (!Expect(States.IsValid() && States.StorageSlots() == 5u &&
						!States.Load(1u, Stack) && Stack == std::vector<K>{K::String} &&
						States.Merge(0u, {}, Added) && Added,
				"packed CFG kinds retain sparse entry depths and decline uninitialized reads"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(States.Merge(1u, {K::I32, K::String}, Added) && Added &&
						States.Merge(2u, {K::I32, K::String}, Added) && Added &&
						States.Merge(3u, {K::I32}, Added) && Added &&
						States.Merge(1u, {K::I32, K::String}, Added) && !Added,
				"packed CFG states preserve independent branch entries and identical joins"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!States.Merge(1u, {K::U32, K::String}, Added) && !Added &&
						!States.Merge(1u, {K::I32}, Added) && !Added &&
						!States.Merge(3u, {K::String}, Added) && !Added &&
						States.Load(1u, Stack) && Stack == std::vector<K>{K::I32, K::String},
				"mismatched kind or depth joins decline without changing the original entry snapshot"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!States.Merge(5u, {}, Added) && !Added && !States.Load(5u, Stack) &&
						States.Merge(4u, {}, Added) && Added && States.Load(4u, Stack) && Stack.empty(),
				"packed states validate indices and handle empty exits without pointer arithmetic"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FTypedLoopInstruction> Zero(3);
		FPackedKindStates EmptyStacks(Zero, 0);
		if (!Expect(EmptyStacks.IsValid() && EmptyStacks.StorageSlots() == 0u &&
						EmptyStacks.Merge(0u, {}, Added) && EmptyStacks.Merge(1u, {}, Added) &&
						EmptyStacks.Load(1u, Stack) && Stack.empty(),
				"zero-depth native CFGs keep empty packed state storage"))
		{
			return EXIT_FAILURE;
		}
		Zero[1].EntryStackDepth = 3;
		FPackedKindStates InvalidDepth(Zero, 2);
		if (!Expect(!InvalidDepth.IsValid() && !InvalidDepth.Merge(0u, {}, Added),
				"packed kind construction rejects depths beyond the verified maximum"))
		{
			return EXIT_FAILURE;
		}
	}


	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	{
		using namespace VeroScript;
		using namespace VeroScript::AsmJit;
		std::vector<JitLinear::FTypedLoopInstruction> Instructions(5);
		Instructions[0].Opcode = EOpcode::LoadLocal;
		Instructions[1].Opcode = EOpcode::ConstI32;
		Instructions[1].Immediate = 3;
		Instructions[2].Opcode = EOpcode::Mul;
		Instructions[2].BytecodeOffset = 12;
		Instructions[3].Opcode = EOpcode::LoadLocal;
		Instructions[3].Operand0 = 1;
		Instructions[4].Opcode = EOpcode::Add;
		JitNative::FArena Arena;
		const auto Kernel = Arena.EmitPureI32Expression(Instructions);
		if (!Expect(Kernel != nullptr, "emit pure integer expression ABI fixture"))
		{
			return EXIT_FAILURE;
		}
		std::array<std::int32_t, 3> Output{71, -1, 93};
		std::array<std::uint32_t, 3> Failure{17, 99, 23};
		if (!Expect(Kernel(7, 11, &Output[1], &Failure[1]) == 0
				&& Output == std::array<std::int32_t, 3>{71, 32, 93}
				&& Failure == std::array<std::uint32_t, 3>{17, 99, 23},
				"pure integer expression preserves argument and output ABI"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Kernel(std::numeric_limits<std::int32_t>::max(), 11,
				&Output[1], &Failure[1]) == static_cast<std::int32_t>(EJitRuntimeStatus::IntegerOverflow)
				&& Output == std::array<std::int32_t, 3>{71, 32, 93}
				&& Failure == std::array<std::uint32_t, 3>{17, 12, 23},
				"pure integer expression preserves failure pointer ABI and source offset"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif

	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	{
		using namespace VeroScript;
		using namespace VeroScript::AsmJit;
		// Constant division uses the shared checked signed lowering, including
		// negative/extreme divisors. Stores preceding an error remain committed.
		for (const auto Op : {EOpcode::Div, EOpcode::Mod})
		{
			for (const std::int32_t Divisor : {0, 1, -1, 2, -2, 3, -3, 7, -7, 31, -31, 997, -997,
				std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()})
			{
				std::vector<JitLinear::FTypedLoopInstruction> Instructions(7);
				Instructions[0].Opcode = EOpcode::LoadThisField;
				Instructions[1].Opcode = EOpcode::ConstI32;
				Instructions[1].Immediate = 1;
				Instructions[2].Opcode = EOpcode::Add;
				Instructions[3].Opcode = EOpcode::StoreThisField;
				Instructions[4].Opcode = EOpcode::LoadLocal;
				Instructions[5].Opcode = EOpcode::ConstI32;
				Instructions[5].Immediate = Divisor;
				Instructions[6].Opcode = Op;
				Instructions[6].BytecodeOffset = 73;
				JitNative::FArena Arena;
				const auto Kernel = Arena.EmitFieldI32Expression(Instructions);
				if (!Expect(Kernel != nullptr, "emit checked constant field arithmetic")) return EXIT_FAILURE;
				std::uint32_t Random = 0x2938A5D7u;
				for (std::size_t Index = 0; Index < 80u; ++Index)
				{
					Random = Random * 1664525u + 1013904223u;
					const std::array<std::int32_t, 8> Boundaries{0, 1, -1, 7, -7,
						std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min() + 1,
						std::numeric_limits<std::int32_t>::max()};
					const std::int32_t Input = Index < Boundaries.size() ? Boundaries[Index] : static_cast<std::int32_t>(Random);
					const bool Overflow = Input == std::numeric_limits<std::int32_t>::min() && Divisor == -1;
					const auto ExpectedStatus = Divisor == 0 ? EJitRuntimeStatus::DivisionByZero : Overflow
						? EJitRuntimeStatus::IntegerOverflow : EJitRuntimeStatus::Ok;
					std::int32_t Field = 17, Output = 29;
					std::array<std::int32_t*, 1> Fields{&Field};
					JitLinear::FTypedLoopNativeMethodBinding::FFieldState State{Fields.data(), 0};
					std::uint32_t Failure = 41;
					const auto Status = Kernel(Input, &State, &Output, &Failure);
					const auto ExpectedOutput = ExpectedStatus != EJitRuntimeStatus::Ok ? 29 : static_cast<std::int32_t>(
						Op == EOpcode::Div ? static_cast<std::int64_t>(Input) / Divisor : static_cast<std::int64_t>(Input) % Divisor);
					if (!Expect(Status == static_cast<std::int32_t>(ExpectedStatus) && Field == 18 && State.MutationCount == 1u &&
									Output == ExpectedOutput && Failure == (ExpectedStatus == EJitRuntimeStatus::Ok ? 41u : 73u),
							"constant field division preserves signed boundaries, prior writes and exact failure offset"))
					{
						return EXIT_FAILURE;
					}
				}
			}
		}
	}
	#endif

	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	{
		using namespace VeroScript;
		using namespace VeroScript::AsmJit;
		std::vector<JitLinear::FTypedLoopInstruction> Instructions(9);
		Instructions[0].Opcode = EOpcode::LoadThisField;
		Instructions[1].Opcode = EOpcode::LoadLocal;
		Instructions[2].Opcode = EOpcode::Add;
		Instructions[3].Opcode = EOpcode::StoreThisField;
		Instructions[4].Opcode = EOpcode::LoadThisField;
		Instructions[4].Operand0 = 1;
		Instructions[5].Opcode = EOpcode::LoadLocal;
		Instructions[6].Opcode = EOpcode::Div;
		Instructions[6].BytecodeOffset = 23;
		Instructions[7].Opcode = EOpcode::StoreThisField;
		Instructions[7].Operand0 = 1;
		Instructions[8].Opcode = EOpcode::LoadThisField;
		JitNative::FArena Arena;
		const auto Kernel = Arena.EmitFieldI32Expression(Instructions);
		std::int32_t First = 7, Second = 12, Result = -1;
		std::array<std::int32_t*, 2> Fields{&First, &Second};
		JitLinear::FTypedLoopNativeMethodBinding::FFieldState State{Fields.data(), 0};
		std::uint32_t Failure = 0;
		if (!Expect(Kernel != nullptr && Kernel(3, &State, &Result, &Failure) == 0 && First == 10 && Second == 4 && Result == 10 && State.MutationCount == 2,
				"scalar field expression commits cross-field updates through borrowed storage"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Kernel(0, &State, &Result, &Failure) == static_cast<std::int32_t>(EJitRuntimeStatus::DivisionByZero) && First == 10 && Second == 4 && Result == 10 && State.MutationCount == 3 && Failure == 23,
				"scalar field expression preserves writes before failure and callee offset"))
		{
			return EXIT_FAILURE;
		}
		// The second argument has an independent native slot; high transport bits
		// are not part of an i32 value. A later failing write must keep the first.
		Instructions[5].Operand0 = 1;
		const auto MultiKernel = reinterpret_cast<JitNative::FFieldI32ArgumentsExpressionKernel>(
			Arena.EmitFieldI32Expression(Instructions, 2));
		std::array<std::uint64_t, 2> Arguments{0xDEADBEEF00000003ull, 2u};
		if (!Expect(MultiKernel && MultiKernel(Arguments.data(), &State, &Result, &Failure) == 0 && First == 13 && Second == 2 && Result == 13 && State.MutationCount == 5,
				"multi-argument field expression preserves distinct scalar argument slots"))
		{
			return EXIT_FAILURE;
		}
		Arguments[1] = 0;
		if (!Expect(MultiKernel(Arguments.data(), &State, &Result, &Failure) == static_cast<std::int32_t>(EJitRuntimeStatus::DivisionByZero) && First == 16 && Second == 2 && Result == 13 && State.MutationCount == 6 && Failure == 23,
				"multi-argument field expression commits preceding writes exactly once on error"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif

	{
		using namespace VeroScript;
		using namespace VeroScript::AsmJit::JitLinear;
		std::array<FValue, 3> Sources{std::int32_t{7}, std::int32_t{11}, std::int32_t{13}};
		std::array<FTypedLoopNativeObjectView, 3> Views{};
		Views[2].TypeIndex = 65534u;
		const FTypedLoopNativeObjectViewRange Range{Views.data(), Views.size(), Sources.data()};
		FValue* Source = nullptr;
		const auto* Found = FindTypedLoopNativeObjectView(MakeTypedLoopNativeObjectViewToken(&Views[2]), &Range, 1, Source);
		if (!Expect(Found == &Views[2] && Source == &Sources[2] && Found->TypeIndex == 65534u,
				"compact object stamp reconstructs exact source slot without truncating type identity"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!FindTypedLoopNativeObjectView(MakeTypedLoopNativeObjectViewToken(Views.data() + Views.size()), &Range, 1, Source) && !Source,
				"object stamp rejects a one-past token before reading source storage"))
		{
			return EXIT_FAILURE;
		}
		const FTypedLoopNativeObjectViewRange Empty;
		if (!Expect(!FindTypedLoopNativeObjectView(MakeTypedLoopNativeObjectViewToken(Views.data()), &Empty, 1, Source) && !Source,
				"empty object view ranges decline without null pointer arithmetic"))
		{
			return EXIT_FAILURE;
		}
		const FTypedLoopNativeObjectViewRange Prefix{Views.data(), 1u, Sources.data()};
		if (!Expect(FindTypedLoopNativeObjectViewAt(Views.data(), 0u, &Prefix, 1u) == Views.data() &&
						!FindTypedLoopNativeObjectViewAt(Views.data(), 1u, &Prefix, 1u) &&
						!FindTypedLoopNativeObjectViewAt(nullptr, 0u, &Empty, 1u),
				"indexed object views validate prepared prefixes before pointer arithmetic"))
		{
			return EXIT_FAILURE;
		}
		FTypedLoopNativeBinding Binding{ETypedLoopLocalKind::ArrayObject, &Sources, Views.data(), Sources.size()};
		FTypedLoopNativeContext Context;
		Context.ObjectViewRanges = &Prefix;
		Context.ObjectViewRangeCount = 1u;
		const auto Container = reinterpret_cast<std::uint64_t>(&Binding);
		if (!Expect(TypedLoopNativeGetIndexI32(&Context, Container, 0u, 17u) == 0 &&
						Context.Scratch == MakeTypedLoopNativeObjectViewToken(Views.data()),
				"object index helper returns a validated compact prefix stamp"))
		{
			return EXIT_FAILURE;
		}
		const auto Previous = Context.Scratch;
		if (!Expect(TypedLoopNativeGetIndexI32(&Context, Container, 1u, 19u) != 0 && Context.Scratch == Previous &&
						Binding.NativeSize == Sources.size() && Context.FailureOffset == 19u,
				"unprepared object stamps fail without extending or truncating actual array bounds"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(TypedLoopNativeGetIndexI32(&Context, Container, Sources.size(), 23u) != 0 &&
						std::string_view(Context.FailureMessage) == "array index out of bounds" && Context.FailureOffset == 23u,
				"object bounds errors still use the actual array length"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::AsmJit::JitScratch::FStorage ScratchStorage;
		VeroScript::AsmJit::JitScratch::FStorageScope ScratchScope(ScratchStorage);
		std::int32_t* RecycledAddress = nullptr;
		{
			VeroScript::AsmJit::JitScratch::TRawScratchLease<std::int32_t> First(128);
			RecycledAddress = First.data();
			if (!Expect(RecycledAddress != nullptr, "large native scratch lease allocates"))
			{
				return EXIT_FAILURE;
			}
		}
		VeroScript::AsmJit::JitScratch::TRawScratchLease<std::int32_t> Reused(128);
		if (!Expect(Reused.data() == RecycledAddress,
				"large native scratch lease reuses its bounded thread-local buffer"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::JitScratch::TRawScratchLease<std::int32_t> Nested(128);
		if (!Expect(Nested.data() != nullptr && Nested.data() != Reused.data(),
				"nested native scratch leases remain isolated"))
		{
			return EXIT_FAILURE;
		}
	}

	// A precise native-JIT failure pins its source position against enclosing
	// trampolines.  That protection must end at the public execution boundary:
	// callers may reuse one VM for another function after observing a failure.
	{
		const auto Module = CompileSource(
			"int first_failure(int value) {\n"
			"  int zero = value - value;\n"
			"  return 10 / zero;\n"
			"}\n"
			"int second_failure(int value) {\n"
			"  int zero = value - value;\n"
			"  int marker = value + 1;\n"
			"  return marker / zero;\n"
			"}\n"
			"void main() {}\n");
		const std::uint16_t First = Module
			? FindFunctionIndex(*Module, "first_failure")
			: static_cast<std::uint16_t>(-1);
		const std::uint16_t Second = Module
			? FindFunctionIndex(*Module, "second_failure")
			: static_cast<std::uint16_t>(-1);
		if (!Expect(Module && First != static_cast<std::uint16_t>(-1)
				&& Second != static_cast<std::uint16_t>(-1),
				"compile reusable-VM JIT diagnostic fixture"))
		{
			return EXIT_FAILURE;
		}

		std::ostringstream BaselineOutput;
		std::istringstream BaselineInput;
		std::ostringstream BaselineErrors;
		VeroScript::VeroScriptVm BaselineVm(
			BaselineOutput, BaselineInput, BaselineErrors);
		VeroScript::Test::Configure(BaselineVm);
		ConfigureBackend(BaselineVm);
		BaselineVm.SetExecutionBackend(VeroScript::EExecutionBackend::Vm);
		VeroScript::FValue Ignored;
		const bool bBaselineOk = BaselineVm.ExecuteFunction(
			*Module, Second, {std::int32_t{7}}, nullptr, &Ignored);
		const VeroScript::FSourceLocation ExpectedLocation =
			BaselineVm.GetError().Location.AsSourceLocation();

		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		const bool bFirstOk = Vm.ExecuteFunction(
			*Module, First, {std::int32_t{7}}, nullptr, &Ignored);
		const VeroScript::FSourceLocation FirstLocation =
			Vm.GetError().Location.AsSourceLocation();
		const bool bSecondOk = Vm.ExecuteFunction(
			*Module, Second, {std::int32_t{7}}, nullptr, &Ignored);
		const VeroScript::FSourceLocation SecondLocation =
			Vm.GetError().Location.AsSourceLocation();
		if (!Expect(!bBaselineOk && !bFirstOk && !bSecondOk
				&& FirstLocation.Line != SecondLocation.Line
				&& SecondLocation.FileLabel == ExpectedLocation.FileLabel
				&& SecondLocation.Line == ExpectedLocation.Line
				&& SecondLocation.Column == ExpectedLocation.Column,
				"reused VM resets native-JIT error-location pinning"))
		{
			std::cerr << "first=" << FirstLocation.Line << ':' << FirstLocation.Column
				<< " second=" << SecondLocation.Line << ':' << SecondLocation.Column
				<< " expected=" << ExpectedLocation.Line << ':' << ExpectedLocation.Column
				<< " error=" << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		const auto Module = CompileSource(
			"int Calls = 0;\n"
			"int touch(double value) { Calls += 1; return (int)value; }\n"
			"void main() { (void)touch(2.75); print(Calls); print((int)3.75); }\n");
		if (!Expect(static_cast<bool>(Module), "compile C-style cast backend parity suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "1\n3\n", "C-style cast backend parity"))
			{
				return EXIT_FAILURE;
			}
		}

		// A selected interface local followed by one common call site is the
		// canonical polymorphic PIC spelling generated by visitors and state
		// machines. Both modulo and power-of-two mask selectors must preserve the
		// final receiver while allowing the JIT to reduce the proven scalar cycle.
		const auto SelectedPicModule = CompileSource(
			"interface IAdd { virtual int add(int value) = 0; }; "
			"class AddOne : IAdd { int add(int value) { return value + 1; } }; "
			"class AddTwo : IAdd { int add(int value) { return value + 2; } }; "
			"class AddThree : IAdd { int add(int value) { return value + 3; } }; "
			"class AddFour : IAdd { int add(int value) { return value + 4; } }; "
			"int masked() { IAdd a=AddOne(); IAdd b=AddTwo(); IAdd c=AddThree(); "
			"IAdd d=AddFour(); IAdd selected=a; int total=0; "
			"for (int i=0; i<16; i++) { int choice=i&3; "
			"if(choice==0) selected=a; else if(choice==1) selected=b; "
			"else if(choice==2) selected=c; else selected=d; "
			"total=selected.add(total); } return total+selected.add(0); } "
			"int modulo() { IAdd a=AddOne(); IAdd b=AddTwo(); IAdd c=AddThree(); "
			"IAdd d=AddFour(); IAdd selected=a; int total=0; "
			"for (int i=0; i<16; i++) { int choice=i%4; "
			"if(choice==0) selected=a; else if(choice==1) selected=b; "
			"else if(choice==2) selected=c; else selected=d; "
			"total=selected.add(total); } return total+selected.add(0); } "
			"int masked_offset() { IAdd a=AddOne(); IAdd b=AddTwo(); IAdd c=AddThree(); "
			"IAdd d=AddFour(); IAdd selected=a; int total=0; "
			"for (int i=1; i<10; i++) { int choice=i&3; "
			"if(choice==0) selected=a; else if(choice==1) selected=b; "
			"else if(choice==2) selected=c; else selected=d; "
			"total=selected.add(total); } return total+selected.add(0); } "
			"class SubOne : IAdd { int add(int value) { return value + -1; } }; "
			"class SubTwo : IAdd { int add(int value) { return value + -2; } }; "
			"int negative() { IAdd a=SubOne(); IAdd b=SubTwo(); IAdd selected=a; int total=0; "
			"for (int i=1; i<6; i++) { int choice=i&1; "
			"if(choice==0) selected=a; else selected=b; "
			"total=selected.add(total); } return total+selected.add(0); } "
			"void main() { print(masked()); print(modulo()); "
			"print(masked_offset()); print(negative()); }");
		if (!Expect(static_cast<bool>(SelectedPicModule),
				"compile selected-receiver polymorphic PIC regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*SelectedPicModule, Backend, "44\n44\n24\n-10\n",
					"selected-receiver mask/modulo PIC parity"))
			{
				return EXIT_FAILURE;
			}
		}
		const FExecutionCapture SelectedPicCapture =
			RunCapture(*SelectedPicModule, VeroScript::EExecutionBackend::Jit);
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		constexpr std::size_t ExpectedSelectedPicCalls = 32;
		#else
		constexpr std::size_t ExpectedSelectedPicCalls = 0;
		#endif
		if (!Expect(SelectedPicCapture.bSuccess
				&& SelectedPicCapture.JitStats.ScalarMethodFastPathI32
					>= ExpectedSelectedPicCalls,
				"selected-receiver mask/modulo PIC remains on the scalar JIT path"))
		{
			std::cerr << "success=" << SelectedPicCapture.bSuccess << " fast=" << SelectedPicCapture.JitStats.ScalarMethodFastPathI32 << " error=" << SelectedPicCapture.ErrorMessage << " id=" << SelectedPicCapture.ErrorMessageId << " output=" << SelectedPicCapture.Output << " compiled=" << SelectedPicCapture.JitStats.CompiledEntries << '\n';
			return EXIT_FAILURE;
		}

		// The selected local may initially be the sole owner of an unrelated
		// object. The loop reducer must then fall back so the first assignment
		// releases that object (and runs its destructor) at the source-visible time.
		const auto SelectedPicLifetimeModule = CompileSource(
			"interface IAdd { virtual int add(int value) = 0; }; "
			"class AddOne : IAdd { int add(int value) { return value + 1; } }; "
			"class AddTwo : IAdd { int add(int value) { return value + 2; } }; "
			"class OldSelection : IAdd { int add(int value) { return value + 9; } "
			"~OldSelection() { print(\"old-selection-destroyed\"); } }; "
			"void main() { IAdd a=AddOne(); IAdd b=AddTwo(); "
			"IAdd selected=OldSelection(); int total=0; "
			"for(int i=0; i<4; i++) { int choice=i&1; "
			"if(choice==0) selected=a; else selected=b; "
			"total=selected.add(total); } print(\"after-loop\"); print(total); }");
		if (!Expect(static_cast<bool>(SelectedPicLifetimeModule),
				"compile selected-receiver PIC lifetime regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*SelectedPicLifetimeModule, Backend,
					"old-selection-destroyed\nafter-loop\n6\n",
					"selected-receiver PIC preserves destructor timing"))
			{
				return EXIT_FAILURE;
			}
		}

		const auto SelectedPicOverflowModule = CompileSource(
			"interface IAdd { virtual int add(int value) = 0; }; "
			"class AddOne : IAdd { int add(int value) { return value + 1; } }; "
			"class AddTwo : IAdd { int add(int value) { return value + 2; } }; "
			"void main() { IAdd a=AddOne(); IAdd b=AddTwo(); IAdd selected=a; "
			"int total=2147483647; for(int i=0; i<8; i++) { int choice=i&1; "
			"if(choice==0) selected=a; else selected=b; "
			"total=selected.add(total); } }");
		if (!Expect(static_cast<bool>(SelectedPicOverflowModule),
				"compile selected-receiver PIC overflow regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Overflow =
				RunCapture(*SelectedPicOverflowModule, Backend);
			if (!Expect(!Overflow.bSuccess
					&& Overflow.ErrorMessageId == "diag.integer_overflow",
					"selected-receiver PIC preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		bool bHadExisting = false;
		std::string Saved;
		#if defined(_WIN32)
		char* Existing = nullptr;
		std::size_t ExistingLength = 0;
		if (_dupenv_s(&Existing, &ExistingLength, "VERO_MAX_CALL_DEPTH") == 0
			&& Existing != nullptr)
		{
			bHadExisting = true;
			Saved.assign(Existing);
			std::free(Existing);
		}
		_putenv_s("VERO_MAX_CALL_DEPTH", "2048");
		#else
		if (const char* const Existing = std::getenv("VERO_MAX_CALL_DEPTH"))
		{
			bHadExisting = true;
			Saved.assign(Existing);
		}
		setenv("VERO_MAX_CALL_DEPTH", "2048", 1);
		#endif
		std::ostringstream Output;
		VeroScript::VeroScriptVm ConfiguredVm(Output);
		VeroScript::Test::Configure(ConfiguredVm);
		const bool bConfigured =
			ConfiguredVm.GetMaxCallDepth() == static_cast<std::size_t>(2048);
		#if defined(_WIN32)
		_putenv_s("VERO_MAX_CALL_DEPTH", "invalid");
		#else
		setenv("VERO_MAX_CALL_DEPTH", "invalid", 1);
		#endif
		VeroScript::VeroScriptVm FallbackVm(Output);
		VeroScript::Test::Configure(FallbackVm);
		const bool bInvalidFallsBack =
			FallbackVm.GetMaxCallDepth() == VeroScript::VeroScriptVm::DefaultMaxCallDepth;
		#if defined(_WIN32)
		_putenv_s("VERO_MAX_CALL_DEPTH", bHadExisting ? Saved.c_str() : "");
		#else
		if (bHadExisting)
		{
			setenv("VERO_MAX_CALL_DEPTH", Saved.c_str(), 1);
		}
		else
		{
			unsetenv("VERO_MAX_CALL_DEPTH");
		}
		#endif
		if (!Expect(bConfigured, "VERO_MAX_CALL_DEPTH configures new VMs")
			|| !Expect(bInvalidFallsBack, "invalid call-depth environment falls back safely"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// One-shot eval lowers InitCode through an ephemeral function so top-level
		// loops use the selected JIT. Ordinary Eval remains the persistent/REPL path.
		const auto Module = CompileSource(
			"int total = 0; "
			"for (int i = 0; i < 10000; i++) { total++; } "
			"print(total);\n");
		if (!Expect(static_cast<bool>(Module), "compile one-shot eval JIT suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
			const bool bExecuted =
				Vm.Execute(*Module, VeroScript::ERunMode::EvalOnce);
			const VeroScript::FJitStats Stats = Vm.GetJitStats();
			#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
			const bool bUsedExpectedBackend = Stats.Dispatches > 0;
			#else
			const bool bUsedExpectedBackend = Stats.CostModelVmFallbacks > 0;
			#endif
			if (!Expect(bExecuted, "one-shot eval executes")
				|| !Expect(Output.str() == "10000\n", "one-shot eval output")
				|| !Expect(Stats.CostModelVmFallbacks <= Stats.Fallbacks,
					"one-shot cost-model fallbacks remain a subset of all fallbacks")
				|| !Expect(bUsedExpectedBackend,
					"one-shot eval honors the selected profitable backend"))
			{
				std::cerr << Errors.str() << Vm.GetError().Message << '\n';
				return EXIT_FAILURE;
			}
		}
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
			if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Eval),
					"persistent eval executes")
				|| !Expect(Output.str() == "10000\n", "persistent eval output")
				|| !Expect(Vm.GetJitStats().Dispatches == 0,
					"persistent eval keeps module initialization in VM"))
			{
				std::cerr << Errors.str();
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Differential coverage for rich top-level state: the temporary module/JIT
		// path must preserve values, ownership, diagnostics, and exit semantics.
		const std::vector<std::string_view> Sources = {
			"int n = 1; for (int i = 0; i < 8; i++) { n += i; } print(n);\n",
			"array<int> values = array<int>(); values.push(2); values.push(5); "
				"values[1] += values[0]; print(values[1]);\n",
			"class Tracked { public: ~Tracked() { print(\"destroyed\"); } }; "
				"Tracked value = Tracked(); print(\"initialized\");\n",
			"print(\"before\"); exit(7); print(\"after\");\n"};
		for (const std::string_view Source : Sources)
		{
			const auto Module = CompileSource(std::string(Source));
			if (!Expect(static_cast<bool>(Module), "compile one-shot eval differential"))
			{
				return EXIT_FAILURE;
			}
			const FExecutionCapture Baseline = RunCapture(
				*Module,
				VeroScript::EExecutionBackend::Vm,
				{},
				VeroScript::ERunMode::Eval);
			for (const VeroScript::EExecutionBackend Backend :
				{VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
			{
				const FExecutionCapture Candidate = RunCapture(
					*Module,
					Backend,
					{},
					VeroScript::ERunMode::EvalOnce);
				if (!Expect(Candidate.bSuccess == Baseline.bSuccess
						&& Candidate.bExitRequested == Baseline.bExitRequested
						&& Candidate.ExitCode == Baseline.ExitCode
						&& Candidate.Output == Baseline.Output
						&& Candidate.Errors.empty() == Baseline.Errors.empty()
						&& Candidate.ErrorMessageId == Baseline.ErrorMessageId,
						"one-shot eval matches VM semantics"))
				{
					std::cerr << "source=" << Source
						<< " backend=" << VeroScript::ExecutionBackendName(Backend)
						<< " candidate-output=" << Candidate.Output
						<< " candidate-errors=" << Candidate.Errors
						<< " baseline-output=" << Baseline.Output
						<< " baseline-errors=" << Baseline.Errors;
					return EXIT_FAILURE;
				}
			}
		}
	}

	{
		// Differential semantics corpus: every Mixed/Jit result must match the VM,
		// including ref aliases, boxed containers, closures, and early exit.
		const std::vector<std::string_view> Sources = {
			"void bump(ref int x) { x = x + 1; }\n"
				"void main() { int n = 4; bump(ref n); print(n); }\n",
			"int RefGlobal = 2; int& get_ref() { return RefGlobal; }\n"
				"class RefOwner { public: int value = 4; int& get() { return this.value; } "
				"void set() { get() += 3; } };\n"
				"void main() { get_ref() = 6; int& r = get_ref(); r++; "
				"RefOwner o = RefOwner(); o.set(); o.get()++; print(RefGlobal); print(o.value); }\n",
			"void main() { map<string,int> m = map<string,int>(); m[\"x\"] = 7; "
				"array<pair<string,int>> es = m.entries(); print(es[0].second); }\n",
			"void main() { int x = 3; fn(int)->int f = fn(int y) -> int => x + y; print(f(4)); }\n",
			"class CallableBox<T,U> { public: T callback; U value; "
				"CallableBox(T input, U initial) { callback = input; value = initial; } "
				"U get() { return this.value; } };\n"
				"void main() { fn(int,int)->int add = fn(int a, int b) -> int => a + b; "
				"CallableBox<fn(int,int)->int,int> box = "
				"CallableBox<fn(int,int)->int,int>(add, 7); print(box.get()); }\n",
			"void main() { array<int> a = array<int>(); a.push(2); a.push(5); "
				"a[1] = a[0] + a[1]; print(a[1]); }\n",
			"struct ReadonlyCounter { int value = 1; void set(int next) { value = next; } };\n"
				"void main() { readonly<ReadonlyCounter> view = "
				"readonly<ReadonlyCounter>(ReadonlyCounter()); "
				"const_cast<ReadonlyCounter>(view.get()).set(12); print(view.get().value); "
				"const array<int> values = {1}; const_cast<array<int>>(values).push(2); "
				"print(len(values)); }\n",
			"class Tracked { public: ~Tracked() { print(\"destroyed\"); } }\n"
				"void discard(Tracked value) {}\n"
				"void main() { discard(Tracked()); print(\"main\"); }\n",
			"class Receiver { public: void ping() {} ~Receiver() { print(\"receiver\"); } }\n"
				"void use_receiver() { Receiver value = Receiver(); value.ping(); }\n"
				"void main() { use_receiver(); print(\"main\"); }\n",
			"class SharedTracked { public: int id; SharedTracked(int value) { id = value; } "
				"~SharedTracked() { print(id); } }\n"
				"void main() { SharedTracked first = SharedTracked(1); "
				"SharedTracked alias = first; first = SharedTracked(2); print(alias.id); }\n",
			"class Temporary { public: void ping() { print(\"ping\"); } "
				"~Temporary() { print(\"temporary\"); } }\n"
				"void main() { Temporary().ping(); Temporary(); print(\"main\"); }\n",
			"class Element { public: ~Element() { print(\"element\"); } }\n"
				"void main() { array<Element> values = array<Element>(); "
				"values.push(Element()); print(\"main\"); }\n",
			"void main() { exit(7); print(1); }\n"};
		for (const std::string_view Source : Sources)
		{
			if (!ExpectDifferentialRun(Source, Source.data()))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// A capture-free closure has no frame-local ownership to promote and can
		// use the ownership-aware Linear executor. Capturing closures remain on the
		// authoritative VM until capture-cell escape ownership is represented.
		const auto Module = CompileSource(
			"int invoke() { fn(int)->int f = fn(int x) -> int => x + 1; return f(4); }\n"
			"void main() { print(invoke()); }\n");
		if (!Expect(static_cast<bool>(Module), "compile closure ownership classification"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::uint16_t Invoke = FindFunctionIndex(*Module, "invoke");
		if (!Expect(Invoke != static_cast<std::uint16_t>(-1)
				&& Cache.EnsureCompiled(*Module, Invoke) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"compile closure ownership entry"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Invoke);
		if (!Expect(Entry != nullptr
				&& Entry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline,
				"capture-free closure uses ownership-aware Linear trampoline")
			|| !ExpectRun(*Module, VeroScript::EExecutionBackend::Vm, "5\n",
				"capture-free closure VM parity")
			|| !ExpectRun(*Module, VeroScript::EExecutionBackend::Mixed, "5\n",
				"capture-free closure Mixed parity")
			|| !ExpectRun(*Module, VeroScript::EExecutionBackend::Jit, "5\n",
				"capture-free closure JIT parity"))
		{
			return EXIT_FAILURE;
		}

		const auto CapturingModule = CompileSource(
			"int invoke_capture(int seed) { "
			"fn(int)->int f = fn(int x) -> int => x + seed; return f(4); }\n"
			"void main() { print(invoke_capture(3)); }\n");
		if (!Expect(static_cast<bool>(CapturingModule),
				"compile capturing closure ownership classification"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache CapturingCache;
		const std::uint16_t CapturingInvoke =
			FindFunctionIndex(*CapturingModule, "invoke_capture");
		if (!Expect(CapturingInvoke != static_cast<std::uint16_t>(-1)
				&& CapturingCache.EnsureCompiled(*CapturingModule, CapturingInvoke)
					== VeroScript::AsmJit::EJitCompileStatus::Ok
				&& CapturingCache.GetCandidate(CapturingInvoke) != nullptr
				&& CapturingCache.GetCandidate(CapturingInvoke)->Kind
					== VeroScript::AsmJit::EJitEntryKind::VmTrampoline,
				"capturing closure retains ownership-safe VM trampoline")
			|| !ExpectRun(*CapturingModule, VeroScript::EExecutionBackend::Jit, "7\n",
				"capturing closure JIT fallback parity"))
		{
			return EXIT_FAILURE;
		}

		const auto ExitModule = CompileSource(
			"int stop_from_closure() { exit(17); return 0; }\n"
			"int invoke_closure_exit() { "
			"fn()->int f = fn() -> int => stop_from_closure(); return 1 + f(); }\n"
			"void main() { print(invoke_closure_exit()); assert(false); }\n");
		if (!Expect(static_cast<bool>(ExitModule),
				"compile capture-free closure exit propagation"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache ExitCache;
		const std::uint16_t ExitInvoke =
			FindFunctionIndex(*ExitModule, "invoke_closure_exit");
		if (!Expect(ExitInvoke != static_cast<std::uint16_t>(-1)
				&& ExitCache.EnsureCompiled(*ExitModule, ExitInvoke)
					== VeroScript::AsmJit::EJitCompileStatus::Ok
				&& ExitCache.GetCandidate(ExitInvoke) != nullptr
				&& ExitCache.GetCandidate(ExitInvoke)->Kind
					== VeroScript::AsmJit::EJitEntryKind::LinearTrampoline,
				"capture-free closure exit uses Linear trampoline"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*ExitModule, Backend);
			if (!Expect(Capture.bSuccess && Capture.bExitRequested
					&& Capture.ExitCode == 17 && Capture.Output.empty()
					&& Capture.ErrorMessageId.empty(),
					"capture-free closure exit unwinds cleanly"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// A destructor failure reports the first error but must not suppress
		// independent remaining local destructors during VM/JIT frame unwind.
		const auto Module = CompileSource(
			"class CleanupProbe {\n"
			"public:\n"
			"\tint id = 0;\n"
			"\tCleanupProbe(int value) { id = value; }\n"
			"\t~CleanupProbe() { print(id); if (id == 2) { assert(false); } }\n"
			"}\n"
			"void fail_cleanup() {\n"
			"\tCleanupProbe first = CleanupProbe(1);\n"
			"\tCleanupProbe second = CleanupProbe(2);\n"
			"}\n"
			"void main() { fail_cleanup(); }\n");
		if (!Expect(static_cast<bool>(Module), "compile destructor unwind suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::uint16_t FunctionIndex = FindFunctionIndex(*Module, "fail_cleanup");
		if (!Expect(
				FunctionIndex != static_cast<std::uint16_t>(-1)
					&& Cache.EnsureCompiled(*Module, FunctionIndex)
						== VeroScript::AsmJit::EJitCompileStatus::Ok
					&& Cache.GetCandidate(FunctionIndex) != nullptr
					&& Cache.GetCandidate(FunctionIndex)->Kind
						== VeroScript::AsmJit::EJitEntryKind::LinearTrampoline,
				"destructor unwind exercises LinearTrampoline"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(
					!Capture.bSuccess && Capture.Output == "2\n1\n"
						&& !Capture.ErrorMessageId.empty(),
					"destructor unwind preserves first error and releases remaining locals"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " success=" << Capture.bSuccess
					<< " error-id=" << Capture.ErrorMessageId
					<< " output=" << Capture.Output << " errors=" << Capture.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Runtime errors can stop a frame before its normal return opcode. The
		// top-level execution cleanup must still unwind its locals in reverse order.
		const auto Module = CompileSource(
			"class AbortProbe {\n"
			"public:\n"
			"\tint id = 0;\n"
			"\tAbortProbe(int value) { id = value; }\n"
			"\t~AbortProbe() { print(id); }\n"
			"}\n"
			"void fail_body() {\n"
			"\tAbortProbe first = AbortProbe(1);\n"
			"\tAbortProbe second = AbortProbe(2);\n"
			"\tassert(false);\n"
			"}\n"
			"void main() { fail_body(); }\n");
		if (!Expect(static_cast<bool>(Module), "compile aborted-frame teardown suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(
					!Capture.bSuccess && Capture.Output == "2\n1\n"
						&& !Capture.ErrorMessageId.empty(),
					"runtime failure releases aborted-frame locals"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " output=" << Capture.Output << " errors=" << Capture.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Even when the user destructor body fails, language-owned fields still
		// receive their destruction pass. This is the script equivalent of an
		// exception-safe object teardown and must agree across every backend.
		const auto Module = CompileSource(
			"class CleanupLeaf {\n"
			"public:\n"
			"\tint id = 3;\n"
			"\t~CleanupLeaf() { print(id); }\n"
			"}\n"
			"class CleanupOwner {\n"
			"public:\n"
			"\tCleanupLeaf child = CleanupLeaf();\n"
			"\t~CleanupOwner() { print(4); assert(false); }\n"
			"}\n"
			"void main() { CleanupOwner owner = CleanupOwner(); }\n");
		if (!Expect(static_cast<bool>(Module), "compile failed-object teardown suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(
					!Capture.bSuccess && Capture.Output == "4\n3\n"
						&& !Capture.ErrorMessageId.empty(),
					"failed destructor body still releases owned fields"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " output=" << Capture.Output << " errors=" << Capture.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Associative containers own every stored value. A destructor failure in
		// one entry must not strand the remaining entries; unordered iteration may
		// choose either of the two valid destruction orders.
		const auto Module = CompileSource(
			"class MapCleanupProbe {\n"
			"public:\n"
			"\tint id = 0;\n"
			"\tMapCleanupProbe(int value) { id = value; }\n"
			"\t~MapCleanupProbe() { print(id); if (id == 2) { assert(false); } }\n"
			"}\n"
			"void main() {\n"
			"\tmap<int, MapCleanupProbe> values = map<int, MapCleanupProbe>();\n"
			"\tvalues[1] = MapCleanupProbe(1);\n"
			"\tvalues[2] = MapCleanupProbe(2);\n"
			"}\n");
		if (!Expect(static_cast<bool>(Module), "compile map teardown suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			const bool bBothEntriesDestroyed =
				Capture.Output == "2\n1\n" || Capture.Output == "1\n2\n";
			if (!Expect(
					!Capture.bSuccess && bBothEntriesDestroyed
						&& !Capture.ErrorMessageId.empty(),
					"map teardown continues after an entry destructor failure"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " output=" << Capture.Output << " errors=" << Capture.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Object parameters skip trivial/native leaves but use the ownership-preserving
		// FValue linear executor once the complete CFG has validated.
		const auto Module = CompileSource(
			"class Box { public: int n = 1; }\n"
			"void discard(Box value) {}\n"
			"void main() { discard(Box()); }\n");
		if (!Expect(static_cast<bool>(Module), "compile object-parameter ownership gate"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::uint16_t Discard = FindFunctionIndex(*Module, "discard");
		if (!Expect(Discard != static_cast<std::uint16_t>(-1)
				&& Cache.EnsureCompiled(*Module, Discard) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"compile object-parameter ownership entry"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Discard);
		if (!Expect(Entry != nullptr
				&& Entry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
				&& !Entry->bNativeLeaf,
				"object parameter uses FValue LinearTrampoline (not native leaf)"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Corrupt/future signature encodings must fail closed instead of reaching an
		// early native leaf that cannot prove its argument ownership contract.
		VeroScript::FBytecodeModule Module;
		Module.Functions.resize(1);
		auto& Function = Module.Functions[0];
		Function.ClosureTypeName = "fn(";
		Function.bReturnsVoid = true;
		Function.Code = {static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		if (!Expect(!VeroScript::AsmJit::JitLowering::RequiresVmOwnership(Function),
				"malformed signature is not an ownership opcode by itself"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(Module, 0) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"malformed signature receives safe fallback entry")
					|| !Expect(Cache.GetCandidate(0) != nullptr
						&& Cache.GetCandidate(0)->Kind != VeroScript::AsmJit::EJitEntryKind::NativeLeaf
						&& Cache.GetCandidate(0)->Kind != VeroScript::AsmJit::EJitEntryKind::NativeCfg
						&& !Cache.GetCandidate(0)->bNativeLeaf,
						"malformed signature cannot enter native leaf"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::AsmJit::JitIr::FSignature Signature;
		if (!Expect(VeroScript::AsmJit::JitIr::ParseSignature("fn(int,double)->bool", Signature),
				"shared JIT signature parses")
					|| !Expect(Signature.Parameters.size() == 2, "shared JIT signature parameter count")
					|| !Expect(Signature.Parameters[0] == VeroScript::AsmJit::JitIr::EValueType::I32,
						"shared JIT signature int parameter")
							|| !Expect(Signature.ReturnType == VeroScript::AsmJit::JitIr::EValueType::Bool,
								"shared JIT signature bool return"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const auto X64 = VeroScript::AsmJit::JitBackend::GetCapabilities(
			VeroScript::AsmJit::JitBackend::EArchitecture::X64);
		const auto A64 = VeroScript::AsmJit::JitBackend::GetCapabilities(
			VeroScript::AsmJit::JitBackend::EArchitecture::A64);
		const auto X86 = VeroScript::AsmJit::JitBackend::GetCapabilities(
			VeroScript::AsmJit::JitBackend::EArchitecture::X86);
		if (!Expect(X64.bI32Cfg && X64.bF64Cfg && X64.bMixedCfg,
				"x64 advertises complete scalar CFG capability")
					|| !Expect(A64.bI32Cfg && A64.bF64Cfg && !A64.bMixedCfg && !A64.bBoxedCfg,
						"AArch64 advertises implemented homogeneous CFG capabilities")
							|| !Expect(X86.bNativeLeaves && !X86.bScalarCfg && !X86.bMixedCfg,
								"x86 advertises scalar leaves without CFG capability"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FBytecodeFunction Function;
		Function.bReturnsVoid = false;
		Function.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::ConstI32), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Jmp), 3, 0, 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::ConstI32), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		VeroScript::AsmJit::JitIr::FFunction Ir;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitIr::BuildFunction(Function, Ir, &Error),
				"shared JIT IR decodes CFG")
					|| !Expect(Ir.Blocks.size() == 2, "shared JIT IR block count")
					|| !Expect(!Ir.bHasLoop, "shared JIT IR loop flag"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FBytecodeModule Module;
		VeroScript::FBytecodeFunction Function;
		Function.ClosureTypeName = "fn(int)->int";
		Function.bReturnsVoid = false;
		Function.ParamCount = 1;
		Function.LocalCount = 1;
		Function.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		VeroScript::AsmJit::JitIr::FFunction Ir;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitIr::BuildFunction(Function, Ir, &Error),
				"shared JIT IR builds typed function")
					|| !Expect(VeroScript::AsmJit::JitIr::AnalyzeTypes(Module, Function, Ir, &Error),
						"shared JIT IR analyzes types")
							|| !Expect(Ir.bTypeAnalysisPrecise, "shared JIT IR precise type analysis")
							|| !Expect(Ir.BlockEntryStates.size() == 1, "shared JIT IR type state count")
							|| !Expect(Ir.BlockEntryStates[0].Locals[0] == VeroScript::AsmJit::JitIr::EValueType::I32,
								"shared JIT IR parameter type seed"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		Plan.bScalarEligible = true;
		std::string Error;
		if (!Expect(!VeroScript::AsmJit::JitBackend::ValidateScalarPlan(
					Plan, VeroScript::AsmJit::JitBackend::EArchitecture::X64, &Error),
				"backend rejects scalar plan without call signature"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// A reachable path that does not initialize a local must not be joined
		// with a scalar path as if Unknown were a lattice bottom. Deserialized or
		// host-authored bytecode must fail closed even though the source compiler
		// normally emits definite initialization.
		VeroScript::FBytecodeModule Module;
		VeroScript::FBytecodeConstant Constant;
		Constant.Kind = VeroScript::EConstantKind::I32;
		Constant.I32 = 7;
		Module.Constants.push_back(Constant);
		VeroScript::FBytecodeFunction Function;
		Function.ClosureTypeName = "fn(bool)->int";
		Function.bReturnsVoid = false;
		Function.ParamCount = 1;
		Function.LocalCount = 2;
		Function.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::JmpIfFalse), 11, 0, 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::ConstI32), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::StoreLocal), 1, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Jmp), 0, 0, 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 1, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitLowering::BuildScalarPlan(Module, Function, Plan, &Error),
				"build partially initialized local plan")
				|| !Expect(Plan.bHasBoxedValues
						&& Plan.LocalSlots.size() == 2
						&& Plan.LocalSlots[1].Type == VeroScript::AsmJit::JitIr::EValueType::Any,
					"partially initialized local remains boxed"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Locals are uninitialized at function entry, but their dominating stores
		// and loop-carried joins must retain concrete scalar types. Treating those
		// entry slots as Any used to reject this function from typed NativeCfg.
		const auto Module = CompileSource(
			"int local_loop(int limit) { int sum = 0; "
			"for (int i = 0; i < limit; i++) { sum += i; } return sum; }\n"
			"void main() { print(local_loop(8)); }\n");
		if (!Expect(static_cast<bool>(Module),
				"compile cross-block local type propagation fixture"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t FunctionIndex = FindFunctionIndex(*Module, "local_loop");
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(FunctionIndex != static_cast<std::uint16_t>(-1)
				&& VeroScript::AsmJit::JitLowering::BuildScalarPlan(
					*Module, Module->Functions[FunctionIndex], Plan, &Error),
				"build cross-block local type propagation plan")
				|| !Expect(Plan.bScalarEligible && !Plan.bHasBoxedValues,
					"loop locals remain eligible for typed NativeCfg")
				|| !Expect(Plan.LocalSlots.size() >= 3
						&& std::all_of(
							Plan.LocalSlots.begin(), Plan.LocalSlots.begin() + 3,
							[](const VeroScript::AsmJit::JitLowering::FValueSlot& Slot)
							{
								return Slot.Type == VeroScript::AsmJit::JitIr::EValueType::I32
														&& Slot.Storage
															== VeroScript::AsmJit::JitLowering::FValueSlot::EStorage::Scalar32;
							}),
					"loop local types converge to i32"))
		{
			std::cerr << Error << " eligible=" << Plan.bScalarEligible
				<< " boxed=" << Plan.bHasBoxedValues
				<< " fallback=" << Plan.bHasRuntimeFallback << " locals=";
			for (const auto& Slot : Plan.LocalSlots)
			{
				std::cerr << static_cast<int>(Slot.Type) << ':'
					<< static_cast<int>(Slot.Storage) << ',';
			}
			std::cerr << '\n';
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "28\n",
					"cross-block local type propagation parity"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		VeroScript::FBytecodeFunction Function;
		Function.Code = {static_cast<std::uint8_t>(VeroScript::EOpcode::MakeClosure)};
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		Plan.bScalarEligible = true;
		Plan.Signature.bValid = true;
		Plan.Signature.ReturnType = VeroScript::AsmJit::JitIr::EValueType::I32;
		Plan.bRequiresVmOwnership = true;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitLowering::RequiresVmOwnership(Function),
				"shared lowering detects ownership opcodes")
					|| !Expect(!VeroScript::AsmJit::JitBackend::ValidateScalarPlan(
							Plan, VeroScript::AsmJit::JitBackend::EArchitecture::X64, &Error),
						"backend rejects VM-owned lowering plan"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!TestDeadLocalStoreLowering() || !TestPartialPlanCapacityFallback()
		|| !TestLargeLocalNativeCapacity())
	{
		return EXIT_FAILURE;
	}

	{
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		Plan.bScalarEligible = true;
		Plan.Signature.bValid = true;
		Plan.Signature.ReturnType = VeroScript::AsmJit::JitIr::EValueType::I32;
		Plan.bHasBoxedValues = true;
		std::string Error;
		if (!Expect(!VeroScript::AsmJit::JitBackend::ValidateScalarPlan(
					Plan, VeroScript::AsmJit::JitBackend::EArchitecture::X64, &Error),
				"backend rejects scalar plan with boxed storage"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FBytecodeModule Module;
		VeroScript::FBytecodeFunction Function;
		Function.ClosureTypeName = "fn(int,int)->int";
		Function.bReturnsVoid = false;
		Function.ParamCount = 2;
		Function.LocalCount = 2;
		Function.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 1, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Add),
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitLowering::BuildScalarPlan(Module, Function, Plan, &Error),
				"scalar lowering plan builds")
					|| !Expect(Plan.bScalarEligible, "scalar lowering plan is eligible")
					|| !Expect(Plan.Blocks.size() == 1 && Plan.Blocks[0].Instructions.size() == 4,
						"scalar lowering plan instruction count")
							|| !Expect(Plan.Blocks[0].Instructions[2].Kind
								== VeroScript::AsmJit::JitLowering::EKind::Arithmetic,
								"scalar lowering plan classifies arithmetic")
									|| !Expect(Plan.LocalCount == 2, "scalar lowering plan preserves local count")
									|| !Expect(Plan.LocalSlots.size() == 2
										&& Plan.LocalSlots[0].Type == VeroScript::AsmJit::JitIr::EValueType::I32
										&& Plan.LocalSlots[0].Storage
										== VeroScript::AsmJit::JitLowering::FValueSlot::EStorage::Scalar32,
										"scalar lowering plan builds typed scalar local slots")
											|| !Expect(Plan.MaxStackSlots == 2, "scalar lowering plan computes stack depth")
											|| !Expect(Plan.Blocks[0].Instructions[0].Operand0 == 0
												&& Plan.Blocks[0].Instructions[1].Operand0 == 1,
												"scalar lowering plan decodes local operands")
													|| !Expect(Plan.Blocks[0].Instructions[0].ResultType
														== VeroScript::AsmJit::JitIr::EValueType::I32
														&& Plan.Blocks[0].Instructions[2].ResultType
														== VeroScript::AsmJit::JitIr::EValueType::I32,
														"scalar lowering plan carries precise scalar types")
															|| !Expect(Plan.RuntimeFallbackOpcodes.empty(),
																"scalar lowering plan has no fallback opcodes")
																	|| !Expect(VeroScript::AsmJit::JitBackend::ValidateScalarPlan(
																			Plan, VeroScript::AsmJit::JitBackend::EArchitecture::X64, &Error),
																		"x64 accepts typed scalar lowering plan")
																			|| !Expect(VeroScript::AsmJit::JitBackend::ValidateScalarPlan(
																					Plan, VeroScript::AsmJit::JitBackend::EArchitecture::A64, &Error),
																				"AArch64 accepts typed scalar lowering plan"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::AsmJit::JitIr::FSignature Signature;
		if (!Expect(VeroScript::AsmJit::JitIr::ParseSignature("fn(int,double,bool)->double", Signature),
				"ABI test signature parses"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::JitAbi::FCallLayout Layout;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitAbi::BuildScalarCallLayout(
					Signature, VeroScript::AsmJit::JitAbi::ECallAbi::MsX64, Layout, &Error),
				"MS x64 scalar call layout builds")
					|| !Expect(Layout.Arguments.size() == 3, "ABI layout preserves argument count")
					|| !Expect(Layout.Arguments[0].RegisterClass == VeroScript::AsmJit::JitAbi::ERegisterClass::Gpr
						&& Layout.Arguments[0].RegisterIndex == 0
						&& Layout.Arguments[1].RegisterClass == VeroScript::AsmJit::JitAbi::ERegisterClass::Fpr
						&& Layout.Arguments[1].RegisterIndex == 1
						&& Layout.Arguments[2].RegisterIndex == 2
						&& Layout.StackBytes == 32,
						"MS x64 layout uses shared positional register slots and shadow space")
							|| !Expect(Layout.Return.RegisterClass == VeroScript::AsmJit::JitAbi::ERegisterClass::Fpr,
								"ABI layout classifies floating return"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(VeroScript::AsmJit::JitAbi::BuildScalarCallLayout(
					Signature, VeroScript::AsmJit::JitAbi::ECallAbi::Aapcs64, Layout, &Error),
				"AAPCS64 scalar call layout builds")
					|| !Expect(Layout.Arguments[0].RegisterIndex == 0
						&& Layout.Arguments[1].RegisterIndex == 0
						&& Layout.Arguments[2].RegisterIndex == 1
						&& Layout.StackBytes == 0,
						"AAPCS64 layout uses independent GPR/FPR sequences"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(VeroScript::AsmJit::JitAbi::BuildScalarCallLayout(
					Signature, VeroScript::AsmJit::JitAbi::ECallAbi::CdeclX86, Layout, &Error),
				"x86 cdecl scalar call layout builds")
					|| !Expect(Layout.Arguments.size() == 3
						&& Layout.Arguments[0].RegisterClass == VeroScript::AsmJit::JitAbi::ERegisterClass::Memory
						&& Layout.Arguments[0].StackOffset == 0
						&& Layout.Arguments[1].StackOffset == 4
						&& Layout.Arguments[2].StackOffset == 12
						&& Layout.StackBytes == 16,
						"x86 cdecl layout uses typed stack slots"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FBytecodeModule Module;
		Module.Functions.resize(1);
		auto& Function = Module.Functions[0];
		Function.ClosureTypeName = "fn(int,int)->int";
		Function.bReturnsVoid = false;
		Function.ParamCount = 2;
		Function.LocalCount = 2;
		Function.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 1, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Add),
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(Module, 0) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"typed scalar fixture compiles for stack capacity hint"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::AsmJit::FJitFunctionEntry* Entry = Cache.GetCandidate(0);
		if (!Expect(Entry != nullptr && Entry->StackCapacityHint == 2
				&& Entry->BoxedStackCapacityHint == 0,
				"compiled scalar entry retains lowering stack capacity hint"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FBytecodeModule Module;
		Module.GlobalTypeNames.push_back("int");
		VeroScript::FBytecodeFunction Function;
		Function.ClosureTypeName = "fn()->int";
		Function.bReturnsVoid = false;
		Function.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadGlobal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitLowering::BuildScalarPlan(Module, Function, Plan, &Error),
				"global scalar lowering plan builds")
					|| !Expect(Plan.bScalarEligible && !Plan.bHasRuntimeFallback,
						"global scalar lowering remains native eligible")
							|| !Expect(Plan.Blocks.size() == 1 && !Plan.Blocks[0].Instructions.empty()
								&& Plan.Blocks[0].Instructions[0].Kind == VeroScript::AsmJit::JitLowering::EKind::GlobalLoad
								&& Plan.Blocks[0].Instructions[0].Operand0 == 0,
								"global scalar lowering records typed slot operand"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FBytecodeModule Module;
		VeroScript::FBytecodeFunction Function;
		Function.ClosureTypeName = "fn(uint,uint)->uint";
		Function.bReturnsVoid = false;
		Function.ParamCount = 2;
		Function.LocalCount = 2;
		Function.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 1, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Add),
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitLowering::BuildScalarPlan(Module, Function, Plan, &Error),
				"uint scalar lowering plan builds")
					|| !Expect(Plan.Signature.ReturnType == VeroScript::AsmJit::JitIr::EValueType::U32,
						"uint scalar lowering preserves unsigned result")
							|| !Expect(VeroScript::AsmJit::JitBackend::ValidateScalarPlan(
									Plan, VeroScript::AsmJit::JitBackend::EArchitecture::A64, &Error),
								"AArch64 accepts uint scalar lowering plan"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::EExecutionBackend Parsed = VeroScript::EExecutionBackend::Vm;
		if (!Expect(VeroScript::TryParseExecutionBackend("interpret", Parsed)
				&& Parsed == VeroScript::EExecutionBackend::Vm,
				"alias interpret → vm"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(VeroScript::TryParseExecutionBackend("hybrid", Parsed)
				&& Parsed == VeroScript::EExecutionBackend::Mixed,
				"alias hybrid → mixed"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::AsmJit::FJitCodeCache Cache;
		VeroScript::FBytecodeModule EmptyModule;
		EmptyModule.Functions.resize(1);
		EmptyModule.Functions[0].bReturnsVoid = true;
		EmptyModule.Functions[0].Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		if (!Expect(
				Cache.EnsureCompiled(EmptyModule, 0) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"trivial void → Ok"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A scalar main may remain native across a side-effecting print helper.
		const std::string Source =
			"void main() {\n"
			"\tint sum = 0;\n"
			"\tint i = 0;\n"
			"\twhile (i < 10000) { sum = sum + 1; i = i + 1; }\n"
			"\tprint(sum);\n"
			"\tprint(true);\n"
			"\teprint(1.5);\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile native main print suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::JitLowering::FPlan LoopPlan;
		std::string LoopPlanError;
		[[maybe_unused]] const auto MainIndex =
			static_cast<std::uint16_t>(Module->MainFunctionIndex);
		if (!Expect(VeroScript::AsmJit::JitLowering::BuildScalarPlan(
					*Module, Module->Functions[MainIndex], LoopPlan, &LoopPlanError),
				"lower optimized main loop suite")
					|| !Expect(std::any_of(LoopPlan.Blocks.begin(), LoopPlan.Blocks.end(),
							[](const VeroScript::AsmJit::JitLowering::FBlock& Block)
							{
								return std::any_of(Block.Instructions.begin(), Block.Instructions.end(),
														[](const VeroScript::AsmJit::JitLowering::FInstruction& Instruction)
														{
															return Instruction.Opcode == VeroScript::EOpcode::JmpIfLocalGeImm
																						&& Instruction.Kind == VeroScript::AsmJit::JitLowering::EKind::Branch;
														});
							}),
						"optimized local comparison remains a lowering branch"))
		{
			std::cerr << LoopPlanError << '\n';
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output == "10000\ntrue\n"
					&& Capture.Errors == "1.5\n",
					"native main print semantics"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(*Module, MainIndex) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"native main print compiles")
					|| !Expect(Cache.GetCandidate(MainIndex) != nullptr
						&& Cache.GetCandidate(MainIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
						"native main print uses mixed CFG for bool/f64 output"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Homogeneous int main + print stays on the fast I32 CFG (not Mixed).
		const std::string Source =
			"void main() {\n"
			"\tint sum = 0;\n"
			"\tint i = 0;\n"
			"\twhile (i < 10000) { sum = sum + 1; i = i + 1; }\n"
			"\tprint(sum);\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile int-only main print suite"))
		{
			return EXIT_FAILURE;
		}
		[[maybe_unused]] const auto MainIndex =
			static_cast<std::uint16_t>(Module->MainFunctionIndex);
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output == "10000\n",
					"int-only main print semantics"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(*Module, MainIndex) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"int-only main print compiles")
					|| !Expect(Cache.GetCandidate(MainIndex) != nullptr
						&& Cache.GetCandidate(MainIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
						&& Cache.GetCandidate(MainIndex)->NativeCode != nullptr,
						"int-only main print uses asmjit I32 CFG"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// A zero-argument scalar builtin must not demote an otherwise native mixed
		// loop to the FValue linear/VM trampoline. In particular, timing a hot loop
		// with now() should only pay for the two clock calls.
		const std::string Source =
			"void main() {\n"
			"\tdouble start = now();\n"
			"\tassert(true);\n"
			"\tsleep(0);\n"
			"\tint sum = 0;\n"
			"\tint i = 0;\n"
			"\twhile (i < 10000) { sum += 1; i++; }\n"
			"\tprint(sum);\n"
			"\tprint(now() - start);\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile timed native loop suite"))
		{
			return EXIT_FAILURE;
		}
		[[maybe_unused]] const auto MainIndex =
			static_cast<std::uint16_t>(Module->MainFunctionIndex);
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output.rfind("10000\n", 0) == 0,
					"timed native loop semantics"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(
				Cache.EnsureCompiled(*Module, MainIndex)
					== VeroScript::AsmJit::EJitCompileStatus::Ok,
				"timed native loop compiles")
				|| !Expect(Cache.GetCandidate(MainIndex) != nullptr
						&& Cache.GetCandidate(MainIndex)->Kind
							== VeroScript::AsmJit::EJitEntryKind::NativeCfg
						&& Cache.GetCandidate(MainIndex)->NativeCode != nullptr,
					"now keeps scalar loop on mixed native CFG"))
		{
			return EXIT_FAILURE;
		}
		#endif
		const std::unique_ptr<VeroScript::FBytecodeModule> FailedAssert =
			CompileSource("void main() { assert(false, \"jit assertion detail\"); }\n");
		if (!Expect(static_cast<bool>(FailedAssert), "compile failed assert parity suite"))
		{
			return EXIT_FAILURE;
		}
		const FExecutionCapture AssertBaseline = RunCapture(
			*FailedAssert, VeroScript::EExecutionBackend::Vm);
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Candidate = RunCapture(*FailedAssert, Backend);
			if (!Expect(!AssertBaseline.bSuccess && !Candidate.bSuccess
					&& AssertBaseline.ErrorMessageId == "diag.assertion_failed_with_message"
					&& Candidate.ErrorMessageId == AssertBaseline.ErrorMessageId
					&& AssertBaseline.ErrorMessageArgs
						== std::vector<std::string>{"jit assertion detail"} && Candidate.ErrorMessageArgs == AssertBaseline.ErrorMessageArgs,
					"assert message failure parity"))
			{
				return EXIT_FAILURE;
			}
		}

		const std::unique_ptr<VeroScript::FBytecodeModule> ExitModule = CompileSource(
			"void main() { int i = 0; while (i < 3) { i++; } exit(7); assert(false); }\n");
		if (!Expect(static_cast<bool>(ExitModule), "compile scalar exit native suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*ExitModule, Backend);
			if (!Expect(Capture.bSuccess && Capture.bExitRequested && Capture.ExitCode == 7
					&& Capture.ErrorMessageId.empty(),
					"native scalar exit terminates with matching status"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		VeroScript::AsmJit::FJitCodeCache ExitCache;
		const auto ExitMainIndex = static_cast<std::uint16_t>(ExitModule->MainFunctionIndex);
		if (!Expect(ExitCache.EnsureCompiled(*ExitModule, ExitMainIndex)
				== VeroScript::AsmJit::EJitCompileStatus::Ok
				&& ExitCache.GetCandidate(ExitMainIndex) != nullptr
				&& ExitCache.GetCandidate(ExitMainIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
				&& ExitCache.GetCandidate(ExitMainIndex)->NativeCode != nullptr,
				"exit keeps preceding scalar loop on mixed native CFG"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// The outer exit request is already set while frame-owned objects unwind.
		// It must not cause a destructor's first builtin call to truncate the rest
		// of that destructor body.
		const std::unique_ptr<VeroScript::FBytecodeModule> ExitDestructor = CompileSource(
			"class ExitProbe { public: ~ExitProbe() { print(1); print(2); } }\n"
			"void main() { ExitProbe value = ExitProbe(); exit(7); }\n");
		if (!Expect(static_cast<bool>(ExitDestructor), "compile exit destructor suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*ExitDestructor, Backend);
			if (!Expect(
					Capture.bSuccess && Capture.bExitRequested && Capture.ExitCode == 7
						&& Capture.Output == "1\n2\n"
						&& Capture.ErrorMessageId.empty(),
					"exit runs complete destructor bodies on every backend"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " output=" << Capture.Output << " errors=" << Capture.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		// An exit in a nested JIT callee has no return value to extract. Every
		// caller tier must unwind without resuming the statement after the call.
		const std::unique_ptr<VeroScript::FBytecodeModule> NestedExit = CompileSource(
			"void stop_now() { exit(9); }\n"
			"void wrapper() { stop_now(); assert(false); }\n"
			"void main() { wrapper(); assert(false); }\n");
		if (!Expect(static_cast<bool>(NestedExit), "compile nested exit propagation suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*NestedExit, Backend);
			if (!Expect(Capture.bSuccess && Capture.bExitRequested && Capture.ExitCode == 9
					&& Capture.ErrorMessageId.empty(),
					"nested exit unwinds every JIT caller"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " success=" << Capture.bSuccess
					<< " exit=" << Capture.bExitRequested
					<< " code=" << Capture.ExitCode
					<< " error-id=" << Capture.ErrorMessageId
					<< " errors=" << Capture.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		// A value destructor may call back into the same globally reachable
		// container. clear() must detach storage before running user code so the
		// nested clear cannot invalidate the outer iterator/index.
		const std::unique_ptr<VeroScript::FBytecodeModule> ReentrantContainers =
			CompileSource(
				"class ReentrantValue {\n"
				"public:\n"
				"  int owner = 0;\n"
				"  ReentrantValue(int value) { owner = value; }\n"
				"  ~ReentrantValue() {\n"
				"    destroyed++;\n"
				"    if (owner == 1 && arrayGuard) { arrayGuard = false; values.clear(); }\n"
				"    if (owner == 2 && mapGuard) { mapGuard = false; mapped.clear(); }\n"
				"    if (owner == 3 && orderedGuard) { orderedGuard = false; ordered.clear(); }\n"
				"  }\n"
				"};\n"
				"bool arrayGuard = true;\n"
				"bool mapGuard = true;\n"
				"bool orderedGuard = true;\n"
				"int destroyed = 0;\n"
				"array<ReentrantValue> values = array<ReentrantValue>();\n"
				"map<int, ReentrantValue> mapped = map<int, ReentrantValue>();\n"
				"ordered_map<int, ReentrantValue> ordered = ordered_map<int, ReentrantValue>();\n"
				"void fill() {\n"
				"  values.push(ReentrantValue(1)); values.push(ReentrantValue(1));\n"
				"  mapped[1] = ReentrantValue(2); mapped[2] = ReentrantValue(2);\n"
				"  ordered[1] = ReentrantValue(3); ordered[2] = ReentrantValue(3);\n"
				"}\n"
				"void main() {\n"
				"  fill();\n"
				"  values.clear(); mapped.clear(); ordered.clear();\n"
				"  assert(len(values) == 0 && len(mapped) == 0 && len(ordered) == 0);\n"
				"  assert(destroyed == 6);\n"
				"}\n");
		if (!Expect(
				static_cast<bool>(ReentrantContainers),
				"compile reentrant container teardown suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*ReentrantContainers, Backend);
			if (!Expect(
					Capture.bSuccess && Capture.ErrorMessageId.empty(),
					"container teardown tolerates destructor reentry on every backend"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " success=" << Capture.bSuccess
					<< " error-id=" << Capture.ErrorMessageId
					<< " output=" << Capture.Output << " errors=" << Capture.Errors;
				return EXIT_FAILURE;
			}
		}
	}

	{
		// NewObject keeps main on the ownership-aware linear trampoline. This
		// covers exit after a bound method call and verifies linear-frame cleanup.
		const std::unique_ptr<VeroScript::FBytecodeModule> LinearExit = CompileSource(
			"class Stopper { public: void stop() { exit(11); } }\n"
			"void main() { Stopper value = Stopper(); value.stop(); assert(false); }\n");
		if (!Expect(static_cast<bool>(LinearExit), "compile linear nested exit suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*LinearExit, Backend);
			if (!Expect(Capture.bSuccess && Capture.bExitRequested && Capture.ExitCode == 11
					&& Capture.ErrorMessageId.empty(),
					"linear bound-method exit unwinds cleanly"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// The ownership opcode keeps invoke() on VmTrampoline. Its left Add operand
		// remains on the VM stack while the nested scalar callee executes exit(),
		// proving that intentional whole-stack clearing is not treated as corruption.
		const std::unique_ptr<VeroScript::FBytecodeModule> StackedExit = CompileSource(
			"int stop_with_value() { exit(13); return 0; }\n"
			"int invoke() { fn(int)->int keep = fn(int x) -> int => x; "
				"return 1 + stop_with_value(); }\n"
			"void main() { print(invoke()); assert(false); }\n");
		if (!Expect(static_cast<bool>(StackedExit), "compile stacked VM-to-JIT exit suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*StackedExit, Backend);
			if (!Expect(Capture.bSuccess && Capture.bExitRequested && Capture.ExitCode == 13
					&& Capture.Output.empty() && Capture.ErrorMessageId.empty(),
					"exit clears pre-existing VM operands without a false stack error"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::uint16_t InvokeIndex = FindFunctionIndex(*StackedExit, "invoke");
		if (!Expect(InvokeIndex != static_cast<std::uint16_t>(-1)
				&& Cache.EnsureCompiled(*StackedExit, InvokeIndex)
					== VeroScript::AsmJit::EJitCompileStatus::Ok
				&& Cache.GetCandidate(InvokeIndex) != nullptr
				&& Cache.GetCandidate(InvokeIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::VmTrampoline,
				"stacked exit caller remains on ownership-safe VM trampoline"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Statement-form postfix results are dead and should not keep their snapshot
		// temporary. Postfix expressions whose old value is consumed remain intact.
		const std::string Source =
			"void discarded_postfix() { int p = 0; p++; p--; print(p); }\n"
			"void loop_postfix(int limit) { int p = 0; for (int i = 0; i < limit; i++) { p++; } print(p); }\n"
			"int preserved_postfix() { int p = 1; int old = p++; return old * 10 + p; }\n"
			"void main() { discarded_postfix(); loop_postfix(3); int q = 0; for (int i = 0; i < 3; i++) { q++; } print(q); print(preserved_postfix()); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile discarded postfix fusion suite"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t Discarded = FindFunctionIndex(*Module, "discarded_postfix");
		std::size_t IncCount = 0;
		std::size_t DecCount = 0;
		if (Discarded != static_cast<std::uint16_t>(-1))
		{
			const auto& Code = Module->Functions[Discarded].Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const auto Op = static_cast<VeroScript::EOpcode>(Code[Ip]);
				IncCount += Op == VeroScript::EOpcode::IncLocal ? 1u : 0u;
				if (Op == VeroScript::EOpcode::AddLocalImm
					&& Ip + 7 <= Code.size()
					&& VeroScript::Bytecode::ReadI32LE(Code.data() + Ip + 3) == -1)
				{
					++DecCount;
				}
				const std::size_t Size = VeroScript::Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Code.size())
				{
					break;
				}
				Ip += Size;
			}
		}
		if (!Expect(Discarded != static_cast<std::uint16_t>(-1)
				&& IncCount == 1 && DecCount == 1,
				"discarded postfix uses fused local increments"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t Loop = FindFunctionIndex(*Module, "loop_postfix");
		std::size_t LoopIncCount = 0;
		if (Loop != static_cast<std::uint16_t>(-1))
		{
			const auto& Code = Module->Functions[Loop].Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const auto Op = static_cast<VeroScript::EOpcode>(Code[Ip]);
				LoopIncCount += Op == VeroScript::EOpcode::IncLocal ? 1u : 0u;
				const std::size_t Size = VeroScript::Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Code.size())
				{
					break;
				}
				Ip += Size;
			}
		}
		if (!Expect(Loop != static_cast<std::uint16_t>(-1) && LoopIncCount == 2,
				"discarded postfix uses fused increments inside loops"))
		{
			std::cerr << "loop IncLocal count: " << LoopIncCount << '\n';
			return EXIT_FAILURE;
		}
		std::size_t MainIncCount = 0;
		const auto& MainCode = Module->Functions[Module->MainFunctionIndex].Code;
		for (std::size_t Ip = 0; Ip < MainCode.size();)
		{
			const auto Op = static_cast<VeroScript::EOpcode>(MainCode[Ip]);
			MainIncCount += Op == VeroScript::EOpcode::IncLocal ? 1u : 0u;
			const std::size_t Size = VeroScript::Bytecode::InstructionSize(Op);
			if (Size == 0 || Ip + Size > MainCode.size())
			{
				break;
			}
			Ip += Size;
		}
		if (!Expect(MainIncCount == 2, "discarded postfix uses fused increments in main"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "0\n3\n3\n12\n", "postfix fusion semantics"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Zero-argument scalar input builtins use the same native call-site bridge.
		// Feed a deterministic stream so both compilation and result packing are
		// checked for every backend without requiring interactive input.
		const std::string Source =
			"int read_int_native() { return __read_int(); }\n"
			"double read_double_native() { return __read_double(); }\n"
			"bool read_bool_native() { return __read_bool(); }\n"
			"void main() { print(read_int_native()); print(read_double_native()); "
				"print(read_bool_native()); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile scalar input native suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend, "42\n3.5\ntrue\n");
			if (!Expect(Capture.bSuccess && Capture.Output == "42\n3.5\ntrue\n",
					"scalar input builtin native result packing"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		VeroScript::AsmJit::FJitCodeCache Cache;
		for (const char* const Name :
			{"read_int_native", "read_double_native", "read_bool_native"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			if (!Expect(Index != static_cast<std::uint16_t>(-1)
					&& Cache.EnsureCompiled(*Module, Index)
						== VeroScript::AsmJit::EJitCompileStatus::Ok
					&& Cache.GetCandidate(Index) != nullptr
					&& Cache.GetCandidate(Index)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
					&& Cache.GetCandidate(Index)->NativeCode != nullptr,
					"scalar input builtin keeps native CFG"))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
	}

	{
		// Bool locals / comparisons must keep true/false formatting on I32 CFG.
		const std::string Source =
			"void main() {\n"
			"\tbool flag = true;\n"
			"\tprint(flag);\n"
			"\tprint(1 < 2);\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile bool print suite"))
		{
			return EXIT_FAILURE;
		}
		[[maybe_unused]] const auto MainIndex =
			static_cast<std::uint16_t>(Module->MainFunctionIndex);
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output == "true\ntrue\n",
					"bool local/compare print semantics"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(*Module, MainIndex) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"bool print compiles")
					|| !Expect(Cache.GetCandidate(MainIndex) != nullptr
						&& Cache.GetCandidate(MainIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
						&& Cache.GetCandidate(MainIndex)->NativeCode != nullptr,
						"bool print uses asmjit I32 CFG with Bool tag"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// to_int(bool) / coerced int locals must print as 0/1, not true/false.
		const std::string Source =
			"void main() {\n"
			"\tprint(to_int(true));\n"
			"\tint x = true;\n"
			"\tprint(x);\n"
			"\tbool c = true;\n"
			"\tprint(c ? true : false);\n"
			"\tprint(c && true);\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile to_int/ternary bool print suite"))
		{
			return EXIT_FAILURE;
		}
		[[maybe_unused]] const auto MainIndex =
			static_cast<std::uint16_t>(Module->MainFunctionIndex);
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output == "1\n1\ntrue\ntrue\n",
					"to_int/ternary bool print semantics"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(*Module, MainIndex) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"to_int/ternary print compiles")
					|| !Expect(Cache.GetCandidate(MainIndex) != nullptr
						&& Cache.GetCandidate(MainIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
						&& Cache.GetCandidate(MainIndex)->NativeCode != nullptr,
						"to_int/ternary print stays on I32 CFG"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// In-method print(this.boolField) must keep Bool Mix tag on I32 CFG.
		const std::string Source =
			"class Flag {\n"
			"public:\n"
			"\tbool on = true;\n"
			"\tvoid dump() {\n"
			"\t\tprint(on);\n"
			"\t\tprint(false);\n"
			"\t\teprint(on);\n"
			"\t}\n"
			"}\n"
			"void main() {\n"
			"\tFlag f = Flag();\n"
			"\tf.dump();\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile this.field bool print suite"))
		{
			return EXIT_FAILURE;
		}
		auto DumpIndex = static_cast<std::uint16_t>(Module->MainFunctionIndex);
		bool bFoundDump = false;
		for (std::uint16_t I = 0; I < Module->Functions.size(); ++I)
		{
			if (Module->Functions[I].Name.find("dump") != std::string::npos
				&& Module->Functions[I].bIsMethod)
			{
				DumpIndex = I;
				bFoundDump = true;
				break;
			}
		}
		if (!Expect(bFoundDump, "locate Flag::dump method"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess
					&& Capture.Output == "true\nfalse\n"
					&& Capture.Errors == "true\n",
					"this.field / false / eprint bool semantics"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(*Module, DumpIndex) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"Flag::dump compiles")
					|| !Expect(Cache.GetCandidate(DumpIndex) != nullptr
						&& Cache.GetCandidate(DumpIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
						&& Cache.GetCandidate(DumpIndex)->NativeCode != nullptr,
						"Flag::dump uses I32 CFG for print(this.field)"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Everyday OOP JIT contract: hot loop in a method stays NativeCfg.
		const std::string Source =
			"class Counter {\n"
			"public:\n"
			"\tint n = 0;\n"
			"\tint run(int limit) {\n"
			"\t\tint s = n;\n"
			"\t\tint i = 0;\n"
			"\t\twhile (i < limit) { s = s + 1; i = i + 1; }\n"
			"\t\tn = s;\n"
			"\t\treturn s;\n"
			"\t}\n"
			"}\n"
			"void main() {\n"
			"\tCounter c = Counter();\n"
			"\tprint(c.run(3));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile method hot-loop suite"))
		{
			return EXIT_FAILURE;
		}
		std::uint16_t RunIndex = 0;
		bool bFoundRun = false;
		for (std::uint16_t I = 0; I < static_cast<std::uint16_t>(Module->Functions.size()); ++I)
		{
			if (Module->Functions[I].Name.find("run") != std::string::npos
				&& Module->Functions[I].bIsMethod)
			{
				RunIndex = I;
				bFoundRun = true;
				break;
			}
		}
		if (!Expect(bFoundRun, "locate Counter::run"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output == "3\n",
					"method hot-loop semantics"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(*Module, RunIndex) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"Counter::run compiles")
					|| !Expect(Cache.GetCandidate(RunIndex) != nullptr
						&& Cache.GetCandidate(RunIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
						&& Cache.GetCandidate(RunIndex)->NativeCode != nullptr,
						"Counter::run uses NativeCfg for everyday OOP JIT"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Host inheritance: script hot loop on MyActor stays NativeCfg; functions with
		// object parameters (read_host) stay VmTrampoline; NewObject allocation stays
		// VmTrampoline. CallNative scalar helpers without boxed locals may still use
		// LinearTrampoline elsewhere.
		struct FActorPayload
		{
			std::int32_t Value = 0;
		};
		auto ActorSubclass = [](
			const VeroScript::FNativeCallContext& Context,
			VeroScript::FValue& OutReturn,
			std::string& OutError) -> bool
		{
			(void)Context;
			(void)OutError;
			auto* const Actor = new FActorPayload{7};
			OutReturn = VeroScript::MakeHostObject(
				1, "AActor", Actor, [](void* P)
				{
					delete static_cast<FActorPayload*>(P);
				});
			return true;
		};
		auto ActorGetValue = [](
			const VeroScript::FNativeCallContext& Context,
			VeroScript::FValue& OutReturn,
			std::string& OutError) -> bool
		{
			(void)OutError;
			auto* const Actor = static_cast<FActorPayload*>(Context.HostThis);
			if (!Actor)
			{
				OutError = "null";
				return false;
			}
			OutReturn = Actor->Value;
			return true;
		};

		VeroScript::FHostBindingRegistry Registry;
		VeroScript::FHostTypeId ActorId;
		std::string ErrorId;
		if (!Expect(
				Registry.RegisterType("AActor", VeroScript::EHostTypeKind::Class, ActorId, ErrorId)
					&& Registry.SetTypeInheritable(ActorId, true, ErrorId)
					&& Registry.RegisterTypeSubclassFactory(ActorId, ActorSubclass, nullptr, ErrorId),
				"host inherit JIT registry setup"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc GetValue;
		GetValue.Kind = VeroScript::EHostMemberKind::Method;
		GetValue.Name = "get_value";
		GetValue.ReturnType = VeroScript::ENativeType::Int;
		GetValue.Callback = ActorGetValue;
		if (!Expect(Registry.RegisterMember(ActorId, GetValue, ErrorId),
				"register AActor.get_value"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = &Registry;
		const VeroScript::FCompileResult Compile = VeroScript::VeroScriptCompile::CompileSource(
			"class MyActor : AActor {\n"
			"public:\n"
			"\tint run(int limit) {\n"
			"\t\tint s = 0;\n"
			"\t\tint i = 0;\n"
			"\t\twhile (i < limit) { s = s + 1; i = i + 1; }\n"
			"\t\treturn s;\n"
			"\t}\n"
			"}\n"
			"int read_host(MyActor a) { return a.get_value(); }\n"
			"void main() {\n"
			"\tMyActor a = MyActor();\n"
			"\tprint(a.run(3));\n"
			"\tprint(read_host(a));\n"
			"}\n",
			"<host_inherit_jit>",
			Options);
		if (!Expect(Compile.bSuccess && static_cast<bool>(Compile.Module),
				"compile host inherit JIT suite"))
		{
			std::cerr << Compile.Error.Message << '\n';
			return EXIT_FAILURE;
		}

		std::uint16_t RunIndex = FindFunctionIndex(*Compile.Module, "run");
		std::uint16_t ReadHostIndex = FindFunctionIndex(*Compile.Module, "read_host");
		std::uint16_t MainIndex = FindFunctionIndex(*Compile.Module, "main");
		if (!Expect(RunIndex != static_cast<std::uint16_t>(-1)
				&& ReadHostIndex != static_cast<std::uint16_t>(-1)
				&& MainIndex != static_cast<std::uint16_t>(-1),
				"locate host inherit JIT functions"))
		{
			for (const auto& Fn : Compile.Module->Functions)
			{
				std::cerr << " fn=" << Fn.Name << " method=" << Fn.bIsMethod << '\n';
			}
			return EXIT_FAILURE;
		}

		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
			if (!Expect(
					Vm.Execute(
						*Compile.Module,
						VeroScript::ERunMode::Script,
						{},
							&Registry.GetNativeFunctions())
							&& Output.str() == "3\n7\n",
					"host inherit JIT semantics"))
			{
				std::cerr << "backend=" << static_cast<int>(Backend)
					<< " out=[" << Output.str() << "] err=" << Errors.str()
					<< Vm.GetError().Message << '\n';
				return EXIT_FAILURE;
			}
		}

		// Factory and clone names are cached only for one active execution. Reusing
		// the same vector object with changed registrations between runs must rebuild
		// the index instead of calling a stale slot.
		std::vector<VeroScript::FNativeFunction> MutableNatives =
			Registry.GetNativeFunctions();
		const std::string FactoryName = VeroScript::MakeHostSubclassNativeName("AActor");
		auto FactoryIt = std::find_if(
			MutableNatives.begin(),
			MutableNatives.end(),
			[&](const VeroScript::FNativeFunction& Native)
			{
				return Native.Name == FactoryName;
			});
		if (!Expect(FactoryIt != MutableNatives.end(), "locate cached host factory fixture"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream CachedOutput;
		std::ostringstream CachedErrors;
		std::istringstream CachedInput;
		VeroScript::VeroScriptVm CachedVm(CachedOutput, CachedInput, CachedErrors);
		VeroScript::Test::Configure(CachedVm);
		ConfigureBackend(CachedVm);
		CachedVm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		if (!Expect(CachedVm.Execute(
				*Compile.Module,
				VeroScript::ERunMode::Script,
						{},
				&MutableNatives),
			"prime active native name index"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FNativeFunction ReplacementFactory = *FactoryIt;
		FactoryIt->Name += ".stale";
		FactoryIt->Callback = [](
			const VeroScript::FNativeCallContext&,
			VeroScript::FValue&,
			std::string& OutError)
		{
			OutError = "stale host factory cache";
			return false;
		};
		MutableNatives.push_back(std::move(ReplacementFactory));
		if (!Expect(CachedVm.Execute(
				*Compile.Module,
				VeroScript::ERunMode::Script,
						{},
				&MutableNatives)
				&& CachedOutput.str() == "3\n7\n3\n7\n",
			"active native name index invalidates between executions"))
		{
			std::cerr << CachedVm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}

		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(Cache.EnsureCompiled(*Compile.Module, RunIndex) == VeroScript::AsmJit::EJitCompileStatus::Ok
				&& Cache.GetCandidate(RunIndex)
				&& Cache.GetCandidate(RunIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"MyActor::run stays NativeCfg with HostBase")
					|| !Expect(Cache.EnsureCompiled(*Compile.Module, ReadHostIndex)
						== VeroScript::AsmJit::EJitCompileStatus::Ok
						&& Cache.GetCandidate(ReadHostIndex)
						&& Cache.GetCandidate(ReadHostIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
						&& !Cache.GetCandidate(ReadHostIndex)->bNativeLeaf,
						"read_host(object param + CallNative) uses FValue LinearTrampoline")
							|| !Expect(Cache.EnsureCompiled(*Compile.Module, MainIndex)
								== VeroScript::AsmJit::EJitCompileStatus::Ok
								&& Cache.GetCandidate(MainIndex)
								&& Cache.GetCandidate(MainIndex)->Kind
									== VeroScript::AsmJit::EJitEntryKind::LinearTrampoline,
								"main(NewObject) uses ownership-aware LinearTrampoline"))
		{
			const auto* Run = Cache.GetCandidate(RunIndex);
			const auto* Read = Cache.GetCandidate(ReadHostIndex);
			const auto* Main = Cache.GetCandidate(MainIndex);
			std::cerr << "run=" << (Run ? static_cast<int>(Run->Kind) : -1)
				<< " read=" << (Read ? static_cast<int>(Read->Kind) : -1)
				<< " main=" << (Main ? static_cast<int>(Main->Kind) : -1) << '\n';
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Exact by-value scalar natives use the allocation-free JIT call bridge.
		// The VM path remains the semantic baseline and deliberately does not
		// increment this JIT-only counter.
		VeroScript::FHostBindingRegistry Registry;
		FScalarNativeProbe Probe;
		std::string ErrorId;
		if (!Expect(
				Registry.RegisterNative(
					"scalar_native_add",
					VeroScript::ENativeType::Int,
					{VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
					ScalarNativeAdd,
					&Probe,
					ErrorId),
				"register scalar native JIT fixture")
			|| !Expect(
				Registry.RegisterNative(
					"scalar_native_add_i64",
					VeroScript::ENativeType::Int64,
					{VeroScript::ENativeType::Int64, VeroScript::ENativeType::Int64},
					ScalarNativeAddI64,
					&Probe,
					ErrorId),
				"register int64 scalar native JIT fixture"))
		{
			std::cerr << ErrorId << '\n';
			return EXIT_FAILURE;
		}

		VeroScript::FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = &Registry;
		const VeroScript::FCompileResult Compile = VeroScript::VeroScriptCompile::CompileSource(
			"int sum_native(int count) {\n"
			"\tint total = 0;\n"
			"\tint i = 0;\n"
			"\twhile (i < count) {\n"
			"\t\ttotal = scalar_native_add(total, 1);\n"
			"\t\ti = i + 1;\n"
			"\t}\n"
			"\treturn total;\n"
			"}\n"
			"int64 sum_native_i64(int count) {\n"
			"\tint64 total = 0i64;\n"
			"\tint i = 0;\n"
			"\twhile (i < count) {\n"
			"\t\ttotal = scalar_native_add_i64(total, 1i64);\n"
			"\t\ti = i + 1;\n"
			"\t}\n"
			"\treturn total;\n"
			"}\n"
			"void main() {\n"
			"\tprint(sum_native(32));\n"
			"\tsum_native(32);\n"
			"\tsum_native(32);\n"
			"\tsum_native(32);\n"
			"\tsum_native(32);\n"
			"\tsum_native(32);\n"
			"\tsum_native(32);\n"
			"\tsum_native(32);\n"
			"\tprint(to_int(sum_native_i64(32)));\n"
			"\tsum_native_i64(32);\n"
			"\tsum_native_i64(32);\n"
			"\tsum_native_i64(32);\n"
			"\tsum_native_i64(32);\n"
			"\tsum_native_i64(32);\n"
			"\tsum_native_i64(32);\n"
			"\tsum_native_i64(32);\n"
			"}\n",
			"<scalar_native_jit>",
			Options);
		if (!Expect(Compile.bSuccess && static_cast<bool>(Compile.Module),
				"compile scalar native JIT fixture"))
		{
			std::cerr << Compile.Error.Message << '\n';
			return EXIT_FAILURE;
		}
		const std::uint16_t SumNativeIndex =
			FindFunctionIndex(*Compile.Module, "sum_native");
		const std::uint16_t SumNativeI64Index =
			FindFunctionIndex(*Compile.Module, "sum_native_i64");
		VeroScript::AsmJit::FJitCodeCache GuardCache;
		if (!Expect(SumNativeIndex != static_cast<std::uint16_t>(-1)
				&& SumNativeI64Index != static_cast<std::uint16_t>(-1),
				"locate scalar native guard functions")
			|| !Expect(GuardCache.EnsureCompiled(*Compile.Module, SumNativeIndex)
					== VeroScript::AsmJit::EJitCompileStatus::Ok
					&& GuardCache.EnsureCompiled(*Compile.Module, SumNativeI64Index)
						== VeroScript::AsmJit::EJitCompileStatus::Ok,
				"compile scalar native guard functions"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::AsmJit::FJitFunctionEntry* const SumEntry =
			GuardCache.GetCandidate(SumNativeIndex);
		const VeroScript::AsmJit::FJitFunctionEntry* const SumI64Entry =
			GuardCache.GetCandidate(SumNativeI64Index);
		if (!Expect(SumEntry && SumI64Entry
				&& SumEntry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
				&& SumI64Entry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
				&& SumEntry->bRequiresScalarNativeGuard
				&& SumI64Entry->bRequiresScalarNativeGuard
				&& SumEntry->ScalarNativeCallGuards.size() == 1
				&& SumI64Entry->ScalarNativeCallGuards.size() == 1
				&& SumEntry->ScalarNativeCallGuards[0] == 2u
				&& SumI64Entry->ScalarNativeCallGuards[0] == ((1u << 8u) | 2u),
				"scalar native guard sites are decoded once into the JIT entry"))
		{
			return EXIT_FAILURE;
		}

		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			Probe = {};
			std::ostringstream Output;
			std::ostringstream Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
			if (!Expect(
					Vm.Execute(
						*Compile.Module,
						VeroScript::ERunMode::Script,
						{},
						&Registry.GetNativeFunctions())
						&& Output.str() == "32\n32\n"
						&& Probe.Calls == 512
						&& !Probe.bInvalidContext,
					"scalar native semantics and context stay identical"))
			{
				std::cerr << "backend=" << static_cast<int>(Backend)
					<< " calls=" << Probe.Calls
					<< " invalid=" << Probe.bInvalidContext
					<< " out=[" << Output.str() << "] err=" << Errors.str()
					<< Vm.GetError().Message << '\n';
				return EXIT_FAILURE;
			}

			#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
			const std::size_t FastCalls =
				Vm.GetJitStats().ScalarNativeFastPathCalls;
			if (!Expect(
					Backend == VeroScript::EExecutionBackend::Vm
						? FastCalls == 0
						: FastCalls > 256,
					"scalar native JIT bridge is reached after JIT warmup"))
			{
				std::cerr << "backend=" << static_cast<int>(Backend)
					<< " actual=" << FastCalls
					<< " native=" << Vm.GetJitStats().NativeCfgEntries
					<< " linear=" << Vm.GetJitStats().LinearTrampolineEntries
					<< " vm=" << Vm.GetJitStats().VmTrampolineEntries << '\n';
				return EXIT_FAILURE;
			}
			#endif
		}

		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		std::string CallbackFailure;
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Jit})
		{
			Probe = {};
			Probe.bFail = true;
			std::ostringstream Output;
			std::ostringstream Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
			const bool bOk = Vm.Execute(
				*Compile.Module,
				VeroScript::ERunMode::Script,
				{},
				&Registry.GetNativeFunctions());
			if (Backend == VeroScript::EExecutionBackend::Vm)
			{
				CallbackFailure = Vm.GetError().Message;
			}
			if (!Expect(
					!bOk
						&& CallbackFailure == "scalar native probe failure"
						&& Vm.GetError().Message == CallbackFailure
						&& (Backend == VeroScript::EExecutionBackend::Vm
							? Vm.GetJitStats().ScalarNativeFastPathCalls == 0
							: Vm.GetJitStats().ScalarNativeFastPathCalls > 0),
					"scalar native callback failures preserve VM diagnostics"))
			{
				return EXIT_FAILURE;
			}
		}

		// A manually supplied table can disagree with the catalog used to compile.
		// The function-level guard must route that case back through the VM instead
		// of entering a scalar-only linear frame.
		std::vector<VeroScript::FNativeFunction> Incompatible =
			Registry.GetNativeFunctions();
		Incompatible[0].ParamFlags = {
			VeroScript::NativeParamFlagRef,
			0};
		std::string BaselineError;
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Jit})
		{
			Probe = {};
			std::ostringstream Output;
			std::ostringstream Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
			const bool bOk = Vm.Execute(
				*Compile.Module,
				VeroScript::ERunMode::Script,
				{},
				&Incompatible);
			if (Backend == VeroScript::EExecutionBackend::Vm)
			{
				BaselineError = Vm.GetError().Message;
			}
			if (!Expect(
					!bOk
						&& !Vm.GetError().Message.empty()
						&& Vm.GetError().Message == BaselineError
						&& Vm.GetJitStats().ScalarNativeFastPathCalls == 0,
					"scalar native guard preserves incompatible-table VM errors"))
			{
				std::cerr << "backend=" << static_cast<int>(Backend)
					<< " error=[" << Vm.GetError().Message
					<< "] baseline=[" << BaselineError << "]\n";
				return EXIT_FAILURE;
			}
		}
		#endif
	}
