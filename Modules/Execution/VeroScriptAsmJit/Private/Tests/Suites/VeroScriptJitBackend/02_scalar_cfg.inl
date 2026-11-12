// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptJitBackendTests / scalar cfg suite. Included by the owning translation unit.

	#include "02_mixed_f64_local_cache.inl"
	#include "02_typed_f64_stack.inl"

	{
		// The compact native string tables back JIT-proven Map/Set storage. Exercise
		// long collision/tombstone/rebuild sequences against the standard containers;
		// script-level tests alone rarely visit every probe-chain transition.
		VeroScript::TScalarNativeHashMap<std::string, std::int32_t> Values;
		VeroScript::TScalarNativeHashSet<std::string> Keys;
		std::unordered_map<std::string, std::int32_t> ReferenceValues;
		std::unordered_set<std::string> ReferenceKeys;
		Values.reserve(8);
		Keys.reserve(8);
		std::uint32_t State = 0x13579BDFu;
		for (std::uint32_t Step = 0; Step < 12000u; ++Step)
		{
			State = State * 1664525u + 1013904223u;
			const std::string Key = "key-" + std::to_string((State >> 7u) % 389u);
			const std::uint32_t Operation = State % 4u;
			if (Operation <= 1u)
			{
				const std::int32_t Value = static_cast<std::int32_t>(State ^ Step);
				const std::size_t Hash = std::hash<std::string>{}(Key);
				Values.InsertOrAssignHashed(Key, Value, Hash);
				ReferenceValues[Key] = Value;
				const bool Inserted = Keys.Insert(Key);
				const bool ReferenceInserted = ReferenceKeys.insert(Key).second;
				if (!Expect(Inserted == ReferenceInserted,
					"native string set insertion parity"))
				{
					return EXIT_FAILURE;
				}
			}
			else if (Operation == 2u)
			{
				if (!Expect(Values.Erase(Key) == (ReferenceValues.erase(Key) != 0u),
						"native string map erase parity")
					|| !Expect(Keys.Erase(Key) == (ReferenceKeys.erase(Key) != 0u),
						"native string set erase parity"))
				{
					return EXIT_FAILURE;
				}
			}
			else
			{
				const std::size_t Hash = std::hash<std::string>{}(Key);
				const std::int32_t* const Found = Values.FindHashed(Key, Hash);
				const auto Reference = ReferenceValues.find(Key);
				if (!Expect((Found != nullptr) == (Reference != ReferenceValues.end())
						&& (!Found || *Found == Reference->second),
						"native string map lookup parity")
					|| !Expect(Keys.ContainsHashed(Key, Hash)
						== (ReferenceKeys.find(Key) != ReferenceKeys.end()),
						"native string set lookup parity"))
				{
					return EXIT_FAILURE;
				}
			}
		}
		std::size_t VisitedValues = 0u;
		Values.ForEach([&](const std::string& Key, const std::int32_t Value)
		{
			const auto It = ReferenceValues.find(Key);
			if (It != ReferenceValues.end() && It->second == Value)
			{
				++VisitedValues;
			}
		});
		std::size_t VisitedKeys = 0u;
		Keys.ForEach([&](const std::string& Key)
		{
			if (ReferenceKeys.find(Key) != ReferenceKeys.end())
			{
				++VisitedKeys;
			}
		});
		if (!Expect(Values.size() == ReferenceValues.size()
				&& VisitedValues == ReferenceValues.size(),
				"native string map traversal parity")
			|| !Expect(Keys.size() == ReferenceKeys.size()
				&& VisitedKeys == ReferenceKeys.size(),
				"native string set traversal parity"))
		{
			return EXIT_FAILURE;
		}
		const auto ValuesCopy = Values;
		const auto KeysCopy = Keys;
		for (const auto& Pair : ReferenceValues)
		{
			const std::int32_t* const Found = ValuesCopy.Find(Pair.first);
			if (!Expect(Found && *Found == Pair.second,
				"native string map copy parity"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const std::string& Key : ReferenceKeys)
		{
			if (!Expect(KeysCopy.Contains(Key), "native string set copy parity"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Resolved homogeneous free calls either enter the callee NativeCfg directly
		// or are safely expanded into the caller. The bridge path still consults the
		// current module cache, while inlining remains generation-scoped.
		const auto Module = CompileSource(
			"int inc_i(int x) { int y = x; y = y + 1; return y; }\n"
			"uint32 inc_u(uint32 x) { uint32 y = x; y = y + 1u; return y; }\n"
			"double inc_d(double x) { double y = x; y = y + 1.0; return y; }\n"
			"int call_i() { return inc_i(4); }\n"
			"uint32 call_u() { return inc_u(5u); }\n"
			"double call_d() { return inc_d(6.0); }\n"
			"void main() { print(call_i()); print(call_u()); print(call_d()); }\n");
		if (!Expect(static_cast<bool>(Module), "compile scalar free-call specialization"))
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
				"execute scalar free-call specialization")
			|| !Expect(Output.str() == "5\n6\n7\n",
				"scalar free-call specialization output"))
		{
			std::cerr << Errors.str() << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		const VeroScript::FJitStats Stats = Vm.GetJitStats();
		const bool bUsedAllDirectBridges = Stats.ScalarFunctionFastPathI32 > 0
			&& Stats.ScalarFunctionFastPathU32 > 0
			&& Stats.ScalarFunctionFastPathF64 > 0;
		if (!Expect(bUsedAllDirectBridges || Stats.InlinedCallSites >= 3,
				"scalar free calls use direct NativeCfg bridges or generation-scoped inlining"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Loop-bearing scalar callees are intentionally not inlined. Once their
		// generation-scoped NativeCfg target is published, both integer and double
		// callers must enter it without rebuilding FValue arguments or consulting
		// the VM call cache on every invocation.
		const auto Module = CompileSource(
			"int direct_loop_i(int x) { for (int i = 0; i < 4; i++) { x = (x * 3 + 1) % 10007; } return x; }\n"
			"double direct_loop_d(double x) { for (double i = 0.0; i < 4.0; i = i + 1.0) { x = x * 1.25 + 0.5; } return x; }\n"
			"int direct_caller_i(int x, int count) { int out = 0; for (int i = 0; i < count; i++) { out += direct_loop_i(x + i); } return out; }\n"
			"double direct_caller_d(double x, double count) { double out = 0.0; for (double i = 0.0; i < count; i = i + 1.0) { out = out + direct_loop_d(x + i); } return out; }\n"
			"int direct_count_i = 3; double direct_count_d = 2.0;\n"
			"void main() { print(direct_caller_i(2, direct_count_i)); print(to_int(direct_caller_d(2.0, direct_count_d))); }\n");
		if (!Expect(static_cast<bool>(Module), "compile direct scalar NativeCfg calls"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "849\n17\n", "direct scalar NativeCfg parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"execute direct scalar NativeCfg statistics")
			|| !Expect(Vm.GetJitStats().DirectNativeCalls >= 5,
				"integer and double calls both use generation-scoped direct targets"))
		{
			std::cerr << Errors.str() << Vm.GetError().Message
				<< " direct_native_calls=" << Vm.GetJitStats().DirectNativeCalls << '\n';
			return EXIT_FAILURE;
		}
		#else
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"execute scalar call-loop fallback statistics")
			|| !Expect(Vm.GetJitStats().CostModelVmFallbacks >= 2,
				"non-native scalar call loops use the lower-cost VM executor"))
		{
			std::cerr << Errors.str() << Vm.GetError().Message
				<< " cost_model_vm_fallbacks="
				<< Vm.GetJitStats().CostModelVmFallbacks << '\n';
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// A scalar loop rejected by the portable cost model must keep its nested
		// method calls in the VM as well, including methods on a local receiver.
		const auto Module = CompileSource(
			"class FallbackCounter { public: int value = 0; "
				"void add(int delta) { value += delta; } "
				"void bump() { this.add(1); } "
				"int run(int limit) { for (int i = 0; i < limit; i++) { this.bump(); } "
				"return value; } }\n"
			"int local_run(int limit) { FallbackCounter counter = FallbackCounter(); "
				"for (int i = 0; i < limit; i++) { counter.bump(); } "
				"return counter.value; }\n"
			"void main() { FallbackCounter counter = FallbackCounter(); "
				"print(counter.run(2000)); print(local_run(2000)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile nested scalar loop fallback"))
		{
			return EXIT_FAILURE;
		}
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
			Vm.SetExecutionBackend(Backend);
			if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
					"execute nested scalar loop fallback")
				|| !Expect(Output.str() == "2000\n2000\n",
					"nested scalar loop fallback preserves field updates"))
			{
				std::cerr << Vm.GetError().Message << '\n';
				return EXIT_FAILURE;
			}
			#if !(VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64))
			if (Backend != VeroScript::EExecutionBackend::Vm)
			{
				const auto Stats = Vm.GetJitStats();
				if (!Expect(Stats.CostModelVmFallbacks >= 2,
						"portable scalar loops select the VM executor")
					|| !Expect(Stats.Dispatches < 100u,
						"portable scalar fallback avoids per-iteration nested JIT dispatch"))
				{
					std::cerr << " dispatches=" << Stats.Dispatches << '\n';
					return EXIT_FAILURE;
				}
			}
			#endif
		}
	}

	{
		// Scalar-loop VM fallback must preserve profitable owned callees through
		// ordinary, bound-method, interface and indirect wrappers independently.
		const std::string Source =
			"int nested_bias = 0;\n"
			"int owned_work() { array<int> values = array<int>(); values.reserve(1024); "
				"int i = 0; while (i < 1024) { values.push(i); i++; } "
				"int total = 0; i = 0; "
				"while (i < 1024) { total += values[i]; i++; } return total; }\n"
			"int owned_bridge() { return owned_work() + nested_bias; }\n"
			"int scalar_driver() { int total = 0; "
				"for (int i = 0; i < 4; i++) { total += owned_bridge(); } return total; }\n"
			"class OwnedWorker { public: "
				"int bridge() { return owned_work() + nested_bias; } "
				"int run() { int total = 0; "
				"for (int i = 0; i < 4; i++) { total += this.bridge(); } return total; } }\n"
			"interface IWorker { virtual int bridge() = 0; };\n"
			"class VirtualWorker : IWorker { public: "
				"int bridge() { return owned_work() + nested_bias; } };\n"
			"int dynamic_driver() { IWorker worker = VirtualWorker(); int total = 0; "
				"for (int i = 0; i < 4; i++) { total += worker.bridge(); } return total; }\n"
			"class IndirectCounter { public: int value = 0; void bump() { value++; } };\n"
			"int indirect_driver() { IndirectCounter counter = IndirectCounter(); "
				"fn()->int callback = fn() -> int => owned_work(); int total = 0; "
				"for (int i = 0; i < 4; i++) { counter.bump(); total += callback(); } "
				"return total + counter.value - 4; }\n";
		for (const char* const Driver : {"scalar_driver()", "worker.run()",
			"dynamic_driver()", "indirect_driver()"})
		{
			const auto Module = CompileSource(Source
				+ "void main() { OwnedWorker worker = OwnedWorker(); print(" + Driver + "); }\n");
			if (!Expect(static_cast<bool>(Module), "compile owned callee fallback regression"))
			{
				return EXIT_FAILURE;
			}
			for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
			{
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
				ConfigureBackend(Vm);
				Vm.SetExecutionBackend(Backend);
				if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
						"execute owned callee fallback regression")
					|| !Expect(Output.str() == "2095104\n",
						"owned callee fallback preserves outputs"))
				{
					std::cerr << Vm.GetError().Message << '\n';
					return EXIT_FAILURE;
				}
				#if !(VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64))
				if (Backend != VeroScript::EExecutionBackend::Vm)
				{
					const auto Stats = Vm.GetJitStats();
					if (!Expect(Stats.CostModelVmFallbacks >= 1u,
							"scalar driver retains VM fallback")
						|| !Expect(Stats.Dispatches >= 4u,
							"owned descendants retain independent backend dispatch"))
					{
						std::cerr << "driver=" << Driver << " dispatches=" << Stats.Dispatches << '\n';
						return EXIT_FAILURE;
					}
				}
				#endif
			}
		}
	}

	{
		// A scalar native signature does not prove that its callback cannot reenter
		// the script. Owned work reached through the host retains independent JIT dispatch.
		VeroScript::FHostBindingRegistry Registry;
		std::string Error;
		if (!Expect(Registry.RegisterNative(
				"invoke_owned",
				VeroScript::ENativeType::Int,
				{},
				[](const VeroScript::FNativeCallContext& Context,
					VeroScript::FValue& OutReturn, std::string& OutError)
				{
					if (!Context.ScriptInvoker)
					{
						OutError = "missing active script invoker";
						return false;
					}
					std::vector<VeroScript::FValue> Arguments;
					return Context.ScriptInvoker->InvokeFunction(
						"owned_work", Arguments, OutReturn, OutError);
				},
				nullptr,
				Error), "register owned host reentry fixture"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = &Registry;
		const auto Compile = VeroScript::VeroScriptCompile::CompileSource(
			"int owned_work() { array<int> values = array<int>(); values.reserve(1024); "
				"int i = 0; while (i < 1024) { values.push(i); i++; } int total = 0; i = 0; "
				"while (i < 1024) { total += values[i]; i++; } return total; }\n"
			"int native_driver() { int total = 0; for (int i = 0; i < 4; i++) "
				"{ total += invoke_owned(); } return total; }\n"
			"void main() { print(native_driver()); }\n", "host-reentry.vs", Options);
		if (!Expect(Compile.bSuccess && static_cast<bool>(Compile.Module),
			"compile owned host reentry fixture"))
		{
			std::cerr << Compile.Error.Message << '\n';
			return EXIT_FAILURE;
		}
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
			Vm.SetExecutionBackend(Backend);
			if (!Expect(Vm.Execute(*Compile.Module, VeroScript::ERunMode::Script,
						{}, &Registry.GetNativeFunctions()), "execute owned host reentry fixture")
				|| !Expect(Output.str() == "2095104\n", "owned host reentry preserves output"))
			{
				std::cerr << Vm.GetError().Message << '\n';
				return EXIT_FAILURE;
			}
			#if !(VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64))
			if (Backend != VeroScript::EExecutionBackend::Vm)
			{
				const auto Stats = Vm.GetJitStats();
				#if !VEROSCRIPT_HAS_ASMJIT
				// A guarded scalar host call can execute through portable Linear,
				// including host-to-script reentry, without native machine code.
				const bool DriverMatchesBackend = Stats.LinearTrampolineEntries != 0u
					&& Stats.NativeLeafEntries == 0u && Stats.NativeCfgEntries == 0u
					&& Stats.NativeTypedLoopExecutions == 0u;
				const char* const DriverMessage = "portable host driver uses non-native Linear entries";
				#else
				// A scalar-only backend may dispatch a portable Linear/bridge entry;
				// that is not a cost-model fallback and must not claim native CFG work.
				const bool DriverMatchesBackend = Stats.NativeCfgEntries == 0u
					&& Stats.ControlledNativeExecutions == 0u;
				const char* const DriverMessage = "scalar host driver uses supported non-CFG execution";
				#endif
				if (!Expect(DriverMatchesBackend, DriverMessage)
					|| !Expect(Stats.Dispatches >= 4u, "host reentry preserves owned JIT dispatch"))
				{
					std::cerr << "dispatches=" << Stats.Dispatches << '\n';
					return EXIT_FAILURE;
				}
			}
			#endif
		}
	}

	{
		// A bounded fixed-point inliner must remove ordinary scalar bridges at
		// every exposed level, not only the first two wrappers.  This is kept as
		// a data-dependent expression so it cannot be replaced by the direct-leaf
		// peephole or constant folding.
		const auto Module = CompileSource(
			"int inline_leaf(int x) { return x * 3 + 1; }\n"
			"int inline_l3(int x) { return inline_leaf(x) + 2; }\n"
			"int inline_l2(int x) { return inline_l3(x) + 3; }\n"
			"int inline_l1(int x) { return inline_l2(x) + 4; }\n"
			"int inline_run(int x) { return inline_l1(x); }\n"
			"void main() { print(inline_run(2)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile transitive scalar inlining"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "16\n", "transitive scalar inlining parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::uint16_t Index = FindFunctionIndex(*Module, "inline_run");
		const VeroScript::AsmJit::EJitCompileStatus Status =
			Index == static_cast<std::uint16_t>(-1)
				? VeroScript::AsmJit::EJitCompileStatus::Unsupported
				: Cache.EnsureCompiled(*Module, Index);
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
		if (!Expect(
				Index != static_cast<std::uint16_t>(-1)
					&& Status == VeroScript::AsmJit::EJitCompileStatus::Ok
					&& Entry && Entry->InlinedCallSites >= 4,
				"transitive scalar inlining reaches bounded fixed point"))
		{
			std::cerr << "  index=" << Index
				<< " status=" << static_cast<int>(Status)
				<< " inline_sites=" << (Entry ? Entry->InlinedCallSites : 0)
				<< '\n';
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Static and virtual one-argument methods with a proven direct field summary
		// are generation/shape-guarded once at loop entry. Both forms must preserve
		// ordinary VM results while avoiding a per-iteration object/PIC bridge.
		const std::string Source =
			"interface ICounter { virtual int update(int salt) = 0; };\n"
			"class Counter : ICounter { public: int value = 1;\n"
			"  int update(int salt) { value = (value + (salt % 17) + 1) % 1000000007; return value; }\n"
			"};\n"
			"int run_bound(int limit) { Counter c = Counter(); int state = 7;\n"
			"  for (int i = 0; i < limit; i++) { state = (state * 75 + 74) % 65521; c.update(state); }\n"
			"  return c.value + state; }\n"
			"int run_virtual(int limit) { ICounter c = Counter(); int state = 7; int value = 1;\n"
			"  for (int i = 0; i < limit; i++) { state = (state * 75 + 74) % 65521; value = c.update(state); }\n"
			"  return value + state; }\n"
			"void main() { print(run_bound(10000)); print(run_virtual(10000)); }\n";
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile native typed method loops"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "95018\n95018\n",
					"native typed method loop parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Mixed);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"execute native typed method loops for stats"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::FJitStats Stats = Vm.GetJitStats();
		if (!Expect(Stats.NativeTypedLoopExecutions >= 2,
				"bound and virtual methods reach native typed loops")
			|| !Expect(Stats.ScalarMethodFastPathI32 >= 19998,
				"native typed methods batch fast-path call accounting"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Non-canonical container expressions must use the typed native loop only
		// after complete runtime kind/CFG validation. This deliberately mixes an
		// independent index array, sparse integer/string keys and borrowed string
		// comparisons so a narrow whole-loop pattern cannot satisfy the test.
		const std::string Source =
			"int work(int limit) {\n"
			"  array<int> values = {1, 2, 3, 4}; array<int> indices = {3, 1, 0, 2};\n"
			"  map<int,int> counts = map<int,int>(); set<int> selected = {0, 2};\n"
			"  array<string> left = {\"aa\", \"bb\", \"cc\", \"dd\"};\n"
			"  array<string> right = {\"aa\", \"bx\", \"cc\", \"dx\"};\n"
			"  map<string,int> names = map<string,int>(); set<string> known = {\"aa\", \"cc\"};\n"
			"  names[\"aa\"] = 5; names[\"bb\"] = 7; names[\"cc\"] = 11; names[\"dd\"] = 13;\n"
			"  int total = 0; for (int i = 0; i < limit; i++) {\n"
			"    int slot = indices[(i * 3 + 1) & 3]; values[slot] = values[slot] + (i % 5);\n"
			"    counts[slot] = values[slot]; if (selected.has(slot)) total += counts[slot];\n"
			"    int textSlot = (i * 5 + 2) & 3;\n"
			"    if (left[textSlot] == right[textSlot]) total += names[left[textSlot]];\n"
			"    if (known.has(left[textSlot])) total++; total %= 1000000007;\n"
			"  } return total + values[indices[limit & 3]];\n"
			"}\nvoid main() { print(work(10000)); }\n";
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module),
				"compile generalized typed native containers"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "12562504\n",
					"generalized typed native containers"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Mixed);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"execute generalized typed native containers for stats"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::FJitStats Stats = Vm.GetJitStats();
		if (!Expect(Stats.NativeTypedLoopExecutions > 0,
				"generalized containers reach native typed loop")
			|| !Expect(Stats.ArrayFastPathGets >= 30000,
				"generalized containers retain borrowed array access")
			|| !Expect(Stats.ScalarMapFastPathGets >= 9000,
				"generalized containers retain typed map access")
			|| !Expect(Stats.ScalarMapFastPathSets >= 10000,
				"generalized containers retain typed map writes"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// A native push may reallocate an array that is also referenced by another
		// local. Every alias binding must observe the refreshed raw view before a
		// subsequent indexed access in the same generated loop.
		const std::string Source =
			"int aliased_push(int limit) {\n"
			"  array<int> values = array<int>(); values.reserve(1); values.push(7);\n"
			"  array<int> alias = values; array<int> pressure = array<int>();\n"
			"  int total = 0; for (int i = 0; i < limit; i++) {\n"
			"    values.push(i); pressure.push(i + 100); total += alias[0];\n"
			"  } return total + len(alias);\n"
			"}\nvoid main() { print(aliased_push(1000)); }\n";
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module),
				"compile aliased native array push"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "8001\n",
					"aliased native array push parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Mixed);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"execute aliased native array push for stats")
			|| !Expect(Vm.GetJitStats().NativeTypedLoopExecutions > 0,
				"aliased array push reaches native typed loop"))
		{
			std::cerr << Errors.str() << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Small, pure scalar free functions are expanded into the caller NativeCfg.
		// The caller loop also verifies that rewritten relative branches remain exact.
		const auto Module = CompileSource(
			"int max_leaf(int a, int b) {\n"
			"\tif (a < b) { a = b; }\n"
			"\treturn a;\n"
			"}\n"
			"int sum_calls(int n) {\n"
			"\tint s = 0;\n"
			"\tint i = 0;\n"
			"\twhile (i < n) { s = max_leaf(s, i); i++; }\n"
			"\treturn s;\n"
			"}\n"
			"void main() { print(sum_calls(100)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile bounded scalar inlining suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "99\n", "bounded scalar inlining parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(
				Cache, *Module, "sum_calls",
				VeroScript::AsmJit::EJitCompileStatus::Ok, "sum_calls inline compile"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t Index = FindFunctionIndex(*Module, "sum_calls");
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
		if (!Expect(Entry != nullptr
				&& Entry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
				&& Entry->InlinedCallSites == 1,
				"sum_calls expands one scalar free-call site"))
		{
			if (Entry)
			{
				std::cerr << "sum_calls kind=" << static_cast<int>(Entry->Kind)
					<< " inline_sites=" << Entry->InlinedCallSites << '\n';
			}
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Ownership-aware Linear callers retain their container identity while a
		// verifier-approved scalar leaf is expanded into the cached bytecode copy.
		// The module function itself must remain untouched for VM execution and hot
		// reload identity.
		const auto Module = CompileSource(
			"int advance(int value) { return (value * 5 + 1) % 17; }\n"
			"int sum_array(int seed) {\n"
			"\tarray<int> values = {3, 5, 7, 11};\n"
			"\tint total = 0; int i = 0;\n"
			"\twhile (i < 32) { seed = advance(seed); total += values[seed & 3]; i++; }\n"
			"\treturn total + seed;\n"
			"}\n"
			"void main() { print(sum_array(2)); }\n");
		if (!Expect(static_cast<bool>(Module),
				"compile ownership-aware scalar inlining suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "210\n",
					"ownership-aware scalar inlining parity"))
			{
				return EXIT_FAILURE;
			}
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::uint16_t Index = FindFunctionIndex(*Module, "sum_array");
		if (!Expect(Index != static_cast<std::uint16_t>(-1)
				&& Cache.EnsureCompiled(*Module, Index)
					== VeroScript::AsmJit::EJitCompileStatus::Ok,
				"compile ownership-aware inlined entry"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
		if (!Expect(Entry && Entry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
				&& Entry->LinearFunction && Entry->InlinedCallSites == 1
				&& Entry->LinearFunction->Code.size()
					> Module->Functions[Index].Code.size(),
				"ownership-aware entry owns one verified scalar expansion"))
		{
			return EXIT_FAILURE;
		}
		const std::weak_ptr<const VeroScript::FBytecodeFunction> LinearLifetime =
			Entry->LinearFunction;
		Cache.InvalidateAll();
		if (!Expect(Cache.GetCandidate(Index) == nullptr && LinearLifetime.expired(),
				"cache invalidation releases the generation-scoped linear rewrite"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectDifferentialRun(
				"int divide_leaf(int left, int right) {\n"
				"\treturn left / right;\n"
				"}\n"
				"int owned_divide(int right) {\n"
				"\tarray<int> values = {1};\n"
				"\treturn divide_leaf(values[0], right);\n"
				"}\n"
				"void main() { print(owned_divide(0)); }\n",
				"ownership-aware inlining preserves callee diagnostics"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Exception tables are function-relative. Until the bytecode inliner can
		// relocate protected ranges and handlers, an otherwise scalar callee with
		// catch metadata must retain its real call frame.
		const auto Module = CompileSource(
			"int guarded_abs(int value) {\n"
			"\ttry { if (value < 0) { return -value; } return value + 1; }\n"
			"\tcatch (...) { return 0; }\n"
			"}\n"
			"int owned_guard(int value) {\n"
			"\tarray<int> values = {11};\n"
			"\treturn guarded_abs(value) + values[0];\n"
			"}\n"
			"void main() { print(owned_guard(-5)); }\n");
		if (!Expect(static_cast<bool>(Module),
				"compile exception-region inline exclusion suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "16\n",
					"exception-region inline exclusion parity"))
			{
				return EXIT_FAILURE;
			}
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::uint16_t Index = FindFunctionIndex(*Module, "owned_guard");
		if (!Expect(Index != static_cast<std::uint16_t>(-1)
				&& Cache.EnsureCompiled(*Module, Index)
					== VeroScript::AsmJit::EJitCompileStatus::Ok,
				"compile exception-region caller entry"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
		if (!Expect(Entry
				&& Entry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
				&& !Entry->LinearFunction && Entry->InlinedCallSites == 0,
				"exception-region callee retains an authoritative call frame"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Nested scalar reads exercise field-slot, Array and Map indexing with an
		// index produced by another indexed expression. Every backend must preserve
		// identical order, lookup and checked-arithmetic semantics.
		const auto Module = CompileSource(
			"class Selector { public: int slot = 0; };\n"
			"int nested_access(int seed) {\n"
			"\tarray<int> order = {2, 0, 3, 1};\n"
			"\tarray<int> values = {11, 13, 17, 19};\n"
			"\tmap<int,int> lookup = map<int,int>();\n"
			"\tlookup[11] = 3; lookup[13] = 5; lookup[17] = 7; lookup[19] = 9;\n"
			"\tSelector selector(); int total = 0; int i = 0;\n"
			"\twhile (i < 16) {\n"
			"\t\tselector.slot = (seed + i) & 3;\n"
			"\t\ttotal += values[order[selector.slot]];\n"
			"\t\ttotal += lookup[values[order[selector.slot]]];\n"
			"\t\ti++;\n"
			"\t}\n"
			"\treturn total;\n"
			"}\n"
			"void main() { print(nested_access(1)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile nested scalar read suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "336\n",
					"nested field/Array/Map scalar read parity"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const auto Module = CompileSource(
			"int char_map_index(int count) {\n"
			"\tmap<char,int> lookup = {'A': 0, 'B': 0};\n"
			"\tordered_map<char,int> ordered = {'A': 0, 'B': 0};\n"
			"\tint total = 0; int i = 0;\n"
			"\twhile (i < count) {\n"
			"\t\tchar key = (i & 1) == 0 ? 'A' : 'B';\n"
			"\t\tlookup[key] = lookup[key] + 1;\n"
			"\t\tordered[key] = lookup[key];\n"
			"\t\ttotal += ordered[key]; i++;\n"
			"\t}\n"
			"\treturn total;\n"
			"}\n"
			"void main() { print(char_map_index(32)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile char Map JIT bridge suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "272\n", "char Map JIT bridge parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		const std::uint16_t FunctionIndex = FindFunctionIndex(*Module, "char_map_index");
		VeroScript::AsmJit::FJitCodeCache Cache;
		const VeroScript::AsmJit::FJitFunctionEntry* Entry = nullptr;
		if (!Expect(
			FunctionIndex != static_cast<std::uint16_t>(-1)
				&& Cache.EnsureCompiled(*Module, FunctionIndex)
					== VeroScript::AsmJit::EJitCompileStatus::Ok
				&& (Entry = Cache.GetCandidate(FunctionIndex)) != nullptr
				&& (Entry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
					|| Entry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline),
			"char Map indexing retains a JIT-lowered entry"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// A neutral void method that touches both signed and unsigned state must
		// never be assigned either homogeneous raw ABI. Mixed preserves both.
		const std::string Source =
			"class MixedCounter {\n"
			"public:\n"
			"\tint signed_value = 0;\n"
			"\tuint32 unsigned_value = 0u;\n"
			"\tvoid bump() {\n"
			"\t\tsigned_value = signed_value + 1;\n"
			"\t\tunsigned_value = unsigned_value + 1u;\n"
			"\t}\n"
			"\tint get_signed() { return signed_value; }\n"
			"\tuint32 get_unsigned() { return unsigned_value; }\n"
			"}\n"
			"void main() {\n"
			"\tMixedCounter value = MixedCounter();\n"
			"\tvalue.bump();\n"
			"\tprint(value.get_signed());\n"
			"\tprint(value.get_unsigned());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile mixed signed/unsigned void method"))
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
					Capture.bSuccess && Capture.Output == "1\n1\n",
					"mixed signed/unsigned void method preserves semantics"))
			{
				std::cerr << "backend=" << static_cast<int>(Backend)
					<< " out=[" << Capture.Output << "] err=" << Capture.Errors << '\n';
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		const std::uint16_t BumpIndex = FindFunctionIndex(*Module, "bump");
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!Expect(
				BumpIndex != static_cast<std::uint16_t>(-1)
					&& Cache.EnsureCompiled(*Module, BumpIndex)
						== VeroScript::AsmJit::EJitCompileStatus::Ok
					&& Cache.GetCandidate(BumpIndex)
		#if VEROSCRIPT_ASMJIT_X64
					&& Cache.GetCandidate(BumpIndex)->ScalarAbi
						== VeroScript::AsmJit::EJitScalarAbi::Mixed
		#else
					&& !Cache.GetCandidate(BumpIndex)->bNativeLeaf
					&& Cache.GetCandidate(BumpIndex)->Kind != VeroScript::AsmJit::EJitEntryKind::NativeCfg
		#endif
					,
				"mixed signed/unsigned void method uses a supported ABI or owned fallback"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Wider/f32 scalar method chains use the Mixed ABI. Their integer void
		// sentinel must not prevent bounded current-this inlining. F64 uses
		// its homogeneous ABI and normalizes that sentinel to an existing F64 zero.
		const std::string Source =
			"class I64Chain {\n"
			"public:\n"
			"\tint64 value = 0i64;\n"
			"\tvoid add(int64 amount) { value = value + amount; }\n"
			"\tvoid bump() { this.add(1i64); }\n"
			"\tint64 run_i64(int64 count) { int64 i = 0i64; while (i < count) { this.bump(); i = i + 1i64; } return value; }\n"
			"}\n"
			"class U64Chain {\n"
			"public:\n"
			"\tuint64 value = 0u64;\n"
			"\tvoid add(uint64 amount) { value = value + amount; }\n"
			"\tvoid bump() { this.add(1u64); }\n"
			"\tuint64 run_u64(uint64 count) { uint64 i = 0u64; while (i < count) { this.bump(); i = i + 1u64; } return value; }\n"
			"}\n"
			"class F32Chain {\n"
			"public:\n"
			"\tfloat value = 0.f;\n"
			"\tvoid add(float amount) { value = value + amount; }\n"
			"\tvoid bump() { this.add(1.f); }\n"
			"\tfloat run_f32(float count) { float i = 0.f; while (i < count) { this.bump(); i = i + 1.f; } return value; }\n"
			"}\n"
			"class F64Chain {\n"
			"public:\n"
			"\tdouble value = 0.0;\n"
			"\tvoid add(double amount) { value = value + amount; }\n"
			"\tvoid bump() { this.add(1.0); }\n"
			"\tdouble run_f64(double count) { double i = 0.0; while (i < count) { this.bump(); i = i + 1.0; } return value; }\n"
			"}\n"
			"void main() {\n"
			"\tI64Chain a = I64Chain(); U64Chain b = U64Chain(); F32Chain c = F32Chain(); F64Chain d = F64Chain();\n"
			"\tprint(to_int(a.run_i64(3i64))); print(to_int(b.run_u64(4u64))); print(to_int(c.run_f32(5.f))); print(to_int(d.run_f64(6.0)));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile wide scalar this-inlining suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "3\n4\n5\n6\n", "wide scalar this-inlining suite"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::pair<const char*, VeroScript::AsmJit::EJitScalarAbi> Methods[] = {
			{"run_i64", VeroScript::AsmJit::EJitScalarAbi::Mixed},
			{"run_u64", VeroScript::AsmJit::EJitScalarAbi::Mixed},
			{"run_f32", VeroScript::AsmJit::EJitScalarAbi::Mixed},
			{"run_f64", VeroScript::AsmJit::EJitScalarAbi::F64},
		};
		for (const auto& [MethodName, ExpectedAbi] : Methods)
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, MethodName);
			#if VEROSCRIPT_ASMJIT_A64
			// The A64 backend advertises homogeneous I32/U32/F64 CFGs. Wider
			// scalar chains still execute through its owned fallback.
			if (ExpectedAbi == VeroScript::AsmJit::EJitScalarAbi::Mixed)
			{
				if (!Expect(Index != static_cast<std::uint16_t>(-1) &&
							Cache.EnsureCompiled(*Module, Index) ==
								VeroScript::AsmJit::EJitCompileStatus::Ok &&
							Cache.GetCandidate(Index) && !Cache.GetCandidate(Index)->bNativeLeaf,
						"wide scalar method chain retains an owned fallback on A64"))
				{
					return EXIT_FAILURE;
				}

				continue;
			}
			#endif
			if (!Expect(
					Index != static_cast<std::uint16_t>(-1)
						&& Cache.EnsureCompiled(*Module, Index)
							== VeroScript::AsmJit::EJitCompileStatus::Ok
						&& Cache.GetCandidate(Index)
						&& Cache.GetCandidate(Index)->ScalarAbi == ExpectedAbi
						&& Cache.GetCandidate(Index)->InlinedCallSites >= 2,
					"wide scalar method chain expands bounded this-call sites"))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
	}

	{
		// A nullary wrapper around a resolved this-field update may be collapsed
		// across a canonical loop only when neither host interception nor mutation
		// observation is active. All three backends retain identical results, and
		// the guarded JIT executions prove those observability barriers still win.
		const std::string Source =
			"class NestedCounter {\n"
			"public:\n"
			"\tint value = 0;\n"
			"\tvoid add(int amount) { value += amount; }\n"
			"\tvoid bump() { this.add(1); }\n"
			"};\n"
			"class Prefix { public: int padding = 40; };\n"
			"class DerivedCounter : Prefix, NestedCounter { };\n"
			"int work(NestedCounter value, int limit) {\n"
			"\tfor (int i = 0; i < limit; i++) { value.bump(); }\n"
			"\treturn value.value;\n"
			"}\n"
			"void main() {\n"
			"\tNestedCounter exact = NestedCounter();\n"
			"\tDerivedCounter derived = DerivedCounter();\n"
			"\tprint(work(exact, 8)); print(work(derived, 8));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile nested local this-call suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "8\n8\n", "nested local this-call parity"))
			{
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
			std::size_t MutationCount = 0;
			Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
			{
				if (Event.FieldName == "value")
				{
					++MutationCount;
				}
			});
			if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script)
					&& Output.str() == "8\n8\n" && MutationCount >= 16,
					"nested local this-call preserves mutation callbacks"))
			{
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
			std::size_t InterceptCount = 0;
			Vm.SetInstanceCallInterceptor(
				[&](const VeroScript::FInstanceCallInterceptContext&,
					VeroScript::FValue&, VeroScript::FScriptError&)
					-> VeroScript::EInstanceCallInterceptResult
				{
					++InterceptCount;
					return VeroScript::EInstanceCallInterceptResult::Unhandled;
				});
			if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script)
					&& Output.str() == "8\n8\n" && InterceptCount >= 16,
					"nested local this-call preserves interception"))
			{
				return EXIT_FAILURE;
			}
		}

		const auto IntegerDivByZeroModule = CompileSource(
			"int mixed_int_div_zero(double marker) { int value = 1; return value / 0; } "
			"void main() { print(mixed_int_div_zero(0.0)); }");
		if (!Expect(static_cast<bool>(IntegerDivByZeroModule),
				"compile mixed integer division-by-zero regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*IntegerDivByZeroModule, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.division_by_zero",
					"mixed integer division preserves division-by-zero diagnostics"))
			{
				return EXIT_FAILURE;
			}
		}

		const auto I64DivOverflowModule = CompileSource(
			"int64 mixed_i64_div_overflow(double marker) { "
			"int64 value = -9223372036854775807i64; value = value - 1i64; "
			"return value / -1i64; } "
			"void main() { print(mixed_i64_div_overflow(0.0)); }");
		if (!Expect(static_cast<bool>(I64DivOverflowModule),
				"compile mixed int64 division-overflow regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*I64DivOverflowModule, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.integer_overflow",
					"mixed int64 division preserves overflow diagnostics"))
			{
				return EXIT_FAILURE;
			}
		}

		const std::pair<const char*, const char*> ConversionFailureCases[] = {
			{
				"int mixed_to_int_overflow(double marker) { return to_int(4294967295u); } "
				"void main() { print(mixed_to_int_overflow(0.0)); }",
				"diag.to_int_overflow",
			},
			{
				"uint32 mixed_to_unsigned_overflow(double marker) { return to_unsigned(-1); } "
				"void main() { print(mixed_to_unsigned_overflow(0.0)); }",
				"diag.to_unsigned_overflow",
			},
		};
		for (const auto& [FailureSource, ExpectedError] : ConversionFailureCases)
		{
			const auto ConversionFailureModule = CompileSource(FailureSource);
			if (!Expect(static_cast<bool>(ConversionFailureModule),
					"compile mixed conversion-overflow regression"))
			{
				return EXIT_FAILURE;
			}
			for (const VeroScript::EExecutionBackend Backend :
				{VeroScript::EExecutionBackend::Vm,
					VeroScript::EExecutionBackend::Mixed,
					VeroScript::EExecutionBackend::Jit})
			{
				const FExecutionCapture Capture = RunCapture(*ConversionFailureModule, Backend);
				if (!Expect(!Capture.bSuccess && Capture.ErrorMessageId == ExpectedError,
						"mixed conversion preserves overflow diagnostics"))
				{
					return EXIT_FAILURE;
				}
			}
		}

		const auto OverflowModule = CompileSource(
			"class OverflowCounter { public: int value = 2147483646; "
			"void add(int amount) { value += amount; } "
			"void bump() { this.add(1); } };\n"
			"void main() { OverflowCounter value = OverflowCounter(); "
			"for (int i = 0; i < 2; i++) { value.bump(); } }\n");
		if (!Expect(static_cast<bool>(OverflowModule),
				"compile nested local this-call overflow suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*OverflowModule, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.integer_overflow",
					"nested local this-call preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
		}

		// Typed Array construction may only bypass the general container bridge for
		// an unobserved local. A local alias of an object field retains the field's
		// mutation origin and must emit one write-barrier event per source push.
		const auto ArrayMutationModule = CompileSource(
			"class Items { public: array<int> signedValues = array<int>(); "
			"array<uint32> unsignedValues = array<uint32>(); "
			"void fill() { array<int> signedLocal = signedValues; "
			"array<uint32> unsignedLocal = unsignedValues; "
			"for (int i = 0; i < 8; i++) signedLocal.push(i & 3); "
			"for (int i = 0; i < 8; i++) unsignedLocal.push(cast<uint32>(i & 3)); "
			"print(len(signedLocal) + len(unsignedLocal)); } }; "
			"void main() { Items items = Items(); items.fill(); }");
		if (!Expect(static_cast<bool>(ArrayMutationModule),
				"compile typed Array mutation-barrier regression"))
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
			std::size_t ContainerAdds = 0;
			Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
			{
				if ((Event.FieldName == "signedValues"
						|| Event.FieldName == "unsignedValues")
					&& Event.Kind == VeroScript::EMutationKind::ContainerAdd)
				{
					++ContainerAdds;
				}
			});
			if (!Expect(Vm.Execute(*ArrayMutationModule, VeroScript::ERunMode::Script)
					&& Output.str() == "16\n" && ContainerAdds == 16,
					"typed Array push preserves field mutation barriers"))
			{
				std::cerr << "array mutation output='" << Output.str()
					<< "' adds=" << ContainerAdds << " errors='" << Errors.str() << "'\n";
				return EXIT_FAILURE;
			}
		}

		// Whole-loop typed Array/Map updates may borrow raw scalar storage only when
		// container writes are unobserved. A field alias retains its mutation origin,
		// so registering a callback must make the optimizer use the ordinary SetIndex
		// path and emit one ContainerSet event for every source assignment.
		const auto IndexedMutationModule = CompileSource(
			"class Buffers { public: array<int> items = array<int>(); "
			"map<int,int> lookup = map<int,int>(); map<int,int> generated = map<int,int>(); "
			"void update() { array<int> localItems = items; map<int,int> localLookup = lookup; "
			"map<int,int> localGenerated = generated; int i = 0; while (i < 17) { "
			"localItems.push(0); localLookup[i] = 0; localGenerated[i] = i % 7; i++; } "
			"int state = 1; int total = 0; i = 0; while (i < 32) { "
			"state = (state * 25173 + 13849) % 65521; int index = state % 17; "
			"localItems[index] = (localItems[index] + (state % 5) + 1) % 100003; "
			"total = (total + localItems[index]) % 1000000007; i++; } "
			"state = 1; i = 0; while (i < 32) { "
			"state = (state * 25173 + 13849) % 65521; int key = state % 17; "
			"localLookup[key] = (localLookup[key] + (state % 5) + 1) % 100003; "
			"total = (total + localLookup[key]) % 1000000007; i++; } print(total); } }; "
			"void main() { Buffers buffers = Buffers(); buffers.update(); }");
		if (!Expect(static_cast<bool>(IndexedMutationModule),
				"compile typed indexed container mutation-barrier regression"))
		{
			return EXIT_FAILURE;
		}
		{
			std::ostringstream BaselineOutput;
			std::ostringstream BaselineErrors;
			std::istringstream BaselineInput;
			VeroScript::VeroScriptVm BaselineVm(
				BaselineOutput, BaselineInput, BaselineErrors);
		VeroScript::Test::Configure(BaselineVm);
			ConfigureBackend(BaselineVm);
		BaselineVm.SetExecutionBackend(VeroScript::EExecutionBackend::Vm);
			if (!Expect(BaselineVm.Execute(
						*IndexedMutationModule, VeroScript::ERunMode::Script),
					"run typed indexed container mutation baseline"))
			{
				return EXIT_FAILURE;
			}

			std::ostringstream Output;
			std::ostringstream Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
			std::size_t ArraySets = 0;
			std::size_t MapSets = 0;
			std::size_t GeneratedMapSets = 0;
			Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
			{
				if (Event.Kind != VeroScript::EMutationKind::ContainerSet)
				{
					return;
				}
				if (Event.FieldName == "items")
				{
					++ArraySets;
				}
				else if (Event.FieldName == "lookup")
				{
					++MapSets;
				}
				else if (Event.FieldName == "generated")
				{
					++GeneratedMapSets;
				}
			});
			if (!Expect(Vm.Execute(*IndexedMutationModule, VeroScript::ERunMode::Script)
					&& Output.str() == BaselineOutput.str()
					&& ArraySets == 32 && MapSets == 49 && GeneratedMapSets == 17,
					"typed indexed Array/Map updates preserve mutation barriers"))
			{
				std::cerr << "indexed mutation output='" << Output.str()
					<< "' baseline='" << BaselineOutput.str() << "' arraySets="
					<< ArraySets << " mapSets=" << MapSets << " generatedMapSets="
					<< GeneratedMapSets << " errors='"
					<< Errors.str() << "'\n";
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Bool fields / bool-returning helpers must also keep true/false formatting.
		const std::string Source =
			"class Flag {\n"
			"public:\n"
			"\tbool on = true;\n"
			"\tbool get() { return on; }\n"
			"}\n"
			"void main() {\n"
			"\tFlag f = Flag();\n"
			"\tprint(f.on);\n"
			"\tprint(f.get());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile bool field/print suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output == "true\ntrue\n",
					"bool field/method print semantics"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Ordinary scalar output remains a supported builtin in lowering.
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
			CompileSource("void main() { print(1); }\n");
		if (!Expect(static_cast<bool>(Module), "compile scalar print lowering suite"))
		{
			return EXIT_FAILURE;
		}
		const auto MainIndex = static_cast<std::uint16_t>(Module->MainFunctionIndex);
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitLowering::BuildScalarPlan(
					*Module, Module->Functions[MainIndex], Plan, &Error),
				"lower scalar print suite")
					|| !Expect(std::find(Plan.RuntimeFallbackOpcodes.begin(),
							Plan.RuntimeFallbackOpcodes.end(), VeroScript::EOpcode::CallBuiltin)
								== Plan.RuntimeFallbackOpcodes.end(),
						"scalar print does not force builtin fallback"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// exit owns VM control flow and must never continue in a scalar helper.
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
			CompileSource("void main() { exit(7); print(1); }\n");
		if (!Expect(static_cast<bool>(Module), "compile exit fallback suite"))
		{
			return EXIT_FAILURE;
		}
		const auto MainIndex = static_cast<std::uint16_t>(Module->MainFunctionIndex);
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitLowering::BuildScalarPlan(
					*Module, Module->Functions[MainIndex], Plan, &Error),
				"lower exit fallback suite")
					|| !Expect(std::find(Plan.RuntimeFallbackOpcodes.begin(),
							Plan.RuntimeFallbackOpcodes.end(), VeroScript::EOpcode::CallBuiltin)
								!= Plan.RuntimeFallbackOpcodes.end(),
						"exit forces builtin fallback"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Boxed print arguments remain on the ownership-safe fallback path.
		if (!ExpectDifferentialRun(
				"void main() { print(\"boxed\"); }\n",
				"boxed main print fallback semantics"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::AsmJit::FJitCodeCache Cache;
		VeroScript::FJitPolicy Policy;
		Policy.MixedHotCallThreshold = 3;
		Cache.SetPolicy(Policy);
		(void)Cache.ShouldCompileOnCall(11);
		Cache.InvalidateAll();
		if (!Expect(!Cache.ShouldCompileOnCall(11) && !Cache.ShouldCompileOnCall(11)
				&& Cache.ShouldCompileOnCall(11)
				&& Cache.GetStats().HotnessDeferrals == 2
				&& Cache.GetStats().HotFunctions == 1,
				"JIT invalidation resets hotness without dropping policy"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::AsmJit::FJitCodeCache Cache;
		VeroScript::FJitPolicy Policy;
		Policy.MixedHotCallThreshold = 3;
		Cache.SetPolicy(Policy);
		if (!Expect(!Cache.ShouldCompileOnCall(7) && !Cache.ShouldCompileOnCall(7)
				&& Cache.ShouldCompileOnCall(7) && Cache.ShouldCompileOnCall(7),
				"mixed JIT hotness threshold")
					|| !Expect(Cache.GetStats().HotnessDeferrals == 2
						&& Cache.GetStats().HotFunctions == 1,
						"mixed JIT hotness statistics"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FBytecodeModule Module;
		Module.Functions.resize(3);
		for (VeroScript::FBytecodeFunction& Function : Module.Functions)
		{
			Function.bReturnsVoid = true;
			Function.Code = {static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		}

		VeroScript::AsmJit::FJitCodeCache BudgetCache;
		VeroScript::FJitPolicy BudgetPolicy;
		BudgetPolicy.MaxCompileAttempts = 1;
		BudgetPolicy.MaxCachedEntries = 0;
		BudgetCache.SetPolicy(BudgetPolicy);
		if (!Expect(BudgetCache.EnsureCompiled(Module, 0) == VeroScript::AsmJit::EJitCompileStatus::Ok
				&& BudgetCache.EnsureCompiled(Module, 1) == VeroScript::AsmJit::EJitCompileStatus::Failed
				&& BudgetCache.GetStats().CompileBudgetSkips == 1,
				"JIT compile attempt budget"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::AsmJit::FJitCodeCache CapacityCache;
		VeroScript::FJitPolicy CapacityPolicy;
		CapacityPolicy.MaxCompileAttempts = 0;
		CapacityPolicy.MaxCachedEntries = 1;
		CapacityCache.SetPolicy(CapacityPolicy);
		if (!Expect(CapacityCache.EnsureCompiled(Module, 0) == VeroScript::AsmJit::EJitCompileStatus::Ok
				&& CapacityCache.EnsureCompiled(Module, 1) == VeroScript::AsmJit::EJitCompileStatus::Failed
				&& CapacityCache.GetStats().CacheCapacitySkips == 1,
				"JIT cache capacity budget"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::AsmJit::FJitCodeCache EagerCache;
		VeroScript::FJitPolicy EagerPolicy;
		EagerPolicy.MaxCompileAttempts = 0;
		EagerPolicy.MaxCachedEntries = 0;
		EagerPolicy.EagerCompileBudget = 1;
		EagerCache.SetPolicy(EagerPolicy);
		EagerCache.PrepareModule(Module, VeroScript::EExecutionBackend::Jit);
		if (!Expect(EagerCache.CompiledCount() == 1
				&& EagerCache.GetStats().EagerCompileSkips == 2,
				"JIT eager preparation budget"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::AsmJit::FJitCodeCache MixedCache;
		MixedCache.PrepareModule(Module, VeroScript::EExecutionBackend::Mixed);
		if (!Expect(
				MixedCache.EnsureCompiled(Module, 0) == VeroScript::AsmJit::EJitCompileStatus::Ok
					&& MixedCache.GetCandidate(0) != nullptr,
				"mixed JIT cache compiles a hot module-local entry"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FBytecodeModule ReplacementModule;
		ReplacementModule.Functions.resize(1);
		ReplacementModule.Functions[0].bReturnsVoid = true;
		ReplacementModule.Functions[0].Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		MixedCache.PrepareModule(
			ReplacementModule,
			VeroScript::EExecutionBackend::Mixed);
		if (!Expect(MixedCache.GetCandidate(0) == nullptr,
				"mixed JIT preparation invalidates prior module entries"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream ErrorOutput;
		VeroScript::VeroScriptVm Vm(Output, Input, ErrorOutput);
		VeroScript::Test::Configure(Vm);
		Vm.RecordJitArrayFastPathPush();
		Vm.RecordJitArrayFastPathGet();
		Vm.RecordJitArrayFastPathSet();
		Vm.InvalidateJitCache();
		const VeroScript::FJitStats Stats = Vm.GetJitStats();
		if (!Expect(Stats.ArrayFastPathPushes == 1 && Stats.ArrayFastPathGets == 1
				&& Stats.ArrayFastPathSets == 1 && Stats.CacheInvalidations == 1,
				"JIT fast-path and invalidation counters report hits"))
		{
			return EXIT_FAILURE;
		}
		Vm.ResetJitStats();
		const VeroScript::FJitStats ResetStats = Vm.GetJitStats();
		if (!Expect(ResetStats.ArrayFastPathPushes == 0 && ResetStats.ArrayFastPathGets == 0
				&& ResetStats.ArrayFastPathSets == 0 && ResetStats.CacheInvalidations == 0,
				"JIT fast-path and invalidation counters reset"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FBytecodeModule Module;
		VeroScript::FBytecodeFunction Function;
		Function.ClosureTypeName = "fn(any)->any";
		Function.ParamCount = 1;
		Function.LocalCount = 1;
		Function.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::GetIndexCopy),
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(VeroScript::AsmJit::JitLowering::BuildScalarPlan(Module, Function, Plan, &Error),
				"object lowering plan builds conservatively")
					|| !Expect(Plan.bHasBoxedValues && Plan.LocalSlots.size() == 1
						&& Plan.LocalSlots[0].Storage
						== VeroScript::AsmJit::JitLowering::FValueSlot::EStorage::Boxed,
						"object lowering plan records boxed local storage")
							|| !Expect(Plan.bHasRuntimeFallback
								&& std::find(Plan.RuntimeFallbackOpcodes.begin(), Plan.RuntimeFallbackOpcodes.end(),
									VeroScript::EOpcode::GetIndexCopy) != Plan.RuntimeFallbackOpcodes.end(),
								"object lowering plan records index fallback opcode"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::AsmJit::FJitCodeCache Cache;
		VeroScript::FBytecodeModule Module;
		Module.Functions.resize(1);
		Module.Functions[0].bReturnsVoid = true;
		Module.Functions[0].Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		if (!Expect(Cache.EnsureCompiled(Module, 0) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"stats reset fixture compiles"))
		{
			return EXIT_FAILURE;
		}
		Cache.InvalidateAll();
		const VeroScript::FJitStats Stats = Cache.GetStats();
		if (!Expect(Stats.CompiledEntries == 0 && Stats.BridgeTrampolineEntries == 0
				&& Stats.NativeLeafEntries == 0 && Stats.UnsupportedEntries == 0,
				"invalidate resets all JIT entry statistics"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Untyped hand-built Add shape must NOT match without ClosureTypeName.
		VeroScript::AsmJit::FJitCodeCache Cache;
		VeroScript::FBytecodeModule Module;
		Module.Functions.resize(1);
		auto& Fn = Module.Functions[0];
		Fn.bReturnsVoid = false;
		Fn.ParamCount = 2;
		Fn.LocalCount = 2;
		Fn.Code = {
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 0, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::LoadLocal), 1, 0,
			static_cast<std::uint8_t>(VeroScript::EOpcode::Add),
			static_cast<std::uint8_t>(VeroScript::EOpcode::Return)};
		if (!Expect(
				Cache.EnsureCompiled(Module, 0) == VeroScript::AsmJit::EJitCompileStatus::Unsupported,
				"untyped add shape stays Unsupported"))
		{
			return EXIT_FAILURE;
		}

		Fn.ClosureTypeName = "fn(int,int)->int";
		VeroScript::AsmJit::FJitCodeCache Cache2;
		if (!Expect(
				Cache2.EnsureCompiled(Module, 0) == VeroScript::AsmJit::EJitCompileStatus::Ok,
				"typed add shape → Ok"))
		{
			return EXIT_FAILURE;
		}
	}

	std::ostringstream DiscardOut;
	std::ostringstream DiscardErr;
	std::istringstream DiscardIn;

	{
		VeroScript::VeroScriptVm Vm(DiscardOut, DiscardIn, DiscardErr);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		VeroScript::VeroScriptDebugger Debugger;
		Vm.SetDebugger(&Debugger);
		if (!Expect(
				Vm.GetEffectiveExecutionBackend() == VeroScript::EExecutionBackend::Vm,
				"debugger forces effective vm"))
		{
			return EXIT_FAILURE;
		}
		Vm.SetDebugger(nullptr);
		if (!Expect(
				Vm.GetEffectiveExecutionBackend() == VeroScript::EExecutionBackend::Jit,
				"effective jit after detach"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
			"int add(int a, int b) { return a + b; }\n"
			"int sub(int a, int b) { return a - b; }\n"
			"int mul(int a, int b) { return a * b; }\n"
			"int div(int a, int b) { return a / b; }\n"
			"int mod(int a, int b) { return a % b; }\n"
			"int neg(int a) { return -a; }\n"
			"int id(int a) { return a; }\n"
			"int forty_two() { return 42; }\n"
			"int inc(int a) { return a + 1; }\n"
			"int dec(int a) { return a - 1; }\n"
			"bool eq(int a, int b) { return a == b; }\n"
			"bool ne(int a, int b) { return a != b; }\n"
			"bool lt(int a, int b) { return a < b; }\n"
			"bool le(int a, int b) { return a <= b; }\n"
			"bool gt(int a, int b) { return a > b; }\n"
			"bool ge(int a, int b) { return a >= b; }\n"
			"bool notb(bool a) { return !a; }\n"
			"bool truth() { return true; }\n"
			"int mask(int x) { return x & 255; }\n"
			"int from(int x) { return 10 - x; }\n"
			"int shl3(int x) { return x << 3; }\n"
			"string strcat(string a, string b) { return a + b; }\n"
			"double dadd(double a, double b) { return a + b; }\n"
			"void empty() {}\n"
			"void main() {\n"
			"\tprint(add(40, 2));\n"
			"\tprint(sub(10, 3));\n"
			"\tprint(mul(6, 7));\n"
			"\tprint(div(20, 4));\n"
			"\tprint(mod(20, 6));\n"
			"\tprint(neg(5));\n"
			"\tprint(id(9));\n"
			"\tprint(forty_two());\n"
			"\tprint(inc(8));\n"
			"\tprint(dec(8));\n"
			"\tprint(eq(1, 1));\n"
			"\tprint(ne(1, 2));\n"
			"\tprint(lt(1, 2));\n"
			"\tprint(le(2, 2));\n"
			"\tprint(gt(3, 2));\n"
			"\tprint(ge(2, 2));\n"
			"\tprint(notb(false));\n"
			"\tprint(truth());\n"
			"\tprint(mask(300));\n"
			"\tprint(from(3));\n"
			"\tprint(shl3(2));\n"
			"\tprint(strcat(\"a\", \"b\"));\n"
			"\tprint(to_string(dadd(1.5, 2.5)));\n"
			"\tempty();\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile arith suite"))
		{
			return EXIT_FAILURE;
		}

		{
			VeroScript::AsmJit::FJitCodeCache Cache;
			const char* const OkNames[] = {
				"add", "sub", "mul", "div", "mod", "neg", "id", "forty_two", "inc", "dec",
				"eq", "ne", "lt", "le", "gt", "ge", "notb", "truth", "mask", "from", "shl3",
				"dadd", "empty"};
			for (const char* Name : OkNames)
			{
				if (!ExpectStatus(Cache, *Module, Name, VeroScript::AsmJit::EJitCompileStatus::Ok, Name))
				{
					return EXIT_FAILURE;
				}
			}
			if (!ExpectStatus(
					Cache,
						*Module,
					"strcat",
					VeroScript::AsmJit::EJitCompileStatus::Ok,
					"string add JITs via linear"))
			{
				return EXIT_FAILURE;
			}

			const std::uint16_t FortyTwo = FindFunctionIndex(*Module, "forty_two");
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(FortyTwo);
			if (!Expect(Entry != nullptr && Entry->PayloadI32 == 42, "const payload baked"))
			{
				return EXIT_FAILURE;
			}
			#if VEROSCRIPT_HAS_ASMJIT
			if (!Expect(Cache.CompiledNativeCount() >= 12, "asmjit native leaves compiled"))
			{
				return EXIT_FAILURE;
			}
			const char* const NativeNames[] = {
				"add", "sub", "mul", "div", "mod", "neg", "forty_two", "inc", "dec", "from",
				"mask", "shl3", "eq", "ne", "lt", "le", "gt", "ge", "notb", "truth", "id"};
			for (const char* Name : NativeNames)
			{
				const std::uint16_t Index = FindFunctionIndex(*Module, Name);
				const VeroScript::AsmJit::FJitFunctionEntry* const NativeEntry = Cache.GetCandidate(Index);
				if (!Expect(
						NativeEntry != nullptr && NativeEntry->bNativeLeaf && NativeEntry->NativeCode != nullptr,
						Name))
				{
					return EXIT_FAILURE;
				}
			}
			#endif
		}

		const std::string Expected =
			"42\n7\n42\n5\n2\n-5\n9\n42\n9\n7\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n"
			"44\n7\n16\nab\n4\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "arith suite output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Div-by-zero must fail on all backends.
		const std::string Source =
			"int div(int a, int b) { return a / b; }\n"
			"void main() { print(div(1, 0)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile div0"))
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
			if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script), "div0 must fail"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Overflow must fail on all backends (JIT uses CheckedAdd).
		const std::string Source =
			"int add(int a, int b) { return a + b; }\n"
			"void main() { print(add(2147483647, 1)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile overflow"))
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
			if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script), "overflow must fail"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Restricted control flow (if / while) should compile to an I32 native CFG leaf.
		const std::string Source =
			"int abs(int x) {\n"
			"\tif (x < 0) { return -x; }\n"
			"\treturn x;\n"
			"}\n"
			"int sum(int n) {\n"
			"\tint s = 0;\n"
			"\tint i = 0;\n"
			"\twhile (i < n) {\n"
			"\t\ts = s + i;\n"
			"\t\ti = i + 1;\n"
			"\t}\n"
			"\treturn s;\n"
			"}\n"
			"void main() {\n"
			"\tprint(abs(-7));\n"
			"\tprint(abs(3));\n"
			"\tprint(sum(5));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile cfg abs/sum"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "abs", VeroScript::AsmJit::EJitCompileStatus::Ok, "abs cfg Ok"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectStatus(Cache, *Module, "sum", VeroScript::AsmJit::EJitCompileStatus::Ok, "sum cfg Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : {"abs", "sum"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Name))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "7\n3\n10\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "cfg abs/sum output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Linear basic-block: multi-statement int/bool with stores + bitwise, no jumps.
		const std::string Source =
			"int mix(int a, int b) {\n"
			"\tint t = a + b;\n"
			"\tt = t * 2;\n"
			"\tt = t - 1;\n"
			"\treturn t & 255;\n"
			"}\n"
			"int band(int a, int b) { return a & b; }\n"
			"int bor(int a, int b) { return a | b; }\n"
			"void main() {\n"
			"\tprint(mix(10, 5));\n"
			"\tprint(band(12, 10));\n"
			"\tprint(bor(1, 2));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile linear mix"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "mix", VeroScript::AsmJit::EJitCompileStatus::Ok, "mix linear Ok"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectStatus(Cache, *Module, "band", VeroScript::AsmJit::EJitCompileStatus::Ok, "band Ok"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectStatus(Cache, *Module, "bor", VeroScript::AsmJit::EJitCompileStatus::Ok, "bor Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : {"mix", "band", "bor"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Name))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "29\n8\n3\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "linear mix output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Nested Call + Abs/Min/Max builtins should JIT (including recursion).
		const std::string Source =
			"int twice(int x) { return x + x; }\n"
			"int go() { return twice(21); }\n"
			"int fib(int n) {\n"
			"\tif (n < 2) { return n; }\n"
			"\treturn fib(n - 1) + fib(n - 2);\n"
			"}\n"
			"int depth_sum(int n) {\n"
			"\tif (n < 1) { return n; }\n"
			"\treturn depth_sum(n - 1) + depth_sum(n - 1000);\n"
			"}\n"
			"uint32 count_down(uint32 n) {\n"
			"\tif (n == 0u) { return 0u; }\n"
			"\treturn 1u + count_down(n - 1u);\n"
			"}\n"
			"int abs_wrap(int x) { return abs(x); }\n"
			"int clamp(int x) { return min(max(x, 0), 10); }\n"
			"void main() {\n"
			"\tprint(go());\n"
			"\tprint(fib(10));\n"
			"\tprint(fib(-4));\n"
			"\tprint(depth_sum(200));\n"
			"\tprint(count_down(10u));\n"
			"\tprint(abs_wrap(-9));\n"
			"\tprint(clamp(15));\n"
			"\tprint(clamp(-3));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile nested call suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const char* const OkNames[] = {
			"twice", "go", "fib", "depth_sum", "count_down", "abs_wrap", "clamp"};
		for (const char* Name : OkNames)
		{
			if (!ExpectStatus(Cache, *Module, Name, VeroScript::AsmJit::EJitCompileStatus::Ok, Name))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : OkNames)
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Name))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "42\n55\n-4\n-179900\n10\n9\n10\n0\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "nested call output"))
			{
				return EXIT_FAILURE;
			}
		}

		std::ostringstream LimitedOutput;
		std::istringstream LimitedInput;
		std::ostringstream LimitedErrors;
		VeroScript::VeroScriptVm LimitedVm(
			LimitedOutput, LimitedInput, LimitedErrors);
		VeroScript::Test::Configure(LimitedVm);
		ConfigureBackend(LimitedVm);
		LimitedVm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		LimitedVm.SetMaxCallDepth(8);
		if (!Expect(!LimitedVm.Execute(*Module, VeroScript::ERunMode::Script)
				&& LimitedVm.GetError().MessageId == "diag.call_stack_overflow",
				"compact scalar recursion preserves the language call-depth limit"))
		{
			return EXIT_FAILURE;
		}

		const auto CompactOverflowModule = CompileSource(
			"int overflow_tree(int n) { if (n < 2147483647) return n; "
			"return overflow_tree(n - 1) + overflow_tree(n - 1); } "
			"void main() { print(overflow_tree(2147483647)); }");
		if (!Expect(static_cast<bool>(CompactOverflowModule),
				"compile compact scalar recursion overflow suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture =
				RunCapture(*CompactOverflowModule, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.integer_overflow",
					"compact scalar recursion preserves checked result overflow"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const std::string Source =
			"int recurse_bounded(int n) {\n"
			"  if (n == 0) { return 0; }\n"
			"  return 1 + recurse_bounded(n - 1);\n"
			"}\n"
			"void main() { print(recurse_bounded(2048)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile bounded JIT recursion suite")
			|| !ExpectRun(
				*Module,
				VeroScript::EExecutionBackend::Jit,
				"2048\n",
				"JIT recursion beyond native-stack threshold falls back safely"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
			"int recurse_forever(int n) { return recurse_forever(n + 1); }\n"
			"void main() { print(recurse_forever(0)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile JIT recursion-limit suite"))
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
		Vm.SetMaxCallDepth(512);
		if (!Expect(
				!Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"JIT infinite recursion should fail safely")
			|| !Expect(
				Vm.GetError().MessageId == "diag.call_stack_overflow",
				"JIT infinite recursion should report call stack overflow"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
			"int recurse_overflow(int n) {\n"
			"  if (n == 0) { return 2147483647; }\n"
			"  return recurse_overflow(n - 1) + 1;\n"
			"}\n"
			"void main() { print(recurse_overflow(8)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile direct-recursion error suite"))
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
		if (!Expect(
				!Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"direct JIT recursion must propagate arithmetic failure")
			|| !Expect(
				Vm.GetError().Message.find("integer overflow") != std::string::npos,
				"direct JIT recursion must preserve the inner error"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// print and other builtins go through the linear trampoline.
		const std::string Source =
			"void shout(string s) { print(s); }\n"
			"string greet(string name) { return \"hi \" + name; }\n"
			"void main() {\n"
			"\tshout(\"hi\");\n"
			"\tprint(greet(\"jit\"));\n"
			"\tprint(to_string(len(\"abc\")));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile shout"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(
				Cache,
					*Module,
				"shout",
				VeroScript::AsmJit::EJitCompileStatus::Ok,
				"print helper JITs via linear"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectStatus(Cache, *Module, "greet", VeroScript::AsmJit::EJitCompileStatus::Ok, "string concat Ok"))
		{
			return EXIT_FAILURE;
		}
		const std::string Expected = "hi\nhi jit\n3\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "string/builtin suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// if/else join + for-loop scalar bodies.
		const std::string Source =
			"int sign(int x) {\n"
			"\tint s = 0;\n"
			"\tif (x < 0) { s = -1; } else if (x > 0) { s = 1; } else { s = 0; }\n"
			"\treturn s;\n"
			"}\n"
			"int sum_for(int n) {\n"
			"\tint s = 0;\n"
			"\tfor (int i = 0; i < n; i = i + 1) {\n"
			"\t\ts = s + i;\n"
			"\t}\n"
			"\treturn s;\n"
			"}\n"
			"void main() {\n"
			"\tprint(sign(-2));\n"
			"\tprint(sign(0));\n"
			"\tprint(sign(5));\n"
			"\tprint(sum_for(5));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile if/for suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "sign", VeroScript::AsmJit::EJitCompileStatus::Ok, "sign Ok"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectStatus(Cache, *Module, "sum_for", VeroScript::AsmJit::EJitCompileStatus::Ok, "sum_for Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : {"sign", "sum_for"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Name))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "-1\n0\n1\n10\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "if/for output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// uint wrapping, double math builtins, scalar globals.
		const std::string Source =
			"int g = 1;\n"
			"uint uadd(uint a, uint b) { return a + b; }\n"
			"unsigned int wrap(unsigned int a) { return a + 1U; }\n"
			"double dmul(double a, double b) { return a * b; }\n"
			"double dsqrt(double x) { return __sqrt(x); }\n"
			"double dfloor(double x) { return __floor(x); }\n"
			"int bnot(int x) { return ~x; }\n"
			"int shl(int x, int n) { return x << n; }\n"
			"int bump_g() { g = g + 1; return g; }\n"
			"uint umin(uint a, uint b) { return min(a, b); }\n"
			"void main() {\n"
			"\tprint(uadd(3U, 4U));\n"
			"\tprint(wrap(4294967295U));\n"
			"\tprint(to_string(dmul(2.0, 3.5)));\n"
			"\tprint(to_string(dsqrt(9.0)));\n"
			"\tprint(to_string(dfloor(3.7)));\n"
			"\tprint(bnot(0));\n"
			"\tprint(shl(1, 4));\n"
			"\tprint(bump_g());\n"
			"\tprint(bump_g());\n"
			"\tprint(umin(8U, 3U));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile uint/double/global suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const char* const OkNames[] = {
			"uadd", "wrap", "dmul", "dsqrt", "dfloor", "bnot", "shl", "bump_g", "umin"};
		for (const char* Name : OkNames)
		{
			if (!ExpectStatus(Cache, *Module, Name, VeroScript::AsmJit::EJitCompileStatus::Ok, Name))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		const char* const NativeUintNames[] = {
			"uadd", "wrap", "umin", "bnot", "shl", "dmul", "dsqrt", "dfloor", "bump_g"};
		for (const char* Name : NativeUintNames)
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const NativeEntry = Cache.GetCandidate(Index);
			if (!Expect(
					NativeEntry != nullptr && NativeEntry->bNativeLeaf && NativeEntry->NativeCode != nullptr,
					Name))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "7\n0\n7\n3\n3\n-1\n16\n2\n3\n3\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "uint/double/global output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Cast builtins, uint peephole locals, ConstF64 leaf, extra math.
		const std::string Source =
			"int cast_i(double x) { return to_int(x); }\n"
			"double cast_d(int x) { return to_double(x); }\n"
			"unsigned int cast_u(int x) { return to_unsigned(x); }\n"
			"uint uacc(uint n) {\n"
			"\tuint s = 0U;\n"
			"\tuint i = 0U;\n"
			"\twhile (i < n) {\n"
			"\t\ts = s + i;\n"
			"\t\ti = i + 1U;\n"
			"\t}\n"
			"\treturn s;\n"
			"}\n"
			"uint umod(uint a, uint b) { return a % b; }\n"
			"uint uneg(uint x) { return -x; }\n"
			"uint ubnot(uint x) { return ~x; }\n"
			"double half() { return 0.5; }\n"
			"double dceil(double x) { return __ceil(x); }\n"
			"uint uabs(uint x) { return abs(x); }\n"
			"void main() {\n"
			"\tprint(cast_i(3.9));\n"
			"\tprint(to_string(cast_d(4)));\n"
			"\tprint(cast_u(5));\n"
			"\tprint(uacc(5U));\n"
			"\tprint(umod(10U, 3U));\n"
			"\tprint(uneg(1U));\n"
			"\tprint(ubnot(0U));\n"
			"\tprint(to_string(half()));\n"
			"\tprint(to_string(dceil(1.2)));\n"
			"\tprint(uabs(7U));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile cast/peephole suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const char* const OkNames[] = {
			"cast_i", "cast_d", "cast_u", "uacc", "umod", "uneg", "ubnot", "half", "dceil", "uabs"};
		for (const char* Name : OkNames)
		{
			if (!ExpectStatus(Cache, *Module, Name, VeroScript::AsmJit::EJitCompileStatus::Ok, Name))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		const char* const NativeU32Names[] = {
			"umod", "uneg", "ubnot", "half", "uacc", "uabs", "dceil",
			"cast_i", "cast_d", "cast_u"};
		for (const char* Name : NativeU32Names)
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const NativeEntry = Cache.GetCandidate(Index);
			if (!Expect(
					NativeEntry != nullptr && NativeEntry->bNativeLeaf && NativeEntry->NativeCode != nullptr,
					Name))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::uint16_t Half = FindFunctionIndex(*Module, "half");
		const VeroScript::AsmJit::FJitFunctionEntry* const HalfEntry = Cache.GetCandidate(Half);
		if (!Expect(
				HalfEntry != nullptr && HalfEntry->PayloadF64 == 0.5,
				"const f64 payload baked"))
		{
			return EXIT_FAILURE;
		}
		const std::string Expected = "3\n4\n5\n10\n1\n4294967295\n4294967295\n0.5\n2\n7\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "cast/peephole output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Nested Call through native CFG.
		const std::string Source =
			"int add1(int x) { return x + 1; }\n"
			"int twice(int x) { return add1(add1(x)); }\n"
			"void main() { print(twice(40)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile nested call"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "twice", VeroScript::AsmJit::EJitCompileStatus::Ok, "twice Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, "twice");
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					"twice native cfg"))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "42\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "nested call output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Homogeneous double/uint CFG: loops, nested calls, globals.
		const std::string Source =
			"double dadd1(double x) { return x + 1.0; }\n"
			"double dtwice(double x) { return dadd1(dadd1(x)); }\n"
			"double dlt(double a, double b) { if (a < b) { return 1.0; } return 0.0; }\n"
			"double dsum(double n) {\n"
			"\tdouble s = 0.0;\n"
			"\tdouble i = 0.0;\n"
			"\twhile (i < n) {\n"
			"\t\ts = s + i;\n"
			"\t\ti = i + 1.0;\n"
			"\t}\n"
			"\treturn s;\n"
			"}\n"
			"double g = 1.5;\n"
			"double bump_dg() { g = g + 1.0; return g; }\n"
			"uint uadd1(uint x) { return x + 1U; }\n"
			"uint utwice(uint x) { return uadd1(uadd1(x)); }\n"
			"void main() {\n"
			"\tprint(to_string(dtwice(20.0)));\n"
			"\tprint(to_string(dlt(1.0, 2.0)));\n"
			"\tprint(to_string(dlt(2.0, 1.0)));\n"
			"\tprint(to_string(dsum(5.0)));\n"
			"\tprint(to_string(bump_dg()));\n"
			"\tprint(utwice(40U));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile double cfg suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const char* const OkNames[] = {
			"dadd1", "dtwice", "dlt", "dsum", "bump_dg", "uadd1", "utwice"};
		for (const char* Name : OkNames)
		{
			if (!ExpectStatus(Cache, *Module, Name, VeroScript::AsmJit::EJitCompileStatus::Ok, Name))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : OkNames)
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Name))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "22\n1\n0\n10\n2.5\n42\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "double cfg output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Mixed scalar CFG: int/double locals, casts, calls.
		const std::string Source =
			"double dadd1(double x) { return x + 1.0; }\n"
			"int mixed_loop(int n) {\n"
			"\tdouble s = 0.0;\n"
			"\tint i = 0;\n"
			"\twhile (i < n) {\n"
			"\t\ts = s + to_double(i);\n"
			"\t\ti = i + 1;\n"
			"\t}\n"
			"\treturn to_int(s);\n"
			"}\n"
			"double mixed_branch(int x, double fallback) {\n"
			"\tif (x < 0) { return fallback; }\n"
			"\treturn to_double(x) + 0.5;\n"
			"}\n"
			"unsigned int mixed_uint(double x, int y) {\n"
			"\treturn to_unsigned(x) + to_unsigned(y);\n"
			"}\n"
			"double mixed_call(int x) { return dadd1(to_double(x)); }\n"
			"void main() {\n"
			"\tprint(mixed_loop(5));\n"
			"\tprint(to_string(mixed_branch(3, 9.0)));\n"
			"\tprint(to_string(mixed_branch(-1, 9.0)));\n"
			"\tprint(mixed_uint(2.0, 3));\n"
			"\tprint(to_string(mixed_call(41)));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile mixed cfg suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		const char* const OkNames[] = {
			"mixed_loop", "mixed_branch", "mixed_uint", "mixed_call", "dadd1"};
		for (const char* Name : OkNames)
		{
			if (!ExpectStatus(Cache, *Module, Name, VeroScript::AsmJit::EJitCompileStatus::Ok, Name))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		for (const char* Name : OkNames)
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Name))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "10\n3.5\n9\n5\n42\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "mixed cfg output"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Cast overflow must fail on all backends.
		const std::string Source =
			"void main() { print(to_int(3000000000.0)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile to_int overflow"))
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
			if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script), "to_int overflow must fail"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Class/struct methods: scalar this-fields compile to asmjit native leaves.
		const std::string Source =
			"class Counter {\n"
			"public:\n"
			"\tint n = 0;\n"
			"\tvoid inc() { n = n + 1; }\n"
			"\tvoid tick() { n++; }\n"
			"\tvoid add(int x) { n = n + x; }\n"
			"\tint get() { return n; }\n"
			"}\n"
			"struct Point {\n"
			"\tint x = 0;\n"
			"\tint y = 0;\n"
			"\tint sum() { return x + y; }\n"
			"}\n"
			"void main() {\n"
			"\tauto c = Counter();\n"
			"\tc.inc();\n"
			"\tc.tick();\n"
			"\tc.add(3);\n"
			"\tprint(c.get());\n"
			"\tauto p = Point();\n"
			"\tp.x = 3;\n"
			"\tp.y = 7;\n"
			"\tprint(p.sum());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile class/struct suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "inc", VeroScript::AsmJit::EJitCompileStatus::Ok, "Counter.inc Ok")
			|| !ExpectStatus(Cache, *Module, "tick", VeroScript::AsmJit::EJitCompileStatus::Ok, "Counter.tick Ok")
			|| !ExpectStatus(Cache, *Module, "add", VeroScript::AsmJit::EJitCompileStatus::Ok, "Counter.add Ok")
			|| !ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "Counter.get Ok")
			|| !ExpectStatus(Cache, *Module, "sum", VeroScript::AsmJit::EJitCompileStatus::Ok, "Point.sum Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : {"inc", "tick", "add", "get", "sum"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			const std::string Msg = std::string("native leaf ") + Name;
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Msg.c_str()))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "5\n10\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "class/struct suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Dynamic arrays + CallMethod (push) under Mixed/Jit linear / direct helpers.
		const std::string Source =
			"int sum_arr(array<int> xs) {\n"
			"\tint s = 0;\n"
			"\tfor (int v : xs) { s = s + v; }\n"
			"\treturn s;\n"
			"}\n"
			"array<int> make() {\n"
			"\tarray<int> a = array<int>();\n"
			"\ta.push(1);\n"
			"\ta.push(2);\n"
			"\ta.push(3);\n"
			"\treturn a;\n"
			"}\n"
			"void main() {\n"
			"\tprint(sum_arr(make()));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile array suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "sum_arr", VeroScript::AsmJit::EJitCompileStatus::Ok, "sum_arr Ok"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectStatus(Cache, *Module, "make", VeroScript::AsmJit::EJitCompileStatus::Ok, "make Ok"))
		{
			return EXIT_FAILURE;
		}
		const std::string Expected = "6\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "array suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Every numeric/bool primitive array uses direct push/get/set paths in the
		// linear tier; exact-width types must not fall back to cloned FValue IO.
		const std::string Source =
			"unsigned int get_u(array<unsigned int> xs) { return xs[0]; }\n"
			"double get_d(array<double> xs) { return xs[0]; }\n"
			"float get_f(array<float> xs) { return xs[0]; }\n"
			"int64 get_i64(array<int64> xs) { return xs[0]; }\n"
			"uint64 get_u64(array<uint64> xs) { return xs[0]; }\n"
			"int8 get_i8(array<int8> xs) { return xs[0]; }\n"
			"uint8 get_u8(array<uint8> xs) { return xs[0]; }\n"
			"int16 get_i16(array<int16> xs) { return xs[0]; }\n"
			"uint16 get_u16(array<uint16> xs) { return xs[0]; }\n"
			"bool get_b(array<bool> xs) { return xs[0]; }\n"
			"void main() {\n"
			"\tarray<unsigned int> u = array<unsigned int>();\n"
			"\tu.push(7U);\n"
			"\tprint(get_u(u));\n"
			"\tarray<double> d = array<double>();\n"
			"\td.push(1.5);\n"
			"\tprint(to_string(get_d(d)));\n"
			"\tarray<float> f = array<float>(); f.push(2.5f); f[0] = 3.5f;\n"
			"\tprint(get_f(f) == 3.5f);\n"
			"\tarray<int64> i64 = array<int64>(); i64.push(9i64);\n"
			"\tprint(get_i64(i64) == 9i64);\n"
			"\tarray<uint64> u64 = array<uint64>(); u64.push(10u64);\n"
			"\tprint(get_u64(u64) == 10u64);\n"
			"\tarray<int8> i8 = array<int8>(); int8 vi8 = 11; i8.push(vi8);\n"
			"\tprint(get_i8(i8) == vi8);\n"
			"\tarray<uint8> u8 = array<uint8>(); uint8 vu8 = 12; u8.push(vu8);\n"
			"\tprint(get_u8(u8) == vu8);\n"
			"\tarray<int16> i16 = array<int16>(); int16 vi16 = 13; i16.push(vi16);\n"
			"\tprint(get_i16(i16) == vi16);\n"
			"\tarray<uint16> u16 = array<uint16>(); uint16 vu16 = 14; u16.push(vu16);\n"
			"\tprint(get_u16(u16) == vu16);\n"
			"\tarray<bool> b = array<bool>(); b.push(true);\n"
			"\tprint(get_b(b));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile typed array fast-path suite"))
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
				"typed array fast-path execution")
					|| !Expect(Output.str()
						== "7\n1.5\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n",
						"typed array fast-path output"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::FJitStats Stats = Vm.GetJitStats();
		if (!Expect(Stats.ArrayFastPathPushes >= 10,
				"typed array scalar pushes use the linear fast path")
			|| !Expect(Stats.ArrayFastPathGets >= 10,
				"typed array scalar reads use the linear fast path")
			|| !Expect(Stats.ArrayFastPathSets >= 1,
				"typed array scalar writes use the linear fast path")
			|| !Expect(Stats.LinearTrampolineEntries >= 1,
				"typed array creator uses ownership-aware LinearTrampoline"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Repeated ownership-aware calls exercise recycled linear frames and stacks.
		// Every array must still release exactly once between scratch leases.
		const std::string Source =
			"int once(int seed) {\n"
			"\tarray<int> values = array<int>();\n"
			"\tvalues.push(seed);\n"
			"\tvalues.push(seed + 1);\n"
			"\treturn values[1];\n"
			"}\n"
			"void main() {\n"
			"\tint total = 0; int i = 0;\n"
			"\twhile (i < 512) { total = total + once(i); i++; }\n"
			"\tprint(total);\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile recycled linear scratch suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "131328\n", "recycled linear scratch semantics"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Rooted scalar array/set/string operations use Linear fusion without
		// changing errors, checked arithmetic, chaining, or exact-width semantics.
		const std::string Source =
			"bool array_ops() {\n"
			"\tarray<int> values = array<int>(); values.push(3);\n"
			"\tint key = 0; int total = 0;\n"
			"\tfor (int i = 0; i < 4; i++) { total += values[key]; }\n"
			"\tint replacement = 9; values[key] = replacement;\n"
			"\treturn total == 12 && values[key] == replacement;\n"
			"}\n"
			"bool array_f64_ops() {\n"
			"\tarray<double> values = array<double>(); values.push(1.5);\n"
			"\tint key = 0; double total = 0.0;\n"
			"\tfor (int i = 0; i < 4; i++) { total += values[key]; }\n"
			"\tdouble replacement = 2.5; values[key] = replacement;\n"
			"\treturn total == 6.0 && values[key] == replacement;\n"
			"}\n"
			"bool array_handle_swap_loop() {\n"
			"\tarray<int> a = {1}; array<int> b = {2};\n"
			"\tfor (int i = 0; i < 3; i++) { array<int> tmp = a; a = b; b = tmp; }\n"
			"\treturn a[0] == 2 && b[0] == 1;\n"
			"}\n"
			"bool set_ops() {\n"
			"\tset<int> i32 = set<int>(); int a = 7; i32.add(a);\n"
			"\tset<unsigned int> u32 = set<unsigned int>(); unsigned int b = 8u; u32.add(b);\n"
			"\tset<int64> i64 = set<int64>(); int64 c = 9i64; i64.add(c);\n"
			"\tset<uint64> u64 = set<uint64>(); uint64 d = 10u64; u64.add(d);\n"
			"\tset<string> strings = set<string>(); string word = \"value\"; strings.add(word);\n"
			"\tstring erasedWord = \"erased\"; strings.add(erasedWord); "
			"strings.remove(word); strings.add(word); strings.remove(erasedWord);\n"
			"\tbool found = i32.has(a) && u32.has(b) && i64.has(c) && u64.has(d)\n"
			"\t\t&& strings.has(word) && !strings.has(erasedWord);\n"
			"\treturn found && i32.remove(a) && !i32.has(a);\n"
			"}\n"
			"bool string_map_ops() {\n"
			"\tmap<string,int> values = map<string,int>(); values.reserve(4);\n"
			"\tvalues[\"alpha\"] = 3; values[\"erased\"] = 5; "
			"values.remove(\"alpha\"); values[\"later\"] = 7; values[\"alpha\"] = 11;\n"
			"\treturn values.has(\"alpha\") && !values.has(\"missing\") "
			"&& values.remove(\"erased\") && !values.has(\"erased\") "
			"&& values[\"alpha\"] + values[\"later\"] == 18;\n"
			"}\n"
			"bool string_comparisons() {\n"
			"\tstring a = \"alpha\"; string same = \"alpha\"; string later = \"beta\";\n"
			"\treturn a == same && a != later;\n"
			"}\n"
			"bool string_swap_loop() {\n"
			"\tstring a = \"alpha\"; string b = \"beta\";\n"
			"\tfor (int i = 0; i < 3; i++) { string tmp = a; a = b; b = tmp; }\n"
			"\treturn a == \"beta\" && b == \"alpha\";\n"
			"}\n"
			"void main() { print(array_ops()); print(array_f64_ops()); "
			"print(array_handle_swap_loop()); print(set_ops()); print(string_map_ops()); "
			"print(string_comparisons()); "
			"print(string_swap_loop()); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile scalar container fusion suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend,
					"true\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n",
					"scalar container fusion parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		{
			// Keep the delayed-token commit regression tied to the native typed-loop
			// path. A semantic-only VM fallback would not exercise the slot-order bug.
			const auto SwapModule = CompileSource(
				"bool swap_arrays(int limit) { array<int> a = {1}; array<int> b = {2}; "
				"int total = 0; for (int i = 0; i < limit; i++) { "
				"array<int> t = a; a = b; b = t; total += a[0]; } "
				"return total == 15002 && a[0] == 2 && b[0] == 1; }\n"
				"bool swap_strings(int limit) { string a = \"alpha\"; string b = \"beta\"; "
				"int total = 0; for (int i = 0; i < limit; i++) { "
				"string t = a; a = b; b = t; if (a == \"beta\") total++; } "
				"return total == 5001 && a == \"beta\" && b == \"alpha\"; }\n"
				"void main() { print(swap_arrays(10001)); print(swap_strings(10001)); }\n");
			std::ostringstream Output;
			std::istringstream Input;
			std::ostringstream Errors;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
			if (!Expect(static_cast<bool>(SwapModule),
					"compile delayed native alias commit regression")
				|| !Expect(Vm.Execute(*SwapModule, VeroScript::ERunMode::Script),
					"execute delayed native alias commit regression")
				|| !Expect(Output.str() == "true\ntrue\n",
					"native alias swaps preserve simultaneous source values")
				|| !Expect(Vm.GetJitStats().NativeTypedLoopExecutions >= 1u,
					"array swap executes the native typed loop"))
			{
				std::cerr << Errors.str() << Vm.GetError().Message
					<< " native_typed_loops="
					<< Vm.GetJitStats().NativeTypedLoopExecutions << '\n';
				return EXIT_FAILURE;
			}
		}
		#endif

		const std::string BoundsSource =
			"int invalid_array_read() { array<int> a = array<int>(); a.push(1); "
			"int total = 0; total += a[1]; return total; }\n"
			"void main() { print(invalid_array_read()); }\n";
		const std::string OverflowSource =
			"int overflow_array_read() { array<int> a = array<int>(); a.push(1); "
			"int total = 2147483647; total += a[0]; return total; }\n"
			"void main() { print(overflow_array_read()); }\n";
		const std::string WriteBoundsSource =
			"int invalid_array_write() { array<int> a = array<int>(); a.push(1); "
			"int value = 2; a[1] = value; return a[0]; }\n"
			"void main() { print(invalid_array_write()); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> BoundsModule =
			CompileSource(BoundsSource);
		std::unique_ptr<VeroScript::FBytecodeModule> OverflowModule =
			CompileSource(OverflowSource);
		std::unique_ptr<VeroScript::FBytecodeModule> WriteBoundsModule =
			CompileSource(WriteBoundsSource);
		if (!Expect(static_cast<bool>(BoundsModule),
				"compile scalar array bounds suite")
			|| !Expect(static_cast<bool>(OverflowModule),
				"compile scalar array overflow suite")
			|| !Expect(static_cast<bool>(WriteBoundsModule),
				"compile scalar array write bounds suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Bounds = RunCapture(*BoundsModule, Backend);
			if (!Expect(!Bounds.bSuccess
					&& Bounds.ErrorMessage.find("array index out of bounds")
						!= std::string::npos,
					"scalar array bounds parity"))
			{
				return EXIT_FAILURE;
			}
			const FExecutionCapture Overflow = RunCapture(*OverflowModule, Backend);
			if (!Expect(!Overflow.bSuccess
					&& Overflow.ErrorMessageId == "diag.integer_overflow",
					"scalar array overflow parity"))
			{
				return EXIT_FAILURE;
			}
			const FExecutionCapture WriteBounds =
				RunCapture(*WriteBoundsModule, Backend);
			if (!Expect(!WriteBounds.bSuccess
					&& WriteBounds.ErrorMessage.find("array index out of bounds")
						!= std::string::npos,
					"scalar array write bounds parity"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// A hand-authored module can contain an ordering opcode that normal source
		// semantics reject. Linear local comparison fusion must preserve the VM's
		// runtime type diagnostic instead of ordering bool/string alternatives.
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(
			"void main() { string a = \"a\"; string b = \"b\"; "
			"if (a == b) { print(true); } }\n");
		if (!Expect(static_cast<bool>(Module),
				"compile malformed local ordering fixture"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t MainIndex = FindFunctionIndex(*Module, "main");
		if (!Expect(MainIndex != static_cast<std::uint16_t>(-1),
				"find malformed local ordering fixture"))
		{
			return EXIT_FAILURE;
		}
		auto& Code = Module->Functions[MainIndex].Code;
		bool bReplaced = false;
		for (std::size_t Ip = 0; Ip < Code.size();)
		{
			const VeroScript::EOpcode Op = static_cast<VeroScript::EOpcode>(Code[Ip]);
			if (Op == VeroScript::EOpcode::Eq)
			{
				Code[Ip] = static_cast<std::uint8_t>(VeroScript::EOpcode::Ge);
				bReplaced = true;
				break;
			}
			Ip += VeroScript::Bytecode::InstructionSize(Op);
		}
		if (!Expect(bReplaced, "patch malformed local ordering fixture"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			const bool bExpectedError = !Capture.bSuccess
				&& Capture.ErrorMessage.find(
					"comparison operands must be numeric")
					!= std::string::npos;
			if (!Expect(bExpectedError,
					"malformed local ordering keeps VM diagnostic"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " success=" << Capture.bSuccess
					<< " message=" << Capture.ErrorMessage
					<< " errors=" << Capture.Errors << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	{
		// More than eight arguments use a pooled secondary scratch buffer. Repeated
		// calls must reuse both the frame and argument storage without changing
		// argument order or ownership semantics. The global read intentionally makes
		// sum10 non-inlineable so this remains a bridge/scratch test now that pure
		// scalar leaves may be expanded inside ownership-aware Linear callers.
		const std::string Source =
			"int sum_bias = 0;\n"
			"int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {\n"
			"\treturn a + b + c + d + e + f + g + h + i + j + sum_bias;\n"
			"}\n"
			"int once(int seed) {\n"
			"\tarray<int> values = array<int>();\n"
			"\tvalues.push(seed);\n"
			"\treturn sum10(seed, seed + 1, seed + 2, seed + 3, seed + 4, "
				"seed + 5, seed + 6, seed + 7, seed + 8, seed + 9) + values[0];\n"
			"}\n"
			"void main() {\n"
			"\tint total = 0; int i = 0;\n"
			"\twhile (i < 256) { total = total + once(i); i++; }\n"
			"\tprint(total);\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile high-arity scratch suite"))
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
				"high-arity scratch execution")
			|| !Expect(Output.str() == "370560\n", "high-arity scratch output"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::FJitStats Stats = Vm.GetJitStats();
		if (!Expect(Stats.LinearArgumentScratchLeases >= 256,
				"high-arity calls lease argument scratch")
			|| !Expect(Stats.LinearArgumentScratchReuses >= 255,
				"high-arity calls reuse argument scratch")
			|| !Expect(Stats.LinearFrameReuses >= 255,
				"repeated linear calls reuse frames"))
		{
			return EXIT_FAILURE;
		}
		Vm.ResetJitStats();
		const VeroScript::FJitStats ResetStats = Vm.GetJitStats();
		if (!Expect(ResetStats.LinearFrameReuses == 0
				&& ResetStats.LinearScratchReuses == 0
				&& ResetStats.LinearScratchAllocations == 0
				&& ResetStats.LinearArgumentScratchLeases == 0
				&& ResetStats.LinearArgumentScratchReuses == 0,
			"linear scratch counters reset"))
		{
			return EXIT_FAILURE;
		}
	}
