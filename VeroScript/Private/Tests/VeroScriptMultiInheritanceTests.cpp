// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
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
		VeroScript::VeroScriptLexer Lexer(Source, "mi.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "mi.vs");
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
		VeroScript::VeroScriptLexer Lexer(Source, "mi.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "mi.vs");
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

	bool Contains(const std::string& Haystack, const char* Needle)
	{
		return Haystack.find(Needle) != std::string::npos;
	}
}

int main()
{
	{
		std::string Output;
		std::string Error;
		const bool Ok = Run(
			"class B1 { public: int x = 1; }\n"
			"class B2 { public: int y = 2; }\n"
			"class D : public B1, public B2 { public: int z = 3; }\n"
			"void main() {\n"
			"    D d = D();\n"
			"    print(d.x);\n"
			"    print(d.y);\n"
			"    print(d.z);\n"
			"}\n",
			Output,
				&Error);
		if (!Expect(Ok, "fields from both bases should run")
			|| !Expect(Output == "1\n2\n3\n", "field layout output mismatch"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = Run(
			"class B1 { public: int x = 10; }\n"
			"class B2 { public: int y = 20; }\n"
			"class D : public B1, public B2 {}\n"
			"void main() {\n"
			"    D d = D();\n"
			"    B1 a = d;\n"
			"    B2 b = d;\n"
			"    print(a.x);\n"
			"    print(b.y);\n"
			"}\n",
			Output,
				&Error);
		if (!Expect(Ok, "upcast to B1 and B2 should run")
			|| !Expect(Output == "10\n20\n", "upcast output mismatch"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class A {}\n"
					"class B : public A {}\n"
					"class C : public A {}\n"
					"class D : public B, public C {}\n"
					"void main() {}\n",
						&Error),
				"diamond should fail")
					|| !Expect(Contains(Error, "diamond"), "diamond error should mention diamond"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"struct S1 { int a; }\n"
					"struct S2 { int b; }\n"
					"struct D : S1, S2 {}\n"
					"void main() {}\n",
						&Error),
				"struct two bases should fail")
					|| !Expect(
						Contains(Error, "struct supports only single inheritance"),
						"struct multi-base error message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = Run(
			"class B1 {\n"
			"    public:\n"
			"    int x;\n"
			"    B1(int n) { x = n; }\n"
			"}\n"
			"class B2 {\n"
			"    public:\n"
			"    int y;\n"
			"    B2(int n) { y = n; }\n"
			"}\n"
			"class D : public B1, public B2{\n"
			"    public:\n"
			"    D() : B1(1), B2(2) {}\n"
			"}\n"
			"void main() {\n"
			"    D d = D();\n"
			"    print(d.x);\n"
			"    print(d.y);\n"
			"}\n",
			Output,
				&Error);
		if (!Expect(Ok, "ctor : B1(), B2() should run")
			|| !Expect(Output == "1\n2\n", "multi base ctor output mismatch"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class B1 {\n"
					"    public:\n"
					"    int x;\n"
					"    B1(int n) { x = n; }\n"
					"}\n"
					"class B2 {\n"
					"    public:\n"
					"    int y = 0;\n"
					"}\n"
					"class D : public B1, public B2{\n"
					"    public:\n"
					"    D() : B2() {}\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"omit non-default base should fail")
					|| !Expect(Contains(Error, "default constructor"), "omit base error"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class B1 { public: int f() { return 1; } }\n"
					"class B2 { public: int f() { return 2; } }\n"
					"class D : public B1, public B2 {}\n"
					"void main() {\n"
					"    D d = D();\n"
					"    print(d.f());\n"
					"}\n",
						&Error),
				"method ambiguity should fail")
					|| !Expect(Contains(Error, "ambiguous"), "ambiguity error message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = Run(
			"class B1 { public: int f() { return 1; } }\n"
			"class B2 { public: int f() { return 2; } }\n"
			"class D : public B1, public B2{\n"
			"    public:\n"
			"    int f() { return 3; }\n"
			"}\n"
			"void main() {\n"
			"    D d = D();\n"
			"    print(d.f());\n"
			"}\n",
			Output,
				&Error);
		if (!Expect(Ok, "override should resolve ambiguity")
			|| !Expect(Output == "3\n", "override output mismatch"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = Run(
			"class B1 {\n"
			"    public:\n"
			"    virtual string Speak() { return \"b1\"; }\n"
			"}\n"
			"class B2 { public: int y = 0; }\n"
			"class D : public B1, public B2{\n"
			"    public:\n"
			"    string Speak() { return \"d\"; }\n"
			"}\n"
			"void main() {\n"
			"    D d = D();\n"
			"    B1 a = d;\n"
			"    print(a.Speak());\n"
			"}\n",
			Output,
				&Error);
		if (!Expect(Ok, "virtual via B1 handle should dispatch")
			|| !Expect(Output == "d\n", "virtual dispatch output mismatch"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = Run(
			"interface IFoo {\n"
			"    virtual int Tag() = 0;\n"
			"}\n"
			"class B1 { public: int x = 1; }\n"
			"class B2 { public: int y = 2; }\n"
			"class D : public B1, public B2, public IFoo{\n"
			"    public:\n"
			"    int Tag() { return x + y; }\n"
			"}\n"
			"void main() {\n"
			"    D d = D();\n"
			"    IFoo i = d;\n"
			"    print(i.Tag());\n"
			"}\n",
			Output,
				&Error);
		if (!Expect(Ok, "class D : B1, B2, IFoo should run")
			|| !Expect(Output == "3\n", "interface + multi base output mismatch"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	// Non-virtual base method must stay CallBoundMethod even when D also lists an interface
	// with a same-named default (wrong emit would dynamically hit E::f).
	{
		std::string Output;
		std::string Error;
		const bool Ok = Run(
			"interface IFoo {\n"
			"    int f() { return 0; }\n"
			"}\n"
			"class B1 { public: int f() { return 1; } }\n"
			"class D : public B1, public IFoo {}\n"
			"class E : public D { public: int f() { return 2; } }\n"
			"void main() {\n"
			"    D d = E();\n"
			"    print(d.f());\n"
			"}\n",
			Output,
				&Error);
		if (!Expect(Ok, "non-virtual bind with interface present")
			|| !Expect(Output == "1\n", "non-virtual should not dispatch to E"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class B1 { public: int x = 1; }\n"
					"class B2 { public: int x = 2; }\n"
					"class D : public B1, public B2 {}\n"
					"void main() {}\n",
						&Error),
				"field collision across bases should fail")
					|| !Expect(Contains(Error, "collision"), "field collision message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	// Probe suite for logic-chain review (temporary; consolidate if all pass).
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int x; B1() { x = 1; } }\n"
					"class B2 { public: int y; B2() { y = 2; } }\n"
					"class D : public B1, public B2 {}\n"
					"void main() { D d = D(); print(d.x); print(d.y); }\n",
					Output,
						&Error),
				"implicit multi default ctors")
					|| !Expect(Output == "1\n2\n", "implicit multi default ctors output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int x; B1(int n) { x = n; print(1); } }\n"
					"class B2 { public: int y; B2(int n) { y = n; print(2); } }\n"
					"class D : public B1, public B2 { public: D() : B2(20), B1(10) {} }\n"
					"void main() { D d = D(); print(d.x); print(d.y); }\n",
					Output,
						&Error),
				"ctor call order = decl order")
					|| !Expect(Output == "1\n2\n10\n20\n", "ctor decl-order output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class A { public: int x = 1; }\n"
					"class Mid : public A {}\n"
					"class B { public: int x = 2; }\n"
					"class D : public Mid, public B {}\n"
					"void main() {}\n",
						&Error),
				"non-adjacent field collision"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"interface IFoo { virtual int t() = 0; }\n"
					"class B1 : public IFoo { public: int t() { return 1; } }\n"
					"class B2 : public IFoo { public: int t() { return 2; } }\n"
					"class D : public B1, public B2 { public: int t() { return 3; } }\n"
					"void main() { D d = D(); print(d.t()); }\n",
					Output,
						&Error),
				"shared iface not diamond")
					|| !Expect(Output == "3\n", "shared iface output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int x = 1; }\n"
					"class B2 { public: int y = 2; }\n"
					"class D : public B1, public B2 {}\n"
					"void foo(B2 b) { print(b.y); }\n"
					"void main() { D d = D(); foo(d); }\n",
					Output,
						&Error),
				"upcast second base via param")
					|| !Expect(Output == "2\n", "upcast second base output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class B1 { public: B1(int n) {} }\n"
					"class B2 { public: B2() {} }\n"
					"class D : public B1, public B2 { public: D() {} }\n"
					"void main() {}\n",
						&Error),
				"omit nondefault among multi user ctor"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class Animal { public: int n = 0; void bump() { n = n + 1; } }\n"
					"class Dog : public Animal {}\n"
					"void main() { Dog d = Dog(); d.bump(); print(d.n); }\n",
					Output,
						&Error),
				"SI LoadThisField via derived")
					|| !Expect(Output == "1\n", "SI LoadThisField output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int x = 0; void bx() { x = 7; } }\n"
					"class B2 { public: int y = 0; void by() { y = 8; } }\n"
					"class D : public B1, public B2 {}\n"
					"void main() { D d = D(); d.bx(); d.by(); print(d.x); print(d.y); }\n",
					Output,
						&Error),
				"MI LoadThisField both bases")
					|| !Expect(Output == "7\n8\n", "MI LoadThisField output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class A1 { public: int f() { return 1; } }\n"
					"class A2 { public: int f() { return 2; } }\n"
					"class B : public A1 {}\n"
					"class C : public A2 {}\n"
					"class D : public B, public C {}\n"
					"void main() { D d = D(); print(d.f()); }\n",
						&Error),
				"nested method ambiguity")
					|| !Expect(Contains(Error, "ambiguous"), "nested ambiguity message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class A { public: int n; A() { n = 5; } }\n"
					"class B1 : public A {}\n"
					"class B2 { public: int y = 9; }\n"
					"class D : public B1, public B2 { public: D() : B1() {} }\n"
					"void main() { D d = D(); print(d.n); print(d.y); }\n",
					Output,
						&Error),
				"explicit empty B1() grandparent")
					|| !Expect(Output == "5\n9\n", "grandparent via explicit B1()"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class A { public: int a = 1; }\n"
					"class B { public: int b = 2; }\n"
					"class C { public: int c = 3; }\n"
					"class D : public A, public B, public C { public: int d = 4; }\n"
					"void main() { D x = D(); print(x.a); print(x.b); print(x.c); print(x.d); }\n",
					Output,
						&Error),
				"three bases flatten order")
					|| !Expect(Output == "1\n2\n3\n4\n", "three bases output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int x = 0; }\n"
					"class B2 { public: virtual string Speak() { return \"b2\"; } }\n"
					"class D : public B1, public B2 { public: string Speak() { return \"d\"; } }\n"
					"void main() { D d = D(); B2 b = d; print(b.Speak()); }\n",
					Output,
						&Error),
				"virtual on second base")
					|| !Expect(Output == "d\n", "second-base virtual output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"interface I { virtual int t() = 0; }\n"
					"class B1 { public: int x = 1; }\n"
					"class B2 { public: int y = 2; }\n"
					"class D : public I, public B1, public B2 { public: int t() { return x+y; } }\n"
					"void main() { D d = D(); I i = d; print(i.t()); }\n",
					Output,
						&Error),
				"iface then two class bases")
					|| !Expect(Output == "3\n", "iface+B1+B2 output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails("class C {public: }\nstruct S : C {}\nvoid main() { }\n", &Error),
				"struct inherits class"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails("struct S {}\nclass C : public S {}\nvoid main() {}\n", &Error),
				"class inherits struct"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails("class B {public: }\nclass D : public B, public B {}\nvoid main() { }\n", &Error),
				"duplicate base"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int x = 1; }\n"
					"class B2 { public: int f() { return 42; } }\n"
					"class D : public B1, public B2 {}\n"
					"void main() { D d = D(); print(d.f()); }\n",
					Output,
						&Error),
				"method only on second base")
					|| !Expect(Output == "42\n", "second-base method output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int x; B1() { x = 1; print(1); } }\n"
					"class B2 { public: int y; B2(int n) { y = n; print(2); } }\n"
					"class D : public B1, public B2 { public: D() : B2(9) {} }\n"
					"void main() { D d = D(); print(d.x); print(d.y); }\n",
					Output,
						&Error),
				"omit defaultable with explicit other")
					|| !Expect(Output == "1\n2\n1\n9\n", "omit+explicit output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class B1 { public: B1(int n) {} }\n"
					"class B2 { public: int y = 0; }\n"
					"class D : public B1, public B2 {}\n"
					"void main() { D d = D(); }\n",
						&Error),
				"implicit D() nondefault base"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int f() { return 1; } }\n"
					"class B2 { public: int f() { return 2; } }\n"
					"class D : public B1, public B2 {}\n"
					"void main() { D d = D(); print(1); }\n",
					Output,
						&Error),
				"ambiguous decls without call ok")
					|| !Expect(Output == "1\n", "no-call ambig output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"interface I { virtual int t() = 0; }\n"
					"class B1 { public: int x = 0; }\n"
					"class B2 : public I { public: int t() { return 7; } }\n"
					"class D : public B1, public B2 {}\n"
					"void main() { D d = D(); I i = d; print(i.t()); }\n",
					Output,
						&Error),
				"iface via second base upcast")
					|| !Expect(Output == "7\n", "iface via second base output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class A { public: int n; A() { n = n + 1; } }\n"
					"class B1 : public A { public: B1() {} }\n"
					"class B2 { public: int y; B2() { y = 9; } }\n"
					"class D : public B1, public B2 {}\n"
					"void main() { D d = D(); print(d.n); print(d.y); }\n",
					Output,
						&Error),
				"implicit D() calls B1() once then B2()")
					|| !Expect(Output == "1\n9\n", "no double A via B1"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class A {}\n"
					"class B2 : public A {}\n"
					"class D : public A, public B2 {}\n"
					"void main() {}\n",
						&Error),
				"D : A, B2 where B2:A is diamond")
					|| !Expect(Contains(Error, "diamond"), "repeated base diamond message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int f(int x) { return x; } }\n"
					"class B2 { public: int f() { return 2; } }\n"
					"class D : public B1, public B2 {}\n"
					"void main() { D d = D(); print(d.f()); print(d.f(9)); }\n",
					Output,
						&Error),
				"cross-base arity overloads")
					|| !Expect(Output == "2\n9\n", "arity overload output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B1 { public: int x = 0; }\n"
					"class B2 { public: virtual int f() { return 1; } }\n"
					"class Mid : public B1, public B2 {}\n"
					"class D : public Mid { public: int f() { return 5; } }\n"
					"void main() { D d = D(); Mid m = d; B2 b = d; print(m.f()); print(b.f()); }\n",
					Output,
						&Error),
				"virtual override through Mid multi")
					|| !Expect(Output == "5\n5\n", "virtual through Mid output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class Grand1 { public: int f() { return 1; } }\n"
					"class Grand2 { public: int f() { return 2; } }\n"
					"class Mid : public Grand1, public Grand2 {}\n"
					"class D : public Mid {}\n"
					"void main() { D d = D(); print(d.f()); }\n",
						&Error),
				"nested Mid ambiguity via D.f()")
					|| !Expect(Contains(Error, "ambiguous"), "nested Mid ambiguity message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class Grand1 { public: int f() { return 1; } }\n"
					"class Grand2 { public: int f() { return 2; } }\n"
					"class Mid : public Grand1, public Grand2 {}\n"
					"void main() { Mid m = Mid(); print(m.f()); }\n",
						&Error),
				"direct Mid ambiguity")
					|| !Expect(Contains(Error, "ambiguous"), "direct Mid ambiguity message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class Grand1 { public: int f() { return 1; } }\n"
					"class Grand2 { public: int f() { return 2; } }\n"
					"class Mid : public Grand1, public Grand2 { public: int f() { return 9; } }\n"
					"class D : public Mid {}\n"
					"void main() { D d = D(); print(d.f()); }\n",
					Output,
						&Error),
				"Mid override clears nested ambiguity")
					|| !Expect(Output == "9\n", "Mid override output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class C {\n"
					"    public:\n"
					"  int f() { return 1; }\n"
					"  int f(int x) { return x; }\n"
					"  int f(int x, int y) { return x + y; }\n"
					"}\n"
					"void main() {\n"
					"  C c = C();\n"
					"  print(c.f());\n"
					"  print(c.f(7));\n"
					"  print(c.f(2, 3));\n"
					"}\n",
					Output,
						&Error),
				"same-type arity overloads")
					|| !Expect(Output == "1\n7\n5\n", "same-type overload output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class C {\n"
					"    public:\n"
					"  int f() { return 1; }\n"
					"  int f() { return 2; }\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"same arity overload rejected")
					|| !Expect(Contains(Error, "duplicate method"), "duplicate method message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B {\n"
					"    public:\n"
					"  virtual int f() { return 1; }\n"
					"  virtual int f(int x) { return x; }\n"
					"}\n"
					"class D : public B{\n"
					"    public:\n"
					"  int f() { return 9; }\n"
					"}\n"
					"void main() {\n"
					"  D d = D();\n"
					"  B b = d;\n"
					"  print(b.f());\n"
					"  print(b.f(4));\n"
					"}\n",
					Output,
						&Error),
				"virtual arity overloads + override")
					|| !Expect(Output == "9\n4\n", "virtual overload override output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class C {\n"
					"    public:\n"
					"  string f(int x) { return \"i\"; }\n"
					"  string f(string s) { return s; }\n"
					"}\n"
					"void main() {\n"
					"  C c = C();\n"
					"  print(c.f(1));\n"
					"  print(c.f(\"ok\"));\n"
					"}\n",
					Output,
						&Error),
				"same-arity param-type overloads")
					|| !Expect(Output == "i\nok\n", "param-type overload output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Error;
		if (!Expect(
				CompileFails(
					"class C {\n"
					"    public:\n"
					"  void f(int x) {}\n"
					"  void f(int y) {}\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"same signature overload rejected")
					|| !Expect(Contains(Error, "duplicate method"), "duplicate method message"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class B {\n"
					"    public:\n"
					"  virtual string f(int x) { return \"b\"; }\n"
					"  virtual string f(string s) { return s; }\n"
					"}\n"
					"class D : public B{\n"
					"    public:\n"
					"  string f(int x) { return \"d\"; }\n"
					"}\n"
					"void main() {\n"
					"  D d = D();\n"
					"  B b = d;\n"
					"  print(b.f(1));\n"
					"  print(b.f(\"z\"));\n"
					"}\n",
					Output,
						&Error),
				"virtual param-type overloads + override")
					|| !Expect(Output == "d\nz\n", "virtual type-overload override output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output;
		std::string Error;
		if (!Expect(
				Run(
					"class Base {}\n"
					"class Derived : public Base {}\n"
					"class C {\n"
					"    public:\n"
					"  string f(Base b) { return \"base\"; }\n"
					"  string f(Derived d) { return \"derived\"; }\n"
					"}\n"
					"void main() {\n"
					"  C c = C();\n"
					"  Derived d = Derived();\n"
					"  Base b = d;\n"
					"  print(c.f(d));\n"
					"  print(c.f(b));\n"
					"}\n",
					Output,
						&Error),
				"subtype prefers exact overload")
					|| !Expect(Output == "derived\nbase\n", "subtype overload output"))
		{
			std::cerr << Error << '\n'
					  << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output;
		std::string Error;
		if (!Expect(Run(R"vs(
class Base<T> { public: T read(const T& n = 4) { return n; } };
class Child<U> : public Base<U> {
public: long result() { long n = Super.read(); return n; }
};
void main() { Child<long> c = Child<long>(); print(c.result()); }
)vs", Output, &Error), "Super substitutes the declaring base's generic return type")
			|| !Expect(Output == "4\n", "generic Super default result"))
		{
			std::cerr << Error << '\n' << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(Run(R"vs(
class Base { public:
    int read(int n) { return n; }
    string read(const int& n) { return "ok"; }
};
class Child : public Base { public:
    string result() { int n = 4; string s = Super.read(ref n); return s; }
};
void main() { Child c = Child(); print(c.result()); }
)vs", Output, &Error), "Super return inference preserves explicit reference arguments")
			|| !Expect(Output == "ok\n", "Super reference overload result"))
		{
			std::cerr << Error << '\n' << Output;
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output;
		std::string Error;
		if (!Expect(Run(R"vs(
class Base<T> { public: T n = 3; T& read() { return n; } };
class Child<U> : public Base<U> { public:
    void bump() { U& item = Super.read(); item++; }
};
void main() { Child<int> c = Child<int>(); c.bump(); print(c.n); }
)vs", Output, &Error), "Super substitutes generic reference results")
			|| !Expect(Output == "4\n", "Super generic reference aliases base storage"))
		{
			std::cerr << Error << '\n' << Output;
			return EXIT_FAILURE;
		}
	}

	{
		std::string Error;
		if (!Expect(CompileFails(R"vs(
class Base { public: int read() = delete; };
class Child : public Base { public: int call() { return Super.read(); } };
void main() { Child c = Child(); print(c.call()); }
)vs", &Error), "Super rejects deleted methods during compilation")
			|| !Expect(Contains(Error, "deleted"), "Super reports the deleted declaration"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	std::cout << "VeroScriptMultiInheritanceTests: OK\n";
	return EXIT_SUCCESS;
}
