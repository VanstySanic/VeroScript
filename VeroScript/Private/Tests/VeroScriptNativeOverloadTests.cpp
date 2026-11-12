// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptVm.h"
#include "../ExecutionSupport/VeroScriptNativeBindingCache.h"

#include <iostream>
#include <sstream>
#include <filesystem>

using namespace VeroScript;
namespace
{
	const char* Stage = "registration";
	bool Tag(const FNativeCallContext& C, FValue& Out, std::string&)
	{
		Out = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(C.UserData));
		return true;
	}
	bool Echo(const FNativeCallContext& C, FValue& Out, std::string&)
	{
		Out = C.Args[C.ArgCount - 1];
		return true;
	}
	bool Constructor(const FNativeCallContext&, FValue& Out, std::string&)
	{
		Out = MakeHostObject(1, "OverloadHost", new int(0), [](void* P)
		{
			delete static_cast<int*>(P);
		});
		return true;
	}
	bool Increment(const FNativeCallContext& C, FValue& Out, std::string& Error)
	{
		auto* Arg = C.MutableArg(C.ArgCount - 1);
		if (!Arg)
		{
			Error = "missing mutable argument";
			return false;
		}
		if (auto* I = std::get_if<std::int32_t>(Arg))
		{
			++*I;
			Out = *I;
		}
		else if (auto* S = std::get_if<std::string>(Arg))
		{
			*S += "!";
			Out = std::int32_t{2};
		}
		else
		{
			Error = "wrong ref overload";
			return false;
		}
		return true;
	}
	bool Run(const FHostBindingRegistry& Registry, const std::string& Source, const std::string& Backend)
	{
		Stage = "compile execution source";
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.Bindings = &Registry;
		const std::string StdlibRoot = (std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Resources/Scripts/stdlib").generic_string();
		O.StdlibSearchPaths = {StdlibRoot};
		auto R = VeroScriptCompile::CompileSource(Source, "host-overloads.vs", O);
		if (!R.bSuccess)
		{
			std::cerr << R.Error.Message << " at " << R.Error.Location.Line << ':' << R.Error.Location.Column << '\n';
			return false;
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		Stage = "serialize";
		if (!VeroScriptBytecodeIO::SerializeBytecode(*R.Module, 42, Bytes, Error)) return false;
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Mode == EExecutionBackend::Jit && Backend.empty())
			{
				continue;
			}
			Stage = "deserialize";
			auto Module = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 42, Error);
			if (!Module)
			{
				std::cerr << Error;
				return false;
			}
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Mode);
			Vm.SetMemoryHostBindings(&Registry);
			if (!Backend.empty() && !Vm.SetJitBackendPath(Backend, Error))
			{
				std::cerr << Error;
				return false;
			}
			Stage = "execute";
			if (!Vm.Execute(*Module, ERunMode::Script, {}, &Registry.GetNativeFunctions()))
			{
				std::cerr << "backend " << static_cast<int>(Mode) << ": " << Vm.GetError().Message << '\n';
				return false;
			}
			Stage = "execution teardown";
		}
		return true;
	}
	bool CheckBindingCache(const std::string& Backend)
	{
		FHostBindingRegistry Registry;
		std::string Error;
		if (!Registry.RegisterNative("cached_value", ENativeType::Int, {ENativeType::Int}, Tag,
				reinterpret_cast<void *>(std::intptr_t{11}), Error) ||
			!Registry.RegisterNative("other_cached_value", ENativeType::Int, {ENativeType::Int}, Tag,
				reinterpret_cast<void *>(std::intptr_t{41}), Error))
		{
			return false;
		}
		const auto Compile = [&](const char* Source)
		{
			FCompileOptions Options = Test::MakeCompileOptions();
			Options.Bindings = &Registry;
			auto Result = VeroScriptCompile::CompileSource(Source, "native-binding-cache.vs", Options);
			if (!Result.bSuccess)
			{
				throw std::runtime_error(Result.Error.Message);
			}
			return std::shared_ptr<FBytecodeModule>(std::move(Result.Module));
		};
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Jit})
		{
			if (Mode == EExecutionBackend::Jit && Backend.empty())
			{
				continue;
			}
			for (const bool Owned : {false, true})
			{
				auto Module = Compile("int work(){return cached_value(7);}");
				auto Natives = Registry.GetNativeFunctions();
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				Test::Configure(Vm);
				Vm.SetExecutionBackend(Mode);
				if (Mode == EExecutionBackend::Jit && !Vm.SetJitBackendPath(Backend, Error)) return false;
				if (Owned && !Vm.SetJitProgramOwner(Module, Error)) return false;
				const auto Invoke = [&](std::int32_t Expected)
				{
					FValue Value;
					return Vm.ExecuteFunction(*Module, 0, {}, &Natives, &Value) &&
					    std::holds_alternative<std::int32_t>(Value) && std::get<std::int32_t>(Value) == Expected;
				};
				Stage = "native cache initial reuse";
				if (!Invoke(11) || !Invoke(11)) return false;
				Stage = "native cache callback replacement";
				Natives[0].Callback = Echo;
				if (!Invoke(7)) return false;
				Natives[0].Callback = Tag;
				Stage = "native cache userdata replacement";
				Natives[0].UserData = reinterpret_cast<void*>(std::intptr_t{22});
				if (!Invoke(22)) return false;
				const auto Valid = Natives[0];
				Stage = "native cache parameter replacement";
				Natives[0].ParamTypes = {ENativeType::Double};
				if (Invoke(22)) return false;
				Natives[0] = Valid;
				if (!Invoke(22)) return false;
				Natives[0].ParamFlags = {1};
				Stage = "native cache ref flag replacement";
				if (Invoke(22)) return false;
				Natives[0] = Valid;
				if (!Invoke(22)) return false;
				Natives[0].Origin.ProviderId = "different-provider";
				Stage = "native cache provider replacement";
				if (Invoke(22)) return false;
				Natives[0] = Valid;
				if (!Invoke(22)) return false;
				auto Owner = std::make_shared<int>(1);
				Stage = "native cache callback owner retirement";
				const std::weak_ptr<int> RetiredOwner = Owner;
				Natives[0].CallbackOwner = Owner;
				if (!Invoke(22)) return false;
				Owner.reset();
				Natives[0].CallbackOwner = std::make_shared<int>(2);
				if (!Invoke(22) || !RetiredOwner.expired()) return false;
				static int CallbackAddress = 0;
				Stage = "native cache aliased callback owner retirement";
				auto CallbackLease = std::shared_ptr<void>(&CallbackAddress, [](void*) {});
				const std::weak_ptr<void> RetiredCallbackLease = CallbackLease;
				Natives[0].CallbackOwner = CallbackLease;
				if (!Invoke(22)) return false;
				CallbackLease.reset();
				// Equal callback/userdata and equal get(), but a distinct owning lease.
				Natives[0].CallbackOwner = std::shared_ptr<void>(&CallbackAddress, [](void*) {});
				if (!Invoke(22) || !RetiredCallbackLease.expired()) return false;
				static int ProviderAddress = 0;
				Stage = "native cache aliased provider retirement";
				auto Provider = std::shared_ptr<const void>(&ProviderAddress, [](const void*) {});
				const std::weak_ptr<const void> RetiredProvider = Provider;
				Natives[0].ProviderIdentity = Provider;
				if (!Invoke(22)) return false;
				Provider.reset();
				// Same provider address, distinct ownership identity.
				Natives[0].ProviderIdentity = std::shared_ptr<const void>(&ProviderAddress, [](const void*) {});
				if (!Invoke(22) || !RetiredProvider.expired()) return false;
				static int ProgramAddress = 0;
				Stage = "native cache aliased program owner retirement";
				auto ProgramLease = std::shared_ptr<const void>(&ProgramAddress, [](const void*) {});
				const std::weak_ptr<const void> RetiredProgram = ProgramLease;
				Vm.SetProgramOwner(ProgramLease);
				if (!Invoke(22)) return false;
				ProgramLease.reset();
				Vm.SetProgramOwner(std::shared_ptr<const void>(&ProgramAddress, [](const void*) {}));
				if (!Invoke(22) || !RetiredProgram.expired()) return false;
				auto Replacement = Compile("int work(){return other_cached_value(7);}");
				Stage = "native cache module reference replacement";
				if (Owned)
				{
					if (!Vm.SetJitProgramOwner(Replacement, Error)) return false;
					Module = std::move(Replacement);
				}
				else
				{
					*Module = std::move(*Replacement);
				}
				if (!Invoke(41)) return false;
			}
		}
		{
			Stage = "native cache oversized metadata execution and retirement";
			auto Bare = VeroScriptCompile::CompileSource("int work(){return 42;}", "native-binding-cache-budget.vs", Test::MakeCompileOptions());
			if (!Bare.bSuccess) return false;
			auto Module = std::move(Bare.Module);
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			Test::Configure(Vm);
			Vm.SetExecutionBackend(EExecutionBackend::Vm);
			FValue Value;
			std::weak_ptr<int> RetiredOwner;
			{
				auto Natives = Registry.GetNativeFunctions();
				auto Owner = std::make_shared<int>(1);
				RetiredOwner = Owner;
				Natives[0].CallbackOwner = Owner;
				// Oversized optional cache metadata must not reject normal execution.
				Natives[0].Name.assign(NativeBindingCache::MaxRetainedBytes + 1, 'x');
				if (!Vm.ExecuteFunction(*Module, 0, {}, &Natives, &Value) || std::get<std::int32_t>(Value) != 42) return false;
			}
			if (!Vm.ExecuteFunction(*Module, 0, {}, nullptr, &Value) || !RetiredOwner.expired()) return false;
		}
		return true;
	}
}

int RunTests(int Argc, char** Argv)
{
	const std::string Backend = Argc > 1 ? Argv[1] : "";
	Stage = "native binding cache refresh";
	if (!CheckBindingCache(Backend))
	{
		std::cerr << "native binding cache regression during " << Stage << '\n';
		return 1;
	}
	FHostBindingRegistry R;
	std::string Error;
	const auto Add = [&](const std::string& Name, const ENativeType P, int Value)
	{
		return R.RegisterNative(Name, ENativeType::Int, {P}, Tag, reinterpret_cast<void*>(static_cast<std::intptr_t>(Value)), Error);
	};
	if (!Add("pick", ENativeType::Int, 11) || !Add("pick", ENativeType::Double, 22) || !Add("pick", ENativeType::String, 33) || !Add("Ns.pick", ENativeType::Int, 11) || !Add("Ns.pick", ENativeType::String, 33) || !Add("Only.ns_pick", ENativeType::Int, 11) || !Add("Only.ns_pick", ENativeType::String, 33))
	{
		std::cerr << Error;
		return 1;
	}
	if (Add("pick", ENativeType::Int, 44))
	{
		std::cerr << "duplicate signature accepted";
		return 1;
	}
	if (R.RegisterNative("pick", ENativeType::String, {ENativeType::Int}, Echo, nullptr, Error))
	{
		std::cerr << "return-only overload accepted";
		return 1;
	}
	if (R.FindFunction("pick"))
	{
		std::cerr << "ambiguous name lookup selected first signature";
		return 1;
	}
	for (const auto P : {ENativeType::Int, ENativeType::String})
	{
		FHostFunctionDesc F;
		F.Name = "echo";
		F.ReturnType = P;
		F.ParamTypes = {P};
		F.Documentation = P == ENativeType::Int ? "integer overload" : "string overload";
		if (!R.RegisterNative(F, Echo, nullptr, Error) || !R.RegisterNative("bump", ENativeType::Int, {P}, Increment, nullptr, Error, {}, {}, {}, {}, {NativeParamFlagRef}))
		{
			std::cerr << Error;
			return 1;
		}
	}
	for (const auto Flags : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}})
	{
		if (!R.RegisterNative("ref_choice", ENativeType::Int, {ENativeType::Int},
		        Flags == 0 ? Tag : Flags == 1 ? Increment
		                                      : Echo,
		        Flags == 0 ? reinterpret_cast<void*>(static_cast<std::intptr_t>(11)) : nullptr,
		        Error, {}, {}, {}, {}, {Flags}))
		{
			return 1;
		}
	}
	if (!R.RegisterNative("constant_input", ENativeType::Int, {ENativeType::Int}, Echo, nullptr, Error,
	        {}, {}, {}, {}, {std::uint8_t{3}}) ||
	    !R.RegisterNative("mutable_input", ENativeType::Int,
	        {ENativeType::Int}, Increment, nullptr, Error, {}, {}, {}, {}, {std::uint8_t{1}}))
	{
		return 1;
	}
	for (const auto P : {ENativeType::Int, ENativeType::String})
	{
		if (!R.RegisterNative("structured", FNativeTypeDesc::Scalar(ENativeType::Int),
		        {{FNativeTypeDesc::Scalar(P), 0}}, Tag, reinterpret_cast<void*>(static_cast<std::intptr_t>(7)), Error))
		{
			return 1;
		}
	}
	if (!R.RegisterNative("arity", ENativeType::Int, {}, Tag, reinterpret_cast<void*>(static_cast<std::intptr_t>(3)), Error) || !R.RegisterNative("arity", ENativeType::Int, {ENativeType::Int, ENativeType::String}, Tag, reinterpret_cast<void*>(static_cast<std::intptr_t>(4)), Error))
	{
		return 1;
	}
	for (const auto* T : {"int", "string"})
	{
		if (!R.RegisterNative("container", FNativeTypeDesc::Scalar(ENativeType::Int),
		        {{FNativeTypeDesc::ArrayOf(T), 0}}, Tag,
		        reinterpret_cast<void*>(static_cast<std::intptr_t>(std::string(T) == "int" ? 1 : 2)), Error))
		{
			return 1;
		}
	}
	if (!R.RegisterTypeAlias("Count", {}, "int", Error))
	{
		std::cerr << Error;
		return 1;
	}
	if (R.RegisterNative("container", FNativeTypeDesc::Scalar(ENativeType::Int),
	        {{FNativeTypeDesc::ArrayOf("Count"), 0}}, Tag, nullptr, Error))
	{
		std::cerr << "transparent alias duplicated a canonical signature";
		return 1;
	}
	FHostTypeId Type;
	if (!R.RegisterType("OverloadHost", EHostTypeKind::Class, Type, Error)) return 1;
	FHostMemberDesc Ctor;
	Ctor.Kind = EHostMemberKind::Constructor;
	Ctor.Callback = Constructor;
	if (!R.RegisterMember(Type, Ctor, Error))
	{
		std::cerr << Error;
		return 1;
	}
	auto RefCtor = Ctor;
	RefCtor.ParamTypes = {ENativeType::Int};
	RefCtor.ParamFlags = {std::uint8_t{3}};
	if (!R.RegisterMember(Type, RefCtor, Error)) return 1;
	if (!R.SetTypeInheritable(Type, true, Error) || !R.RegisterTypeSubclassFactory(Type, Constructor, nullptr, Error)) return 1;
	for (const auto Kind : {EHostMemberKind::Method, EHostMemberKind::StaticMethod})
	{
		for (const auto P : {ENativeType::Int, ENativeType::String})
		{
			FHostMemberDesc M;
			M.Kind = Kind;
			M.Name = Kind == EHostMemberKind::Method ? "echo" : "make";
			M.ReturnType = P;
			M.ParamTypes = {P};
			M.Callback = Echo;
			M.bConstMethod = Kind == EHostMemberKind::Method;
			if (!R.RegisterMember(Type, M, Error))
			{
				std::cerr << Error;
				return 1;
			}
			if (R.RegisterMember(Type, M, Error))
			{
				std::cerr << "duplicate member accepted";
				return 1;
			}
		}
	}
	for (const auto P : {ENativeType::Int, ENativeType::String})
	{
		FHostMemberDesc M;
		M.Name = "native_read";
		M.ReturnType = ENativeType::Int;
		M.ParamTypes = {P};
		M.Callback = Tag;
		M.UserData = reinterpret_cast<void*>(static_cast<std::intptr_t>(P == ENativeType::Int ? 11 : 33));
		if (!R.RegisterMember(Type, M, Error)) return 1;
	}
	for (const bool Const : {false, true})
	{
		FHostMemberDesc M;
		M.Name = "qualified";
		M.ReturnType = ENativeType::Int;
		M.bConstMethod = Const;
		M.Callback = Tag;
		M.UserData = reinterpret_cast<void*>(static_cast<std::intptr_t>(Const ? 2 : 1));
		if (!R.RegisterMember(Type, M, Error)) return 1;
	}
	for (const bool Reverse : {false, true})
	{
		for (const bool Second : {false, true})
		{
			const bool Static = Reverse != Second;
			FHostMemberDesc M;
			M.Name = Reverse ? "mixed_reverse" : "mixed";
			M.Kind = Static ? EHostMemberKind::StaticMethod : EHostMemberKind::Method;
			M.ReturnType = ENativeType::Int;
			M.ParamTypes = {Static ? ENativeType::Double : ENativeType::Int64};
			M.bConstMethod = !Static;
			M.Callback = Tag;
			M.UserData = reinterpret_cast<void*>(static_cast<std::intptr_t>(Static ? 2 : 1));
			if (!R.RegisterMember(Type, M, Error)) return 1;
		}
	}
	for (const auto Kind : {EHostMemberKind::Method, EHostMemberKind::StaticMethod})
	{
		FHostMemberDesc M;
		M.Kind = Kind;
		M.Name = Kind == EHostMemberKind::Method ? "constant_read" : "constant_make";
		M.ReturnType = ENativeType::Int;
		M.ParamTypes = {ENativeType::Int};
		M.ParamFlags = {std::uint8_t{3}};
		M.bConstMethod = Kind == EHostMemberKind::Method;
		M.Callback = Echo;
		if (!R.RegisterMember(Type, M, Error)) return 1;
	}
	for (const auto P : {ENativeType::Int, ENativeType::String})
	{
		if (!R.RegisterNative("create", ENativeType::HostObject, {P}, Constructor, nullptr, Error, "OverloadHost")) return 1;
	}
	for (const auto P : {ENativeType::Int, ENativeType::String})
	{
		const bool Integer = P == ENativeType::Int;
		const auto Value = reinterpret_cast<void*>(static_cast<std::intptr_t>(Integer ? 11 : 33));
		const auto Other = Integer ? ENativeType::String : ENativeType::Int;
		if (!R.RegisterNative("pair_pick", ENativeType::Int, {P, Other}, Tag, Value, Error) || !R.RegisterNative("callable_pick", ENativeType::Int, {ENativeType::Function}, Tag, Value, Error, {}, {Integer ? "fn(int)->int" : "fn(string)->string"}) || !R.RegisterNative("factory_return", Integer ? ENativeType::HostObject : ENativeType::String, {P}, Integer ? Constructor : Echo, nullptr, Error, Integer ? "OverloadHost" : ""))
		{
			return 1;
		}
		for (const auto Kind : {EHostMemberKind::Method, EHostMemberKind::StaticMethod})
		{
			FHostMemberDesc M;
			M.Kind = Kind;
			M.Name = Kind == EHostMemberKind::Method ? "pair_read" : "pair_make";
			M.bConstMethod = Kind == EHostMemberKind::Method;
			M.ReturnType = ENativeType::Int;
			M.ParamTypes = {P, Other};
			M.Callback = Tag;
			M.UserData = Value;
			if (!R.RegisterMember(Type, M, Error)) return 1;
			M.Name = Kind == EHostMemberKind::Method ? "self" : "factory";
			M.ReturnType = ENativeType::HostObject;
			M.ReturnHostTypeName = "OverloadHost";
			M.ParamTypes = {P};
			M.Callback = Constructor;
			if (!R.RegisterMember(Type, M, Error)) return 1;
		}
	}
	if (!Run(R, R"VS(
void main(){const OverloadHost h=OverloadHost();
assert(pair_pick(1,"x")==11 && pair_pick("x",1)==33);
assert(h.pair_read(1,"x")==11 && h.pair_read("x",1)==33);
assert(OverloadHost::pair_make(1,"x")==11 && OverloadHost::pair_make("x",1)==33);
assert(callable_pick([](int value)->int{return value;})==11);
auto callback=[](string value)->string{return value;};assert(callable_pick(callback)==33);
auto text=factory_return("text");assert(text=="text" && factory_return(1).echo("x")=="x");
assert(h.self(1).echo("a")=="a" && h.self("x").echo(2)==2);
assert(OverloadHost::factory(1).echo("b")=="b" && OverloadHost::factory("x").echo(3)==3);}
)VS",
	        Backend))
	{
		return 1;
	}
	if (!Run(R, R"VS(
import "callable.vs";
class Derived : OverloadHost{public: int first(){return native_read(8);}string second(){return Super.echo("base");}};
int calc(){return pick(1)+pick(2.0)+pick("x");}
void main(){assert(calc()==66);assert(Ns.pick(1)==11 && Ns.pick("x")==33);
assert(structured(1)==7 && structured("x")==7);assert(arity()==3 && arity(1,"x")==4);
array<int> ints={1};array<string> strings={"x"};assert(container(ints)==1 && container(strings)==2);
assert(echo(7)==7 && echo("ok")=="ok");int x=1;string s="x";
assert(bump(ref x)==2 && x==2);assert(bump(ref s)==2 && s=="x!");
auto increment_capture=[&value=x]()->int{return bump(ref value);};
assert(increment_capture()==3 && x==3);
auto append_capture=[&value=s]()->int{return bump(ref value);};
assert(append_capture()==2 && s=="x!!");
assert(bump(1)==2 && bump("copy")==2);
OverloadHost h=OverloadHost();assert(h.echo(8)==8 && h.echo("hi")=="hi");
assert(OverloadHost::make(9)==9 && OverloadHost::make("bye")=="bye");
assert(OverloadHost.make(10)==10 && h.make("instance")=="instance");
auto a=h.echo("type");assert(a=="type");auto b=echo("infer");assert(b=="infer");}
)VS",
	        Backend))
	{
		return 1;
	}
	if (!Run(R, R"VS(
import "callable.vs";
class Derived : OverloadHost{public: int first(){return native_read(8);}string second(){return Super.echo("base");}};
void main(){Derived d=Derived();assert(d.first()==11 && d.second()=="base");
assert(create(1).echo("created")=="created" && create("x").echo(2)==2);
OverloadHost h=OverloadHost();auto a=std::bind_method<fn(int)->int>(h,"echo");
auto b=std::bind_method<fn(string)->string>(h,"echo");assert(a(5)==5 && b("bound")=="bound");}
)VS",
	        Backend))
	{
		return 1;
	}
	if (!Run(R, R"VS(
import "callable.vs";
void main(){OverloadHost mutable_host=OverloadHost();const OverloadHost constant_host=OverloadHost();
assert(mutable_host.qualified()==1 && constant_host.qualified()==2);
auto a=std::bind_method<fn()->int>(mutable_host,"qualified");auto b=std::bind_method<fn()->int>(constant_host,"qualified");
assert(a()==1 && b()==2);}
)VS",
	        Backend))
	{
		return 1;
	}
	if (!Run(R, R"VS(
class LambdaChild : OverloadHost {
public:
int constant() const {
auto bare=[&]()->int{return qualified();};
auto direct=[=]()->int{return this.qualified();};
auto base=[&]()->int{return Super.qualified();};
auto nested=[=]()->int{auto inner=[&]()->int{return qualified();};return inner();};
return bare()+direct()+base()+nested();}
int mutable_value(){auto call=[&]()->int{return qualified();};return call();}
};
void main(){const LambdaChild c=LambdaChild();LambdaChild h=LambdaChild();
assert(c.constant()==8 && h.mutable_value()==1);}
)VS",
	        Backend))
	{
		return 1;
	}
	if (!Run(R, R"VS(
void main(){OverloadHost h=OverloadHost();const OverloadHost c=OverloadHost();int64 x=1;
assert(h.mixed(x)==1 && c.mixed(x)==1 && h.mixed(1.0)==2 && c.mixed(1.0)==2);
assert(h.mixed_reverse(x)==1 && c.mixed_reverse(x)==1 && h.mixed_reverse(1.0)==2 && c.mixed_reverse(1.0)==2);
assert(OverloadHost::mixed(1)==2 && OverloadHost::mixed_reverse(1)==2);}
)VS",
	        Backend))
	{
		return 1;
	}
	if (!Run(R, R"VS(
class ReadChild : OverloadHost {public:int read(){const int x=8;return Super.constant_read(ref x);}int implicit(){const int x=9;return constant_read(ref x);}};
void main(){int x=5;assert(ref_choice(x)==11 && x==5);assert(ref_choice(ref x)==6 && x==6);
const int y=7;assert(ref_choice(ref y)==7 && constant_input(ref y)==7);
const OverloadHost h=OverloadHost(ref y);assert(h.constant_read(ref y)==7 && OverloadHost::constant_make(ref y)==7);
ReadChild child=ReadChild();assert(child.read()==8 && child.implicit()==9);}
)VS",
	        Backend))
	{
		return 1;
	}
	if (!Run(R, R"VS(
using namespace Only;
namespace Only { int read(){return ns_pick(1);} }
void main(){assert(ns_pick(1)==11 && ns_pick("x")==33 && Only.read()==11);}
)VS",
	        Backend))
	{
		return 1;
	}
	if (!Run(R, "using namespace Only;void main(){assert(ns_pick(1)==11 && ns_pick(\"x\")==33);}", Backend)) return 1;
	{
		Stage = "compiler extension overloads";
		if (!Run(R, R"VS(
import "callable.vs";
class Child : OverloadHost {public:string native_read(int value){return "child";}int base(){return Super.native_read(1);}};
void main(){Child child=Child();auto value=child.native_read(1);assert(value=="child" && child.base()==11);
auto callback=std::bind_method<fn(int)->string>(child,"native_read");assert(callback(1)=="child");}
)VS",
		        Backend))
		{
			return 1;
		}
		const std::string Root = (std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Resources/Scripts/stdlib").generic_string();
		FCompileOptions ExtensionOptions = VeroScript::Test::MakeCompileOptions();
		ExtensionOptions.StdlibSearchPaths = {Root};
		ExtensionOptions.ScriptCompileExtensions.Bindings = &R;
		ExtensionOptions.ScriptCompileExtensions.bBindingsDeterministic = true;
		ExtensionOptions.ScriptCompileExtensions.BindingConfiguration = "native-overloads-0.1.1";
		ExtensionOptions.SourceOverlays = {{"E:/VeroScriptTests/native-overload-processor.vs", R"VS(
import "compiler.vs";
export void register_extension(std::compiler::registration reg){
assert(pick(1)==11 && pick(2.0)==22 && pick("x")==33);
assert(echo("compiler")=="compiler");OverloadHost h=OverloadHost();assert(h.echo(7)==7 && h.echo("x")=="x");}
)VS"}};
		ExtensionOptions.ScriptCompileExtensions.Roots = {{"E:/VeroScriptTests/native-overload-processor.vs", {}}};
		for (int I = 0; I < 2; ++I)
		{
			const auto Extended = VeroScriptCompile::CompileSource("void main(){}", "E:/VeroScriptTests/overload-business.vs", ExtensionOptions);
			if (!Extended.bSuccess)
			{
				std::cerr << "compiler extension lost overloads: " << Extended.Error.Message;
				return 1;
			}
		}
	}
	FCompileOptions O = VeroScript::Test::MakeCompileOptions();
	O.Bindings = &R;
	Stage = "invalid overload diagnostics";
	auto Raw = R.GetNativeFunctions();
	Raw.push_back(Raw.front());
	FCompileOptions RawOptions = VeroScript::Test::MakeCompileOptions();
	RawOptions.Natives = &Raw;
	const auto Duplicate = VeroScriptCompile::CompileSource("void main(){}", "duplicate-native.vs", RawOptions);
	if (Duplicate.bSuccess || Duplicate.Error.MessageId != "diag.registernative_duplicate_name")
	{
		std::cerr << "duplicate raw-table signature accepted";
		return 1;
	}
	{
		FHostBindingRegistry Aliases;
		if (!Aliases.RegisterTypeAlias("Tiny", {}, "int8", Error)) return 1;
		for (const auto* CallbackType : {"fn(array<Tiny[2]>)->Tiny", "fn(array<Tiny>[2])->Tiny",
		         "fn(array<Tiny[3]>)->Tiny", "fn(array<Tiny[2]>)->int"})
		{
			if (!Aliases.RegisterNative("nested_array_callback", ENativeType::Int, {ENativeType::Function},
			        Tag, nullptr, Error, {}, {CallbackType}))
			{
				std::cerr << "distinct nested array/function signatures collapsed: " << CallbackType << ' ' << Error;
				return 1;
			}
		}
		if (Aliases.RegisterNative("nested_array_callback", ENativeType::Int, {ENativeType::Function},
		        Tag, nullptr, Error, {}, {"fn(array<char[2]>)->char"}))
		{
			std::cerr << "equivalent nested array/function aliases accepted";
			return 1;
		}
		if (!Aliases.RegisterNative("alias_call", ENativeType::Int, {ENativeType::Function}, Tag,
		        reinterpret_cast<void*>(static_cast<std::intptr_t>(11)), Error, {}, {"fn(Tiny)->Tiny"}) ||
		    !Aliases.RegisterNative("alias_call", ENativeType::Int, {ENativeType::Function}, Tag,
		        reinterpret_cast<void*>(static_cast<std::intptr_t>(33)), Error, {}, {"fn(string)->string"}))
		{
			return 1;
		}
		if (!Run(Aliases, "void main(){assert(alias_call([](Tiny x)->Tiny{return x;})==11);"
		                  "assert(alias_call([](string x)->string{return x;})==33);}",
		        Backend))
		{
			return 1;
		}
		FHostTypeId AliasHost;
		if (!Aliases.RegisterType("AliasHost", EHostTypeKind::Class, AliasHost, Error)) return 1;
		for (const auto Kind : {EHostMemberKind::Method, EHostMemberKind::StaticMethod})
		{
			FHostMemberDesc Member;
			Member.Kind = Kind;
			Member.Name = Kind == EHostMemberKind::Method ? "call" : "static_call";
			Member.ReturnType = ENativeType::Int;
			Member.ParamTypes = {ENativeType::Function};
			Member.ParamHostTypeNames = {"fn(Tiny)->Tiny"};
			Member.Callback = Tag;
			if (!Aliases.RegisterMember(AliasHost, Member, Error)) return 1;
			Member.ParamHostTypeNames = {"fn(char)->char"};
			if (Aliases.RegisterMember(AliasHost, Member, Error))
			{
				std::cerr << "host member function aliases duplicated a signature";
				return 1;
			}
		}
		for (const auto Flags : {std::uint8_t{0}, NativeParamFlagRef,
		         std::uint8_t(NativeParamFlagRef | NativeParamFlagConstRef)})
		{
			const std::string Name = "alias_callback_" + std::to_string(Flags);
			if (!Aliases.RegisterNative(Name, ENativeType::Int, {ENativeType::Function}, Tag, nullptr, Error,
			        {}, {"fn(const ref Tiny[2][3])->array<Tiny>"}, {}, {}, {Flags}) ||
			    Aliases.RegisterNative(Name, ENativeType::Int, {ENativeType::Function}, Tag, nullptr, Error,
			        {}, {"fn(const ref char[2][3])->array<char>"}, {}, {}, {Flags}))
			{
				std::cerr << "function/reference aliases duplicated a signature: " << Error;
				return 1;
			}
			FNativeFunction First;
			First.Name = Name;
			First.ReturnType = ENativeType::Int;
			First.RuntimeReturnTypeName = "int";
			First.ParamTypes = {ENativeType::Int8};
			First.ParamFlags = {Flags};
			First.Callback = Tag;
			First.RuntimeParamTypeNames = {"int8"};
			auto Second = First;
			Second.RuntimeParamTypeNames = {"char"};
			for (const auto Kind : {ENativeType::Int8, ENativeType::Function, ENativeType::Array})
			{
				First.ParamTypes = Second.ParamTypes = {Kind};
				if (Kind != ENativeType::Int8)
				{
					const std::string Prefix = Kind == ENativeType::Array ? "array<" : "";
					const std::string Suffix = Kind == ENativeType::Array ? ">" : "";
					First.ParamHostTypeNames = Second.ParamHostTypeNames = {"fn(ref char[2][3])->array<short>"};
					First.ParamElementTypeNames = Second.ParamElementTypeNames = First.ParamHostTypeNames;
					First.RuntimeParamTypeNames = {Prefix + "fn(ref int8[2][3])->array<int16>" + Suffix};
					Second.RuntimeParamTypeNames = {Prefix + "fn(ref char[2][3])->array<short>" + Suffix};
				}
				std::vector<FNativeFunction> RawAliases = {First, Second};
				FCompileOptions AliasOptions = VeroScript::Test::MakeCompileOptions();
				AliasOptions.Natives = &RawAliases;
				const auto Result = VeroScriptCompile::CompileSource("void main(){}", "duplicate-native-alias.vs", AliasOptions);
				if (Result.bSuccess || Result.Error.MessageId != "diag.registernative_duplicate_name")
				{
					std::cerr << "raw aliases duplicated a canonical signature: " << Result.Error.Message;
					return 1;
				}
			}
		}
	}
	for (const auto* Source : {"void main(){pick();}", "void main(){pick(map<int,int>());}",
	         "void main(){OverloadHost h=OverloadHost();h.echo();}", "void main(){OverloadHost::echo(1);}",
	         "void main(){int64 x=1;mutable_input(ref x);}", "void main(){const int x=1;mutable_input(ref x);}",
	         "class C:OverloadHost{public:int read()const{auto f=[&]()->int{return native_read(1);};return f();}};void main(){}",
	         "class C:OverloadHost{public:int read()const{auto f=[&]()->int{return Super.native_read(1);};return f();}};void main(){}"})
	{
		auto Result = VeroScriptCompile::CompileSource(Source, "invalid-overload.vs", O);
		if (Result.bSuccess)
		{
			std::cerr << "invalid overload accepted: " << Source;
			return 1;
		}
	}
	if (!Add("ambiguous", ENativeType::Int64, 1) || !Add("ambiguous", ENativeType::Double, 2)) return 1;
	auto Ambiguous = VeroScriptCompile::CompileSource("void main(){ambiguous(1);}", "ambiguous.vs", O);
	if (Ambiguous.bSuccess || Ambiguous.Error.MessageId != "diag.ambiguous_call_to")
	{
		std::cerr << "ambiguous overload was not diagnosed: " << Ambiguous.Error.Message;
		return 1;
	}
	for (const auto* Source : {
	         "void main(){OverloadHost h=OverloadHost();h.mixed(1);}",
	         "void main(){const OverloadHost h=OverloadHost();h.mixed(1);}",
	         "void main(){OverloadHost h=OverloadHost();h.mixed_reverse(1);}",
	         "void main(){const OverloadHost h=OverloadHost();h.mixed_reverse(1);}"})
	{
		const auto Result = VeroScriptCompile::CompileSource(Source, "ambiguous-host-method.vs", O);
		if (Result.bSuccess || Result.Error.MessageId != "diag.ambiguous_call_to")
		{
			std::cerr << "static/instance ambiguity depended on registration order: " << Source;
			return 1;
		}
	}
	std::string Json;
	Stage = "export catalog";
	if (!ExportBindingCatalogJson(R, Json)) return 1;
	FHostBindingRegistry Imported;
	Stage = "import catalog";
	if (!ImportBindingCatalogJson(Json, Imported, Error))
	{
		std::cerr << Error;
		return 1;
	}
	O.Bindings = &Imported;
	Stage = "compile imported catalog";
	auto Analysis = VeroScriptCompile::CompileSource("void main(){auto a=echo(1);auto b=echo(\"x\");OverloadHost h=OverloadHost();auto c=h.echo(\"x\");}", "catalog-overloads.vs", O);
	if (!Analysis.bSuccess || Imported.GetFunctions().size() != R.GetFunctions().size())
	{
		std::cerr << "catalog overload round trip failed: " << Analysis.Error.Message;
		return 1;
	}
	std::cout << "native overload tests passed\n";
	return 0;
}

int main(int Argc, char** Argv)
{
	try
	{
		return RunTests(Argc, Argv);
	}
	catch (const std::exception& E)
	{
		std::cerr << "native overload test exception during " << Stage << ": " << E.what() << '\n';
		return 1;
	}
}
