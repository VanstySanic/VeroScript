// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptHost.h"
#include "VeroScriptSession.h"
#include "../../../../../VeroScript/Private/Tests/VeroScriptTestJitCapabilities.h"
#include "VeroScriptAsmJit.h"
#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptCompile.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

using namespace VeroScript;
namespace
{
	struct FHost final : IEngineHost
	{
		//~ Begin IEngineHost Interface
		void OnPrintLine(std::string_view Line) override
		{
			Printed.append(Line);
			Printed.push_back('\n');
			if (OnPrint)
			{
				OnPrint();
			}
		}

		void OnDiagnosticLine(std::string_view) override
		{
		}
		//~ End IEngineHost Interface

		std::string Printed;
		std::function<void()> OnPrint;

	};
	bool Check(bool Value, const char* Message)
	{
		if (!Value)
		{
			std::cerr << "SessionJitCacheTests: " << Message << '\n';
		}
		return Value;
	}
	bool Call(VeroScriptSession& Session, const char* Name, std::int32_t Expected,
	const std::vector<FValue>& Args = {})
	{
		FValue Value;
		const auto Result = Session.CallFunction(Name, Args, &Value);
		return Check(Result.bSuccess && std::holds_alternative<std::int32_t>(Value)
		&& std::get<std::int32_t>(Value) == Expected, Name);
	}
	bool NativeNoop(const FNativeCallContext&, FValue&, std::string&)
	{
		return true;
	}
	bool CacheIsEmpty(const VeroScriptSession& Session)
	{
		const auto Stats = Session.GetJitCacheStats();
		return Stats.CompileAttempts == 0 && Stats.RetainedPayloadBytes == 0
		&& Stats.NativeBytes == 0 && !Stats.bResourceLimitReached;
	}
	struct FThrowingInputBuffer final : std::streambuf
	{
		int_type underflow() override
		{
			++Calls;
			throw std::runtime_error("test input interruption");
		}

		std::size_t Calls = 0;

	};
}

int main()
{
	for (const auto FunctionCount : {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{4096}})
	{
		FBytecodeModule Module;
		Module.Functions.resize(FunctionCount);
		AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(Module, EExecutionBackend::Mixed);
		if (!Check(Cache.RetainedPayloadBytes() == AsmJit::FJitCodeCache::MinimumPayloadBytes(FunctionCount),
				"minimum AsmJit payload includes all generation metadata"))
		{
			return EXIT_FAILURE;
		}
		if (FunctionCount == 4096)
		{
			const auto Before = Cache.RetainedPayloadBytes();
			(void)Cache.EnsureCompiled(Module, 0);
			const auto WithEntry = Cache.RetainedPayloadBytes();
			Cache.DiscardCandidate(0);
			if (!Check(Before < FunctionCount * sizeof(AsmJit::FJitFunctionEntry) / 2u &&
			        WithEntry >= Cache.RetainedPayloadBytes() + sizeof(AsmJit::FJitFunctionEntry),
			    "unvisited functions retain only dispatch slots and discarded artifacts release their payload"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	{
		FBytecodeModule Module;
		Module.Functions.emplace_back().Code = {static_cast<std::uint8_t>(EOpcode::SameObject)};
		FJitProgramView Program;
		Program.Module = &Module;
		std::string Error;
		const auto Factory = CreateAsmJitBackendFactory();
		const auto Instance = Factory->CreateInstance(Program, {}, Error);
		if (!Check(Instance != nullptr, "unsupported candidate instance"))
		{
			return EXIT_FAILURE;
		}
		const auto Before = Instance->GetResourceUsage().RetainedBytes;
		FJitCompileRequest Request;
		Request.Program = Program;
		Request.AvailableResources.RetainedBytes = AsmJit::FJitCodeCache::MinimumPayloadBytes(Module.Functions.size());
		FJitCompileResponse Response;
		if (!Check(Instance->Compile(Request, Response) == EJitCompilationResult::Unsupported &&
		        !Response.Function &&
		        Instance->GetResourceUsage().RetainedBytes <= Before + Request.AvailableResources.RetainedBytes,
		    "unsupported candidate retains no artifact payload"))
		{
			return EXIT_FAILURE;
		}
	}
	const char* Source = "int state=7; int next(){state++;return state;} "
		"int add(int a,int b){return a+b;} int fail(int n){return 10/n;} void main(){}";
	for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
	{
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
		Session.SetExecutionBackend(Backend);
		if (!Check(Session.LoadSource(Source, "session-cache.vs").bSuccess, "load")
			|| !Call(Session, "add", 48, {std::int32_t{17}, std::int32_t{31}})
			|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "cold call compiles"))
		{
			return EXIT_FAILURE;
		}
		for (int I = 0; I < 16; ++I)
		{
			if (!Call(Session, "add", 48, {std::int32_t{17}, std::int32_t{31}})
					|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "warm call must not recompile")
					|| !Check(Session.GetLastJitStats().CompiledEntries == 0, "compilation stats are run scoped"))
			{
				return EXIT_FAILURE;
			}
		}
		if (!Call(Session, "next", 8) || !Call(Session, "next", 8))
		{
			return EXIT_FAILURE;
		}
		// A failure discards the borrowed cache; later calls must remain usable.
		if (!Check(!Session.CallFunction("fail", {std::int32_t{0}}).bSuccess, "division error")
			|| !Call(Session, "add", 3, {std::int32_t{1}, std::int32_t{2}})
			|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "failure cache is not retained"))
		{
			return EXIT_FAILURE;
		}
		FJitPolicy Policy;
		Policy.bReuseSessionCode = false;
		Session.SetJitPolicy(Policy);
		for (int I = 0; I < 2; ++I)
		{
			if (!Call(Session, "next", 8)
					|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "explicit cache opt-out"))
			{
				return EXIT_FAILURE;
			}
		}
		Policy.bReuseSessionCode = true;
		Session.SetJitPolicy(Policy);
		if (!Call(Session, "next", 8) || !Call(Session, "next", 8)
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "reenable reuse"))
		{
			return EXIT_FAILURE;
		}
		const auto WarmCache = Session.GetJitCacheStats();
		if (!Check(WarmCache.CompileAttempts > 0
				&& WarmCache.RetainedPayloadBytes > 0,
				"warm cache exposes settled context statistics"))
		{
			return EXIT_FAILURE;
		}
		FEngineConfig Config;
		Config.JitBackendFactory = CreateAsmJitBackendFactory();
		Config.ExecutionBackend = EExecutionBackend::Vm;
		Session.ApplyConfig(Config);
		if (!Check(CacheIsEmpty(Session),
				"ApplyConfig immediately invalidates the cache context")
			|| !Call(Session, "next", 8)
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "VM remains interpreter"))
		{
			return EXIT_FAILURE;
		}
		Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
		Session.SetExecutionBackend(Backend);
		if (!Call(Session, "next", 8)
			|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "backend change cold cache"))
		{
			return EXIT_FAILURE;
		}
		FExecutionLimits Limits;
		Limits.MaxInstructions = 10000;
		Session.SetExecutionLimits(Limits);
		if (!Check(CacheIsEmpty(Session), "governance immediately invalidates the cache context")
			|| !Call(Session, "next", 8)
			|| !Check((Backend == EExecutionBackend::Mixed ? Session.GetLastJitStats().Dispatches == 0
				: Session.GetLastJitStats().ControlledNativeExecutions > 0 || !Test::SupportsAsmJitControlFlow(CreateAsmJitBackendFactory()->GetDescription())), "governance uses the selected safe path"))
		{
			return EXIT_FAILURE;
		}
		Session.SetExecutionLimits({});
		if (!Call(Session, "next", 8) || !Call(Session, "next", 8)
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "warm immediately before replacement"))
		{
			return EXIT_FAILURE;
		}
		Session.SetExecutionControl(std::make_shared<FExecutionControl>());
		if (!Check(CacheIsEmpty(Session), "execution control immediately invalidates cache"))
		{
			return EXIT_FAILURE;
		}
		Session.SetExecutionControl({});
		if (!Call(Session, "next", 8) || !Call(Session, "next", 8)
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "warm after execution control removal"))
		{
			return EXIT_FAILURE;
		}
		FScriptCapabilities Restricted = Session.GetCapabilities();
		Restricted.bAllowFileRead = false;
		Session.SetCapabilities(Restricted);
		if (!Check(CacheIsEmpty(Session), "capability policy immediately invalidates cache"))
		{
			return EXIT_FAILURE;
		}
		Session.SetCapabilities({});
		if (!Call(Session, "next", 8) || !Call(Session, "next", 8)
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "warm after capability reset"))
		{
			return EXIT_FAILURE;
		}
		Session.SetHotReloadEnabled(true);
		if (!Check(CacheIsEmpty(Session), "hot reload immediately invalidates cache"))
		{
			return EXIT_FAILURE;
		}
		Session.SetHotReloadEnabled(false);
		if (!Call(Session, "next", 8) || !Call(Session, "next", 8)
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "warm after hot-reload reset"))
		{
			return EXIT_FAILURE;
		}
		if (!Check(Session.LoadSource("int next(){return 99;} void main(){}", "session-cache.vs").bSuccess, "replace module")
			|| !Call(Session, "next", 99)
			|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "replacement cannot use old warm code")
			|| !Call(Session, "next", 99)
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "replacement warms independently"))
		{
			return EXIT_FAILURE;
		}
		// Runtime revisions permit stable bindings while invalidating registration changes.
		if (!Check(Session.RegisterNative("native_noop", ENativeType::Void, {}, NativeNoop), "register native")
			|| !Check(CacheIsEmpty(Session), "native registration immediately invalidates cache"))
		{
			return EXIT_FAILURE;
		}
		for (int I = 0; I < 2; ++I)
		{
			if (!Call(Session, "next", 99)
					|| !Check((Session.GetLastJitStats().CompileAttempts > 0) == (I == 0), "stable native binding reuses code"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	{
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
		Session.SetExecutionBackend(EExecutionBackend::Mixed);
		FJitPolicy Policy;
		Policy.MixedHotCallThreshold = 3;
		Session.SetJitPolicy(Policy);
		if (!Check(Session.LoadSource("int value(){return 5;} int other(){return 6;} void main(){}", "heat.vs").bSuccess, "heat load"))
		{
			return EXIT_FAILURE;
		}
		for (int I = 0; I < 3; ++I)
		{
			if (!Call(Session, "value", 5)
					|| !Check((Session.GetLastJitStats().CompileAttempts > 0) == (I == 2), "heat survives fresh VM"))
			{
				return EXIT_FAILURE;
			}
		}
		Policy.MixedHotCallThreshold = 1;
		Policy.MaxCompileAttempts = 1;
		Session.SetJitPolicy(Policy);
		if (!Call(Session, "value", 5) || !Call(Session, "other", 6)
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0 && Session.GetLastJitStats().CompileBudgetSkips > 0,
				"compile budget is generation scoped"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// A small eager budget belongs to the requested no-init host entry,
		// rather than being consumed by an unrelated low-index function.
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
		Session.SetExecutionBackend(EExecutionBackend::Jit);
		FJitPolicy Policy;
		Policy.MaxCompileAttempts = 1;
		Policy.EagerCompileBudget = 1;
		Session.SetJitPolicy(Policy);
		if (!Check(Session.LoadSource(
				"int cold0(){return 0;}int cold1(){return 1;}int cold2(){return 2;}"
				"int cold3(){return 3;}int requested(int n){return n*2+1;}void main(){}",
				"preferred-entry.vs")
					   .bSuccess,
				"preferred entry load")
			|| !Call(Session, "requested", 43, {std::int32_t{21}})
			|| !Check(Session.GetLastJitStats().CompileAttempts == 1
					&& Session.GetLastJitStats().Dispatches > 0,
				"eager budget compiles and dispatches the requested entry")
			|| !Call(Session, "requested", 45, {std::int32_t{22}})
			|| !Check(Session.GetLastJitStats().CompileAttempts == 0
					&& Session.GetLastJitStats().Dispatches > 0,
				"requested entry remains warm without recompilation"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
		Session.SetExecutionBackend(EExecutionBackend::Jit);
		std::string Source = "int leaf(int n){return n+7;} int relay(int n){return leaf(n)*3;} "
		    "int requested(int n){return relay(n)+leaf(n);} void main(){print(requested(5));}";
		for (int I = 0; I < 256; ++I)
		{
			Source += "int cold" + std::to_string(I) + "(){return " + std::to_string(I) + ";}";
		}
		if (!Check(Session.LoadSource(Source, "direct-preparation.vs").bSuccess, "direct preparation load")
		    || !Call(Session, "requested", 48, {std::int32_t{5}})
		    || !Check(Session.GetLastJitStats().CompileAttempts == 3 && Session.GetLastJitStats().EagerCompileSkips >= 256,
		        "named entry prepares only its direct call graph")
		    || !Call(Session, "requested", 52, {std::int32_t{6}})
		    || !Check(Session.GetLastJitStats().CompileAttempts == 0, "direct preparation stays warm")
		    || !Call(Session, "cold255", 255)
		    || !Check(Session.GetLastJitStats().CompileAttempts == 1, "a previously unrelated body remains callable")
		    || !Check(Session.Run().bSuccess && Host.Printed == "48\n", "module entry retains normal execution"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// Cached object plans must receive the current VM/objects, not pointers
		// into a previous invocation. Destructors still run at each call end.
		for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			FHost Host;
			VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
			Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
			Session.SetExecutionBackend(Backend);
			if (!Check(Session.LoadSource(
				"class Item {public: int x=0; ~Item(){print(91);} int get(){return x;}};"
				"int use(int n){Item value=Item();value.x=n;return value.get();} void main(){}",
				"owned-cache.vs")
						   .bSuccess,
					"object cache load"))
			{
				return EXIT_FAILURE;
			}
			for (int I = 0; I < 40; ++I)
			{
				Host.Printed.clear();
				if (!Call(Session, "use", I, {static_cast<std::int32_t>(I)})
					|| !Check(Host.Printed == "91\n", "one destructor per fresh call")
					|| !Check(I == 0 || Session.GetLastJitStats().CompileAttempts == 0,
						"object/method plans reuse without stale VM state"))
				{
					return EXIT_FAILURE;
				}
			}
			// Print is an observable host boundary even without native bindings.
			bool ReentryRejected = false;
			Host.OnPrint = [&]
			{
				ReentryRejected = !Session.CallFunction("use", {std::int32_t{4}}).bSuccess;
			};
			if (!Call(Session, "use", 7, {std::int32_t{7}})
				|| !Check(ReentryRejected, "cached invocation preserves no-reentry"))
			{
				return EXIT_FAILURE;
			}
			Host.OnPrint = {};
			std::size_t Intercepted = 0;
			Session.SetInstanceCallInterceptor([&](const FInstanceCallInterceptContext&,
				FValue&, FScriptError&)
			{
				++Intercepted;
				return EInstanceCallInterceptResult::Unhandled;
			});
			if (!Check(CacheIsEmpty(Session), "interceptor immediately invalidates cache"))
			{
				return EXIT_FAILURE;
			}
			for (int I = 0; I < 2; ++I)
			{
				if (!Call(Session, "use", 8, {std::int32_t{8}})
						|| !Check((Session.GetLastJitStats().CompileAttempts > 0) == (I == 0),
							"stable interceptor retains generation code"))
				{
					return EXIT_FAILURE;
				}
			}
			if (!Check(Intercepted > 0, "interceptor remains observable"))
			{
				return EXIT_FAILURE;
			}
			Session.SetInstanceCallInterceptor({});
			if (!Call(Session, "use", 8, {std::int32_t{8}}) || !Call(Session, "use", 8, {std::int32_t{8}})
				|| !Check(Session.GetLastJitStats().CompileAttempts == 0,
					"interceptor removal warms fresh code"))
			{
				return EXIT_FAILURE;
			}
			Session.SetMutationCallback([](const FMutationEvent&)
			{
			});
			if (!Check(CacheIsEmpty(Session), "mutation callback immediately invalidates cache"))
			{
				return EXIT_FAILURE;
			}
			for (int I = 0; I < 2; ++I)
			{
				if (!Call(Session, "use", 8, {std::int32_t{8}})
						|| !Check((Session.GetLastJitStats().CompileAttempts > 0) == (I == 0), "stable observation callback retains code"))
				{
					return EXIT_FAILURE;
				}
			}
			Session.SetMutationCallback({});
			if (!Call(Session, "use", 9, {std::int32_t{9}}) || !Call(Session, "use", 10, {std::int32_t{10}})
				|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "callback removal permits fresh cache"))
			{
				return EXIT_FAILURE;
			}
			Session.Clear();
			if (!Check(!Session.IsLoaded(), "clear drops loaded generation")
				|| !Check(Session.LoadSource("int use(int n){return n+11;}void main(){}", "owned-cache.vs").bSuccess, "load after clear")
				|| !Call(Session, "use", 12, {std::int32_t{1}}))
			{
				return EXIT_FAILURE;
			}
		}
	}
	{
		// Reused plans/direct targets must rebind container storage, scalar
		// arguments and error locations to each fresh invocation.
		const char* NestedSource =
			"int recur(int n){if(n<=0)return 1;return recur(n-1)+n;}\n"
			"int kernel(int n){array<int> values=array<int>();"
			"for(int i=0;i<n;i++)values.push(i+2);int total=0;"
			"for(int i=0;i<n;i++)total+=values[i];return total+recur(6);}\n"
			"int divide(int n){print(n);return 100/n;}\n"
			"void bump(ref int n){n+=5;}\n"
			"void main(){}\n";
		for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			FHost Host, ReferenceHost;
			VeroScriptSession Session(Host), Reference(ReferenceHost);
		VeroScript::Test::Configure(Session);
		VeroScript::Test::Configure(Reference);
			Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
			Session.SetExecutionBackend(Backend);
			Reference.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
			Reference.SetExecutionBackend(EExecutionBackend::Vm);
			if (!Check(Session.LoadSource(NestedSource, "nested-cache.vs").bSuccess
					&& Reference.LoadSource(NestedSource, "nested-cache.vs").bSuccess, "nested load"))
			{
				return EXIT_FAILURE;
			}
			for (int N = 0; N < 12; ++N)
			{
				if (!Call(Session, "kernel", N * (N - 1) / 2 + 2 * N + 22, {static_cast<std::int32_t>(N)})
						|| !Check(N == 0 || Session.GetLastJitStats().CompileAttempts == 0,
							"nested/container plans warm across fresh VM storage"))
				{
					return EXIT_FAILURE;
				}
			}
			for (int N = 0; N < 3; ++N)
			{
				std::vector<FValue> Arguments{static_cast<std::int32_t>(N)};
				if (!Check(Session.CallFunction("bump", Arguments).bSuccess
						&& std::holds_alternative<std::int32_t>(Arguments[0])
						&& std::get<std::int32_t>(Arguments[0]) == N + 5, "cached session ref write-back"))
				{
					return EXIT_FAILURE;
				}
			}
			if (!Call(Session, "divide", 50, {std::int32_t{2}}))
			{
				return EXIT_FAILURE;
			}
			Host.Printed.clear();
			const auto Actual = Session.CallFunction("divide", {std::int32_t{0}});
			const auto Expected = Reference.CallFunction("divide", {std::int32_t{0}});
			if (!Check(!Actual.bSuccess && !Expected.bSuccess
					&& Actual.ExitCode == Expected.ExitCode && Host.Printed == ReferenceHost.Printed
					&& Actual.Error.Message == Expected.Error.Message
					&& Actual.Error.MessageId == Expected.Error.MessageId
					&& Actual.Error.MessageArgs == Expected.Error.MessageArgs
					&& Actual.Error.Location.FileLabel == Expected.Error.Location.FileLabel
					&& Actual.Error.Location.Line == Expected.Error.Location.Line
					&& Actual.Error.Location.Column == Expected.Error.Location.Column,
					"warmed failure retains VM diagnostic and partial output"))
			{
				return EXIT_FAILURE;
			}
			if (!Call(Session, "kernel", 27, {std::int32_t{2}}))
			{
				return EXIT_FAILURE;
			}
			FScriptCapabilities Capabilities;
			Capabilities.bAllowConsoleOutput = false;
			Session.SetCapabilities(Capabilities);
			if (!Check(!Session.CallFunction("divide", {std::int32_t{2}}).bSuccess
					, "capability changes remain enforced on controlled paths"))
			{
				return EXIT_FAILURE;
			}
			Session.SetCapabilities({});
			auto Control = std::make_shared<FExecutionControl>();
			Control->RequestCancel();
			Session.SetExecutionControl(Control);
			if (!Check(!Session.CallFunction("kernel", {std::int32_t{2}}).bSuccess
					&& Session.GetLastJitStats().Dispatches == 0, "cancelled invocation does not enter cached JIT"))
			{
				return EXIT_FAILURE;
			}
			Session.SetExecutionControl({});
			if (!Call(Session, "kernel", 27, {std::int32_t{2}})
				|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "governance exit rebuilds code"))
			{
				return EXIT_FAILURE;
			}
			for (int UnlimitedKind = 0; UnlimitedKind < 2; ++UnlimitedKind)
			{
				FJitPolicy Policy;
				if (UnlimitedKind == 0)
				{
					Policy.MaxCompileAttempts = 0;
				}
				else
				{
					Policy.MaxCachedEntries = 0;
				}
				Session.SetJitPolicy(Policy);
				for (int CallIndex = 0; CallIndex < 2; ++CallIndex)
				{
					if (!Call(Session, "kernel", 27, {std::int32_t{2}})
							|| !Check(CallIndex == 0 ? Session.GetLastJitStats().CompileAttempts > 0
								: Session.GetLastJitStats().CompileAttempts == 0,
								"unlimited policies retain code without resetting cumulative attempts"))
					{
						return EXIT_FAILURE;
					}
				}
			}
		}
	}
	{
		// The retained code must not remember a prior receiver identity or an
		// overload chosen for different argument types. Host-held return values
		// also remain valid while later invocations use fresh VM storage.
		const char* SourceWithShapes =
			"interface Choice {virtual int value()=0;};"
			"class First:Choice {int value(){return 11;}};"
			"class Second:Choice {int value(){return 29;}};"
			"int choose(int tag){Choice c=First();if(tag!=0)c=Second();return c.value();}"
			"class Held {public:int x=0;int get(){return x;}};"
			"Held make(int n){Held v=Held();v.x=n;return v;}"
			"int read(Held v){return v.x;}"
			"int twice(int n){return n*2;}function(int)->int callable(){return twice;}"
			"int overload(int n){return n+1;}"
			"double overload(double n){return n+0.5;}"
			"void main(){}";
		for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			FHost Host;
			VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
			Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
			Session.SetExecutionBackend(Backend);
			if (!Check(Session.LoadSource(SourceWithShapes, "shape-cache.vs").bSuccess,
					"shape cache load"))
			{
				return EXIT_FAILURE;
			}
			for (int N = 0; N < 20; ++N)
			{
				if (!Call(Session, "choose", (N & 1) == 0 ? 11 : 29, {std::int32_t{N & 1}})
						|| !Check(N < 2 || Session.GetLastJitStats().CompileAttempts == 0,
							"alternating receiver shapes use current invocation"))
				{
					return EXIT_FAILURE;
				}
			}
			FValue Saved, Later;
			if (!Check(Session.CallFunction("make", {std::int32_t{3}}, &Saved).bSuccess,
					"first host-held object")
				|| !Check(Session.CallFunction("make", {std::int32_t{42}}, &Later).bSuccess,
					"later host-held object")
				|| !Check(Session.GetLastJitStats().CompileAttempts == 0,
					"object factory code reused")
				|| !Call(Session, "read", 3, {Saved}) || !Call(Session, "read", 42, {Later})
				|| !Call(Session, "read", 3, {Saved})
				|| !Check(Session.GetLastJitStats().CompileAttempts == 0,
					"host-held objects survive intervening fresh VMs"))
			{
				return EXIT_FAILURE;
			}
			FValue Function;
			if (!Check(Session.CallFunction("callable", {}, &Function).bSuccess, "retained callable factory")) return EXIT_FAILURE;
			const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(Function));
			for (int Iteration = 0; Iteration < 3; ++Iteration)
			{
				FValue Result;
				if (!Check(Session.CallMethod(std::get<FObjectHandle>(Saved), "get", {}, &Result).bSuccess && std::get<std::int32_t>(Result) == 3 && (Iteration == 0 || Session.GetLastJitStats().CompileAttempts == 0), "method entry reuses generation cache") || !Check(Session.InvokeCallable(Callable, {std::int32_t{21}}, &Result).bSuccess && std::get<std::int32_t>(Result) == 42 && (Iteration == 0 || Session.GetLastJitStats().CompileAttempts == 0), "retained callable reuses generation cache"))
				{
					return EXIT_FAILURE;
				}
			}
			Session.ReleaseCallable(Callable);
			for (int N = 0; N < 8; ++N)
			{
				if (!Call(Session, "overload", N + 1, {static_cast<std::int32_t>(N)})
					|| !Check(N == 0 || Session.GetLastJitStats().CompileAttempts == 0,
						"integer overload remains warm"))
				{
					return EXIT_FAILURE;
				}
				FValue Value;
				if (!Check(Session.CallFunction("overload", {static_cast<double>(N)}, &Value).bSuccess
						&& std::holds_alternative<double>(Value) && std::get<double>(Value) == N + 0.5,
						"current argument types choose double overload")
					|| !Check(N == 0 || Session.GetLastJitStats().CompileAttempts == 0,
						"double overload remains warm"))
				{
					return EXIT_FAILURE;
				}
			}
		}
	}
	{
		for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			FHost Host;
			VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
			Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
			Session.SetExecutionBackend(Backend);
			if (!Check(Session.LoadSource(Source, "transitions.vs").bSuccess, "transition load")
				|| !Call(Session, "next", 8) || !Call(Session, "next", 8))
			{
				return EXIT_FAILURE;
			}
			if (!Check(!Session.LoadSource("invalid source !", "bad.vs").bSuccess, "failed replacement")
				|| !Call(Session, "next", 8)
				|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "failed load preserves warm generation"))
			{
				return EXIT_FAILURE;
			}
			auto Compiled = VeroScriptCompile::CompileSource(
				"int state=7;int next(){return 91;}void main(){}", "new.vs");
			if (!Check(Compiled.bSuccess,
					"compile replacement"))
			{
				return EXIT_FAILURE;
			}
			auto Replacement = std::move(Compiled.Module);
			if (!Check(Session.ReloadCompiledModule(Replacement, "new.vs").bSuccess && !Replacement,
					"stage from actually warm cache")
				|| !Call(Session, "next", 91) || !Call(Session, "next", 91) || !Call(Session, "next", 91)
				|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "replacement warms after transaction"))
			{
				return EXIT_FAILURE;
			}
			Compiled = VeroScriptCompile::CompileSource(
				"int boom(){return 10/0;}int state=boom();int next(){return 33;}void main(){}", "rollback.vs");
			if (!Check(Compiled.bSuccess, "compile runtime-failing initializer"))
			{
				return EXIT_FAILURE;
			}
			Replacement = std::move(Compiled.Module);
			if (!Check(Session.ReloadCompiledModule(Replacement, "rollback.vs").bSuccess, "stage rollback from warm")
				|| !Check(!Session.CallFunction("next", {}).bSuccess, "initializer aborts transaction")
				|| !Call(Session, "next", 91)
				|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "rollback rebuilds restored generation")
				|| !Call(Session, "next", 91)
				|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "restored code warms"))
			{
				return EXIT_FAILURE;
			}
			Session.SetHotReloadEnabled(true);
			if (!Call(Session, "next", 91)
				|| !Check(Session.GetLastJitStats().Dispatches > 0, "persistent transition retains backend execution"))
			{
				return EXIT_FAILURE;
			}
			Session.SetHotReloadEnabled(false);
			if (!Call(Session, "next", 91)
				|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "leaving persistent rebuilds"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	{
		// A failed invocation may discard code, but cannot replenish its budget.
		for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			FHost Host;
			VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
			Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
			Session.SetExecutionBackend(Backend);
			FJitPolicy Policy;
			Policy.MaxCompileAttempts = 3;
			Policy.EagerCompileBudget = 1;
			Session.SetJitPolicy(Policy);
			if (!Check(Session.LoadSource("int fail(int n){return 10/n;}void main(){}", "budget.vs").bSuccess,
					"budget failure load"))
			{
				return EXIT_FAILURE;
			}
			std::size_t Attempts = 0;
			for (int I = 0; I < 8; ++I)
			{
				if (!Check(!Session.CallFunction("fail", {std::int32_t{0}}).bSuccess, "repeated failure"))
				{
					return EXIT_FAILURE;
				}
				Attempts += Session.GetLastJitStats().CompileAttempts;
				if (!Check(Attempts <= 3 && Session.GetJitCacheStats().CompileAttempts == Attempts,
						"failed rebuild consumes independent budget"))
				{
					return EXIT_FAILURE;
				}
			}
			// Other APIs use their own VM and stats, but do not refund this
			// unchanged CallFunction cache context or charge stale run stats.
			if (!Check(Session.Run().bSuccess
					&& Session.GetJitCacheStats().CompileAttempts == Attempts,
					"ordinary Run preserves the independent exhausted context")
				|| !Call(Session, "fail", 5, {std::int32_t{2}})
				|| !Check(Attempts == 3 && Session.GetLastJitStats().CompileAttempts == 0,
					"exhausted generation remains usable without compilation"))
			{
				return EXIT_FAILURE;
			}
			Session.SetJitPolicy({});
			if (!Call(Session, "fail", 5, {std::int32_t{2}})
				|| !Check(Session.GetLastJitStats().CompileAttempts > 0, "explicit policy reset grants new budget"))
			{
				return EXIT_FAILURE;
			}
			if (!Check(Session.LoadSource(
					"int work(int n){set<int> v={1,2,3};int state=1;int total=0;"
					"for(int i=0;i<n;i++){state=(state*7+3)%257;if(v.has(state))total++;}return total;}void main(){}",
					"metadata-limit.vs")
						   .bSuccess,
					"metadata budget load"))
			{
				return EXIT_FAILURE;
			}
			FJitProgramView BudgetProgram;
			BudgetProgram.Module = Session.GetLoadedModule();
			std::string BudgetError;
			auto BudgetInstance = VeroScript::CreateAsmJitBackendFactory()->CreateInstance(BudgetProgram, {}, BudgetError);
			FSessionJitCacheLimits MetadataLimits;
			MetadataLimits.MaxRetainedPayloadBytes = FJitBackendCache::MinimumRetainedBytes(
				Session.GetLoadedModule()->Functions.size())
				+ BudgetInstance->GetResourceUsage().RetainedBytes
				+ VeroScript::AsmJit::FJitCodeCache::MinimumPayloadBytes(Session.GetLoadedModule()->Functions.size()) + 64;
			Session.SetJitCacheLimits(MetadataLimits);
			if (!Call(Session, "work", 0, {std::int32_t{0}})
				|| !Check(Session.GetLastJitStats().CompileAttempts > 0
					&& Session.GetJitCacheStats().bResourceLimitReached
					&& Session.GetJitCacheStats().RetainedPayloadBytes == 0,
					"nested plan/native payload limit checked after compilation")
				|| !Call(Session, "work", 0, {std::int32_t{0}})
				|| !Check(Session.GetLastJitStats().CompileAttempts == 0, "metadata oversize cannot recompile"))
			{
				return EXIT_FAILURE;
			}
			if (!Check(Session.LoadSource("int fail(int n){return 10/n;}void main(){}", "budget.vs").bSuccess,
					"restore resource fixture"))
			{
				return EXIT_FAILURE;
			}
			for (const bool NativeLimit : {false, true})
			{
				FSessionJitCacheLimits Limits;
				if (NativeLimit)
				{
					Limits.MaxNativeBytes = 0;
				}
				else
				{
					Limits.MaxRetainedPayloadBytes = 1;
				}
				Session.SetJitCacheLimits(Limits);
				for (int I = 0; I < 4; ++I)
				{
					if (!Call(Session, "fail", 5, {std::int32_t{2}}))
					{
						return EXIT_FAILURE;
					}
					const auto Stats = Session.GetJitCacheStats();
					// No-asmjit legitimately retains zero native bytes.
					if ((!NativeLimit || Stats.bResourceLimitReached)
						&& !Check(Stats.RetainedPayloadBytes == 0 && Stats.NativeBytes == 0
							&& (I == 0 || Session.GetLastJitStats().CompileAttempts == 0),
							"oversize does not repeatedly cold compile or retain code"))
					{
						return EXIT_FAILURE;
					}
					if (!NativeLimit && !Check(Stats.bResourceLimitReached
							&& Session.GetLastJitStats().CompileAttempts == 0, "dense table rejected before allocation"))
					{
						return EXIT_FAILURE;
					}
					#if VEROSCRIPT_HAS_ASMJIT
					if (NativeLimit && !Check(Stats.bResourceLimitReached, "native oversize branch exercised"))
					{
						return EXIT_FAILURE;
					}
					#else
					if (NativeLimit && !Check(!Stats.bResourceLimitReached && Stats.NativeBytes == 0
							&& Stats.RetainedPayloadBytes > 0
							&& (I == 0 || Session.GetLastJitStats().CompileAttempts == 0),
							"zero native quota permits portable cached plans"))
					{
						return EXIT_FAILURE;
					}
					#endif
				}
			}
		}
	}
	{
		// Host-owned streams may enable C++ exceptions. An unwound invocation
		// must drop the lease without refunding compilation or reporting stale
		// retained memory, both before and after the first successful warm call.
		for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			for (const int Scenario : {0, 1, 2})
			{
				const bool WarmFirst = Scenario == 1;
				FThrowingInputBuffer Buffer;
				FHost Host;
				VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
				Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
				Session.SetExecutionBackend(Backend);
				FJitPolicy Policy;
				Policy.MaxCompileAttempts = 1;
				Policy.EagerCompileBudget = 1;
				Session.SetJitPolicy(Policy);
				auto Input = std::make_unique<std::istream>(&Buffer);
				Input->exceptions(std::ios::badbit);
				auto* const InputPtr = Input.get();
				Session.SetOwnedInputStream(std::move(Input));
				const auto Loaded = Session.LoadSource(
						"import \"io.vs\";int seed(){return 1;}int initial=seed();"
						"int value(){return 17;}int read_value(){string text=std::io::read_line();return 23;}void main(){}",
						"exception-budget.vs");
				if (!Check(Loaded.bSuccess, "exception budget load"))
				{
					std::cerr << Loaded.Error.Location.FileLabel << ":" << Loaded.Error.Location.Line << ":" << Loaded.Error.Location.Column << ": " << Loaded.Error.Message << '\n';
					return EXIT_FAILURE;
				}
				if (Scenario == 2)
				{
					FSessionJitCacheLimits Limits;
					// Permit the empty instance but reject its generation metadata.
					FJitProgramView BudgetProgram;
					BudgetProgram.Module = Session.GetLoadedModule();
					std::string BudgetError;
					auto BudgetInstance = VeroScript::CreateAsmJitBackendFactory()->CreateInstance(BudgetProgram, {}, BudgetError);
					Limits.MaxRetainedPayloadBytes = FJitBackendCache::MinimumRetainedBytes(
							Session.GetLoadedModule()->Functions.size())
						+ BudgetInstance->GetResourceUsage().RetainedBytes + 64;
					Session.SetJitCacheLimits(Limits);
				}
				if (WarmFirst && (!Call(Session, "value", 17) || !Call(Session, "value", 17)
							|| !Check(Session.GetLastJitStats().CompileAttempts == 0
								&& Session.GetJitCacheStats().RetainedPayloadBytes > 0,
								"exception starts with a genuinely warm lease")))
				{
					return EXIT_FAILURE;
				}
				std::size_t PreviousAttempts = Session.GetJitCacheStats().CompileAttempts;
				for (int I = 0; I < 8; ++I)
				{
					InputPtr->clear();
					bool Interrupted = false;
					try
					{
						(void)Session.CallFunction("read_value", {});
					}
					catch (const std::runtime_error& Error)
					{
						Interrupted = std::string_view(Error.what()) == "test input interruption";
					}
					const auto Stats = Session.GetJitCacheStats();
					if (!Check(Interrupted && Buffer.Calls == static_cast<std::size_t>(I + 1),
								"the same host input exception propagates on every call")
							|| !Check(Stats.CompileAttempts > 0 && Stats.CompileAttempts >= PreviousAttempts
								&& Stats.CompileAttempts <= Policy.MaxCompileAttempts,
								"C++ exception must not refund compile attempts")
							|| !Check(Stats.CompileAttempts == PreviousAttempts
								+ Session.GetLastJitStats().CompileAttempts,
								"exceptional invocation statistics are run-scoped deltas")
							|| !Check(Stats.RetainedPayloadBytes == 0 && Stats.NativeBytes == 0,
								"C++ exception must clear retained-memory gauges")
							|| !Check(Scenario != 2 || Stats.bResourceLimitReached,
								"C++ exception must preserve the resource-limit latch"))
					{
						return EXIT_FAILURE;
					}
					PreviousAttempts = Stats.CompileAttempts;
				}
				if (!Check(PreviousAttempts == Policy.MaxCompileAttempts,
							"repeated C++ exceptions exhaust the original budget")
						|| !Call(Session, "value", 17)
						|| !Check(Session.GetLastJitStats().CompileAttempts == 0,
							"exceptional budget exhaustion still allows VM execution"))
				{
					return EXIT_FAILURE;
				}
			}
		}
	}
	{
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		Session.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory());
		Session.SetExecutionBackend(EExecutionBackend::Jit);
		if (!Check(Session.LoadSource(Source, "persistent.vs").bSuccess, "persistent load")
			|| !Call(Session, "next", 8) || !Call(Session, "next", 9)
			|| !Check(Session.GetLastJitStats().Dispatches > 0 && Session.GetLastJitStats().CompileAttempts == 0,
				"persistent state reuses generation-bound JIT code"))
		{
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
