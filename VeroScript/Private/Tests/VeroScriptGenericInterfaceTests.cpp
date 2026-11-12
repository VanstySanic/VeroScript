// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptEngine.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include "VeroScriptAst.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Condition;
	}

	bool Run(
		const std::string& Source,
		std::string& OutOutput,
		std::string* OutError = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "geniface.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "geniface.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			if (OutError)
			{
				*OutError = Parser.GetError().Message;
			}
			return false;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			if (OutError)
			{
				*OutError = Compiler.GetError().Message;
			}
			return false;
		}
		std::ostringstream Output;
		std::istringstream In;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, In, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(*Module, VeroScript::ERunMode::Script))
		{
			if (OutError)
			{
				*OutError = Vm.GetError().Message;
			}
			return false;
		}
		OutOutput = Output.str();
		return true;
	}

	bool CompileFails(const std::string& Source, std::string* OutError = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "geniface.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "geniface.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			if (OutError)
			{
				*OutError = Parser.GetError().Message;
			}
			return true;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			if (OutError)
			{
				*OutError = Compiler.GetError().Message;
			}
			return true;
		}
		return false;
	}
}

int main()
{
	std::string Output;
	std::string Error;
	if (!Expect(Run(
		"class B{};class D:public B<>{};interface I{virtual int Read()=0;};"
		"class C:public I<>{public:int Read(){return 7;}};void main(){D d=D();C c=C();print(c.Read());}",
		Output, &Error), "empty type argument lists retain their existing arity-zero meaning") ||
		!Expect(Output == "7\n", "arity-zero inherited types still execute"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Run(
		"class B{public:int value=5;};using BA=B;class D:public BA<>{};"
		"interface I{virtual int Read()=0;};using IA=I;"
		"class C:public IA<>{public:int Read(){return 7;}};"
		"void main(){D d=D();C c=C();IA i=c;print(d.value);print(i.Read());}",
		Output, &Error), "empty inheritance argument lists preserve ordinary type aliases") ||
		!Expect(Output == "5\n7\n", "aliased arity-zero inherited types still execute"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	for (const std::string Source : {
		"class B{};class D:public B<>>{};",
		"struct B{};struct D:B<>>{};",
		"interface I{};class C:public I<>>{};",
		"class D<T=array<array<int>>>>{};",
		"struct D<T=array<array<int>>>>{};",
		"interface D<T=array<array<int>>>>{};"})
	{
		VeroScript::VeroScriptLexer Lexer(Source, "extra-close.vs");
		VeroScript::VeroScriptParser Parser(Lexer.Tokenize(), "extra-close.vs");
		const auto Program = Parser.ParseProgram();
		if (!Expect(Parser.HasError() && Parser.GetError().Message.find("unexpected '>'") != std::string::npos,
			"extra generic close is rejected by parsing, including empty/default lists"))
		{
			std::cerr << Source << '\n' << Parser.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}
	if (!Expect(Run(
		"class B<T>{public:T value;};class D:public B<array<int>>{};"
		"struct SB<T>{T value;};struct S:SB<array<int>>{};"
		"interface I<T>{virtual T Read()=0;};interface J:I<array<int>>{};"
		"class C:public J{public:array<int> Read(){array<int> a=array<int>();a.push(9);return a;}};"
		"void main(){D d=D();d.value.push(7);S s=S();s.value.push(8);C c=C();J j=c;"
		"print(d.value[0]);print(s.value[0]);print(j.Read()[0]);}", Output, &Error),
		"nested generic class/struct/interface inheritance") || !Expect(Output == "7\n8\n9\n", "nested generic inherited members execute"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	for (const std::string Source : {
		"class B<T>{public:T value;};class D:public B<array<array<int>>>>{};",
		"interface I<T>{virtual T Read()=0;};interface J:I<array<array<int>>>>{};"})
	{
		if (!Expect(CompileFails(Source, &Error), "extra generic close in inheritance is rejected"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!Expect(Run(
		"class Factory<T>{public:virtual fn()->const T& Create() const{throw 0;}};"
		"void main(){Factory<int> f=Factory<int>();print(1);}", Output, &Error),
		"generic parameter in a callable ref return") || !Expect(Output == "1\n", "callable ref return substitution"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Run(
		"template<T> int Use(fn(T)->int f){T n=cast<T>(7);return f(n);}"
		"void main(){fn(int)->int f=[](int n)->int{return n+1;};print(Use<int>(f));}", Output, &Error),
		"generic parameter only in a callable argument") || !Expect(Output == "8\n", "generic callable argument executes"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
		Run(
			"class Box<T1, T2 = int> { public: T1 first; T2 second; }\n"
			"template<T, U = array<T>>\n"
			"struct Values { U items; }\n"
			"void main() {\n"
			"    Box<string> box = Box<string>();\n"
			"    box.first = \"default\"; box.second = 7;\n"
			"    Values<int> values = Values<int>();\n"
			"    values.items.push(3);\n"
			"    print(box.first); print(box.second + values.items[0]);\n"
			"}\n",
			Output,
			&Error),
		"generic class/struct default type arguments")
		|| !Expect(Output == "default\n10\n", "generic default type argument output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
		Run(
			"interface IValue<T, U = int> { virtual U Read(T input) = 0; }\n"
			"class Reader : public IValue<string> {\n"
			"public: int Read(string input) { return 11; }\n"
			"}\n"
			"void main() { IValue<string> value = Reader(); print(value.Read(\"x\")); }\n",
			Output,
			&Error),
		"generic interface default type argument")
		|| !Expect(Output == "11\n", "generic interface default output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
		Run(
			"template<T = int, U = T> U make_value() { return 6; }\n"
			"template<T, U = int> U select_value(T ignored, U value) { return value; }\n"
			"void main() {\n"
			"    int value = make_value();\n"
			"    double precise = make_value<double>();\n"
			"    print(value); print(precise);\n"
			"    print(select_value<int>(7, \"deduced\"));\n"
			"}\n",
			Output,
			&Error),
		"generic function default and partially explicit type arguments")
		|| !Expect(
			Output == "6\n6\ndeduced\n",
			"generic function default type argument output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
		CompileFails(
			"template<T> T make_required() { return 1; }\n"
			"void main() { int value = make_required(); }\n",
			&Error),
		"missing required generic function type argument is rejected"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
		CompileFails(
			"class Invalid<T = int, U> { public: T first; U second; }\n"
			"void main() {}\n",
			&Error),
		"required type parameter cannot follow a defaulted parameter")
		|| !Expect(
			Error.find("cannot follow") != std::string::npos,
			"non-trailing generic default diagnostic"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
		CompileFails(
			"class Invalid<T, U = MissingType> { public: T first; U second; }\n"
			"void main() {}\n",
			&Error),
		"unknown generic default type is rejected"))
	{
		std::cerr << "expected invalid default error, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
		CompileFails(
			"template<T = MissingType> T invalid_default() { return 1; }\n"
			"void main() {}\n",
			&Error),
		"unknown generic function default type is rejected"))
	{
		std::cerr << "expected invalid function default error, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
		Run(
			"template<T>\n"
			"interface IReadable { virtual T Read() = 0; }\n"
			"template<T, U>\n"
			"struct PairValue { T first; U second; }\n"
			"template<T>\n"
			"class Box : public IReadable<T> {\n"
			"public:\n"
			"    T value;\n"
			"    Box(T initial);\n"
			"    T Read();\n"
			"}\n"
			"template<T> Box<T>::Box(T initial) { this.value = initial; }\n"
			"template<T> T Box<T>::Read() { return this.value; }\n"
			"template<T> T identity(T value) { return value; }\n"
			"void main() {\n"
			"    PairValue<int, string> pair = PairValue<int, string>();\n"
			"    pair.first = 4; pair.second = \"ok\";\n"
			"    Box<int> box(5);\n"
			"    print(identity<int>(pair.first) + box.Read());\n"
			"    print(pair.second);\n"
			"}\n",
			Output,
			&Error),
		"template prefix normalizes to existing generic semantics")
		|| !Expect(Output == "9\nok\n", "template prefix output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
		Run(
			"template<T> GenericMeta(Name=\"annotated\") class Annotated { public: T value; }\n"
			"void main() { Annotated<int> value = Annotated<int>(); value.value = 6; print(value.value); }\n",
			Output,
			&Error),
		"declaration annotations may follow a template prefix")
		|| !Expect(Output == "6\n", "annotated template prefix output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
		Run(
			"template<T> interface IValue { virtual T Get() = 0; }\n"
			"template<T> class Constrained where T : IValue<int> { public: T value; }\n"
			"class IntValue : public IValue<int> { public: int Get() { return 3; } }\n"
			"template<T> struct First { Second<T> value; }\n"
			"template<T> struct Second { T value; }\n"
			"void main() {\n"
			"    Constrained<IntValue> constrained = Constrained<IntValue>();\n"
			"    constrained.value = IntValue();\n"
			"    First<int> first = First<int>();\n"
			"    first.value = Second<int>(); first.value.value = 4;\n"
			"    print(constrained.value.Get() + first.value.value);\n"
			"}\n",
			Output,
			&Error),
		"template prefix preserves where bounds and forward generic references")
		|| !Expect(Output == "7\n", "template bounds and forward-reference output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
		CompileFails(
			"template<T> class Bad<T> { public: T value; }\n"
			"void main() {}\n",
			&Error),
		"template prefix and suffix cannot be combined")
		|| !Expect(
			Error.find("cannot combine") != std::string::npos,
			"template prefix conflict diagnostic"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
		Run(
			"namespace Generic {\n"
			"    export template<T> struct Cell { T value; }\n"
			"    template<T> T Echo(T value) { return value; }\n"
			"}\n"
			"void main() {\n"
			"    Generic::Cell<int> cell = Generic::Cell<int>();\n"
			"    cell.value = Generic::Echo<int>(11);\n"
			"    print(cell.value);\n"
			"}\n",
			Output,
			&Error),
		"template prefix works inside namespaces and after export")
		|| !Expect(Output == "11\n", "namespace template prefix output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
		CompileFails(
			"template<T> namespace Invalid {}\n"
			"void main() {}\n",
			&Error),
		"template prefix cannot be silently ignored on a namespace")
		|| !Expect(
			Error.find("not supported on namespace declarations") != std::string::npos,
			"invalid template target diagnostic"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Box<T> {\n"
				"public:\n"
				"    T value;\n"
				"    Box(T initial);\n"
				"    T Get() const;\n"
				"}\n"
				"Box<T>::Box(T initial) { this.value = initial; }\n"
				"T Box<T>::Get() const { return this.value; }\n"
				"void main() {\n"
				"    Box<int> number = Box<int>(12);\n"
				"    Box<string> text = Box<string>(\"ok\");\n"
				"    print(number.Get());\n"
				"    print(text.Get());\n"
				"}\n",
				Output,
				&Error),
			"generic same-file out-of-class definitions")
		|| !Expect(Output == "12\nok\n", "generic out-of-class definition output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
			CompileFails(
				"class Missing<T> { public: T Get(); }\n"
				"void main() {}\n",
				&Error),
			"unused generic member declaration still requires a definition"))
	{
		std::cerr << "expected missing generic definition failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface Boxable<T> {\n"
				"    virtual T get() = 0;\n"
				"    virtual void set(T v) = 0;\n"
				"}\n"
				"class Cell : public Boxable<int>{\n"
				"    public:\n"
				"    int value;\n"
				"    int get() { return value; }\n"
				"    void set(int v) { value = v; }\n"
				"}\n"
				"void main() {\n"
				"    Cell c = Cell();\n"
				"    c.set(7);\n"
				"    Boxable<int> x = c;\n"
				"    print(x.get());\n"
				"}\n",
				Output,
					&Error),
			"generic interface impl + upcast + call"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "7\n", "generic interface call output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface Boxable<T> {\n"
				"    virtual T get() = 0;\n"
				"}\n"
				"class Cell : public Boxable<int>{\n"
				"    public:\n"
				"}\n"
				"void main() { Cell value = Cell(); }\n",
					&Error),
			"missing abstract generic method fail"))
	{
		std::cerr << "expected compile failure, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface Greeter<T> {\n"
				"    void hello() { print(\"hi\"); }\n"
				"    virtual T id(T v) = 0;\n"
				"}\n"
				"class C : public Greeter<int>{\n"
				"    public:\n"
				"    int id(int v) { return v; }\n"
				"}\n"
				"void main() {\n"
				"    Greeter<int> g = C();\n"
				"    g.hello();\n"
				"    print(g.id(3));\n"
				"}\n",
				Output,
					&Error),
			"generic interface default method"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "hi\n3\n", "default method output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IParent<T> {\n"
				"    virtual T parentGet() = 0;\n"
				"}\n"
				"interface IChild<T> : IParent<T> {\n"
				"    virtual void childSpeak() = 0;\n"
				"}\n"
				"class C : public IChild<int>{\n"
				"    public:\n"
				"    int parentGet() { return 9; }\n"
				"    void childSpeak() { print(\"c\"); }\n"
				"}\n"
				"void main() {\n"
				"    IChild<int> x = C();\n"
				"    print(x.parentGet());\n"
				"    x.childSpeak();\n"
				"    IParent<int> y = x;\n"
				"    print(y.parentGet());\n"
				"}\n",
				Output,
					&Error),
			"IChild<T> : IParent<T> flatten"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "9\nc\n9\n", "flatten output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface Boxable<T> {\n"
				"    virtual T get() = 0;\n"
				"}\n"
				"struct Point : Boxable<int> {\n"
				"    int get() { return 1; }\n"
				"}\n"
				"void main() {\n"
				"    Point p = Point();\n"
				"    Boxable<int> x = p;\n"
				"    print(x.get());\n"
				"}\n",
				Output,
					&Error),
			"struct assign to generic interface (value-copy box)"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "1\n", "generic struct→interface call output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface Boxable<T> {\n"
				"    virtual T get() = 0;\n"
				"}\n"
				"struct Point : Boxable<int> {\n"
				"    int get() { return 42; }\n"
				"}\n"
				"void main() {\n"
				"    Boxable<int> x = Point();\n"
				"    print(x.get());\n"
				"}\n",
				Output,
					&Error),
			"temp generic struct→interface box"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "42\n", "temp generic box output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface Boxable<T> {\n"
				"    virtual T get() = 0;\n"
				"}\n"
				"struct Point : Boxable<int> {\n"
				"    int get() { return 4; }\n"
				"}\n"
				"void main() {\n"
				"    Point p = Point();\n"
				"    print(p.get());\n"
				"}\n",
				Output,
					&Error),
			"struct may implement generic interface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "4\n", "struct impl output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface Boxable<T> {\n"
				"    virtual T get() = 0;\n"
				"}\n"
				"class Cell : public Boxable{\n"
				"    public:\n"
				"    int get() { return 1; }\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"reject bare generic interface without args"))
	{
		std::cerr << "expected bare interface failure, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface Boxable<T> {\n"
				"    virtual T get() = 0;\n"
				"}\n"
				"void main() {\n"
				"    Boxable x = nullptr;\n"
				"}\n",
					&Error),
			"reject bare Boxable as variable type"))
	{
		std::cerr << "expected bare variable failure, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface PairLike<T,U> {\n"
				"    virtual T first() = 0;\n"
				"    virtual U second() = 0;\n"
				"}\n"
				"class Pair : public PairLike<int,string>{\n"
				"    public:\n"
				"    int first() { return 1; }\n"
				"    string second() { return \"b\"; }\n"
				"}\n"
				"void main() {\n"
				"    PairLike<int,string> p = Pair();\n"
				"    print(p.first());\n"
				"    print(p.second());\n"
				"}\n",
				Output,
					&Error),
			"two type params"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "1\nb\n", "two type params output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Parent/child type params may use different names; subst must follow declaring iface.
	if (!Expect(
			Run(
				"interface IParent<T> {\n"
				"    virtual T parentGet() = 0;\n"
				"}\n"
				"interface IChild<U> : IParent<U> {\n"
				"    virtual void childSpeak() = 0;\n"
				"}\n"
				"class C : public IChild<int>{\n"
				"    public:\n"
				"    int parentGet() { return 9; }\n"
				"    void childSpeak() { print(\"c\"); }\n"
				"}\n"
				"void main() {\n"
				"    IChild<int> x = C();\n"
				"    print(x.parentGet());\n"
				"    x.childSpeak();\n"
				"}\n",
				Output,
					&Error),
			"renamed type params IChild<U>:IParent<U>"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "9\nc\n", "renamed type params output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Non-generic child of concrete parent instance.
	if (!Expect(
			Run(
				"interface IParent<T> {\n"
				"    virtual T parentGet() = 0;\n"
				"}\n"
				"interface IChild : IParent<int> {\n"
				"    virtual void childSpeak() = 0;\n"
				"}\n"
				"class C : public IChild{\n"
				"    public:\n"
				"    int parentGet() { return 5; }\n"
				"    void childSpeak() { print(\"c\"); }\n"
				"}\n"
				"void main() {\n"
				"    IChild x = C();\n"
				"    print(x.parentGet());\n"
				"    IParent<int> y = x;\n"
				"    print(y.parentGet());\n"
				"}\n",
				Output,
					&Error),
			"non-generic child of IParent<int>"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "5\n5\n", "concrete parent flatten output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// IChild<T> : IParent<int> — parent method uses int, not T.
	if (!Expect(
			Run(
				"interface IParent<T> { virtual T get() = 0; }\n"
				"interface IChild<T> : IParent<int> { virtual T own() = 0; }\n"
				"class C : public IChild<string>{\n"
				"    public:\n"
				"    int get() { return 1; }\n"
				"    string own() { return \"a\"; }\n"
				"}\n"
				"void main() {\n"
				"    IChild<string> x = C();\n"
				"    print(x.get());\n"
				"    print(x.own());\n"
				"    IParent<int> y = x;\n"
				"    print(y.get());\n"
				"}\n",
				Output,
					&Error),
			"IChild<T>:IParent<int> correct int get"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "1\na\n1\n", "mixed parent concrete output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Must not silently accept string get() when parent requires int.
	if (!Expect(
			CompileFails(
				"interface IParent<T> { virtual T get() = 0; }\n"
				"interface IChild<T> : IParent<int> { virtual T own() = 0; }\n"
				"class C : public IChild<string>{\n"
				"    public:\n"
				"    string get() { return \"bad\"; }\n"
				"    string own() { return \"a\"; }\n"
				"}\n"
				"void main() { C value = C(); }\n",
					&Error),
			"reject wrong get for IParent<int> via IChild<string>"))
	{
		std::cerr << "expected signature fail, got success\n";
		return EXIT_FAILURE;
	}

	// Multi-base class + generic interface.
	if (!Expect(
			Run(
				"interface Boxable<T> { virtual T get() = 0; }\n"
				"class B1 { public: int x = 1; }\n"
				"class B2 { public: int y = 2; }\n"
				"class D : public B1, public B2, public Boxable<int>{\n"
				"    public:\n"
				"    int get() { return x + y; }\n"
				"}\n"
				"void main() {\n"
				"    D d = D();\n"
				"    Boxable<int> b = d;\n"
				"    print(b.get());\n"
				"}\n",
				Output,
					&Error),
			"multi-base + generic interface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "3\n", "MI + generic iface output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Parent default method via generic child.
	if (!Expect(
			Run(
				"interface IParent<T> {\n"
				"    void hello() { print(\"hi\"); }\n"
				"    virtual T get() = 0;\n"
				"}\n"
				"interface IChild<T> : IParent<T> {\n"
				"    void bye() { print(\"bye\"); }\n"
				"}\n"
				"class C : public IChild<int>{\n"
				"    public:\n"
				"    int get() { return 3; }\n"
				"}\n"
				"void main() {\n"
				"    IChild<int> x = C();\n"
				"    x.hello();\n"
				"    x.bye();\n"
				"    print(x.get());\n"
				"    IParent<int> y = x;\n"
				"    y.hello();\n"
				"}\n",
				Output,
					&Error),
			"parent default via generic child"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "hi\nbye\n3\nhi\n", "parent default via child output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// ---- V2-D15: open generic class/struct implements interface
	if (!Expect(
			Run(
				"interface Boxable<T> {\n"
				"    virtual T get() = 0;\n"
				"    virtual void set(T v) = 0;\n"
				"}\n"
				"class Box<T> : public Boxable<T>{\n"
				"    public:\n"
				"    T value;\n"
				"    T get() { return value; }\n"
				"    void set(T v) { value = v; }\n"
				"}\n"
				"void main() {\n"
				"    Box<int> b = Box<int>();\n"
				"    Boxable<int> x = b;\n"
				"    x.set(42);\n"
				"    print(x.get());\n"
				"}\n",
				Output,
					&Error),
			"open generic class Box<T>:public Boxable<T>"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "42\n", "open generic Box upcast output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface I<T> { virtual T get() = 0; }\n"
				"struct S<T> : I<T> {\n"
				"    T v;\n"
				"    T get() { return v; }\n"
				"}\n"
				"void main() {\n"
				"    S<int> s = S<int>();\n"
				"    s.v = 7;\n"
				"    print(s.get());\n"
				"}\n",
				Output,
					&Error),
			"open generic struct implements I<T>"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "7\n", "generic struct impl output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface I<T> { virtual T get() = 0; }\n"
				"struct S<T> : I<T> {\n"
				"    T get() { return 9; }\n"
				"}\n"
				"void main() {\n"
				"    S<int> s = S<int>();\n"
				"    I<int> x = s;\n"
				"    print(x.get());\n"
				"}\n",
				Output,
					&Error),
			"open generic struct assign to interface (value-copy box)"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "9\n", "open generic struct→interface call output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface Boxable<T> { virtual T get() = 0; }\n"
				"class Box<T> : public Boxable<T>{\n"
				"    public:\n"
				"}\n"
				"void main() { Box<int> b = Box<int>(); }\n",
					&Error),
			"missing method on generic impl fails at instantiation"))
	{
		std::cerr << "expected missing method fail, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface Greeter<T> {\n"
				"    void hello() { print(\"hi\"); }\n"
				"    virtual T id(T v) = 0;\n"
				"}\n"
				"class C<T> : public Greeter<T>{\n"
				"    public:\n"
				"    T id(T v) { return v; }\n"
				"}\n"
				"void main() {\n"
				"    Greeter<int> g = C<int>();\n"
				"    g.hello();\n"
				"    print(g.id(3));\n"
				"}\n",
				Output,
					&Error),
			"default method on generic class without override"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "hi\n3\n", "generic class default method output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface Boxable<T> { virtual T get() = 0; }\n"
				"class Box<T> : public Boxable<T>{\n"
				"    public:\n"
				"    T value;\n"
				"    T get() { return value; }\n"
				"}\n"
				"void main() {\n"
				"    Box<string> b = Box<string>();\n"
				"    Boxable<int> x = b;\n"
				"}\n",
					&Error),
			"Boxable<int> rejects Box<string>"))
	{
		std::cerr << "expected type mismatch fail, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface PairLike<T,U> {\n"
				"    virtual T first() = 0;\n"
				"    virtual U second() = 0;\n"
				"}\n"
				"class PairBox<T,U> : public PairLike<T,U>{\n"
				"    public:\n"
				"    T a;\n"
				"    U b;\n"
				"    T first() { return a; }\n"
				"    U second() { return b; }\n"
				"}\n"
				"void main() {\n"
				"    PairBox<int,string> p = PairBox<int,string>();\n"
				"    p.a = 1;\n"
				"    p.b = \"z\";\n"
				"    PairLike<int,string> x = p;\n"
				"    print(x.first());\n"
				"    print(x.second());\n"
				"}\n",
				Output,
					&Error),
			"PairBox<T,U>:PairLike<T,U>"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "1\nz\n", "PairBox output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class B<T> {\n"
				"    public:\n"
				"    T v;\n"
				"    T get() { return v; }\n"
				"}\n"
				"class D<T> : public B<T>{\n"
				"    public:\n"
				"    T w;\n"
				"}\n"
				"void main() {\n"
				"    D<int> d = D<int>();\n"
				"    d.v = 7;\n"
				"    d.w = 8;\n"
				"    B<int> b = d;\n"
				"    print(b.get());\n"
				"    print(d.w);\n"
				"}\n",
				Output,
					&Error),
			"open generic class D<T>:public B<T>"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "7\n8\n", "generic class inherit output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"struct SB<T> { T v; }\n"
				"struct SD<T> : SB<T> { T w; }\n"
				"void main() {\n"
				"    SD<int> d = SD<int>();\n"
				"    d.v = 3;\n"
				"    SB<int> b = d;\n"
				"    print(b.v);\n"
				"}\n",
				Output,
					&Error),
			"open generic struct SD<T>:SB<T>"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "3\n", "generic struct inherit output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IGet<T> { virtual T get() = 0; }\n"
				"class B<T> { public: T v; }\n"
				"class D<T> : public B<T>, public IGet<T>{\n"
				"    public:\n"
				"    T get() { return v; }\n"
				"}\n"
				"void main() {\n"
				"    D<int> d = D<int>();\n"
				"    d.v = 9;\n"
				"    IGet<int> x = d;\n"
				"    print(x.get());\n"
				"}\n",
				Output,
					&Error),
			"generic class base + interface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "9\n", "generic base+iface output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class B<T> {\n"
				"    public:\n"
				"    T v;\n"
				"    B(T x) { v = x; }\n"
				"}\n"
				"class D<T> : public B<T>{\n"
				"    public:\n"
				"    D(T x) : B(x) {}\n"
				"}\n"
				"void main() {\n"
				"    D<int> d = D<int>(5);\n"
				"    print(d.v);\n"
				"}\n",
				Output,
					&Error),
			"generic ctor chain D(T):B(T)"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "5\n", "generic ctor chain output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class B<T> {\n"
				"    public:\n"
				"    T v;\n"
				"    T get() { return v; }\n"
				"}\n"
				"class D<T> : public B<T> {}\n"
				"void main() {\n"
				"    D<int> d = D<int>();\n"
				"    d.v = 11;\n"
				"    print(d.get());\n"
				"}\n",
				Output,
					&Error),
			"call inherited method on D<T> as D"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "11\n", "inherited method via D output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface I {\n"
				"    virtual int f() = 0;\n"
				"    int f(int x) { return x; }\n"
				"}\n"
				"interface J : I {\n"
				"    int f() { return 7; }\n"
				"}\n"
				"class C : public J {}\n"
				"void main() {\n"
				"    C c = C();\n"
				"    J j = c;\n"
				"    print(j.f());\n"
				"    print(j.f(3));\n"
				"}\n",
				Output,
					&Error),
			"interface child override keeps parent arity overload"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "7\n3\n", "interface arity flatten output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class B<T> {\n"
				"    public:\n"
				"    T pad;\n"
				"    virtual int id() { return 1; }\n"
				"}\n"
				"class D<T> : public B<T>{\n"
				"    public:\n"
				"    int id() override { return 9; }\n"
				"}\n"
				"void main() {\n"
				"    D<int> d = D<int>();\n"
				"    B<int> b = d;\n"
				"    print(b.id());\n"
				"}\n",
				Output,
					&Error),
			"open generic virtual override"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "9\n", "generic virtual override output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// ---- V2-D21: open generic multi-base
	if (!Expect(
			Run(
				"class B1<T> { public: T a; }\n"
				"class B2<T> { public: T b; }\n"
				"class D<T> : public B1<T>, public B2<T> {}\n"
				"void main() {\n"
				"    D<int> d = D<int>();\n"
				"    d.a = 1;\n"
				"    d.b = 2;\n"
				"    B1<int> x = d;\n"
				"    B2<int> y = d;\n"
				"    print(x.a);\n"
				"    print(y.b);\n"
				"}\n",
				Output,
					&Error),
			"open multi-base D<T>:B1<T>,B2<T>"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "1\n2\n", "open multi-base fields output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class B1 { public: int a; }\n"
				"class B2<T> { public: T b; }\n"
				"class D<T> : public B1, public B2<T> {}\n"
				"void main() {\n"
				"    D<int> d = D<int>();\n"
				"    d.a = 3;\n"
				"    d.b = 4;\n"
				"    print(d.a);\n"
				"    print(d.b);\n"
				"}\n",
				Output,
					&Error),
			"mixed concrete + open multi-base"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "3\n4\n", "mixed multi-base output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IGet<T> { virtual T get() = 0; }\n"
				"class B1<T> { public: T a; }\n"
				"class B2<T> { public: T b; }\n"
				"class D<T> : public B1<T>, public B2<T>, public IGet<T>{\n"
				"    public:\n"
				"    T get() { return a; }\n"
				"}\n"
				"void main() {\n"
				"    D<int> d = D<int>();\n"
				"    d.a = 5;\n"
				"    IGet<int> i = d;\n"
				"    print(i.get());\n"
				"}\n",
				Output,
					&Error),
			"open multi-base + interface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "5\n", "open multi-base + iface output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class B1<T> {\n"
				"    public:\n"
				"    T a;\n"
				"    B1(T x) { a = x; }\n"
				"}\n"
				"class B2<T> {\n"
				"    public:\n"
				"    T b;\n"
				"    B2(T y) { b = y; }\n"
				"}\n"
				"class D<T> : public B1<T>, public B2<T>{\n"
				"    public:\n"
				"    D(T x, T y) : B1(x), B2(y) {}\n"
				"}\n"
				"void main() {\n"
				"    D<int> d = D<int>(6, 7);\n"
				"    print(d.a);\n"
				"    print(d.b);\n"
				"}\n",
				Output,
					&Error),
			"open multi-base ctor inits"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "6\n7\n", "open multi-base ctor output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class B1<T> {\n"
				"    public:\n"
				"    T pad;\n"
				"    virtual int id() { return 1; }\n"
				"}\n"
				"class B2<T> {\n"
				"    public:\n"
				"    T pad2;\n"
				"    virtual int id() { return 2; }\n"
				"}\n"
				"class D<T> : public B1<T>, public B2<T>{\n"
				"    public:\n"
				"    int id() override { return 9; }\n"
				"}\n"
				"void main() {\n"
				"    D<int> d = D<int>();\n"
				"    B1<int> a = d;\n"
				"    B2<int> b = d;\n"
				"    print(a.id());\n"
				"    print(b.id());\n"
				"}\n",
				Output,
					&Error),
			"open multi-base override disambiguates"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "9\n9\n", "open multi-base override output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class B1<T> { public: int f() { return 1; } }\n"
				"class B2<T> { public: int f() { return 2; } }\n"
				"class D<T> : public B1<T>, public B2<T> {}\n"
				"void main() { D<int> d = D<int>(); print(d.f()); }\n",
					&Error),
			"open multi-base method ambiguity fail"))
	{
		std::cerr << "expected ambiguity error, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class A<T> { public: T v; }\n"
				"class Mid1<T> : public A<T> {}\n"
				"class Mid2<T> : public A<T> {}\n"
				"class D<T> : public Mid1<T>, public Mid2<T> {}\n"
				"void main() { D<int> d = D<int>(); }\n",
					&Error),
			"open generic diamond rejected"))
	{
		std::cerr << "expected diamond error, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class B1<T> { public: T a; }\n"
				"class B2<T> { public: T b; }\n"
				"class D<T> : public B1, public B2 {}\n"
				"void main() { D<int> d = D<int>(); }\n",
					&Error),
			"bare open template multi-base rejected"))
	{
		std::cerr << "expected bare-template reject, got success\n";
		return EXIT_FAILURE;
	}

	// Multi-parent generic interfaces
	if (!Expect(
			Run(
				"interface IA<T> { virtual T a() = 0; }\n"
				"interface IB<T> { virtual T b() = 0; }\n"
				"interface IC<T> : IA<T>, IB<T> {}\n"
				"class C : public IC<int>{\n"
				"    public:\n"
				"    int a() { return 1; }\n"
				"    int b() { return 2; }\n"
				"}\n"
				"void main() {\n"
				"    IC<int> c = C();\n"
				"    print(c.a());\n"
				"    print(c.b());\n"
				"    IA<int> a = c;\n"
				"    print(a.a());\n"
				"}\n",
				Output,
					&Error),
			"generic multi-parent interface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "1\n2\n1\n", "generic multi-parent output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Generic parent method T m() unifies with concrete int m() from sibling.
	if (!Expect(
			Run(
				"interface IA<T> { virtual T m() = 0; }\n"
				"interface IB { virtual int m() = 0; }\n"
				"interface IC<T> : IA<T>, IB {}\n"
				"class C : public IC<int>{\n"
				"    public:\n"
				"    int m() { return 7; }\n"
				"}\n"
				"void main() {\n"
				"    IC<int> c = C();\n"
				"    print(c.m());\n"
				"}\n",
				Output,
					&Error),
			"generic+concrete same signature after subst"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "7\n", "generic+concrete unify output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(Run(R"vs(
interface Base<T> {
    T read(const T& n) { return n; }
    static T copy(T n) { return n; }
};
interface Left<U> : Base<U> {};
interface Right<V> : Base<V> {};
interface Child<W> : Left<W>, Right<W> {};
class F : Child<int> {};
void main() {
    Child<int> i = F(); int n = 7;
    print(i.read(ref n));
    print(Child<int>::copy(8));
    print(Child<long>::copy(9i64));
}
)vs", Output, &Error), "inherited generic methods retain declaring specialization through a diamond")
		|| !Expect(Output == "7\n8\n9\n", "inherited generic method and static diamond output"))
	{
		std::cerr << Error << " output=[" << Output << "]\n";
		return EXIT_FAILURE;
	}

	std::cout << "VeroScriptGenericInterfaceTests passed\n";
	return EXIT_SUCCESS;
}
