// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptPackage.h"
#include "VeroScriptVm.h"

#include <iostream>
#include <sstream>

using namespace VeroScript;

namespace
{
	enum class EExpected
	{
		Success,
		CompileError,
		RuntimeError
	};
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	std::string BackendPath;

	bool Run(const char* Label, const char* Source, EExpected Expected = EExpected::Success,
	    const char* ErrorText = "", std::uint64_t Budget = 0)
	{
		auto Compiled = VeroScriptCompile::CompileSource(Source, "attribute-test.vs", Options);
		if (!Compiled.bSuccess)
		{
			if (Expected == EExpected::CompileError &&
			    Compiled.Error.Message.find(ErrorText) != std::string::npos)
			{
				return true;
			}

			std::cerr << Label << ": compile: " << Compiled.Error.Location.Line << ": "
			          << Compiled.Error.Message << '\n';
			return false;
		}
		if (Expected == EExpected::CompileError)
		{
			std::cerr << Label << ": unexpectedly compiled\n";
			return false;
		}
		// A binding must survive serialization without retaining a compile-time
		// selection, token or borrowed declaration. Exercise both execution policies.
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && BackendPath.empty())
			{
				continue;
			}
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			if (!VeroScriptBytecodeIO::SerializeBytecode(*Compiled.Module, 91, Bytes, Error))
			{
				std::cerr << Label << ": serialize: " << Error << '\n';
				return false;
			}
			auto Module = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 91, Error);
			if (!Module)
			{
				std::cerr << Label << ": deserialize: " << Error << '\n';
				return false;
			}
			if (Backend == EExecutionBackend::Mixed)
			{
				FPackageBuildOptions PackageOptions;
				PackageOptions.PackageName = "attribute-tests";
				PackageOptions.bStripDebugInfo = true;
				FPackageData Package;
				if (!BuildPackage(*Module, PackageOptions, Bytes, Error) ||
				    !ReadPackage(FByteView(Bytes), Package, Error))
				{
					std::cerr << Label << ": package: " << Error << '\n';
					return false;
				}
				Module = std::move(Package.Module);
			}
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			if (!BackendPath.empty() && !Vm.SetJitBackendPath(BackendPath, Error))
			{
				std::cerr << Label << ": backend: " << Error << '\n';
				return false;
			}
			Vm.SetExecutionLimits({Budget, 0});
			const bool Success = Vm.Execute(*Module, ERunMode::Script);
			if (Success != (Expected == EExpected::Success) ||
			    (!Success && Vm.GetError().Message.find(ErrorText) == std::string::npos) ||
			    (Budget && Vm.GetError().MessageId != "diag.runtime_execution_instruction_limit_exceeded"))
			{
				std::cerr << Label << ": run: " << Vm.GetError().Location.Line << ": "
				          << Vm.GetError().Message << '\n';
				return false;
			}
		}
		return true;
	}
}

int main(int Argc, char** Argv)
{
	if (Argc < 2 || Argc > 3) return 2;
	Options.StdlibSearchPaths = {Argv[1]};
	if (Argc == 3)
	{
		BackendPath = Argv[2];
	}
	bool Ok = true;
	Ok &= Run("literal method template binding", R"VS(
import "attribute.vs";
class Reader{public:template<typename T> T echo(T value){return value;}
template<typename T> T read(){return cast<T>(7);}};
int main(){Reader reader=Reader();
auto echo=std::bind_method<fn(int)->int>(reader,"echo");assert(echo(11)==11);
auto value=std::make_attribute_by_name<int>(reader,"read");assert(value()==7);
weak<Reader> target=weak<Reader>(reader);
auto weakValue=std::make_weak_attribute_by_name<int>(target,"read");assert(weakValue()==7);
return 0;}
)VS");
	Ok &= Run("dynamic names exclude incidental method template instances", R"VS(
import "attribute.vs";
class Reader{public:template<typename T> T echo(T value){return value;}};
int main(){Reader reader=Reader();assert(reader.echo(3)==3);string name="echo";
auto callback=std::bind_method<fn(int)->int>(reader,name);return 0;}
)VS",
	    EExpected::RuntimeError, "no public instance method");
	Ok &= Run("failed stored-value copy preserves configuration", R"VS(
import "attribute.vs";
bool fail=false;struct S{int value=7;S()=default;S(const S& other){if(fail)throw 9;value=other.value;}};
int main(){std::attribute<S> value=std::attribute<S>(S());S replacement=S();replacement.value=8;
fail=true;int caught=0;try{value.set(replacement);}catch(int n){caught=n;}
fail=false;assert(caught==9 && value.get().value==7);return 0;}
)VS");
	Ok &= Run("stored-value copy retains snapshot across reentry", R"VS(
import "attribute.vs";
bool change=false;struct S{int value=7;S()=default;S(const S& other){if(change){change=false;owner.value.reset();}value=other.value;}};
class Owner{public:std::attribute<S> value;};Owner owner=Owner();
int main(){owner.value.set(S());change=true;S result=owner.value.get();assert(result.value==7 && !owner.value.is_set());return 0;}
)VS");
	Ok &= Run("try_get output overload", R"VS(
import "attribute.vs";
int model=4;bool available=false;
int main(){std::attribute<int> a=std::attribute<int>();int output=19;
assert(!a.try_get(ref output) && output==19);a.set(3);assert(a.try_get(ref output) && output==3);
a.bind([]()->int{return model;});assert(a.try_get(ref output) && output==4);
model=8;assert(a.try_get(ref output) && output==8);
a.bind_optional([]()->optional<int>{return available?optional<int>(model):optional<int>();});
assert(!a.try_get(ref output) && output==8);available=true;model=12;
assert(a.try_get(ref output) && output==12);a.bind([]()->int{throw 7;});
int caught=0;try{a.try_get(ref output);}catch(int value){caught=value;}
assert(caught==7 && output==12);return 0;}
)VS");
	Ok &= Run("configuration identity and live getter results", R"VS(
import "attribute.vs";
int current=2;
int main(){std::attribute<int> value=std::attribute<int>();std::attribute<int> empty=std::attribute<int>();
assert(value.has_same_configuration(ref empty));value.set(3);
std::attribute<int> copy=value;assert(copy.has_same_configuration(ref value));
value.set(4);assert(!value.has_same_configuration(ref copy) && copy.get()==3);
value.bind([]()->int{return current;});std::attribute<int> bound=value;assert(value.get()==2);
current=7;assert(value.has_same_configuration(ref bound) && value.get()==7);
value.reset();assert(value.has_same_configuration(ref empty) && !value.is_set());
value.set(3);assert(!value.has_same_configuration(ref copy) && !value.has_same_configuration(ref bound));return 0;}
)VS");
	Ok &= Run("int64 construction, read and rebinding", R"VS(
import "attribute.vs";
int main(){std::attribute<int64> value=std::attribute<int64>(3);assert(value.get()==3);
std::attribute<int64> copy=value;copy.set(4294967296i64);assert(copy()==4294967296i64 && value()==3);
value.bind([]()->int64{return 9;});assert(value.get()==9 && copy.get()==4294967296i64);
value.reset();assert(!value.is_set());return 0;}
)VS");
	Ok &= Run("states, copies, evaluation", R"VS(
import "attribute.vs";
int reads = 0;
int next() { reads += 1; return reads; }
int accept(std::attribute<int> value) { return value.get(); }
int main() {
    std::attribute<int> a = std::attribute<int>();
    assert(!a.is_set() && !a.is_bound());
    assert(!a.try_get().has_value() && a.get_or(8) == 8);
    a.set(4); assert(a.is_set() && !a.is_bound());
    std::attribute<int> copy = a; copy.set(6); assert(a() == 4 && copy.get() == 6);
    a.bind(next); assert(reads == 0 && a.is_bound());
    int converted = a; assert(converted == 1 && reads == 1);
    assert(a() == 2 && a.get_or(99) == 3 && reads == 3);
    copy = a; copy.reset(); assert(!copy.is_set() && a.is_bound());
    assert(a.try_get().get() == 4 && reads == 4);
    assert(accept(12) == 12);
    a.bind_optional([]() -> optional<int> { return optional<int>(); });
    assert(a.is_set() && a.is_bound() && !a.try_get().has_value());
    assert(a.get_or(23) == 23);
    a.reset(); assert(!a.is_bound() && !a.is_set());
    return 0;
})VS");
	Ok &= Run("inactive and nested values", R"VS(
import "attribute.vs";
int constructed = 0;
struct Item { public: int value; Item(int v) { value=v; constructed += 1; } };
int result() { return 17; }
int main() {
    std::attribute<Item> empty = std::attribute<Item>(); assert(constructed == 0);
    empty.set(Item(5)); Item item = empty.get(); item.value = 9; assert(empty().value == 5);
    std::attribute<Item> copy = empty; copy.set(Item(8)); assert(empty.get().value == 5);
    std::attribute<optional<int>> nested = std::attribute<optional<int>>();
    assert(!nested.try_get().has_value()); nested.set(optional<int>());
    assert(nested.try_get().has_value() && !nested.get().has_value());
    std::attribute<fn()->int> callback = result; fn()->int f = callback;
    assert(f() == 17 && callback.get()() == 17);
    return 0;
})VS");
	Ok &= Run("bool conversions and short circuit", R"VS(
import "attribute.vs";
int reads = 0;
bool read() { reads += 1; return true; }
int main() {
    std::attribute<bool> a = true; a.bind(read);
    if (a) { assert(reads == 1); }
    assert(a && true); assert(reads == 2);
    assert(true || a); assert(reads == 2);
    assert(!(false && a)); assert(reads == 2);
    assert(!(!a)); assert(reads == 3);
    int value = a ? 7 : 9; assert(value == 7 && reads == 4);
    return 0;
})VS");
	Ok &= Run("weak getter and weak name", R"VS(
import "attribute.vs";
class Model { public: int value=5; int read() const { return value; } };
int read(Model m) { return m.value; }
int main() {
    Model model = Model(); weak<Model> observer = weak<Model>(model);
    auto a = std::make_weak_attribute<int>(observer, read);
    auto b = std::make_weak_attribute_by_name<int>(observer, "read");
    assert(a() == 5 && b() == 5); model.value = 8; assert(a() == 8 && b() == 8);
    model = nullptr; assert(!a.try_get().has_value() && !b.try_get().has_value());
    assert(a.is_bound() && b.is_bound() && b.get_or(13) == 13);
    auto expired = std::make_weak_attribute_by_name<int>(observer, "read");
    assert(expired.is_bound() && !expired.try_get().has_value());
    return 0;
})VS");
	Ok &= Run("method signatures, virtual dispatch and delegate reuse", R"VS(
import "attribute.vs";
import "delegate.vs";
class Base { public: virtual int read() const { return 3; } };
class Model : public Base {
public:
    int value=7;
    int read() const override { return value; }
    int adjust(ref int n) { n += 2; return n; }
    int choose(int n) { return n+value; }
    string choose(string n) { return n; }
    int& reference() { return value; }
    void write(int n) { value=n; }
};
int main() {
    Model model=Model(); Base base=model;
    auto a=std::make_attribute_by_name<int>(base,"read"); assert(a() == 7);
    string name="choose";
    fn(int)->int f=std::bind_method<fn(int)->int>(model,name); assert(f(2)==9);
    fn(ref int)->int modify=std::bind_method<fn(ref int)->int>(model,"adjust");
    int n=3; assert(modify(ref n)==5 && n==5);
    fn()->int& reference=std::bind_method<fn()->int&>(model,"reference");
    reference()=11; assert(a()==11);
    fn(int)->void write=std::bind_method<fn(int)->void>(model,"write"); write(14);
    std::delegate<int> d=std::delegate<int>(); d.bind(std::bind_method<fn()->int>(model,"read"));
    assert(d.execute()==14);
    model=nullptr; base=nullptr; assert(d.execute()==14 && a()==14);
    return 0;
})VS");
	Ok &= Run("shared struct current payload", R"VS(
import "attribute.vs";
struct Counter { public: int value=2; int read() const { return value; } int increment() { value+=1; return value; } };
int main() {
    shared<Counter> target=shared<Counter>(Counter());
    auto strong=std::make_attribute_by_name<int>(target,"read");
    auto weak=std::make_weak_attribute_by_name<int>(weak<shared<Counter>>(target),"read");
    fn()->int increment=std::bind_method<fn()->int>(target,"increment");
    assert(increment()==3 && target.value.value==3 && strong()==3 && weak()==3);
    Counter replacement=Counter(); replacement.value=19; target.value=replacement;
    assert(strong()==19 && weak()==19 && increment()==20 && target.value.value==20);
    strong.reset(); fn()->int idle=[]()->int{return 0;}; increment=idle; target=nullptr; assert(!weak.try_get().has_value());
    return 0;
})VS");
	Ok &= Run("reentrant reader keeps snapshot", R"VS(
import "attribute.vs";
class Owner {public: std::attribute<int> value;};
Owner owner=Owner();
int read(){owner.value.set(21);return 9;}
int main(){owner.value.bind(read);assert(owner.value.get()==9);assert(owner.value.get()==21);return 0;}
)VS");
	Ok &= Run("configuration published before destructor reentry", R"VS(
import "attribute.vs";
class Owner {public: std::attribute<Item> value;};
Owner owner=Owner();
int releases=0;
class Item {public: ~Item(){releases+=1;assert(owner.value.is_bound());owner.value.reset();}};
Item read(){return nullptr;}
int main(){owner.value.set(Item());owner.value.bind(read);assert(releases==1);assert(!owner.value.is_set());return 0;}
)VS");
	Ok &= Run("generic and interface methods", R"VS(
import "attribute.vs";
interface Readable { virtual int read() const = 0; };
class Box<T>:public Readable {public:T value;Box(T v){value=v;}int read() const override{return cast<int>(value);}};
int main(){Box<int> box=Box<int>(15);Readable view=box;auto a=std::make_attribute_by_name<int>(view,"read");assert(a()==15);auto b=std::make_attribute_by_name<int>(box,"read");assert(b()==15);return 0;}
)VS");
	Ok &= Run("budget propagates out of try_get", R"VS(
import "attribute.vs";
class Loop {public:int read(){while(true){}return 0;}};
int main(){Loop target=Loop();auto a=std::make_attribute_by_name<int>(target,"read");a.try_get();return 0;}
)VS",
	    EExpected::RuntimeError, "", 2000);
	Ok &= Run("bound method script exception propagates", R"VS(
import "attribute.vs";
class Model {public:int read(){throw 7;}};
int main(){Model model=Model();auto a=std::make_attribute_by_name<int>(model,"read");int caught=0;try{a.try_get();}catch(int n){caught=n;}assert(caught==7);return 0;}
)VS");
	Ok &= Run("bound method exit propagates", R"VS(
import "attribute.vs";
class Model {public:int read(){exit(0);assert(false);return 0;}};
int main(){Model model=Model();auto a=std::make_attribute_by_name<int>(model,"read");a.get();assert(false);return 0;}
)VS");
	Ok &= Run("static receiver and overload qualifiers", R"VS(
import "callable.vs";
class Base{public:int read(){return 1;}int qualified(){return 2;}int qualified() const{return 3;}int add(int n=9){return n+1;}};
class Derived:public Base{public:int read(){return 4;}};
int main(){Derived target=Derived();auto base=std::bind_method<fn()->int,Base>(target,"read");assert(base()==1);auto derived=std::bind_method<fn()->int>(target,"read");assert(derived()==4);const Base frozen=target;auto c=std::bind_method<fn()->int>(frozen,"qualified");assert(c()==3);auto m=std::bind_method<fn()->int>(target,"qualified");assert(m()==2);auto add=std::bind_method<fn(int)->int>(target,"add");assert(add(4)==5);return 0;}
)VS");
	Ok &= Run("packed variadic signature", R"VS(
import "callable.vs";
class Model{public:int total(int... values){int sum=0;for(int value:values){sum+=value;}return sum;}};
int main(){Model target=Model();auto f=std::bind_method<fn(array<int>)->int>(target,"total");array<int> values={2,3};assert(f(values)==5);return 0;}
)VS");
	Ok &= Run("fixed null value remains set", R"VS(
import "attribute.vs";
class Model{};
int main(){std::attribute<Model> a=nullptr;assert(a.is_set() && a.try_get().has_value() && a.get()==nullptr);return 0;}
)VS");
	Ok &= Run("empty strict get", R"VS(import "attribute.vs"; int main(){std::attribute<int> a=std::attribute<int>();a.get();return 0;})VS",
	    EExpected::RuntimeError, "std::attribute has no value");
	Ok &= Run("getter error propagates", R"VS(import "attribute.vs"; int main(){std::attribute<int> a=1;a.bind([]()->int{assert(false,"getter failed");return 0;});a.try_get();return 0;})VS",
	    EExpected::RuntimeError, "getter failed");
	Ok &= Run("dynamic signature rejection", R"VS(import "attribute.vs";class M{public:int read(int x){return x;}};int main(){M m=M();string n="read";std::make_attribute_by_name<int>(m,n);return 0;})VS",
	    EExpected::RuntimeError, "no public instance method");
	Ok &= Run("static signature rejection", R"VS(import "attribute.vs";class M{public:int read(int x){return x;}};int main(){M m=M();std::make_attribute_by_name<int>(m,"read");return 0;})VS",
	    EExpected::CompileError, "no public instance method");
	Ok &= Run("const receiver rejection", R"VS(import "attribute.vs";class M{public:int read(){return 1;}};int main(){const M m=M();std::make_attribute_by_name<int>(m,"read");return 0;})VS",
	    EExpected::CompileError, "no public instance method");
	Ok &= Run("private receiver rejection", R"VS(import "attribute.vs";class M{private:int read(){return 1;}};int main(){M m=M();std::make_attribute_by_name<int>(m,"read");return 0;})VS",
	    EExpected::CompileError, "no public instance method");
	Ok &= Run("numeric attribute is not bool", R"VS(import "attribute.vs";int main(){std::attribute<int> a=1;if(a){}return 0;})VS",
	    EExpected::CompileError);
	Ok &= Run("bare struct rejection",
	    R"VS(import "attribute.vs";struct S{public:int read(){return 1;}};int main(){S s=S();std::make_attribute_by_name<int>(s,"read");return 0;})VS",
	    EExpected::CompileError, "value-type method binding requires");
	return Ok ? 0 : 1;
}
