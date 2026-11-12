// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptHost.h"
#include "VeroScriptNative.h"
#include "VeroScriptLocale.h"
#include "VeroScriptSession.h"
#include "VeroScriptBytecode.h"
#include "../VeroScriptModuleMetaInternal.h"
#include "VeroScriptValue.h"

#include "VeroScriptTestTemporaryDirectory.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptSessionApiTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	struct FCaptureHost final : VeroScript::IEngineHost
	{
		//~ Begin VeroScript::IEngineHost Interface
		void OnPrintLine(const std::string_view Line) override
		{
			PrintText.append(Line);
			PrintText.push_back('\n');
		}

		void OnDiagnosticLine(const std::string_view Line) override
		{
			DiagnosticText.append(Line);
			DiagnosticText.push_back('\n');
		}
		//~ End VeroScript::IEngineHost Interface

		std::string PrintText;
		std::string DiagnosticText;
	};

	struct FActor
	{
		std::int32_t Value = 0;
	};

	struct FVec2
	{
		std::int32_t X = 0;
		std::int32_t Y = 0;
	};

	std::int32_t ActorSubclassCalls = 0;
	std::int32_t Vec2SubclassCalls = 0;

	bool ActorSubclassFactory(
	    const VeroScript::FNativeCallContext& Context,
	    VeroScript::FValue& OutReturn,
	    std::string& OutError)
	{
		(void)Context;
		(void)OutError;
		++ActorSubclassCalls;
		auto* const Actor = new FActor{7};
		OutReturn = VeroScript::MakeHostObject(
		    /*TypeId=*/1,
		    "AActor",
		    Actor,
		    [](void* Payload)
		{
			delete static_cast<FActor*>(Payload);
		});
		return true;
	}

	bool Vec2SubclassFactory(
	    const VeroScript::FNativeCallContext& Context,
	    VeroScript::FValue& OutReturn,
	    std::string& OutError)
	{
		(void)Context;
		(void)OutError;
		++Vec2SubclassCalls;
		auto* const Vec = new FVec2{5, 0};
		OutReturn = VeroScript::MakeHostObject(
		    /*TypeId=*/2,
		    "Vec2",
		    Vec,
		    [](void* Payload)
		{
			delete static_cast<FVec2*>(Payload);
		});
		return true;
	}

	bool Vec2Clone(
	    const VeroScript::FNativeCallContext& Context,
	    VeroScript::FValue& OutReturn,
	    std::string& OutError)
	{
		(void)OutError;
		auto* const Src = static_cast<FVec2*>(Context.HostThis);
		if (!Src)
		{
			OutError = "null Vec2";
			return false;
		}
		auto* const Copy = new FVec2{*Src};
		OutReturn = VeroScript::MakeHostObject(
		    /*TypeId=*/2,
		    "Vec2",
		    Copy,
		    [](void* Payload)
		{
			delete static_cast<FVec2*>(Payload);
		});
		return true;
	}

	bool TriggerReentry(
	    const VeroScript::FNativeCallContext& Context,
	    VeroScript::FValue& OutReturn,
	    std::string& OutError)
	{
		(void)OutReturn;
		auto* const Session = static_cast<VeroScript::VeroScriptSession*>(Context.UserData);
		if (!Session)
		{
			OutError = "null session";
			return false;
		}
		VeroScript::FObjectHandle Object;
		const VeroScript::FRunResult Construct =
		    Session->ConstructObject("Entity", {}, Object);
		if (Construct.bSuccess)
		{
			OutError = "reentry ConstructObject unexpectedly succeeded";
			return false;
		}
		OutError = Construct.Error.Message.empty()
		    ? "reentry rejected"
		    : Construct.Error.Message;
		return false;
	}

	struct FCommitReentryContext
	{
		VeroScript::VeroScriptSession* Session = nullptr;
		std::unique_ptr<VeroScript::FBytecodeModule> Module;
		VeroScript::FRunResult Result;
	};

	struct FRetainCallableContext
	{
		VeroScript::VeroScriptSession* Session = nullptr;
		VeroScript::FRetainedCallableHandle Handle;
		bool bReleaseRejectedDuringRun = false;
	};

	bool RetainCallableForLater(
	    const VeroScript::FNativeCallContext& Context,
	    VeroScript::FValue&,
	    std::string& OutError)
	{
		auto* const State = static_cast<FRetainCallableContext*>(Context.UserData);
		const auto* const Callable = Context.ArgCount == 1
		    ? std::get_if<VeroScript::FFunctionHandle>(&Context.Args[0])
		    : nullptr;
		if (!State || !State->Session || !Callable || !*Callable)
		{
			OutError = "invalid retained callable context";
			return false;
		}
		State->Handle = State->Session->RetainCallable(*Callable);
		if (!State->Handle)
		{
			OutError = "session rejected callable retention";
			return false;
		}
		return true;
	}

	bool ReleaseCallableDuringRun(
	    const VeroScript::FNativeCallContext& Context,
	    VeroScript::FValue&,
	    std::string& OutError)
	{
		auto* const State = static_cast<FRetainCallableContext*>(Context.UserData);
		if (!State || !State->Session || !State->Handle)
		{
			OutError = "invalid retained callable release context";
			return false;
		}
		State->bReleaseRejectedDuringRun =
		    !State->Session->ReleaseCallable(State->Handle);
		if (!State->bReleaseRejectedDuringRun)
		{
			OutError = "active execution released a retained callable";
			return false;
		}
		return true;
	}

	bool TriggerCommitReentry(
	    const VeroScript::FNativeCallContext& Context,
	    VeroScript::FValue& OutReturn,
	    std::string& OutError)
	{
		(void)OutReturn;
		auto* const State = static_cast<FCommitReentryContext*>(Context.UserData);
		if (!State || !State->Session || !State->Module)
		{
			OutError = "invalid commit reentry context";
			return false;
		}
		State->Result = State->Session->CommitCompiledFile(
		    State->Module,
		    "<commit_reentry>");
		if (State->Result.bSuccess)
		{
			OutError = "reentry CommitCompiledFile unexpectedly succeeded";
			return false;
		}
		if (!State->Module)
		{
			OutError = "reentry CommitCompiledFile consumed the module on failure";
			return false;
		}
		return true;
	}
}

int main()
{
	{
		VeroScript::Tests::FScopedTemporaryDirectory Directory("VeroScript-constexpr-reload-");
		if (!Directory) return Fail("constexpr hot reload fixture directory");
		const auto Path = Directory.Get() / "main.vs";
		const auto Write = [&](int Value)
		{
			std::ofstream File(Path, std::ios::binary);
			File << "constexpr int generation=" << Value
			     << ";struct Config{static constexpr int generation=" << Value
			     << ";};void main(){print(generation);print(Config::generation);}";
			return bool(File);
		};
		FCaptureHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Write(1) || !Session.LoadFile(Path.generic_string()).bSuccess || !Session.Run().bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("constexpr hot reload initial generation");
		}
		if (Session.SetGlobal("generation", std::int32_t{9}).bSuccess)
		{
			return Fail("host write must reject constexpr globals");
		}
		if (!Write(2) || !Session.Reload().bSuccess || !Session.Run().bSuccess || Host.PrintText != "1\n1\n2\n2\n")
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			return Fail("hot reload must initialize the new constexpr values");
		}
	}

	{
		FCaptureHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Session.LoadSource(R"(
int writes=0;
struct S{int x=2;S()=default;S(const S& o)=delete;void operator=(const S& o){writes+=1;x=o.x+1;}};
class H{public:S value=S();};S saved=S();H holder=H();void main(){}
)",
		                "<session_special_members>")
		        .bSuccess ||
		    !Session.Run(VeroScript::ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("session special member fixture should load");
		}
		VeroScript::FValue Holder, Count;
		VeroScript::FObjectHandle Source;
		if (!Session.GetGlobal("holder", Holder).bSuccess || !Session.ConstructObject("S", {}, Source).bSuccess || !Session.SetField(std::get<VeroScript::FObjectHandle>(Holder), "value", Source).bSuccess || !Session.SetGlobal("saved", Source).bSuccess || !Session.GetGlobal("writes", Count).bSuccess || std::get<std::int32_t>(Count) != 2)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("host assignment hooks must use the session's live globals");
		}
	}
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		FCaptureHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetExecutionBackend(Backend);
		if (!Session.LoadSource(
		                "void deep(int n) { if (n > 0) { deep(n - 1); } } "
		                "struct Trace { public: int value = 1; ~Trace() { deep(100); print(78); } }; "
		                "void fail(ref Trace value) { value = Trace(); throw 9; } "
		                "void main() {}",
		                "<ref_cleanup>")
		        .bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ref cleanup fixture must compile");
		}
		VeroScript::FObjectHandle Object;
		if (!Session.ConstructObject("Trace", {}, Object).bSuccess || !Object)
		{
			return Fail("ref cleanup object must construct");
		}
		Host.PrintText.clear();
		std::vector<VeroScript::FValue> Arguments{Object};
		const VeroScript::FRunResult Failed = Session.CallFunction("fail", Arguments);
		if (Failed.bSuccess || Host.PrintText != "78\n78\n" || std::get<VeroScript::FObjectHandle>(Arguments[0]) != Object)
		{
			return Fail("failed ref calls must safely destroy cloned arguments without write-back");
		}
	}
	using namespace VeroScript;
	const Tests::FScopedTemporaryDirectory TemporaryDirectory(
	    "VeroScript-session-api-");
	if (!TemporaryDirectory)
	{
		return Fail("create unique session API test directory");
	}
	const std::filesystem::path& TemporaryRoot = TemporaryDirectory.Get();
	#if !defined(_WIN32)
	{
		const auto LiteralDirectory = TemporaryRoot / "folder\\literal";
		std::filesystem::create_directories(LiteralDirectory);
		const auto MainPath = LiteralDirectory / "main.vs";
		{
			std::ofstream Dependency(LiteralDirectory / "dep.vs");
			Dependency << "export int answer() { return 42; }\n";
			std::ofstream Main(MainPath);
			Main << "import \"dep.vs\"; void main() { print(answer()); }\n";
		}
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadFile(MainPath.generic_string()).bSuccess || !Session.Run().bSuccess || Host.PrintText != "42\n")
		{
			std::cerr << Host.DiagnosticText;
			return Fail("POSIX literal backslash directory supports source and sibling imports");
		}
	}
	#endif

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		const FRunResult Load = Session.LoadSource(
		    "class Entity {\n"
		    "public:\n"
		    "\tint Hits = 0;\n"
		    "\tfloat Acc = 0.0;\n"
		    "\tvoid BeginPlay() { Hits = 1; }\n"
		    "\tvoid Tick(float Dt) { Acc = Acc + Dt; Hits = Hits + 1; }\n"
		    "\tint GetHits() { return Hits; }\n"
		    "\tfloat GetAcc() { return Acc; }\n"
		    "}\n"
		    "class Base {\n"
		    "public:\n"
		    "\tvirtual int Id() { return 1; }\n"
		    "}\n"
		    "class Derived : Base {\n"
		    "public:\n"
		    "\tint Id() override { return 2; }\n"
		    "}\n"
		    "int main() { print(\"main\"); return 17; }\n",
		    "<session_callmethod>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("LoadSource should succeed");
		}

		FObjectHandle Entity;
		{
			const FRunResult Construct = Session.ConstructObject("Entity", {}, Entity);
			if (!Construct.bSuccess || !Entity)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("ConstructObject Entity should succeed");
			}
		}

		if (!Session.HasInstanceMethod(Entity, "BeginPlay", 0) || !Session.HasInstanceMethod(Entity, "Tick", 1) || Session.HasInstanceMethod(Entity, "Missing", 0))
		{
			return Fail("HasInstanceMethod should resolve BeginPlay/Tick only");
		}

		{
			FValue Stale = std::string("stale");
			const FRunResult Begin = Session.CallMethod(Entity, "BeginPlay", {}, &Stale);
			if (!Begin.bSuccess)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("CallMethod BeginPlay should succeed");
			}
			if (std::holds_alternative<std::string>(Stale))
			{
				return Fail("void CallMethod should clear OutReturn before invoke");
			}
		}

		{
			std::vector<FValue> Args{0.5};
			const FRunResult Tick = Session.CallMethod(Entity, "Tick", Args);
			if (!Tick.bSuccess)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("CallMethod Tick should succeed");
			}
		}

		{
			FValue Hits;
			const FRunResult GetHits = Session.CallMethod(Entity, "GetHits", {}, &Hits);
			if (!GetHits.bSuccess || !std::holds_alternative<std::int32_t>(Hits) || std::get<std::int32_t>(Hits) != 2)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("GetHits should return 2 after BeginPlay+Tick");
			}
		}

		{
			FValue Acc;
			const FRunResult GetAcc = Session.CallMethod(Entity, "GetAcc", {}, &Acc);
			if (!GetAcc.bSuccess || !std::holds_alternative<float>(Acc) || std::get<float>(Acc) != 0.5f)
			{
				std::cerr << "GetAcc success=" << GetAcc.bSuccess
				          << " value-index=" << Acc.index();
				if (const auto* Value = std::get_if<float>(&Acc))
				{
					std::cerr << " float=" << *Value;
				}
				if (const auto* Value = std::get_if<double>(&Acc))
				{
					std::cerr << " double=" << *Value;
				}
				std::cerr << '\n';
				std::cerr << Host.DiagnosticText;
				return Fail("GetAcc should return 0.5");
			}
		}

		{
			const FRunResult Missing = Session.CallMethod(Entity, "NoSuchMethod");
			if (Missing.bSuccess || Missing.ExitCode != 4 || Missing.Error.MessageId != "diag.callmethod_unknown_method")
			{
				return Fail("missing method should fail with ExitCode 4 + MessageId");
			}
		}
		{
			Host.DiagnosticText.clear();
			const FRunResult QuietMissing = Session.CallMethod(
			    Entity,
			    "NoSuchMethod",
			    {},
			    nullptr,
			    EMissingMethodPolicy::Quiet);
			if (QuietMissing.bSuccess || QuietMissing.ExitCode != 4 || !Host.DiagnosticText.empty())
			{
				return Fail("Quiet missing method should not write diagnostics");
			}
		}
		{
			// Constructors are not instance methods for CallMethod.
			if (Session.HasInstanceMethod(Entity, "Entity", 0))
			{
				return Fail("HasInstanceMethod must not treat constructors as instance methods");
			}
			const FRunResult CtorCall = Session.CallMethod(Entity, "Entity");
			if (CtorCall.bSuccess || CtorCall.Error.MessageId != "diag.callmethod_unknown_method")
			{
				return Fail("CallMethod must reject constructor names on live instances");
			}
		}

		{
			FObjectHandle Derived;
			const FRunResult Construct = Session.ConstructObject("Derived", {}, Derived);
			if (!Construct.bSuccess || !Derived)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("ConstructObject Derived should succeed");
			}
			FValue Id;
			const FRunResult Call = Session.CallMethod(Derived, "Id", {}, &Id);
			if (!Call.bSuccess || !std::holds_alternative<std::int32_t>(Id) || std::get<std::int32_t>(Id) != 2)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("virtual CallMethod should dispatch to Derived::Id");
			}
		}

		{
			Host.PrintText.clear();
			const FRunResult Run = Session.Run();
			if (!Run.bSuccess || Run.bExitRequested || Run.ExitCode != 17 || Host.PrintText != "main\n")
			{
				std::cerr << Host.DiagnosticText << Host.PrintText;
				return Fail("Session Run(int main) should propagate its exit code after CallMethod");
			}
		}
	}

	for (const EExecutionBackend Backend :
	    {EExecutionBackend::Vm, EExecutionBackend::Mixed})
	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetExecutionBackend(Backend);
		Session.SetHotReloadEnabled(true);
		if (!Session.LoadSource(
		                "int Value = 5;\n"
		                "int& value_ref() { return Value; }\n"
		                "void set_value(int input) { value_ref() = input; }\n"
		                "void main() {}\n",
		                "<reference_return_session_api>")
		        .bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("reference-return session source should compile");
		}
		FValue ReturnValue;
		if (!Session.CallFunction("value_ref", {}, &ReturnValue).bSuccess || !std::holds_alternative<std::int32_t>(ReturnValue) || std::get<std::int32_t>(ReturnValue) != 5)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("CallFunction should expose a safe snapshot of a reference return");
		}
		ReturnValue = std::int32_t{99};
		FValue GlobalValue;
		if (!Session.GetGlobal("Value", GlobalValue).bSuccess || !std::holds_alternative<std::int32_t>(GlobalValue) || std::get<std::int32_t>(GlobalValue) != 5)
		{
			return Fail("mutating a host return snapshot must not mutate script storage");
		}
		if (!Session.CallFunction(
		                "set_value",
		                {FValue{std::int32_t{12}}})
		        .bSuccess ||
		    !Session.GetGlobal("Value", GlobalValue).bSuccess || !std::holds_alternative<std::int32_t>(GlobalValue) || std::get<std::int32_t>(GlobalValue) != 12)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("reference-return mutation should work through every backend");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Session.LoadSource(
		                "class RefCellHolder { public: int Value = 3; }\n"
		                "int Counter = 4;\n"
		                "RefCellHolder Holder = RefCellHolder();\n"
		                "function()->int CounterCallback = function()->int => 0;\n"
		                "function()->int FieldCallback = function()->int => 0;\n"
		                "int InvokeCounter() { return CounterCallback(); }\n"
		                "int InvokeField() { return FieldCallback(); }\n"
		                "void main() {\n"
		                "    int& counter_alias = Counter;\n"
		                "    int& field_alias = Holder.Value;\n"
		                "    CounterCallback = [&counter_alias]() -> int { return ++counter_alias; };\n"
		                "    FieldCallback = [&field_alias]() -> int { return ++field_alias; };\n"
		                "}\n",
		                "<persistent_reference_cells>")
		        .bSuccess ||
		    !Session.Run().bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("persistent reference-cell source should run");
		}
		FValue Counter;
		FValue HolderValue;
		if (!Session.GetGlobal("Counter", Counter).bSuccess || !std::holds_alternative<std::int32_t>(Counter) || std::get<std::int32_t>(Counter) != 4 || !Session.GetGlobal("Holder", HolderValue).bSuccess)
		{
			return Fail("GetGlobal must hide closure reference-cell storage");
		}
		const auto* Holder = std::get_if<FObjectHandle>(&HolderValue);
		if (!Holder || !*Holder || !Session.SetGlobal("Counter", std::int32_t{9}).bSuccess || !Session.SetField(*Holder, "Value", std::int32_t{12}).bSuccess)
		{
			return Fail("host writes must preserve persistent reference aliases");
		}
		FValue CounterResult;
		FValue FieldResult;
		if (!Session.CallFunction("InvokeCounter", {}, &CounterResult).bSuccess || !Session.CallFunction("InvokeField", {}, &FieldResult).bSuccess || !std::holds_alternative<std::int32_t>(CounterResult) || std::get<std::int32_t>(CounterResult) != 10 || !std::holds_alternative<std::int32_t>(FieldResult) || std::get<std::int32_t>(FieldResult) != 13)
		{
			return Fail("retained closures must observe host writes through reference cells");
		}

		bool bSawVisibleField = false;
		FExternalSerializerDesc Serializer;
		Serializer.Serialize = [&bSawVisibleField](
		                           const FExternalObjectView& View,
		                           std::vector<std::uint8_t>&,
		                           std::string& OutError)
		{
			if (View.Fields.size() != 1 || !View.Fields[0].Value || !std::holds_alternative<std::int32_t>(*View.Fields[0].Value) || std::get<std::int32_t>(*View.Fields[0].Value) != 13)
			{
				OutError = "serializer observed private reference-cell storage";
				return false;
			}
			bSawVisibleField = true;
			return true;
		};
		Serializer.Deserialize = [](
		                             const std::uint8_t*, std::size_t, std::uint32_t,
		                             std::vector<FExternalFieldAssignment>&, std::string&)
		{
			return true;
		};
		std::string Error;
		std::vector<std::uint8_t> Bytes;
		std::uint32_t Version = 0;
		if (!Session.RegisterExternalSerializer("RefCellHolder", std::move(Serializer), &Error) || !Session.SerializeObject(*Holder, Bytes, Version, Error) || !bSawVisibleField)
		{
			std::cerr << Error;
			return Fail("external serialization must see the referenced field value");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FValue Result;
		if (!Session.LoadSource(
		                "interface IRootValue {\n"
		                "    virtual int get() const = 0;\n"
		                "    int doubled() const { return get() * 2; }\n"
		                "}\n"
		                "interface IValue : IRootValue {\n"
		                "    private: int secret() const { return 6; }\n"
		                "    public:\n"
		                "    static int plus_one(int value) { return value + 1; }\n"
		                "}\n"
		                "class Impl : public IValue { public: int get() const { return 17; } }\n"
		                "Impl item = Impl();\n"
		                "int inspect() {\n"
		                "    type<IValue> info = type<IValue>();\n"
		                "    for (int i = 0; i < info.method_count(); ++i) {\n"
		                "        method_info<IValue> method = method_info<IValue>(info.qualified_name, i);\n"
		                "        if (method.name() == \"get\") return method.invoke<int>(item);\n"
		                "    }\n"
		                "    return -1;\n"
		                "}\n"
		                "int inspect_default() {\n"
		                "    type<IValue> info = type<IValue>();\n"
		                "    for (int i = 0; i < info.method_count(); ++i) {\n"
		                "        method_info<IValue> method = method_info<IValue>(info.qualified_name, i);\n"
		                "        if (method.name() == \"doubled\") return method.invoke<int>(item);\n"
		                "    }\n"
		                "    return -1;\n"
		                "}\n"
		                "int inspect_static() {\n"
		                "    type<IValue> info = type<IValue>();\n"
		                "    for (int i = 0; i < info.method_count(); ++i) {\n"
		                "        method_info<IValue> method = method_info<IValue>(info.qualified_name, i);\n"
		                "        if (method.name() == \"plus_one\") return method.invoke_static<int>(4);\n"
		                "    }\n"
		                "    return -1;\n"
		                "}\n"
		                "int inspect_private() {\n"
		                "    type<IValue> info = type<IValue>();\n"
		                "    for (int i = 0; i < info.method_count(); ++i) {\n"
		                "        method_info<IValue> method = method_info<IValue>(info.qualified_name, i);\n"
		                "        if (method.name() == \"secret\") return method.invoke<int>(item);\n"
		                "    }\n"
		                "    return -1;\n"
		                "}\n"
		                "void main() {}\n",
		                "<interface_reflection>")
		        .bSuccess ||
		    !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("inspect", {}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 17)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("interface reflection should bind abstract descriptors to runtime implementations");
		}
		Result = {};
		if (!Session.CallFunction("inspect_default", {}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 34)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("interface reflection should invoke inherited default method bodies");
		}
		Result = {};
		if (!Session.CallFunction("inspect_static", {}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 5)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("interface reflection should invoke public static methods");
		}
		const FRunResult PrivateCall = Session.CallFunction("inspect_private", {}, &Result);
		if (PrivateCall.bSuccess || PrivateCall.Error.Message.find("not publicly accessible") == std::string::npos)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("interface reflection must reject private methods");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FValue Result;
		if (!Session.LoadSource(
		                "interface ISource<T> { virtual T get() const = 0; }\n"
		                "class IntSource : public ISource<int> { public: int get() const { return 23; } }\n"
		                "IntSource item = IntSource();\n"
		                "int inspect_generic() {\n"
		                "    type<ISource<int>> info = type<ISource<int>>();\n"
		                "    for (int i = 0; i < info.method_count(); ++i) {\n"
		                "        method_info<ISource<int>> method = method_info<ISource<int>>(info.qualified_name, i);\n"
		                "        if (method.name() == \"get\") return method.invoke<int>(item);\n"
		                "    }\n"
		                "    return -1;\n"
		                "}\n"
		                "void main() {}\n",
		                "<generic_interface_reflection>")
		        .bSuccess ||
		    !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("inspect_generic", {}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 23)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("generic interface reflection should invoke abstract descriptors");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		std::string AliasError;
		if (!Session.GetBindingRegistry().RegisterTypeAlias(
		        "AliasInt", {"T"}, "int", AliasError) ||
		    !Session.LoadSource(
		                "class AliasHolder { public: AliasInt<string> Value = 3; }\n",
		                "<external_alias_types>")
		        .bSuccess)
		{
			std::cerr << AliasError << Host.DiagnosticText;
			return Fail("transparent alias Session source should compile");
		}
		FObjectHandle Object;
		FValue Value;
		if (!Session.ConstructObject("AliasHolder", {}, Object).bSuccess || !Session.GetField(Object, "Value", Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 3 || !Session.SetField(Object, "Value", std::int32_t{7}).bSuccess || Session.SetField(Object, "Value", std::string("wrong")).bSuccess)
		{
			return Fail("Session field access must validate transparent aliases by runtime type");
		}

		bool bObservedTypeSplit = false;
		FExternalSerializerDesc Serializer;
		Serializer.Serialize = [&bObservedTypeSplit](
		                           const FExternalObjectView& View,
		                           std::vector<std::uint8_t>& OutBytes,
		                           std::string& OutError)
		{
			if (View.Fields.size() != 1 || View.Fields[0].TypeName != "AliasInt<string>" || View.Fields[0].RuntimeTypeName != "int")
			{
				OutError = "external field type identities do not match";
				return false;
			}
			bObservedTypeSplit = true;
			OutBytes.clear();
			return true;
		};
		Serializer.Deserialize = [](
		                             const std::uint8_t*,
		                             std::size_t,
		                             std::uint32_t,
		                             std::vector<FExternalFieldAssignment>&,
		                             std::string&)
		{
			return true;
		};
		if (!Session.RegisterExternalSerializer(
		        "AliasHolder", std::move(Serializer), &AliasError))
		{
			return Fail("transparent alias external serializer should register");
		}
		std::vector<std::uint8_t> Bytes;
		std::uint32_t Version = 0;
		if (!Session.SerializeObject(Object, Bytes, Version, AliasError) || !bObservedTypeSplit)
		{
			std::cerr << AliasError;
			return Fail("external serialization must expose declared and runtime alias types");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FRetainCallableContext Retained{&Session, {}};
		if (!Session.RegisterNative(
		        "retain_for_later",
		        FNativeTypeDesc::Scalar(ENativeType::Void),
		        {{FNativeTypeDesc::Function("fn(ref int)->int"), 0}},
		        RetainCallableForLater,
		        &Retained))
		{
			return Fail("retained callable native registration should succeed");
		}
		if (!Session.RegisterNative(
		        "release_during_run",
		        FNativeTypeDesc::Scalar(ENativeType::Void),
		        {},
		        ReleaseCallableDuringRun,
		        &Retained))
		{
			return Fail("retained callable release native registration should succeed");
		}
		const FRunResult Load = Session.LoadSource(
		    "int bump(ref int value) { value = value + 5; return value * 2; }\n"
		    "void main() { function(ref int)->int callback = bump; "
		    "retain_for_later(callback); release_during_run(); }\n",
		    "<retained_callable>");
		if (!Load.bSuccess || !Session.Run().bSuccess || !Retained.Handle || !Retained.bReleaseRejectedDuringRun || !Session.IsCallableValid(Retained.Handle))
		{
			std::cerr << Host.DiagnosticText;
			return Fail("callable should remain valid after its producing Run returns");
		}
		FRetainedCallableHandle Forged = Retained.Handle;
		++Forged.ProgramGeneration;
		if (Session.ReleaseCallable(Forged) || !Session.IsCallableValid(Retained.Handle))
		{
			return Fail("callable release must validate the complete generation identity");
		}
		std::vector<FValue> Args{std::int32_t{7}};
		FValue ReturnValue = std::int32_t{0};
		const FRunResult Invoke = Session.InvokeCallable(
		    Retained.Handle, Args, &ReturnValue);
		if (!Invoke.bSuccess || !std::holds_alternative<std::int32_t>(Args[0]) || std::get<std::int32_t>(Args[0]) != 12 || !std::holds_alternative<std::int32_t>(ReturnValue) || std::get<std::int32_t>(ReturnValue) != 24)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("retained callable should preserve ref write-back and return value");
		}
		if (!Session.LoadSource(
		                "void main() {}\n",
		                "<retained_callable_replacement>")
		        .bSuccess ||
		    Session.IsCallableValid(Retained.Handle) || Session.InvokeCallable(Retained.Handle).bSuccess)
		{
			return Fail("non-hot-reload module replacement must invalidate old callables");
		}
		if (Session.ReleaseCallable(Retained.Handle))
		{
			return Fail("module replacement should already release stale callable storage");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadSource(
		                "class CallableOwner { public: int Value = 9; "
		                "fn()->int getter() { return [=]() -> int => Value; } }\n"
		                "void main() {}\n",
		                "<weak_retained_callable>")
		        .bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("weak retained callable source should compile");
		}
		FObjectHandle Owner;
		if (!Session.ConstructObject("CallableOwner", {}, Owner).bSuccess || !Owner)
		{
			return Fail("weak retained callable owner should construct");
		}
		FValue CallableValue;
		if (!Session.CallMethod(Owner, "getter", {}, &CallableValue).bSuccess)
		{
			return Fail("weak retained callable getter should run");
		}
		const auto* const Callable = std::get_if<FFunctionHandle>(&CallableValue);
		const FRetainedCallableHandle WeakHandle = Callable && *Callable
		    ? Session.RetainCallable(
		          *Callable,
		          true,
		          ERetainedThisCapture::Weak)
		    : FRetainedCallableHandle{};
		FValue ReturnValue;
		if (!WeakHandle || !Session.InvokeCallable(WeakHandle, {}, &ReturnValue).bSuccess || !std::holds_alternative<std::int32_t>(ReturnValue) || std::get<std::int32_t>(ReturnValue) != 9)
		{
			return Fail("weak retained callable should invoke while this is alive");
		}
		CallableValue = std::int32_t{0};
		Owner.reset();
		if (Session.IsCallableValid(WeakHandle) || Session.InvokeCallable(WeakHandle).bSuccess)
		{
			return Fail("weak retained callable must fail safely after this expires");
		}
		if (!Session.ReleaseCallable(WeakHandle))
		{
			return Fail("expired weak callable should remain explicitly releasable");
		}
	}

	for (const EExecutionBackend Backend :
	    {EExecutionBackend::Vm, EExecutionBackend::Mixed})
	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetExecutionBackend(Backend);
		if (!Session.LoadSource(
		                "class DefaultsBase {\n"
		                "public: int Value = 0; DefaultsBase() { Value = 1; }\n"
		                "default { Value = 3; }\n"
		                "}\n"
		                "class DefaultsChild : DefaultsBase {\n"
		                "public: DefaultsChild() {}\n"
		                "default { Value = Value + 4; }\n"
		                "}\n",
		                "<class_defaults>")
		        .bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("class-default source should compile");
		}
		FObjectHandle Object;
		if (!Session.ConstructObject("DefaultsChild", {}, Object).bSuccess || !Object)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("class-default object construction should succeed");
		}
		FValue Before;
		if (!Session.GetField(Object, "Value", Before).bSuccess || !std::holds_alternative<std::int32_t>(Before) || std::get<std::int32_t>(Before) != 1)
		{
			return Fail("class defaults must remain separate from construction");
		}
		if (!Session.ApplyClassDefaults(Object).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ApplyClassDefaults should run base then derived blocks");
		}
		FValue After;
		if (!Session.GetField(Object, "Value", After).bSuccess || !std::holds_alternative<std::int32_t>(After) || std::get<std::int32_t>(After) != 7)
		{
			return Fail("class defaults should apply base-to-derived");
		}
	}

	for (const EExecutionBackend Backend :
	    {EExecutionBackend::Vm, EExecutionBackend::Mixed})
	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetExecutionBackend(Backend);
		std::uint32_t MutationCount = 0;
		std::uint32_t ContainerAdds = 0;
		std::uint32_t ContainerSets = 0;
		std::uint32_t ContainerRemoves = 0;
		std::uint32_t ChannelMutations = 0;
		std::uint32_t ItemsFieldWrites = 0;
		std::uint32_t OtherFieldWrites = 0;
		std::string LastField;
		Session.SetMutationCallback(
		    [&](const FMutationEvent& Event)
		{
			if (Event.Object && Event.FieldName == "Value" && Event.Kind == EMutationKind::FieldWrite)
			{
				++MutationCount;
				LastField = Event.FieldName;
			}
			if (Event.Object && Event.FieldName == "Other" && Event.Kind == EMutationKind::FieldWrite)
			{
				++OtherFieldWrites;
			}
			if (Event.FieldName == "Items" || Event.FieldName == "Values")
			{
				ContainerAdds += Event.Kind == EMutationKind::ContainerAdd ? 1u : 0u;
				ContainerSets += Event.Kind == EMutationKind::ContainerSet ? 1u : 0u;
				ContainerRemoves += Event.Kind == EMutationKind::ContainerRemove ? 1u : 0u;
				ItemsFieldWrites += Event.FieldName == "Items" && Event.Kind == EMutationKind::FieldWrite ? 1u : 0u;
			}
			if (Event.FieldName == "Messages")
			{
				++ChannelMutations;
			}
		});
		if (!Session.LoadSource(
		                "import \"concurrent.vs\";\n"
		                "class MutationBox {\n"
		                "public: int Value = 0; int Other = 0; array<int> Items = array<int>(); "
		                "map<int,int> Values = map<int,int>();\n"
		                "channel<int> Messages = channel<int>();\n"
		                "void set(int next) { Value = next; }\n"
		                "void mutate() { Items.push(1); Items[0] = 2; "
		                "Values[1] = 3; Values.remove(1); }\n"
		                "void set_item(int next) { Items[0] = next; }\n"
		                "void mutate_dense_fields() { for (int i = 0; i < 4; i++) { "
		                "Value = Value + 1; Other += 2; } }\n"
		                "void mutate_refs() { int& value_ref = Value; int& item_ref = Items[0]; "
		                "value_ref++; item_ref++; }\n"
		                "void mutate_captured_refs() { "
		                "int& value_ref = Value; int& item_ref = Items[0]; "
		                "function()->void apply = [&value_ref, &item_ref]() -> void { "
		                "value_ref++; item_ref++; }; apply(); }\n"
		                "void replace_items_by_ref() { array<int> replacement = {9}; "
		                "array<int>& items_ref = Items; items_ref = replacement; "
		                "items_ref.push(10); }\n"
		                "void mutate_channel() { "
		                "Messages.send(7); int received = Messages.recv(); "
		                "Messages.close(); }\n"
		                "}\n"
		                "void bump_external(MutationBox box) { box.Value = box.Value + 2; }\n"
		                "void main() { MutationBox box(); box.mutate_channel(); }\n",
		                "<mutation_barrier>")
		        .bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("mutation-barrier source should compile");
		}
		if (!Session.Run().bSuccess || ChannelMutations != 3)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("channel operations must emit field-origin barriers");
		}
		FObjectHandle Object;
		if (!Session.ConstructObject("MutationBox", {}, Object).bSuccess || !Object)
		{
			return Fail("mutation-barrier object construction should succeed");
		}
		if (!Session.CallMethod(Object, "mutate_channel").bSuccess || ChannelMutations != 6)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("escaped object channels must remain open across Session calls");
		}
		MutationCount = 0;
		if (!Session.CallMethod(
		                Object, "set", {static_cast<std::int32_t>(4)})
		        .bSuccess ||
		    MutationCount != 1 || LastField != "Value" || Object->MutationVersion == 0)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("VM/JIT field writes must emit one mutation event");
		}
		if (!Session.SetField(Object, "Value", static_cast<std::int32_t>(5)).bSuccess || MutationCount != 2)
		{
			return Fail("Session field writes must use the same mutation barrier");
		}
		FValue FastFieldAfter;
		if (!Session.CallFunction("bump_external", {Object}).bSuccess || MutationCount != 3 || !Session.GetField(Object, "Value", FastFieldAfter).bSuccess || !std::holds_alternative<std::int32_t>(FastFieldAfter) || std::get<std::int32_t>(FastFieldAfter) != 7)
		{
			std::cerr << Host.DiagnosticText;
			return Fail(
			    "borrowed local-field JIT updates must preserve mutation barriers");
		}
		const std::uint32_t ValueWritesBeforeDense = MutationCount;
		if (!Session.CallMethod(Object, "mutate_dense_fields").bSuccess || MutationCount != ValueWritesBeforeDense + 4 || OtherFieldWrites != 4)
		{
			std::cerr << Host.DiagnosticText;
			return Fail(
			    "dense field loops must preserve every observable mutation barrier");
		}
		if (!Session.CallMethod(Object, "mutate").bSuccess || ContainerAdds == 0 || ContainerSets == 0 || ContainerRemoves == 0 || ChannelMutations != 6)
		{
			std::cerr << Host.DiagnosticText;
			std::cerr << "backend=" << static_cast<int>(Backend)
			          << " add=" << ContainerAdds
			          << " set=" << ContainerSets
			          << " remove=" << ContainerRemoves
			          << " channel=" << ChannelMutations << '\n';
			return Fail("container mutations must emit VM/JIT field-origin barriers");
		}
		const std::uint32_t ContainerSetsBeforeFastSet = ContainerSets;
		if (!Session.CallMethod(
		                Object, "set_item", {static_cast<std::int32_t>(6)})
		        .bSuccess ||
		    ContainerSets != ContainerSetsBeforeFastSet + 1)
		{
			std::cerr << Host.DiagnosticText;
			return Fail(
			    "scalar array SetIndex fast path must preserve the field mutation barrier");
		}
		const std::uint32_t MutationsBeforeRefs = MutationCount;
		const std::uint32_t ContainerSetsBeforeRefs = ContainerSets;
		if (!Session.CallMethod(Object, "mutate_refs").bSuccess || !Session.CallMethod(Object, "mutate_captured_refs").bSuccess || MutationCount != MutationsBeforeRefs + 2 || ContainerSets != ContainerSetsBeforeRefs + 2)
		{
			std::cerr << Host.DiagnosticText;
			return Fail(
			    "direct and captured reference writes must preserve field mutation barriers");
		}
		const std::uint32_t ContainerAddsBeforeReplace = ContainerAdds;
		const std::uint32_t ItemsFieldWritesBeforeReplace = ItemsFieldWrites;
		if (!Session.CallMethod(Object, "replace_items_by_ref").bSuccess || ItemsFieldWrites != ItemsFieldWritesBeforeReplace + 1 || ContainerAdds != ContainerAddsBeforeReplace + 1)
		{
			std::cerr << Host.DiagnosticText;
			std::cerr << "backend=" << static_cast<int>(Backend)
			          << " item_writes=" << ItemsFieldWrites
			          << " adds_before=" << ContainerAddsBeforeReplace
			          << " adds_after=" << ContainerAdds << '\n';
			return Fail(
			    "container assignment through a field reference must restamp its mutation origin");
		}

		// Mutation callbacks are part of the write operation. A callback failure must
		// fail the call on every backend, including the scalar array JIT fast path.
		Session.SetMutationCallback([](const FMutationEvent&)
		{
			throw std::runtime_error("intentional mutation callback failure");
		});
		const FRunResult MutationFailure = Session.CallMethod(
		    Object, "set_item", {static_cast<std::int32_t>(8)});
		if (MutationFailure.bSuccess || MutationFailure.Error.Message.find("mutation callback failed") == std::string::npos)
		{
			return Fail("VM/JIT mutation callback failures must propagate");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadSource(
		                "class Serializable { public: int Value = 9; }\n"
		                "class Holder { public: Serializable Ref = nullptr; }\n"
		                "interface IRootMarker {}\n"
		                "interface IMarker : IRootMarker {}\n"
		                "class BaseValue {}\n"
		                "class MiddleValue : public BaseValue {}\n"
		                "class DerivedValue : public MiddleValue, IMarker {}\n"
		                "class OtherValue {}\n"
		                "class PolymorphicHolder {\n"
		                "public:\n"
		                "    BaseValue BaseRef = nullptr;\n"
		                "    IRootMarker InterfaceRef = nullptr;\n"
		                "}\n",
		                "<external_serialization>")
		        .bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("external serialization source should compile");
		}
		FExternalSerializerDesc Serializer;
		Serializer.Version = 7;
		bool bSerializeReentryRejected = false;
		bool bDeserializeReentryRejected = false;
		Serializer.Serialize = [&Session, &bSerializeReentryRejected](
		                           const FExternalObjectView& Object,
		                           std::vector<std::uint8_t>& OutBytes,
		                           std::string& OutError)
		{
			bSerializeReentryRejected = !Session.LoadSource(
			                                        "void main() {}\n", "<serialize_reentry>")
			                                 .bSuccess;
			if (!bSerializeReentryRejected)
			{
				OutError = "external serializer reentered the session";
				return false;
			}
			for (const FExternalFieldView& Field : Object.Fields)
			{
				if (Field.Name == "Value" && Field.Value)
				{
					const auto* Value = std::get_if<std::int32_t>(Field.Value);
					if (!Value)
					{
						OutError = "Value is not int";
						return false;
					}
					OutBytes = {
					    static_cast<std::uint8_t>(*Value & 0xff),
					    static_cast<std::uint8_t>((*Value >> 8) & 0xff),
					    static_cast<std::uint8_t>((*Value >> 16) & 0xff),
					    static_cast<std::uint8_t>((*Value >> 24) & 0xff)};
					return true;
				}
			}
			OutError = "missing Value";
			return false;
		};
		Serializer.Deserialize = [&Session, &bDeserializeReentryRejected](
		                             const std::uint8_t* Bytes,
		                             const std::size_t ByteCount,
		                             const std::uint32_t EncodedVersion,
		                             std::vector<FExternalFieldAssignment>& OutFields,
		                             std::string& OutError)
		{
			bDeserializeReentryRejected = !Session.LoadSource(
			                                          "void main() {}\n", "<deserialize_reentry>")
			                                   .bSuccess;
			if (!bDeserializeReentryRejected)
			{
				OutError = "external deserializer reentered the session";
				return false;
			}
			if (EncodedVersion != 7 || ByteCount != 4 || !Bytes)
			{
				OutError = "unsupported serialized value";
				return false;
			}
			const std::uint32_t Raw = static_cast<std::uint32_t>(Bytes[0]) | (static_cast<std::uint32_t>(Bytes[1]) << 8) | (static_cast<std::uint32_t>(Bytes[2]) << 16) | (static_cast<std::uint32_t>(Bytes[3]) << 24);
			OutFields.push_back({"Value", static_cast<std::int32_t>(Raw)});
			return true;
		};
		std::string ExternalError;
		if (!Session.RegisterExternalSerializer(
		        "Serializable", std::move(Serializer), &ExternalError))
		{
			std::cerr << ExternalError;
			return Fail("external serializer registration should succeed");
		}
		FObjectHandle ValueObject;
		if (!Session.ConstructObject("Serializable", {}, ValueObject).bSuccess)
		{
			return Fail("serializable object construction should succeed");
		}
		std::vector<std::uint8_t> Bytes;
		std::uint32_t Version = 0;
		if (!Session.SerializeObject(
		        ValueObject, Bytes, Version, ExternalError) ||
		    Version != 7 || Bytes.size() != 4 || !bSerializeReentryRejected)
		{
			std::cerr << ExternalError;
			return Fail("external serializer should receive a stable field view");
		}
		ValueObject->Fields[0] = std::string("invalid");
		if (Session.SerializeObject(ValueObject, Bytes, Version, ExternalError) || !Bytes.empty() || Version != 0)
		{
			return Fail("external serialization must reject malformed runtime layouts");
		}
		ValueObject->Fields[0] = std::int32_t{9};
		const std::vector<std::uint8_t> Replacement{42, 0, 0, 0};
		if (!Session.DeserializeObject(
		        ValueObject,
		        Replacement.data(),
		        Replacement.size(),
		        7,
		        ExternalError) ||
		    !bDeserializeReentryRejected)
		{
			std::cerr << ExternalError;
			return Fail("external deserializer should commit validated fields");
		}
		FValue Value;
		if (!Session.GetField(ValueObject, "Value", Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 42)
		{
			return Fail("external deserialization should update the object");
		}
		FObjectHandle Holder;
		if (!Session.ConstructObject("Holder", {}, Holder).bSuccess || !Session.SetField(Holder, "Ref", FValue(ValueObject)).bSuccess)
		{
			return Fail("reference enumeration setup should succeed");
		}
		std::uint32_t ScriptReferences = 0;
		bool bVisitorReentryRejected = false;
		if (!Session.EnumerateObjectReferences(
		        Holder,
		        [&](const FExternalObjectReference& Reference)
		{
			bVisitorReentryRejected = !Session.LoadSource(
			                                      "void main() {}\n", "<reference_visitor_reentry>")
			                               .bSuccess;
			if (Reference.Kind == EExternalReferenceKind::ScriptObject && Reference.ScriptObject == ValueObject)
			{
				++ScriptReferences;
			}
		},
		        ExternalError) ||
		    ScriptReferences != 1 || !bVisitorReentryRejected)
		{
			std::cerr << ExternalError;
			return Fail("external reference enumeration should visit script references once");
		}

		FObjectHandle DerivedValue;
		FObjectHandle OtherValue;
		FObjectHandle PolymorphicHolder;
		if (!Session.ConstructObject("DerivedValue", {}, DerivedValue).bSuccess || !Session.ConstructObject("OtherValue", {}, OtherValue).bSuccess || !Session.ConstructObject("PolymorphicHolder", {}, PolymorphicHolder).bSuccess || !Session.SetField(PolymorphicHolder, "BaseRef", FValue(DerivedValue)).bSuccess || !Session.SetField(PolymorphicHolder, "InterfaceRef", FValue(DerivedValue)).bSuccess || !Session.SetField(PolymorphicHolder, "InterfaceRef", FValue(FObjectHandle{})).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("runtime field validation must honor base/interface assignability");
		}
		Host.DiagnosticText.clear();
		if (Session.SetField(
		               PolymorphicHolder, "BaseRef", FValue(OtherValue))
		        .bSuccess)
		{
			return Fail("runtime field validation must reject unrelated object types");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadSource(
		                "class DeletedApi { public: @hidden() int removed(int value) = delete; }\n"
		                "@hidden() void removed_free() = delete;\n"
		                "@asset(primary) export const int SharedValue = 9;\n",
		                "<deleted_metadata>")
		        .bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("deleted declarations should compile as metadata stubs");
		}
		std::vector<FInstanceMethodInfo> Methods;
		std::vector<FFreeFunctionInfo> Functions;
		if (!Session.EnumerateInstanceMethods("DeletedApi", Methods) || !Session.EnumerateFreeFunctions(Functions))
		{
			return Fail("deleted declarations should remain reflectable");
		}
		const auto HasHiddenDeleted = [](const auto& Entries, const std::string& Name)
		{
			for (const auto& Entry : Entries)
			{
				if (Entry.Name == Name && Entry.bDeleted)
				{
					for (const auto& Meta : Entry.DeclMeta)
					{
						if (Meta.first == "__annot" && Meta.second == "@hidden")
						{
							return true;
						}
					}
				}
			}
			return false;
		};
		if (!HasHiddenDeleted(Methods, "removed") || !HasHiddenDeleted(Functions, "removed_free"))
		{
			return Fail("deleted reflection must preserve annotations and bDeleted");
		}
		std::vector<FLoadedGlobalInfo> Globals;
		if (!Session.EnumerateGlobals(Globals))
		{
			return Fail("top-level globals should remain reflectable");
		}
		bool bFoundSharedValue = false;
		for (const FLoadedGlobalInfo& Global : Globals)
		{
			if (Global.Name != "SharedValue")
			{
				continue;
			}
			bFoundSharedValue = Global.DeclaredTypeName == "int" && Global.RuntimeTypeName == "int" && Global.bConst && Global.bExported;
			bool bHasAssetAnnotation = false;
			for (const auto& Meta : Global.DeclMeta)
			{
				bHasAssetAnnotation = bHasAssetAnnotation || (Meta.first == "__annot" && Meta.second == "@asset");
			}
			bFoundSharedValue = bFoundSharedValue && bHasAssetAnnotation;
		}
		if (!bFoundSharedValue)
		{
			return Fail("global reflection should preserve type, visibility, and annotations");
		}
		FObjectHandle Object;
		if (!Session.ConstructObject("DeletedApi", {}, Object).bSuccess || !Object)
		{
			return Fail("deleted reflection test object should construct");
		}
		const FRunResult DeletedCall =
		    Session.CallMethod(Object, "removed", {std::int32_t{1}});
		if (DeletedCall.bSuccess || DeletedCall.Error.MessageId != "diag.use_of_deleted_declaration")
		{
			return Fail("runtime reflection calls must reject deleted declarations");
		}
	}

	{
		const std::string InterceptSource =
		    "class Base {\n"
		    "public:\n"
		    "  virtual int calc(int value) { return value + 1; }\n"
		    "  int direct(int value) { return value + 2; }\n"
		    "  int pick(int value) { return value + 3; }\n"
		    "  int pick(string value) { return len(value); }\n"
		    "}\n"
		    "class Derived : Base {\n"
		    "public:\n"
		    "  int Count = 0;\n"
		    "  Derived() { Count = 0; }\n"
		    "  int calc(int value) override { return value + 10; }\n"
		    "  void bump() { Count = Count + 1; }\n"
		    "  int outer(int value) { return calc(value); }\n"
		    "  int exercise() { bump(); return calc(1) + direct(1) + pick(1) + Count; }\n"
		    "}\n"
		    "class GenericBox<T> { public: T Value; GenericBox() {} }\n"
		    "void main() { GenericBox<int> box = GenericBox<int>(); "
		    "Derived value = Derived(); print(value.exercise()); }\n";

		for (const EExecutionBackend Backend :
		    {EExecutionBackend::Vm, EExecutionBackend::Mixed})
		{
			FCaptureHost Host;
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.SetExecutionBackend(Backend);
			std::int32_t CalcCalls = 0;
			std::int32_t DirectCalls = 0;
			std::int32_t PickCalls = 0;
			std::int32_t BumpCalls = 0;
			std::int32_t OuterCalls = 0;
			std::int32_t LifecycleCalls = 0;
			bool bResolvedDerivedOverride = false;
			bool bResolvedBaseDirect = false;
			bool bResolvedIntOverload = false;
			Session.SetInstanceCallInterceptor(
			    [&](
			        const FInstanceCallInterceptContext& Context,
			        FValue& OutReturn,
			        FScriptError& OutError) -> EInstanceCallInterceptResult
			{
				if (Context.Target.MethodName == "Derived" || Context.Target.MethodName == "GenericBox" || (!Context.Target.MethodName.empty() && Context.Target.MethodName.front() == '~'))
				{
					++LifecycleCalls;
				}
				if (!std::holds_alternative<FObjectHandle>(Context.Receiver) || Context.Target.ProgramGeneration == 0 || Context.Target.FunctionIndex >= 0xFFFFu)
				{
					OutError.Message = "invalid interceptor context";
					return EInstanceCallInterceptResult::Failed;
				}
				if (Context.Target.MethodName == "calc")
				{
					++CalcCalls;
					bResolvedDerivedOverride =
					    Context.Target.DeclaringTypeName.find("Derived") != std::string_view::npos;
					OutReturn = std::int32_t{100};
					return EInstanceCallInterceptResult::Handled;
				}
				if (Context.Target.MethodName == "direct")
				{
					++DirectCalls;
					bResolvedBaseDirect =
					    Context.Target.DeclaringTypeName.find("Base") != std::string_view::npos;
					FValue Original;
					if (!Context.Proceed(Original, OutError))
					{
						return EInstanceCallInterceptResult::Failed;
					}
					OutReturn = std::get<std::int32_t>(Original) + 10;
					return EInstanceCallInterceptResult::Handled;
				}
				if (Context.Target.MethodName == "pick")
				{
					++PickCalls;
					bResolvedIntOverload =
					    Context.Target.QualifiedName.find("#3:int") != std::string_view::npos;
				}
				if (Context.Target.MethodName == "bump")
				{
					++BumpCalls;
					return EInstanceCallInterceptResult::Handled;
				}
				if (Context.Target.MethodName == "outer")
				{
					++OuterCalls;
					return Context.Proceed(OutReturn, OutError)
					    ? EInstanceCallInterceptResult::Handled
					    : EInstanceCallInterceptResult::Failed;
				}
				return EInstanceCallInterceptResult::Unhandled;
			});
			if (!Session.LoadSource(
			                InterceptSource,
			                "C:/cache#1/instance_interceptor.vs")
			        .bSuccess ||
			    !Session.Run().bSuccess || Host.PrintText != "117\n" || CalcCalls != 1 || DirectCalls != 1 || PickCalls != 1 || BumpCalls != 1 || LifecycleCalls != 0 || !bResolvedDerivedOverride || !bResolvedBaseDirect || !bResolvedIntOverload)
			{
				std::cerr << Host.DiagnosticText << Host.PrintText;
				return Fail("instance interceptor VM/JIT resolved dispatch should succeed");
			}

			FObjectHandle Object;
			if (!Session.ConstructObject("Derived", {}, Object).bSuccess || !Object)
			{
				return Fail("instance interceptor test should construct Derived");
			}
			FValue Return;
			const std::int32_t CalcBefore = CalcCalls;
			if (!Session.CallMethod(Object, "outer", {std::int32_t{2}}, &Return).bSuccess || !std::holds_alternative<std::int32_t>(Return) || std::get<std::int32_t>(Return) != 100 || CalcCalls != CalcBefore + 1 || OuterCalls != 1)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("Proceed should bypass only its current call and intercept nested calls");
			}

			const std::int32_t CalcBeforeBypass = CalcCalls;
			if (!Session.CallMethodBypassingInterceptor(
			                Object,
			                "outer",
			                {std::int32_t{3}},
			                &Return)
			        .bSuccess ||
			    !std::holds_alternative<std::int32_t>(Return) || std::get<std::int32_t>(Return) != 100 || OuterCalls != 1 || CalcCalls != CalcBeforeBypass + 1)
			{
				return Fail("entry bypass should preserve interception of nested calls");
			}
			const std::int32_t CalcBeforeBody = CalcCalls;
			if (!Session.CallMethodBypassingInterceptor(
			                Object,
			                "calc",
			                {std::int32_t{2}},
			                &Return)
			        .bSuccess ||
			    !std::holds_alternative<std::int32_t>(Return) || std::get<std::int32_t>(Return) != 12 || CalcCalls != CalcBeforeBody)
			{
				return Fail("entry bypass should execute the resolved original method body");
			}
		}
	}

	{
		const std::filesystem::path ScriptPath =
		    TemporaryRoot / "veroscript_instance_interceptor_generation.vs";
		const auto WriteGenerationScript =
		    [&](const int ReturnValue)
		{
			std::ofstream Script(ScriptPath, std::ios::binary);
			Script
			    << "class Versioned { public: int value() { return "
			    << ReturnValue
			    << "; } }\n"
			    << "void main() { Versioned item = Versioned(); print(item.value()); }\n";
		};
		WriteGenerationScript(1);
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		std::vector<std::uint64_t> Generations;
		std::vector<std::uint32_t> FunctionIndices;
		Session.SetInstanceCallInterceptor(
		    [&](
		        const FInstanceCallInterceptContext& Context,
		        FValue&,
		        FScriptError&) -> EInstanceCallInterceptResult
		{
			if (Context.Target.MethodName == "value")
			{
				Generations.push_back(Context.Target.ProgramGeneration);
				FunctionIndices.push_back(Context.Target.FunctionIndex);
			}
			return EInstanceCallInterceptResult::Unhandled;
		});
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run().bSuccess)
		{
			std::filesystem::remove(ScriptPath);
			return Fail("interceptor generation test should load and run");
		}
		WriteGenerationScript(2);
		if (!Session.Reload().bSuccess || !Session.Run().bSuccess || Host.PrintText != "1\n2\n" || Generations.size() != 2 || Generations[0] == 0 || Generations[1] == 0 || Generations[0] == Generations[1] || FunctionIndices.size() != 2)
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			std::filesystem::remove(ScriptPath);
			return Fail("hot reload must advance resolved method program generation");
		}
		std::filesystem::remove(ScriptPath);
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		std::int32_t Calls = 0;
		Session.SetInstanceCallInterceptor(
		    [&](const FInstanceCallInterceptContext& Context, FValue& Out, FScriptError& Error)
		        -> EInstanceCallInterceptResult
		{
			if (Context.Target.MethodName != "get") return EInstanceCallInterceptResult::Unhandled;
			++Calls;
			return Context.Proceed(Out, Error)
			    ? EInstanceCallInterceptResult::Handled
			    : EInstanceCallInterceptResult::Failed;
		});
		if (!Session.LoadSource(
		                "class RefIntercept { public: int value = 7; "
		                "int& get(int& input) { input++; return value; } };\n"
		                "void main() { RefIntercept object = RefIntercept(); int n = 2; "
		                "method_info<RefIntercept> method = "
		                "method_info<RefIntercept>(type<RefIntercept>().qualified_name, 0); "
		                "method.invoke<int&>(object, &n) = 42; print(object.value); print(n); }\n",
		                "<reflection_reference_interceptor>")
		        .bSuccess ||
		    !Session.Run().bSuccess || Host.PrintText != "42\n3\n" || Calls != 1)
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			return Fail("reflected reference calls must preserve aliases through interceptor Proceed");
		}
		Session.SetInstanceCallInterceptor(
		    [](const FInstanceCallInterceptContext& Context, FValue& Out, FScriptError&)
		        -> EInstanceCallInterceptResult
		{
			if (Context.Target.MethodName != "get") return EInstanceCallInterceptResult::Unhandled;
			Out = std::int32_t{42};
			return EInstanceCallInterceptResult::Handled;
		});
		const auto InvalidReturn = Session.Run();
		if (InvalidReturn.bSuccess || InvalidReturn.Error.Message.find("return type mismatch") == std::string::npos)
		{
			return Fail("reference-returning interceptors must reject scalar handled results");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadSource(
		                "struct InterceptValue { public: int value = 7; void change() { value = 42; } };\n"
		                "class ValueOwner { public: InterceptValue value = InterceptValue(); "
		                "int number = 11; InterceptValue copy() { return value; } "
		                "int& number_ref() { return number; } int snapshot() { return number; } };\n"
		                "void main() { ValueOwner object = ValueOwner(); object.copy().change(); "
		                "print(object.value.value); "
		                "method_info<ValueOwner> method = method_info<ValueOwner>(type<ValueOwner>().qualified_name, 0); "
		                "method.invoke<InterceptValue>(object).change(); print(object.value.value); "
		                "int& alias = object.number_ref(); int snapshot = object.snapshot(); alias++; print(snapshot); "
		                "method_info<ValueOwner> scalar = method_info<ValueOwner>(type<ValueOwner>().qualified_name, 2); "
		                "int reflected = scalar.invoke<int>(object); alias++; print(reflected); }\n",
		                "<interceptor_value_return>")
		        .bSuccess)
		{
			return Fail("interceptor value-return probe should compile");
		}
		Session.SetInstanceCallInterceptor(
		    [](const FInstanceCallInterceptContext& Context, FValue& Out, FScriptError&)
		        -> EInstanceCallInterceptResult
		{
			if (Context.Target.MethodName != "copy" && Context.Target.MethodName != "snapshot")
			{
				return EInstanceCallInterceptResult::Unhandled;
			}
			Out = std::get<FObjectHandle>(Context.Receiver)->Fields[Context.Target.MethodName == "copy" ? 0 : 1];
			return EInstanceCallInterceptResult::Handled;
		});
		if (!Session.Run().bSuccess || Host.PrintText != "7\n7\n11\n12\n")
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			return Fail("interceptor handled value returns must copy borrowed structs and unwrap reference cells");
		}
	}

	{
		// An interceptor belongs to its Session; a second Session must retain
		// ordinary execution even when both load identical bytecode.
		const std::string Source =
		    "class Isolated { public: int value() { return 7; } }\n"
		    "void main() { Isolated item = Isolated(); print(item.value()); }\n";
		FCaptureHost InterceptedHost;
		FCaptureHost PlainHost;
		VeroScriptSession Intercepted(InterceptedHost);
		VeroScript::Test::Configure(Intercepted);
		VeroScriptSession Plain(PlainHost);
		VeroScript::Test::Configure(Plain);
		Intercepted.SetInstanceCallInterceptor(
		    [](const FInstanceCallInterceptContext& Context, FValue& Out, FScriptError&)
		        -> EInstanceCallInterceptResult
		{
			if (Context.Target.MethodName == "value")
			{
				Out = std::int32_t{9};
				return EInstanceCallInterceptResult::Handled;
			}
			return EInstanceCallInterceptResult::Unhandled;
		});
		if (!Intercepted.LoadSource(Source, "<interceptor_isolation>").bSuccess || !Plain.LoadSource(Source, "<interceptor_isolation>").bSuccess || !Intercepted.Run().bSuccess || !Plain.Run().bSuccess || InterceptedHost.PrintText != "9\n" || PlainHost.PrintText != "7\n")
		{
			return Fail("instance interceptors must remain Session-local");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadSource(
		                "class Failure { public: int run() { return 1; } }\n",
		                "<interceptor_failure>")
		        .bSuccess)
		{
			return Fail("interceptor failure test should compile");
		}
		FObjectHandle Object;
		if (!Session.ConstructObject("Failure", {}, Object).bSuccess)
		{
			return Fail("interceptor failure test should construct");
		}
		Session.SetInstanceCallInterceptor(
		    [](const FInstanceCallInterceptContext&, FValue&, FScriptError& OutError)
		        -> EInstanceCallInterceptResult
		{
			OutError.Message = "host rejected call";
			return EInstanceCallInterceptResult::Failed;
		});
		const FRunResult Call = Session.CallMethod(Object, "run");
		if (Call.bSuccess || Call.Error.Message != "host rejected call")
		{
			return Fail("interceptor failure should propagate structured runtime error");
		}
		Session.SetInstanceCallInterceptor(
		    [](const FInstanceCallInterceptContext& Context, FValue& Out, FScriptError& Error)
		        -> EInstanceCallInterceptResult
		{
			(void)Context.Proceed(Out, Error);
			return EInstanceCallInterceptResult::Unhandled;
		});
		const FRunResult InvalidProceed = Session.CallMethod(Object, "run");
		if (InvalidProceed.bSuccess || InvalidProceed.Error.Message.find("Unhandled after calling Proceed") == std::string::npos)
		{
			return Fail("Proceed followed by Unhandled must be rejected without double execution");
		}
		Session.SetInstanceCallInterceptor(
		    [](const FInstanceCallInterceptContext& Context, FValue& Out, FScriptError& Error)
		        -> EInstanceCallInterceptResult
		{
			if (!Context.Proceed(Out, Error))
			{
				return EInstanceCallInterceptResult::Failed;
			}
			// A second Proceed is invalid even if the interceptor tries to
			// conceal that failure behind Handled.
			(void)Context.Proceed(Out, Error);
			return EInstanceCallInterceptResult::Handled;
		});
		const FRunResult ConcealedProceedFailure = Session.CallMethod(Object, "run");
		if (ConcealedProceedFailure.bSuccess || ConcealedProceedFailure.Error.Message.find("only once") == std::string::npos)
		{
			return Fail("a failed Proceed must not be concealed by Handled");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		const FRunResult Load = Session.LoadSource(
		    "class Over {\n"
		    "public:\n"
		    "\tint F(int X) { return X; }\n"
		    "\tint F(string S) { return 7; }\n"
		    "\tint G(float X) { return 1; }\n"
		    "\tint G(double X) { return 2; }\n"
		    "\tint Wide(int64 X) { return 64; }\n"
		    "\tint Wide(uint64 X) { return 65; }\n"
		    "\tint Small(char X) { return 1; }\n"
		    "\tint Small(short X) { return 2; }\n"
		    "}\n"
		    "enum class Mode : int64 { Active = 7i64 }\n"
		    "enum class OtherMode : int64 { Active = 8i64 }\n"
		    "int EnumPick(Mode Value) { return 66; }\n"
		    "int EnumPick(OtherMode Value) { return 68; }\n"
		    "int UnsignedPick(unsigned int Value) { return 67; }\n"
		    "class Stopper {\n"
		    "public:\n"
		    "\tvoid Boom() { exit(9); }\n"
		    "}\n"
		    "interface ITick {\n"
		    "\tvoid Tick() {}\n"
		    "}\n"
		    "class Ticked : public ITick {}\n"
		    "int Pick(int X) { return X; }\n"
		    "int Pick(string S) { return 9; }\n"
		    "class CtorBox {\n"
		    "public:\n"
		    "\tint Value = 0;\n"
		    "\tCtorBox(int V) { Value = V; }\n"
		    "}\n",
		    "<session_resolve_hardening>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("resolve-hardening LoadSource should succeed");
		}
		{
			FObjectHandle Discard;
			const FRunResult UnknownType = Session.ConstructObject("NoSuchType", {}, Discard);
			if (UnknownType.bSuccess || UnknownType.ExitCode != 4 || UnknownType.Error.MessageId != "diag.constructobject_unknown_type")
			{
				std::cerr << Host.DiagnosticText << UnknownType.Error.MessageId << '\n';
				return Fail("ConstructObject unknown type should be ExitCode 4");
			}
		}
		FObjectHandle Over;
		if (!Session.ConstructObject("Over", {}, Over).bSuccess || !Over)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ConstructObject Over should succeed");
		}
		{
			FObjectHandle Built;
			if (!Session.ConstructObject(
			                "CtorBox",
			                {static_cast<std::int32_t>(8)},
			                Built)
			        .bSuccess ||
			    !Built)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("ConstructObject CtorBox(8) should succeed");
			}
			FValue Value;
			if (!Session.GetField(Built, "Value", Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 8)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("CtorBox.Value should be 8 after ConstructObject");
			}
		}
		if (!Session.HasInstanceMethod(Over, "F", 1) || !Session.HasFreeFunction("Pick", 1))
		{
			return Fail("Has* should be true for overloaded name+arity");
		}
		{
			std::vector<FInstanceMethodInfo> OverMethods;
			if (!Session.EnumerateInstanceMethods("Over", OverMethods))
			{
				return Fail("EnumerateInstanceMethods Over should succeed");
			}
			int FCount = 0;
			for (const FInstanceMethodInfo& Method : OverMethods)
			{
				if (Method.Name == "F" && Method.ParamCount == 1)
				{
					++FCount;
				}
			}
			if (FCount < 2)
			{
				return Fail("EnumerateInstanceMethods should list both F overloads");
			}
		}
		{
			FValue Ret;
			const FRunResult Converted = Session.CallMethod(Over, "F", {1.5}, &Ret);
			if (!Converted.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 1)
			{
				std::cerr << Host.DiagnosticText << Converted.Error.MessageId << '\n';
				return Fail("by-value external calls should apply numeric conversion");
			}
		}
		{
			FObjectHandle Discard;
			const FRunResult BadCtor = Session.ConstructObject(
			    "CtorBox",
			    {std::string("x")},
			    Discard);
			if (BadCtor.bSuccess || BadCtor.ExitCode != 4 || BadCtor.Error.MessageId != "diag.constructobject_unknown_constructor")
			{
				std::cerr << Host.DiagnosticText << BadCtor.Error.MessageId << '\n';
				return Fail("ConstructObject bad ctor args should be ExitCode 4");
			}
		}
		{
			FValue Ret;
			const FRunResult TypedInt = Session.CallMethod(
			    Over,
			    "F",
			    {static_cast<std::int32_t>(1)},
			    &Ret);
			if (!TypedInt.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 1)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("runtime int arg should select F(int)");
			}
		}
		{
			FValue Ret;
			const FRunResult TypedStr = Session.CallMethod(
			    Over,
			    "F",
			    {std::string("x")},
			    &Ret);
			if (!TypedStr.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 7)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("runtime string arg should select F(string)");
			}
		}
		{
			FValue Ret;
			const FRunResult DoubleCall = Session.CallMethod(Over, "G", {1.5}, &Ret);
			if (!DoubleCall.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 2)
			{
				std::cerr << Host.DiagnosticText << DoubleCall.Error.MessageId << '\n';
				return Fail("runtime double should select G(double)");
			}
			const FRunResult FloatCall = Session.CallMethod(Over, "G", {1.5f}, &Ret);
			if (!FloatCall.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 1)
			{
				std::cerr << Host.DiagnosticText << FloatCall.Error.MessageId << '\n';
				return Fail("runtime float should select G(float)");
			}
		}
		{
			FValue Ret;
			const FRunResult TypedInt64 = Session.CallMethod(
			    Over,
			    "Wide",
			    {std::int64_t{9}},
			    &Ret);
			if (!TypedInt64.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 64)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("runtime int64 arg should select Wide(int64)");
			}
			const FRunResult TypedUInt64 = Session.CallMethod(
			    Over,
			    "Wide",
			    {std::uint64_t{9}},
			    &Ret);
			if (!TypedUInt64.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 65)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("runtime uint64 arg should select Wide(uint64)");
			}
		}
		{
			const FRunResult AmbiguousSmall = Session.CallMethod(
			    Over,
			    "Small",
			    {std::int32_t{1}});
			if (AmbiguousSmall.bSuccess || AmbiguousSmall.ExitCode != 4 || AmbiguousSmall.Error.MessageId != "diag.callmethod_ambiguous_method")
			{
				std::cerr << Host.DiagnosticText << AmbiguousSmall.Error.MessageId << '\n';
				return Fail("equally ranked int-to-char/short conversions should be ambiguous");
			}
		}
		{
			FValue Ret;
			const FRunResult TypedUnsigned = Session.CallFunction(
			    "UnsignedPick",
			    {std::uint32_t{3}},
			    &Ret);
			if (!TypedUnsigned.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 67)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("runtime uint32 arg should match unsigned int");
			}
			const FRunResult TypedEnum = Session.CallFunction(
			    "EnumPick",
			    {FEnumValue{"Mode", 7}},
			    &Ret);
			if (!TypedEnum.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 66)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("runtime strong enum arg should match its enum type");
			}
			const FRunResult TypedOtherEnum = Session.CallFunction(
			    "EnumPick",
			    {FEnumValue{"OtherMode", 8}},
			    &Ret);
			if (!TypedOtherEnum.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 68)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("distinct strong enums must retain distinct overload signatures");
			}
		}
		{
			FValue Ret;
			const FRunResult Exact = Session.CallMethod(
			    Over,
			    "F#3:int",
			    {static_cast<std::int32_t>(3)},
			    &Ret);
			if (!Exact.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 3)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("mangled leaf F#3:int should disambiguate CallMethod");
			}
		}
		{
			FValue Ret;
			const FRunResult FreeTyped = Session.CallFunction(
			    "Pick",
			    {static_cast<std::int32_t>(4)},
			    &Ret);
			if (!FreeTyped.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 4)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("runtime int arg should select Pick(int)");
			}
		}
		{
			Host.DiagnosticText.clear();
			const FRunResult QuietMissing = Session.CallFunction(
			    "NoSuchFn",
			    {},
			    nullptr,
			    EMissingMethodPolicy::Quiet);
			if (QuietMissing.bSuccess || QuietMissing.ExitCode != 4 || QuietMissing.Error.MessageId != "diag.callfunction_unknown_function" || !Host.DiagnosticText.empty())
			{
				return Fail("CallFunction Quiet unknown should not write diagnostics");
			}
		}
		{
			std::vector<FFreeFunctionInfo> Fns;
			if (!Session.EnumerateFreeFunctions(Fns))
			{
				return Fail("EnumerateFreeFunctions should succeed");
			}
			int PickCount = 0;
			for (const FFreeFunctionInfo& Fn : Fns)
			{
				if (Fn.Name == "Pick" && Fn.ParamCount == 1)
				{
					++PickCount;
				}
			}
			if (PickCount < 2)
			{
				return Fail("EnumerateFreeFunctions should list both Pick overloads");
			}
		}
		{
			std::vector<FInstanceMethodInfo> Methods;
			if (!Session.EnumerateInstanceMethods("Ticked", Methods))
			{
				return Fail("EnumerateInstanceMethods Ticked should succeed");
			}
			bool bSawTick = false;
			for (const FInstanceMethodInfo& Method : Methods)
			{
				if (Method.Name == "Tick" && Method.ParamCount == 0)
				{
					bSawTick = true;
				}
			}
			if (!bSawTick)
			{
				return Fail("EnumerateInstanceMethods should include interface default Tick");
			}
			if (!Session.HasInstanceMethodOnType("Ticked", "Tick", 0))
			{
				return Fail("HasInstanceMethodOnType should see interface default Tick");
			}
		}
		{
			FObjectHandle Stopper;
			if (!Session.ConstructObject("Stopper", {}, Stopper).bSuccess || !Stopper)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("ConstructObject Stopper should succeed");
			}
			const FRunResult ExitCall = Session.CallMethod(Stopper, "Boom");
			if (!ExitCall.bSuccess || !ExitCall.bExitRequested || ExitCall.ExitCode != 9)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("exit() inside CallMethod should report bExitRequested + code");
			}
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FHostTypeId ActorId;
		if (!Session.RegisterHostType("AActor", EHostTypeKind::Class, &ActorId) || !Session.SetHostTypeInheritable(ActorId, true) || !Session.RegisterHostTypeSubclassFactory(ActorId, ActorSubclassFactory))
		{
			return Fail("host AActor registration should succeed");
		}
		ActorSubclassCalls = 0;
		const FRunResult Load = Session.LoadSource(
		    "class MyActor : AActor {\n"
		    "public:\n"
		    "\tint Extra = 9;\n"
		    "}\n",
		    "<session_script_owner>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("MyActor LoadSource should succeed");
		}
		FObjectHandle Actor;
		const FRunResult Construct = Session.ConstructObject("MyActor", {}, Actor);
		if (!Construct.bSuccess || !Actor || !Actor->GetHostBase())
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ConstructObject MyActor should produce HostBase");
		}
		if (ActorSubclassCalls != 1)
		{
			return Fail("@subclass factory should run once");
		}
		const FObjectHandle Owner = GetScriptOwner(Actor->GetHostBase());
		if (Owner.get() != Actor.get())
		{
			return Fail("GetScriptOwner should return the constructing script instance");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FHostTypeId ActorId;
		if (!Session.RegisterHostType("AActor", EHostTypeKind::Class, &ActorId) || !Session.SetHostTypeInheritable(ActorId, true) || !Session.RegisterHostTypeSubclassFactory(ActorId, ActorSubclassFactory))
		{
			return Fail("host Tick registry should succeed");
		}
		ActorSubclassCalls = 0;
		const FRunResult Load = Session.LoadSource(
		    "class MyActor : AActor {\n"
		    "public:\n"
		    "\tfloat Acc = 0.0;\n"
		    "\tvoid BeginPlay() { Acc = 1.0; }\n"
		    "\tvoid Tick(float Dt) { Acc = Acc + Dt; }\n"
		    "\tfloat GetAcc() { return Acc; }\n"
		    "}\n"
		    "class Plain {}\n",
		    "<session_host_tick>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("MyActor Tick LoadSource should succeed");
		}
		if (!Session.GetLoadedModule())
		{
			return Fail("GetLoadedModule should be non-null after Load");
		}
		if (!Session.HasInstanceMethodOnType("MyActor", "Tick", 1) || !Session.HasInstanceMethodOnType("MyActor", "BeginPlay", 0) || Session.HasInstanceMethodOnType("MyActor", "Tick", 0) || Session.HasInstanceMethodOnType("Plain", "Tick", 1))
		{
			return Fail("HasInstanceMethodOnType should see MyActor Tick/BeginPlay only");
		}

		std::vector<FLoadedTypeInfo> HostTypes;
		if (!Session.EnumerateTypes(HostTypes, true) || HostTypes.size() != 1 || HostTypes[0].SimpleName != "MyActor" || HostTypes[0].HostBaseTypeName != "AActor" || HostTypes[0].HostBaseTypeId != ActorId.Value)
		{
			return Fail("EnumerateTypes(host-only) should list MyActor : AActor");
		}

		std::vector<FInstanceMethodInfo> Methods;
		if (!Session.EnumerateInstanceMethods("MyActor", Methods))
		{
			return Fail("EnumerateInstanceMethods MyActor should succeed");
		}
		bool bSawTick = false;
		bool bSawBegin = false;
		for (const FInstanceMethodInfo& Method : Methods)
		{
			if (Method.Name == "Tick" && Method.ParamCount == 1 && Method.bReturnsVoid)
			{
				bSawTick = true;
				if (Method.ParamTypeNames.size() != 1 || Method.ParamTypeNames[0] != "float")
				{
					return Fail("Tick metadata should decode ParamTypeNames=[float]");
				}
			}
			if (Method.Name == "BeginPlay" && Method.ParamCount == 0 && Method.bReturnsVoid)
			{
				bSawBegin = true;
			}
		}
		if (!bSawTick || !bSawBegin)
		{
			return Fail("EnumerateInstanceMethods should list BeginPlay and Tick(float)");
		}

		std::vector<FLoadedFieldInfo> Fields;
		if (!Session.EnumerateFields("MyActor", Fields, true))
		{
			return Fail("EnumerateFields MyActor should succeed");
		}
		bool bSawAcc = false;
		for (const FLoadedFieldInfo& Field : Fields)
		{
			if (Field.Name == "Acc" && Field.TypeName == "float" && Field.bDeclaredOnType)
			{
				bSawAcc = true;
			}
		}
		if (!bSawAcc)
		{
			return Fail("EnumerateFields should list declared Acc:float");
		}

		FObjectHandle Actor;
		const FRunResult Construct = Session.ConstructObject("MyActor", {}, Actor);
		if (!Construct.bSuccess || !Actor || !Actor->GetHostBase())
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ConstructObject MyActor (Tick suite) should succeed");
		}
		if (!Session.HasInstanceMethod(Actor, "Tick", 1))
		{
			return Fail("HasInstanceMethod on host subclass should see Tick");
		}
		{
			const FRunResult Begin = Session.CallMethod(Actor, "BeginPlay");
			if (!Begin.bSuccess)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("CallMethod BeginPlay on host subclass should succeed");
			}
		}
		{
			std::vector<FValue> Args{0.25};
			const FRunResult Tick = Session.CallMethod(Actor, "Tick", Args);
			if (!Tick.bSuccess)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("CallMethod Tick(float) on host subclass should succeed");
			}
		}
		{
			FValue Acc;
			const FRunResult GetAcc = Session.CallMethod(Actor, "GetAcc", {}, &Acc);
			if (!GetAcc.bSuccess || !std::holds_alternative<float>(Acc) || std::get<float>(Acc) != 1.25f)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("host subclass Acc should be 1.25 after BeginPlay+Tick");
			}
		}
		if (GetScriptOwner(Actor->GetHostBase()).get() != Actor.get())
		{
			return Fail("host subclass ScriptOwner should stay bound after CallMethod");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FHostTypeId VecId;
		if (!Session.RegisterHostType("Vec2", EHostTypeKind::Struct, &VecId) || !Session.SetHostTypeInheritable(VecId, true) || !Session.RegisterHostTypeClone(VecId, Vec2Clone) || !Session.RegisterHostTypeSubclassFactory(VecId, Vec2SubclassFactory))
		{
			return Fail("Vec2 HostBase registry should succeed");
		}
		Vec2SubclassCalls = 0;
		const FRunResult Load = Session.LoadSource(
		    "struct MyVec : Vec2 {\n"
		    "\tint Tag = 1;\n"
		    "\tMyVec Dup() {\n"
		    "\t\tMyVec Copy = this;\n"
		    "\t\treturn Copy;\n"
		    "\t}\n"
		    "}\n",
		    "<session_struct_clone_owner>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("MyVec LoadSource should succeed");
		}
		FObjectHandle Original;
		const FRunResult Construct = Session.ConstructObject("MyVec", {}, Original);
		if (!Construct.bSuccess || !Original || !Original->GetHostBase())
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ConstructObject MyVec should succeed");
		}
		FValue DupValue;
		const FRunResult Dup = Session.CallMethod(Original, "Dup", {}, &DupValue);
		if (!Dup.bSuccess || !std::holds_alternative<FObjectHandle>(DupValue))
		{
			std::cerr << Host.DiagnosticText;
			return Fail("MyVec.Dup should return a script struct");
		}
		const FObjectHandle Copy = std::get<FObjectHandle>(DupValue);
		if (!Copy || !Copy->GetHostBase())
		{
			return Fail("MyVec.Dup copy should have HostBase");
		}
		if (Copy.get() == Original.get())
		{
			return Fail("struct Dup should allocate a distinct script instance");
		}
		if (Copy->GetHostBase().get() == Original->GetHostBase().get())
		{
			return Fail("struct @clone should produce a distinct HostBase handle");
		}
		if (GetScriptOwner(Original->GetHostBase()).get() != Original.get())
		{
			return Fail("original HostBase ScriptOwner must stay original");
		}
		if (GetScriptOwner(Copy->GetHostBase()).get() != Copy.get())
		{
			return Fail("cloned HostBase ScriptOwner must be the copy, not original");
		}
		if (GetScriptOwner(Copy->GetHostBase()).get() == Original.get())
		{
			return Fail("cloned HostBase must not retain original ScriptOwner");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);

		FObjectHandle Stashed;
		FHostTypeId ActorId;
		if (!Session.RegisterHostType("AActor", EHostTypeKind::Class, &ActorId) || !Session.SetHostTypeInheritable(ActorId, true) || !Session.RegisterHostTypeSubclassFactory(ActorId, ActorSubclassFactory))
		{
			return Fail("hot-reload host AActor setup should succeed");
		}
		FHostMemberDesc Capture;
		Capture.Kind = EHostMemberKind::Method;
		Capture.Name = "capture";
		Capture.ReturnType = ENativeType::Void;
		Capture.Callback =
		    [](const FNativeCallContext& Context, FValue& OutReturn, std::string& OutError) -> bool
		{
			(void)OutReturn;
			auto* const Out = static_cast<FObjectHandle*>(Context.UserData);
			if (!Out || Context.ArgCount < 1)
			{
				OutError = "capture expects host receiver";
				return false;
			}
			const auto* const HostHandle = std::get_if<FHostObjectHandle>(&Context.Args[0]);
			if (!HostHandle || !*HostHandle)
			{
				OutError = "capture expects HostObject receiver";
				return false;
			}
			*Out = GetScriptOwner(*HostHandle);
			if (!*Out)
			{
				OutError = "capture ScriptOwner empty";
				return false;
			}
			return true;
		};
		Capture.UserData = &Stashed;
		if (!Session.RegisterHostMember(ActorId, Capture))
		{
			return Fail("RegisterHostMember capture should succeed");
		}

		const std::filesystem::path TempRoot =
		    TemporaryRoot / "callmethod-hotreload";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "entity.vs";
		{
			std::ofstream(ScriptPath)
			    << "class Entity : AActor {\n"
			    << "public:\n"
			    << "\tint Hits = 0;\n"
			    << "\tvoid Hit() { Hits = Hits + 1; }\n"
			    << "\tint GetHits() { return Hits; }\n"
			    << "}\n"
			    << "Entity g = Entity();\n"
			    << "void main() { g.capture(); }\n";
		}
		ActorSubclassCalls = 0;
		const FRunResult Load = Session.LoadFile(ScriptPath.generic_string());
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot-reload CallMethod LoadFile should succeed");
		}
		if (!Session.Run().bSuccess || !Stashed)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot-reload first Run should capture Entity via ScriptOwner");
		}
		if (!Session.CallMethod(Stashed, "Hit").bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("CallMethod before reload should succeed");
		}
		{
			std::ofstream(ScriptPath)
			    << "class Entity : AActor {\n"
			    << "public:\n"
			    << "\tint Hits = 0;\n"
			    << "\tint Bonus = 10;\n"
			    << "\tvoid Hit() { Hits = Hits + 1; }\n"
			    << "\tint GetHits() { return Hits + Bonus; }\n"
			    << "}\n"
			    << "Entity g = Entity();\n"
			    << "void main() { g.capture(); }\n";
		}
		Host.DiagnosticText.clear();
		const FRunResult Reload = Session.Reload();
		if (!Reload.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("compatible Reload should succeed");
		}
		if (!Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("post-reload Eval should finish migration init");
		}
		if (!Session.CallMethod(Stashed, "Hit").bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("CallMethod after reload should keep object identity");
		}
		FValue Hits;
		const FRunResult GetHits = Session.CallMethod(Stashed, "GetHits", {}, &Hits);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!GetHits.bSuccess || !std::holds_alternative<std::int32_t>(Hits) || std::get<std::int32_t>(Hits) != 12)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("after reload Hits+Bonus should be 12 (2 hits + 10)");
		}
	}

	{
		// Hot reload with script-enum object fields: retained values must migrate,
		// and a field added without an initializer must default to 0.
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "hotreload-enum-field";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "enum_field.vs";
		std::ofstream(ScriptPath)
		    << "enum class Color { Red = 1, Green = 2, Blue = 4 }\n"
		    << "class Holder {\n"
		    << "public:\n"
		    << "\tColor c = Color::Green;\n"
		    << "\tint n = 7;\n"
		    << "}\n"
		    << "Holder g = Holder();\n"
		    << "Color GetC() { return g.c; }\n"
		    << "void main() {}\n";
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("enum-field hot reload setup should succeed");
		}
		std::ofstream(ScriptPath)
		    << "enum class Color { Red = 1, Green = 2, Blue = 4 }\n"
		    << "class Holder {\n"
		    << "public:\n"
		    << "\tColor c = Color::Green;\n"
		    << "\tint n = 7;\n"
		    << "\tColor added;\n"
		    << "}\n"
		    << "Holder g = Holder();\n"
		    << "Color GetC() { return g.c; }\n"
		    << "Color GetAdded() { return g.added; }\n"
		    << "void main() {}\n";
		Host.DiagnosticText.clear();
		if (!Session.Reload().bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("reload with enum object field should migrate");
		}
		if (!Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("enum-field post-reload Eval should succeed");
		}
		FValue C;
		FValue Added;
		const FRunResult GetC = Session.CallFunction("GetC", {}, &C);
		const FRunResult GetAdded = Session.CallFunction("GetAdded", {}, &Added);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!GetC.bSuccess || !std::holds_alternative<FEnumValue>(C) || std::get<FEnumValue>(C).TypeName != "Color" || std::get<FEnumValue>(C).Value != 2)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("retained enum field should keep Color::Green after reload");
		}
		if (!GetAdded.bSuccess || !std::holds_alternative<FEnumValue>(Added) || std::get<FEnumValue>(Added).TypeName != "Color" || std::get<FEnumValue>(Added).Value != 0)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("enum field added by reload should default to 0");
		}
	}

	{
		// Namespaced enum fields keep dotted type names; migrate + default like top-level.
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "hotreload-namespace-enum";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "ns_enum.vs";
		std::ofstream(ScriptPath)
		    << "namespace Ns {\n"
		    << "\tenum class Color { Red = 1, Green = 2 }\n"
		    << "}\n"
		    << "class Holder {\n"
		    << "public:\n"
		    << "\tNs::Color c = Ns::Color::Green;\n"
		    << "}\n"
		    << "Holder g = Holder();\n"
		    << "Ns::Color GetC() { return g.c; }\n"
		    << "void main() {}\n";
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("namespaced enum-field setup should succeed");
		}
		std::ofstream(ScriptPath)
		    << "namespace Ns {\n"
		    << "\tenum class Color { Red = 1, Green = 2 }\n"
		    << "}\n"
		    << "class Holder {\n"
		    << "public:\n"
		    << "\tNs::Color c = Ns::Color::Green;\n"
		    << "\tNs::Color added;\n"
		    << "}\n"
		    << "Holder g = Holder();\n"
		    << "Ns::Color GetC() { return g.c; }\n"
		    << "Ns::Color GetAdded() { return g.added; }\n"
		    << "void main() {}\n";
		Host.DiagnosticText.clear();
		if (!Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("namespaced enum-field reload should migrate");
		}
		FValue C;
		FValue Added;
		const FRunResult GetC = Session.CallFunction("GetC", {}, &C);
		const FRunResult GetAdded = Session.CallFunction("GetAdded", {}, &Added);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!GetC.bSuccess || !std::holds_alternative<FEnumValue>(C) || std::get<FEnumValue>(C).TypeName != "Ns.Color" || std::get<FEnumValue>(C).Value != 2 || !GetAdded.bSuccess || !std::holds_alternative<FEnumValue>(Added) || std::get<FEnumValue>(Added).TypeName != "Ns.Color" || std::get<FEnumValue>(Added).Value != 0)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("namespaced enum fields should retain/default with enum identity");
		}
	}

	{
		// Host enum fields are not in Module.Enums; migration must consult bindings.
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		FHostTypeId ColorId;
		if (!Session.RegisterHostType("HostColor", EHostTypeKind::Enum, &ColorId))
		{
			return Fail("RegisterHostType HostColor should succeed");
		}
		auto AddMember = [&](const char* Name, const std::int32_t Value) -> bool
		{
			FHostMemberDesc Member;
			Member.Kind = EHostMemberKind::EnumMember;
			Member.Name = Name;
			Member.EnumValue = Value;
			return Session.RegisterHostMember(ColorId, Member);
		};
		if (!AddMember("Red", 1) || !AddMember("Green", 2) || !AddMember("Blue", 4))
		{
			return Fail("RegisterHostMember HostColor.* should succeed");
		}
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "hotreload-host-enum";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "host_enum.vs";
		std::ofstream(ScriptPath)
		    << "class Holder {\n"
		    << "public:\n"
		    << "\tHostColor c = HostColor::Green;\n"
		    << "}\n"
		    << "Holder g = Holder();\n"
		    << "int GetC() { return g.c; }\n"
		    << "void main() {}\n";
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("host-enum field setup should succeed");
		}
		std::ofstream(ScriptPath)
		    << "class Holder {\n"
		    << "public:\n"
		    << "\tHostColor c = HostColor::Green;\n"
		    << "\tHostColor added;\n"
		    << "}\n"
		    << "Holder g = Holder();\n"
		    << "int GetC() { return g.c; }\n"
		    << "int GetAdded() { return g.added; }\n"
		    << "void main() {}\n";
		Host.DiagnosticText.clear();
		if (!Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("host-enum field reload should migrate");
		}
		FValue C;
		FValue Added;
		const FRunResult GetC = Session.CallFunction("GetC", {}, &C);
		const FRunResult GetAdded = Session.CallFunction("GetAdded", {}, &Added);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!GetC.bSuccess || !std::holds_alternative<std::int32_t>(C) || std::get<std::int32_t>(C) != 2 || !GetAdded.bSuccess || !std::holds_alternative<std::int32_t>(Added) || std::get<std::int32_t>(Added) != 0)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("host enum fields should retain/default as int");
		}
	}

	{
		// Private struct leaf matching an exported enum must still default to a struct,
		// not int32 (IsEnumRuntimeType must not leaf-match module-qualified type names).
		// (Class fields intentionally default to null in MakeDefaultValue; structs do not.)
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "hotreload-enum-class-leaf";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::string TempRootStr = TempRoot.generic_string();
		FEngineConfig Config;
		Config.ImportPaths = {TempRootStr};
		Session.ApplyConfig(Config);
		std::ofstream(TempRoot / "color.vs")
		    << "export enum Color { Red = 1, Green = 2 }\n";
		const std::filesystem::path ScriptPath = TempRoot / "main.vs";
		std::ofstream(ScriptPath)
		    << "import \"color.vs\";\n"
		    << "struct Color {\n"
		    << "\tint x = 9;\n"
		    << "}\n"
		    << "class Holder {\n"
		    << "public:\n"
		    << "\tint n = 1;\n"
		    << "}\n"
		    << "Holder g = Holder();\n"
		    << "int GetN() { return g.n; }\n"
		    << "void main() {}\n";
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("enum/struct leaf-collision setup should succeed");
		}
		std::ofstream(ScriptPath)
		    << "import \"color.vs\";\n"
		    << "struct Color {\n"
		    << "\tint x = 9;\n"
		    << "}\n"
		    << "class Holder {\n"
		    << "public:\n"
		    << "\tint n = 1;\n"
		    << "\tColor c;\n"
		    << "}\n"
		    << "Holder g = Holder();\n"
		    << "int GetN() { return g.n; }\n"
		    << "int GetX() { return g.c.x; }\n"
		    << "void main() {}\n";
		Host.DiagnosticText.clear();
		if (!Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("enum/struct leaf-collision reload should construct struct field");
		}
		FValue X;
		const FRunResult GetX = Session.CallFunction("GetX", {}, &X);
		std::filesystem::remove_all(TempRoot, RemoveError);
		// MakeDefaultValue for structs honors Field.DefaultValue (here x=9).
		// Success of `.x` member access proves `c` is a struct, not int32 from enum leaf match.
		if (!GetX.bSuccess || !std::holds_alternative<std::int32_t>(X) || std::get<std::int32_t>(X) != 9)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("added private struct field colliding with enum leaf must default to struct");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "hotreload-init-rollback";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "globals.vs";
		std::ofstream(ScriptPath)
		    << "int X = 1;\n"
		    << "fn()->int F = fn()->int => 1;\n"
		    << "int ReadF() { return F(); }\n"
		    << "void main() {}\n";
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot-reload init rollback setup should succeed");
		}
		FValue FirstClosureResult;
		if (!Session.CallFunction("ReadF", {}, &FirstClosureResult).bSuccess || !std::holds_alternative<std::int32_t>(FirstClosureResult) || std::get<std::int32_t>(FirstClosureResult) != 1)
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("retained closure seed should execute");
		}

		std::ofstream(ScriptPath)
		    << "int X = 1;\n"
		    << "fn()->int F = fn()->int => 2;\n"
		    << "int ReadF() { return F(); }\n"
		    << "void main() {}\n";
		FValue MigratedClosureResult;
		if (!Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("ReadF", {}, &MigratedClosureResult).bSuccess || !std::holds_alternative<std::int32_t>(MigratedClosureResult) || std::get<std::int32_t>(MigratedClosureResult) != 2)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot reload should remap retained closure generation and body");
		}

		std::ofstream(ScriptPath)
		    << "int AbortInit() { exit(0); return 0; }\n"
		    << "int X = 1;\n"
		    << "fn()->int F = fn()->int => 3;\n"
		    << "int ReadF() { return F(); }\n"
		    << "int Y = AbortInit();\n"
		    << "void main() {}\n";
		if (!Session.Reload().bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot-reload replacement should stage before initialization");
		}
		Host.DiagnosticText.clear();
		const FRunResult Initialize = Session.Run(ERunMode::Eval);
		if (Initialize.bSuccess || Initialize.bExitRequested || Initialize.ExitCode != 3 || Initialize.Error.MessageId != "diag.hot_reload_initialization_failed")
		{
			std::cerr << Host.DiagnosticText << Initialize.Error.MessageId << '\n';
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("incomplete hot-reload initialization must fail and roll back");
		}
		FValue X;
		FValue Y;
		FValue RolledBackClosureResult;
		const FRunResult GetX = Session.GetGlobal("X", X);
		const FRunResult GetY = Session.GetGlobal("Y", Y);
		const FRunResult ReadF = Session.CallFunction(
		    "ReadF", {}, &RolledBackClosureResult);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!GetX.bSuccess || !std::holds_alternative<std::int32_t>(X) || std::get<std::int32_t>(X) != 1 || GetY.bSuccess || GetY.Error.MessageId != "diag.getglobal_unknown_global" || !ReadF.bSuccess || !std::holds_alternative<std::int32_t>(RolledBackClosureResult) || std::get<std::int32_t>(RolledBackClosureResult) != 2)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("failed hot-reload initialization must restore the prior module and globals");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "hotreload-ref-cell";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "ref_cell.vs";
		const auto WriteVersion = [&ScriptPath](const int Increment)
		{
			std::ofstream(ScriptPath)
			    << "int X = 1;\n"
			    << "function()->int F = function()->int => 0;\n"
			    << "int Invoke() { return F(); }\n"
			    << "void main() { int& alias = X; F = [&alias]() -> int { X += "
			    << Increment << "; return alias; }; }\n";
		};
		WriteVersion(1);
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run().bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("reference-cell hot-reload setup should succeed");
		}
		WriteVersion(2);
		FValue Result;
		FValue X;
		if (!Session.Reload().bSuccess || !Session.Run().bSuccess || !Session.CallFunction("Invoke", {}, &Result).bSuccess || !Session.GetGlobal("X", X).bSuccess || !std::holds_alternative<std::int32_t>(Result) || !std::holds_alternative<std::int32_t>(X) || std::get<std::int32_t>(Result) != 3 || std::get<std::int32_t>(X) != 3)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot reload must preserve and rebind reference-cell globals");
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	{
		// exit(n!=0) during hot-reload init must still report ExitCode 3 (not leak n).
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "hotreload-init-exit9";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "globals.vs";
		std::ofstream(ScriptPath)
		    << "int X = 1;\n"
		    << "void main() {}\n";
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot-reload exit(9) rollback setup should succeed");
		}
		std::ofstream(ScriptPath)
		    << "int AbortInit() { exit(9); return 0; }\n"
		    << "int X = 1;\n"
		    << "int Y = AbortInit();\n"
		    << "void main() {}\n";
		if (!Session.Reload().bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot-reload exit(9) replacement should stage");
		}
		Host.DiagnosticText.clear();
		const FRunResult Initialize = Session.Run(ERunMode::Eval);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (Initialize.bSuccess || Initialize.bExitRequested || Initialize.ExitCode != 3 || Initialize.Error.MessageId != "diag.hot_reload_initialization_failed")
		{
			std::cerr << Host.DiagnosticText << Initialize.Error.MessageId
			          << " exit=" << Initialize.ExitCode << '\n';
			return Fail("exit(9) during hot-reload init must fail with ExitCode 3");
		}
	}

	{
		// A genuine runtime error during hot-reload init keeps its own ExitCode and
		// diagnostic; rollback must not mask it as diag.hot_reload_initialization_failed.
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "hotreload-init-error";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "globals.vs";
		std::ofstream(ScriptPath)
		    << "int X = 1;\n"
		    << "void main() {}\n";
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot-reload init error setup should succeed");
		}
		std::ofstream(ScriptPath)
		    << "int X = 1;\n"
		    << "array<int> Empty = array<int>();\n"
		    << "int Y = Empty[5];\n"
		    << "void main() {}\n";
		if (!Session.Reload().bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("hot-reload init error replacement should stage");
		}
		Host.DiagnosticText.clear();
		const FRunResult Initialize = Session.Run(ERunMode::Eval);
		FValue X;
		const FRunResult GetX = Session.GetGlobal("X", X);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (Initialize.bSuccess || Initialize.bExitRequested || Initialize.ExitCode != 1 || Initialize.Error.MessageId != "diag.array_index_out_of_bounds")
		{
			std::cerr << Host.DiagnosticText << Initialize.Error.MessageId
			          << " exit=" << Initialize.ExitCode << '\n';
			return Fail("runtime error during hot-reload init must keep ExitCode 1 and its diagnostic");
		}
		if (!GetX.bSuccess || !std::holds_alternative<std::int32_t>(X) || std::get<std::int32_t>(X) != 1)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("runtime error during hot-reload init must roll back to prior module");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.RegisterNative(
		        "trigger_reentry",
		        ENativeType::Void,
		        {},
		        TriggerReentry,
		        &Session))
		{
			return Fail("RegisterNative trigger_reentry should succeed");
		}
		const FRunResult Load = Session.LoadSource(
		    "class Entity {}\n"
		    "void main() { trigger_reentry(); }\n",
		    "<session_reentry>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("reentry LoadSource should succeed");
		}
		Host.DiagnosticText.clear();
		const FRunResult Run = Session.Run();
		if (Run.bSuccess)
		{
			return Fail("reentry from Run into ConstructObject should fail the native");
		}
		const std::string ExpectedReentry = Locale::TranslateId(
		    "diag.action_not_allowed_while_run_in_progress", {"ConstructObject"});
		if (Host.DiagnosticText.find(ExpectedReentry) == std::string::npos)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("reentry should name ConstructObject in action_not_allowed diagnostic");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FCommitReentryContext Reentry;
		Reentry.Session = &Session;
		if (!Session.RegisterNative(
		        "trigger_commit_reentry",
		        ENativeType::Void,
		        {},
		        TriggerCommitReentry,
		        &Reentry))
		{
			return Fail("RegisterNative trigger_commit_reentry should succeed");
		}
		const FRunResult Load = Session.LoadSource(
		    "void main() { trigger_commit_reentry(); }\n",
		    "<session_commit_reentry>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("commit reentry LoadSource should succeed");
		}
		const FBytecodeModule* const OriginalModule = Session.GetLoadedModule();

		const std::filesystem::path TempRoot =
		    TemporaryRoot / "compile-commit";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "replacement.vs";
		std::ofstream(ScriptPath) << "void main() { print(\"replacement\"); }\n";

		const FRunResult Compile =
		    Session.CompileFile(ScriptPath.generic_string(), Reentry.Module);
		if (!Compile.bSuccess || !Reentry.Module || Session.GetLoadedModule() != OriginalModule)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("CompileFile should produce a module without replacing the loaded module");
		}

		Host.DiagnosticText.clear();
		const FRunResult Run = Session.Run();
		if (!Run.bSuccess || Reentry.Result.bSuccess || Reentry.Result.ExitCode != 2 || Reentry.Result.Error.MessageId != "diag.action_not_allowed_while_run_in_progress" || !Reentry.Module || Session.GetLoadedModule() != OriginalModule)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("CommitCompiledFile must be rejected without consuming or replacing modules");
		}
		if (Host.DiagnosticText.find("CommitCompiledFile") == std::string::npos)
		{
			std::cerr << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("commit reentry diagnostic should name CommitCompiledFile");
		}

		const FRunResult Commit = Session.CommitCompiledFile(
		    Reentry.Module,
		    ScriptPath.generic_string());
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Commit.bSuccess || Reentry.Module || Session.LoadedFilePath() != ScriptPath.generic_string())
		{
			std::cerr << Host.DiagnosticText;
			return Fail("CommitCompiledFile should take ownership after execution completes");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadSource("class Box {}\n", "<session_clear_nomodule>").bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("Clear suite LoadSource should succeed");
		}
		FObjectHandle Box;
		if (!Session.ConstructObject("Box", {}, Box).bSuccess || !Box)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ConstructObject Box before Clear should succeed");
		}
		Session.Clear();
		FObjectHandle Discard;
		const FRunResult AfterClear = Session.ConstructObject("Box", {}, Discard);
		if (AfterClear.bSuccess || AfterClear.Error.MessageId != "diag.no_module_loaded")
		{
			std::cerr << Host.DiagnosticText << AfterClear.Error.MessageId << '\n';
			return Fail("ConstructObject after Clear should be no_module_loaded");
		}
		FValue DiscardValue;
		const FRunResult CallAfterClear = Session.CallMethod(Box, "Nope", {}, &DiscardValue);
		if (CallAfterClear.bSuccess || CallAfterClear.Error.MessageId != "diag.no_module_loaded")
		{
			std::cerr << Host.DiagnosticText << CallAfterClear.Error.MessageId << '\n';
			return Fail("CallMethod after Clear should be no_module_loaded");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FHostTypeId ActorId;
		if (!Session.RegisterHostType("AActor", EHostTypeKind::Class, &ActorId) || !Session.SetHostTypeInheritable(ActorId, true) || !Session.RegisterHostTypeSubclassFactory(ActorId, ActorSubclassFactory))
		{
			return Fail("expired ScriptOwner registry should succeed");
		}
		if (!Session.LoadSource(
		                "class MyActor : AActor {\n"
		                "public:\n"
		                "\tMyActor() {}\n"
		                "}\n",
		                "<session_script_owner_expired>")
		        .bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("expired ScriptOwner LoadSource should succeed");
		}
		FHostObjectHandle StashedHost;
		{
			FObjectHandle Actor;
			if (!Session.ConstructObject("MyActor", {}, Actor).bSuccess || !Actor || !Actor->GetHostBase())
			{
				std::cerr << Host.DiagnosticText;
				return Fail("ConstructObject for expired ScriptOwner should succeed");
			}
			StashedHost = Actor->GetHostBase();
			if (!GetScriptOwner(StashedHost))
			{
				return Fail("ScriptOwner should be alive while Actor is held");
			}
		}
		if (GetScriptOwner(StashedHost))
		{
			return Fail("GetScriptOwner should be empty after script instance is released");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		const FRunResult Load = Session.LoadSource(
		    "namespace Alpha\n"
		    "{\n"
		    "\tclass Dup {}\n"
		    "}\n"
		    "namespace Beta\n"
		    "{\n"
		    "\tclass Dup {}\n"
		    "}\n"
		    "void main() { Alpha.Dup A = Alpha.Dup(); Beta.Dup B = Beta.Dup(); }\n",
		    "<session_ambiguous_type>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ambiguous type LoadSource should succeed");
		}
		std::vector<FLoadedTypeInfo> Types;
		Session.EnumerateTypes(Types);
		std::size_t DupLeaves = 0;
		for (const FLoadedTypeInfo& Type : Types)
		{
			if (Type.SimpleName == "Dup")
			{
				++DupLeaves;
			}
		}
		if (DupLeaves < 2)
		{
			for (const FLoadedTypeInfo& Type : Types)
			{
				std::cerr << "type=[" << Type.TypeName << "] simple=[" << Type.SimpleName << "]\n";
			}
			return Fail("expected two Dup leaves in Types");
		}
		FObjectHandle Discard;
		const FRunResult Ambiguous = Session.ConstructObject("Dup", {}, Discard);
		if (Ambiguous.bSuccess || Ambiguous.ExitCode != 4 || Ambiguous.Error.MessageId != "diag.constructobject_ambiguous_type_name")
		{
			std::cerr << Host.DiagnosticText << Ambiguous.Error.MessageId << '\n';
			return Fail("ConstructObject Dup should be ambiguous_type_name");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const FRunResult Load = Session.LoadSource(
		    "int Counter = 1;\n"
		    "int Add(int A, int B) { return A + B; }\n"
		    "class Box {\n"
		    "public:\n"
		    "\tint Value = 5;\n"
		    "}\n",
		    "<session_callfunction_fields>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("CallFunction suite LoadSource should succeed");
		}
		if (!Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("CallFunction suite Eval init should succeed");
		}
		if (!Session.HasFreeFunction("Add", 2))
		{
			return Fail("HasFreeFunction Add(2) should be true");
		}
		{
			FValue Sum;
			const FRunResult Call = Session.CallFunction(
			    "Add",
			    {static_cast<std::int32_t>(2), static_cast<std::int32_t>(3)},
			    &Sum);
			if (!Call.bSuccess || !std::holds_alternative<std::int32_t>(Sum) || std::get<std::int32_t>(Sum) != 5)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("CallFunction Add(2,3) should return 5");
			}
		}
		{
			FObjectHandle Box;
			if (!Session.ConstructObject("Box", {}, Box).bSuccess || !Box)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("ConstructObject Box should succeed");
			}
			FValue Value;
			if (!Session.GetField(Box, "Value", Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 5)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("GetField Value should be 5");
			}
			if (!Session.SetField(Box, "Value", static_cast<std::int32_t>(11)).bSuccess)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("SetField Value should succeed");
			}
			Value = {};
			if (!Session.GetField(Box, "Value", Value).bSuccess || !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 11)
			{
				return Fail("GetField after SetField should be 11");
			}
			{
				const FRunResult Missing = Session.SetField(
				    Box,
				    "Missing",
				    static_cast<std::int32_t>(1));
				if (Missing.bSuccess || Missing.ExitCode != 4 || Missing.Error.MessageId != "diag.setfield_unknown_field")
				{
					std::cerr << Host.DiagnosticText << Missing.Error.MessageId << '\n';
					return Fail("SetField unknown field should be ExitCode 4");
				}
			}
			{
				FValue Discard;
				const FRunResult MissingGet = Session.GetField(Box, "Missing", Discard);
				if (MissingGet.bSuccess || MissingGet.ExitCode != 4 || MissingGet.Error.MessageId != "diag.getfield_unknown_field")
				{
					std::cerr << Host.DiagnosticText << MissingGet.Error.MessageId << '\n';
					return Fail("GetField unknown field should be ExitCode 4");
				}
			}
		}
		{
			FValue Counter;
			if (!Session.GetGlobal("Counter", Counter).bSuccess || !std::holds_alternative<std::int32_t>(Counter) || std::get<std::int32_t>(Counter) != 1)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("GetGlobal Counter should be 1");
			}
			if (!Session.SetGlobal("Counter", static_cast<std::int32_t>(42)).bSuccess)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("SetGlobal Counter should succeed");
			}
			Counter = {};
			if (!Session.GetGlobal("Counter", Counter).bSuccess || !std::holds_alternative<std::int32_t>(Counter) || std::get<std::int32_t>(Counter) != 42)
			{
				return Fail("GetGlobal after SetGlobal should be 42");
			}
			FValue Discard;
			const FRunResult UnknownGlobal = Session.GetGlobal("NoSuchGlobal", Discard);
			if (UnknownGlobal.bSuccess || UnknownGlobal.ExitCode != 4 || UnknownGlobal.Error.MessageId != "diag.getglobal_unknown_global")
			{
				std::cerr << Host.DiagnosticText << UnknownGlobal.Error.MessageId << '\n';
				return Fail("GetGlobal unknown should be ExitCode 4");
			}
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadSource("int X = 1;\n", "<session_global_requires_hr>").bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("global-requires-hr setup should succeed");
		}
		FValue Discard;
		const FRunResult NoHr = Session.GetGlobal("X", Discard);
		if (NoHr.bSuccess || NoHr.ExitCode != 2 || NoHr.Error.MessageId != "diag.getglobal_requires_hot_reload")
		{
			std::cerr << Host.DiagnosticText << NoHr.Error.MessageId << '\n';
			return Fail("GetGlobal without hot reload should be ExitCode 2");
		}
		const FRunResult SetNoHr = Session.SetGlobal("X", static_cast<std::int32_t>(2));
		if (SetNoHr.bSuccess || SetNoHr.ExitCode != 2 || SetNoHr.Error.MessageId != "diag.setglobal_requires_hot_reload")
		{
			std::cerr << Host.DiagnosticText << SetNoHr.Error.MessageId << '\n';
			return Fail("SetGlobal without hot reload should be ExitCode 2");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Session.LoadSource("int X = 1;\n", "<session_global_requires_init>").bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("global-requires-init LoadSource should succeed");
		}
		FValue Discard;
		const FRunResult NoInit = Session.GetGlobal("X", Discard);
		if (NoInit.bSuccess || NoInit.ExitCode != 2 || NoInit.Error.MessageId != "diag.getglobal_requires_initialized_state")
		{
			std::cerr << Host.DiagnosticText << NoInit.Error.MessageId << '\n';
			return Fail("GetGlobal before init should be ExitCode 2");
		}
		const FRunResult SetNoInit = Session.SetGlobal("X", static_cast<std::int32_t>(2));
		if (SetNoInit.bSuccess || SetNoInit.ExitCode != 2 || SetNoInit.Error.MessageId != "diag.setglobal_requires_initialized_state")
		{
			std::cerr << Host.DiagnosticText << SetNoInit.Error.MessageId << '\n';
			return Fail("SetGlobal before init should be ExitCode 2");
		}
	}

	{
		// A failed reload can recycle both the candidate generation and module address.
		FBytecodeModule Storage;
		Storage.Functions.resize(2);
		Storage.FunctionIndices.emplace("Pick", 0);
		ModuleMeta::FFreeFunctionLookupCache Lookup;
		std::uint16_t Index = 0;
		auto Owner = std::shared_ptr<const FBytecodeModule>(std::make_shared<int>(0), &Storage);
		if (Lookup.Resolve(Owner, 2, "Pick", 0, Index, nullptr) != EMethodResolveStatus::Found ||
		    Index != 0)
		{
			return Fail("cached candidates resolve the first owner");
		}
		Owner.reset();
		Storage.FunctionIndices.at("Pick") = 1;
		Owner = std::shared_ptr<const FBytecodeModule>(std::make_shared<int>(0), &Storage);
		if (Lookup.Resolve(Owner, 2, "Pick", 0, Index, nullptr) != EMethodResolveStatus::Found ||
		    Index != 1)
		{
			return Fail("cached candidates reject a recycled generation and address under a different owner");
		}
	}

	{
		FCaptureHost Host;
		std::vector<FValue> ChangedArguments{std::int32_t{-4}};
		VeroScriptSession Session(Host);
		VeroScriptSession Replacement(Host);
		VeroScript::Test::Configure(Session);
		VeroScript::Test::Configure(Replacement);
		Session.SetHotReloadEnabled(true);
		const auto ChangeArgument = [](const FNativeCallContext& Context, FValue& OutReturn, std::string&) -> bool
		{
			*static_cast<FValue*>(Context.UserData) = std::int32_t{202};
			OutReturn = std::int32_t{0};
			return true;
		};
		if (!Session.RegisterNative("change_lookup_argument", ENativeType::Int, {},
		            ChangeArgument, ChangedArguments.data()) ||
		    !Replacement.RegisterNative("change_lookup_argument", ENativeType::Int, {},
		        ChangeArgument, ChangedArguments.data()) ||
		    !Session.LoadSource("int counter=0; int PickRange(int8 value){++counter;return 11;}"
		            "int PickRange(uint8 value){++counter;return 22;}", "<cached-lookup>").bSuccess ||
		    !Replacement.LoadSource("int counter=1000; int init=change_lookup_argument(); int shift(){return 0;}"
		            "int PickRange(uint8 value){++counter;return 44;}"
		            "int PickRange(int8 value){++counter;return 33;}", "<cached-lookup>").bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("cached free-function lookup loads both generations");
		}
		const auto CallChoice = [&](std::int32_t Argument, std::int32_t Expected)
		{
			FValue Result;
			return Session.CallFunction("PickRange", {Argument}, &Result).bSuccess &&
			    std::holds_alternative<std::int32_t>(Result) &&
			    std::get<std::int32_t>(Result) == Expected;
		};
		if (!CallChoice(-2, 11) || !CallChoice(200, 22) || !CallChoice(-3, 11) || !CallChoice(201, 22))
		{
			std::cerr << Host.DiagnosticText;
			return Fail("cached candidates must rerank the same input type for each numeric range");
		}
		auto NewModule = std::make_unique<FBytecodeModule>(*Replacement.GetLoadedModule());
		FValue Count;
		FValue AfterInitialization;
		// The initializer changes the live arguments between the two resolutions.
		if (!Session.ReloadCompiledModule(NewModule, "<cached-lookup>").bSuccess ||
		    !Session.CallFunction("PickRange", ChangedArguments, &AfterInitialization).bSuccess ||
		    !std::holds_alternative<std::int32_t>(AfterInitialization) ||
		    std::get<std::int32_t>(AfterInitialization) != 44 || !CallChoice(-2, 33) ||
		    !Session.GetGlobal("counter", Count).bSuccess ||
		    !std::holds_alternative<std::int32_t>(Count) || std::get<std::int32_t>(Count) != 6)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("cached candidates follow a reloaded generation without dropping retained globals");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		const FRunResult Load = Session.LoadSource(
		    "int Pick(int X) { return X; }\n"
		    "int Pick(string S) { return 1; }\n"
		    "int Amb(float X) { return 1; }\n"
		    "int Amb(double X) { return 2; }\n"
		    "class Dual {\n"
		    "public:\n"
		    "\tDual(float X) {}\n"
		    "\tDual(double X) {}\n"
		    "}\n"
		    "class Box { public: int Value = 0; }\n",
		    "<session_api_negative_suite>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("negative-suite LoadSource should succeed");
		}
		{
			FValue Ret;
			const FRunResult DoubleFree = Session.CallFunction("Amb", {1.5}, &Ret);
			if (!DoubleFree.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 2)
			{
				std::cerr << Host.DiagnosticText << DoubleFree.Error.MessageId << '\n';
				return Fail("CallFunction should select the exact double overload");
			}
		}
		{
			FObjectHandle Constructed;
			const FRunResult DoubleCtor = Session.ConstructObject("Dual", {1.5}, Constructed);
			if (!DoubleCtor.bSuccess || !Constructed)
			{
				std::cerr << Host.DiagnosticText << DoubleCtor.Error.MessageId << '\n';
				return Fail("ConstructObject should select the exact double constructor");
			}
		}
		{
			FValue Discard;
			const FRunResult NullObj = Session.GetField({}, "Value", Discard);
			if (NullObj.bSuccess || NullObj.ExitCode != 2 || NullObj.Error.MessageId != "diag.getfield_requires_object")
			{
				std::cerr << Host.DiagnosticText << NullObj.Error.MessageId << '\n';
				return Fail("GetField null object should be ExitCode 2");
			}
		}
		{
			FObjectHandle Box;
			if (!Session.ConstructObject("Box", {}, Box).bSuccess || !Box)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("negative-suite ConstructObject Box should succeed");
			}
			const FRunResult EmptyName = Session.SetField(Box, "", static_cast<std::int32_t>(1));
			if (EmptyName.bSuccess || EmptyName.ExitCode != 2 || EmptyName.Error.MessageId != "diag.setfield_requires_a_non_empty_name")
			{
				std::cerr << Host.DiagnosticText << EmptyName.Error.MessageId << '\n';
				return Fail("SetField empty name should be ExitCode 2");
			}
		}
		Session.Clear();
		{
			FValue Discard;
			const FRunResult AfterClear = Session.CallFunction("Pick", {static_cast<std::int32_t>(1)}, &Discard);
			if (AfterClear.bSuccess || AfterClear.Error.MessageId != "diag.no_module_loaded")
			{
				std::cerr << Host.DiagnosticText << AfterClear.Error.MessageId << '\n';
				return Fail("CallFunction after Clear should be no_module_loaded");
			}
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Session.LoadSource("int Counter = 1;\n", "<session_setglobal_unknown>").bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("setglobal-unknown setup should succeed");
		}
		const FRunResult Unknown = Session.SetGlobal(
		    "Missing",
		    static_cast<std::int32_t>(1));
		if (Unknown.bSuccess || Unknown.ExitCode != 4 || Unknown.Error.MessageId != "diag.setglobal_unknown_global")
		{
			std::cerr << Host.DiagnosticText << Unknown.Error.MessageId << '\n';
			return Fail("SetGlobal unknown should be ExitCode 4");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		const FRunResult Load = Session.LoadSource(
		    "class Music {}\n"
		    "class Player {\n"
		    "public:\n"
		    "\tint Use(Music M) { return 1; }\n"
		    "\tint Use(string S) { return 2; }\n"
		    "\tint Peek(float X) const { return 3; }\n"
		    "}\n",
		    "<session_param_decode_music>");
		if (!Load.bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("Music param decode LoadSource should succeed");
		}
		std::vector<FInstanceMethodInfo> Methods;
		if (!Session.EnumerateInstanceMethods("Player", Methods))
		{
			return Fail("EnumerateInstanceMethods Player should succeed");
		}
		bool bSawMusic = false;
		bool bSawPeek = false;
		for (const FInstanceMethodInfo& Method : Methods)
		{
			if (Method.Name == "Use" && Method.ParamCount == 1 && Method.ParamTypeNames.size() == 1 && Method.ParamTypeNames[0] == "Music")
			{
				bSawMusic = true;
			}
			if (Method.Name == "Peek" && Method.ParamCount == 1 && Method.bIsConst && Method.ParamTypeNames.size() == 1 && Method.ParamTypeNames[0] == "float")
			{
				bSawPeek = true;
			}
		}
		if (!bSawMusic)
		{
			return Fail("ParamTypeNames must keep trailing c in type Music");
		}
		if (!bSawPeek)
		{
			return Fail("const Peek(float) must still decode ParamTypeNames=[float]");
		}
		FObjectHandle Player;
		FObjectHandle Track;
		if (!Session.ConstructObject("Player", {}, Player).bSuccess || !Player || !Session.ConstructObject("Music", {}, Track).bSuccess || !Track)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("ConstructObject Player/Music should succeed");
		}
		FValue Ret;
		const FRunResult Call = Session.CallMethod(Player, "Use", {Track}, &Ret);
		if (!Call.bSuccess || !std::holds_alternative<std::int32_t>(Ret) || std::get<std::int32_t>(Ret) != 1)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("CallMethod Use(Music) should select Music overload");
		}
	}

	{
		// `ref` parameter write-back through the non-const CallFunction / CallMethod
		// overloads: scalars, whole-value struct reassignment, const-ref exclusion,
		// no write-back on failure, and JIT backend coverage (entry is forced to the
		// interpreter for ref-param targets; must not silently skip the write-back).
		const char* const RefSource =
		    "struct HitInfo {\n"
		    "public:\n"
		    "\tint Damage = 1;\n"
		    "\tint Bone = 2;\n"
		    "}\n"
		    "class Bomb {\n"
		    "public:\n"
		    "\tint F = 0;\n"
		    "}\n"
		    "void MutateInt(ref int X) { X = 7; }\n"
		    "void MutateAll(int A, ref int B, const ref int C) { B = A + C + B; }\n"
		    "void MutateHit(ref HitInfo Info) { Info.Damage = 99; }\n"
		    "void ReplaceHit(ref HitInfo Info) { Info = HitInfo(); }\n"
		    "void MutateString(ref string S) { S = S + \"!\"; }\n"
		    "void FailAfterMutate(ref int X) { X = 5; Bomb B = nullptr; print(B.F); }\n"
		    "void ExitAfterMutate(ref int X) { X = 5; exit(3); }\n"
		    "struct Trace {\n"
		    "\t~Trace() { print(\"dtor\"); }\n"
		    "}\n"
		    "void ReplaceTrace(ref Trace T) { T = Trace(); }\n"
		    "void MixTrace(Trace T, ref int X) { X = 9; }\n"
		    "class Body {\n"
		    "public:\n"
		    "\tint Total = 0;\n"
		    "\tvoid AddInto(ref int Acc) { Acc = Acc + 3; Total = Total + 1; }\n"
		    "}\n";
		for (const EExecutionBackend Backend :
		    {EExecutionBackend::Vm, EExecutionBackend::Mixed})
		{
			FCaptureHost Host;
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.SetExecutionBackend(Backend);
			if (!Session.LoadSource(RefSource, "<session_ref_writeback>").bSuccess)
			{
				std::cerr << Host.DiagnosticText;
				return Fail("ref write-back LoadSource should succeed");
			}

			{
				std::vector<FValue> Args{static_cast<std::int32_t>(1)};
				const FRunResult Call = Session.CallFunction("MutateInt", Args);
				if (!Call.bSuccess || !std::holds_alternative<std::int32_t>(Args[0]) || std::get<std::int32_t>(Args[0]) != 7)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("MutateInt(ref int) must write 7 back into Arguments[0]");
				}
			}

			{
				std::vector<FValue> Args{
				    static_cast<std::int32_t>(2),
				    static_cast<std::int32_t>(10),
				    static_cast<std::int32_t>(30)};
				const FRunResult Call = Session.CallFunction("MutateAll", Args);
				if (!Call.bSuccess || std::get<std::int32_t>(Args[0]) != 2 || std::get<std::int32_t>(Args[1]) != 42 || std::get<std::int32_t>(Args[2]) != 30)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("MutateAll must write back only the writable ref slot");
				}
			}

			{
				std::vector<FValue> Args{std::string("hi")};
				const FRunResult Call = Session.CallFunction("MutateString", Args);
				if (!Call.bSuccess || !std::holds_alternative<std::string>(Args[0]) || std::get<std::string>(Args[0]) != "hi!")
				{
					std::cerr << Host.DiagnosticText;
					return Fail("MutateString(ref string) must write back the final string");
				}
			}

			{
				FObjectHandle Hit;
				if (!Session.ConstructObject("HitInfo", {}, Hit).bSuccess || !Hit)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("ConstructObject HitInfo should succeed");
				}
				std::vector<FValue> Args{Hit};
				const FRunResult Call = Session.CallFunction("MutateHit", Args);
				if (!Call.bSuccess || !std::holds_alternative<FObjectHandle>(Args[0]))
				{
					std::cerr << Host.DiagnosticText;
					return Fail("MutateHit(ref HitInfo) should succeed and keep a handle");
				}
				FValue Damage;
				if (!Session.GetField(std::get<FObjectHandle>(Args[0]), "Damage", Damage)
				        .bSuccess ||
				    std::get<std::int32_t>(Damage) != 99)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("MutateHit must write the mutated struct back");
				}
				// Struct value semantics: the caller's original copy stays intact.
				FValue OriginalDamage;
				if (!Session.GetField(Hit, "Damage", OriginalDamage).bSuccess || std::get<std::int32_t>(OriginalDamage) != 1)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("MutateHit must not mutate the host's original struct copy");
				}
			}

			{
				FObjectHandle Hit;
				if (!Session.ConstructObject("HitInfo", {}, Hit).bSuccess || !Hit)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("ConstructObject HitInfo (replace) should succeed");
				}
				if (!Session.SetField(Hit, "Damage", static_cast<std::int32_t>(50)).bSuccess)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("SetField HitInfo.Damage=50 should succeed");
				}
				std::vector<FValue> Args{Hit};
				const FRunResult Call = Session.CallFunction("ReplaceHit", Args);
				FValue Damage;
				if (!Call.bSuccess || !std::holds_alternative<FObjectHandle>(Args[0]) || !Session.GetField(std::get<FObjectHandle>(Args[0]), "Damage", Damage).bSuccess || std::get<std::int32_t>(Damage) != 1)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("ReplaceHit whole-value reassignment must be written back");
				}
			}

			{
				std::vector<FValue> Args{static_cast<std::int32_t>(1)};
				Host.DiagnosticText.clear();
				const FRunResult Call = Session.CallFunction("FailAfterMutate", Args);
				if (Call.bSuccess || !std::holds_alternative<std::int32_t>(Args[0]) || std::get<std::int32_t>(Args[0]) != 1)
				{
					return Fail("failed calls must not write back ref arguments");
				}
			}

			{
				std::vector<FValue> Args{static_cast<std::int32_t>(1)};
				Host.DiagnosticText.clear();
				const FRunResult Call = Session.CallFunction("ExitAfterMutate", Args);
				if (!Call.bSuccess || !Call.bExitRequested || Call.ExitCode != 3 || !std::holds_alternative<std::int32_t>(Args[0]) || std::get<std::int32_t>(Args[0]) != 1)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("exit() must not write back ref arguments");
				}
			}

			{
				// Whole-value ref replace must run the previous host value's destructor
				// via AssignValue (raw FValue move-assign would skip script dtors).
				FObjectHandle TraceObj;
				if (!Session.ConstructObject("Trace", {}, TraceObj).bSuccess || !TraceObj)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("ConstructObject Trace should succeed");
				}
				Host.PrintText.clear();
				std::vector<FValue> Args{TraceObj};
				const FRunResult Call = Session.CallFunction("ReplaceTrace", Args);
				const bool bHasDtorPrint = Host.PrintText.find("dtor") != std::string::npos;
				if (!Call.bSuccess || !std::holds_alternative<FObjectHandle>(Args[0]) || !bHasDtorPrint)
				{
					std::string Escaped = Host.PrintText;
					for (char& Ch : Escaped)
					{
						if (Ch == '\n')
						{
							Ch = '|';
						}
					}
					std::cerr << "diag=[" << Host.DiagnosticText << "] print=[" << Escaped
					          << "] success=" << Call.bSuccess << " code=" << Call.ExitCode << '\n';
					return Fail("ReplaceTrace must write back and run prior Trace destructor");
				}
			}

			{
				// By-value struct beside a writable ref must still run ~Trace when the
				// callee returns (AliasArgs must not keep a second owner).
				FObjectHandle TraceObj;
				if (!Session.ConstructObject("Trace", {}, TraceObj).bSuccess || !TraceObj)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("ConstructObject Trace (MixTrace) should succeed");
				}
				Host.PrintText.clear();
				std::vector<FValue> Args{TraceObj, static_cast<std::int32_t>(1)};
				const FRunResult Call = Session.CallFunction("MixTrace", Args);
				if (!Call.bSuccess || std::get<std::int32_t>(Args[1]) != 9 || Host.PrintText.find("dtor") == std::string::npos)
				{
					std::string Escaped = Host.PrintText;
					for (char& Ch : Escaped)
					{
						if (Ch == '\n')
						{
							Ch = '|';
						}
					}
					std::cerr << "diag=[" << Host.DiagnosticText << "] print=[" << Escaped << "]\n";
					return Fail("MixTrace must write back ref int and destroy by-value Trace");
				}
			}

			{
				FObjectHandle Body;
				if (!Session.ConstructObject("Body", {}, Body).bSuccess || !Body)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("ConstructObject Body should succeed");
				}
				std::vector<FValue> Args{static_cast<std::int32_t>(4)};
				const FRunResult Call = Session.CallMethod(Body, "AddInto", Args);
				FValue Total;
				if (!Call.bSuccess || std::get<std::int32_t>(Args[0]) != 7 || !Session.GetField(Body, "Total", Total).bSuccess || std::get<std::int32_t>(Total) != 1)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("CallMethod AddInto(ref int) must write back and run on this");
				}
			}

			{
				const std::vector<FValue> ConstArgs{static_cast<std::int32_t>(1)};
				const FRunResult Call = Session.CallFunction("MutateInt", ConstArgs);
				if (!Call.bSuccess || std::get<std::int32_t>(ConstArgs[0]) != 1)
				{
					std::cerr << Host.DiagnosticText;
					return Fail("const CallFunction overload must not write back");
				}
			}
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FNativeParamDesc Param;
		Param.Type = FNativeTypeDesc::ArrayOf(FNativeTypeDesc::ArrayOf(
		    FNativeTypeDesc::Scalar(ENativeType::Int)));
		Param.Flags = NativeParamFlagRef | NativeParamFlagConstRef;
		if (!Session.RegisterNative(
		        "structured_echo",
		        FNativeTypeDesc::ArrayOf(FNativeTypeDesc::ArrayOf(
		            FNativeTypeDesc::Scalar(ENativeType::Int))),
		        {Param},
		        [](const FNativeCallContext& Context, FValue& OutReturn, std::string&) -> bool
		{
			if (Context.ArgCount != 1)
			{
				return false;
			}
			OutReturn = Context.Args[0];
			return true;
		}))
		{
			std::cerr << Host.DiagnosticText;
			return Fail("structured RegisterNative should accept array metadata");
		}
		const auto& Natives = Session.GetBindingCatalog().GetNativeFunctions();
		if (Natives.size() != 1 || !Natives.front().bHasStructuredTypeDescs || Natives.front().StructuredParamTypes.size() != 1 || Natives.front().StructuredParamTypes[0].TypeArguments.size() != 1 || Natives.front().StructuredParamTypes[0].TypeArguments[0].Type != ENativeType::Array)
		{
			return Fail("Session must preserve recursive structured native metadata");
		}
		if (!Session.LoadSource(
		                "void main() {\n"
		                "  array<array<int>> xs = {{4, 5}, {6}};\n"
		                "  print(len(structured_echo(xs)));\n"
		                "}\n",
		                "<structured_native>")
		        .bSuccess ||
		    !Session.Run().bSuccess || Host.PrintText != "2\n")
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			return Fail("structured RegisterNative should compile and execute");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FHostTypeId EnumId;
		if (!Session.RegisterHostType("SessionColor", EHostTypeKind::Enum, &EnumId) || !Session.SetHostEnumScoped(EnumId))
		{
			std::cerr << Host.DiagnosticText;
			return Fail("Session should expose scoped host enum configuration");
		}
		const FHostTypeDesc* const Type = Session.GetBindingCatalog().FindType(EnumId);
		if (!Type || !Type->bScopedEnum)
		{
			return Fail("Session scoped enum setting should reach binding registry");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.SourceOverlays = {
		    {"virtual/session_overlay.vs",
		        "export int session_overlay_value() { return 42; }\n"}};
		Session.ApplyConfig(Config);
		if (!Session.LoadSource(
		                "import \"session_overlay.vs\";\n"
		                "void main() { print(session_overlay_value()); }\n",
		                "virtual/main.vs")
		        .bSuccess ||
		    !Session.Run().bSuccess || Host.PrintText != "42\n")
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			return Fail("Session ApplyConfig source overlays should apply to LoadSource imports");
		}
	}

	{
		const std::filesystem::path TempRoot =
		    TemporaryRoot / "overlay-reload";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "main.vs";
		const std::filesystem::path OverlayPath = TempRoot / "overlay.vs";
		{
			std::ofstream Script(ScriptPath, std::ios::binary);
			Script << "import \"overlay.vs\";\n"
			       << "void main() { print(session_overlay_value()); }\n";
		}
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig FirstConfig;
		FirstConfig.SourceOverlays = {
		    {OverlayPath.generic_string(),
		        "export int session_overlay_value() { return 1; }\n"}};
		Session.ApplyConfig(FirstConfig);
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run().bSuccess)
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("Session source overlay should apply to LoadFile imports");
		}
		FEngineConfig SecondConfig;
		SecondConfig.SourceOverlays = {
		    {OverlayPath.generic_string(),
		        "export int session_overlay_value() { return 2; }\n"}};
		Session.ApplyConfig(SecondConfig);
		if (!Session.Reload().bSuccess || !Session.Run().bSuccess || Host.PrintText != "1\n2\n")
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return Fail("Session source overlay updates should apply to Reload imports");
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.PredefinedMacros.push_back(
		    {"HOST_SUM", {"left", "right"}, false, "left + right", "session-host"});
		Session.ApplyConfig(Config);
		if (!Session.LoadSource(
		                "void main() { print(HOST_SUM(20, 22)); }\n",
		                "<predefined_macro_session>")
		        .bSuccess ||
		    !Session.Run().bSuccess || Host.PrintText != "42\n")
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			return Fail("FEngineConfig predefined macros should apply to LoadSource");
		}
	}

	{
		const std::filesystem::path ScriptPath =
		    TemporaryRoot / "predefined-macro-reload.vs";
		{
			std::ofstream Script(ScriptPath, std::ios::binary);
			Script << "void main() { print(HOST_VALUE()); }\n";
		}
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		FEngineConfig FirstConfig;
		FirstConfig.PredefinedMacros = {
		    {"HOST_VALUE", {}, false, "1", "reload-host"}};
		Session.ApplyConfig(FirstConfig);
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run().bSuccess)
		{
			std::filesystem::remove(ScriptPath);
			return Fail("predefined macro hot-reload seed should load");
		}
		FEngineConfig SecondConfig;
		SecondConfig.PredefinedMacros = {
		    {"HOST_VALUE", {}, false, "2", "reload-host"}};
		Session.ApplyConfig(SecondConfig);
		if (!Session.Reload().bSuccess || !Session.Run().bSuccess || Host.PrintText != "1\n2\n")
		{
			std::cerr << Host.DiagnosticText << Host.PrintText;
			std::filesystem::remove(ScriptPath);
			return Fail("hot reload should compile with the current predefined macro environment");
		}
		std::filesystem::remove(ScriptPath);
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.ExecutionBackend = EExecutionBackend::Mixed;
		Config.ExecutionLimits.MaxInstructions = 64;
		Session.ApplyConfig(Config);
		if (!Session.LoadSource("void main() { while (true) {} }\n", "<instruction_limit>").bSuccess)
		{
			return Fail("execution instruction limit test source should compile");
		}
		const FRunResult RunResult = Session.Run();
		if (RunResult.bSuccess || RunResult.Error.MessageId != "diag.runtime_execution_instruction_limit_exceeded" || Host.DiagnosticText.empty())
		{
			std::cerr << Host.DiagnosticText;
			return Fail("execution instruction limits should stop JIT-requested infinite loops");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		auto Control = std::make_shared<FExecutionControl>();
		Control->RequestCancel();
		FEngineConfig Config;
		Config.ExecutionControl = Control;
		Session.ApplyConfig(Config);
		if (!Session.LoadSource("void main() { print(1); }\n", "<cancelled>").bSuccess)
		{
			return Fail("pre-cancelled execution test source should compile");
		}
		const FRunResult RunResult = Session.Run();
		if (RunResult.bSuccess || RunResult.Error.MessageId != "diag.execution_cancelled_by_host" || !Host.PrintText.empty())
		{
			std::cerr << Host.DiagnosticText;
			return Fail("pre-cancelled execution control should stop the VM before script effects");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		auto Control = std::make_shared<FExecutionControl>();
		FEngineConfig Config;
		Config.ExecutionBackend = EExecutionBackend::Mixed;
		Config.ExecutionControl = Control;
		Session.ApplyConfig(Config);
		if (!Session.LoadSource("void main() { while (true) {} }\n", "<async_cancel>").bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("async cancellation test source should compile");
		}
		std::thread Canceller([Control]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			Control->RequestCancel();
		});
		const FRunResult RunResult = Session.Run();
		Canceller.join();
		if (RunResult.bSuccess || RunResult.Error.MessageId != "diag.execution_cancelled_by_host")
		{
			std::cerr << Host.DiagnosticText;
			return Fail("execution control should be callable from a host thread during Run");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.ExecutionBackend = EExecutionBackend::Mixed;
		Config.Capabilities.bAllowConsoleOutput = false;
		Session.ApplyConfig(Config);
		if (!Session.LoadSource("void main() { print(1); }\n", "<console_capability>").bSuccess || Session.Run().bSuccess || Host.DiagnosticText.find("console output capability") == std::string::npos)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("capability policy should deny console builtins without executing them");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.ExecutionBackend = EExecutionBackend::Mixed;
		Config.Capabilities.bAllowProcessDirectoryChange = false;
		Session.ApplyConfig(Config);
		if (!Session.LoadSource(
		                "import \"io.vs\"; void main() { std::filesystem::chdir(\".\"); }\n",
		                "<chdir_capability>")
		        .bSuccess ||
		    Session.Run().bSuccess || Host.DiagnosticText.find("process directory change capability") == std::string::npos)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("process directory capability should deny chdir independently");
		}
	}

	for (const char* Query : {"is_file", "is_directory"})
	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.ExecutionBackend = EExecutionBackend::Mixed;
		Config.Capabilities.bAllowFileRead = false;
		Session.ApplyConfig(Config);
		const std::string Source = std::string("import \"io.vs\"; void main() { std::filesystem::") + Query + "(\".\"); }\n";
		if (!Session.LoadSource(Source, "<filesystem_read_capability>").bSuccess || Session.Run().bSuccess || Host.DiagnosticText.find("file read capability") == std::string::npos)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("filesystem type queries must require file read capability");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.ExecutionBackend = EExecutionBackend::Mixed;
		Config.Capabilities.bAllowFileDelete = false;
		Session.ApplyConfig(Config);
		if (!Session.LoadSource(
		                "import \"io.vs\"; void main() { std::filesystem::remove_dir(\".\"); }\n",
		                "<filesystem_delete_capability>")
		        .bSuccess ||
		    Session.Run().bSuccess || Host.DiagnosticText.find("file delete capability") == std::string::npos)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("remove_dir must require file delete capability");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		// A host can use the full uint64 range to express "effectively no
		// deadline".  It must not wrap through the native steady_clock duration
		// and expire before the script begins.
		Config.ExecutionLimits.TimeoutMilliseconds =
		    std::numeric_limits<std::uint64_t>::max();
		Session.ApplyConfig(Config);
		if (!Session.LoadSource("void main() { print(1); }\n", "<saturated_timeout>").bSuccess || !Session.Run().bSuccess || Host.PrintText != "1\n")
		{
			std::cerr << Host.DiagnosticText;
			return Fail("an unrepresentable timeout should saturate instead of overflowing");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.ExecutionLimits.TimeoutMilliseconds = 25;
		Session.ApplyConfig(Config);
		if (!Session.LoadSource(
		                "import \"concurrent.vs\";\n"
		                "void main() { channel<int> ch = channel<int>(); std::concurrent::channel_recv(ch); }\n",
		                "<channel_execution_timeout>")
		        .bSuccess)
		{
			return Fail("channel execution timeout test source should compile");
		}
		const FRunResult RunResult = Session.Run();
		if (RunResult.bSuccess || RunResult.Error.MessageId != "diag.execution_time_limit_exceeded")
		{
			std::cerr << Host.DiagnosticText;
			return Fail("execution limits should interrupt blocking channel receives");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path ScriptPath =
		    TemporaryRoot / "reflection-reload.vs";
		{
			std::ofstream(ScriptPath)
			    << "class Counter { public: int value = 1; }\n"
			    << "Counter counter = Counter();\n"
			    << "field_info<Counter> selected = field_info<Counter>(type<Counter>().qualified_name, 0);\n"
			    << "int change(int next) { selected.set<int>(counter, next); return selected.get<int>(counter); }\n"
			    << "int inspect(field_info<Counter> item) { return item.get<int>(counter); }\n"
			    << "void main() {}\n";
		}
		FValue Result;
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("reflection Session should initialize");
		}
		int ReflectionWrites = 0;
		Session.SetMutationCallback([&](const FMutationEvent& Event)
		{
			if (Event.FieldName == "value")
			{
				++ReflectionWrites;
			}
		});
		if (!Session.CallFunction("change", {std::int32_t{7}}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 7 || ReflectionWrites != 1)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("reflected write should mutate once across Session VM calls");
		}
		{
			std::ofstream(ScriptPath)
			    << "class Counter { public: int bonus = 3; int value = 1; }\n"
			    << "Counter counter = Counter();\n"
			    << "field_info<Counter> selected = field_info<Counter>(type<Counter>().qualified_name, 1);\n"
			    << "int change(int next) { selected.set<int>(counter, next); return selected.get<int>(counter); }\n"
			    << "int inspect(field_info<Counter> item) { return item.get<int>(counter); }\n"
			    << "int inspect_object(Counter item) { return selected.get<int>(item); }\n"
			    << "void main() {}\n";
		}
		Result = {};
		if (!Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("compatible reflection hot reload should initialize");
		}
		ReflectionWrites = 0;
		if (!Session.CallFunction("change", {std::int32_t{9}}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 9 || ReflectionWrites != 1)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("reflected descriptor should follow compatible hot reload field reorder");
		}
		FValue ForeignDescriptor;
		FValue ForeignObject;
		if (!Session.GetGlobal("selected", ForeignDescriptor).bSuccess)
		{
			return Fail("hot-reloaded descriptor should be available to host");
		}
		if (!Session.GetGlobal("counter", ForeignObject).bSuccess || !Session.CallFunction("inspect_object", {ForeignObject}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 9)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("same-Session object should remain valid for reflected reads");
		}
		FCaptureHost OtherHost;
		VeroScriptSession OtherSession(OtherHost);
		VeroScript::Test::Configure(OtherSession);
		OtherSession.SetHotReloadEnabled(true);
		if (!OtherSession.LoadFile(ScriptPath.generic_string()).bSuccess || !OtherSession.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << OtherHost.DiagnosticText;
			return Fail("second reflection Session should initialize");
		}
		const FRunResult ForeignCall = OtherSession.CallFunction(
		    "inspect", {ForeignDescriptor}, &Result);
		if (ForeignCall.bSuccess || ForeignCall.Error.Message.find("foreign reflection descriptor") == std::string::npos)
		{
			std::cerr << OtherHost.DiagnosticText;
			return Fail("reflection must reject descriptors from another Session");
		}
		if (OtherSession.CallFunction("inspect_object", {ForeignObject}, &Result).bSuccess)
		{
			return Fail("reflection must reject objects from another Session");
		}
		{
			std::ofstream(ScriptPath)
			    << "class Counter { public: int bonus = 3; private: int value = 1; }\n"
			    << "Counter counter = Counter();\n"
			    << "field_info<Counter> selected = field_info<Counter>(type<Counter>().qualified_name, 1);\n"
			    << "int change(int next) { selected.set<int>(counter, next); return selected.get<int>(counter); }\n"
			    << "int inspect(field_info<Counter> item) { return item.get<int>(counter); }\n"
			    << "void main() {}\n";
		}
		if (!Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("reflection access-change hot reload should initialize");
		}
		const FRunResult InaccessibleCall = Session.CallFunction(
		    "change", {std::int32_t{11}}, &Result);
		if (InaccessibleCall.bSuccess)
		{
			return Fail("reflected descriptor must reject a member made private by hot reload");
		}
	}

	{
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const std::filesystem::path ScriptPath =
		    TemporaryRoot / "reflection-declaration-move.vs";
		{
			std::ofstream(ScriptPath)
			    << "class Base { public: int value = 1; }\n"
			    << "class Derived : public Base {}\n"
			    << "Derived target = Derived();\n"
			    << "field_info<Derived> selected = field_info<Derived>(type<Derived>().qualified_name, 0);\n"
			    << "int inspect() { return selected.get<int>(target); }\n"
			    << "void main() {}\n";
		}
		FValue Result;
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("inspect", {}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 1)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("inherited reflection descriptor should initialize");
		}
		{
			std::ofstream(ScriptPath)
			    << "class Base {}\n"
			    << "class Derived : public Base { public: int value = 2; }\n"
			    << "Derived target = Derived();\n"
			    << "field_info<Derived> selected = field_info<Derived>(type<Derived>().qualified_name, 0);\n"
			    << "int inspect() { return selected.get<int>(target); }\n"
			    << "void main() {}\n";
		}
		if (!Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("declaration-move reflection hot reload should initialize");
		}
		if (Session.CallFunction("inspect", {}, &Result).bSuccess)
		{
			return Fail("reflected descriptor must not rebind to a different declaration");
		}
	}

	{
		const std::filesystem::path ScriptPath =
		    TemporaryRoot / "callable-reload.vs";
		const auto WriteSource = [&](const int Multiplier)
		{
			std::ofstream(ScriptPath)
			    << "import \"concurrent.vs\";\n"
			    << "int multiply(int value) { return value * "
			    << Multiplier << "; }\n"
			    << "fn(int)->int saved = multiply;\n"
			    << "int execute(fn(int)->int work) {\n"
			    << "    task<int> result = std::concurrent::spawn_task<int>(work, 21);\n"
			    << "    return result.await();\n"
			    << "}\n"
			    << "void main() {}\n";
		};
		WriteSource(2);
		FCaptureHost Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		FValue Saved;
		FValue Result;
		if (!Session.LoadFile(ScriptPath.generic_string()).bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.GetGlobal("saved", Saved).bSuccess || !Session.CallFunction("execute", {Saved}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 42)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("saved callable should spawn across Session VM calls");
		}
		FCaptureHost OtherHost;
		VeroScriptSession OtherSession(OtherHost);
		VeroScript::Test::Configure(OtherSession);
		OtherSession.SetHotReloadEnabled(true);
		if (!OtherSession.LoadFile(ScriptPath.generic_string()).bSuccess || !OtherSession.Run(ERunMode::Eval).bSuccess)
		{
			std::cerr << OtherHost.DiagnosticText;
			return Fail("second callable Session should initialize");
		}
		const FRunResult ForeignCall = OtherSession.CallFunction(
		    "execute", {Saved}, &Result);
		if (ForeignCall.bSuccess || ForeignCall.Error.Message.find("foreign") == std::string::npos)
		{
			std::cerr << OtherHost.DiagnosticText;
			return Fail("callable spawn must reject a foreign Session function value");
		}
		WriteSource(3);
		Result = {};
		if (!Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("execute", {Saved}, &Result).bSuccess || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 63)
		{
			std::cerr << Host.DiagnosticText;
			return Fail("saved callable should remap to a compatible hot reload");
		}
	}

	return EXIT_SUCCESS;
}
