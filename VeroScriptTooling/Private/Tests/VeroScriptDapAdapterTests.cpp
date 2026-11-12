// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptDapAdapterTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	struct FLiveHostState
	{
		int CallCount = 0;
	};

	// Both domains stay alive while a control request overlaps a disconnect.
	// Info may already be in flight when the protocol owner attaches a new domain.
	class FControlTestDomain final : public VeroScript::Tooling::IVeroScriptLiveDebugDomain
	{
	public:
		using FInfo = VeroScript::Tooling::FLiveDebugDomainInfo;

		//~ Begin VeroScript::Tooling::IVeroScriptLiveDebugDomain Interface
		FInfo Info() const override
		{
			if (ObserveInfo)
			{
				ObserveInfo();
			}
			return Description;
		}

		bool Connect(FEventHandler, std::string&) override
		{
			Connected = !ConnectResult || ConnectResult();
			return Connected;
		}

		bool Activate(std::string&) override
		{
			return Connected;
		}

		void Disconnect() noexcept override
		{
			if (InControl.load())
			{
				DisconnectedDuringControl = true;
			}

			Connected = false;
		}

		VeroScript::Tooling::FDapSetBreakpointsResult SetBreakpoints(std::string_view,
		    const std::vector<VeroScript::Tooling::FLiveDebugBreakpoint>&) override
		{
			return BindBreakpoints ? BindBreakpoints() : VeroScript::Tooling::FDapSetBreakpointsResult{};
		}

		VeroScript::Tooling::FDapContinueResult Resume(VeroScript::Tooling::EDebugRunMode) override
		{
			return {};
		}

		VeroScript::Tooling::FDapContinueResult Pause() override
		{
			++PauseRequests;
			InControl = false;
			return {Connected.load(), false};
		}

		void RequestCancel() override
		{
			++CancelRequests;
			InControl = false;
		}

		VeroScript::Tooling::FDapStackTraceResult StackTrace() const override
		{
			return {};
		}

		VeroScript::Tooling::FDapScopesResult Scopes(int) const override
		{
			return {};
		}

		VeroScript::Tooling::FDapVariablesResult Variables(int) const override
		{
			return {};
		}

		VeroScript::Tooling::FDapEvaluateResult Evaluate(
		    std::string_view, int, std::string_view) const override
		{
			return {};
		}

		VeroScript::Tooling::FDapSetVariableResult SetVariable(
		    int, std::string_view, std::string_view) override
		{
			return {};
		}

		bool IsStopped() const override
		{
			return false;
		}

		bool IsContextRunning() const override
		{
			return Connected;
		}
		//~ End VeroScript::Tooling::IVeroScriptLiveDebugDomain Interface

		explicit FControlTestDomain(std::string Id)
		{
			Description.HostId = "control-host";
			Description.DomainId = std::move(Id);
			Description.bCanPause = Description.bCanTerminate = true;
		}

		FInfo Description;

		std::function<void()> ObserveInfo;

		std::function<bool()> ConnectResult;

		std::function<VeroScript::Tooling::FDapSetBreakpointsResult()> BindBreakpoints;

		std::atomic<bool> Connected{false};

		std::atomic<bool> InControl{false}, DisconnectedDuringControl{false};

		std::atomic<int> PauseRequests{0}, CancelRequests{0};
	};

	bool CheckDomainControlIdentity(bool Cancel, int Reattach)
	{
		using namespace VeroScript::Tooling;
		FControlTestDomain Original("original"), Replacement("replacement");
		VeroScriptDapAdapter Adapter;
		if (!Adapter.Initialize().bSuccess || !Adapter.AttachDomain(Original, "original").bSuccess) return false;
		std::mutex Mutex;
		std::condition_variable Changed;
		bool Entered = false, Released = false, DisconnectStarted = false, Disconnected = false;
		Original.ObserveInfo = [&]
		{
			std::unique_lock Lock(Mutex);
			Original.InControl = true;
			Entered = true;
			Changed.notify_all();
			Changed.wait(Lock,
			    [&]
			{
				return Released;
			});
		};
		bool PauseSucceeded = false;
		std::thread Control(
		    [&]
		{
			if (Cancel)
			{
				Adapter.Terminate();
			}

			else
			{
				PauseSucceeded = Adapter.Pause().bSuccess;
			}
		});
		bool Observed = false;
		{
			std::unique_lock Lock(Mutex);
			Observed = Changed.wait_for(Lock, std::chrono::seconds(5),
			    [&]
			{
				return Entered;
			});
		}
		bool Attached = false;
		std::thread Lifecycle(
		    [&]
		{
			{
				std::lock_guard Lock(Mutex);
				DisconnectStarted = true;
			}
			Changed.notify_all();
			Adapter.Disconnect();
			if (Reattach == 2)
			{
				// Reusing the same domain object starts a new connection too.
				Original.ObserveInfo = {};
				Attached = Adapter.AttachDomain(Original, "original").bSuccess;
			}
			else
			{
				Attached = Reattach == 0 || Adapter.AttachDomain(Replacement, "replacement").bSuccess;
			}

			{
				std::lock_guard Lock(Mutex);
				Disconnected = true;
			}
			Changed.notify_all();
		});
		bool Drained = false;
		{
			std::unique_lock Lock(Mutex);
			Observed = Observed &&
			    Changed.wait_for(Lock, std::chrono::seconds(5),
			        [&]
			{
				return DisconnectStarted;
			});
			Drained = !Changed.wait_for(Lock, std::chrono::milliseconds(100),
			    [&]
			{
				return Disconnected;
			});
			Released = true;
		}
		Changed.notify_all();
		Control.join();
		Lifecycle.join();
		Adapter.Disconnect();
		const bool Correct = Observed && Attached && Drained && !Original.DisconnectedDuringControl &&
		    PauseSucceeded == !Cancel && Original.PauseRequests == (Cancel ? 0 : 1) &&
		    Original.CancelRequests == (Cancel ? 1 : 0) && Replacement.PauseRequests == 0 &&
		    Replacement.CancelRequests == 0;
		if (!Correct)
		{
			std::cerr << "domain control target: cancel=" << Cancel << " reattach=" << Reattach
			          << " original=" << Original.PauseRequests << ',' << Original.CancelRequests
			          << " replacement=" << Replacement.PauseRequests << ',' << Replacement.CancelRequests
			          << '\n';
		}

		return Correct;
	}

	bool CheckDomainAttachControls(const int Mode)
	{
		using namespace VeroScript::Tooling;
		FControlTestDomain Domain("attaching");
		VeroScriptDapAdapter Adapter;
		if (!Adapter.Initialize().bSuccess) return false;
		std::mutex Mutex;
		std::condition_variable Changed;
		bool Entered = false, Released = false;
		const auto WaitForControls = [&]
		{
			std::unique_lock Lock(Mutex);
			Entered = true;
			Changed.notify_all();
			Changed.wait(Lock,
			    [&]
			{
				return Released;
			});
		};
		if (Mode == 2)
		{
			if (!Adapter.SetBreakpoints("pending.vs", {{1, {}}}).bSuccess) return false;
			Domain.BindBreakpoints = [&]
			{
				WaitForControls();
				FDapSetBreakpointsResult Result;
				Result.bSuccess = false;
				return Result;
			};
		}
		else
		{
			Domain.ConnectResult = [&]
			{
				WaitForControls();
				return Mode == 1;
			};
		}

		FDapLaunchResult Attached;
		std::thread Lifecycle(
		    [&]
		{
			Attached = Adapter.AttachDomain(Domain, "attaching");
		});
		bool Observed = false;
		{
			std::unique_lock Lock(Mutex);
			Observed = Changed.wait_for(Lock, std::chrono::seconds(5),
			    [&]
			{
				return Entered;
			});
		}
		// A connection is unavailable to cross-thread controls until Connect and
		// all pending breakpoint bindings have succeeded. The host may reject either.
		const bool PauseSucceeded = Adapter.Pause().bSuccess;
		Adapter.Terminate();
		{
			std::lock_guard Lock(Mutex);
			Released = true;
		}
		Changed.notify_all();
		Lifecycle.join();
		const bool Published = Adapter.IsDomainAttached();
		const bool ControlSucceeded = Mode != 1 || Adapter.Pause().bSuccess;
		Adapter.Disconnect();
		const bool Correct = Observed && !PauseSucceeded && ControlSucceeded &&
		    Attached.bSuccess == (Mode == 1) && Published == (Mode == 1) &&
		    Domain.PauseRequests == (Mode == 1 ? 1 : 0) && Domain.CancelRequests == 0 && !Domain.Connected;
		if (!Correct)
		{
			std::cerr << "domain attach controls: mode=" << Mode << " pause=" << Domain.PauseRequests
			          << " cancel=" << Domain.CancelRequests << '\n';
		}

		return Correct;
	}

	bool LiveHostAdd(
	    const VeroScript::FNativeCallContext& Context,
	    VeroScript::FValue& OutReturn,
	    std::string& OutError)
	{
		(void)OutError;
		auto* const State = static_cast<FLiveHostState*>(Context.UserData);
		if (State)
		{
			++State->CallCount;
		}
		OutReturn = static_cast<std::int32_t>(
		    std::get<std::int32_t>(Context.Args[0]) + std::get<std::int32_t>(Context.Args[1]));
		return true;
	}
}

int main()
{
	using namespace VeroScript::Tooling;
	{
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.FileLabel = "replace-event-handler.vs";
		Launch.Source = "void main(){print(1);print(2);}";
		Launch.bStopOnEntry = false;
		auto Owner = std::make_shared<int>(1);
		const std::weak_ptr<int> Lifetime = Owner;
		bool bAlive = false;
		int Calls = 0;
		Adapter.SetEventHandler([Owner, &Lifetime, &bAlive, &Calls, &Adapter](const FDapEvent& Event)
		{
			(void)Owner;
			if (Event.Kind != EDapEventKind::Output || Event.Category != "stdout") return;
			++Calls;
			const auto* Weak = &Lifetime;
			auto* Alive = &bAlive;
			Adapter.SetEventHandler({});
			*Alive = !Weak->expired();
		});
		Owner.reset();
		if (!Expect(Adapter.Launch(Launch).bSuccess, "load event handler lifetime regression")) return EXIT_FAILURE;
		const auto Run = Adapter.ConfigurationDone();
		if (!Expect(Run.bSuccess && Run.Output == "1\n2\n" && Calls == 1 && bAlive && Lifetime.expired(),
		        "clearing an event handler keeps active captures alive and suppresses later events"))
		{
			return EXIT_FAILURE;
		}

		std::vector<int> Visits;
		Adapter.SetEventHandler([Count = 0, &Visits](const FDapEvent& Event) mutable
		{
			if (Event.Kind == EDapEventKind::Output && Event.Category == "stdout")
			{
				Visits.push_back(++Count);
			}
		});
		if (!Expect(Adapter.Launch(Launch).bSuccess, "reload event handler state regression")) return EXIT_FAILURE;
		const auto Repeated = Adapter.ConfigurationDone();
		if (!Expect(Repeated.bSuccess && Visits == std::vector<int>{1, 2},
		        "event handler mutable captures keep the same identity across deliveries"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch, Replacement;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Replacement.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.FileLabel = "active-launch.vs";
		Launch.Source = "void main(){\nint value=7;\nprint(value);\n}\n";
		Replacement.FileLabel = "replacement-launch.vs";
		Replacement.Source = "void main(){print(99);}\n";
		int Stops = 0;
		bool bCorrect = false;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped) return;
			++Stops;
			bCorrect = [&]()
			{
				if (!Adapter.IsRunning() || !Adapter.IsStopped()) return false;
				const auto Stack = Adapter.StackTrace();
				if (!Stack.bSuccess || Stack.StackFrames.empty()) return false;
				const int Frame = Stack.StackFrames.front().Id;
				const auto Scopes = Adapter.Scopes(Frame);
				if (!Scopes.bSuccess || Scopes.Scopes.empty()) return false;
				const int Locals = Scopes.Scopes.front().VariablesReference;
				const auto Rejected = [](const auto& Result)
				{
					return !Result.bSuccess && !Result.Diagnostics.empty() && Result.Diagnostics.front().Code == "dap.already_running";
				};
				if (!Rejected(Adapter.Launch(Replacement)) || !Rejected(Adapter.Attach(Replacement)) || !Rejected(Adapter.ConfigurationDone()))
				{
					return false;
				}
				const auto StillStopped = Adapter.StackTrace();
				const auto Value = Adapter.Evaluate("value", Frame);
				return Adapter.IsRunning() && Adapter.IsStopped() && Adapter.IsLoaded() && StillStopped.bSuccess && StillStopped.StackFrames.size() == Stack.StackFrames.size() && StillStopped.StackFrames.front().Id == Frame && Adapter.Variables(Locals).bSuccess && Value.bSuccess && Value.Result == "7";
			}();
			bCorrect &= Adapter.Continue().bSuccess;
		});
		if (!Expect(Adapter.Launch(Launch).bSuccess && Adapter.SetBreakpoints(Launch.FileLabel, {{3, {}}}).bSuccess,
		        "prepare reentrant lifecycle rejection"))
		{
			return EXIT_FAILURE;
		}
		const auto Run = Adapter.ConfigurationDone();
		if (!Expect(Run.bSuccess && Stops == 1 && bCorrect && Run.Output.find('7') != std::string::npos,
		        "active launch, attach and configurationDone leave the paused VM and handles intact"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Adapter.Launch(Replacement).bSuccess,
		        "relaunch remains available after the original execution returns"))
		{
			return EXIT_FAILURE;
		}
		const auto Reloaded = Adapter.ConfigurationDone();
		if (!Expect(Reloaded.bSuccess && Reloaded.Output.find("99") != std::string::npos && Stops == 1,
		        "the replacement program runs normally after rejecting active lifecycle requests"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FControlTestDomain Domain("rollback");
		VeroScriptDapAdapter Adapter;
		if (!Expect(Adapter.Initialize().bSuccess && Adapter.SetBreakpoints("pending.vs", {{1, {}}}).bSuccess,
		        "prepare attachment rollback"))
		{
			return EXIT_FAILURE;
		}

		Domain.BindBreakpoints = []() -> FDapSetBreakpointsResult
		{
			throw std::runtime_error("host breakpoint binding failed");
		};
		bool RolledBack = false;
		try
		{
			Adapter.AttachDomain(Domain, "rollback");
		}
		catch (const std::runtime_error&)
		{
			RolledBack = !Domain.Connected && !Adapter.IsDomainAttached();
		}
		if (!Expect(RolledBack, "host exceptions disconnect an unpublished attachment")) return EXIT_FAILURE;
		if (!Expect(Adapter.SetBreakpoints("pending.vs", {}).bSuccess &&
		            Adapter.AttachDomain(Domain, "rollback").bSuccess,
		        "a rejected attachment does not prevent a later connection"))
		{
			return EXIT_FAILURE;
		}

		Adapter.Disconnect();
	}
	for (const int Mode : {0, 1, 2})
	{
		if (!Expect(CheckDomainAttachControls(Mode),
		        "controls cannot enter a pending or rejected domain attachment"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const int Reattach : {1, 0, 2})
	{
		for (const bool Cancel : {false, true})
		{
			if (!Expect(CheckDomainControlIdentity(Cancel, Reattach),
			        "in-flight controls retain the original domain across disconnect and reattach"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	constexpr const char* Source =
	    "void main() {\n"
	    "    int x = 1;\n"
	    "    int y = 2;\n"
	    "    print(x + y);\n"
	    "}\n";

	{
		VeroScriptDapAdapter Adapter;
		int StoppedCount = 0;
		int OutputCount = 0;
		int ExitedCount = 0;
		int TerminatedCount = 0;
		std::uint32_t StoppedLine = 0;
		std::string EvalPlus;
		std::string SetVarValue;
		std::string LocalXAfterSet;
		EDapStopReason LastStopReason = EDapStopReason::Breakpoint;

		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			switch (Event.Kind)
			{
			case EDapEventKind::Stopped:
				{
					++StoppedCount;
					LastStopReason = Event.StopReason;
					if (!Expect(Adapter.IsStopped(), "adapter should report stopped"))
					{
						return;
					}
					const FDapStackTraceResult Stack = Adapter.StackTrace();
					if (!Expect(Stack.bSuccess && !Stack.StackFrames.empty(), "stackTrace while stopped"))
					{
						return;
					}
					StoppedLine = Stack.StackFrames.front().Line;
					const FDapEvaluateResult Eval =
					    Adapter.Evaluate("x + 1", Stack.StackFrames.front().Id);
					if (!Expect(Eval.bSuccess, "evaluate x+1"))
					{
						return;
					}
					EvalPlus = Eval.Result;
					const FDapScopesResult Scopes = Adapter.Scopes(Stack.StackFrames.front().Id);
					if (!Expect(Scopes.bSuccess && !Scopes.Scopes.empty(), "scopes while stopped"))
					{
						return;
					}
					const FDapSetVariableResult Set =
					    Adapter.SetVariable(Scopes.Scopes.front().VariablesReference, "x", "5");
					if (!Expect(Set.bSuccess, "setVariable x=5"))
					{
						return;
					}
					SetVarValue = Set.Value;
					const FDapVariablesResult Vars =
					    Adapter.Variables(Scopes.Scopes.front().VariablesReference);
					for (const FDapVariable& Var : Vars.Variables)
					{
						if (Var.Name == "x")
						{
							LocalXAfterSet = Var.Value;
						}
					}
					Adapter.Continue();
					break;
				}
			case EDapEventKind::Output:
				{
					++OutputCount;
					break;
				}
			case EDapEventKind::Exited:
				{
					++ExitedCount;
					break;
				}
			case EDapEventKind::Terminated:
				{
					++TerminatedCount;
					break;
				}
			default:
				{
					break;
				}
			}
		});

		const FDapInitializeResult Init = Adapter.Initialize();
		if (!Expect(
		        Init.bSuccess && Init.Capabilities.bSupportsConfigurationDoneRequest && Init.Capabilities.bSupportsEvaluateRequest && Init.Capabilities.bSupportsConditionalBreakpoints && Init.Capabilities.bSupportsSetVariable && !Init.Version.empty() && !Init.Platform.empty() && Init.JitCapability == "backend-services",
		        "initialize caps + platform metadata"))
		{
			return EXIT_FAILURE;
		}

		const FDapEvaluateResult NotStopped = Adapter.Evaluate("x", 0);
		if (!Expect(!NotStopped.bSuccess, "evaluate fails when not stopped"))
		{
			return EXIT_FAILURE;
		}

		FDapLaunchArgs LaunchArgs;
		LaunchArgs.VmBackendFactory = VeroScript::Test::VmFactory();
		LaunchArgs.Source = Source;
		LaunchArgs.FileLabel = "debug.vs";
		const FDapLaunchResult Launch = Adapter.Launch(LaunchArgs);
		if (!Expect(Launch.bSuccess && Adapter.IsLoaded(), "launch source"))
		{
			return EXIT_FAILURE;
		}

		const FDapSetBreakpointsResult Breakpoints = Adapter.SetBreakpoints(
		    "debug.vs",
		    std::vector<FDapSourceBreakpoint>{{4, {}}});
		if (!Expect(
		        Breakpoints.bSuccess && Breakpoints.Breakpoints.size() == 1 && Breakpoints.Breakpoints.front().bVerified && Breakpoints.Breakpoints.front().Line == 4,
		        "setBreakpoints verified"))
		{
			return EXIT_FAILURE;
		}

		const FDebugRunResult Run = Adapter.ConfigurationDone();
		if (!Expect(Run.bSuccess, "configurationDone/run should succeed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(StoppedCount >= 1, "should stop on breakpoint"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(LastStopReason == EDapStopReason::Breakpoint, "stop reason breakpoint"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(StoppedLine == 4, "stopped on line 4"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(EvalPlus == "2", "evaluate x+1 => 2"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(SetVarValue == "5" && LocalXAfterSet == "5", "setVariable updates local"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(OutputCount == 1 && Run.Output.find('7') != std::string::npos, "stdout after setVariable"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(ExitedCount == 1 && TerminatedCount == 1, "exited and terminated"))
		{
			return EXIT_FAILURE;
		}

		Adapter.Disconnect();
		if (!Expect(!Adapter.IsLoaded(), "disconnect clears session"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter Adapter;
		std::vector<FDapEvent> OutputEvents;
		bool bRunInProgress = false;
		bool bOutputArrivedDuringRun = false;
		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Output)
			{
				return;
			}
			bOutputArrivedDuringRun = bOutputArrivedDuringRun || bRunInProgress;
			OutputEvents.push_back(Event);
		});
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source =
		    "void main() {\n"
		    "    assert(!std::console::is_interactive() && !std::console::is_output_terminal());\n"
		    "    assert(!std::console::supports_color() && !std::console::supports_cursor() && !std::console::supports_clear());\n"
		    "    assert(std::console::width() == 0 && std::console::height() == 0);\n"
		    "    assert(!std::console::set_color(\"bright_red\", \"default\"));\n"
		    "    assert(!std::console::clear() && !std::console::clear_line(\"all\"));\n"
		    "    assert(!std::console::move_cursor(0, 0) && !std::console::set_cursor_visible(false));\n"
		    "    assert(!std::console::reset_style());\n"
		    "    std::console::write(\"out\");\n"
		    "    std::console::flush();\n"
		    "    std::console::error(\"err\");\n"
		    "}\n";
		Launch.FileLabel = "dap-console.vs";
		if (!Expect(Adapter.Launch(Launch).bSuccess, "launch console DAP session"))
		{
			return EXIT_FAILURE;
		}
		bRunInProgress = true;
		const FDebugRunResult Run = Adapter.ConfigurationDone();
		bRunInProgress = false;
		bool bSawStdout = false;
		bool bSawStderr = false;
		for (const FDapEvent& Event : OutputEvents)
		{
			bSawStdout = bSawStdout || (Event.Category == "stdout" && Event.Output == "out");
			bSawStderr = bSawStderr || (Event.Category == "stderr" && Event.Output == "err");
		}
		if (!Expect(
		        Run.bSuccess && Run.Output == "out" && Run.ErrorOutput == "err" && bOutputArrivedDuringRun && bSawStdout && bSawStderr,
		        "DAP streams flushed stdout and stderr before termination"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* ContainerSource =
		    "class Box {\n"
		    "public:\n"
		    "    int value;\n"
		    "}\n"
		    "void main() {\n"
		    "    array<int> xs = array<int>();\n"
		    "    xs.push(10);\n"
		    "    xs.push(20);\n"
		    "    map<string,int> scores = map<string,int>();\n"
		    "    scores[\"alice\"] = 7;\n"
		    "    scores[\"line\\nkey\"] = 8;\n"
		    "    set<string> tags = set<string>();\n"
		    "    tags.add(\"red\");\n"
		    "    queue<int> q = queue<int>();\n"
		    "    q.push(3);\n"
		    "    stack<int> s = stack<int>();\n"
		    "    s.push(4);\n"
		    "    ordered_map<string,int> om = ordered_map<string,int>();\n"
		    "    om[\"first\"] = 5;\n"
		    "    deque<int> d = deque<int>();\n"
		    "    d.push_back(6);\n"
		    "    Box b = new Box();\n"
		    "    b.value = 42;\n"
		    "    int fixed[2] = {1, 2};\n"
		    "    int64 bigKey = 9223372036854775807i64;\n"
		    "    map<int64,int> big = map<int64,int>();\n"
		    "    big[bigKey] = 11;\n"
		    "    print(0);\n"
		    "}\n";
		VeroScriptDapAdapter Adapter;
		bool bExpandedArray = false;
		bool bExpandedMap = false;
		bool bExpandedSet = false;
		bool bExpandedEvaluate = false;
		int ExpandedAdditionalKinds = 0;
		bool bEditedArrayChild = false;
		bool bEditedNativeArrayChild = false;
		bool bEditedMapChild = false;
		bool bEditedSetChild = false;
		bool bEditedObjectField = false;
		bool bExpandedAndEditedInt64Map = false;
		bool bExpandedInt64Map = false;
		bool bExpansionFailed = false;
		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped)
			{
				return;
			}
			const FDapVariablesResult Locals = Adapter.Variables(1);
			if (!Locals.bSuccess)
			{
				bExpansionFailed = true;
				Adapter.Continue();
				return;
			}
			int ArrayReference = 0;
			for (const FDapVariable& Local : Locals.Variables)
			{
				if (Local.VariablesReference == 0)
				{
					continue;
				}
				const FDapVariablesResult Children =
				    Adapter.Variables(Local.VariablesReference);
				if (!Children.bSuccess)
				{
					bExpansionFailed = true;
					continue;
				}
				if (Local.Name == "xs")
				{
					ArrayReference = Local.VariablesReference;
					bExpandedArray =
					    Children.Variables.size() == 2 && Children.Variables[0].Name == "[0]" && Children.Variables[0].Value == "10" && Children.Variables[1].Name == "[1]" && Children.Variables[1].Value == "20";
				}
				else if (Local.Name == "scores")
				{
					bExpandedMap =
					    Children.Variables.size() == 2 && Children.Variables[0].Name == "[\"alice\"]" && Children.Variables[0].Value == "7" && Children.Variables[1].Name == "[\"line\\nkey\"]" && Children.Variables[1].Value == "8";
				}
				else if (Local.Name == "tags")
				{
					bExpandedSet =
					    Children.Variables.size() == 1 && Children.Variables[0].Value == "red";
				}
				else if (Local.Name == "q")
				{
					ExpandedAdditionalKinds +=
					    Children.Variables.size() == 1 && Children.Variables[0].Value == "3";
				}
				else if (Local.Name == "s")
				{
					ExpandedAdditionalKinds +=
					    Children.Variables.size() == 1 && Children.Variables[0].Value == "4";
				}
				else if (Local.Name == "om")
				{
					ExpandedAdditionalKinds +=
					    Children.Variables.size() == 1 && Children.Variables[0].Name == "[\"first\"]" && Children.Variables[0].Value == "5";
				}
				else if (Local.Name == "d")
				{
					ExpandedAdditionalKinds +=
					    Children.Variables.size() == 1 && Children.Variables[0].Value == "6";
				}
				else if (Local.Name == "b")
				{
					ExpandedAdditionalKinds +=
					    Children.Variables.size() == 1 && Children.Variables[0].Name == "value" && Children.Variables[0].Value == "42";
				}
				else if (Local.Name == "big")
				{
					const std::string ChildName = "[9223372036854775807]";
					bExpandedInt64Map =
					    Children.Variables.size() == 1 && Children.Variables[0].Name == ChildName && Children.Variables[0].Value == "11";
				}
			}
			const auto EditChild =
			    [&](const std::string& LocalName,
			        const std::string& ChildName,
			        const std::string& Expression,
			        const std::string& ExpectedValue) -> bool
			{
				const FDapVariablesResult CurrentLocals = Adapter.Variables(1);
				for (const FDapVariable& Local : CurrentLocals.Variables)
				{
					if (Local.Name == LocalName && Local.VariablesReference != 0)
					{
						const FDapSetVariableResult Set = Adapter.SetVariable(
						    Local.VariablesReference,
						    ChildName,
						    Expression);
						return Set.bSuccess && Set.Value == ExpectedValue;
					}
				}
				return false;
			};
			bEditedMapChild = EditChild("scores", "[\"alice\"]", "9", "9");
			bEditedSetChild = EditChild("tags", "[0]", "\"blue\"", "blue");
			bEditedObjectField = EditChild("b", "value", "43", "43");
			bExpandedAndEditedInt64Map =
			    bExpandedInt64Map && EditChild("big", "[9223372036854775807]", "12", "12");
			const FDapEvaluateResult Evaluated = Adapter.Evaluate("xs", 0, "watch");
			if (!Evaluated.bSuccess || Evaluated.VariablesReference == 0)
			{
				std::cerr << "evaluate xs failed: success=" << Evaluated.bSuccess
				          << " ref=" << Evaluated.VariablesReference
				          << " error=[" << Evaluated.Error << "] result=["
				          << Evaluated.Result << "]\n";
				for (const FDapVariable& Local : Locals.Variables)
				{
					std::cerr << "local=[" << Local.Name << "] value=["
					          << Local.Value << "]\n";
				}
				bExpansionFailed = true;
			}
			else
			{
				const FDapVariablesResult EvaluatedChildren =
				    Adapter.Variables(Evaluated.VariablesReference);
				bExpandedEvaluate =
				    EvaluatedChildren.bSuccess && EvaluatedChildren.Variables.size() == 2 && EvaluatedChildren.Variables[0].Value == "10" && EvaluatedChildren.Variables[1].Value == "20";
			}
			{
				const FDapVariablesResult CurrentLocals = Adapter.Variables(1);
				for (const FDapVariable& Local : CurrentLocals.Variables)
				{
					if (Local.Name == "xs")
					{
						ArrayReference = Local.VariablesReference;
						break;
					}
				}
			}
			if (ArrayReference != 0)
			{
				const FDapSetVariableResult Set =
				    Adapter.SetVariable(ArrayReference, "[0]", "99");
				if (Set.bSuccess && Set.Value == "99")
				{
					const FDapVariablesResult UpdatedLocals = Adapter.Variables(1);
					int NativeArrayReference = 0;
					for (const FDapVariable& Local : UpdatedLocals.Variables)
					{
						if (Local.Name == "fixed")
						{
							NativeArrayReference = Local.VariablesReference;
						}
						if (Local.Name != "xs" || Local.VariablesReference == 0)
						{
							continue;
						}
						const FDapVariablesResult Updated =
						    Adapter.Variables(Local.VariablesReference);
						bEditedArrayChild =
						    Updated.bSuccess && Updated.Variables.size() == 2 && Updated.Variables[0].Value == "99";
					}
					if (NativeArrayReference != 0)
					{
						const FDapSetVariableResult NativeSet =
						    Adapter.SetVariable(NativeArrayReference, "[1]", "77");
						bEditedNativeArrayChild =
						    NativeSet.bSuccess && NativeSet.Value == "77";
					}
				}
			}
			Adapter.Continue();
		});
		if (!Expect(Adapter.Initialize().bSuccess, "initialize container DAP adapter"))
		{
			return EXIT_FAILURE;
		}
		FDapLaunchArgs LaunchArgs;
		LaunchArgs.VmBackendFactory = VeroScript::Test::VmFactory();
		LaunchArgs.Source = ContainerSource;
		LaunchArgs.FileLabel = "debug-containers.vs";
		if (!Expect(Adapter.Launch(LaunchArgs).bSuccess, "launch container debug source"))
		{
			return EXIT_FAILURE;
		}
		const FDapSetBreakpointsResult Breakpoints = Adapter.SetBreakpoints(
		    "debug-containers.vs",
		    std::vector<FDapSourceBreakpoint>{{28, {}}});
		if (!Expect(
		        Breakpoints.bSuccess && Breakpoints.Breakpoints.front().bVerified,
		        "set container expansion breakpoint"))
		{
			return EXIT_FAILURE;
		}
		const FDebugRunResult Run = Adapter.ConfigurationDone();
		if (!Run.bSuccess || bExpansionFailed || !bExpandedArray || !bExpandedMap || !bExpandedSet || !bExpandedEvaluate || !bEditedArrayChild || !bEditedNativeArrayChild || !bEditedMapChild || !bEditedSetChild || !bEditedObjectField || !bExpandedAndEditedInt64Map || ExpandedAdditionalKinds != 5)
		{
			std::cerr << "container debug state: run=" << Run.bSuccess
			          << " failed=" << bExpansionFailed
			          << " array=" << bExpandedArray
			          << " map=" << bExpandedMap
			          << " set=" << bExpandedSet
			          << " evaluate=" << bExpandedEvaluate
			          << " edited=" << bEditedArrayChild
			          << " nativeEdited=" << bEditedNativeArrayChild
			          << " mapEdited=" << bEditedMapChild
			          << " setEdited=" << bEditedSetChild
			          << " objectEdited=" << bEditedObjectField
			          << " int64Map=" << bExpandedAndEditedInt64Map << '\n';
		}
		if (!Expect(
		        Run.bSuccess && !bExpansionFailed && bExpandedArray && bExpandedMap && bExpandedSet && bExpandedEvaluate && bEditedArrayChild && bEditedNativeArrayChild && bEditedMapChild && bEditedSetChild && bEditedObjectField && bExpandedAndEditedInt64Map && ExpandedAdditionalKinds == 5,
		        "DAP expands objects and all collection kinds and edits container children"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.FileLabel = "replace-container-child.vs";
		Launch.Source =
		    "class Box{public:array<int> values={1};};\nvoid main(){\n"
		    "Box box=new Box();\narray<int> replacement={7,8};\narray<int> reset={4,5};\nprint(0);\n}\n";
		bool bChecked = false, bCorrect = false;
		int LastReference = 0;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped) return;
			bChecked = true;
			bCorrect = [&]()
			{
				const auto FindReference = [](const FDapVariablesResult& Page, const std::string_view Name)
				{
					for (const auto& Variable : Page.Variables)
					{
						if (Variable.Name == Name) return Variable.VariablesReference;
					}
					return 0;
				};
				const auto Stack = Adapter.StackTrace();
				if (!Stack.bSuccess || Stack.StackFrames.empty()) return false;
				const auto Scopes = Adapter.Scopes(Stack.StackFrames.front().Id);
				if (!Scopes.bSuccess || Scopes.Scopes.empty()) return false;
				const int Locals = Scopes.Scopes.front().VariablesReference;
				const int Box = FindReference(Adapter.Variables(Locals), "box");
				if (!Box) return false;
				const int OldValues = FindReference(Adapter.Variables(Box), "values");
				if (!OldValues) return false;
				const auto Set = Adapter.SetVariable(Box, "values", "replacement");
				if (!Set.bSuccess || !Set.VariablesReference || Set.VariablesReference == OldValues || Adapter.Variables(Box).bSuccess || Adapter.Variables(OldValues).bSuccess)
				{
					return false;
				}
				const auto Values = Adapter.Variables(Set.VariablesReference);
				if (!Values.bSuccess || Values.Variables.size() != 2 || Values.Variables[0].Value != "7" || Values.Variables[1].Value != "8")
				{
					return false;
				}
				const auto Scalar = Adapter.SetVariable(Set.VariablesReference, "[0]", "9");
				if (!Scalar.bSuccess || Scalar.Value != "9" || Scalar.VariablesReference != 0 || Adapter.Variables(Set.VariablesReference).bSuccess)
				{
					return false;
				}
				// Local assignments use the same result-page path as child assignments.
				const auto LocalSet = Adapter.SetVariable(Locals, "replacement", "reset");
				if (!LocalSet.bSuccess || !LocalSet.VariablesReference) return false;
				LastReference = LocalSet.VariablesReference;
				const auto LocalValues = Adapter.Variables(LastReference);
				return LocalValues.bSuccess && LocalValues.Variables.size() == 2 && LocalValues.Variables[0].Value == "4" && LocalValues.Variables[1].Value == "5";
			}();
			Adapter.Continue();
		});
		if (!Expect(Adapter.Launch(Launch).bSuccess && Adapter.SetBreakpoints(Launch.FileLabel, {{6, {}}}).bSuccess && Adapter.ConfigurationDone().bSuccess && bChecked && bCorrect && LastReference != 0 && !Adapter.Variables(LastReference).bSuccess,
		        "replacing a container child returns an expandable editable page and expires old handles"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter EntryAdapter;
		bool bSawEntry = false;
		EntryAdapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped)
			{
				return;
			}
			if (Event.StopReason == EDapStopReason::Entry)
			{
				bSawEntry = true;
			}
			EntryAdapter.Continue();
		});
		EntryAdapter.Initialize();
		FDapLaunchArgs EntryLaunch;
		EntryLaunch.VmBackendFactory = VeroScript::Test::VmFactory();
		EntryLaunch.Source = Source;
		EntryLaunch.FileLabel = "entry.vs";
		EntryLaunch.bStopOnEntry = true;
		if (!Expect(EntryAdapter.Launch(EntryLaunch).bSuccess, "launch stopOnEntry"))
		{
			return EXIT_FAILURE;
		}
		const FDebugRunResult EntryRun = EntryAdapter.ConfigurationDone();
		if (!Expect(EntryRun.bSuccess && bSawEntry, "stopOnEntry should stop with entry reason"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter CondAdapter;
		int CondStops = 0;
		CondAdapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped)
			{
				return;
			}
			++CondStops;
			CondAdapter.Continue();
		});
		CondAdapter.Initialize();
		FDapLaunchArgs CondLaunch;
		CondLaunch.VmBackendFactory = VeroScript::Test::VmFactory();
		CondLaunch.Source = Source;
		CondLaunch.FileLabel = "cond.vs";
		if (!Expect(CondAdapter.Launch(CondLaunch).bSuccess, "launch conditional"))
		{
			return EXIT_FAILURE;
		}
		const FDapSetBreakpointsResult CondBp = CondAdapter.SetBreakpoints(
		    "cond.vs",
		    std::vector<FDapSourceBreakpoint>{{4, "x == 1"}});
		if (!Expect(CondBp.bSuccess && CondBp.Breakpoints.front().bVerified, "conditional BP set"))
		{
			return EXIT_FAILURE;
		}
		const FDebugRunResult CondRun = CondAdapter.ConfigurationDone();
		if (!Expect(CondRun.bSuccess && CondStops == 1, "conditional x==1 should stop once"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter SkipAdapter;
		int SkipStops = 0;
		SkipAdapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped)
			{
				return;
			}
			++SkipStops;
			SkipAdapter.Continue();
		});
		SkipAdapter.Initialize();
		FDapLaunchArgs SkipLaunch;
		SkipLaunch.VmBackendFactory = VeroScript::Test::VmFactory();
		SkipLaunch.Source = Source;
		SkipLaunch.FileLabel = "skip.vs";
		if (!Expect(SkipAdapter.Launch(SkipLaunch).bSuccess, "launch false condition"))
		{
			return EXIT_FAILURE;
		}
		const FDapSetBreakpointsResult SkipBp = SkipAdapter.SetBreakpoints(
		    "skip.vs",
		    std::vector<FDapSourceBreakpoint>{{4, "x == 99"}});
		if (!Expect(SkipBp.bSuccess && SkipBp.Breakpoints.front().bVerified, "false-condition BP set"))
		{
			return EXIT_FAILURE;
		}
		const FDebugRunResult SkipRun = SkipAdapter.ConfigurationDone();
		if (!Expect(SkipRun.bSuccess && SkipStops == 0, "conditional x==99 should not stop"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(SkipRun.Output.find('3') != std::string::npos, "false condition still prints"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Launch with mixed backend preference; debugger still stops (effective Vm).
		VeroScriptDapAdapter MixedAdapter;
		int MixedStops = 0;
		MixedAdapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Stopped)
			{
				++MixedStops;
				MixedAdapter.Continue();
			}
		});
		MixedAdapter.Initialize();
		FDapLaunchArgs MixedLaunch;
		MixedLaunch.VmBackendFactory = VeroScript::Test::VmFactory();
		MixedLaunch.Source = Source;
		MixedLaunch.FileLabel = "mixed.vs";
		MixedLaunch.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
		if (!Expect(MixedAdapter.Launch(MixedLaunch).bSuccess, "launch with mixed backend"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        MixedAdapter.SetBreakpoints(
		                        "mixed.vs",
		                        std::vector<FDapSourceBreakpoint>{{4, {}}})
		            .bSuccess,
		        "setBreakpoints under mixed preference"))
		{
			return EXIT_FAILURE;
		}
		const FDebugRunResult MixedRun = MixedAdapter.ConfigurationDone();
		if (!Expect(MixedRun.bSuccess && MixedStops >= 1, "mixed launch still stops under debugger"))
		{
			std::cerr << "mixedStops=" << MixedStops << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Live host registry: DAP Execute must call real Callbacks (not catalog stubs).
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		FLiveHostState LiveState;
		if (!Registry.RegisterNative(
		        "host_add",
		        VeroScript::ENativeType::Int,
		        {VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
		        LiveHostAdd,
		        &LiveState,
		        ErrorId))
		{
			return Fail("RegisterNative host_add for live DAP");
		}

		VeroScriptDapAdapter Adapter;
		std::string Printed;
		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Output)
			{
				Printed += Event.Output;
			}
			else if (Event.Kind == EDapEventKind::Stopped)
			{
				Adapter.Continue();
			}
		});
		Adapter.Initialize();

		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source =
		    "void main() {\n"
		    "    print(host_add(40, 2));\n"
		    "}\n";
		Launch.FileLabel = "live_host.vs";
		Launch.Bindings = &Registry;
		Launch.bLiveBindings = true;
		if (!Expect(Adapter.Launch(Launch).bSuccess, "launch with live Bindings"))
		{
			return EXIT_FAILURE;
		}
		const FDebugRunResult Run = Adapter.ConfigurationDone();
		if (!Expect(Run.bSuccess, "configurationDone with live host_add"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(LiveState.CallCount == 1, "live host_add should be invoked once"))
		{
			std::cerr << "CallCount=" << LiveState.CallCount << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Printed.find("42") != std::string::npos, "print should show host_add result"))
		{
			std::cerr << "printed:\n"
			          << Printed;
			return EXIT_FAILURE;
		}
	}

	{
		// Attach mirrors Launch for in-process + live registry (IDE TCP uses attach).
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		FLiveHostState LiveState;
		if (!Registry.RegisterNative(
		        "host_add",
		        VeroScript::ENativeType::Int,
		        {VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
		        LiveHostAdd,
		        &LiveState,
		        ErrorId))
		{
			return Fail("RegisterNative host_add for attach DAP");
		}

		VeroScriptDapAdapter Adapter;
		std::string Printed;
		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Output)
			{
				Printed += Event.Output;
			}
			else if (Event.Kind == EDapEventKind::Stopped)
			{
				Adapter.Continue();
			}
		});
		Adapter.Initialize();

		FDapLaunchArgs AttachArgs;
		AttachArgs.VmBackendFactory = VeroScript::Test::VmFactory();
		AttachArgs.Source =
		    "void main() {\n"
		    "    print(host_add(10, 32));\n"
		    "}\n";
		AttachArgs.FileLabel = "attach_host.vs";
		AttachArgs.Bindings = &Registry;
		AttachArgs.bLiveBindings = true;
		if (!Expect(Adapter.Attach(AttachArgs).bSuccess, "attach with live Bindings"))
		{
			return EXIT_FAILURE;
		}
		const FDebugRunResult Run = Adapter.ConfigurationDone();
		if (!Expect(Run.bSuccess && LiveState.CallCount == 1, "attach configurationDone"))
		{
			std::cerr << "CallCount=" << LiveState.CallCount << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Printed.find("42") != std::string::npos, "attach print host_add"))
		{
			std::cerr << "printed:\n"
			          << Printed;
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter Adapter;
		std::string Printed;
		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Output)
			{
				Printed += Event.Output;
			}
		});
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source = "void main() { print(HOST_SUM(19, 23)); }\n";
		Launch.FileLabel = "dap_host_macro.vs";
		Launch.PredefinedMacros = {
		    {"HOST_SUM", {"left", "right"}, false, "left + right", "dap-host"}};
		if (!Expect(Adapter.Launch(Launch).bSuccess, "DAP accepts predefined macros") || !Expect(Adapter.ConfigurationDone().bSuccess && Printed.find("42") != std::string::npos, "DAP predefined macro applies during compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source =
		    "int dive(int n) {\n"
		    "    if (n <= 0) { return 0; }\n"
		    "    return 1 + dive(n - 1);\n"
		    "}\n"
		    "void main() { print(dive(8)); }\n";
		Launch.FileLabel = "dap-recursion-depth.vs";
		Launch.MaxCallDepth = 4;
		if (!Expect(
		        Adapter.Launch(Launch).bSuccess,
		        "DAP launch accepts max call depth") ||
		    !Expect(
		        !Adapter.ConfigurationDone().bSuccess,
		        "DAP launch propagates max call depth to debug session"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Generic owners contain their own `#` before the `::Method#signature`
		// boundary. Stack-frame formatting must still expose the real method leaf.
		VeroScriptDapAdapter Adapter;
		int Stops = 0;
		std::string TopFrameName;
		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped)
			{
				return;
			}
			++Stops;
			const FDapStackTraceResult Stack = Adapter.StackTrace();
			if (Stack.bSuccess && !Stack.StackFrames.empty())
			{
				TopFrameName = Stack.StackFrames.front().Name;
			}
			Adapter.Continue();
		});
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source =
		    "class Box<T> {\n"
		    "public:\n"
		    "    int Touch(T Value) {\n"
		    "        int Copy = Value;\n"
		    "        return Copy;\n"
		    "    }\n"
		    "}\n"
		    "void main() {\n"
		    "    Box<int> Value = Box<int>();\n"
		    "    print(Value.Touch(7));\n"
		    "}\n";
		Launch.FileLabel = "dap#generic::method.vs";
		if (!Expect(Adapter.Launch(Launch).bSuccess, "launch generic method stack test") || !Expect(Adapter.SetBreakpoints("dap#generic::method.vs", std::vector<FDapSourceBreakpoint>{{4, {}}}).bSuccess, "set generic method breakpoint") || !Expect(Adapter.ConfigurationDone().bSuccess, "run generic method stack test") || !Expect(Stops == 1 && TopFrameName == "Touch", "generic method frame name"))
		{
			std::cerr << "stops=" << Stops << " frame=" << TopFrameName << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter Adapter;
		int DapExitCode = -1;
		Adapter.SetEventHandler(
		    [&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Exited)
			{
				DapExitCode = Event.ExitCode;
			}
		});
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source = "int main() { return 23; }\n";
		Launch.FileLabel = "dap-int-main.vs";
		if (!Expect(Adapter.Launch(Launch).bSuccess, "launch int main debug source"))
		{
			return EXIT_FAILURE;
		}
		const FDebugRunResult Run = Adapter.ConfigurationDone();
		if (!Expect(
		        Run.bSuccess && Run.ExitCode == 23 && DapExitCode == 23,
		        "DAP should propagate int main exit code"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A nearby source line without a VM opcode must relocate to an actual
		// instruction in the same function, never report its requested line as
		// verified merely because the debugger allocated an ID.
		constexpr const char* MultilineSource =
		    "int main()\n"
		    "{\n"
		    "    print(\"hello\");\n"
		    "    double begin = std::chrono::steady_now();\n"
		    "    print(\"hello\");\n"
		    "\n"
		    "    double elapsed_us =\n"
		    "        (std::chrono::steady_now() - begin) * 1000000.0;\n"
		    "\n"
		    "    print(elapsed_us);\n"
		    "    return 0;\n"
		    "}\n";
		VeroScriptDapAdapter Adapter;
		std::uint32_t StoppedLine = 0;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Stopped)
			{
				const FDapStackTraceResult Stack = Adapter.StackTrace();
				if (!Stack.StackFrames.empty())
				{
					StoppedLine = Stack.StackFrames.front().Line;
				}
				Adapter.Continue();
			}
		});
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source = MultilineSource;
		Launch.FileLabel = "multiline-breakpoint.vs";
		if (!Expect(Adapter.Launch(Launch).bSuccess, "launch multiline breakpoint source"))
		{
			return EXIT_FAILURE;
		}
		const FDapSetBreakpointsResult Mapped = Adapter.SetBreakpoints(
		    "multiline-breakpoint.vs", {{6, {}}});
		if (!Expect(Mapped.Breakpoints.size() == 1 && Mapped.Breakpoints.front().bVerified && Mapped.Breakpoints.front().Line == 7,
		        "non-executable source line should move to its executable line"))
		{
			std::cerr << "mapped line=" << (Mapped.Breakpoints.empty() ? 0 : Mapped.Breakpoints.front().Line) << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Adapter.ConfigurationDone().bSuccess && StoppedLine == 7,
		        "mapped breakpoint must stop at the reported line"))
		{
			return EXIT_FAILURE;
		}
		StoppedLine = 0;
		if (!Expect(Adapter.Launch(Launch).bSuccess,
		        "relaunch multiline breakpoint source"))
		{
			return EXIT_FAILURE;
		}
		const FDapSetBreakpointsResult Declaration = Adapter.SetBreakpoints(
		    "multiline-breakpoint.vs", {{7, {}}});
		if (!Expect(Declaration.Breakpoints.size() == 1 && Declaration.Breakpoints.front().bVerified && Declaration.Breakpoints.front().Line == 7 && Adapter.ConfigurationDone().bSuccess && StoppedLine == 7,
		        "multiline declaration breakpoint must hit the reported line"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Replacing or clearing a source via another spelling must remove its old
		// registrations, including pending entries and entries retained for relaunch.
		const std::string Path = (std::filesystem::current_path() / "alias-breakpoints.vs").generic_string();
		const std::string Alias = (std::filesystem::current_path() / "." / "alias-breakpoints.vs").generic_string();
		for (const bool bClear : {false, true})
		{
			VeroScriptDapAdapter Adapter;
			std::vector<std::uint32_t> Stops;
			std::vector<FDapBreakpoint> Rebound;
			Adapter.SetEventHandler(
			    [&](const FDapEvent& Event)
			{
				if (Event.Kind == EDapEventKind::Stopped)
				{
					const auto Stack = Adapter.StackTrace();
					if (!Stack.StackFrames.empty())
					{
						Stops.push_back(Stack.StackFrames.front().Line);
					}

					Adapter.Continue();
				}
				else if (Event.Kind == EDapEventKind::Breakpoint)
				{
					Rebound.push_back(Event.Breakpoint);
				}
			});
			Adapter.Initialize();
			Adapter.SetBreakpoints(Path, {{3, {}}});
			const auto Replacement = Adapter.SetBreakpoints(Alias,
			    bClear ? std::vector<FDapSourceBreakpoint>{} : std::vector<FDapSourceBreakpoint>{{4, {}}});
			FDapLaunchArgs Launch;
			Launch.VmBackendFactory = VeroScript::Test::VmFactory();
			Launch.Source = "void main(){\nint a=1;\nprint(a);\nprint(2);\n}\n";
			Launch.FileLabel = Path;
			if (!Expect(Replacement.Breakpoints.size() == (bClear ? 0u : 1u) &&
			            Adapter.Launch(Launch).bSuccess && Adapter.ConfigurationDone().bSuccess,
			        "replace pending source breakpoints through a path alias"))
			{
				return EXIT_FAILURE;
			}

			if (!Expect(Stops == (bClear ? std::vector<std::uint32_t>{} : std::vector<std::uint32_t>{4}) &&
			            Rebound.size() == (bClear ? 0u : 1u),
			        "a replaced breakpoint cannot survive launch under its old path spelling"))
			{
				return EXIT_FAILURE;
			}

			Stops.clear();
			Rebound.clear();
			Adapter.SetBreakpoints(Path, {{3, {}}});
			Adapter.SetBreakpoints(Alias, {});
			if (!Expect(Adapter.Launch(Launch).bSuccess && Adapter.ConfigurationDone().bSuccess &&
			            Stops.empty() && Rebound.empty(),
			        "cleared source aliases cannot restore old breakpoints during relaunch"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	{
		// Two absolute files with the same basename must not be conflated.
		const std::string CompiledPath =
		    (std::filesystem::current_path() / "compiled" / "same.vs").string();
		const std::string OtherPath =
		    (std::filesystem::current_path() / "other" / "same.vs").string();
		VeroScriptDapAdapter Adapter;
		std::vector<FDapBreakpoint> ChangedBreakpoints;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Breakpoint)
			{
				ChangedBreakpoints.push_back(Event.Breakpoint);
			}
		});
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source = "int main() { print(1); return 0; }\n";
		Launch.FileLabel = CompiledPath;
		if (!Expect(Adapter.Launch(Launch).bSuccess, "launch absolute source path"))
		{
			return EXIT_FAILURE;
		}
		const FDapSetBreakpointsResult Wrong = Adapter.SetBreakpoints(
		    OtherPath, {{1, {}}});
		const FDapSetBreakpointsResult Right = Adapter.SetBreakpoints(
		    CompiledPath, {{1, {}}});
		if (!Expect(Wrong.Breakpoints.size() == 1 && !Wrong.Breakpoints.front().bVerified && Wrong.Breakpoints.front().Id > 0 && !Wrong.Breakpoints.front().Message.empty() && Right.Breakpoints.size() == 1 && Right.Breakpoints.front().bVerified && Right.Breakpoints.front().Id > Wrong.Breakpoints.front().Id,
		        "different absolute source paths must not share a breakpoint"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Adapter.Launch(Launch).bSuccess && ChangedBreakpoints.size() == 2 && ChangedBreakpoints[0].Id == Wrong.Breakpoints[0].Id && !ChangedBreakpoints[0].bVerified && ChangedBreakpoints[1].Id == Right.Breakpoints[0].Id && ChangedBreakpoints[1].bVerified,
		        "relaunch must preserve IDs and publish all binding outcomes"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A basename is never sufficient evidence that another relative or
		// absolute path names the compiled source.
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source = "int main() { print(1); return 0; }\n";
		Launch.FileLabel = "virtual-source.vs";
		if (!Expect(Adapter.Launch(Launch).bSuccess, "launch relative source label"))
		{
			return EXIT_FAILURE;
		}
		const FDapSetBreakpointsResult OtherRelative = Adapter.SetBreakpoints(
		    "other/virtual-source.vs", {{1, {}}});
		const FDapSetBreakpointsResult OtherAbsolute = Adapter.SetBreakpoints(
		    (std::filesystem::current_path() / "other" / "virtual-source.vs").string(),
		    {{1, {}}});
		const FDapSetBreakpointsResult Exact = Adapter.SetBreakpoints(
		    "virtual-source.vs", {{1, {}}});
		if (!Expect(OtherRelative.Breakpoints.size() == 1 && !OtherRelative.Breakpoints[0].bVerified && OtherAbsolute.Breakpoints.size() == 1 && !OtherAbsolute.Breakpoints[0].bVerified && Exact.Breakpoints.size() == 1 && Exact.Breakpoints[0].bVerified,
		        "same basename must not verify another source path"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A relative path that resolves to the very same existing file must
		// still match its absolute IDE path without basename-only matching.
		const std::filesystem::path Root = std::filesystem::current_path() / ("veroscript-dap-identity-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Root);
		const std::filesystem::path MainPath = Root / "main.vs";
		std::ofstream(MainPath, std::ios::binary)
		    << "int main() { print(1); return 0; }\n";
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Path = (Root.filename() / "main.vs").generic_string();
		const bool bLoaded = Adapter.Launch(Launch).bSuccess;
		const FDapSetBreakpointsResult Bound = Adapter.SetBreakpoints(
		    MainPath.generic_string(), {{1, {}}});
		const bool bValid = bLoaded && Bound.Breakpoints.size() == 1 && Bound.Breakpoints[0].bVerified;
		std::error_code RemoveError;
		std::filesystem::remove_all(Root, RemoveError);
		if (!Expect(bValid, "relative and absolute aliases of one file must match"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// The engine's legacy basename matching must not make a verified DAP
		// breakpoint fire in another compiled module with the same basename.
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("veroscript-dap-same-name-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Root);
		const std::filesystem::path ImportPath = Root / "same.vs";
		std::ofstream(ImportPath, std::ios::binary)
		    << "export int imported()\n{\n    int x = 1;\n"
		       "    print(x);\n    return x;\n}\n";
		const std::string MainSource =
		    "import \"" + ImportPath.generic_string() + "\";\n"
		                                                "int main()\n{\n    int x = imported();\n"
		                                                "    print(x);\n    return 0;\n}\n";
		VeroScriptDapAdapter Adapter;
		std::vector<std::string> StoppedFiles;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Stopped)
			{
				const FDapStackTraceResult Stack = Adapter.StackTrace();
				if (!Stack.StackFrames.empty())
				{
					StoppedFiles.push_back(Stack.StackFrames.front().Path);
				}
				Adapter.Continue();
			}
		});
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source = MainSource;
		Launch.FileLabel = "same.vs";
		const bool bLoaded = Adapter.Launch(Launch).bSuccess;
		const FDapSetBreakpointsResult Bound = Adapter.SetBreakpoints(
		    "same.vs", {{4, {}}});
		const bool bValid = bLoaded && Bound.Breakpoints.size() == 1 && Bound.Breakpoints[0].bVerified && Adapter.ConfigurationDone().bSuccess && !StoppedFiles.empty() && std::all_of(StoppedFiles.begin(), StoppedFiles.end(), [](const std::string& File)
		{
			return File == "same.vs";
		});
		std::error_code RemoveError;
		std::filesystem::remove_all(Root, RemoveError);
		if (!Expect(bValid, "DAP breakpoint must not hit same-named imported module"))
		{
			std::cerr << "loaded=" << bLoaded
			          << " verified=" << (Bound.Breakpoints.empty() ? false : Bound.Breakpoints[0].bVerified)
			          << " stopped=" << StoppedFiles.size() << '\n';
			for (const std::string& File : StoppedFiles)
			{
				std::cerr << "stop file=" << File << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		// A disk import has its own preprocessor map; its debugger line is
		// not necessarily the physical line shown by the editor.
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("veroscript-dap-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Root);
		const std::filesystem::path MainPath = Root / "main.vs";
		const std::filesystem::path ImportPath = Root / "dep.vs";
		std::ofstream(MainPath, std::ios::binary)
		    << "import \"dep.vs\";\nint main() { return imported(); }\n";
		std::ofstream(ImportPath, std::ios::binary)
		    << "#define VALUE 42\n#if 0\n"
		       "export int ignored() { return 0; }\n#endif\n"
		       "export int imported()\n{\n    int x = VALUE;\n"
		       "    return x;\n}\n";
		VeroScriptDapAdapter Adapter;
		std::uint32_t StoppedLine = 0;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Stopped)
			{
				const FDapStackTraceResult Stack = Adapter.StackTrace();
				if (!Stack.StackFrames.empty())
				{
					StoppedLine = Stack.StackFrames.front().Line;
				}
				Adapter.Continue();
			}
		});
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Path = MainPath.generic_string();
		const bool bLoaded = Adapter.Launch(Launch).bSuccess;
		const FDapSetBreakpointsResult Bound = Adapter.SetBreakpoints(
		    ImportPath.generic_string(), {{3, {}}, {7, {}}});
		const bool bValid = bLoaded && Bound.Breakpoints.size() == 2 && !Bound.Breakpoints[0].bVerified && Bound.Breakpoints[1].bVerified && Bound.Breakpoints[1].Line == 7 && Adapter.ConfigurationDone().bSuccess && StoppedLine == 7;
		std::error_code RemoveError;
		std::filesystem::remove_all(Root, RemoveError);
		if (!Expect(bValid, "imported module breakpoint must map to its physical line"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* PreprocessedSource =
		    "#define VALUE 3\n"
		    "int main()\n"
		    "{\n"
		    "#if 0\n"
		    "    print(99);\n"
		    "#endif\n"
		    "    int x = VALUE;\n"
		    "    print(x);\n"
		    "    return 0;\n"
		    "}\n";
		VeroScriptDapAdapter Adapter;
		std::uint32_t StoppedLine = 0;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Stopped)
			{
				const FDapStackTraceResult Stack = Adapter.StackTrace();
				if (!Stack.StackFrames.empty())
				{
					StoppedLine = Stack.StackFrames.front().Line;
				}
				Adapter.Continue();
			}
		});
		Adapter.Initialize();
		const FDapSetBreakpointsResult Pending = Adapter.SetBreakpoints(
		    "preprocessed-breakpoint.vs", {{8, {}}});
		if (!Expect(Pending.Breakpoints.size() == 1 && !Pending.Breakpoints.front().bVerified && !Pending.Breakpoints.front().Message.empty(),
		        "pre-launch breakpoint must remain pending"))
		{
			return EXIT_FAILURE;
		}
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source = PreprocessedSource;
		Launch.FileLabel = "preprocessed-breakpoint.vs";
		if (!Expect(Adapter.Launch(Launch).bSuccess, "launch preprocessed source"))
		{
			return EXIT_FAILURE;
		}
		const FDapSetBreakpointsResult Bound = Adapter.SetBreakpoints(
		    "preprocessed-breakpoint.vs", {{5, {}}, {8, {}}, {50, {}}});
		if (!Expect(Bound.Breakpoints.size() == 3 && !Bound.Breakpoints[0].bVerified && !Bound.Breakpoints[0].Message.empty() && Bound.Breakpoints[1].bVerified && Bound.Breakpoints[1].Line == 8 && !Bound.Breakpoints[2].bVerified && !Bound.Breakpoints[2].Message.empty(),
		        "inactive preprocessor line must not bind; active line must map"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Adapter.ConfigurationDone().bSuccess && StoppedLine == 8,
		        "preprocessed breakpoint must stop at its physical source line"))
		{
			return EXIT_FAILURE;
		}
		StoppedLine = 0;
		if (!Expect(Adapter.Launch(Launch).bSuccess && Adapter.ConfigurationDone().bSuccess && StoppedLine == 8,
		        "relaunch must rebind only executable breakpoints with the new line map"))
		{
			return EXIT_FAILURE;
		}

		VeroScriptDapAdapter PreLaunchAdapter;
		std::uint32_t PreLaunchStoppedLine = 0;
		FDapBreakpoint PreLaunchBoundEvent;
		int PreLaunchBreakpointEvents = 0;
		PreLaunchAdapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind == EDapEventKind::Breakpoint)
			{
				PreLaunchBoundEvent = Event.Breakpoint;
				++PreLaunchBreakpointEvents;
			}
			if (Event.Kind == EDapEventKind::Stopped)
			{
				const FDapStackTraceResult Stack = PreLaunchAdapter.StackTrace();
				if (!Stack.StackFrames.empty())
				{
					PreLaunchStoppedLine = Stack.StackFrames.front().Line;
				}
				PreLaunchAdapter.Continue();
			}
		});
		PreLaunchAdapter.Initialize();
		const FDapSetBreakpointsResult PendingAgain =
		    PreLaunchAdapter.SetBreakpoints("preprocessed-breakpoint.vs", {{8, {}}});
		if (!Expect(PreLaunchAdapter.Launch(Launch).bSuccess && PendingAgain.Breakpoints.size() == 1 && !PendingAgain.Breakpoints[0].bVerified && PendingAgain.Breakpoints[0].Id > 0 && PreLaunchBreakpointEvents == 1 && PreLaunchBoundEvent.Id == PendingAgain.Breakpoints[0].Id && PreLaunchBoundEvent.bVerified && PreLaunchBoundEvent.Line == 8 && PreLaunchAdapter.ConfigurationDone().bSuccess && PreLaunchStoppedLine == 8,
		        "pre-launch breakpoint must bind after compilation"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.FileLabel = "handle-lifetimes.vs";
		Launch.Source = "void main(){\n array<int> values={1};\n print(values[0]);\n print(values[0]);\n}\n";
		int Stops = 0;
		int OldFrame = -1;
		int OldLocals = 0;
		int OldChildren = 0;
		bool bCorrect = true;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped) return;
			++Stops;
			const auto Stack = Adapter.StackTrace();
			if (!Stack.bSuccess || Stack.StackFrames.empty())
			{
				bCorrect = false;
				Adapter.Continue();
				return;
			}
			const int Frame = Stack.StackFrames.front().Id;
			const auto Scopes = Adapter.Scopes(Frame);
			if (!Scopes.bSuccess || Scopes.Scopes.empty())
			{
				bCorrect = false;
				Adapter.Continue();
				return;
			}
			const int Locals = Scopes.Scopes.front().VariablesReference;
			const auto Variables = Adapter.Variables(Locals);
			const auto Array = std::find_if(Variables.Variables.begin(), Variables.Variables.end(),
			    [](const FDapVariable& Variable)
			{
				return Variable.Name == "values";
			});
			if (!Variables.bSuccess || Array == Variables.Variables.end() || !Array->VariablesReference)
			{
				bCorrect = false;
				Adapter.Continue();
				return;
			}
			if (Stops == 1)
			{
				OldFrame = Frame;
				OldLocals = Locals;
				OldChildren = Array->VariablesReference;
			}
			else
			{
				bCorrect &= Frame != OldFrame && Locals != OldLocals && !Adapter.Scopes(OldFrame).bSuccess && !Adapter.Evaluate("values[0]", OldFrame).bSuccess && !Adapter.Variables(OldLocals).bSuccess && !Adapter.SetVariable(OldLocals, "values", "array<int>()").bSuccess && !Adapter.Variables(OldChildren).bSuccess;
				const int Children = Array->VariablesReference;
				const auto Again = Adapter.Variables(Locals);
				const auto SameArray = std::find_if(Again.Variables.begin(), Again.Variables.end(),
				    [](const FDapVariable& Variable)
				{
					return Variable.Name == "values";
				});
				bCorrect &= SameArray != Again.Variables.end() && SameArray->VariablesReference == Children;
				const auto Set = Adapter.SetVariable(Children, "[0]", "9");
				const auto Refreshed = Adapter.Variables(Locals);
				bCorrect &= Set.bSuccess && !Adapter.Variables(Children).bSuccess && Refreshed.bSuccess && Adapter.Evaluate("values[0]", Frame).Result == "9";
			}
			Adapter.Continue();
		});
		if (!Expect(Adapter.Launch(Launch).bSuccess && Adapter.SetBreakpoints(Launch.FileLabel, {{3, {}}, {4, {}}}).bSuccess && Adapter.ConfigurationDone().bSuccess && Stops == 2 && bCorrect,
		        "DAP handles expire across stops and edits; repeated expansion preserves identity"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.FileLabel = "memory-inspection.vs";
		Launch.Source = "void main(){\n auto bytes=new std::memory::buffer(8);\n print(1);\n print(2);\n}\n";
		int Stops = 0;
		int OldChildren = 0;
		bool bCorrect = true;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped)
			{
				return;
			}
			++Stops;
			const auto Stack = Adapter.StackTrace();
			if (!Stack.bSuccess || Stack.StackFrames.empty())
			{
				bCorrect = false;
				Adapter.Continue();
				return;
			}
			const int Frame = Stack.StackFrames.front().Id;
			const auto Scopes = Adapter.Scopes(Frame);
			if (!Scopes.bSuccess || Scopes.Scopes.empty())
			{
				bCorrect = false;
				Adapter.Continue();
				return;
			}
			const int Locals = Scopes.Scopes.front().VariablesReference;
			const auto Variables = Adapter.Variables(Locals);
			const auto FindBytes = [](const FDapVariablesResult& Page)
			{
				return std::find_if(Page.Variables.begin(), Page.Variables.end(),
				    [](const FDapVariable& Variable)
				{
					return Variable.Name == "bytes";
				});
			};
			const auto Bytes = FindBytes(Variables);
			if (!Variables.bSuccess || Bytes == Variables.Variables.end() || !Bytes->VariablesReference)
			{
				bCorrect = false;
				Adapter.Continue();
				return;
			}
			const int Children = Bytes->VariablesReference;
			const auto Page = Adapter.Variables(Children);
			bCorrect &= Page.bSuccess && Page.Variables.size() == 5;
			bool LengthFound = false, IdentityFound = false;
			for (const auto& Child : Page.Variables)
			{
				bCorrect &= !Child.VariablesReference && !Child.Memory.StorageAddress && !Child.Memory.ObjectId && !Adapter.SetVariable(Children, Child.Name, "0").bSuccess;
				if (Child.Name == "[byte_length]")
				{
					LengthFound = Child.Value == "8";
				}
				if (Child.Name == "[object_id]")
				{
					IdentityFound = Bytes->Memory.ObjectId && Child.Value == std::to_string(*Bytes->Memory.ObjectId);
				}
			}
			const auto Again = Adapter.Variables(Locals);
			const auto SameBytes = FindBytes(Again);
			bCorrect &= LengthFound && IdentityFound && SameBytes != Again.Variables.end() && SameBytes->VariablesReference == Children && SameBytes->Value == Bytes->Value;
			const auto Watch = Adapter.Evaluate("bytes", Frame);
			bCorrect &= Watch.bSuccess && Watch.VariablesReference && Adapter.Variables(Watch.VariablesReference).Variables.size() == 4;
			if (Stops == 2)
			{
				bCorrect &= Children != OldChildren && !Adapter.Variables(OldChildren).bSuccess;
			}
			OldChildren = Children;
			Adapter.Continue();
		});
		if (!Expect(Adapter.Launch(Launch).bSuccess && Adapter.SetBreakpoints(Launch.FileLabel, {{3, {}}, {4, {}}}).bSuccess && Adapter.ConfigurationDone().bSuccess && Stops == 2 && bCorrect,
		        "memory metadata uses read-only standard DAP children, including watches and stop expiry"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// A protocol/control thread may cancel a paused program without entering
		// the debugger. The owning stop callback can then return normally.
		VeroScriptDapAdapter Adapter;
		Adapter.Initialize();
		FDapLaunchArgs Launch;
		Launch.VmBackendFactory = VeroScript::Test::VmFactory();
		Launch.Source = "int main(){\nint value=7;\nprint(value);\nreturn 0;\n}";
		Launch.FileLabel = "paused-control-cancel.vs";
		int Stops = 0;
		int FrameId = -1;
		Adapter.SetEventHandler([&](const FDapEvent& Event)
		{
			if (Event.Kind != EDapEventKind::Stopped) return;
			++Stops;
			const auto Stack = Adapter.StackTrace();
			if (Stack.bSuccess && !Stack.StackFrames.empty())
			{
				FrameId = Stack.StackFrames.front().Id;
			}
			std::thread Controller([&]
			{
				Adapter.Terminate();
			});
			Controller.join();
		});
		if (!Expect(Adapter.Launch(Launch).bSuccess && Adapter.SetBreakpoints(Launch.FileLabel, {{3, {}}}).bSuccess,
		        "load a program for cross-thread cancellation at a stop"))
		{
			return EXIT_FAILURE;
		}
		const auto Run = Adapter.ConfigurationDone();
		if (!Expect(Stops == 1 && FrameId >= 0 && !Run.bSuccess && Run.ExitCode != 0 && Run.Output.empty() && !Adapter.IsStopped() && !Adapter.IsRunning() && !Adapter.Scopes(FrameId).bSuccess,
		        "cross-thread cancellation exits the paused call without execution or stale handles"))
		{
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
