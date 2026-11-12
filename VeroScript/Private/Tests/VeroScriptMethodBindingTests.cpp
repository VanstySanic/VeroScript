// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptMethodBinding.h"
#include "VeroScriptCompile.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptVm.h"

#include <iostream>
#include <sstream>

using namespace VeroScript;

namespace
{
	bool Check(bool Success, const std::string& Label)
	{
		if (!Success)
		{
			std::cerr << Label << '\n';
		}

		return Success;
	}
	FHostObjectHandle HostValue(std::string Type, int Value)
	{
		return MakeHostObject(0, std::move(Type), new int(Value),
		    [](void* Payload)
		{
			delete static_cast<int*>(Payload);
		});
	}
	int VoidCalls = 0;
	bool Touch(const FNativeCallContext&, FValue&, std::string&)
	{
		++VoidCalls;
		return true;
	}
	bool Create(const FNativeCallContext& Context, FValue& Result, std::string&)
	{
		Result = HostValue(*static_cast<const std::string*>(Context.UserData), 4);
		return true;
	}
	bool Read(const FNativeCallContext& Context, FValue& Result, std::string&)
	{
		Result = std::int32_t{*static_cast<int*>(std::get<FHostObjectHandle>(Context.Args[0])->Payload)};
		return true;
	}
	bool Replace(const FNativeCallContext& Context, FValue& Result, std::string& Error)
	{
		FValue* Receiver = Context.MutableArgs ? Context.MutableArgs[0] : nullptr;
		if (!Receiver)
		{
			Error = "missing mutable host receiver";
			return false;
		}
		const auto& Original = std::get<FHostObjectHandle>(Context.Args[0]);
		const int Value = *static_cast<int*>(Original->Payload) + 1;
		*Receiver = HostValue(Original->TypeName, Value);
		Result = std::int32_t{Value};
		return true;
	}
	bool Clone(const FNativeCallContext& Context, FValue& Result, std::string&)
	{
		const auto& Original = std::get<FHostObjectHandle>(Context.Args[0]);
		Result = HostValue(Original->TypeName, *static_cast<int*>(Original->Payload));
		return true;
	}

	bool TestForwardMethodDeclarations(const FCompileOptions& Options)
	{
		struct FCase
		{
			const char* Label;
			const char* Source;
			bool bExpectedSuccess;
			const char* ExpectedOutput = nullptr;
		};
		const FCase Cases[] =
		    {
		        {"constructor before target", R"VS(
import "callable.vs";
class B{public:B(){auto f=std::bind_method<fn()->int>(this,"read");print(f());}
 virtual int read(){return 1;}}
class D:public B{public:int read()override{return 2;}}
void main(){D value=D();}
)VS",
		            true},
		        {"constructor after target", R"VS(
import "callable.vs";
class B{public:virtual int read(){return 1;}
 B(){auto f=std::bind_method<fn()->int>(this,"read");print(f());}}
class D:public B{public:int read()override{return 2;}}
void main(){D value=D();}
)VS",
		            true},
		        {"method before target", R"VS(
import "callable.vs";
class B{public:int probe(){auto f=std::bind_method<fn()->int>(this,"read");return f();}
 int read(){return 1;}}
void main(){B value=B();print(value.probe());}
)VS",
		            true},
		        {"method after target", R"VS(
import "callable.vs";
class B{public:int read(){return 1;}
 int probe(){auto f=std::bind_method<fn()->int>(this,"read");return f();}}
void main(){B value=B();print(value.probe());}
)VS",
		            true},
		        {"struct target before method bodies", R"VS(
import "callable.vs";
struct S{public:int read(){return 3;}}
class C{public:C(){shared<S> value=shared<S>(S());
 auto f=std::bind_method<fn()->int>(value,"read");print(f());}}
void main(){C value=C();}
)VS",
		            true},
		        {"interface default before target", R"VS(
import "callable.vs";
interface I{public:int probe(){auto f=std::bind_method<fn()->int>(this,"read");return f();}
 virtual int read(){return 3;}}
class D:public I{}
void main(){D value=D();print(value.probe());}
)VS",
		            true},
		        {"generic constructor binds a later virtual declaration", R"VS(
import "callable.vs";
class B<T>{public:T value;B(T input):value(input){auto f=std::bind_method<fn()->T>(this,"read");print(f());}
 virtual T read(){return value;}}
class D:public B<int>{public:D():B(7){}int read()override{return 9;}}
void main(){D value=D();print(value.read());}
)VS",
		            true, "7\n9\n"},
		        {"generic method binds a later declaration", R"VS(
import "callable.vs";
class B<T>{public:T probe(){auto f=std::bind_method<fn()->T>(this,"read");return f();}
 T read(){return cast<T>(7);}}
void main(){B<int> value=B<int>();print(value.probe());}
)VS",
		            true, "7\n"},
		        {"generic struct static body binds a later declaration", R"VS(
import "callable.vs";
struct S<T>{T value;S(T input):value(input){}
 static T probe(shared<S<T>> target){auto f=std::bind_method<fn()->T>(target,"read");return f();}
 T read(){return value;}}
using Value=S<int>;
void main(){shared<Value> value=shared<Value>(Value(7));print(S<int>::probe(value));}
)VS",
		            true, "7\n"},
		        {"generic interface default body binds a later pure declaration", R"VS(
import "callable.vs";
interface I<T>{public:T probe(){auto f=std::bind_method<fn()->T>(this,"read");return f();}
 virtual T read()=0;}
class D:public I<int>{public:int read()override{return 7;}}
void main(){D value=D();print(value.probe());}
)VS",
		            true, "7\n"},
		        {"generic interface binding preserves parameter metadata", R"VS(
import "callable.vs";
interface I<T>{public:T probe(T input){auto f=std::bind_method<fn(T)->T>(this,"read");return f(input);}
 virtual T read(T input)=0;}
class D:public I<int>{public:int read(int input)override{return input;}}
void main(){D value=D();print(value.probe(3));}
)VS",
		            true, "3\n"},
		        {"instantiated member template uses its owner's declarations", R"VS(
import "callable.vs";
class B<T>{public:U probe<U>(U extra){auto f=std::bind_method<fn()->T>(this,"read");return extra+f();}
 T read(){return cast<T>(7);}}
void main(){B<int> value=B<int>();print(value.probe<int>(2));}
)VS",
		            true, "9\n"},
		        {"generic owner binds a later member template on demand", R"VS(
import "callable.vs";
class B<T>{public:B(){auto f=std::bind_method<fn(T)->T>(this,"identity");print(f(cast<T>(7)));}
 U identity<U>(U value){return value;}}
void main(){B<int> value=B<int>();}
)VS",
		            true, "7\n"},
		        {"generic forward target keeps return signature", R"VS(
import "callable.vs";
class B<T>{public:B(){auto f=std::bind_method<fn()->double>(this,"read");}
 T read(){return cast<T>(7);}}
void main(){B<int> value=B<int>();}
)VS",
		            false},
		        {"generic forward target keeps private access", R"VS(
import "callable.vs";
class B<T>{public:B(){auto f=std::bind_method<fn()->T>(this,"read");}
 private:T read(){return cast<T>(7);}}
void main(){B<int> value=B<int>();}
)VS",
		            false},
		        {"generic forward target keeps protected access", R"VS(
import "callable.vs";
class B<T>{public:B(){auto f=std::bind_method<fn()->T>(this,"read");}
 protected:T read(){return cast<T>(7);}}
void main(){B<int> value=B<int>();}
)VS",
		            false},
		        {"forward target keeps return signature", R"VS(
import "callable.vs";
class B{public:B(){auto f=std::bind_method<fn()->double>(this,"read");}
 int read(){return 1;}}
void main(){B value=B();}
)VS",
		            false},
		        {"forward target keeps private access", R"VS(
import "callable.vs";
class B{public:B(){auto f=std::bind_method<fn()->int>(this,"read");}
 private:int read(){return 1;}}
void main(){B value=B();}
)VS",
		            false},
		        {"forward target keeps protected access", R"VS(
import "callable.vs";
class B{public:B(){auto f=std::bind_method<fn()->int>(this,"read");}
 protected:int read(){return 1;}}
void main(){B value=B();}
)VS",
		            false},
		};
		for (const FCase& Case : Cases)
		{
			const auto Compiled = VeroScriptCompile::CompileSource(Case.Source,
			    "binding-forward-declarations.vs", Options);
			if (!Check(Compiled.bSuccess == Case.bExpectedSuccess,
			        std::string(Case.Label) + ": " + Compiled.Error.Message))
			{
				return false;
			}
			if (!Case.bExpectedSuccess && !Check(Compiled.Error.Message.find("no public instance method matches") != std::string::npos, std::string(Case.Label) + ": unexpected diagnostic: " + Compiled.Error.Message))
			{
				return false;
			}
			if (Case.ExpectedOutput)
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(EExecutionBackend::Vm);
				if (!Check(Vm.Execute(*Compiled.Module, ERunMode::Script) && Output.str() == Case.ExpectedOutput,
				        std::string(Case.Label) + ": " + Vm.GetError().Message + " output=[" + Output.str() + "]"))
				{
					return false;
				}
			}
		}
		return true;
	}
}

int main(int Argc, char** Argv)
{
	if (Argc != 2) return 2;
	bool Ok = true;
	std::string Error;
	// A mutable receiver prefers a mutable method even if less-preferred const
	// candidates are ambiguous. Candidate enumeration order must not decide it.
	FMethodBindingSelection ConstA;
	ConstA.OwnerType = "A";
	ConstA.MethodName = "read";
	ConstA.FunctionType = "fn()->int";
	ConstA.LinkageName = "A::read";
	ConstA.bPublic = true;
	ConstA.bConst = true;
	auto ConstB = ConstA;
	ConstB.OwnerType = "B";
	ConstB.LinkageName = "B::read";
	auto Mutable = ConstA;
	Mutable.OwnerType = "M";
	Mutable.LinkageName = "M::read";
	Mutable.bConst = false;
	const FMethodBindingRequest MutableRequest{"M", "read", "fn()->int", false};
	FMethodBindingSelection Selected;
	for (const auto& Ordered : {std::vector<FMethodBindingSelection>{ConstA, ConstB, Mutable},
	         std::vector<FMethodBindingSelection>{Mutable, ConstB, ConstA}})
	{
		Ok &= Check(SelectBindableMethod(MutableRequest, Ordered, Selected, Error) &&
		        Selected.LinkageName == Mutable.LinkageName,
		    "method selection depends on candidate order");
	}

	Ok &= Check(!SelectBindableMethod(MutableRequest, {ConstA, ConstB}, Selected, Error) &&
	        Error.find("ambiguous") != std::string::npos,
	    "ambiguous preferred candidates accepted");
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	Options.StdlibSearchPaths = {Argv[1]};
	if (!TestForwardMethodDeclarations(Options)) return 1;
	auto Compiled = VeroScriptCompile::CompileSource(
	    "export class Model { public: int read() const { return 2; } }; int main(){return 0;}",
	    "binding-service.vs", Options);
	if (!Check(Compiled.bSuccess, Compiled.Error.Message)) return 1;
	std::vector<FMethodBindingSelection> Candidates;
	Ok &= Check(CollectMethodBindingCandidates(*Compiled.Module, nullptr, "Model", Candidates, Error), Error);
	// Runtime metadata carries the qualified type identity.
	if (Candidates.empty())
	{
		for (const auto& Type : Compiled.Module->Types)
		{
			if (Type.Name.find("Model") != std::string::npos)
			{
				CollectMethodBindingCandidates(*Compiled.Module, nullptr, Type.Name, Candidates, Error);
				break;
			}
		}
	}
	if (!Check(!Candidates.empty(), "no bindable Model metadata")) return 1;
	FMethodBindingRequest Request{Candidates[0].OwnerType, "read", "fn()->int", true};
	FMethodBindingContext Context;
	Context.Module = Compiled.Module.get();
	Context.ProgramOwner = std::make_shared<int>(0);
	Context.ProgramGeneration = 1;
	std::shared_ptr<const FMethodBindingDescriptor> Descriptor;
	Ok &= Check(ResolveMethodBinding(Context, Request, Descriptor, Error), Error);
	if (!Descriptor) return 1;
	Ok &= Check(ValidateMethodBinding(Context, *Descriptor, Error), Error);
	const FValue Token = MakeMethodBindingToken(Descriptor);
	Ok &= Check(ReadMethodBindingToken(Token) == Descriptor, "opaque token did not retain descriptor");
	++Context.ProgramGeneration;
	Ok &= Check(!ValidateMethodBinding(Context, *Descriptor, Error), "stale generation accepted");
	FValue Result;
	bool Invoked = false;
	const auto Invoke = [&](const auto&, auto, const auto&, const auto&, auto&, auto&)
	{
		Invoked = true;
		return true;
	};
	Ok &= Check(!InvokeBoundMethod(Context, *Descriptor, FValue{}, {}, Invoke, Result, Error) && !Invoked,
	    "Invoke must revalidate without trusting a previous Validate");
	--Context.ProgramGeneration;
	Context.ProgramOwner = std::make_shared<int>(0);
	Ok &= Check(!ValidateMethodBinding(Context, *Descriptor, Error), "foreign program accepted");
	Context.ProgramOwner = Descriptor->ProgramOwner;
	Context.CheckWork = []
	{
		return false;
	};
	Ok &= Check(!ValidateMethodBinding(Context, *Descriptor, Error), "cancellation ignored");

	FHostBindingRegistry Registry;
	const std::string ClassName = "HostModel", StructName = "HostCounter";
	for (const auto* Name : {&ClassName, &StructName})
	{
		FHostTypeId Id;
		const bool ValueType = Name == &StructName;
		Ok &= Check(
		    Registry.RegisterType(*Name, ValueType ? EHostTypeKind::Struct : EHostTypeKind::Class, Id, Error),
		    Error);
		FHostMemberDesc Constructor;
		Constructor.Kind = EHostMemberKind::Constructor;
		Constructor.ReturnType = ENativeType::HostObject;
		Constructor.ReturnHostTypeName = *Name;
		Constructor.Callback = Create;
		Constructor.UserData = const_cast<std::string*>(Name);
		Ok &= Check(Registry.RegisterMember(Id, Constructor, Error), Error);
		FHostMemberDesc Getter;
		Getter.Name = "read";
		Getter.ReturnType = ENativeType::Int;
		Getter.bConstMethod = true;
		Getter.Callback = Read;
		Ok &= Check(Registry.RegisterMember(Id, Getter, Error), Error);
		FHostMemberDesc VoidMethod;
		VoidMethod.Name = "touch";
		VoidMethod.ReturnType = ENativeType::Void;
		VoidMethod.Callback = Touch;
		Ok &= Check(Registry.RegisterMember(Id, VoidMethod, Error), Error);
		if (ValueType)
		{
			// Const callability is independent from the host-value write-back
			// adapter. A const receiver must not be forwarded as mutable storage.
			auto ConstAdapter = Getter;
			ConstAdapter.Name = "read_with_storage";
			ConstAdapter.bMutableThis = true;
			Ok &= Check(Registry.RegisterMember(Id, ConstAdapter, Error), Error);
			FHostMemberDesc Mutator;
			Mutator.Name = "increment";
			Mutator.ReturnType = ENativeType::Int;
			Mutator.bMutableThis = true;
			Mutator.Callback = Replace;
			Ok &= Check(Registry.RegisterMember(Id, Mutator, Error), Error);
			Ok &= Check(Registry.RegisterTypeClone(Id, Clone, nullptr, Error), Error);
		}
	}
	if (!Ok) return 1;
	Options.Bindings = &Registry;
	Compiled = VeroScriptCompile::CompileSource(R"VS(
import "attribute.vs";
void call_touch(HostModel model){auto touch=std::bind_method<fn()->void>(model,"touch");touch();}
int main() {
    HostModel model=HostModel();
    auto a=std::make_attribute_by_name<int>(model,"read"); assert(a()==4,"host strong");
    auto b=std::make_weak_attribute_by_name<int>(weak<HostModel>(model),"read"); assert(b()==4,"host weak alive");
    call_touch(model);
    a.reset(); model=nullptr; assert(!b.try_get().has_value(),"host weak expired");
    shared<HostCounter> counter=shared<HostCounter>(HostCounter());
    auto value=std::make_attribute_by_name<int>(counter,"read");
    fn()->int increment=std::bind_method<fn()->int>(counter,"increment");
    assert(increment()==5,"host first increment"); assert(value()==5,"host first writeback");
    assert(increment()==6,"host second increment"); assert(counter.value.read()==6,"host second writeback");
    const shared<HostCounter> frozen=counter;
    auto const_reader=std::make_attribute_by_name<int>(frozen,"read_with_storage");
    assert(const_reader()==6,"const host receiver with value adapter");
    return 0;
})VS",
	    "host-attribute.vs", Options);
	if (!Check(Compiled.bSuccess, Compiled.Error.Message)) return 1;
	std::ostringstream Output, Errors;
	std::istringstream Input;
	VeroScriptVm Vm(Output, Input, Errors);
	VeroScript::Test::Configure(Vm);
	Vm.SetMemoryHostBindings(&Registry);
	Ok &= Check(Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Registry.GetNativeFunctions()),
	    Vm.GetError().Message);
	Ok &= Check(VoidCalls == 1, "bound native void method must execute exactly once");
	return Ok ? 0 : 1;
}
