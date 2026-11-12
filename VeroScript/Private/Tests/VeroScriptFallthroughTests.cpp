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
		VeroScript::VeroScriptLexer Lexer(Source, "ft.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "ft.vs");
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
		VeroScript::VeroScriptLexer Lexer(Source, "ft.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "ft.vs");
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
				"void main() {\n"
				"    int x = 1;\n"
				"    switch (x) {\n"
				"    case 1:\n"
				"        print(\"a\");\n"
				"        fallthrough;\n"
				"    case 2:\n"
				"        print(\"b\");\n"
				"    }\n"
				"}\n",
				Output,
					&Error),
			"basic fallthrough should run"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "a\nb\n", "basic fallthrough output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void main() {\n"
				"    int x = 2;\n"
				"    switch (x) {\n"
				"    case 1:\n"
				"        print(\"a\");\n"
				"        fallthrough;\n"
				"    case 2:\n"
				"        print(\"b\");\n"
				"    }\n"
				"}\n",
				Output,
					&Error),
			"match later case without prior fallthrough"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "b\n", "later case only output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void main() {\n"
				"    int x = 1;\n"
				"    switch (x) {\n"
				"    case 1:\n"
				"        print(\"one\");\n"
				"        fallthrough;\n"
				"    default:\n"
				"        print(\"def\");\n"
				"    }\n"
				"}\n",
				Output,
					&Error),
			"fallthrough into default"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "one\ndef\n", "fallthrough into default output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void main() {\n"
				"    int x = 1;\n"
				"    switch (x) {\n"
				"    case 1:\n"
				"        print(\"a\");\n"
				"    case 2:\n"
				"        print(\"b\");\n"
				"    }\n"
				"}\n",
				Output,
					&Error),
			"no implicit fallthrough"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "a\n", "no implicit fallthrough output"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void main() {\n"
				"    switch (1) {\n"
				"    case 1:\n"
				"        fallthrough;\n"
				"    }\n"
				"}\n",
					&Error),
			"last-case fallthrough should fail"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void main() {\n"
				"    switch (1) {\n"
				"    case 1:\n"
				"        if (true) { fallthrough; }\n"
				"        print(1);\n"
				"    case 2:\n"
				"        break;\n"
				"    }\n"
				"}\n",
					&Error),
			"nested fallthrough should fail"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void main() {\n"
				"    switch (1) {\n"
				"    case 1:\n"
				"        break;\n"
				"        fallthrough;\n"
				"    case 2:\n"
				"        break;\n"
				"    }\n"
				"}\n",
					&Error),
			"unreachable fallthrough should fail"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
			CompileFails(
				"void main() { fallthrough; }\n",
					&Error),
			"fallthrough outside switch should fail"))
	{
		return EXIT_FAILURE;
	}

	std::cout << "VeroScriptFallthroughTests passed\n";
	return EXIT_SUCCESS;
}
