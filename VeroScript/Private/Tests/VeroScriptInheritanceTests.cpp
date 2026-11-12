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
		VeroScript::VeroScriptLexer Lexer(Source, "inh.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "inh.vs");
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
		VeroScript::VeroScriptLexer Lexer(Source, "inh.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "inh.vs");
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

	// Bare calls in an instance body fall back to `this` only after ordinary
	// call targets fail. This covers overloads, inherited non-virtual methods,
	// virtual dispatch, declaration order, defaults, and local/free shadowing.
	if (!Expect(
			Run(
				"int Pick(int value) { return 100; }\n"
				"class ImplicitBase {\n"
				"public:\n"
				"    virtual int VirtualValue(int value) { return value + 1; }\n"
				"    int Inherited(int value) { return value + 20; }\n"
				"    int Overload(int value) { return value + 30; }\n"
				"    int Overload(string value) { return len(value) + 40; }\n"
				"    int Pick(int value) { return value + 1; }\n"
				"    int Local(int value) { return value + 900; }\n"
				"}\n"
				"class ImplicitDerived : ImplicitBase {\n"
				"public:\n"
				"    static int StaticValue() { return 9; }\n"
				"    int VirtualValue(int value) override { return value + 10; }\n"
				"    int RunCalls() {\n"
				"        fn(int)->int Local = fn(int value) -> int => value + 7;\n"
				"        return VirtualValue(1) + Inherited(2)\n"
				"            + Overload(3) + Overload(\"four\")\n"
				"            + DeclaredLater(5) + Defaulted(1)\n"
				"            + Pick(0) + Local(1) + StaticValue();\n"
				"    }\n"
				"    int DeclaredLater(int value) { return value * 2; }\n"
				"    int Defaulted(int value, int extra = 5) { return value + extra; }\n"
				"}\n"
				"void main() {\n"
				"    ImplicitDerived value = ImplicitDerived();\n"
				"    print(value.RunCalls());\n"
				"}\n",
				Output,
				&Error),
			"implicit this method resolution"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "243\n", "implicit this resolution and shadow priority output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class StaticContext {\n"
				"public:\n"
				"    int InstanceOnly() { return 1; }\n"
				"    static int Bad() { return InstanceOnly(); }\n"
				"}\n"
				"void main() {}\n",
				&Error),
			"static method must not receive implicit this"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string name;\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    int age;\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    d.name = \"Rex\";\n"
				"    print(d.name);\n"
				"}\n",
				Output,
					&Error),
			"class field inherit"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "Rex\n", "class field inherit output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"struct Point {\n"
				"    int x;\n"
				"}\n"
				"struct Point3 : Point {\n"
				"    int z;\n"
				"}\n"
				"void main() {\n"
				"    Point3 p = Point3();\n"
				"    p.x = 3;\n"
				"    print(p.x);\n"
				"}\n",
				Output,
					&Error),
			"struct field inherit"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "3\n", "struct field inherit output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Animal { public: int x; }\n"
				"struct Dog : Animal { }\n"
				"void main() {}\n",
					&Error),
			"reject class:public struct cross-kind"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"struct Animal { int x; }\n"
				"class Dog : public Animal {public:  }\n"
				"void main() {}\n",
					&Error),
			"reject struct:class cross-kind"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string name;\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    d.name = \"Up\";\n"
				"    Animal a = d;\n"
				"    print(a.name);\n"
				"}\n",
				Output,
					&Error),
			"class upcast assignment"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "Up\n", "class upcast output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string Speak() { return \"a\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    string Speak() { return \"d\"; }\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    Animal a = d;\n"
				"    print(a.Speak());\n"
				"    print(d.Speak());\n"
				"}\n",
				Output,
					&Error),
			"non-virtual shadow"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "a\nd\n", "non-virtual shadow output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    virtual string Speak() { return \"a\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    string Speak() override { return \"d\"; }\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    Animal a = d;\n"
				"    print(a.Speak());\n"
				"    print(d.Speak());\n"
				"}\n",
				Output,
					&Error),
			"virtual override dispatch"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "d\nd\n", "virtual override output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    virtual string Speak() { return \"a\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    string Speak() { return \"woof\"; }\n"
				"}\n"
				"void main() {\n"
				"    Animal a = Dog();\n"
				"    print(a.Speak());\n"
				"}\n",
				Output,
					&Error),
			"auto-override virtual"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "woof\n", "auto-override output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Animal {\n"
				"    public:\n"
				"    string Speak() { return \"a\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    string Speak() override { return \"d\"; }\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"override without virtual base"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string name;\n"
				"    Animal() { name = \"anon\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    Dog() : Animal() {}\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    print(d.name);\n"
				"}\n",
				Output,
					&Error),
			"ctor with Base()"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "anon\n", "ctor Base() output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Animal {\n"
				"    public:\n"
				"    string name;\n"
				"    Animal(string n) { name = n; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    Dog() {}\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"omit base init when base has only non-default ctor"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string name;\n"
				"    Animal(string n) { name = n; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    Dog(string n) : Animal(n) {}\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog(\"Fido\");\n"
				"    print(d.name);\n"
				"}\n",
				Output,
					&Error),
			"Dog(Animal) style ctor"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "Fido\n", "Dog(Animal) ctor output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Non-virtual this.Speak() must bind statically even if Speak is declared after Talk.
	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string Talk() { return this.Speak(); }\n"
				"    string Speak() { return \"a\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    string Speak() { return \"d\"; }\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    print(d.Talk());\n"
				"}\n",
				Output,
					&Error),
			"non-virtual this call order-independent"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "a\n", "non-virtual this call must stay on Animal::Speak"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Animal {\n"
				"    public:\n"
				"    Animal(string n) {}\n"
				"}\n"
				"void main() { Animal a = Animal(); }\n",
					&Error),
			"suppress bare Type() when only non-zero-arg user ctor"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Animal { public: string name; }\n"
				"class Dog : public Animal { public: string name; }\n"
				"void main() {}\n",
					&Error),
			"field name collision with base"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class A : public B {}\n"
				"class B : public A {}\n"
				"void main() {}\n",
					&Error),
			"inheritance cycle"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Box<T> { public: T v; }\n"
				"class Derived : public Box {}\n"
				"void main() {}\n",
					&Error),
			"reject inheriting from generic"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	if (!Expect(Run("class Animal{public:int read(){return 7;}};"
		"class Derived<T>:public Animal{public:T value;};"
		"void main(){Derived<int> value=Derived<int>();print(value.read());}",Output,&Error) && Output == "7\n",
		"generic derived types retain ordinary base behavior"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"struct Point { int x; }\n"
				"struct Point3 : Point { int z; }\n"
				"void main() {\n"
				"    Point3 p = Point3();\n"
				"    p.x = 7;\n"
				"    Point q = p;\n"
				"    print(q.x);\n"
				"}\n",
				Output,
					&Error),
			"struct upcast by value"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "7\n", "struct upcast output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Derived() with no user ctor must still run base default ctor.
	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string name;\n"
				"    Animal() { name = \"anon\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    print(d.name);\n"
				"}\n",
				Output,
					&Error),
			"implicit Dog() runs base Animal()"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "anon\n", "implicit Dog() base ctor output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Animal {\n"
				"    public:\n"
				"    Animal(string n) {}\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"}\n"
				"void main() { Dog d = Dog(); }\n",
					&Error),
			"implicit Dog() when base has only non-default ctor"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	// Grandparent user default ctor must run when middle type has none and derived has a user ctor.
	if (!Expect(
			Run(
				"class A {\n"
				"    public:\n"
				"    string name;\n"
				"    A() { name = \"fromA\"; }\n"
				"}\n"
				"class B : public A{\n"
				"    public:\n"
				"}\n"
				"class C : public B{\n"
				"    public:\n"
				"    C() {}\n"
				"}\n"
				"void main() {\n"
				"    C c = C();\n"
				"    print(c.name);\n"
				"}\n",
				Output,
					&Error),
			"user ctor skips middle — still runs grandparent A()"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "fromA\n", "grandparent default ctor output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Nearest ancestor user ctor chains; must not double-call A() via EmitImplicitAncestorDefaultCtors.
	if (!Expect(
			Run(
				"class A {\n"
				"    public:\n"
				"    int n;\n"
				"    A() { n = n + 1; }\n"
				"}\n"
				"class B : public A{\n"
				"    public:\n"
				"    B() {}\n"
				"}\n"
				"class C : public B{\n"
				"    public:\n"
				"}\n"
				"void main() {\n"
				"    C c = C();\n"
				"    print(c.n);\n"
				"}\n",
				Output,
					&Error),
			"implicit C() must not double-call A()"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "1\n", "A() must run exactly once"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Omit : Base() when base has zero-arg user ctor.
	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    string name;\n"
				"    Animal() { name = \"anon\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    Dog() {}\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Dog();\n"
				"    print(d.name);\n"
				"}\n",
				Output,
					&Error),
			"omit Base() still calls Animal()"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "anon\n", "omit Base() output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"class Animal {\n"
				"    public:\n"
				"    virtual string Speak() { return \"a\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"    int Speak() { return 1; }\n"
				"}\n"
				"void main() {}\n",
					&Error),
			"virtual override return type mismatch"))
	{
		std::cerr << "expected compile failure\n";
		return EXIT_FAILURE;
	}

	// Inherited virtual via Derived static type (no redeclaration on Dog).
	if (!Expect(
			Run(
				"class Animal {\n"
				"    public:\n"
				"    virtual string Speak() { return \"a\"; }\n"
				"}\n"
				"class Dog : public Animal{\n"
				"    public:\n"
				"}\n"
				"class Cat : public Dog{\n"
				"    public:\n"
				"    string Speak() { return \"meow\"; }\n"
				"}\n"
				"void main() {\n"
				"    Dog d = Cat();\n"
				"    print(d.Speak());\n"
				"}\n",
				Output,
					&Error),
			"virtual via intermediate static type"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "meow\n", "virtual via Dog static type output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Struct method CallBoundMethod / this fields with inheritance.
	if (!Expect(
			Run(
				"struct Point {\n"
				"    int x;\n"
				"    int GetX() { return this.x; }\n"
				"}\n"
				"struct Point3 : Point {\n"
				"    int z;\n"
				"    int Sum() { return this.GetX() + this.z; }\n"
				"}\n"
				"void main() {\n"
				"    Point3 p = Point3();\n"
				"    p.x = 2;\n"
				"    p.z = 5;\n"
				"    print(p.Sum());\n"
				"}\n",
				Output,
					&Error),
			"struct inherited method CallBound"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "7\n", "struct inherited method output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Box {\n"
					"    public:\n"
					"    private:\n"
					"        int secret;\n"
					"    public:\n"
					"        Box() { secret = 1; }\n"
					"}\n"
					"void main() {\n"
					"    Box b = Box();\n"
					"    print(b.secret);\n"
					"}\n",
					Output,
						&AccessError),
				"private field access outside class should fail"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Base {\n"
					"    public:\n"
					"    protected:\n"
					"        int n;\n"
					"    public:\n"
					"        Base() { n = 2; }\n"
					"}\n"
					"class Derived : public Base{\n"
					"    public:\n"
					"        int Get() { return n; }\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"protected field via derived method should work"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "2\n", "protected field via derived method output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Base {\n"
					"    public:\n"
					"    protected:\n"
					"        int Get() { return 3; }\n"
					"}\n"
					"class Derived : public Base{\n"
					"    public:\n"
					"        using Base::Get;\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"using Base::Get should promote protected method"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "3\n", "using Base::Get output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Base {\n"
					"    public:\n"
					"    protected:\n"
					"        int Get() { return 4; }\n"
					"}\n"
					"class Derived : public Base{\n"
					"    public:\n"
					"        using Base.Get;\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"using Base.Get should be equivalent to Base::Get"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "4\n", "using Base.Get output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"namespace N {\n"
					"    class Base {\n"
					"        public:\n"
					"        protected:\n"
					"            int Get() { return 5; }\n"
					"    }\n"
					"}\n"
					"class Derived : public N::Base{\n"
					"    public:\n"
					"        using N::Base::Get;\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"using N::Base::Get should promote namespaced base method"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "5\n", "using N::Base::Get output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Base {\n"
					"    public:\n"
					"        int n;\n"
					"        Base() { n = 1; }\n"
					"}\n"
					"class Derived : private Base{\n"
					"    public:\n"
					"        Derived() {}\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.n);\n"
					"}\n",
					Output,
						&AccessError),
				"private inheritance should hide public base members from outside"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Base {\n"
					"    public:\n"
					"        int n;\n"
					"        Base() { n = 9; }\n"
					"}\n"
					"class Derived : private Base{\n"
					"    public:\n"
					"        int Get() { return n; }\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"private inheritance: derived method may still use base members"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "9\n", "private inheritance derived method output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Base {\n"
					"    public:\n"
					"    private:\n"
					"        int secret;\n"
					"    public:\n"
					"        Base() { secret = 1; }\n"
					"}\n"
					"class Derived : public Base{\n"
					"    public:\n"
					"        using Base::secret;\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.secret);\n"
					"}\n",
					Output,
						&AccessError),
				"using must not promote inaccessible private base members"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Box {\n"
					"    public:\n"
					"    private:\n"
					"        int n;\n"
					"    public:\n"
					"        Box() { n = 1; }\n"
					"}\n"
					"void main() {\n"
					"    Box b = Box();\n"
					"    b.n += 1;\n"
					"}\n",
					Output,
						&AccessError),
				"compound assign to private field from outside should fail"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Box {\n"
					"    public:\n"
					"    private:\n"
					"        int n;\n"
					"    public:\n"
					"        Box() { n = 4; }\n"
					"        int Get() { return n; }\n"
					"}\n"
					"void main() {\n"
					"    Box b = Box();\n"
					"    print(b.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"ctor/method may access own private fields by bare name"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "4\n", "own private bare-name access output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Box<T> {\n"
					"    public:\n"
					"    private:\n"
					"        T value;\n"
					"    public:\n"
					"        Box(T v) { value = v; }\n"
					"        T Get() { return value; }\n"
					"}\n"
					"void main() {\n"
					"    Box<int> b = Box<int>(7);\n"
					"    print(b.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"generic class method may access own private fields"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "7\n", "generic private bare-name access output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Box {\n"
					"    public:\n"
					"    private:\n"
					"        int foo(int n) { return n; }\n"
					"    public:\n"
					"        int foo() { return 1; }\n"
					"}\n"
					"void main() {\n"
					"    Box b = Box();\n"
					"    print(b.foo(2));\n"
					"}\n",
					Output,
						&AccessError),
				"private overload must stay inaccessible even when public overload exists"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Box {\n"
					"    public:\n"
					"    private:\n"
					"        int foo() { return 0; }\n"
					"    public:\n"
					"        int foo(int n) { return n; }\n"
					"}\n"
					"void main() {\n"
					"    Box b = Box();\n"
					"    print(b.foo(5));\n"
					"}\n",
					Output,
						&AccessError),
				"public overload must remain callable when private overload shares the name"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "5\n", "public overload access output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Base {\n"
					"    public:\n"
					"    protected:\n"
					"        int n;\n"
					"    public:\n"
					"        Base() { n = 1; }\n"
					"}\n"
					"class Derived : public Base{\n"
					"    public:\n"
					"        int PeekBase(Base b) { return b.n; }\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.PeekBase(d));\n"
					"}\n",
					Output,
						&AccessError),
				"protected member must not be accessed through a base-typed object"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Base {\n"
					"    public:\n"
					"    protected:\n"
					"        int n;\n"
					"    public:\n"
					"        Base() { n = 8; }\n"
					"}\n"
					"class Derived : public Base{\n"
					"    public:\n"
					"        int PeekDerived(Derived d) { return d.n; }\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    print(d.PeekDerived(d));\n"
					"}\n",
					Output,
						&AccessError),
				"protected member may be accessed through derived-typed object"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "8\n", "protected via derived-typed object output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"void set_el(ref int x) { x = 9; }\n"
					"class Base {\n"
					"    public:\n"
					"    private:\n"
					"        array<int> items;\n"
					"    public:\n"
					"        Base() { items = array<int>(); items.push(1); }\n"
					"}\n"
					"class Derived : public Base{\n"
					"    public:\n"
					"        void Bad() { set_el(ref items[0]); }\n"
					"}\n"
					"void main() {\n"
					"    Derived d = Derived();\n"
					"    d.Bad();\n"
					"}\n",
					Output,
						&AccessError),
				"ref-index through inherited private this-field must fail"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Box {\n"
					"    public:\n"
					"    private:\n"
					"        array<int> items;\n"
					"    public:\n"
					"        Box() { items = array<int>(); items.push(3); }\n"
					"        int Get() { return items[0]; }\n"
					"}\n"
					"void main() {\n"
					"    Box b = Box();\n"
					"    print(b.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"own private array field index via bare name should work"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "3\n", "own private array index output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Box {\n"
					"    public:\n"
					"        int x;\n"
					"}\n"
					"class Box<T> {\n"
					"    public:\n"
					"    private:\n"
					"        T secret;\n"
					"    public:\n"
					"        Box(T v) { secret = v; }\n"
					"        T Get() { return secret; }\n"
					"}\n"
					"void main() {\n"
					"    Box<int> b = Box<int>(1);\n"
					"    print(b.secret);\n"
					"}\n",
					Output,
						&AccessError),
				"private field on arity-1 Box must not fail-open when arity-0 Box shares the name"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				Run(
					"class Box {\n"
					"    public:\n"
					"        int x;\n"
					"}\n"
					"class Box<T> {\n"
					"    public:\n"
					"    private:\n"
					"        T secret;\n"
					"    public:\n"
					"        Box(T v) { secret = v; }\n"
					"        T Get() { return secret; }\n"
					"}\n"
					"void main() {\n"
					"    Box<int> b = Box<int>(9);\n"
					"    print(b.Get());\n"
					"}\n",
					Output,
						&AccessError),
				"own private field on arity-1 Box still works beside arity-0 Box"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "9\n", "arity-disambiguated private access output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Counter {\n"
					"    public:\n"
					"    private:\n"
					"        int n;\n"
					"    public:\n"
					"        Counter() { n = 0; }\n"
					"        Counter operator++() { n = n + 1; return this; }\n"
					"}\n"
					"void main() {\n"
					"    Counter c = Counter();\n"
					"    c.n++;\n"
					"}\n",
					Output,
						&AccessError),
				"overloaded ++ on private field must stay inaccessible"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class A {\n"
					"    public:\n"
					"        int x;\n"
					"        A() { x = 1; }\n"
					"}\n"
					"class B : private A{\n"
					"    public:\n"
					"        B() {}\n"
					"}\n"
					"class C : public B{\n"
					"    public:\n"
					"        using A::x;\n"
					"        C() {}\n"
					"}\n"
					"void main() {\n"
					"    C c = C();\n"
					"    print(c.x);\n"
					"}\n",
					Output,
						&AccessError),
				"using ancestor through private inheritance from further derived must fail"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Box {\n"
					"    public:\n"
					"        int foo(double n) { return 1; }\n"
					"    private:\n"
					"        int foo(int n) { return n; }\n"
					"}\n"
					"void main() {\n"
					"    Box b = Box();\n"
					"    print(b.foo(2));\n"
					"}\n",
					Output,
						&AccessError),
				"private better-match overload must not be bypassed by public same-name"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
	}

	// C++ defaults: class members and inheritance are private unless marked.
	{
		std::string AccessError;
		if (!Expect(
				!Run(
					"class Box { int n = 1; }\n"
					"void main() { Box b = Box(); print(b.n); }\n",
					Output,
						&AccessError),
				"class members default private"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				!Run(
					"class Base { public: int n = 1; }\n"
					"class Derived : Base {}\n"
					"void main() { Derived d = Derived(); print(d.n); }\n",
					Output,
						&AccessError),
				"class inheritance defaults private"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct Box { int n = 1; }\n"
					"void main() { Box b = Box(); print(b.n); }\n",
					Output,
						&AccessError),
				"struct members default public"))
		{
			std::cerr << AccessError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "1\n", "struct default public output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	// Constructor overloads by parameter type (not only arity).
	{
		if (!Expect(
				Run(
					"class Box {\n"
					"public:\n"
					"    int n;\n"
					"    string s;\n"
					"    Box(int x) { this.n = x; this.s = \"i\"; }\n"
					"    Box(string t) { this.n = 0; this.s = t; }\n"
					"}\n"
					"void main() {\n"
					"    Box a = Box(7);\n"
					"    Box b = Box(\"hi\");\n"
					"    print(a.n);\n"
					"    print(a.s);\n"
					"    print(b.n);\n"
					"    print(b.s);\n"
					"}\n",
					Output,
						&Error),
				"constructor type overloads"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "7\ni\n0\nhi\n", "constructor type overload output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"class Box {\n"
					"public:\n"
					"    Box(int x) {}\n"
					"    Box(int y) {}\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"duplicate constructor same signature"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	// final class / method / interface
	{
		if (!Expect(
				Run(
					"class Base {\n"
					"public:\n"
					"    virtual int Value() final { return 7; }\n"
					"}\n"
					"class Leaf final : public Base {\n"
					"public:\n"
					"    int Extra() { return 1; }\n"
					"}\n"
					"void main() {\n"
					"    Leaf x = Leaf();\n"
					"    print(x.Value());\n"
					"    print(x.Extra());\n"
					"}\n",
					Output,
						&Error),
				"final class and method ok"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "7\n1\n", "final class and method output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"class Base final {\n"
					"public:\n"
					"}\n"
					"class Derived : public Base {\n"
					"public:\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"inherit final class"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"struct Base final {\n"
					"}\n"
					"struct Derived : Base {\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"inherit final struct"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"interface IBase final {\n"
					"    int Value();\n"
					"}\n"
					"interface IChild : IBase {\n"
					"    int Extra();\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"extend final interface"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"class Base {\n"
					"public:\n"
					"    virtual int Value() final { return 1; }\n"
					"}\n"
					"class Derived : public Base {\n"
					"public:\n"
					"    int Value() override { return 2; }\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"override final method"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"class Base {\n"
					"public:\n"
					"    virtual int Value() { return 1; }\n"
					"}\n"
					"class Mid : public Base {\n"
					"public:\n"
					"    int Value() override final { return 2; }\n"
					"}\n"
					"class Leaf : public Mid {\n"
					"public:\n"
					"    int Value() override { return 3; }\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"override override-final method"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	// Destructors: derived body then base; virtual via base handle; struct overwrite.
	{
		if (!Expect(
				Run(
					"class Base {\n"
					"public:\n"
					"    virtual ~Base() { print(\"B\"); }\n"
					"}\n"
					"class Der : public Base {\n"
					"public:\n"
					"    ~Der() { print(\"D\"); }\n"
					"}\n"
					"void main() {\n"
					"    Base x = Der();\n"
					"}\n",
					Output,
						&Error),
				"virtual destructor order"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "D\nB\n", "virtual destructor order output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"void main() {\n"
					"    S a = S();\n"
					"    a = S();\n"
					"    print(\"m\");\n"
					"}\n",
					Output,
						&Error),
				"struct destructor overwrite and scope"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "s\nm\ns\n", "struct destructor overwrite output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		// Consumer CloneStruct elision: script free-function returns are already cloned.
		// Skipping the consumer copy must not change observable destructor counts.
		if (!Expect(
				Run(
					"struct S {\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"S mk() {\n"
					"    S t = S();\n"
					"    return t;\n"
					"}\n"
					"void main() {\n"
					"    S a = mk();\n"
					"    print(\"m\");\n"
					"}\n",
					Output,
						&Error),
				"struct dtor init from free-function return"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "s\nm\ns\n", "struct dtor init from free-function return output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"void main() {\n"
					"    S a = S();\n"
					"    print(\"m\");\n"
					"}\n",
					Output,
						&Error),
				"struct dtor init from default ctor"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "m\ns\n", "struct dtor init from default ctor output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    S() {}\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"void main() {\n"
					"    S a = S();\n"
					"    print(\"m\");\n"
					"}\n",
					Output,
						&Error),
				"struct dtor init from user ctor (CtorTemp)"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		// The constructor result becomes a directly initialized value. Its
		// internal CtorTemp is transferred and owns no second destructor call.
		if (!Expect(Output == "m\ns\n", "struct dtor init from user ctor output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"S mk() {\n"
					"    S t = S();\n"
					"    return t;\n"
					"}\n"
					"void sink(S v) {}\n"
					"void main() {\n"
					"    sink(mk());\n"
					"    print(\"m\");\n"
					"    sink(S());\n"
					"    print(\"n\");\n"
					"}\n",
					Output,
						&Error),
				"struct dtor by-value arg from return vs ctor"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "s\ns\nm\ns\nn\n", "struct dtor by-value arg output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"S mk() {\n"
					"    S t = S();\n"
					"    return t;\n"
					"}\n"
					"S wrap() {\n"
					"    return mk();\n"
					"}\n"
					"S fromLocal() {\n"
					"    S t = S();\n"
					"    return t;\n"
					"}\n"
					"void main() {\n"
					"    S a = wrap();\n"
					"    print(\"m\");\n"
					"    S b = fromLocal();\n"
					"    print(\"n\");\n"
					"}\n",
					Output,
						&Error),
				"struct dtor return rvalue vs local"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "s\nm\ns\nn\ns\ns\n", "struct dtor return rvalue vs local output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    int x = 0;\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"S mk() {\n"
					"    S t = S();\n"
					"    t.x = 7;\n"
					"    return t;\n"
					"}\n"
					"void main() {\n"
					"    S a = S();\n"
					"    a = mk();\n"
					"    print(to_string(a.x));\n"
					"}\n",
					Output,
						&Error),
				"struct dtor assign from free-function return"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "s\ns\n7\ns\n", "struct dtor assign from free-function return output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"void main() {\n"
					"    array<S> xs = array<S>();\n"
					"    S a = S();\n"
					"    xs.push(a);\n"
					"    print(\"m\");\n"
					"    xs.clear();\n"
					"    print(\"c\");\n"
					"}\n",
					Output,
						&Error),
				"struct destructor in array clear"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "m\ns\nc\ns\n", "struct destructor in array clear output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    ~S() { print(\"t\"); }\n"
					"}\n"
					"void main() {\n"
					"    S();\n"
					"    print(\"m\");\n"
					"}\n",
					Output,
						&Error),
				"struct destructor expression temporary"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "t\nm\n", "struct destructor expression temporary output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"class C {\n"
					"public:\n"
					"    ~C() { print(\"g\"); }\n"
					"}\n"
					"C g = C();\n"
					"void main() {\n"
					"    print(\"m\");\n"
					"}\n",
					Output,
						&Error),
				"class destructor global at shutdown"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "m\ng\n", "class destructor global at shutdown output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"class C {\n"
					"public:\n"
					"    ~C() { print(\"c\"); }\n"
					"}\n"
					"void main() {\n"
					"    C a = C();\n"
					"    C b = a;\n"
					"}\n",
					Output,
						&Error),
				"aliased class locals destructor once"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "c\n", "aliased class locals destructor once output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"class C {\n"
					"public:\n"
					"    ~C() { print(\"e\"); }\n"
					"}\n"
					"void main() {\n"
					"    C a = C();\n"
					"    C b = a;\n"
					"    exit(0);\n"
					"}\n",
					Output,
						&Error),
				"aliased class locals destructor on exit"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "e\n", "aliased class locals destructor on exit output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"class C {\n"
					"public:\n"
					"    array<C> bag;\n"
					"    ~C() { bag.clear(); }\n"
					"}\n"
					"void main() {\n"
					"    array<C> a = array<C>();\n"
					"    C x = C();\n"
					"    x.bag = a;\n"
					"    a.push(x);\n"
					"    a[0] = C();\n"
					"    print(\"ok\");\n"
					"}\n",
					Output,
						&Error),
				"SetIndex releases old element without dangling slot"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "ok\n", "SetIndex releases old element without dangling slot output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct S {\n"
					"    int id;\n"
					"    ~S() { print(\"s\"); }\n"
					"}\n"
					"void main() {\n"
					"    S held = S();\n"
					"    held.id = 1;\n"
					"    fn()->int f = fn() -> int => held.id;\n"
					"    f = fn() -> int => 0;\n"
					"    print(\"m\");\n"
					"}\n",
					Output,
						&Error),
				"closure capture destructor on overwrite"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "s\nm\ns\n", "closure capture destructor on overwrite output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"class B {\n"
					"public:\n"
					"    virtual void f() {}\n"
					"}\n"
					"class D : public B {\n"
					"public:\n"
					"    void f() final override {}\n"
					"}\n"
					"void main() { print(\"ok\"); }\n",
					Output,
						&Error),
				"final override any order accepted"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "ok\n", "final override any order output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"class B {\n"
					"public:\n"
					"    virtual void f() final final {}\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"duplicate final specifier rejected"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				CompileFails(
					"struct S {\n"
					"    ~S() const {}\n"
					"}\n"
					"void main() {}\n",
						&Error),
				"destructor const trailer rejected"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				Error.find("destructor cannot be 'const'") != std::string::npos,
				"destructor const trailer message"))
		{
			std::cerr << "got: [" << Error << "]\n";
			return EXIT_FAILURE;
		}
		if (!Expect(
				Run(
					"struct Counter {\n"
					"    int n;\n"
					"    fn()->int makeGetter() {\n"
					"        return fn() -> int => n;\n"
					"    }\n"
					"}\n"
					"void main() {\n"
					"    Counter c = Counter();\n"
					"    c.n = 10;\n"
					"    fn()->int g = c.makeGetter();\n"
					"    c.n = 20;\n"
					"    print(g());\n"
					"}\n",
					Output,
						&Error),
				"struct this captured by value in closure"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "10\n", "struct this value-capture snapshot"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}

		// CallMethod must NOT be peephole-inlined: override side effects must run.
		if (!Expect(
				Run(
					"class Base {\n"
					"public:\n"
					"\tint n;\n"
					"\tBase() { n = 0; }\n"
					"\tvirtual void Tick() { n = n + 1; print(\"b\"); }\n"
					"}\n"
					"class Derived : public Base {\n"
					"public:\n"
					"\tvoid Tick() override { n = n + 1; print(\"d\"); }\n"
					"}\n"
					"void main() {\n"
					"\tDerived d = Derived();\n"
					"\tBase b = d;\n"
					"\tb.Tick();\n"
					"}\n",
					Output,
						&Error),
				"virtual tick must not IncField-inline away override"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "d\n", "virtual tick override side effect"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}

		// Inherited int field ++ from a derived method (may IncField-inline via base field lookup).
		if (!Expect(
				Run(
					"class Base {\n"
					"public:\n"
					"\tint n;\n"
					"\tBase() { n = 0; }\n"
					"}\n"
					"class Derived : public Base {\n"
					"public:\n"
					"\tvoid Bump() { n = n + 1; }\n"
					"}\n"
					"void main() {\n"
					"\tDerived d = Derived();\n"
					"\td.Bump();\n"
					"\td.Bump();\n"
					"\tprint(to_string(d.n));\n"
					"}\n",
					Output,
						&Error),
				"inherited int field bump"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "2\n", "inherited int field bump output"))
		{
			std::cerr << "got: [" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}

	std::cout << "VeroScriptInheritanceTests passed\n";
	return EXIT_SUCCESS;
}
