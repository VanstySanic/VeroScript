// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

{
	const std::string Declarations =
		"class PrefixBase {public:int bias=7;virtual int evaluate(int value){return bias+value;}};"
		"int traverse(array<PrefixBase> values,int count){int total=0;"
		"for(int i=0;i<count;i++){total+=values[i].evaluate(i+3);}return total;}"
		"int shifted(array<PrefixBase> values,int count){int total=0;"
		"for(int i=1;i<count;i++){total+=values[i].evaluate(i+3);}return total;}"
		"int changing_counter(array<PrefixBase> values,int count){int total=0;"
		"for(int i=0;i<count;i++){if(i==0)i++;total+=values[i].evaluate(i+3);}return total;}"
		"int changing_limit(array<PrefixBase> values,int count){int total=0;"
		"for(int i=0;i<count;i++){count=count-1;total+=values[i].evaluate(i+3);}return total;}"
		"int indirect(array<PrefixBase> values,int count){int total=0;"
		"for(int i=0;i<count;i++){total+=values[(i+1)%32].evaluate(i+3);}return total;}";
	const std::string Initialize =
		"array<PrefixBase> values=array<PrefixBase>();"
		"for(int i=0;i<64;i++){PrefixBase item=PrefixBase();item.bias=i*3+7;values.push(item);}"
		"for(int i=64;i<2048;i++){values.push(nullptr);}";
	const auto Module = CompileSource(Declarations + "void main(){" + Initialize +
		"print(traverse(values,-1));print(traverse(values,0));print(traverse(values,1));print(traverse(values,32));}");
	if (!Expect(static_cast<bool>(Module), "compile bounded object-prefix traversal with unread unsupported tail")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Capture = RunCapture(*Module, Backend);
		if (!Expect(Capture.bSuccess && Capture.Output == "0\n0\n10\n2304\n",
				"object prefix traversal preserves negative, zero, one and 32-element semantics without reading the tail"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Capture.JitStats.NativeTypedLoopExecutions >= 2u,
																 "one and 32-element object prefixes stay native despite a large unread null tail"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, "traverse"));
	if (!Expect(Entry && Entry->TypedLoopPlans && std::any_of(Entry->TypedLoopPlans->Plans.begin(), Entry->TypedLoopPlans->Plans.end(), [](const auto &Plan)
													  {
														  return Plan.ObjectArrayPrefixCounterSlot != 0xFFFFu && Plan.ObjectArrayPrefixLimitSlot != 0xFFFFu;
													  }),
			"canonical object array traversal retains its induction-prefix proof"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Name : {"changing_counter", "changing_limit", "indirect"})
	{
		const auto* Other = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(!Other || !Other->TypedLoopPlans || std::none_of(Other->TypedLoopPlans->Plans.begin(), Other->TypedLoopPlans->Plans.end(), [](const auto &Plan)
															{
																return Plan.ObjectArrayPrefixCounterSlot != 0xFFFFu;
															}),
				"counter mutation, changing limits and noncanonical indices retain full object preparation"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
	for (const auto Name : {"shifted", "changing_counter", "changing_limit", "indirect"})
	{
		const auto Other = CompileSource(Declarations + "void main(){" + Initialize + "print(" + Name + "(values,32));}");
		if (!Expect(static_cast<bool>(Other), "compile noncanonical object-prefix holdout")) return EXIT_FAILURE;
		const auto Reference = RunCapture(*Other, VeroScript::EExecutionBackend::Vm);
		for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			const auto Candidate = RunCapture(*Other, Backend);
			if (!Expect(Reference.bSuccess && Candidate.bSuccess && Candidate.Output == Reference.Output,
					"nonzero starts and unproved induction/index paths keep authoritative semantics"))
			{
				return EXIT_FAILURE;
			}
			#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
			if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Candidate.JitStats.NativeTypedLoopExecutions == 0u,
																	 "unproved prefixes retain full-scan fallback for the unsupported unread tail"))
			{
				return EXIT_FAILURE;
			}
			#endif
		}
	}
}

{
	const auto Module = CompileSource(
		"class PrefixBase {public:int bias=7;virtual int evaluate(int value){return bias+value;}};"
		"class PrefixState {public:int x=0;int y=5;int update(int amount){x=x+amount;y=y+x;return x+y;}};"
		"int overrun(array<PrefixBase> values,PrefixState state,int count){int total=0;"
		"for(int i=0;i<count;i++){total+=state.update(1);total+=values[i].evaluate(i);}return total;}");
	if (!Expect(static_cast<bool>(Module), "compile object-prefix bounds error after field writes")) return EXIT_FAILURE;
	std::string Message;
	std::uint32_t Line = 0, Column = 0;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
		VeroScript::FObjectHandle Receiver, State;
		if (!Expect(Vm.ConstructObject(*Module, "PrefixBase", {}, Receiver, nullptr) &&
						Vm.ConstructObject(*Module, "PrefixState", {}, State, nullptr) && Receiver && State,
				"construct retained objects for prefix overrun"))
		{
			return EXIT_FAILURE;
		}
		auto Values = std::make_shared<VeroScript::FDynamicArray>();
		Values->ElementTypeName = Receiver->GetTypeName();
		Values->Elements.push_back(Receiver);
		const auto Before = State->MutationVersion;
		const bool Ok = Vm.ExecuteFreeFunction(*Module, "overrun", {Values, State, std::int32_t{2}}, nullptr);
		if (!Expect(!Ok && Vm.HasError() && !State->bDestroyed && State->Fields.size() == 2u &&
						std::get<std::int32_t>(State->Fields[0]) == 2 && std::get<std::int32_t>(State->Fields[1]) == 8 &&
						State->MutationVersion == Before + 4u,
				"oversize prefix limits preserve every field write before the original array bound failure"))
		{
			return EXIT_FAILURE;
		}
		const auto& Error = Vm.GetError();
		if (Backend == VeroScript::EExecutionBackend::Vm)
		{
			Message = Error.Message; Line = Error.Location.Line; Column = Error.Location.Column;
		}
		else if (!Expect(Error.Message == Message && Error.Location.Line == Line && Error.Location.Column == Column,
					 "prepared prefixes retain exact original GetIndex bounds diagnostics"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Vm.GetJitStats().NativeTypedLoopExecutions != 0u,
																 "prefix overrun fixture actually executes native object code before failing"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
}

