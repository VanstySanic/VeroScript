// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

{
	// A single retained-object construction loop pays the same boxed ownership
	// bridges as the construction half of a build/scan pair. Returning only its
	// length, or escaping the retaining array, must not evade the cost choice.
	std::string Source =
		"class AllocBase{public:int value=7;};"
		"class AllocDerived:public AllocBase{public:int tag=9;};"
		"int build_plain(int n){array<AllocBase> values=array<AllocBase>();"
		"for(int i=0;i<n;i++)values.push(AllocBase());return len(values);}"
		"int build_derived(int n){array<AllocBase> values=array<AllocBase>();"
		"for(int i=0;i<n;i++){AllocBase value=AllocDerived();value.value=i+3;values.push(value);}"
		"return len(values);}"
		"array<AllocBase> make_escaping(int n){array<AllocBase> values=array<AllocBase>();"
		"for(int i=0;i<n;i++)values.push(AllocDerived());return values;}"
		"int native_fields(int n){AllocDerived value=AllocDerived();array<int> marker={13};"
		"int total=0;for(int i=0;i<n;i++){value.value=(value.value+i)%100003;total+=value.value;}"
		"return total+marker[0];}void main(){";
	std::ostringstream Expected;
	for (const int Count : {0, 3, 1024})
	{
		const std::string Number = std::to_string(Count);
		Source += "print(build_plain(" + Number + "));print(build_derived(" + Number + "));"
			"array<AllocBase> items" + Number + "=make_escaping(" + Number + ");"
			"print(len(items" + Number + "));";
		Source += Count == 0 ? "print(0);" : "print(items" + Number + "[" + std::to_string(Count - 1) + "].value);";
		Source += "print(native_fields(" + Number + "));";
		int Field = 7, Total = 0;
		for (int Index = 0; Index < Count; ++Index)
		{
			Field = (Field + Index) % 100003;
			Total += Field;
		}
		Expected << Count << '\n' << Count << '\n' << Count << '\n'
			<< (Count == 0 ? 0 : 7) << '\n' << Total + 13 << '\n';
	}
	Source += '}';
	const auto Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile single retained-object allocation loops")) return EXIT_FAILURE;
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	for (const char* Name : {"build_plain", "build_derived", "make_escaping"})
	{
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(Entry && Entry->bPreferVmForExecution,
				"single-loop retained object construction prefers the canonical VM"))
		{
			return EXIT_FAILURE;
		}
	}
	const auto* Native = Cache.GetCandidate(FindFunctionIndex(*Module, "native_fields"));
	if (!Expect(Native && !Native->bPreferVmForExecution && Native->TypedLoopPlans && Native->TypedLoopPlans->bHasProfitablePlan && std::any_of(Native->TypedLoopPlans->Plans.begin(), Native->TypedLoopPlans->Plans.end(), [](const auto &Plan)
																																		{
																																			return Plan.bProfitable && Plan.bUsesDirectScalarFields;
																																		}),
			"profitable field loops with object and array prefixes retain typed execution"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
		VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, Expected.str(),
				"zero, short and large allocation loops preserve escaping array values and native field work"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	const auto Module = CompileSource(
		"array<int> replace(array<int> prior){prior.push(29);array<int> result={41};return result;}"
		"int array_loops(int n){array<int> values=array<int>();array<int> alias=values;"
		"for(int i=0;i<n;i++){values.push(i);}"
		"values=replace(alias);for(int i=0;i<n;i++){values.push(i+7);}"
		"return alias[n]*10000+values[0]*100+values[n];}"
		"struct CustomPush{int total=0;void push(int value){total+=value;}};"
		"int object_loop(int n){CustomPush receiver=CustomPush();array<int> other={7};"
		"for(int i=0;i<n;i++){receiver.push(i);}return receiver.total+other[0];}"
		"void main(){print(array_loops(4));print(object_loop(4));}");
	if (!Expect(static_cast<bool>(Module), "compile independent array regions separated by a call"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectRun(*Module, Backend, "294110\n13\n",
		        "array regions preserve aliases and custom push dispatch"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const bool bObserve : {false, true})
	{
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		if (bObserve)
		{
			Vm.SetMutationCallback([](const VeroScript::FMutationEvent&) {});
		}
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str() == "294110\n13\n",
		        "array regions rebind after a call and preserve custom push")
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
			|| !Expect(bObserve ? Vm.GetJitStats().NativeTypedLoopExecutions == 0
			                    : Vm.GetJitStats().NativeTypedLoopExecutions == 2,
				"native array regions bind both live arrays and respect mutation observation")
	#endif
		    )
		{
			return EXIT_FAILURE;
		}
	}
}

{
	const auto Module = CompileSource(
		"int main(string[] args){int value=len(args[0]);int n=150000;"
		"for(int i=0;i<n;i++){value=(value*31+i)&1048575;}print(value);return 0;}");
	if (!Expect(static_cast<bool>(Module), "compile boxed entry with scalar loop"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, "main"));
	if (!Expect(Entry && Entry->bPreferVmForExecution,
			"boxed prefix does not make an unlowered scalar loop profitable in Linear"))
	{
		return EXIT_FAILURE;
	}

	const std::string Argument(37, 'x');
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
		VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script, {Argument})
				&& Output.str() == "810781\n",
				"boxed scalar entry preserves argument and loop semantics")
			|| !Expect(Backend == VeroScript::EExecutionBackend::Vm
					|| Vm.GetJitStats().CostModelVmFallbacks > 0,
				"boxed scalar entry falls back before running the Linear loop"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	const auto Module = CompileSource(
		"int snapshot(int seed){auto advance=[state=seed](int delta) mutable -> int"
		"{state=state*3+delta;return state;};"
		"int one=1;int two=2;int first=advance(one);int second=advance(two);"
		"return seed*10000+first*100+second;}"
		"int inside_loop(int n){int sum=0;int one=1;"
		"for(int i=0;i<n;i++){auto advance=[state=i](int delta) mutable -> int"
		"{state=state+delta;return state;};sum+=advance(one);sum+=advance(one);}return sum;}"
		"int copied(int seed){auto advance=[state=seed](int delta) mutable -> int"
		"{state=state+delta;return state;};auto alias=advance;int one=1;"
		"int first=advance(one);return first*100+alias(one);}"
		"void main(){print(snapshot(3));print(snapshot(5));print(snapshot(3));"
		"print(inside_loop(4));print(copied(7));}");
	if (!Expect(static_cast<bool>(Module), "compile mutable closure optimization probes"))
	{
		return EXIT_FAILURE;
	}

	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Snapshot = Cache.GetCandidate(FindFunctionIndex(*Module, "snapshot"));
	const auto* InsideLoop = Cache.GetCandidate(FindFunctionIndex(*Module, "inside_loop"));
	const auto* Copied = Cache.GetCandidate(FindFunctionIndex(*Module, "copied"));
	if (!Expect(Snapshot && Snapshot->InlinedCallSites >= 2,
			"nonescaping mutable capture is reused as a local slot")
		|| !Expect(InsideLoop && InsideLoop->InlinedCallSites == 0,
			"repeated creation inside a loop retains its independent closure state")
		|| !Expect(Copied && Copied->InlinedCallSites == 0,
			"copied closures retain shared mutable state"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
		VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "31032\n51650\n31032\n24\n809\n",
				"mutable capture reads and resets agree across backends"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!ExpectDifferentialRun(
			"int work(int seed){auto advance=[state=seed](int delta) mutable -> int"
			"{state=state+delta;return state;};int one=1;return advance(one);}"
			"void main(){print(work(2147483647));}",
			"mutable capture inlining preserves arithmetic errors"))
	{
		return EXIT_FAILURE;
	}
}

{
	const auto Module = CompileSource(
		"class Item { public: int x=0; int y=0; int sum(){return x+y;} }; "
		"int backward(int n){array<Item> values=array<Item>();"
		"for(int i=0;i<n;i++){Item p=Item();p.x=(i+17)&255;p.y=(i*7)&127;values.push(p);}"
		"int total=0;for(int i=n-1;i>=0;i--)total+=values[i].sum();return total;} "
		"int forward(int n){array<Item> values=array<Item>();int i=0;"
		"while(i<n){Item p=Item();p.x=i;p.y=3;values.push(p);i++;}"
		"int total=0;i=0;while(i<n){total+=values[i].sum();i++;}return total;} "
		"void main(){print(backward(0));print(backward(8));print(forward(0));print(forward(8));}");
	if (!Expect(static_cast<bool>(Module), "compile retained local object loops"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* BackwardEntry = Cache.GetCandidate(FindFunctionIndex(*Module, "backward"));
	const auto* ForwardEntry = Cache.GetCandidate(FindFunctionIndex(*Module, "forward"));
	if (!Expect(BackwardEntry && !BackwardEntry->bPreferVmForExecution,
			"canonical retained object projection is scalar-replaced across both loops")
		|| !Expect(ForwardEntry && !ForwardEntry->bPreferVmForExecution,
			"forward retained object projection is scalar-replaced across both loops"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
		VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "0\n360\n0\n52\n", "retained object loops preserve semantics"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// Loaded packages can contain in-range but semantically aliased local slots.
	// Scalar replacement must reject such a shape instead of treating the array
	// and loop counter as independent storage.
	const auto Module = CompileSource(
		"class AliasItem{public:int x=0;int y=0;int sum(){return x+y;}};"
		"int aliased(int n){array<AliasItem>a=array<AliasItem>();"
		"for(int i=0;i<n;i++){AliasItem p=AliasItem();p.x=(i+1)&7;"
		"p.y=(i*3)&7;a.push(p);}int total=0;"
		"for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}"
		"void main(){print(aliased(4));}");
	if (!Expect(static_cast<bool>(Module),
			"compile retained object slot-alias rejection probe"))
	{
		return EXIT_FAILURE;
	}
	const std::uint16_t FunctionIndex = FindFunctionIndex(*Module, "aliased");
	auto& Code = Module->Functions[FunctionIndex].Code;
	if (!Expect(Code.size() == 147u,
			"retained object slot-alias probe uses canonical lowering"))
	{
		return EXIT_FAILURE;
	}
	// StoreLocal at byte 3 normally targets the array slot. Alias it with the
	// construction counter encoded by StoreLocal at byte 9.
	Code[4] = Code[10];
	Code[5] = Code[11];
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Entry = Cache.GetCandidate(FunctionIndex);
	if (!Expect(Entry && (!Entry->TypedLoopPlans
			|| !Entry->TypedLoopPlans->bHasRetainedObjectArrayProjection),
			"retained object scalar replacement rejects aliased local slots"))
	{
		return EXIT_FAILURE;
	}
}

{
	const auto Module = CompileSource(
		"class PeriodicItem{public:int x=0;int y=0;int sum(){return x+y;}};"
		"int periodic(int n){array<PeriodicItem> values=array<PeriodicItem>();"
		"int i=0;while(i<n){PeriodicItem p=PeriodicItem();p.x=i%97;"
		"p.y=(i%13)*3;values.push(p);i++;}int total=0;"
		"for(int j=0;j<n;j++)total+=values[j].sum();return total;}"
		"void main(){print(periodic(0));print(periodic(-3));print(periodic(8));}");
	if (!Expect(static_cast<bool>(Module), "compile periodic retained object loop"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, "periodic"));
	if (!Expect(Entry && !Entry->bPreferVmForExecution,
			"periodic forward retained object projection is scalar-replaced"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
		VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "0\n0\n112\n",
				"periodic retained object loop preserves semantics"))
		{
			return EXIT_FAILURE;
		}
	}
}

{
	// The cross-loop scalar replacement is deliberately fail-closed, but its
	// checked arithmetic must still fail at the same source operation as ordinary
	// object materialization. Cover zero/negative trips and each reachable failure
	// phase independently.
	const std::string PairPrefix =
		"class EdgePair{public:int first=0;int second=0;"
		"int sum(){return first+second;}}; ";
	if (!ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();for(int i=0;i<n;i++){"
			  "EdgePair p=EdgePair();p.first=(i+17)&255;p.second=(i*7)&127;a.push(p);}"
			  "int total=0;for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}"
			  "void main(){print(work(0));print(work(-3));}",
			"retained object projection zero and negative trip parity")
		|| !ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();for(int i=0;i<n;i++){"
			  "EdgePair p=EdgePair();p.first=(i+2147483647)&-1;"
			  "p.second=(i*1)&-1;a.push(p);}int total=0;"
			  "for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}"
			  "void main(){print(work(2));}",
			"retained object first-field overflow parity")
		|| !ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();for(int i=0;i<n;i++){"
			  "EdgePair p=EdgePair();p.first=(i+1)&-1;"
			  "p.second=(i*2147483647)&-1;a.push(p);}int total=0;"
			  "for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}"
			  "void main(){print(work(3));}",
			"retained object second-field overflow parity")
		|| !ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();for(int i=0;i<n;i++){"
			  "EdgePair p=EdgePair();p.first=(i+1)&-1;"
			  "p.second=(i*2147483647)&-1;a.push(p);}int total=0;"
			  "for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}"
			  "void main(){print(work(2));}",
			"retained object projection overflow parity")
		|| !ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();for(int i=0;i<n;i++){"
			  "EdgePair p=EdgePair();p.first=(i+1)&-1;"
			  "p.second=(i*1000000000)&-1;a.push(p);}int total=0;"
			  "for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}"
			  "void main(){print(work(3));}",
			"retained object total overflow parity")
		|| !ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();int i=0;while(i<n){"
			  "EdgePair p=EdgePair();p.first=i;p.second=3;a.push(p);i++;}"
			  "int total=0;i=0;while(i<n){total+=a[i].sum();i++;}return total;}"
			  "void main(){print(work(0));print(work(-3));}",
			"forward retained object zero and negative trip parity")
		|| !ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();int i=0;while(i<n){"
			  "EdgePair p=EdgePair();p.first=i;p.second=2147483647;a.push(p);i++;}"
			  "int total=0;i=0;while(i<n){total+=a[i].sum();i++;}return total;}"
			  "void main(){print(work(2));}",
			"forward retained object projection overflow parity")
		|| !ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();int i=0;while(i<n){"
			  "EdgePair p=EdgePair();p.first=i%2147483647;"
			  "p.second=(i%1)*0;a.push(p);i++;}int total=0;"
			  "for(int j=0;j<n;j++)total+=a[j].sum();return total;}"
			  "void main(){print(work(65537));}",
			"periodic forward retained object total overflow parity")
		|| !ExpectDifferentialRun(PairPrefix
			+ "int work(int n){array<EdgePair>a=array<EdgePair>();for(int i=0;i<n;i++){"
			  "EdgePair p=EdgePair();p.first=(i+1)&7;p.second=(i*3)&7;a.push(p);}"
			  "int total=0;for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}"
			  "void main(){print(work(-2147483647-1));}",
			"retained object reverse-counter overflow parity"))
	{
		return EXIT_FAILURE;
	}
}

{
	// Held-out random selector shapes: direct state-table polymorphism receives a
	// complete fused transform, while normalized field read/modify/write bytecode
	// remains in the general typed CFG. Both paths must prove full loop coverage;
	// an unrelated small indexed loop must not mask a genuinely unsupported loop.
	const auto Module = CompileSource(
		"interface RandomOp { virtual int step(int value)=0; }; "
		"class RandomAddA:RandomOp { public:int step(int value){return (value+3)%1000003;} }; "
		"class RandomAddB:RandomOp { public:int step(int value){return (value+5)%1000003;} }; "
		"class RandomAddC:RandomOp { public:int step(int value){return (value+1)%1000003;} }; "
		"class RandomAddD:RandomOp { public:int step(int value){return (value+2)%1000003;} }; "
		"class RandomFields { public:int a=1;int b=1;int c=4;int d=3; }; "
		"int random_dispatch(){array<int> choices={0,2,2,1,2,0,0,0,0,2,2,1,3,1,1,3,0};"
		"RandomOp a=RandomAddA();RandomOp b=RandomAddB();RandomOp c=RandomAddC();"
		"RandomOp d=RandomAddD();RandomOp current=a;int state=1;int total=0;int i=0;"
		"while(i<101023){state=(state*7+13)%17;int choice=choices[state];"
		"if(choice==0)current=a;else if(choice==1)current=b;"
		"else if(choice==2)current=c;else current=d;"
		"total=current.step(total);i++;}return total+state;} "
		"int random_fields(){array<int> choices={0,3,1,1,0,0,0,1,3,3,0,1,0,3,1,2,1,2,3,3,3,2,0};"
		"RandomFields value=RandomFields();int state=22;int total=0;int i=0;"
		"while(i<91126){state=(state*4+4)%23;int delta=(state%7)+1;"
		"int choice=choices[state];if(choice==0){value.a=(value.a+delta)%100003;"
		"total=(total+value.a)%1000003;}else if(choice==1){"
		"value.b=(value.b+delta)%100003;total=(total+value.b)%1000003;}"
		"else if(choice==2){value.c=(value.c+delta)%100003;"
		"total=(total+value.c)%1000003;}else{value.d=(value.d+delta)%100003;"
		"total=(total+value.d)%1000003;}i++;}int finalChoice=choices[state];"
		"if(finalChoice==0)return total+value.a;if(finalChoice==1)return total+value.b;"
		"if(finalChoice==2)return total+value.c;return total+value.d;} "
		"int unrelated_index_loop(){array<int> values={4,5};RandomOp a=RandomAddA();"
		"RandomOp b=RandomAddB();RandomOp c=RandomAddC();RandomOp d=RandomAddD();"
		"RandomOp current=a;int state=1;int total=0;int i=0;while(i<32){"
		"state=(state*7+13)%17;int choice=state%4;if(choice==0)current=a;"
		"else if(choice==1)current=b;else if(choice==2)current=c;else current=d;"
		"total=current.step(total);i++;}int extra=0;for(int j=0;j<2;j++)extra+=values[j];"
		"return total+state+extra;} void main(){print(random_dispatch());"
		"print(random_fields());print(unrelated_index_loop());}");
	if (!Expect(static_cast<bool>(Module),
			"compile optimized indirect field and receiver loops"))
	{
		return EXIT_FAILURE;
	}
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* DispatchEntry =
		Cache.GetCandidate(FindFunctionIndex(*Module, "random_dispatch"));
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	if (!Expect(DispatchEntry && !DispatchEntry->bPreferVmForExecution
			&& DispatchEntry->TypedLoopPlans
			&& std::any_of(DispatchEntry->TypedLoopPlans->Plans.begin(),
				DispatchEntry->TypedLoopPlans->Plans.end(), [](const auto& Plan)
				{
					return Plan.bProfitable
									&& Plan.FusedKind == VeroScript::AsmJit::JitLinear::FTypedLoopPlan::
									EFusedKind::StateIndirectVirtualTransform;
				}),
			"direct-state polymorphic loop uses its complete fusion"))
	{
		return EXIT_FAILURE;
	}
	#else
	if (!Expect(DispatchEntry && DispatchEntry->bPreferVmForExecution
			&& DispatchEntry->TypedLoopPlans
			&& DispatchEntry->TypedLoopPlans->bNativeOnlyPlanUnavailable,
			"direct-state polymorphic loop prefers VM without a native backend"))
	{
		return EXIT_FAILURE;
	}
	#endif
	const auto* FieldsEntry =
		Cache.GetCandidate(FindFunctionIndex(*Module, "random_fields"));
	if (!Expect(FieldsEntry && !FieldsEntry->bPreferVmForExecution
			&& FieldsEntry->TypedLoopPlans
			&& FieldsEntry->TypedLoopPlans->bHasProfitablePlan
			&& std::any_of(FieldsEntry->TypedLoopPlans->Plans.begin(),
				FieldsEntry->TypedLoopPlans->Plans.end(), [](const auto& Plan)
				{
					return Plan.bProfitable && Plan.bUsesDirectScalarFields;
				}),
			"normalized field assignments remain in the typed CFG"))
	{
		return EXIT_FAILURE;
	}
	const auto* UnrelatedEntry =
		Cache.GetCandidate(FindFunctionIndex(*Module, "unrelated_index_loop"));
	if (!Expect(UnrelatedEntry && !UnrelatedEntry->bPreferVmForExecution,
			"separate indexed loop does not disable direct polymorphic transform"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
		VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		if (!ExpectRun(*Module, Backend, "284137\n1013148\n98\n",
				"random indirect field/receiver loops preserve semantics"))
		{
			return EXIT_FAILURE;
		}
	}
	std::ostringstream Output;
	VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
	ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
	if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
			"execute optimized random indirect loops")
		|| !Expect(Output.str() == "284137\n1013148\n98\n",
			"optimized random indirect loop output")
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		|| !Expect(Vm.GetJitStats().CostModelVmFallbacks == 0,
			"complete random-loop coverage avoids VM cost fallback")
		|| !Expect(Vm.GetJitStats().ArrayFastPathGets >= 101023u + 91126u,
			"both random selector arrays use typed storage")
		|| !Expect(Vm.GetJitStats().ScalarMethodFastPathI32 >= 101023u,
			"polymorphic transform batches generation-qualified scalar calls")
	#else
		|| !Expect(Vm.GetJitStats().CostModelVmFallbacks >= 1,
			"polymorphic loop uses VM fallback without a native backend")
		|| !Expect(Vm.GetJitStats().Dispatches < 100u,
			"portable VM fallback suppresses per-iteration Linear redispatch")
		|| !Expect(Vm.GetJitStats().ArrayFastPathGets >= 91126u,
			"portable typed CFG still accelerates the random field selector")
	#endif
	)
	{
		return EXIT_FAILURE;
	}
}

{
	// The compact direct-state virtual fusion must preserve the bytecode VM at
	// its boundaries: a zero-trip loop performs no speculative access, while
	// checked recurrence overflow and a negative selector retain the exact
	// diagnostic identity and source location.
	const std::string Prefix =
		"interface EdgeOp { virtual int step(int value)=0; }; "
		"class EdgeA:EdgeOp { public:int step(int value){return value+1;} }; "
		"class EdgeB:EdgeOp { public:int step(int value){return value+2;} }; ";
	if (!ExpectDifferentialRun(Prefix
			+ "void main(){array<int> choices={0,1};EdgeOp a=EdgeA();EdgeOp b=EdgeB();"
			  "EdgeOp current=a;int state=1;int total=0;int i=0;while(i<0){"
			  "state=(state*3+1)%2;int choice=choices[state];if(choice==0)current=a;"
			  "else current=b;total=current.step(total);i++;}print(total+state);}",
			"direct-state virtual zero-trip parity")
		|| !ExpectDifferentialRun(Prefix
			+ "void main(){array<int> choices={0,1};EdgeOp a=EdgeA();EdgeOp b=EdgeB();"
			  "EdgeOp current=a;int state=-1;int total=0;int i=0;while(i<1){"
			  "state=(state*1+0)%2;int choice=choices[state];if(choice==0)current=a;"
			  "else current=b;total=current.step(total);i++;}print(total);}",
			"direct-state virtual negative-index diagnostic parity")
		|| !ExpectDifferentialRun(Prefix
			+ "void main(){array<int> choices={0,1};EdgeOp a=EdgeA();EdgeOp b=EdgeB();"
			  "EdgeOp current=a;int state=2147483647;int total=0;int i=0;while(i<1){"
			  "state=(state*2+0)%2;int choice=choices[state];if(choice==0)current=a;"
			  "else current=b;total=current.step(total);i++;}print(total);}",
			"direct-state virtual recurrence-overflow diagnostic parity"))
	{
		return EXIT_FAILURE;
	}
}

#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
{
	// Mixed can compile the outer loop before a receiver implementation that is
	// absent from the current selector data has ever been called. The loop
	// preflight must compile every statically proven receiver dependency so one
	// cold implementation cannot disable the whole fusion for this invocation.
	const auto Module = CompileSource(
		"interface SparseOp { virtual int step(int value)=0; }; "
		"class SparseA:SparseOp { public:int step(int value){return (value+1)%100003;} }; "
		"class SparseB:SparseOp { public:int step(int value){return (value+2)%100003;} }; "
		"class SparseC:SparseOp { public:int step(int value){return (value+3)%100003;} }; "
		"class SparseD:SparseOp { public:int step(int value){return (value+4)%100003;} }; "
		"int work(){array<int> choices={0,1,3,1,0};SparseOp a=SparseA();"
		"SparseOp b=SparseB();SparseOp c=SparseC();SparseOp d=SparseD();"
		"SparseOp current=a;int state=1;int total=0;int i=0;while(i<1000){"
		"state=(state*3+1)%5;int choice=choices[state];if(choice==0)current=a;"
		"else if(choice==1)current=b;else if(choice==2)current=c;else current=d;"
		"total=current.step(total);i++;}return total+state;}void main(){print(work());}");
	if (!Expect(static_cast<bool>(Module),
			"compile sparse-selector Mixed dependency fixture"))
	{
		return EXIT_FAILURE;
	}
	const FExecutionCapture Baseline =
		RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
	const FExecutionCapture Candidate =
		RunCapture(*Module, VeroScript::EExecutionBackend::Mixed);
	if (!Expect(Baseline.bSuccess && Candidate.bSuccess
			&& Candidate.Output == Baseline.Output,
			"sparse-selector Mixed fusion preserves output")
		|| !Expect(Candidate.JitStats.ScalarMethodFastPathI32 >= 1000u,
			"sparse-selector Mixed fusion compiles cold receiver dependencies"))
	{
		return EXIT_FAILURE;
	}
}
#endif
