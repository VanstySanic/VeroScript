// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

{
	const auto Module = CompileSource(
		"int64 query(string a,string b){return __find(a,b);}"
		"int64 last(string a,string b){return __string_last_find(a,b);}"
		"bool starts(string a,string b){return __string_starts_with(a,b);}"
		"bool ends(string a,string b){return __string_ends_with(a,b);}"
		"int work(int count){string text=\"\";for(int i=0;i<count;i++)text+=\"ab\";"
		"int64 value=query(text,\"b\")+last(text,\"b\");if(starts(text,\"a\"))value+=1i64;"
		"if(ends(text,\"b\"))value+=1i64;return to_int(value);}"
		"void main(){print(work(0));print(work(1));print(work(32));print(work(128));"
		"print(query(\"a\\0bc\\0b\",\"\\0b\"));print(last(\"a\\0bc\\0b\",\"\\0b\"));"
		"if(starts(\"a\\0bc\\0b\",\"a\\0\"))print(1);if(ends(\"a\\0bc\\0b\",\"\\0b\"))print(1);}");
	if (!Expect(static_cast<bool>(Module), "compile readonly text builtin forwarders around native construction")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Module, Backend);
		if (!Expect(Capture.bSuccess && Capture.Output == "-2\n4\n66\n258\n1\n4\n1\n1\n",
				"readonly text queries preserve empty, short, heap-backed and embedded-NUL values"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.NativeTypedLoopExecutions != 0u,
																 "readonly builtin forwarders preserve native text coverage for profitable inputs"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
	using namespace VeroScript::AsmJit;
	const auto Query = FindFunctionIndex(*Module, "query");
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	const auto Coverage = CompileSource(
		"int64 query(string a,string b){return __find(a,b);}"
		"int work(int count){string text=\"\";for(int i=0;i<count;i++)text+=\"ab\";return to_int(query(text,\"b\"));}"
		"void main(){print(work(128));}");
	if (!Expect(static_cast<bool>(Coverage), "compile profitable readonly query coverage")) return EXIT_FAILURE;
	const auto Covered = RunCapture(*Coverage, VeroScript::EExecutionBackend::Jit);
	if (!Expect(Covered.bSuccess && Covered.Output == "1\n" && Covered.JitStats.NativeTypedLoopExecutions != 0u &&
					Covered.JitStats.CostModelVmFallbacks == 0u,
			"profitable native text calls use the readonly shortcut without boxed query fallback"))
	{
		return EXIT_FAILURE;
	}
	#endif
	JitLinear::FReadonlyTextBuiltinForwarder Description;
	if (!Expect(Query < Module->Functions.size() &&
					JitLinear::DescribeReadonlyTextBuiltinForwarder(Module->Functions[Query], Description) &&
					Description.Builtin == VeroScript::EBuiltinId::Find,
			"readonly query identity is proved from signature and bytecode"))
	{
		return EXIT_FAILURE;
	}
	for (int Variant = 0; Variant < 9; ++Variant)
	{
		auto Function = Module->Functions[Query];
		switch (Variant)
		{
		case 0:
			{
				Function.ClosureTypeName = "fn(string,string)->bool"; break;
			}
		case 1:
			{
				Function.ParamFlags = {VeroScript::BytecodeParamFlagRef, 0}; break;
			}
		case 2:
			{
				Function.LocalCount = 3; break;
			}
		case 3:
			{
				Function.bUsesLocalStaticStorage = true; break;
			}
		case 4:
			{
				Function.PauseMarkerOffsets.push_back(0); break;
			}
		case 5:
			{
				Function.Captures.emplace_back(); break;
			}
		case 6:
			{
				Function.UnavailableReason = "unavailable"; break;
			}
		case 7:
			{
				Function.bIsMethod = Function.bIsStaticMethod = true; break;
			}
		case 8:
			{
				Function.Code[1] = 1; break; // a reversed argument load is not the same projection.
			}
		default:
			{
				break;
			}
		}
		if (!Expect(!JitLinear::DescribeReadonlyTextBuiltinForwarder(Function, Description),
				"ambiguous signatures, refs, stateful/debug/capture bodies and reordered operands decline the shortcut"))
		{
			return EXIT_FAILURE;
		}
	}
	FJitCodeCache Cache;
	if (!Expect(Cache.EnsureCompiled(*Module, Query) == EJitCompileStatus::Ok &&
					Cache.GetCandidate(Query)->ReadonlyTextBuiltin == static_cast<std::uint8_t>(VeroScript::EBuiltinId::Find) &&
					Cache.RetainedPayloadBytes() >= sizeof(FJitFunctionEntry),
			"cached readonly query metadata participates in ordinary retained resource accounting"))
	{
		return EXIT_FAILURE;
	}
	const auto Changed = CompileSource("int64 query(string a,string b){return __find(b,a);}void main(){print(query(\"ab\",\"b\"));}");
	if (!Expect(static_cast<bool>(Changed), "compile changed readonly query body")) return EXIT_FAILURE;
	Cache.InvalidateAll();
	const auto ChangedIndex = FindFunctionIndex(*Changed, "query");
	if (!Expect(Cache.EnsureCompiled(*Changed, ChangedIndex) == EJitCompileStatus::Ok &&
					Cache.GetCandidate(ChangedIndex)->ReadonlyTextBuiltin == 0xFFu,
			"generation invalidation cannot retain a forwarder after its operand semantics change"))
	{
		return EXIT_FAILURE;
	}

	// The callable remains correct when the budget denies backend publication.
	std::ostringstream Output, Errors;
	std::istringstream Input;
	VeroScript::VeroScriptVm Limited(Output, Input, Errors);
	VeroScript::Test::Configure(Limited);
	ConfigureBackend(Limited);
	Limited.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
	std::string Error;
	if (!Expect(Limited.SetJitResourceLimits({1u, 1u}, Error) &&
					Limited.Execute(*Module, VeroScript::ERunMode::Script) &&
					Output.str() == "-2\n4\n66\n258\n1\n4\n1\n1\n",
			"readonly query execution preserves authoritative fallback under tiny JIT budgets"))
	{
		return EXIT_FAILURE;
	}
}

{
	// Calling the public Core bridge directly also covers callers other than the
	// readonly wrapper recognizer. An observer forces the canonical stack route.
	const std::array<VeroScript::EBuiltinId, 4> Queries = {VeroScript::EBuiltinId::Find,
		VeroScript::EBuiltinId::StringLastFind, VeroScript::EBuiltinId::StringStartsWith, VeroScript::EBuiltinId::StringEndsWith};
	const std::array<std::pair<std::string, std::string>, 7> Cases = {{{"", ""}, {"abc", ""}, {"", "a"},
		{"aaaa", "aa"}, {std::string("a\0bc\0b", 6u), std::string("\0b", 2u)},
		{std::string("\xff\0\xfe", 3u), std::string("\0\xfe", 2u)}, {std::string(8192u, 'x') + "yz", std::string(64u, 'x') + "yz"}}};
	const auto SameScalar = [](const VeroScript::FValue& A, const VeroScript::FValue& B)
	{
		return A.index() == B.index() && ((std::holds_alternative<std::int64_t>(A) && std::get<std::int64_t>(A) == std::get<std::int64_t>(B)) ||
			(std::holds_alternative<bool>(A) && std::get<bool>(A) == std::get<bool>(B)));
	};
	for (const auto Query : Queries)
	{
		for (const auto &Case : Cases)
		{
			VeroScript::FValue Expected;
			std::uint64_t ExpectedWork = 0u;
			for (const bool Observer : {true, false})
			{
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
				if (Observer)
				{
					Vm.SetMutationCallback([](const VeroScript::FMutationEvent &)
						{
						});
				}
				VeroScript::FValue Arguments[] = {Case.first, Case.second}, Result = std::int32_t{91};
				bool HasResult = false;
				if (!Expect(Vm.JitInvokeBuiltin(Query, Arguments, 2u, HasResult, Result) && HasResult && !Vm.HasError() &&
								std::get<std::string>(Arguments[0]) == Case.first && std::get<std::string>(Arguments[1]) == Case.second,
						"borrowed readonly queries preserve arguments and byte-oriented empty, NUL, malformed-UTF8 and heap values"))
				{
					return EXIT_FAILURE;
				}
				if (Observer)
				{
					Expected = Result;
					ExpectedWork = Vm.GetExecutedInstructionCount();
				}
				else if (!Expect(SameScalar(Result, Expected) && Vm.GetExecutedInstructionCount() == ExpectedWork,
							 "borrowed queries reuse canonical results and exactly the same work accounting"))
				{
					return EXIT_FAILURE;
				}
			}
			for (const std::size_t Alias : {0u, 1u})
			{
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
				VeroScript::FValue Arguments[] = {Case.first, Case.second};
				bool HasResult = false;
				if (!Expect(Vm.JitInvokeBuiltin(Query, Arguments, 2u, HasResult, Arguments[Alias]) && HasResult && SameScalar(Arguments[Alias], Expected),
						"readonly query output may alias either consumed argument"))
				{
					return EXIT_FAILURE;
				}
			}
		}
	}
	for (const auto Query : Queries)
	{
		for (const bool LongNeedle : {false, true})
		{
			std::string ExpectedError;
			for (const bool Observer : {true, false})
			{
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
				if (Observer)
				{
					Vm.SetMutationCallback([](const VeroScript::FMutationEvent &)
						{
						});
				}
				Vm.SetStandardLibraryLimits({3u, 0u, 0u});
				VeroScript::FValue Arguments[] = {std::string(LongNeedle ? "a" : "abcdef"), std::string(LongNeedle ? "abcdef" : "a")};
				VeroScript::FValue Result = std::int32_t{91};
				bool HasResult = true;
				if (!Expect(!Vm.JitInvokeBuiltin(Query, Arguments, 2u, HasResult, Result) && !HasResult && Vm.HasError() &&
								std::get<std::int32_t>(Result) == 91,
						"readonly query text limits leave output unchanged"))
				{
					return EXIT_FAILURE;
				}
				if (Observer)
				{
					ExpectedError = Vm.GetError().Message;
				}
				else if (!Expect(Vm.GetError().Message == ExpectedError, "borrowed readonly queries retain canonical limit diagnostics"))
				{
					return EXIT_FAILURE;
				}
			}
		}
	}
	for (const auto Query : Queries)
	{
		for (const bool WrongType : {false, true})
		{
			std::string ExpectedError;
			for (const bool Observer : {true, false})
			{
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
				if (Observer)
				{
					Vm.SetMutationCallback([](const VeroScript::FMutationEvent &)
						{
						});
				}
				VeroScript::FValue Arguments[] = {std::string("abc"), WrongType ? VeroScript::FValue{std::int32_t{1}} : VeroScript::FValue{std::string("a")}};
				VeroScript::FValue Result = std::int32_t{91};
				bool HasResult = true;
				if (!Expect(!Vm.JitInvokeBuiltin(Query, Arguments, WrongType ? 2u : 1u, HasResult, Result) &&
								!HasResult && Vm.HasError() && std::get<std::int32_t>(Result) == 91,
						"readonly query count/type failures leave caller output unchanged"))
				{
					return EXIT_FAILURE;
				}
				if (Observer)
				{
					ExpectedError = Vm.GetError().Message;
				}
				else if (!Expect(Vm.GetError().Message == ExpectedError, "query count/type validation retains canonical error precedence"))
				{
					return EXIT_FAILURE;
				}
			}
		}
		for (const bool Observer : {true, false})
		{
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
			if (Observer)
			{
				Vm.SetMutationCallback([](const VeroScript::FMutationEvent &)
					{
					});
			}
			Vm.SetExecutionCheck([]() {return false;});
			VeroScript::FValue Arguments[] = {std::string(8192u, 'a'), std::string("a")}, Result = std::int32_t{91};
			bool HasResult = true;
			if (!Expect(!Vm.JitInvokeBuiltin(Query, Arguments, 2u, HasResult, Result) && !HasResult && Vm.HasError() &&
							std::get<std::int32_t>(Result) == 91,
					"external cancellation checks retain the canonical governed query path"))
			{
				return EXIT_FAILURE;
			}
		}
	}
}

{
	// Preserve the old bridge's temporary operand-stack requirement even though
	// the borrowed route itself needs no argument copies on that stack.
	struct FStackProbe
	{
		VeroScript::VeroScriptVm* Vm = nullptr;
		bool Called = false, Succeeded = false, HasResult = true, UntouchedOutput = false;
		std::string Error;
	};
	FStackProbe Probe;
	VeroScript::FHostBindingRegistry Registry;
	std::string Error;
	if (!Expect(Registry.RegisterNative("stack_probe", VeroScript::ENativeType::Void, {}, [](const VeroScript::FNativeCallContext &Context, VeroScript::FValue &Out, std::string &OutError)
					{
						auto &P = *static_cast<FStackProbe *>(Context.UserData);
						P.Called = true;
						VeroScript::FValue Arguments[] = {std::string("aaaa"), std::string("aa")}, Result = std::int32_t{91};
						P.Succeeded = P.Vm->JitInvokeBuiltin(VeroScript::EBuiltinId::Find, Arguments, 2u, P.HasResult, Result);
						P.UntouchedOutput = std::holds_alternative<std::int32_t>(Result) && std::get<std::int32_t>(Result) == 91;
						P.Error = P.Vm->GetError().Message;
						Out = std::int32_t{0};
						OutError = P.Error;
						return P.Succeeded;
					},
					&Probe, Error),
			"register near-full Core operand stack query probe"))
	{
		return EXIT_FAILURE;
	}
	auto Options = VeroScript::Test::MakeCompileOptions();
	Options.Bindings = &Registry;
	const auto Compiled = VeroScript::VeroScriptCompile::CompileSource("void main(){stack_probe();}", "query-stack.vs", Options);
	if (!Expect(Compiled.bSuccess && Compiled.Module, "compile Core query stack-room fixture")) return EXIT_FAILURE;
	for (const std::size_t Room : {0u, 1u, 2u})
	{
		for (const bool Observer : {true, false})
		{
			auto Module = *Compiled.Module;
			auto &Function = Module.Functions[FindFunctionIndex(Module, "main")];
			const auto ConstantIndex = static_cast<std::uint16_t>(Module.Constants.size());
			VeroScript::FBytecodeConstant Constant;
			Constant.Kind = VeroScript::EConstantKind::I32;
			Constant.I32 = 0;
			Module.Constants.push_back(Constant);
			std::vector<std::uint8_t> Code;
			Code.reserve((VeroScript::VeroScriptVm::MaxOperandStackDepth - Room) * 3u + Function.Code.size());
			for (std::size_t I = Room; I < VeroScript::VeroScriptVm::MaxOperandStackDepth; ++I)
			{
				Code.push_back(static_cast<std::uint8_t>(VeroScript::EOpcode::ConstI32));
				VeroScript::Bytecode::WriteU16LE(Code, ConstantIndex);
			}
			Code.insert(Code.end(), Function.Code.begin(), Function.Code.end());
			Function.Code = std::move(Code);
			Function.DebugLines.clear();
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Vm);
			if (Observer)
			{
				Vm.SetMutationCallback([](const VeroScript::FMutationEvent &)
					{
					});
			}
			if (Room < 2u)
			{
				Vm.SetStandardLibraryLimits({1u, 0u, 0u});
			}
			Probe = {};
			Probe.Vm = &Vm;
			const auto Ok = Vm.Execute(Module, VeroScript::ERunMode::Script, {}, &Registry.GetNativeFunctions());
			if (!Expect(Probe.Called && Ok == (Room == 2u) && Probe.Succeeded == (Room == 2u) &&
							(Room == 2u ? Probe.HasResult : (!Probe.HasResult && Probe.UntouchedOutput && Probe.Error == "operand stack overflow")),
					"borrowed queries preserve exact temporary stack-room and overflow-before-text-limit behavior"))
			{
				return EXIT_FAILURE;
			}
		}
	}
}

{
	const auto Module = CompileSource(
		"int64 query(string a,string b){return __find(a,b);}"
		"int work(int count){string text=\"\";for(int i=0;i<count;i++)text+=\"ab\";return to_int(query(text,\"b\"));}"
		"void main(){print(work(4));}");
	if (!Expect(static_cast<bool>(Module), "compile readonly query call-depth fixture")) return EXIT_FAILURE;
	for (const std::size_t Depth : {1u, 2u, 3u})
	{
		bool ExpectedOk = false;
		std::string ExpectedOutput, Message;
		std::uint32_t Line = 0, Column = 0;
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
			Vm.SetExecutionBackend(Backend);
			Vm.SetMaxCallDepth(Depth);
			const bool Ok = Vm.Execute(*Module, VeroScript::ERunMode::Script);
			if (Backend == VeroScript::EExecutionBackend::Vm)
			{
				ExpectedOk = Ok; ExpectedOutput = Output.str(); Message = Vm.GetError().Message;
				Line = Vm.GetError().Location.Line; Column = Vm.GetError().Location.Column;
			}
			else if (!Expect(Ok == ExpectedOk && Output.str() == ExpectedOutput && Vm.GetError().Message == Message &&
					Vm.GetError().Location.Line == Line && Vm.GetError().Location.Column == Column,
					"readonly query shortcuts preserve shallow logical depth and original call-site errors"))
			{
				std::cerr << "backend=" << static_cast<int>(Backend) << " depth=" << Depth << " ok=" << Ok << '/' << ExpectedOk
					<< " output=" << Output.str() << '/' << ExpectedOutput << " error=" << Vm.GetError().Message << '/' << Message
					<< " location=" << Vm.GetError().Location.Line << ':' << Vm.GetError().Location.Column << '/'
					<< Line << ':' << Column << '\n';
				return EXIT_FAILURE;
			}
			if (!Expect(Ok == (Depth == 3u), "query call-depth fixture reaches the exact wrapper depth")) return EXIT_FAILURE;
		}
	}

	const auto Failure = CompileSource(
		"int64 query(string a,string b){return __char_code(a,4294967296i64);}"
		"int work(int count){string text=\"\";for(int i=0;i<count;i++)text+=\"ab\";return to_int(query(text,\"b\"));}"
		"void main(){print(work(4));}");
	if (!Expect(static_cast<bool>(Failure), "compile changed non-forwarder query error")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Failure, VeroScript::EExecutionBackend::Vm);
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Candidate = RunCapture(*Failure, Backend);
		if (!Expect(!Reference.bSuccess && !Candidate.bSuccess && Candidate.ErrorMessage == Reference.ErrorMessage &&
						Candidate.ErrorLine == Reference.ErrorLine && Candidate.ErrorColumn == Reference.ErrorColumn,
				"query bodies beyond the exact proof retain ordinary callee failure diagnostics"))
		{
			return EXIT_FAILURE;
		}
	}

	std::ostringstream Output, Errors;
	std::istringstream Input;
	VeroScript::VeroScriptVm Vm(Output, Input, Errors);
	VeroScript::Test::Configure(Vm);
	ConfigureBackend(Vm);
	Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
	if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script) &&
					!Vm.Execute(*Failure, VeroScript::ERunMode::Script) &&
					Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str() == "1\n1\n",
			"query cache/module replacement and subsequent reuse keep metadata and call-depth state isolated"))
	{
		return EXIT_FAILURE;
	}
}

