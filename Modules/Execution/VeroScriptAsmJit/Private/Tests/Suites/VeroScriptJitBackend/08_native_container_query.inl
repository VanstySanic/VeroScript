// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// The direct read-only entry and the generic adapter share query semantics.
// Keep these checks available without asmjit as well as through native scripts.
{
	using namespace VeroScript;
	using namespace VeroScript::AsmJit::JitLinear;
	constexpr std::uint32_t Offset = 0x12345678u;
	const auto PushAliases = [&](auto Initial, ETypedLoopLocalKind Kind, const std::uint64_t Value)
	{
		using T = decltype(Initial);
		std::vector<T> Values{Initial};
		FTypedLoopNativeBinding Root{Kind, &Values, Values.data(), Values.size()};
		FTypedLoopNativeBinding Receiver = Root, Tail = Root;
		Root.AliasRoot = Receiver.AliasRoot = Tail.AliasRoot = &Root;
		Root.NextAlias = &Receiver;
		Receiver.NextAlias = &Tail;
		const auto Token = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&Receiver));
		FTypedLoopNativeContext Context;
		Context.ArrayGets = 31u;
		Context.MapSets = 37u;
		for (std::size_t Count = 1; Count <= 257u; ++Count)
		{
			if (TypedLoopNativeContainerMethodI32(&Context, Token, Value, 2u, Offset) != 0 || Context.FailureOffset != Offset || Context.FailureMessage || Context.Scratch != Token || Context.ArrayPushes != Count || Context.ArrayGets != 31u || Context.MapSets != 37u || Values.front() != Initial || Values.back() != static_cast<T>(Value))
			{
				return false;
			}
			for (const auto* Alias : {&Root, &Receiver, &Tail})
			{
				if (Alias->NativeData != Values.data() || Alias->NativeSize != Values.size()) return false;
			}
		}
		return true;
	};
	if (!Expect(PushAliases(std::int32_t{7}, ETypedLoopLocalKind::ArrayI32,
	                static_cast<std::uint64_t>(INT32_MIN)) &&
	            PushAliases(std::uint32_t{7}, ETypedLoopLocalKind::ArrayU32, UINT32_MAX),
	        "native push refreshes every alias after reallocation and preserves element bits and statistics"))
	{
		return EXIT_FAILURE;
	}
	const auto Check = [&](FTypedLoopNativeBinding& Binding, std::uint64_t Key,
	                       bool Found, const char* Error = nullptr)
	{
		const auto Token = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&Binding));
		for (const bool Generic : {false, true})
		{
			FTypedLoopNativeContext Context;
			Context.Scratch = 77;
			Context.FailureMessage = "previous error";
			Context.ArrayGets = 31;
			Context.MapSets = 37;
			const auto Status = Generic
			    ? TypedLoopNativeContainerMethodI32(&Context, Token, Key, 1, Offset)
			    : TypedLoopNativeContainsI32(&Context, Token, Key, Offset);
			if (Context.FailureOffset != Offset || Context.ArrayGets != 31 || Context.MapSets != 37)
			{
				return false;
			}
			if (Error)
			{
				if (Status != static_cast<std::int32_t>(VeroScript::AsmJit::EJitRuntimeStatus::GenericFailure) || Context.Scratch != 77 || !Context.FailureMessage || std::string_view(Context.FailureMessage) != Error)
				{
					return false;
				}
			}
			else if (Status != 0 || Context.FailureMessage != nullptr || Context.Scratch != (Found ? 1u : 0u))
			{
				return false;
			}
		}
		return true;
	};
	const auto NumericMap = [&](auto& Values, ETypedLoopLocalKind Kind, auto Key)
	{
		Values.InsertOrAssign(Key, 9);
		FTypedLoopNativeBinding Binding{Kind, &Values};
		return Check(Binding, static_cast<std::uint64_t>(Key), true) && Check(Binding, 123, false);
	};
	FMap::FIntI32Entries II;
	FMap::FIntU32Entries IU;
	FMap::FUIntI32Entries UI;
	FMap::FUIntU32Entries UU;
	if (!Expect(NumericMap(II, ETypedLoopLocalKind::MapI32I32, std::int32_t{-1}) && NumericMap(IU, ETypedLoopLocalKind::MapI32U32, std::int32_t{-1}) && NumericMap(UI, ETypedLoopLocalKind::MapU32I32, UINT32_MAX) && NumericMap(UU, ETypedLoopLocalKind::MapU32U32, UINT32_MAX),
	        "native contains preserves all numeric map kinds"))
	{
		return EXIT_FAILURE;
	}
	FSet::FIntScalarElements Signed;
	FSet::FUIntScalarElements Unsigned;
	Signed.Insert(INT32_MIN);
	Unsigned.Insert(UINT32_MAX);
	FTypedLoopNativeBinding SI{ETypedLoopLocalKind::SetI32, &Signed};
	FTypedLoopNativeBinding SU{ETypedLoopLocalKind::SetU32, &Unsigned};
	if (!Expect(Check(SI, static_cast<std::uint64_t>(INT32_MIN), true) && Check(SI, 123, false) && Check(SU, UINT32_MAX, true) && Check(SU, 123, false),
	        "native contains preserves signed and unsigned set keys"))
	{
		return EXIT_FAILURE;
	}
	const std::string Key = "query-key";
	FTypedLoopNativeStringView View{&Key, Key.data(), Key.size(),
	    std::hash<std::string>{}(Key)};
	const auto StringToken = TypedLoopNativeStringTag | static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&View));
	FMap::FStringI32Entries MapStrings;
	FSet::FStringScalarElements SetStrings;
	FTypedLoopNativeBinding MS{ETypedLoopLocalKind::MapStringI32, &MapStrings};
	FTypedLoopNativeBinding SS{ETypedLoopLocalKind::SetString, &SetStrings};
	if (!Expect(Check(MS, StringToken, false) && Check(SS, StringToken, false),
	        "native contains empty string containers"))
	{
		return EXIT_FAILURE;
	}
	MapStrings.InsertOrAssign(Key, 9);
	SetStrings.Insert(Key);
	if (!Expect(Check(MS, StringToken, true) && Check(SS, StringToken, true) && Check(SS, 0, false, "JIT typed loop: runtime plan mismatch") && Check(SS, TypedLoopNativeStringTag, false, "JIT typed loop: invalid borrowed string"),
	        "native contains validates borrowed string queries"))
	{
		return EXIT_FAILURE;
	}
	View.Value = nullptr;
	FTypedLoopNativeBinding Unsupported;
	if (!Expect(Check(MS, StringToken, false, "JIT typed loop: invalid borrowed string") && Check(Unsupported, 0, false, "JIT typed loop: runtime plan mismatch"),
	        "native contains preserves failure state and location"))
	{
		return EXIT_FAILURE;
	}
	FTypedLoopNativeContext Context;
	Context.Scratch = 77;
	const auto Failure = static_cast<std::int32_t>(VeroScript::AsmJit::EJitRuntimeStatus::GenericFailure);
	if (!Expect(TypedLoopNativeContainsI32(nullptr, 0, 0, Offset) == Failure && TypedLoopNativeContainsI32(&Context, 0, 0, Offset) == Failure && Context.Scratch == 77 && Context.FailureOffset == VeroScript::AsmJit::InvalidJitInstructionOffset,
	        "native contains rejects missing context or receiver without side effects"))
	{
		return EXIT_FAILURE;
	}
}

#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
{
	using namespace VeroScript;
	using namespace VeroScript::AsmJit::JitLinear;
	for (const bool Expression : {false, true})
	{
		const std::string Source =
		    "int work(){set<int> values=set<int>();values.reserve(4096);"
		    "int i=0;while(i<4096){values.add(i);i++;}int total=0;i=0;"
		    "while(i<500000){if(values.has(" +
		    std::string(Expression ? "i%8192" : "i") + ")){total++;}i++;}return total;}void main(){print(work());}";
		auto Module = CompileSource(Source.c_str());
		if (!Expect(static_cast<bool>(Module), "compile native query path diagnostic"))
		{
			return EXIT_FAILURE;
		}
		for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			const auto Before = TestNativeContainsCallCount();
			if (!ExpectRun(*Module, Backend, Expression ? "250144\n" : "4096\n",
			        "native query path diagnostic output"))
			{
				return EXIT_FAILURE;
			}
			const auto Calls = TestNativeContainsCallCount() - Before;
			#if VEROSCRIPT_HAS_ASMJIT
			if (!Expect(Calls == 500000, "exact native query entry exercised"))
			{
				return EXIT_FAILURE;
			}
			#else
			if (!Expect(Calls == 0, "asmjit-off does not execute native query entry"))
			{
				return EXIT_FAILURE;
			}
			#endif
			std::cout << "[native-query observed] expression=" << Expression
			          << " backend=" << ExecutionBackendName(Backend)
			          << " calls=" << Calls << '\n';
		}
	}
}
#endif
