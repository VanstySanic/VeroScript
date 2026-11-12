// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptM2Tests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool ExpectOutput(
		const std::string_view Source,
		const VeroScript::ERunMode RunMode,
		const std::string_view ExpectedOutput,
		const char* Message)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			RunMode);
		if (!Expect(Result.bSuccess, Message))
		{
			std::cerr << Errors.str();
			return false;
		}
		if (!Expect(Errors.str().empty(), "expected no errors"))
		{
			std::cerr << Errors.str();
			return false;
		}
		return Expect(Output.str() == ExpectedOutput, Message);
	}
}

int main()
{
	if (!ExpectOutput(
			"void main() { print(\"hello, VeroScript\"); }",
			VeroScript::ERunMode::Script,
			"hello, VeroScript\n",
			"script mode should call main"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"int inc(int x) { return x + 1; }\n"
			"void main() { print(inc(41)); }",
			VeroScript::ERunMode::Script,
			"42\n",
			"user function call"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int n = 0;\n"
			"    while (n < 3) {\n"
			"        n = n + 1;\n"
			"    }\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"while loop and assignment"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ifstream HelloFile("examples/hello.vs");
		if (!Expect(static_cast<bool>(HelloFile), "examples/hello.vs should exist"))
		{
			return EXIT_FAILURE;
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			"examples/hello.vs",
			Output,
			Errors);
		if (!Expect(Result.bSuccess, "examples/hello.vs should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "hello, VeroScript\n", "hello.vs output"))
		{
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
