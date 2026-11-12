// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptHost.h"
#include "VeroScriptSession.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptPlatform.h"

#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

static bool TestAliasedSessionOutputs()
{
	using namespace VeroScript;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	const char* Source =
	    "class CounterForAliasedSessionOutput { public: "
	    "int fieldForAliasedSessionOutput=42; int twice(int value){return value*2;} "
	    "int answerForAliasedSessionOutput(){return 42;} } "
	    "int globalForAliasedSessionOutput=42; int twice(int value){return value*2;} "
	    "int answerForAliasedSessionOutput(){return 42;} "
	    "fn(int)->int callback(){return twice;} void main(){}";
	const auto IsAnswer = [](const FRunResult& Result, const FValue& Value)
	{
		return Result.bSuccess && std::holds_alternative<std::int32_t>(Value) && std::get<std::int32_t>(Value) == 42;
	};
	if (!Session.LoadSource(Source, "aliased-session-output.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
	{
		return false;
	}
	FObjectHandle Object;
	FValue CallableValue;
	if (!Session.ConstructObject("CounterForAliasedSessionOutput", {}, Object).bSuccess || !Session.CallFunction("callback", {}, &CallableValue).bSuccess)
	{
		return false;
	}
	const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(CallableValue));
	if (!Callable) return false;
	for (int Kind = 0; Kind != 4; ++Kind)
	{
		// Exercise both read-only arguments and the overload that writes ref slots.
		for (const bool WriteBack : {false, true})
		{
			std::vector<FValue> Arguments{std::int32_t{21}};
			const auto Invoke = [&](auto& Args) -> FRunResult
			{
				if (Kind == 0) return Session.CallFunction("twice", Args, &Arguments[0]);
				if (Kind == 1) return Session.CallMethod(Object, "twice", Args, &Arguments[0]);
				if (Kind == 2) return Session.CallMethodBypassingInterceptor(Object, "twice", Args, &Arguments[0]);
				return Session.InvokeCallable(Callable, Args, &Arguments[0]);
			};
			const auto Result = WriteBack ? Invoke(Arguments)
			                              : Invoke(static_cast<const std::vector<FValue>&>(Arguments));
			if (!IsAnswer(Result, Arguments[0]))
			{
				std::cerr << "Session cleared an aliased argument before reading it\n"
				          << Errors.str();
				return false;
			}
		}
	}
	// Heap-backed names must remain valid while their owning output is replaced.
	FValue Value = std::string("answerForAliasedSessionOutput");
	if (!IsAnswer(Session.CallFunction(std::get<std::string>(Value), {}, &Value), Value))
	{
		return false;
	}
	Value = std::string("answerForAliasedSessionOutput");
	if (!IsAnswer(Session.CallMethod(Object, std::get<std::string>(Value), {}, &Value), Value))
	{
		return false;
	}
	Value = std::string("fieldForAliasedSessionOutput");
	if (!IsAnswer(Session.GetField(Object, std::get<std::string>(Value), Value), Value)) return false;
	Value = std::string("globalForAliasedSessionOutput");
	if (!IsAnswer(Session.GetGlobal(std::get<std::string>(Value), Value), Value)) return false;
	// The receiver and the constructor's type-name view may also belong to outputs.
	Value = Object;
	if (!IsAnswer(Session.CallMethod(std::get<FObjectHandle>(Value), "answerForAliasedSessionOutput", {}, &Value), Value))
	{
		return false;
	}
	Value = Object;
	if (!IsAnswer(Session.GetField(std::get<FObjectHandle>(Value), "fieldForAliasedSessionOutput", Value), Value)) return false;
	const std::string ExpectedType = Object->GetTypeName();
	if (!Session.ConstructObject(Object->GetTypeName(), {}, Object).bSuccess || !Object || Object->GetTypeName() != ExpectedType)
	{
		return false;
	}
	Session.ReleaseCallable(Callable);
	// Native generation roots are invalidated before the VM returns. A staged
	// output must keep the returned graph alive during that language cleanup.
	FValueRoot NativeRoot;
	VeroScriptSession ReturnedGraph(Host);
	VeroScript::Test::Configure(ReturnedGraph);
	if (!ReturnedGraph.RegisterNative("keep_array", FNativeTypeDesc::ArrayOf("int"),
	        {{FNativeTypeDesc::ArrayOf("int"), 0}},
	        [](const FNativeCallContext& Call, FValue& Return, std::string& Error)
	{
		if (!Call.ScriptInvoker || Call.ArgCount != 1 || !Call.ScriptInvoker->RetainForGeneration(Call.Args[0], *static_cast<FValueRoot*>(Call.UserData), Error))
		{
			return false;
		}
		Return = Call.Args[0];
		return true;
	}, &NativeRoot) ||
	    !ReturnedGraph.LoadSource("array<int> result(){array<int> values={42};return keep_array(values);}",
	                      "staged-output-root.vs")
	        .bSuccess ||
	    !ReturnedGraph.CallFunction("result", {}, &Value).bSuccess)
	{
		return false;
	}
	const auto* Array = std::get_if<FDynamicArrayHandle>(&Value);
	if (!Array || !*Array || (*Array)->ElementCount() != 1 || NativeRoot.IsValid()) return false;
	FValue Element;
	if ((*Array)->HasScalarElements())
	{
		if (!(*Array)->TryGetScalar(0, Element)) return false;
	}
	else
	{
		Element = (*Array)->Elements[0];
	}
	if (!std::holds_alternative<std::int32_t>(Element) || std::get<std::int32_t>(Element) != 42) return false;
	return true;
}

static bool TestRetainedCallableOwnership()
{
	using namespace VeroScript;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	VeroScriptSession Session(Host), Other(Host);
	VeroScript::Test::Configure(Session);
	VeroScript::Test::Configure(Other);
	const char* Source = "fn()->int make(){int value=42;return [value]()->int{return value;};} "
	                     "int execute(fn()->int work){return work();} void main(){}";
	FValue Value, OtherValue;
	if (!Session.LoadSource(Source, "shared-callable.vs").bSuccess || !Other.LoadSource(Source, "other-callable.vs").bSuccess || !Session.CallFunction("make", {}, &Value).bSuccess || !Other.CallFunction("make", {}, &OtherValue).bSuccess)
	{
		return false;
	}
	const auto First = Session.RetainCallable(std::get<FFunctionHandle>(Value));
	const auto Second = Session.RetainCallable(std::get<FFunctionHandle>(Value));
	const auto Foreign = Other.RetainCallable(std::get<FFunctionHandle>(OtherValue));
	if (!First || !Second || !Foreign || Session.IsCallableValid(Foreign) || Other.IsCallableValid(First) || Session.ReleaseCallable(Foreign) || Session.InvokeCallable(Foreign).bSuccess)
	{
		return false;
	}
	if (!Session.ReleaseCallable(First)) return false;
	FValue Answer;
	if (!Session.InvokeCallable(Second, {}, &Answer).bSuccess || !std::holds_alternative<std::int32_t>(Answer) || std::get<std::int32_t>(Answer) != 42)
	{
		std::cerr << "releasing one callable corrupted another registration\n"
		          << Errors.str();
		return false;
	}
	FValueRoot External;
	std::string Error;
	if (!Session.RetainValue(Value, External, Error) || !Session.ReleaseCallable(Second)) return false;
	Value = {};
	if (!External.Read() || !Session.CallFunction("execute", {*External.Read()}, &Answer).bSuccess || !std::holds_alternative<std::int32_t>(Answer) || std::get<std::int32_t>(Answer) != 42)
	{
		std::cerr << "releasing a callable corrupted an exported value root\n"
		          << Errors.str();
		return false;
	}
	External.Reset();
	if (!Session.DrainValueRoots(Error)) return false;
	FValue DeferredValue;
	if (!Other.CallFunction("make", {}, &DeferredValue).bSuccess)
	{
		return false;
	}
	const std::weak_ptr<FFunctionValue> DeferredFunction = std::get<FFunctionHandle>(DeferredValue);
	const auto Deferred = Other.RetainCallable(std::get<FFunctionHandle>(DeferredValue), false);
	DeferredValue = {};
	bool Released = false, Invoked = false;
	std::thread ForeignThread([&]
	{
		FValue Result;
		Invoked = Other.InvokeCallable(Deferred, {}, &Result).bSuccess && std::holds_alternative<std::int32_t>(Result) && std::get<std::int32_t>(Result) == 42;
		Released = Other.ReleaseCallable(Deferred);
	});
	ForeignThread.join();
	if (!Invoked || !Released || DeferredFunction.expired() || !Other.InvokeCallable(Foreign, {}, &Answer).bSuccess || !DeferredFunction.expired() || !Other.ReleaseCallable(Foreign))
	{
		return false;
	}
	return true;
}

static bool TestRetainedCallableReload()
{
	using namespace VeroScript;
	for (const bool GlobalCallable : {false, true})
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::string Source =
		    "class Box { public: int value=42; fn()->int getter(){return [=]()->int=>value;} } "
		    "Box shared=Box(); int read(){return shared.value;} void main(){} " +
		    std::string(GlobalCallable
		            ? "fn()->int saved=shared.getter(); fn()->int make(){return saved;} "
		              "int zero(){return 0;} void drop(){saved=zero;} int invoke(){return saved();}"
		            : "fn()->int make(){return shared.getter();}");
		FValue Value;
		if (!Session.LoadSource(Source, "callable-reload.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("make", {}, &Value).bSuccess)
		{
			return false;
		}
		const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(Value));
		const std::weak_ptr<FFunctionValue> OldFunction = std::get<FFunctionHandle>(Value);
		Value = {};
		auto Replacement = VeroScriptCompile::CompileSource(Source, "callable-reload.vs", {});
		if (!Callable || !Replacement.bSuccess || !Session.ReloadCompiledModule(Replacement.Module, "callable-reload.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess || Session.IsCallableValid(Callable) || !Session.CallFunction("read", {}, &Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 42)
		{
			std::cerr << "old callable cleanup corrupted migrated globals\n"
			          << Errors.str();
			return false;
		}
		if (GlobalCallable)
		{
			if (!Session.CallFunction("invoke", {}, &Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 42 || !Session.CallFunction("drop").bSuccess || !OldFunction.expired())
			{
				return false;
			}
		}
		else if (!OldFunction.expired())
		{
			return false;
		}
	}
	return true;
}

static bool TestPendingReloadCallableCleanup()
{
	using namespace VeroScript;
	for (int Stop = 0; Stop != 4; ++Stop)
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		int Destroyed = 0;
		{
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.SetHotReloadEnabled(true);
			if (!Session.RegisterNative("notify", ENativeType::Void, {},
			        [](const FNativeCallContext& Call, FValue&, std::string&)
			{
				++*static_cast<int*>(Call.UserData);
				return true;
			}, &Destroyed))
			{
				return false;
			}
			const char* Source = "class RemovedType { public: int value=42; ~RemovedType(){notify();} } "
			                     "fn()->int make(){RemovedType object=RemovedType();return [object]()->int{return object.value;};} "
			                     "void main(){}";
			FValue Value;
			if (!Session.LoadSource(Source, "pending-reload.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("make", {}, &Value).bSuccess)
			{
				return false;
			}
			const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(Value));
			Value = {};
			FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
			Options.Bindings = &Session.GetBindingCatalog();
			auto Replacement = VeroScriptCompile::CompileSource("void main(){}", "pending-reload.vs", Options);
			if (!Callable || !Replacement.bSuccess || !Session.ReloadCompiledModule(Replacement.Module, "pending-reload.vs").bSuccess)
			{
				return false;
			}
			// Stop before running replacement initialization. The retained object
			// still requires the old module, which alone defines RemovedType.
			if (Stop == 0)
			{
				Session.Clear();
				if (Session.IsLoaded() || Destroyed != 1) return false;
			}
			else if (Stop == 2)
			{
				if (!Session.LoadSource("void main(){}", "replacement.vs").bSuccess || Destroyed != 1)
				{
					return false;
				}
			}
			else if (Stop == 3)
			{
				Session.SetHotReloadEnabled(false);
				if (!Session.InvokeCallable(Callable, {}, &Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 42)
				{
					return false;
				}
			}
		}
		if (Destroyed != 1)
		{
			std::cerr << "pending reload lost callable cleanup metadata\n"
			          << Errors.str();
			return false;
		}
	}
	return true;
}

static bool TestFailedInitializationExports()
{
	using namespace VeroScript;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	int Destroyed = 0;
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	bool Called = false, RetainRejected = false, StageRejected = false;
	FRetainedCallableHandle ReplacementCallable;
	struct FProbeState
	{
		VeroScriptSession& Session;
		bool& Called;
		bool& RetainRejected;
		bool& StageRejected;
		FRetainedCallableHandle& ReplacementCallable;
	} ProbeState{Session, Called, RetainRejected, StageRejected, ReplacementCallable};
	if (!Session.RegisterNative("notify", ENativeType::Void, {},
	        [](const FNativeCallContext& Call, FValue&, std::string&)
	{
		++*static_cast<int*>(Call.UserData);
		return true;
	}, &Destroyed) ||
	    !Session.RegisterNative("probe", FNativeTypeDesc::Scalar(ENativeType::Int),
	        {{FNativeTypeDesc::Function("fn()->int"), 0}},
	        [](const FNativeCallContext& Call, FValue& Return, std::string& Error)
	{
		auto& State = *static_cast<FProbeState*>(Call.UserData);
		State.Called = true;
		FValueRoot External, Reservation;
		State.RetainRejected = !State.Session.RetainValue(Call.Args[0], External, Error);
		if (!State.Session.ReserveValueResult(Reservation, Error)) return false;
		FValue Prepared = Call.Args[0];
		State.StageRejected = !State.Session.StageValueResult(Reservation, Prepared, Error);
		State.ReplacementCallable = State.Session.RetainCallable(std::get<FFunctionHandle>(Call.Args[0]));
		Return = std::int32_t{0};
		return true;
	}, &ProbeState) ||
	    !Session.LoadSource("int version(){return 1;} void main(){}", "failed-exports.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
	{
		return false;
	}
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	Options.Bindings = &Session.GetBindingCatalog();
	auto Replacement = VeroScriptCompile::CompileSource(
	    "class TransientOnly { public: int read()const{return 42;} ~TransientOnly(){notify();} } "
	    "fn()->int make(){TransientOnly object=TransientOnly();return [object]()->int{return object.read();};} "
	    "int initialized=1/probe(make()); void main(){}",
	    "failed-exports.vs", Options);
	if (!Replacement.bSuccess)
	{
		std::cerr << Replacement.Error.Message << '\n';
		return false;
	}
	if (!Replacement.bSuccess || !Session.ReloadCompiledModule(Replacement.Module, "failed-exports.vs").bSuccess || Session.Run(ERunMode::Eval).bSuccess || !Called || !RetainRejected || !StageRejected || !ReplacementCallable || Session.IsCallableValid(ReplacementCallable) || Destroyed != 1)
	{
		return false;
	}
	FValue Version;
	return Session.CallFunction("version", {}, &Version).bSuccess && std::holds_alternative<std::int32_t>(Version) && std::get<std::int32_t>(Version) == 1;
}

static bool TestFailedInitializationPreservesJournalValues()
{
	using namespace VeroScript;
	for (const bool Container : {false, true})
	{
		for (const bool NativeRetention : {false, true})
		{
			std::ostringstream Output, Errors;
			FStreamEngineHost Host(Output, Errors);
			int Destroyed = 0;
			const auto Fail = [&](const char* Stage)
			{
				std::cerr << "rollback journal " << Stage << " failed (container=" << Container
				          << ", native=" << NativeRetention << ", destroyed=" << Destroyed << ")\n"
				          << Errors.str();
				return false;
			};
			{
				VeroScriptSession Session(Host);
				VeroScript::Test::Configure(Session);
				Session.SetHotReloadEnabled(true);
				struct FProbeState
				{
					VeroScriptSession& Session;
					bool NativeRetention;
					bool Called = false;
					bool Retained = false;
					FValueRoot Root{};
					FRetainedCallableHandle Callable{};
				} State{Session, NativeRetention};
				if (!Session.RegisterNative("notify", ENativeType::Void, {},
				        [](const FNativeCallContext& Call, FValue&, std::string&)
				{
					++*static_cast<int*>(Call.UserData);
					return true;
				}, &Destroyed) ||
				    !Session.RegisterNative("probe", FNativeTypeDesc::Scalar(ENativeType::Int),
				        {{FNativeTypeDesc::Function("fn()->int"), 0}},
				        [](const FNativeCallContext& Call, FValue& Return, std::string& Error)
				{
					auto& Probe = *static_cast<FProbeState*>(Call.UserData);
					Probe.Called = true;
					if (Call.ArgCount != 1) return false;
					const auto* Function = std::get_if<FFunctionHandle>(&Call.Args[0]);
					if (!Function || !*Function) return false;
					if (Probe.NativeRetention)
					{
						Probe.Retained = Call.ScriptInvoker && Call.ScriptInvoker->RetainForGeneration(Call.Args[0], Probe.Root, Error);
					}
					else
					{
						Probe.Callable = Probe.Session.RetainCallable(*Function);
						Probe.Retained = static_cast<bool>(Probe.Callable);
					}
					Return = std::int32_t{0};
					return Probe.Retained;
				}, &State))
				{
					return Fail("native registration");
				}
				const std::string Access = Container ? "shared[0]" : "shared.child";
				const std::string Source =
				    "class JournalChild { public: int value=42; ~JournalChild(){notify();} } " + std::string(Container ? "array<JournalChild> shared={JournalChild()}; " : "class JournalOwner { public: JournalChild child=JournalChild(); } "
				                                                                                                                                                           "JournalOwner shared=JournalOwner(); ") +
				    "int read(){return " + Access + ".value;} void main(){}";
				if (!Session.LoadSource(Source, "rollback-journal-roots.vs").bSuccess) return Fail("initial load");
				if (!Session.Run(ERunMode::Eval).bSuccess) return Fail("initial run");
				if (Destroyed != 0) return Fail("initial lifetime");
				// Remove the old object from the current graph. Its field/container
				// snapshot in the rollback journal must protect it while failed-init
				// cleanup retires the replacement closure that also captures it.
				const std::string ReplacementSource = Source + " fn()->int detach(){JournalChild original=" + Access + ";" + Access + "=nullptr;return [original]()->int{return original.value;};} " + "int initialized=1/probe(detach());";
				FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
				Options.Bindings = &Session.GetBindingCatalog();
				auto Replacement = VeroScriptCompile::CompileSource(
				    ReplacementSource, "rollback-journal-roots.vs", Options);
				if (!Replacement.bSuccess || !Session.ReloadCompiledModule(Replacement.Module, "rollback-journal-roots.vs").bSuccess || Session.Run(ERunMode::Eval).bSuccess || !State.Called || !State.Retained || Destroyed != 0 || (NativeRetention ? State.Root.IsValid() : Session.IsCallableValid(State.Callable)))
				{
					std::cerr << "failed initialization destroyed an object retained by the rollback journal"
					          << " (container=" << Container << ", native=" << NativeRetention << ")\n"
					          << Errors.str();
					return false;
				}
				FValue Answer;
				if (!Session.CallFunction("read", {}, &Answer).bSuccess || !std::holds_alternative<std::int32_t>(Answer) || std::get<std::int32_t>(Answer) != 42 || Destroyed != 0)
				{
					return Fail("restored read");
				}
				Session.Clear();
				if (Session.IsLoaded() || Destroyed != 1) return Fail("clear");
			}
			if (Destroyed != 1) return Fail("session destruction");
		}
	}
	return true;
}

static bool TestCleanupPreservesWeakReferences()
{
	using namespace VeroScript;
	// Owner-thread retirement, native completion during execution, and selective
	// old-generation retirement all need the weak slots of the live session.
	for (int CleanupKind = 0; CleanupKind != 3; ++CleanupKind)
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		struct FWeakCleanupState
		{
			FValueRoot Root;
			std::vector<std::int32_t> Observed;
		} State;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const auto Fail = [&](const char* Stage)
		{
			std::cerr << "weak cleanup " << Stage << " failed (kind=" << CleanupKind
			          << ", observed=" << State.Observed.size() << ")\n"
			          << Errors.str();
			return false;
		};
		if (!Session.RegisterNative("notify", ENativeType::Void, {ENativeType::Int},
		        [](const FNativeCallContext& Call, FValue&, std::string&)
		{
			if (Call.ArgCount != 1 || !std::holds_alternative<std::int32_t>(Call.Args[0])) return false;
			static_cast<FWeakCleanupState*>(Call.UserData)->Observed.push_back(std::get<std::int32_t>(Call.Args[0]));
			return true;
		}, &State) ||
		    !Session.RegisterNative("keep", FNativeTypeDesc::Scalar(ENativeType::Void),
		        {{FNativeTypeDesc::Function("fn()->int"), 0}},
		        [](const FNativeCallContext& Call, FValue&, std::string& Error)
		{
			return Call.ArgCount == 1 && Call.ScriptInvoker && Call.ScriptInvoker->RetainForGeneration(Call.Args[0], static_cast<FWeakCleanupState*>(Call.UserData)->Root, Error);
		}, &State) ||
		    !Session.RegisterNative("drop", ENativeType::Void, {},
		        [](const FNativeCallContext& Call, FValue&, std::string&)
		{
			static_cast<FWeakCleanupState*>(Call.UserData)->Root.Reset();
			return true;
		}, &State) ||
		    !Session.RegisterNative("cleanup_count", ENativeType::Int, {},
		        [](const FNativeCallContext& Call, FValue& Return, std::string&)
		{
			Return = static_cast<std::int32_t>(
			    static_cast<FWeakCleanupState*>(Call.UserData)->Observed.size());
			return true;
		}, &State))
		{
			return Fail("native registration");
		}
		const char* Source =
		    "class Target { public: int value=42; } Target live=Target(); "
		    "class Observer { public: weak<Target> target; "
		    "Observer(Target input){target=weak<Target>(input);} "
		    "~Observer(){Target locked=target.lock();notify(target.expired()||locked==nullptr?0:locked.value);} } "
		    "fn()->int make(){Observer observer=Observer(live);return [observer]()->int{return observer.target.expired()?0:1;};} "
		    "void store(){keep(make());} int release(){drop();return cleanup_count();} "
		    "int read(){return live.value;} void main(){}";
		if (!Session.LoadSource(Source, "cleanup-weak-slots.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !State.Observed.empty())
		{
			std::cerr << "weak cleanup fixture did not initialize\n"
			          << Errors.str();
			return false;
		}
		FValue Answer;
		if (CleanupKind == 1)
		{
			if (!Session.CallFunction("store").bSuccess || !State.Root.IsValid() || !State.Observed.empty() || !Session.CallFunction("release", {}, &Answer).bSuccess || !std::holds_alternative<std::int32_t>(Answer) || std::get<std::int32_t>(Answer) != 1 || State.Root.IsValid())
			{
				return Fail("active retirement");
			}
			// cleanup_count runs in the same script invocation after drop returns,
			// proving cleanup borrowed the active VM rather than waiting for idle.
		}
		else
		{
			FValue Value;
			if (!Session.CallFunction("make", {}, &Value).bSuccess || !std::holds_alternative<FFunctionHandle>(Value))
			{
				return Fail("create callback");
			}
			const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(Value));
			Value = {};
			if (!Callable || !State.Observed.empty()) return Fail("retain callback");
			if (CleanupKind == 0)
			{
				if (!Session.ReleaseCallable(Callable)) return Fail("release callback");
			}
			else
			{
				FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
				Options.Bindings = &Session.GetBindingCatalog();
				auto Replacement = VeroScriptCompile::CompileSource(Source, "cleanup-weak-slots.vs", Options);
				if (!Replacement.bSuccess || !Session.ReloadCompiledModule(Replacement.Module, "cleanup-weak-slots.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
				{
					return Fail("replace callback");
				}
			}
			if (Session.IsCallableValid(Callable)) return Fail("invalidate callback");
		}
		if (State.Observed != std::vector<std::int32_t>{42})
		{
			std::cerr << "cleanup lost the weak slots of a still-live target (kind="
			          << CleanupKind << ", observed=" << State.Observed.size() << ", first="
			          << (State.Observed.empty() ? -1 : State.Observed.front()) << ", expected=42)\n"
			          << Errors.str();
			return false;
		}
		if (!Session.CallFunction("read", {}, &Answer).bSuccess || !std::holds_alternative<std::int32_t>(Answer) || std::get<std::int32_t>(Answer) != 42)
		{
			return Fail("live target");
		}
		Session.Clear();
		if (State.Observed != std::vector<std::int32_t>{42})
		{
			return Fail("clear");
		}
	}
	return true;
}

static bool TestReloadRetirementUsesCurrentContext()
{
	const auto RunCase = [](const bool ViaDefaults)
	{
		using namespace VeroScript;
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		struct FState
		{
			FValueRoot Root;
			std::vector<std::int32_t> Observed;
			bool Stored = false;
			bool Dropped = false;
		} State;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Session.RegisterNative("drop", ENativeType::Void, {},
		        [](const FNativeCallContext& Call, FValue&, std::string&)
		{
			auto& Probe = *static_cast<FState*>(Call.UserData);
			Probe.Dropped = true;
			Probe.Root.Reset();
			return true;
		}, &State) ||
		    !Session.RegisterNative("notify", ENativeType::Void, {ENativeType::Int},
		        [](const FNativeCallContext& Call, FValue&, std::string&)
		{
			static_cast<FState*>(Call.UserData)->Observed.push_back(std::get<std::int32_t>(Call.Args[0]));
			return true;
		}, &State) ||
		    !Session.RegisterNative("save", FNativeTypeDesc::Scalar(ENativeType::Int),
		        {{FNativeTypeDesc::Function("fn()->int"), 0}},
		        [](const FNativeCallContext& Call, FValue& Return, std::string& Error)
		{
			auto& Probe = *static_cast<FState*>(Call.UserData);
			Return = std::int32_t{1};
			Probe.Stored = Call.ScriptInvoker && Call.ScriptInvoker->RetainForGeneration(Call.Args[0], Probe.Root, Error);
			return Probe.Stored;
		}, &State))
		{
			return false;
		}
		const char* Initial = "int padding=11;class Old{public:int value=1;~Old(){drop();}};"
		                      "fn()->int make(){Old object=Old();return [object]()->int{return object.value;};}"
		                      "class Counter{public:int value=1;default{value=9;}}Counter receiver=Counter();void main(){}";
		FValue Value;
		if (!Session.LoadSource(Initial, "reload-retirement-context.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("make", {}, &Value).bSuccess)
		{
			std::cerr << "reload retirement context initial load failed\n"
			          << Errors.str();
			return false;
		}
		const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(Value));
		Value = {};
		FObjectHandle Receiver;
		if (ViaDefaults)
		{
			if (!Session.GetGlobal("receiver", Value).bSuccess || !std::holds_alternative<FObjectHandle>(Value)) return false;
			Receiver = std::get<FObjectHandle>(Value);
			Value = {};
		}
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = &Session.GetBindingCatalog();
		auto Replacement = VeroScriptCompile::CompileSource(
		    "int padding=11;int sentinel=42;class New{public:int value=7;~New(){notify(sentinel);}};"
		    "fn()->int makeNew(){New object=New();return [object]()->int{return object.value;};}"
		    "class Counter{public:int value=1;default{value=9;}}Counter receiver=Counter();"
		    "int initialized=save(makeNew());int read(){return sentinel;}void main(){}",
		    "reload-retirement-context.vs", Options);
		// Successful initialization stores a new-generation root. Destroying the
		// old callback then retires it; its destructor must read the new globals.
		if (!Callable || !Replacement.bSuccess || !Session.ReloadCompiledModule(Replacement.Module, "reload-retirement-context.vs").bSuccess || !(ViaDefaults ? Session.ApplyClassDefaults(Receiver) : Session.Run(ERunMode::Eval)).bSuccess || !State.Stored || !State.Dropped || State.Root.IsValid() || Session.IsCallableValid(Callable) || State.Observed != std::vector<std::int32_t>{42} || !Errors.str().empty() || !Session.CallFunction("read", {}, &Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 42)
		{
			std::cerr << "retired native roots used the wrong hot reload context (defaults=" << ViaDefaults << ")\n"
			          << Errors.str();
			return false;
		}
		Session.Clear();
		return !Session.IsLoaded() && State.Observed == std::vector<std::int32_t>{42} && Errors.str().empty();
	};
	return RunCase(false) && RunCase(true);
}

static bool TestClassDefaultsValueSafePoint()
{
	using namespace VeroScript;
	// Cover success, body/cleanup failures, and a receiver in a retired root.
	// Each path must preserve borrowed inputs and drain roots before returning.
	for (int Failure = 0; Failure != 4; ++Failure)
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		struct FState
		{
			FValueRoot Root;
			int Destroyed = 0;
		} State;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Session.RegisterNative("notify", ENativeType::Void, {},
		        [](const FNativeCallContext& Call, FValue&, std::string&)
		{
			++static_cast<FState*>(Call.UserData)->Destroyed;
			return true;
		}, &State) ||
		    !Session.RegisterNative("release", ENativeType::Void, {},
		        [](const FNativeCallContext& Call, FValue&, std::string&)
		{
			static_cast<FState*>(Call.UserData)->Root.Reset();
			return true;
		}, &State))
		{
			return false;
		}
		const std::string Source =
		    "class Child{public:int value=7;~Child(){notify();" + std::string(Failure == 2 ? "assert(false);" : "") + "}};fn()->int make(){Child child=Child();return [child]()->int{return child.value;};}"
		                                                                                                              "class Counter{public:int value=1;default{release();value=42;" +
		    std::string(Failure == 1 ? "assert(false);" : "") + "}}fn()->int hold(Counter counter){return [counter]()->int{return counter.value;};}void main(){}";
		FObjectHandle Object;
		FValue Value;
		std::string Error;
		if (!Session.LoadSource(Source, "defaults-value-cleanup.vs").bSuccess || !Session.ConstructObject("Counter", {}, Object).bSuccess || !(Failure == 3 ? Session.CallFunction("hold", {Object}, &Value) : Session.CallFunction("make", {}, &Value)).bSuccess || !Session.RetainValue(Value, State.Root, Error))
		{
			return false;
		}
		Value = {};
		if (Failure == 3)
		{
			State.Root.Reset();
		}
		const auto Applied = Session.ApplyClassDefaults(Object);
		// Check before another Session operation can hide a missing safe point.
		const int ExpectedDestroyed = Failure == 3 ? 0 : 1;
		if (Applied.bSuccess != (Failure == 0 || Failure == 3) || State.Destroyed != ExpectedDestroyed || State.Root.IsValid() || ((Failure == 1 || Failure == 2) && Applied.Error.Message.empty()) || (Failure == 2 && Applied.Error.Location.FileLabel != "<value-cleanup>"))
		{
			std::cerr << "class defaults value cleanup failed (kind=" << Failure
			          << ", destroyed=" << State.Destroyed << ")\n"
			          << Errors.str();
			return false;
		}
		if (!Session.GetField(Object, "value", Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 42)
		{
			return false;
		}
		Session.Clear();
		if (Session.IsLoaded() || State.Destroyed != ExpectedDestroyed) return false;
	}
	return true;
}

static bool TestFailedCommitPreservesModuleOwnership()
{
	using namespace VeroScript;
	for (const bool PackageLoad : {false, true})
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		int CleanupCalls = 0;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.RegisterNative("fail_commit_cleanup", ENativeType::Void, {},
		        [](const FNativeCallContext& Call, FValue&, std::string& Error)
		{
			++*static_cast<int*>(Call.UserData);
			Error = "expected generation cleanup failure";
			return false;
		}, &CleanupCalls) ||
		    !Session.LoadSource("class FailingCommitCleanup { public: "
		                        "~FailingCommitCleanup(){fail_commit_cleanup();} } void main(){}",
		                "failed-commit-owner.vs")
		        .bSuccess)
		{
			return false;
		}
		FObjectHandle Object;
		if (!Session.ConstructObject("FailingCommitCleanup", {}, Object).bSuccess)
		{
			return false;
		}
		// A generation-owned root passes the ordinary external-borrow check, but
		// invalidation at commit must run its destructor before replacing code.
		FValueRoot Reservation, GenerationRoot;
		std::string Error;
		if (!Session.ReserveValueResult(Reservation, Error)) return false;
		auto Roots = Reservation.Owner();
		Reservation.Reset();
		if (!Roots->RetainForGeneration(Object, GenerationRoot, Error)) return false;
		Object.reset();
		auto Replacement = VeroScriptCompile::CompileSource("void main(){}", "replacement.vs");
		if (!Replacement.bSuccess || !Replacement.Module) return false;
		const auto* Previous = Session.GetLoadedModule();
		const auto* Candidate = Replacement.Module.get();
		FPackageData Package;
		Package.Kind = EPackageKind::Application;
		if (PackageLoad)
		{
			Package.Module = std::move(Replacement.Module);
		}
		const auto Result = PackageLoad ? Session.LoadPackage(Package, "replacement.vsapp")
		                                : Session.CommitCompiledFile(Replacement.Module, "replacement.vs");
		const auto* Retained = PackageLoad ? Package.Module.get() : Replacement.Module.get();
		if (Result.bSuccess || CleanupCalls == 0 || Retained != Candidate || Session.GetLoadedModule() != Previous)
		{
			std::cerr << "failed commit consumed caller-owned module (package=" << PackageLoad << ")\n"
			          << Errors.str();
			return false;
		}
	}
	return true;
}

static bool TestVariantObjectReferences()
{
	using namespace VeroScript;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	FHostTypeId HostType;
	if (!Session.RegisterHostType("VariantReferenceHost", EHostTypeKind::Class, &HostType) || !Session.LoadSource("class VariantReferenceHolder { public: variant first; variant second; variant host; } "
	                                                                                                              "class VariantReferenceNode { public: variant back; variant nested; }",
	                                                                                                      "variant-references.vs")
	                                                                                              .bSuccess)
	{
		return false;
	}
	FObjectHandle Holder, Node;
	if (!Session.ConstructObject("VariantReferenceHolder", {}, Holder).bSuccess || !Session.ConstructObject("VariantReferenceNode", {}, Node).bSuccess)
	{
		return false;
	}
	const auto HostObject = MakeHostObject(HostType.Value, "VariantReferenceHost", nullptr);
	const auto NestedHostObject = MakeHostObject(HostType.Value, "VariantReferenceHost", nullptr);
	FValue BoxedNode, BoxedHolder, BoxedHost, BoxedNestedHost;
	std::string Error;
	if (!Session.MakeVariant("VariantReferenceNode", Node, BoxedNode, Error) || !Session.MakeVariant("VariantReferenceHolder", Holder, BoxedHolder, Error) || !Session.MakeVariant("VariantReferenceHost", HostObject, BoxedHost, Error) || !Session.MakeVariant("VariantReferenceHost", NestedHostObject, BoxedNestedHost, Error) || !Session.SetField(Holder, "first", BoxedNode).bSuccess || !Session.SetField(Holder, "second", BoxedNode).bSuccess || !Session.SetField(Holder, "host", BoxedHost).bSuccess || !Session.SetField(Node, "nested", BoxedNestedHost).bSuccess || !Session.SetField(Node, "back", BoxedHolder).bSuccess)
	{
		std::cerr << "variant reference setup failed: " << Error << '\n'
		          << Errors.str();
		return false;
	}
	int ScriptReferences = 0, HostReferences = 0;
	std::vector<const void*> Order;
	const bool Enumerated = Session.EnumerateObjectReferences(Holder, [&](const FExternalObjectReference& Reference)
	{
		Order.push_back(Reference.Kind == EExternalReferenceKind::ScriptObject
		        ? static_cast<const void*>(Reference.ScriptObject.get())
		        : Reference.HostObject.get());
		if (Reference.Kind == EExternalReferenceKind::ScriptObject && Reference.ScriptObject == Node)
		{
			++ScriptReferences;
		}
		if (Reference.Kind == EExternalReferenceKind::HostObject && Reference.HostObject == HostObject)
		{
			++HostReferences;
		}
	}, Error);
	// Break the deliberately constructed cycle even when enumeration fails.
	const bool Unlinked = Session.SetField(Node, "back", BoxedHost).bSuccess;
	const std::vector<const void*> Expected{Node.get(), NestedHostObject.get(), HostObject.get()};
	if (!Enumerated || !Unlinked || ScriptReferences != 1 || HostReferences != 1 || Order != Expected)
	{
		std::cerr << "variant payload references were missed or repeated: " << ScriptReferences << ", "
		          << HostReferences << ", " << Error << '\n';
		return false;
	}
	return true;
}

static bool TestDeepObjectReferences()
{
	using namespace VeroScript;
	constexpr int Depth = 12000;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	int Destroyed = 0;
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	if (!Session.RegisterNative("count_reference_destruction", ENativeType::Void, {},
	        [](const FNativeCallContext& Call, FValue&, std::string&)
	{
		++*static_cast<int*>(Call.UserData);
		return true;
	}, &Destroyed))
	{
		return false;
	}
	const auto Source = std::string("class DeepReferenceNode { public: int number=0; DeepReferenceNode next=nullptr; ") + "~DeepReferenceNode(){count_reference_destruction();} } DeepReferenceNode root=nullptr; " + "void main(){for(int i=0;i<" + std::to_string(Depth) + ";i++){DeepReferenceNode node=DeepReferenceNode();node.number=i;node.next=root;root=node;}}";
	if (!Session.LoadSource(Source, "deep-object-references.vs").bSuccess || !Session.Run().bSuccess)
	{
		std::cerr << "deep reference graph construction failed\n"
		          << Errors.str();
		return false;
	}
	FValue Root;
	if (!Session.GetGlobal("root", Root).bSuccess || !std::holds_alternative<FObjectHandle>(Root)) return false;
	int ExpectedNumber = Depth - 2;
	bool InOrder = true;
	std::string Error;
	const bool Enumerated = Session.EnumerateObjectReferences(std::get<FObjectHandle>(Root),
	    [&](const FExternalObjectReference& Reference)
	{
		const auto Object = Reference.ScriptObject;
		const auto* Number = Object && !Object->Fields.empty() ? std::get_if<std::int32_t>(&Object->Fields[0]) : nullptr;
		if (!Number || *Number != ExpectedNumber)
		{
			InOrder = false;
		}
		--ExpectedNumber;
	}, Error);
	// Drop the borrowed copy first; Clear then owns normal language cleanup.
	Root = {};
	Session.Clear();
	if (!Enumerated || !InOrder || ExpectedNumber != -1 || Session.IsLoaded() || Destroyed != Depth)
	{
		std::cerr << "deep reference enumeration/cleanup failed: " << Error << ", remaining="
		          << ExpectedNumber << ", destroyed=" << Destroyed << '\n'
		          << Errors.str();
		return false;
	}
	return true;
}

static bool TestIndexedObjectField()
{
	using namespace VeroScript;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	// A runtime binding named shared must win over the soft shared<T> type.
	const char* Source = "class IndexedChild { public: int value=42; } "
	                     "array<IndexedChild> shared={IndexedChild()}; int read(){return shared[0].value;}";
	FValue Answer;
	if (!Session.LoadSource(Source, "indexed-object-field.vs").bSuccess || !Session.CallFunction("read", {}, &Answer).bSuccess || !std::holds_alternative<std::int32_t>(Answer) || std::get<std::int32_t>(Answer) != 42)
	{
		std::cerr << "indexed object field did not return 42\n"
		          << Errors.str();
		return false;
	}
	return true;
}

template <typename TMap>
static bool TestReentrantMapDiscard()
{
	using namespace VeroScript;
	std::vector<int> ReleasedTables;
	bool bDiscarding = false;
	bool bValid = true;
	auto Map = std::make_shared<TMap>();
	const auto Prepare = [&](auto& Entries, auto FirstKey, auto SecondKey, const int Table)
	{
		Entries.emplace(std::move(FirstKey), std::int32_t{11});
		Entries.emplace(std::move(SecondKey), std::int32_t{22});
		const auto Current = Entries.begin();
		const auto CurrentKey = Current->first;
		const auto LaterKey = std::next(Current)->first;
		auto HostObject = std::make_shared<FHostObject>();
		HostObject->TypeName = "ReentrantCleanupHost";
		HostObject->PayloadOwner = std::shared_ptr<void>(nullptr,
		    [&, CurrentKey, LaterKey, Table](void*)
		{
			if (!bDiscarding) return;
			ReleasedTables.push_back(Table);
			const auto Assigned = Entries.find(CurrentKey);
			bValid = bValid && Assigned != Entries.end() && std::holds_alternative<std::int32_t>(Assigned->second) && std::get<std::int32_t>(Assigned->second) == 0;
			// Erasing another node leaves the range-for's current iterator valid.
			// Cleanup must not have queued a pointer to that erased node.
			bValid = Entries.erase(LaterKey) == 1 && bValid;
		});
		Current->second = std::move(HostObject);
	};
	Prepare(Map->IntEntries, std::int32_t{1}, std::int32_t{2}, 0);
	Prepare(Map->UIntEntries, std::uint32_t{1}, std::uint32_t{2}, 1);
	Prepare(Map->Int64Entries, std::int64_t{1}, std::int64_t{2}, 2);
	Prepare(Map->UInt64Entries, std::uint64_t{1}, std::uint64_t{2}, 3);
	Prepare(Map->StrEntries, std::string("first"), std::string("second"), 4);
	Prepare(Map->WStrEntries, std::u16string(u"first"), std::u16string(u"second"), 5);
	Prepare(Map->EnumEntries, FEnumValue{"CleanupKey", 1}, FEnumValue{"CleanupKey", 2}, 6);
	Prepare(Map->HostEntries, std::make_shared<FHostObject>(), std::make_shared<FHostObject>(), 7);
	// Keep the inner cursor suspended beneath an outer cursor as well.
	auto Parent = std::make_shared<TMap>();
	Parent->IntEntries.emplace(0, Map);
	std::vector<FValue> Roots{Parent};
	FBytecodeModule Module;
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	bDiscarding = true;
	const bool bReleased = Vm.DiscardExternalValues(Module, Roots, {});
	bDiscarding = false;
	if (!bReleased || !bValid || ReleasedTables != std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7} || !Roots.empty() || !Parent->IntEntries.empty() || !Map->IntEntries.empty() || !Map->UIntEntries.empty() || !Map->Int64Entries.empty() || !Map->UInt64Entries.empty() || !Map->StrEntries.empty() || !Map->WStrEntries.empty() || !Map->EnumEntries.empty() || !Map->HostEntries.empty())
	{
		std::cerr << "reentrant map discard invalidated a later entry or changed release order\n";
		return false;
	}
	return true;
}

static bool TestReentrantMapCleanup()
{
	return TestReentrantMapDiscard<VeroScript::FMap>() && TestReentrantMapDiscard<VeroScript::FOrderedMap>();
}

static bool TestLocalStaticValueOperations()
{
	using namespace VeroScript;
	for (const bool Initialized : {false, true})
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const char* Source =
		    "int next(){static int count=40;return ++count;}"
		    "class Token{public:~Token(){print(next());}}"
		    "class Holder{public:Token value=Token();}"
		    "Token global=Token();Holder holder=Holder();void main(){}";
		FValue Value;
		if (!Session.LoadSource(Source, "local-static-value-operations.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess || (Initialized && !Session.CallFunction("next", {}, &Value).bSuccess) || !Session.SetGlobal("global", FObjectHandle{}).bSuccess || !Session.GetGlobal("holder", Value).bSuccess || !Session.SetField(std::get<FObjectHandle>(Value), "value", FObjectHandle{}).bSuccess)
		{
			std::cerr << "local static state was unavailable to value hooks\n"
			          << Errors.str();
			return false;
		}
		Value = {};
		if (!Session.CallFunction("next", {}, &Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != (Initialized ? 44 : 43) || Output.str() != (Initialized ? "42\n43\n" : "41\n42\n"))
		{
			std::cerr << "value hooks did not share the Session local static state: " << Output.str() << Errors.str();
			return false;
		}
	}
	return true;
}

static bool TestLocalStaticRetiredRoots(const bool CoreVmOnly = false)
{
	using namespace VeroScript;
	for (int Mode = 0; Mode != 4; ++Mode)
	{
		const bool CoreVm = (Mode & 1) != 0;
		if (CoreVmOnly && !CoreVm)
		{
			continue;
		}
		const bool Initialized = (Mode & 2) != 0;
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		FValueRoot Root;
		VeroScriptSession Session(Host);
		Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Session.RegisterNative("keep", FNativeTypeDesc::Scalar(ENativeType::Void),
		        {{FNativeTypeDesc::Function("fn()->int"), 0}},
		        [](const FNativeCallContext& Call, FValue&, std::string& Error)
		{
			return Call.ArgCount == 1 && Call.ScriptInvoker && Call.ScriptInvoker->RetainForGeneration(Call.Args[0], *static_cast<FValueRoot*>(Call.UserData), Error);
		}, &Root) ||
		    !Session.RegisterNative("drop", ENativeType::Void, {},
		        [](const FNativeCallContext& Call, FValue&, std::string&)
		{
			static_cast<FValueRoot*>(Call.UserData)->Reset();
			return true;
		}, &Root))
		{
			return false;
		}
		const std::string Source = std::string(
		                               "int next(){static int count=40;return ++count;}"
		                               "class Target{public:int value=7;} Target live=Target();"
		                               "class Token{public:weak<Target> target;Token(){target=weak<Target>(live);}"
		                               "~Token(){assert(!target.expired());print(next());}}"
		                               "fn()->int make(){Token token=Token();return [token]()->int{return 1;};}") +
		    (Initialized ? "void main(){print(next());keep(make());drop();print(next());}"
		                 : "void main(){keep(make());drop();print(next());}");
		bool Succeeded = false;
		if (CoreVm)
		{
			auto Options = Test::MakeCompileOptions();
			Options.Bindings = &Session.GetBindingCatalog();
			auto Compiled = VeroScriptCompile::CompileSource(Source, "local-static-retired-roots.vs", Options);
			if (!Compiled.bSuccess) return false;
			std::istringstream Input;
			VeroScriptVm Vm(Output, Input, Errors);
			Test::Configure(Vm);
			Succeeded = Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Session.GetBindingCatalog().GetNativeFunctions());
		}
		else
		{
			Succeeded = Session.LoadSource(Source, "local-static-retired-roots.vs").bSuccess && Session.Run().bSuccess;
		}
		if (!Succeeded || Root.IsValid() || Output.str() != (Initialized ? "41\n42\n43\n" : "41\n42\n"))
		{
			std::cerr << "retired roots did not share active local static state (Core VM=" << CoreVm
			          << "): " << Output.str() << Errors.str();
			return false;
		}
	}
	return true;
}

static bool TestLocalStaticVisibility()
{
	using namespace VeroScript;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	VeroScriptSession Session(Host);
	Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	const char* Source = "int next(){static int count=40;return ++count;} int visible=7;void main(){}";
	if (!Session.LoadSource(Source, "local-static-visibility.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
	{
		return false;
	}
	const auto* Module = Session.GetLoadedModule();
	std::string HiddenName;
	for (std::size_t Index = 0; Index < Module->GlobalMetadata.size(); ++Index)
	{
		if (Module->GlobalMetadata[Index].bLocalStatic)
		{
			HiddenName = Module->GlobalNames[Index];
		}
	}
	if (HiddenName.empty()) return false;
	for (const bool Initialized : {false, true})
	{
		FValue Value;
		if (Initialized && !Session.CallFunction("next", {}, &Value).bSuccess)
		{
			return false;
		}
		if (Session.GetGlobal(HiddenName, Value).bSuccess || Session.SetGlobal(HiddenName, std::int32_t{99}).bSuccess)
		{
			std::cerr << "local static storage leaked through the Session global API\n";
			return false;
		}
	}
	FValue Value;
	return Session.GetGlobal("visible", Value).bSuccess && std::get<std::int32_t>(Value) == 7 && Session.CallFunction("next", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 42;
}

static bool TestLocalStaticCallableCleanup()
{
	using namespace VeroScript;
	for (const bool Clear : {false, true})
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const char* Source =
		    "int next(){static int count=40;return ++count;}"
		    "class Token{public:~Token(){print(next());}}"
		    "fn()->int make(){Token token=Token();return [token]()->int{return 1;};}";
		FValue Value;
		if (!Session.LoadSource(Source, "local-static-callable-cleanup.vs").bSuccess || !Session.CallFunction("next", {}, &Value).bSuccess || !Session.CallFunction("make", {}, &Value).bSuccess)
		{
			return false;
		}
		const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(Value));
		Value = {};
		if (!Callable) return false;
		if (Clear)
		{
			Session.Clear();
		}
		else if (!Session.ReleaseCallable(Callable))
		{
			return false;
		}
		if (Output.str() != "42\n" || !Errors.str().empty() || Session.IsCallableValid(Callable))
		{
			std::cerr << "retained callable cleanup lost local statics (clear=" << Clear << "): "
			          << Output.str() << Errors.str();
			return false;
		}
	}
	return true;
}

static bool TestLocalStaticVariantCopy()
{
	using namespace VeroScript;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	VeroScriptSession Session(Host);
	Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	const char* Source =
	    "int next(){static int count=40;return ++count;}"
	    "struct Item{int value=1;Item(){}Item(const Item& other){value=other.value;print(next());}}"
	    "Item item=Item();void main(){}";
	FValue Item, Value, Box;
	std::string Error;
	if (!Session.LoadSource(Source, "local-static-variant-copy.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.GetGlobal("item", Item).bSuccess || !Session.CallFunction("next", {}, &Value).bSuccess)
	{
		return false;
	}
	const auto Before = std::get<std::int32_t>(Value);
	Output.str({});
	if (!Session.MakeVariant("Item", Item, Box, Error))
	{
		std::cerr << "variant copy lost local static state: " << Error << '\n'
		          << Errors.str();
		return false;
	}
	FValueRoot Root;
	if (!std::holds_alternative<FScriptVariantHandle>(Box) || !Session.RetainValue(Box, Root, Error) || !Session.CallFunction("next", {}, &Value).bSuccess || std::get<std::int32_t>(Value) != Before + 2 || Output.str() != std::to_string(Before + 1) + '\n')
	{
		return false;
	}
	Root.Reset();
	Box = {};
	Item = {};
	Session.Clear();
	return Errors.str().empty();
}

static bool TestEntryConfigurationAndNativeBindings()
{
	using namespace VeroScript;
	struct FRestoreDepth
	{
		std::optional<std::string> Previous = Environment::GetUtf8("VERO_MAX_CALL_DEPTH");
		~FRestoreDepth()
		{
			if (Previous)
			{
				(void)Environment::SetUtf8("VERO_MAX_CALL_DEPTH", *Previous);
			}
			else
			{
				(void)Environment::UnsetUtf8("VERO_MAX_CALL_DEPTH");
			}
		}
	} Restore;
	std::ostringstream Output, Errors;
	std::istringstream Input;
	FStreamEngineHost Host(Output, Errors);
	if (!GetHostPlatformInfo().bMobile)
	{
		if (!Environment::SetUtf8("VERO_MAX_CALL_DEPTH", "11")) return false;
		VeroScriptVm PublicVm(Output);
		if (PublicVm.GetMaxCallDepth() != 11) return false;
		if (!Environment::SetUtf8("VERO_MAX_CALL_DEPTH", "19")) return false;
		VeroScriptVm StreamVm(Output, Input, Errors);
		if (StreamVm.GetMaxCallDepth() != 19) return false;
		PublicVm.SetMaxCallDepth(0);
		if (PublicVm.GetMaxCallDepth() != 19) return false;
		VeroScriptSession Session(Host);
		Test::Configure(Session);
		if (!Session.LoadSource("int recursive(int n){if(n==0)return 7;return recursive(n-1)+1;}", "entry-depth.vs").bSuccess) return false;
		if (!Environment::SetUtf8("VERO_MAX_CALL_DEPTH", "4")) return false;
		FValue Value;
		if (Session.CallFunction("recursive", {std::int32_t{8}}, &Value).bSuccess || Session.RequiresReset()) return false;
		if (!Environment::SetUtf8("VERO_MAX_CALL_DEPTH", "64") ||
			!Session.CallFunction("recursive", {std::int32_t{8}}, &Value).bSuccess || std::get<std::int32_t>(Value) != 15)
		{
			return false;
		}
		Session.SetMaxCallDepth(3);
		if (Session.CallFunction("recursive", {std::int32_t{8}}, &Value).bSuccess) return false;
		Session.SetMaxCallDepth(0);
		if (!Session.CallFunction("recursive", {std::int32_t{8}}, &Value).bSuccess || std::get<std::int32_t>(Value) != 15) return false;
	}
	for (const bool Persistent : {false, true})
	{
		struct FState
		{
			VeroScriptSession* Session = nullptr;
			int Value = 10;
			int Calls = 0;
		} State;
		VeroScriptSession Session(Host);
		State.Session = &Session;
		Test::Configure(Session);
		Session.SetHotReloadEnabled(Persistent);
		if (!Session.RegisterNative("entry_native_value", ENativeType::Int, {}, [](const FNativeCallContext &Context, FValue &Return, std::string &Error)
				{
					auto &State = *static_cast<FState *>(Context.UserData);
					++State.Calls;
					std::vector<FValue> Args{std::int32_t{31}};
					FValue Inner;
					if (!Context.ScriptInvoker || !Context.ScriptInvoker->InvokeFunction("inner", Args, Inner, Error)) return false;
					FValue Reentered;
					if (State.Session->CallFunction("inner", Args, &Reentered).bSuccess)
					{
						Error = "Session allowed public reentry during native execution";
						return false;
					}
					Return = std::get<std::int32_t>(Inner) + State.Value;
					return true;
				},
				&State) ||
			!Session.LoadSource("int inner(int n){return n+1;}int work(){return entry_native_value();}", "entry-binding.vs").bSuccess)
		{
			return false;
		}
		FValue Value;
		for (int Index = 0; Index != 2; ++Index)
		{
			if (!Session.CallFunction("work", {}, &Value).bSuccess || std::get<std::int32_t>(Value) != 42) return false;
		}
		State.Value = 11;
		if (!Session.CallFunction("work", {}, &Value).bSuccess || std::get<std::int32_t>(Value) != 43 || State.Calls != 3) return false;
		const auto PreviousRevision = Session.GetBindingCatalog().GetRevision();
		if (!Session.RegisterNative("entry_second_value", ENativeType::Int, {},
				[](const FNativeCallContext &, FValue &Return, std::string &)
				{
					Return = std::int32_t{64};
					return true;
				}) ||
			Session.GetBindingCatalog().GetRevision() == PreviousRevision ||
			!Session.CallFunction("work", {}, &Value).bSuccess || std::get<std::int32_t>(Value) != 43)
		{
			return false;
		}
		FCompileOptions Options = Test::MakeCompileOptions();
		Options.Bindings = &Session.GetBindingCatalog();
		auto Replacement = VeroScriptCompile::CompileSource("int work(){return entry_second_value();}", "entry-binding.vs", Options);
		if (!Replacement.bSuccess || !Session.ReloadCompiledModule(Replacement.Module, "entry-binding.vs").bSuccess ||
			!Session.CallFunction("work", {}, &Value).bSuccess || std::get<std::int32_t>(Value) != 64)
		{
			return false;
		}
		Session.Clear();
	}
	return true;
}

int main(const int ArgumentCount, char** Arguments)
{
	if (ArgumentCount == 2)
	{
		const std::string Selected = Arguments[1];
		if (Selected == "--entry-configuration-only") return TestEntryConfigurationAndNativeBindings() ? 0 : 1;
		if (Selected == "--journal-only") return TestFailedInitializationPreservesJournalValues() ? 0 : 1;
		if (Selected == "--failed-exports-only") return TestFailedInitializationExports() ? 0 : 1;
		if (Selected == "--weak-only") return TestCleanupPreservesWeakReferences() ? 0 : 1;
		if (Selected == "--reload-context-only") return TestReloadRetirementUsesCurrentContext() ? 0 : 1;
		if (Selected == "--defaults-only") return TestClassDefaultsValueSafePoint() ? 0 : 1;
		if (Selected == "--indexed-field-only") return TestIndexedObjectField() ? 0 : 1;
		if (Selected == "--commit-ownership-only") return TestFailedCommitPreservesModuleOwnership() ? 0 : 1;
		if (Selected == "--variant-references-only") return TestVariantObjectReferences() ? 0 : 1;
		if (Selected == "--deep-references-only") return TestDeepObjectReferences() ? 0 : 1;
		if (Selected == "--reentrant-map-cleanup-only") return TestReentrantMapCleanup() ? 0 : 1;
		if (Selected == "--local-static-values-only") return TestLocalStaticValueOperations() ? 0 : 1;
		if (Selected == "--local-static-roots-only") return TestLocalStaticRetiredRoots() ? 0 : 1;
		if (Selected == "--local-static-core-roots-only") return TestLocalStaticRetiredRoots(true) ? 0 : 1;
		if (Selected == "--local-static-visibility-only") return TestLocalStaticVisibility() ? 0 : 1;
		if (Selected == "--local-static-callables-only") return TestLocalStaticCallableCleanup() ? 0 : 1;
		if (Selected == "--local-static-variant-only") return TestLocalStaticVariantCopy() ? 0 : 1;
	}
	const auto Check = [](const char* Name, const bool Passed)
	{
		if (!Passed)
		{
			std::cerr << Name << " failed\n";
		}
		return Passed;
	};
	if (!Check("Entry configuration and native binding cache", TestEntryConfigurationAndNativeBindings())) return 1;
	if (!Check("Aliased session outputs", TestAliasedSessionOutputs()) || !Check("Retained callable ownership", TestRetainedCallableOwnership()) || !Check("Retained callable reload", TestRetainedCallableReload()) || !Check("Pending reload callable cleanup", TestPendingReloadCallableCleanup()) || !Check("Failed initialization exports", TestFailedInitializationExports()) || !Check("Rollback journal values", TestFailedInitializationPreservesJournalValues()) || !Check("Cleanup weak references", TestCleanupPreservesWeakReferences()) || !Check("Reload retirement context", TestReloadRetirementUsesCurrentContext()) || !Check("Class defaults value cleanup", TestClassDefaultsValueSafePoint()) || !Check("Failed commit module ownership", TestFailedCommitPreservesModuleOwnership()) || !Check("Variant object references", TestVariantObjectReferences()) || !Check("Deep object references", TestDeepObjectReferences()) || !Check("Reentrant map cleanup", TestReentrantMapCleanup()) || !Check("Local static value operations", TestLocalStaticValueOperations()) || !Check("Local static retired roots", TestLocalStaticRetiredRoots()) || !Check("Local static visibility", TestLocalStaticVisibility()) || !Check("Local static callable cleanup", TestLocalStaticCallableCleanup()) || !Check("Local static variant copy", TestLocalStaticVariantCopy()) || !Check("Indexed object field", TestIndexedObjectField()))
	{
		std::cerr << "C++ Session lifetime regression failed\n";
		return 1;
	}
	return 0;
}
