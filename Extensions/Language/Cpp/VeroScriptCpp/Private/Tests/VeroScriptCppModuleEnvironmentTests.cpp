// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"
#include "VeroScriptSession.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptBytecode.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	int Failures = 0;
	void Expect(bool Condition, const char* Message)
	{
		if (!Condition)
		{
			++Failures;
			std::cerr << "FAIL: " << Message << '\n';
		}
	}
	bool Answer(const FNativeCallContext& Context, FValue& Out, std::string&)
	{
		Out = *static_cast<const std::int32_t*>(Context.UserData);
		return true;
	}
	constexpr const char* Entry = "veroscript-module:/app/main.vs";
	constexpr const char* Source = "export int entry(){return native_answer();} int main(){return entry();}";
	std::shared_ptr<const FModuleEnvironment> Environment(
		const std::shared_ptr<const IHostBindingCatalog>& Bindings = {})
	{
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "app";
		Description.Scopes = {{"app", {"native"}}, {"native", {}}};
		Description.Modules = {{"app.vs", "app", Entry, "app", "cpp.test.source.1", std::string(Source)}};
		Description.Bindings = Bindings;
		Description.AvailableDependencies = {{"native", "1", "cpp.test.abi", "cpp.test.build", "cpp.test.content"}};
		std::string Error;
		auto Result = FModuleEnvironment::Create(std::move(Description), Error);
		if (!Result)
		{
			++Failures;
			std::cerr << "FAIL: environment: " << Error << '\n';
		}
		return Result;
	}
	void TestBackendDefaults()
	{
		std::ostringstream Output, Diagnostics;
		FStreamEngineHost Host(Output, Diagnostics);
		std::string Error;
		int VmQueries = 0, JitQueries = 0;
		VeroScriptSession* ReentrySession = nullptr;
		bool bReentered = false;
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "app";
		Description.Scopes = {{"app", {}}};
		Description.Modules = {
			{"defaults.vs", "defaults", "veroscript-module:/app/defaults.vs", "app", "defaults.1", "int main(){return 7;}"},
			{"explicit-auto.vs", "explicit-auto", "veroscript-module:/app/explicit-auto.vs", "app", "defaults.2", "int main(){return 8;}"},
			{"host.vs", "host", "veroscript-module:/app/host.vs", "app", "defaults.3", "int main(){return 9;}"}};
		Description.ExecutionBackends.Identity = "backend-defaults";
		Description.ExecutionBackends.Vm = [&](std::string_view Request, auto& Factory, auto& Failure)
		{
			++VmQueries;
			if (Request == "missing")
			{
				Failure = "requested VM is unavailable";
				return false;
			}
			Factory = VeroScript::Test::VmFactory();
			return true;
		};
		Description.ExecutionBackends.Jit = [&](std::string_view Request, auto& Factory, auto& Failure)
		{
			++JitQueries;
			Factory.reset();
			if (Request == "probe")
			{
				bReentered = ReentrySession->Run().bSuccess;
				Failure = "probe is unavailable";
				return false;
			}
			if (Request == "missing")
			{
				Failure = "requested JIT is unavailable";
				return false;
			}
			return true;
		};
		FEngineConfig Config;
		Config.ModuleEnvironment = FModuleEnvironment::Create(Description, Error);
		if (!Config.ModuleEnvironment)
		{
			Expect(false, "create backend-default environment");
			std::cerr << Error << '\n';
			return;
		}
		VeroScriptSession Session(Host);
		Expect(Session.SetFallbackVmBackendFactory(VeroScript::Test::VmFactory(), Error), "assemble product VM fallback");
		Session.ApplyConfig(Config);
		Expect(VmQueries == 1 && JitQueries == 1, "package defaults take priority over product fallback");
		const auto Loaded = Session.LoadSource("int main(){return 7;}", "veroscript-module:/app/defaults.vs");
		const auto VmQueriesBeforeExecution = VmQueries;
		Expect(Loaded.bSuccess && Session.Run().ExitCode == 7 && Session.Run().ExitCode == 7,
			"automatic Session backend executes normally");
		Expect(VmQueries == VmQueriesBeforeExecution && JitQueries == 1,
			"execution reuses the VM selection and the resolved absence of an optional JIT");
		const auto JitChanged = Session.SetJitBackendPath("missing", Error);
		Expect(!JitChanged && Error == "requested JIT is unavailable"
			&& Session.Run().ExitCode == 7, "failed explicit JIT selection preserves the Session");
		const auto VmChanged = Session.SetVmBackendPath("missing", Error);
		Expect(!VmChanged && Error == "requested VM is unavailable"
			&& Session.Run().ExitCode == 7, "failed explicit VM selection preserves the Session");
		ReentrySession = &Session;
		Expect(!Session.SetJitBackendPath("probe", Error) && !bReentered && Session.Run().ExitCode == 7,
			"backend-selection callbacks cannot reenter the Session");

		Description.SelectedBackends = {{"vm", "none"}, {"jit", "none"}};
		Config.ModuleEnvironment = FModuleEnvironment::Create(Description, Error);
		VeroScriptSession Disabled(Host);
		Expect(Disabled.SetFallbackVmBackendFactory(VeroScript::Test::VmFactory(), Error), "assemble disabled product VM fallback");
		const auto PreviousVmQueries = VmQueries, PreviousJitQueries = JitQueries;
		Disabled.ApplyConfig(Config);
		Expect(VmQueries == PreviousVmQueries && JitQueries == PreviousJitQueries, "disabled services do not query defaults");
		Expect(Disabled.SetVmBackendPath("auto", Error) && Disabled.SetJitBackendPath("auto", Error),
			"explicit auto can override disabled environment defaults");
		Expect(Disabled.LoadSource("int main(){return 8;}", "veroscript-module:/app/explicit-auto.vs").bSuccess
			&& Disabled.Run().ExitCode == 8, "explicit automatic VM remains selected");

		FEngineConfig Explicit;
		Explicit.ModuleEnvironment = Config.ModuleEnvironment;
		Explicit.VmBackendFactory = VeroScript::Test::VmFactory();
		const auto Direct = VeroScript::Test::FEngine::RunSource("int main(){return 9;}", "veroscript-module:/app/host.vs", Host, ERunMode::Script, Explicit);
		Expect(Direct.bSuccess && Direct.ExitCode == 9, "explicit host VM overrides disabled environment default");

		Description.SelectedBackends = {{"jit", "missing"}};
		Description.ExecutionBackends.Jit = [](std::string_view Request, auto& Factory, std::string& Failure)
		{
			Factory.reset();
			if (Request == AutomaticBackend) return true;
			Failure = "configured JIT is unavailable";
			return false;
		};
		Config.ModuleEnvironment = FModuleEnvironment::Create(Description, Error);
		VeroScriptSession Override(Host);
		Expect(Override.SetFallbackVmBackendFactory(VeroScript::Test::VmFactory(), Error)
			&& Override.SetJitBackendPath("auto", Error), "select auto before applying the environment");
		Override.ApplyConfig(Config);
		Expect(Override.LoadSource("int main(){return 7;}", "veroscript-module:/app/defaults.vs").bSuccess
			&& Override.Run().ExitCode == 7, "explicit optional auto overrides an unavailable project default");
		Override.SetExecutionBackend(EExecutionBackend::Vm);
		Expect(Override.Run().ExitCode == 7, "automatic selection preserves VM-only policy");
		Override.SetExecutionBackend(EExecutionBackend::Mixed);
		Expect(Override.Run().ExitCode == 7, "explicit optional auto survives execution-policy changes");
		Expect(Override.SetJitBackendPath("none", Error), "disable JIT after automatic selection");
		Override.ApplyConfig(Config);
		Expect(Override.Run().ExitCode == 7, "explicit disable survives configuration reapplication");
	}

	void TestEnvironmentBindings()
	{
		std::int32_t Value = 42;
		auto Catalog = std::make_shared<FHostBindingRegistry>();
		Catalog->SetRegistrationOwner({}, {"native", {}, true});
		std::string Error;
		Expect(Catalog->RegisterNative("native_answer", ENativeType::Int, {}, Answer, &Value, Error), "register environment native");
		Catalog->SetRegistrationOwner({});
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.ModuleEnvironment = Environment(Catalog);
		if (!Config.ModuleEnvironment) return;
		std::ostringstream Output, Diagnostics;
		FStreamEngineHost Host(Output, Diagnostics);
		Expect(VeroScript::Test::FEngine::CheckSource(Source, Entry, Host, Config).bSuccess, "Engine CheckSource uses the environment catalog");
		Expect(VeroScript::Test::FEngine::CheckFile(Entry, Host, Config).bSuccess, "Engine CheckFile resolves memory modules");
		const auto Run = VeroScript::Test::FEngine::RunSource(Source, Entry, Host, ERunMode::Script, Config);
		Expect(Run.bSuccess && Run.ExitCode == Value, "Engine RunSource uses the environment native callbacks");
		const auto FileRun = VeroScript::Test::FEngine::RunFile(Entry, Host, Config);
		Expect(FileRun.bSuccess && FileRun.ExitCode == Value, "Engine RunFile uses the environment native callbacks");

		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.ApplyConfig(Config);
		Expect(&Session.GetBindingCatalog() == Catalog.get(), "Session exposes the effective immutable catalog");
		Expect(!Session.RegisterNative("unreachable", ENativeType::Int, {}, Answer, &Value), "environment Session rejects hidden local registration");
		FHostTypeId UnusedType;
		Expect(!Session.RegisterHostType("UnreachableType", EHostTypeKind::Class, &UnusedType), "environment Session rejects hidden local type registration");
		bool RejectedRegistry = false;
		try
		{
			(void)Session.GetBindingRegistry();
		}
		catch (const std::logic_error&)
		{
			RejectedRegistry = true;
		}
		Expect(RejectedRegistry, "published environment cannot expose a mutable shadow registry");
		RejectedRegistry = false;
		try
		{
			(void)static_cast<const VeroScriptSession&>(Session).GetBindingRegistry();
		}
		catch (const std::logic_error&)
		{
			RejectedRegistry = true;
		}
		Expect(RejectedRegistry, "const registry lookup must not return a hidden catalog");
		Expect(Session.LoadFile(Entry).bSuccess, "Session LoadFile resolves environment memory modules");
		FValue Result;
		Expect(Session.CallFunction("entry", {}, &Result).bSuccess
			&& std::get<std::int32_t>(Result) == Value, "Session CallFunction uses the environment native callbacks");
		Expect(Session.GetDebugProgram().Bindings == Catalog.get(), "debug view uses the effective catalog");
		Expect(Session.GetModuleEnvironment() == Config.ModuleEnvironment
			&& Session.GetDebugProgram().ModuleEnvironment == Config.ModuleEnvironment.get()
			&& Session.GetDebugProgram().ModuleEnvironment->FindSource(Entry)->Source == Source,
			"debug views expose the snapshot containing mapped memory sources");
		const auto SessionRun = Session.Run();
		Expect(SessionRun.bSuccess && SessionRun.ExitCode == Value, "Session Run uses the environment native callbacks");
		for (const std::uint64_t Budget : {std::uint64_t{4096}, std::uint64_t{0}, std::uint64_t{4096}})
		{
			Session.SetExecutionLimits({Budget, 0});
			Expect(Session.CallFunction("entry", {}, &Result).bSuccess
				&& std::get<std::int32_t>(Result) == Value && Session.Run().ExitCode == Value,
				"environment native snapshots survive quota policy changes");
		}
		auto Control = std::make_shared<FExecutionControl>();
		Session.SetExecutionControl(Control);
		Expect(Session.CallFunction("entry", {}, &Result).bSuccess
			&& std::get<std::int32_t>(Result) == Value, "environment native snapshots accept a live cancellation token");
		Control->RequestCancel();
		Expect(!Session.CallFunction("entry", {}, &Result).bSuccess, "cached environment natives do not bypass cancellation");
		Control->Reset();
		Expect(Session.CallFunction("entry", {}, &Result).bSuccess
			&& std::get<std::int32_t>(Result) == Value, "environment native snapshots remain usable after cancellation reset");
		Session.SetExecutionControl({});
		Session.SetExecutionLimits({});
		const auto PreviousProgram = Session.GetDebugProgram();
		Session.ApplyConfig({});
		Expect(Session.GetDebugProgram().Module == PreviousProgram.Module
			&& &Session.GetBindingCatalog() == Catalog.get(), "changing a loaded environment preserves program and catalog");
		Session.Clear();
		std::int32_t ReplacementValue = 73;
		auto ReplacementCatalog = std::make_shared<FHostBindingRegistry>();
		ReplacementCatalog->SetRegistrationOwner({}, {"native", {}, true});
		Expect(ReplacementCatalog->RegisterNative("native_answer", ENativeType::Int, {}, Answer, &ReplacementValue, Error),
			"register replacement environment native");
		ReplacementCatalog->SetRegistrationOwner({});
		auto ReplacementConfig = Config;
		ReplacementConfig.ModuleEnvironment = Environment(ReplacementCatalog);
		Session.ApplyConfig(ReplacementConfig);
		Expect(Session.GetModuleEnvironment() == ReplacementConfig.ModuleEnvironment
			&& &Session.GetBindingCatalog() == ReplacementCatalog.get(), "cleared Session adopts the replacement environment and catalog");
		const auto ReplacementLoad = Session.LoadFile(Entry);
		Expect(ReplacementLoad.bSuccess, "load entry under the replacement environment");
		if (ReplacementLoad.bSuccess)
		{
			const auto ReplacementCall = Session.CallFunction("entry", {}, &Result);
			Expect(ReplacementCall.bSuccess && std::get<std::int32_t>(Result) == ReplacementValue,
				"replacement environment free call cannot reuse retired native snapshots");
			const auto ReplacementRun = Session.Run();
			Expect(ReplacementRun.bSuccess && ReplacementRun.ExitCode == ReplacementValue,
				"replacement environment Run cannot reuse retired native snapshots");
		}
		Session.Clear();
		Session.ApplyConfig({});
		Expect(Session.RegisterNative("native_answer", ENativeType::Int, {}, Answer, &Value), "Clear permits returning to mutable local registration");
		Expect(Session.LoadSource(Source, "local.vs").bSuccess && Session.Run().bSuccess, "local Session execution still works after environment removal");

		VeroScriptSession Local(Host);
		VeroScript::Test::Configure(Local);
		Expect(Local.RegisterNative("native_answer", ENativeType::Int, {}, Answer, &Value), "register local native before configuring an environment");
		Config.ExecutionLimits.MaxInstructions = 1;
		Local.ApplyConfig(Config);
		Expect(Local.GetBindingCatalog().GetNativeFunctions().size() == 1
			&& &Local.GetBindingCatalog() != Catalog.get() && Local.GetExecutionLimits().MaxInstructions == 0,
			"conflicting environment configuration must preserve the local registry and other settings");
		Expect(Local.LoadSource(Source, "local.vs").bSuccess && Local.Run().bSuccess, "rejected environment leaves local bindings usable");

		VeroScriptSession HostOnly(Host);
		VeroScript::Test::Configure(HostOnly);
		Config = {};
		Config.ModuleEnvironment = Environment();
		HostOnly.ApplyConfig(Config);
		Expect(HostOnly.RegisterNative("native_answer", ENativeType::Int, {}, Answer, &Value), "environment without a catalog allows explicit Session host bindings");
		Expect(HostOnly.LoadSource(Source, Entry).bSuccess && HostOnly.Run().bSuccess,
			"environment without a catalog preserves local native execution");
		if (Failures)
		{
			std::cerr << Diagnostics.str();
		}
	}
	void TestConfigurationFailureStopsExecution()
	{
		std::int32_t Calls = 0;
		auto Catalog = std::make_shared<FHostBindingRegistry>();
		std::string Error;
		Expect(Catalog->RegisterNative("native_answer", ENativeType::Int, {},
			[](const FNativeCallContext& Context, FValue& Out, std::string&)
			{
				++*static_cast<std::int32_t*>(Context.UserData);
				Out = std::int32_t{23};
				return true;
			}, &Calls, Error), "register configuration-failure business counter");
		std::ostringstream Output, Diagnostics;
		FStreamEngineHost Host(Output, Diagnostics);
		bool FailSelection = false;
		bool FailOnlyOnce = false;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.ModuleEnvironment = Environment(Catalog);
		Session.ApplyConfig(Config);
		Expect(Session.LoadFile(Entry).bSuccess && Session.Run().ExitCode == 23,
			"execute original environment before resolver failure");
		Session.Clear();
		auto Description = Config.ModuleEnvironment->GetDescription();
		Description.ExecutionBackends.Identity = "cpp.configuration-failure";
		Description.ExecutionBackends.Jit = [&](std::string_view, auto& Factory, std::string& Failure)
		{
			Factory.reset();
			if (FailSelection)
			{
				if (FailOnlyOnce)
				{
					FailSelection = false;
				}
				Failure = "intentional execution configuration failure";
				return false;
			}
			return true;
		};
		Config.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
		Expect(static_cast<bool>(Config.ModuleEnvironment), "create replacement resolver environment");
		if (!Config.ModuleEnvironment) return;
		Session.ApplyConfig(Config);
		Expect(Session.LoadFile(Entry).bSuccess, "load dependency-free resolver failure entry");
		Expect(Session.GetDebugProgram().Module && Session.GetDebugProgram().Module->RequiredExternalDependencies.empty(),
			"resolver failure fixture has no external dependency that can independently reject execution");
		const auto CallsBeforeFailure = Calls;
		FailSelection = true;
		Session.SetExecutionBackend(EExecutionBackend::Jit);
		auto Control = std::make_shared<FExecutionControl>();
		Control->RequestCancel();
		Session.SetExecutionControl(Control);
		Session.SetExecutionLimits({1, 0});
		const auto FailedRun = Session.Run();
		FValue Value;
		const auto FailedCall = Session.CallFunction("entry", {}, &Value);
		Expect(!FailedRun.bSuccess && !FailedCall.bSuccess && Calls == CallsBeforeFailure
			&& FailedRun.Error.Message == "intentional execution configuration failure"
			&& FailedCall.Error.Message == "intentional execution configuration failure",
			"configuration failure preserves its original error and cannot execute business code before governance is installed");
		FailSelection = false;
		Session.SetExecutionBackend(EExecutionBackend::Mixed);
		Session.SetExecutionControl({});
		Session.SetExecutionLimits({});
		Expect(Session.CallFunction("entry", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 23
			&& Calls == CallsBeforeFailure + 1 && !Session.RequiresReset(), "corrected configuration remains callable after a failed setup");
		const auto* PreviousModule = Session.GetDebugProgram().Module;
		std::unique_ptr<FBytecodeModule> Replacement;
		Expect(Session.CompileFile(Entry, Replacement).bSuccess && Session.ReloadCompiledModule(Replacement, Entry).bSuccess,
			"stage a replacement before execution configuration fails");
		const auto CallsBeforeReloadFailure = Calls;
		FailOnlyOnce = true;
		FailSelection = true;
		Session.SetExecutionBackend(EExecutionBackend::Jit);
		const auto FailedReload = Session.Run();
		Expect(!FailedReload.bSuccess && Calls == CallsBeforeReloadFailure
			&& FailedReload.Error.Message == "intentional execution configuration failure"
			&& !FailedReload.Diagnostics.empty()
			&& FailedReload.Diagnostics.front().Message == "intentional execution configuration failure",
			"pending replacement preserves the original configuration error and diagnostics");
		Expect(Session.GetDebugProgram().Module == PreviousModule && !Session.RequiresReset(),
			"pending replacement rolls back when the resolver recovers for cleanup");
		Session.SetExecutionBackend(EExecutionBackend::Mixed);
		Session.Clear();
	}
	void TestCleanupAfterBackendSelectionFailure()
	{
		const std::string Type = "class CleanupNode { public: CleanupNode next; ~CleanupNode(){native_cleanup();} }; ";
		for (const std::string& Source : {
			Type + "CleanupNode root=CleanupNode(); CleanupNode entry(){root.next=root;return root;}",
			Type + "CleanupNode entry(){static CleanupNode root=CleanupNode();root.next=root;return root;}"})
		{
			for (const std::string_view Action : {"destroy", "clear", "disable"})
			{
				std::int32_t Destructions = 0;
				bool FailSelection = false;
				std::string Error;
				auto Catalog = std::make_shared<FHostBindingRegistry>();
				Expect(Catalog->RegisterNative("native_cleanup", ENativeType::Void, {},
					[](const FNativeCallContext& Context, FValue&, std::string&)
					{
						++*static_cast<std::int32_t*>(Context.UserData);
						return true;
					}, &Destructions, Error), "register cleanup counter");
				FModuleEnvironmentDesc Description;
				Description.EntryOwnerId = "app";
				Description.Scopes = {{"app", {}}};
				Description.Modules = {{"cleanup.vs", "cleanup", Entry, "app", "cleanup.1", Source}};
				Description.Bindings = Catalog;
				Description.ExecutionBackends.Identity = "cpp.cleanup.configuration-failure";
				Description.ExecutionBackends.Jit = [&](std::string_view, auto& Factory, std::string& Failure)
				{
					Factory.reset();
					if (FailSelection)
					{
						Failure = "intentional cleanup backend selection failure";
						return false;
					}
					return true;
				};
				FEngineConfig Config;
				Config.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
				Expect(static_cast<bool>(Config.ModuleEnvironment), "create cleanup resolver environment");
				if (!Config.ModuleEnvironment) return;
				VeroScript::Test::Configure(Config);
				std::ostringstream Output, Diagnostics;
				FStreamEngineHost Host(Output, Diagnostics);
				FValueRoot Root;
				std::weak_ptr<FScriptObject> Weak;
				{
					VeroScriptSession Session(Host);
					Session.ApplyConfig(Config);
					Session.SetHotReloadEnabled(true);
					FValue Value;
					const bool Prepared = Session.LoadFile(Entry).bSuccess &&
						Session.CallFunction("entry", {}, &Value).bSuccess &&
						std::holds_alternative<FObjectHandle>(Value) &&
						Session.RetainValue(Value, Root, Error);
					Expect(Prepared, "prepare cyclic persistent and exported values before resolver failure");
					if (!Prepared) return;
					Weak = std::get<FObjectHandle>(Value);
					Value = FValue{};
					FailSelection = true;
					Session.SetExecutionBackend(EExecutionBackend::Jit);
					const auto Failed = Session.CallFunction("entry");
					Expect(!Failed.bSuccess && Failed.Error.Message == "intentional cleanup backend selection failure"
						&& Destructions == 0, "ordinary execution still rejects the unavailable backend before cleanup");
					if (Action != "destroy")
					{
						Root.Reset();
						if (Action == "clear")
						{
							Session.Clear();
							Expect(!Session.IsLoaded(), "Clear completes after backend selection fails");
						}
						else
						{
							Session.SetHotReloadEnabled(false);
						}
						Expect(Weak.expired() && Destructions == 1,
							"explicit cleanup releases cyclic globals and local statics using the established VM");
					}
				}
				Expect(Weak.expired() && Destructions == 1,
					"Session teardown releases cycles and runs their script destructor exactly once after resolver failure");
			}
		}
	}
}

int main()
{
	TestBackendDefaults();
	TestEnvironmentBindings();
	TestConfigurationFailureStopsExecution();
	TestCleanupAfterBackendSelectionFailure();
	return Failures ? 1 : 0;
}
