// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptHost.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;

	void Require(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			throw std::runtime_error(Message);
		}
	}

	class FDebugBridge final : public ISessionDebugBridge
	{
	public:
		//~ Begin ISessionDebugBridge Interface
		void ProgramChanged(const FSessionDebugProgramView&) noexcept override
		{
		}
		std::shared_ptr<VeroScriptDebugger> BeginExecution(const FSessionDebugExecutionContext&) noexcept override
		{
			return Debugger;
		}
		void EndExecution() noexcept override
		{
		}
		void Detached() noexcept override
		{
		}
		//~ End ISessionDebugBridge Interface

	private:
		std::shared_ptr<VeroScriptDebugger> Debugger = std::make_shared<VeroScriptDebugger>();
	};

	const FJitFunctionDiagnostic& Find(const FJitDiagnostics& Snapshot, const std::string_view Name)
	{
		for (const auto& Function : Snapshot.Functions)
		{
			if (Function.Name.find(Name) != std::string::npos) return Function;
		}
		std::string Message = "missing Session function diagnostic: ";
		Message += Name;
		for (const auto& Function : Snapshot.Functions)
		{
			Message += " [" + Function.Name + "]";
		}
		throw std::runtime_error(Message);
	}

	void CheckSnapshot(const FJitDiagnostics& Snapshot)
	{
		Require(!Snapshot.Functions.empty(), "executed invocation must retain its diagnostics");
		for (const auto& Function : Snapshot.Functions)
		{
			Require(Function.Calls == Function.Returned + Function.ControlTransfers + Function.Failures,
				"diagnostics must preserve invocation outcome accounting");
			Require(Function.InclusiveNanoseconds >= Function.SelfNanoseconds, "invalid diagnostic timing");
		}
	}

	void TestSession(const std::shared_ptr<const IJitBackendFactory>& Factory, const EExecutionBackend Backend)
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		FEngineConfig Config;
		Test::Configure(Config);
		Config.JitBackendFactory = Factory;
		Config.ExecutionBackend = Backend;
		std::string Error;
		Require(Session.ApplyConfig(Config, &Error), "configure diagnostic Session");
		Require(Session.LoadSource(R"VS(
class Box {
public:
    int value;
    Box(int initial) { value = initial; }
    int read() { return value; }
}
int answer() { return 7; }
int fail(int divisor) { return 7 / divisor; }
fn()->int make() { return []()->int { return 7; }; }
void main() { print(answer()); }
)VS", "cpp-session-diagnostics.vs").bSuccess, "load diagnostic Session");
		FValue Value;
		Require(Session.CallFunction("answer", {}, &Value).bSuccess, "default diagnostic call");
		Require(Session.GetLastJitDiagnostics().Functions.empty(), "diagnostics must be disabled by default");
		FJitPolicy Policy = Session.GetJitPolicy();
		Policy.Diagnostics = {16, 64, true};
		Session.SetJitPolicy(Policy);
		Require(Session.Run().bSuccess, "profile Session Run");
		CheckSnapshot(Session.GetLastJitDiagnostics());
		// A backend may inline answer into main; only actual Core entries are
		// recorded. Direct host calls below still require one answer entry.
		Require(Find(Session.GetLastJitDiagnostics(), "main").Calls == 1, "Run function entry count");
		const auto Saved = Session.GetLastJitDiagnostics();
		for (int Invocation = 0; Invocation < 3; ++Invocation)
		{
			Require(Session.CallFunction("answer", {}, &Value).bSuccess, "profile reused free-function scratch");
			Require(std::get<std::int32_t>(Value) == 7, "diagnostics changed the return value");
			const auto& Snapshot = Session.GetLastJitDiagnostics();
			CheckSnapshot(Snapshot);
			Require(Find(Snapshot, "answer").Calls == 1, "Session snapshots must not accumulate across VMs");
			if (Backend == EExecutionBackend::Jit && Factory && Factory->GetDescription().bNativeCode)
			{
				const auto& Stats = Session.GetLastJitStats();
				Require(Stats.NativeLeafExecutions + Stats.NativeCfgExecutions == 1,
					"native entry counters must describe only the current Session invocation");
			}
		}
		FObjectHandle Object;
		Require(Session.ConstructObject("Box", {std::int32_t{7}}, Object).bSuccess, "profile construction");
		CheckSnapshot(Session.GetLastJitDiagnostics());
		Require(Session.CallMethod(Object, "read", {}, &Value).bSuccess, "profile method");
		CheckSnapshot(Session.GetLastJitDiagnostics());
		Require(std::get<std::int32_t>(Value) == 7, "diagnostics changed the method result");
		Require(Session.CallFunction("make", {}, &Value).bSuccess, "create diagnostic callable");
		const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(Value));
		Require(Session.IsCallableValid(Callable), "retain diagnostic callable");
		Require(Session.InvokeCallable(Callable, {}, &Value).bSuccess, "profile retained callable");
		CheckSnapshot(Session.GetLastJitDiagnostics());
		Require(Session.ReleaseCallable(Callable), "release diagnostic callable");
		Require(!Session.CallFunction("fail", {std::int32_t{0}}, &Value).bSuccess, "diagnostics must not swallow failure");
		CheckSnapshot(Session.GetLastJitDiagnostics());
		Require(Find(Session.GetLastJitDiagnostics(), "fail").Failures == 1, "retain failed invocation diagnostics");
		Require(Session.CallFunction("answer", {}, &Value).bSuccess, "recover after failed diagnostic invocation");
		Require(Find(Session.GetLastJitDiagnostics(), "answer").Returned == 1, "stale diagnostic frames after failure");
		auto Bridge = std::make_shared<FDebugBridge>();
		Require(Session.SetDebugBridge(Bridge), "attach diagnostic bridge");
		Require(Session.Run().bSuccess, "profile debug execution");
		CheckSnapshot(Session.GetLastJitDiagnostics());
		Require(Session.SetDebugBridge(nullptr), "detach diagnostic bridge");
		Policy.Diagnostics = {1, 64, false};
		Session.SetJitPolicy(Policy);
		Require(Session.Run().bSuccess, "bounded diagnostic execution");
		const auto& Bounded = Session.GetLastJitDiagnostics();
		Require(Bounded.Functions.size() <= 1, "honor diagnostic record bound");
		if (Backend == EExecutionBackend::Vm)
		{
			Require(Bounded.DroppedRecords > 0, "report dropped VM diagnostic records");
		}
		Require(!Bounded.bTimingEnabled, "disable diagnostic timing");
		for (const auto& Function : Bounded.Functions)
		{
			Require(Function.InclusiveNanoseconds == 0 && Function.SelfNanoseconds == 0, "disabled timing must stay zero");
		}
		Policy.Diagnostics = {};
		Session.SetJitPolicy(Policy);
		Require(Session.Run().bSuccess && Session.GetLastJitDiagnostics().Functions.empty(), "clear the snapshot on a disabled run");
		Require(!Saved.Functions.empty() && Find(Saved, "main").Calls == 1, "copied snapshot must own its names and records");
		Policy.Diagnostics = {16, 64, false};
		Session.SetJitPolicy(Policy);
		Require(Session.CallFunction("answer", {}, &Value).bSuccess, "collect before Clear");
		Session.Clear();
		Require(Session.GetLastJitDiagnostics().Functions.empty(), "Clear diagnostic snapshot");
		Require(Find(Saved, "main").Calls == 1, "copied snapshot must survive program destruction");
	}
}

int main(int ArgumentCount, char** Arguments)
{
	try
	{
		TestSession({}, VeroScript::EExecutionBackend::Vm);
		if (ArgumentCount > 1)
		{
			std::string Error;
			const auto Factory = VeroScript::LoadJitBackend(Arguments[1], Error);
			Require(!!Factory, "load matching JIT backend");
			TestSession(Factory, VeroScript::EExecutionBackend::Jit);
			TestSession(Factory, VeroScript::EExecutionBackend::Mixed);
		}
		std::cout << "C++ Session JIT diagnostics passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
