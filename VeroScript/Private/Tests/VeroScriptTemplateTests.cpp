// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptTemplate.h"
#include "VeroScriptTemplateQuery.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAst.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptPackage.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptTestTemporaryDirectory.h"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
using namespace VeroScript;

namespace
{
	int Benchmark()
	{
		const std::pair<const char *, std::string> Workloads[] = {
		    {"type_only", "template<typename T> struct Box{T value;T read(){return value;}};void main(){Box<int> "
		                  "value=Box<int>();print(value.read());}"},
		    {"method", "class Box{public:template<typename T> T read(T value){return value;}};void main(){Box "
		               "value=Box();print(value.read(7));}"},
		    {"equivalent_values", "template<typename T,int N> struct Box{T data[N];};void main(){Box<int,3> "
		                          "first=Box<int,3>();Box<int,(1+2)> second=Box<int,(1+2)>();}"},
		    {"value_instances", []
		{
			std::string Source = "template<int N> struct Box{int data[N];int size(){return N;}};void main(){";
			for (int I = 1; I <= 32; ++I)
			{
				Source += "Box<" + std::to_string(I) + "> value" + std::to_string(I) + "=Box<" +
				    std::to_string(I) + ">();";
			}
			return Source + "}";
		}()}};
		for (const auto &Workload : Workloads)
		{
			const auto Start = std::chrono::steady_clock::now();
			FCompileResult Last;
			for (int Iteration = 0; Iteration < 20; ++Iteration)
			{
				Last = VeroScriptCompile::CompileSource(Workload.second, "template-benchmark.vs");
				if (!Last.bSuccess)
				{
					std::cerr << Last.Error.Message << '\n';
					return 1;
				}
			}
			const auto Cold =
			    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count() / 20;
			VeroScriptCompiler Compiler;
			const auto PreparedStart = std::chrono::steady_clock::now();
			for (int Iteration = 0; Iteration < 20; ++Iteration)
			{
				if (!Compiler.Compile(Last.PreparedProgram->GetProgram()))
				{
					return 1;
				}
			}
			std::cout
			    << Workload.first << " cold_ms=" << Cold << " prepared_ms="
			    << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - PreparedStart).count() /
			        20
			    << " types=" << Last.Module->Types.size() << " functions=" << Last.Module->Functions.size() << '\n';
		}
		return 0;
	}

	std::string BackendPath;
	std::uint64_t BackendDispatches = 0, NativeEntries = 0;

	bool Expect(const bool Condition, const std::string &Message)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Condition;
	}

	FTemplateEvaluationResult Evaluate(const std::string &Source, const FTemplateEvaluationContext &Context = {})
	{
		VeroScriptLexer Lexer(Source, "constant.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "constant.vs");
		auto Expression = Parser.ParseExpressionFragment({}, {});
		if (!Expression || Lexer.HasError() || Parser.HasError())
		{
			std::cerr << "parse failed: " << Source << '\n';
			return {};
		}
		return EvaluateTemplateConstant(*Expression, Context);
	}

	bool Execute(const FBytecodeModule &Module, const std::string &Expected)
	{
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!VeroScriptBytecodeIO::SerializeBytecode(Module, 97, Bytes, Error))
		{
			return Expect(false, Error);
		}
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && BackendPath.empty())
			{
				continue;
			}
			auto Copy = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 97, Error);
			if (!Copy)
			{
				return Expect(false, Error);
			}
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			if (!BackendPath.empty() && !Vm.SetJitBackendPath(BackendPath, Error))
			{
				return Expect(false, Error);
			}
			Vm.SetExecutionBackend(Backend);
			FJitPolicy Policy;
			Policy.MixedHotCallThreshold = 1;
			Policy.EagerCompileBudget = 256;
			Vm.SetJitPolicy(Policy);
			if (!Vm.Execute(*Copy, ERunMode::Script) || Output.str() != Expected)
			{
				return Expect(false, "template execution: " + Vm.GetError().Message + "; output: " + Output.str());
			}
			const auto Stats = Vm.GetJitStats();
			BackendDispatches += Stats.Dispatches;
			NativeEntries += Stats.NativeLeafEntries + Stats.NativeCfgEntries;
		}
		return true;
	}

	bool Run(const std::string &Source, const std::string &Expected)
	{
		VeroScriptLexer Lexer(Source, "template.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "template.vs");
		auto Program = Parser.ParseProgram();
		if (!Program || Lexer.HasError() || Parser.HasError())
		{
			return Expect(false, std::string("template parse at ") + std::to_string(Parser.GetError().Location.Column) + ": " + Parser.GetError().Message + "\n" + Source);
		}
		VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Module)
		{
			return Expect(false, std::string("template compile at ") + std::to_string(Compiler.GetError().Location.Column) + ": " + Compiler.GetError().Message + "\n" + Source);
		}
		return Execute(*Module, Expected) || Expect(false, Source);
	}

	bool Reject(const std::string &Source, const std::string &Diagnostic)
	{
		VeroScriptLexer Lexer(Source, "reject-template.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "reject-template.vs");
		auto Program = Parser.ParseProgram();
		if (Parser.HasError() || !Program)
		{
			return Expect(Parser.GetError().Message.find(Diagnostic) != std::string::npos,
			    "expected diagnostic '" + Diagnostic + "': " + Parser.GetError().Message);
		}
		VeroScriptCompiler Compiler;
		const auto Module = Compiler.Compile(*Program);
		return Expect(!Module && Compiler.GetError().Message.find(Diagnostic) != std::string::npos,
		    "expected diagnostic '" + Diagnostic + "': " + Compiler.GetError().Message);
	}

	bool CompileOnly(const std::string &Source)
	{
		auto Result = VeroScriptCompile::CompileSource(Source, "constexpr-compile-only.vs");
		return Expect(Result.bSuccess, "constexpr compile-only: " + Result.Error.Message + "\n" + Source);
	}

	bool Library(const std::string &, const std::string &, const std::string &);
	bool GenericClassReferences()
	{
		bool Ok = true;
		Ok &= Run(R"VS(
class Link<T>{public:T value;Link<T> next;
    void Set(Link<T> item){next=item;} Link<T> Get(){return next;}};
void main(){Link<int> first=Link<int>();Link<int> second=Link<int>();
    second.value=42;print(first.next==nullptr);first.Set(second);
    print(first.Get().value);first.next=nullptr;}
)VS", "true\n42\n");
		Ok &= Run(R"VS(
int destroyed=0;
class Link<T>{public:T value;Link<T> next=nullptr;
    Link(T item){value=item;}~Link(){destroyed+=1;}};
void main(){Link<int> first=Link<int>(7);Link<int> second=Link<int>(42);
    first.next=second;second=nullptr;print(destroyed);print(first.next.value);
    first=nullptr;print(destroyed);}
)VS", "0\n42\n2\n");
		Ok &= Run(R"VS(
namespace N{template<typename T=int,int Tag=2> class Link{public:
    using Self=Link<T,Tag>;Self next;int value=Tag;Self Get(){return next;}};}
void main(){N::Link<> first=N::Link<>();N::Link<int,2> second=N::Link<int,2>();
    first.next=second;print(first.Get().value);first.next=nullptr;}
)VS", "2\n");
		Ok &= Run("template<typename T,typename U=int> struct Node{U value;};"
		          "class Node<T>{public:T value;Node<T> next;};"
		          "void main(){Node<int> first=Node<int>();Node<int> second=Node<int>();"
		          "second.value=42;first.next=second;print(first.next.value);}", "42\n");
		Ok &= Run("interface IHasId{virtual int id()=0;};"
		          "class Item:public IHasId{public:int id(){return 7;}};"
		          "class Link<T> where T:IHasId{public:T value;Link<T> next;int Read(){return value.id();}};"
		          "void main(){Link<Item> first=Link<Item>();Link<Item> second=Link<Item>();"
		          "second.value=Item();first.next=second;print(first.next.Read());}", "7\n");
		Ok &= Run(R"VS(
class Node<T>{public:T value;array<Node<T>> children=array<Node<T>>();
    optional<Node<T>> selected;weak<Node<T>> parent;};
void main(){Node<int> first=Node<int>();Node<int> second=Node<int>();
    first.value=7;second.value=42;first.children.push(second);
    first.selected=optional<Node<int>>(second);second.parent=weak<Node<int>>(first);
    print(first.children[0].value);print(first.selected.get().value);
    print(second.parent.lock().value);}
)VS", "42\n42\n7\n");
		Ok &= Run(R"VS(
class Link<T>{public:T value;Link<T> next;};
class Derived<T>:public Link<T>{public:int Read(){return value;}};
void main(){Derived<int> first=Derived<int>();Derived<int> second=Derived<int>();
    second.value=42;first.next=second;print(first.next.value);print(second.Read());
    first.next=nullptr;}
)VS", "42\n42\n");
		Ok &= Run(R"VS(
class Left<T>{public:T value;Right<T> next;};class Right<T>{public:Left<T> next;};
void main(){Left<int> first=Left<int>();Right<int> second=Right<int>();
    first.value=42;first.next=second;second.next=first;print(first.next.next.value);
    first.next=nullptr;second.next=nullptr;}
)VS", "42\n");
		Ok &= Run(R"VS(
class Link<T>{public:T value;Link<T> next;};
void main(){Link<int> first=Link<int>();Link<int> second=Link<int>();second.value=42;
    type<Link<int>> info=type<Link<int>>();
    for(int i=0;i<info.field_count();++i){field_info<Link<int>> field(info.qualified_name,i);
        if(field.name()=="next"){field.set<Link<int>>(first,second);print(field.get<Link<int>>(first).value);}}
    first.next=nullptr;}
)VS", "42\n");
		Ok &= Library("export class Link<T>{public:T value;Link<T> next;Link<T> Get(){return next;}};",
		    "Link<int> first=Link<int>();Link<int> second=Link<int>();second.value=42;"
		    "first.next=second;print(first.Get().value);first.next=nullptr;", "42\n");
		Ok &= Reject("struct Bad<T>{Bad<T> next;};void main(){Bad<int> value=Bad<int>();}",
		    "recursive generic instantiation");
		Ok &= Reject("struct Left<T>{Right<T> next;};struct Right<T>{Left<T> next;};"
		             "void main(){Left<int> value=Left<int>();}", "recursive generic instantiation");
		Ok &= Reject("class Bad<T>:public Bad<T>{};void main(){Bad<int> value=Bad<int>();}",
		    "cyclic inheritance");
		Ok &= Reject("class Left<T>:public Right<T>{};class Right<T>:public Left<T>{};"
		             "void main(){Left<int> value=Left<int>();}", "cyclic inheritance");
		Ok &= Reject("class Bad<T>{public:Bad<array<T>> next;};void main(){Bad<int> value=Bad<int>();}",
		    "nesting limit exceeded");
		Ok &= Reject("template<int N> class Bad{public:Bad<N+1> next;};"
		             "void main(){Bad<0> value=Bad<0>();}", "nesting limit exceeded");
		Ok &= Reject("interface IHasId{virtual int id()=0;};"
		             "class Item:public IHasId{public:int id(){return 7;}};class Missing{};"
		             "class Link<T> where T:IHasId{public:Link<T> next;Link<Missing> invalid;};"
		             "void main(){Link<Item> value=Link<Item>();}", "where");
		return Ok;
	}

	bool TemplateArityIsolation()
	{
		bool Ok = true;
		Ok &= Run("struct Leaf{int value=7;};"
		          "template<typename T,typename U=int> struct Box{U child;int tag=2;};"
		          "template<typename T> struct Box{T child;int tag=1;};"
		          "void main(){print(Box<Leaf>().child.value);print(Box<Leaf>().tag);"
		          "Box<Leaf> local=Box<Leaf>();print(local.child.value);print(Box<Leaf,int>().tag);}",
		    "7\n1\n7\n2\n");
		Ok &= Run("struct Leaf{int value=8;};"
		          "template<typename T> struct Box{T child;int tag=1;};"
		          "template<typename T,typename U=int> struct Box{U child;int tag=2;};"
		          "void main(){print(Box<Leaf>().child.value);print(Box<Leaf>().tag);print(Box<Leaf,int>().tag);}",
		    "8\n1\n2\n");
		Ok &= Run("struct Leaf{int value=9;};"
		          "template<typename T,typename U=int> struct Base{U child;int tag=2;};"
		          "template<typename T> struct Base{T child;int tag=1;};"
		          "template<typename T=Leaf> struct Box:public Base<T>{};"
		          "void main(){print(Box<>().child.value);print(Box<>().tag);}",
		    "9\n1\n");
		Ok &= Run("struct Leaf{int value=10;};"
		          "template<typename T,typename U=int> class Base{public:U child;int tag=2;};"
		          "template<typename T> class Base{public:T child;int tag=1;};"
		          "template<typename T=Leaf> class Box:public Base<T>{};"
		          "void main(){print(Box<>().child.value);print(Box<>().tag);}",
		    "10\n1\n");
		Ok &= Run("struct Leaf{int value=11;};"
		          "template<typename T,typename U=int> class Box{public:U child;};"
		          "template<typename T> struct Box{T child;};"
		          "void main(){print(Box<Leaf>().child.value);}",
		    "11\n");
		Ok &= Run("struct Leaf{int value=12;};"
		          "template<typename T,int Count=2> struct Box{T items[Count];};"
		          "template<typename T> struct Box{T child;};"
		          "void main(){print(Box<Leaf>().child.value);print(len(Box<Leaf,3>().items));}",
		    "12\n3\n");
		Ok &= Library("export struct Leaf{int value=13;};"
		              "export template<typename T,typename U=int> struct Box{U child;};"
		              "export template<typename T> struct Box{T child;};",
		    "print(Box<Leaf>().child.value);", "13\n");
		Ok &= Reject("template<typename T,typename U=int> struct Box{U child;};"
		             "template<typename T,typename U=int,typename V=int> struct Box{V child;};"
		             "void main(){Box<int> value=Box<int>();}",
		    "ambiguous");
		return Ok;
	}

	bool NestedTemplateDefaults()
	{
		bool Ok = true;
		Ok &= Run("struct Leaf{int value=21;};template<typename T=Leaf> struct Outer{"
		          "template<typename U=T> struct Box{U child;};};"
		          "void main(){print(Outer<>::Box<>().child.value);print(Outer<>::Box<Leaf>().child.value);"
		          "Outer<>::Box<> local=Outer<Leaf>::Box<Leaf>();print(local.child.value);}",
		    "21\n21\n21\n");
		Ok &= Run("struct Leaf{int value=22;};template<typename T=Leaf> class Outer{public:"
		          "template<typename U=T> class Box{public:U child;};};"
		          "void main(){print(Outer<>::Box<>().child.value);}",
		    "22\n");
		Ok &= Run("struct Leaf{int value=23;};template<typename T=Leaf> struct Outer{"
		          "struct Box{T child;};};void main(){print(Outer<>::Box().child.value);}",
		    "23\n");
		Ok &= Run("struct Leaf{int value=24;};template<typename T=Leaf> struct Outer{"
		          "template<typename U=T> struct Middle{template<typename V=U> struct Inner{V child;};};};"
		          "void main(){print(Outer<>::Middle<>::Inner<>().child.value);}",
		    "24\n");
		Ok &= Run("namespace N{struct Leaf{int value=25;};template<typename T=Leaf> struct Outer{"
		          "template<typename U=T> struct Box{U child;};};}struct Leaf{int value=99;};"
		          "template<typename T> int read(T unused){return N::Outer<>::Box<>().child.value;}"
		          "void main(){print(read(0));}",
		    "25\n");
		Ok &= Run("struct Leaf{int value=26;};template<typename T=Leaf,int N=2> struct Outer{"
		          "template<typename U=T,int M=(N+1)> struct Box{U children[M];};};"
		          "void main(){print(Outer<>::Box<>().children[0].value);print(len(Outer<>::Box<>().children));}",
		    "26\n3\n");
		Ok &= Run("export struct T{int value=28;};template<typename U=T> struct Outer{"
		          "template<typename V=U> struct Box{V child;};};"
		          "template<typename T> int read(T unused){return Outer<>::Box<>().child.value;}"
		          "void main(){print(read(0));}",
		    "28\n");
		Ok &= Run("export struct T{int value=29;};template<typename U=T> U make(){return U();}"
		          "template<typename T> int read(T unused){return make<>().value;}"
		          "void main(){print(read(0));}",
		    "29\n");
		Ok &= Run("const int N=3;template<int Count=N> struct Box{int data[Count];};"
		          "template<int N> int read(){return len(Box<>().data);}void main(){print(read<9>());}",
		    "3\n");
		Ok &= Run("export struct T{int value=30;};template<typename U> int fixed(){return T().value;}"
		          "template<typename T> int read(T unused){return fixed<int>();}void main(){print(read(0));}",
		    "30\n");
		Ok &= Run("export struct T{int value=34;};template<typename U> U identity(U value){return value;}"
		          "template<typename T> int read(T unused){::T value=::T();return identity(value).value;}"
		          "void main(){print(read(0));}",
		    "34\n");
		Ok &= Library("export struct Leaf{int value=27;};export template<typename T=Leaf> struct Outer{"
		              "template<typename U=T> struct Box{U child;};};",
		    "print(Outer<>::Box<>().child.value);", "27\n");
		Ok &= Reject("template<typename T> struct Empty{};void main(){Empty<Missing> value=Empty<Missing>();}",
		    "unknown field type");
		Ok &= Reject("template<int N> struct Empty{};void main(){Empty<Missing> value=Empty<Missing>();}",
		    "invalid template value");
		return Ok;
	}

	bool GenericMemberChains()
	{
		bool Ok = true;
		Ok &= Run("struct Leaf{int value=7;};template<typename T=Leaf> struct Box{T child;};"
		          "void main(){print(Box<>().child.value);print(Box<Leaf>().child.value);"
		          "auto child=Box<>().child;print(child.value);"
		          "Box<Leaf> box=Box<>();print(box.child.value);}",
		    "7\n7\n7\n7\n");
		Ok &= Run("struct Leaf{int value=8;int read(){return value;}};"
		          "template<typename T=Leaf> class Box{public:T child;};"
		          "void main(){print(Box<>().child.value);print(Box<>().child.read());}",
		    "8\n8\n");
		Ok &= Run("struct Leaf{int value=9;};template<typename T> struct Inner{T child;};"
		          "template<typename T=Leaf> struct Box{Inner<T> child;};"
		          "void main(){print(Box<>().child.child.value);}",
		    "9\n");
		Ok &= Run("struct Leaf{int value=10;};template<typename T> struct Base{T child;};"
		          "template<typename T=Leaf> struct Box:public Base<T>{};"
		          "void main(){print(Box<>().child.value);}",
		    "10\n");
		Ok &= Run("struct Leaf{int value=11;};template<typename T> class Base{public:T child;};"
		          "template<typename T=Leaf> class Box:public Base<T>{};"
		          "void main(){print(Box<>().child.value);}",
		    "11\n");
		Ok &= Run("struct Leaf{int value=12;};template<typename T=Leaf,int Count=2> struct Box{T children[Count];};"
		          "void main(){print(Box<>().children[0].value);print(len(Box<>().children));}",
		    "12\n2\n");
		Ok &= Run("struct Leaf{int value=13;};template<typename T=Leaf,typename U=T> struct Box{U child;};"
		          "void main(){print(Box<>().child.value);}",
		    "13\n");
		Ok &= Run("struct Leaf{int value=14;};template<typename T=Leaf> struct Box{T child;};"
		          "template<typename T> int read(T unused){return Box<>().child.value;}"
		          "void main(){print(read(0));}",
		    "14\n");
		Ok &= Run("namespace N{struct Leaf{int value=15;};template<typename T=Leaf> struct Box{T child;};}"
		          "void main(){print(N::Box<>().child.value);}",
		    "15\n");
		Ok &= Run("struct Leaf{int value=17;};template<typename T=Leaf> struct Box{using Value=T;Value child;};"
		          "void main(){print(Box<>().child.value);}",
		    "17\n");
		Ok &= Library("export struct Leaf{int value=16;};"
		              "export template<typename T=Leaf> struct Box{T child;};",
		    "print(Box<>().child.value);", "16\n");
		Ok &= Reject("template<typename T=int> struct Box{T child;};"
		             "void main(){print(Box<>().child.value);}",
		    "unknown field");
		Ok &= Reject("class Leaf{int value=7;};template<typename T=Leaf> struct Box{T child;};"
		             "void main(){print(Box<>().child.value);}",
		    "cannot access member");
		return Ok;
	}

	bool DependentMemberAliases()
	{
		bool Ok = true;
		{
			const std::string Source = "namespace N{enum class Mode{Ready=1};template<Mode M> class Base{};"
			                           "class Table{};}struct Adapter<T,U>{static int get(){return 7;}};"
			                           "void main(){int value=0;value=Adapter<N::Base<N::Mode::Ready>,N::Table>::get();}";
			VeroScriptLexer Lexer(Source, "template-outline.vs");
			const auto Tokens = Lexer.Tokenize();
			VeroScriptParser Parser(Tokens, "template-outline.vs");
			Parser.SetDeclarationOutline(true, Source);
			const auto Program = Parser.ParseProgram();
			Ok &= Expect(Program && !Lexer.HasError() && !Parser.HasError(),
			    "qualified nested application in declaration outline: " + Parser.GetError().Message);
		}
		Ok &= Run("enum class Role:uint8{Read=1u};template<G=int> int read(const ::Role& value){"
		          "return cast<int>(value);}namespace N{enum class Role:uint8{Read=9u};"
		          "template<G=int> int global(const ::Role& value){return read<int>(ref value);}}"
		          "void main(){Role value=Role::Read;print(read<int>(ref value));print(N::global<int>(ref value));}",
		    "1\n1\n");
		Ok &= Run("struct Box<T>{T value;};template<G=int,T=int> string text(const ::Box<T>& value){"
		          "return to_string(value.value);}template<G=int,T=int> int tagged(type<::Box<T>> tag){return 7;}"
		          "void main(){Box<string> value=Box<string>();value.value=\"qualified\";"
		          "print(text<int>(ref value));print(tagged<int>(type<Box<string>>()));}",
		    "qualified\n7\n");
		Ok &= Library("export struct ImportedBox<T>{T value;};"
		              "export template<G=int,U=int> int tagged(type<::ImportedBox<U>> tag){return 7;}"
		              "export struct Adapter<T>{static int schema(){return tagged<int>(type<T>());}};",
		    "print(Adapter<ImportedBox<string>>::schema());", "7\n");
		Ok &= Library("export struct Adapter<T>{static int64 size(const T& value){return len(value);}};",
		    "array<int> values=array<int>{3,4};"
		    "print(Adapter<array<int>>::size(ref values));"
		    "map<string,array<int>> nested=map<string,array<int>>();nested[\"key\"]=values;"
		    "print(Adapter<map<string,array<int>>>::size(ref nested));",
		    "2\n1\n");
		Ok &= Run("struct Base<T>{T value;};using NamedBase=Base<int>;using Alias=NamedBase;"
		          "struct Child:Alias{int own=9;};void main(){Child value=Child();value.value=7;"
		          "print(value.value);print(value.own);}",
		    "7\n9\n");
		Ok &= Run("namespace N{const int Amount=3;template<int Count=Amount> class Base{public:int value=Count;};"
		          "interface I<T=int>{T read(){return 8;}};class Child:public Base<>,I<>{};"
		          "struct Values<T=int>{T value=7;};struct Derived:public Values<>{};}"
		          "void main(){N::Child value=N::Child();N::Derived other=N::Derived();"
		          "print(value.value);print(value.read());print(other.value);}",
		    "3\n8\n7\n");
		Ok &= Run("const int Amount=99;constexpr int adjusted(int value){return value+99;}"
		          "namespace N{const int Amount=3;constexpr int adjusted(int value){return value+1;}"
		          "template<int Count> struct Number{int value=Count;};class Record{public:"
		          "Number<adjusted(Amount)> stored;Number<adjusted(Amount)> get()const{return stored;}"
		          "void set(Number<adjusted(Amount)> value){stored=value;}};}"
		          "void main(){N::Record record=N::Record();N::Number<4> value=record.get();"
		          "record.set(value);print(record.get().value);}",
		    "4\n");
		Ok &= Run("namespace N{enum class Mode:uint8{Last=255u};template<Mode Selection> struct Tagged{"
		          "Mode selected=Selection;};}void main(){N::Tagged<N::Mode::Last> value=N::Tagged<N::Mode::Last>();"
		          "print(cast<int>(value.selected));}",
		    "255\n");
		Ok &= Run("enum class Small:int8{Value=-1};enum class Large:uint32{Value=4294967295u};"
		          "template<Small N> struct S{Small value=N;};template<Large N> struct L{Large value=N;};"
		          "void main(){S<Small::Value> s=S<Small::Value>();L<Large::Value> l=L<Large::Value>();"
		          "print(cast<int>(s.value));print(cast<uint64>(l.value));}",
		    "-1\n4294967295\n");
		Ok &= Reject("enum class Left:uint8{Value=1u};enum class Right:uint8{Value=1u};"
		             "template<Left Selection> struct Bad{Right value=Selection;};"
		             "void main(){Bad<Left::Value> value=Bad<Left::Value>();}",
		    "");
		Ok &= Run("template<int N,typename T> struct Base{int value=N;T extra;};"
		          "template<int N> struct Shifted:public Base<(N>>1),int>{};"
		          "template<int N> struct Quoted:public Base<'>',int>{};"
		          "template<int N> struct Comma:public Base<',',int>{};"
		          "void main(){Shifted<18> a=Shifted<18>();Quoted<3> b=Quoted<3>();Comma<3> c=Comma<3>();"
		          "print(a.value);print(b.value);print(c.value);}",
		    "9\n62\n44\n");
		Ok &= Run("template<int N> struct Number{int value=N;};template<int N> class Base{"
		          "Number<N*2> stored;public:Number<N*2> get()const{return stored;}"
		          "void set(Number<N*2> value){stored=value;}};template<int N> class Derived:public Base<N+1>{};"
		          "void main(){Derived<3> a=Derived<3>();Derived<9> b=Derived<9>();"
		          "auto value=a.get();Number<8> copy=value;a.set(copy);"
		          "print(a.get().value);print(b.get().value);print(a.get().value);}",
		    "8\n20\n8\n");
		Ok &= Run("namespace T{int read(){return 90;}}class Policy{public:static int read(){return 7;}};"
		          "class Other{public:static int read(){return 11;}};"
		          "template<typename T> int selected(){using A=T;typedef B=A;return A::read()+B::read();}"
		          "void main(){print(selected<Policy>());print(selected<Other>());print(selected<Policy>());}",
		    "14\n22\n14\n");
		Ok &= Run("namespace T{int value=90;int& alias(){return value;}}"
		          "class Policy{public:static int value=7;static int& alias(){return value;}};"
		          "template<typename T> int selected(){using A=T;int& value=A::alias();value+=1;return A::value;}"
		          "void main(){print(selected<Policy>());print(Policy::value);print(T::value);}",
		    "8\n8\n90\n");
		Ok &= Run("namespace T{int read(){return 90;}}class Policy{public:static int read(){return 7;}};"
		          "template<typename T> int selected(){return ::T::read()+T::read();}"
		          "void main(){print(selected<Policy>());}",
		    "97\n");
		Ok &= Run("namespace N{class T{public:struct read{int value=90;};};"
		          "class Policy{public:static int read(){return 7;}};"
		          "template<typename T> int selected(){using A=T;return T::read()+A::read();}}"
		          "void main(){print(N::selected<N::Policy>());}",
		    "14\n");
		Ok &= Run("class Policy{public:static int read(){return 7;}};struct Value{int read(){return 4;}};"
		          "template<typename T> int selected(){using A=T;{Value A=Value();return A.read();}}"
		          "void main(){print(selected<Policy>());}",
		    "4\n");
		Ok &= Run("namespace T{int read(){return 90;}}class Policy{public:static int read(){return 7;}};"
		          "template<typename T> int selected(){using A=T;auto getter=[]()->int{return A::read();};return getter();}"
		          "void main(){print(selected<Policy>());}",
		    "7\n");
		Ok &= Run("struct Policy{int read(){return 7;}};template<typename T> int selected(){using A=T;"
		          "auto getter=[](A value)->int{return value.read();};return getter(T());}"
		          "void main(){print(selected<Policy>());}",
		    "7\n");
		Ok &= Run("namespace T{const bool ready=false;}class Policy{public:static constexpr bool ready=true;};"
		          "template<typename T> int selected(){using A=T;if constexpr(A::ready)return 7;else return 9;}"
		          "void main(){print(selected<Policy>());}",
		    "7\n");
		Ok &= Reject("namespace T{int read(){return 90;}}template<typename T> int selected(){using A=T;return A::read();}"
		             "void main(){print(selected<int>());}",
		    "");
		Ok &= Reject("namespace T{int read(){return 90;}}class Policy{private:static int read(){return 7;}};"
		             "template<typename T> int selected(){using A=T;return A::read();}void main(){print(selected<Policy>());}",
		    "access");
		Ok &= Library("namespace T{int read(){return 90;}}export class Policy{public:static int read(){return 7;}};"
		              "export template<typename T> int selected(){using A=T;return A::read();}",
		    "print(selected<Policy>());", "7\n");
		return Ok;
	}

	bool ConstantBranches()
	{
		bool Ok = true;
		Ok &= Run("void main(){if constexpr(true){print(1);}else{int invalid=missing();}if constexpr(false){int bad=1/0;}else if constexpr(2+2==4)print(2);else missing();}", "1\n2\n");
		Ok &= Run("template<int N> int sum(){if constexpr(N==0)return 0;else return N+sum<N-1>();}void main(){print(sum<5>());print(sum<0>());}", "15\n0\n");
		Ok &= Run("template<typename T,bool Member> int read(T value){if constexpr(Member)return value.read();else return value;}struct C{int read(){return 8;}};void main(){print(read<C,true>(C()));print(read<int,false>(3));}", "8\n3\n");
		Ok &= Run("template<bool Enabled> struct C{template<int N> int read(){if constexpr(Enabled){if constexpr(N>0)return N;else return 4;}else return 9;}};void main(){C<true> a=C<true>();C<false> b=C<false>();print(a.read<3>());print(a.read<0>());print(b.read<1>());}", "3\n4\n9\n");
		Ok &= Run("constexpr int bump(int n){return n+1;}void main(){constexpr auto a=bump(2);const int b=a+1;if constexpr(b==4){int b=7;print(b);}else{constexpr int invalid=1/0;}print(b);}", "7\n4\n");
		Ok &= Run("constexpr int choose(int n){if constexpr(true){return n+1;}else{print(n);throw 1;}}template<int N> constexpr int sum(){if constexpr(N==0)return 0;else return N+sum<N-1>();}void main(){constexpr int x=choose(2)+sum<4>();print(x);print(choose(6));}", "13\n7\n");
		Ok &= Run("void main(){auto f=[]()->int{constexpr int n=2;if constexpr(n==2)return 5;else return missing();};print(f());}", "5\n");
		Ok &= Run("template<bool Flag> int run(){int unused=8;auto f=[]()->int{if constexpr(Flag)return 2;else return unused;};return f();}void main(){print(run<true>());}", "2\n");
		Ok &= Run("int Released=0;struct C{~C(){Released+=1;}};void work(){for(int i=0;i<3;++i){if constexpr(true){C value=C();if(i<2)continue;break;}else missing();}}void main(){work();print(Released);}", "3\n");
		Ok &= Run("void main(){switch(1){case 1:if constexpr(false){fallthrough;}print(2);break;default:break;}}", "2\n");
		Ok &= Run("interface I{virtual int read()=0;}class A<T> where T:I{public:using Alias=int;};template<bool Flag> int get(){if constexpr(Flag){A<int>::Alias value=1;return value;}else return 2;}void main(){print(get<false>());}", "2\n");
		Ok &= Run("struct Ready{static constexpr bool enabled=true;};struct Disabled{static constexpr bool enabled=false;};template<typename Policy> int test(){if constexpr(Policy::enabled)return 3;else return 6;}void main(){print(test<Ready>());print(test<Disabled>());}", "3\n6\n");
		Ok &= Run("void main(){if constexpr(false){goto missing_label;}else print(1);}", "1\n");
		Ok &= CompileOnly("constexpr int spin(){while(true){}return 0;}void main(){if constexpr(false){constexpr int n=spin();int values[0];}else {}} ");
		Ok &= Run("void main(){if constexpr(false){struct Bad{int values[0];};enum class E{Value=1/0};}else print(1);}", "1\n");
		Ok &= Run("void main(){if constexpr(true){struct Good{int value=7;int read(){return value;}};Good item=Good();print(item.read());}else missing();}", "7\n");
		Ok &= Reject("void main(){if constexpr(true){struct Bad{int values[0];};}}", "array");
		Ok &= Run("interface I{virtual int read()=0;}class A<T> where T:I{public:using Alias=int;};template<bool Flag> int get(){if constexpr(Flag) A<int>::Alias value=1;return 2;}void main(){print(get<false>());}", "2\n");
		Ok &= Run("class Owner{private:struct Hidden{};};void main(){if constexpr(false){Owner::Hidden value=Owner::Hidden();}else print(1);}", "1\n");
		Ok &= Reject("class Owner{private:struct Hidden{};};void main(){if constexpr(true){Owner::Hidden value=Owner::Hidden();}}", "access");
		Ok &= Run("void main(){constexpr int enabled=1;auto f=[]()->int{if constexpr(enabled)return 3;else return missing();};print(f());}", "3\n");
		Ok &= Run("template<typename T,bool Enabled> int pick(){if constexpr(Enabled){T::Missing value=T::Missing();return 1;}else return 7;}void main(){print(pick<int,false>());}", "7\n");
		Ok &= Run("void main(){if constexpr(true){enum class E{Value=7};constexpr E selected=E::Value;print(cast<int>(selected));}else missing();}", "7\n");
		Ok &= Reject("void main(){if constexpr(true){enum class E:uint8{Value=999};}}", "enum");
		Ok &= Run("void main(){if constexpr(false){struct Never{static constexpr int value=1/0;};}else print(1);}", "1\n");
		Ok &= Run("void main(){if constexpr(true){struct Local{static constexpr int value=7;};print(Local::value);}else missing();}", "7\n");
		Ok &= Run("class Base final{};void main(){if constexpr(false){class Bad:Base{};}else print(1);}", "1\n");
		Ok &= Reject("class Base final{};void main(){if constexpr(true){class Bad:Base{};}}", "final");
		Ok &= Reject("template<typename T,bool Enabled> int pick(){if constexpr(Enabled){T::Missing value=T::Missing();return 1;}else return 7;}void main(){print(pick<int,true>());}", "Missing");
		Ok &= Run("int read(){if constexpr(true){return 7;}}void main(){print(read());}", "7\n");
		Ok &= Run("void main(){const int n=2;{const int n=3;if constexpr(n==3)print(3);}if constexpr(n==2)print(2);}", "3\n2\n");
		Ok &= Run("enum Flag{Ready=1};void main(){if constexpr(Flag::Ready)print(1);if constexpr(18446744073709551615u64)print(2);}", "1\n2\n");
		Ok &= Run("void main(){if constexpr(true){enum E{Ready=2};if constexpr(E::Ready==2)print(2);else missing();}}", "2\n");
		Ok &= Run("void main(){enum E{Ready=2};if constexpr(Ready==2)print(2);else missing();}", "2\n");
		Ok &= Run("void main(){if constexpr(true){enum E{Ready=3};if constexpr(Ready==3)print(3);else missing();}}", "3\n");
		Ok &= Run("void main(){enum E{Ready=3};constexpr int n=Ready;int values[n];if constexpr(n==3)print(len(values));else missing();}", "3\n");
		Ok &= Run("constexpr int Ready=2;void main(){const int original=Ready;enum E{Ready=3};if constexpr(original==2)print(original);else missing();if constexpr(Ready==3)print(Ready);else missing();}", "2\n3\n");
		Ok &= Run("constexpr int id(int n){return n;}constexpr int Ready=2;void main(){const int original=id(Ready);enum E{Ready=3};constexpr int snapshot=original;print(snapshot);}", "2\n");
		Ok &= Run("void main(){const int Ready=2;{enum E{Ready=3};if constexpr(Ready==3)print(Ready);else missing();}if constexpr(Ready==2)print(Ready);else missing();}", "3\n2\n");
		Ok &= Run("template<int N> int read(){enum E{Ready=N};if constexpr(Ready==N)return Ready;else return missing();}void main(){print(read<4>());print(read<7>());}", "4\n7\n");
		Ok &= Run("struct C{int Ready=2;int read(){enum E{Ready=3};if constexpr(Ready==3)return Ready;else return missing();}};void main(){C c=C();print(c.read());}", "3\n");
		Ok &= Run("class Policy{public:static constexpr int value=7;static constexpr int read(){return value;}};"
		          "template<typename T> constexpr int selected(){if constexpr(T::value==7)return T::read();else return 0;}"
		          "constexpr int result=selected<Policy>();void main(){print(result);}",
		    "7\n");
		Ok &= Run("namespace T{string read(){return \"wrong\";}string alias(){return \"wrong\";}int value=90;}"
		          "class Policy{public:static int value=7;static int read(){return value;}static int& alias(){return value;}};"
		          "template<typename T> int selected(){int& value=T::alias();value+=1;return T::read()+T::value;}"
		          "void main(){print(selected<Policy>());print(Policy::value);}",
		    "16\n8\n");
		Ok &= Reject("namespace T{int read(){return 90;}}template<typename T> int selected(){return T::read();}"
		             "void main(){print(selected<int>());}",
		    "");
		Ok &= Run("void main(){const int Ready=2;auto f=[]()->int{enum E{Ready=3};if constexpr(Ready==3)return Ready;else return missing();};print(f());}", "3\n");
		Ok &= Run("void main(){enum E{Ready=3};auto f=[]()->int{if constexpr(Ready==3)return Ready;else return missing();};print(f());}", "3\n");
		Ok &= Run("void main(){enum E:uint32{Ready=4294967295u};if constexpr(Ready==4294967295u)print(Ready);else missing();}", "4294967295\n");
		Ok &= Run("void main(){fn()->int f=[](){if constexpr(true)return 7;else return missing();};print(f());}", "7\n");
		Ok &= Run("void main(){if constexpr(true){struct Policy{static constexpr bool ready=true;};if constexpr(Policy::ready)print(3);else missing();}}", "3\n");
		Ok &= Run("void main(){using Count=int;const Count n=4;if constexpr(n==4){using Value=int64;constexpr Value next=n+1;if constexpr(next==5)print(next);else missing();}}", "5\n");
		Ok &= Run("struct C{int value=0;C(){if constexpr(true)value=6;else missing();}~C(){if constexpr(false)missing();else print(value);}};void work(){C c=C();}void main(){work();}", "6\n");
		Ok &= Run("void main(){for(const int n=2;true;){if constexpr(n==2)print(n);else missing();break;}}", "2\n");
		Ok &= Run("void main(){constexpr int n=4;auto outer=[]()->int{constexpr int n=7;auto inner=[]()->int{if constexpr(n==7)return 7;else missing();};return inner();};print(outer());if constexpr(n==4)print(n);else missing();}", "7\n4\n");
		Ok &= Run("template<int N> int choose(){if constexpr(N>0){constexpr int M=N-1;if constexpr(M>0)return choose<M>();else return N;}else return 0;}void main(){print(choose<3>());print(choose<0>());}", "1\n0\n");
		Ok &= Run("template<bool Enabled> int get(){if constexpr(Enabled){struct Local<T>{T value;T read(){return value;}};Local<int> item=Local<int>();item.value=8;return item.read();}else return 9;}void main(){print(get<true>());print(get<false>());}", "8\n9\n");
		Ok &= Run("void main(){auto f=[]()->int{if constexpr(true)return 7;else return missing();};print(f());}", "7\n");
		Ok &= Run("void main(){if constexpr((false&&(1/0))||(true?1:(1/0)))print(1);else missing();}", "1\n");
		Ok &= Run("constexpr int n=3;if constexpr(n==3){print(n);}else missing();", "3\n");
		Ok &= Run("interface I{virtual int read()=0;}class A<T> where T:I{public:using Alias=int;};if constexpr(false){A<int>::Alias value=1;}else print(2);", "2\n");
		Ok &= Reject("interface I{virtual int read()=0;}class A<T> where T:I{public:using Alias=int;};if constexpr(true){A<int>::Alias value=1;}", "does not satisfy");
		Ok &= Library("export template<int N> int count(){if constexpr(N==0)return 0;else return N+count<N-1>();}", "print(count<3>());", "6\n");
		Ok &= Library("export template<int N> int read(){enum E{Ready=N};if constexpr(Ready==N)return Ready;else return missing();}", "print(read<4>());print(read<7>());", "4\n7\n");
		Ok &= Reject("interface I{virtual int read()=0;}class A<T> where T:I{public:using Alias=int;};template<bool Flag> int get(){if constexpr(Flag){A<int>::Alias value=1;return value;}else return 2;}void main(){print(get<true>());}", "does not satisfy");
		Ok &= Reject("void main(){if(false){missing();}}", "missing");
		Ok &= Reject("void main(){if constexpr(true){int values[0];}}", "array");
		Ok &= Reject("void main(){int n=1;if constexpr(n)print(1);}", "if constexpr");
		Ok &= Reject("constexpr int f(int n){if constexpr(n>0)return 1;else return 2;}void main(){}", "constant");
		Ok &= Reject("void main(){if constexpr(1/0)print(1);}", "if constexpr");
		Ok &= Reject("enum class Flag{Ready=1};void main(){if constexpr(Flag::Ready)print(1);}", "if constexpr");
		Ok &= Reject("void main(){if constexpr(true){int hidden=1;}print(hidden);}", "hidden");
		Ok &= Reject("void main(){if constexpr(true){missing();}else print(2);}", "missing");
		Ok &= Reject("void main(){if constexpr(true){int duplicate=1;int duplicate=2;}}", "duplicate");
		Ok &= Reject("void main(){int Ready=2;{enum E{Ready=3};if constexpr(true)Ready=4;}}", "const");
		Ok &= Reject("int Ready=2;void modify(int& n){n=7;}void main(){enum E{Ready=3};if constexpr(true)modify(ref Ready);}", "ref");
		Ok &= Reject("int read(){if constexpr(false)return 1;}void main(){}", "return");
		Ok &= Reject("void main(){if constexpr(false){int broken=;}else print(1);}", "expression");
		Ok &= Reject("template<bool Flag> int f(){if constexpr(Flag)return missing();else return 1;}void main(){print(f<true>());}", "missing");
		Ok &= Reject("template<int N> int read(){enum E{N=3};if constexpr(N==3)return N;else return 0;}void main(){print(read<5>());}", "template value parameter");
		Ok &= Reject("int Ready(){return 99;}void main(){enum E{Ready=3};if constexpr(true)print(Ready());}", "not callable");
		Ok &= Reject("int Ready(){return 99;}void main(){enum E{Ready=3};auto result=Ready();}", "not callable");
		Ok &= Reject("constexpr int Ready(){return 1;}void main(){enum E{Ready=0};if constexpr(Ready())print(7);}", "not callable");
		Ok &= Reject("struct C{static constexpr int Ready(){return 1;}int read(){enum E{Ready=0};if constexpr(Ready())return 7;else return 8;}};void main(){C c=C();print(c.read());}", "not callable");
		Ok &= Reject("void main(){enum E{print=0};print(7);}", "not callable");
		Ok &= Run("namespace N{constexpr int Ready(){return 2;}}void main(){enum E{Ready=0};if constexpr(N::Ready()==2)print(N::Ready());else missing();}", "2\n");
		Ok &= Run("void main(){enum E{print=0};std::print(7);}", "7\n");
		Ok &= Run("int Ready(){return 99;}void main(){enum E{Ready=3};if constexpr(false)print(Ready());else print(Ready);}", "3\n");
		Ok &= Run("constexpr int Ready(){return 2;}void main(){const int original=Ready();enum E{Ready=0};if constexpr(original==2)print(original);else missing();}", "2\n");
		Ok &= Run("void main(){enum E{cast=0};constexpr uint8 n=3;if constexpr(cast<int>(n)==3)print(cast<int>(n));else missing();}", "3\n");
		{
			FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
			Options.ConstantEvaluationLimits.MaxWork = 30;
			auto Limited = VeroScriptCompile::CompileSource("constexpr int spin(){while(true){}return 0;}void main(){if constexpr(spin())print(1);}", "if-constexpr-limit.vs", Options);
			Ok &= Expect(!Limited.bSuccess && Limited.Error.Message.find("if constexpr") != std::string::npos, "if constexpr shares mandatory evaluation budgets");
		}
		const std::string Source = "template<int N> int value(){if constexpr(N>0)return N;else return -N;}void main(){print(value<2>());print(value<-3>());}";
		auto Prepared = VeroScriptCompile::CompileSource(Source, "if-constexpr-reuse.vs");
		Ok &= Expect(Prepared.bSuccess, "if constexpr prepared compilation: " + Prepared.Error.Message);
		if (Prepared.bSuccess)
		{
			VeroScriptCompiler Compiler;
			for (int I = 0; I < 2; ++I)
			{
				auto Module = Compiler.Compile(Prepared.PreparedProgram->GetProgram());
				Ok &= Expect(bool(Module), "if constexpr immutable AST reuse");
				if (Module)
				{
					Ok &= Execute(*Module, "2\n3\n");
				}
			}
			const auto CompileShared = [&]()
			{
				VeroScriptCompiler Independent;
				auto Module = Independent.Compile(Prepared.PreparedProgram->GetProgram());
				return Module ? std::string{} : Independent.GetError().Message;
			};
			auto First = std::async(std::launch::async, CompileShared);
			auto Second = std::async(std::launch::async, CompileShared);
			const auto FirstError = First.get(), SecondError = Second.get();
			Ok &= Expect(FirstError.empty() && SecondError.empty(), "if constexpr parallel immutable AST compilation: " + FirstError + SecondError);
		}
		return Ok;
	}

	bool ConstantConditionalAssignments()
	{
		bool Ok = true;
		Ok &= Run("constexpr int choose(bool flag){int value=1;return flag?value:(value=2);}"
		          "void main(){constexpr int yes=choose(true);constexpr int no=choose(false);"
		          "print(yes);print(no);bool flag=true;print(choose(flag));flag=false;print(choose(flag));}",
		    "1\n2\n1\n2\n");
		// The returned local state exposes effects from either unselected branch.
		Ok &= Run("constexpr int left(bool flag){int value=1;int selected=flag?7:(value=2);return value*10+selected;}"
		          "constexpr int right(bool flag){int value=1;int selected=flag?(value=3):8;return value*10+selected;}"
		          "void main(){constexpr int a=left(true);constexpr int b=left(false);"
		          "constexpr int c=right(true);constexpr int d=right(false);print(a);print(b);print(c);print(d);"
		          "bool flag=true;print(left(flag));print(right(flag));flag=false;print(left(flag));print(right(flag));}",
		    "17\n22\n33\n18\n17\n33\n22\n18\n");
		Ok &= Run("constexpr int update(bool flag){int value=4;int selected=flag?(value+=3):value++;"
		          "return value*10+selected;}"
		          "void main(){constexpr int yes=update(true);constexpr int no=update(false);"
		          "print(yes);print(no);bool flag=true;print(update(flag));flag=false;print(update(flag));}",
		    "77\n54\n77\n54\n");
		Ok &= Run("constexpr int nested(bool outer,bool inner){int value=1;"
		          "int selected=outer?(inner?(value=2):value):(inner?value:(value=3));return value*10+selected;}"
		          "void main(){constexpr int a=nested(true,true);constexpr int b=nested(true,false);"
		          "constexpr int c=nested(false,true);constexpr int d=nested(false,false);"
		          "print(a);print(b);print(c);print(d);bool yes=true;bool no=false;"
		          "print(nested(yes,yes));print(nested(yes,no));print(nested(no,yes));print(nested(no,no));}",
		    "22\n11\n11\n33\n22\n11\n11\n33\n");
		Ok &= Reject("constexpr int invalid(int n){const int selected=true?(n+=3):0;"
		             "constexpr int value=selected;return value;}",
		    "constexpr");
		return Ok;
	}

	bool ConstantCrossFileDefaults()
	{
		Tests::FScopedTemporaryDirectory Directory("VeroScript-constexpr-defaults-");
		if (!Directory) return false;
		const auto Write = [&](const char *Name, const char *Source)
		{
			std::ofstream File(Directory.Get() / Name, std::ios::binary);
			File << Source;
			return bool(File);
		};
		if (!Write("declaration.vs", "namespace API{export constexpr int answer(int value=3);}") ||
		    !Write("definition.vs", "import \"declaration.vs\";const int Default=9;constexpr int API::answer(int input){return input+Default;}") ||
		    !Write("main.vs", "import \"declaration.vs\";import \"definition.vs\";void main(){constexpr int n=API::answer();print(n);int runtime=2;print(API::answer(runtime));}"))
		{
			return false;
		}
		auto Result = VeroScriptCompile::CompileFile((Directory.Get() / "main.vs").generic_string());
		return Expect(Result.bSuccess, "constexpr cross-file default: " + Result.Error.Message) && Execute(*Result.Module, "12\n11\n");
	}

	bool ConstantGovernance()
	{
		const std::string Source = "constexpr int sum(int n){int s=0;for(int i=0;i<n;++i)s+=i;return s;}"
		                           "constexpr int count=sum(10);void main(){print(count);}";
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.ConstantEvaluationLimits.MaxCacheBytes = 0;
		auto Uncached = VeroScriptCompile::CompileSource(Source, "constexpr-policy.vs", Options);
		if (!Expect(Uncached.bSuccess, "zero-cache constexpr compile: " + Uncached.Error.Message) ||
		    !Execute(*Uncached.Module, "45\n"))
		{
			return false;
		}
		Options.ConstantEvaluationLimits.MaxWork = 8;
		auto Limited = VeroScriptCompile::CompileSource(Source, "constexpr-policy.vs", Options);
		if (!Expect(!Limited.bSuccess, "constexpr work limit was ignored")) return false;
		Options.ConstantEvaluationLimits = {};
		Options.ConstantEvaluationLimits.MaxMemoryBytes = 256;
		auto Memory = VeroScriptCompile::CompileSource(Source, "constexpr-policy.vs", Options);
		if (!Expect(!Memory.bSuccess, "constexpr memory limit was ignored")) return false;
		Options.ConstantEvaluationLimits = {};
		Options.ConstantEvaluationLimits.MaxCallDepth = 3;
		auto Depth = VeroScriptCompile::CompileSource("constexpr int f(int n){if(n==0)return 0;return f(n-1)+1;}constexpr int x=f(10);", "constexpr-depth.vs", Options);
		if (!Expect(!Depth.bSuccess, "constexpr call-depth limit was ignored")) return false;
		Options.ConstantEvaluationLimits = {};
		Options.ConstantEvaluationLimits.MaxWork = 0;
		auto InvalidPolicy = VeroScriptCompile::CompileSource(Source, "constexpr-policy.vs", Options);
		if (!Expect(!InvalidPolicy.bSuccess && InvalidPolicy.Error.MessageId == "diag.constexpr.positive_limits", "invalid constexpr policy was not rejected before binding")) return false;
		VeroScriptLexer Lexer("constexpr int sum(int n){int s=0;for(int i=0;i<n;++i)s+=1;return s;}struct Config{static constexpr int value=sum(2000);};void main(){print(Config::value);}", "constexpr-cancel.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "constexpr-cancel.vs");
		auto Program = Parser.ParseProgram();
		if (!Expect(Program && !Parser.HasError(), "constexpr cancellation fixture parse")) return false;
		VeroScriptCompiler Compiler;
		bool Cancelled = false;
		unsigned Checks = 0;
		try
		{
			FAnalysisControlScope Control([&]
			{
				return ++Checks > 1000;
			});
			(void)Compiler.Compile(*Program);
		}
		catch (const FAnalysisCancelled &)
		{
			Cancelled = true;
		}
		if (!Expect(Cancelled, "constexpr cancellation did not propagate")) return false;
		auto Reused = Compiler.Compile(*Program);
		if (!Expect(bool(Reused), "compiler/AST reuse after constexpr cancellation: " + Compiler.GetError().Message) || !Execute(*Reused, "2000\n")) return false;
		std::vector<std::future<bool>> Requests;
		for (int I = 0; I < 3; ++I)
		{
			Requests.emplace_back(std::async(std::launch::async, [Source]
			{
				return VeroScriptCompile::CompileSource(Source, "constexpr-parallel.vs").bSuccess;
			}));
		}
		for (auto &Request : Requests)
		{
			if (!Expect(Request.get(), "parallel constexpr request failed")) return false;
		}
		return CompileOnly(Source);
	}

	bool ConstantMetadata()
	{
		auto Result = VeroScriptCompile::CompileSource("constexpr uint64 maximum=18446744073709551615u64;enum class E:int64{Last=-1i64};constexpr E last=E::Last;void main(){}", "constexpr-metadata.vs");
		if (!Expect(Result.bSuccess, "constexpr metadata producer: " + Result.Error.Message)) return false;
		const auto &Module = *Result.Module;
		std::string Error;
		for (std::size_t I = 0; I < Module.GlobalMetadata.size(); ++I)
		{
			const auto &Meta = Module.GlobalMetadata[I];
			if (!Meta.bConstexpr)
			{
				continue;
			}
			if (!Expect(Meta.bConst && Meta.ConstantValue && Meta.ConstantValue->Bits == UINT64_MAX,
			        "constexpr exact global value missing"))
			{
				return false;
			}
			auto Invalid = Module;
			Invalid.GlobalMetadata[I].ConstantValue.reset();
			if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Invalid, Error), "missing constexpr payload accepted")) return false;
			Invalid = Module;
			Invalid.GlobalMetadata[I].ConstantValue->Type.Width = 8;
			if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Invalid, Error), "wrong constexpr payload width accepted")) return false;
			Invalid = Module;
			Invalid.GlobalMetadata[I].bConst = false;
			if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Invalid, Error), "writable constexpr metadata accepted")) return false;
		}
		return Execute(Module, "");
	}

	bool Library(const std::string &Source, const std::string &Consumer, const std::string &Expected)
	{
		Tests::FScopedTemporaryDirectory Directory("VeroScript-template-library-");
		if (!Directory)
		{
			return false;
		}
		const auto SourcePath = Directory.Get() / "source.vs";
		{
			std::ofstream File(SourcePath);
			File << Source;
			if (!File)
			{
				return false;
			}
		}
		auto Producer = VeroScriptCompile::CompileFile(SourcePath.generic_string());
		if (!Producer.bSuccess)
		{
			return Expect(false, "template library compile at " + std::to_string(Producer.Error.Location.Column) + ": " + Producer.Error.Message + "\n" + Source);
		}
		FPackageBuildOptions Options;
		Options.Kind = EPackageKind::Library;
		Options.PackageName = "template-capabilities";
		Options.bStripDebugInfo = true;
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!BuildPackage(*Producer.Module, Options, Bytes, Error))
		{
			return Expect(false, "template library package: " + Error);
		}
		const auto PackagePath = Directory.Get() / "templates.vslib";
		{
			std::ofstream File(PackagePath, std::ios::binary);
			File.write(reinterpret_cast<const char *>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
			if (!File)
			{
				return false;
			}
		}
		std::filesystem::remove(SourcePath);
		auto Result = VeroScriptCompile::CompileSource("import \"" + PackagePath.generic_string() + "\";void main(){" +
		        Consumer + "}",
		    (Directory.Get() / "consumer.vs").generic_string());
		if (!Result.bSuccess)
		{
			return Expect(false, "template library consumer: " + Result.Error.Message + "\n" + Source + "\n" + Consumer);
		}
		if (!VeroScriptBytecodeIO::SerializeBytecode(*Result.Module, 73, Bytes, Error))
		{
			return Expect(false, Error);
		}
		auto Module = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 73, Error);
		if (!Module)
		{
			return Expect(false, Error);
		}
		return Execute(*Module, Expected);
	}

	bool CrossFileLibrary()
	{
		Tests::FScopedTemporaryDirectory Directory("VeroScript-template-cross-library-");
		if (!Directory)
		{
			return false;
		}
		const auto Write = [&](const char *Name, const std::string &Source)
		{
			std::ofstream File(Directory.Get() / Name, std::ios::binary);
			File << Source;
			return bool(File);
		};
		if (!Write("decl.vs",
		        "export template<typename T,int N> class Box{public:template<typename U> U plus(U value);};") ||
		    !Write("impl.vs", "import \"decl.vs\";const int Hidden=4;template<typename X,int Count> template<typename "
		                      "Y> Y Box<X,Count>::plus(Y input){return input+Count+Hidden;}") ||
		    !Write("source.vs", "export import \"decl.vs\";import \"impl.vs\";"))
		{
			return false;
		}
		auto Producer = VeroScriptCompile::CompileFile((Directory.Get() / "source.vs").generic_string());
		if (!Producer.bSuccess)
		{
			return Expect(false, "cross-file producer: " + Producer.Error.Message);
		}
		FPackageBuildOptions Options;
		Options.Kind = EPackageKind::Library;
		Options.PackageName = "cross-template";
		Options.bStripDebugInfo = true;
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!BuildPackage(*Producer.Module, Options, Bytes, Error))
		{
			return Expect(false, Error);
		}
		const auto Package = Directory.Get() / "library.vslib";
		{
			std::ofstream File(Package, std::ios::binary);
			File.write(reinterpret_cast<const char *>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
			if (!File)
			{
				return false;
			}
		}
		for (const auto *Name : {"decl.vs", "impl.vs", "source.vs"})
		{
			std::filesystem::remove(Directory.Get() / Name);
		}
		auto Consumer = VeroScriptCompile::CompileSource(
		    "import \"" + Package.generic_string() +
		        "\";void main(){Box<int,3> value=Box<int,3>();print(value.plus(7));print(value.plus<int64>(8i64));}",
		    (Directory.Get() / "consumer.vs").generic_string());
		return Consumer.bSuccess ? Execute(*Consumer.Module, "14\n15\n")
		                         : Expect(false, "cross-file consumer: " + Consumer.Error.Message);
	}

	bool ParallelCompilation()
	{
		const auto Compile = []
		{
			auto Result = VeroScriptCompile::CompileSource(
			    "template<typename T,int N> struct Box{T data[N];template<typename U> U echo(U value){return "
			    "value;}};void main(){Box<int,3> value=Box<int,3>();print(value.echo(7));}",
			    "parallel-template.vs");
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			if (Result.bSuccess)
			{
				VeroScriptBytecodeIO::SerializeBytecode(*Result.Module, 91, Bytes, Error);
			}
			return Bytes;
		};
		const auto Baseline = Compile();
		if (Baseline.empty())
		{
			return false;
		}
		std::vector<std::future<std::vector<std::uint8_t>>> Futures;
		for (int Index = 0; Index < 4; ++Index)
		{
			Futures.push_back(std::async(std::launch::async, Compile));
		}
		bool Success = true;
		for (auto &Future : Futures)
		{
			Success = Expect(Future.get() == Baseline, "parallel template compilation is isolated and deterministic") &&
			    Success;
		}
		return Success;
	}

	bool SignatureQueries()
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "struct Payload{int value;};class Box{public:template<typename T> T echo(T value){return missing(value);}};"
		    "template<typename T> T identity(T value){return missing(value);}void main(){}",
		    "template-query.vs");
		if (!Compiled.bSuccess)
		{
			return Expect(false, Compiled.Error.Message);
		}
		const auto &Program = Compiled.PreparedProgram->GetProgram();
		FTemplateCallTypeQuery Query(Program, nullptr, Program.EntryModulePath, {});
		const std::vector<std::string> Explicit{"Payload"};
		bool Ok = Expect(Query.ResolveReturnType("Box", "echo", {"Payload"}) == "Payload",
		    "method query infers its signature without instantiating an unused invalid body");
		Ok &= Expect(Query.ResolveReturnType("Box", "echo", {"Payload"}, &Explicit) == "Payload",
		    "explicit method signature query agrees with inference");
		Ok &= Expect(Query.ResolveReturnType({}, "identity", {"Payload"}) == "Payload",
		    "free signature query shares core inference");
		Ok &= Expect(Query.ResolveReturnType("Box", "missing", {"Payload"}).empty() &&
		        Query.ResolveReturnType("Box", "echo", {"Payload"}) == "Payload",
		    "a failed query does not poison subsequent queries");
		return Ok;
	}

	bool CapturedParameterIdentity()
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "template<typename T,T N> int local(){struct Local{T data[N];};Local value=Local();return N;}"
		    "void main(){print(local<int,3>());}",
		    "template-local-schema.vs");
		if (!Compiled.bSuccess)
		{
			return Expect(false, Compiled.Error.Message);
		}
		const auto &Program = Compiled.PreparedProgram->GetProgram();
		const auto Parent = std::find_if(Program.Functions.begin(), Program.Functions.end(),
		    [](const auto &F)
		{
			return F.Name == "local";
		});
		const auto Local = std::find_if(Program.Structs.begin(), Program.Structs.end(),
		    [](const auto &T)
		{
			return T.Scope.SimpleName == "Local";
		});
		if (Parent == Program.Functions.end() || Local == Program.Structs.end())
		{
			return false;
		}
		const auto &Expected = Parent->TemplateParameters.GetParameters();
		const auto &Captured = Local->TemplateParameters.GetParameters();
		if (!Expect(Captured.size() == 2 && Local->TemplateParameters.GetEnclosingCount() == 2,
		        "local type captures the complete ordered callable environment"))
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Expected.size(); ++Index)
		{
			if (!Expect(Captured[Index].Name == Expected[Index].Name && Captured[Index].Kind == Expected[Index].Kind &&
			            Captured[Index].Id == Expected[Index].Id,
			        "local capture preserves declaration parameter identities"))
			{
				return false;
			}
		}
		return true;
	}

	bool EnumMetadataIdentity()
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "template<int N> class Owner{public:enum class Tag{Ready=3};"
		    "template<Tag V=Tag::Ready> int read(){return N;}};"
		    "enum class Other{Ready=3};template<Other V> int other(){return 0;}"
		    "void main(){Owner<4> a=Owner<4>();Owner<5> "
		    "b=Owner<5>();print(a.read());print(b.read());print(other<Other::Ready>());}",
		    "enum-template-identity.vs");
		if (!Compiled.bSuccess)
		{
			return Expect(false, Compiled.Error.Message);
		}
		auto &Module = *Compiled.Module;
		const auto First = std::find_if(Module.Functions.begin(), Module.Functions.end(), [](const auto &F)
		{
			return !F.TemplateInstance.Arguments.empty() && F.bIsMethod;
		});
		const auto Other = std::find_if(Module.Functions.begin(), Module.Functions.end(), [](const auto &F)
		{
			return !F.TemplateInstance.Arguments.empty() && !F.bIsMethod;
		});
		if (First == Module.Functions.end() || Other == Module.Functions.end())
		{
			return false;
		}
		const auto OriginalTypes = First->TemplateInstance.ValueTypeNames;
		First->TemplateInstance.ValueTypeNames.clear();
		std::string DomainError;
		if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Module, DomainError), "missing closed value types accepted"))
		{
			return false;
		}
		First->TemplateInstance.ValueTypeNames = OriginalTypes;
		const auto Original = First->TemplateInstance.Arguments[0];
		const auto Second = std::find_if(First + 1, Module.Functions.end(),
		    [&](const auto &F)
		{
			return F.bIsMethod && !F.TemplateInstance.Arguments.empty() &&
			    F.TemplateInstance.Arguments[0] != Original;
		});
		if (Second == Module.Functions.end())
		{
			return false;
		}
		std::string Error;
		First->TemplateInstance.Arguments[0] = Second->TemplateInstance.Arguments[0];
		if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Module, Error),
		        "wrong enclosing instance of nested enum accepted in template metadata"))
		{
			return false;
		}
		First->TemplateInstance.Arguments[0] = Other->TemplateInstance.Arguments[0];
		return Expect(!VeroScriptBytecodeIO::ValidateModule(Module, Error),
		    "wrong nested enum identity accepted in template metadata");
	}

	bool Metadata()
	{
		auto Compiled = VeroScriptCompile::CompileSource("template<typename T,int N> class Box{public:T values[N];"
		                                                 "template<typename U> U echo(U value){return value;}};"
		                                                 "void main(){Box<int,3> box=Box<int,3>();print(box.echo(7));}",
		    "template-metadata.vs");
		if (!Compiled.bSuccess)
		{
			return Expect(false, Compiled.Error.Message);
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!VeroScriptBytecodeIO::SerializeBytecode(*Compiled.Module, 95, Bytes, Error))
		{
			return Expect(false, Error);
		}
		auto Module = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 95, Error);
		if (!Module)
		{
			return Expect(false, Error);
		}
		std::vector<FTemplateDeclarationInfo> Declarations;
		ModuleMeta::EnumerateTemplateDeclarations(*Module, Declarations);
		const auto Method = std::find_if(Declarations.begin(), Declarations.end(), [](const auto &Declaration)
		{
			return Declaration.Kind == "method" && Declaration.Name == "echo";
		});
		if (Method == Declarations.end() || Method->Parameters.GetParameters().size() != 1 ||
		    Method->EnclosingParameters.GetParameters().size() != 2 ||
		    Method->EnclosingParameters.GetParameters()[1].Kind != ETemplateParameterKind::Value)
		{
			return Expect(false, "open method metadata lost its parameter kinds or enclosing environment");
		}
		const auto Function =
		    std::find_if(Module->Functions.begin(), Module->Functions.end(),
		        [&](const auto &Candidate)
		{
			return Candidate.TemplateInstance.DeclarationId == Method->Id;
		});
		if (Function == Module->Functions.end() ||
		    Function->TemplateInstance.Arguments != std::vector<std::string>{"int"} ||
		    Function->TemplateInstance.EnclosingArguments.size() != 2 ||
		    FormatTemplateArgumentName(Function->TemplateInstance.EnclosingArguments[1]) != "3")
		{
			return Expect(false, "closed method metadata lost its exact arguments");
		}
		auto Invalid = *Module;
		const auto Index = static_cast<std::size_t>(Function - Module->Functions.begin());
		Invalid.Functions[Index].TemplateInstance.DeclarationId = "missing declaration";
		if (VeroScriptBytecodeIO::ValidateModule(Invalid, Error))
		{
			return Expect(false, "missing template declaration accepted");
		}
		Invalid = *Module;
		Invalid.Functions[Index].TemplateInstance.EnclosingArguments[1] = "@value:V3:int32:0:4294967296";
		if (VeroScriptBytecodeIO::ValidateModule(Invalid, Error))
		{
			return Expect(false, "out-of-range template metadata accepted");
		}
		Invalid = *Module;
		Invalid.TemplateDeclarations.front().Kind = "enum";
		if (VeroScriptBytecodeIO::ValidateModule(Invalid, Error))
		{
			return Expect(false, "wrong catalog kind accepted");
		}
		Invalid = *Module;
		const auto DeclarationIndex = static_cast<std::size_t>(Method - Declarations.begin());
		auto Records = Invalid.TemplateDeclarations[DeclarationIndex].Parameters.GetParameters();
		Records.front().Id =
		    Invalid.TemplateDeclarations[DeclarationIndex].EnclosingParameters.GetParameters().front().Id;
		Invalid.TemplateDeclarations[DeclarationIndex].Parameters.Assign(std::move(Records));
		if (VeroScriptBytecodeIO::ValidateModule(Invalid, Error))
		{
			return Expect(false, "duplicate parameter identity accepted");
		}
		auto Dependent =
		    VeroScriptCompile::CompileSource("template<typename T,T Value> int get(){return cast<int>(Value);}"
		                                     "void main(){print(get<int,7>());}",
		        "dependent-metadata.vs");
		if (!Dependent.bSuccess)
		{
			return Expect(false, Dependent.Error.Message);
		}
		for (auto &Entry : Dependent.Module->Functions)
		{
			if (Entry.TemplateInstance.Arguments.size() == 2)
			{
				Entry.TemplateInstance.Arguments[0] = "bool";
			}
		}
		return Expect(!VeroScriptBytecodeIO::ValidateModule(*Dependent.Module, Error),
		    "mismatched dependent value domain accepted");
	}
} // namespace

int main(const int Argc, char **Argv)
{
	if (Argc == 2 && std::string_view(Argv[1]) == "--benchmark")
	{
		return Benchmark();
	}
	if (Argc > 1)
	{
		BackendPath = Argv[1];
	}
	bool Ok = ConstantBranches();
	Ok &= TemplateArityIsolation();
	Ok &= NestedTemplateDefaults();
	Ok &= GenericMemberChains();
	Ok &= GenericClassReferences();
	Ok &= DependentMemberAliases();
	Ok &= ConstantConditionalAssignments();
	Ok &= Run("constexpr int square(int x){return x*x;} constexpr int count=square(8);void main(){constexpr auto x=count+1;print(x);int runtime=3;print(square(runtime));}", "65\n9\n");
	Ok &= Run("constexpr int sum(int n){int result=0;for(int i=0;i<n;++i){if(i==2)continue;result+=i;}return result;}void main(){constexpr int n=sum(5);print(n);}", "8\n");
	Ok &= Run("constexpr int factorial(int n){if(n<=1)return 1;return n*factorial(n-1);}void main(){constexpr int n=factorial(6);print(n);}", "720\n");
	Ok &= Run("struct Limits{static constexpr int size=3;static constexpr int twice(int n){return n*2;}};void main(){constexpr int n=Limits::twice(Limits::size);print(n);}", "6\n");
	Ok &= Run("template<typename T> constexpr T twice(T value){return value+value;}template<int N=twice<int>(2)> struct Box{int data[N];};void main(){constexpr int n=twice<int>(4);int data[n];Box<> value=Box<>();print(len(data));}", "8\n");
	Ok &= Run("constexpr uint64 maximum=18446744073709551615u64;constexpr int64 minimum=-9223372036854775808i64;void main(){print(maximum);print(minimum);}", "18446744073709551615\n-9223372036854775808\n");
	Ok &= Run("constexpr int choose(int n){while(n<5){++n;}do{--n;}while(n>3);switch(n){case 3:return 7;default:return 0;}}void main(){constexpr int x=choose(1);print(x);switch(x){case choose(1):print(9);break;default:break;}}", "7\n9\n");
	Ok &= Library("constexpr int helper(int n){return n+1;}export constexpr int answer(int n){return helper(n)*2;}export constexpr int count=answer(3);", "constexpr int n=answer(count);print(n);int x=2;print(answer(x));", "18\n6\n");
	Ok &= Reject("int f(){return 1;}constexpr int x=f();", "constexpr");
	Ok &= Reject("constexpr int f(int n){constexpr int x=n;return x;}", "constexpr");
	Ok &= Reject("constexpr int f(int n){if(n==0){print(1);}return n;}", "constexpr");
	Ok &= Reject("constexpr float f(float n){return n;}", "constexpr");
	Ok &= Reject("struct Bad{constexpr int x=1;};", "constexpr");
	Ok &= Reject("constexpr int x;", "constexpr");
	Ok &= Reject("constexpr int forever(int n){while(true){++n;}return n;}constexpr int x=forever(0);", "constexpr");
	Ok &= Reject("constexpr int f(int n){return f(n);}constexpr int x=f(1);", "constexpr");

	Ok &= Run("namespace Config{constexpr int triple(int n){return n*3;}constexpr int count=triple(4);}void main(){constexpr int n=Config::triple(Config::count);print(n);}", "36\n");
	Ok &= Run("constexpr int f(int n);constexpr int f(int value){return value+1;}void main(){constexpr int n=f(2);print(n);}", "3\n");
	Ok &= Run("constexpr int value(){return 4;}enum class Values{First=value(),Second=First+2,Third};void main(){print(cast<int>(Values::First));print(cast<int>(Values::Second));print(cast<int>(Values::Third));}", "4\n6\n7\n");
	Ok &= Run("template<typename T> struct Config{static constexpr int count=4;template<typename U> static constexpr U double_value(U value){return value+value;}static constexpr int capacity(){return count+2;}};void main(){constexpr int n=Config<int>::double_value<int>(3)+Config<int>::capacity();print(n);}", "12\n");
	Ok &= Run("constexpr int f(int n){const int base=3;constexpr int x=base+1;if(n>0)return x;return 1/0;}void main(){constexpr int x=f(1);print(x);}", "4\n");
	Ok &= Reject("constexpr int f(int n);int f(int n){return n;}", "declaration");
	Ok &= Reject("template<typename T> constexpr int f(T n){print(n);return 1;}", "constexpr");
	Ok &= Reject("constexpr int f()=delete;", "constexpr");
	Ok &= Reject("enum class Bad{First=Second,Second=3};", "enum");
	Ok &= Library("class Hidden{public:static constexpr int value(int n){return n+1;}};export constexpr int get(int n){return Hidden::value(n);}", "constexpr int n=get(7);print(n);", "8\n");
	Ok &= CompileOnly("constexpr int spin(){while(true){}return 0;}template<typename T>struct Config{int value=spin();};void unused(){Config<int> item=Config<int>();}void main(){}");
	Ok &= CompileOnly("constexpr int spin(){while(true){}return 0;}void unused(){const int x=spin();}void main(){}");
	Ok &= CompileOnly("constexpr int spin(){while(true){}return 0;}constexpr int unused(){const int x=spin();return 1;}void main(){}");
	Ok &= Run("constexpr int f(int n){return n+1;}void main(){const int n=f(3);constexpr int x=n;int data[n];print(x);print(len(data));}", "4\n4\n");
	Ok &= Run("constexpr int base=2;constexpr int f(int n){return n;}void main(){const int n=f(base);int base=7;constexpr int x=n;print(x);}", "2\n");
	Ok &= Run("namespace N{constexpr int f(){return 3;}}constexpr int g(){return N::f();}void main(){int N=9;constexpr int x=g();print(x);}", "3\n");
	Ok &= Run("constexpr int f(int n){return n;}using A=int;void main(){const int n=f(cast<A>(3));{using A=bool;constexpr int x=n;print(x);}}", "3\n");
	Ok &= Run("constexpr int f(){const int n=3;{int n=7;}constexpr int x=n;return x;}void main(){constexpr int x=f();print(x);}", "3\n");
	Ok &= Reject("constexpr int f(int n){const int x=n;constexpr int y=x;return y;}", "constexpr");
	Ok &= Library("export constexpr auto maximum=18446744073709551615u64;", "constexpr uint64 x=maximum;print(x);", "18446744073709551615\n");
	Ok &= Library("export enum class E{First=7};export constexpr E value=E::First;", "constexpr E x=value;print(cast<int>(x));", "7\n");
	Ok &= Run("constexpr int plus(int n){return n+1;}int read(const int& n){return n;}void main(){constexpr int n=4;fn(int)->int call=plus;auto get=[n]()->int{return n;};print(read(ref n));print(call(2));print(get());}", "4\n3\n4\n");
	Ok &= Reject("void main(){constexpr int n=4;n=5;}", "const");
	Ok &= Run("constexpr int one(){return 1;}constexpr int sum(){int n=0;for(int i=0;i<10000;++i)n+=one();return n;}void main(){constexpr int n=sum();print(n);}", "10000\n");
	Ok &= Run("namespace N{constexpr int size(){return 3;}int length(int values[size()]){return len(values);}}void main(){int values[3];int size=8;print(N::length(values));}", "3\n");
	Ok &= Run("struct C{static constexpr int value=3;static constexpr int twice(int n){return n*2;}};void main(){type<C> info=type<C>();for(int i=0;i<info.field_count();++i){field_info<C> f=field_info<C>(info.qualified_name,i);if(f.name()==\"value\")print(f.is_constexpr());}for(int i=0;i<info.method_count();++i){method_info<C> m=method_info<C>(info.qualified_name,i);if(m.name()==\"twice\")print(m.is_constexpr());}}", "true\ntrue\n");
	Ok &= Run("interface Policy{static constexpr int value(int n){return n+1;}};void main(){constexpr int n=Policy::value(2);print(n);}", "3\n");
	Ok &= Run("class Config{private:static constexpr int hidden(int n){return n+1;}public:static constexpr int value(){return hidden(2);}};void main(){constexpr int n=Config::value();print(n);}", "3\n");
	Ok &= Reject("class Config{private:static constexpr int hidden(int n){return n+1;}};constexpr int n=Config::hidden(2);", "access");
	Ok &= Reject("constexpr int f(int64 n){return cast<int>(n);}int f(int n){return n;}constexpr int x=f(1);", "constexpr");
	Ok &= ConstantCrossFileDefaults();
	{
		std::string Oversized = "constexpr int f(int n){return n;}constexpr int value=f(";
		for (int I = 0; I < 256; ++I)
		{
			Oversized += I ? ",1" : "1";
		}
		Ok &= Reject(Oversized + ");", "255 arguments");
	}
	Ok &= ConstantGovernance();
	Ok &= ConstantMetadata();
	Ok &= Run("int value=4;int& mutable_value(){return value;}const int& readonly_value(){return value;}void main(){mutable_value()=6;print(readonly_value());}", "6\n");
	Ok &= Metadata();
	Ok &= Run("enum class Key{First};template<typename T> void verify(T key){map<T,int> a=map<T,int>();"
	          "ordered_map<T,int> b=ordered_map<T,int>();set<T> c=set<T>();a[key]=7;b[key]=9;c.add(key);"
	          "print(a.has(key));print(b.has(key));print(c.has(key));print(len(a.entries()));print(len(b.entries()));}"
	          "void main(){verify(Key::First);}",
	    "true\ntrue\ntrue\n1\n1\n");
	Ok &= Run("enum class Color{Zero=0,Red=1};template<typename T> struct Holder{T value;array<T> colors;"
	          "T read(){return value;}};void main(){Holder<Color> h=Holder<Color>();print(h.value==Color::Zero);"
	          "print(h.read()==Color::Zero);h.colors.push(Color::Red);print(h.colors[0]==Color::Red);}",
	    "true\ntrue\ntrue\n");
	Ok &= Run("enum class Color{Red=1};template<typename T> T echo(T value){return value;}"
	          "template<typename T> struct Holder{T value;};void main(){Color c=Color::Red;"
	          "print(echo(c)==Color::Red);Holder<Color> h=Holder<Color>();h.value=c;print(h.value==Color::Red);}",
	    "true\ntrue\n");
	Ok &= Run("using T=int;T echo<T>(T value){return value;}void main(){print(echo(\"text\"));}", "text\n");
	Ok &= Run("void main(){const std::array<int> values={1};const_cast<std::array<int>>(values).push(2);"
	          "print(std::len(values));}",
	    "2\n");
	Ok &= Run("template<int N> struct Buffer{int greater[N > 0 ? N : 1];int shifted[N >> 1];"
	          "int lesser[N < 4 ? N : 4];};void main(){Buffer<6> value=Buffer<6>();"
	          "print(std::len(value.greater));print(std::len(value.shifted));print(std::len(value.lesser));}",
	    "6\n3\n4\n");
	Ok &= Run("namespace N{using T=int;template<typename T> struct Box{T value;};}"
	          "void main(){N::Box<string> value=N::Box<string>();value.value=\"text\";print(value.value);}",
	    "text\n");
	Ok &= Run("template<int N> struct Box{int data[N];int size(){return N;}};"
	          "template<int N,typename T=Box<N+1>> struct Select{T value;int size(){return value.size();}};"
	          "void main(){Select<3> value=Select<3>();print(value.size());}",
	    "4\n");
	Ok &= Run("template<int N> class C{private:template<int M> struct Hidden{int read(){return N+M;}};"
	          "public:template<typename T=Hidden<N+1>> int read(){T value=T();return value.read();}};"
	          "void main(){C<3> value=C<3>();print(value.read());}",
	    "7\n");
	Ok &= Run("template<int N> class C{public:struct Hidden{int read(){return N;}};"
	          "template<typename T=Hidden> int read(){T value=T();return value.read();}};"
	          "void main(){C<3> value=C<3>();print(value.read());}",
	    "3\n");
	Ok &= Run("class Base{protected:struct Hidden{int read(){return 7;}};};class C:Base{public:"
	          "template<typename T=Hidden> int read(){T value=T();return value.read();}};"
	          "void main(){C value=C();print(value.read());}",
	    "7\n");
	Ok &= Run("template<int N> class Base{protected:struct Hidden{int read(){return N;}};};"
	          "template<int N> class C:Base<N>{public:template<typename T=Hidden> int read(){"
	          "T value=T();return value.read();}};void main(){C<3> value=C<3>();print(value.read());}",
	    "3\n");
	Ok &= Run("template<int N> class Base{protected:int value=N;};"
	          "template<int N> class C:Base<N>{public:int read(){return value;}};"
	          "void main(){C<3> value=C<3>();print(value.read());}",
	    "3\n");
	Ok &= Run("template<int N> class Owner{private:int value=N;public:struct Reader{"
	          "int read(Owner owner){return owner.value;}};};void main(){Owner<3> value=Owner<3>();"
	          "Owner<3>::Reader reader=Owner<3>::Reader();print(reader.read(value));}",
	    "3\n");
	Ok &= Run("template<int N> class Owner{private:int value=N;friend class Reader<N>;};"
	          "template<int N> class Reader{public:int read(Owner<N> owner){return owner.value;}};"
	          "void main(){Owner<3> value=Owner<3>();Reader<3> reader=Reader<3>();print(reader.read(value));}",
	    "3\n");
	Ok &= Reject("template<int N> class Owner{private:int value=N;friend class Reader<N>;};"
	             "template<int N> class Reader{public:int read(Owner<4> owner){return owner.value;}};"
	             "void main(){Owner<4> value=Owner<4>();Reader<3> reader=Reader<3>();print(reader.read(value));}",
	    "cannot access");
	Ok &= Run("template<typename T> class Base{public:template<typename U> struct Inner{T first;U second;};};"
	          "template<typename T> class C:Base<T>{public:template<typename U=Inner<int>> int read(){"
	          "U value=U();value.second=7;return value.second;}};void main(){C<string> value=C<string>();"
	          "print(value.read());}",
	    "7\n");
	Ok &= Reject("class Base{private:struct Hidden{};};class C:Base{public:"
	             "template<typename T=Hidden> int read(){return 7;}};void main(){}",
	    "cannot access");
	Ok &= Reject("class A{public:struct Item{};};class B{public:struct Item{};};class C:A,B{public:"
	             "template<typename T=Item> int read(){return 7;}};void main(){}",
	    "ambiguous");
	Ok &= Library("export template<int N> class Base{protected:struct Item{int read(){return N;}};};"
	              "export template<int N> class Factory:Base<N>{public:template<typename T=Item> int read(){"
	              "T value=T();return value.read();}};",
	    "Factory<3> value=Factory<3>();print(value.read());", "3\n");
	Ok &= Run("template<int N> struct Box{int data[N];int size(){return N;}};"
	          "class C{public:template<int N,typename T=Box<N+1>> int read(){T value=T();return value.size();}};"
	          "void main(){C value=C();print(value.read<3>());}",
	    "4\n");
	Ok &= Run("template<int N> struct Box{int data[N];int size(){return N;}};"
	          "template<int N,typename T> int read(){T value=T();return value.size();}"
	          "template<int N> int caller(){return read<3,Box<N+1>>();}"
	          "void main(){print(caller<7>());}",
	    "8\n");
	Ok &= Run("template<int N> struct Box{int data[N];int size(){return N;}};"
	          "template<typename T> T create(){return T();}"
	          "void main(){print(create<Box<4>>().size());}",
	    "4\n");
	Ok &= Run("template<int N> class Box{public:int data[N];int size(){return N;}};"
	          "template<typename T> T create(){return T();}"
	          "void main(){print(create<Box<4>>().size());}",
	    "4\n");
	Ok &= Run("template<int N> struct Box{int data[N];int size(){return N;}};"
	          "template<int N,typename T=Box<2*N+1>> int read(){T value=T();return value.size();}"
	          "void main(){print(read<3>());}",
	    "7\n");
	Ok &= Run("namespace Other{struct N{};}template<int N,typename T=Other::N> int read(){T value=T();return N;}"
	          "void main(){print(read<3>());}",
	    "3\n");
	Ok &= Reject("template<int N,typename T=Missing<'N'>> struct Invalid{};void main(){}", "type");
	Ok &= Reject("template<int N> struct Box{};"
	             "template<int N,typename T=Box<Missing+1>> struct Invalid{};void main(){}",
	    "invalid template type default");
	Ok &= Run("template<int N> struct Box{int data[N];int size(){return N;}};"
	          "template<int N,typename T=Box<'N'>> int read(){T value=T();return value.size();}"
	          "void main(){print(read<3>());}",
	    "78\n");
	Ok &= Run("int tag(int value){return 1;}int tag(int64 value){return 2;}"
	          "template<typename T,typename U> int read(){U value=cast<U>(7);return tag(value);}"
	          "template<typename T> int caller(){return read<int64,T>();}"
	          "void main(){print(caller<int>());}",
	    "1\n");
	Ok &= Library("export template<int N> struct Box{int data[N];int size(){return N;}};"
	              "export template<int N,typename T=Box<N+1>> struct Select{T value;int size(){return value.size();}};",
	    "Select<3> value=Select<3>();print(value.size());", "4\n");

	Ok &= Run("class A<T,U=int>{public:int read(){return 2;}};class A<T>{public:int read(){return 1;}};"
	          "void main(){A<int> value=A<int>();print(value.read());}",
	    "1\n");
	Ok &= Run("struct A<T,U=int>{int read(){return 2;}};class A<T>{public:int read(){return 1;}};"
	          "void main(){A<int> value=A<int>();print(value.read());}",
	    "1\n");
	Ok &= Run("class A<T,U=int>{public:int read(){return 2;}};struct A<T>{int read(){return 1;}};"
	          "void main(){A<int> value=A<int>();print(value.read());}",
	    "1\n");
	Ok &= Reject("class A<T,U=int>{};class A<T,U=int,V=int>{};void main(){A<int> value=A<int>();}",
	    "ambiguous template type arguments");
	Ok &= Run("template<int N> struct Buffer{int values[cast<int>(N)>0 ? N : 1];};"
	          "void main(){Buffer<3> value=Buffer<3>();int data[cast<int>(2)+1];"
	          "print(std::len(value.values));print(std::len(data));}",
	    "3\n3\n");
	Ok &= Run("template<int N> class Owner{public:enum class Tag{Ready=3};};"
	          "template<Owner<3>::Tag V> int literal(){return cast<int>(V);}"
	          "template<Owner<(1+2)>::Tag V> int expression(){return cast<int>(V);}"
	          "void main(){print(literal<Owner<3>::Tag::Ready>());print(expression<Owner<3>::Tag::Ready>());}",
	    "3\n3\n");
	Ok &= Run("interface I<T,U=int>{int read(){return 2;}};interface I<T>{int read(){return 1;}};"
	          "class C:I<int>{};void main(){C value=C();print(value.read());}",
	    "1\n");
	Ok &= Run("class A<T,U=int>{public:int read(){return 2;}};class A<int N>{public:int read(){return N;}};"
	          "void main(){A<4> value=A<4>();print(value.read());}",
	    "4\n");
	Ok &= Run("template<int N> class Owner{public:enum class Tag{Ready=3};};"
	          "template<int N,Owner<N>::Tag V> int read(){return N+cast<int>(V);}"
	          "void main(){print(read<4,Owner<4>::Tag::Ready>());}",
	    "7\n");
	Ok &= Run("const int Extra=1;template<int N> class Owner{public:enum class Tag{Ready=3};};"
	          "template<int N,Owner<N+Extra>::Tag V> int read(){return N+cast<int>(V);}"
	          "void main(){print(read<3,Owner<4>::Tag::Ready>());}",
	    "6\n");
	Ok &= Library("const int Extra=1;template<int N> class Owner{public:enum class Tag{Ready=3};};"
	              "template<int N,Owner<N+Extra>::Tag V> int read(){return N+cast<int>(V);}"
	              "export int answer(){return read<3,Owner<4>::Tag::Ready>();}",
	    "print(answer());", "6\n");
	Ok &= EnumMetadataIdentity();
	Ok &= SignatureQueries();
	Ok &= CapturedParameterIdentity();
	Ok &= Run(
	    "template<int N> struct Buffer{int data[N];int size(){return N;}};"
	    "template<int N> class Factory{public:template<typename T> Buffer<N+1> make(T value){return Buffer<N+1>();}};"
	    "void main(){Factory<3> factory=Factory<3>();print(factory.make(7).size());}",
	    "4\n");

	Ok &= CrossFileLibrary();
	Ok &= ParallelCompilation();
	Ok &= Run(
	    "template<int N> struct Counter{static int current=0;template<typename T> T add(){current+=N;return "
	    "cast<T>(current);}};"
	    "void main(){Counter<3> first=Counter<3>();Counter<(1+2)> same=Counter<(1+2)>();Counter<4> other=Counter<4>();"
	    "print(first.add<int>());print(same.add<int>());print(other.add<int>());}",
	    "3\n6\n4\n");
	Ok &= Run(
	    "template<typename T,int N> int extent(T values[N]){return N;}"
	    "template<typename T,int N> struct Buffer{T data[N];};template<typename T,int N> int rank(Buffer<T,N> "
	    "value){return 1;}"
	    "template<typename T> int rank(Buffer<T,3> value){return 2;}void main(){int values[3];print(extent(values));"
	    "Buffer<int,3> value=Buffer<int,3>();print(rank(value));}",
	    "3\n2\n");

	Ok &= Run("template<int N,typename T> class Outer{public:static const int Size=N;using Storage=T[N];"
	          "struct Inner{int read(){return Size;}};};void main(){Outer<4,int>::Inner "
	          "inner=Outer<4,int>::Inner();print(inner.read());"
	          "Outer<4,int>::Storage values;values[0]=7;print(values[0]);}",
	    "4\n7\n");

	Ok &= Run("const auto Size=3;template<int N=Size> int read(){return N;}void main(){print(read());}", "3\n");
	Ok &= Run("template<int N> int recursive(int n){if(n<=0)return N;return recursive<N>(n-1)+1;}"
	          "void main(){print(recursive<3>(4));}",
	    "7\n");
	Ok &= Reject("template<int N> int endless(){return endless<N+1>();}void main(){print(endless<0>());}",
	    "nested too deeply");

	Ok &=
	    Library("export class Factory{public:template<typename T,int N=4> T plus(T value){return value+N;}};",
	        "Factory factory=Factory();print(factory.plus<int,3>(4));print(factory.plus<int64>(5i64));", "7\n9\n");
	Ok &= Library("const int DefaultSize=3;export template<typename T,int N=DefaultSize> struct Buffer{T values[N];int "
	              "size(){return N;}};",
	    "Buffer<int> first=Buffer<int>();Buffer<int64,7> "
	    "next=Buffer<int64,7>();print(first.size());print(next.size());",
	    "3\n7\n");
	Ok &= Library("enum class Hidden{Ready=3};export template<Hidden Value=Hidden::Ready> struct Choice{int "
	              "read(){return cast<int>(Value);}};"
	              "export int answer(){Choice<> value=Choice<>();return value.read();}",
	    "print(answer());Choice<> next=Choice<>();print(next.read());", "3\n3\n");
	{
		const std::string Source = "class Box<T>{public:template<class V> struct Nested{V value;};"
		                           "template<typename U=T> U echo(U input){return input;}"
		                           "U suffix<U=T>(U input){return input;}};";
		VeroScriptLexer Lexer(Source, "method-template.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "method-template.vs");
		auto Program = Parser.ParseProgram();
		Ok &= Expect(Program && !Parser.HasError() && Program->Classes.size() == 1 && Program->Structs.size() == 1 &&
		        Program->Classes[0].Methods.size() == 2 &&
		        Program->Classes[0].Methods[0].TypeParams == std::vector<std::string>{"U"} &&
		        Program->Classes[0].Methods[1].TypeParamDefaults == std::vector<std::string>{"T"},
		    "method and nested type templates parse: " + Parser.GetError().Message);
	}
	Ok &= Run("template<typename T, class U = int> struct Cell { T value; U count; };"
	          "template<class T> T echo(T value){return value;}"
	          "class Tag<T>{public:int get(){return 9;}};"
	          "void main(){Cell<string> c=Cell<string>();c.value=\"ready\";print(echo(c.value));"
	          "Tag<int> t=Tag<int>();print(t.get());}",
	    "ready\n9\n");
	Ok &= Run("U echo<typename T, class U = T>(T value){return value;}"
	          "void main(){print(echo<int>(7));}",
	    "7\n");
	Ok &= Run("class Box<T>{public:T value;template<typename U> U echo(U input){return input;}"
	          "template<class U=T> static U make(U input){return input;}};"
	          "void main(){Box<int> box=Box<int>();print(box.echo(17));"
	          "print(box.echo<string>(\"method\"));print(Box<int>::make(23));}",
	    "17\nmethod\n23\n");
	Ok &= Run("template<int N> int offset(int value){return value+N;}"
	          "template<typename T, int N=4> struct Buffer{T data[N];int size(){return N;}};"
	          "void main(){Buffer<int,3> a=Buffer<int,3>();Buffer<int> b=Buffer<int>();"
	          "print(offset<5>(2));print(a.size());print(b.size());}",
	    "7\n3\n4\n");
	Ok &= Run("template<uint64 N> uint64 constant(){return N;}"
	          "void main(){print(constant<18446744073709551615u64>());}",
	    "18446744073709551615\n");
	Ok &= Run("class Choice{public:int pick(int64 value){return 1;}"
	          "template<typename T> int pick(T value){return 2;}"
	          "template<typename T> int rank(T value){return 3;}"
	          "template<typename T> int rank(array<T> value){return 4;}};"
	          "void main(){Choice c=Choice();array<int> values={1,2};"
	          "print(c.pick(5));print(c.pick(5i64));print(c.rank(values));}",
	    "2\n1\n4\n");
	Ok &= Run("enum class Policy{Small=2,Large=5};"
	          "template<Policy Mode=Policy::Small> int policy(){if(Mode==Policy::Small){return 2;}return 5;}"
	          "template<typename T,T Value> T constant(){return Value;}"
	          "void main(){print(policy<Policy::Large>());print(policy());print(constant<int,7>());"
	          "print(constant<uint64,18446744073709551615u64>());print(constant<bool,true>());}",
	    "5\n2\n7\n18446744073709551615\ntrue\n");
	Ok &= Run("int pick(int64 value){return 1;}template<typename T> int pick(T value){return 2;}"
	          "template<typename T> int rank(T value){return 3;}"
	          "template<typename T> int rank(array<T> value){return 4;}"
	          "void main(){array<int> values={1,2};print(pick(5));print(pick(5i64));"
	          "print(rank(values));print(rank<int>(values));}",
	    "2\n1\n4\n4\n");
	Ok &= Run("interface I{template<typename T> T echo(T value){return value;}};class C:I{};"
	          "void main(){C c=C();I view=c;print(c.echo(8));print(view.echo<string>(\"interface\"));}",
	    "8\ninterface\n");
	Ok &= Run("int choose(int value){return 1;}"
	          "template<typename T=int> int choose(int value){return 2;}"
	          "class Choice{public:int choose(int value){return 3;}"
	          "template<typename T=int> int choose(int value){return 4;}};"
	          "struct B{int value=5;};template<typename T> struct B{T value;};"
	          "void main(){Choice c=Choice();print(choose(0));print(choose<>(0));"
	          "print(c.choose(0));print(c.choose<>(0));B<> b=B<>();print(b.value);}",
	    "1\n2\n3\n4\n5\n");
	Ok &= Reject("const int N=7;template<int N=N> int value(){return N;}void main(){}", "invalid template default");
	Ok &= Run("const int Count=3;template<int N> int value(){return N;}"
	          "namespace A{const int Count=6;template<int N=Count> int read(){return N;}}"
	          "void main(){const int Local=Count+2;print(value<Local>());print(A::read());}",
	    "5\n6\n");
	Ok &= Run("class Box<T>{public:template<class U> U echo(U value);};"
	          "template<typename X> template<typename Y> Y Box<X>::echo(Y input){return input;}"
	          "void main(){Box<int> box=Box<int>();print(box.echo<string>(\"defined\"));}",
	    "defined\n");
	Ok &= Run("template<int N> struct Outer{template<typename T> struct Inner{T data[N];int size(){return N;}};};"
	          "void main(){Outer<3>::Inner<int> item=Outer<3>::Inner<int>();print(item.size());}",
	    "3\n");
	Ok &= Run("template<typename T,int N> struct Buffer{T data[N];};"
	          "template<typename T,int N> int size(Buffer<T,N> item){return N;}"
	          "void main(){Buffer<int,3> item=Buffer<int,3>();print(size(item));}",
	    "3\n");
	Ok &= Run("template<typename T> T identity(T value){return value;}"
	          "class Echo{public:template<typename T> T echo(T value){return value;}"
	          "template<typename T> static T make(T value){return value;}};"
	          "void main(){Echo object=Echo();auto free=identity<int>;auto member=object.echo<int>;"
	          "auto factory=Echo::make<string>;print(free(7));print(member(8));print(factory(\"closed\"));}",
	    "7\n8\nclosed\n");
	Ok &= Run("template<typename T> T identity(T value){return value;}"
	          "class Factory{public:template<typename T> T make(){return cast<T>(9);}};"
	          "void main(){fn(int)->int first=identity<>;Factory factory=Factory();"
	          "fn()->int second=factory.make<>;print(first(8));print(second());}",
	    "8\n9\n");
	Ok &= Run("template<bool B=3<4> bool value(){return B;}"
	          "void main(){print(value());print(value<3<2>());}",
	    "true\nfalse\n");
	Ok &= Run("enum Policy{Ready=3};template<typename T,T Value> T read(){return Value;}"
	          "void main(){print(cast<int>(read<Policy,Policy::Ready>()));}",
	    "3\n");
	Ok &= Run("template<int N> interface Sized{virtual int size()=0;};"
	          "class Three:Sized<3>{public:int size(){return 3;}};"
	          "template<typename T,int N> int read(T value) where T:Sized<N>{return value.size();}"
	          "class Box<T>{public:template<int N> int read(T value) where T:Sized<N>{return value.size();}};"
	          "void main(){Three value=Three();Box<Three> box=Box<Three>();print(read<Three,3>(value));"
	          "print(box.read<3>(value));}",
	    "3\n3\n");
	Ok &= Run("class Family{public:template<typename T> T echo(T value){return value;}};"
	          "template<typename X> class Family{public:template<typename T> T echo(T value){return value;}};"
	          "void main(){Family a=Family();Family<int> b=Family<int>();print(a.echo(4));print(b.echo(5));}",
	    "4\n5\n");
	Ok &= Run("class C{public:template<typename T=int> T echo(T value){return value;}"
	          "int run(){return echo<int>(4)+echo<>(5);}};void main(){C c=C();print(c.run());}",
	    "9\n");
	Ok &= Run("class C{public:template<typename T> static T make(T value){return value;}"
	          "static int factory(){return make<int>(6);}};void main(){print(C::factory());}",
	    "6\n");
	Ok &= Run("template<typename T> T echo(T value);template<typename U> U echo(U input){return input;}"
	          "void main(){print(echo(7));}",
	    "7\n");
	Ok &= Run("template<typename T> class Outer{public:template<T Value> T get();};"
	          "template<typename X> template<X N> X Outer<X>::get(){return N;}"
	          "void main(){Outer<int> value=Outer<int>();print(value.get<8>());}",
	    "8\n");
	Ok &= Run("enum Policy{Ready=3};template<Policy Value> int read(){return cast<int>(Value);}"
	          "void main(){print(read<Ready>());}",
	    "3\n");
	Ok &= Run("struct Fixed{int data[3];};void main(){Fixed item=Fixed();item.data[0]=7;print(item.data[0]);}", "7\n");
	Ok &= Run("enum class Color{Red=1,Green=2};void main(){Color color=Color::Green;"
	          "switch(color){case Color::Red:print(1);break;case Color::Green:print(2);break;default:print(0);}}",
	    "2\n");
	Ok &= Run("class Base<T>{public:template<typename U> U echo(U value){return value;}};"
	          "class Child:Base<int>{};void main(){Child child=Child();auto "
	          "call=child.echo<string>;print(call(\"inherited\"));}",
	    "inherited\n");
	Ok &= Run("int Created=0;int Released=0;class Factory{public:~Factory(){Released+=1;}"
	          "template<typename T> static T echo(T value){return value;}};"
	          "Factory make(){Created+=1;return Factory();}void main(){auto call=make().echo<int>;"
	          "print(Created);print(Released);print(call(7));}",
	    "1\n1\n7\n");
	Ok &= Run("class Ref{public:template<typename T> T& borrow(T& value){return value;}};"
	          "void main(){Ref object=Ref();int value=4;object.borrow<int>(ref value)=7;"
	          "auto call=object.borrow<int>;call(ref value)=9;print(value);}",
	    "9\n");
	Ok &= Run("template<int N> int read(){return N;}class Limits{public:static const int Base=3;static const int "
	          "Size=Base+2;};"
	          "template<int N> class GenericLimits{public:static const int Size=N+1;};"
	          "void main(){print(read<Limits::Size>());print(read<GenericLimits<7>::Size>());}",
	    "5\n8\n");
	Ok &= Run("class C{private:static const int Limit=4;public:template<int N=Limit> int read(){return N;}};"
	          "template<typename T=void> void noop(){}void main(){C value=C();print(value.read());noop();}",
	    "4\n");
	Ok &= Reject("class C{private:static const int Limit=4;};template<int N> int read(){return N;}"
	             "void main(){print(read<C::Limit>());}",
	    "cannot access member");
	Ok &= Run("enum class Key:uint8{One=1};void main(){map<Key,int> values=map<Key,int>();values[Key::One]=7;"
	          "print(values[Key::One]);set<Key> chosen={Key::One};print(chosen.has(Key::One));}",
	    "7\ntrue\n");
	Ok &= Run("template<typename T,T N> int local(){struct Local{T data[N];};Local "
	          "value=Local();value.data[0]=N;return value.data[0];}"
	          "void main(){print(local<int,3>());}",
	    "3\n");
	Ok &= Run("template<int N> class Owner{public:enum class Tag{Ready=3};template<Tag V=Tag::Ready> int read(){return "
	          "cast<int>(V)+N;}};"
	          "void main(){Owner<4> value=Owner<4>();print(value.read());}",
	    "7\n");
	Ok &= Run("template<typename R,typename T> R count(T... values){return cast<R>(len(values));}"
	          "class C{public:template<typename R,typename T> R count(T... values){return cast<R>(len(values));}};"
	          "void main(){fn(array<int>)->int first=count<>;C item=C();fn(array<int>)->int second=item.count<>;"
	          "array<int> values={1,2,3};print(first(values));print(second(values));}",
	    "3\n3\n");
	Ok &= Run("class C{public:template<typename T> int choose(T value){return 1;}"
	          "template<class U> int choose(U value) const{return 2;}};"
	          "void main(){C first=C();const C second=C();print(first.choose<int>(7));print(second.choose<int>(7));}",
	    "1\n2\n");
	Ok &= Run("template<int N> int choose(){return 1;}template<bool B> int choose(){return 2;}"
	          "class C{public:template<int N> int choose(){return 3;}template<bool B> int choose(){return 4;}};"
	          "void main(){C "
	          "item=C();print(choose<7>());print(choose<true>());print(item.choose<7>());print(item.choose<true>());}",
	    "1\n2\n3\n4\n");
	Ok &= Reject("class C{public:template<typename T> T echo(T value){return value;}"
	             "template<class U> U echo(U input){return input;}};void main(){}",
	    "duplicate method");
	Ok &= Reject("template<typename T> T echo(T value){return value;}"
	             "template<class U> U echo(U input){return input;}void main(){}",
	    "duplicate function");
	Ok &= Reject("template<int N> struct C{int N;};void main(){}", "cannot shadow a template value parameter");
	Ok &= Reject("template<int N> struct C{int N(){return 7;}};void main(){}",
	    "cannot shadow a template value parameter");
	Ok &= Reject("class C{public:template<int N=4> int read();};int C::read<int N=4>(){return N;}void main(){}",
	    "matching declaration");
	Ok &= Run("template<typename T,int N=3> class Box{public:template<typename U> U echo(U value){return value;}};"
	          "void main(){Box<int> box=Box<int>();box.echo(5);type<Box<int>> info=type<Box<int>>();"
	          "print(info.template_argument_count());print(info.template_argument_kind(1));print(info.template_"
	          "argument_name(1));"
	          "module_info module=module_info();for(int i=0;i<module.template_declaration_count();++i){"
	          "if(module.template_declaration_name(i)==\"echo\"){print(module.template_parameter_count(i));"
	          "print(module.template_own_parameter_count(i));print(module.template_enclosing_parameter_count(i));"
	          "print(module.template_parameter_kind(i,1));print(module.template_parameter_default(i,1));}}"
	          "for(int i=0;i<info.method_count();++i){method_info<Box<int>> method(info.qualified_name,i);"
	          "if(method.template_argument_count()>0){print(method.template_argument_name(0));"
	          "print(method.template_enclosing_argument_count());print(method.invoke<int>(box,7));}}}",
	    "2\nvalue\n3\n3\n1\n2\nvalue\n3\nint\n2\n7\n");
	Ok &= Reject("class A{private:enum class Secret{Ready=3};};template<int N> int read(){return N;}"
	             "void main(){print(read<cast<int>(A::Secret::Ready)>());}",
	    "cannot access");
	for (const auto &Example :
	    {std::pair<const char *, const char *>{"method_templates.vs", "7\nmethod\n9\n11\nbound\n"},
	        {"value_template_parameters.vs", "5\n12\ntrue\n18446744073709551615\n"},
	        {"constexpr.vs", "10\n15\n"},
	        {"template_methods_main.vs", "cross-file\n"}})
	{
		const auto Compiled =
		    VeroScriptCompile::CompileFile(std::string(VEROSCRIPT_TEMPLATE_EXAMPLES_DIR) + "/" + Example.first);
		Ok &= Compiled.bSuccess ? Execute(*Compiled.Module, Example.second)
		                        : Expect(false, "example: " + Compiled.Error.Message);
	}
	Ok &= Reject("template<int N=M,int M=1> int value(){return N;}void main(){}", "invalid template default");
	Ok &= Reject("template<bool B=2> bool value(){return B;}void main(){}", "outside its declared value type");
	Ok &= Reject("template<int N=work()> int value(){return N;}void main(){}", "invalid template default");
	Ok &= Reject("template<string N> int unused(){return 0;}void main(){}", "requires an integral, bool, or enum type");
	Ok &= Reject("template<int N=Missing> int unused(){return 0;}void main(){}", "invalid template default");
	Ok &=
	    Reject("template<int N> int value(int N){return N;}void main(){}", "cannot shadow a template value parameter");
	Ok &= Reject("class C{public:template<int N> virtual int value(){return N;}};void main(){}", "cannot be virtual");
	for (const auto &Case : {std::pair<const char *, std::uint64_t>{"(2 + 3) * 4", 20},
	         {"18446744073709551615u64", std::numeric_limits<std::uint64_t>::max()},
	         {"-9223372036854775808i64", std::uint64_t{1} << 63},
	         {"false && (1 / 0 == 0)", 0},
	         {"true || (1 / 0 == 0)", 1},
	         {"true ? 7 : 1 / 0", 7},
	         {"(true ? 2147483647 : 0i64) + 1", std::uint64_t{1} << 31},
	         {"cast<char>(100) + cast<char>(100)", 200},
	         {"cast<uint8>(255) + cast<uint8>(1)", 256},
	         {"~0u64", std::numeric_limits<std::uint64_t>::max()},
	         {"-3 >> 1", std::numeric_limits<std::uint32_t>::max() - 1},
	         {"-3 % 2", std::numeric_limits<std::uint32_t>::max()},
	         {"cast<bool>(2)", 1}})
	{
		const auto Result = Evaluate(Case.first);
		Ok &= Expect(Result.Succeeded() && Result.Value.Bits == Case.second,
		    std::string(Case.first) + ": " + Result.Reason);
	}
	for (const char *Source :
	    {"2147483647 + 1", "18446744073709551615u64 + 1u64", "-9223372036854775808i64 / -1i64", "cast<uint8>(256)",
	        "cast<uint64>(-1)", "1 / 0", "1 << 32", "1 << -1", "-1 << 1", "1 + 1u", "true + 1", "false && (true + 1)",
	        "true || work()", "true ? 1 : work()", "cast<int>(1.0)"})
	{
		const auto Result = Evaluate(Source);
		Ok &= Expect(Result.Status == ETemplateEvaluationStatus::Invalid ||
		        Result.Status == ETemplateEvaluationStatus::NotConstant,
		    std::string("reject ") + Source);
	}

	{
		FUnaryExpr Unary(EUnaryOp::Negate, {});
		FBinaryExpr Binary(EBinaryOp::Add, {}, {});
		FConditionalExpr Conditional({}, {}, {});
		for (const FExpr *Invalid : {static_cast<const FExpr *>(&Unary), static_cast<const FExpr *>(&Binary),
		         static_cast<const FExpr *>(&Conditional)})
		{
			Ok &= Expect(EvaluateTemplateConstant(*Invalid, {}).Status == ETemplateEvaluationStatus::Invalid,
			    "malformed public constant AST fails without dereferencing missing children");
		}
	}
	FTemplateEvaluationContext Context;
	Context.ResolveName = [](const std::string_view Name, const FSourceLocation &)
	{
		FTemplateConstantSymbol Symbol;
		if (Name == "N")
		{
			Symbol.Status = ETemplateEvaluationStatus::Dependent;
			TryGetTemplateValueType("int", Symbol.Value.Type);
		}
		return Symbol;
	};
	Ok &= Expect(Evaluate("N + 1", Context).Status == ETemplateEvaluationStatus::Dependent,
	    "dependent values remain dependent");
	Ok &= Expect(Evaluate("N ? 1 : work()", Context).Status == ETemplateEvaluationStatus::NotConstant,
	    "dependent conditions do not hide impure branches");
	Context.MaxNodes = 2;
	Ok &= Expect(Evaluate("1 + 2", Context).Status == ETemplateEvaluationStatus::LimitExceeded,
	    "finite evaluation budget");
	Context.MaxNodes = 65536;
	Context.CheckWork = [](std::uint64_t)
	{
		return false;
	};
	Ok &= Expect(Evaluate("1 + 2", Context).Status == ETemplateEvaluationStatus::Interrupted,
	    "cancellation remains distinct");

	FIdentifierExpr A("A"), B("B");
	Context = {};
	Context.ResolveName = [&](const std::string_view Name, const FSourceLocation &)
	{
		FTemplateConstantSymbol Symbol;
		Symbol.Status = ETemplateEvaluationStatus::Constant;
		Symbol.Identity = std::string(Name);
		Symbol.Expression = Name == "A" ? &B : &A;
		return Symbol;
	};
	Ok &= Expect(EvaluateTemplateConstant(A, Context).Status == ETemplateEvaluationStatus::Invalid,
	    "constant dependency cycle");

	{
		FTemplateEvaluationContext Interrupted;
		Interrupted.CheckWork = [](std::uint64_t) -> bool
		{
			throw FAnalysisCancelled();
		};
		bool Cancelled = false;
		try
		{
			(void)Evaluate("1+2", Interrupted);
		}
		catch (const FAnalysisCancelled &)
		{
			Cancelled = true;
		}
		Ok &= Expect(Cancelled && Evaluate("1+2").Succeeded(),
		    "analysis cancellation remains distinct and retry succeeds");
	}
	const auto Maximum = Evaluate("18446744073709551615u64");
	FTemplateArgument ValueArgument;
	ValueArgument.Kind = ETemplateParameterKind::Value;
	ValueArgument.Value = Maximum.Value;
	FTemplateArgument Decoded;
	const auto Encoded = EncodeTemplateArgument(ValueArgument);
	Ok &= Expect(DecodeTemplateArgument(Encoded, Decoded) && EncodeTemplateArgument(Decoded) == Encoded &&
	        Decoded.Value.Bits == std::numeric_limits<std::uint64_t>::max(),
	    "exact uint64 argument roundtrip");
	for (const char *Invalid :
	    {"", "V1:x0:0:0", "V3:int32:0:18446744073709551616", "V4:bool1:3:2", "T03:int", "X3:int", "T4:int"})
	{
		Ok &= Expect(!DecodeTemplateArgument(Invalid, Decoded), std::string("malformed argument rejected: ") + Invalid);
	}
	FTemplateArgument TypeArgument;
	TypeArgument.TypeName = "int";
	const auto Key = MakeTemplateInstanceKey("module::Buffer", "Outer<int,4>", {TypeArgument, ValueArgument});
	Ok &= Expect(!Key.empty() &&
	        Key != MakeTemplateInstanceKey("module::Buffer", "Outer<int,8>", {TypeArgument, ValueArgument}),
	    "owner identity separates otherwise identical arguments");
	FTemplateValueType Integer, BooleanType, EnumA{"A", 32, false, false, true, true},
	    EnumB{"B", 32, false, false, true, true};
	TryGetTemplateValueType("int", Integer);
	TryGetTemplateValueType("bool", BooleanType);
	const auto Seven = Evaluate("7");
	Ok &= Expect(!ConvertTemplateConstant(Seven.Value, BooleanType).Succeeded(), "bool value arguments require bool");
	Ok &=
	    Expect(!ConvertTemplateConstant(Seven.Value, EnumA).Succeeded(), "enum value arguments require enum identity");
	const auto EnumValue = ConvertTemplateConstant(Seven.Value, EnumA, true);
	Ok &= Expect(EnumValue.Succeeded() && !ConvertTemplateConstant(EnumValue.Value, EnumB).Succeeded(),
	    "distinct enum identities");
	Ok &= Expect(!ConvertTemplateConstant(EnumValue.Value, Integer).Succeeded() &&
	        ConvertTemplateConstant(EnumValue.Value, Integer, true).Succeeded(),
	    "scoped enum requires an explicit conversion");
	if (!BackendPath.empty())
	{
		Ok &= Expect(BackendDispatches != 0 && NativeEntries != 0, "template tests did not dispatch native JIT code");
	}
	return Ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
