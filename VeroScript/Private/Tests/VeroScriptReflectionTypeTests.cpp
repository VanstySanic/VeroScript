// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"

#include <iostream>
#include <sstream>
#include <string>
#include <utility>

using namespace VeroScript;
namespace
{
	int Failures = 0;
	void Expect(const bool Success, const std::string& Message)
	{
		if (!Success)
		{
			++Failures;
			std::cerr << "FAIL: " << Message << '\n';
		}
	}
	bool Run(const FBytecodeModule& Module, const std::string& Expected, const EExecutionBackend Backend,
		const bool ExpectRuntimeFailure = false)
	{
		std::ostringstream Out, Err;
		std::istringstream In;
		VeroScriptVm Vm(Out, In, Err);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionBackend(Backend);
		const bool Success = Vm.Execute(Module, ERunMode::Script);
		if (ExpectRuntimeFailure)
		{
			return !Success && Vm.GetError().Message.find(Expected) != std::string::npos;
		}

		if (!Success || Out.str() != Expected)
		{
			std::cerr << Vm.GetError().Message << "\noutput: " << Out.str() << '\n';
			return false;
		}
		return true;
	}
	void Check(const std::string& Source, const std::string& Expected, const std::string& Name,
		const bool ExpectRuntimeFailure = false)
	{
		auto Result = VeroScriptCompile::CompileSource(Source, Name + ".vs");
		Expect(Result.bSuccess, Name + ": compile");
		if (!Result.bSuccess)
		{
			std::cerr << Result.Error.Message << '\n';
			return;
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		Expect(VeroScriptBytecodeIO::SerializeBytecode(*Result.Module, 19, Bytes, Error), Name + ": serialize");
		auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 19, Error);
		Expect(bool(Loaded), Name + ": load bytecode: " + Error);
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed})
		{
			Expect(Run(*Result.Module, Expected, Backend, ExpectRuntimeFailure), Name + ": execute source");
			if (Loaded)
			{
				Expect(Run(*Loaded, Expected, Backend, ExpectRuntimeFailure),
					Name + ": execute loaded bytecode");
			}
		}
	}
	std::string ReplaceElement(const std::string& Shape, const std::string& Element)
	{
		const auto Position = Shape.find("ELEMENT");
		return Shape.substr(0, Position) + Element + Shape.substr(Position + 7);
	}
}

int main()
{
	for (const auto& Types : {std::pair<std::string, std::string>{"long", "int64"},
		std::pair<std::string, std::string>{"unsigned long", "uint64"},
		std::pair<std::string, std::string>{"int64", "long"},
		std::pair<std::string, std::string>{"uint64", "unsigned long"}})
	{
		const auto& [Written, Equivalent] = Types;
		Check("void main(){fn(" + Written + ")->" + Written + " original=[](" + Written + " x)->" + Written +
			"{return x;};fn(" + Equivalent + ")->" + Equivalent + " alias=original;print(alias(cast<" +
			Equivalent + ">(7)));}", "7\n", "callable-alias-assignment-" + Written);
		for (const std::string Shape : {"array<ELEMENT>", "array<array<ELEMENT>>", "map<string,array<ELEMENT>>",
			"multimap<string,ELEMENT>", "ordered_map<string,ELEMENT>", "ordered_multimap<string,ELEMENT>",
			"queue<ELEMENT>", "priority_queue<ELEMENT>", "stack<ELEMENT>", "deque<ELEMENT>"})
		{
			const std::string Declared = ReplaceElement(Shape, Written);
			const std::string Requested = ReplaceElement(Shape, Equivalent);
			Check("struct Value{" + Declared + " value=" + Declared + "();" + Declared +
				" Read() const{return value;}" + Declared + "& Ref(){return value;}const " + Declared +
				"& ConstRef() const{return value;}void Take(" + Declared + " v){value=v;print(1);}void TakeRef(" +
				Declared + "& v){v=value;print(2);}};void main(){Value value=Value();type<Value> t=type<Value>();" +
				Requested + " input=" + Requested + "();field_info<Value> field(t.qualified_name,0);" + Requested +
				" snapshot=field.get<" + Requested + ">(value);field.set<" + Requested + ">(value,input);"
				"for(int i=0;i<t.method_count();++i){method_info<Value> m(t.qualified_name,i);"
				"if(m.name()==\"Read\"){" + Requested + " r=m.invoke<" + Requested + ">(value);print(3);}"
				"if(m.name()==\"Ref\"){" + Requested + "& r=m.invoke<" + Requested + "&>(value);r=input;print(4);}"
				"if(m.name()==\"ConstRef\"){const " + Requested + "& r=m.invoke<const " + Requested + "&>(value);print(5);}"
				"if(m.name()==\"Take\")m.invoke<void>(value,input);"
				"if(m.name()==\"TakeRef\")m.invoke<void>(value,&input);}print(6);}",
				"3\n4\n5\n1\n2\n6\n", "container-alias-" + Declared);
		}
	}
	Check(R"(
struct Value {
    array<long> values=array<long>();
    array<long>& Ref(){return values;}
    void Push(array<long>& target){target.push(9i64);}
};
void main(){
    Value value=Value(); value.values.push(7i64);
    type<Value> t=type<Value>();field_info<Value> field(t.qualified_name,0);
    array<int64> snapshot=field.get<array<int64>>(value);print(snapshot[0]);
    array<int64> replacement=array<int64>();replacement.push(8i64);
    field.set<array<int64>>(value,replacement);
    for(int i=0;i<t.method_count();++i){
        method_info<Value> m(t.qualified_name,i);
        if(m.name()=="Ref"){array<int64>& r=m.invoke<array<int64>&>(value);r.push(10i64);}
        if(m.name()=="Push")m.invoke<void>(value,&replacement);
    }
    print(value.values[0]);print(value.values[1]);print(replacement[2]);
})", "7\n8\n10\n9\n", "reflected-array-alias-storage");
	for (const std::string Type : {"multiset<int>", "ordered_multiset<int>"})
	{
		Check("struct S{void Take(" + Type + " v){print(1);}void TakeRef(" + Type +
				"& v){print(2);}};"
				"void main(){S s=S();" +
				Type + " v=" + Type +
				"();type<S> t=type<S>();"
				"for(int i=0;i<t.method_count();++i){method_info<S> m(t.qualified_name,i);"
				"if(m.name()==\"Take\")m.invoke<void>(s,v);if(m.name()==\"TakeRef\")m.invoke<void>(s,&v);}}",
			"1\n2\n", "reflected-" + Type);
	}

	Check(R"(
long Increment(long x){return x+1i64;}
struct Value{
    fn(long)->long callback;
    fn(long)->long Read(){return callback;}
    void Take(fn(long)->long input){print(input(3i64));}
};
void main(){Value value=Value();value.callback=Increment;type<Value> t=type<Value>();field_info<Value> field(t.qualified_name,0);
    fn(int64)->int64 fnc=field.get<fn(int64)->int64>(value);print(fnc(1i64));
    field.set<fn(int64)->int64>(value,fnc);
    for(int i=0;i<t.method_count();++i){method_info<Value> m(t.qualified_name,i);
        if(m.name()=="Read"){fn(int64)->int64 f=m.invoke<fn(int64)->int64>(value);print(f(2i64));}
        if(m.name()=="Take")m.invoke<void>(value,fnc);}
})",
		"2\n3\n4\n", "reflected-callable-alias");
	Check("void main(){fn(ref long)->void original=[](long& n)->void{n+=1i64;};"
		"fn(ref int64)->void alias=original;int64 n=7;alias(ref n);print(n);}",
		"8\n", "callable-alias-reference-storage");
	Check(R"(
struct Value{void Take(long numbers[2][3]){print(numbers[1][2]);}};
void main(){Value value=Value();type<Value> t=type<Value>();
    int64 numbers[2][3]={{1i64,2i64,3i64},{4i64,5i64,6i64}};
    for(int i=0;i<t.method_count();++i){method_info<Value> m(t.qualified_name,i);
        if(m.name()=="Take")m.invoke<void>(value,numbers);}}
)", "6\n", "reflected-native-array-alias");
	Check("void main(){priority_queue<unsigned long> q=priority_queue<unsigned long>();"
		"q.push(cast<unsigned long>(2));q.push(cast<unsigned long>(7));print(q.top());q.pop();print(q.top());}",
		"7\n2\n", "unsigned-long-priority-queue");
	for (const std::string OwnerKind : {"class", "interface"})
	{
		Check(OwnerKind +
				" Base{public:virtual array<long> operator array<long>()=0;};"
				"class Derived:public Base{public:array<int64> operator array<int64>() override{"
				"array<int64> r=array<int64>();r.push(2i64);return r;}};void main(){Derived d=Derived();"
				"type<Base> t=type<Base>();for(int i=0;i<t.method_count();++i){method_info<Base> m(t.qualified_name,i);"
				"if(m.name()==\"operator conv array<long>\"){array<int64> r=m.invoke<array<int64>>(d);print(r[0]);}}}",
			"2\n", "reflected-container-conversion-" + OwnerKind);
	}

	Check("struct Value{array<long> values=array<long>();};void main(){Value v=Value();"
		  "field_info<Value> f(type<Value>().qualified_name,0);array<uint64> a=f.get<array<uint64>>(v);}",
		"different type", "reject-container-signedness", true);
	Check("struct A{int n=1;};struct B{int n=1;};struct Value{array<A> values=array<A>();};"
		"void main(){Value v=Value();field_info<Value> f(type<Value>().qualified_name,0);"
		"array<B> a=f.get<array<B>>(v);}", "different type", "reject-container-object-identity", true);
	Check("struct Value{map<string,int> values=map<string,int>();};void main(){Value v=Value();"
		"field_info<Value> f(type<Value>().qualified_name,0);multimap<string,int> a=f.get<multimap<string,int>>(v);}",
		"different type", "reject-container-kind", true);
	Check("struct Value{void Take(long values[2][3]){}};void main(){Value v=Value();"
		"type<Value> t=type<Value>();int64 a[3][2];for(int i=0;i<t.method_count();++i){"
		"method_info<Value> m(t.qualified_name,i);if(m.name()==\"Take\")m.invoke<void>(v,a);}}",
		"type mismatch", "reject-array-dimension-order", true);
	Check("struct Value{array<long> values=array<long>();const array<long>& Read() const{return values;}};"
		"void main(){Value v=Value();type<Value> t=type<Value>();for(int i=0;i<t.method_count();++i){"
		"method_info<Value> m(t.qualified_name,i);if(m.name()==\"Read\")m.invoke<array<int64>&>(v);}}",
		"incompatible signature", "reject-removing-ref-const", true);
	for (const std::string Target : {"fn(uint64)->uint64", "fn(int)->int", "fn(long,long)->long", "fn(ref long)->long"})
	{
		auto Invalid = VeroScriptCompile::CompileSource(
			"void main(){fn(long)->long f=[](long n)->long{return n;};" + Target + " g=f;}", "invalid-callable-signature.vs");
		Expect(!Invalid.bSuccess, "callable aliases never relax signedness, width, arity or ref qualifiers: " + Target);
	}
	{
		auto Invalid = VeroScriptCompile::CompileSource(
			"void main(){fn(const long&)->long f=[](const long& n)->long{return n;};"
			"fn(int64&)->int64 g=f;}", "invalid-callable-const.vs");
		Expect(!Invalid.bSuccess, "callable alias assignment preserves const-reference parameter qualifiers");
	}
	for (const auto& Types : {std::pair<std::string, std::string>{"long", "int64"},
		std::pair<std::string, std::string>{"unsigned long", "uint64"},
		std::pair<std::string, std::string>{"int64", "long"},
		std::pair<std::string, std::string>{"uint64", "unsigned long"}})
	{
		FHostBindingRegistry Bindings;
		std::string Error;
		Expect(Bindings.RegisterNative("Accept", ENativeType::Int, {ENativeType::Array},
				   [](const FNativeCallContext& Context, FValue& Out, std::string&)
				   {
					   const auto* Handle = Context.ArgCount == 1
						   ? std::get_if<FDynamicArrayHandle>(&Context.Args[0])
						   : nullptr;
					   if (!Handle || !*Handle || (*Handle)->Elements.size() != 1) return false;
					   Out = std::int32_t{7};
					   return true;
				   },
				   nullptr, Error, {}, {}, {}, {Types.second}),
			"register native array consumer");
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = &Bindings;
		auto Result = VeroScriptCompile::CompileSource("void main(){array<" + Types.first + "> a=array<" +
			Types.first + ">();a.push(cast<" + Types.first + ">(7));print(Accept(a));}", "native-array-alias.vs", Options);
		Expect(Result.bSuccess, "compile native array alias argument: " + Result.Error.Message);
		if (Result.bSuccess)
		{
			std::ostringstream Out;
			VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
			const bool Success = Vm.Execute(*Result.Module, ERunMode::Script, {}, &Bindings.GetNativeFunctions());
			Expect(Success && Out.str() == "7\n", "native array alias argument: " + Vm.GetError().Message);
		}
	}
	for (const auto& Types : {std::pair<std::string, std::string>{"long", "int64"},
		std::pair<std::string, std::string>{"unsigned long", "uint64"},
		std::pair<std::string, std::string>{"int64", "long"},
		std::pair<std::string, std::string>{"uint64", "unsigned long"}})
	{
		for (const bool bRef : {false, true})
		{
			FHostBindingRegistry Bindings;
			std::string Error;
			const auto Signature = "fn(" + Types.second + ")->" + Types.second;
			Expect(Bindings.RegisterNative(
					   "Echo", FNativeTypeDesc::Function(Signature),
					   {{FNativeTypeDesc::Function(Signature), bRef ? NativeParamFlagRef : std::uint8_t{0}}},
					   [](const FNativeCallContext& Context, FValue& Out, std::string&)
					   {
						   if (Context.ArgCount != 1) return false;
						   Out = Context.Args[0];
						   return true;
					   },
					   nullptr, Error),
				"register native callable identity: " + Error);
			FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
			Options.Bindings = &Bindings;
			auto Result = VeroScriptCompile::CompileSource("void main(){fn(" + Types.first + ")->" + Types.first +
				" f=[](" + Types.first + " x)->" + Types.first + "{return x;};fn(" + Types.second + ")->" + Types.second +
				" g=Echo(" + (bRef ? "ref f" : "f") + ");print(g(cast<" + Types.second + ">(7)));}",
				"native-callable-alias.vs", Options);
			Expect(Result.bSuccess, "compile native callable alias: " + Result.Error.Message);
			if (Result.bSuccess)
			{
				std::ostringstream Out;
				VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
				const bool Success = Vm.Execute(*Result.Module, ERunMode::Script, {}, &Bindings.GetNativeFunctions());
				Expect(Success && Out.str() == "7\n", "native callable alias round-trip: " + Vm.GetError().Message);
			}
		}
	}
	return Failures ? 1 : 0;
}
