// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptPath.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include "VeroScriptTestTemporaryDirectory.h"

#include "VeroScriptAst.h"

#include <cstdlib>
#include <filesystem>
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
		const std::string& Input,
		std::string& OutOutput,
		std::string* OutError = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "fmt.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "fmt.vs");
		// This suite exercises the VM builtin implementations directly. Public
		// source uses the wrappers exported by io.vs/string.vs instead.
		Parser.SetAllowInternalBuiltins(true);
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
		std::istringstream In(Input);
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
}

int main()
{
	std::string Output;
	std::string Error;
	const VeroScript::Tests::FScopedTemporaryDirectory TemporaryDirectory(
		"VeroScript-format-io-");
	if (!Expect(static_cast<bool>(TemporaryDirectory),
		"create unique format/I/O test directory"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void main() {\n"
				"    print(\"a={} b={}\", 1, 2);\n"
				"    print(format(\"{:>4}\", \"7\"));\n"
				"    print(format(\"{:.2}\", \"1.5\"));\n"
				"    print(format(\"{:x}\", \"255\"));\n"
				"    print(format(\"brace {{}} ok\", array<string>()));\n"
				"    print(format(\"{0}-{0}\", \"z\"));\n"
				"    array<string> direct = {\"q\"};\n"
				"    print(\"array={}\", direct);\n"
				"    print(\"raw {} %d\");\n"
				"}\n",
				"",
				Output,
					&Error),
			"format/print should run"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
			Output
				== "a=1 b=2\n   7\n1.50\nff\nbrace {} ok\nz-z\n"
					"array=q\nraw {} %d\n",
			"format output mismatch"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void main() {\n"
				"    int a = __read_int();\n"
				"    double b = __read_double();\n"
				"    string c = __read_string();\n"
				"    bool d = __read_bool();\n"
				"    print(\"{} {} {} {}\", a, b, c, d);\n"
				"}\n",
				"12  3.5  hello true\n",
				Output,
					&Error),
			"read_* should run"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "12 3.5 hello true\n", "read_* output mismatch"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			!Run(
				"void main() { int x = __read_int(); print(x); }\n",
				"",
				Output,
					&Error),
			"read_int EOF should fail"))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
			!Run(
				"void main() {\n"
				"    int a = 1;\n"
				"    print(\"{}\", ref a);\n"
				"}\n",
				"",
				Output,
					&Error),
			"print should reject ref args"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void main() {\n"
				"    double d = __read_double();\n"
				"    int i = __read_int();\n"
				"    print(\"{} {}\", d, i);\n"
				"}\n",
				"+3.5 +7\n",
				Output,
					&Error),
			"leading + tokens should parse"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output == "3.5 7\n", "leading + output mismatch"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void main() {\n"
				"    print(abs(-7i64));\n"
				"    print(abs(8u64));\n"
				"    print(min(-9i64, 3i64));\n"
				"    print(max(4u64, 5000000000u64));\n"
				"}\n",
				"",
				Output,
				&Error),
			"64-bit abs/min/max should run"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(
			Output == "7\n8\n-9\n5000000000\n",
			"64-bit abs/min/max output mismatch"))
	{
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			!Run(
				"void main() {\n"
				"    int64 n = -9223372036854775807i64;\n"
				"    n = n - 1i64;\n"
				"    print(abs(n));\n"
				"}\n",
				"",
				Output,
				&Error),
			"abs(INT64_MIN) should report overflow"))
	{
		return EXIT_FAILURE;
	}

	// Public file APIs use UTF-8 paths on every platform, including Windows.
	{
		const std::filesystem::path Utf8Root =
			TemporaryDirectory.Get() / VeroScript::PathUtil::FromUtf8(
				"veroscript-io-\xE4\xB8\xAD\xE6\x96\x87");
		const std::filesystem::path Utf8File =
			Utf8Root / VeroScript::PathUtil::FromUtf8(
				"\xE6\x96\x87\xE4\xBB\xB6.txt");
		std::error_code FileError;
		std::filesystem::remove_all(Utf8Root, FileError);
		std::filesystem::create_directories(Utf8Root, FileError);
		const std::string FileName = VeroScript::PathUtil::ToUtf8(Utf8File);
		const std::string Utf8Source =
			"void main() {\n"
			"    __write_file(\"" +
			FileName + "\", \"hello\");\n"
			"    __append_file(\"" +
			FileName + "\", \" world\");\n"
			"    assert(__exists(\"" +
			FileName + "\"));\n"
			"    print(__read_file(\"" +
			FileName + "\"));\n"
			"    assert(__delete_file(\"" +
			FileName + "\"));\n"
			"}\n";
		if (!Expect(
				!FileError && Run(Utf8Source, "", Output, &Error),
				"UTF-8 file path builtins should run"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Output == "hello world\n", "UTF-8 file path output mismatch"))
		{
			return EXIT_FAILURE;
		}
		std::filesystem::remove_all(Utf8Root, FileError);
	}

	std::cout << "VeroScriptFormatIOTests passed\n";
	return EXIT_SUCCESS;
}
