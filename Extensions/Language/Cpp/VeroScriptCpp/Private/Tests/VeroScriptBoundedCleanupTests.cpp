// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestJitCapabilities.h"
#include "VeroScriptSession.h"
#include "VeroScriptCompile.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptVm.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptValueRoot.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{
	using namespace VeroScript;

	class FHost final : public IEngineHost
	{
	public:
		//~ Begin IEngineHost Interface
		void OnPrintLine(std::string_view Line) override
		{
			Output.append(Line).push_back('\n');
		}
		void OnDiagnosticLine(std::string_view) override
		{
		}
		//~ End IEngineHost Interface

		std::string Output;
	};

	void Require(bool Condition, const std::string& Message)
	{
		if (!Condition)
		{
			throw std::runtime_error(Message);
		}
	}

	struct FHook
	{
		std::shared_ptr<FExecutionControl> Cancel;
		std::chrono::milliseconds Delay{0};
		FValueRoot Retained;
	};

	bool Interrupt(const FNativeCallContext& Context, FValue&, std::string&)
	{
		auto& Hook = *static_cast<FHook*>(Context.UserData);
		if (Hook.Delay.count())
		{
			std::this_thread::sleep_for(Hook.Delay);
		}
		if (Hook.Cancel)
		{
			Hook.Cancel->RequestCancel();
		}
		return true;
	}

	bool SaveCleanup(const FNativeCallContext& Context, FValue&, std::string& Error)
	{
		return Context.ScriptInvoker && Context.ScriptInvoker->RetainForGeneration(
			Context.Args[0], static_cast<FHook*>(Context.UserData)->Retained, Error);
	}

	bool RetireCleanup(const FNativeCallContext& Context, FValue&, std::string&)
	{
		static_cast<FHook*>(Context.UserData)->Retained.Reset();
		return true;
	}

	bool HasDiagnostic(const FRunResult& Result, std::string_view Id)
	{
		return std::any_of(Result.Diagnostics.begin(), Result.Diagnostics.end(), [&](const FScriptError& Error)
		{
			return Error.MessageId == Id;
		});
	}

	enum class EInterruption
	{
		Instructions,
		Timeout,
		Cancel,
		PriorRuntimeFault
	};

	struct FCase
	{
		const char* Name;
		const char* Source;
		int Expected = 1;
		bool bReset = false;
		const char* Secondary = nullptr;
		EInterruption Interruption = EInterruption::Instructions;
		std::chrono::milliseconds Delay{0};
	};

	void CheckCase(const FCase& Case, EExecutionBackend Mode,
		const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		auto Control = std::make_shared<FExecutionControl>();
		FHook Hook;
		Hook.Delay = Case.Delay;
		if (Case.Interruption == EInterruption::Cancel)
		{
			Hook.Cancel = Control;
		}
		Require(Session.RegisterNative("interrupt_hook", ENativeType::Void, {}, Interrupt, &Hook), "register hook");
		Require(Session.RegisterNative("save_cleanup", FNativeTypeDesc::Scalar(ENativeType::Void),
			{{FNativeTypeDesc::Function("fn()->void"), 0}}, SaveCleanup, &Hook), "register cleanup root");
		Require(Session.RegisterNative("retire_cleanup", ENativeType::Void, {}, RetireCleanup, &Hook), "register root retirement");
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.ExecutionBackend = Mode;
		Config.JitBackendFactory = Factory;
		Config.ExecutionControl = Control;
		Session.ApplyConfig(Config);
		const std::string Source = std::string("int cleaned=0;int Read(){return cleaned;}int main(){return 0;}") + Case.Source;
		auto Loaded = Session.LoadSource(Source, Case.Name);
		Require(Loaded.bSuccess, std::string(Case.Name) + ": " + Loaded.Error.Message);
		Require(Session.Run().bSuccess, std::string(Case.Name) + ": initialize");
		FExecutionLimits Limits{80, 0};
		if (std::string_view(Case.Name) == "simple.vs")
		{
			Limits.MaxInstructions = 20;
		}
		std::string_view Primary = "diag.runtime_execution_instruction_limit_exceeded";
		if (Case.Interruption == EInterruption::Timeout)
		{
			Limits = {0, 50};
			Primary = "diag.execution_time_limit_exceeded";
		}
		else if (Case.Interruption == EInterruption::Cancel)
		{
			Limits = {};
			Primary = "diag.execution_cancelled_by_host";
		}
		else if (Case.Interruption == EInterruption::PriorRuntimeFault)
		{
			Primary = "diag.division_by_zero";
		}
		Session.SetExecutionLimits(Limits);
		const auto Start = std::chrono::steady_clock::now();
		const auto Result = Session.CallFunction("Exhaust", {});
		Require(std::chrono::steady_clock::now() - Start < std::chrono::seconds(5), std::string(Case.Name) + ": unbounded cleanup");
		Require(!Result.bSuccess && Result.Error.MessageId == Primary, std::string(Case.Name) + ": wrong primary: " + Result.Error.Message);
		Require(Session.RequiresReset() == Case.bReset, std::string(Case.Name) + ": wrong reuse state");
		if (Case.Secondary)
		{
			Require(HasDiagnostic(Result, Case.Secondary), std::string(Case.Name) + ": missing secondary diagnostic");
		}
		Session.SetExecutionLimits({});
		Control->Reset();
		FValue Value;
		const auto Read = Session.CallFunction("Read", {}, &Value);
		if (Case.bReset)
		{
			Require(!Read.bSuccess && Read.Error.MessageId == "diag.execution_state_reset_required", std::string(Case.Name) + ": unsafe reuse accepted");
			Require(HasDiagnostic(Result, "diag.execution_state_reset_required"), std::string(Case.Name) + ": reset requirement not returned");
			Session.Clear();
			Require(!Session.RequiresReset() && !Session.IsLoaded(), std::string(Case.Name) + ": clear did not reset");
			Require(Session.LoadSource("int main(){return 0;}", "fresh.vs").bSuccess && Session.Run().bSuccess, std::string(Case.Name) + ": fresh load failed");
		}
		else
		{
			Require(Read.bSuccess && std::get<std::int32_t>(Value) == Case.Expected, std::string(Case.Name) + ": finally/destructor effects missing");
			if (std::string_view(Case.Name) == "simple.vs")
			{
				std::cout << "persistent finally mode=" << static_cast<int>(Mode) << " limit=20 cleaned=1\n";
			}
		}
		if (std::string_view(Case.Name) == "throw-cleanup.vs" || std::string_view(Case.Name) == "business-catch.vs")
		{
			Require(Host.Output == "outer\n", "throw-cleanup.vs: enclosing finally did not run");
		}
	}

	void CheckCoreState()
	{
		auto Compiled = VeroScriptCompile::CompileSource(
			"struct Guard{~Guard(){print(77);}};Guard item=Guard();"
			"void main(){try{while(true){}}finally{while(true){}}}", "core-state.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		FHotReloadState State;
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionLimits({80, 0});
		Require(!Vm.ExecutePersistent(*Compiled.Module, ERunMode::Script, State) && State.bExecutionResetRequired,
			"Core persistent state did not retain reset requirement");
		VeroScriptVm Fresh(Output);
		VeroScript::Test::Configure(Fresh);
		Require(!Fresh.ExecutePersistent(*Compiled.Module, ERunMode::Script, State)
			&& Fresh.GetError().MessageId == "diag.execution_state_reset_required", "fresh VM reused unsafe state");
		VeroScriptVm Disposer(Output);
		VeroScript::Test::Configure(Disposer);
		Disposer.SetExecutionLimits({80, 0});
		Require(Disposer.DiscardPersistentState(*Compiled.Module, State) && State.Empty() && Output.str().empty(),
			"discarding unsafe state ran unfinished script cleanup");
	}

	void CheckNative(EExecutionBackend Mode, const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		if (!Factory || !Factory->GetDescription().bNativeCode) return;
		auto Compiled = VeroScriptCompile::CompileSource(
			"void Work()\n{\nwhile(true){}\n}\n"
			"void main(){try{Work();}finally{print(1);}}", "native-cleanup.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> Module(std::move(Compiled.Module));
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		std::string Error;
		Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
		Vm.SetExecutionBackend(Mode);
		FJitPolicy Policy;
		Policy.MixedHotCallThreshold = 1;
		Policy.bEnableControlledMixed = true;
		Vm.SetJitPolicy(Policy);
		// Keep this a native polling/unwind test. A quota alone deliberately
		// prefers VM arithmetic batching for this scalar loop.
		Vm.SetExecutionControl(std::make_shared<FExecutionControl>());
		Vm.SetExecutionLimits({500, 0});
		Require(!Vm.Execute(*Module, ERunMode::Script), "native limit unexpectedly succeeded");
		Require(Vm.GetError().MessageId == "diag.runtime_execution_instruction_limit_exceeded", "native primary changed");
		Require(Vm.GetError().Location.Line == 3, "native primary lost its interruption location");
		Require(Output.str() == "1\n" && !Vm.RequiresReset(), "native finally did not complete");
		const auto Stats = Vm.GetJitStats();
		// Dispatches counts successful returns; this entry must terminate instead.
		Require(Test::SupportsAsmJitControlFlow(Factory->GetDescription())
		    ? Stats.ControlledNativeExecutions > 0 : Stats.ControlledNativeExecutions == 0,
		    "controlled native cleanup matches backend capability");
		std::cout << "controlled native cleanup mode=" << static_cast<int>(Mode)
			<< " entries=" << Stats.ControlledNativeExecutions << '\n';
	}
}

int main(int Argc, char** Argv)
{
	try
	{
		std::shared_ptr<const IJitBackendFactory> Factory;
		if (Argc > 1)
		{
			std::string Error;
			Factory = LoadJitBackend(Argv[1], Error);
			Require(Factory != nullptr, Error);
		}
		const FCase Cases[] = {
			{"simple.vs", "void Exhaust(){try{while(true){}}finally{cleaned=1;}}"},
			{"nested.vs", "void Exhaust(){try{try{while(true){}}finally{cleaned=1;}}finally{cleaned=cleaned*10+2;}}", 12},
			{"helper.vs", "void Clean(){cleaned=1;}void Exhaust(){try{while(true){}}finally{Clean();}}"},
			{"caught.vs", "void Exhaust(){try{while(true){}}finally{try{throw 5;}catch(int value){cleaned=value;}}}", 5},
			{"destructor.vs", "struct Guard{~Guard(){cleaned+=4;}};void Exhaust(){Guard item=Guard();try{while(true){}}finally{cleaned+=1;}}", 5},
			{"retired-root.vs", "class Guard{public:int value=0;~Guard(){cleaned+=4;}};void Prepare(){Guard item=Guard();save_cleanup([item]()->void{cleaned+=item.value;});}void Exhaust(){Prepare();try{while(true){}}finally{retire_cleanup();cleaned+=1;}}", 5},
			{"retired-root-infinite.vs", "class Guard{public:int value=0;~Guard(){while(true){}}};void Prepare(){Guard item=Guard();save_cleanup([item]()->void{cleaned+=item.value;});}void Exhaust(){Prepare();try{while(true){}}finally{retire_cleanup();}}", 0, true, "diag.execution_cleanup_instruction_limit_exceeded"},
			{"throw-cleanup.vs", "void Exhaust(){try{try{while(true){}}finally{throw 7;}}finally{print(\"outer\");}}", 0, true, "diag.execution_cleanup_failed"},
			{"business-catch.vs", "void Exhaust(){try{try{while(true){}}finally{throw 7;}}catch(...){print(\"business\");}finally{print(\"outer\");}}", 0, true, "diag.execution_cleanup_failed"},
			{"destructor-infinite.vs", "struct Guard{~Guard(){while(true){}}};void Exhaust(){Guard item=Guard();while(true){}}", 0, true, "diag.execution_cleanup_instruction_limit_exceeded"},
			{"already-destructor.vs", "struct Guard{~Guard(){while(true){}}};void Exhaust(){Guard item=Guard();}", 0, true},
			{"prior-fault.vs", "void Exhaust(){try{int zero=0;print(1/zero);}finally{while(true){}}}", 0, true, "diag.runtime_execution_instruction_limit_exceeded", EInterruption::PriorRuntimeFault},
			{"prior-destructor-fault.vs", "struct Guard{~Guard(){while(true){}}};void Exhaust(){Guard item=Guard();int zero=0;print(1/zero);}", 0, true, "diag.runtime_execution_instruction_limit_exceeded", EInterruption::PriorRuntimeFault},
			{"infinite-cleanup.vs", "void Exhaust(){try{while(true){}}finally{while(true){}}}", 0, true, "diag.execution_cleanup_instruction_limit_exceeded"},
			{"helper-infinite.vs", "void Clean(){while(true){}}void Exhaust(){try{while(true){}}finally{Clean();}}", 0, true, "diag.execution_cleanup_instruction_limit_exceeded"},
			{"cleanup-time.vs", "void Exhaust(){try{while(true){}}finally{while(true){interrupt_hook();}}}", 0, true, "diag.execution_cleanup_time_limit_exceeded", EInterruption::Instructions, std::chrono::milliseconds(2)},
			{"exit-cleanup.vs", "void Exhaust(){try{while(true){}}finally{exit(5);}}", 0, true, "diag.execution_cleanup_failed"},
			{"already-finally.vs", "void Exhaust(){try{cleaned=1;}finally{while(true){}}}", 0, true},
			{"cancel.vs", "void Exhaust(){try{interrupt_hook();while(true){}}finally{cleaned=1;}}", 1, false, nullptr, EInterruption::Cancel},
			{"timeout.vs", "void Exhaust(){try{interrupt_hook();while(true){}}finally{cleaned=1;}}", 1, false, nullptr, EInterruption::Timeout, std::chrono::milliseconds(75)}
		};
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Mode == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}
			for (const auto& Case : Cases)
			{
				CheckCase(Case, Mode, Factory);
			}
		}
		CheckNative(EExecutionBackend::Mixed, Factory);
		CheckNative(EExecutionBackend::Jit, Factory);
		CheckCoreState();
		std::cout << "bounded cleanup and persistent-state reuse passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
