// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	{
		using namespace VeroScript;
		using namespace VeroScript::AsmJit;
		const auto BitsOf = [](const double Value)
		{
			std::uint64_t Bits = 0;
			std::memcpy(&Bits, &Value, sizeof(Bits));
			return Bits;
		};
		const auto IsNaNBits = [](const std::uint64_t Bits)
		{
			return (Bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull
				&& (Bits & 0x000fffffffffffffull) != 0u;
		};
		const auto SameDouble = [&](const std::uint64_t Actual, const double Expected)
		{
			const auto ExpectedBits = BitsOf(Expected);
			return Actual == ExpectedBits || (IsNaNBits(Actual) && IsNaNBits(ExpectedBits));
		};
		const auto LocalSlot = [](const FBytecodeFunction& Function, const char* Name)
		{
			for (std::size_t Slot = 0; Slot < Function.LocalNames.size(); ++Slot)
			{
				if (Function.LocalNames[Slot] == Name) return Slot;
			}
			return static_cast<std::size_t>(Function.LocalCount);
		};
		const auto Module = CompileSource(
			"double mixed_cached(int n, double seed, double delta, bool early) { "
			"double value = seed; double total = 0.0; "
			"for (int i = 0; i < n; ++i) { double term = cast<double>(i % 7) * delta; "
			"if ((i & 1) == 0) value = value * 0.875 + term; "
			"else value = value * 0.5 - term; total += value; "
			"if (early && i == 2) return total - value; } return total + value; }\n"
			"double cached_parameter(int n, double value) { "
			"for (int i = 0; i < n; ++i) { value += 1; } return value; }\n"
			"double cached_optional(int n, bool enter) { double total = 0.0; "
			"for (int i = 0; i < n; ++i) { if (enter) { double part = 1.25; "
			"total += part; } } return total; }\n"
			"double cached_overflow(int n, int seed) { double value = 1.25; "
			"for (int i = 0; i < n; ++i) { int next = seed + 1; seed = next; "
			"double part = cast<double>(next); value += part; } return value; }\n"
			"double cached_divide(int n, double divisor) { double value = 2.0; "
			"double total = 1.0; for (int i = 0; i < n; ++i) { "
			"value = value / divisor; total += value; } return total + value; }\n"
			"void main() {}\n");
		if (!Expect(static_cast<bool>(Module), "compile mixed F64 local cache cases")) return EXIT_FAILURE;
		JitNative::FArena Arena;
		const auto FunctionIndex = FindFunctionIndex(*Module, "mixed_cached");
		if (!Expect(FunctionIndex < Module->Functions.size(), "find mixed F64 cache loop")) return EXIT_FAILURE;
		const auto& Function = Module->Functions[FunctionIndex];
		const auto Kernel = Arena.EmitLinearMixedCfg(*Module, Function);
		const auto ValueSlot = LocalSlot(Function, "value");
		const auto TotalSlot = LocalSlot(Function, "total");
		if (!Expect(Kernel && ValueSlot < Function.LocalCount && TotalSlot < Function.LocalCount,
				"emit mixed F64 cache loop"))
		{
			return EXIT_FAILURE;
		}
		const std::uint64_t Seeds[]{0u, 0x8000000000000000ull, 0x3ff4000000000000ull,
			0xc004000000000000ull, 0x7fefffffffffffffull, 0x7ff0000000000000ull,
			0xfff0000000000000ull, 0x7ff8000000000042ull};
		for (const auto SeedBits : Seeds)
		{
			double Seed = 0.0;
			std::memcpy(&Seed, &SeedBits, sizeof(Seed));
			for (const int Count : {-1, 0, 1, 2, 3, 17, 2048})
			{
				for (const bool Early : {false, true})
				{
					// Volatile intermediates make every source multiply/add/sub round
					// separately; a compiler cannot replace this oracle with an FMA.
					volatile double Value = Seed;
					volatile double Total = 0.0;
					double Expected = 0.0;
					bool Returned = false;
					for (int Index = 0; Index < Count; ++Index)
					{
						volatile double Term = static_cast<double>(Index % 7) * 0.03125;
						volatile double Product = Value * ((Index & 1) == 0 ? 0.875 : 0.5);
						Value = (Index & 1) == 0 ? Product + Term : Product - Term;
						Total = Total + Value;
						if (Early && Index == 2)
						{
							Expected = Total - Value;
							Returned = true;
							break;
						}
					}
					if (!Returned)
					{
						Expected = Total + Value;
					}
					std::vector<std::uint64_t> Locals(Function.LocalCount, 0x123456789abcdef0ull);
					std::vector<std::uint64_t> Stack(Function.Code.size() + 16u);
					Locals[0] = static_cast<std::uint32_t>(Count);
					Locals[1] = SeedBits;
					Locals[2] = BitsOf(0.03125);
					Locals[3] = Early ? 1u : 0u;
					std::uint64_t Out = 0;
					const auto Status = Kernel(nullptr, Locals.data(), Stack.data(), &Out, nullptr);
					if (!Expect(Status == 0 && SameDouble(Out, Expected) && SameDouble(Locals[ValueSlot], Value) && SameDouble(Locals[TotalSlot], Total),
							"mixed F64 multi-local branches, early return and IEEE values match scalar oracle"))
					{
						return EXIT_FAILURE;
					}
				}
			}
		}
		for (const char* Name : {"cached_parameter", "cached_optional", "cached_overflow", "cached_divide"})
		{
			const auto Index = FindFunctionIndex(*Module, Name);
			if (!Expect(Index < Module->Functions.size(), "find F64 local cache boundary case")) return EXIT_FAILURE;
			const auto& Boundary = Module->Functions[Index];
			const auto BoundaryKernel = Arena.EmitLinearMixedCfg(*Module, Boundary);
			if (!Expect(BoundaryKernel != nullptr, "emit F64 local cache boundary case")) return EXIT_FAILURE;
			for (const int Count : {0, 1, 3})
			{
				constexpr std::uint64_t Unwritten = 0x123456789abcdef0ull;
				std::vector<std::uint64_t> Locals(Boundary.LocalCount, Unwritten);
				std::vector<std::uint64_t> Stack(Boundary.Code.size() + 16u);
				Locals[0] = static_cast<std::uint32_t>(Count);
				Locals[1] = Name == std::string_view("cached_parameter") ? BitsOf(-0.0)
					: Name == std::string_view("cached_overflow") ? static_cast<std::uint32_t>(INT32_MAX) : 0u;
				std::uint64_t Out = Unwritten;
				const auto Failure = DecodeJitKernelFailure(
					BoundaryKernel(nullptr, Locals.data(), Stack.data(), &Out, nullptr));
				const auto PartSlot = LocalSlot(Boundary, "part");
				if (Name == std::string_view("cached_parameter"))
				{
					if (!Expect(Failure.Status == EJitRuntimeStatus::Ok && Out == BitsOf(Count == 0 ? -0.0 : static_cast<double>(Count)) && Locals[1] == Out, "F64 parameter cache preserves by-value mutation and signed zero"))
					{
						return EXIT_FAILURE;
					}
				}
				else if (Name == std::string_view("cached_optional"))
				{
					if (!Expect(Failure.Status == EJitRuntimeStatus::Ok && Out == BitsOf(0.0) && PartSlot < Boundary.LocalCount && Locals[PartSlot] == Unwritten,
							"F64 cache does not flush a branch-local slot before initialization"))
					{
						return EXIT_FAILURE;
					}
					Locals[1] = 1u;
					if (!Expect(BoundaryKernel(nullptr, Locals.data(), Stack.data(), &Out, nullptr) == 0 && Out == BitsOf(Count * 1.25) && Locals[PartSlot] == (Count == 0 ? Unwritten : BitsOf(1.25)),
							"F64 branch-local stores become visible at native return"))
					{
						return EXIT_FAILURE;
					}
				}
				else
				{
					const auto ExpectedStatus = Count == 0 ? EJitRuntimeStatus::Ok
						: Name == std::string_view("cached_overflow") ? EJitRuntimeStatus::IntegerOverflow
						: EJitRuntimeStatus::DivisionByZero;
					const auto CurrentSlot = LocalSlot(Boundary, "value");
					if (!Expect(Failure.Status == ExpectedStatus && CurrentSlot < Boundary.LocalCount && Locals[CurrentSlot] == BitsOf(Name == std::string_view("cached_overflow") ? 1.25 : 2.0) && (Count == 0 || Out == Unwritten) && (PartSlot == Boundary.LocalCount || Locals[PartSlot] == Unwritten),
							"F64 cache flushes initialized locals and leaves output/unwritten slots intact on checked failure"))
					{
						return EXIT_FAILURE;
					}
				}
			}
		}

		// Exceed the ten-register cache and mix cached/ordinary local accesses,
		// including the existing fused affine updates in the same loop.
		std::ostringstream ManySource;
		ManySource << "double many_cached(int n, bool fail) { ";
		for (int Index = 0; Index < 12; ++Index)
		{
			ManySource << "double v" << Index << " = " << Index << ".25; ";
		}
		ManySource << "for (int i = 0; i < n; ++i) { ";
		for (int Index = 0; Index < 12; ++Index)
		{
			ManySource << "v" << Index << " = v" << Index << " * 0.5 + 0.03125; ";
		}
		ManySource << "} if (fail) { int checked = n; checked += 2147483647; } return ";
		for (int Index = 0; Index < 12; ++Index)
		{
			ManySource << (Index == 0 ? "" : " + ") << "v" << Index;
		}
		ManySource << "; } void main() {}";
		const auto ManyModule = CompileSource(ManySource.str());
		if (!Expect(static_cast<bool>(ManyModule), "compile bounded F64 register cache case")) return EXIT_FAILURE;
		const auto ManyIndex = FindFunctionIndex(*ManyModule, "many_cached");
		if (!Expect(ManyIndex < ManyModule->Functions.size(), "find bounded F64 cache case")) return EXIT_FAILURE;
		const auto& Many = ManyModule->Functions[ManyIndex];
		const auto ManyKernel = Arena.EmitLinearMixedCfg(*ManyModule, Many);
		if (!Expect(ManyKernel != nullptr, "emit bounded F64 register cache case")) return EXIT_FAILURE;
		for (const int Count : {0, 1, 29})
		{
			std::vector<std::uint64_t> Locals(Many.LocalCount);
			std::vector<std::uint64_t> Stack(Many.Code.size() + 16u);
			Locals[0] = static_cast<std::uint32_t>(Count);
			double Expected = 0.0;
			for (int Index = 0; Index < 12; ++Index)
			{
				volatile double Value = Index + 0.25;
				for (int Step = 0; Step < Count; ++Step)
				{
					volatile double Product = Value * 0.5;
					Value = Product + 0.03125;
				}
				Expected += Value;
			}
			std::uint64_t Out = 0;
			if (!Expect(ManyKernel(nullptr, Locals.data(), Stack.data(), &Out, nullptr) == 0 && SameDouble(Out, Expected), "F64 local cache remains correct beyond available XMM registers"))
			{
				return EXIT_FAILURE;
			}
		}

		const auto OverflowIndex = FindFunctionIndex(*Module, "cached_overflow");
		const auto& Overflow = Module->Functions[OverflowIndex];
		const auto OverflowKernel = Arena.EmitLinearMixedCfg(*Module, Overflow);
		if (!Expect(OverflowKernel != nullptr, "emit F64 cache failure-after-write case")) return EXIT_FAILURE;
		std::vector<std::uint64_t> OverflowLocals(Overflow.LocalCount);
		std::vector<std::uint64_t> OverflowStack(Overflow.Code.size() + 16u);
		OverflowLocals[0] = 3u;
		OverflowLocals[1] = static_cast<std::uint32_t>(INT32_MAX - 1);
		std::uint64_t OverflowOut = 0x123456789abcdef0ull;
		const auto AfterWriteFailure = DecodeJitKernelFailure(OverflowKernel(
			nullptr, OverflowLocals.data(), OverflowStack.data(), &OverflowOut, nullptr));
		if (!Expect(AfterWriteFailure.Status == EJitRuntimeStatus::IntegerOverflow && OverflowLocals[LocalSlot(Overflow, "value")] == BitsOf(1.25 + static_cast<double>(INT32_MAX)) && OverflowLocals[LocalSlot(Overflow, "part")] == BitsOf(static_cast<double>(INT32_MAX)) && OverflowOut == 0x123456789abcdef0ull,
				"F64 cache commits prior completed loop writes when a later iteration overflows"))
		{
			return EXIT_FAILURE;
		}

		// Call through a generated MS x64 wrapper so the compiler cannot hide a
		// broken nonvolatile-register contract by spilling its own test values.
		// Give every XMM register distinct low/high qwords and verify all 128 bits
		// after both normal and checked-failure exits, as well as preserved r13.
		struct FAbiProbeCall
		{
			JitNative::FLinearMixedKernel Kernel = nullptr;
			std::uint64_t* Locals = nullptr;
			std::uint64_t* Stack = nullptr;
			std::uint64_t* Out = nullptr;
			std::int32_t Status = 0;
			std::uint64_t R13 = 0;
			std::array<std::array<std::uint64_t, 2>, 10> Xmm{};
		};
		using FAbiProbe = void (VEROSCRIPT_JIT_CALL *)(FAbiProbeCall*);
		asmjit::JitRuntime ProbeRuntime;
		asmjit::CodeHolder ProbeCode;
		if (!Expect(ProbeCode.init(ProbeRuntime.environment(), ProbeRuntime.cpu_features()) == asmjit::kErrorOk,
				"initialize F64 cache ABI probe"))
		{
			return EXIT_FAILURE;
		}
		asmjit::x86::Assembler ProbeAssembler(&ProbeCode);
		auto& A = ProbeAssembler;
		namespace X86 = asmjit::x86;
		constexpr std::uint64_t R13Sentinel = 0xa13579bdf02468ceull;
		constexpr std::uint64_t LowSentinel = 0x0123456789abcdefull;
		constexpr std::uint64_t HighSentinel = 0xfedcba9876543210ull;
		A.push(X86::rbx);
		A.push(X86::r13);
		A.sub(X86::rsp, 216u); // 32-byte shadow + fifth argument + ten saves; align call.
		A.mov(X86::rbx, X86::rcx);
		A.mov(X86::r13, R13Sentinel);
		for (std::uint32_t Index = 0; Index < 10u; ++Index)
		{
			const auto Reg = X86::xmm(6u + Index);
			A.movdqu(X86::ptr(X86::rsp, static_cast<std::int32_t>(48u + Index * 16u)), Reg);
			A.mov(X86::rax, LowSentinel + Index);
			A.movq(Reg, X86::rax);
			A.mov(X86::rax, HighSentinel - Index);
			A.movq(X86::xmm0, X86::rax);
			A.punpcklqdq(Reg, X86::xmm0);
		}
		A.xor_(X86::ecx, X86::ecx);
		A.mov(X86::rdx, X86::qword_ptr(X86::rbx, offsetof(FAbiProbeCall, Locals)));
		A.mov(X86::r8, X86::qword_ptr(X86::rbx, offsetof(FAbiProbeCall, Stack)));
		A.mov(X86::r9, X86::qword_ptr(X86::rbx, offsetof(FAbiProbeCall, Out)));
		A.mov(X86::qword_ptr(X86::rsp, 32), 0u);
		A.mov(X86::rax, X86::qword_ptr(X86::rbx, offsetof(FAbiProbeCall, Kernel)));
		A.call(X86::rax);
		A.mov(X86::dword_ptr(X86::rbx, offsetof(FAbiProbeCall, Status)), X86::eax);
		A.mov(X86::qword_ptr(X86::rbx, offsetof(FAbiProbeCall, R13)), X86::r13);
		for (std::uint32_t Index = 0; Index < 10u; ++Index)
		{
			const auto Reg = X86::xmm(6u + Index);
			A.movdqu(X86::ptr(X86::rbx, static_cast<std::int32_t>(offsetof(FAbiProbeCall, Xmm) + Index * 16u)), Reg);
			A.movdqu(Reg, X86::ptr(X86::rsp, static_cast<std::int32_t>(48u + Index * 16u)));
		}
		A.add(X86::rsp, 216u);
		A.pop(X86::r13);
		A.pop(X86::rbx);
		A.ret();
		FAbiProbe AbiProbe = nullptr;
		if (!Expect(ProbeRuntime.add(&AbiProbe, &ProbeCode) == asmjit::kErrorOk && AbiProbe,
				"emit F64 cache ABI probe"))
		{
			return EXIT_FAILURE;
		}
		for (const bool Fail : {false, true})
		{
			std::vector<std::uint64_t> Locals(Many.LocalCount);
			std::vector<std::uint64_t> Stack(Many.Code.size() + 16u);
			Locals[0] = 3u;
			Locals[1] = Fail ? 1u : 0u;
			std::uint64_t Out = 0;
			FAbiProbeCall Probe;
			Probe.Kernel = ManyKernel;
			Probe.Locals = Locals.data();
			Probe.Stack = Stack.data();
			Probe.Out = &Out;
			AbiProbe(&Probe);
			if (!Expect(DecodeJitKernelFailure(Probe.Status).Status == (Fail ? EJitRuntimeStatus::IntegerOverflow : EJitRuntimeStatus::Ok) && Probe.R13 == R13Sentinel, "F64 cache preserves r13 on normal and failure returns"))
			{
				return EXIT_FAILURE;
			}
			for (std::size_t Index = 0; Index < Probe.Xmm.size(); ++Index)
			{
				if (!Expect(Probe.Xmm[Index][0] == LowSentinel + Index && Probe.Xmm[Index][1] == HighSentinel - Index,
						"F64 cache preserves all 128 bits of xmm6-15 on normal and failure returns"))
				{
					return EXIT_FAILURE;
				}
			}
		}
		{
			const auto Slot = LocalSlot(Many, "v0");
			if (!Expect(Slot < Many.LocalCount, "find cached local for aliased raw return")) return EXIT_FAILURE;
			std::vector<std::uint64_t> Locals(Many.LocalCount);
			std::vector<std::uint64_t> Stack(Many.Code.size() + 16u);
			Locals[0] = 1u;
			if (!Expect(ManyKernel(nullptr, Locals.data(), Stack.data(), &Locals[Slot], nullptr) == 0 && Locals[Slot] == BitsOf(34.875),
					"F64 cache commits locals before an aliased raw return store"))
			{
				return EXIT_FAILURE;
			}
		}

		FBytecodeModule UninitializedModule;
		FBytecodeConstant Zero;
		Zero.Kind = EConstantKind::I32;
		Zero.I32 = 0;
		FBytecodeConstant One;
		One.Kind = EConstantKind::F64;
		One.F64 = 1.0;
		UninitializedModule.Constants = {Zero, One};
		FBytecodeFunction Uninitialized;
		Uninitialized.Name = "uninitialized_f64";
		Uninitialized.ClosureTypeName = "fn(int,bool)->double";
		Uninitialized.ParamCount = 2;
		Uninitialized.LocalCount = 4;
		Uninitialized.bReturnsVoid = false;
		// The false edge skips slot3's only initialization, then a counted loop
		// joins it with the initialized edge before attempting to return slot3.
		Uninitialized.Code = {
			static_cast<std::uint8_t>(EOpcode::ConstI32), 0, 0,
			static_cast<std::uint8_t>(EOpcode::StoreLocal), 2, 0,
			static_cast<std::uint8_t>(EOpcode::LoadLocal), 1, 0,
			static_cast<std::uint8_t>(EOpcode::JmpIfFalse), 6, 0, 0, 0,
			static_cast<std::uint8_t>(EOpcode::ConstF64), 1, 0,
			static_cast<std::uint8_t>(EOpcode::StoreLocal), 3, 0,
			static_cast<std::uint8_t>(EOpcode::LoadLocal), 2, 0,
			static_cast<std::uint8_t>(EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(EOpcode::Lt),
			static_cast<std::uint8_t>(EOpcode::JmpIfFalse), 8, 0, 0, 0,
			static_cast<std::uint8_t>(EOpcode::IncLocal), 2, 0,
			static_cast<std::uint8_t>(EOpcode::Jmp), 236, 255, 255, 255,
			static_cast<std::uint8_t>(EOpcode::LoadLocal), 3, 0,
			static_cast<std::uint8_t>(EOpcode::Return)};
		UninitializedModule.Functions.push_back(std::move(Uninitialized));
		if (!Expect(Arena.EmitLinearMixedCfg(UninitializedModule, UninitializedModule.Functions[0]) == nullptr,
				"F64 local caching retains CFG definite-initialization rejection after branch/loop merges"))
		{
			return EXIT_FAILURE;
		}
	}
#endif
