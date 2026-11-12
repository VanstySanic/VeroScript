// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptSessionDebug.h" // Must also compile without Session.h first.
#include "VeroScriptSession.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptCompile.h"
#include "VeroScriptDebugger.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{
	using namespace VeroScript;

	void Require(bool Condition, const char* Message)
	{
		if (!Condition)
		{
			throw std::runtime_error(Message);
		}
	}

	class FProbeBridge final : public ISessionDebugBridge
	{
	public:
		//~ Begin ISessionDebugBridge Interface
		void ProgramChanged(const FSessionDebugProgramView& Program) noexcept override
		{
			++Changes;
			Generation = Program.Generation;
			bLoaded = Program.Module != nullptr;
			bValid &= !bExecuting && Program.Bindings != nullptr && Program.SourceOverlays != nullptr;
			bValid &= std::this_thread::get_id() == OwnerThread;
		}
		std::shared_ptr<VeroScriptDebugger> BeginExecution(
			const FSessionDebugExecutionContext& Context) noexcept override
		{
			++Begins;
			bValid &= !bExecuting && Context.Program.Module != nullptr;
			bValid &= Context.Program.Generation == Generation && !Context.Program.Owner.expired();
			bValid &= Context.ThreadId == OwnerThread && std::this_thread::get_id() == OwnerThread;
			bExecuting = true;
			Kind = Context.Kind;
			Object = Context.Object;
			if (!bConnected) return nullptr;
			Debugger->SetRunMode(EDebugRunMode::StepIn);
			return Debugger;
		}
		void EndExecution() noexcept override
		{
			++Ends;
			bValid &= bExecuting && Debugger->GetStackTrace().empty();
			bExecuting = false;
			Object.reset();
		}
		void Detached() noexcept override
		{
			++Detaches;
			bValid &= !bExecuting && std::this_thread::get_id() == OwnerThread;
			bConnected = false;
		}
		//~ End ISessionDebugBridge Interface
		std::shared_ptr<VeroScriptDebugger> Debugger = std::make_shared<VeroScriptDebugger>();
		std::thread::id OwnerThread = std::this_thread::get_id();
		ESessionDebugExecutionKind Kind = ESessionDebugExecutionKind::Run;
		FObjectHandle Object;
		std::uint64_t Generation = 0;
		int Changes = 0, Begins = 0, Ends = 0, Detaches = 0;
		bool bLoaded = false, bConnected = true, bExecuting = false, bValid = true;
	};

	void TestExistingSession()
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		auto Bridge = std::make_shared<FProbeBridge>();
		{
			VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
			Session.SetExecutionBackend(EExecutionBackend::Jit);
			Require(Session.SetDebugBridge(Bridge), "install bridge");
			Require(Bridge->Changes == 1 && !Bridge->bLoaded, "idle installation must not run/load");
			const char* Source =
				"class Counter { public: int Value = 1; Counter() { Value = 2; }\n"
				"default { Value = 10; }\n"
				"int Add(int value) { Value = Value + value; return Value; } }\n"
				"int twice(int value) { return value * 2; }\n"
				"fn(int)->int callback() { return twice; }\n"
				"void main() { print(twice(3)); }\n";
			Require(Session.LoadSource(Source, "session-debug.vs").bSuccess, "load test source");
			Require(Bridge->Changes == 2 && Bridge->bLoaded, "publish loaded program");
			const auto Generation = Bridge->Generation;
			Require(!Session.LoadSource("void main( {", "invalid.vs").bSuccess, "bad load must fail");
			Require(Bridge->Changes == 2 && Bridge->Generation == Generation, "failed load preserves program");

			int Stops = 0;
			bool bEditArgument = false;
			FObjectHandle ExpectedObject;
			Bridge->Debugger->SetStopCallback([&](VeroScriptDebugger& Active, EDebugStopReason)
			{
				++Stops;
				Require(std::this_thread::get_id() == Bridge->OwnerThread, "stop on execution thread");
				Require(!Active.GetStackTrace().empty(), "inspect existing VM frames");
				if (Bridge->Kind == ESessionDebugExecutionKind::CallMethod
					|| Bridge->Kind == ESessionDebugExecutionKind::ApplyClassDefaults)
				{
					Require(Bridge->Object == ExpectedObject, "publish the actual executing object");
				}
				Require(!Session.SetDebugBridge(nullptr), "reject bridge removal during execution");
				if (bEditArgument)
				{
					Require(Active.Evaluate("value").bOk, "evaluate on existing call frame");
					Require(Active.SetLocal("value", "21").bOk, "edit existing call argument");
				}
				Active.SetRunMode(EDebugRunMode::Continue);
			});
			Require(Session.Run().bSuccess && Output.str() == "6\n", "debug existing Session.Run");
			Require(Bridge->Kind == ESessionDebugExecutionKind::Run, "run context kind");
			FObjectHandle Counter;
			Require(Session.ConstructObject("Counter", {}, Counter).bSuccess, "debug construction");
			ExpectedObject = Counter;
			Require(Bridge->Kind == ESessionDebugExecutionKind::ConstructObject, "constructor context kind");
			Require(Session.ApplyClassDefaults(Counter).bSuccess, "debug class defaults");
			Require(Bridge->Kind == ESessionDebugExecutionKind::ApplyClassDefaults, "defaults context kind");
			FValue Result;
			Require(Session.CallMethod(Counter, "Add", {std::int32_t{5}}, &Result).bSuccess,
				"debug existing object method");
			Require(std::get<std::int32_t>(Result) == 15, "existing object state must change");
			Require(Bridge->Kind == ESessionDebugExecutionKind::CallMethod, "method context kind");
			bEditArgument = true;
			Require(Session.CallFunction("twice", {std::int32_t{3}}, &Result).bSuccess,
				"debug free function");
			Require(std::get<std::int32_t>(Result) == 42, "debugger edits must affect real execution");
			Require(Session.GetLastJitStats().CompileAttempts == 0, "debug calls must bypass JIT preparation");
			bEditArgument = false;
			Require(Session.CallFunction("callback", {}, &Result).bSuccess, "create retained callback");
			const auto Handle = Session.RetainCallable(std::get<FFunctionHandle>(Result));
			Require(static_cast<bool>(Handle), "retain callback");
			Require(Session.InvokeCallable(Handle, {std::int32_t{8}}, &Result).bSuccess,
				"debug callback after its originating call ends");
			Require(Bridge->Kind == ESessionDebugExecutionKind::InvokeCallable
				&& std::get<std::int32_t>(Result) == 16, "retained callback context and result");
			Require(Stops == Bridge->Begins && Bridge->Begins == Bridge->Ends, "balanced debug calls");
			Require(Session.ReleaseCallable(Handle), "release retained callback");
			const int Begins = Bridge->Begins;
			Session.Clear();
			Require(Bridge->Begins == Begins && !Bridge->bLoaded && Bridge->Generation != Generation,
				"Clear invalidates program without debugging cleanup");
			Require(Session.SetDebugBridge(Bridge) && Bridge->Detaches == 0, "same bridge install is idempotent");
			Require(Bridge->bValid, "bridge invariants");
		}
		Require(Bridge->Detaches == 1 && Bridge->bValid, "destroy session detaches exactly once");

		VeroScriptSession Replacement(Host);
		VeroScript::Test::Configure(Replacement);
		auto First = std::make_shared<FProbeBridge>();
		auto Second = std::make_shared<FProbeBridge>();
		Require(Replacement.SetDebugBridge(First) && Replacement.SetDebugBridge(Second), "replace bridge");
		Require(First->Detaches == 1 && Second->Changes == 1, "replacement lifecycle");
		Require(Replacement.SetDebugBridge(nullptr) && Second->Detaches == 1, "unregister bridge");
	}

	void TestFailureAndRollback()
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		auto Bridge = std::make_shared<FProbeBridge>();
		Session.SetDebugBridge(Bridge);
		Session.SetHotReloadEnabled(true);
		Require(Session.LoadSource("int kept = 7; int main() { return kept; }", "good.vs").bSuccess,
			"load rollback baseline");
		Require(Session.Run().bSuccess, "initialize baseline");
		const auto OriginalGeneration = Bridge->Generation;
		auto Bad = VeroScriptCompile::CompileSource(
			"int kept = 7; int divisor = 0; int failed = 1 / divisor; int main() { return kept; }",
			"good.vs");
		Require(Bad.bSuccess && Bad.Module != nullptr, "compile runtime init failure");
		const auto Reload = Session.ReloadCompiledModule(Bad.Module, "good.vs");
		if (!Reload.bSuccess)
		{
			std::cerr << Errors.str() << Reload.Error.Message << '\n';
		}
		Require(Reload.bSuccess, "stage reload");
		Require(Bridge->Generation != OriginalGeneration, "publish staged generation");
		Require(!Session.Run().bSuccess, "fail replacement initialization");
		Require(Bridge->Generation == OriginalGeneration && Bridge->bValid, "publish restored generation after End");
		Require(Session.Run().bSuccess && Bridge->Begins == Bridge->Ends, "debug old program after rollback");

		Require(Session.LoadSource("int main() { return 4; }", "empty-state.vs").bSuccess,
			"load a baseline without globals");
		Require(Session.Run().bSuccess, "initialize empty retained state");
		const auto EmptyGeneration = Bridge->Generation;
		auto EmptyBad = VeroScriptCompile::CompileSource(
			"int divisor = 0; int failed = 1 / divisor; int main() { return 100; }", "empty-state.vs");
		Require(EmptyBad.bSuccess && Session.ReloadCompiledModule(EmptyBad.Module, "empty-state.vs").bSuccess,
			"stage initialization after empty retained state");
		Require(!Session.Run().bSuccess && Bridge->Generation == EmptyGeneration && Bridge->bValid,
			"empty retained state must roll back failed initialization");
		const auto Restored = Session.Run();
		Require(Restored.bSuccess && Restored.ExitCode == 4, "empty-state rollback restores old program");

		Session.SetHotReloadEnabled(false);
		Require(Session.RegisterNative("host_throw", ENativeType::Void, {},
			[](const FNativeCallContext&, FValue&, std::string&) -> bool
			{
				throw std::runtime_error("native failure");
			}), "register throwing native");
		Require(Session.LoadSource("void main() { host_throw(); }", "throw.vs").bSuccess, "load throwing native");
		try
		{
			Require(!Session.Run().bSuccess, "native exception must not succeed");
		}
		catch (const std::runtime_error &Error)
		{
			Require(std::string_view(Error.what()) == "native failure", "unexpected native failure");
		}
		Require(Bridge->Begins == Bridge->Ends && Bridge->Debugger->GetStackTrace().empty(),
			"exception must detach dead VM and balance execution callbacks");
		Require(Session.SetDebugBridge(nullptr), "Session reusable after exception");
	}

	void TestStopCallbackException()
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		auto Bridge = std::make_shared<FProbeBridge>();
		Session.SetDebugBridge(Bridge);
		Require(Session.LoadSource("int twice(int value) { return value * 2; }", "stop-throw.vs").bSuccess,
			"load stop callback failure test");
		Bridge->Debugger->SetStopCallback([](VeroScriptDebugger&, EDebugStopReason)
		{
			throw std::runtime_error("stop callback failure");
		});
		try
		{
			Require(!Session.CallFunction("twice", {std::int32_t{3}}).bSuccess,
				"throwing stop callback must not succeed");
		}
		catch (const std::runtime_error& Error)
		{
			Require(std::string_view(Error.what()) == "stop callback failure", "unexpected callback failure");
		}
		Require(Bridge->Begins == 1 && Bridge->Ends == 1 && Bridge->bValid,
			"stop callback exception must detach and end the call");
		Bridge->Debugger->SetStopCallback({});
		FValue Value;
		Require(Session.CallFunction("twice", {std::int32_t{3}}, &Value).bSuccess
			&& std::get<std::int32_t>(Value) == 6, "call after throwing stop callback");
	}

	void TestDebuggerOwnership()
	{
		class FTransientBridge final : public ISessionDebugBridge
		{
		public:
			//~ Begin ISessionDebugBridge Interface
			void ProgramChanged(const FSessionDebugProgramView &) noexcept override
			{
			}
			std::shared_ptr<VeroScriptDebugger> BeginExecution(
				const FSessionDebugExecutionContext &) noexcept override
			{
				return Debugger;
			}
			void EndExecution() noexcept override
			{
				Debugger.reset();
				bRetainedThroughEnd = !WeakDebugger.expired();
			}
			void Detached() noexcept override
			{
			}
			//~ End ISessionDebugBridge Interface
			std::shared_ptr<VeroScriptDebugger> Debugger = std::make_shared<VeroScriptDebugger>();
			std::weak_ptr<VeroScriptDebugger> WeakDebugger = Debugger;
			bool bRetainedThroughEnd = false;
		};
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		auto Bridge = std::make_shared<FTransientBridge>();
		Session.SetDebugBridge(Bridge);
		Require(Session.LoadSource("int twice(int value) { return value * 2; }", "ownership.vs").bSuccess,
			"load ownership test");
		Require(Session.CallFunction("twice", {std::int32_t{3}}).bSuccess, "execute transient debugger");
		Require(Bridge->bRetainedThroughEnd && Bridge->WeakDebugger.expired(),
			"Session must retain the debugger through VM destruction without leaking it");
	}

	void TestTeardownDoesNotPause()
	{
		for (const bool bClear : {false, true})
		{
			std::ostringstream Output, Errors;
			FStreamEngineHost Host(Output, Errors);
			auto Bridge = std::make_shared<FProbeBridge>();
			struct FCleanupProbe
			{
				FProbeBridge* Bridge;
				bool bClear;
				int Calls = 0;
				bool bValid = true;
			} Probe{Bridge.get(), bClear};
			int CleanupStops = 0;
			{
				VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
				Session.SetHotReloadEnabled(true);
				Session.SetDebugBridge(Bridge);
				Require(Session.RegisterNative("observe_cleanup", ENativeType::Void, {},
					[](const FNativeCallContext& Context, FValue&, std::string&) -> bool
					{
						auto& State = *static_cast<FCleanupProbe*>(Context.UserData);
						++State.Calls;
						State.bValid &= !State.Bridge->bExecuting
							&& State.Bridge->Detaches == (State.bClear ? 0 : 1);
						return true;
					}, &Probe), "register cleanup observer");
				Require(Session.LoadSource(
					"class Trace { public: ~Trace() { observe_cleanup(); } }\n"
					"Trace root = Trace();\nvoid main() {}\n", "teardown.vs").bSuccess,
					"load persistent destructor test");
				Require(Session.Run().bSuccess, "initialize retained destructor test");
				Bridge->Debugger->SetBreakpoint("", 1);
				Bridge->Debugger->SetStopCallback([&](VeroScriptDebugger&, EDebugStopReason)
				{
					++CleanupStops;
				});
				if (bClear)
				{
					Session.Clear();
				}
			}
			Require(Probe.Calls == 1 && Probe.bValid && CleanupStops == 0
				&& Bridge->Begins == 1 && Bridge->Ends == 1 && Bridge->Detaches == 1,
				"cleanup must run destructors without entering debug callbacks");
		}
	}

	void TestCleanupReentry()
	{
		for (int Action = 0; Action != 5; ++Action)
		{
			std::ostringstream Output, Errors;
			FStreamEngineHost Host(Output, Errors);
			struct FProbe
			{
				VeroScriptSession* Session = nullptr;
				int Calls = 0;
				bool bGuarded = true;
				FFunctionHandle Candidate;
			} Probe;
			{
				VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
				Probe.Session = &Session;
				Session.SetHotReloadEnabled(true);
				Require(Session.RegisterNative("observe_cleanup", ENativeType::Void, {},
					[](const FNativeCallContext& Context, FValue&, std::string&) -> bool
					{
						auto& State = *static_cast<FProbe*>(Context.UserData);
						++State.Calls;
						// A harmless mutation proves the same guard that protects Clear,
						// Load and execution without destroying an active VM in this test.
						State.Session->SetExecutionBackend(EExecutionBackend::Vm);
						State.bGuarded &= State.Session->GetExecutionBackend() == EExecutionBackend::Mixed;
						if (State.bGuarded)
						{
							const auto Generation = State.Session->GetDebugProgram().Generation;
							State.Session->Clear();
							State.bGuarded &= State.Session->IsLoaded()
								&& State.Session->GetDebugProgram().Generation == Generation
								&& !State.Session->LoadSource("void main() {}", "reentrant.vs").bSuccess
								&& !State.Session->CallFunction("read_free").bSuccess
								&& !State.Session->RetainCallable(State.Candidate);
						}
						return true;
					}, &Probe), "register cleanup reentry probe");
				const char* Source =
					"int read_free() { return 7; } fn()->int candidate() { return read_free; }\n"
					"class Trace { public: ~Trace() { observe_cleanup(); }\n"
					"int read() { return 1; }\n"
					"fn()->int getter() { return [=]() -> int => read(); } }\n";
				if (Action == 3)
				{
					Require(Session.LoadSource(Source, "callable-cleanup.vs").bSuccess, "load callable cleanup");
					FValue Candidate;
					Require(Session.CallFunction("candidate", {}, &Candidate).bSuccess, "create cleanup retain candidate");
					Probe.Candidate = std::get<FFunctionHandle>(Candidate);
					FObjectHandle Object;
					FValue Value;
					Require(Session.ConstructObject("Trace", {}, Object).bSuccess, "construct captured cleanup object");
					Require(Session.CallMethod(Object, "getter", {}, &Value).bSuccess, "create captured cleanup callable");
					const auto Handle = Session.RetainCallable(std::get<FFunctionHandle>(Value));
					Require(static_cast<bool>(Handle), "retain cleanup callable");
					Value = {};
					Object.reset();
					Require(Session.ReleaseCallable(Handle), "release cleanup callable");
				}
				else
				{
					Require(Session.LoadSource(std::string(Source) + "Trace root = Trace(); void main() {}",
						"cleanup-reentry.vs").bSuccess, "load cleanup reentry fixture");
					Require(Session.Run().bSuccess, "initialize cleanup reentry fixture");
					FValue Candidate;
					Require(Session.CallFunction("candidate", {}, &Candidate).bSuccess, "create cleanup retain candidate");
					Probe.Candidate = std::get<FFunctionHandle>(Candidate);
					if (Action == 0)
					{
						Session.Clear();
					}
					if (Action == 1)
					{
						Session.SetHotReloadEnabled(false);
					}
					if (Action == 2)
					{
						Require(Session.LoadSource("int ping() { return 7; }", "replacement.vs").bSuccess,
							"replace cleanup fixture");
					}
					// Action 4 checks Session destruction.
				}
				if (Action != 4)
				{
					Require(Probe.Calls == 1 && Probe.bGuarded, "cleanup must reject mutating Session reentry");
					Require(Session.LoadSource("int ping() { return 7; }", "after-cleanup.vs").bSuccess,
						"cleanup guard must end before the next host operation");
					FValue Result;
					Require(Session.CallFunction("ping", {}, &Result).bSuccess && std::get<std::int32_t>(Result) == 7,
						"Session must remain usable after cleanup");
				}
			}
			Require(Probe.Calls == 1 && Probe.bGuarded, "destruction must reject mutating Session reentry");
		}
	}

	// A minimal host queue: the protocol thread never touches VM inspection APIs.
	void TestProtocolScheduling(bool bCancel)
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		auto Bridge = std::make_shared<FProbeBridge>();
		auto Control = std::make_shared<FExecutionControl>();
		Session.SetExecutionControl(Control);
		Session.SetDebugBridge(Bridge);
		Require(Session.LoadSource("int twice(int value) { return value * 2; }", "queue.vs").bSuccess,
			"load protocol test");
		std::mutex Mutex;
		std::condition_variable Changed;
		bool bStopped = false, bInspect = false, bInspected = false, bRelease = false;
		bool bTimedOut = false, bEvaluationOk = false;
		Bridge->Debugger->SetStopCallback([&](VeroScriptDebugger& Active, EDebugStopReason)
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			bStopped = true;
			Changed.notify_all();
			if (!Changed.wait_for(Lock, std::chrono::seconds(5), [&]
				{
					return bInspect;
				}))
			{
				bTimedOut = true;
				return;
			}
			bEvaluationOk = std::this_thread::get_id() == Bridge->OwnerThread
				&& Active.Evaluate("value").Value == "9";
			bInspected = true;
			Changed.notify_all();
			if (!Changed.wait_for(Lock, std::chrono::seconds(5), [&]
				{
					return bRelease;
				}))
			{
				bTimedOut = true;
				return;
			}
			Bridge->bConnected = false;
			Active.ClearBreakpoints();
			Active.ResetPauseRequest();
			Active.SetRunMode(EDebugRunMode::Continue);
		});
		std::thread Protocol([&]
		{
			std::unique_lock<std::mutex> Lock(Mutex);
			if (!Changed.wait_for(Lock, std::chrono::seconds(5), [&]
				{
					return bStopped;
				}))
			{
				bTimedOut = true;
				return;
			}
			bInspect = true;
			Changed.notify_all();
			if (!Changed.wait_for(Lock, std::chrono::seconds(5), [&]
				{
					return bInspected;
				}))
			{
				bTimedOut = true;
				return;
			}
			if (bCancel)
			{
				Control->RequestCancel();
			}
			bRelease = true; // Simulate disconnect releasing the paused execution.
			Changed.notify_all();
		});
		const auto Result = Session.CallFunction("twice", {std::int32_t{9}});
		Protocol.join();
		Require(!bTimedOut && bEvaluationOk, "marshal evaluation onto execution thread");
		Require(Result.bSuccess != bCancel, "disconnect preserves explicit cancellation policy");
		Require(Control->IsCancellationRequested() == bCancel, "disconnect and cancel are separate");
		const int Begins = Bridge->Begins;
		Control->Reset();
		FValue Value;
		Require(Session.CallFunction("twice", {std::int32_t{9}}, &Value).bSuccess
			&& std::get<std::int32_t>(Value) == 18, "host remains usable after disconnect");
		Require(Bridge->Begins == Begins + 1 && Bridge->Begins == Bridge->Ends && Bridge->bValid,
			"disconnected bridge may leave later calls undebugged");
	}
}

int main()
{
	try
	{
		TestExistingSession();
		TestFailureAndRollback();
		TestStopCallbackException();
		TestDebuggerOwnership();
		TestTeardownDoesNotPause();
		TestCleanupReentry();
		TestProtocolScheduling(false);
		TestProtocolScheduling(true);
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
	return 0;
}
