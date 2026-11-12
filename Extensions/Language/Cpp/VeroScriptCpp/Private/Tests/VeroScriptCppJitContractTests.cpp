// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"
#include "VeroScriptHost.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"
#include "VeroScriptModuleEnvironment.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

namespace
{
	using namespace VeroScript;

	class FTemporaryDirectory final
	{
	public:
		FTemporaryDirectory()
			: Path(std::filesystem::temp_directory_path() / ("veroscript_cpp_jit_contract_"
				+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
		{
			std::filesystem::create_directories(Path);
		}
		~FTemporaryDirectory()
		{
			std::error_code Ignored;
			std::filesystem::remove_all(Path, Ignored);
		}
		const std::filesystem::path Path;
	};

	struct FProbe
	{
		std::atomic<std::size_t> Instances{0};
		std::atomic<std::size_t> Entries{0};
		std::atomic<std::size_t> OwnedPrograms{0};
	};

	class FDebugBridge final : public ISessionDebugBridge
	{
	public:
		//~ Begin ISessionDebugBridge Interface
		void ProgramChanged(const FSessionDebugProgramView &) noexcept override
		{
		}
		std::shared_ptr<VeroScriptDebugger> BeginExecution(
			const FSessionDebugExecutionContext &) noexcept override
		{
			++Begins;
			if (!bConnected) return nullptr;
			Debugger->SetRunMode(EDebugRunMode::StepIn);
			return Debugger;
		}
		void EndExecution() noexcept override
		{
			++Ends;
			bDetachedVm &= Debugger->GetStackTrace().empty();
		}
		void Detached() noexcept override
		{
			++Detaches;
		}
		//~ End ISessionDebugBridge Interface
		std::shared_ptr<VeroScriptDebugger> Debugger = std::make_shared<VeroScriptDebugger>();
		std::size_t Begins = 0, Ends = 0, Detaches = 0;
		bool bConnected = true, bDetachedVm = true;
	};

	class FExecutable final : public IJitExecutable
	{
	public:
		//~ Begin IJitExecutable Interface
		EJitInvocationResult Invoke(FJitCallContext& Context) const override
		{
			Context.MarkEntered();
			++Probe->Entries;
			Context.ReturnValue = std::int32_t{42};
			Context.bHasReturn = true;
			return EJitInvocationResult::Returned;
		}
		//~ End IJitExecutable Interface

		explicit FExecutable(std::shared_ptr<FProbe> InProbe) : Probe(std::move(InProbe))
		{
		}

	private:
		std::shared_ptr<FProbe> Probe;
	};

	class FInstance final : public IJitBackendInstance
	{
	public:
		//~ Begin IJitBackendInstance Interface
		const FJitBackendDescription& GetDescription() const noexcept override
		{
			return Description;
		}

		EJitCompilationResult Compile(const FJitCompileRequest& Request, FJitCompileResponse& Response) override
		{
			if (Request.Program.Module->Functions[Request.FunctionIndex].Name.find("backend_target#") == std::string::npos)
			{
				return EJitCompilationResult::Unsupported;
			}
			auto Function = std::make_shared<FJitCompiledFunction>();
			Function->FunctionIndex = Request.FunctionIndex;
			Function->Identity = Request.Program.Identity;
			Function->ProgramOwner = Request.Program.Owner;
			Function->Executable = std::make_shared<FExecutable>(Probe);
			Response.Function = std::move(Function);
			return EJitCompilationResult::Ready;
		}

		FJitResourceUsage GetResourceUsage() const noexcept override
		{
			return {};
		}

		void Retire() noexcept override
		{
		}
		//~ End IJitBackendInstance Interface

		FInstance(std::shared_ptr<FProbe> InProbe, FJitBackendDescription InDescription)
			: Probe(std::move(InProbe)), Description(std::move(InDescription))
		{
		}

	private:
		std::shared_ptr<FProbe> Probe;
		FJitBackendDescription Description;
	};

	class FFactory final : public IJitBackendFactory
	{
	public:
		//~ Begin IJitBackendFactory Interface
		const FJitBackendDescription& GetDescription() const noexcept override
		{
			return Description;
		}

		std::unique_ptr<IJitBackendInstance> CreateInstance(const FJitProgramView& Program,
			const FJitResourceLimits&, std::string&) const override
		{
			++Probe->Instances;
			if (Program.Owner)
			{
				++Probe->OwnedPrograms;
			}
			return std::make_unique<FInstance>(Probe, Description);
		}
		//~ End IJitBackendFactory Interface

		explicit FFactory(std::shared_ptr<FProbe> InProbe) : Probe(std::move(InProbe))
		{
			Description.Id = "cpp-contract-probe";
			Description.DisplayName = "Public C++ contract probe";
			Description.Target = "portable-cpp";
		}

	private:
		std::shared_ptr<FProbe> Probe;
		FJitBackendDescription Description;
	};

	bool Expect(const bool Condition, const char* const Message, const std::ostringstream& Diagnostics)
	{
		if (!Condition)
		{
			std::cerr << Message << '\n' << Diagnostics.str();
		}
		return Condition;
	}
}

int main()
{
	std::ostringstream Output;
	std::ostringstream Diagnostics;
	FStreamEngineHost Host(Output, Diagnostics);
	auto Probe = std::make_shared<FProbe>();
	auto Factory = std::make_shared<FFactory>(Probe);
	FEngineConfig Config;
	VeroScript::Test::Configure(Config);
	Config.ExecutionBackend = EExecutionBackend::Jit;
	Config.JitBackendFactory = Factory;
	const std::string Source = "int backend_target() { return 99; }\n"
		"void main() { print(backend_target()); }\n";
	if (!Expect(VeroScript::Test::FEngine::RunSource(Source, "<jit-config>", Host, ERunMode::Script, Config).bSuccess && Output.str() == "42\n" && Probe->Entries == 1, "Engine lost its configured backend", Diagnostics))
	{
		return 1;
	}
	Output.str({});
	if (!Expect(VeroScript::Test::FEngine::RunSource(Source, "<jit-stream>", Output, Diagnostics,
											ERunMode::Script, {}, {}, nullptr, {}, {}, EExecutionBackend::Jit,
											{}, {}, true, false, Factory)
						.bSuccess &&
					Output.str() == "42\n" && Probe->Entries == 2,
				"Stream source entry lost its explicitly selected backend", Diagnostics))
	{
		return 1;
	}
	const FTemporaryDirectory Temporary;
	const auto SourcePath = Temporary.Path / "stream.vs";
	{
		std::ofstream File(SourcePath, std::ios::binary);
		File << Source;
		if (!Expect(static_cast<bool>(File), "Could not write stream file fixture", Diagnostics)) return 1;
	}
	Output.str({});
	if (!Expect(VeroScript::Test::FEngine::RunFile(PathUtil::ToUtf8(SourcePath), Output, Diagnostics,
										  {}, {}, false, nullptr, {}, {}, {}, EExecutionBackend::Jit, nullptr,
										  {}, {}, true, false, Factory)
						.bSuccess &&
					Output.str() == "42\n" && Probe->Entries == 3,
				"Stream file entry lost its explicitly selected backend", Diagnostics))
	{
		return 1;
	}
	Output.str({});
	if (!Expect(VeroScript::Test::FEngine::RunSource(Source, "<mixed-stream>", Output, Diagnostics).bSuccess && Output.str() == "99\n" && !VeroScript::Test::FEngine::RunSource(Source, "<jit-no-factory>", Output, Diagnostics, ERunMode::Script, {}, {}, nullptr, {}, {}, EExecutionBackend::Jit).bSuccess,
				"Stream entry changed its default or missing-backend policy", Diagnostics))
	{
		return 1;
	}

	Output.str({});
	Diagnostics.str({});
	VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
	{
		auto Catalog = std::make_shared<FHostBindingRegistry>();
		std::string Error;
		if (!Catalog->RegisterNative(
				"environment_native", ENativeType::Int, {},
				[](const FNativeCallContext&, FValue& Out, std::string&)
				{
					Out = std::int32_t{0};
					return true;
				},
				nullptr, Error))
		{
			return 1;
		}

		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "app";
		Description.Scopes = {{"app", {}}};
		Description.Bindings = Catalog;
		auto EnvironmentConfig = Config;
		auto EnvironmentProbe = std::make_shared<FProbe>();
		EnvironmentConfig.JitBackendFactory = std::make_shared<FFactory>(EnvironmentProbe);
		EnvironmentConfig.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
		VeroScriptSession EnvironmentSession(Host);
		VeroScript::Test::Configure(EnvironmentSession);
		EnvironmentSession.ApplyConfig(EnvironmentConfig);
		FValue EnvironmentValue;
		if (!Expect(EnvironmentConfig.ModuleEnvironment &&
					EnvironmentSession.LoadSource(Source, "<jit-environment>").bSuccess &&
					EnvironmentSession.CallFunction("backend_target", {}, &EnvironmentValue).bSuccess &&
					EnvironmentSession.GetJitCacheStats().RetainedPayloadBytes != 0,
				"Environment natives must use the same JIT retention policy as local natives", Diagnostics))
		{
			return 1;
		}

		const auto FirstInstances = EnvironmentProbe->Instances.load();
		if (!Expect(EnvironmentSession.CallFunction("backend_target", {}, &EnvironmentValue).bSuccess &&
					EnvironmentProbe->Instances == FirstInstances &&
					EnvironmentSession.GetLastJitStats().CompileAttempts == 0 &&
					EnvironmentSession.GetJitCacheStats().RetainedPayloadBytes != 0,
				"Stable environment native revisions must retain the configured session backend", Diagnostics))
		{
			return 1;
		}
	}
	Session.ApplyConfig(Config);
	if (!Expect(Session.LoadSource(Source, "<jit-session>").bSuccess && Session.Run().bSuccess && Output.str() == "42\n" && Probe->OwnedPrograms == 1, "Session did not bind its real module owner", Diagnostics))
	{
		return 1;
	}
	const std::size_t RunInstances = Probe->Instances;
	const auto RunUsage = Session.GetJitCacheStats();
	if (!Expect(Session.Run().bSuccess && Probe->Instances == RunInstances && Output.str() == "42\n42\n" && Session.GetLastJitStats().CompileAttempts == 0 && Session.GetJitCacheStats().CompileAttempts == RunUsage.CompileAttempts,
				"Session Run did not reuse its backend or cumulative quota", Diagnostics))
	{
		return 1;
	}
	FValue Result;
	if (!Expect(Session.CallFunction("backend_target", {}, &Result).bSuccess && std::get<std::int32_t>(Result) == 42, "Session call ignored backend selection", Diagnostics))
	{
		return 1;
	}
	const std::size_t WarmInstances = Probe->Instances;
	const auto WarmUsage = Session.GetJitCacheStats();
	if (!Expect(WarmUsage.RetainedPayloadBytes != 0 && Session.CallFunction("backend_target", {}, &Result).bSuccess && std::get<std::int32_t>(Result) == 42 && Probe->Instances == WarmInstances && Session.GetLastJitStats().CompileAttempts == 0 && Session.GetJitCacheStats().CompileAttempts == WarmUsage.CompileAttempts,
				"Session did not reuse its configured backend across calls", Diagnostics))
	{
		return 1;
	}

	{
		VeroScriptSession Registered(Host);
		VeroScript::Test::Configure(Registered);
		Registered.ApplyConfig(Config);
		if (!Expect(Registered.LoadSource(Source, "<jit-registration>").bSuccess
			&& Registered.CallFunction("backend_target", {}, &Result).bSuccess
			&& Registered.GetJitCacheStats().RetainedPayloadBytes != 0,
			"Could not warm the binding registration cache fixture", Diagnostics))
		{
			return 1;
		}
		if (!Expect(Registered.RegisterNative("unused_host", FNativeTypeDesc{ENativeType::Void}, {},
			[](const FNativeCallContext&, FValue&, std::string&)
			{
				return true;
			})
			&& Registered.GetJitCacheStats().RetainedPayloadBytes == 0
			&& Registered.GetJitCacheStats().NativeBytes == 0,
			"Descriptor native registration did not invalidate previously cached code", Diagnostics))
		{
			return 1;
		}
	}

	// A selected backend deliberately returns 42, while the source returns 99.
	// This proves debugging uses the real interpreter even after warming code,
	// and does not merely report zero compilations in a VM-only configuration.
	auto DebugBridge = std::make_shared<FDebugBridge>();
	std::size_t Stops = 0;
	DebugBridge->Debugger->SetStopCallback([&](VeroScriptDebugger& Debugger, EDebugStopReason)
	{
		++Stops;
		Debugger.SetRunMode(EDebugRunMode::Continue);
	});
	const std::size_t BeforeDebugInstances = Probe->Instances;
	const std::size_t BeforeDebugEntries = Probe->Entries;
	Output.str({});
	if (!Expect(Session.SetDebugBridge(DebugBridge) && Session.GetJitCacheStats().RetainedPayloadBytes == 0 && Session.CallFunction("backend_target", {}, &Result).bSuccess && std::get<std::int32_t>(Result) == 99 && Session.Run().bSuccess && Output.str() == "99\n" && Stops == 2 && Probe->Instances == BeforeDebugInstances && Probe->Entries == BeforeDebugEntries && Session.GetLastJitStats().CompileAttempts == 0 && DebugBridge->Begins == 2 && DebugBridge->Ends == 2 && DebugBridge->bDetachedVm,
				"Session debugging reused or prepared a selected backend", Diagnostics))
	{
		return 1;
	}
	DebugBridge->bConnected = false;
	if (!Expect(Session.CallFunction("backend_target", {}, &Result).bSuccess && std::get<std::int32_t>(Result) == 42 && Stops == 2 && Probe->Instances > BeforeDebugInstances && Probe->Entries == BeforeDebugEntries + 1 && DebugBridge->Begins == 3 && DebugBridge->Ends == 3 && DebugBridge->bDetachedVm && Session.SetDebugBridge(nullptr) && DebugBridge->Detaches == 1,
				"Session did not resume its selected backend after debugger disconnect", Diagnostics))
	{
		return 1;
	}
	Session.SetJitBackendFactory(nullptr);
	if (!Expect(!Session.CallFunction("backend_target", {}, &Result).bSuccess,
				"Forced JIT accepted an absent backend", Diagnostics))
	{
		return 1;
	}
	Session.SetExecutionBackend(EExecutionBackend::Mixed);
	if (!Expect(Session.CallFunction("backend_target", {}, &Result).bSuccess && std::get<std::int32_t>(Result) == 99, "Explicit no-backend selection did not use the VM", Diagnostics))
	{
		return 1;
	}
	Session.SetJitBackendFactory(Factory);
	Session.SetExecutionBackend(EExecutionBackend::Jit);
	FSessionJitCacheLimits Limits;
	Limits.MaxRetainedPayloadBytes = 1;
	Session.SetJitCacheLimits(Limits);
	const std::size_t LimitedInstances = Probe->Instances;
	if (!Expect(Session.CallFunction("backend_target", {}, &Result).bSuccess && std::get<std::int32_t>(Result) == 99 && Probe->Instances == LimitedInstances && Session.GetJitCacheStats().bResourceLimitReached,
				"Session allocated backend metadata beyond its configured retention limit", Diagnostics))
	{
		return 1;
	}
	Session.SetJitCacheLimits({});

	Output.str({});
	Session.SetJitBackendFactory(Factory);
	const std::size_t Before = Probe->Instances;
	const std::size_t BeforeEntries = Probe->Entries;
	const std::string WorkerSource = "import \"concurrent.vs\";\n"
		"int backend_target() { return 99; }\n"
		"void main() { task<int> work = std::concurrent::spawn_task<int>(backend_target); print(work.await()); }\n";
	if (!Expect(Session.LoadSource(WorkerSource, "<jit-worker>").bSuccess && Session.Run().bSuccess && Output.str() == "42\n" && Probe->Instances >= Before + 2 && Probe->Entries == BeforeEntries + 1,
				"Callable worker did not create its independent configured backend", Diagnostics))
	{
		return 1;
	}
	std::cout << "C++ backend selection and worker contract checks passed\n";
	return 0;
}
