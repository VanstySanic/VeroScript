// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptCompile.h"
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
		VeroScript::VeroScriptLexer Lexer(Source, "def.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "def.vs");
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
		VeroScript::VeroScriptLexer Lexer(Source, "def.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "def.vs");
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

	bool ParseFails(const std::string& Source, std::string* OutError = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "def.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "def.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			if (OutError)
			{
				*OutError = Parser.GetError().Message;
			}
			return true;
		}
		return false;
	}

	bool LoaderCompileFails(const std::string& Source, std::string* OutError = nullptr)
	{
		VeroScript::FCompileResult Result =
			VeroScript::VeroScriptCompile::CompileSource(Source, "def.vs");
		if (!Result.bSuccess)
		{
			if (OutError)
			{
				*OutError = Result.Error.Message;
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

	if (!Expect(
			Run(
				"void show(int x = 7) { print(x); }\n"
				"void main() {\n"
				"    show();\n"
				"    show(3);\n"
				"}\n",
				Output,
					&Error),
			"free function default arg")
				|| !Expect(Output == "7\n3\n", "free default output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void pair(int a, int b = 1, int c = 2) {\n"
				"    print(a); print(b); print(c);\n"
				"}\n"
				"void main() {\n"
				"    pair(9);\n"
				"    pair(8, 7);\n"
				"    pair(1, 2, 3);\n"
				"}\n",
				Output,
					&Error),
			"trailing multi defaults")
				|| !Expect(Output == "9\n1\n2\n8\n7\n2\n1\n2\n3\n", "multi default output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class C {\n"
				"    public:\n"
				"    void show(string s = \"hi\") { print(s); }\n"
				"}\n"
				"void main() {\n"
				"    C c = C();\n"
				"    c.show();\n"
				"    c.show(\"ok\");\n"
				"}\n",
				Output,
					&Error),
			"method default arg")
				|| !Expect(Output == "hi\nok\n", "method default output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void bad(int a = 1, int b) {}\n"
				"void main() {}\n",
					&Error),
			"non-trailing default rejected"))
	{
		std::cerr << "expected trailing fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void bad(ref int x = 1) {}\n"
				"void main() {}\n",
					&Error),
			"ref default rejected"))
	{
		std::cerr << "expected ref-default fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void f(int x) { print(x); }\n"
				"void f(int x, int y = 0) { print(y); }\n"
				"void main() { f(1); }\n",
					&Error),
			"overlapping default overload ambiguous"))
	{
		std::cerr << "expected ambiguous fail, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void bad(int x = foo()) {}\n"
				"void main() { bad(); }\n",
					&Error),
			"non-literal default rejected at call"))
	{
		std::cerr << "expected non-literal default fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface I { void f(int x = 1); }\n"
				"void main() {}\n",
					&Error),
			"interface param default rejected"))
	{
		std::cerr << "expected interface default fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"T id<T>(T x, int n = 0) {\n"
				"    print(n);\n"
				"    return x;\n"
				"}\n"
				"void main() {\n"
				"    print(id(5));\n"
				"    print(id(8, 2));\n"
				"}\n",
				Output,
					&Error),
			"generic free function defaults")
				|| !Expect(Output == "0\n5\n2\n8\n", "generic default output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"T id<T>(T x, int n = 0) {\n"
				"    return x;\n"
				"}\n"
				"void main() {\n"
				"    auto y = id(5);\n"
				"    print(y);\n"
				"    print(id(3) + 1);\n"
				"}\n",
				Output,
					&Error),
			"generic defaults with auto/typed context")
				|| !Expect(Output == "5\n4\n", "generic default auto output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	// `= default` is intentionally limited to the special members for which
	// VeroScript has an unambiguous generated implementation. `= delete`
	// remains an overload-resolution constraint and produces no bytecode body.
	if (!Expect(
			Run(
				"struct Value {\n"
				"    int n = 40;\n"
				"    Value() = default;\n"
				"    Value(string text) = delete;\n"
				"    ~Value() = default;\n"
				"    int get() const { return n; }\n"
				"    int get(int unused) const = delete;\n"
				"    static int parse(int value) { return value; }\n"
				"    static int parse(string text) = delete;\n"
				"    Value operator+(int amount) { return Value(); }\n"
				"    Value operator+(string text) = delete;\n"
				"}\n"
				"int choose(int value) { return value; }\n"
				"int choose(string text) = delete;\n"
				"void main() {\n"
				"    Value value();\n"
				"    print(value.get());\n"
				"    print(Value::parse(41));\n"
				"    print(choose(42));\n"
				"}\n",
				Output,
				&Error),
			"defaulted special members and unused deleted overloads")
			|| !Expect(Output == "40\n41\n42\n", "default/delete output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"struct Box<T> {\n"
				"    T value;\n"
				"    Box() = default;\n"
				"    Box(string text) = delete;\n"
				"    T get() const { return value; }\n"
				"    T get(int unused) const = delete;\n"
				"}\n"
				"void main() { Box<int> box(); print(box.get()); }\n",
				Output,
				&Error),
			"generic type skips deleted member bodies")
			|| !Expect(Output == "0\n", "generic default/delete output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Base {\n"
				"public:\n"
				"    int base_value = 20;\n"
				"    Base() = default;\n"
				"}\n"
				"class Derived : public Base {\n"
				"public:\n"
				"    int derived_value = 22;\n"
				"    Derived() = default;\n"
				"}\n"
				"void main() { Derived value(); print(value.base_value + value.derived_value); }\n",
				Output,
				&Error),
			"defaulted derived constructor initializes base and fields")
			|| !Expect(Output == "42\n", "defaulted inheritance output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	const auto ExpectRejected = [&Error](const std::string& Source, const char* Label)
	{
		return Expect(CompileFails(Source, &Error), Label);
	};
	if (!ExpectRejected(
			"struct V { V(string text) = delete; }\n"
			"void main() { V value(\"x\"); }\n",
			"deleted constructor rejected")
		|| !ExpectRejected(
			"struct V { int removed(int value) = delete; }\n"
			"void main() { V value(); print(value.removed(1)); }\n",
			"deleted instance method rejected")
		|| !ExpectRejected(
			"struct V { static int removed(int value) = delete; }\n"
			"void main() { print(V::removed(1)); }\n",
			"deleted static method rejected")
		|| !ExpectRejected(
			"int removed(int value) = delete;\n"
			"void main() { print(removed(1)); }\n",
			"deleted free function rejected")
		|| !ExpectRejected(
			"int removed(int value) = delete;\n"
			"void main() { function(int)->int callback = removed; }\n",
			"deleted free function cannot be captured as a function value")
		|| !ExpectRejected(
			"class V { public: int removed(int value) = delete;\n"
			"int call() { return removed(1); } }\n"
			"void main() {}\n",
			"implicit-this call to deleted method rejected")
		|| !ExpectRejected(
			"struct V { V operator+(int value) = delete; }\n"
			"void main() { V value(); V result = value + 1; }\n",
			"deleted operator rejected")
		|| !ExpectRejected(
			"struct V { V(int value) = default; }\nvoid main() {}\n",
			"parameterized constructor cannot be defaulted")
		|| !ExpectRejected(
			"int f(int value) = default;\nvoid main() {}\n",
			"ordinary function cannot be defaulted")
		|| !ExpectRejected(
			"struct V { int f() = default; }\nvoid main() {}\n",
			"ordinary method cannot be defaulted")
		|| !ExpectRejected(
			"struct V { ~V() = delete; }\nvoid main() {}\n",
			"managed destructor cannot be deleted")
		|| !ExpectRejected(
			"interface I { int f() = delete; }\nvoid main() {}\n",
			"interface method cannot be deleted")
		|| !ExpectRejected(
			"class V { public: virtual int f() = delete; }\nvoid main() {}\n",
			"virtual method cannot be deleted")
		|| !ExpectRejected(
			"class Base { public: virtual int f(int value) { return value; } }\n"
			"class Derived : public Base { public: int f(int value) = delete; }\n"
			"void main() {}\n",
			"deleted method cannot implicitly override a virtual base method")
		|| !ExpectRejected(
			"interface I { int f(int value); }\n"
			"class Derived : I { public: int f(int value) = delete; }\n"
			"void main() {}\n",
			"deleted method does not implement an abstract interface method")
		|| !ExpectRejected(
			"class Base<T> { public: virtual int f(T value) { return 1; } }\n"
			"class Derived<T> : public Base<T> { public: int f(T value) = delete; }\n"
			"void main() { Derived<int> value(); }\n",
			"deleted generic method cannot override a concrete virtual slot")
		|| !ExpectRejected(
			"class V<T> { public: T value; V(int number) = delete; V(int number) {} }\n"
			"void main() {}\n",
			"unused generic class rejects duplicate constructor signatures")
		|| !ExpectRejected(
			"struct V<T> { T value; V(int number) = delete; V(int number) {} }\n"
			"void main() {}\n",
			"unused generic struct rejects duplicate constructor signatures")
		|| !ExpectRejected(
			"struct V { V(int value) = delete; }\nvoid main() { V value(); }\n",
			"deleted user constructor suppresses implicit default constructor")
		|| !ExpectRejected(
			"class Base { public: Base() = delete; }\n"
			"class Derived : public Base { public: Derived() = default; }\n"
			"void main() { Derived value=Derived(); }\n",
			"defaulted derived constructor cannot bypass a deleted base constructor"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Error.find("Base") != std::string::npos, "unavailable constructor reports its failing base"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
			Run("class Base { public: Base() = delete; }\n"
				"class Derived : public Base { public: Derived() = default; }\n"
				"void main() {}\n", Output, &Error),
			"unused unavailable defaulted constructor preserves its type declaration"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class NonVirtualBase { public: int f(int value) { return value; } }\n"
				"class NonVirtualDerived : public NonVirtualBase {\n"
				"public: int f(int value) = delete;\n"
				"}\n"
				"class VirtualBase { public: virtual int f(int value) { return value; } }\n"
				"class OverloadDerived : public VirtualBase {\n"
				"public: int f(string text) = delete;\n"
				"}\n"
				"void main() { print(42); }\n",
				Output,
				&Error),
			"deleted non-virtual hiding and distinct overload remain valid")
			|| !Expect(Output == "42\n", "deleted hiding/overload output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			ParseFails(
				"class V<T> { public: T value;\n"
				"    V(int number) = delete; V(int number) {} }\n"
				"void main() {}\n",
				&Error),
			"parser rejects duplicate constructors in unused generic types")
		|| !Expect(
			Error.find("duplicate constructor") != std::string::npos,
			"parser reports duplicate-constructor diagnostic")
		|| !Expect(
			LoaderCompileFails(
				"class V<T> { public: T value;\n"
				"    V(int number) = delete; V(int number) {} }\n"
				"void main() {}\n",
				&Error),
			"module-loader path rejects duplicate constructors in unused generic types")
		|| !Expect(
			Error.find("duplicate constructor") != std::string::npos,
			"module-loader path reports duplicate-constructor diagnostic"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	std::cout << "VeroScriptDefaultParamTests passed\n";
	return EXIT_SUCCESS;
}
