// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Exercise ownership independently of native availability. Every assigned token
// must root the frame's actual string, including SSO and heap-backed values.

{
	const auto Module = CompileSource(
		"string build(string value,string suffix){for(int i=0;i<4;i++)value+=suffix;return value;}"
		"string grow(int count,string value,string suffix){for(int i=0;i<count;i++)value+=suffix;return value;}"
		"string preserve(int count,string value,string suffix){string saved=value;"
		"for(int i=0;i<count;i++)value+=suffix;return saved+\"|\"+value;}"
		"string self(string value){for(int i=0;i<4;i++)value+=value;return value;}"
		"string wide_grow(int64 count,string value,string suffix){for(int64 i=0i64;i<count;i++)value+=suffix;return value;}"
		"int64 lengths(int count,string value,string suffix){int64 sum=0i64;"
		"for(int i=0;i<count;i++){value+=suffix;sum+=len(value);}return sum;}"
		"wstring wide_text(wstring value,wstring suffix){for(int i=0;i<4;i++)value+=suffix;return value;}"
		"void main(){string initial=\"abcdefghijklmnopqrstuvwxyz0123456789\";"
		"print(build(\"a\",\"xy\"));print(grow(0,\"a\",\"z\"));print(grow(1,\"a\",\"\"));"
		"print(grow(31,initial,\"0123456789abcdef\"));print(preserve(7,initial,\"xy\"));"
		"print(self(\"a\"));print(wide_grow(7i64,\"a\",\"bc\"));print(lengths(7,\"a\",\"xy\"));"
		"print(initial);print(wide_text(L\"A\",L\"BC\"));}");
	if (!Expect(static_cast<bool>(Module), "compile compact string-parameter appends without loop literals")) return EXIT_FAILURE;
	const std::string Initial = "abcdefghijklmnopqrstuvwxyz0123456789";
	std::string Large = Initial;
	for (int I = 0; I < 31; ++I)
	{
		Large += "0123456789abcdef";
	}
	const auto Expected = std::string("axyxyxyxy\na\na\n") + Large + '\n' + Initial + '|' + Initial +
		"xyxyxyxyxyxyxy\n" + std::string(16u, 'a') + "\nabcbcbcbcbcbcbc\n63\n" + Initial + "\nABCBCBCBC\n";
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Module, Backend);
		if (!Expect(Capture.bSuccess && Capture.Output == Expected,
				"compact string appends preserve zero trips, parameter copies, SSO, heap growth, self aliases and wide counters"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.NativeTypedLoopExecutions != 0u,
																 "string parameters and compact append bytecode reach native text kernels"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	for (const auto Name : {"build", "grow", "preserve", "self", "wide_grow", "lengths"})
	{
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(Entry && !Entry->bPreferVmForExecution && Entry->TypedLoopPlans &&
				std::any_of(Entry->TypedLoopPlans->Plans.begin(), Entry->TypedLoopPlans->Plans.end(), [](const auto& Plan)
				{return (Plan.NativeI32Code || Plan.NativeWideCode) && std::any_of(Plan.Instructions.begin(), Plan.Instructions.end(),
					[](const auto& I) {return I.Opcode == VeroScript::EOpcode::AddLocalLocal && I.bTextAdd;});}),
				"verified compact UTF-8 append retains complete native coverage"))
		{
			std::cerr << "function=" << Name << " entry=" << static_cast<bool>(Entry)
				<< " preferVM=" << (Entry && Entry->bPreferVmForExecution)
				<< " plans=" << (Entry && Entry->TypedLoopPlans ? Entry->TypedLoopPlans->Plans.size() : 0u) << '\n';
			return EXIT_FAILURE;
		}
	}
	const auto* Compact = Cache.GetCandidate(FindFunctionIndex(*Module, "build"));
	if (!Expect(std::any_of(Compact->TypedLoopPlans->Plans.begin(), Compact->TypedLoopPlans->Plans.end(),
					[](const auto &Plan)
					{
						return Plan.MaxStackDepth == 0u && Plan.NativeI32Code;
					}),
			"stack-neutral compact append and immediate loop guard form a valid native kernel"))
	{
		return EXIT_FAILURE;
	}
	const auto* Wide = Cache.GetCandidate(FindFunctionIndex(*Module, "wide_text"));
	if (!Expect(Wide && Wide->bPreferVmForExecution, "unsupported wstring compact appends retain their VM cost choice")) return EXIT_FAILURE;
	#endif
}

{
	const auto Unsafe = CompileSource("string nested(string value,string suffix){for(int i=0;i<4;i++)value=value+(value=suffix);return value;}"
		"void main(){print(nested(\"a\",\"bc\"));}");
	if (!Expect(static_cast<bool>(Unsafe), "compile parameter-based nested string assignment")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Unsafe, Backend);
		if (!Expect(Capture.bSuccess && Capture.Output == "abcbcbcbc\n" && Capture.JitStats.NativeTypedLoopExecutions == 0u,
				"parameter-based nested assignment declines before borrowed operand snapshots can be changed"))
		{
			return EXIT_FAILURE;
		}
	}
	const auto Failure = CompileSource("int fail(string value,string suffix,int64 index){int sum=0;"
		"for(int i=0;i<4;i++){value+=suffix;sum+=__char_code(value,index);}return sum;}"
		"void main(){print(fail(\"a\",\"bc\",4294967296i64));}");
	if (!Expect(static_cast<bool>(Failure), "compile failing compact append followed by wide character lookup")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Failure, VeroScript::EExecutionBackend::Vm);
	if (!Expect(!Reference.bSuccess && !Reference.ErrorMessage.empty(), "VM reports later wide text failure")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Failure, Backend);
		if (!Expect(!Capture.bSuccess && Capture.Output == Reference.Output && Capture.ErrorMessageId == Reference.ErrorMessageId &&
						Capture.ErrorLine == Reference.ErrorLine && Capture.ErrorColumn == Reference.ErrorColumn,
				"compact appends preserve the original subsequent wide text error location"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.NativeTypedLoopExecutions != 0u,
																 "compact append error fixture actually enters the wide native loop"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
}

{
	// An earlier operand is a language value snapshot. A nested string write
	// must not change that operand through the native borrowed local view.
	struct FSnapshotCase { const char* Initial; const char* Expression; const char* Suffix; };
	for (const auto& Case : {FSnapshotCase{"a", "s=s+(s=\"b\");", "b"},
		FSnapshotCase{"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", "s=s+(s=\"b\");", "b"},
		FSnapshotCase{"a", "s=s+(other=\"z\");", "z"},
		FSnapshotCase{"abcdefghijklmnopqrstuvwx", "s=s+((i&1)==0?(s=\"b\"):(s=\"c\"));", nullptr}})
	{
		const std::string Source = std::string("string work(int n){int seen=0;string s=\"") + Case.Initial +
			"\";string other=\"q\";for(int i=0;i<n;i++){seen++;" + Case.Expression +
			"}return to_string(seen)+\":\"+s;}void main(){print(work(0));print(work(1));print(work(7));}";
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile nested string assignment snapshot fixture")) return EXIT_FAILURE;
		std::string Expected;
		for (const int Count : {0, 1, 7})
		{
			Expected += std::to_string(Count) + ":" + Case.Initial;
			for (int I = 0; I < Count; ++I)
			{
				Expected += Case.Suffix ? Case.Suffix : (I & 1) == 0 ? "b"
																	 : "c";
			}
			Expected += '\n';
		}
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			const auto Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output == Expected,
					"nested string stores preserve short and heap operand snapshots, CFG joins and exactly-once preceding increments"))
			{
				return EXIT_FAILURE;
			}
			#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
			if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.NativeTypedLoopExecutions == 0u,
																	 "live borrowed string operands decline owning native stores before any source mutation"))
			{
				return EXIT_FAILURE;
			}
			#endif
		}
	}
	const auto Self = CompileSource("string work(int n){string s=\"a\";for(int i=0;i<n;i++){s=s+s;}return s;}"
		"void main(){print(work(0));print(work(1));print(work(7));}");
	if (!Expect(static_cast<bool>(Self), "compile ordinary self-concatenation snapshot fixture")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Self, Backend, std::string("a\naa\n") + std::string(128u, 'a') + '\n',
				"ordinary self concatenation preserves both consumed snapshots"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	using namespace VeroScript;
	using namespace VeroScript::AsmJit::JitLinear;
	using K = ETypedLoopLocalKind;
	constexpr std::uint32_t Offset = 0x12345678u;
	std::array<FValue, 2> Frame{std::string("first"), std::string("second")};
	std::array<FTypedLoopNativeBinding, 2> Bindings{};
	std::array<FTypedLoopNativeStringView, 2> LocalViews{};
	std::array<std::string, 2> Temporaries{};
	std::array<FTypedLoopNativeStringView, 2> TemporaryViews{};
	std::array<K, 2> InstructionKinds{K::I32, K::I32};
	std::array<std::uint64_t, 2> Locals{};
	for (std::size_t Index = 0; Index < Frame.size(); ++Index)
	{
		Bindings[Index].Kind = K::String;
		Bindings[Index].Storage = std::get_if<std::string>(&Frame[Index]);
	}
	FTypedLoopNativeStringScratch Strings{Temporaries.data(), TemporaryViews.data(),
	    LocalViews.data(), Temporaries.size(), LocalViews.size()};
	FTypedLoopNativeContext Context;
	Context.Bindings = Bindings.data();
	Context.InstructionKinds = InstructionKinds.data();
	Context.Locals = Locals.data();
	Context.Strings = &Strings;
	const auto CheckFrameView = [&](const std::size_t Slot, const std::string_view Expected)
	{
		const auto& Value = std::get<std::string>(Frame[Slot]);
		const auto& View = LocalViews[Slot];
		return Value == Expected && View.Value == &Value && View.Data == Value.data() && View.Size == Value.size() && View.Hash == std::hash<std::string>{}(Value) && Locals[Slot] == (reinterpret_cast<std::uint64_t>(&View) | TypedLoopNativeStringTag);
	};
	const auto Store = [&](const std::uint32_t Slot, const std::uint64_t Token)
	{
		Context.FailureMessage = "previous error";
		if (TypedLoopNativeStoreStringLocal(&Context, Slot, Token, Offset) != 0 || Context.FailureMessage || Context.FailureOffset != Offset)
		{
			return false;
		}
		Locals[Slot] = Context.Scratch;
		return true;
	};
	const auto CheckConversion = [&](const K Kind, const std::uint64_t Bits, const char* Expected)
	{
		InstructionKinds[0] = Kind;
		Context.FailureMessage = "previous error";
		if (TypedLoopNativeToString(&Context, 0, Bits, Offset) != 0 || Context.FailureMessage || Context.FailureOffset != Offset || !Store(0, Context.Scratch) || !Store(1, Locals[0]) || !CheckFrameView(0, Expected) || !CheckFrameView(1, Expected))
		{
			return false;
		}
		// Reuse the exact same conversion site and mutate its first destination.
		// A copied local must keep its prior value across both operations.
		InstructionKinds[0] = K::I32;
		if (TypedLoopNativeToString(&Context, 0, 7u, Offset) != 0 || !Store(0, Context.Scratch) || !CheckFrameView(0, "7") || !CheckFrameView(1, Expected))
		{
			return false;
		}
		InstructionKinds[1] = Kind;
		return TypedLoopNativeToString(&Context, 1, Bits, Offset) == 0 && Temporaries[0] == "7" && Temporaries[1] == Expected && CheckFrameView(1, Expected);
	};
	if (!Expect(CheckConversion(K::I32, static_cast<std::uint64_t>(INT32_MIN), "-2147483648") && CheckConversion(K::I32, INT32_MAX, "2147483647") && CheckConversion(K::U32, UINT32_MAX, "4294967295") && CheckConversion(K::I64, static_cast<std::uint64_t>(INT64_MIN), "-9223372036854775808") && CheckConversion(K::I64, INT64_MAX, "9223372036854775807") && CheckConversion(K::U64, UINT64_MAX, "18446744073709551615"),
	        "integer conversion and string local ownership preserve full widths and independent copies"))
	{
		return EXIT_FAILURE;
	}
	Context.Scratch = 77;
	InstructionKinds[0] = K::Bool;
	if (!Expect(TypedLoopNativeToString(&Context, 0, 1u, Offset) != 0 && Context.Scratch == 77 && Context.FailureOffset == Offset && Context.FailureMessage && TypedLoopNativeToString(&Context, 2, 1u, Offset) != 0 && Context.Scratch == 77 && TypedLoopNativeStoreStringLocal(&Context, 2, Locals[0], Offset) != 0 && TypedLoopNativeStoreStringLocal(&Context, 0, 0u, Offset) != 0 && Context.Scratch == 77 && CheckFrameView(0, "7"),
	        "invalid string scratch, conversion types and destinations fail without committing"))
	{
		return EXIT_FAILURE;
	}
}

{
	const auto Module = CompileSource(
	    "int stringMapLoop(int count, int seed) { map<string,int> values = map<string,int>(); "
	    "for (int i = 0; i < 31; i++) values[to_string(i)] = i; "
	    "int state = seed; int total = 0; for (int i = 0; i < count; i++) { "
	    "state = (state * 75 + 74) % 65521; string key = to_string(state % 31); "
	    "int value = (values[key] + state) % 1009; values[key] = value; total += value; "
	    "} return total; } void main() { print(stringMapLoop(500,17)); }");
	if (!Expect(static_cast<bool>(Module), "compile string conversion map loop")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Module, Backend);
		if (!Expect(Capture.bSuccess && Capture.Output == "249292\n",
		        "map keys produced by integer conversion preserve lookup and insertion semantics"))
		{
			std::cerr << "backend=" << static_cast<int>(Backend) << " output=[" << Capture.Output
				<< "] errors=[" << Capture.Errors << "] error=" << Capture.ErrorMessage
				<< " at " << Capture.ErrorFile << ':' << Capture.ErrorLine << ':' << Capture.ErrorColumn << '\n';
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		// A single cold Mixed call may intentionally stay below its heat threshold.
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.NativeTypedLoopExecutions != 0u, "integer string conversion admits the complete native map loop"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
}

{
	const auto Module = CompileSource(
	    "int formatContainers() { map<string,int> values = map<string,int>(); set<int> unique = set<int>(); "
	    "for (int i = 0; i < 31; i++) values[to_string(i)] = i; "
	    "for (int i = 0; i < 31; i++) unique.add(i); "
	    "if (to_string(values) != \"map<string,int>[31]\") return 1; "
	    "if (to_string(unique) != \"set<int>[31]\") return 2; return 0; } "
	    "void main() { print(formatContainers()); }");
	if (!Expect(static_cast<bool>(Module), "compile container formatting after native conversion loops")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Module, Backend);
		if (!Expect(Capture.bSuccess && Capture.Output == "0\n",
		        "container text snapshots retain scalar map and set lengths after native writes"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.NativeTypedLoopExecutions != 0u, "scalar container formatting retains the native conversion loop"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
}

for (const std::string_view Source : {
         "int preserve(int n, int seed) { array<int> counts = {0}; "
         "string saved = \"initial\"; string last = \"initial\"; for (int i = 0; i < n; i++) { "
         "string current = to_string(seed + i); string copy = current; "
         "if (i == 0) saved = copy; last = current; "
         "if (current == to_string(seed + i)) counts[0] = counts[0] + 1; "
         "} print(saved); print(last); return counts[0]; } void main() { print(preserve(19,17)); }",
         "int preserve(int n, int64 value) { string first = \"initial\"; string last = \"initial\"; "
         "int total = 0; for (int i = 0; i < n; i++) { string current = to_string(value); "
         "if (i == 0) first = current; last = current; total += to_int(len(current)); value = value + 1; "
         "} print(first); print(last); return total; } "
         "void main() { print(preserve(3,to_int64(\"-9223372036854775808\"))); }",
         "int preserve(int n, uint64 value) { string first = \"initial\"; string last = \"initial\"; "
         "int total = 0; for (int i = 0; i < n; i++) { string current = to_string(value); "
         "if (i == 0) first = current; last = current; total += to_int(len(current)); value = value - to_uint64(1); "
         "} print(first); print(last); return total; } "
         "void main() { print(preserve(3,to_uint64(\"18446744073709551615\"))); }"})
{
	if (!ExpectDifferentialRun(Source,
	        "conversion temporaries and copied locals remain independent across iterations in every width"))
	{
		return EXIT_FAILURE;
	}
}

for (const std::string_view Source : {
         "int work(int count) { map<string,int> values = map<string,int>(); values[\"0\"] = 7; "
         "int total = 0; for (int i = 0; i < count; i++) { string key = to_string(i); "
         "total += values[key]; } return total; } void main() { print(work(3)); }",
         "int work(int count) { map<string,int> values = map<string,int>(); int total = 0; "
         "for (int i = 0; i < count; i++) { string key = to_string(i); values[key] = i; "
         "total += values[key] / (1-i); } return total; } void main() { print(work(3)); }",
         "void work(ref string result, int count) { map<string,int> values = map<string,int>(); "
         "for (int i = 0; i < count; i++) { result = to_string(i); values[result] = i; } } "
         "void main() { string result = \"initial\"; work(ref result,19); print(result); }",
         "int work(int count) { array<int> values = {0}; string saved = \"initial\"; "
         "for (int i = 0; i < count; i++) { string current = to_string(i); "
         "if (current == (saved = to_string(i))) values[0] += 1; } print(saved); return values[0]; } "
         "void main() { print(work(19)); }",
         "int work(int count, double value) { map<string,int> values = map<string,int>(); "
         "int total = 0; for (int i = 0; i < count; i++) { string key = to_string(value); "
         "values[key] = i; total += values[key]; value += 0.5; } return total; } "
         "void main() { print(work(19,1.25)); }"})
{
	if (!ExpectDifferentialRun(Source,
	        "conversion loops preserve failure locations, ref writeback and live string operands"))
	{
		return EXIT_FAILURE;
	}
}

{
	const auto Module = CompileSource(
	    "class TextState { public: string text = \"initial\"; }; "
	    "void work(ref string result, int count) { map<string,int> values = map<string,int>(); "
	    "for (int i = 0; i < count; i++) { result = to_string(i); values[result] = i; } } "
	    "void main() { TextState state = TextState(); work(ref state.text,19); print(state.text); }");
	if (!Expect(static_cast<bool>(Module), "compile observed ref string conversion loop")) return EXIT_FAILURE;
	std::size_t BaselineMutations = 0u;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		std::istringstream Input;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
		std::size_t Mutations = 0u;
		Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
		{
			if (!Event.Object || Event.FieldName != "text" || Event.FieldIndex >= Event.Object->Fields.size())
			{
				return;
			}
			Event.Object->Fields.reserve(Event.Object->Fields.capacity() + 7u);
			if (auto* Value = std::get_if<std::string>(&Event.Object->Fields[Event.FieldIndex]))
			{
				*Value += "!";
				++Mutations;
			}
		});
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str() == "18!\n" && Mutations >= 19u,
		        "ref string conversion writes preserve synchronous mutation observers"))
		{
			return EXIT_FAILURE;
		}
		if (Backend == VeroScript::EExecutionBackend::Vm)
		{
			BaselineMutations = Mutations;
		}
		else if (!Expect(Mutations == BaselineMutations,
		             "ref string conversion observers match VM write counts"))
		{
			return EXIT_FAILURE;
		}
	}
}

// The raw text boundary preserves byte semantics and the complete index width.
{
	using namespace VeroScript::AsmJit::JitLinear;
	using K = ETypedLoopLocalKind;
	std::string Text("A\0\xC3\xA9", 4u);
	FTypedLoopNativeStringView View{&Text, Text.data(), Text.size(), 0u};
	const auto Token = reinterpret_cast<std::uint64_t>(&View) | TypedLoopNativeStringTag;
	FTypedLoopNativeContext Context;
	for (const auto Kind : {K::I32, K::U32, K::I64, K::U64})
	{
		if (!Expect(TypedLoopNativeCharacterCode(&Context, Token, 1u, Kind, 37u) == 0 && Context.Scratch == 0u &&
						TypedLoopNativeCharacterCode(&Context, Token, 2u, Kind, 37u) == 0 && Context.Scratch == 195u &&
						TypedLoopNativeCharacterCode(&Context, Token, 3u, Kind, 37u) == 0 && Context.Scratch == 169u,
				"native char code retains embedded NUL and UTF8 byte values"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const auto Kind : {K::I64, K::U64})
	{
		Context.Scratch = 97u;
		if (!Expect(TypedLoopNativeCharacterCode(&Context, Token, UINT64_C(1) << 32u, Kind, 41u) != 0 &&
						Context.Scratch == 97u && Context.FailureOffset == 41u && Context.FailureMessage &&
						std::string_view(Context.FailureMessage) == "char index out of range",
				"native char code checks every index bit before narrowing"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!Expect(TypedLoopNativeCharacterCode(&Context, Token, UINT64_MAX, K::I64, 41u) != 0 &&
					TypedLoopNativeCharacterCode(&Context, Token, UINT32_MAX, K::I32, 41u) != 0,
			"native char code rejects negative signed indices"))
	{
		return EXIT_FAILURE;
	}
}

{
	using namespace VeroScript::AsmJit::JitLinear;
	using K = ETypedLoopLocalKind;
	std::array<std::string, 3> Values{"left", "right", "old"};
	std::array<FTypedLoopNativeStringView, 3> Views{};
	std::array<FTypedLoopNativeBinding, 3> Bindings{};
	std::array<std::uint8_t, 3> HashValid{1u, 1u, 1u};
	std::array<FTypedLoopInstruction, 2> Instructions{};
	std::array<K, 2> Kinds{K::U64, K::String};
	const auto Reset = [&]()
	{
		Values = {"left", "right", "old"};
		for (std::size_t I = 0; I < Values.size(); ++I)
		{
			Views[I] = {&Values[I], Values[I].data(), Values[I].size(), std::hash<std::string>{}(Values[I])};
			Bindings[I].Kind = K::String;
			Bindings[I].Storage = &Values[I];
			HashValid[I] = 1u;
		}
	};
	Reset();
	FTypedLoopNativeStringScratch Strings{};
	Strings.LocalViews = Views.data();
	Strings.LocalCount = Views.size();
	Strings.LocalHashValid = HashValid.data();
	Strings.Instructions = Instructions.data();
	Strings.InstructionCount = Instructions.size();
	FTypedLoopNativeContext Context;
	Context.Strings = &Strings;
	Context.Bindings = Bindings.data();
	Context.InstructionKinds = Kinds.data();
	const auto Token = [&](const std::size_t I)
	{
		return reinterpret_cast<std::uint64_t>(&Views[I]) | TypedLoopNativeStringTag;
	};
	Instructions[1].bTextConcatStore = true;
	Instructions[1].Immediate = 99;
	for (std::uint16_t Target = 0; Target < 3u; ++Target)
	{
		Reset();
		Instructions[1].Operand0 = Target;
		if (!Expect(TypedLoopNativeStoreText(&Context, 1u, Token(0u), Token(1u)) == 0 &&
						Values[Target] == "leftright" && !HashValid[Target] &&
						NativeTextHash(Context, Views[Target]) == std::hash<std::string>{}(Values[Target]) && HashValid[Target],
				"native concat stores retain both input snapshots and lazy hashes for either alias"))
		{
			return EXIT_FAILURE;
		}
	}
	Reset();
	Instructions[1].Operand0 = 0u;
	if (!Expect(TypedLoopNativeStoreText(&Context, 1u, Token(0u), Token(0u)) == 0 && Values[0] == "leftleft",
			"native concat supports self append across destination growth"))
	{
		return EXIT_FAILURE;
	}
	Reset();
	Instructions[1].TextFormatInstruction = 0u;
	Instructions[1].Operand0 = 2u;
	if (!Expect(TypedLoopNativeStoreText(&Context, 1u, Token(0u), UINT64_MAX) == 0 && Values[2] == "left18446744073709551615" && Values[0] == "left",
			"native concat formats the full unsigned width directly into its frame destination"))
	{
		return EXIT_FAILURE;
	}
	Reset();
	Values[0] = std::string(47u, 'x') + std::string("\0end", 4u);
	Views[0] = {&Values[0], Values[0].data(), Values[0].size(), 0u};
	Instructions[1].TextFormatInstruction = 0xFFFFu;
	Instructions[1].Operand0 = 0u;
	if (!Expect(TypedLoopNativeStoreStringLocal(&Context, 2u, Token(0u), 100u) == 0,
			"copy text snapshot before destination growth"))
	{
		return EXIT_FAILURE;
	}
	const std::string Snapshot = Values[2];
	if (!Expect(TypedLoopNativeStoreText(&Context, 1u, Token(0u), Token(0u)) == 0 && Values[0] == Snapshot + Snapshot && Values[2] == Snapshot,
			"heap growth and embedded NUL preserve independent prior snapshots"))
	{
		return EXIT_FAILURE;
	}
	Context.Scratch = 79u;
	if (!Expect(TypedLoopNativeCharacterCode(&Context, Token(0u), UINT64_MAX, K::U64, 103u) != 0 &&
					Context.Scratch == 79u && Values[0] == Snapshot + Snapshot && Values[2] == Snapshot,
			"later text failure retains committed frame strings without reporting or replay"))
	{
		return EXIT_FAILURE;
	}
	Instructions[1].bTextConcatStore = false;
	Instructions[1].Operand0 = 2u;
	Instructions[1].TextFormatInstruction = 0u;
	Kinds[0] = K::I64;
	if (!Expect(TypedLoopNativeStoreText(&Context, 1u, static_cast<std::uint64_t>(INT64_MIN), 0u) == 0 && Values[2] == "-9223372036854775808",
			"native direct format stores preserve signed minimum"))
	{
		return EXIT_FAILURE;
	}
}

for (const std::string_view Source : {
         "int project(string s, int64 i) { return __char_code(s,i); } "
         "int work(int n, string s) { int total = 0; for (int i = 0; i < n; i++) { "
         "total += project(s,to_int64(i % 4)); } return total; } void main() { print(work(127,\"abcd\")); }",
         "string identity(string value) { return value; } "
         "string work(int n, int seed) { string result = \"\"; string saved = \"initial\"; "
         "for (int i = 0; i < n; i++) { result = result + to_string(seed+i); if (i == 2) saved = identity(result); } "
         "return saved + \"|\" + result; } void main() { print(work(17,97)); }",
         "string work(int n) { string value = \"abcdefghijklmnopqrstuvwx\"; string alias = \"\"; "
         "for (int i = 0; i < n; i++) { alias = value; value = value + \"!\"; "
         "if (len(alias) + 1 != len(value)) return \"bad\"; } return alias; } void main() { print(work(5)); }",
         "string work(int n) { string left = \"a\"; string right = \"b\"; string out = \"old\"; "
         "for (int i = 0; i < n; i++) { out = left + right; right = left + right; left = left + left; } "
         "return out + \"|\" + right; } void main() { print(work(5)); }",
         "string work(int n) { string result = \"initial\"; for (int i = 0; i < n; i++) "
         "result = result + (\"(\" + to_string(i) + \")\"); return result; } void main() { print(work(7)); }",
         "int project(string s, int64 i) { return __char_code(s,i); } "
         "int work(int n, string s) { string result = \"initial\"; for (int i = 0; i < n; i++) { "
         "result = result + to_string(i); int c = project(s,to_int64(i)); } return 0; } void main() { print(work(7,\"ab\")); }",
         "int work(int n, string s) { int total = 0; for (int i = 0; i < n; i++) "
         "total += __char_code(s,4294967296u64); return total; } void main() { print(work(1,\"abcd\")); }"})
{
	if (!ExpectDifferentialRun(Source,
			"native text loops preserve wrappers, independent stores, aliases, unsupported concat and failure locations"))
	{
		return EXIT_FAILURE;
	}
}

{
	const auto Module = CompileSource(
	    "int project(string s, int64 i) { return __char_code(s,i); } "
	    "string identity(string value) { return value; } "
	    "int work(int n, string input) { int total = 0; string result = \"\"; string saved = \"initial\"; "
	    "for (int i = 0; i < n; i++) { result = result + to_string(i); "
	    "saved = identity(result); total += project(input,to_int64(i % 4)); } "
	    "if (saved != result) return -1; return total; } void main() { print(work(31,\"abcd\")); }");
	if (!Expect(static_cast<bool>(Module), "compile complete native text coverage loop")) return EXIT_FAILURE;
	const auto Capture = RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
	if (!Expect(Capture.bSuccess && Capture.Output == "3052\n",
			"complete native text loop retains scalar result and string snapshots"))
	{
		return EXIT_FAILURE;
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	if (!Expect(Capture.JitStats.NativeTypedLoopExecutions != 0u,
			"byte projection, formatting append and identity share a complete native text loop"))
	{
		return EXIT_FAILURE;
	}
	#endif
}
