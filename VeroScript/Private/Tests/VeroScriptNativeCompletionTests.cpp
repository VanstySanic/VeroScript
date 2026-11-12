// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptSession.h"
#include "VeroScriptEngine.h"
#include "VeroScriptHost.h"
#include "VeroScriptVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptJitBackendLoader.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <new>

namespace
{
	thread_local bool FailNextAllocation = false;
}
void* operator new(std::size_t Size)
{
	if (FailNextAllocation)
	{
		FailNextAllocation = false;
		throw std::bad_alloc();
	}
	if (void* Block = std::malloc(Size ? Size : 1))
	{
		return Block;
	}
	throw std::bad_alloc();
}
void* operator new[](std::size_t Size)
{
	return ::operator new(Size);
}
void operator delete(void* Block) noexcept
{
	std::free(Block);
}
void operator delete[](void* Block) noexcept
{
	std::free(Block);
}
void operator delete(void* Block, std::size_t) noexcept
{
	std::free(Block);
}
void operator delete[](void* Block, std::size_t) noexcept
{
	std::free(Block);
}

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
struct FState
{
	FValueRoot Root;
	unsigned Destroyed = 0, Called = 0;
	bool CheckCleanupRetention = false;
	const std::thread::id Owner = std::this_thread::get_id();
};
static bool Save(const FNativeCallContext& C, FValue&, std::string& Error)
{
	return C.ScriptInvoker &&
	    C.ScriptInvoker->RetainForGeneration(C.Args[0], static_cast<FState*>(C.UserData)->Root, Error);
}
static bool CanSave(const FNativeCallContext& C, FValue& Out, std::string&)
{
	std::string Failure;
	Out = C.ScriptInvoker &&
	    C.ScriptInvoker->RetainForGeneration(C.Args[0], static_cast<FState*>(C.UserData)->Root, Failure);
	return true;
}
static bool Drop(const FNativeCallContext& C, FValue&, std::string&)
{
	static_cast<FState*>(C.UserData)->Root.Reset();
	return true;
}
static bool DropWithAllocationFailure(const FNativeCallContext& C, FValue& Out, std::string& Error)
{
	Drop(C, Out, Error);
	FailNextAllocation = true;
	return true;
}
static bool Fire(const FNativeCallContext& C, FValue&, std::string& Error)
{
	auto& S = *static_cast<FState*>(C.UserData);
	const auto* Value = S.Root.Read();
	if (!Value)
	{
		Error = "completion root is invalid";
		return false;
	}
	const auto Callable = std::get<FFunctionHandle>(*Value);
	std::vector<FValue> Arguments;
	FValue Result;
	return C.ScriptInvoker && C.ScriptInvoker->InvokeCallable(Callable, Arguments, Result, Error);
}
static bool Notify(const FNativeCallContext& C, FValue&, std::string& Error)
{
	auto& S = *static_cast<FState*>(C.UserData);
	if (S.Owner != std::this_thread::get_id())
	{
		Error = "destructor ran on another thread";
		return false;
	}
	++S.Destroyed;
	if (S.CheckCleanupRetention)
	{
		FValueRoot Forbidden;
		std::string Failure;
		if (!C.ScriptInvoker || C.ScriptInvoker->RetainForGeneration(std::int32_t{1}, Forbidden, Failure) ||
		    Failure.empty())
		{
			Error = "cleanup admitted a new asynchronous root";
			return false;
		}
	}
	return true;
}
static bool Hit(const FNativeCallContext& C, FValue&, std::string&)
{
	++static_cast<FState*>(C.UserData)->Called;
	return true;
}
static void Register(VeroScriptSession& Session, FState& State)
{
	CHECK(Session.RegisterNative("save",
	    FNativeTypeDesc::Scalar(ENativeType::Void),
	    {{FNativeTypeDesc::Function("fn()->void"), 0}},
	    Save,
	    &State));
	CHECK(Session.RegisterNative("drop", ENativeType::Void, {}, Drop, &State));
	CHECK(Session.RegisterNative("fire", ENativeType::Void, {}, Fire, &State));
	CHECK(Session.RegisterNative("notify", ENativeType::Void, {}, Notify, &State));
	CHECK(Session.RegisterNative("hit", ENativeType::Void, {}, Hit, &State));
}
static const char* Model = R"VS(
import "callable.vs";
int destroyed=0;
class Other {public: ~Other(){destroyed+=1;notify();}};
Other other=nullptr;
class Watch {public:
 ~Watch(){other=nullptr;destroyed+=1;notify();}
 void invoke(){hit();drop();}
};
void prepare(){Watch w=Watch();save([w]()mutable->void{w.invoke();});}
void bound(){Watch w=Watch();save(std::bind_method<fn()->void>(w,"invoke"));}
int count(){return destroyed;}
void release(){drop();}
void execute(){fire();}
void prepare_global(){other=Other();prepare();}
)VS";
static void SessionTests(EExecutionBackend Backend, const std::shared_ptr<const IJitBackendFactory>& Factory = {})
{
	std::ostringstream Output, Error;
	FStreamEngineHost Host(Output, Error);
	FState State;
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	FEngineConfig Config;
	Config.StdlibSearchPaths = {VEROSCRIPT_TEST_STDLIB};
	Config.bUseBytecodeCache = false;
	Config.JitBackendFactory = Factory;
	Session.ApplyConfig(Config);
	Session.SetExecutionBackend(Backend);
	Session.SetHotReloadEnabled(true);
	Register(Session, State);
	const auto Require = [&](const FRunResult& R)
	{
		if (!R.bSuccess)
		{
			throw std::runtime_error(Error.str() + R.Error.Message);
		}
	};
	Require(Session.LoadSource(Model, "native-completion.vs"));
	Require(Session.CallFunction("prepare"));
	CHECK(State.Root.IsValid() && State.Destroyed == 0);
	Require(Session.CallFunction("execute"));
	CHECK(!State.Root.IsRegistered() && State.Called == 1 && State.Destroyed == 1);
	FValue Count;
	Require(Session.CallFunction("count", {}, &Count));
	CHECK(std::get<std::int32_t>(Count) == 1);
	Require(Session.CallFunction("bound"));
	Require(Session.CallFunction("execute"));
	CHECK(State.Called == 2 && State.Destroyed == 2);
	Require(Session.CallFunction("prepare"));
	std::thread Retire(
	    [&]
	{
		State.Root.Reset();
	});
	Retire.join();
	CHECK(State.Destroyed == 2);
	Require(Session.CallFunction("count", {}, &Count));
	CHECK(State.Destroyed == 3 && std::get<std::int32_t>(Count) == 3);
	Require(Session.CallFunction("prepare"));
	FValueRoot Stale = State.Root;
	CHECK(!Session.LoadSource("class {", "failed-load.vs").bSuccess && Stale.IsValid());
	Require(Session.LoadSource(Model, "replacement.vs"));
	CHECK(!Stale.IsValid() && State.Destroyed == 4);
	Require(Session.CallFunction("prepare"));
	Stale = State.Root;
	Session.Clear();
	CHECK(!Stale.IsValid() && State.Destroyed == 5 && !Session.IsLoaded());
	Require(Session.LoadSource(Model, "exports.vs"));
	Require(Session.CallFunction("prepare"));
	FValueRoot Export;
	std::string Failure;
	CHECK(Session.RetainValue(*State.Root.Read(), Export, Failure));
	Require(Session.CallFunction("release"));
	CHECK(State.Destroyed == 5 && Export.IsValid());
	CHECK(!Session.LoadSource(Model, "blocked-replacement.vs").bSuccess && Export.IsValid());
	Export.Reset();
	Require(Session.CallFunction("count", {}, &Count));
	CHECK(State.Destroyed == 6 && std::get<std::int32_t>(Count) == 1);
	Require(Session.CallFunction("prepare_global"));
	State.CheckCleanupRetention = true;
	Require(Session.CallFunction("release"));
	State.CheckCleanupRetention = false;
	Require(Session.CallFunction("count", {}, &Count));
	CHECK(State.Destroyed == 8 && std::get<std::int32_t>(Count) == 3);
	Session.Clear();
	const char* FaultModel = R"VS(
class Fault {public:int tag=1;~Fault(){notify();assert(false);}};
void prepare(){Fault w=Fault();save([w]()->void{assert(w.tag==1);});}
)VS";
	Require(Session.LoadSource(FaultModel, "cleanup-failure.vs"));
	Require(Session.CallFunction("prepare"));
	const auto Rejected = Session.LoadSource("int main(){return 0;}", "failed-replacement.vs");
	CHECK(!Rejected.bSuccess && Rejected.Error.Message.find("assert") != std::string::npos);
	CHECK(Session.IsLoaded() && Session.HasFreeFunction("prepare", 0));
	Require(Session.LoadSource("int main(){return 0;}", "retry-replacement.vs"));
	Session.Clear();
	Require(Session.LoadSource(FaultModel, "clear-cleanup-failure.vs"));
	Require(Session.CallFunction("prepare"));
	Error.str("");
	Error.clear();
	Session.Clear();
	CHECK(Session.IsLoaded() && !Error.str().empty());
	Session.Clear();
	CHECK(!Session.IsLoaded());
}
static const char* BudgetModel = R"VS(
class Watch {public:int tag=1;~Watch(){for(int i=0;i<20;++i)hit();}};
void prepare(){Watch w=Watch();save([w]()->void{assert(w.tag==1);});}
int main(){for(int i=0;i<100;++i){prepare();drop();}return 0;}
)VS";
static void SessionBudgetTests()
{
	std::ostringstream Output, Error;
	FStreamEngineHost Host(Output, Error);
	FState State;
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	FExecutionLimits Limits;
	Limits.MaxInstructions = 5000;
	Session.SetExecutionLimits(Limits);
	Register(Session, State);
	CHECK(Session.LoadSource(BudgetModel, "native-completion-budget.vs").bSuccess);
	const auto Result = Session.CallFunction("main");
	CHECK(!Result.bSuccess && Result.Error.Message.find("instruction limit") != std::string::npos);
	CHECK(State.Called > 0 && State.Called < 2000);
}
static void FailedInitializationTests()
{
	std::ostringstream Output, Error;
	FStreamEngineHost Host(Output, Error);
	FState State;
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	Register(Session, State);
	CHECK(Session
	        .LoadSource(R"VS(
class Watch {public:int tag=1;~Watch(){notify();}};
int initialize(){Watch w=Watch();save([w]()->void{assert(w.tag==1);});assert(false);return 0;}
int initialized=initialize();
void entry(){}
)VS",
	            "failed-completion-initialization.vs")
	        .bSuccess);
	const auto Result = Session.CallFunction("entry");
	CHECK(!Result.bSuccess && Result.Error.Message.find("assert") != std::string::npos);
	CHECK(!State.Root.IsValid() && State.Destroyed == 1);
	CHECK(Session
	        .LoadSource(R"VS(
class Watch {public:int tag=1;~Watch(){notify();}};
void prepare(){Watch w=Watch();save([w]()->void{assert(w.tag==1);});}
void stop(){exit(0);}
)VS",
	            "exit-completion-scope.vs")
	        .bSuccess);
	CHECK(Session.CallFunction("prepare").bSuccess && State.Root.IsValid());
	const auto Exit = Session.CallFunction("stop");
	CHECK(Exit.bSuccess && Exit.bExitRequested && !State.Root.IsValid() && State.Destroyed == 2);
}
static void BareVmTests(EExecutionBackend Backend, const std::shared_ptr<const IJitBackendFactory>& Factory = {})
{
	FState State;
	std::vector<FNativeFunction> Natives;
	for (const auto& Entry : {std::make_pair("save", Save),
	         std::make_pair("can_save", CanSave),
	         std::make_pair("drop", Drop),
	         std::make_pair("notify", Notify),
	         std::make_pair("hit", Hit),
	         std::make_pair("fire", Fire)})
	{
		FNativeFunction Native;
		Native.Name = Entry.first;
		Native.Callback = Entry.second;
		Native.UserData = &State;
		if (Native.Name == "save" || Native.Name == "can_save")
		{
			Native.ParamTypes = {ENativeType::Function};
			Native.ParamHostTypeNames = {"fn()->void"};
		}
		if (Native.Name == "can_save")
		{
			Native.ReturnType = ENativeType::Bool;
		}
		Natives.push_back(std::move(Native));
	}
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	Options.Natives = &Natives;
	const auto Code = VeroScriptCompile::CompileSource(R"VS(
int destroyed=0;
class Watch {public:int tag=1;~Watch(){destroyed+=1;notify();}};
void prepare(){Watch w=Watch();save([w]()->void{assert(w.tag==1);});}
int main(){prepare();assert(destroyed==0);drop();assert(destroyed==1);prepare();return 0;}
)VS",
	    "bare-native-completion.vs",
	    Options);
	if (!Code.bSuccess)
	{
		throw std::runtime_error(Code.Error.Message);
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	VeroScript::Test::Configure(Vm);
	Vm.SetExecutionBackend(Backend);
	std::string Failure;
	if (Factory && !Vm.SetJitBackendFactory(Factory, Failure))
	{
		throw std::runtime_error(Failure);
	}
	CHECK(Vm.Execute(*Code.Module, ERunMode::Script, {}, &Natives));
	CHECK(Vm.GetProcessExitCode() == 0 && !State.Root.IsValid() && State.Destroyed == 2);
	const auto FinalCode = VeroScriptCompile::CompileSource(R"VS(
class Final {public:~Final(){assert(!can_save([]()->void{}));notify();}};
Final value=Final();
int main(){return 0;}
)VS",
	    "final-completion-cleanup.vs",
	    Options);
	CHECK(FinalCode.bSuccess);
	CHECK(Vm.Execute(*FinalCode.Module, ERunMode::Script, {}, &Natives));
	CHECK(!State.Root.IsValid() && State.Destroyed == 3);
	const auto BudgetCode = VeroScriptCompile::CompileSource(BudgetModel, "bare-completion-budget.vs", Options);
	CHECK(BudgetCode.bSuccess);
	FExecutionLimits Limits;
	Limits.MaxInstructions = 5000;
	Vm.SetExecutionLimits(Limits);
	CHECK(!Vm.Execute(*BudgetCode.Module, ERunMode::Script, {}, &Natives));
	CHECK(Vm.GetError().Message.find("instruction limit") != std::string::npos);
	CHECK(State.Called > 0 && State.Called < 2000);
}
static void CleanupAllocationTests()
{
	FState State;
	std::vector<FNativeFunction> Natives;
	for (const auto& Entry : {std::make_pair("save", Save),
	         std::make_pair("drop_allocation", DropWithAllocationFailure),
	         std::make_pair("notify", Notify)})
	{
		FNativeFunction Native;
		Native.Name = Entry.first;
		Native.Callback = Entry.second;
		Native.UserData = &State;
		if (Native.Name == "save")
		{
			Native.ParamTypes = {ENativeType::Function};
			Native.ParamHostTypeNames = {"fn()->void"};
		}
		Natives.push_back(std::move(Native));
	}
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	Options.Natives = &Natives;
	const char* Source = R"VS(
class Watch {public:int tag=1;~Watch(){notify();}};
void prepare(){Watch w=Watch();save([w]()->void{assert(w.tag==1);});}
int main(){prepare();drop_allocation();return 0;}
)VS";
	const auto Code = VeroScriptCompile::CompileSource(Source, "native-root-allocation-failure.vs", Options);
	CHECK(Code.bSuccess);
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	VeroScript::Test::Configure(Vm);
	Vm.SetExecutionBackend(EExecutionBackend::Vm);
	CHECK(!Vm.Execute(*Code.Module, ERunMode::Script, {}, &Natives));
	CHECK(!FailNextAllocation && Vm.GetError().Message.find("memory") != std::string::npos);
	CHECK(!State.Root.IsValid() && State.Destroyed == 1);
	const auto Next = VeroScriptCompile::CompileSource("int main(){return 0;}", "reuse-after-allocation.vs", Options);
	CHECK(Next.bSuccess && Vm.Execute(*Next.Module, ERunMode::Script, {}, &Natives));
	FStreamEngineHost Host(Output, Output);
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	Session.SetExecutionBackend(EExecutionBackend::Vm);
	Register(Session, State);
	CHECK(Session.RegisterNative("drop_allocation", ENativeType::Void, {}, DropWithAllocationFailure, &State));
	CHECK(Session.LoadSource(Source, "session-root-allocation-failure.vs").bSuccess);
	const auto Result = Session.CallFunction("main");
	CHECK(!Result.bSuccess && Result.Error.Message.find("memory") != std::string::npos);
	CHECK(!FailNextAllocation && !State.Root.IsValid() && State.Destroyed == 2);
	CHECK(Session.LoadSource("int main(){return 0;}", "session-reuse-after-allocation.vs").bSuccess);
	CHECK(Session.CallFunction("main").bSuccess);
}
int main(int Argc, char** Argv)
{
	try
	{
		std::shared_ptr<const IJitBackendFactory> Factory;
		if (Argc == 2)
		{
			std::string Failure;
			Factory = LoadJitBackend(Argv[1], Failure);
			if (!Factory)
			{
				throw std::runtime_error(Failure);
			}
		}
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}
			SessionTests(Backend, Factory);
			BareVmTests(Backend, Factory);
		}
		SessionBudgetTests();
		FailedInitializationTests();
		CleanupAllocationTests();
		std::cout << "Owned native completions, closures, bound receivers and generation cleanup passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
