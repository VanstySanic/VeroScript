// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"
#include "VeroScriptVm.h"
#include "VeroScriptVmBackendLoader.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace VeroScript;

namespace
{
	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	struct FTestHost final : IEngineHost
	{
		void OnPrintLine(std::string_view) override
		{
		}
		void OnDiagnosticLine(std::string_view) override
		{
		}
	};
	enum class EBehavior
	{
		Normal,
		Decline,
		DeclineOnce,
		EnterThenDecline,
		Throw,
		WrongBoundary,
		NoRebind,
		NoCleanup,
		DeclinePending,
		ObserveFramePool,
		UnknownResult
	};
	struct FCounts
	{
		int Created = 0;
		int Destroyed = 0;
		int Bound = 0;
		int Declined = 0;
		std::size_t MaxPooledLocalCapacity = 0;
	};
	class FTestInstance final : public IVmBackendInstance
	{
	public:
		FTestInstance(std::shared_ptr<const IVmBackendFactory> InFactory, EBehavior InBehavior, FCounts& InCounts)
		    : Factory(std::move(InFactory)), Behavior(InBehavior), Counts(InCounts)
		{
			std::string Error;
			Inner = Factory->CreateInstance(Error);
			Require(Inner != nullptr, Error);
			++Counts.Created;
		}
		~FTestInstance() override
		{
			Factory->DestroyInstance(Inner);
			++Counts.Destroyed;
		}
		bool Bind(const void* Context, const FBytecodeModule* Program, std::uint64_t Generation, std::string& Error) override
		{
			++Counts.Bound;
			return Inner->Bind(Context, Program, Generation, Error);
		}
		void Unbind() noexcept override
		{
			Inner->Unbind();
		}
		bool SupportsIdleRebinding() const noexcept override
		{
			return Behavior != EBehavior::NoRebind;
		}
		EVmExecutionResult Execute(FVmInvocation& Call) override
		{
			if (Behavior == EBehavior::UnknownResult)
			{
				Call.bEntered = true;
				return static_cast<EVmExecutionResult>(255);
			}
			if (Behavior == EBehavior::Decline)
			{
				++Counts.Declined;
				return EVmExecutionResult::NotEntered;
			}
			if (Behavior == EBehavior::DeclineOnce && Counts.Declined++ == 0) return EVmExecutionResult::NotEntered;
			if (Behavior == EBehavior::DeclinePending &&
			    (bObservedPendingTransfer || Call.State.GetPendingExceptionFlag() || Call.State.GetErrorFlag()))
			{
				++Counts.Declined;
				return EVmExecutionResult::NotEntered;
			}
			if (Behavior == EBehavior::EnterThenDecline)
			{
				Call.bEntered = true;
				return EVmExecutionResult::NotEntered;
			}
			if (Behavior == EBehavior::Throw)
			{
				Call.bEntered = true;
				throw std::runtime_error("test backend exception");
			}
			if (Behavior == EBehavior::WrongBoundary)
			{
				Call.bEntered = true;
				Call.State.GetFrames().emplace_back();
				return EVmExecutionResult::Returned;
			}
			const auto Result = Inner->Execute(Call);
			// Core may park the transfer in an owning cleanup frame before entering
			// a destructor. Remember the observed transfer rather than depending on
			// its temporary residence in the VM's pending-exception slot.
			if (Result == EVmExecutionResult::ControlTransfer &&
			    (Call.State.GetPendingExceptionFlag() || Call.State.GetErrorFlag()))
			{
				bObservedPendingTransfer = true;
			}
			if (Behavior == EBehavior::ObserveFramePool)
			{
				for (const auto& Frame : Call.State.GetFreeFrames())
				{
					Counts.MaxPooledLocalCapacity = (std::max)(Counts.MaxPooledLocalCapacity, Frame.Locals.capacity());
				}
			}
			return Result;
		}

	private:
		std::shared_ptr<const IVmBackendFactory> Factory;
		IVmBackendInstance* Inner = nullptr;
		EBehavior Behavior;
		FCounts& Counts;
		bool bObservedPendingTransfer = false;
	};
	class FTestFactory final : public IVmBackendFactory
	{
	public:
		FTestFactory(EBehavior InBehavior, FCounts& InCounts)
		    : Inner(BytecodeVm::CreateFactory(GetVmHostServices())), Behavior(InBehavior), Counts(InCounts)
		{
			Description = Inner->GetDescription();
			if (Behavior == EBehavior::NoCleanup)
			{
				Description.Capabilities &= ~VmCleanupCapability;
			}
		}
		const FVmBackendDescription& GetDescription() const noexcept override
		{
			return Description;
		}
		const void* GetHostIdentity() const noexcept override
		{
			return Inner->GetHostIdentity();
		}
		IVmBackendInstance* CreateInstance(std::string&) const override
		{
			return new FTestInstance(Inner, Behavior, Counts);
		}
		void DestroyInstance(IVmBackendInstance* Instance) const noexcept override
		{
			delete Instance;
		}

	private:
		std::shared_ptr<const IVmBackendFactory> Inner;
		EBehavior Behavior;
		FCounts& Counts;
		FVmBackendDescription Description;
	};
	void CheckResult(EBehavior Behavior, bool Reset, bool Frozen)
	{
		FCounts Counts;
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Vm.SetExecutionBackend(EExecutionBackend::Vm);
		std::string Error;
		Require(Vm.SetVmBackendFactory(std::make_shared<FTestFactory>(Behavior, Counts), Error), Error);
		auto Code = VeroScriptCompile::CompileSource("int main(){return 42;}");
		Require(Code.bSuccess, Code.Error.Message);
		Require(!Vm.Execute(*Code.Module, ERunMode::Script), "invalid backend result succeeded");
		Require(!Vm.GetError().Message.empty(), "backend failure has no diagnostic");
		Require(Vm.RequiresReset() == Reset, "incorrect reset requirement");
		Require(Vm.GetVmExecutionDomain()->IsFrozen() == Frozen, "incorrect selection commitment");
	}
}

int main(int Argc, char** Argv)
{
	try
	{
		CheckResult(EBehavior::Decline, false, false);
		CheckResult(EBehavior::EnterThenDecline, true, true);
		CheckResult(EBehavior::Throw, true, true);
		CheckResult(EBehavior::WrongBoundary, true, true);
		CheckResult(EBehavior::UnknownResult, true, true);
		{
			FCounts Counts;
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			Vm.SetExecutionBackend(EExecutionBackend::Vm);
			std::string Error;
			Require(Vm.SetVmBackendFactory(std::make_shared<FTestFactory>(EBehavior::ObserveFramePool, Counts), Error), Error);
			auto Code = VeroScriptCompile::CompileSource(
			    "int wide(int n){if(n==0)return 73;return wide(n-1);} int small(int value){return value+1;}",
			    "frame-pool.vs");
			Require(Code.bSuccess, Code.Error.Message);
			// Unused local slots exercise a wide recursive frame without generating
			// thousands of unrelated source declarations for this test.
			Code.Module->Functions.at(0).LocalCount = 32768;
			FValue Value;
			Require(Vm.ExecuteFunction(*Code.Module, 0, {std::int32_t{7}}, nullptr, &Value), Vm.GetError().Message);
			Require(std::get<std::int32_t>(Value) == 73, "wide recursion returned an incorrect value");
			Require(Counts.MaxPooledLocalCapacity <= 4096, "wide local allocation was retained by the idle VM");
			Require(Vm.ExecuteFunction(*Code.Module, 1, {std::int32_t{80}}, nullptr, &Value), Vm.GetError().Message);
			Require(std::get<std::int32_t>(Value) == 81 && Counts.MaxPooledLocalCapacity != 0,
			    "small calls must still recycle their local storage");
		}
		for (const auto Behavior : {EBehavior::DeclineOnce, EBehavior::Decline})
		{
			FCounts Counts;
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			Vm.SetExecutionBackend(EExecutionBackend::Vm);
			std::string Failure;
			Require(Vm.SetVmBackendFactory(std::make_shared<FTestFactory>(Behavior, Counts), Failure), Failure);
			auto Code = VeroScriptCompile::CompileSource(
			    "int work(){try{return 42;}finally{print(999);}}", "declined-body.vs");
			Require(Code.bSuccess, Code.Error.Message);
			FValue Returned;
			Require(!Vm.ExecuteFunction(*Code.Module, 0, {}, nullptr, &Returned), "declined body unexpectedly succeeded");
			Require(Output.str().empty(), "a function that was never entered ran its finally");
			Require(Counts.Declined == 1, "declined body was retried through its finally");
			Require(!Vm.RequiresReset() && !Vm.GetVmExecutionDomain()->IsFrozen(),
			    "unentered function committed or invalidated the execution domain");
			if (Behavior == EBehavior::DeclineOnce)
			{
				Require(Vm.ExecuteFunction(*Code.Module, 0, {}, nullptr, &Returned), Vm.GetError().Message);
				Require(std::get<std::int32_t>(Returned) == 42 && Output.str().find("999") != std::string::npos,
				    "successful retry did not retain normal finally behavior");
			}
		}
		{
			std::ostringstream PollOutput;
			VeroScriptVm PollVm(PollOutput);
			std::string Failure;
			Require(PollVm.SetVmBackendFactory(BytecodeVm::CreateFactory(GetVmHostServices()), Failure), Failure);
			PollVm.SetExecutionBackend(EExecutionBackend::Vm);
			unsigned EnteredPolls = 0;
			PollVm.SetExecutionCheck([&]()
			{
				if (PollVm.GetVmBackendStats().Entries != 0 && ++EnteredPolls == 3)
				{
					throw std::runtime_error("Core-owned execution poll failed");
				}
				return true;
			});
			auto Code = VeroScriptCompile::CompileSource("void main(){while(true){}}", "host-poll.vs");
			Require(Code.bSuccess, Code.Error.Message);
			bool CoreException = false;
			try
			{
				(void)PollVm.Execute(*Code.Module, ERunMode::Script);
			}
			catch (const std::runtime_error& Exception)
			{
				CoreException = std::string(Exception.what()) == "Core-owned execution poll failed";
			}
			Require(CoreException && EnteredPolls >= 3, "host poll exception did not retain its Core behavior");
			Require(PollVm.GetError().MessageId != "diag.vm_backend_failed", "Core poll exception was misclassified as a backend failure");
		}
		{
			FCounts PendingCounts;
			std::ostringstream Stream;
			VeroScriptVm Vm(Stream);
			std::string Failure;
			Require(Vm.SetVmBackendFactory(std::make_shared<FTestFactory>(EBehavior::DeclinePending, PendingCounts), Failure), Failure);
			auto Code = VeroScriptCompile::CompileSource(R"(
struct Token{int value;Token(int input){value=input;}~Token(){print("field{}",value);}};
class Ancestor{public:Token root=Token(10);Ancestor(){}~Ancestor(){print("R{}",root.value);}};
class A:Ancestor{public:Token left=Token(20);A(){}~A(){print("A{}",left.value);}};
class B{public:Token right=Token(30);B(){}~B(){print("B{}",right.value);throw 55;}};
class D:A,B{public:Token own=Token(40);D(){throw 9;}~D(){print("D{}",own.value);}};
void work(){D value=D();} void main(){try{work();}catch(int){}}
)");
			Require(Code.bSuccess, Code.Error.Message);
			Require(!Vm.Execute(*Code.Module, ERunMode::Script), "pending call unexpectedly completed");
			Require(PendingCounts.Declined > 0, "pending transfer no-entry scenario was not exercised");
			Require(!Vm.RequiresReset(), "unchanged pending transfer was misclassified as invalid backend state");
		}
		const auto Factory = BytecodeVm::CreateFactory(GetVmHostServices());
		std::ostringstream Output;
		FTestHost Host;
		VeroScriptSession Session(Host);
		std::string Error;
		Require(Session.SetVmBackendFactory(Factory, Error), Error);
		auto Loaded = Session.LoadSource("int value=0; int next(){return ++value;}", "domain.vs");
		Require(Loaded.bSuccess, Loaded.Error.Message);
		FValue Result;
		auto Called = Session.CallFunction("next", {}, &Result);
		Require(Called.bSuccess, Called.Error.Message);
		Require(Session.GetVmExecutionDomain()->IsFrozen(), "Session domain did not freeze");
		Session.Clear();
		Require(!Session.SetVmBackendFactory({}, Error), "Clear thawed the Session domain");
		Loaded = Session.LoadSource("int next(){return 9;}", "reloaded.vs");
		Require(Loaded.bSuccess, Loaded.Error.Message);
		Called = Session.CallFunction("next", {}, &Result);
		Require(Called.bSuccess, Called.Error.Message);
		Require(std::get<std::int32_t>(Result) == 9, "reload used stale program binding");

		FCounts Counts;
		{
			VeroScriptVm Vm(Output);
			Require(Vm.SetVmBackendFactory(std::make_shared<FTestFactory>(EBehavior::NoRebind, Counts), Error), Error);
			for (int I = 0; I < 3; ++I)
			{
				auto Code = VeroScriptCompile::CompileSource("int main(){return 2;}");
				Require(Code.bSuccess && Vm.Execute(*Code.Module, ERunMode::Script), Vm.GetError().Message);
			}
		}
		Require(Counts.Created == Counts.Destroyed && Counts.Created >= 3, "non-rebindable instance was retained");
		{
			VeroScriptVm Vm(Output);
			Require(Vm.SetVmBackendFactory(std::make_shared<FTestFactory>(EBehavior::NoCleanup, Counts), Error), Error);
			Vm.SetExecutionLimits({20, 0});
			auto Code = VeroScriptCompile::CompileSource("void main(){try {while(true){}} finally {print(1);}}");
			Require(Code.bSuccess, Code.Error.Message);
			Require(!Vm.Execute(*Code.Module, ERunMode::Script) && Vm.RequiresReset(), "unavailable cleanup did not require reset");
			Require(Vm.GetError().MessageId == "diag.runtime_execution_instruction_limit_exceeded", "cleanup replaced the termination error");
		}

		if (Argc > 1)
		{
			std::string Path = Argv[1];
			Require(LoadVmBackend(Path, Error, &Path) != nullptr, Error);
			Error = Path;
			Require(LoadVmBackend(Error, Error) != nullptr, Error);
			VeroScriptVm AliasVm(Output);
			Error = Path;
			Require(AliasVm.SetVmBackendPath(Error, Error), Error);
			for (const auto& Selection : {Path, PathUtil::ToUtf8(PathUtil::FromUtf8(Path).parent_path())})
			{
				FTestHost ConfigHost;
				VeroScriptSession ConfigSession(ConfigHost);
				FEngineConfig Config;
				Config.VmBackendPath = Selection;
				ConfigSession.ApplyConfig(Config);
				auto ConfigLoaded = ConfigSession.LoadSource("int next(){return 19;}", "config.vs");
				Require(ConfigLoaded.bSuccess, ConfigLoaded.Error.Message);
				auto ConfigCalled = ConfigSession.CallFunction("next", {}, &Result);
				Require(ConfigCalled.bSuccess && std::get<std::int32_t>(Result) == 19,
				    "VM path configuration did not execute");
				const auto Selected = ConfigSession.GetVmExecutionDomain()->GetFactory();
				Config.ExecutionLimits.MaxInstructions = 100;
				ConfigSession.ApplyConfig(Config);
				Require(ConfigSession.GetExecutionLimits().MaxInstructions == 100,
				    "identical VM path rejected an unrelated configuration update");
				Require(ConfigSession.GetVmExecutionDomain()->GetFactory() == Selected && ConfigSession.GetVmExecutionDomain()->IsFrozen(), "identical path changed the execution domain");
				Config.VmBackendPath = "missing-other-vm";
				Config.ExecutionLimits.MaxInstructions = 200;
				ConfigSession.ApplyConfig(Config);
				Require(ConfigSession.GetExecutionLimits().MaxInstructions == 100,
				    "changed frozen VM path partially applied the configuration");
			}
		}
		if (Argc > 2)
		{
			VeroScriptVm Vm(Output);
			Vm.SetExecutionBackend(EExecutionBackend::Jit);
			Require(Vm.SetJitBackendPath(Argv[2], Error), Error);
			auto Code = VeroScriptCompile::CompileSource("int value(){return 37;}");
			Require(Code.bSuccess, Code.Error.Message);
			std::shared_ptr<const FBytecodeModule> Owner = std::move(Code.Module);
			Require(Vm.SetJitProgramOwner(Owner, Error), Error);
			FValue Returned;
			Require(Vm.ExecuteFunction(*Owner, 0, {}, nullptr, &Returned), Vm.GetError().Message);
			Require(std::get<std::int32_t>(Returned) == 37, "closed JIT-only result");
			Require(Vm.GetVmExecutionDomain()->IsFrozen(), "JIT did not freeze the empty VM selection");
			Require(!Vm.SetVmBackendFactory(Factory, Error), "JIT-only domain accepted a later VM");
		}
		std::cout << "VM contract, reset, freeze, rebind and alias checks passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
