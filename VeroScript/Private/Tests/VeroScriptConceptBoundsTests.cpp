// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAst.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
	bool Expect(const bool Condition, const char* Label)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}
		return Condition;
	}

	bool Run(const std::string& Source, std::string& OutOutput, std::string* OutError = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "bounds.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "bounds.vs");
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
		VeroScript::VeroScriptLexer Lexer(Source, "bounds.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "bounds.vs");
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
		"namespace Contract{interface I{virtual int read()=0;}"
		"interface Box<T>{virtual T get()=0;}}"
		"using Bound=Contract::I;using BoundAgain=Bound;using BoxBound=Contract::Box<int>;"
		"struct Item:public Contract::I{int read(){return 7;}};"
		"class IntBox:public Contract::Box<int>{public:int get(){return 8;}};"
		"int Read<T>(T item) where T:BoundAgain{return item.read();}"
		"int Unbox<T>(T item) where T:BoxBound{return item.get();}"
		"struct Holder<T> where T:BoundAgain{T item;int get(){return item.read();}};"
		"class Receiver<T> where T:BoxBound{public:T item;int get(){return item.get();}};"
		"void main(){print(Read(Item()));print(Unbox(IntBox()));"
		"Holder<Item> h=Holder<Item>();h.item=Item();print(h.get());"
		"Receiver<IntBox> r=Receiver<IntBox>();r.item=IntBox();print(r.get());}",
		Output, &Error), "interface aliases are valid function/struct/class constraints") ||
		!Expect(Output == "7\n8\n7\n8\n", "aliased constraints preserve method dispatch"))
	{
		std::cerr << Error << '\n' << Output;
		return EXIT_FAILURE;
	}
	if (!Expect(CompileFails("using Bound=int;void Read<T>(T item) where T:Bound{}void main(){}", &Error),
			"non-interface aliases remain invalid constraints") ||
		!Expect(CompileFails("interface I{};using Bound=I;void Read<T>(T item) where T:Bound{}"
							 "void main(){Read<int>(1);}",
					&Error),
			"aliased constraints reject non-implementers"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IHasId {\n"
				"    virtual int id() = 0;\n"
				"}\n"
				"class Dog : public IHasId{\n"
				"    public:\n"
				"    int id() { return 7; }\n"
				"}\n"
				"int getId<T>(T x) where T : IHasId {\n"
				"    return x.id();\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    print(getId(d));\n"
				"}\n",
				Output,
					&Error),
			"bounds ok")
				|| !Expect(Output == "7\n", "bounds ok output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface IHasId {\n"
				"    virtual int id() = 0;\n"
				"}\n"
				"class Cat {\n"
				"    public:\n"
				"    int id() { return 1; }\n"
				"}\n"
				"int getId<T>(T x) where T : IHasId {\n"
				"    return x.id();\n"
				"}\n"
				"void main() {\n"
				"    Cat c = Cat();\n"
				"    print(getId(c));\n"
				"}\n",
					&Error),
			"bounds reject non-implementer")
				|| !Expect(
					Error.find("where T : IHasId") != std::string::npos
						|| Error.find("does not satisfy") != std::string::npos,
					"bounds error mentions where clause"))
	{
		std::cerr << "expected bound fail with where clause, got: " << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void f(int x) where T : IFoo {}\n"
				"void main() {}\n",
					&Error),
			"where on non-generic rejected"))
	{
		std::cerr << "expected non-generic where fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface IFoo { virtual void f() = 0; }\n"
				"void id<T>(T x) where T : int {}\n"
				"void main() {}\n",
					&Error),
			"bound must be interface"))
	{
		std::cerr << "expected non-interface bound fail: " << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IA { virtual int a() = 0; }\n"
				"interface IB { virtual int b() = 0; }\n"
				"class Pair : public IA, public IB{\n"
				"    public:\n"
				"    int a() { return 1; }\n"
				"    int b() { return 2; }\n"
				"}\n"
				"int both<T>(T x) where T : IA, IB {\n"
				"    return x.a() + x.b();\n"
				"}\n"
				"void main() {\n"
				"    print(both(Pair()));\n"
				"}\n",
				Output,
					&Error),
			"multi interface bounds")
				|| !Expect(Output == "3\n", "multi bound output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IHasId {\n"
				"    virtual int id() = 0;\n"
				"}\n"
				"class Dog : public IHasId{\n"
				"    public:\n"
				"    int id() { return 11; }\n"
				"}\n"
				"class Box<T> where T : IHasId{\n"
				"    public:\n"
				"    T item;\n"
				"    int get() { return item.id(); }\n"
				"}\n"
				"void main() {\n"
				"    Box<Dog> b = Box<Dog>();\n"
				"    b.item = Dog();\n"
				"    print(b.get());\n"
				"}\n",
				Output,
					&Error),
			"class where bounds ok")
				|| !Expect(Output == "11\n", "class where output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface IHasId {\n"
				"    virtual int id() = 0;\n"
				"}\n"
				"class Cat {\n"
				"    public:\n"
				"    int id() { return 1; }\n"
				"}\n"
				"class Box<T> where T : IHasId{\n"
				"    public:\n"
				"    T item;\n"
				"}\n"
				"void main() {\n"
				"    Box<Cat> b = Box<Cat>();\n"
				"}\n",
					&Error),
			"class where rejects non-implementer"))
	{
		std::cerr << "expected class bound fail, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface ITag { virtual int tag() = 0; }\n"
				"struct Mark : ITag {\n"
				"    int tag() { return 4; }\n"
				"}\n"
				"struct Wrap<T> where T : ITag {\n"
				"    T value;\n"
				"}\n"
				"void main() {\n"
				"    Wrap<Mark> w = Wrap<Mark>();\n"
				"    w.value = Mark();\n"
				"    print(w.value.tag());\n"
				"}\n",
				Output,
					&Error),
			"struct where bounds ok")
				|| !Expect(Output == "4\n", "struct where output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface ITag { virtual int tag() = 0; }\n"
				"class Plain {}\n"
				"interface IHolder<T> where T : ITag {\n"
				"    virtual T get() = 0;\n"
				"}\n"
				"class Bad : public IHolder<Plain>{\n"
				"    public:\n"
				"    Plain get() { return Plain(); }\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"interface where rejects non-implementer"))
	{
		std::cerr << "expected interface bound fail, got success: " << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IEq<T> { virtual bool eq(T o) = 0; }\n"
				"class Dog : public IEq<Dog>{\n"
				"    public:\n"
				"    bool eq(Dog o) { return true; }\n"
				"}\n"
				"class Cat : public IEq<Cat>{\n"
				"    public:\n"
				"    bool eq(Cat o) { return true; }\n"
				"}\n"
				"class Inner<T> where T : IEq<T>{\n"
				"    public:\n"
				"    T v;\n"
				"}\n"
				"class Outer<T> where T : IEq<T>{\n"
				"    public:\n"
				"    Inner<Cat> x;\n"
				"    T own;\n"
				"}\n"
				"void main() {\n"
				"    Outer<Dog> o = Outer<Dog>();\n"
				"    o.x = Inner<Cat>();\n"
				"    o.x.v = Cat();\n"
				"    o.own = Dog();\n"
				"    print(o.x.v.eq(Cat()) ? 1 : 0);\n"
				"}\n",
				Output,
					&Error),
			"F-bound nested type-param collision")
				|| !Expect(Output == "1\n", "F-bound nested output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IBox<T> { virtual T get() = 0; }\n"
				"class NumBox : public IBox<int>{\n"
				"    public:\n"
				"    int get() { return 9; }\n"
				"}\n"
				"int unwrap<T>(T x) where T : IBox<int> {\n"
				"    return x.get();\n"
				"}\n"
				"void main() {\n"
				"    print(unwrap(NumBox()));\n"
				"}\n",
				Output,
					&Error),
			"type-app bound IBox<int>")
				|| !Expect(Output == "9\n", "type-app bound output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface IBox<T> { virtual T get() = 0; }\n"
				"void f<T>(T x) where T : IBox {}\n"
				"void main() {}\n",
					&Error),
			"bare generic bound rejected"))
	{
		std::cerr << "expected bare IBox fail, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface IBox<T> { virtual T get() = 0; }\n"
				"void f<U>(U x) where U : IBox<int, string> {}\n"
				"void main() {}\n",
					&Error),
			"bound arity mismatch rejected"))
	{
		std::cerr << "expected bound arity fail, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IBox<T> { virtual T get() = 0; }\n"
				"class IntBox : public IBox<int>{\n"
				"    public:\n"
				"    int get() { return 5; }\n"
				"}\n"
				"class Holder<T, U> where T : IBox<U>{\n"
				"    public:\n"
				"    T item;\n"
				"    U read() { return item.get(); }\n"
				"}\n"
				"void main() {\n"
				"    Holder<IntBox, int> h = Holder<IntBox, int>();\n"
				"    h.item = IntBox();\n"
				"    print(h.read());\n"
				"}\n",
				Output,
					&Error),
			"where T : IBox<U> multi-param")
				|| !Expect(Output == "5\n", "multi-param where output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	std::cout << "VeroScriptConceptBoundsTests passed\n";
	return EXIT_SUCCESS;
}
