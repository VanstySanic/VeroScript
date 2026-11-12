// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

{
	const auto Module = CompileSource(
		"string source=\"abc\\0x\";wstring wide_source=L\"\\u4e2d\\u6587\";"
		"int narrow(int n){string chunk=source;string text=\"\";for(int i=0;i<n;i++)text+=to_string(chunk);return to_int(len(text));}"
		"int wide(int n){wstring chunk=wide_source;string text=\"\";for(int i=0;i<n;i++)text+=to_string(chunk);return to_int(len(text));}"
		"int boolean(int n){bool value=true;string text=\"\";for(int i=0;i<n;i++)text+=to_string(value);return to_int(len(text));}"
		"int decimal(int n){double value=1.25;string text=\"\";for(int i=0;i<n;i++)text+=to_string(value);return to_int(len(text));}"
		"int integral(int n,int seed){string text=\"\";for(int i=0;i<n;i++)text+=to_string(seed+i);return to_int(len(text));}"
		"int indexed(array<int> values,int n){string text=\"\";for(int i=0;i<n;i++)text+=to_string(values[i]);return to_int(len(text));}"
		"void main(){print(narrow(0));print(narrow(1));print(narrow(32));print(wide(4));print(boolean(4));print(decimal(4));"
		"print(integral(32,17));array<int> values={1,23,-456};print(indexed(values,3));}");
	if (!Expect(static_cast<bool>(Module), "compile noninteger formatting and integer native coverage")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
	if (!Expect(Reference.bSuccess, "noninteger formatting reference succeeds")) return EXIT_FAILURE;
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Candidate = RunCapture(*Module, Backend);
		if (!Expect(Candidate.bSuccess && Candidate.Output == Reference.Output,
				"formatting keeps canonical string, Unicode, bool, decimal and integer values"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	for (const auto Name : {"narrow", "wide", "boolean", "decimal"})
	{
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(Entry && Entry->bPreferVmForExecution,
				"unsupported native format inputs preserve profitable canonical VM selection"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const auto Name : {"integral", "indexed"})
	{
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(Entry && !Entry->bPreferVmForExecution && Entry->TypedLoopPlans &&
						Entry->TypedLoopPlans->bHasProfitablePlan,
				"integer and dynamically proved indexed integer formatting retain native plans"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
}

{
	const auto Module = CompileSource(
		"int counted(int n,int seed){string text=\"\";int state=seed;for(int i=0;i<n;i++){"
		"state=(state*31+7)%1009;text+=to_string(state);text+=\";\";}return to_int(len(text))+state;}"
		"int suffix(int n,string extra){string text=\"\";for(int i=0;i<n;i++)text+=extra;return to_int(len(text));}"
		"int literal_suffix(int n){string text=\"\";string extra=\"abcdefghabcdefgh\";for(int i=0;i<n;i++)text+=extra;return to_int(len(text));}"
		"int self_append(int n){string text=\"abcdefgh\";for(int i=0;i<n;i++)text+=text;return to_int(len(text));}"
		"int snapshot(int n){string text=\"a\";string saved=\"\";for(int i=0;i<n;i++){saved=text;text+=\"b\";}return to_int(len(text)+len(saved));}"
		"int loop_temp(int n){string text=\"\";for(int i=0;i<n;i++){string tmp=\"abcdefghijklmnop\";text+=tmp;}return to_int(len(text));}"
		"int loop_alias(int n){string text=\"\";int sum=0;for(int i=0;i<n;i++){text+=\"x\";string copy=text;sum+=to_int(len(copy));}return sum;}"
		"int opaque(int n){int total=0;for(int i=0;i<n;i++)total+=i;return total;}"
		"int post_call(int n){string text=\"\";for(int i=0;i<n;i++)text+=\"a\";return opaque(32)+to_int(len(text));}"
		"int mutated(int n){string text=\"\";for(int i=0;i<n;i++){text+=\"a\";n--;}return to_int(len(text));}"
		"int nonzero(int n){string text=\"\";for(int i=3;i<n;i++)text+=\"a\";return to_int(len(text));}"
		"int twice(int n){string text=\"\";for(int i=0;i<n;i++)text+=\"a\";"
		"for(int i=0;i<n;i++)text+=\"b\";return to_int(len(text));}"
		"void main(){print(counted(-1,17));print(counted(0,17));print(counted(1,17));print(counted(8,17));"
		"print(counted(128,17));print(suffix(1,\"abcdefghijklmnopqrstuvwxyz\"));"
		"print(literal_suffix(1));print(self_append(4));print(snapshot(4));print(loop_temp(4));print(loop_alias(4));print(post_call(1));"
		"print(mutated(8));print(nonzero(8));print(twice(8));}");
	if (!Expect(static_cast<bool>(Module), "compile workload-based text entry selection")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Candidate = RunCapture(*Module, Backend);
		if (!Expect(Reference.bSuccess && Candidate.bSuccess && Candidate.Output == Reference.Output,
				"short text selection preserves negative, zero, small, large and noncanonical invocation semantics"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Candidate.JitStats.CostModelVmFallbacks >= 2u &&
																		 Candidate.JitStats.NativeTypedLoopExecutions != 0u,
																 "verified short inputs choose VM while sufficiently large inputs retain native execution"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Counted = Cache.GetCandidate(FindFunctionIndex(*Module, "counted"));
	if (!Expect(Counted && Counted->TextEntryVmTripThreshold != 0u,
			"canonical bounded integer text work receives an input-based cost hint"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Name : {"suffix", "literal_suffix", "self_append", "snapshot", "loop_temp", "loop_alias", "post_call", "mutated", "nonzero", "twice"})
	{
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(Entry && Entry->TextEntryVmTripThreshold == 0u,
				"unknown suffix sizes, mutable bounds, nonzero starts and unrelated loops decline the small-work shortcut"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
}

{
	const std::string LargeLiteral(262144u, 'a');
	const auto Module = CompileSource(std::string(
		"int64 query(string a,string b){return __find(a,b);}"
		"int large_prefix(int n){string text=\"") + LargeLiteral +
		"\";for(int i=0;i<n;i++)text+=\"x\";return to_int(len(text));}"
		"int large_body(int n){string text=\"\";for(int i=0;i<n;i++)text+=\"" + LargeLiteral +
		"\";return to_int(len(text));}"
		"int large_needle(int n){string text=\"\";for(int i=0;i<n;i++)text+=\"x\";return to_int(query(text,\"" + LargeLiteral +
		"\"));}void main(){print(large_prefix(1));print(large_body(1));print(large_needle(1));}");
	if (!Expect(static_cast<bool>(Module), "compile independently large prefix, loop literal and readonly query operands")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Candidate = RunCapture(*Module, Backend);
		if (!Expect(Reference.bSuccess && Candidate.bSuccess && Candidate.Output == Reference.Output,
				"small trip counts do not change large literal/query value semantics"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	for (const auto Name : {"large_prefix", "large_body", "large_needle"})
	{
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(Entry && Entry->TextEntryVmTripThreshold == 0u,
				"large known literals decline the cheap-work hint even when the loop has one trip"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
}

{
	const auto Module = CompileSource(
		"int scan(int n,int seed){string text=\"\";int state=seed;for(int i=0;i<n;i++){"
		"state=(state*31+7)%1009;text+=to_string(state);text+=\";\";}"
		"int sum=0;for(int i=0;i<len(text);i++){int c=__char_code(text,i);sum=(sum+c)%1009;}return sum;}"
		"int changed_scan(int n){string text=\"\";for(int i=0;i<n;i++)text+=\"ab\";"
		"int sum=0;for(int i=0;i<len(text);i++){sum+=__char_code(text,i);if(i==0)text+=\"x\";}return sum;}"
		"int replaced_scan(int n){string text=\"\";for(int i=0;i<n;i++)text+=\"ab\";text=\"x\";"
		"int sum=0;for(int i=0;i<len(text);i++)sum+=__char_code(text,i);return sum;}"
		"int suffix_scan(int n,string suffix){string text=\"\";for(int i=0;i<n;i++)text+=suffix;"
		"int sum=0;for(int i=0;i<len(text);i++)sum+=__char_code(text,i);return sum;}"
		"int self_scan(int n){string text=\"a\";for(int i=0;i<n;i++)text+=text;"
		"int sum=0;for(int i=0;i<len(text);i++)sum+=__char_code(text,i);return sum;}"
		"int alias_scan(int n){string text=\"\";string other=\"abcdefghijklmnop\";for(int i=0;i<n;i++)text+=\"x\";"
		"string tmp=other;for(int i=0;i<len(text);i++)tmp=text+tmp;return to_int(len(tmp));}"
		"void main(){print(scan(-1,17));print(scan(0,17));print(scan(1,17));print(scan(64,17));"
		"print(changed_scan(4));print(replaced_scan(4));print(suffix_scan(1,\"abcdef\"));print(self_scan(4));print(alias_scan(4));}");
	if (!Expect(static_cast<bool>(Module), "compile bounded construction followed by readonly text scanning")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Candidate = RunCapture(*Module, Backend);
		if (!Expect(Reference.bSuccess && Candidate.bSuccess && Candidate.Output == Reference.Output,
				"affine text work selection preserves short construction, large scans and rejected mutation/suffix cases"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Candidate.JitStats.CostModelVmFallbacks != 0u &&
																		 Candidate.JitStats.NativeTypedLoopExecutions != 0u,
																 "bounded short builder/scans choose VM while large scans retain native kernels"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Scan = Cache.GetCandidate(FindFunctionIndex(*Module, "scan"));
	if (!Expect(Scan && Scan->TextEntryVmTripThreshold > 2u,
			"bounded builder/scan derives total work from stable input and maximum appended byte length"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Name : {"changed_scan", "replaced_scan", "suffix_scan", "self_scan", "alias_scan"})
	{
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(Entry && Entry->TextEntryVmTripThreshold == 0u,
				"mutating scans, replaced roots, unknown suffixes and exponential self-appends do not receive affine hints"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
}

{
	const std::string ScanTail = "int sum=0;for(int j=0;j<len(text);j++)sum=(sum+__char_code(text,j))%1009;return sum;}";
	const auto Module = CompileSource(std::string(
		"int bounded(int n,int seed){string text=\"\";int state=seed;for(int i=0;i<n;i++){state=(state*31+7)%1009;text+=to_string(state);text+=\";\";}") + ScanTail +
		"int unknown(int n,int seed){string text=\"\";int state=seed;for(int i=0;i<n;i++){state=(state*31+7);text+=to_string(state);text+=\";\";}" + ScanTail +
		"int joined(int n,int seed){string text=\"\";int state=seed;for(int i=0;i<n;i++){if((i&1)!=0)state%=1009;text+=to_string(state);text+=\";\";}" + ScanTail +
		"int rewritten(int n,int seed){string text=\"\";int state=seed;for(int i=0;i<n;i++){state%=1009;state=seed+i;text+=to_string(state);text+=\";\";}" + ScanTail +
		"int negative(int n,int seed){string text=\"\";int state=seed;for(int i=0;i<n;i++){state%=-1009;text+=to_string(state);text+=\";\";}" + ScanTail +
		"void main(){print(bounded(2,-17));print(bounded(4,17));print(bounded(128,-17));print(unknown(2,-17));print(joined(4,-1017));print(rewritten(4,-17));print(negative(4,-1017));}");
	if (!Expect(static_cast<bool>(Module), "compile signed modulo range and control-flow text cost proofs")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Candidate = RunCapture(*Module, Backend);
		if (!Expect(Reference.bSuccess && Candidate.bSuccess && Candidate.Output == Reference.Output,
				"modulo width cost proof preserves negative values, joins, rewrites and profitable native large scans"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Bounded = Cache.GetCandidate(FindFunctionIndex(*Module, "bounded"));
	const auto* Unknown = Cache.GetCandidate(FindFunctionIndex(*Module, "unknown"));
	if (!Expect(Bounded && Unknown && Bounded->TextEntryVmTripThreshold > Unknown->TextEntryVmTripThreshold &&
					Unknown->TextEntryVmTripThreshold != 0u,
			"same-block positive modulus yields a conservative tighter signed decimal width"))
	{
		return EXIT_FAILURE;
	}
	for (const auto Name : {"joined", "rewritten", "negative"})
	{
		const auto* Entry = Cache.GetCandidate(FindFunctionIndex(*Module, Name));
		if (!Expect(Entry && Entry->TypedLoopPlans && Entry->TypedLoopPlans->Plans.size() == 2u,
				"control-flow negative cost fixtures retain two planned loops"))
		{
			return EXIT_FAILURE;
		}
		const auto& Plans = Entry->TypedLoopPlans->Plans;
		const auto FullWidthThreshold = 1024u / (Plans[0].Instructions.size() + 12u * Plans[1].Instructions.size()) + 1u;
		if (!Expect(Entry->TextEntryVmTripThreshold == FullWidthThreshold,
				"branch joins, subsequent writes and negative divisors retain the full signed-I32 byte bound"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif
}

{
	const std::string A(1024u, 'a'), B(1539u, 'b');
	const auto Body = std::string("string text=\"\";for(int i=0;i<n;i++){if((i&1)!=0)text+=\"") + A + "\";else text+=\"" + B + "\";}";
	const auto Module = CompileSource(std::string("int64 query(string a,string b){return __find(a,b);}") +
		"int append(int n){" + Body + "return to_int(len(text));}" +
		"int search(int n){" + Body + "return to_int(query(text,\"b\"));}" +
		"void main(){print(append(1));print(append(16));print(append(128));print(search(16));}");
	if (!Expect(static_cast<bool>(Module), "compile literal append and readonly-query memory work selection")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Candidate = RunCapture(*Module, Backend);
		if (!Expect(Reference.bSuccess && Candidate.bSuccess && Candidate.Output == Reference.Output,
				"literal workload selection preserves middle-size and large text values"))
		{
			return EXIT_FAILURE;
		}
	}
	#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	VeroScript::AsmJit::FJitCodeCache Cache;
	Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
	const auto* Append = Cache.GetCandidate(FindFunctionIndex(*Module, "append"));
	const auto* Search = Cache.GetCandidate(FindFunctionIndex(*Module, "search"));
	if (!Expect(Append && Search && Append->TextEntryVmTripThreshold > Search->TextEntryVmTripThreshold &&
					Search->TextEntryVmTripThreshold != 0u,
			"shared literal append copies do not prematurely amortize native startup"))
	{
		return EXIT_FAILURE;
	}
	#endif
}

{
	const auto Module = CompileSource(
		"int lengths(int n){string text=\"a\\0b\";wstring wide=L\"\\u4e2d\\u6587\";"
		"for(int i=0;i<n;i++)text+=\"0123456789abcdef0123456789abcdef\";"
		"return 7+to_int(len(text))+to_int(len(wide))+to_int(len(text));}"
		"int indirect(ref string text){return to_int(len(text));}"
		"void main(){print(lengths(0));print(lengths(1));print(lengths(128));string s=\"a\\0bc\";print(indirect(ref s));print(s);}");
	if (!Expect(static_cast<bool>(Module), "compile borrowed local length reads around native text growth")) return EXIT_FAILURE;
	const auto Reference = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
	for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		const auto Candidate = RunCapture(*Module, Backend);
		if (!Expect(Reference.bSuccess && Candidate.bSuccess && Candidate.Output == Reference.Output,
				"length reads preserve stack operands, UTF-16 units, embedded NULs and reference fallback"))
		{
			return EXIT_FAILURE;
		}
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		if (Backend == VeroScript::EExecutionBackend::Jit && !Expect(Candidate.JitStats.NativeTypedLoopExecutions != 0u,
																 "heap-backed length fixture executes native growth before borrowed reads"))
		{
			return EXIT_FAILURE;
		}
		#endif
	}
}
