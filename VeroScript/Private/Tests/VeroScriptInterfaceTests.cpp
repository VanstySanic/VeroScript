// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptEngine.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include "VeroScriptTestTemporaryDirectory.h"

#include "VeroScriptAst.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
		VeroScript::VeroScriptLexer Lexer(Source, "iface.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "iface.vs");
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
		VeroScript::VeroScriptLexer Lexer(Source, "iface.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "iface.vs");
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
	const VeroScript::Tests::FScopedTemporaryDirectory TemporaryDirectory(
		"VeroScript-interface-");
	if (!Expect(static_cast<bool>(TemporaryDirectory),
		"create unique interface test directory"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
		Run(
			"interface IValue { virtual int Get() const = 0; };\n"
			"class Value : public IValue { public: int Get() const { return 7; } };\n"
			"void main() { Value value(); print(value.Get()); }\n",
			Output,
			&Error),
		"interface declaration accepts the recommended trailing semicolon"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "7\n", "semicolon-terminated interface executes"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"class Dog : public ISpeak{\n"
				"    public:\n"
				"    void speak() { print(\"woof\"); }\n"
				"}\n"
				"void main() {\n"
				"    ISpeak a = Dog();\n"
				"    a.speak();\n"
				"}\n",
				Output,
					&Error),
			"class impl + upcast + dynamic call"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "woof\n", "class impl output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"class Cat : public ISpeak{\n"
				"    public:\n"
				"}\n"
				"void main() { Cat cat = Cat(); }\n",
					&Error),
			"missing abstract method fail"))
	{
		std::cerr << "expected compile failure, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"struct Point : ISpeak {\n"
				"    void speak() { print(\"hi\"); }\n"
				"}\n"
				"void main() {\n"
				"    Point p = Point();\n"
				"    ISpeak a = p;\n"
				"    a.speak();\n"
				"}\n",
				Output,
					&Error),
			"struct assign to interface (value-copy box)"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "hi\n", "struct→interface call output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Value-copy box: mutating via I does not change the original struct.
	if (!Expect(
			Run(
				"interface ICounter {\n"
				"    virtual void bump() = 0;\n"
				"    virtual int get() = 0;\n"
				"}\n"
				"struct C : ICounter {\n"
				"    int n;\n"
				"    void bump() { this.n = this.n + 1; }\n"
				"    int get() { return this.n; }\n"
				"}\n"
				"void main() {\n"
				"    C s = C();\n"
				"    s.n = 0;\n"
				"    ICounter x = s;\n"
				"    x.bump();\n"
				"    print(s.get());\n"
				"    print(x.get());\n"
				"}\n",
				Output,
					&Error),
			"struct→interface copy isolation"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "0\n1\n", "copy isolation output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Temporary construction boxes in one step.
	if (!Expect(
			Run(
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"struct Point : ISpeak {\n"
				"    void speak() { print(\"tmp\"); }\n"
				"}\n"
				"void main() {\n"
				"    ISpeak a = Point();\n"
				"    a.speak();\n"
				"}\n",
				Output,
					&Error),
			"temp struct→interface box"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "tmp\n", "temp box output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Default method via boxed interface handle.
	if (!Expect(
			Run(
				"interface IGreet {\n"
				"    void greet() { print(\"boxed-default\"); }\n"
				"}\n"
				"struct S : IGreet {\n"
				"}\n"
				"void main() {\n"
				"    IGreet g = S();\n"
				"    g.greet();\n"
				"}\n",
				Output,
					&Error),
			"default method via boxed interface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "boxed-default\n", "boxed default output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Interface handle identity: I→I must share the same boxed copy.
	if (!Expect(
			Run(
				"interface ICounter {\n"
				"    virtual void bump() = 0;\n"
				"    virtual int get() = 0;\n"
				"}\n"
				"struct C : ICounter {\n"
				"    int n;\n"
				"    void bump() { this.n = this.n + 1; }\n"
				"    int get() { return this.n; }\n"
				"}\n"
				"void main() {\n"
				"    C s = C();\n"
				"    s.n = 0;\n"
				"    ICounter a = s;\n"
				"    ICounter b = a;\n"
				"    b.bump();\n"
				"    print(a.get());\n"
				"    print(b.get());\n"
				"    print(s.get());\n"
				"}\n",
				Output,
					&Error),
			"I→I shares boxed handle"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "1\n1\n0\n", "I→I identity output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Unboxing interface → struct is still forbidden.
	if (!Expect(
			CompileFails(
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"struct Point : ISpeak {\n"
				"    void speak() { print(\"hi\"); }\n"
				"}\n"
				"void main() {\n"
				"    ISpeak a = Point();\n"
				"    Point p = a;\n"
				"}\n",
					&Error),
			"interface→struct unbox fail"))
	{
		std::cerr << "expected unbox error, got success\n";
		return EXIT_FAILURE;
	}

	// Non-implementor cannot box.
	if (!Expect(
			CompileFails(
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"struct Point {\n"
				"    int v;\n"
				"}\n"
				"void main() {\n"
				"    Point p = Point();\n"
				"    ISpeak a = p;\n"
				"}\n",
					&Error),
			"non-implementor box fail"))
	{
		std::cerr << "expected non-implementor error, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"struct Point : ISpeak {\n"
				"    void speak() { print(\"pt\"); }\n"
				"}\n"
				"void main() {\n"
				"    Point p = Point();\n"
				"    p.speak();\n"
				"}\n",
				Output,
					&Error),
			"struct impl OK via method bind"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "pt\n", "struct impl output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string name;\n"
				"}\n"
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"class Dog : public Animal, public ISpeak{\n"
				"    public:\n"
				"    void speak() { print(this.name); }\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    d.name = \"Rex\";\n"
				"    ISpeak a = d;\n"
				"    a.speak();\n"
				"}\n",
				Output,
					&Error),
			"class : public Base, public I"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "Rex\n", "class Base+I output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IParent {\n"
				"    virtual void hello() = 0;\n"
				"}\n"
				"interface IChild : IParent {\n"
				"}\n"
				"class Impl : public IChild{\n"
				"    public:\n"
				"    void hello() { print(\"flat\"); }\n"
				"}\n"
				"void main() {\n"
				"    IChild c = Impl();\n"
				"    c.hello();\n"
				"}\n",
				Output,
					&Error),
			"interface parent flatten"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "flat\n", "flatten output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IGreet {\n"
				"    void greet() { print(\"default\"); }\n"
				"}\n"
				"class Person : public IGreet{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    IGreet g = Person();\n"
				"    g.greet();\n"
				"}\n",
				Output,
					&Error),
			"default method without override"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "default\n", "default method output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IGreet {\n"
				"    void greet() { print(\"default\"); }\n"
				"}\n"
				"class Person : public IGreet{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    Person p = Person();\n"
				"    p.greet();\n"
				"}\n",
				Output,
					&Error),
			"default method via class receiver"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "default\n", "default method class-receiver output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IGreet {\n"
				"    void greet() { print(\"default\"); }\n"
				"}\n"
				"struct S : IGreet {\n"
				"}\n"
				"void main() {\n"
				"    S s = S();\n"
				"    s.greet();\n"
				"}\n",
				Output,
					&Error),
			"default method via struct receiver"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "default\n", "default method struct-receiver output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Struct CallBoundMethod must bind to the declaring parent, not the listed child.
	if (!Expect(
			Run(
				"interface IParent {\n"
				"    void hello() { print(\"hi\"); }\n"
				"}\n"
				"interface IChild : IParent {}\n"
				"struct S : IChild {}\n"
				"void main() {\n"
				"    S s = S();\n"
				"    s.hello();\n"
				"}\n",
				Output,
					&Error),
			"struct inherited parent default"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "hi\n", "struct inherited parent default output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface ISpeak {\n"
				"    virtual void speak() = 0;\n"
				"}\n"
				"class Animal : public ISpeak{\n"
				"    public:\n"
				"    void speak() { print(\"a\"); }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    ISpeak x = Dog();\n"
				"    x.speak();\n"
				"}\n",
				Output,
					&Error),
			"child of implementor upcasts to I"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "a\n", "child-of-implementor output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IParent {\n"
				"    virtual void hello() = 0;\n"
				"}\n"
				"interface IChild : IParent {\n"
				"}\n"
				"class Impl : public IChild{\n"
				"    public:\n"
				"    void hello() { print(\"up\"); }\n"
				"}\n"
				"void main() {\n"
				"    IParent p = Impl();\n"
				"    p.hello();\n"
				"}\n",
				Output,
					&Error),
			"upcast to parent interface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "up\n", "parent-interface upcast output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IGreet {\n"
				"    virtual void greet() { print(\"default\"); }\n"
				"}\n"
				"class Person : public IGreet{\n"
				"    public:\n"
				"    void greet() { print(\"override\"); }\n"
				"}\n"
				"void main() {\n"
				"    IGreet g = Person();\n"
				"    g.greet();\n"
				"}\n",
				Output,
					&Error),
			"override virtual default method"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "override\n", "override virtual default output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface IFoo {\n"
				"    virtual void m() = 0;\n"
				"}\n"
				"void main() {\n"
				"    IFoo x = IFoo();\n"
				"}\n",
					&Error),
			"IFoo() construct fail"))
	{
		std::cerr << "expected construct fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IFoo {\n"
				"    virtual void m() = 0;\n"
				"}\n"
				"class C : public IFoo{\n"
				"    public:\n"
				"    void m() {}\n"
				"}\n"
				"void main() {\n"
				"    IFoo x = nullptr;\n"
				"    print(x == nullptr);\n"
				"}\n",
				Output,
					&Error),
			"nullptr to interface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "true\n", "nullptr output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// V2-D13: two unrelated class bases are allowed (diamond still rejected).
	if (!Expect(
			Run(
				"class A { public: int x = 1; }\n"
				"class B { public: int y = 2; }\n"
				"class C : public A, public B {}\n"
				"void main() {\n"
				"    C c = C();\n"
				"    print(c.x + c.y);\n"
				"}\n",
				Output,
					&Error),
			"two class bases ok"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "3\n", "two class bases output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class A {}\n"
				"class B : public A {}\n"
				"class C : public A {}\n"
				"class D : public B, public C {}\n"
				"void main() {}\n",
					&Error),
			"diamond inheritance fail"))
	{
		std::cerr << "expected diamond fail: " << Error << '\n';
		return EXIT_FAILURE;
	}

	// Probe: IChild-typed value upcast to IParent
	if (!Expect(
			Run(
				"interface IParent {\n"
				"    virtual void hello() = 0;\n"
				"}\n"
				"interface IChild : IParent {\n"
				"}\n"
				"class Impl : public IChild{\n"
				"    public:\n"
				"    void hello() { print(\"up\"); }\n"
				"}\n"
				"void main() {\n"
				"    IChild c = Impl();\n"
				"    IParent p = c;\n"
				"    p.hello();\n"
				"}\n",
				Output,
					&Error),
			"iface-typed upcast to parent iface"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "up\n", "iface-typed parent upcast output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Probe: a default method may implicitly call another instance method.
	if (!Expect(
			Run(
				"interface I {\n"
				"    virtual void speak() = 0;\n"
				"    void shout() { speak(); }\n"
				"}\n"
				"class Dog : public I{\n"
				"    public:\n"
				"    void speak() { print(\"woof\"); }\n"
				"}\n"
				"void main() {\n"
				"    I x = Dog();\n"
				"    x.shout();\n"
				"}\n",
				Output,
					&Error),
			"default calls implicit-this abstract"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "woof\n", "default implicit-this abstract output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Probe: child inherits interface defaults from base type
	if (!Expect(
			Run(
				"interface IGreet {\n"
				"    void greet() { print(\"hi\"); }\n"
				"}\n"
				"class Animal : public IGreet{\n"
				"    public:\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    d.greet();\n"
				"}\n",
				Output,
					&Error),
			"inherited default via child type"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "hi\n", "inherited default via child output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Probe: class method on base beats virtual interface default for CallMethod
	if (!Expect(
			Run(
				"interface I {\n"
				"    virtual void m() { print(\"iface\"); }\n"
				"}\n"
				"class Base : public I{\n"
				"    public:\n"
				"    void m() { print(\"base\"); }\n"
				"}\n"
				"class Child : public Base{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    I x = Child();\n"
				"    x.m();\n"
				"}\n",
				Output,
					&Error),
			"base method beats virtual iface default"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "base\n", "base-beats-default output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Probe: re-abstracting a default parent method requires implementation
	if (!Expect(
			CompileFails(
				"interface IParent {\n"
				"    void hello() { print(\"d\"); }\n"
				"}\n"
				"interface IChild : IParent {\n"
				"    virtual void hello() = 0;\n"
				"}\n"
				"class Impl : public IChild{\n"
				"    public:\n"
				"}\n"
				"void main() { Impl value = Impl(); }\n",
					&Error),
			"reabstract default must implement"))
	{
		std::cerr << "expected reabstract implement fail\n";
		return EXIT_FAILURE;
	}

	// Probe: wrong arity on interface-typed receiver
	if (!Expect(
			CompileFails(
				"interface I {\n"
				"    virtual void m(int x) = 0;\n"
				"}\n"
				"class C : public I{\n"
				"    public:\n"
				"    void m(int x) {}\n"
				"}\n"
				"void main() {\n"
				"    I x = C();\n"
				"    x.m();\n"
				"}\n",
					&Error),
			"wrong arity on iface receiver"))
	{
		std::cerr << "expected arity fail\n";
		return EXIT_FAILURE;
	}

	// Ambiguous sibling interface defaults require override
	if (!Expect(
			CompileFails(
				"interface IA {\n"
				"    void m() { print(\"A\"); }\n"
				"}\n"
				"interface IB {\n"
				"    void m() { print(\"B\"); }\n"
				"}\n"
				"class C : public IA, public IB{\n"
				"    public:\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"ambiguous sibling defaults fail"))
	{
		std::cerr << "expected ambiguous fail, err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}
	if (!Expect(
			Error.find("ambiguous interface method") != std::string::npos,
			"ambiguous error message"))
	{
		std::cerr << "got err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	// Inherited interface + new sibling with same default is also ambiguous
	if (!Expect(
			CompileFails(
				"interface IA {\n"
				"    void m() { print(\"A\"); }\n"
				"}\n"
				"interface IB {\n"
				"    void m() { print(\"B\"); }\n"
				"}\n"
				"class Base : public IA{\n"
				"    public:\n"
				"}\n"
				"class Child : public Base, public IB{\n"
				"    public:\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"inherited+sibling ambiguous defaults fail"))
	{
		std::cerr << "expected inherited ambiguous fail, err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	// Override clears ambiguity (virtual defaults remain dynamically dispatchable)
	if (!Expect(
			Run(
				"interface IA {\n"
				"    virtual void m() { print(\"A\"); }\n"
				"}\n"
				"interface IB {\n"
				"    virtual void m() { print(\"B\"); }\n"
				"}\n"
				"class C : public IA, public IB{\n"
				"    public:\n"
				"    void m() { print(\"C\"); }\n"
				"}\n"
				"void main() {\n"
				"    IA a = C();\n"
				"    a.m();\n"
				"}\n",
				Output,
					&Error),
			"override clears ambiguity"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "C\n", "override disambiguate output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Default methods cannot access implementor fields via this
	if (!Expect(
			CompileFails(
				"interface I {\n"
				"    void show() { print(this.name); }\n"
				"}\n"
				"class C : public I{\n"
				"    public:\n"
				"    string name;\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"default this.field rejected"))
	{
		std::cerr << "expected this.field fail, err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	// Multi-parent interfaces: flatten, upcast, diamond
	if (!Expect(
			Run(
				"interface IA { virtual void a() = 0; }\n"
				"interface IB { virtual void b() = 0; }\n"
				"interface IC : IA, IB {}\n"
				"class C : public IC{\n"
				"    public:\n"
				"    void a() { print(\"a\"); }\n"
				"    void b() { print(\"b\"); }\n"
				"}\n"
				"void main() {\n"
				"    IC c = C();\n"
				"    c.a();\n"
				"    c.b();\n"
				"    IA a = c;\n"
				"    a.a();\n"
				"    IB b = c;\n"
				"    b.b();\n"
				"}\n",
				Output,
					&Error),
			"multi-parent interface run"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "a\nb\na\nb\n", "multi-parent interface output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IS { void s() { print(\"s\"); } }\n"
				"interface IA : IS {}\n"
				"interface IB : IS {}\n"
				"interface IC : IA, IB {}\n"
				"class C : public IC {}\n"
				"void main() {\n"
				"    IC c = C();\n"
				"    c.s();\n"
				"}\n",
				Output,
					&Error),
			"interface diamond run"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "s\n", "interface diamond output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface IA { void m() { print(\"A\"); } }\n"
				"interface IB { void m() { print(\"B\"); } }\n"
				"interface IC : IA, IB {}\n"
				"void main() {}\n",
					&Error),
			"multi-parent sibling default clash"))
	{
		std::cerr << "expected default clash, err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IA { void m() { print(\"A\"); } }\n"
				"interface IB { void m() { print(\"B\"); } }\n"
				"interface IC : IA, IB {\n"
				"    void m() { print(\"C\"); }\n"
				"}\n"
				"class C : public IC {}\n"
				"void main() {\n"
				"    IC c = C();\n"
				"    c.m();\n"
				"}\n",
				Output,
					&Error),
			"multi-parent redeclare resolves defaults"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "C\n", "multi-parent redeclare output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface IA { virtual int m() = 0; }\n"
				"interface IB { virtual void m() = 0; }\n"
				"interface IC : IA, IB {}\n"
				"void main() {}\n",
					&Error),
			"multi-parent signature conflict"))
	{
		std::cerr << "expected signature conflict, err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	// Wrong arity redeclare must NOT clear sibling default clash.
	if (!Expect(
			CompileFails(
				"interface IA { void m() { print(\"A\"); } }\n"
				"interface IB { void m() { print(\"B\"); } }\n"
				"interface IC : IA, IB {\n"
				"    void m(int x) { print(x); }\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"wrong-arity redeclare does not clear default clash"))
	{
		std::cerr << "expected default clash after wrong-arity redeclare, err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface A : B<int> {}\n"
				"interface B<T> : A {}\n"
				"void main() {}\n",
					&Error),
			"generic interface cycle rejected"))
	{
		std::cerr << "expected interface cycle, err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	// Module import of exported interface
	{
		const std::filesystem::path TempRoot =
			TemporaryDirectory.Get() / "import-interface";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream LibFile(TempRoot / "iface_lib.vs");
			LibFile << "export interface ISpeak {\n"
				<< "    virtual void speak() = 0;\n"
				<< "}\n";
		}
		{
			std::ofstream MainFile(TempRoot / "main.vs");
			MainFile << "import \"iface_lib.vs\";\n"
				<< "class Dog : public ISpeak{\n"
				<< "    public:\n"
				<< "    void speak() { print(\"woof\"); }\n"
				<< "}\n"
				<< "void main() {\n"
				<< "    ISpeak a = Dog();\n"
				<< "    a.speak();\n"
				<< "}\n";
		}
		std::ostringstream ImportOutput;
		std::ostringstream ImportErrors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").string(),
			ImportOutput,
			ImportErrors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "imported interface should run"))
		{
			std::cerr << ImportErrors.str() << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(ImportOutput.str() == "woof\n", "imported interface output"))
		{
			std::cerr << "got: [" << ImportOutput.str() << "]\n";
			return EXIT_FAILURE;
		}
	}

	if (!Expect(
			CompileFails(
				"interface A { virtual void f() = 0; }\n"
				"interface B : A, A {}\n"
				"void main() {}\n",
					&Error),
			"duplicate parent interface rejected"))
	{
		std::cerr << "expected duplicate parent interface, err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface ISpeak { virtual void speak() = 0; }\n"
				"using Voice = ISpeak;\n"
				"interface ILoud : Voice {\n"
				"    void speak() { print(\"loud\"); }\n"
				"}\n"
				"class Cat : public ILoud {}\n"
				"void main() {\n"
				"    ILoud c = Cat();\n"
				"    c.speak();\n"
				"}\n",
				Output,
					&Error),
			"interface parent via type alias"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "loud\n", "alias parent interface output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Non-virtual interface default cannot be shadowed by an own method: dispatch
	// would depend on the static reference type. Require `virtual` (see next case).
	if (!Expect(
			CompileFails(
				"interface IGreet {\n"
				"    void greet() { print(\"default\"); }\n"
				"}\n"
				"class Person : public IGreet{\n"
				"    public:\n"
				"    void greet() { print(\"override\"); }\n"
				"}\n"
				"void main() {\n"
				"    Person p = Person();\n"
				"    p.greet();\n"
				"}\n",
					&Error),
			"non-virtual default shadow rejected"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	// Shadow rejection also covers an interface inherited via a base class: the
	// derived class must not add a same-signature method over a non-virtual default.
	if (!Expect(
			CompileFails(
				"interface IGreet {\n"
				"    void greet() { print(\"default\"); }\n"
				"}\n"
				"class Base : public IGreet{\n"
				"    public:\n"
				"}\n"
				"class Derived : public Base{\n"
				"    public:\n"
				"    void greet() { print(\"derived\"); }\n"
				"}\n"
				"void main() {\n"
				"    Derived d = Derived();\n"
				"    d.greet();\n"
				"}\n",
					&Error),
			"inherited non-virtual default shadow rejected"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	// Different param shape (ref vs by-value) is a distinct overload — not shadowing.
	if (!Expect(
			Run(
				"interface I {\n"
				"    void f(int x) { print(\"default\"); }\n"
				"}\n"
				"class C : public I{\n"
				"    public:\n"
				"    void f(ref int x) { print(\"ref\"); }\n"
				"}\n"
				"void main() {\n"
				"    int n = 1;\n"
				"    I i = C();\n"
				"    i.f(n);\n"
				"    C c = C();\n"
				"    c.f(ref n);\n"
				"}\n",
				Output,
					&Error),
			"ref overload is not non-virtual shadow"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "default\nref\n", "ref overload is not non-virtual shadow output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// An interface may declare abstract-method overloads that differ by parameter type
	// (same as classes); same name + same arity is not by itself a conflict.
	if (!Expect(
			Run(
				"interface I {\n"
				"    virtual int f(int x) = 0;\n"
				"    virtual int f(string x) = 0;\n"
				"}\n"
				"class C : public I{\n"
				"    public:\n"
				"    int f(int x) { return x + 1; }\n"
				"    int f(string x) { return 0; }\n"
				"}\n"
				"void main() {\n"
				"    C c = C();\n"
				"    print(c.f(5));\n"
				"    print(c.f(\"hi\"));\n"
				"}\n",
				Output,
					&Error),
			"interface method overload by parameter type"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "6\n0\n", "interface method overload by parameter type output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// A return-type-only difference on the same interface signature is still a conflict.
	if (!Expect(
			CompileFails(
				"interface I {\n"
				"    virtual int f(int x) = 0;\n"
				"    virtual string f(int x) = 0;\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"interface return-type-only clash rejected"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	// A non-virtual default with no shadowing own method binds statically and is
	// callable through both interface and concrete receivers (single implementation).
	if (!Expect(
			Run(
				"interface IGreet {\n"
				"    void greet() { print(\"default\"); }\n"
				"}\n"
				"class Person : public IGreet{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    IGreet g = Person();\n"
				"    g.greet();\n"
				"    Person p = Person();\n"
				"    p.greet();\n"
				"}\n",
				Output,
					&Error),
			"non-virtual default static bind"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "default\ndefault\n", "non-virtual default static bind output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Interface access domains + private helper used by default body.
	if (!Expect(
			Run(
				"interface ICalc {\n"
				"public:\n"
				"    int value() { return this.secret(); }\n"
				"private:\n"
				"    int secret() { return 42; }\n"
				"}\n"
				"class Box : public ICalc {\n"
				"public:\n"
				"}\n"
				"void main() {\n"
				"    ICalc c = Box();\n"
				"    print(c.value());\n"
				"}\n",
				Output,
					&Error),
			"interface private default helper"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "42\n", "interface private default helper output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}
	if (!Expect(
			CompileFails(
				"interface ICalc {\n"
				"private:\n"
				"    int secret() { return 1; }\n"
				"}\n"
				"class Box : public ICalc {\n"
				"public:\n"
				"}\n"
				"void main() {\n"
				"    ICalc c = Box();\n"
				"    print(c.secret());\n"
				"}\n",
					&Error),
			"private interface method inaccessible"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
			CompileFails(
				"interface IFoo {\n"
				"private:\n"
				"    virtual void m() = 0;\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"abstract interface method must be public"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	// Pure virtual syntax is explicit. A body-less interface declaration is not
	// silently promoted to a virtual slot.
	if (!Expect(
			CompileFails(
				"interface ITick { void Tick(); }\n"
				"void main() {}\n",
				&Error),
			"bare interface declaration is rejected")
		|| !Expect(
			Error.find("= 0") != std::string::npos,
			"bare interface declaration diagnostic suggests explicit pure syntax"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Invalid { public: void Tick() = 0; }\n"
				"void main() {}\n",
				&Error),
			"non-virtual pure method is rejected")
		|| !Expect(
			CompileFails(
				"class Invalid { public: virtual final void Tick() = 0; }\n"
				"void main() {}\n",
				&Error),
			"final pure method is rejected"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IBase { virtual int Value() = 0; }\n"
				"interface IMiddle : IBase { int Value() override = 0; }\n"
				"class Concrete : public IMiddle {\n"
				"public:\n"
				"    int Value() override { return 8; }\n"
				"}\n"
				"void main() { IMiddle value = Concrete(); print(value.Value()); }\n",
				Output,
				&Error),
			"derived interface explicit pure override")
		|| !Expect(Output == "8\n", "derived pure interface override output")
		|| !Expect(
			CompileFails(
				"interface IInvalid { int Missing() override = 0; }\n"
				"void main() {}\n",
				&Error),
			"interface override requires a matching virtual base"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	// C++-style declarations and same-file definitions cover constructors,
	// const methods, pure method bodies, and pure virtual destructors.
	if (!Expect(
			Run(
				"class AbstractCounter {\n"
				"public:\n"
				"    int value;\n"
				"    AbstractCounter(int seed);\n"
				"    int Add(int delta) const;\n"
				"    virtual int Score() = 0;\n"
				"    virtual ~AbstractCounter() = 0;\n"
				"}\n"
				"AbstractCounter::AbstractCounter(int seed) { this.value = seed; }\n"
				"int AbstractCounter::Add(int delta) const { return this.value + delta; }\n"
				"int AbstractCounter::Score() { return this.value; }\n"
				"AbstractCounter::~AbstractCounter() { print(\"base-dtor\"); }\n"
				"class Counter : public AbstractCounter {\n"
				"public:\n"
				"    Counter(int seed) : AbstractCounter(seed) {}\n"
				"    int Score() override { return this.Add(1); }\n"
				"    ~Counter() { print(\"derived-dtor\"); }\n"
				"}\n"
				"void main() {\n"
				"    Counter counter = Counter(6);\n"
				"    print(counter.Score());\n"
				"}\n",
				Output,
				&Error),
			"same-file member definitions and pure virtual destructor")
		|| !Expect(
			Output == "7\nderived-dtor\nbase-dtor\n",
			"same-file definition execution and destructor order"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class AbstractCounter {\n"
				"public:\n"
				"    virtual int Score() = 0;\n"
				"}\n"
				"void main() { AbstractCounter value = AbstractCounter(); }\n",
				&Error),
			"abstract type construction is rejected")
		|| !Expect(
			Error.find("abstract type") != std::string::npos,
			"abstract type construction diagnostic"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"namespace Sample {\n"
				"    class Value {\n"
				"    public:\n"
				"        int number;\n"
				"        Value(int initial);\n"
				"        int Read() const;\n"
				"        static int Twice(int input);\n"
				"    }\n"
				"    Value::Value(int initial) { this.number = initial; }\n"
				"    int Value::Read() const { return this.number; }\n"
				"    int Value::Twice(int input) { return input * 2; }\n"
				"}\n"
				"void main() {\n"
				"    Sample::Value value = Sample::Value(5);\n"
				"    print(value.Read());\n"
				"    print(Sample::Value::Twice(6));\n"
				"}\n",
				Output,
				&Error),
			"namespace-qualified and static same-file definitions")
		|| !Expect(Output == "5\n12\n", "namespace out-of-class definition output"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class MissingDefinition { public: int Value(); }\n"
				"void main() {}\n",
				&Error),
			"unresolved same-file method declaration is rejected")
		|| !Expect(
			CompileFails(
				"class MissingDestructor { public: virtual ~MissingDestructor() = 0; }\n"
				"void main() {}\n",
				&Error),
			"pure virtual destructor requires a definition")
		|| !Expect(
			CompileFails(
				"class Duplicate { public: int Value(); }\n"
				"int Duplicate::Value() { return 1; }\n"
				"int Duplicate::Value() { return 2; }\n"
				"void main() {}\n",
				&Error),
			"duplicate out-of-class definition is rejected"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	// Interface field state slices
	{
		std::string FieldOutput;
		std::string FieldError;
		if (!Expect(
				Run(
					"interface ICounter {\n"
					"    int n = 0;\n"
					"    void bump() { this.n = this.n + 1; }\n"
					"}\n"
					"class Box : public ICounter {\n"
					"public:\n"
					"}\n"
					"void main() {\n"
					"    Box b = Box();\n"
					"    b.bump();\n"
					"    b.bump();\n"
					"    print(b.n);\n"
					"    ICounter c = b;\n"
					"    print(c.n);\n"
					"}\n",
					FieldOutput,
						&FieldError),
				"interface field slice"))
		{
			std::cerr << FieldError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(FieldOutput == "2\n2\n", "interface field slice output"))
		{
			std::cerr << "got: [" << FieldOutput << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"interface IBase {\n"
					"    int x = 1;\n"
					"}\n"
					"interface ILeft : IBase {}\n"
					"interface IRight : IBase {}\n"
					"class C : public ILeft, public IRight {\n"
					"public:\n"
					"}\n"
					"void main() {\n"
					"    C c = C();\n"
					"    print(c.x);\n"
					"}\n",
					FieldOutput,
						&FieldError),
				"interface field diamond dedupe"))
		{
			std::cerr << FieldError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(FieldOutput == "1\n", "interface field diamond output"))
		{
			std::cerr << "got: [" << FieldOutput << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"interface IA { int n = 1; }\n"
					"interface IB { int n = 2; }\n"
					"class C : public IA, public IB {\n"
					"public:\n"
					"}\n"
					"void main() {}\n",
						&FieldError),
				"sibling interface field conflict"))
		{
			std::cerr << FieldError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"interface I {\n"
					"public:\n"
					"    virtual final void F() { print(\"d\"); }\n"
					"}\n"
					"class C : public I {\n"
					"public:\n"
					"    void F() { print(\"c\"); }\n"
					"}\n"
					"void main() {}\n",
						&FieldError),
				"cannot override virtual final interface default"))
		{
			std::cerr << FieldError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"interface IBase {\n"
					"public:\n"
					"    virtual final void F() { print(\"b\"); }\n"
					"}\n"
					"interface IDer : public IBase {\n"
					"public:\n"
					"    virtual void F() { print(\"d\"); }\n"
					"}\n"
					"void main() {}\n",
						&FieldError),
				"cannot override virtual final in derived interface"))
		{
			std::cerr << FieldError << '\n';
			return EXIT_FAILURE;
		}
	}

	std::cout << "VeroScriptInterfaceTests passed\n";
	return EXIT_SUCCESS;
}
