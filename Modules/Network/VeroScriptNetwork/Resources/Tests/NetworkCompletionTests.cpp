// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "NetworkTestProviders.h"
#include "VeroScriptNetworkModule.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptSession.h"
#include "VeroScriptHost.h"
#include "VeroScriptCompile.h"
#include "VeroScriptJitBackendLoader.h"
#include "NetworkBindingSupport.h"
#include <filesystem>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace VeroScript;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error(#X); \
		} \
	} \
	while (false)
struct FCounts
{
	unsigned Destroyed = 0, Called = 0;
	std::thread::id Thread = std::this_thread::get_id();
	FHostObjectHandle FaultContext;
};
class FThrowingInvoker final : public IScriptCallInvoker
{
public:
	FThrowingInvoker(IScriptCallInvoker& Active, bool Allocation) : Active(Active), Allocation(Allocation)
	{
	}
	bool InvokeCallable(const FFunctionHandle&, std::vector<FValue>&, FValue&, std::string&) override
	{
		++Attempts;
		if (Allocation)
		{
			throw std::bad_alloc();
		}
		throw std::runtime_error("Injected invocation failure");
	}
	bool InvokeFunction(std::string_view, std::vector<FValue>&, FValue&, std::string&) override
	{
		throw std::logic_error("Unexpected named invocation");
	}
	std::uint64_t GetProgramGeneration() const override
	{
		return Active.GetProgramGeneration();
	}
	std::shared_ptr<const void> GetProgramOwner() const override
	{
		return Active.GetProgramOwner();
	}
	unsigned Attempts = 0;

private:
	IScriptCallInvoker& Active;
	bool Allocation;
};
static bool CheckDispatchFailure(const FNativeCallContext& C, FValue& Out, std::string&)
{
	auto& Counts = *static_cast<FCounts*>(C.UserData);
	auto& Context = *static_cast<NetworkBinding::FContextValue*>(Counts.FaultContext->Payload);
	FThrowingInvoker Invoker(*C.ScriptInvoker, std::get<bool>(C.Args[0]));
	std::string Error;
	std::size_t Count = 0;
	const bool Ok = Context.Services->Dispatch(*Context.Value, Invoker, 64, Count, Error);
	Out = !Ok && !Error.empty() && Count == 2 && Invoker.Attempts == 1;
	// A failure must also leave the dispatch scope reusable.
	Error.clear();
	CHECK(Context.Services->Dispatch(*Context.Value, Invoker, 64, Count, Error) && Count == 0);
	return true;
}
static bool Destroyed(const FNativeCallContext& C, FValue&, std::string&)
{
	auto& Counts = *static_cast<FCounts*>(C.UserData);
	CHECK(Counts.Thread == std::this_thread::get_id());
	++Counts.Destroyed;
	return true;
}
static bool Called(const FNativeCallContext& C, FValue&, std::string&)
{
	++static_cast<FCounts*>(C.UserData)->Called;
	return true;
}
static const char* Model = R"VS(
Network::context context=Network::create_context(true);
Network::tcp_listener listener=nullptr;
Network::operation pending=nullptr;
Network::operation escaped=nullptr;
class Watch {public:int tag=73;~Watch(){record_destroyed();}};
void prepare(){
 listener=Network::listen(context,"127.0.0.1",0);
 pending=listener.accept(5000);
 Watch w=Watch();
 Network::context owner=context;
 pending.on_complete([w,owner](Network::operation completed)mutable->void{
  assert(w.tag==73 && completed.done());
  assert(completed.message_id()=="network.cancelled");
  escaped=completed;
  record_called();
  owner.shutdown();
 });
}
void cancel(){pending.cancel();}
int64 pump(){context.poll();return context.dispatch(64);}
bool escaped_ok(){return escaped.done() && escaped.message_id()=="network.cancelled";}
Network::context get_context(){return context;}
Network::operation get_pending(){return pending;}
)VS";
static void Run(
    const std::string& Path, EExecutionBackend Backend, const std::shared_ptr<const IJitBackendFactory>& Factory)
{
	FCounts Counts;
	std::string Error;
	FHostBindingRegistry Base;
	CHECK(Base.RegisterNative("record_destroyed", ENativeType::Void, {}, Destroyed, &Counts, Error));
	CHECK(Base.RegisterNative("record_called", ENativeType::Void, {}, Called, &Counts, Error));
	CHECK(Base.RegisterNative(
	    "check_dispatch_failure", ENativeType::Bool, {ENativeType::Bool}, CheckDispatchFailure, &Counts, Error));
	FNativeExtensionRegistry Registry(&Base);
	auto BackendCatalog = Network::Test::AddProviders(Registry, Error, !Path.empty());
	CHECK(BackendCatalog);
	FNativeExtensionLoadOptions Options;
	Options.ContentIdentity = Network::GetImplementationIdentity();
	auto Module = Path.empty() ? QueryNativeExtension(Network::QueryModule, std::make_shared<int>(1), Error, Options)
	                           : LoadNativeExtension(Path, Error);
	FNativeExtensionRegistrationOptions Registration;
	Registration.Services = {BackendCatalog->Binding()};
	Registration.Origin.ProviderId = "network";
	CHECK(Module && Registry.Add(Module, Registration, Error));
	FModuleEnvironmentDesc Desc;
	Desc.EntryOwnerId = "project";
	const auto Root = std::filesystem::current_path().generic_string();
	Desc.SourceRoots = {{Root, "project"}};
	Desc.Scopes = {{"project", {"network"}}, {"network", {}}};
	Desc.Bindings = Registry.GetBindings();
	Desc.Owners = Registry.GetOwners();
	Desc.AvailableDependencies = Registry.GetDependencies();
	auto Environment = FModuleEnvironment::Create(std::move(Desc), Error);
	CHECK(Environment);
	std::ostringstream Output, Diagnostics;
	FStreamEngineHost Host(Output, Diagnostics);
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	FEngineConfig Config;
	VeroScript::Test::Configure(Config);
	Config.ModuleEnvironment = Environment;
	Config.ExecutionBackend = Backend;
	Config.JitBackendFactory = Factory;
	CHECK(Session.ApplyConfig(Config));
	Session.SetHotReloadEnabled(true);
	const auto Require = [&](const FRunResult& Result)
	{
		if (!Result.bSuccess)
		{
			throw std::runtime_error(Result.Error.Message + "\n" + Diagnostics.str());
		}
	};
	Require(Session.LoadSource(Model, Root + "/network-completion-session.vs"));
	Require(Session.CallFunction("prepare"));
	CHECK(Counts.Destroyed == 0 && Counts.Called == 0);
	FValue Context, Pending;
	Require(Session.CallFunction("get_context", {}, &Context));
	Require(Session.CallFunction("get_pending", {}, &Pending));
	auto NativeContext =
	    static_cast<NetworkBinding::FContextValue*>(std::get<FHostObjectHandle>(Context)->Payload)->Value;
	auto NativeOperation =
	    static_cast<NetworkBinding::FOperationValue*>(std::get<FHostObjectHandle>(Pending)->Payload)->Value;
	CHECK(!NativeContext->IsClosed() && !NativeOperation.IsComplete());
	Require(Session.LoadSource(Model, Root + "/network-completion-replacement.vs"));
	CHECK(NativeContext->IsClosed() && NativeOperation.IsComplete());
	CHECK(NativeOperation.State() == Network::EOperationState::Cancelled && Counts.Destroyed == 1 && Counts.Called == 0);
	Require(Session.CallFunction("prepare"));
	Require(Session.CallFunction("cancel"));
	CHECK(Counts.Called == 0 && Counts.Destroyed == 1);
	FValue Result;
	Require(Session.CallFunction("pump", {}, &Result));
	CHECK(std::get<std::int64_t>(Result) == 1 && Counts.Called == 1 && Counts.Destroyed == 2);
	Require(Session.CallFunction("escaped_ok", {}, &Result));
	CHECK(std::get<bool>(Result));
	Require(Session.CallFunction("pump", {}, &Result));
	CHECK(std::get<std::int64_t>(Result) == 0 && Counts.Called == 1);
	Session.Clear();
	CHECK(!Session.IsLoaded());
	// Exercise the migration transaction as well as LoadSource replacement.
	// Migrating a host handle must not revive its retired completion generation.
	Require(Session.LoadSource(Model, Root + "/network-completion-reload.vs"));
	Require(Session.CallFunction("prepare"));
	Require(Session.CallFunction("get_context", {}, &Context));
	Require(Session.CallFunction("get_pending", {}, &Pending));
	NativeContext = static_cast<NetworkBinding::FContextValue*>(std::get<FHostObjectHandle>(Context)->Payload)->Value;
	NativeOperation =
	    static_cast<NetworkBinding::FOperationValue*>(std::get<FHostObjectHandle>(Pending)->Payload)->Value;
	FCompileOptions CompileOptions;
	VeroScript::Test::Configure(CompileOptions);
	CompileOptions.ModuleEnvironment = Environment;
	auto Replacement = VeroScriptCompile::CompileSource(Model, Root + "/network-completion-reload.vs", CompileOptions);
	CHECK(Replacement.bSuccess);
	Require(Session.ReloadCompiledModule(Replacement.Module, Root + "/network-completion-reload.vs"));
	CHECK(NativeContext->IsClosed() && NativeOperation.IsComplete() && Counts.Called == 1 && Counts.Destroyed == 3);
	CHECK(!Session.CallFunction("pump").bSuccess);
	// A new load creates a usable context; initialization rollback must keep
	// the old operations cancelled rather than rebinding callbacks by name.
	Require(Session.LoadSource(Model, Root + "/network-completion-rollback.vs"));
	Require(Session.CallFunction("prepare"));
	Require(Session.CallFunction("get_context", {}, &Context));
	NativeContext = static_cast<NetworkBinding::FContextValue*>(std::get<FHostObjectHandle>(Context)->Payload)->Value;
	const std::string FailedInitialization =
	    std::string(Model) +
	    "\nint fail_initialize(){assert(false);return 0;}\nint failed_initialize=fail_initialize();\n";
	Replacement = VeroScriptCompile::CompileSource(
	    FailedInitialization, Root + "/network-completion-rollback.vs", CompileOptions);
	CHECK(Replacement.bSuccess);
	Require(Session.ReloadCompiledModule(Replacement.Module, Root + "/network-completion-rollback.vs"));
	CHECK(!Session.CallFunction("get_context", {}, &Result).bSuccess);
	CHECK(Session.IsLoaded() && NativeContext->IsClosed() && Counts.Called == 1 && Counts.Destroyed == 4);
	Require(Session.CallFunction("get_context", {}, &Result));
	CHECK(static_cast<NetworkBinding::FContextValue*>(std::get<FHostObjectHandle>(Result)->Payload)->Value->IsClosed());
	CHECK(!Session.CallFunction("pump").bSuccess);
	Session.Clear();
	CHECK(!Session.IsLoaded());
	const std::string FaultModel = std::string(Model) + R"VS(
void prepare_fault(){
 Network::operation first=Network::resolve(context,"localhost",80,5000);
 Network::operation second=Network::resolve(context,"localhost",80,5000);
 first.cancel();second.cancel();
 first.on_complete([](Network::operation op)->void{record_called();});
 second.on_complete([](Network::operation op)->void{record_called();});
}
void check_fault(bool allocation){assert(check_dispatch_failure(allocation));}
)VS";
	Require(Session.LoadSource(FaultModel, Root + "/network-completion-dispatch-fault.vs"));
	Require(Session.CallFunction("get_context", {}, &Context));
	Counts.FaultContext = std::get<FHostObjectHandle>(Context);
	for (const bool Allocation : {true, false})
	{
		Require(Session.CallFunction("prepare_fault"));
		Require(Session.CallFunction("check_fault", {Allocation}));
	}
	CHECK(Counts.Called == 1);
	Counts.FaultContext.reset();
	Session.Clear();
	CHECK(!Session.IsLoaded());
	const auto ClosureStatics = R"VS(
Network::tcp_listener listener = nullptr;
Network::operation pending = nullptr;
function() -> int saved_counter = []() -> int => 0;
int observed = 0;
Network::context saved_context()
{
	static Network::context context = Network::create_context(true);
	return context;
}
void prepare_features()
{
	Network::context context = saved_context();
	listener = Network::listen(context, "127.0.0.1", 0);
	pending = listener.accept(5000);
	int total = 40;
	const int tag = 7;
	auto counter = [count = 0]() mutable -> int
	{
		static int calls = 0;
		return ++calls + ++count;
	};
	saved_counter = counter;
	pending.on_complete([&target = total, next = counter, owner = context, tag](Network::operation completed) mutable -> void
	{
		assert(completed.done() && completed.message_id() == "network.cancelled");
		assert(next() == 2 && next() == 4 && tag == 7 && !owner.closed());
		target += next();
		observed = target;
	});
}
void cancel_features()
{
	pending.cancel();
}
int64 pump_features()
{
	saved_context().poll();
	return saved_context().dispatch(64);
}
bool features_ok()
{
	return observed == 46 && saved_counter() == 8;
}
)VS";
	Require(Session.LoadSource(ClosureStatics, Root + "/network-completion-closure-statics.vs"));
	Require(Session.CallFunction("prepare_features"));
	Require(Session.CallFunction("saved_context", {}, &Context));
	NativeContext = static_cast<NetworkBinding::FContextValue*>(std::get<FHostObjectHandle>(Context)->Payload)->Value;
	CHECK(!NativeContext->IsClosed());
	Require(Session.CallFunction("cancel_features"));
	Require(Session.CallFunction("pump_features", {}, &Result));
	CHECK(std::get<std::int64_t>(Result) == 1);
	Require(Session.CallFunction("features_ok", {}, &Result));
	CHECK(std::get<bool>(Result) && !NativeContext->IsClosed());
	Session.Clear();
	CHECK(!Session.IsLoaded() && NativeContext->IsClosed());
}
int main(int Argc, char** Argv)
{
	try
	{
		std::string Path;
		std::shared_ptr<const IJitBackendFactory> Factory;
		for (int I = 1; I < Argc; ++I)
		{
			if (std::string_view(Argv[I]) == "--jit" && I + 1 < Argc)
			{
				std::string Failure;
				Factory = LoadJitBackend(Argv[++I], Failure);
				if (!Factory)
				{
					throw std::runtime_error(Failure);
				}
			}
			else if (Path.empty())
			{
				Path = std::filesystem::absolute(Argv[I]).generic_string();
			}
			else
			{
				throw std::runtime_error("Unexpected completion test argument");
			}
		}
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			Run({}, Backend, Factory);
			if (!Path.empty())
			{
				Run(Path, Backend, Factory);
			}
		}
		std::cout << "Script completion dispatch, escaping receivers, shutdown and reload passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
