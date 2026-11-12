// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptJitBackendTests / objects and dispatch suite. Included by the owning translation unit.

{
	// VM fallback must materialize map pair entries before indexed field access.
	const std::string Source =
			"void main() {\n"
			"\tconst map<string,int> m = {\"a\": 7};\n"
			"\tarray<pair<string,int>> es = m.entries();\n"
			"\tprint(es[0].first);\n"
			"\tprint(es[0].second);\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile map entries VM fallback suite")
			|| !ExpectRun(*Module, VeroScript::EExecutionBackend::Vm,
				"a\n7\n", "map entries VM fallback output"))
	{
		return EXIT_FAILURE;
	}
}

{
	// A receiver selected through an Array and a separate index expression must
	// retain virtual resolution while the proven pure i32 targets execute as one
	// typed loop. This guards the non-canonical receiver path: a local-only PIC
	// cannot satisfy this shape because the selected receiver changes each turn.
	const std::string Source =
			"interface IOperation { virtual int step(int value, int salt) = 0; }; "
			"class Add : IOperation { public: int step(int value, int salt) { "
			"return (value + 1 + (salt % 3)) % 1000000007; } }; "
			"class Xor : IOperation { public: int step(int value, int salt) { "
			"return ((value ^ (salt & 7)) + 7) % 1000000007; } }; "
			"int advance(int state) { return (state * 75 + 74) % 65521; } "
			"int run(int count, int seed) { "
			"array<IOperation> operations = {Add(), Xor()}; "
			"array<int> order = {1, 0, 1, 0}; int state = seed; int total = 0; "
			"for (int i = 0; i < count; i++) { state = advance(state); "
			"int index = order[(state + total) & 3]; "
			"IOperation operation = operations[index]; "
			"total = operation.step(total, state); } return total + state; } "
			"void main() { print(run(1000, 17)); }";
	const auto Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module),
				"compile array receiver typed virtual suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "35807\n",
					"array receiver typed virtual parity"))
		{
			return EXIT_FAILURE;
		}
	}
	const FExecutionCapture Capture = RunCapture(
			*Module, VeroScript::EExecutionBackend::Jit);
	if (!Expect(Capture.bSuccess && Capture.Output == "35807\n"
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
				&& Capture.JitStats.ScalarMethodFastPathI32 >= 998u
				&& Capture.JitStats.ArrayFastPathGets >= 1996u,
	#else
			,
	#endif
			"array receiver virtual call uses generation-qualified typed loop"))
	{
		return EXIT_FAILURE;
	}
}

{
	// The native multi-target kernel is entered only after proving the complete
	// recurrence, intrinsic arithmetic and both array lookups. Signed recurrences,
	// callee overflow and invalid order entries must retain exact VM diagnostics.
	const std::string Prefix =
		"interface EdgeOperation { virtual int step(int value, int salt) = 0; }; "
		"class EdgeAdd : EdgeOperation { public: int step(int value, int salt) { "
		"return (value + 1 + (salt % 3)) % 1000000007; } }; "
		"class EdgeXor : EdgeOperation { public: int step(int value, int salt) { "
		"return ((value ^ (salt & 7)) + 7) % 1000000007; } }; "
		"int edgeAdvance(int state) { return (state * 75 + 74) % 65521; } ";
	if (!ExpectDifferentialRun(Prefix
			+ "int run(int count, int seed) { array<EdgeOperation> operations = "
			  "{EdgeAdd(), EdgeXor()}; array<int> order = {1,0,1,0}; "
			  "int state=seed;int total=0;for(int i=0;i<count;i++){"
			  "state=edgeAdvance(state);int index=order[(state+total)&3];"
			  "EdgeOperation operation=operations[index];"
			  "total=operation.step(total,state);}return total+state;}"
			  "void main(){print(run(5,-17));}",
			"array virtual signed recurrence fallback parity")
		|| !ExpectDifferentialRun(
			"interface OverflowOperation { virtual int step(int value,int salt)=0; };"
			"class OverflowAdd:OverflowOperation{public:int step(int value,int salt){"
			"return(value+2147483647+(salt%3))%1000000007;}};"
			"class OverflowXor:OverflowOperation{public:int step(int value,int salt){"
			"return((value^(salt&7))+7)%1000000007;}};"
			"int advanceOverflow(int state){return(state*75+74)%65521;}"
			"int run(int count){array<OverflowOperation> operations="
			"{OverflowAdd(),OverflowXor()};array<int> order={0};int state=17;"
			"int total=0;for(int i=0;i<count;i++){state=advanceOverflow(state);"
			"int index=order[(state+total)&0];OverflowOperation operation="
			"operations[index];total=operation.step(total,state);}return total;}"
			"void main(){print(run(1));}",
			"array virtual intrinsic overflow fallback parity")
		|| !ExpectDifferentialRun(Prefix
			+ "int run(int count){array<EdgeOperation> operations="
			  "{EdgeAdd(),EdgeXor()};array<int> order={2};int state=17;"
			  "int total=0;for(int i=0;i<count;i++){state=edgeAdvance(state);"
			  "int index=order[(state+total)&0];EdgeOperation operation="
			  "operations[index];total=operation.step(total,state);}return total;}"
			  "void main(){print(run(1));}",
			"array virtual invalid order fallback parity")
		|| !ExpectDifferentialRun(
			"interface ReducingOperation{virtual int step(int value,int salt)=0;};"
			"class ReducingAdd:ReducingOperation{public:int step(int value,int salt){"
			"return(value+1+(salt%3))%97;}};"
			"class ReducingXor:ReducingOperation{public:int step(int value,int salt){"
			"return((value^(salt&7))+7)%97;}};"
			"int reducingAdvance(int state){return(state*75+74)%65521;}"
			"int run(int count){array<ReducingOperation> operations="
			"{ReducingAdd(),ReducingXor()};array<int> order={1,0,1,0};"
			"int state=17;int total=0;for(int i=0;i<count;i++){"
			"state=reducingAdvance(state);int index=order[(state+total)&3];"
			"ReducingOperation operation=operations[index];"
			"total=operation.step(total,state);}return total+state;}"
			"void main(){print(run(1000));}",
			"array virtual required result reduction parity"))
	{
		return EXIT_FAILURE;
	}
}

{
	// A resolved method may update one i32 field and return that updated field.
	// The direct summary must preserve the returned value, both checked additions,
	// both modulo operations, virtual resolution and ordinary field mutation.
	const std::string Source =
			"interface ICounter { virtual int update(int salt) = 0; }; "
			"class Counter : ICounter { public: int value = 1; "
			"int update(int salt) { value = (value + (salt % 17) + 1) % 1000000007; "
			"return value; } }; "
			"int exercise(ICounter counter) { int total = 0; "
			"for (int salt = 17; salt <= 51; salt += 17) { "
			"total += counter.update(salt); } return total; } "
			"void main() { Counter counter = Counter(); print(exercise(counter)); "
			"print(counter.value); }";
	const auto Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module),
				"compile returned direct-field method suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "9\n4\n",
					"returned direct-field method parity"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* const Entry = Cache.GetCandidate(
			FindFunctionIndex(*Module, "update"));
	if (!Expect(Entry && Entry->bDirectThisFieldAddModuloI32
				&& Entry->ScalarReturnKind == VeroScript::AsmJit::EJitScalarValueKind::I32
				&& Entry->DirectThisFieldAddendI32 == 1
				&& Entry->DirectThisArgumentModuloI32 == 17
				&& Entry->DirectThisFieldModuloI32 == 1000000007,
				"returned direct-field method has exact native summary"))
	{
		return EXIT_FAILURE;
	}
	#endif
	const FExecutionCapture Capture = RunCapture(
			*Module, VeroScript::EExecutionBackend::Jit);
	if (!Expect(Capture.bSuccess && Capture.Output == "9\n4\n"
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
				&& Capture.JitStats.ScalarMethodFastPathI32 >= 3
	#endif
			,
				"returned direct-field method uses boxed fast path"))
	{
		return EXIT_FAILURE;
	}
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		std::istringstream Input;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		std::size_t MutationCount = 0;
		Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
		{
			if (!Event.Object || Event.FieldName != "value"
					|| Event.FieldIndex >= Event.Object->Fields.size())
			{
				return;
			}
			// A host observer may grow or replace object storage. Force pointer
			// invalidation here so a direct method cache cannot dereference a field
			// borrowed before the synchronous write barrier.
			Event.Object->Fields.reserve(Event.Object->Fields.capacity() + 64u);
			auto* const Value = std::get_if<std::int32_t>(
					&Event.Object->Fields[Event.FieldIndex]);
			if (Value)
			{
				*Value += 10;
				++MutationCount;
			}
		});
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script)
					&& Output.str() == "69\n34\n" && MutationCount == 3,
					"returned direct-field method safely reloads reentrant mutation result"))
		{
			return EXIT_FAILURE;
		}
	}
	const auto OverflowModule = CompileSource(
			"class Counter { public: int value = 2147483647; "
			"int update(int salt) { value = (value + (salt % 17) + 1) % 1000000007; "
			"return value; } }; void main() { Counter counter = Counter(); "
			"print(counter.update(0)); }");
	if (!Expect(static_cast<bool>(OverflowModule),
				"compile returned direct-field overflow suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		const FExecutionCapture Overflow = RunCapture(*OverflowModule, Backend);
		if (!Expect(!Overflow.bSuccess
					&& Overflow.ErrorMessageId == "diag.integer_overflow",
					"returned direct-field method preserves checked overflow"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Runtime-invariant signed divisors in a direct field method use the same
	// checked remainder semantics as the VM. Cover negative divisors explicitly:
	// the native strength reduction must truncate toward zero and must preserve
	// the INT_MIN % -1 overflow trap.
	const std::string Source =
			"class Counter { public: int value = -20; "
			"int update(int salt) { value = (value + (salt % -7) + 3) % -11; "
			"return value; } }; "
			"int exercise(Counter counter) { int total = 0; "
			"for (int salt = -10; salt <= 20; salt += 30) { "
			"total += counter.update(salt); } return total; } "
			"void main() { Counter counter = Counter(); print(exercise(counter)); "
			"print(counter.value); }";
	const auto Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module),
				"compile negative direct-field modulus suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "-9\n0\n",
					"negative direct-field modulus parity"))
		{
			return EXIT_FAILURE;
		}
	}
	const FExecutionCapture Capture = RunCapture(
			*Module, VeroScript::EExecutionBackend::Jit);
	if (!Expect(Capture.bSuccess && Capture.Output == "-9\n0\n"
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
				&& Capture.JitStats.ScalarMethodFastPathI32 >= 2u,
	#else
			,
	#endif
			"negative direct-field modulus uses checked native method path"))
	{
		return EXIT_FAILURE;
	}

	const auto OverflowModule = CompileSource(
			"class Counter { public: int value = 0; "
			"int update(int salt) { value = (value + (salt % -1) + 3) % -11; "
			"return value; } }; int exercise(Counter counter, int minimum) { "
			"int total = 0; for (int i = 0; i < 1; i++) { "
			"total += counter.update(minimum); } return total; } "
			"void main() { Counter counter = Counter(); int minimum = -2147483647 - 1; "
			"print(exercise(counter, minimum)); }");
	if (!Expect(static_cast<bool>(OverflowModule),
				"compile direct-field signed remainder overflow suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		const FExecutionCapture Overflow = RunCapture(*OverflowModule, Backend);
		if (!Expect(!Overflow.bSuccess
					&& Overflow.ErrorMessageId == "diag.integer_overflow",
					"direct-field signed remainder preserves INT_MIN overflow"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Every raw-field optimization must decline when a synchronous mutation
	// observer can replace field storage or re-enter the VM. Exercise repeated
	// use of the same bytecode site so a stale cache cannot hide behind distinct
	// call-site entries.
	const std::string Source =
			"class Counter { public: int value = 0; }; "
			"class UCounter { public: uint32 value = 0U; }; "
			"int inc_accumulate(Counter counter) { int total = 0; "
			"for (int i = 0; i < 3; i++) { counter.value++; total += counter.value; } "
			"return total; } "
			"void add_loop(Counter counter) { "
			"for (int i = 0; i < 3; i++) { counter.value += 1; } } "
			"void modulo_loop(Counter counter) { "
			"for (int i = 0; i < 3; i++) { counter.value = (counter.value + i) % 1000; } } "
			"void unsigned_loop(UCounter counter) { "
			"for (int i = 0; i < 3; i++) { counter.value += 1U; } } "
			"void main() { "
			"Counter first = Counter(); print(inc_accumulate(first)); print(first.value); "
			"Counter second = Counter(); add_loop(second); print(second.value); "
			"Counter third = Counter(); modulo_loop(third); print(third.value); "
			"UCounter fourth = UCounter(); unsigned_loop(fourth); print(fourth.value); }";
	const auto Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module),
				"compile reentrant cached-field mutation suite"))
	{
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
		std::size_t MutationCount = 0;
		Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
		{
			if (!Event.Object || Event.FieldName != "value"
					|| Event.FieldIndex >= Event.Object->Fields.size())
			{
				return;
			}
			Event.Object->Fields.reserve(Event.Object->Fields.capacity() + 64u);
			VeroScript::FValue& Field = Event.Object->Fields[Event.FieldIndex];
			if (auto* SignedValue = std::get_if<std::int32_t>(&Field))
			{
				*SignedValue += 10;
				++MutationCount;
			}
			else if (auto* UnsignedValue = std::get_if<std::uint32_t>(&Field))
			{
				*UnsignedValue += 10U;
				++MutationCount;
			}
		});
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script)
					&& Output.str() == "66\n33\n33\n33\n33\n"
					&& Errors.str().empty() && MutationCount == 12,
					"reentrant callbacks invalidate every raw field cache"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " cached-field mutation output='" << Output.str()
					<< "' count=" << MutationCount << " hasError=" << Vm.HasError()
					<< " exit=" << Vm.WasExitRequested()
					<< " errors='" << Errors.str() << "'\n";
			return EXIT_FAILURE;
		}
	}
}

{
	// A scalar-signature leaf that owns only local maps cannot publish their
	// handles. Its int/uint-key scalar values stay unboxed in the linear tier.
	const std::string Source =
			"int scalar_identity(int value) { return value; }\n"
			"int map_i32(int n) {\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tm.reserve(2048);\n"
			"\tint i = 0;\n"
			"\twhile (i < n) { m[i] = i + 1; i++; }\n"
			"\tm[-1] = 7; m[-1] = 9;\n"
			"\treturn m[n - 1] + m[-1];\n"
			"}\n"
			"double map_f64(unsigned int n) {\n"
			"\tmap<unsigned int,double> m = map<unsigned int,double>(); m.reserve(2048);\n"
			"\tunsigned int i = 0U;\n"
			"\tdouble v = 0.5;\n"
			"\twhile (i < n) { m[i] = v; v = v + 1.0; i++; }\n"
			"\treturn m[n - 1U];\n"
			"}\n"
			"unsigned int map_u32() {\n"
			"\tmap<int,unsigned int> m = map<int,unsigned int>();\n"
			"\tm[0] = 10U; m[1] = 20U; m[2] = 30U; m[3] = 40U;\n"
			"\tm[3] = 42U;\n"
			"\treturn m[3];\n"
			"}\n"
			"bool map_bool() {\n"
			"\tmap<unsigned int,bool> m = map<unsigned int,bool>();\n"
			"\tm[4U] = false; m[4U] = true;\n"
			"\treturn m[4U];\n"
			"}\n"
			"float map_f32() {\n"
			"\tmap<int,float> m = map<int,float>();\n"
			"\tm[1] = 1.25f; m[1] = 2.5f;\n"
			"\treturn m[1];\n"
			"}\n"
			"int64 map_i64() {\n"
			"\tmap<int64,int64> m = map<int64,int64>();\n"
			"\tm[7i64] = 40i64; m[7i64] = 41i64;\n"
			"\treturn m[7i64];\n"
			"}\n"
			"uint64 map_u64() {\n"
			"\tmap<uint64,uint64> m = map<uint64,uint64>();\n"
			"\tm[8u64] = 50u64; m[8u64] = 51u64;\n"
			"\treturn m[8u64];\n"
			"}\n"
			"int map_i8() {\n"
			"\tmap<int,int8> m = map<int,int8>(); int8 v = 12;\n"
			"\tm[0] = v;\n"
			"\treturn to_int(m[0]);\n"
			"}\n"
			"int map_u8() {\n"
			"\tmap<int,uint8> m = map<int,uint8>(); uint8 v = 13;\n"
			"\tm[0] = v; return to_int(m[0]);\n"
			"}\n"
			"int map_i16() {\n"
			"\tmap<int,int16> m = map<int,int16>(); int16 v = 14;\n"
			"\tm[0] = v; return to_int(m[0]);\n"
			"}\n"
			"int map_u16() {\n"
			"\tmap<int,uint16> m = map<int,uint16>(); uint16 v = 15;\n"
			"\tm[0] = v; return to_int(m[0]);\n"
			"}\n"
			"bool map_i64_bool() {\n"
			"\tmap<int64,bool> m = map<int64,bool>();\n"
			"\tm[9i64] = true; return m[9i64];\n"
			"}\n"
			"int map_with_now(int n) {\n"
			"\tdouble stamp = now();\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tfor (int i = 0; i < n; i++) { m[i] = i + 1; }\n"
			"\tif (stamp < 0.0) { return 0; }\n"
			"\treturn m[n - 1];\n"
			"}\n"
			"int map_with_scalar_call(int n) {\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tm.reserve(n);\n"
			"\tfor (int i = 0; i < n; i++) { m[i] = scalar_identity(i + 1); }\n"
			"\treturn m[n - 1];\n"
			"}\n"
			"void main() {\n"
			"\tprint(map_i32(1000));\n"
			"\tprint(to_string(map_f64(10U)));\n"
			"\tprint(map_u32());\n"
			"\tprint(map_bool());\n"
			"\tprint(map_f32() == 2.5f);\n"
			"\tprint(map_i64() == 41i64);\n"
			"\tprint(map_u64() == 51u64);\n"
			"\tprint(map_i8() == 12);\n"
			"\tprint(map_u8() == 13);\n"
			"\tprint(map_i16() == 14);\n"
			"\tprint(map_u16() == 15);\n"
			"\tprint(map_i64_bool());\n"
			"\tprint(map_with_now(8));\n"
			"\tprint(map_with_scalar_call(8));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile scalar map storage suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectRun(
					*Module, Backend,
					"1009\n9.5\n42\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n"
					"true\ntrue\ntrue\n8\n8\n",
					"scalar map storage parity"))
		{
			return EXIT_FAILURE;
		}
	}
	std::ostringstream Output;
	std::istringstream Input;
	std::ostringstream Errors;
	VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
	ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
	if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"scalar map storage JIT execution")
			|| !Expect(
				Output.str() == "1009\n9.5\n42\ntrue\ntrue\ntrue\ntrue\ntrue\n"
					"true\ntrue\ntrue\ntrue\n8\n8\n",
				"scalar map storage JIT output"))
	{
		return EXIT_FAILURE;
	}
	const VeroScript::FJitStats Stats = Vm.GetJitStats();
	if (!Expect(Stats.ScalarMapFastPathSets >= 1046,
				"scalar map stores use raw-value storage")
			|| !Expect(Stats.ScalarMapFastPathGets >= 14,
				"scalar map loads use raw-value storage"))
	{
		std::cerr << "scalar map stats get=" << Stats.ScalarMapFastPathGets
				<< " set=" << Stats.ScalarMapFastPathSets << '\n';
		return EXIT_FAILURE;
	}
}

{
	// Power-of-two flat tables must retain useful distribution for both
	// high-word int64 keys and low-word patterns that cancel under x^(x>>16).
	const std::string Source =
			"int hash_spread() {\n"
			"\tmap<int64,int> values = map<int64,int>(); values.reserve(32);\n"
			"\tset<int64> keys = set<int64>(); keys.reserve(32);\n"
			"\tvalues[65537i64] = 1; values[131074i64] = 2;\n"
			"\tvalues[196611i64] = 3; values[262148i64] = 4;\n"
			"\tvalues[4294967296i64] = 5; values[8589934592i64] = 6;\n"
			"\tkeys.add(65537i64); keys.add(131074i64); keys.add(196611i64);\n"
			"\tkeys.add(262148i64); keys.add(4294967296i64); keys.add(8589934592i64);\n"
			"\tif (!keys.has(65537i64) || !keys.has(8589934592i64)) return -1;\n"
			"\treturn values[65537i64] + values[131074i64] + values[196611i64]\n"
			"\t\t+ values[262148i64] + values[4294967296i64] + values[8589934592i64];\n"
			"}\n"
			"void main() { print(hash_spread()); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile scalar hash-spread suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "21\n", "scalar hash-spread parity"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Scalar-map accumulation fusion accepts both local keys and non-i32 scalar
	// values while preserving the VM's missing-key and overflow failures.
	const std::string Source =
			"int map_accumulate_local_key() {\n"
			"\tmap<int,int> m = map<int,int>(); m[2] = 7;\n"
			"\tint key = 2; int total = 0;\n"
			"\tfor (int i = 0; i < 5; i++) { total += m[key]; }\n"
			"\treturn total;\n"
			"}\n"
			"bool map_accumulate_f64() {\n"
			"\tmap<int,double> m = map<int,double>(); m[1] = 1.5;\n"
			"\tint key = 1; double total = 0.0;\n"
			"\tfor (int i = 0; i < 4; i++) { total += m[key]; }\n"
			"\treturn total == 6.0;\n"
			"}\n"
			"bool map_set_from_locals() {\n"
			"\tmap<int,double> m = map<int,double>();\n"
			"\tint key = 3; double value = 2.5; m[key] = value;\n"
			"\treturn m[key] == value;\n"
			"}\n"
			"int map_expression_reduction() {\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tfor (int i = 0; i < 8; i++) { m[i] = i % 4; }\n"
			"\tint total = 0;\n"
			"\tfor (int i = 0; i < 20; i++) { total = (total + m[i % 8]) % 17; }\n"
			"\treturn total;\n"
			"}\n"
			"void main() { print(map_accumulate_local_key()); "
			"print(map_accumulate_f64()); print(map_set_from_locals()); "
			"print(map_expression_reduction()); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile scalar map accumulation suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "35\ntrue\ntrue\n13\n",
					"scalar map accumulation parity"))
		{
			return EXIT_FAILURE;
		}
	}

	const std::string MissingSource =
			"int missing_map_accumulate() {\n"
			"\tmap<int,int> m = map<int,int>(); int total = 0;\n"
			"\ttotal += m[0]; return total;\n"
			"}\n"
			"void main() { print(missing_map_accumulate()); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> MissingModule =
			CompileSource(MissingSource);
	if (!Expect(static_cast<bool>(MissingModule),
				"compile scalar map accumulation missing-key suite"))
	{
		return EXIT_FAILURE;
	}
	const std::string OverflowSource =
			"int overflow_map_accumulate() {\n"
			"\tmap<int,int> m = map<int,int>(); m[0] = 1;\n"
			"\tint total = 2147483647; total += m[0]; return total;\n"
			"}\n"
			"void main() { print(overflow_map_accumulate()); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> OverflowModule =
			CompileSource(OverflowSource);
	if (!Expect(static_cast<bool>(OverflowModule),
				"compile scalar map accumulation overflow suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		const FExecutionCapture Missing = RunCapture(*MissingModule, Backend);
		if (!Expect(!Missing.bSuccess
					&& Missing.ErrorMessage.find("map key not found") != std::string::npos,
					"scalar map accumulation missing key parity"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " error=" << Missing.ErrorMessage << ' ' << Missing.Errors;
			return EXIT_FAILURE;
		}
		const FExecutionCapture Overflow = RunCapture(*OverflowModule, Backend);
		if (!Expect(!Overflow.bSuccess
					&& Overflow.ErrorMessageId == "diag.integer_overflow",
					"scalar map accumulation overflow parity"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " error=" << Overflow.ErrorMessageId << ' ' << Overflow.Errors;
			return EXIT_FAILURE;
		}
	}
}

{
	// Calling a container method is an observable/generic boundary. Such a map
	// must keep the canonical FValue storage and never enter the local raw tier.
	const std::string Source =
			"bool visible_map() {\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tm[1] = 7;\n"
			"\treturn m.has(1);\n"
			"}\n"
			"bool equal_maps() {\n"
			"\tmap<int,int> a = map<int,int>();\n"
			"\tmap<int,int> b = map<int,int>();\n"
			"\ta[1] = 7;\n"
			"\tb[1] = 8;\n"
			"\treturn a == b;\n"
			"}\n"
			"void main() { print(visible_map()); print(equal_maps()); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile escaping map guard suite"))
	{
		return EXIT_FAILURE;
	}
	std::ostringstream Output;
	std::istringstream Input;
	std::ostringstream Errors;
	VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
	ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
	if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"escaping map guard execution")
			|| !Expect(Output.str() == "true\nfalse\n", "escaping map guard output")
			|| !Expect(Vm.GetJitStats().ScalarMapFastPathGets == 0
					&& Vm.GetJitStats().ScalarMapFastPathSets == 0,
				"generic map boundary disables raw-value storage"))
	{
		return EXIT_FAILURE;
	}
}

{
	// The fused local-map assignment must perform the addition before mutating
	// typed storage and retain the interpreter's checked int32 overflow error.
	// A counted batch must decline the whole range when a later element would
	// overflow, preserving the exact per-iteration failure.
	const std::string Source =
			"int overflow_map() {\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tint key = 0;\n"
			"\tint value = 2147483647;\n"
			"\tm[key] = value + 1;\n"
			"\treturn m[key];\n"
			"}\n"
			"void main() { print(overflow_map()); }\n";
	const std::string LoopSource =
			"void overflow_map_loop() {\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tfor (int i = 0; i < 3; i++) { m[i] = i + 2147483647; }\n"
			"}\n"
			"void main() { overflow_map_loop(); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	std::unique_ptr<VeroScript::FBytecodeModule> LoopModule =
			CompileSource(LoopSource);
	if (!Expect(static_cast<bool>(Module), "compile scalar map overflow suite")
			|| !Expect(static_cast<bool>(LoopModule),
				"compile scalar map batch-overflow suite"))
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
					!Capture.bSuccess
						&& Capture.ErrorMessageId == "diag.integer_overflow",
					"scalar map fused add overflow must fail identically"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " error=" << Capture.ErrorMessageId << ' ' << Capture.Errors;
			return EXIT_FAILURE;
		}
		const FExecutionCapture LoopCapture = RunCapture(*LoopModule, Backend);
		if (!Expect(
					!LoopCapture.bSuccess
						&& LoopCapture.ErrorMessageId == "diag.integer_overflow"
						&& LoopCapture.ErrorLine == 3,
					"scalar map batch must preserve checked overflow position"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " error=" << LoopCapture.ErrorMessageId
					<< " line=" << LoopCapture.ErrorLine << ' ' << LoopCapture.Errors;
			return EXIT_FAILURE;
		}
	}
}

{
	// Returning a map or assigning it through a ref parameter publishes the
	// handle. These paths must retain canonical FValue-backed storage.
	const std::string Source =
			"map<int,int> make_map() {\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tm[1] = 17;\n"
			"\treturn m;\n"
			"}\n"
			"void set_map(ref map<int,int> out) {\n"
			"\tmap<int,int> m = map<int,int>();\n"
			"\tm[2] = 29;\n"
			"\tout = m;\n"
			"}\n"
			"void main() {\n"
			"\tmap<int,int> a = make_map();\n"
			"\tprint(a[1]);\n"
			"\tmap<int,int> b = map<int,int>();\n"
			"\tset_map(ref b);\n"
			"\tprint(b[2]);\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile scalar map escape suite"))
	{
		return EXIT_FAILURE;
	}
	std::ostringstream Output;
	std::istringstream Input;
	std::ostringstream Errors;
	VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
	ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
	if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"scalar map escape JIT execution")
			|| !Expect(Output.str() == "17\n29\n", "scalar map escape output")
			|| !Expect(Vm.GetJitStats().ScalarMapFastPathGets == 0
					&& Vm.GetJitStats().ScalarMapFastPathSets == 0,
				"return/ref escape disables raw-value map storage"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	// Closures with captures: body may interpret; call site stays correct under Jit.
	const std::string Source =
			"int apply(fn(int)->int f, int x) { return f(x); }\n"
			"void main() {\n"
			"\tint base = 40;\n"
			"\tfn(int)->int add = fn(int x) -> int => x + base;\n"
			"\tprint(apply(add, 2));\n"
			"\tprint(apply(fn(int n) -> int => n * 2, 21));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile closure suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "apply", VeroScript::AsmJit::EJitCompileStatus::Ok, "apply Ok"))
	{
		return EXIT_FAILURE;
	}
	const std::string Expected = "42\n42\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "closure suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Ref params + index refs on the linear frame path (ResolveRefStorage).
	const std::string Source =
			"void bump(ref int x) { x = x + 1; }\n"
			"void bump_at(ref array<int> xs, int i) { xs[i] = xs[i] + 1; }\n"
			"int ref_demo() {\n"
			"\tint n = 10;\n"
			"\tbump(ref n);\n"
			"\tarray<int> a = array<int>();\n"
			"\ta.push(5);\n"
			"\tbump(ref a[0]);\n"
			"\treturn n + a[0];\n"
			"}\n"
			"int map_demo() {\n"
			"\tmap<string, int> m = map<string, int>();\n"
			"\tm[\"a\"] = 3;\n"
			"\tm[\"b\"] = 4;\n"
			"\treturn m[\"a\"] + m[\"b\"];\n"
			"}\n"
			"void main() {\n"
			"\tprint(ref_demo());\n"
			"\tprint(map_demo());\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile ref/map suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "bump", VeroScript::AsmJit::EJitCompileStatus::Ok, "bump Ok"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectStatus(Cache, *Module, "ref_demo", VeroScript::AsmJit::EJitCompileStatus::Ok, "ref_demo Ok"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectStatus(Cache, *Module, "map_demo", VeroScript::AsmJit::EJitCompileStatus::Ok, "map_demo Ok"))
	{
		return EXIT_FAILURE;
	}
	// 11 (n) + 6 (a[0]) = 17
	const std::string Expected = "17\n7\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "ref/map suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Once an indexed reference escapes into a closure, both generic and
	// scalar-specialized SetIndex paths must update its heap cell in place.
	const std::string Source =
			"void main() {\n"
			"\tarray<int> values = {1};\n"
			"\tint& element = values[0];\n"
			"\tfunction()->int read_element = [&element]() -> int { return element; };\n"
			"\tvalues[0] = 5;\n"
			"\tprint(values[0]);\n"
			"\tprint(read_element());\n"
			"\tmap<int,int> mapped = map<int,int>();\n"
			"\tmapped[1] = 2;\n"
			"\tint& mapped_value = mapped[1];\n"
			"\tfunction()->int read_map = [&mapped_value]() -> int { return mapped_value; };\n"
			"\tmapped[1] = 8;\n"
			"\tprint(mapped[1]);\n"
			"\tprint(read_map());\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile escaped index ref suite"))
	{
		return EXIT_FAILURE;
	}
	const std::string Expected = "5\n5\n8\n8\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "escaped index ref suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Indexed array access through linear GetIndex/SetIndex.
	const std::string Source =
			"int at(array<int> xs, int i) { return xs[i]; }\n"
			"array<int> set_at(array<int> xs, int i, int v) {\n"
			"\txs[i] = v;\n"
			"\treturn xs;\n"
			"}\n"
			"void main() {\n"
			"\tarray<int> a = array<int>();\n"
			"\ta.push(10);\n"
			"\ta.push(20);\n"
			"\ta = set_at(a, 1, 99);\n"
			"\tprint(at(a, 0));\n"
			"\tprint(at(a, 1));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile index suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "at", VeroScript::AsmJit::EJitCompileStatus::Ok, "at Ok"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectStatus(Cache, *Module, "set_at", VeroScript::AsmJit::EJitCompileStatus::Ok, "set_at Ok"))
	{
		return EXIT_FAILURE;
	}
	const std::string Expected = "10\n99\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "index suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Capture-bearing CallIndirect must not corrupt caller locals via JIT.
	const std::string Source =
			"void main() {\n"
			"\tint x = 1;\n"
			"\tfn(int)->int f = fn(int n) -> int => x + n;\n"
			"\tprint(f(2));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile capture lambda"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectRun(
				*Module,
				VeroScript::EExecutionBackend::Jit,
				"3\n",
				"capture CallIndirect under jit"))
	{
		return EXIT_FAILURE;
	}
}

{
	// channel/spawn/join under Mixed + Jit (workers inherit RequestedExecutionBackend).
	const std::string Source =
			"void worker(channel<int> ch) {\n"
			"\tint x = __channel_recv(ch);\n"
			"\t__channel_send(ch, x + 1);\n"
			"}\n"
			"void main() {\n"
			"\tchannel<int> ch = channel<int>();\n"
			"\tint t = __spawn(\"worker\", ch);\n"
			"\t__channel_send(ch, 41);\n"
			"\t__join_task(t);\n"
			"\tprint(__channel_recv(ch));\n"
			"\tchannel<string> cs = channel<string>();\n"
			"\t__channel_send(cs, \"hi\");\n"
			"\tprint(__channel_recv(cs));\n"
			"\t__channel_close(cs);\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile channel/spawn suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "worker", VeroScript::AsmJit::EJitCompileStatus::Ok, "worker Ok"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectStatus(Cache, *Module, "main", VeroScript::AsmJit::EJitCompileStatus::Ok, "main Ok"))
	{
		return EXIT_FAILURE;
	}
	const std::string Expected = "42\nhi\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "channel/spawn suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Dynamic CallMethod (virtual/interface) via ExecuteCallMethod under all backends.
	const std::string Source =
			"interface I {\n"
			"\tvirtual int value() = 0;\n"
			"}\n"
			"class A : I {\n"
			"\tint value() { return 7; }\n"
			"}\n"
			"class B : I {\n"
			"\tint value() { return 11; }\n"
			"}\n"
			"int take(I x) { return x.value(); }\n"
			"void main() {\n"
			"\tprint(take(A()));\n"
			"\tprint(take(B()));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile CallMethod suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "take", VeroScript::AsmJit::EJitCompileStatus::Ok, "take Ok"))
	{
		return EXIT_FAILURE;
	}
	const std::uint16_t TakeIndex = FindFunctionIndex(*Module, "take");
	const VeroScript::AsmJit::FJitFunctionEntry* const TakeEntry = Cache.GetCandidate(TakeIndex);
	if (!Expect(
				TakeEntry != nullptr
					&& TakeEntry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
					&& TakeEntry->StackCapacityHint >= 1
					&& TakeEntry->BoxedStackCapacityHint >= 1,
				"dynamic interface dispatch uses sized FValue LinearTrampoline"))
	{
		if (TakeEntry)
		{
			std::cerr << " kind=" << static_cast<int>(TakeEntry->Kind)
					<< " stack=" << TakeEntry->StackCapacityHint
					<< " boxed=" << TakeEntry->BoxedStackCapacityHint << '\n';
		}
		return EXIT_FAILURE;
	}
	const std::string Expected = "7\n11\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "CallMethod suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Two distinct interface receivers are called alternately in the same frame.
	// This exercises the small polymorphic frame cache; the receiver type and the
	// implementation must be selected independently at each call site.
	const std::string Source =
			"interface I { virtual int value() = 0; }\n"
			"class A : I { int value() { return 3; } }\n"
			"class B : I { int value() { return 8; } }\n"
			"int take(I a, I b) {\n"
			"\tint total = 0; int i = 0;\n"
			"\twhile (i < 4) { total = total + a.value(); total = total + b.value(); i++; }\n"
			"\treturn total;\n"
			"}\n"
			"void main() { A a = A(); B b = B(); print(take(a, b)); print(take(b, a)); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile polymorphic frame-cache suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "44\n44\n",
				"polymorphic frame-cache dispatch semantics"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	const FExecutionCapture Capture =
			RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
	if (!Expect(Capture.bSuccess && Capture.Output == "44\n44\n",
				"constant virtual loop capture")
			|| !Expect(Capture.JitStats.ScalarMethodFastPathI32 == 4,
				"constant virtual loop resolves each rooted call site only once per frame"))
	{
		return EXIT_FAILURE;
	}
	#endif
}

{
	// A call site wider than the fixed cross-frame L2 must remain both correct
	// and allocation-free after its lazy runtime-type dispatch table warms up.
	constexpr int TypeCount = 160;
	std::ostringstream Source;
	Source << "interface IWideDispatch { virtual int value() = 0; "
			"virtual int other() = 0; };\n";
	for (int Index = 0; Index < TypeCount; ++Index)
	{
		Source << "class WideDispatch" << Index
				<< " : IWideDispatch { public: int value() override { return "
				<< Index << "; } int other() override { return 1; } };\n";
	}
	Source << "int take_wide(IWideDispatch value) { "
			"return value.value() + value.other(); }\n"
			<< "void main() { int total = 0; for (int round = 0; round < 3; round++) { ";
	for (int Index = 0; Index < TypeCount; ++Index)
	{
		Source << "total += take_wide(WideDispatch" << Index << "()); ";
	}
	Source << "} print(total); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source.str());
	if (!Expect(static_cast<bool>(Module), "compile wide polymorphic dispatch suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "38640\n",
				"wide polymorphic dispatch semantics"))
		{
			return EXIT_FAILURE;
		}
	}
}

if (!TestWideObjectFieldCache())
{
	return EXIT_FAILURE;
}
if (!TestClockBuiltinF64NativeCfg())
{
	return EXIT_FAILURE;
}
if (!TestFloatingAbsSpecialValues())
{
	return EXIT_FAILURE;
}
if (!TestNativeKernelDiagnosticLocations())
{
	return EXIT_FAILURE;
}
if (!TestLongBuiltinAddChain())
{
	return EXIT_FAILURE;
}
if (!TestAddChainOverloadFallback())
{
	return EXIT_FAILURE;
}
if (!TestAdditivePolymorphicTransformLoop())
{
	return EXIT_FAILURE;
}

{
	// Non-this object fields stay in the FValue tier: reads, writes and the
	// resulting mutation barrier still go through the VM's guarded field helpers.
	const std::string Source =
			"class FieldBox { public: int x = 1; int y = 2; double d = 1.5; }\n"
			"int touch(FieldBox value, int limit) {\n"
			"\tint i = 0;\n"
			"\twhile (i < limit) { value.x = value.x + value.y; i++; }\n"
			"\treturn value.x;\n"
			"}\n"
			"int sum_field(FieldBox value, int limit) {\n"
			"\tint result = 0;\n"
			"\tfor (int i = 0; i < limit; i++) { result += value.y; }\n"
			"\treturn result;\n"
			"}\n"
			"double sum_double(FieldBox value, int limit) {\n"
			"\tdouble result = 0.0;\n"
			"\tfor (int i = 0; i < limit; i++) { result += value.d; }\n"
			"\treturn result;\n"
			"}\n"
			"void sum_into(int& result, FieldBox value, int limit) {\n"
			"\tfor (int i = 0; i < limit; i++) { result += value.y; }\n"
			"}\n"
			"void main() { FieldBox value = FieldBox(); FieldBox other = FieldBox(); "
			"print(touch(value, 3)); print(sum_field(value, 3)); "
			"print(sum_field(other, 2)); print(sum_double(other, 2)); "
			"int referenced = 1; sum_into(ref referenced, value, 3); print(referenced); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile dynamic object-field suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	const std::uint16_t TouchIndex = FindFunctionIndex(*Module, "touch");
	if (!Expect(
				TouchIndex != static_cast<std::uint16_t>(-1)
					&& Cache.EnsureCompiled(*Module, TouchIndex)
						== VeroScript::AsmJit::EJitCompileStatus::Ok
					&& Cache.GetCandidate(TouchIndex) != nullptr
					&& Cache.GetCandidate(TouchIndex)->Kind
						== VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
					&& Cache.GetCandidate(TouchIndex)->BoxedStackCapacityHint >= 1,
				"dynamic object fields use sized FValue LinearTrampoline"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "7\n6\n4\n3\n7\n",
				"dynamic object-field semantics"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// The borrowed compound local-field superinstruction must retain the
	// interpreter's checked signed-integer overflow semantics.
	const std::string Source =
			"class FieldBox { public: int x = 2147483647; }\n"
			"void bump(FieldBox value) { value.x += 1; }\n"
			"void main() { FieldBox value = FieldBox(); bump(value); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile local-field overflow suite"))
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
					!Capture.bSuccess && Capture.ErrorMessageId == "diag.integer_overflow",
					"local-field immediate overflow must fail identically"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " error=" << Capture.ErrorMessageId << ' ' << Capture.Errors;
			return EXIT_FAILURE;
		}
	}
}

{
	// Dense field-loop fusion must stop at the same write that the ordinary
	// bytecode path rejects; it must never wrap a later field or skip overflow.
	const std::string Source =
			"class DenseFieldBox { public: int x = 2147483646; int y = 2147483647; }\n"
			"void bump(DenseFieldBox value) {\n"
			"\tfor (int i = 0; i < 2; i++) { value.x += 1; value.y += 1; }\n"
			"}\n"
			"void main() { DenseFieldBox value = DenseFieldBox(); bump(value); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile dense field-loop overflow suite"))
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
					!Capture.bSuccess
						&& Capture.ErrorMessageId == "diag.integer_overflow"
						&& Capture.ErrorLine == 3,
					"dense field-loop overflow must fail identically"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " error=" << Capture.ErrorMessageId
					<< " line=" << Capture.ErrorLine << ' ' << Capture.Errors;
			return EXIT_FAILURE;
		}
	}
}

{
	// Repeated writes to the same field are order-dependent. They must stay on
	// the per-update fallback instead of using the distinct-field closed form.
	const std::string Source =
			"class DenseAliasBox { public: int x = 0; int y = 1; }\n"
			"int update(DenseAliasBox value) {\n"
			"\tfor (int i = 0; i < 3; i++) {\n"
			"\t\tvalue.x += 1; value.x += 2; value.y += 3;\n"
			"\t}\n"
			"\treturn value.x * 100 + value.y;\n"
			"}\n"
			"void main() { DenseAliasBox value = DenseAliasBox(); print(update(value)); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile dense aliased-field suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "910\n",
				"dense aliased-field updates preserve order"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Power-of-two remainder strength reduction must preserve truncation toward
	// zero for negative dividends; a plain unsigned bit mask would be wrong.
	const std::string Source =
			"int sum_negative_remainders() {\n"
			"\tarray<int> values = array<int>();\n"
			"\tfor (int i = -9; i < 1; i++) { values.push(i % 8); }\n"
			"\tint total = 0;\n"
			"\tfor (int i = 0; i < len(values); i++) { total += values[i]; }\n"
			"\treturn total;\n"
			"}\n"
			"void main() { print(sum_negative_remainders()); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile negative remainder loop suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "-29\n",
				"negative power-of-two remainder semantics"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Canonical container loops must not lower INT_MIN % -1 to a native
	// remainder instruction. The language defines it as checked overflow.
	const std::string Source =
			"int fill() {\n"
			"\tarray<int> values = array<int>();\n"
			"\tfor (int i = -2147483648; i < -2147483647; i++) {\n"
			"\t\tvalues.push(i % -1);\n"
			"\t}\n"
			"\treturn len(values);\n"
			"}\n"
			"void main() { print(fill()); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile modulo-overflow loop suite"))
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
					!Capture.bSuccess
						&& Capture.ErrorMessageId == "diag.integer_overflow"
						&& Capture.ErrorLine == 4,
					"INT_MIN modulo -1 loop must report checked overflow"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " error=" << Capture.ErrorMessageId << ' ' << Capture.Errors;
			return EXIT_FAILURE;
		}
	}
}

{
	// Typed string-map probes run through a Linear bridge. A missing key must
	// preserve both the VM diagnostic identity and the failing source line.
	const std::string Source =
			"void main() {\n"
			"\tmap<string,int> values = map<string,int>();\n"
			"\tstring key = \"missing\";\n"
			"\tprint(values[key]);\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile string-map error-location suite"))
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
					!Capture.bSuccess
						&& Capture.ErrorMessageId == "diag.map_key_not_found"
						&& Capture.ErrorLine == 4,
					"string-map fast failure must retain the expression line"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " line=" << Capture.ErrorLine << ' ' << Capture.Errors;
			return EXIT_FAILURE;
		}
	}
}

{
	// int64/uint64 signatures use real full-width scalar values. Supported
	// operations may use NativeCfg; the safe linear fallback must preserve the
	// same width rather than narrowing through the old int32/uint32 paths.
	const std::string Source =
			"int64 neg64(int64 x) { return -x; }\n"
			"int64 not64(int64 x) { return ~x; }\n"
			"int64 shift64(int64 x) { return x << 33; }\n"
			"uint64 ushift64(uint64 x) { return x >> 60; }\n"
			"void main() {\n"
			"\tprint(neg64(4294967296i64));\n"
			"\tprint(not64(0i64));\n"
			"\tprint(shift64(1i64));\n"
			"\tprint(ushift64(18446744073709551615u64));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile boxed 64-bit operator suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	for (const char* Name : {"neg64", "not64", "shift64", "ushift64"})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Name);
		const std::string Message = std::string(Name) + " uses a full-width JIT path";
		if (!Expect(
					Index != static_cast<std::uint16_t>(-1)
						&& Cache.EnsureCompiled(*Module, Index)
							== VeroScript::AsmJit::EJitCompileStatus::Ok
						&& Cache.GetCandidate(Index) != nullptr
						&& (Cache.GetCandidate(Index)->Kind
								== VeroScript::AsmJit::EJitEntryKind::NativeCfg
							|| Cache.GetCandidate(Index)->Kind
								== VeroScript::AsmJit::EJitEntryKind::LinearTrampoline),
					Message.c_str()))
		{
			return EXIT_FAILURE;
		}
	}
	const std::string Expected = "-4294967296\n-1\n8589934592\n15\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "boxed 64-bit operator semantics"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Struct arguments are cloned by the call site before the callee enters its
	// FValue linear frame. Optimizing the callee must not turn struct values into
	// class-like shared aliases.
	const std::string Source =
			"struct ValueBox { int x = 1; };\n"
			"void mutate(ValueBox value) { value.x = value.x + 8; }\n"
			"int read(ValueBox value) { return value.x; }\n"
			"void main() { ValueBox value = ValueBox(); mutate(value); print(value.x); print(read(value)); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile linear struct-value suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	for (const char* Name : {"mutate", "read"})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Name);
		if (!Expect(
					Index != static_cast<std::uint16_t>(-1)
						&& Cache.EnsureCompiled(*Module, Index)
							== VeroScript::AsmJit::EJitCompileStatus::Ok
						&& Cache.GetCandidate(Index) != nullptr
						&& Cache.GetCandidate(Index)->Kind
							== VeroScript::AsmJit::EJitEntryKind::LinearTrampoline,
					"boxed struct callee uses FValue LinearTrampoline"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "1\n1\n", "linear struct-value semantics"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Mixed CFG emits signed Add directly. It must keep the VM's checked int64
	// overflow behavior instead of silently applying unsigned wraparound.
	const std::string Source =
			"int64 overflow64() {\n"
			"\tint64 value = 9223372036854775807i64;\n"
			"\tint64 one = 1i64;\n"
			"\treturn value + one;\n"
			"}\n"
			"void main() { print(overflow64()); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile mixed int64 overflow suite"))
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
					!Capture.bSuccess && Capture.ErrorMessageId == "diag.integer_overflow",
					"mixed int64 Add preserves checked overflow"))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " success=" << Capture.bSuccess
					<< " error-id=" << Capture.ErrorMessageId
					<< " errors=" << Capture.Errors;
			return EXIT_FAILURE;
		}
	}
}

{
	// A block-scoped non-I32 local is uninitialized on the loop-entry edge and
	// concrete on the back-edge. That is not a type conflict: StoreLocal
	// dominates every in-scope read.
	const std::string Source =
			"double nested_f64(int limit) {\n"
			"\tdouble total = 0.0;\n"
			"\tfor (int outer = 0; outer < 3; outer++) {\n"
			"\t\tdouble part = 0.0;\n"
			"\t\tfor (int i = 0; i < limit; i++) { part += 1.0; }\n"
			"\t\ttotal += part;\n"
			"\t}\n"
			"\treturn total;\n"
			"}\n"
			"float nested_f32(int limit) {\n"
			"\tfloat total = 0.f;\n"
			"\tfor (int outer = 0; outer < 3; outer++) {\n"
			"\t\tfloat part = 0.f;\n"
			"\t\tfor (int i = 0; i < limit; i++) { part += 1.f; }\n"
			"\t\ttotal += part;\n"
			"\t}\n"
			"\treturn total;\n"
			"}\n"
			"int64 nested_i64(int limit) {\n"
			"\tint64 total = 0i64;\n"
			"\tfor (int outer = 0; outer < 3; outer++) {\n"
			"\t\tint64 part = 0i64;\n"
			"\t\tfor (int i = 0; i < limit; i++) { part += 1i64; }\n"
			"\t\ttotal += part;\n"
			"\t}\n"
			"\treturn total;\n"
			"}\n"
			"uint64 nested_u64(int limit) {\n"
			"\tuint64 total = 0u64;\n"
			"\tfor (int outer = 0; outer < 3; outer++) {\n"
			"\t\tuint64 part = 0u64;\n"
			"\t\tfor (int i = 0; i < limit; i++) { part += 1u64; }\n"
			"\t\ttotal += part;\n"
			"\t}\n"
			"\treturn total;\n"
			"}\n"
			"double nested_with_now(int limit) {\n"
			"\tdouble stamp = now();\n"
			"\tdouble steady = steady_now();\n"
			"\tdouble total = 0.0;\n"
			"\tfor (int outer = 0; outer < 3; outer++) {\n"
			"\t\tdouble part = 0.0;\n"
			"\t\tfor (int i = 0; i < limit; i++) { part += 1.0; }\n"
			"\t\ttotal += part;\n"
			"\t}\n"
			"\tif (stamp < 0.0) { return 0.0; }\n"
			"\tif (steady < 0.0) { return 0.0; }\n"
			"\treturn total;\n"
			"}\n"
			"void main() {\n"
			"\tprint(to_int(nested_f64(4)));\n"
			"\tprint(to_int(nested_f32(4)));\n"
			"\tprint(to_int(nested_i64(4)));\n"
			"\tprint(to_int(nested_u64(4)));\n"
			"\tprint(to_int(nested_with_now(4)));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile block-local mixed CFG suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "12\n12\n12\n12\n12\n",
					"block-local mixed CFG suite"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	for (const char* Name : {"nested_f64", "nested_f32", "nested_i64",
			"nested_u64", "nested_with_now"})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Name);
		if (!Expect(Index != static_cast<std::uint16_t>(-1)
					&& Cache.EnsureCompiled(*Module, Index)
						== VeroScript::AsmJit::EJitCompileStatus::Ok
					&& Cache.GetCandidate(Index) != nullptr
					&& Cache.GetCandidate(Index)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
					"block-local primitive function retains NativeCfg"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
}

{
	// The dynamically selected raw method leaf must keep the checked integer
	// semantics of both the VM and the ordinary NativeCfg entry.
	const auto Module = CompileSource(
			"interface IFastOverflow { virtual int add(int x) = 0; }\n"
			"class FastOverflow : IFastOverflow { int add(int x) { return (x + 1) % 97; } }\n"
			"int invoke(IFastOverflow value, int x) { return value.add(x); }\n"
			"void main() { FastOverflow value = FastOverflow(); "
			"print(invoke(value, 1)); print(invoke(value, 1)); "
			"print(invoke(value, 2147483647)); }\n");
	if (!Expect(static_cast<bool>(Module), "compile direct scalar overflow suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		const FExecutionCapture Capture = RunCapture(*Module, Backend);
		if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.integer_overflow",
					"direct scalar method overflow parity"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		if (Backend != VeroScript::EExecutionBackend::Vm
				&& !Expect(Capture.JitStats.ScalarMethodFastPathI32 > 0,
					"overflow is reported by the dynamic direct scalar path"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
}

{
	// Exact-width scalar locals and scoped-enum comparisons must remain in the
	// raw mixed CFG tier. In particular, postfix increment must not make a
	// uint64 loop fall back to the boxed linear interpreter.
	const std::string Source =
			"enum class Mode : int { A = 1, B = 2 };\n"
			"enum class WideMode : int64 { A = 1i64, B = 2i64 };\n"
			"uint64 count_u64(uint64 limit) {\n"
			"\tuint64 value = 0u64;\n"
			"\twhile (value < limit) { value++; }\n"
			"\treturn value;\n"
			"}\n"
			"uint64 count_u64_constant() {\n"
			"\tuint64 value = 0u64;\n"
			"\twhile (value < 18u64) { value++; }\n"
			"\treturn value;\n"
			"}\n"
			"int sum_i8(int limit) {\n"
			"\tint8 step = 1; int total = 0;\n"
			"\tfor (int i = 0; i < limit; i++) { total += step; }\n"
			"\treturn total;\n"
			"}\n"
			"uint32 sum_u8(int limit) {\n"
			"\tuint8 step = 1u; uint32 total = 0u;\n"
			"\tfor (int i = 0; i < limit; i++) { total += step; }\n"
			"\treturn total;\n"
			"}\n"
			"int sum_i16(int limit) {\n"
			"\tint16 step = 1; int total = 0;\n"
			"\tfor (int i = 0; i < limit; i++) { total += step; }\n"
			"\treturn total;\n"
			"}\n"
			"uint32 sum_u16(int limit) {\n"
			"\tuint16 step = 1u; uint32 total = 0u;\n"
			"\tfor (int i = 0; i < limit; i++) { total += step; }\n"
			"\treturn total;\n"
			"}\n"
			"int count_mode(int limit) {\n"
			"\tMode value = Mode::A; int total = 0;\n"
			"\tfor (int i = 0; i < limit; i++) { if (value == Mode::A) { total++; } }\n"
			"\treturn total;\n"
			"}\n"
			"int count_wide_mode(int limit) {\n"
			"\tWideMode value = WideMode::A; int total = 0;\n"
			"\tfor (int i = 0; i < limit; i++) { if (value != WideMode::B) { total++; } }\n"
			"\treturn total;\n"
			"}\n"
			"void main() {\n"
			"\tprint(count_u64(17u64));\n"
			"\tprint(count_u64_constant());\n"
			"\tprint(sum_i8(11)); print(sum_u8(12));\n"
			"\tprint(sum_i16(13)); print(sum_u16(14));\n"
			"\tprint(count_mode(15)); print(count_wide_mode(16));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile exact-width scalar CFG suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "17\n18\n11\n12\n13\n14\n15\n16\n",
					"exact-width scalar CFG suite"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	for (const char* Name : {"count_u64", "count_u64_constant", "sum_i8",
			"sum_u8", "sum_i16", "sum_u16", "count_mode", "count_wide_mode"})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Name);
		if (!Expect(Index != static_cast<std::uint16_t>(-1)
					&& Cache.EnsureCompiled(*Module, Index)
						== VeroScript::AsmJit::EJitCompileStatus::Ok
					&& Cache.GetCandidate(Index) != nullptr
					&& Cache.GetCandidate(Index)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
					"exact-width scalar function retains NativeCfg"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
}

{
	// Narrow integers are first-class scalar ABI values, even though their
	// arithmetic is promoted to 32 bits. A narrow parameter/return must not
	// turn an otherwise scalar call chain into boxed VM dispatch.
	const std::string Source =
			"int8 identity_i8(int8 value) { return value; }\n"
			"uint8 identity_u8(uint8 value) { return value; }\n"
			"int16 identity_i16(int16 value) { return value; }\n"
			"uint16 identity_u16(uint16 value) { return value; }\n"
			"int bump_i8(int8 value) { return value + 1; }\n"
			"int bump_i16(int16 value) { return value + 1; }\n"
			"void main() {\n"
			"\tint8 a = -7; uint8 b = 8u; int16 c = -900; uint16 d = 901u;\n"
			"\tprint(identity_i8(a)); print(identity_u8(b));\n"
			"\tprint(identity_i16(c)); print(identity_u16(d));\n"
			"\tprint(bump_i8(a)); print(bump_i16(c));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile narrow scalar ABI suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "-7\n8\n-900\n901\n-6\n-899\n",
					"narrow scalar ABI parity"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	struct FExpectedNarrow
	{
		const char* Name;
		VeroScript::AsmJit::EJitScalarValueKind ParameterKind;
		VeroScript::AsmJit::EJitScalarValueKind ReturnKind;
	};
	for (const FExpectedNarrow Expected : {
			 FExpectedNarrow{"identity_i8", VeroScript::AsmJit::EJitScalarValueKind::I8,
				VeroScript::AsmJit::EJitScalarValueKind::I8},
			FExpectedNarrow{"identity_u8", VeroScript::AsmJit::EJitScalarValueKind::U8,
				VeroScript::AsmJit::EJitScalarValueKind::U8},
			FExpectedNarrow{"identity_i16", VeroScript::AsmJit::EJitScalarValueKind::I16,
				VeroScript::AsmJit::EJitScalarValueKind::I16},
			FExpectedNarrow{"identity_u16", VeroScript::AsmJit::EJitScalarValueKind::U16,
				VeroScript::AsmJit::EJitScalarValueKind::U16},
			FExpectedNarrow{"bump_i8", VeroScript::AsmJit::EJitScalarValueKind::I8,
				VeroScript::AsmJit::EJitScalarValueKind::I32},
			FExpectedNarrow{"bump_i16", VeroScript::AsmJit::EJitScalarValueKind::I16,
				VeroScript::AsmJit::EJitScalarValueKind::I32}})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Expected.Name);
		const VeroScript::AsmJit::EJitCompileStatus Status =
				Index != static_cast<std::uint16_t>(-1)
				? Cache.EnsureCompiled(*Module, Index)
				: VeroScript::AsmJit::EJitCompileStatus::Unsupported;
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry =
				Index != static_cast<std::uint16_t>(-1) ? Cache.GetCandidate(Index) : nullptr;
		if (!Expect(Index != static_cast<std::uint16_t>(-1)
					&& Status == VeroScript::AsmJit::EJitCompileStatus::Ok
					&& Entry != nullptr
					&& Entry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
					&& Entry->ScalarAbi == VeroScript::AsmJit::EJitScalarAbi::Mixed
					&& Entry->ScalarParameterCount == 1
					&& static_cast<VeroScript::AsmJit::EJitScalarValueKind>(
						Entry->ScalarParameterKinds & 0xFu) == Expected.ParameterKind
					&& Entry->ScalarReturnKind == Expected.ReturnKind,
					(std::string("narrow scalar ABI preserves exact signature metadata: ")
						+ Expected.Name).c_str()))
		{
			if (Entry)
			{
				std::cerr << "  actual kind=" << static_cast<int>(Entry->Kind)
						<< " abi=" << static_cast<int>(Entry->ScalarAbi)
						<< " params=" << static_cast<int>(Entry->ScalarParameterCount)
						<< " paramKind=" << (Entry->ScalarParameterKinds & 0xFu)
						<< " returnKind=" << static_cast<int>(Entry->ScalarReturnKind) << '\n';
			}
			if (Index < Module->Functions.size())
			{
				const auto& FailedFunction = Module->Functions[Index];
				std::cerr << "  closure=" << FailedFunction.ClosureTypeName << " code=";
				for (const std::uint8_t Byte : FailedFunction.Code)
				{
					std::cerr << static_cast<unsigned>(Byte) << ',';
				}
				std::cerr << '\n';
			}
			return EXIT_FAILURE;
		}
	}
	#endif
}

{
	// Narrow globals and fields use exact FValue storage while NativeCfg keeps
	// promoted arithmetic in raw 32-bit slots.
	const std::string Source =
			"int8 GlobalI8 = -3; uint8 GlobalU8 = 4u;\n"
			"int16 GlobalI16 = -500; uint16 GlobalU16 = 501u;\n"
			"class NarrowFields {\n"
			"public:\n"
			"\tint8 i8 = -7; uint8 u8 = 8u;\n"
			"\tint16 i16 = -900; uint16 u16 = 901u;\n"
			"\tvoid bump() { i8 = i8 + 1; u8 = u8 + 1u;"
				" i16 = i16 + 1; u16 = u16 + 1u; }\n"
			"}\n"
			"void main() {\n"
			"\tGlobalI8 = GlobalI8 + 1; GlobalU8 = GlobalU8 + 1u;\n"
			"\tGlobalI16 = GlobalI16 + 1; GlobalU16 = GlobalU16 + 1u;\n"
			"\tNarrowFields value = NarrowFields(); value.bump();\n"
			"\tprint(GlobalI8); print(GlobalU8); print(GlobalI16); print(GlobalU16);\n"
			"\tprint(value.i8); print(value.u8); print(value.i16); print(value.u16);\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile narrow scalar storage suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend,
					"-2\n5\n-499\n502\n-6\n9\n-899\n902\n",
					"narrow scalar storage parity"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Checked narrow conversions stay in NativeCfg without changing truncation
	// or overflow behavior.
	if (!ExpectDifferentialRun(
				"void main() {\n"
				"\tint source = 127; int8 a = source; print(a);\n"
				"\tuint32 usource = 255u; uint8 b = usource; print(b);\n"
				"\tdouble wide = -32768.75; int16 c = wide; print(c);\n"
				"\tfloat uwide = 65535.9f; uint16 d = uwide; print(d);\n"
				"}\n",
				"narrow conversion success parity"))
	{
		return EXIT_FAILURE;
	}
	for (const char* Source : {
			 "void main() { int source = 128; int8 value = source; print(value); }\n",
			"void main() { int source = -1; uint8 value = source; print(value); }\n",
			"void main() { int source = 32768; int16 value = source; print(value); }\n",
			"void main() { int source = -1; uint16 value = source; print(value); }\n"})
	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile narrow conversion overflow parity"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
					VeroScript::EExecutionBackend::Mixed,
					VeroScript::EExecutionBackend::Jit})
		{
			if (!Expect(!RunCapture(*Module, Backend).bSuccess,
						"narrow conversion overflow parity"))
			{
				return EXIT_FAILURE;
			}
		}
	}
}

{
	// Narrow signed abs preserves its declared width. The minimum value cannot
	// be represented after negation and must fail identically even though the
	// mixed tier normally stores narrow locals in sign-extended I32 slots.
	for (const char* Source : {
			 "void main() { int8 value = -128; print(abs(value)); }\n",
			"void main() { int16 value = -32768; print(abs(value)); }\n",
			"int8 exact_abs(int8 value) { return abs(value); } "
				"void main() { int8 value = -128; print(exact_abs(value)); }\n",
			"int16 exact_abs(int16 value) { return abs(value); } "
				"void main() { int16 value = -32768; print(exact_abs(value)); }\n"})
	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile narrow abs overflow suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
					VeroScript::EExecutionBackend::Mixed,
					VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(!Capture.bSuccess
						&& Capture.ErrorMessageId == "diag.integer_overflow",
						"narrow abs minimum preserves overflow"))
			{
				return EXIT_FAILURE;
			}
		}
	}
}

{
	// The dispatch cache key includes arity. Two interface overloads share the
	// same leaf-name string and runtime type, but must resolve independently.
	const std::string Source =
			"interface I {\n"
			"\tvirtual int calc(int x) = 0;\n"
			"\tvirtual int calc(int x, int y) = 0;\n"
			"}\n"
			"class C : I {\n"
			"\tint calc(int x) { return x + 10; }\n"
			"\tint calc(int x, int y) { return x + y + 20; }\n"
			"}\n"
			"int take(I x) { return x.calc(1) * 100 + x.calc(2, 3); }\n"
			"void main() { C c = C(); print(take(c)); print(take(c)); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile overloaded CallMethod cache suite"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "1125\n1125\n",
					"overloaded CallMethod cache preserves arity"))
		{
			return EXIT_FAILURE;
		}
	}
	const FExecutionCapture JitCapture = RunCapture(
			*Module, VeroScript::EExecutionBackend::Jit);
	if (!Expect(JitCapture.bSuccess && JitCapture.Output == "1125\n1125\n",
				"overloaded CallMethod JIT output"))
	{
		return EXIT_FAILURE;
	}
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	if (!Expect(JitCapture.JitStats.ScalarMethodFastPathI32 >= 4,
				"overloaded CallMethod uses boxed-to-raw scalar bridge"))
	{
		return EXIT_FAILURE;
	}
	#endif
}

{
	// Boxed interface dispatch must preserve script-visible scalar kinds while
	// entering the raw I32/U32/F64/Mixed ABI, including bool and void results.
	const std::string Source =
			"int64 free_step(int64 x) { int64 y = x + 1i64; return y; }\n"
			"int64 call_free(int64 x) { return free_step(x); }\n"
			"interface IScalar {\n"
			"\tvirtual bool invert(bool x) = 0;\n"
			"\tvirtual uint32 add(uint32 x) = 0;\n"
			"\tvirtual double scale(double x) = 0;\n"
			"\tvirtual int64 add64(int64 x) = 0;\n"
			"\tvirtual uint64 add_u64(uint64 x) = 0;\n"
			"\tvirtual float scale_float(float x) = 0;\n"
			"\tvirtual int64 combine_i64(int64 a, int64 b) = 0;\n"
			"\tvirtual uint64 combine_u64(uint64 a, uint64 b) = 0;\n"
			"\tvirtual float combine_float(float a, float b) = 0;\n"
			"\tvirtual bool less_float(float a, float b) = 0;\n"
			"\tvirtual double mix(double x, int64 y) = 0;\n"
			"\tvirtual bool positive(double x) = 0;\n"
			"\tvirtual bool positive_float(float x) = 0;\n"
			"\tvirtual void set_double(double x) = 0;\n"
			"\tvirtual double get_double() = 0;\n"
			"\tvirtual void set_i64(int64 x) = 0;\n"
			"\tvirtual int64 get_i64() = 0;\n"
			"\tvirtual void set(int x) = 0;\n"
			"\tvirtual int get() = 0;\n"
			"}\n"
			"class ScalarImpl : IScalar {\n"
			"\tint value = 0;\n"
			"\tdouble double_value = 0.0;\n"
			"\tint64 int64_value = 0i64;\n"
			"\tbool invert(bool x) { return !x; }\n"
			"\tuint32 add(uint32 x) { return x + 2u; }\n"
			"\tdouble scale(double x) { return x * 2.0; }\n"
			"\tint64 add64(int64 x) { return x + 2i64; }\n"
			"\tuint64 add_u64(uint64 x) { return x + 3u64; }\n"
			"\tfloat scale_float(float x) { return x * 2.f; }\n"
			"\tint64 combine_i64(int64 a, int64 b) { return a + b; }\n"
			"\tuint64 combine_u64(uint64 a, uint64 b) { return a + b; }\n"
			"\tfloat combine_float(float a, float b) { return a + b; }\n"
			"\tbool less_float(float a, float b) { return a < b; }\n"
			"\tdouble mix(double x, int64 y) { return x + to_double(y); }\n"
			"\tbool positive(double x) { return x > 0.0; }\n"
			"\tbool positive_float(float x) { return x > 0.f; }\n"
			"\tvoid set_double(double x) { double_value = x; }\n"
			"\tdouble get_double() { return double_value; }\n"
			"\tvoid set_i64(int64 x) { int64_value = x; }\n"
			"\tint64 get_i64() { return int64_value; }\n"
			"\tvoid set(int x) { value = x; }\n"
			"\tint get() { return value; }\n"
			"}\n"
			"void exercise(IScalar x) {\n"
			"\tprint(x.invert(true));\n"
			"\tprint(x.add(5u));\n"
			"\tprint(x.scale(1.5) == 3.0);\n"
			"\tprint(x.add64(5i64) == 7i64);\n"
			"\tprint(x.add_u64(5u64) == 8u64);\n"
			"\tprint(x.scale_float(1.5f) == 3.f);\n"
			"\tprint(x.combine_i64(4i64, 5i64) == 9i64);\n"
			"\tprint(x.combine_u64(6u64, 7u64) == 13u64);\n"
			"\tprint(x.combine_float(1.25f, 2.5f) == 3.75f);\n"
			"\tprint(x.less_float(1.f, 2.f));\n"
			"\tprint(x.mix(1.5, 2i64) == 3.5);\n"
			"\tprint(x.positive(0.25));\n"
			"\tprint(x.positive_float(0.25f));\n"
			"\tx.set_double(2.5);\n"
			"\tprint(x.get_double() == 2.5);\n"
			"\tx.set_i64(-9i64);\n"
			"\tprint(x.get_i64() == -9i64);\n"
			"\tprint(call_free(8i64) == 9i64);\n"
			"\tx.set(9);\n"
			"\tprint(x.get());\n"
			"}\n"
			"void main() { ScalarImpl x = ScalarImpl(); exercise(x); exercise(x); }\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile boxed scalar interface suite"))
	{
		return EXIT_FAILURE;
	}
	const std::string Expected =
			"false\n7\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n9\n"
			"false\n7\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n9\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "boxed scalar interface suite"))
		{
			return EXIT_FAILURE;
		}
	}
	const FExecutionCapture JitCapture = RunCapture(
			*Module, VeroScript::EExecutionBackend::Jit);
	if (!Expect(JitCapture.bSuccess && JitCapture.Output == Expected,
				"boxed scalar interface JIT output"))
	{
		return EXIT_FAILURE;
	}
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	if (!Expect(
				JitCapture.JitStats.ScalarMethodFastPathI32 >= 3
					&& JitCapture.JitStats.ScalarMethodFastPathU32 >= 1
					&& JitCapture.JitStats.ScalarMethodFastPathF64 >= 3
	#if VEROSCRIPT_ASMJIT_X64
					&& JitCapture.JitStats.ScalarMethodFastPathMixed >= 7
					&& (JitCapture.JitStats.ScalarFunctionFastPathMixed >= 1
						|| JitCapture.JitStats.InlinedCallSites >= 1)
	#endif
					,
				"boxed scalar interface calls use supported raw ABIs"))
	{
		std::cerr << "scalar fast stats i32="
				<< JitCapture.JitStats.ScalarMethodFastPathI32
				<< " u32=" << JitCapture.JitStats.ScalarMethodFastPathU32
				<< " f64=" << JitCapture.JitStats.ScalarMethodFastPathF64
				<< " mixed-method=" << JitCapture.JitStats.ScalarMethodFastPathMixed
				<< " mixed-function=" << JitCapture.JitStats.ScalarFunctionFastPathMixed
				<< '\n';
		return EXIT_FAILURE;
	}
	#endif
}

{
	// Container create + index via direct JIT helpers under Mixed/Jit.
	const std::string Source =
			"int set_demo() {\n"
			"\tset<int> s = set<int>();\n"
			"\ts.add(1);\n"
			"\ts.add(2);\n"
			"\treturn s.has(1) ? 1 : 0;\n"
			"}\n"
			"int deque_demo() {\n"
			"\tdeque<int> d = deque<int>();\n"
			"\td.push_back(10);\n"
			"\td.push_back(20);\n"
			"\treturn d[0] + d[1];\n"
			"}\n"
			"int omap_demo() {\n"
			"\tordered_map<string, int> m = ordered_map<string, int>();\n"
			"\tm[\"x\"] = 4;\n"
			"\tm[\"y\"] = 5;\n"
			"\treturn m[\"x\"] + m[\"y\"];\n"
			"}\n"
			"void main() {\n"
			"\tprint(set_demo());\n"
			"\tprint(deque_demo());\n"
			"\tprint(omap_demo());\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile container suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "set_demo", VeroScript::AsmJit::EJitCompileStatus::Ok, "set_demo Ok")
			|| !ExpectStatus(Cache, *Module, "deque_demo", VeroScript::AsmJit::EJitCompileStatus::Ok, "deque_demo Ok")
			|| !ExpectStatus(Cache, *Module, "omap_demo", VeroScript::AsmJit::EJitCompileStatus::Ok, "omap_demo Ok"))
	{
		return EXIT_FAILURE;
	}
	const std::string Expected = "1\n30\n9\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "container suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// ParamFlags mark refs; native leaves stay off ref functions; deque SetIndex is direct.
	const std::string Source =
			"void bump(ref int x) { x = x + 1; }\n"
			"int leaf_method_host() {\n"
			"\treturn 9;\n"
			"}\n"
			"class Holder {\n"
			"public:\n"
			"\tint ten() { return 10; }\n"
			"}\n"
			"int deque_set() {\n"
			"\tdeque<int> d = deque<int>();\n"
			"\td.push_back(1);\n"
			"\td.push_back(2);\n"
			"\td[1] = 40;\n"
			"\treturn d[0] + d[1];\n"
			"}\n"
			"void main() {\n"
			"\tint n = 3;\n"
			"\tbump(ref n);\n"
			"\tprint(n);\n"
			"\tprint(leaf_method_host());\n"
			"\tHolder h = Holder();\n"
			"\tprint(h.ten());\n"
			"\tprint(deque_set());\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile ref-flags/deque-set suite"))
	{
		return EXIT_FAILURE;
	}
	const std::uint16_t Bump = FindFunctionIndex(*Module, "bump");
	if (!Expect(Bump != static_cast<std::uint16_t>(-1), "find bump")
			|| !Expect(
				VeroScript::BytecodeFunctionHasRefParams(Module->Functions[Bump]),
				"bump has ref ParamFlags"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "bump", VeroScript::AsmJit::EJitCompileStatus::Ok, "bump Ok")
			|| !ExpectStatus(Cache, *Module, "deque_set", VeroScript::AsmJit::EJitCompileStatus::Ok, "deque_set Ok")
			|| !ExpectStatus(Cache, *Module, "ten", VeroScript::AsmJit::EJitCompileStatus::Ok, "Holder.ten Ok")
			|| !ExpectStatus(
				Cache,
					*Module,
				"leaf_method_host",
				VeroScript::AsmJit::EJitCompileStatus::Ok,
				"leaf_method_host Ok"))
	{
		return EXIT_FAILURE;
	}
	{
		const VeroScript::AsmJit::FJitFunctionEntry* const BumpEntry = Cache.GetCandidate(Bump);
		if (!Expect(
					BumpEntry != nullptr
						&& BumpEntry->Kind == VeroScript::AsmJit::EJitEntryKind::VmTrampoline,
					"ref bump uses ownership-safe VM trampoline"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	{
		const VeroScript::AsmJit::FJitFunctionEntry* const BumpEntry = Cache.GetCandidate(Bump);
		if (!Expect(
					BumpEntry != nullptr && !BumpEntry->bNativeLeaf,
					"ref bump must not be native leaf"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t Leaf = FindFunctionIndex(*Module, "leaf_method_host");
		const VeroScript::AsmJit::FJitFunctionEntry* const LeafEntry = Cache.GetCandidate(Leaf);
		if (!Expect(
					LeafEntry != nullptr && LeafEntry->bNativeLeaf && LeafEntry->NativeCode != nullptr,
					"scalar leaf_method_host native"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t Ten = FindFunctionIndex(*Module, "ten");
		const VeroScript::AsmJit::FJitFunctionEntry* const TenEntry = Cache.GetCandidate(Ten);
		if (!Expect(
					TenEntry != nullptr && TenEntry->bNativeLeaf && TenEntry->NativeCode != nullptr,
					"Holder.ten native leaf"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
	const std::string Expected = "4\n9\n10\n41\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "ref-flags/deque-set suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Void Call+Pop must leave a native/linear sentinel (no opstack underflow).
	const std::string Source =
			"void ping() {}\n"
			"void bump_local(int x) { ping(); }\n"
			"int after_void(int x) {\n"
			"\tping();\n"
			"\treturn x + 1;\n"
			"}\n"
			"void dping() {}\n"
			"double dafter_void(double x) {\n"
			"\tdping();\n"
			"\treturn x + 1.0;\n"
			"}\n"
			"void main() {\n"
			"\tbump_local(1);\n"
			"\tprint(after_void(40));\n"
			"\tprint(to_int(dafter_void(2.0)));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile void-call suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "ping", VeroScript::AsmJit::EJitCompileStatus::Ok, "ping Ok")
			|| !ExpectStatus(Cache, *Module, "bump_local", VeroScript::AsmJit::EJitCompileStatus::Ok, "bump_local Ok")
			|| !ExpectStatus(Cache, *Module, "after_void", VeroScript::AsmJit::EJitCompileStatus::Ok, "after_void Ok"))
	{
		return EXIT_FAILURE;
	}
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	for (const char* Name : {"bump_local", "after_void"})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Name);
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
		const std::string Msg = std::string("void-call native ") + Name;
		if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Msg.c_str()))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
	const std::string Expected = "41\n3\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "void-call suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Double this-fields + IncField fusion under F64 native CFG.
	const std::string Source =
			"class Meter {\n"
			"public:\n"
			"\tdouble v = 0.0;\n"
			"\tvoid inc() { v = v + 1.0; }\n"
			"\tvoid tick() { v++; }\n"
			"\tvoid add(double x) { v = v + x; }\n"
			"\tdouble get() { return v; }\n"
			"\tdouble clamp_pos(double x) { return !(x < 0.0) ? x : 0.0; }\n"
			"}\n"
			"void main() {\n"
			"\tauto m = Meter();\n"
			"\tm.inc();\n"
			"\tm.tick();\n"
			"\tm.add(3.0);\n"
			"\tprint(to_int(m.get()));\n"
			"\tprint(to_int(m.clamp_pos(-2.0)));\n"
			"\tprint(to_int(m.clamp_pos(4.0)));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile f64 this-field suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "inc", VeroScript::AsmJit::EJitCompileStatus::Ok, "Meter.inc Ok")
			|| !ExpectStatus(Cache, *Module, "tick", VeroScript::AsmJit::EJitCompileStatus::Ok, "Meter.tick Ok")
			|| !ExpectStatus(Cache, *Module, "add", VeroScript::AsmJit::EJitCompileStatus::Ok, "Meter.add Ok")
			|| !ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "Meter.get Ok")
			|| !ExpectStatus(Cache, *Module, "clamp_pos", VeroScript::AsmJit::EJitCompileStatus::Ok, "Meter.clamp_pos Ok"))
	{
		return EXIT_FAILURE;
	}
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	for (const char* Name : {"inc", "tick", "add", "get", "clamp_pos"})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Name);
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
		const std::string Msg = std::string("f64 native leaf ") + Name;
		#if VEROSCRIPT_ASMJIT_A64
		// clamp_pos keeps a bool across Not before its branch, which needs the
		// mixed stack representation rather than the homogeneous F64 scratch stack.
		if (std::string_view(Name) == "clamp_pos")
		{
			if (!Expect(Entry && !Entry->bNativeLeaf, "F64 predicate chains retain an owned fallback on A64"))
			{
				return EXIT_FAILURE;
			}

			continue;
		}
		#endif
		if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Msg.c_str()))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
	const std::string Expected = "5\n0\n4\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "f64 this-field suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Inherited scalar fields resolve for native MethodField* matchers.
	const std::string Source =
			"class Base {\n"
			"public:\n"
			"\tint n = 0;\n"
			"}\n"
			"class Derived : public Base {\n"
			"public:\n"
			"\tvoid bump() { n = n + 1; }\n"
			"\tint get() { return n; }\n"
			"}\n"
			"void main() {\n"
			"\tDerived d = Derived();\n"
			"\td.bump();\n"
			"\td.bump();\n"
			"\tprint(d.get());\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile inherited this-field suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "bump", VeroScript::AsmJit::EJitCompileStatus::Ok, "Derived.bump Ok")
			|| !ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "Derived.get Ok"))
	{
		return EXIT_FAILURE;
	}
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	for (const char* Name : {"bump", "get"})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Name);
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
		const std::string Msg = std::string("inherited native leaf ") + Name;
		if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Msg.c_str()))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
	const std::string Expected = "2\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "inherited this-field suite"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Mixed scalar this-fields (int + double) compile to Mixed native CFG leaves.
	const std::string Source =
			"class Pair {\n"
			"public:\n"
			"\tint a = 0;\n"
			"\tdouble b = 0.0;\n"
			"\tvoid bump() { a = a + 1; b = b + 1.0; }\n"
			"\tvoid tick() { a++; }\n"
			"\tint getA() { return a; }\n"
			"\tdouble getB() { return b; }\n"
			"\tdouble sum() { return to_double(a) + b; }\n"
			"}\n"
			"void main() {\n"
			"\tauto p = Pair();\n"
			"\tp.bump();\n"
			"\tp.tick();\n"
			"\tprint(p.getA());\n"
			"\tprint(to_int(p.getB()));\n"
			"\tprint(to_int(p.sum()));\n"
			"}\n";
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile mixed this-field suite"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	if (!ExpectStatus(Cache, *Module, "bump", VeroScript::AsmJit::EJitCompileStatus::Ok, "Pair.bump Ok")
			|| !ExpectStatus(Cache, *Module, "tick", VeroScript::AsmJit::EJitCompileStatus::Ok, "Pair.tick Ok")
			|| !ExpectStatus(Cache, *Module, "getA", VeroScript::AsmJit::EJitCompileStatus::Ok, "Pair.getA Ok")
			|| !ExpectStatus(Cache, *Module, "getB", VeroScript::AsmJit::EJitCompileStatus::Ok, "Pair.getB Ok")
			|| !ExpectStatus(Cache, *Module, "sum", VeroScript::AsmJit::EJitCompileStatus::Ok, "Pair.sum Ok"))
	{
		return EXIT_FAILURE;
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	for (const char* Name : {"bump", "tick", "sum"})
	{
		const std::uint16_t Index = FindFunctionIndex(*Module, Name);
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
		const std::string Msg = std::string("mixed native leaf ") + Name;
		if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Msg.c_str()))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
	const std::string Expected = "2\n1\n3\n";
	for (const VeroScript::EExecutionBackend Backend :
		{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected, "mixed this-field suite"))
		{
			return EXIT_FAILURE;
		}
	}
}
