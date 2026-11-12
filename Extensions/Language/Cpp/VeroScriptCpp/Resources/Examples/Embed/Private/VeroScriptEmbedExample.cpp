// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Repository-integrated embedding example and self-test.

/**
 * VeroScript 0.1.1 embed example:
 * IEngineHost + VeroScriptSession + RegisterNative + opt-in hot reload +
 * in-process DAP with live host bindings.
 */

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptHost.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptNative.h"
#include "VeroScriptSession.h"
#include "VeroScriptPath.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptVersion.h"

#include "VeroScriptDapAdapter.h"
#include "VeroScriptDapTcp.h"
#include "VeroScriptDapTypes.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
	struct FCaptureHost final : VeroScript::IEngineHost
	{
		//~ Begin IEngineHost Interface
		void OnPrintLine(const std::string_view Line) override
		{
			PrintText.append(Line);
			PrintText.push_back('\n');
			std::cout << Line << '\n';
		}

		void OnDiagnosticLine(const std::string_view Line) override
		{
			DiagnosticText.append(Line);
			DiagnosticText.push_back('\n');
			std::cerr << Line << '\n';
		}
		//~ End IEngineHost Interface

		std::string PrintText;
		std::string DiagnosticText;
	};

	struct FHostState
	{
		int LastSum = 0;
		std::string LastLog;
	};

	// Synchronous native example. A Tooling live domain supplies the same bridge
	// boundary with a queued stop callback for requests from the protocol thread.
	class FEmbeddedSessionDebugBridge final : public VeroScript::ISessionDebugBridge
	{
	public:
		//~ Begin VeroScript::ISessionDebugBridge Interface
		void ProgramChanged(const VeroScript::FSessionDebugProgramView &) noexcept override
		{
		}
		std::shared_ptr<VeroScript::VeroScriptDebugger> BeginExecution(
			const VeroScript::FSessionDebugExecutionContext &) noexcept override
		{
			Debugger->SetRunMode(VeroScript::EDebugRunMode::StepIn);
			return Debugger;
		}
		void EndExecution() noexcept override
		{
			++CompletedCalls;
		}
		void Detached() noexcept override
		{
			Debugger->SetStopCallback({});
		}
		//~ End VeroScript::ISessionDebugBridge Interface
		std::shared_ptr<VeroScript::VeroScriptDebugger> Debugger =
			std::make_shared<VeroScript::VeroScriptDebugger>();
		int CompletedCalls = 0;
	};

	bool HostAdd(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* const State = static_cast<FHostState*>(Context.UserData);
		const std::int32_t Left = std::get<std::int32_t>(Context.Args[0]);
		const std::int32_t Right = std::get<std::int32_t>(Context.Args[1]);
		const std::int64_t Sum = static_cast<std::int64_t>(Left) + Right;
		if (Sum < (std::numeric_limits<std::int32_t>::min)()
			|| Sum > (std::numeric_limits<std::int32_t>::max)())
		{
			OutError = "host_add result exceeds int range";
			return false;
		}
		if (State)
		{
			State->LastSum = static_cast<std::int32_t>(Sum);
		}
		OutReturn = static_cast<std::int32_t>(Sum);
		return true;
	}

	bool HostLog(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutReturn;
		(void)OutError;
		auto* const State = static_cast<FHostState*>(Context.UserData);
		if (State)
		{
			State->LastLog = std::get<std::string>(Context.Args[0]);
		}
		return true;
	}

	bool HostInvokeCallable(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		if (!Context.ScriptInvoker || Context.ArgCount != 2)
		{
			OutError = "host_invoke requires an active script callback";
			return false;
		}
		const auto* const Callable =
			std::get_if<VeroScript::FFunctionHandle>(&Context.Args[0]);
		if (!Callable || !*Callable)
		{
			OutError = "host_invoke expected a bound function";
			return false;
		}
		std::vector<VeroScript::FValue> Arguments{Context.Args[1]};
		return Context.ScriptInvoker->InvokeCallable(
			*Callable, Arguments, OutReturn, OutError);
	}

	bool HostCountBatches(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		const auto* const Batches = Context.ArgCount == 1
			? std::get_if<VeroScript::FDynamicArrayHandle>(&Context.Args[0])
			: nullptr;
		if (!Batches || !*Batches)
		{
			OutError = "host_count_batches expected array<array<int>>";
			return false;
		}
		const std::size_t Count = (*Batches)->Elements.size();
		if (static_cast<std::uint64_t>(Count)
			> static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			OutError = "host_count_batches result exceeds int64 range";
			return false;
		}
		OutReturn = static_cast<std::int64_t>(Count);
		return true;
	}

	struct FDemoActor
	{
		std::int32_t Value = 42;
	};

	bool DemoActorSubclass(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		if (Context.ArgCount < 1 || !std::holds_alternative<std::string>(Context.Args[0]))
		{
			OutError = "subclass factory expects script type name";
			return false;
		}
		auto* const Actor = new FDemoActor{};
		OutReturn = VeroScript::MakeHostObject(
			/*TypeId=*/1,
			"AActor",
			Actor,
			[](void* Payload)
			{
				delete static_cast<FDemoActor*>(Payload);
			});
		return true;
	}

	bool DemoActorGetValue(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* const Actor = static_cast<FDemoActor*>(Context.HostThis);
		if (!Actor)
		{
			OutError = "null AActor";
			return false;
		}
		OutReturn = Actor->Value;
		return true;
	}

	bool RegisterDemoNatives(VeroScript::VeroScriptSession& Session, FHostState& State)
	{
		if (!Session.RegisterNative(
				"host_add",
				VeroScript::ENativeType::Int,
				{VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
				HostAdd,
					&State))
		{
			return false;
		}
		if (!Session.RegisterNative(
				"host_log",
				VeroScript::ENativeType::Void,
				{VeroScript::ENativeType::String},
				HostLog,
					&State))
		{
			return false;
		}
		if (!Session.RegisterNative(
				"host_invoke",
				VeroScript::FNativeTypeDesc::Scalar(VeroScript::ENativeType::Int),
				{
					{VeroScript::FNativeTypeDesc::Function("fn(int)->int"), 0},
					{VeroScript::FNativeTypeDesc::Scalar(VeroScript::ENativeType::Int), 0},
				},
				HostInvokeCallable))
		{
			return false;
		}
		if (!Session.RegisterNative(
				"host_count_batches",
				VeroScript::FNativeTypeDesc::Scalar(VeroScript::ENativeType::Int64),
				{{VeroScript::FNativeTypeDesc::ArrayOf(
					VeroScript::FNativeTypeDesc::ArrayOf(
						VeroScript::FNativeTypeDesc::Scalar(VeroScript::ENativeType::Int))),
					0}},
				HostCountBatches))
		{
			return false;
		}
		return true;
	}

	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptEmbedExample: " << Message << '\n';
		return EXIT_FAILURE;
	}

	// Match CLI semantics: int main() and exit(n) keep bSuccess=true while setting ExitCode.
	bool RunSucceeded(const VeroScript::FRunResult& Result)
	{
		return Result.bSuccess && Result.ExitCode == 0;
	}

	int RunSelfTest(const std::string_view JitBackendPath)
	{
		{
			FCaptureHost BoundaryHost;
			FHostState BoundaryState;
			VeroScript::VeroScriptSession BoundarySession(BoundaryHost);
			VeroScript::BytecodeVm::Configure(BoundarySession);
			if (!JitBackendPath.empty())
			{
				std::string Error;
				if (!BoundarySession.SetJitBackendPath(JitBackendPath, Error))
				{
					return Fail("native boundary backend setup failed");
				}
				BoundarySession.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
			}
			if (!RegisterDemoNatives(BoundarySession, BoundaryState)
				|| !BoundarySession.LoadSource(
					"int add(int left, int right) { return host_add(left, right); }",
					"embed-native-boundaries.vs").bSuccess)
			{
				return Fail("native boundary setup failed");
			}
			for (const std::int32_t Limit : {
				(std::numeric_limits<std::int32_t>::min)(),
				(std::numeric_limits<std::int32_t>::max)()})
			{
				VeroScript::FValue Result;
				if (!BoundarySession.CallFunction("add", {Limit, std::int32_t{0}}, &Result).bSuccess
					|| !std::holds_alternative<std::int32_t>(Result)
					|| std::get<std::int32_t>(Result) != Limit)
				{
					return Fail("native addition rejected a valid int boundary");
				}
				const auto Overflow = BoundarySession.CallFunction("add",
					{Limit, std::int32_t{Limit < 0 ? -1 : 1}}, &Result);
				if (Overflow.bSuccess || BoundaryState.LastSum != Limit
					|| Overflow.Error.Message.find("host_add result exceeds int range") == std::string::npos)
				{
					return Fail("native addition must reject overflow before changing host state");
				}
			}
		}
		FCaptureHost Host;
		FHostState State;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::BytecodeVm::Configure(Session);
		if (!JitBackendPath.empty())
		{
			std::string Error;
			if (!Session.SetJitBackendPath(JitBackendPath, Error))
			{
				std::cerr << Error << '\n';
				return Fail("Select JIT backend failed");
			}
		}
		if (!RegisterDemoNatives(Session, State))
		{
			return Fail("RegisterNative failed");
		}

		{
			Host.PrintText.clear();
			Host.DiagnosticText.clear();
			const VeroScript::FRunResult LoadResult = Session.LoadSource(
				"int twice(int value)\n"
				"{\n"
				"    return value * 2;\n"
				"}\n"
				"void main()\n"
				"{\n"
				"    int total = host_add(40, 2);\n"
				"    print(total);\n"
				"    function(int)->int callback = twice;\n"
				"    print(host_invoke(callback, 21));\n"
				"    array<array<int>> batches = {{1, 2}, {3}};\n"
				"    print(host_count_batches(batches));\n"
				"    host_log(\"ok-embed\");\n"
				"}\n",
				"embed-selftest");
			if (!LoadResult.bSuccess)
			{
				return Fail("LoadSource demo failed");
			}
			const VeroScript::FRunResult RunResult = Session.Run();
			if (!RunSucceeded(RunResult))
			{
				return Fail("Run demo failed");
			}
			if (Host.PrintText != "42\n42\n2\n"
				|| State.LastSum != 42 || State.LastLog != "ok-embed")
			{
				std::cerr << "print:\n"
						  << Host.PrintText;
				return Fail("demo output mismatch");
			}

			VeroScript::FJitPolicy Policy;
			Policy.MixedHotCallThreshold = 4;
			Policy.EagerCompileBudget = 32;
			Session.SetJitPolicy(Policy);
			Session.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
			if (Session.GetJitPolicy().MixedHotCallThreshold != 4
				|| Session.GetJitPolicy().EagerCompileBudget != 32)
			{
				return Fail("Session JIT policy round-trip failed");
			}
			Host.PrintText.clear();
			const VeroScript::FRunResult JitRun = Session.Run();
			if (!JitBackendPath.empty()
				&& (!RunSucceeded(JitRun) || Host.PrintText != "42\n42\n2\n"))
			{
				return Fail("Run with JIT backend/policy failed");
			}
			// Non-debug Run should record at least the SetJitPolicy invalidation.
			if (JitBackendPath.empty() && (JitRun.bSuccess || !Host.PrintText.empty()))
			{
				return Fail("JIT without an explicitly selected backend should fail before running");
			}
			if (!JitBackendPath.empty() && Session.GetLastJitStats().CacheInvalidations < 1)
			{
				return Fail("expected GetLastJitStats after JIT Run");
			}
			Session.Clear();
			if (Session.GetLastJitStats().CacheInvalidations != 0
				|| Session.GetJitPolicy().MixedHotCallThreshold != 4)
			{
				return Fail("Clear should zero JIT stats but keep policy");
			}
			// Restore a loadable module for later selftest steps that expect IsLoaded.
			const VeroScript::FRunResult ReloadDemo = Session.LoadSource(
				"int twice(int value)\n"
				"{\n"
				"    return value * 2;\n"
				"}\n"
				"void main()\n"
				"{\n"
				"    int total = host_add(40, 2);\n"
				"    print(total);\n"
				"    function(int)->int callback = twice;\n"
				"    print(host_invoke(callback, 21));\n"
				"    array<array<int>> batches = {{1, 2}, {3}};\n"
				"    print(host_count_batches(batches));\n"
				"    host_log(\"ok-embed\");\n"
				"}\n",
				"embed-selftest");
			if (!ReloadDemo.bSuccess)
			{
				return Fail("re-LoadSource after Clear failed");
			}
			Session.SetExecutionBackend(VeroScript::EExecutionBackend::Mixed);
		}

		{
			Host.PrintText.clear();
			Host.DiagnosticText.clear();
			const VeroScript::FRunResult BadLoad = Session.LoadSource(
				"void main()\n"
				"{\n"
				"    print(;\n",
				"embed-diag");
			if (BadLoad.bSuccess)
			{
				return Fail("expected diagnostic LoadSource to fail");
			}
			if (Host.DiagnosticText.empty())
			{
				return Fail("expected OnDiagnosticLine for bad source");
			}
			if (!Session.IsLoaded())
			{
				return Fail("failed LoadSource should keep previous module");
			}
			Host.PrintText.clear();
			const VeroScript::FRunResult KeepRun = Session.Run();
			if (!RunSucceeded(KeepRun) || Host.PrintText != "42\n42\n2\n")
			{
				return Fail("previous module should still run after failed LoadSource");
			}
		}

		{
			Host.PrintText.clear();
			Host.DiagnosticText.clear();
			const VeroScript::FRunResult FileLoad = Session.LoadFile("embed_demo.vs");
			if (!FileLoad.bSuccess)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("LoadFile embed_demo.vs failed (is the asset deployed?)");
			}
			const VeroScript::FRunResult FileRun = Session.Run();
			if (!RunSucceeded(FileRun))
			{
				std::cerr << Host.DiagnosticText;
				return Fail("Run embed_demo.vs failed");
			}
			if (Host.PrintText.find("embed-file") == std::string::npos
				|| Host.PrintText.find("7\n") == std::string::npos)
			{
				std::cerr << "print:\n"
						  << Host.PrintText;
				return Fail("embed_demo.vs output mismatch");
			}
		}

		{
			// 0.1.1 language-level hot reload: retained globals across Runs.
			FCaptureHost HotReloadHost;
			VeroScript::VeroScriptSession HotReloadSession(HotReloadHost);
			VeroScript::BytecodeVm::Configure(HotReloadSession);
			HotReloadSession.SetHotReloadEnabled(true);
			const VeroScript::FRunResult LoadResult = HotReloadSession.LoadSource(
				"int runs = 0;\n"
				"void main()\n"
				"{\n"
				"    runs++;\n"
				"    print(runs);\n"
				"}\n",
				"embed-hot-reload");
			if (!LoadResult.bSuccess)
			{
				return Fail("hot reload LoadSource failed");
			}
			HotReloadHost.PrintText.clear();
			if (!RunSucceeded(HotReloadSession.Run()) || HotReloadHost.PrintText != "1\n")
			{
				std::cerr << HotReloadHost.PrintText << HotReloadHost.DiagnosticText;
				return Fail("hot reload first run failed");
			}
			HotReloadHost.PrintText.clear();
			if (!RunSucceeded(HotReloadSession.Run()) || HotReloadHost.PrintText != "2\n")
			{
				std::cerr << HotReloadHost.PrintText << HotReloadHost.DiagnosticText;
				return Fail("hot reload should retain globals across Runs");
			}
		}

		{
			// Debug the existing Session, retaining its real native callbacks.
			FCaptureHost DebugHost;
			FHostState DebugState;
			VeroScript::VeroScriptSession DebugSession(DebugHost);
			VeroScript::BytecodeVm::Configure(DebugSession);
			auto Bridge = std::make_shared<FEmbeddedSessionDebugBridge>();
			int Stops = 0;
			bool Inspected = false;
			Bridge->Debugger->SetStopCallback([&](VeroScript::VeroScriptDebugger& Active,
				VeroScript::EDebugStopReason)
			{
				++Stops;
				Inspected = Active.Evaluate("left").Value == "20";
				Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
			});
			VeroScript::FValue Result;
			if (!RegisterDemoNatives(DebugSession, DebugState)
				|| !DebugSession.LoadSource(
					"int add(int left, int right) { return host_add(left, right); }\n",
					"embed-session-debug.vs").bSuccess
				|| !DebugSession.SetDebugBridge(Bridge)
				|| !DebugSession.CallFunction("add", {std::int32_t{20}, std::int32_t{22}}, &Result).bSuccess
				|| std::get<std::int32_t>(Result) != 42 || DebugState.LastSum != 42
				|| Stops != 1 || !Inspected || Bridge->CompletedCalls != 1
				|| !DebugSession.SetDebugBridge(nullptr))
			{
				return Fail("existing Session debug bridge failed");
			}
		}

		{
			// Binding-backed DAP launch: this creates its own VM, not a Session attach.
			FCaptureHost DapHost;
			FHostState DapState;
			VeroScript::VeroScriptSession DapSession(DapHost);
			VeroScript::BytecodeVm::Configure(DapSession);
			if (!RegisterDemoNatives(DapSession, DapState))
			{
				return Fail("DAP live RegisterNative failed");
			}

			using namespace VeroScript::Tooling;
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
			Launch.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
			Launch.Source =
				"void main()\n"
				"{\n"
				"    print(host_add(20, 22));\n"
				"    host_log(\"dap-live\");\n"
				"}\n";
			Launch.FileLabel = "embed-dap-live.vs";
			Launch.Bindings = &DapSession.GetBindingCatalog();
			Launch.bLiveBindings = true;
			if (!Adapter.Launch(Launch).bSuccess)
			{
				return Fail("DAP live Launch failed");
			}
			const FDebugRunResult Run = Adapter.ConfigurationDone();
			if (!Run.bSuccess)
			{
				for (const auto& Diag : Run.Diagnostics)
				{
					std::cerr << Diag.Message << '\n';
				}
				return Fail("DAP live ConfigurationDone failed");
			}
			if (DapState.LastSum != 42 || DapState.LastLog != "dap-live"
				|| Printed.find("42") == std::string::npos)
			{
				std::cerr << "sum=" << DapState.LastSum << " log=" << DapState.LastLog
					<< " printed=\n"
						  << Printed;
				return Fail("DAP live host callbacks mismatch");
			}

			// Export catalog for IDE LSP/DAP (stubs). Same names, different process semantics.
			if (!DapSession.ExportBindingsToFile("embed_bindings.json"))
			{
				return Fail("ExportBindingsToFile failed");
			}
		}

		{
			// Host Class inherit: SetHostTypeInheritable + @subclass + catalog flags.
			FCaptureHost InheritHost;
			VeroScript::VeroScriptSession InheritSession(InheritHost);
			VeroScript::BytecodeVm::Configure(InheritSession);
			VeroScript::FHostTypeId ActorId;
			if (!InheritSession.RegisterHostType(
					"AActor", VeroScript::EHostTypeKind::Class, &ActorId)
						|| !InheritSession.SetHostTypeInheritable(ActorId, true)
						|| !InheritSession.RegisterHostTypeSubclassFactory(
							ActorId, DemoActorSubclass))
			{
				return Fail("host inherit type setup failed");
			}
			VeroScript::FHostMemberDesc GetValue;
			GetValue.Kind = VeroScript::EHostMemberKind::Method;
			GetValue.Name = "get_value";
			GetValue.ReturnType = VeroScript::ENativeType::Int;
			GetValue.Callback = DemoActorGetValue;
			if (!InheritSession.RegisterHostMember(ActorId, GetValue))
			{
				return Fail("host inherit RegisterHostMember failed");
			}
			const VeroScript::FRunResult InheritLoad = InheritSession.LoadSource(
				"class MyActor : AActor\n"
				"{\n"
				"public:\n"
				"    MyActor()\n"
				"    {\n"
				"    }\n"
				"    int ScriptBonus()\n"
				"    {\n"
				"        return 1;\n"
				"    }\n"
				"};\n",
				"embed-host-inherit");
			if (!InheritLoad.bSuccess)
			{
				std::cerr << InheritHost.DiagnosticText;
				return Fail("host inherit LoadSource failed");
			}
			VeroScript::FObjectHandle Actor;
			if (!RunSucceeded(InheritSession.ConstructObject("MyActor", {}, Actor)) || !Actor
				|| !Actor->GetHostBase())
			{
				std::cerr << InheritHost.DiagnosticText;
				return Fail("ConstructObject MyActor should succeed with HostBase");
			}
			if (VeroScript::GetScriptOwner(Actor->GetHostBase()).get() != Actor.get())
			{
				return Fail("GetScriptOwner should reverse-lookup the script instance");
			}
			VeroScript::FValue Bonus;
			if (!RunSucceeded(InheritSession.CallMethod(Actor, "ScriptBonus", {}, &Bonus))
				|| !std::holds_alternative<std::int32_t>(Bonus)
				|| std::get<std::int32_t>(Bonus) != 1)
			{
				std::cerr << InheritHost.DiagnosticText;
				return Fail("CallMethod ScriptBonus should return 1");
			}
			std::vector<VeroScript::FLoadedTypeInfo> HostTypes;
			if (!InheritSession.EnumerateTypes(HostTypes, /*bHostSubclassesOnly=*/true)
				|| HostTypes.size() != 1
				|| HostTypes.front().SimpleName != "MyActor")
			{
				return Fail("EnumerateTypes(host subclasses) should list MyActor");
			}
			if (!InheritSession.ExportBindingsToFile("embed_bindings_inherit.json"))
			{
				return Fail("ExportBindingsToFile inherit catalog failed");
			}
			std::string CatalogJson;
			if (!VeroScript::ExportBindingCatalogJson(
					InheritSession.GetBindingCatalog(), CatalogJson)
						|| CatalogJson.find("\"inheritable\": true") == std::string::npos
						|| CatalogJson.find("\"hasSubclass\": true") == std::string::npos)
			{
				return Fail("inherit catalog should export inheritable/hasSubclass");
			}
		}

		std::cout << "VeroScriptEmbedExample selftest OK ("
			<< VeroScript::GetVersionString() << ")\n";
		return EXIT_SUCCESS;
	}

	int RunDapTcp(std::uint16_t Port)
	{
		FCaptureHost Host;
		FHostState State;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::BytecodeVm::Configure(Session);
		if (!RegisterDemoNatives(Session, State))
		{
			return Fail("RegisterNative failed");
		}

		using namespace VeroScript::Tooling;
		FDapTcpRunOptions Opt;
		Opt.Stdio.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
		Opt.BindAddress = "127.0.0.1";
		Opt.Port = Port;
		Opt.MaxSessions = 0;
		Opt.bAnnounceListen = true;
		Opt.Stdio.LiveBindings = &Session.GetBindingCatalog();
		std::cerr << "VeroScriptEmbedExample: DAP TCP with live bindings (IDE Attach)\n";
		std::cerr << "This mode launches a separate VM; existing Session debugging uses SetDebugBridge.\n";
		return VeroScriptDapTcp::Run(Opt);
	}

	int RunDemo()
	{
		FCaptureHost Host;
		FHostState State;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::BytecodeVm::Configure(Session);
		if (!RegisterDemoNatives(Session, State))
		{
			return Fail("RegisterNative failed");
		}

		const VeroScript::FRunResult LoadResult = Session.LoadFile("embed_demo.vs");
		if (!LoadResult.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("LoadFile embed_demo.vs failed");
		}
		const VeroScript::FRunResult RunResult = Session.Run();
		if (!RunSucceeded(RunResult))
		{
			std::cerr << Host.DiagnosticText;
			return Fail("Run failed");
		}
		return EXIT_SUCCESS;
	}

	void PrintUsage(const char* ProgramName)
	{
		std::cout
			<< "Usage: " << ProgramName << " [selftest [backend-path]|demo|dap-tcp [port]]\n"
			<< "  selftest  Native Session and in-process DAP acceptance checks (default)\n"
			<< "  demo      LoadFile embed_demo.vs and Run\n"
			<< "  dap-tcp   Listen for IDE Attach (default 4711; 0 selects a free port) with LiveBindings\n"
			<< "\n"
			<< "Live DAP (host process):\n"
			<< "  Session.RegisterNative(...);\n"
			<< "  FDapTcpRunOptions Opt; Opt.MaxSessions = 0;\n"
			<< "  Opt.Stdio.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());\n"
			<< "  Opt.Stdio.LiveBindings = &Session.GetBindingCatalog();\n"
			<< "  VeroScriptDapTcp::Run(Opt);\n"
			<< "  IDE: Debug → Attach to Host (DAP attach + configurationDone).\n"
			<< "IDE catalog path uses ExportBindingsToFile stubs only (no live callbacks).\n";
	}
}

int RunEmbedExample(const std::vector<std::string>& Arguments)
{
	const std::string Mode = Arguments.size() > 1 ? Arguments[1] : "selftest";
	if (Mode == "selftest")
	{
		return RunSelfTest(Arguments.size() > 2 ? std::string_view(Arguments[2]) : std::string_view());
	}
	if (Mode == "demo")
	{
		return RunDemo();
	}
	if (Mode == "dap-tcp")
	{
		std::uint16_t Port = 4711;
		if (Arguments.size() > 2)
		{
			try
			{
				std::size_t ParsedLength = 0;
				const std::string& PortText = Arguments[2];
				const int Parsed = std::stoi(PortText, &ParsedLength);
				if (ParsedLength != PortText.size() || Parsed < 0 || Parsed > 65535)
				{
					std::cerr << "port must be 0..65535\n";
					return EXIT_FAILURE;
				}
				Port = static_cast<std::uint16_t>(Parsed);
			}
			catch (...)
			{
				std::cerr << "invalid port\n";
				return EXIT_FAILURE;
			}
		}
		return RunDapTcp(Port);
	}
	PrintUsage(Arguments.empty() ? "VeroScriptEmbedExample" : Arguments[0].c_str());
	return EXIT_FAILURE;
}

#if defined(_WIN32)
int wmain(const int ArgumentCount, wchar_t* const ArgumentValues[])
{
	std::vector<std::string> Arguments;
	Arguments.reserve(static_cast<std::size_t>(ArgumentCount));
	for (int Index = 0; Index < ArgumentCount; ++Index)
	{
		Arguments.push_back(VeroScript::PathUtil::ToUtf8(std::filesystem::path(ArgumentValues[Index])));
	}
	return RunEmbedExample(Arguments);
}
#else
int main(const int ArgumentCount, char* const Arguments[])
{
	return RunEmbedExample(std::vector<std::string>(Arguments, Arguments + ArgumentCount));
}
#endif
