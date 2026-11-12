// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptJitBackendTests / host and reload integration suite. Included by the owning translation unit.

	{
		const auto Module = CompileSource(
			"class Point {public:int x=3;int y=5;"
			"int explicit_update(int delta){int local=delta;local++;local+=3;local+=delta;"
			"int old=this.x++;int before=++this.y;int assigned=(this.x=(this.x+local)%997);"
			"this.y=(this.y+assigned)%991;return ((old+before+assigned)^(~delta))&255;}"
			"int implicit_update(int delta){x=x+1;y=y+1;return (x+y+delta)%997;}"
			"int constant_math(int value){this.x=(this.x+1)%997;this.y=(this.y+value%(-31))%991;"
			"return (((value/7)^(value%7))+(this.x<<1)+(this.y>>1))&255;}};\n"
			"class Child:public Point{public:int marker=17;};\n"
			"int explicit_work(int count){Child point=Child();int total=0;"
			"for(int i=0;i<count;i++){total+=point.explicit_update(i&31);}return total+point.x+point.y+point.marker;}\n"
			"int implicit_work(int count){Point point=Point();int total=0;"
			"for(int i=0;i<count;i++){total+=point.implicit_update(i&31);}return total+point.x+point.y;}\n"
			"int constant_work(int count){Point point=Point();int total=0;"
			"for(int i=0;i<count;i++){total+=point.constant_math((i&511)-255);}return total+point.x+point.y;}\n"
			"void main(){print(explicit_work(0));print(explicit_work(1));print(explicit_work(271));"
			"print(implicit_work(271));print(constant_work(271));}\n");
		if (!Expect(static_cast<bool>(Module), "compile canonical and explicit receiver method expressions")) return EXIT_FAILURE;
		const auto Duplicate = VeroScript::VeroScriptCompile::CompileSource(
			"class Base {public:int value=3;};class Derived:public Base{public:int value=7;};",
			"<duplicate_inherited_instance_field>", VeroScript::Test::MakeCompileOptions());
		if (!Expect(!Duplicate.bSuccess, "inherited instance field keys remain unambiguous for method field borrowing")) return EXIT_FAILURE;
		const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
		if (!Expect(Reference.bSuccess, "VM executes receiver expression reference")) return EXIT_FAILURE;
		for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Reference.Output,
					"normalized receiver expressions preserve assignment results, increments, inherited fields and signed arithmetic"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		const auto Native = RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		if (!Expect(Native.bSuccess && Native.JitStats.NativeTypedLoopExecutions >= 3u,
				"canonical, explicit-this and constant arithmetic methods actually execute native loop kernels"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		for (const auto Name : {"explicit_work", "implicit_work", "constant_work"})
		{
			const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
			if (!Expect(Entry && !Entry->bPreferVmForExecution && Entry->TypedLoopPlans &&
							std::any_of(Entry->TypedLoopPlans->Plans.begin(), Entry->TypedLoopPlans->Plans.end(),
								[](const auto &Plan)
								{
									return Plan.NativeI32Code && !Plan.FieldMethods.empty();
								}),
					"receiver normalization retains proven complete native field coverage"))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
	}

	{
		const auto Module = CompileSource(
			"class Point { public: int x=3; int y=5; "
			"int update(int delta){x=(x+delta)%997;y=(y+x)%991;return (x^y)&127;} };\n"
			"class ChildPoint : public Point {public: int marker=7;};\n"
			"int work(int count){ChildPoint value=ChildPoint();int total=0;"
			"for(int i=0;i<count;i++){total+=value.update((i*3)&31);}"
			"return total+value.x+value.y+value.marker;}\n"
			"void main(){print(work(0));print(work(1));print(work(271));}\n");
		if (!Expect(static_cast<bool>(Module), "compile general multi-field method loop")) return EXIT_FAILURE;
		const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
		for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (!Expect(Reference.bSuccess && ExpectRun(*Module, Backend, Reference.Output,
												  "multi-field methods with arithmetic arguments preserve inherited field semantics"),
					"general multi-field method parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		const auto Native = RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		if (!Expect(Native.bSuccess && Native.JitStats.NativeTypedLoopExecutions != 0,
				"multi-field method executes its native loop kernel"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, "work"));
		if (!Expect(Entry && !Entry->bPreferVmForExecution && Entry->TypedLoopPlans && std::any_of(Entry->TypedLoopPlans->Plans.begin(), Entry->TypedLoopPlans->Plans.end(), [](const auto &Plan)
																						   {
																							   return !Plan.FieldMethods.empty() && Plan.NativeI32Code != nullptr;
																						   }),
				"general rooted field-method loop has complete native coverage"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		const auto Module = CompileSource(
			"struct State {int left=0;int right=0;"
			"int update(int amount,int salt){left=(left*3+amount+salt)%65521;"
			"right=(right+left*5+amount)%65521;return ((left+right)^salt)&1023;}"
			"int many(int a,int b,int c,int d,int e,int f,int g,int h){"
			"int delta=(a+b+c+d+e+f+g+h)&31;left=(left+delta)%65521;"
			"right=(right+left)%65521;return (left^right)&255;}"
			"int score(){return ((left&127)*(right&127))&255;}};\n"
			"int pair(int count,int seed){State value=State();value.left=seed;int total=0;"
			"for(int i=0;i<count;i++){total+=value.update((i*7+seed)&511,(i^seed)&127);total+=value.score();}"
			"return total+value.left+value.right;}\n"
			"int many_work(int count){State value=State();int total=0;"
			"for(int i=0;i<count;i++){total+=value.many(i&3,2,3,4,5,6,7,(i*3)&7);}"
			"return total+value.left+value.right;}\n"
			"void main(){print(pair(0,17));print(pair(1,31));print(pair(271,17));"
			"print(many_work(0));print(many_work(1));print(many_work(271));}\n");
		if (!Expect(static_cast<bool>(Module), "compile bounded multi-argument field methods")) return EXIT_FAILURE;
		const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
		for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (!Expect(Reference.bSuccess && ExpectRun(*Module, Backend, Reference.Output,
												  "multi-argument methods preserve computed arguments, local values and ordered field writes"),
					"multi-argument field method parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		const auto Native = RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		if (!Expect(Native.bSuccess && Native.JitStats.NativeTypedLoopExecutions >= 2u,
				"two and eight argument field methods execute native loop kernels"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		for (const auto Name : {"pair", "many_work"})
		{
			const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
			if (!Expect(Entry && !Entry->bPreferVmForExecution && Entry->TypedLoopPlans &&
							std::any_of(Entry->TypedLoopPlans->Plans.begin(), Entry->TypedLoopPlans->Plans.end(),
								[](const auto &Plan)
								{
									return Plan.NativeI32Code && std::any_of(
																	 Plan.FieldMethods.begin(), Plan.FieldMethods.end(), [](const auto &Method)
																	 {
																		 return Method.ArgumentCount > 1u;
																	 });
								}),
					"bounded multi-argument field method has complete native coverage"))
			{
				return EXIT_FAILURE;
			}
		}
		const auto* PairEntry = Cache.GetCandidate(FindFunctionIndex(*Module, "pair"));
		if (!Expect(std::any_of(PairEntry->TypedLoopPlans->Plans.begin(), PairEntry->TypedLoopPlans->Plans.end(),
						[](const auto &Plan)
						{
							return std::any_of(Plan.FieldMethods.begin(), Plan.FieldMethods.end(),
								[](const auto &Method)
								{
									return Method.ArgumentCount == 0u;
								});
						}),
				"zero-argument field getter shares native method coverage"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		const auto Module = CompileSource(
			"class Point {public: int x=7;int y=12;\n"
			"int update(int increment,int divisor){x=x+increment;y=y/divisor;return x+y;}\n"
			"int inc_failure(int arg){this.x++;this.y=this.y/arg;return x+y;}\n"
			"int local_failure(int arg){this.x++;int local=arg;local++;this.y=local;return x+y;}\n"
			"int neg_failure(int arg){this.x++;this.y=-arg;return x+y;}\n"
			"int shift_failure(int arg){this.x++;this.y=this.y<<arg;return x+y;} };\n"
			"int fail(Point point,int count,int divisor){int total=0;"
			"for(int i=0;i<count;i++){total+=point.update(3,divisor);}return total;}\n"
			"int fail_inc(Point point,int count,int arg){int total=0;for(int i=0;i<count;i++){total+=point.inc_failure(arg);}return total;}\n"
			"int fail_local(Point point,int count,int arg){int total=0;for(int i=0;i<count;i++){total+=point.local_failure(arg);}return total;}\n"
			"int fail_neg(Point point,int count,int arg){int total=0;for(int i=0;i<count;i++){total+=point.neg_failure(arg);}return total;}\n"
			"int fail_shift(Point point,int count,int arg){int total=0;for(int i=0;i<count;i++){total+=point.shift_failure(arg);}return total;}\n");
		if (!Expect(static_cast<bool>(Module), "compile native field error commit fixture")) return EXIT_FAILURE;
		struct FFailureCase { const char* Name; std::int32_t Argument; std::int32_t ExpectedX; };
		for (const auto& Case : {FFailureCase{"fail", 0, 10}, FFailureCase{"fail_inc", 0, 8},
			FFailureCase{"fail_local", std::numeric_limits<std::int32_t>::max(), 8},
			FFailureCase{"fail_neg", std::numeric_limits<std::int32_t>::min(), 8}, FFailureCase{"fail_shift", 32, 8}})
		{
			std::uint32_t ErrorLine = 0, ErrorColumn = 0;
			std::string ErrorId;
			for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
			{
				std::ostringstream Output, Errors;
				std::istringstream Input;
				VeroScript::VeroScriptVm Vm(Output, Input, Errors);
				VeroScript::Test::Configure(Vm);
				ConfigureBackend(Vm);
				Vm.SetExecutionBackend(Backend);
				VeroScript::FObjectHandle Object;
				if (!Expect(Vm.ConstructObject(*Module, "Point", {}, Object, nullptr) && Object,
						"construct retained receiver for field error commit"))
				{
					return EXIT_FAILURE;
				}
				const auto InitialVersion = Object->MutationVersion;
				const bool Ok = Vm.ExecuteFreeFunction(*Module, Case.Name,
					{Object, std::int32_t{200000}, Case.Argument}, nullptr);
				if (!Expect(!Ok && Vm.HasError() && !Object->bDestroyed && Object->Fields.size() == 2u &&
								std::get<std::int32_t>(Object->Fields[0]) == Case.ExpectedX && std::get<std::int32_t>(Object->Fields[1]) == 12 &&
								Object->MutationVersion == InitialVersion + 1u,
						"failed field kernel preserves prior write and commits its version exactly once"))
				{
					return EXIT_FAILURE;
				}
				const auto& Error = Vm.GetError();
				if (Backend == VeroScript::EExecutionBackend::Vm)
				{
					ErrorId = Error.MessageId; ErrorLine = Error.Location.Line; ErrorColumn = Error.Location.Column;
				}
				else if (!Expect(Error.MessageId == ErrorId && Error.Location.Line == ErrorLine && Error.Location.Column == ErrorColumn,
							 "native field failure retains original callee diagnostic location"))
				{
					return EXIT_FAILURE;
				}
				#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
				if (Backend == VeroScript::EExecutionBackend::Jit &&
					!Expect(Vm.GetJitStats().NativeTypedLoopExecutions != 0u,
						"field error commit fixture actually executes native loop"))
				{
					return EXIT_FAILURE;
				}
				#endif
			}
		}
	}

	{
		const auto Module = CompileSource(
			"struct Point {int x=0;int y=0;int update(int amount){"
			"x=(x+amount)%65521;y=(y+x)%65521;return (x^y)&255;}};\n"
			"int signed_limit(){Point point=Point();point.x=17;int total=0;"
			"for(int i=0;i<4294967296i64;i++){total+=point.update(i);if(i>=2)break;}"
			"return total+point.x+point.y;}\n"
			"int unsigned_limit(){Point point=Point();point.x=17;int total=0;"
			"for(uint32 i=0u;i<4294967296u64;i++){total+=point.update(to_int(i));if(i>=2u)break;}"
			"return total+point.x+point.y;}\n"
			"void main(){print(signed_limit());print(unsigned_limit());}\n");
		if (!Expect(static_cast<bool>(Module), "compile wide nonempty field-loop bounds")) return EXIT_FAILURE;
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "159\n159\n",
					"wide constants cannot be truncated by the zero-trip field-loop guard"))
			{
				return EXIT_FAILURE;
			}
		}
		// Bytecode validation/runtime still has to diagnose a mixed-sign bound;
		// a speculative zero-trip check must not swallow that original error.
		bool Changed = false;
		for (auto& Constant : Module->Constants)
		{
			if (Constant.Kind == VeroScript::EConstantKind::I64 && Constant.I64 == 4294967296ll)
			{
				Constant.Kind = VeroScript::EConstantKind::U64;
				Constant.U64 = 4294967296ull;
				Changed = true;
			}
		}
		if (!Expect(Changed, "retag wide signed bound for mixed-sign bytecode regression")) return EXIT_FAILURE;
		const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
		if (!Expect(!Reference.bSuccess && !Reference.ErrorMessage.empty(), "VM diagnoses mixed-sign wide bound")) return EXIT_FAILURE;
		for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			const auto Candidate = RunCapture(*Module, Backend);
			if (!Expect(!Candidate.bSuccess && Candidate.Output == Reference.Output && Candidate.ErrorMessageId == Reference.ErrorMessageId &&
							Candidate.ErrorMessage == Reference.ErrorMessage && Candidate.ErrorLine == Reference.ErrorLine && Candidate.ErrorColumn == Reference.ErrorColumn,
					"zero-trip check preserves VM error for a wide unsigned constant with signed counter"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const auto Module = CompileSource(
			"class BaseValue {public: int bias=0; virtual int evaluate(int value)=0;};\n"
			"class PlusValue : public BaseValue {public: int evaluate(int value) override{return (value+bias)&255;}};\n"
			"class XorValue : public BaseValue {public: int evaluate(int value) override{return (value^bias)&127;}};\n"
			"int traverse(array<BaseValue> values,int count,int salt){int total=0;"
			"for(int i=0;i<count;i++){total+=values[i].evaluate(i+salt);}return total;}\n"
			"int empty_traverse(array<BaseValue> values,int count){int counter=7;int total=19;"
			"for(;counter<count;counter++){total+=values[counter].evaluate(counter+3);}return counter*100+total;}\n"
			"void main(){array<BaseValue> values=array<BaseValue>();"
			"for(int i=0;i<271;i++){BaseValue value=nullptr;"
			"if((i&1)==0){value=PlusValue();}else{value=XorValue();}value.bias=i*3;values.push(value);}"
			"print(empty_traverse(values,0));print(traverse(values,0,7));print(traverse(values,1,7));print(traverse(values,271,19));}\n");
		if (!Expect(static_cast<bool>(Module), "compile field-aware polymorphic array traversal")) return EXIT_FAILURE;
		const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
		for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (!Expect(Reference.bSuccess && ExpectRun(*Module, Backend, Reference.Output,
												  "polymorphic array traversal preserves actual overrides and fields"),
					"field-aware polymorphic traversal parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		const auto Native = RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		if (!Expect(Native.bSuccess && Native.JitStats.NativeTypedLoopExecutions != 0,
				"field-aware polymorphic traversal executes native loop"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, "traverse"));
		if (!Expect(Entry && !Entry->bPreferVmForExecution && Entry->TypedLoopPlans && std::any_of(Entry->TypedLoopPlans->Plans.begin(), Entry->TypedLoopPlans->Plans.end(), [](const auto &Plan)
																						   {
																							   return Plan.NativeI32Code && std::any_of(Plan.GenericPureMethods.begin(), Plan.GenericPureMethods.end(),
																																[](const auto &Method)
																																{
																																	return !Method.FieldNameIndices.empty();
																																});
																						   }),
				"field-aware polymorphic traversal has a complete native chain"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// this.foo(scalar) via CallBoundMethod uses GetJitThisObject (LoadThis is a no-op).
		const std::string Source =
			"class Acc {\n"
			"public:\n"
			"\tint n = 0;\n"
			"\tvoid add(int x) { n = n + x; }\n"
			"\tvoid bump() { this.add(1); }\n"
			"\tint get() { return n; }\n"
			"}\n"
			"void main() {\n"
			"\tauto a = Acc();\n"
			"\ta.bump();\n"
			"\ta.bump();\n"
			"\ta.add(3);\n"
			"\tprint(a.get());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile this-call suite"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t MainIndex = FindFunctionIndex(*Module, "main");
		std::size_t ObservableBoundCalls = 0;
		if (MainIndex != static_cast<std::uint16_t>(-1))
		{
			const auto& Code = Module->Functions[MainIndex].Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const auto Op = static_cast<VeroScript::EOpcode>(Code[Ip]);
				if (Op == VeroScript::EOpcode::CallBoundMethod)
				{
					++ObservableBoundCalls;
				}
				const std::size_t Size = VeroScript::Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Code.size())
				{
					break;
				}
				Ip += Size;
			}
		}
		if (!Expect(MainIndex != static_cast<std::uint16_t>(-1) && ObservableBoundCalls >= 2,
				"instance calls remain observable for runtime interception"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "add", VeroScript::AsmJit::EJitCompileStatus::Ok, "Acc.add Ok")
			|| !ExpectStatus(Cache, *Module, "bump", VeroScript::AsmJit::EJitCompileStatus::Ok, "Acc.bump Ok")
			|| !ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "Acc.get Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : {"add", "bump", "get"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			const std::string Msg = std::string("this-call native leaf ") + Name;
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Msg.c_str()))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "5\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "this-call suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// The raw nested-call fast path may borrow consumed operand-stack slots,
		// but script parameters remain by-value locals from the caller's viewpoint.
		const std::string Source =
			"class ParamCopy {\n"
			"public:\n"
			"\tint mutate(int x) { x = x + 1; return x; }\n"
			"\tint verify() { int x = 4; int y = this.mutate(x); return x * 10 + y; }\n"
			"}\n"
			"void main() { auto value = ParamCopy(); print(value.verify()); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile scalar parameter-copy suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "45\n", "scalar parameter-copy semantics"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Delegating wrappers are fused only for a statically-bound, non-virtual
		// add(1). Different constants and virtual dispatch must retain their calls.
		const std::string Source =
			"class SafeFold {\n"
			"public:\n"
			"\tint n = 0;\n"
			"\tvoid add(int x) { n = n + x; }\n"
			"\tvoid add_two() { this.add(2); }\n"
			"\tvirtual void add_one_virtual() { this.add(1); }\n"
			"\tdouble d = 0.0;\n"
			"\tvoid add_mixed(int x) { d = d + x; }\n"
			"\tvoid add_one_mixed() { this.add_mixed(1); }\n"
			"\tint get() { return n; }\n"
			"}\n"
			"void main() { auto a = SafeFold(); a.add_two(); a.add_one_virtual(); "
			"a.add_one_mixed(); print(a.get()); print(to_string(a.d)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile guarded delegated-inc suite"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t MainIndex = FindFunctionIndex(*Module, "main");
		std::size_t FusedIncFields = 0;
		if (MainIndex != static_cast<std::uint16_t>(-1))
		{
			const auto& Code = Module->Functions[MainIndex].Code;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const auto Op = static_cast<VeroScript::EOpcode>(Code[Ip]);
				FusedIncFields += Op == VeroScript::EOpcode::IncField ? 1u : 0u;
				const std::size_t Size = VeroScript::Bytecode::InstructionSize(Op);
				if (Size == 0 || Ip + Size > Code.size())
				{
					break;
				}
				Ip += Size;
			}
		}
		if (!Expect(MainIndex != static_cast<std::uint16_t>(-1) && FusedIncFields == 0,
				"delegated-inc fusion preserves constants and virtual dispatch")
					|| !ExpectRun(*Module, VeroScript::EExecutionBackend::Vm, "3\n1\n",
						"guarded delegated-inc VM semantics")
							|| !ExpectRun(*Module, VeroScript::EExecutionBackend::Mixed, "3\n1\n",
								"guarded delegated-inc Mixed semantics")
									|| !ExpectRun(*Module, VeroScript::EExecutionBackend::Jit, "3\n1\n",
										"guarded delegated-inc Jit semantics"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Nested native method must use callee this, not the outer method frame this.
		const std::string Source =
			"class Inner {\n"
			"public:\n"
			"\tint n = 3;\n"
			"\tint get() { return n; }\n"
			"}\n"
			"class Outer {\n"
			"public:\n"
			"\tint n = 10;\n"
			"\tint fetch(Inner i) { return i.get(); }\n"
			"}\n"
			"void main() {\n"
			"\tOuter o = Outer();\n"
			"\tInner i = Inner();\n"
			"\tprint(o.fetch(i));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile nested-this suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "Inner.get Ok"))
		{
			return EXIT_FAILURE;
		}
		// Outer.fetch takes an object param — may be linear/interpret, not native. Still Ok if present.
		{
			const std::uint16_t FetchIdx = FindFunctionIndex(*Module, "fetch");
			if (!Expect(FetchIdx != static_cast<std::uint16_t>(-1), "find Outer.fetch"))
			{
				return EXIT_FAILURE;
			}
			(void)Cache.EnsureCompiled(*Module, FetchIdx);
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		{
			const std::uint16_t GetIdx = FindFunctionIndex(*Module, "get");
			const VeroScript::AsmJit::FJitFunctionEntry* const GetEntry = Cache.GetCandidate(GetIdx);
			if (!Expect(
					GetEntry != nullptr && GetEntry->bNativeLeaf && GetEntry->NativeCode != nullptr,
					"Inner.get native leaf"))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "3\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "nested-this suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Nested this-calls + bool arg + void CallMethod-from-this stay native.
		const std::string Source =
			"class Acc {\n"
			"public:\n"
			"\tint n = 0;\n"
			"\tbool ok = false;\n"
			"\tvoid add(int x) { n = n + x; }\n"
			"\tint get() { return n; }\n"
			"\tvoid set_flag(bool f) { ok = f; }\n"
			"\tvoid mark() { this.set_flag(true); }\n"
			"\tvoid nest() { this.add(this.get() + 1); this.mark(); }\n"
			"\tint get_flag() { return ok ? 1 : 0; }\n"
			"}\n"
			"void main() {\n"
			"\tauto a = Acc();\n"
			"\ta.nest();\n"
			"\ta.nest();\n"
			"\tprint(a.get());\n"
			"\tprint(a.get_flag());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile nested this-call suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "nest", VeroScript::AsmJit::EJitCompileStatus::Ok, "Acc.nest Ok")
			|| !ExpectStatus(Cache, *Module, "mark", VeroScript::AsmJit::EJitCompileStatus::Ok, "Acc.mark Ok")
			|| !ExpectStatus(Cache, *Module, "set_flag", VeroScript::AsmJit::EJitCompileStatus::Ok, "Acc.set_flag Ok")
			|| !ExpectStatus(Cache, *Module, "add", VeroScript::AsmJit::EJitCompileStatus::Ok, "Acc.add Ok")
			|| !ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "Acc.get Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : {"nest", "mark", "set_flag", "add", "get"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			const std::string Msg = std::string("nested this-call native leaf ") + Name;
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Msg.c_str()))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		// nest: 0→1, then 1→3; flag set.
		const std::string Expected = "3\n1\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "nested this-call suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Virtual this.value() uses CallMethod-from-this under native I32 CFG.
		const std::string Source =
			"interface Counter {\n"
			"\tvirtual int value() = 0;\n"
			"\tvirtual void bump() = 0;\n"
			"}\n"
			"class Impl : Counter {\n"
			"public:\n"
			"\tint n = 0;\n"
			"\tint value() { return n; }\n"
			"\tvoid bump() { n = n + 1; }\n"
			"\tint drive() {\n"
			"\t\tthis.bump();\n"
			"\t\tthis.bump();\n"
			"\t\treturn this.value();\n"
			"\t}\n"
			"}\n"
			"void main() {\n"
			"\tauto x = Impl();\n"
			"\tprint(x.drive());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile this CallMethod suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "drive", VeroScript::AsmJit::EJitCompileStatus::Ok, "Impl.drive Ok")
			|| !ExpectStatus(Cache, *Module, "bump", VeroScript::AsmJit::EJitCompileStatus::Ok, "Impl.bump Ok")
			|| !ExpectStatus(Cache, *Module, "value", VeroScript::AsmJit::EJitCompileStatus::Ok, "Impl.value Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		{
			const std::uint16_t DriveIdx = FindFunctionIndex(*Module, "drive");
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(DriveIdx);
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					"Impl.drive native leaf"))
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
			if (!ExpectRun(*Module, Backend, Expected, "this CallMethod suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Mixed this-call with int + double args packs via Mixed helper.
		const std::string Source =
			"class MixAcc {\n"
			"public:\n"
			"\tint n = 0;\n"
			"\tdouble d = 0.0;\n"
			"\tvoid mix(int a, double b) { n = n + a; d = d + b; }\n"
			"\tvoid apply() { this.mix(2, 3.5); }\n"
			"\tint getN() { return n; }\n"
			"\tdouble getD() { return d; }\n"
			"}\n"
			"void main() {\n"
			"\tauto m = MixAcc();\n"
			"\tm.apply();\n"
			"\tm.apply();\n"
			"\tprint(m.getN());\n"
			"\tprint(to_int(m.getD()));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile mixed this-call suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "mix", VeroScript::AsmJit::EJitCompileStatus::Ok, "MixAcc.mix Ok")
			|| !ExpectStatus(Cache, *Module, "apply", VeroScript::AsmJit::EJitCompileStatus::Ok, "MixAcc.apply Ok")
			|| !ExpectStatus(Cache, *Module, "getN", VeroScript::AsmJit::EJitCompileStatus::Ok, "MixAcc.getN Ok")
			|| !ExpectStatus(Cache, *Module, "getD", VeroScript::AsmJit::EJitCompileStatus::Ok, "MixAcc.getD Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		for (const char* Name : {"mix", "apply", "getN", "getD"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			const std::string Msg = std::string("mixed this-call native leaf ") + Name;
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr,
					Msg.c_str()))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "4\n7\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "mixed this-call suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// U32 / F64 this-call suites (homogeneous native CFG).
		const std::string Source =
			"class UAcc {\n"
			"public:\n"
			"\tunsigned n = 0;\n"
			"\tvoid add(unsigned x) { n = n + x; }\n"
			"\tvoid bump() { this.add(1U); }\n"
			"\tunsigned get() { return n; }\n"
			"}\n"
			"class DAcc {\n"
			"public:\n"
			"\tdouble n = 0.0;\n"
			"\tvoid add(double x) { n = n + x; }\n"
			"\tvoid bump() { this.add(1.5); }\n"
			"\tdouble get() { return n; }\n"
			"}\n"
			"void main() {\n"
			"\tauto u = UAcc();\n"
			"\tu.bump();\n"
			"\tu.bump();\n"
			"\tprint(u.get());\n"
			"\tauto d = DAcc();\n"
			"\td.bump();\n"
			"\td.bump();\n"
			"\tprint(to_int(d.get()));\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile u32/f64 this-call suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "add", VeroScript::AsmJit::EJitCompileStatus::Ok, "UAcc/DAcc.add Ok")
			|| !ExpectStatus(Cache, *Module, "bump", VeroScript::AsmJit::EJitCompileStatus::Ok, "UAcc/DAcc.bump Ok")
			|| !ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "UAcc/DAcc.get Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		// FindFunctionIndex returns the first match; compile both type methods and assert all leaves.
		for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
		{
			const std::string& Name = Module->Functions[Index].Name;
			if (Name.find("UAcc::") == std::string::npos && Name.find("DAcc::") == std::string::npos)
			{
				continue;
			}
			if (Name.find("::add#") == std::string::npos
				&& Name.find("::bump#") == std::string::npos
				&& Name.find("::get#") == std::string::npos)
			{
				continue;
			}
			(void)Cache.EnsureCompiled(*Module, static_cast<std::uint16_t>(Index));
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(static_cast<std::uint16_t>(Index));
			const std::string Msg = std::string("u32/f64 this-call native leaf ") + Name;
			const VeroScript::AsmJit::EJitScalarAbi ExpectedAbi =
				Name.find("DAcc::") != std::string::npos
				? VeroScript::AsmJit::EJitScalarAbi::F64
				: VeroScript::AsmJit::EJitScalarAbi::U32;
			if (!Expect(
					Entry != nullptr && Entry->bNativeLeaf && Entry->NativeCode != nullptr
						&& Entry->ScalarAbi == ExpectedAbi,
					Msg.c_str()))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "2\n3\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "u32/f64 this-call suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Returning `this` is non-scalar — must not be a native leaf.
		const std::string Source =
			"class Box {\n"
			"public:\n"
			"\tint n = 1;\n"
			"\tBox self() { return this; }\n"
			"\tint get() { return n; }\n"
			"}\n"
			"void main() {\n"
			"\tauto b = Box();\n"
			"\tprint(b.self().get());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile orphan/this-return suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "self", VeroScript::AsmJit::EJitCompileStatus::Ok, "Box.self Ok")
			|| !ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "Box.get Ok"))
		{
			return EXIT_FAILURE;
		}
		{
			const std::uint16_t SelfIdx = FindFunctionIndex(*Module, "self");
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(SelfIdx);
			if (!Expect(
					Entry != nullptr
						&& Entry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
						&& !Entry->bNativeLeaf,
					"Box.self uses FValue LinearTrampoline (object return, not native leaf)"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT
		{
			const std::uint16_t SelfIdx = FindFunctionIndex(*Module, "self");
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(SelfIdx);
			if (!Expect(
					Entry != nullptr && !Entry->bNativeLeaf,
					"Box.self must not be native leaf"))
			{
				return EXIT_FAILURE;
			}
		}
		#endif
		const std::string Expected = "1\n";
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, Expected, "orphan/this-return suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// U32 this-field IncField fusion (n = n + 1U / n++) stays a native leaf.
		const std::string Source =
			"class UCounter {\n"
			"public:\n"
			"\tunsigned n = 0;\n"
			"\tvoid inc() { n = n + 1U; }\n"
			"\tvoid tick() { n++; }\n"
			"\tunsigned get() { return n; }\n"
			"}\n"
			"void main() {\n"
			"\tauto c = UCounter();\n"
			"\tc.inc();\n"
			"\tc.tick();\n"
			"\tprint(c.get());\n"
			"}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile u32 IncField suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "inc", VeroScript::AsmJit::EJitCompileStatus::Ok, "UCounter.inc Ok")
			|| !ExpectStatus(Cache, *Module, "tick", VeroScript::AsmJit::EJitCompileStatus::Ok, "UCounter.tick Ok")
			|| !ExpectStatus(Cache, *Module, "get", VeroScript::AsmJit::EJitCompileStatus::Ok, "UCounter.get Ok"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		for (const char* Name : {"inc", "tick", "get"})
		{
			const std::uint16_t Index = FindFunctionIndex(*Module, Name);
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			const std::string Msg = std::string("u32 IncField native leaf ") + Name;
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
			if (!ExpectRun(*Module, Backend, Expected, "u32 IncField suite"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Native CFG analysis must not fall back solely because an otherwise scalar
		// function crosses the former 128-local or 4096-byte heuristic thresholds.
		std::ostringstream SourceBuilder;
		SourceBuilder << "int many_scalar_locals(int n) {\n";
		for (int I = 0; I < 140; ++I)
		{
			SourceBuilder << "\tint value" << I << " = " << I << ";\n";
		}
		SourceBuilder
			<< "\tint result = 0;\n"
			<< "\tfor (int i = 0; i < n; i++) { result++; }\n"
			<< "\treturn result + value139 - 139;\n"
			<< "}\n"
			<< "int large_scalar_body(int n) {\n\tint result = 0;\n";
		for (int I = 0; I < 900; ++I)
		{
			SourceBuilder << "\tif (n > " << I << ") { result += 1; }\n";
		}
		SourceBuilder
			<< "\treturn result;\n}\n"
			<< "void main() { print(many_scalar_locals(5)); print(large_scalar_body(0)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
			CompileSource(SourceBuilder.str());
		if (!Expect(static_cast<bool>(Module), "compile native CFG threshold suite"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t LocalIndex = FindFunctionIndex(*Module, "many_scalar_locals");
		const std::uint16_t LargeIndex = FindFunctionIndex(*Module, "large_scalar_body");
		if (!Expect(LocalIndex != static_cast<std::uint16_t>(-1)
				&& LargeIndex != static_cast<std::uint16_t>(-1),
			"find native CFG threshold functions")
			|| !Expect(Module->Functions[LocalIndex].LocalCount > 128,
				"threshold suite crosses former local limit")
			|| !Expect(Module->Functions[LargeIndex].Code.size() > 4096,
				"threshold suite crosses former code-size limit"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		if (!ExpectStatus(Cache, *Module, "many_scalar_locals",
				VeroScript::AsmJit::EJitCompileStatus::Ok, "many locals JIT compile")
			|| !ExpectStatus(Cache, *Module, "large_scalar_body",
				VeroScript::AsmJit::EJitCompileStatus::Ok, "large body JIT compile"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		if (!Expect(Cache.GetCandidate(LocalIndex) != nullptr
				&& Cache.GetCandidate(LocalIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
			"many locals retain NativeCfg")
			|| !Expect(Cache.GetCandidate(LargeIndex) != nullptr
				&& Cache.GetCandidate(LargeIndex)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"large scalar body retains NativeCfg"))
		{
			return EXIT_FAILURE;
		}
		#endif
		if (!ExpectRun(*Module, VeroScript::EExecutionBackend::Jit,
			"5\n0\n", "native CFG threshold execution"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Dynamic scalar calls may retain a second raw leaf only when the method
		// does not observe its receiver. The ordinary NativeCfg entry remains intact.
		const auto Module = CompileSource(
			"class FastLeaf { public:\n"
			"\tint value() { return 1; }\n"
			"\tint add(int x) { return x + 1; }\n"
			"\tunsigned int uvalue() { return 4294967295u; }\n"
			"\tunsigned int uadd(unsigned int x, unsigned int y) { return x + y; }\n"
			"\tbool less(unsigned int x) { return x < 10u; }\n"
			"\tbool flip(bool x) { return !x; }\n"
			"\tint identity(int x) { return x; }\n"
			"\tdouble identity_double(double x) { return x; }\n"
			"\tint64 add_i64(int64 x) { return x + 1i64; }\n"
			"\tuint64 identity_u64(uint64 x) { return x; }\n"
			"\tfloat scale_f32(float x) { return x * 2.f; }\n"
			"}\n"
			"void main() { FastLeaf v = FastLeaf(); print(v.value() + v.add(2)); "
			"print(v.uvalue()); print((int)v.uadd(2u, 3u)); print(v.less(9u) ? 1 : 0); "
			"print(v.flip(false) ? 1 : 0); "
			"print(v.identity(9)); print((int)v.identity_double(2.0)); "
			"print(v.add_i64(8i64) == 9i64 ? 1 : 0); "
			"print(v.identity_u64(7u64) == 7u64 ? 1 : 0); "
			"print(v.scale_f32(1.5f) == 3.f ? 1 : 0); }\n");
		if (!Expect(static_cast<bool>(Module), "compile direct scalar method leaf suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "4\n4294967295\n5\n1\n1\n9\n2\n1\n1\n1\n",
				"direct scalar method parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto IsPortableDirectKind = [](const VeroScript::AsmJit::EJitDirectScalarKind Kind)
		{
			switch (Kind)
			{
			case VeroScript::AsmJit::EJitDirectScalarKind::UnaryI64:
			case VeroScript::AsmJit::EJitDirectScalarKind::UnaryI64RightImm:
			case VeroScript::AsmJit::EJitDirectScalarKind::BinaryI64:
			case VeroScript::AsmJit::EJitDirectScalarKind::UnaryU64:
			case VeroScript::AsmJit::EJitDirectScalarKind::UnaryU64RightImm:
			case VeroScript::AsmJit::EJitDirectScalarKind::BinaryU64:
			case VeroScript::AsmJit::EJitDirectScalarKind::UnaryF32:
			case VeroScript::AsmJit::EJitDirectScalarKind::UnaryF32RightImm:
			case VeroScript::AsmJit::EJitDirectScalarKind::BinaryF32:
				return true;
			default:
				return false;
			}
		};
		for (const char* Name :
			{"value", "add", "uvalue", "uadd", "less", "flip", "identity",
				"identity_double", "add_i64", "identity_u64", "scale_f32"})
		{
			// Metadata descriptors also have a value() method. Resolve the test
			// receiver explicitly rather than selecting an unrelated builtin type.
			const std::uint16_t Index = FindFunctionIndex(*Module, Name, "FastLeaf");
			const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(Index);
			#if VEROSCRIPT_ASMJIT_A64
			if (std::string_view(Name) == "less" || std::string_view(Name) == "add_i64"
				|| std::string_view(Name) == "identity_u64" || std::string_view(Name) == "scale_f32")
			{
				if (!Expect(Entry && !Entry->bNativeLeaf,
						"mixed scalar signatures retain an owned fallback on A64"))
				{
					return EXIT_FAILURE;
				}

				continue;
			}
			#endif
			if (!Expect(Entry && Entry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
					&& (Entry->DirectScalarCode
						|| IsPortableDirectKind(Entry->DirectScalarKind))
					&& Entry->DirectScalarKind != VeroScript::AsmJit::EJitDirectScalarKind::None,
				"pure scalar method retains validated direct leaf"))
			{
				std::cerr << "method=" << Name << " index=" << Index << '\n';
				return EXIT_FAILURE;
			}
		}
		#endif
	}

	{
		// Portable direct leaves must preserve the same checked failure semantics
		// as the interpreter and the general Mixed CFG path.
		const std::pair<const char*, const char*> FailureCases[] = {
			{
				"interface IWide { virtual int64 add(int64 a, int64 b) = 0; }\n"
				"class Wide : IWide { int64 add(int64 a, int64 b) { return a + b; } }\n"
				"void main() { IWide value = Wide(); "
				"print(value.add(9223372036854775807i64, 1i64)); }\n",
				"diag.integer_overflow",
			},
			{
				"interface INarrow { virtual float divide(float a, float b) = 0; }\n"
				"class Narrow : INarrow { float divide(float a, float b) { return a / b; } }\n"
				"void main() { INarrow value = Narrow(); print(value.divide(1.f, 0.f)); }\n",
				"diag.division_by_zero",
			},
		};
		for (const auto& [Source, ExpectedError] : FailureCases)
		{
			const auto Module = CompileSource(Source);
			if (!Expect(static_cast<bool>(Module), "compile portable direct failure suite"))
			{
				return EXIT_FAILURE;
			}
			for (const VeroScript::EExecutionBackend Backend :
				{VeroScript::EExecutionBackend::Vm,
					VeroScript::EExecutionBackend::Mixed,
					VeroScript::EExecutionBackend::Jit})
			{
				const FExecutionCapture Capture = RunCapture(*Module, Backend);
				if (!Expect(!Capture.bSuccess && Capture.ErrorMessageId == ExpectedError,
						"portable direct failure parity"))
				{
					std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
						<< " expected=" << ExpectedError
						<< " actual=" << Capture.ErrorMessageId << ' ' << Capture.Errors;
					return EXIT_FAILURE;
				}
			}
		}
	}

	{
		// Interface dispatch must exercise each direct scalar ABI rather than only
		// attaching dormant raw leaves to concrete methods.
		const auto Module = CompileSource(
			"interface IDirectScalar {\n"
			"\tvirtual unsigned int uvalue() = 0;\n"
			"\tvirtual unsigned int bump(unsigned int x) = 0;\n"
			"\tvirtual bool less(unsigned int x) = 0;\n"
			"\tvirtual bool flip(bool x) = 0;\n"
			"\tvirtual int identity(int x) = 0;\n"
			"\tvirtual double identity_double(double x) = 0;\n"
			"}\n"
			"class DirectScalar : IDirectScalar {\n"
			"\tunsigned int uvalue() { return 1u; }\n"
			"\tunsigned int bump(unsigned int x) { return x + 1u; }\n"
			"\tbool less(unsigned int x) { return x < 10u; }\n"
			"\tbool flip(bool x) { return !x; }\n"
			"\tint identity(int x) { return x; }\n"
			"\tdouble identity_double(double x) { return x; }\n"
			"}\n"
			"int work(IDirectScalar value) {\n"
			"\tunsigned int current = 1u; int total = 0;\n"
			"\tfor (int i = 0; i < 4; i++) {\n"
			"\t\ttotal += (int)value.uvalue();\n"
			"\t\tcurrent = value.bump(current);\n"
			"\t\ttotal += value.less(current) ? 1 : 0;\n"
			"\t\ttotal += value.flip(false) ? 1 : 0;\n"
			"\t\ttotal += value.identity(i);\n"
			"\t\ttotal += (int)value.identity_double(1.0);\n"
			"\t}\n"
			"\treturn total + (int)current;\n"
			"}\n"
			"void main() { DirectScalar value = DirectScalar(); print(work(value)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile dynamic direct scalar ABI suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "27\n", "dynamic direct scalar ABI parity"))
			{
				return EXIT_FAILURE;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		const FExecutionCapture Capture =
			RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		if (!Expect(Capture.bSuccess
				&& Capture.JitStats.ScalarMethodFastPathU32 >= 8
				&& Capture.JitStats.ScalarMethodFastPathI32 >= 8
				&& Capture.JitStats.ScalarMethodFastPathF64 >= 4
		#if VEROSCRIPT_ASMJIT_X64
				&& Capture.JitStats.ScalarMethodFastPathMixed >= 4
		#endif
				,
			"all supported dynamic scalar ABIs execute direct leaves"))
		{
			std::cerr << "direct scalar stats i32="
				<< Capture.JitStats.ScalarMethodFastPathI32
				<< " u32=" << Capture.JitStats.ScalarMethodFastPathU32
				<< " f64=" << Capture.JitStats.ScalarMethodFastPathF64
				<< " mixed=" << Capture.JitStats.ScalarMethodFastPathMixed << '\n';
			return EXIT_FAILURE;
		}
		#endif
	}

	{
		// Stable-shape field reductions and statically bound scalar instance calls
		// keep the same checked arithmetic under VM, Mixed, and JIT. The JIT case
		// must enter the direct method ABI rather than rebuilding boxed call frames.
		const std::string Source =
			"class Counter {\n"
			"public:\n"
			"\tint value = 0;\n"
			"\tvoid add(int delta) { value = (value + delta) % 101; }\n"
			"};\n"
			"class DerivedCounter : public Counter { public: int marker = 1; };\n"
			"int method_work() { Counter value = Counter(); "
			"for (int i = 0; i < 100; i++) value.add(i); return value.value; }\n"
			"int field_work() { Counter value = Counter(); "
			"for (int i = 0; i < 100; i++) "
			"value.value = (value.value + (i % 17)) % 101; return value.value; }\n"
			"int inherited_work() { DerivedCounter value = DerivedCounter(); "
			"value.add(5); return value.value + value.marker; }\n"
			"void main() { print(method_work()); print(field_work()); "
			"print(inherited_work()); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module),
				"compile stable object scalar ABI suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "1\n78\n6\n",
					"stable object scalar ABI parity"))
			{
				return EXIT_FAILURE;
			}
		}
		const FExecutionCapture Capture =
			RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		if (!Expect(Capture.bSuccess, "bound object method JIT execution"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		if (!Expect(Capture.JitStats.ScalarMethodFastPathI32 >= 100,
				"bound object method enters direct i32 ABI"))
		{
			return EXIT_FAILURE;
		}
		#endif

		const auto Overflow = CompileSource(
			"class Counter { public: int value = 2147483647; };\n"
			"int fail() { Counter value = Counter(); "
			"value.value = (value.value + 1) % 97; return value.value; }\n"
			"void main() { print(fail()); }\n");
		if (!Expect(static_cast<bool>(Overflow),
				"compile stable object field overflow suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture OverflowCapture = RunCapture(*Overflow, Backend);
			if (!Expect(!OverflowCapture.bSuccess
					&& OverflowCapture.ErrorMessageId == "diag.integer_overflow",
					"stable object field overflow parity"))
			{
				return EXIT_FAILURE;
			}
		}

		const std::vector<std::string> ModuloOverflowSources = {
			"class Counter { public: int value = -2147483647 - 1; };\n"
			"int fail() { Counter value = Counter(); "
			"value.value = (value.value + 0) % -1; return value.value; }\n"
			"void main() { print(fail()); }\n",
			"int fail() { map<int, int> values = map<int, int>(); "
			"values[0] = -2147483647 - 1; int total = 0; "
			"total = (total + values[0]) % -1; return total; }\n"
			"void main() { print(fail()); }\n",
		};
		for (const std::string& ModuloOverflowSource : ModuloOverflowSources)
		{
			const auto ModuloOverflow = CompileSource(ModuloOverflowSource);
			if (!Expect(static_cast<bool>(ModuloOverflow),
					"compile fused signed modulo overflow suite"))
			{
				return EXIT_FAILURE;
			}
			for (const VeroScript::EExecutionBackend Backend :
				{VeroScript::EExecutionBackend::Vm,
					VeroScript::EExecutionBackend::Mixed,
					VeroScript::EExecutionBackend::Jit})
			{
				const FExecutionCapture ModuloCapture =
					RunCapture(*ModuloOverflow, Backend);
				if (!Expect(!ModuloCapture.bSuccess
						&& ModuloCapture.ErrorMessageId == "diag.integer_overflow",
						"fused signed modulo overflow parity"))
				{
					return EXIT_FAILURE;
				}
			}
		}
	}

	{
		// Primitive arrays receive typed storage only while the handle remains
		// function-local. Returning/ref-publishing or comparing the array is an
		// observable boundary and must retain canonical FValue-backed elements.
		const auto Local = CompileSource(
			"int work(int count) { array<int> values = array<int>(); values.reserve(count); "
			"for (int i = 0; i < count; i++) { values.push(i % 8); } "
			"int total = 0; for (int i = 0; i < count; i++) { total += values[i]; } "
			"return total + len(values); }\n"
			"void main() { print(work(10)); }\n");
		const auto Escaping = CompileSource(
			"array<int> make_values() { array<int> values = array<int>(); "
			"values.push(7); return values; }\n");
		const auto Comparing = CompileSource(
			"bool compare_values() { array<int> a = array<int>(); "
			"array<int> b = array<int>(); a.push(7); b.push(8); return a == b; }\n");
		if (!Expect(static_cast<bool>(Local) && static_cast<bool>(Escaping)
				&& static_cast<bool>(Comparing),
				"compile scalar array storage proof suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache LocalCache;
		LocalCache.PrepareModule(*Local, VeroScript::EExecutionBackend::Jit);
		const auto* const LocalEntry = LocalCache.GetCandidate(FindFunctionIndex(*Local, "work"));
		VeroScript::AsmJit::FJitCodeCache EscapingCache;
		EscapingCache.PrepareModule(*Escaping, VeroScript::EExecutionBackend::Jit);
		const auto* const EscapingEntry =
			EscapingCache.GetCandidate(FindFunctionIndex(*Escaping, "make_values"));
		VeroScript::AsmJit::FJitCodeCache ComparingCache;
		ComparingCache.PrepareModule(*Comparing, VeroScript::EExecutionBackend::Jit);
		const auto* const ComparingEntry =
			ComparingCache.GetCandidate(FindFunctionIndex(*Comparing, "compare_values"));
		if (!Expect(LocalEntry && LocalEntry->bUseScalarArrayStorage,
				"local primitive array enables typed storage")
			|| !Expect(EscapingEntry && !EscapingEntry->bUseScalarArrayStorage,
				"escaping array keeps canonical storage")
			|| !Expect(ComparingEntry && !ComparingEntry->bUseScalarArrayStorage,
				"array equality keeps canonical storage")
			|| !ExpectRun(*Local, VeroScript::EExecutionBackend::Jit,
				"39\n", "typed scalar array execution"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Integral sets receive flat storage only under a function-local no-escape proof.
		const auto Local = CompileSource(
			"int work() { set<int> values = set<int>(); values.add(7); values.add(7); "
			"values.remove(7); values.add(9); values.add(1); "
			"int hits = 0; for (int i = 0; i < 4; i++) { if (values.has(i % 2)) hits++; } "
			"return values.has(7) ? 0i64 : (values.has(9) ? len(values) + hits : 0i64); }\n"
			"void main() { print(work()); }\n");
		const auto Escaping = CompileSource(
			"set<int> make_values() { set<int> values = set<int>(); "
			"values.add(7); return values; }\n");
		if (!Expect(static_cast<bool>(Local) && static_cast<bool>(Escaping),
				"compile scalar set storage proof suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache LocalCache;
		LocalCache.PrepareModule(*Local, VeroScript::EExecutionBackend::Jit);
		const auto* const LocalEntry = LocalCache.GetCandidate(FindFunctionIndex(*Local, "work"));
		VeroScript::AsmJit::FJitCodeCache EscapingCache;
		EscapingCache.PrepareModule(*Escaping, VeroScript::EExecutionBackend::Jit);
		const auto* const EscapingEntry =
			EscapingCache.GetCandidate(FindFunctionIndex(*Escaping, "make_values"));
		const bool bHasPredecodedAdd = LocalEntry
			&& std::find(LocalEntry->ScalarSetCallKinds.begin(),
				LocalEntry->ScalarSetCallKinds.end(), std::uint8_t{1})
				!= LocalEntry->ScalarSetCallKinds.end();
		const bool bHasPredecodedHas = LocalEntry
			&& std::find(LocalEntry->ScalarSetCallKinds.begin(),
				LocalEntry->ScalarSetCallKinds.end(), std::uint8_t{2})
				!= LocalEntry->ScalarSetCallKinds.end();
		const bool bHasPredecodedRemove = LocalEntry
			&& std::find(LocalEntry->ScalarSetCallKinds.begin(),
				LocalEntry->ScalarSetCallKinds.end(), std::uint8_t{3})
				!= LocalEntry->ScalarSetCallKinds.end();
		if (!Expect(LocalEntry && LocalEntry->bUseScalarSetStorage,
				"local integral set enables flat storage")
			|| !Expect(bHasPredecodedAdd && bHasPredecodedHas && bHasPredecodedRemove,
				"flat set call sites are predecoded once per JIT entry")
			|| !Expect(EscapingEntry && !EscapingEntry->bUseScalarSetStorage,
				"escaping set keeps canonical storage")
			|| !ExpectRun(*Local, VeroScript::EExecutionBackend::Jit,
				"4\n", "flat scalar set duplicate/delete/reinsert execution"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Direct ordinary-method summaries and short-lived object scalar replacement
		// must retain the interpreter's checked arithmetic behavior. In particular,
		// batching a canonical loop may not defer or suppress the failing iteration.
		const std::string OrdinarySource =
			"class Increment { public: int apply(int value) { return value + 1; } };\n"
			"void main() { Increment operation = Increment(); int total = 2147483645; "
			"for (int i = 0; i < 3; i++) { total = operation.apply(total); } }\n";
		const std::string ShortLivedSource =
			"class Pair { public: int first = 1; int second = 2; "
			"int sum() { return first + second; } };\n"
			"void main() { int total = 2147483642; for (int i = 0; i < 3; i++) { "
			"Pair value = Pair(); total += value.sum(); } }\n";
		const std::string WideSource =
			"class WideIncrement { public: int64 apply(int64 value) { "
			"return value + 1i64; } };\n"
			"void main() { WideIncrement operation = WideIncrement(); "
			"int64 total = 9223372036854775806i64; for (int i = 0; i < 2; i++) { "
			"total = operation.apply(total); } }\n";
		for (const std::string* Source :
			{&OrdinarySource, &ShortLivedSource, &WideSource})
		{
			const std::unique_ptr<VeroScript::FBytecodeModule> Module =
				CompileSource(*Source);
			if (!Expect(static_cast<bool>(Module),
					"compile scalar method overflow suite"))
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
						"scalar method batching must preserve integer overflow"))
				{
					std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
						<< " error=" << Capture.ErrorMessageId << ' ' << Capture.Errors;
					return EXIT_FAILURE;
				}
			}
		}
	}

	{
		// A method-call loop is batchable only when the method argument is the
		// destination's previous value. Keep independent loop arguments independent
		// for both statically bound and virtual dispatch.
		const std::string Source =
			"interface IOperation { virtual int apply(int value) = 0; };\n"
			"class AddTwo : IOperation { public: int apply(int value) { "
			"return value + 2; } };\n"
			"int bound_work(int limit) { AddTwo operation = AddTwo(); int total = 0; "
			"for (int i = 0; i < limit; i++) { total = operation.apply(i); } return total; }\n"
			"int virtual_work(int limit) { IOperation operation = AddTwo(); int total = 0; "
			"for (int i = 0; i < limit; i++) { total = operation.apply(i); } return total; }\n"
			"void main() { print(bound_work(5)); print(virtual_work(5)); }\n";
		const std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile non-recurrent method loop suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "6\n6\n",
					"method batching requires a proven self recurrence"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Stable-field loop fusion is representation-generic across the primitive
		// scalar alternatives and must retain checked signed overflow.
		const std::string ScalarSource =
			"class Fields { public: uint32 u32 = 2u; int64 i64 = 3i64; "
			"uint64 u64 = 4u64; float f32 = 5.f; double f64 = 6.0; };\n"
			"void main() { Fields value = Fields(); uint32 u32 = 0u; int64 i64 = 0i64; "
			"uint64 u64 = 0u64; float f32 = 0.f; double f64 = 0.0; "
			"for (int i = 0; i < 4; i++) { u32 += value.u32; } "
			"for (int i = 0; i < 4; i++) { i64 += value.i64; } "
			"for (int i = 0; i < 4; i++) { u64 += value.u64; } "
			"for (int i = 0; i < 4; i++) { f32 += value.f32; } "
			"for (int i = 0; i < 4; i++) { f64 += value.f64; } "
			"print(to_int(u32)); print(to_int(i64)); print(to_int(u64)); "
			"print(to_int(f32)); print(to_int(f64)); }\n";
		const std::unique_ptr<VeroScript::FBytecodeModule> ScalarModule =
			CompileSource(ScalarSource);
		if (!Expect(static_cast<bool>(ScalarModule), "compile scalar field reduction suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*ScalarModule, Backend, "8\n12\n16\n20\n24\n",
					"stable field reduction preserves primitive scalar semantics"))
			{
				return EXIT_FAILURE;
			}
		}

		const std::string OverflowSource =
			"class Field { public: int64 value = 2i64; };\n"
			"void main() { Field field = Field(); int64 total = 9223372036854775806i64; "
			"for (int i = 0; i < 2; i++) { total += field.value; } }\n";
		const std::unique_ptr<VeroScript::FBytecodeModule> OverflowModule =
			CompileSource(OverflowSource);
		if (!Expect(static_cast<bool>(OverflowModule), "compile field reduction overflow suite"))
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
					"stable field reduction preserves signed overflow"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// A counted-loop batch may snapshot its limit only when the optimized body
		// cannot modify that same local. Cover both ordinary method recurrence and
		// short-lived object reduction; each must re-evaluate the shrinking limit.
		const std::string Source =
			"class Decrement { public: int apply(int value) { return value + -1; } };\n"
			"class NegativePair { public: int first = -1; int second = -1; "
			"int sum() { return first + second; } };\n"
			"class NegativeField { public: int value = -1; };\n"
			"int method_limit() { Decrement operation = Decrement(); int limit = 5; "
			"for (int i = 0; i < limit; i++) { limit = operation.apply(limit); } "
			"return limit; }\n"
			"int object_limit() { int limit = 5; for (int i = 0; i < limit; i++) { "
			"NegativePair value = NegativePair(); limit += value.sum(); } return limit; }\n"
			"int field_limit() { NegativeField value = NegativeField(); int limit = 5; "
			"for (int i = 0; i < limit; i++) { limit += value.value; } return limit; }\n"
			"void main() { print(method_limit()); print(object_limit()); print(field_limit()); }\n";
		const std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile loop limit alias suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "2\n1\n2\n",
					"loop batching must preserve a mutated loop limit"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Package/embedding metadata remains authoritative even when a method body
		// happens to match a scalar summary. Deleted functions must reach the normal
		// VM validation path instead of being fused.
		const std::string Source =
			"class Increment { public: double apply(double value) { return value + 1.0; } };\n"
			"void main() { Increment operation = Increment(); double total = 0.0; "
			"for (int i = 0; i < 3; i++) { total = operation.apply(total); } "
			"print(to_int(total)); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile scalar method metadata guard suite"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t ApplyIndex = FindFunctionIndex(*Module, "apply");
		if (!Expect(ApplyIndex != static_cast<std::uint16_t>(-1),
				"find scalar method metadata guard function"))
		{
			return EXIT_FAILURE;
		}
		Module->Functions[ApplyIndex].bDeleted = true;
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(!Capture.bSuccess,
					"scalar method fusion must preserve declaration metadata validation"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Scalar replacement must retain CreateObject's runtime abstract-type guard,
		// including for validated modules supplied by a package or embedding host.
		const std::string Source =
			"class Pair { public: int first = 1; int second = 2; "
			"int sum() { return first + second; } };\n"
			"void main() { int total = 0; for (int i = 0; i < 3; i++) { "
			"Pair value = Pair(); total += value.sum(); } print(total); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile abstract object guard suite"))
		{
			return EXIT_FAILURE;
		}
		auto TypeIt = std::find_if(
			Module->Types.begin(), Module->Types.end(), [](const auto& Type)
			{
				constexpr std::size_t LeafSize = 4;
				return Type.Name.size() >= LeafSize
						&& Type.Name.compare(Type.Name.size() - LeafSize, LeafSize, "Pair") == 0;
			});
		if (!Expect(TypeIt != Module->Types.end(), "find abstract object guard type"))
		{
			return EXIT_FAILURE;
		}
		TypeIt->bIsAbstract = true;
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(!Capture.bSuccess,
					"object scalar replacement must preserve abstract-type rejection"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// A destructor makes allocation lifetime observable, so the short-lived
		// scalar-replacement proof must reject the otherwise matching object shape.
		const std::string Source =
			"int destroyed = 0;\n"
			"class ObservedPair { public: int first = 1; int second = 2; "
			"~ObservedPair() { destroyed = destroyed + 1; } "
			"int sum() { return first + second; } };\n"
			"int work() { int total = 0; for (int i = 0; i < 3; i++) { "
			"ObservedPair value = ObservedPair(); total += value.sum(); } return total; }\n"
			"void main() { print(work()); print(destroyed); }\n";
		const std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile observable object lifetime suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "9\n3\n",
					"object scalar replacement must preserve destructors"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// A script object may outlive one direct VM invocation and be passed to a
		// different compatible module. Its cached type index must never be reused
		// solely because both invocations have the same program generation.
		const std::string ProducerSource =
			"class Padding { public: int ignored = 0; }\n"
			"class Shared { public: int value = 41; }\n"
			"Shared make_shared() { return Shared(); }\n";
		const std::string ConsumerSource =
			"class Shared { public: int value = 0; }\n"
			"class Padding { public: int ignored = 0; }\n"
			"int read_shared(Shared value) { return value.value + 1; }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Producer =
			CompileSource(ProducerSource);
		std::unique_ptr<VeroScript::FBytecodeModule> Consumer =
			CompileSource(ConsumerSource);
		if (!Expect(
				static_cast<bool>(Producer) && static_cast<bool>(Consumer),
				"compile cross-module object type-tag suite"))
		{
			return EXIT_FAILURE;
		}
		const std::uint16_t MakeIndex = FindFunctionIndex(*Producer, "make_shared");
		const std::uint16_t ReadIndex = FindFunctionIndex(*Consumer, "read_shared");
		if (!Expect(
				MakeIndex != static_cast<std::uint16_t>(-1)
					&& ReadIndex != static_cast<std::uint16_t>(-1),
				"find cross-module object functions"))
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
			VeroScript::FValue Object;
			if (!Expect(
					Vm.ExecuteFunction(*Producer, MakeIndex, {}, nullptr, &Object),
					"create cross-module object"))
			{
				std::cerr << Errors.str();
				return EXIT_FAILURE;
			}
			VeroScript::FValue Result;
			if (!Expect(
					Vm.ExecuteFunction(*Consumer, ReadIndex, {Object}, nullptr, &Result)
						&& std::holds_alternative<std::int32_t>(Result)
						&& std::get<std::int32_t>(Result) == 42,
					"re-resolve object type tag for a different module"))
			{
				std::cerr << Errors.str();
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Literal-bound counted loops use JmpIfLocalGeImm after compiler peephole
		// fusion. Stable container/string reductions may batch that exact loop, but
		// must preserve mutable-key, mutable-limit, bounds/key and overflow semantics.
		const std::string StableSource =
			"void main() { array<int> a = array<int>(); a.push(2); "
			"map<int,int> m = map<int,int>(); m[0] = 3; "
			"set<int> s = set<int>(); s.add(7); string x = \"same\"; string y = \"same\"; "
			"int at = 0; int mt = 0; int st = 0; int xt = 0; "
			"for (int i = 0; i < 4; i++) { at += a[0]; } "
			"for (int i = 0; i < 4; i++) { mt += m[0]; } "
			"for (int i = 0; i < 4; i++) { if (s.has(7)) st++; } "
			"for (int i = 0; i < 4; i++) { if (x == y) xt++; } "
			"print(at); print(mt); print(st); print(xt); }\n";
		const std::string AliasSource =
			"void main() { array<int> a = array<int>(); a.push(-1); "
			"set<int> s = set<int>(); s.add(0); int limit = 5; "
			"for (int i = 0; i < limit; i++) { limit += a[0]; } "
			"int total = 0; int& key = total; for (int i = 0; i < 2; i++) { "
			"if (s.has(key)) total++; } print(limit); print(total); }\n";
		const std::string ArrayBoundsSource =
			"void main() { array<int> a = array<int>(); a.push(1); int total = 0; "
			"for (int i = 0; i < 2; i++) { total += a[1]; } }\n";
		const std::string MapKeySource =
			"void main() { map<int,int> m = map<int,int>(); m[0] = 1; int total = 0; "
			"for (int i = 0; i < 2; i++) { total += m[1]; } }\n";
		const std::string RefKeySource =
			"void main() { map<int,int> m = map<int,int>(); m[0] = 1; int total = 0; "
			"int& key = total; for (int i = 0; i < 2; i++) { total += m[key]; } }\n";
		const std::string OverflowSource =
			"void main() { array<int> a = array<int>(); a.push(1); "
			"int total = 2147483646; for (int i = 0; i < 2; i++) { total += a[0]; } }\n";
		const auto Stable = CompileSource(StableSource);
		const auto Alias = CompileSource(AliasSource);
		const auto ArrayBounds = CompileSource(ArrayBoundsSource);
		const auto MapKey = CompileSource(MapKeySource);
		const auto RefKey = CompileSource(RefKeySource);
		const auto Overflow = CompileSource(OverflowSource);
		if (!Expect(Stable && Alias && ArrayBounds && MapKey && RefKey && Overflow,
				"compile stable container reduction suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Stable, Backend, "8\n12\n4\n4\n",
					"stable container/string reduction execution")
				|| !ExpectRun(*Alias, Backend, "2\n1\n",
					"stable container reduction alias guards"))
			{
				return EXIT_FAILURE;
			}
			for (const auto* ErrorModule :
				{ArrayBounds.get(), MapKey.get(), RefKey.get(), Overflow.get()})
			{
				const FExecutionCapture Capture = RunCapture(*ErrorModule, Backend);
				if (!Expect(!Capture.bSuccess,
						"stable container reduction must preserve runtime failures"))
				{
					return EXIT_FAILURE;
				}
			}
		}
	}

	{
		// Exact integer loop reductions must preserve checked signed arithmetic,
		// wrapping unsigned arithmetic, and the final value of a non-escaping array.
		// Mixed and VM remain the authoritative parity references.
		const std::string Source =
			"class I32Step { public: int apply(int value) { return value + 3; } };\n"
			"class U32Step { public: uint32 apply(uint32 value) { return value + 2u; } };\n"
			"class I64Step { public: int64 apply(int64 value) { return value + 3i64; } };\n"
			"class I64NegativeStep { public: int64 apply(int64 value) { return value + -3i64; } };\n"
			"class U64Step { public: uint64 apply(uint64 value) { return value + 2u64; } };\n"
			"float float_work(float value) { for (float i = 0.f; i < 4.f; i += 1.f) "
			"value += 1.f; return value; }\n"
			"double double_work(double value) { for (double i = 0.0; i < 4.0; i += 1.0) "
			"value += 1.0; return value; }\n"
			"int closed_i32(int value) { for (int i = -2; i < 5; i++) value += 3; "
			"return value; }\n"
			"int closed_i32_negative(int value) { for (int i = -3; i < 2; i++) value += -4; "
			"return value; }\n"
			"int closed_i32_empty(int value) { for (int i = 5; i < 2; i++) value += 3; "
			"return value; }\n"
			"uint32 closed_u32(uint32 value) { for (int i = 0; i < 4; i++) value += 2u; "
			"return value; }\n"
			"uint32 mixed_counter_u32(uint32 value) { for (int i = -2; i < 2; i++) "
			"value += 1u; return value; }\n"
			"float closed_f32(float value) { for (float i = -2.f; i < 5.f; i += 1.f) "
			"value += 1.f; return value; }\n"
			"float closed_f32_negative(float value) { "
			"for (float i = -3.f; i < 2.f; i += 1.f) value += -2.f; return value; }\n"
			"void main() { I32Step i32s = I32Step(); U32Step u32s = U32Step(); "
			"I64Step i64s = I64Step(); I64NegativeStep i64ns = I64NegativeStep(); "
			"U64Step u64s = U64Step(); "
			"int i32v = 7; uint32 u32v = 4294967294u; int64 i64v = 7i64; "
			"int64 i64low = -9223372036854775807i64 - 1i64; "
			"int64 i64high = 9223372036854775807i64; "
			"uint64 u64v = 18446744073709551614u64; "
			"for (int i = 0; i < 4; i++) i32v = i32s.apply(i32v); "
			"for (int i = 0; i < 2; i++) u32v = u32s.apply(u32v); "
			"for (int i = 0; i < 4; i++) i64v = i64s.apply(i64v); "
			"for (int i = 0; i < 4; i++) i64low = i64s.apply(i64low); "
			"for (int i = 0; i < 4; i++) i64high = i64ns.apply(i64high); "
			"for (int i = 0; i < 2; i++) u64v = u64s.apply(u64v); "
			"array<int> values = { 0 }; int offset = 7; "
			"for (int i = 0; i < 5; i++) values[0] = i + offset; "
			"print(i32v); print(u32v); print(i64v); print(i64low); print(i64high); "
			"print(u64v); print(values[0]); "
			"print(to_int(float_work(0.f))); print(to_int(double_work(0.0))); "
			"print(closed_i32(7)); print(closed_i32_negative(10)); print(closed_i32_empty(9)); "
			"print(closed_u32(4294967294u)); print(mixed_counter_u32(0u)); "
			"print(closed_f32(0.5f)); print(closed_f32(16777216.f)); "
			"print(closed_f32_negative(10.5f)); }\n";
		const std::string OverflowSource =
			"void main() { array<int> values = { 0 }; int offset = 2147483647; "
			"for (int i = 0; i < 2; i++) values[0] = i + offset; }\n";
		const std::string ClosedOverflowSource =
			"int closed_overflow(int value) { for (int i = 0; i < 1; i++) value += 3; "
			"return value; } void main() { print(closed_overflow(2147483646)); }\n";
		const std::string ClosedNegativeOverflowSource =
			"int closed_overflow(int value) { for (int i = 0; i < 1; i++) value += -3; "
			"return value; } void main() { print(closed_overflow(-2147483647)); }\n";
		const auto Module = CompileSource(Source);
		const auto Overflow = CompileSource(OverflowSource);
		const auto ClosedOverflow = CompileSource(ClosedOverflowSource);
		const auto ClosedNegativeOverflow = CompileSource(ClosedNegativeOverflowSource);
		if (!Expect(Module && Overflow && ClosedOverflow && ClosedNegativeOverflow,
				"compile exact integer loop reduction suite"))
		{
			return EXIT_FAILURE;
		}
		for (const char* const FunctionName : {"float_work", "double_work"})
		{
			const std::uint16_t FunctionIndex = FindFunctionIndex(*Module, FunctionName);
			std::size_t IncCount = 0;
			std::size_t ImmediateBranchCount = 0;
			if (FunctionIndex != static_cast<std::uint16_t>(-1))
			{
				const auto& Code = Module->Functions[FunctionIndex].Code;
				for (std::size_t Ip = 0; Ip < Code.size();)
				{
					const auto Op = static_cast<VeroScript::EOpcode>(Code[Ip]);
					IncCount += Op == VeroScript::EOpcode::IncLocal ? 1u : 0u;
					ImmediateBranchCount +=
						Op == VeroScript::EOpcode::JmpIfLocalGeImm ? 1u : 0u;
					const std::size_t Size = VeroScript::Bytecode::InstructionSize(Op);
					if (Size == 0 || Ip + Size > Code.size())
					{
						break;
					}
					Ip += Size;
				}
			}
			if (!Expect(FunctionIndex != static_cast<std::uint16_t>(-1)
					&& IncCount == 2 && ImmediateBranchCount == 1,
					"exact float constants use local arithmetic peepholes"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend,
					"19\n2\n19\n-9223372036854775796\n9223372036854775795\n2\n11\n4\n4\n"
					"28\n-10\n9\n6\n4\n7.5\n16777216\n0.5\n",
					"exact integer loop reduction parity"))
			{
				return EXIT_FAILURE;
			}
			const FExecutionCapture Capture = RunCapture(*Overflow, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.integer_overflow",
					"array final-store reduction preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
			const FExecutionCapture ClosedCapture = RunCapture(*ClosedOverflow, Backend);
			if (!Expect(!ClosedCapture.bSuccess
					&& ClosedCapture.ErrorMessageId == "diag.integer_overflow",
					"closed recurrence preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
			const FExecutionCapture ClosedNegativeCapture =
				RunCapture(*ClosedNegativeOverflow, Backend);
			if (!Expect(!ClosedNegativeCapture.bSuccess
					&& ClosedNegativeCapture.ErrorMessageId == "diag.integer_overflow",
					"closed negative recurrence preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Hot scalar conversions, non-throwing protected regions, U32 object fields,
		// and typed Array/Map updates must remain byte-for-byte equivalent to the VM.
		// This suite also prevents these common forms from silently falling back to
		// the generic boxed container/interpreter paths.
		const std::string Source =
			"class State { public: uint32 value = 1u; };\n"
			"uint32 protected_work() { uint32 x = 1u; for (int i = 0; i < 8; i++) { "
			"try { x = x * 3u + 1u; } catch (...) { return 0u; } } return x; }\n"
			"uint32 field_work() { State value = State(); for (int i = 0; i < 32; i++) "
			"value.value = (value.value + 7u) * 3u - 2u; return value.value; }\n"
			"uint32 array_work() { array<uint32> values = { 10u, 20u, 30u, 40u }; "
			"uint32 x = 1u; uint32 total = 0u; for (int i = 0; i < 8; i++) { "
			"x = x * 3u + 1u; total += values[to_int(x & 3u)]; "
			"values[cast<int>(x & 3u)] = x; } return total; }\n"
			"uint32 map_work() { map<uint32, uint32> values = map<uint32, uint32>(); "
			"for (uint32 i = 0u; i < 4u; i++) values[i] = 0u; uint32 x = 1u; "
			"for (int i = 0; i < 8; i++) { x = x * 3u + 1u; uint32 key = x & 3u; "
			"values[key] = values[key] + 1u; } return values[0u] + values[1u]; }\n"
			"bool nested_map_work() { array<int> keys = { 2, 1 }; "
			"map<int, double> values = map<int, double>(); values[1] = 1.0; values[2] = 2.0; "
			"int index = 0; double delta = 0.75; "
			"values[keys[index]] = values[keys[index]] + delta; "
			"return values[2] == 2.75; }\n"
			"void main() { print(protected_work()); print(field_work()); "
			"print(array_work()); print(map_work()); print(nested_map_work()); }\n";
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module),
				"compile scalar protected/container fast-path suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "9841\n3325923393\n1665\n8\ntrue\n",
					"scalar protected/container fast-path parity"))
			{
				return EXIT_FAILURE;
			}
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto* const ProtectedEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "protected_work"));
		if (!Expect(ProtectedEntry
				&& ProtectedEntry->Kind != VeroScript::AsmJit::EJitEntryKind::VmTrampoline,
				"non-throwing catch region remains on an optimized JIT path"))
		{
			return EXIT_FAILURE;
		}
		const FExecutionCapture Capture =
			RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		if (!Expect(Capture.bSuccess
				&& Capture.JitStats.ArrayFastPathGets >= 8
				&& Capture.JitStats.ArrayFastPathSets >= 8
				&& Capture.JitStats.ScalarMapFastPathGets >= 8
				&& Capture.JitStats.ScalarMapFastPathSets >= 8,
				"typed U32 Array/Map paths remain active"))
		{
			return EXIT_FAILURE;
		}

		const auto BoundsModule = CompileSource(
			"void main() { array<uint32> values = { 1u }; uint32 state = 1u; "
			"uint32 total = 0u; for (int i = 0; i < 128; i++) { "
			"state = state * 3u + 1u; total += values[cast<int>(state & 3u)]; } "
			"print(total); }");
		if (!Expect(static_cast<bool>(BoundsModule),
				"compile affine array bounds regression"))
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
					"affine array cycle preserves the first bounds failure"))
			{
				return EXIT_FAILURE;
			}
		}

		const auto BranchOverflowModule = CompileSource(
			"int overflow_masked_increment() { uint32 value = 2u; "
			"int hits = 2147483647; if ((value & 1u) == 0u) hits++; "
			"return hits; } void main() { print(overflow_masked_increment()); }");
		if (!Expect(static_cast<bool>(BranchOverflowModule),
				"compile masked conditional increment overflow regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture OverflowCapture =
				RunCapture(*BranchOverflowModule, Backend);
			if (!Expect(!OverflowCapture.bSuccess
					&& OverflowCapture.ErrorMessageId == "diag.integer_overflow",
					"masked conditional increment preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// A compound map update evaluates map[key] before its right operand.
		// Speculative fast-path decoding must not let a later arithmetic fault
		// replace the authoritative missing-key diagnostic.
		const auto Module = CompileSource(
			"void main() { map<uint32, uint32> values = map<uint32, uint32>(); "
			"array<uint32> keys = { 1u }; int index = 0; uint32 zero = 0u; "
			"values[keys[index]] = values[keys[index]] / zero; }\n");
		if (!Expect(static_cast<bool>(Module),
				"compile map update evaluation-order regression"))
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
					&& Capture.ErrorMessage.find("map key not found") != std::string::npos,
					"map update preserves missing-key evaluation order"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Whole-loop JIT specializations must preserve VM semantics while keeping
		// affine container indexing, field methods, short-lived objects, and a
		// four-state polymorphic call site off the generic ownership/dispatch bridge.
		const std::string Source =
			"const int MOD = 1000000007; "
			"int InitHits = 0; "
			"class Counter { public: int value = 1; "
			"void step(int delta) { value = (value + delta) % MOD; } }; "
			"interface IOp { virtual int step(int value) = 0; }; "
			"class AddOne : IOp { public: int step(int value) { return (value + 1) % MOD; } }; "
			"class AddThree : IOp { public: int step(int value) { return (value + 3) % MOD; } }; "
			"class AddFive : IOp { public: int step(int value) { return (value + 5) % MOD; } }; "
			"class AddSeven : IOp { public: int step(int value) { return (value + 7) % MOD; } }; "
			"class PairValue { public: int a; int b; PairValue(int x, int y) { a = x; b = y; } "
			"int sum() { return a + b; } }; "
			"class SideEffectValue { public: int value; "
			"SideEffectValue(int x) { value = x; } ~SideEffectValue() { InitHits++; } "
			"int get() { return value; } }; "
			"int method_work() { Counter value = Counter(); "
			"for (int i = 0; i < 16; i++) value.step(i); return value.value; } "
			"int array_work() { array<int> values = array<int>(); "
			"for (int i = 0; i < 32; i++) values.push(i); int total = 0; "
			"for (int i = 0; i < 64; i++) total = (total + values[(i * 5) % 32]) % MOD; "
			"return total; } "
			"int map_work() { map<int, int> values = map<int, int>(); "
			"for (int i = 0; i < 32; i++) values[i] = i; int total = 0; "
			"for (int i = 0; i < 64; i++) total = (total + values[(i * 5) % 32]) % MOD; "
			"return total; } "
			"int set_work() { set<int> values = set<int>(); "
			"for (int i = 0; i < 16; i++) values.add(i); int total = 0; "
			"for (int i = 0; i < 64; i++) if (values.has((i * 5) % 32)) total++; "
			"return total; } "
			"int object_work() { int total = 0; for (int i = 0; i < 64; i++) { "
			"PairValue value = PairValue(i % 10, 3); total = (total + value.sum()) % MOD; } "
			"return total; } "
			"int side_effect_work() { int total = 0; for (int i = 0; i < 8; i++) { "
			"SideEffectValue value = SideEffectValue(i); total += value.get(); } return total; } "
			"int polymorphic_work() { IOp a = AddOne(); IOp b = AddThree(); "
			"IOp c = AddFive(); IOp d = AddSeven(); IOp current = a; int total = 0; "
			"for (int i = 0; i < 64; i++) { int choice = i % 4; "
			"if (choice == 0) current = a; else if (choice == 1) current = b; "
			"else if (choice == 2) current = c; else current = d; "
			"total = current.step(total); } return total; } "
			"void main() { print(method_work()); print(array_work()); print(map_work()); "
			"print(set_work()); print(object_work()); print(side_effect_work()); print(InitHits); "
			"print(polymorphic_work()); }";
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module),
				"compile whole-loop specialization regression suite"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "121\n992\n992\n32\n468\n28\n8\n256\n",
					"whole-loop specialization parity"))
			{
				return EXIT_FAILURE;
			}
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto* const ArrayEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "array_work"));
		const auto* const MapEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "map_work"));
		const auto* const SetEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "set_work"));
		if (!Expect(ArrayEntry && ArrayEntry->bUseScalarArrayStorage,
				"affine Array workload retains typed storage")
			|| !Expect(MapEntry && MapEntry->bUseScalarMapStorage,
				"affine Map workload retains typed storage")
			|| !Expect(SetEntry && SetEntry->bUseScalarSetStorage,
				"affine Set workload retains typed storage"))
		{
			return EXIT_FAILURE;
		}
		const FExecutionCapture Capture =
			RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		constexpr std::size_t ExpectedMethodFastPaths = 140;
		#elif VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X86
		// Scalar x86 leaves do not provide the native receiver/CFG path.
		constexpr std::size_t ExpectedMethodFastPaths = 0;
		#else
		constexpr std::size_t ExpectedMethodFastPaths = 64;
		#endif
		if (!Expect(Capture.bSuccess
			&& Capture.JitStats.ArrayFastPathGets >= 64
			&& Capture.JitStats.ScalarMapFastPathGets >= 64
			&& Capture.JitStats.ScalarMethodFastPathI32 >= ExpectedMethodFastPaths,
				"whole-loop typed paths remain active"))
		{
			std::cerr << "whole-loop stats array="
				<< Capture.JitStats.ArrayFastPathGets
				<< " map=" << Capture.JitStats.ScalarMapFastPathGets
				<< " method=" << Capture.JitStats.ScalarMethodFastPathI32 << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Regression coverage for the generalized hot paths used by dynamic dispatch,
		// string containers, affine random access, and protected no-throw loops. Keep
		// the trip counts small: the assertions below verify admission and statistics,
		// while differential execution verifies the observable VM semantics.
		const std::string Source =
			"interface IAdd { virtual int add(int value) = 0; }; "
			"class AddOne : IAdd { int add(int value) { return value + 1; } }; "
			"class AddThree : IAdd { int add(int value) { return value + 3; } }; "
			"class AddFive : IAdd { int add(int value) { return value + 5; } }; "
			"class AddSeven : IAdd { int add(int value) { return value + 7; } }; "
			"int branch_pic() { IAdd a = AddOne(); IAdd b = AddThree(); "
			"IAdd c = AddFive(); IAdd d = AddSeven(); int total = 0; "
			"for (int i = 0; i < 16; i++) { int choice = i & 3; "
			"if (choice == 0) total = a.add(total); "
			"else if (choice == 1) total = b.add(total); "
			"else if (choice == 2) total = c.add(total); "
			"else total = d.add(total); } return total; } "
			"uint32 recurrence() { uint32 value = 1u; for (int i = 0; i < 8; i++) "
			"value = value * 3u + 1u; return value; } "
			"uint32 protected_recurrence() { uint32 value = 1u; "
			"for (int i = 0; i < 8; i++) { try { value = value * 3u + 1u; } "
			"catch (...) { return 0u; } } return value; } "
			"int scalar_next(int value) { return (value * 3 + 1) % 1000000007; } "
			"int protected_scalar_call() { int value=1; for(int i=0;i<8;i++) { "
			"try { value=scalar_next(value); } catch (...) { return 0; } } return value; } "
			"int string_map_work() { map<string, int> values = map<string, int>(); "
			"values[\"a\"] = 1; values[\"b\"] = 2; values[\"c\"] = 3; values[\"d\"] = 4; "
			"array<string> keys = { \"a\", \"b\", \"c\", \"d\" }; int total = 0; "
			"for (int i = 0; i < 8; i++) total += values[keys[i & 3]]; return total; } "
			"int string_set_work() { set<string> values = { \"needle\" }; "
			"string needle = \"needle\"; int total = 0; "
			"for (int i = 0; i < 8; i++) if (values.has(needle)) total++; return total; } "
			"uint32 array_cycle_work() { array<uint32> values = array<uint32>(); "
			"values.reserve(8); for (int i = 0; i < 8; i++) "
			"values.push(cast<uint32>(i & 3)); uint32 state = 1u; uint32 total = 0u; "
			"for (int i = 0; i < 128; i++) { state = state * 3u + 1u; "
			"total += values[cast<int>(state & 7u)]; } return total; } "
			"void main() { print(branch_pic()); print(recurrence()); "
			"print(protected_recurrence()); print(protected_scalar_call()); print(string_map_work()); "
			"print(string_set_work()); print(array_cycle_work()); }";
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module),
				"compile generalized dynamic/container hot-path regressions"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "64\n9841\n9841\n9841\n20\n8\n64\n",
					"generalized dynamic/container hot-path parity"))
			{
				return EXIT_FAILURE;
			}
		}

		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto* const RecurrenceEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "recurrence"));
		const auto* const ProtectedEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "protected_recurrence"));
		const auto* const ProtectedCallEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "protected_scalar_call"));
		const auto* const StringMapEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "string_map_work"));
		const auto* const StringSetEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "string_set_work"));
		const auto* const ArrayCycleEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "array_cycle_work"));
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		constexpr VeroScript::AsmJit::EJitEntryKind ExpectedProtectedCallKind =
			VeroScript::AsmJit::EJitEntryKind::NativeCfg;
		#else
		constexpr VeroScript::AsmJit::EJitEntryKind ExpectedProtectedCallKind =
			VeroScript::AsmJit::EJitEntryKind::LinearTrampoline;
		#endif
		if (!Expect(RecurrenceEntry
				&& RecurrenceEntry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline,
				"affine U32 recurrence selects the folding linear kernel")
			|| !Expect(ProtectedEntry
					&& ProtectedEntry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline,
				"inert catch preserves the no-throw optimized path")
			|| !Expect(ProtectedCallEntry
					&& ProtectedCallEntry->Kind == ExpectedProtectedCallKind,
				"catch-only scalar call is inlined before native lowering")
			|| !Expect(StringMapEntry
					&& StringMapEntry->Kind == VeroScript::AsmJit::EJitEntryKind::LinearTrampoline,
				"string Map reduction remains on the optimized linear path")
			|| !Expect(StringSetEntry && StringSetEntry->bUseScalarSetStorage,
				"string Set literals retain typed scalar storage")
			|| !Expect(ArrayCycleEntry && ArrayCycleEntry->bUseScalarArrayStorage,
				"unsigned affine arrays retain typed scalar storage"))
		{
			return EXIT_FAILURE;
		}
		const FExecutionCapture Capture =
			RunCapture(*Module, VeroScript::EExecutionBackend::Jit);
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		constexpr std::size_t ExpectedPicCalls = 16;
		#else
		constexpr std::size_t ExpectedPicCalls = 0;
		#endif
		if (!Expect(Capture.bSuccess
				&& Capture.JitStats.ScalarMethodFastPathI32 >= ExpectedPicCalls
				&& Capture.JitStats.ScalarMapFastPathGets >= 8
				&& Capture.JitStats.ArrayFastPathPushes >= 8
				&& Capture.JitStats.ArrayFastPathGets >= 128,
				"generalized PIC and string Map paths remain active"))
		{
			return EXIT_FAILURE;
		}

		const auto PicOverflowModule = CompileSource(
			"interface IAdd { virtual int add(int value) = 0; }; "
			"class AddOne : IAdd { int add(int value) { return value + 1; } }; "
			"class AddTwo : IAdd { int add(int value) { return value + 2; } }; "
			"void main() { IAdd a = AddOne(); IAdd b = AddTwo(); "
			"int total = 2147483647; for (int i = 0; i < 8; i++) { int choice = i & 1; "
			"if (choice == 0) total = a.add(total); else total = b.add(total); } }");
		if (!Expect(static_cast<bool>(PicOverflowModule),
				"compile direct-branch PIC overflow regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Overflow = RunCapture(*PicOverflowModule, Backend);
			if (!Expect(!Overflow.bSuccess
					&& Overflow.ErrorMessageId == "diag.integer_overflow",
					"direct-branch PIC preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Primitive work in a heterogeneous function must remain native. An int
		// induction variable beside uint/double state is an ordinary loop shape,
		// not a reason to route every arithmetic opcode through the mixed helper ABI.
		const std::string Source =
			"uint32 step(uint32 value) { return value * 1664525u + 1013904223u; } "
			"uint32 branch_work() { uint32 value = 1u; int even = 0; "
			"for (int i = 0; i < 8; i++) { value = step(value); "
			"if ((value & 1u) == 0u) even++; } print(even); return value; } "
			"uint32 free_work(int begin) { uint32 value = 1u; "
			"for (int i = begin; i < 3; i++) value = step(value); return value; } "
			"uint32 masked_ne_wrap() { uint32 value = 2u; uint32 hits = 4294967295u; "
			"if ((value & 1u) != 1u) hits++; return hits; } "
			"double scale(double value) { return value * 2.0 + 1.0; } "
			"double double_work() { double value = 1.0; "
			"for (int i = 0; i < 8; i++) value = value * 1.0000001 + 0.000001; "
			"return value; } "
			"double mixed_div_neg(int count) { double value = 8.0; "
			"for (int i = 0; i < count; i++) value = -value / -2.0; return value; } "
			"float mixed_f32_div_neg(int count) { float value = 8.0f; "
			"for (int i = 0; i < count; i++) value = -value / -2.0f; return value; } "
			"int mixed_div_mod(int count, double unused) { int value = 100; "
			"for (int i = 0; i < count; i++) value = value / 3 + value % 3; return value; } "
			"int64 mixed_i64_div_mod(int count, double unused) { int64 value = 100i64; "
			"for (int i = 0; i < count; i++) value = value / 3i64 + value % 3i64; return value; } "
			"double mixed_abs(int count) { double value = -8.0; "
			"for (int i = 0; i < count; i++) value = abs(value - 1.0); return value; } "
			"double mixed_min_max(int count) { double value = -8.0; "
			"for (int i = 0; i < count; i++) value = max(min(value + 4.0, 3.0), -2.0); "
			"return value; } "
			"double mixed_to_double(int count) { int value = -3; double total = 0.0; "
			"for (int i = 0; i < count; i++) { total += to_double(value); value++; } "
			"return total; } "
			"float mixed_to_float(double value, int count) { float total = 0.0f; "
			"for (int i = 0; i < count; i++) total += to_float(value); return total; } "
			"int mixed_to_int(uint32 value, double unused) { return to_int(value); } "
			"uint32 mixed_to_unsigned(int value, double unused) { return to_unsigned(value); } "
			"int64 mixed_to_int64(int value, double unused) { return to_int64(value); } "
			"uint64 mixed_to_uint64(uint32 value, double unused) { return to_uint64(value); } "
			"int primitive_ops() { int signed_value = 20; "
			"signed_value = (signed_value - 3) * 2; "
			"uint32 unsigned_value = (15u & 7u) | (8u ^ 1u); "
			"int64 wide = 10i64 * 3i64 - 4i64; "
			"uint64 unsigned_wide = 10u64 * 4u64 - 3u64; "
			"float narrow_float = 1.5f * 2.0f - 0.5f; "
			"double wide_float = 3.0 * 2.0 - 1.0; int flags = 0; "
			"if (narrow_float < 3.0f) flags++; if (wide_float >= 5.0) flags++; "
			"return signed_value + cast<int>(unsigned_value) + cast<int>(wide) "
			"+ cast<int>(unsigned_wide) + flags; } "
			"void main() { print(branch_work()); print(free_work(-2)); print(free_work(5)); "
			"print(masked_ne_wrap()); print(to_int(scale(2.5))); "
			"print(to_int(double_work() * 1000000.0)); print(to_int(mixed_div_neg(3))); "
			"print(to_int(mixed_f32_div_neg(3))); print(mixed_div_mod(3, 0.0)); "
			"print(to_int(mixed_i64_div_mod(3, 0.0))); print(to_int(mixed_abs(3))); "
			"print(to_int(mixed_min_max(3))); print(to_int(mixed_to_double(3))); "
			"print(to_int(mixed_to_float(1.5, 2))); print(mixed_to_int(42u, 0.0)); "
			"print(mixed_to_unsigned(42, 0.0)); print(to_int(mixed_to_int64(-42, 0.0))); "
			"print(to_int(mixed_to_uint64(42u, 0.0))); print(primitive_ops()); }";
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile mixed primitive loop regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend,
					"4\n2388811721\n217083232\n1\n0\n6\n1000008\n1\n1\n4\n4\n7\n3\n-6\n3\n42\n42\n-42\n42\n114\n",
					"mixed primitive loop parity"))
			{
				return EXIT_FAILURE;
			}
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto* const BranchEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "branch_work"));
		const auto* const DoubleEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "double_work"));
		const auto* const DivNegEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_div_neg"));
		const auto* const F32DivNegEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_f32_div_neg"));
		const auto* const DivModEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_div_mod"));
		const auto* const I64DivModEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_i64_div_mod"));
		const auto* const AbsEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_abs"));
		const auto* const MinMaxEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_min_max"));
		const auto* const ToDoubleEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_to_double"));
		const auto* const ToFloatEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_to_float"));
		const auto* const ToIntEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_to_int"));
		const auto* const ToUnsignedEntry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_to_unsigned"));
		const auto* const ToInt64Entry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_to_int64"));
		const auto* const ToUInt64Entry =
			Cache.GetCandidate(FindFunctionIndex(*Module, "mixed_to_uint64"));
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (!Expect(BranchEntry
				&& BranchEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg
				&& BranchEntry->InlinedCallSites >= 1,
				"mixed uint branch loop keeps native inlining")
			|| !Expect(DoubleEntry
					&& DoubleEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed double loop remains native")
			|| !Expect(DivNegEntry
					&& DivNegEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed float division and unary arithmetic remain native")
			|| !Expect(F32DivNegEntry
					&& F32DivNegEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed float32 division and unary arithmetic remain native")
			|| !Expect(DivModEntry
					&& DivModEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed int division and modulo remain native")
			|| !Expect(I64DivModEntry
					&& I64DivModEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed int64 division and modulo remain native")
			|| !Expect(AbsEntry
					&& AbsEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed numeric abs remains native")
			|| !Expect(MinMaxEntry
					&& MinMaxEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed numeric min/max remains native")
			|| !Expect(ToDoubleEntry
					&& ToDoubleEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed to_double remains native")
			|| !Expect(ToFloatEntry
					&& ToFloatEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed to_float remains native")
			|| !Expect(ToIntEntry
					&& ToIntEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed to_int remains native")
			|| !Expect(ToUnsignedEntry
					&& ToUnsignedEntry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed to_unsigned remains native")
			|| !Expect(ToInt64Entry
					&& ToInt64Entry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed to_int64 remains native")
			|| !Expect(ToUInt64Entry
					&& ToUInt64Entry->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"mixed to_uint64 remains native"))
		{
			return EXIT_FAILURE;
		}
		#else
		(void)BranchEntry;
		(void)DoubleEntry;
		(void)DivNegEntry;
		(void)F32DivNegEntry;
		(void)DivModEntry;
		(void)I64DivModEntry;
		(void)AbsEntry;
		(void)MinMaxEntry;
		(void)ToDoubleEntry;
		(void)ToFloatEntry;
		(void)ToIntEntry;
		(void)ToUnsignedEntry;
		(void)ToInt64Entry;
		(void)ToUInt64Entry;
		#endif

		const auto DivByZeroModule = CompileSource(
			"double mixed_div_zero(int marker) { double value = -1.0; return value / 0.0; } "
			"void main() { print(to_int(mixed_div_zero(0))); }");
		if (!Expect(static_cast<bool>(DivByZeroModule),
				"compile mixed float division-by-zero regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*DivByZeroModule, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.division_by_zero",
					"mixed float division preserves division-by-zero diagnostics"))
			{
				return EXIT_FAILURE;
			}
		}

		const auto AbsOverflowModule = CompileSource(
			"int mixed_abs_overflow(double marker) { int value = -2147483647 - 1; "
			"return abs(value); } void main() { print(mixed_abs_overflow(0.0)); }");
		if (!Expect(static_cast<bool>(AbsOverflowModule),
				"compile mixed abs-overflow regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*AbsOverflowModule, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.integer_overflow",
					"mixed abs preserves integer-overflow diagnostics"))
			{
				return EXIT_FAILURE;
			}
		}

		const auto OverflowModule = CompileSource(
			"int overflow(int value, double marker) { for (int i = 0; i < 1; i++) "
			"value = value * 2 + 1; return value + to_int(marker); } "
			"void main() { print(overflow(2147483647, 0.0)); }");
		if (!Expect(static_cast<bool>(OverflowModule),
				"compile mixed primitive overflow regression"))
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
					"mixed primitive fusion preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
		}

		// The whole-loop LCG fast path must retain the exact bytecode ordering
		// when the conditional signed increment overflows: the recurrence has
		// executed, while the failed increment and the loop counter have not.
		const auto ConditionalOverflowModule = CompileSource(
			"int overflow_branch_loop() { uint32 value = 1u; int hits = 2147483646; "
			"for (int i = 0; i < 3; i++) { "
			"value = value * 1664525u + 1013904223u; "
			"if ((value & 1u) == 0u) hits++; } return hits; } "
			"void main() { print(overflow_branch_loop()); }");
		if (!Expect(static_cast<bool>(ConditionalOverflowModule),
				"compile registerized conditional-loop overflow regression"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture =
				RunCapture(*ConditionalOverflowModule, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.integer_overflow",
					"registerized conditional loop preserves checked overflow"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Mixed profitability is structural: helper-dominated ownership loops fall
		// back before entering Linear, while compact typed/fused loops remain enabled.
		const auto Module = CompileSource(
			"class FieldBlock { public: int a=0; int b=1; int c=2; int d=3; "
			"int e=4; int f=5; int g=6; int h=7; }; "
			"class DoubleFieldBlock { public: double a=0.0; double b=1.0; }; "
			"class Item { public: int value=1; int get(){return value;} }; "
			"class Pair { public: int first=0; int second=0; int sum(){return first+second;} }; "
			"interface Operation { virtual int step(int value)=0; }; "
			"class Add3:Operation { public:int step(int value){return (value+3)%1000000007;} }; "
			"class Add5:Operation { public:int step(int value){return (value+5)%1000000007;} }; "
			"class Add7:Operation { public:int step(int value){return (value+7)%1000000007;} }; "
			"class Add11:Operation { public:int step(int value){return (value+11)%1000000007;} }; "
			"int fast_array(int n) { array<int> values=array<int>(); values.reserve(n); "
			"for(int i=0;i<n;i++) values.push(i); int total=0; "
			"for(int i=0;i<n;i++) total+=values[i]; return total; } "
			"int indirect_array(int n) { array<int> order=array<int>(); "
			"array<int> values=array<int>(); for(int i=0;i<n;i++){order.push(i);values.push(i);} "
			"int total=0; for(int i=0;i<n;i++) total+=values[order[i]]; return total; } "
			"int branch_fields(FieldBlock value,int n) { int total=0; for(int i=0;i<n;i++){ "
			"int k=i%8; if(k==0)total+=value.a;else if(k==1)total+=value.b;"
			"else if(k==2)total+=value.c;else if(k==3)total+=value.d;"
			"else if(k==4)total+=value.e;else if(k==5)total+=value.f;"
			"else if(k==6)total+=value.g;else total+=value.h;} return total; } "
			"double double_fields(int n) { DoubleFieldBlock value=DoubleFieldBlock(); "
			"for(int i=0;i<n;i++){if((i&1)==0)value.a+=0.5;else value.b+=1.25;}"
			"return value.a+value.b; } "
			"int local_receivers(int n) { Item a=Item(); Item b=Item(); int total=0; "
			"for(int i=0;i<n;i++){if((i%2)==0)total+=a.get();else total+=b.get();}return total;} "
			"int stateful_short(int n) { int total=0; for(int i=0;i<n;i++){Item a=Item();"
			"a.value=i%7;total=(total+a.get()+(i%5))%1000000007;}return total;} "
			"int state_fields(int n){FieldBlock value=FieldBlock();int state=17;"
			"for(int i=0;i<n;i++){state=(state*25173+13849)%65521;"
			"int delta=(state%13)+1;int choice=state&7;"
			"if(choice==0)value.a+=delta;else if(choice==1)value.b+=delta;"
			"else if(choice==2)value.c+=delta;else if(choice==3)value.d+=delta;"
			"else if(choice==4)value.e+=delta;else if(choice==5)value.f+=delta;"
			"else if(choice==6)value.g+=delta;else value.h+=delta;}"
			"return value.a+value.b+value.c+value.d+value.e+value.f+value.g+value.h+state;} "
			"int state_virtual(int n){Operation a=Add3();Operation b=Add5();"
			"Operation c=Add7();Operation d=Add11();Operation current=a;"
			"int state=17;int total=0;for(int i=0;i<n;i++){"
			"state=(state*25173+13849)%65521;int choice=state&3;"
			"if(choice==0)current=a;else if(choice==1)current=b;"
			"else if(choice==2)current=c;else current=d;"
			"total=current.step(total);}return total+state;} "
			"int state_virtual_outer(int n){Operation a=Add3();Operation b=Add5();"
			"Operation c=Add7();Operation d=Add11();Operation current=a;"
			"int state=17;int total=0;for(int i=0;i<n;i++){"
			"state=(state*25173+13849)%65521;int choice=state%4;"
			"if(choice==0)current=a;else if(choice==1)current=b;"
			"else if(choice==2)current=c;else current=d;"
			"total=current.step(total)%1000000007;}return total+state;} "
			"int assigned_short(int n){int state=17;int total=0;for(int i=0;i<n;i++){"
			"state=(state*25173+13849)%65536;Pair value=Pair();"
			"value.first=state%97;value.second=state%43;"
			"total=(total+value.sum())%1000000007;}return total;} "
			"int xorshift_array(int n){array<int> values=array<int>();values.reserve(64);"
			"for(int i=0;i<64;i++)values.push(i);uint32 state=362436069u;int total=0;"
			"for(int i=0;i<n;i++){state=state^(state<<13u);state=state^(state>>17u);"
			"state=state^(state<<5u);int k=to_int(state&63u);int v=values[k]+1;"
			"values[k]=v;total+=v;}return total;} "
			"int xorshift_map(int n){map<int,int> values=map<int,int>();values.reserve(64);"
			"for(int i=0;i<64;i++)values[i]=i;uint32 state=521288629u;int total=0;"
			"for(int i=0;i<n;i++){state=state^(state<<13u);state=state^(state>>17u);"
			"state=state^(state<<5u);int k=to_int(state&63u);int v=values[k]+1;"
			"values[k]=v;total+=v;}return total;} "
			"int xorshift_set(int n){set<int> values=set<int>();values.reserve(64);"
			"for(int i=0;i<64;i+=2)values.add(i);uint32 state=88675123u;int total=0;"
			"for(int i=0;i<n;i++){state=state^(state<<13u);state=state^(state>>17u);"
			"state=state^(state<<5u);if(values.has(to_int(state&63u)))total++;}return total;} "
			"uint32 next_set_state(uint32 state){state=state^(state<<13u);"
			"state=state^(state>>17u);return state^(state<<5u);} "
			"int xorshift_set_call(int n){set<int> values=set<int>();values.reserve(64);"
			"for(int i=0;i<64;i+=2)values.add(i);uint32 state=88675123u;int total=0;"
			"for(int i=0;i<n;i++){state=next_set_state(state);"
			"if(values.has(to_int(state&63u)))total++;}return total;} "
			"int string_map_call(int n){array<string> keys={\"a\",\"b\",\"c\",\"d\"};"
			"map<string,int> values=map<string,int>();for(int i=0;i<4;i++)values[keys[i]]=i+1;"
			"uint32 state=1234567u;int total=0;for(int i=0;i<n;i++){"
			"state=next_set_state(state);total+=values[keys[to_int(state&3u)]];}return total;} "
			"double mixed_numeric_containers(int n){array<float> a=array<float>();"
			"map<uint32,double> m=map<uint32,double>();set<uint32> s=set<uint32>();"
			"a.reserve(8);m.reserve(8);s.reserve(8);for(int i=0;i<8;i++){"
			"uint32 key=to_unsigned(i);a.push(to_float(i)*0.5f);m[key]=to_double(i);s.add(key);}"
			"double total=0.0;for(int i=0;i<n;i++){uint32 key=to_unsigned(i&7);"
			"if(s.has(key))total+=m[key]+to_double(a[to_int(key)]);}return total;} "
			"uint32 constant_uint_map(){map<int,uint32> values=map<int,uint32>();"
			"values.reserve(64);for(int i=0;i<64;i++)values[i]=0u;int state=1;int i=0;"
			"while(i<32){state=(state*25173+13849)%64;"
			"values[state]=values[state]+1u;i++;}return values[state];} "
			"int aliased_counter_state(){map<int,int> values=map<int,int>();"
			"values.reserve(64);for(int i=0;i<64;i++)values[i]=0;int state=1;"
			"while(state<32){state=(state*1+0)%64;"
			"values[state]=values[state]+1;state++;}return state;} "
			"int aliased_set_counter_total(){set<int> values={1,2,3,4,5,6,7};"
			"int value=0;int state=1;int limit=20;while(value<limit){"
			"state=(state*17+3)%31;if(values.has(state))value++;value++;}"
			"return value*100+state;} "
			"int partially_typed(int n){array<int> values=array<int>();values.reserve(64);"
			"for(int i=0;i<64;i++)values.push(i);uint32 state=17u;int total=0;"
			"for(int i=0;i<n;i++){state=state^(state<<13u);state=state^(state>>17u);"
			"state=state^(state<<5u);total+=values[to_int(state&63u)];print(total);}"
			"return total;} "
			"double affine_double_map(int n){map<int,double> values=map<int,double>();"
			"values.reserve(64);for(int i=0;i<64;i++)values[i]=1.0;int state=1;"
			"for(int i=0;i<n;i++){state=(state*25173+13849)%64;"
			"values[state]=values[state]+0.5;}return values[state];} "
			"int general_double_map(int n){map<int,double> values=map<int,double>();"
			"values.reserve(64);for(int i=0;i<64;i++)values[i]=(i%7)*0.25+1.0;"
			"int state=17;double total=0.0;for(int i=0;i<n;i++){"
			"state=(state*25173+13849)%65521;int key=(state+i*13)%64;"
			"double value=values[key]*1.000001+(state%7)*0.25;"
			"if(value>10000.0)value*=0.5;values[key]=value;total+=value;}"
			"return to_int(total)+state;} "
			"int generic_short(int n){uint32 state=1357911u;int total=0;for(int i=0;i<n;i++){"
			"state=state^(state<<13u);state=state^(state>>17u);state=state^(state<<5u);"
			"Pair value=Pair();value.first=to_int(state&31u);"
			"value.second=to_int((state>>8u)&31u);total+=value.sum();}return total;} "
			"bool string_arrays() { array<string> a={\"a\",\"b\"}; "
			"array<string> b={\"a\",\"c\"}; bool same=false; "
			"for(int i=0;i<2;i++) if(a[i]==b[i]) same=true; return same; } "
			"void main(){print(fast_array(4));print(state_fields(100));"
			"print(state_virtual(100));print(state_virtual_outer(100));"
			"print(assigned_short(100));print(xorshift_array(257));"
			"print(xorshift_map(257));print(xorshift_set(257));print(xorshift_set_call(257));"
			"print(string_map_call(257));print(mixed_numeric_containers(0));"
			"print(partially_typed(0));print(generic_short(0));print(generic_short(257));"
			"print(general_double_map(5));print(aliased_counter_state());"
			"print(aliased_set_counter_total());"
			"print(double_fields(4));}\n");
		if (!Expect(static_cast<bool>(Module),
				"compile Mixed profitability classification suite"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto EntryFor = [&](const char* Name)
		{
			return Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		};
		const auto HasFusedKind = [](const VeroScript::AsmJit::FJitFunctionEntry* Entry,
			const VeroScript::AsmJit::JitLinear::FTypedLoopPlan::EFusedKind Kind)
		{
			return Entry && Entry->TypedLoopPlans
				&& std::any_of(Entry->TypedLoopPlans->Plans.begin(),
					Entry->TypedLoopPlans->Plans.end(),
					[Kind](const VeroScript::AsmJit::JitLinear::FTypedLoopPlan& Plan)
					{
						return Plan.FusedKind == Kind;
					});
		};
		const auto HasHoistedObjectPlan = [](const VeroScript::AsmJit::FJitFunctionEntry* Entry)
		{
			return Entry && Entry->TypedLoopPlans
				&& std::any_of(Entry->TypedLoopPlans->Plans.begin(),
					Entry->TypedLoopPlans->Plans.end(),
					[](const VeroScript::AsmJit::JitLinear::FTypedLoopPlan& Plan)
					{
						return Plan.bHoistsNonEscapingObject
									&& Plan.NativeI32Code != nullptr;
					});
		};
		const auto HasExpectedHoistedObjectBehavior = [&](
			const VeroScript::AsmJit::FJitFunctionEntry* Entry)
		{
			#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
			return Entry && !Entry->bPreferVmForExecution
				&& HasHoistedObjectPlan(Entry);
			#else
			(void)HasHoistedObjectPlan;
			return Entry && Entry->bPreferVmForExecution;
			#endif
		};
		const auto* LocalReceivers = EntryFor("local_receivers");
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		const bool LocalReceiversEligible = LocalReceivers && !LocalReceivers->bPreferVmForExecution && LocalReceivers->TypedLoopPlans &&
			std::any_of(LocalReceivers->TypedLoopPlans->Plans.begin(), LocalReceivers->TypedLoopPlans->Plans.end(),
				[](const auto& Plan) {return Plan.NativeI32Code && std::any_of(Plan.FieldMethods.begin(), Plan.FieldMethods.end(),
					[](const auto& Method) {return Method.ArgumentCount == 0u;});});
		#else
		const bool LocalReceiversEligible = LocalReceivers && LocalReceivers->bPreferVmForExecution;
		#endif
		if (!Expect(EntryFor("fast_array")
				&& !EntryFor("fast_array")->bPreferVmForExecution,
				"compact typed array remains Mixed eligible")
			|| !Expect(EntryFor("indirect_array")
				&& !EntryFor("indirect_array")->bPreferVmForExecution,
				"multi-container indirect loop uses typed storage in Mixed")
			|| !Expect(EntryFor("branch_fields")
				&& !EntryFor("branch_fields")->bPreferVmForExecution,
				"direct i32 field loop remains Mixed eligible")
			|| !Expect(EntryFor("double_fields")
				&& !EntryFor("double_fields")->bPreferVmForExecution,
				"direct f64 field loop remains Mixed eligible")
			|| !Expect(LocalReceiversEligible,
				"stable zero-argument receiver branches use complete field kernels when available")
			|| !Expect(HasExpectedHoistedObjectBehavior(EntryFor("stateful_short")),
				"stateful short-lived object loop hoists its proven non-escaping object")
			|| !Expect(EntryFor("string_arrays")
				&& !EntryFor("string_arrays")->bPreferVmForExecution,
				"borrowed string-array equality loop remains Mixed eligible")
			|| !Expect(EntryFor("state_fields")
				&& !EntryFor("state_fields")->bPreferVmForExecution,
				"state-driven field loop remains JIT eligible")
			|| !Expect(EntryFor("state_virtual")
				&& !EntryFor("state_virtual")->bPreferVmForExecution,
				"state-driven polymorphic transform remains JIT eligible")
			|| !Expect(EntryFor("state_virtual_outer")
				&& !EntryFor("state_virtual_outer")->bPreferVmForExecution,
				"state-driven polymorphic transform with outer modulus remains JIT eligible")
			|| !Expect(HasExpectedHoistedObjectBehavior(EntryFor("assigned_short")),
				"assigned non-escaping object projection remains JIT eligible")
			|| !Expect(EntryFor("xorshift_array")
				&& !EntryFor("xorshift_array")->bPreferVmForExecution,
				"uint32 bitwise array loop uses typed uint IR")
			|| !Expect(EntryFor("xorshift_map")
				&& !EntryFor("xorshift_map")->bPreferVmForExecution,
				"uint32 bitwise map loop uses typed uint IR")
			|| !Expect(EntryFor("xorshift_set")
				&& !EntryFor("xorshift_set")->bPreferVmForExecution,
				"uint32 bitwise set loop uses typed uint IR")
			|| !Expect(EntryFor("xorshift_set_call")
				&& EntryFor("xorshift_set_call")->bUseScalarSetStorage
				&& !EntryFor("xorshift_set_call")->bPreferVmForExecution,
				"scalar helper calls preserve non-escaping typed Set storage")
			|| !Expect(EntryFor("string_map_call")
				&& EntryFor("string_map_call")->bUseScalarArrayStorage
				&& EntryFor("string_map_call")->bUseScalarMapStorage
				&& !EntryFor("string_map_call")->bPreferVmForExecution,
				"scalar helper calls preserve multi-container typed storage")
			|| !Expect(EntryFor("mixed_numeric_containers")
				&& EntryFor("mixed_numeric_containers")->bUseScalarArrayStorage
				&& EntryFor("mixed_numeric_containers")->bUseScalarMapStorage
				&& EntryFor("mixed_numeric_containers")->bUseScalarSetStorage
				&& !EntryFor("mixed_numeric_containers")->bPreferVmForExecution,
				"uint32/float/double multi-container workload retains typed storage")
			|| !Expect(HasFusedKind(EntryFor("constant_uint_map"),
					VeroScript::AsmJit::JitLinear::FTypedLoopPlan::EFusedKind::
						DirectConstantScalarIndexedUpdate),
				"constant-bound uint32 map update uses whole-loop typed fusion")
			|| !Expect(!HasFusedKind(EntryFor("aliased_counter_state"),
					VeroScript::AsmJit::JitLinear::FTypedLoopPlan::EFusedKind::
						DirectConstantScalarIndexedUpdate),
				"fused constant map update rejects aliased counter/state locals")
			|| !Expect(!HasFusedKind(EntryFor("aliased_set_counter_total"),
					VeroScript::AsmJit::JitLinear::FTypedLoopPlan::EFusedKind::
						DirectAffineSetContains),
				"fused Set membership rejects aliased counter/total locals")
			|| !Expect(EntryFor("partially_typed")
				&& EntryFor("partially_typed")->bPreferVmForExecution,
				"partial typed-loop coverage does not suppress VM cost fallback")
			|| !Expect(EntryFor("affine_double_map")
				&& !EntryFor("affine_double_map")->bPreferVmForExecution,
				"canonical int-double map kernel remains Mixed eligible")
			|| !Expect(EntryFor("general_double_map")
				&& !EntryFor("general_double_map")->bPreferVmForExecution,
				"general int-double map loop uses typed F64 IR")
			|| !Expect(HasExpectedHoistedObjectBehavior(EntryFor("generic_short")),
				"converted non-escaping object projection uses native scalar replacement"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend,
					"6\n31733\n31713\n31713\n6497\n9270\n8616\n139\n139\n647\n0\n0\n0\n8032\n29705\n32\n2013\n4.5\n",
					"Mixed profitability classification semantics"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
			Vm.SetExecutionBackend(Backend);
			VeroScript::FValue Result;
			if (!Expect(Vm.ExecuteFunction(*Module, FindFunctionIndex(*Module, "local_receivers"),
							{std::int32_t{257}}, nullptr, &Result) &&
							std::get_if<std::int32_t>(&Result) && std::get<std::int32_t>(Result) == 257,
					"stable zero-argument receiver branches match VM on actual execution"))
			{
				return EXIT_FAILURE;
			}
			#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
			if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Vm.GetJitStats().NativeTypedLoopExecutions != 0u,
																	 "stable receiver branch fixture actually enters its native field loop"))
			{
				return EXIT_FAILURE;
			}
			#endif
		}
		{
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
			ConfigureBackend(Vm);
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
			if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script),
					"cost-model fallback execution")
				|| !Expect(Output.str()
					== "6\n31733\n31713\n31713\n6497\n9270\n8616\n139\n139\n647\n0\n0\n0\n8032\n29705\n32\n2013\n4.5\n",
					"cost-model fallback output")
			#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
				|| !Expect(Vm.GetJitStats().NativeTypedLoopExecutions >= 5,
					"non-canonical uint32/container loops execute native typed kernels")
				|| !Expect(Vm.GetJitStats().CostModelVmFallbacks >= 1,
					"cost-model fallback counter covers the remaining partial loop")
			#else
				|| !Expect(Vm.GetJitStats().NativeTypedLoopExecutions == 0,
					"execution without the x64 typed-loop emitter reports no native typed kernels")
			#endif
			)
			{
				return EXIT_FAILURE;
			}
			Vm.ResetJitStats();
			if (!Expect(Vm.GetJitStats().CostModelVmFallbacks == 0,
					"cost-model fallback counter resets"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Regression: an indexed selector can choose among virtual receiver
		// parameters without constructing those receivers in the hot function. This
		// shape used to evade the complex-loop cost model and made Mixed/JIT slower
		// than the VM on every iteration.
		const auto Module = CompileSource(
			"interface IndexedOperation { virtual int step(int value)=0; }; "
			"class IndexedAdd3:IndexedOperation { public:int step(int value){return (value+3)%1000000007;} }; "
			"class IndexedAdd5:IndexedOperation { public:int step(int value){return (value+5)%1000000007;} }; "
			"class IndexedAdd7:IndexedOperation { public:int step(int value){return (value+7)%1000000007;} }; "
			"class IndexedAdd11:IndexedOperation { public:int step(int value){return (value+11)%1000000007;} }; "
			"int indexed_virtual_params(IndexedOperation a,IndexedOperation b,"
			"IndexedOperation c,IndexedOperation d,int n){array<int> order={1,1,2,0,2,2,3};"
			"IndexedOperation current=a;int state=9601;int total=0;int i=0;while(i<n){"
			"state=(state*469+1242)%14581;int sel=order[(state+(i%7))%7];"
			"if(sel==0)current=a;else if(sel==1)current=b;else if(sel==2)current=c;else current=d;"
			"total=current.step(total);i++;}return total+state;} "
			"void main(){IndexedOperation a=IndexedAdd3();IndexedOperation b=IndexedAdd5();"
			"IndexedOperation c=IndexedAdd7();IndexedOperation d=IndexedAdd11();"
			"print(indexed_virtual_params(a,b,c,d,100));}\n");
		if (!Expect(static_cast<bool>(Module),
				"compile indexed polymorphic receiver cost regression"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const VeroScript::AsmJit::FJitFunctionEntry* const Entry = Cache.GetCandidate(
			FindFunctionIndex(*Module, "indexed_virtual_params"));
		if (!Expect(Entry && Entry->bPreferVmForExecution,
				"indexed polymorphic receiver loop prefers VM without complete typed coverage")
		#if !(VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64))
			|| !Expect(Entry && Entry->bSuppressNestedJitOnVmFallback,
				"portable indexed polymorphic fallback suppresses nested Linear dispatch")
		#endif
		)
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*Module, Backend);
			if (!Expect(Capture.bSuccess && Capture.Output == "11878\n",
					"indexed polymorphic receiver fallback preserves semantics")
				|| (Backend != VeroScript::EExecutionBackend::Vm
					&& !Expect(Capture.JitStats.CostModelVmFallbacks >= 1,
						"indexed polymorphic receiver executes through cost fallback")))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		const auto Module = CompileSource(
			"int checked(int value) { if (value < 0) return -value; "
			"if ((value & 3) == 0) throw value + 11; return value * 3; } "
			"int caught_work(int limit, int start) { int total = 0; "
			"for (int i = start; i < limit; ++i) { "
			"try { total = total + i * 2 + checked(i); } "
			"catch (int value) { value += 3; total -= value; } } return total; } "
			"void main() { print(caught_work(12, -2)); }");
		if (!Expect(static_cast<bool>(Module), "compile conditional scalar throw in a protected call"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
		    {VeroScript::EExecutionBackend::Vm,
		        VeroScript::EExecutionBackend::Mixed,
		        VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "213\n",
				    "protected scalar calls discard retained operands and bind each caught value"))
			{
				return EXIT_FAILURE;
			}
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, "caught_work"));
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		constexpr auto ExpectedKind = VeroScript::AsmJit::EJitEntryKind::NativeCfg;
		constexpr std::uint16_t ExpectedInlineSites = 1;
		#else
		constexpr auto ExpectedKind = VeroScript::AsmJit::EJitEntryKind::VmTrampoline;
		constexpr std::uint16_t ExpectedInlineSites = 0;
		#endif
		if (!Expect(Entry && Entry->Kind == ExpectedKind && Entry->InlinedCallSites == ExpectedInlineSites,
			    "protected scalar call uses a verified native catch edge where available"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::string Source =
		    "int checked(int value) {\n"
		    "  if (value > 0) throw value;\n"
		    "  return 1 / value;\n"
		    "}\n"
		    "int work(int value) { int result = 0; try { result = checked(value); } "
		    "catch (int thrown) { result = thrown + 10; } return result; }\n"
		    "void main() { print(work(1)); print(work(0)); }";
		if (!ExpectDifferentialRun(Source,
		        "native caught scalar call preserves runtime error and callee source location"))
		{
			return EXIT_FAILURE;
		}
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "compile scalar catch diagnostics regression"))
		{
			return EXIT_FAILURE;
		}
		const auto CalleeIndex = FindFunctionIndex(*Module, "checked");
		if (!Expect(CalleeIndex < Module->Functions.size(), "find scalar catch callee"))
		{
			return EXIT_FAILURE;
		}
		// Model a stripped library leaf linked into a source caller.
		Module->Functions[CalleeIndex].DebugLines.clear();
		Module->Functions[CalleeIndex].Location = {};
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, "work"));
		if (!Expect(Entry && Entry->Kind == VeroScript::AsmJit::EJitEntryKind::VmTrampoline,
		        "a stripped scalar catch callee keeps its original diagnostic boundary"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectDifferentialRun(
	        "int checked(int value) { if (value > 0) throw cast<int64>(value); return value; } "
	        "int work(int value) { try { return 4 + checked(value); } "
	        "catch (int thrown) { return thrown + 90; } } "
	        "void main() { try { print(work(9)); } catch (int64 thrown) { print(thrown); } }",
	        "native catch optimization preserves a mismatched exception for outer handlers"))
	{
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
