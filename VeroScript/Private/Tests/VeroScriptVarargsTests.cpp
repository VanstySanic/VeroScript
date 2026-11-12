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
		VeroScript::VeroScriptLexer Lexer(Source, "varargs.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "varargs.vs");
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
		VeroScript::VeroScriptLexer Lexer(Source, "varargs.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "varargs.vs");
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

	if (!Expect(
			Run(
				"void sum(int... xs) {\n"
				"    int t = 0;\n"
				"    for (int i = 0; i < len(xs); i = i + 1) {\n"
				"        t = t + xs[i];\n"
				"    }\n"
				"    print(t);\n"
				"}\n"
				"void main() {\n"
				"    sum();\n"
				"    sum(1);\n"
				"    sum(1, 2, 3);\n"
				"    array<int> a = {4, 5};\n"
				"    sum(a);\n"
				"}\n",
				Output,
					&Error),
			"free int...")
				|| !Expect(Output == "0\n1\n6\n9\n", "free int... output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void show_all<T>(T... xs) {\n"
				"    print(len(xs));\n"
				"    if (len(xs) > 0) { print(xs[0]); }\n"
				"}\n"
				"void main() {\n"
				"    show_all<int>();\n"
				"    show_all(4, 5, 6);\n"
				"    array<int> values = {7, 8};\n"
				"    show_all(values);\n"
				"}\n",
				Output,
				&Error),
			"generic variadic inference and packing")
			|| !Expect(
				Output == "0\n3\n4\n2\n7\n",
				"generic variadic output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void tag(string prefix, string... parts) {\n"
				"    print(prefix);\n"
				"    print(len(parts));\n"
				"    if (len(parts) > 0) { print(parts[0]); }\n"
				"}\n"
				"void main() {\n"
				"    tag(\"p\");\n"
				"    tag(\"p\", \"a\", \"b\");\n"
				"}\n",
				Output,
					&Error),
			"free with fixed + ...")
				|| !Expect(Output == "p\n0\np\n2\na\n", "fixed+... output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class C {\n"
				"    public:\n"
				"    void show(int... xs) {\n"
				"        print(len(xs));\n"
				"    }\n"
				"}\n"
				"void main() {\n"
				"    C c = C();\n"
				"    c.show();\n"
				"    c.show(1, 2);\n"
				"}\n",
				Output,
					&Error),
			"method ...")
				|| !Expect(Output == "0\n2\n", "method ... output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void f(int x = 7, int... xs) {\n"
				"    print(x);\n"
				"    print(len(xs));\n"
				"}\n"
				"void main() {\n"
				"    f();\n"
				"    f(3);\n"
				"    f(3, 8, 9);\n"
				"}\n",
				Output,
					&Error),
			"default + ...")
				|| !Expect(Output == "7\n0\n3\n0\n3\n2\n", "default+... output"))
	{
		std::cerr << Error << '\n'
				  << Output;
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void bad(int... xs, int y) {}\n"
				"void main() {}\n",
					&Error),
			"variadic must be last"))
	{
		std::cerr << "expected last-only fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void bad(ref int... xs) {}\n"
				"void main() {}\n",
					&Error),
			"ref variadic rejected"))
	{
		std::cerr << "expected ref fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"interface I { void f(int... xs); }\n"
				"void main() {}\n",
					&Error),
			"interface variadic rejected"))
	{
		std::cerr << "expected iface fail\n";
		return EXIT_FAILURE;
	}

	for (const char* InvalidNull : {
		"void count(int... values) {} void main() { count(nullptr); }",
		"struct Value { int n; } void count(Value... values) {} void main() { count(nullptr); }"})
	{
		if (!Expect(CompileFails(InvalidNull, &Error), "null variadic argument rejects value types"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	std::cout << "VeroScriptVarargsTests passed\n";
	return EXIT_SUCCESS;
}
