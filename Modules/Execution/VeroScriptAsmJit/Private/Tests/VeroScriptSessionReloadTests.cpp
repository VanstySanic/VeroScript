// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptAsmJit.h"
#include "VeroScriptCompile.h"
#include "VeroScriptCompiledExecution.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptSession.h"
#include "../../../../../VeroScript/Private/Tests/VeroScriptTestJitCapabilities.h"
#include <iostream>
#include <stdexcept>

using namespace VeroScript;
namespace
{
	std::shared_ptr<const IJitBackendFactory> Factory;
	struct FHost final : IEngineHost
	{
		void OnPrintLine(std::string_view) override
		{
		}
		void OnDiagnosticLine(std::string_view) override
		{
		}
	};
	void Require(const bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	void Check(const FRunResult& Result, const std::string& Action)
	{
		Require(Result.bSuccess, Action + ": " + Result.Error.Message);
	}
	std::int32_t Call(VeroScriptSession& Session, const char* Name, const std::vector<FValue>& Args = {})
	{
		FValue Value;
		Check(Session.CallFunction(Name, Args, &Value), Name);
		return std::get<std::int32_t>(Value);
	}
	void Configure(VeroScriptSession& Session, const EExecutionBackend Backend)
	{
		Test::Configure(Session);
		Session.SetJitBackendFactory(Factory);
		Session.SetExecutionBackend(Backend);
		Session.SetHotReloadEnabled(true);
		FJitPolicy Policy;
		Policy.MixedHotCallThreshold = 1;
		Session.SetJitPolicy(Policy);
	}
	void PersistentCalls(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		std::int32_t NativeValue = 11;
		Require(Session.RegisterNative("host_value", ENativeType::Int, {},
			[](const FNativeCallContext& Context, FValue& Value, std::string&)
			{
				Value = *static_cast<std::int32_t*>(Context.UserData);
				return true;
			}, &NativeValue), "register persistent native");
		Check(Session.LoadSource("int count=0;int next(){return ++count;}int native_value(){return host_value();}"
			"int local(){static int n=0;return ++n;}void main(){}", "persistent-jit.vs"), "load persistent");
		Check(Session.Run(), "initialize persistent");
		for (int I = 1; I <= 4; ++I)
		{
			Require(Call(Session, "next") == I && Call(Session, "local") == I, "globals and local statics persist");
			Require(Call(Session, "native_value") == NativeValue, "native callback reads current host state");
			++NativeValue;
		}
		if (Backend != EExecutionBackend::Vm)
		{
			Require(Session.GetLastJitStats().Dispatches > 0 && Session.GetLastJitStats().CompileAttempts == 0,
				"persistent native entry dispatches warm generation code");
		}
	}
	void ObservableCalls(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		Check(Session.LoadSource("class Cell{public:int value=0;int Read(){return value;}"
			"void Set(ref int n){value=n;n+=1;}};Cell cell=Cell();"
			"int update(int n){cell.value=n;return cell.Read();}"
			"int update_ref(ref int n){cell.Set(ref n);return n;}void main(){}", "observable-jit.vs"), "load observable");
		Check(Session.Run(), "initialize observable");
		std::vector<std::string> Events;
		Session.SetMutationCallback([&](const FMutationEvent& Event)
		{
			Events.push_back("write:" + std::to_string(std::get<std::int32_t>(*Event.Value)));
			Event.Object->Fields.reserve(Event.Object->Fields.capacity() + 8);
		});
		Session.SetInstanceCallInterceptor([&](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			Events.push_back("call:" + std::string(Context.Target.MethodName));
			if (!Context.Proceed(Value, Error)) return EInstanceCallInterceptResult::Failed;
			if (Context.Target.MethodName == "Read")
			{
				Value = std::get<std::int32_t>(Value) + 1;
			}
			return EInstanceCallInterceptResult::Handled;
		});
		for (int I = 1; I <= 3; ++I)
		{
			Events.clear();
			Require(Call(Session, "update", {std::int32_t{I}}) == I + 1, "intercepted Proceed result");
			Require(Events == std::vector<std::string>{"write:" + std::to_string(I), "call:Read"},
				"observation order and values match VM");
			if (Backend != EExecutionBackend::Vm && Test::SupportsAsmJitControlFlow(Factory->GetDescription()))
			{
				std::string Reasons;
				if (Session.GetLastJitStats().ControlledNativeExecutions == 0)
				{
					for (const auto& Function : Session.GetDebugProgram().Module->Functions)
					{
						std::vector<FCompiledInstruction> Plan;
						std::string Reason;
						if (!BuildCompiledFunctionPlan(Function, Plan, Reason))
						{
							Reasons += "\n" + Function.Name + ": " + Reason;
						}
					}
				}
				Require(Session.GetLastJitStats().ControlledNativeExecutions >= 2,
					"observable caller and method use native Core-barrier execution; entries=" +
					std::to_string(Session.GetLastJitStats().ControlledNativeExecutions) + Reasons);
			}
		}
		Events.clear();
		std::vector<FValue> Arguments{std::int32_t{12}};
		FValue Value;
		Check(Session.CallFunction("update_ref", Arguments, &Value), "intercepted reference call");
		Require(std::get<std::int32_t>(Value) == 13 && std::get<std::int32_t>(Arguments.front()) == 13 &&
			Events == std::vector<std::string>{"call:Set", "write:12"}, "Proceed preserves ref write-back and notification order");
	}
	void InterceptorCallLocation(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		Check(Session.LoadSource("class Cell{\npublic:\nint Read(){return 1;}\n};\nCell cell=Cell();\n"
			"int outer(){\nreturn cell.Read();\n}\nvoid main(){}\n", "interceptor-site.vs"), "load interceptor location");
		Check(Session.Run(), "initialize interceptor location");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext&, FValue&, FScriptError& Error)
		{
			Error.Message = "intentional interceptor failure";
			return EInstanceCallInterceptResult::Failed;
		});
		const auto Result = Session.CallFunction("outer", {});
		Require(!Result.bSuccess && Result.Error.Location.FileLabel == "interceptor-site.vs" && Result.Error.Location.Line == 7,
			"interceptor error uses the call site; mode=" + std::to_string(static_cast<int>(Backend)) +
			" line=" + std::to_string(Result.Error.Location.Line) + ": " + Result.Error.Message);
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			if (!Context.Proceed(Value, Error)) return EInstanceCallInterceptResult::Failed;
			Value = std::string("wrong return type");
			return EInstanceCallInterceptResult::Handled;
		});
		const auto Mismatch = Session.CallFunction("outer", {});
		Require(!Mismatch.bSuccess && Mismatch.Error.Location.Line == 7, "post-Proceed type errors retain the caller location");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext&, FValue&, FScriptError& Error)
		{
			Error.Message = "host supplied location";
			Error.Location.FileLabel = "host-interceptor.vs";
			Error.Location.Line = 42;
			return EInstanceCallInterceptResult::Failed;
		});
		const auto Explicit = Session.CallFunction("outer", {});
		Require(!Explicit.bSuccess && Explicit.Error.Location.FileLabel == "host-interceptor.vs" && Explicit.Error.Location.Line == 42,
			"explicit host diagnostics are preserved");
	}
	void NestedInterception(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		Check(Session.LoadSource("class Probe{\npublic:\nint value=7;\nint Leaf(){return ++value;}\n"
			"int Chain(){return Leaf();}\nint& Ref(){return value;}\nint Fail(int n){\nreturn value/n;\n}\n};\n"
			"Probe probe=Probe();\nint nested(){return probe.Chain();}\nint refs(){probe.Ref()=23;return probe.value;}\n"
			"int failing(){return probe.Fail(0);}\nvoid main(){}\n", "nested-interception.vs"), "load nested interception");
		Check(Session.Run(), "initialize nested interception");
		std::vector<std::string> Events;
		Session.SetMutationCallback([&](const FMutationEvent& Event)
		{
			Events.push_back("write:" + std::to_string(std::get<std::int32_t>(*Event.Value)));
			Event.Object->Fields.reserve(Event.Object->Fields.capacity() + 8);
		});
		Session.SetInstanceCallInterceptor([&](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			Events.push_back(std::string(Context.Target.MethodName));
			Context.Proceed(Value, Error);
			// A handler cannot turn a failed original call into success.
			return EInstanceCallInterceptResult::Handled;
		});
		Require(Call(Session, "nested") == 8 && Events == std::vector<std::string>{"Chain", "Leaf", "write:8"},
			"nested method interception and mutation order");
		if (Backend != EExecutionBackend::Vm && Test::SupportsAsmJitControlFlow(Factory->GetDescription()))
		{
			Require(Session.GetLastJitStats().ControlledNativeExecutions >= 3, "supported nested method bodies remain native");
		}
		Events.clear();
		Require(Call(Session, "refs") == 23 && Events == std::vector<std::string>{"Ref", "write:23"},
			"intercepted reference return keeps write-back and relocation semantics");
		const auto Failed = Session.CallFunction("failing", {});
		Require(!Failed.bSuccess && Failed.Error.MessageId == "diag.division_by_zero" && Failed.Error.Location.Line == 8,
			"nested failure keeps the original body diagnostic");
		Require(!Session.RequiresReset() && Call(Session, "nested") == 24, "ordinary nested failure leaves the Session reusable");
	}
	void InterceptedValueReturns(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		Check(Session.LoadSource(
			"int copies=0;struct S{int value=7;S()=default;S(const S& other){value=other.value;copies++;}};"
			"struct NoCopy{int value=11;NoCopy()=default;NoCopy(const NoCopy& other)=delete;};"
			"struct Derived:public S{int extra=3;};"
			"class Maker{public:S value=S();Derived derived=Derived();S Read(){return value;}NoCopy Make(){return NoCopy();}};Maker maker=Maker();"
			"int take(){copies=0;S result=maker.Read();result.value=9;return copies*100+maker.value.value;}"
			"int noncopy(){NoCopy result=maker.Make();return result.value;}"
			"int reset(){copies=0;return 0;}int count(){return copies;}void main(){}",
			"intercepted-values.vs"), "load intercepted value returns");
		Check(Session.Run(), "initialize intercepted values");
		Require(Call(Session, "take") == 107, "ordinary value return copies once and isolates storage");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			return Context.Proceed(Value, Error) ? EInstanceCallInterceptResult::Handled : EInstanceCallInterceptResult::Failed;
		});
		Require(Call(Session, "take") == 107, "Proceed preserves the original value return without a second copy");
		Require(Call(Session, "noncopy") == 11, "Proceed preserves an owned noncopyable return value");
		FObjectHandle Object;
		Check(Session.ConstructObject("Maker", {}, Object), "construct value-return receiver");
		Call(Session, "reset");
		FValue Returned;
		Check(Session.CallMethod(Object, "Read", {}, &Returned), "host entry proceeds through a value return");
		Require(Call(Session, "count") == 1, "host entry preserves the original copy boundary");
		const auto Borrowed = std::get<FObjectHandle>(Object->Fields.front());
		for (const bool ProceedFirst : {false, true})
		{
			Session.SetInstanceCallInterceptor([Borrowed, ProceedFirst](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
			{
				if (ProceedFirst && !Context.Proceed(Value, Error)) return EInstanceCallInterceptResult::Failed;
				Value = Borrowed;
				return EInstanceCallInterceptResult::Handled;
			});
			Call(Session, "reset");
			Check(Session.CallMethod(Object, "Read", {}, &Returned), "interceptor substitutes a borrowed value");
			Require(Call(Session, "count") == (ProceedFirst ? 2 : 1) && std::get<FObjectHandle>(Returned) != Borrowed,
				"replacement values retain their own copy boundary even after Proceed");
		}
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			if (!Context.Proceed(Value, Error)) return EInstanceCallInterceptResult::Failed;
			const auto& Receiver = std::get<FObjectHandle>(Context.Receiver);
			Receiver->Fields.front() = std::move(Value);
			Value = FThisFieldRef{Receiver, 0};
			return EInstanceCallInterceptResult::Handled;
		});
		Call(Session, "reset");
		Check(Session.CallMethod(Object, "Read", {}, &Returned), "borrow a stored Proceed result");
		Require(Call(Session, "count") == 2 &&
			std::get<FObjectHandle>(Returned) != std::get<FObjectHandle>(Object->Fields.front()),
			"a reference to a stored Proceed result still copies for a value return");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError&)
		{
			Value = std::get<FObjectHandle>(Context.Receiver)->Fields.at(1);
			return EInstanceCallInterceptResult::Handled;
		});
		Call(Session, "reset");
		Check(Session.CallMethod(Object, "Read", {}, &Returned), "substitute a derived struct for a base return");
		Require(Call(Session, "count") == 1 && std::get<FObjectHandle>(Returned)->Fields.size() == 1,
			"the intercepted value is copied through the declared base type");
	}
	void InterceptedArrayReturns(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		Check(Session.LoadSource(
			"int copies=0;struct S{int value=7;S()=default;S(const S& other){value=other.value;copies++;}};"
			"using Values=S[2];class Maker{public:Values values;Values Read(){return values;}};Maker maker=Maker();"
			"int take(){copies=0;Values result=maker.Read();result[0].value=9;return copies*100+maker.values[0].value;}"
			"int reset(){copies=0;return 0;}int count(){return copies;}void main(){}",
			"intercepted-arrays.vs"), "load intercepted array returns");
		Check(Session.Run(), "initialize intercepted arrays");
		Require(Call(Session, "take") == 207, "ordinary fixed array return copies each element once");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			FValue Original;
			if (!Context.Proceed(Original, Error)) return EInstanceCallInterceptResult::Failed;
			Value = std::move(Original);
			return EInstanceCallInterceptResult::Handled;
		});
		Require(Call(Session, "take") == 207, "moving a Proceed array result preserves its prepared elements");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			FValue Original;
			if (!Context.Proceed(Original, Error)) return EInstanceCallInterceptResult::Failed;
			Value = Original;
			return EInstanceCallInterceptResult::Handled;
		});
		Require(Call(Session, "take") == 407, "a copied array result crosses a new value boundary");
		FObjectHandle Object;
		Check(Session.ConstructObject("Maker", {}, Object), "construct array-return receiver");
		const FValue Borrowed = Object->Fields.front();
		for (const bool ProceedFirst : {false, true})
		{
			Session.SetInstanceCallInterceptor([Borrowed, ProceedFirst](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
			{
				if (ProceedFirst && !Context.Proceed(Value, Error)) return EInstanceCallInterceptResult::Failed;
				Value = Borrowed;
				return EInstanceCallInterceptResult::Handled;
			});
			Call(Session, "reset");
			FValue Returned;
			Check(Session.CallMethod(Object, "Read", {}, &Returned), "substitute a fixed array");
			const auto& Result = std::get<FNativeArray>(Returned).Storage().Elements;
			const auto& Source = std::get<FNativeArray>(Borrowed).Storage().Elements;
			Require(Call(Session, "count") == (ProceedFirst ? 4 : 2) &&
				std::get<FObjectHandle>(Result[0]) != std::get<FObjectHandle>(Source[0]),
				"substituted arrays still copy their elements after optional Proceed");
		}
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			if (!Context.Proceed(Value, Error)) return EInstanceCallInterceptResult::Failed;
			const auto& Receiver = std::get<FObjectHandle>(Context.Receiver);
			Receiver->Fields.front() = std::move(Value);
			Value = FThisFieldRef{Receiver, 0};
			return EInstanceCallInterceptResult::Handled;
		});
		Call(Session, "reset");
		FValue Returned;
		Check(Session.CallMethod(Object, "Read", {}, &Returned), "borrow a stored Proceed array");
		const auto& Result = std::get<FNativeArray>(Returned).Storage().Elements;
		const auto& Source = std::get<FNativeArray>(Object->Fields.front()).Storage().Elements;
		Require(Call(Session, "count") == 4 &&
			std::get<FObjectHandle>(Result[0]) != std::get<FObjectHandle>(Source[0]),
			"a reference to a stored Proceed array still copies its elements for a value return");
	}
	void InterceptedReturnCleanup(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		Check(Session.LoadSource(
			"int destroyed=0;struct Item{int value=7;~Item(){destroyed++;}};using Items=Item[2];"
			"class Maker{public:Item fallback=Item();Items items;Item Make(){return Item();}Items Read(){return items;}};"
			"Maker maker=Maker();void single(){destroyed=0;Item result=maker.Make();}"
			"void run_array(){destroyed=0;Items result=maker.Read();}"
			"int count(){return destroyed;}void main(){}", "intercepted-return-cleanup.vs"),
			"load intercepted return cleanup");
		Check(Session.Run(), "initialize intercepted return cleanup");
		const auto RunAndCount = [&](const char* Function)
		{
			Check(Session.CallFunction(Function, {}), Function);
			return Call(Session, "count");
		};
		Require(RunAndCount("single") == 1 && RunAndCount("run_array") == 2, "ordinary results release at call completion");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			return Context.Proceed(Value, Error) ? EInstanceCallInterceptResult::Handled : EInstanceCallInterceptResult::Failed;
		});
		Require(RunAndCount("single") == 1 && RunAndCount("run_array") == 2, "forwarded results retain ordinary destruction counts");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			if (!Context.Proceed(Value, Error)) return EInstanceCallInterceptResult::Failed;
			Value = std::get<FObjectHandle>(Context.Receiver)->Fields.front();
			return EInstanceCallInterceptResult::Handled;
		});
		Require(RunAndCount("single") == 2, "a replaced Proceed result and its replacement are both destroyed");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			FValue Original;
			if (!Context.Proceed(Original, Error)) return EInstanceCallInterceptResult::Failed;
			Value = Original;
			return EInstanceCallInterceptResult::Handled;
		});
		Require(RunAndCount("run_array") == 4, "copy forwarding releases the original array elements and the returned copies");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			if (!Context.Proceed(Value, Error)) return EInstanceCallInterceptResult::Failed;
			[[maybe_unused]] const FValue Discarded = std::move(Value);
			throw std::runtime_error("interceptor cleanup probe");
		});
		for (const auto* Function : {"single", "run_array"})
		{
			const auto Failed = Session.CallFunction(Function, {});
			const int Expected = std::string_view(Function) == "single" ? 1 : 2;
			Require(!Failed.bSuccess && Failed.Error.Message.find("interceptor cleanup probe") != std::string::npos &&
				!Session.RequiresReset() && Call(Session, "count") == Expected,
				"callback failure cleans moved results and preserves the primary error");
		}
	}
	void ControlledInterception(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		FJitPolicy Policy;
		Policy.MixedHotCallThreshold = 1;
		Policy.bEnableControlledMixed = true;
		Session.SetJitPolicy(Policy);
		Check(Session.LoadSource("class Runner{public:void Burn(){while(true){}}};Runner runner=Runner();int cleaned=0;"
			"void exhaust(){try{runner.Burn();}finally{cleaned=1;}}int read(){return cleaned;}void main(){}",
			"controlled-interception.vs"), "load controlled interception");
		Check(Session.Run(), "initialize controlled interception");
		Session.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext& Context, FValue& Value, FScriptError& Error)
		{
			return Context.Proceed(Value, Error) ? EInstanceCallInterceptResult::Handled : EInstanceCallInterceptResult::Failed;
		});
		// Exercise polling-capable native interception; quota-only loops may
		// intentionally select the VM's lower-cost exact batching.
		Session.SetExecutionControl(std::make_shared<FExecutionControl>());
		Session.SetExecutionLimits({100, 0});
		const auto Failed = Session.CallFunction("exhaust", {});
		Require(!Failed.bSuccess && Failed.Error.MessageId == "diag.runtime_execution_instruction_limit_exceeded" &&
			!Session.RequiresReset(), "intercepted methods preserve instruction limits and bounded cleanup");
		if (Backend != EExecutionBackend::Vm && Test::SupportsAsmJitControlFlow(Factory->GetDescription()))
		{
			Require(Session.GetLastJitStats().ControlledNativeExecutions > 0, "governed intercepted method entered native code");
		}
		Session.SetExecutionLimits({});
		Require(Call(Session, "read") == 1, "finally executes after a controlled intercepted method is interrupted");
	}
	void MigratableRoots(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		const std::string Label = "root-migration.vs";
		const auto Source = [](const char* Fields, const char* Body, const char* Extra = "")
		{
			return std::string("class Root{public:int value=7;") + Fields + "int Read(){" + Body + "}};" + Extra;
		};
		Check(Session.LoadSource(Source("", "return value;", "void main(){}"), Label), "load roots");
		Check(Session.Run(), "initialize roots");
		FObjectHandle Object;
		Check(Session.ConstructObject("Root", {}, Object), "construct host-only root");
		FValueRoot First, Alias;
		std::string Error;
		Require(Session.RetainMigratableValue(FValue(Object), First, Error) &&
			Session.RetainMigratableValue(FValue(Object), Alias, Error), "retain migration aliases: " + Error);
		const auto Previous = Session.GetDebugProgram().Generation;
		const auto Stage = [&](const std::string& Text)
		{
			auto Code = VeroScriptCompile::CompileSource(Text, Label, Test::MakeCompileOptions());
			Require(Code.bSuccess, "compile replacement: " + Code.Error.Message);
			return Session.ReloadCompiledModule(Code.Module, Label);
		};
		Check(Stage(Source("int added=9;", "return value+added;", "void main(){}")), "stage roots");
		const auto Pending = Session.GetReloadStatus();
		Require(Pending.State == EReloadState::Pending && Pending.OperationId != 0 &&
			Pending.PreviousGeneration == Previous && Pending.CommittedGeneration == Previous && !First.Read(),
			"staging is observable and roots are suspended");
		Check(Session.Run(), "commit roots");
		const auto Applied = Session.GetReloadStatus();
		Require(Applied.State == EReloadState::Applied && Applied.CurrentGeneration == Applied.CommittedGeneration &&
			std::get<FObjectHandle>(*First.Read()).get() == Object.get() &&
			std::get<FObjectHandle>(*Alias.Read()).get() == Object.get() && Object->Fields.size() == 2,
			"host-only roots migrate with alias identity");
		FValue Read;
		Check(Session.CallMethod(Object, "Read", {}, &Read), "read migrated object");
		Require(std::get<std::int32_t>(Read) == 16, "added field receives its language default");
		const auto Rejected = Stage("class Root{public:string value=\"changed\";int Read(){return 1;}};void main(){}");
		Require(!Rejected.bSuccess && Session.GetReloadStatus().State == EReloadState::Rejected && First.Read() &&
			Object->Fields.size() == 2 && Session.GetReloadStatus().CurrentGeneration == Applied.CurrentGeneration,
			"incompatible migration preserves existing roots and resumes their access");
		Check(Stage(Source("int added=9;int failed=12;", "return value+added+failed;",
			"int divide(int n){return 10/n;}int trigger=divide(0);void main(){}")), "stage failing initialization");
		const auto FailedOperation = Session.GetReloadStatus();
		Require(!Session.Run().bSuccess, "initialization fails");
		const auto RolledBack = Session.GetReloadStatus();
		Require(RolledBack.State == EReloadState::RolledBack && RolledBack.CurrentGeneration == Applied.CurrentGeneration &&
			RolledBack.CommittedGeneration == Applied.CommittedGeneration && First.Read() && Object->Fields.size() == 2,
			"rollback restores host roots and committed generation");
		Check(Session.CallMethod(Object, "Read", {}, &Read), "read rolled-back object");
		Require(std::get<std::int32_t>(Read) == 16, "rolled-back object uses previous code");
		Check(Stage(Source("int added=9;int committed=13;", "return value+added+committed;",
			"int divide(int n){return 10/n;}void main(){divide(0);}")), "stage business error");
		Require(Session.GetReloadStatus().OperationId > FailedOperation.OperationId &&
			Session.GetReloadStatus().CandidateGeneration == FailedOperation.CandidateGeneration,
			"operation identity remains unique when a rolled-back candidate generation is reused");
		Require(!Session.Run().bSuccess, "business failure after initialization");
		const auto BusinessFailure = Session.GetReloadStatus();
		Require(BusinessFailure.State == EReloadState::AppliedWithErrors && First.Read() && Object->Fields.size() == 3 &&
			BusinessFailure.CurrentGeneration == BusinessFailure.CommittedGeneration,
			"business failure preserves a committed replacement");
	}
	void BindingRevisions()
	{
		FHost Host;
		auto Catalog = std::make_shared<FHostBindingRegistry>();
		std::string Error;
		std::int32_t Number = 41;
		const auto Register = [&]
		{
			Require(Catalog->RegisterNative("host_value", ENativeType::Int, {},
				[](const FNativeCallContext& Context, FValue& Value, std::string&)
				{
					Value = *static_cast<std::int32_t*>(Context.UserData);
					return true;
				}, &Number, Error), "register external catalog: " + Error);
		};
		Register();
		VeroScriptSession Session(Host);
		Test::Configure(Session);
		FEngineConfig Config;
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "project";
		Description.Scopes = {{"project", {}}};
		Description.Bindings = Catalog;
		Config.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
		Require(Config.ModuleEnvironment != nullptr, "external binding environment: " + Error);
		Config.JitBackendFactory = Factory;
		Config.ExecutionBackend = EExecutionBackend::Jit;
		Session.ApplyConfig(Config);
		Check(Session.LoadSource("int value(){return host_value();}void main(){}", "<binding-revisions>"), "load catalog");
		Require(Call(Session, "value") == 41 && Call(Session, "value") == 41 &&
			Session.GetLastJitStats().CompileAttempts == 0, "stable external catalog reuses code");
		const auto Revision = Catalog->GetRevision();
		Catalog->Clear();
		Number = 73;
		Register();
		Require(Catalog->GetRevision() != Revision && Call(Session, "value") == 73 &&
			Session.GetLastJitStats().CompileAttempts > 0, "catalog revision invalidates code before the next invocation");
		Require(Call(Session, "value") == 73 && Session.GetLastJitStats().CompileAttempts == 0,
			"replacement catalog warms independently");
		Catalog->InvalidateRuntimeBindings();
		Require(Call(Session, "value") == 73 && Session.GetLastJitStats().CompileAttempts > 0,
			"explicit host invalidation never trusts address equality");
		const auto BeforeAssignment = Catalog->GetRevision();
		auto Snapshot = *Catalog;
		*Catalog = Snapshot;
		Require(Catalog->GetRevision() != BeforeAssignment && Call(Session, "value") == 73 &&
			Session.GetLastJitStats().CompileAttempts > 0, "assigning a registry snapshot invalidates retained code");
		const auto BeforeMove = Catalog->GetRevision();
		auto Moved = std::move(*Catalog);
		Require(Catalog->GetRevision() != BeforeMove, "moving a catalog invalidates its original address");
		*Catalog = std::move(Moved);
		Require(Call(Session, "value") == 73 && Session.GetLastJitStats().CompileAttempts > 0,
			"restored catalog ownership is validated before reuse");
	}
	void MigratableGraphs(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		const auto Source = [](const char* Extra)
		{
			return std::string("class Graph{public:int value=7;Graph link;array<Graph> items;variant payload;") + Extra +
				"};fn()->int make(){Graph node=Graph();node.link=node;array<Graph> list={node};"
				"node.items=list;node.payload=node;return [node]()->int{return node.link.value+node.items[0].value;};}"
				"int invoke(fn()->int f){return f();}void break_cycle(Graph n){n.link=nullptr;n.items.clear();n.payload=0;}void main(){}";
		};
		Check(Session.LoadSource(Source(""), "migration-graph.vs"), "load graph");
		FValue Value;
		Check(Session.CallFunction("make", {}, &Value), "create host-only closure graph");
		FValueRoot Root;
		std::string Error;
		Require(Session.RetainMigratableValue(Value, Root, Error), "retain closure graph: " + Error);
		Value = {};
		auto Replacement = VeroScriptCompile::CompileSource(Source("int added=9;"), "migration-graph.vs", Test::MakeCompileOptions());
		Require(Replacement.bSuccess, "compile graph replacement: " + Replacement.Error.Message);
		Check(Session.ReloadCompiledModule(Replacement.Module, "migration-graph.vs"), "stage closure graph");
		Require(!Root.Read(), "closure graph stays suspended during staging");
		Check(Session.Run(), "commit closure graph");
		Require(Root.Read() && Call(Session, "invoke", {*Root.Read()}) == 14,
			"host-only closure, cycle, variant and array survive migration");
		Replacement = VeroScriptCompile::CompileSource(Source("int added=9;int failed=13;") +
			"int divide(int n){return 10/n;}int trigger=divide(0);", "migration-graph.vs", Test::MakeCompileOptions());
		Require(Replacement.bSuccess, "compile graph rollback candidate: " + Replacement.Error.Message);
		Check(Session.ReloadCompiledModule(Replacement.Module, "migration-graph.vs"), "stage closure graph rollback");
		Require(!Session.Run().bSuccess && Session.GetReloadStatus().State == EReloadState::RolledBack &&
			Root.Read() && Call(Session, "invoke", {*Root.Read()}) == 14, "rollback restores a host-only cyclic closure graph");
		// Break the explicit script cycle before releasing its last host root.
		const auto Closure = std::get<FFunctionHandle>(*Root.Read());
		const auto Node = std::get<FObjectHandle>(Closure->Captures.front());
		Require(Node->Fields.size() == 5, "objects reachable only through a closure receive the new layout");
		Check(Session.CallFunction("break_cycle", {Node}), "release explicit script graph cycles");
	}
	void ReloadCleanupFailures(const EExecutionBackend Backend)
	{
		FHost Host;
		VeroScriptSession Session(Host);
		Configure(Session, Backend);
		Require(Session.RegisterNative("fail_cleanup", ENativeType::Void, {},
			[](const FNativeCallContext&, FValue&, std::string& Error)
			{
				Error = "expected retiring callback cleanup failure";
				return false;
			}), "register failing cleanup");
		Check(Session.LoadSource("class Removed{public:~Removed(){fail_cleanup();}};"
			"fn()->int make(){Removed value=Removed();return [value]()->int{return 1;};}void main(){}",
			"cleanup-status.vs"), "load cleanup fixture");
		FValue Value;
		Check(Session.CallFunction("make", {}, &Value), "make retiring callback");
		Require(static_cast<bool>(Session.RetainCallable(std::get<FFunctionHandle>(Value))), "retain old callback");
		Value = {};
		auto Replacement = VeroScriptCompile::CompileSource("int read(){return 9;}void main(){}", "cleanup-status.vs", Test::MakeCompileOptions());
		Require(Replacement.bSuccess, "compile cleanup replacement");
		Check(Session.ReloadCompiledModule(Replacement.Module, "cleanup-status.vs"), "stage cleanup replacement");
		Require(!Session.Run().bSuccess && Session.GetReloadStatus().State == EReloadState::AppliedWithErrors &&
			!Session.RequiresReset() && Call(Session, "read") == 9,
			"retirement failure reports an applied generation with errors");
		Require(Session.GetReloadStatus().State == EReloadState::AppliedWithErrors, "later calls preserve the last reload result");
	}
	void ReloadReset(const EExecutionBackend Backend)
	{
		std::weak_ptr<FScriptObject> Observed;
		FHost Host;
		{
			VeroScriptSession Session(Host);
			Configure(Session, Backend);
			const std::string Source =
				"class Item{public:int id;Item(){id=0;}Item(int n){id=n;}"
				"~Item(){if(id==2){try{for(;;){}}finally{for(;;){}}}}};"
				"class Owner{public:Item item=Item(1);Owner next;Owner(){next=this;}};"
				"Owner root=Owner();Owner read_root(){return root;}void main(){}";
			Check(Session.LoadSource(Source, "rollback-reset.vs"), "load rollback-reset fixture");
			Check(Session.Run(), "initialize rollback-reset fixture");
			FValue Root;
			Check(Session.CallFunction("read_root", {}, &Root), "observe rollback-reset graph");
			Observed = std::get<FObjectHandle>(Root);
			Require(!Observed.expired(), "rollback-reset fixture has a live cycle");
			Root = {};
			auto Replacement = VeroScriptCompile::CompileSource(Source +
				"int fail(){root.item=Item(2);throw 7;}int trigger=fail();", "rollback-reset.vs", Test::MakeCompileOptions());
			Require(Replacement.bSuccess, "compile rollback-reset replacement: " + Replacement.Error.Message);
			Check(Session.ReloadCompiledModule(Replacement.Module, "rollback-reset.vs"), "stage rollback-reset replacement");
			Session.SetExecutionLimits({80, 0});
			Require(!Session.Run().bSuccess && Session.RequiresReset() && Session.GetReloadStatus().State == EReloadState::ResetRequired,
				"failed bounded rollback never reports restoration completed");
			Session.SetExecutionLimits({});
			Require(!Session.CallFunction("main", {}).bSuccess, "unsettled rollback state cannot be reused");
		}
		Require(Observed.expired(), "destroying an unsettled Session releases its global cycles");
	}
	void ResetMigrationReleasesGraph(const EExecutionBackend Backend)
	{
		std::weak_ptr<FScriptObject> Observed;
		FHost Host;
		{
			VeroScriptSession Session(Host);
			Configure(Session, Backend);
			const std::string Source = "class Node{public:Node next;Node(){next=this;}};void main(){}";
			Check(Session.LoadSource(Source, "migration-reset-graph.vs"), "load reset graph");
			Check(Session.Run(), "initialize reset graph");
			FObjectHandle Object;
			Check(Session.ConstructObject("Node", {}, Object), "construct reset graph");
			Observed = Object;
			FValueRoot Root;
			std::string Error;
			Require(Session.RetainMigratableValue(Object, Root, Error), "retain reset graph: " + Error);
			Object.reset();
			auto Replacement = VeroScriptCompile::CompileSource(Source +
				"int fail(){try{for(;;){}}finally{for(;;){}}return 0;}int trigger=fail();", "migration-reset-graph.vs", Test::MakeCompileOptions());
			Require(Replacement.bSuccess, "compile reset graph candidate: " + Replacement.Error.Message);
			Check(Session.ReloadCompiledModule(Replacement.Module, "migration-reset-graph.vs"), "stage reset graph");
			Session.SetExecutionLimits({100, 0});
			Require(!Session.Run().bSuccess && Session.RequiresReset(), "reset graph exhausts bounded initialization cleanup");
			Root.Reset();
		}
		Require(Observed.expired(), "recreating an unusable Session releases its pending cyclic host graph");
	}
}
int main(int Argc, char** Argv)
{
	try
	{
		std::string Error;
		Factory = Argc > 1 ? LoadJitBackend(Argv[1], Error) : CreateAsmJitBackendFactory();
		Require(Factory != nullptr, "load JIT factory: " + Error);
		BindingRevisions();
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			PersistentCalls(Backend);
			InterceptorCallLocation(Backend);
			ObservableCalls(Backend);
			NestedInterception(Backend);
			InterceptedValueReturns(Backend);
			InterceptedArrayReturns(Backend);
			InterceptedReturnCleanup(Backend);
			ControlledInterception(Backend);
			MigratableRoots(Backend);
			MigratableGraphs(Backend);
			ReloadCleanupFailures(Backend);
			ReloadReset(Backend);
			ResetMigrationReleasesGraph(Backend);
		}
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
	return 0;
}
