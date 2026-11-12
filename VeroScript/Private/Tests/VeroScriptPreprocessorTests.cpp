// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

	bool PreprocessOk(
		const std::string& Source,
		const std::unordered_map<std::string, std::string>& Defines,
		std::string& OutSource,
		std::string* OutError = nullptr,
		std::uint32_t* OutErrorLine = nullptr)
	{
		std::string Error;
		std::uint32_t ErrorLine = 0;
		if (!VeroScript::PreprocessSource(Source, Defines, OutSource, Error, &ErrorLine))
		{
			if (OutError)
			{
				*OutError = Error;
			}
			if (OutErrorLine)
			{
				*OutErrorLine = ErrorLine;
			}
			return false;
		}
		return true;
	}

	bool RunWithDefines(
		const std::string& Source,
		const std::vector<std::string>& Defines,
		std::string& OutOutput,
		std::string* OutError = nullptr)
	{
		VeroScript::VeroScriptModuleLoader Loader("pp.vs");
		Loader.SetDefines(Defines);
		Loader.SetSourceOverlay("pp.vs", Source);
		const std::unique_ptr<VeroScript::FProgram> Program =
			Loader.LoadSourceModule(Source, "pp.vs");
		if (!Program || Loader.HasError())
		{
			if (OutError)
			{
				*OutError = Loader.HasError() ? Loader.GetError().Message : "load";
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
}

int main()
{
	int Failures = 0;

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#define FOO\n"
			"#ifdef FOO\n"
			"print(1);\n"
			"#else\n"
			"print(0);\n"
			"#endif\n",
			{},
			Out,
				&Error);
		Failures += Expect(Ok, "ifdef define active") ? 0 : 1;
		Failures += Expect(Out.find("print(1);") != std::string::npos, "active branch kept") ? 0 : 1;
		Failures += Expect(Out.find("print(0);") == std::string::npos, "inactive branch blanked")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#ifdef FOO\n"
			"print(1);\n"
			"#else\n"
			"print(0);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok, "ifdef missing else") ? 0 : 1;
		Failures += Expect(Out.find("print(0);") != std::string::npos, "else taken") ? 0 : 1;
		Failures += Expect(Out.find("print(1);") == std::string::npos, "ifdef body blanked") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#ifndef FOO\n"
			"print(2);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(2);") != std::string::npos, "ifndef without FOO")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk("#define FOO 1\nprint(FOO);\n", {}, Out, &Error);
		Failures += Expect(Ok, "object-like define value") ? 0 : 1;
		Failures += Expect(Out.find("print(1);") != std::string::npos, "macro replaced") ? 0 : 1;
		Failures += Expect(Out.find("FOO") == std::string::npos, "macro name gone") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define FOO 1\n"
			"print(FOOBAR);\n"
			"print(\"FOO\");\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(FOOBAR);") != std::string::npos, "no partial match")
			? 0
			: 1;
		Failures += Expect(Out.find("print(\"FOO\");") != std::string::npos, "no replace in string")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#define ID(x) (x)\n"
			"print(ID(42));\n",
			{},
			Out,
				&Error);
		Failures += Expect(Ok, "function-like define") ? 0 : 1;
		Failures += Expect(Out.find("print((42));") != std::string::npos, "function-like expand")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define ADD(a, b) ((a)+(b))\n"
			"print(ADD(2, 3));\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(((2)+(3)));") != std::string::npos, "two-arg macro")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define A 7\n"
			"#define WRAP(x) (x)\n"
			"print(WRAP(A));\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print((7));") != std::string::npos, "arg pre-expand")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define F(x) x\n"
			"print(F);\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(F);") != std::string::npos, "no invoke without paren")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define STR(x) #x\n"
			"print(STR(hello));\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(\"hello\");") != std::string::npos, "stringize")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define CAT(a, b) a##b\n"
			"#define VALUE 9\n"
			"print(CAT(VAL, UE));\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(9);") != std::string::npos, "token paste then expand")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk("#define BAD(a, a) a\n", {}, Out, &Error);
		Failures += Expect(!Ok, "reject duplicate param") ? 0 : 1;
		Failures += Expect(Error.find("duplicate") != std::string::npos, "duplicate param error")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#define F(x, y) x\n"
			"F(1)\n",
			{},
			Out,
				&Error);
		Failures += Expect(!Ok, "reject arity mismatch") ? 0 : 1;
		Failures += Expect(Error.find("argument") != std::string::npos, "arity error text") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define ZERO() 0\n"
			"print(ZERO());\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(0);") != std::string::npos, "zero-arg function-like")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
			"void main() { print(MAX(3, 8)); }\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output == "8\n", "function-like macro end-to-end") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define CALL(fn, ...) fn(__VA_ARGS__)\n"
			"print(CALL(abs, -3));\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(abs(-3));") != std::string::npos, "__VA_ARGS__ expand")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define PACK(...) (__VA_ARGS__)\n"
			"print(PACK(1, 2, 3));\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print((1, 2, 3));") != std::string::npos,
			"variadic-only __VA_ARGS__")
			? 0
			: 1;
	}

	{
		const std::string Source =
			"print(__FILE__);\n"
			"print(__LINE__);\n"
			"#define CALL_LINE() __LINE__\n"
			"print(CALL_LINE());\n"
			"#if defined(__FILE__) && __LINE__ == 5\n"
			"print(\"builtins\");\n"
			"#endif\n"
			"print(\"__LINE__\");\n";
		std::string Out;
		std::string Error;
		VeroScript::FPreprocessLineMap Map;
		const bool Ok = VeroScript::PreprocessSource(
			Source,
			{},
			{},
			VeroScript::FPreprocessContext{"Scripts\\Folder\\demo.vs"},
			Out,
			Error,
			nullptr,
			&Map);
		Failures += Expect(
			Ok && Out.find("print(\"Scripts/Folder/demo.vs\");") != std::string::npos
				&& Out.find("print(2);") != std::string::npos
				&& Out.find("print(4);") != std::string::npos
				&& Out.find("print(\"builtins\");") != std::string::npos
				&& Out.find("print(\"__LINE__\");") != std::string::npos,
			"__FILE__ and __LINE__ expand dynamically at the invocation site")
			? 0
			: 1;
		Failures += Expect(
			Map.ActiveMacros.size() >= 2
				&& Map.ActiveMacros[0].Name == "__FILE__"
				&& Map.ActiveMacros[1].Name == "__LINE__"
				&& Map.ActiveMacros[0].SourceName == "<builtin>",
			"tooling line maps expose builtin macro metadata")
			? 0
			: 1;

		const bool RedefineOk = PreprocessOk(
			"#define __LINE__ 9\n",
			{},
			Out,
			&Error);
		Failures += Expect(
			!RedefineOk
				&& Error.find("reserved preprocessor identifier") != std::string::npos,
			"builtin macros cannot be redefined")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"print(\\\n"
			"__LINE__);\n"
			"#define CONTINUED_CALL_LINE() __LINE__\n"
			"print(\\\n"
			"CONTINUED_CALL_LINE());\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(2);") != std::string::npos
				&& Out.find("print(5);") != std::string::npos,
			"__LINE__ preserves the physical invocation line across continuations")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define LOG(fmt, ...) print(fmt __VA_OPT__(,) __VA_ARGS__)\n"
			"void main() {\n"
			"    LOG(\"hello\");\n"
			"    LOG(\"value={}\", 7);\n"
			"}\n",
			{},
			Output,
			&Error);
		Failures += Expect(
			Ok && Output == "hello\nvalue=7\n",
			"__VA_OPT__ removes content for empty variadic arguments")
			? 0
			: 1;
		Failures += Expect(
			Ok && Output == "hello\nvalue=7\n",
			"__VA_OPT__ contributes content for non-empty variadic arguments")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define EMPTY\n"
			"#define OPTIONAL_VALUE(...) 0 __VA_OPT__(+ 7)\n"
			"void main() {\n"
			"    print(OPTIONAL_VALUE());\n"
			"    print(OPTIONAL_VALUE(EMPTY));\n"
			"    print(OPTIONAL_VALUE(token));\n"
			"}\n",
			{},
			Output,
			&Error);
		Failures += Expect(
			Ok && Output == "0\n0\n7\n",
			"__VA_OPT__ emptiness is tested after variadic macro expansion")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define WRAP(...) call(__VA_OPT__((__VA_ARGS__)))\n"
			"WRAP();\n"
			"WRAP(1, (2));\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("call();") != std::string::npos
				&& Out.find("call((1, (2)));") != std::string::npos,
			"__VA_OPT__ supports balanced nested parentheses")
			? 0
			: 1;
		std::string PasteOutput;
		const bool PasteOk = PreprocessOk(
			"#define PASTE_IF(left, right, ...) __VA_OPT__(left ## right)\n"
			"PASTE_IF(item, value);\n"
			"PASTE_IF(item, value, enabled);\n",
			{},
			PasteOutput);
		Failures += Expect(
			PasteOk && PasteOutput.find("\n;\n") != std::string::npos
				&& PasteOutput.find("itemvalue;") != std::string::npos,
			"__VA_OPT__ composes with token paste")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define STRINGIZED(x, ...) #__VA_OPT__(x##x x##x)\n"
			"print(STRINGIZED(, 0));\n"
			"print(STRINGIZED(a, 0));\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(\"\");") != std::string::npos
				&& Out.find("print(\"aa aa\");") != std::string::npos,
			"__VA_OPT__ composes with stringization and token paste")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define INNER_PASTE(x, ...) __VA_OPT__(a x ## x) ## b\n"
			"#define EMPTY_OPT(...) __VA_OPT__()/**/__VA_OPT__()\n"
			"#define OUTER_PASTE(x) a ## x ## b\n"
			"#define FORWARD(x) OUTER_PASTE(x)\n"
			"INNER_PASTE(, 1);\n"
			"FORWARD(EMPTY_OPT());\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("a b;") != std::string::npos
				&& Out.find("ab;") != std::string::npos,
			"__VA_OPT__ preserves placemarker semantics across adjacent pastes")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define EMPTY\n"
			"#define PRESENT(...) __VA_OPT__(7)\n"
			"#define KEEP_CHAR(x, ...) __VA_OPT__('x')\n"
			"print(PRESENT());\n"
			"print(PRESENT(EMPTY));\n"
			"print(PRESENT(','));\n"
			"print(KEEP_CHAR(9, enabled));\n"
			"print(\"__VA_OPT__(literal)\");\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print();") != std::string::npos
				&& Out.find("print(7);") != std::string::npos
				&& Out.find("print('x');") != std::string::npos
				&& Out.find("print('9');") == std::string::npos
				&& Out.find("\"__VA_OPT__(literal)\"") != std::string::npos,
			"__VA_OPT__ tests expanded tokens and ignores quoted-literal contents")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		Failures += Expect(
			!PreprocessOk(
				"#define BAD(x) __VA_OPT__(x)\n",
							{},
				Out,
				&Error)
				&& Error.find("only valid in a variadic macro") != std::string::npos,
			"non-variadic source macro rejects __VA_OPT__")
			? 0
			: 1;
		Failures += Expect(
			!PreprocessOk(
				"#define BAD(...) __VA_OPT__ value\n",
							{},
				Out,
				&Error)
				&& Error.find("must be followed by '('") != std::string::npos,
			"source macro rejects __VA_OPT__ without parentheses")
			? 0
			: 1;
		Failures += Expect(
			!PreprocessOk(
				"#define BAD(...) __VA_OPT__((value)\n",
							{},
				Out,
				&Error)
				&& Error.find("unterminated __VA_OPT__") != std::string::npos,
			"source macro rejects unterminated __VA_OPT__")
			? 0
			: 1;
		Failures += Expect(
			!PreprocessOk(
				"#define BAD(...) __VA_OPT__(__VA_OPT__(value))\n",
							{},
				Out,
				&Error)
				&& Error.find("cannot be nested") != std::string::npos,
			"source macro rejects nested __VA_OPT__")
			? 0
			: 1;
		Failures += Expect(
			!PreprocessOk(
				"#define BAD(...) __VA_OPT__(##)\n",
							{},
				Out,
				&Error)
				&& Error.find("cannot begin or end with '##'") != std::string::npos,
			"source macro rejects boundary token paste inside __VA_OPT__")
			? 0
			: 1;
		Failures += Expect(
			!PreprocessOk(
				"#define __VA_OPT__ 1\n",
							{},
				Out,
				&Error)
				&& Error.find("reserved preprocessor identifier") != std::string::npos,
			"source macro cannot redefine __VA_OPT__")
			? 0
			: 1;
		Failures += Expect(
			!PreprocessOk(
				"value;\n",
							{{"__VA_ARGS__", "1"}},
				Out,
				&Error)
				&& Error.find("reserved preprocessor identifier") != std::string::npos,
			"initial defines cannot redefine __VA_ARGS__")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define E(fmt, ...) log(fmt, ##__VA_ARGS__)\n"
			"E(\"hi\");\n"
			"E(\"n\", 1);\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("log(\"hi\");") != std::string::npos, ",##__VA_ARGS__ empty")
			? 0
			: 1;
		Failures += Expect(
			Ok && (Out.find("log(\"n\", 1);") != std::string::npos
				|| Out.find("log(\"n\",1);") != std::string::npos),
			",##__VA_ARGS__ nonempty")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define STRS(...) #__VA_ARGS__\n"
			"print(STRS(a, b));\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(\"a, b\");") != std::string::npos,
			"stringize __VA_ARGS__")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define ID(x) x\n"
			"print(ID());\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print();") != std::string::npos, "F() empty single arg")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#define F(x) __VA_ARGS__\n"
			"F(1)\n",
			{},
			Out,
				&Error);
		Failures += Expect(!Ok, "reject __VA_ARGS__ in non-variadic") ? 0 : 1;
		Failures += Expect(
			Error.find("__VA_ARGS__") != std::string::npos,
			"non-variadic __VA_ARGS__ error")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define JOIN(a, b) ((a)+(b))\n"
			"#define CALL(op, ...) op(__VA_ARGS__)\n"
			"void main() { print(CALL(JOIN, 2, 4)); }\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output == "6\n", "variadic end-to-end") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define E(fmt, ...) log(fmt, ##__VA_ARGS__)\n"
			"E();\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("log();") != std::string::npos, "named+variadic F() empty")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define JOIN(a, b, c) a##b##c\n"
			"print(JOIN(pre, , post));\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(prepost);") != std::string::npos,
			"chained ## with empty middle")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
			"#define max MAX\n"
			"print(max(3, 8));\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(((3) > (8) ? (3) : (8)));") != std::string::npos,
			"object-like alias glues to function-like call")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define G() 7\n"
			"#define F() G\n"
			"print(F()());\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(7);") != std::string::npos, "F()() glue chain")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define F() F\n"
			"print(F()());\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(F());") != std::string::npos,
			"self-painting F()() leaves trailing ()")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define ADD(a, b) ((a)+(b))\n"
			"#if ADD(1, 2) == 3\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(1);") != std::string::npos,
			"function-like in #if")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define F(x) F(x)\n"
			"print(F(1));\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(F(1));") != std::string::npos, "self-ref terminates")
			? 0
			: 1;
	}

	{
		std::string Nested = "#define ID(x) x\nprint(";
		for (int Level = 0; Level < 64; ++Level)
		{
			Nested += "ID(";
		}
		Nested += "1";
		for (int Level = 0; Level < 64; ++Level)
		{
			Nested += ")";
		}
		Nested += ");\n";
		std::string Out;
		const bool Ok = PreprocessOk(Nested, {}, Out);
		Failures += Expect(
			Ok && Out.find("print(1);") != std::string::npos,
			"moderate nested ID() expands")
			? 0
			: 1;
	}

	{
		std::string Nested = "#define ID(x) x\nprint(";
		for (int Level = 0; Level < 600; ++Level)
		{
			Nested += "ID(";
		}
		Nested += "1";
		for (int Level = 0; Level < 600; ++Level)
		{
			Nested += ")";
		}
		Nested += ");\n";
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(Nested, {}, Out, &Error);
		Failures += Expect(
			!Ok && Error.find("nested too deeply") != std::string::npos,
			"deep macro nesting rejected")
			? 0
			: 1;
	}

	{
		std::string Chain = "#define A599 1\n";
		for (int Level = 598; Level >= 0; --Level)
		{
			Chain += "#define A" + std::to_string(Level) + " A" + std::to_string(Level + 1) + "\n";
		}
		Chain += "print(A0);\n";
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(Chain, {}, Out, &Error);
		Failures += Expect(
			!Ok && Error.find("nested too deeply") != std::string::npos,
			"deep object-like chain rejected")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define INNER(x) (x)\n"
			"#define OUTER(a, b) INNER(a)+b\n"
			"print(OUTER(1, 2));\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print((1)+2);") != std::string::npos,
			"nested function-like")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define ID(x) (x)\n"
			"print(ID(1 /*plus*/ + /*two*/ 2));\n",
			{},
			Out);
		Failures += Expect(
			Ok
				&& (Out.find("print((1  +  2));") != std::string::npos
					|| Out.find("print((1 + 2));") != std::string::npos),
			"block comment in macro arg becomes space")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define STR(x) #x\n"
			"print(STR(a /*b*/ c));\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(\"a c\");") != std::string::npos,
			"stringize drops arg comments")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define F (1)\n"
			"print(F);\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print((1));") != std::string::npos,
			"space before ( is object-like")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define A B\n"
			"#define B 7\n"
			"print(A);\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(7);") != std::string::npos, "nested expand") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 1 + 1 == 2\n"
			"print(1);\n"
			"#else\n"
			"print(0);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "#if arithmetic")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define VER 2\n"
			"#if VER >= 2\n"
			"print(ok);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(ok);") != std::string::npos, "#if macro value")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if defined(FOO)\n"
			"print(1);\n"
			"#elif defined FOO\n"
			"print(2);\n"
			"#else\n"
			"print(3);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(3);") != std::string::npos, "#if defined else")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define FOO\n"
			"#if defined(FOO) && 1\n"
			"print(yes);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(yes);") != std::string::npos, "#if defined &&")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 0\n"
			"print(a);\n"
			"#elif 1\n"
			"print(b);\n"
			"#else\n"
			"print(c);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(b);") != std::string::npos, "#elif taken") ? 0 : 1;
		Failures += Expect(Out.find("print(a);") == std::string::npos, "#if 0 blanked") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 1 // keep\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "#if line comment")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 1 /* block */\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "#if block comment")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 010 == 8\n"
			"print(1);\n"
			"#else\n"
			"print(0);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "#if octal") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 1\n"
			"print(a);\n"
			"#elif 1\n"
			"print(b);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(a);") != std::string::npos
				&& Out.find("print(b);") == std::string::npos,
			"#elif after taken skipped")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define OPEN /*\n"
			"int dead = 1;\n"
			"*/\n"
			"print(1);\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "define opens file comment")
			? 0
			: 1;
		Failures += Expect(
			Out.find("/*") != std::string::npos && Out.find("*/") != std::string::npos,
			"define-opened comment delimiters in output")
			? 0
			: 1;
	}

	{
		// Inactive unclosed `/*` must not leak a hanging comment into the lexer output.
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 0\n"
			"#define OPEN /*\n"
			"secret\n"
			"*/\n"
			"#endif\n"
			"print(1);\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(1);") != std::string::npos,
			"inactive define /* must not swallow later code")
			? 0
			: 1;
		Failures += Expect(Out.find("/*") == std::string::npos, "no hanging /* from inactive define")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 0 /*\n"
			"print(dead);\n"
			"*/\n"
			"print(kept);\n"
			"#endif\n"
			"print(1);\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(1);") != std::string::npos,
			"inactive #if /* must not hang comment")
			? 0
			: 1;
		Failures += Expect(Out.find("print(kept);") == std::string::npos, "#if 0 body blanked")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define OPEN /*\n"
			"int dead = 1;\n"
			"*/\n"
			"void main() { print(1); }\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output.find("1") != std::string::npos, "define /* comments following lines")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define N 7 // trailing\n"
			"void main() { print(N); }\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output.find("7") != std::string::npos, "define strips // comment")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define N 3 /* c */\n"
			"void main() { print(N); }\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output.find("3") != std::string::npos, "define strips block comment")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define N 9 /*\n"
			"int dead = 1;\n"
			"*/\n"
			"void main() { print(N); }\n",
			{},
			Output,
				&Error);
		Failures += Expect(
			Ok && Output.find("9") != std::string::npos,
			"define unclosed /* starts file comment, body is 9")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 1 /*\n"
			"print(dead);\n"
			"*/\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "#if unclosed /*")
			? 0
			: 1;
		Failures += Expect(
			Out.find("print(dead);") == std::string::npos
				|| (Out.find("/*") != std::string::npos
					&& Out.find("print(dead);") > Out.find("/*")
					&& Out.find("print(dead);") < Out.find("*/")),
			"#if comment swallows following line")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#define N 1 + \\\n"
			"2\n"
			"print(N);\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1 + 2);") != std::string::npos, "backslash join define")
			? 0
			: 1;
		// True join: one blank (directive) + one code line — not an extra pad line.
		Failures += Expect(Out == "\nprint(1 + 2);\n", "backslash true join no pad") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"print(1 + \\\n"
			"2);\n",
			{},
			Out);
		Failures += Expect(Ok && Out == "print(1 + 2);\n", "backslash code true join") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 1 + \\\n"
			"1 == 2\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "backslash join #if")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"void main() {\n"
			"  print(1 + \\\n"
			"2);\n"
			"}\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output.find("3") != std::string::npos, "backslash join code")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 0 && (1/0)\n"
			"print(dead);\n"
			"#else\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "&& short-circuit")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 1 || (1/0)\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "|| short-circuit")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk("#if 1 && (1/0)\nprint(1);\n#endif\n", {}, Out, &Error);
		Failures += Expect(!Ok, "&& still evaluates right when left true") ? 0 : 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk("#if 1 << 64\nprint(1);\n#endif\n", {}, Out, &Error);
		Failures += Expect(!Ok && Error.find("shift") != std::string::npos, "reject shift >= 64")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 0 && (1 << 999)\n"
			"print(dead);\n"
			"#else\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(Ok && Out.find("print(1);") != std::string::npos, "shift UB skipped in &&")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#if 0\n"
			"#if\n"
			"#endif\n"
			"#endif\n",
			{},
			Out,
				&Error);
		Failures += Expect(!Ok && Error.find("empty") != std::string::npos, "reject empty #if when skipped")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#if 1\n"
			"print(1);\n"
			"#elif\n"
			"#endif\n",
			{},
			Out,
				&Error);
		Failures += Expect(!Ok && Error.find("empty") != std::string::npos, "reject empty #elif")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#if 1\n"
			"print(1);\n"
			"#elif !!!\n"
			"#endif\n",
			{},
			Out,
				&Error);
		Failures += Expect(!Ok, "reject garbage #elif even when skipped") ? 0 : 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 1\n"
			"print(1);\n"
			"#elif 1/0\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(1);") != std::string::npos,
			"skipped #elif 1/0 still ok (syntax-only)")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#if 0\n"
			"#if !!!\n"
			"#endif\n"
			"#endif\n",
			{},
			Out,
				&Error);
		Failures += Expect(!Ok, "reject garbage nested #if when parent inactive") ? 0 : 1;
	}

	{
		std::string Out;
		std::string Error;
		std::uint32_t ErrorLine = 0;
		const bool Ok = PreprocessOk("#ifdef FOO\nprint(1);\n", {}, Out, &Error, &ErrorLine);
		Failures += Expect(!Ok, "unterminated ifdef") ? 0 : 1;
		Failures += Expect(ErrorLine == 1, "unterminated points at open directive") ? 0 : 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#ifdef DEBUG\n"
			"void main() { print(\"dbg\"); }\n"
			"#else\n"
			"void main() { print(\"rel\"); }\n"
			"#endif\n",
			{"DEBUG"},
			Output,
				&Error);
		Failures += Expect(Ok, "run with -D DEBUG") ? 0 : 1;
		Failures += Expect(Output.find("dbg") != std::string::npos, "DEBUG branch ran") ? 0 : 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#ifdef DEBUG\n"
			"void main() { print(\"dbg\"); }\n"
			"#else\n"
			"void main() { print(\"rel\"); }\n"
			"#endif\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok, "run without DEBUG") ? 0 : 1;
		Failures += Expect(Output.find("rel") != std::string::npos, "release branch ran") ? 0 : 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define LOCAL\n"
			"#ifdef LOCAL\n"
			"void main() { print(\"local\"); }\n"
			"#endif\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output.find("local") != std::string::npos, "file-local define")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define LOCAL\n"
			"#undef LOCAL\n"
			"#ifdef LOCAL\n"
			"void main() { print(\"bad\"); }\n"
			"#else\n"
			"void main() { print(\"ok\"); }\n"
			"#endif\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output.find("ok") != std::string::npos, "undef works") ? 0 : 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"void main() { print(N); }\n",
			{"N=42"},
			Output,
				&Error);
		Failures += Expect(Ok && Output.find("42") != std::string::npos, "CLI -DN=42 replace")
			? 0
			: 1;
	}

	{
		std::string Output;
		std::string Error;
		const bool Ok = RunWithDefines(
			"#define MSG \"hi\"\n"
			"void main() { print(MSG); }\n",
			{},
			Output,
				&Error);
		Failures += Expect(Ok && Output.find("hi") != std::string::npos, "string body macro")
			? 0
			: 1;
	}

	{
		const auto Sorted = VeroScript::NormalizeDefineList({"B=2", "A", "A", ""});
		Failures += Expect(
			Sorted.size() == 2 && Sorted[0] == "A" && Sorted[1] == "B=2",
			"normalize")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#if 18446744073709551616\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out,
				&Error);
		Failures += Expect(
			!Ok && Error.find("out of range") != std::string::npos,
			"reject oversized #if literal")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#if 9223372036854775807 + 1\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out,
				&Error);
		Failures += Expect(
			!Ok && Error.find("overflow") != std::string::npos,
			"reject #if addition overflow")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk(
			"#if 4611686018427387904 * 2\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out,
				&Error);
		Failures += Expect(
			!Ok && Error.find("overflow") != std::string::npos,
			"reject #if multiplication overflow")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		const bool Ok = PreprocessOk("#if 1 << 63\nprint(1);\n#endif\n", {}, Out, &Error);
		Failures += Expect(
			!Ok && Error.find("overflow") != std::string::npos,
			"reject #if left shift overflow")
			? 0
			: 1;
	}

	{
		std::string Out;
		const bool Ok = PreprocessOk(
			"#if 0 && (9223372036854775807 + 1)\n"
			"print(dead);\n"
			"#else\n"
			"print(1);\n"
			"#endif\n",
			{},
			Out);
		Failures += Expect(
			Ok && Out.find("print(1);") != std::string::npos,
			"addition overflow skipped in &&")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		VeroScript::FPreprocessLineMap Map;
		std::unordered_map<std::string, std::string> Defines;
		const bool MapOk = VeroScript::PreprocessSource(
			"#define VAL 99\nvoid main() { print(VAL); }\n",
			Defines,
			Out,
			Error,
			nullptr,
				&Map);
		std::uint32_t PhysicalLine = 0;
		std::uint32_t PhysicalColumn = 0;
		Failures += Expect(MapOk && !Map.OutByteToPhysical.empty(), "source map emitted") ? 0 : 1;
		Failures += Expect(
			Map.ActiveMacros.size() == 3 && Map.ActiveMacros[2].bActive
				&& Map.ActiveMacros[2].Name == "VAL",
			"define recorded as active macro")
			? 0
			: 1;
		Failures += Expect(
			VeroScript::MapPreprocessedToPhysical(Map, 2, 21, PhysicalLine, PhysicalColumn)
				&& PhysicalLine == 2
				&& PhysicalColumn == 21,
			"expanded VAL maps to macro name site")
			? 0
			: 1;
		std::uint32_t PreprocessedLine = 0;
		std::uint32_t PreprocessedColumn = 0;
		Failures += Expect(
			VeroScript::MapPhysicalToPreprocessed(
				Map,
				2,
				21,
				PreprocessedLine,
				PreprocessedColumn)
					&& PreprocessedLine == 2
					&& PreprocessedColumn == 21,
			"physical VAL site maps back to expansion")
			? 0
			: 1;
		std::uint32_t PhysLine = 0;
		std::uint32_t PrepLine = 0;
		const bool LineMapOk = VeroScript::PreprocessSource(
			"#define VAL 99\n"
			"void main() {\n"
			"    print(VAL);\n"
			"}\n",
			Defines,
			Out,
			Error,
			nullptr,
				&Map);
		Failures += Expect(LineMapOk, "line map source for breakpoint") ? 0 : 1;
		Failures += Expect(
			VeroScript::MapPhysicalLineToPreprocessedLine(Map, 3, PrepLine)
				&& PrepLine == 3,
			"physical macro use line maps to preprocessed")
			? 0
			: 1;
		Failures += Expect(
			VeroScript::MapPreprocessedLineToPhysicalLine(Map, PrepLine, PhysLine)
				&& PhysLine == 3,
			"preprocessed line maps back to physical macro use")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		VeroScript::FPreprocessLineMap Map;
		std::unordered_map<std::string, std::string> Defines;
		const bool MapOk = VeroScript::PreprocessSource(
			"#define TEMP 1\n#undef TEMP\n#define TEMP 7\nvoid main() {}\n",
			Defines,
			Out,
			Error,
			nullptr,
				&Map);
		Failures += Expect(MapOk && Map.ActiveMacros.size() == 5, "undef adds timeline tombstone")
			? 0
			: 1;
		Failures += Expect(
			Map.ActiveMacros[2].bActive && Map.ActiveMacros[2].Body == "1",
			"first define kept as define event")
			? 0
			: 1;
		Failures += Expect(
			!Map.ActiveMacros[3].bActive && Map.ActiveMacros[3].Name == "TEMP",
			"undef recorded as inactive tombstone")
			? 0
			: 1;
		Failures += Expect(
			Map.ActiveMacros[4].bActive && Map.ActiveMacros[4].Body == "7",
			"redefine after undef is active define event")
			? 0
			: 1;
	}

	{
		const std::vector<VeroScript::FPredefinedMacro> Macros{
			{"HOST_STR", {"value"}, false, "#value", "unit-host"},
			{"HOST_CAT", {"left", "right"}, false, "left ## right", "unit-host"},
			{"HOST_ADD", {"first"}, true, "first + __VA_ARGS__", "unit-host"},
			{"HOST_OPT", {"first"}, true,
				"first __VA_OPT__(+ __VA_ARGS__)", "unit-host"},
			{"HOST_STR_OPT", {"value"}, true,
				"#__VA_OPT__(value ## value)", "unit-host"}};
		std::string Out;
		std::string Error;
		VeroScript::FPreprocessLineMap Map;
		const std::unordered_map<std::string, std::string> Defines;
		const bool Ok = VeroScript::PreprocessSource(
			"int token = 4;\n"
			"print(HOST_STR(hello world));\n"
			"print(HOST_CAT(to, ken));\n"
			"print(HOST_ADD(1, 2));\n"
			"print(HOST_OPT(4));\n"
			"print(HOST_OPT(4, 5));\n"
			"print(HOST_STR_OPT(,));\n"
			"print(HOST_STR_OPT(x, enabled));\n",
			Defines,
			Macros,
			Out,
			Error,
			nullptr,
			&Map);
		Failures += Expect(Ok, "host predefined function macros preprocess") ? 0 : 1;
		Failures += Expect(
			Out.find("\"hello world\"") != std::string::npos,
			"host macro supports stringize")
			? 0
			: 1;
		Failures += Expect(
			Out.find("token") != std::string::npos,
			"host macro supports token paste")
			? 0
			: 1;
		Failures += Expect(
			Out.find("1 + 2") != std::string::npos,
			"host macro supports __VA_ARGS__")
			? 0
			: 1;
		Failures += Expect(
			Out.find("print(4") != std::string::npos
				&& Out.find("4 + 5") != std::string::npos
				&& Out.find("print(\"\")") != std::string::npos
				&& Out.find("print(\"xx\")") != std::string::npos,
			"host macro supports __VA_OPT__")
			? 0
			: 1;
		Failures += Expect(
			Map.ActiveMacros.size() == 7
				&& Map.ActiveMacros[0].bPredefined
				&& Map.ActiveMacros[0].DirectiveStartLine == 0,
			"host macros are visible to tooling as predefined macro events")
			? 0
			: 1;
	}

	{
		const std::vector<VeroScript::FPredefinedMacro> Macros{
			{"HOST_VALUE", {"value"}, false, "value", "<host>"}};
		std::string Out;
		std::string Error;
		const std::unordered_map<std::string, std::string> Defines;
		const bool DirectRedefine = VeroScript::PreprocessSource(
			"#define HOST_VALUE(value) value + 1\n",
			Defines,
			Macros,
			Out,
			Error);
		Failures += Expect(
			!DirectRedefine && Error.find("#undef") != std::string::npos,
			"predefined macro direct redefine is rejected")
			? 0
			: 1;
		const bool UndefThenRedefine = VeroScript::PreprocessSource(
			"#undef HOST_VALUE\n"
			"#define HOST_VALUE(value) value + 1\n"
			"print(HOST_VALUE(2));\n",
			Defines,
			Macros,
			Out,
			Error);
		Failures += Expect(
			UndefThenRedefine && Out.find("2 + 1") != std::string::npos,
			"#undef permits translation-unit-local replacement")
			? 0
			: 1;
	}

	{
		std::string Error;
		Failures += Expect(
			!VeroScript::ValidatePredefinedMacros(
							{{"BAD", {"x"}, false, "__VA_ARGS__", "<host>"}},
				Error),
			"non-variadic predefined macro rejects __VA_ARGS__")
			? 0
			: 1;
		Failures += Expect(
			!VeroScript::ValidatePredefinedMacros(
							{{"BAD_OPT", {"x"}, false, "__VA_OPT__(x)", "<host>"}},
				Error)
				&& Error.find("__VA_OPT__") != std::string::npos,
			"non-variadic predefined macro rejects __VA_OPT__")
			? 0
			: 1;
		Failures += Expect(
			!VeroScript::ValidatePredefinedMacros(
							{{"__VA_OPT__", {}, true, "__VA_ARGS__", "<host>"}},
				Error)
				&& Error.find("reserved preprocessor identifier") != std::string::npos,
			"host cannot register __VA_OPT__ as a macro name")
			? 0
			: 1;
		Failures += Expect(
			!VeroScript::ValidatePredefinedMacros(
							{{"DUP", {"x", "x"}, false, "x", "<host>"}},
				Error),
			"predefined macro rejects duplicate parameters")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		std::uint32_t ErrorLine = 0;
		const std::unordered_map<std::string, std::string> Defines;
		const bool Ok = VeroScript::PreprocessSource(
			"#include \"dep.vs\" // module alias\n"
			"#if 0\n"
			"#include \"inactive.vs\"\n"
			"#endif\n"
			"void main() {}\n",
			Defines,
			Out,
			Error,
			&ErrorLine);
		Failures += Expect(
			Ok
				&& Out.find("export import \"dep.vs\";") != std::string::npos
				&& Out.find("inactive.vs") == std::string::npos,
			"#include lowers to export import only in an active branch")
			? 0
			: 1;

		for (const std::string& Invalid : {
				 std::string("#include <dep.vs>\n"),
				std::string("#include \"\"\n"),
				std::string("#include \"dep.vs\" trailing\n"),
				std::string("#include \"unterminated\n")})
		{
			Failures += Expect(
				!VeroScript::PreprocessSource(
					Invalid,
					Defines,
					Out,
					Error,
					&ErrorLine)
					&& ErrorLine == 1,
				"invalid #include syntax is rejected on its source line")
				? 0
				: 1;
		}
		Failures += Expect(
			!VeroScript::PreprocessSource(
				"#pragma once\n",
				Defines,
				Out,
				Error,
				&ErrorLine)
				&& ErrorLine == 1
				&& Error.find("unsupported preprocessor directive: #pragma")
					!= std::string::npos,
			"#pragma once is intentionally unnecessary and unsupported")
			? 0
			: 1;
	}

	{
		// Keep scratch files under this build tree. Using one process-global temp
		// name makes simultaneous x86/x64 CTest runs delete each other's modules.
		const std::filesystem::path Root = std::filesystem::current_path()
			/ "VeroScriptPreprocessorTests" / "include_alias";
		std::error_code ErrorCode;
		std::filesystem::remove_all(Root, ErrorCode);
		std::filesystem::create_directories(Root, ErrorCode);
		const std::filesystem::path MainPath = Root / "main.vs";
		const std::filesystem::path DependencyPath = Root / "dep.vs";
		{
			std::ofstream Main(MainPath, std::ios::binary);
			Main << "#include \"dep.vs\"\n"
					"#include \"dep.vs\"\n"
					"void main() { print(included_value()); }\n";
			std::ofstream Dependency(DependencyPath, std::ios::binary);
			Dependency << "export int included_value() { return 42; }\n"
						  "int private_value() { return 7; }\n";
		}

		VeroScript::VeroScriptModuleLoader Loader(MainPath.generic_string());
		const std::unique_ptr<VeroScript::FProgram> Program = Loader.LoadMainModule();
		std::unique_ptr<VeroScript::FBytecodeModule> Module;
		if (Program)
		{
			VeroScript::VeroScriptCompiler Compiler;
			Module = Compiler.Compile(*Program);
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		bool bExecuted = false;
		if (Module)
		{
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			bExecuted = Vm.Execute(*Module, VeroScript::ERunMode::Script);
		}
		Failures += Expect(
			Program && Module && bExecuted && Output.str() == "42\n",
			"#include uses module import resolution and duplicate suppression")
			? 0
			: 1;

		{
			std::ofstream Main(MainPath, std::ios::binary);
			Main << "#include \"dep.vs\"\n"
					"void main() { print(private_value()); }\n";
		}
		VeroScript::VeroScriptModuleLoader PrivateLoader(MainPath.generic_string());
		const std::unique_ptr<VeroScript::FProgram> PrivateProgram =
			PrivateLoader.LoadMainModule();
		std::unique_ptr<VeroScript::FBytecodeModule> PrivateModule;
		bool bPrivateCompileFailed = !PrivateProgram;
		if (PrivateProgram)
		{
			VeroScript::VeroScriptCompiler Compiler;
			PrivateModule = Compiler.Compile(*PrivateProgram);
			bPrivateCompileFailed = !PrivateModule || Compiler.HasError();
		}
		Failures += Expect(
			bPrivateCompileFailed,
			"#include preserves export visibility")
			? 0
			: 1;

		const std::filesystem::path FacadePath = Root / "facade.vs";
		const std::filesystem::path ConsumerPath = Root / "consumer.vs";
		{
			std::ofstream Facade(FacadePath, std::ios::binary);
			Facade << "#include \"dep.vs\"\n";
			std::ofstream Consumer(ConsumerPath, std::ios::binary);
			Consumer << "import \"facade.vs\";\n"
						"void main() { print(included_value()); }\n";
		}
		VeroScript::VeroScriptModuleLoader ConsumerLoader(ConsumerPath.generic_string());
		const std::unique_ptr<VeroScript::FProgram> ConsumerProgram =
			ConsumerLoader.LoadMainModule();
		std::unique_ptr<VeroScript::FBytecodeModule> ConsumerModule;
		if (ConsumerProgram)
		{
			VeroScript::VeroScriptCompiler Compiler;
			ConsumerModule = Compiler.Compile(*ConsumerProgram);
		}
		std::ostringstream ConsumerOutput;
		std::istringstream ConsumerInput;
		std::ostringstream ConsumerErrors;
		bool bConsumerExecuted = false;
		if (ConsumerModule)
		{
			VeroScript::VeroScriptVm Vm(
				ConsumerOutput,
				ConsumerInput,
				ConsumerErrors);
		VeroScript::Test::Configure(Vm);
			bConsumerExecuted = Vm.Execute(
				*ConsumerModule,
				VeroScript::ERunMode::Script);
		}
		Failures += Expect(
			ConsumerProgram
				&& ConsumerModule
				&& bConsumerExecuted
				&& ConsumerOutput.str() == "42\n",
			"#include re-exports transitively without an include guard")
			? 0
			: 1;

		const std::filesystem::path CyclePath = Root / "cycle.vs";
		{
			std::ofstream Main(MainPath, std::ios::binary);
			Main << "#include \"cycle.vs\"\nvoid main() {}\n";
			std::ofstream Cycle(CyclePath, std::ios::binary);
			Cycle << "#include \"main.vs\"\n";
		}
		VeroScript::VeroScriptModuleLoader CycleLoader(MainPath.generic_string());
		CycleLoader.SetAllowCircularImports(false);
		const std::unique_ptr<VeroScript::FProgram> CycleProgram =
			CycleLoader.LoadMainModule();
		Failures += Expect(
			!CycleProgram && CycleLoader.HasError()
				&& CycleLoader.GetError().Message.find("circular import")
					!= std::string::npos,
			"#include preserves strict circular-import diagnostics")
			? 0
			: 1;

		{
			std::ofstream Main(MainPath, std::ios::binary);
			Main << "#include \"d\\x65p.vs\"\nvoid main() {}\n";
			std::ofstream Dependency(DependencyPath, std::ios::binary);
			Dependency << "// dependency guard\n#error dependency publication stop\n";
		}
		VeroScript::VeroScriptModuleLoader ErrorDirectiveLoader(
			MainPath.generic_string());
		const std::unique_ptr<VeroScript::FProgram> ErrorDirectiveProgram =
			ErrorDirectiveLoader.LoadMainModule();
		const VeroScript::FScriptError& ErrorDirective =
			ErrorDirectiveLoader.GetError();
		Failures += Expect(
			!ErrorDirectiveProgram
				&& ErrorDirectiveLoader.HasError()
				&& ErrorDirective.Message.find("#error: dependency publication stop")
					!= std::string::npos
				&& ErrorDirective.Location.Line == 2
				&& ErrorDirective.Location.FileLabel.find("dep.vs")
					!= std::string::npos,
			"#error preserves dependency file and line through an include chain")
			? 0
			: 1;
		std::filesystem::remove_all(Root, ErrorCode);
	}

	{
		const std::string Source = "void main() { print(HOST_ID(7)); }\n";
		VeroScript::VeroScriptModuleLoader First("fingerprint.vs");
		First.SetPredefinedMacros({{"HOST_ID", {"x"}, false, "x", "<host>"}});
		const std::unique_ptr<VeroScript::FProgram> FirstProgram =
			First.LoadSourceModule(Source, "fingerprint.vs");
		VeroScript::VeroScriptModuleLoader Second("fingerprint.vs");
		Second.SetPredefinedMacros({{"HOST_ID", {"x"}, false, "x + 1", "<host>"}});
		const std::unique_ptr<VeroScript::FProgram> SecondProgram =
			Second.LoadSourceModule(Source, "fingerprint.vs");
		Failures += Expect(
			FirstProgram && SecondProgram
				&& First.GetSourceFingerprint() != Second.GetSourceFingerprint(),
			"predefined macro body participates in module fingerprint")
			? 0
			: 1;
	}

	{
		VeroScript::VeroScriptModuleLoader Baseline("fingerprint.vs");
		VeroScript::VeroScriptModuleLoader WithImplicit("fingerprint.vs");
		WithImplicit.AddImplicitImportPath("shared.vs");
		VeroScript::VeroScriptModuleLoader StrictCircular("fingerprint.vs");
		StrictCircular.SetAllowCircularImports(false);
		VeroScript::VeroScriptModuleLoader ProjectWide("fingerprint.vs");
		ProjectWide.SetAllowProjectWideAccess(true);
		Failures += Expect(
			Baseline.GetModuleResolutionFingerprint()
					!= WithImplicit.GetModuleResolutionFingerprint()
				&& Baseline.GetModuleResolutionFingerprint()
					!= StrictCircular.GetModuleResolutionFingerprint()
				&& Baseline.GetModuleResolutionFingerprint()
					!= ProjectWide.GetModuleResolutionFingerprint(),
			"module-loading policy participates in the resolution fingerprint")
			? 0
			: 1;
	}

	{
		const std::filesystem::path Root = std::filesystem::current_path()
			/ "VeroScriptPreprocessorTests" / "predefined_macro_import";
		std::error_code ErrorCode;
		std::filesystem::remove_all(Root, ErrorCode);
		std::filesystem::create_directories(Root, ErrorCode);
		const std::filesystem::path MainPath = Root / "main.vs";
		const std::filesystem::path ImportPath = Root / "dep.vs";
		{
			std::ofstream Main(MainPath, std::ios::binary);
			Main << "import \"dep.vs\";\nvoid main() { print(imported()); }\n";
			std::ofstream Imported(ImportPath, std::ios::binary);
			Imported << "export int imported() { return HOST_SUM(20, 22); }\n";
		}
		VeroScript::VeroScriptModuleLoader Loader(MainPath.generic_string());
		Loader.SetPredefinedMacros(
			{{"HOST_SUM", {"left", "right"}, false, "left + right", "import-host"}});
		const std::unique_ptr<VeroScript::FProgram> Program = Loader.LoadMainModule();
		std::unique_ptr<VeroScript::FBytecodeModule> Module;
		if (Program)
		{
			VeroScript::VeroScriptCompiler Compiler;
			Module = Compiler.Compile(*Program);
		}
		Failures += Expect(
			Program && Module && !Loader.HasError(),
			"predefined macros seed every imported translation unit")
			? 0
			: 1;
		std::filesystem::remove_all(Root, ErrorCode);
	}

	{
		std::string DeepUnary = "#if ";
		DeepUnary.append(300, '!');
		DeepUnary += "1\n#endif\n";
		std::string Out;
		std::string Error;
		Failures += Expect(
			!PreprocessOk(DeepUnary, {}, Out, &Error)
				&& Error.find("#if expression nested too deeply") != std::string::npos,
			"deep #if unary expressions are diagnosed")
			? 0
			: 1;
	}

	{
		// A loaded program is a self-contained compiler input. Its token-backed
		// string_views must remain valid after the short-lived loader is destroyed.
		std::unique_ptr<VeroScript::FProgram> Program;
		std::string LoadError;
		{
			VeroScript::VeroScriptModuleLoader Loader("lifetime.vs");
			Program = Loader.LoadSourceModule(
				"namespace Lifetime { struct Item { string Name; } }\n"
				"void main() { Lifetime::Item item = Lifetime::Item(); item.Name = \"alive\"; print(item.Name); }\n",
				"lifetime.vs");
			if (!Program && Loader.HasError())
			{
				LoadError = Loader.GetError().Message;
			}
		}
		std::vector<std::string> MemoryChurn(
			4096, std::string(512, 'x'));
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Program
			? Compiler.Compile(*Program)
			: nullptr;
		if (!Program || !Module || Compiler.HasError())
		{
			std::cerr << "loader lifetime detail: "
				<< (!LoadError.empty() ? LoadError : Compiler.GetError().Message)
				<< '\n';
		}
		Failures += Expect(
			Program && !Program->LifetimeAnchors.empty() && Module && !Compiler.HasError(),
			"loaded program retains parser backing storage after loader destruction")
			? 0
			: 1;
	}

	{
		std::string Out;
		std::string Error;
		std::uint32_t ErrorLine = 0;
		Failures += Expect(
			!PreprocessOk(
				"#if 1\n#error publication stopped: 数据无效\n#endif\n",
							{},
				Out,
				&Error,
				&ErrorLine)
				&& ErrorLine == 2
				&& Error.find("#error: publication stopped: 数据无效") != std::string::npos,
			"active #error preserves its message and line")
			? 0
			: 1;

		Error.clear();
		ErrorLine = 0;
		Failures += Expect(
			PreprocessOk(
				"#if 0\n#error ignored\n#endif\nprint(1);\n",
							{},
				Out,
				&Error,
				&ErrorLine)
				&& Out.find("print(1);") != std::string::npos,
			"inactive #error is ignored")
			? 0
			: 1;

		Error.clear();
		ErrorLine = 0;
		Failures += Expect(
			!PreprocessOk("#error\n", {}, Out, &Error, &ErrorLine)
				&& ErrorLine == 1 && Error == "#error",
			"empty active #error has a stable diagnostic")
			? 0
			: 1;

		Error.clear();
		Failures += Expect(
			!PreprocessOk("#define MESSAGE expanded\n#error MESSAGE\n", {}, Out, &Error)
				&& Error == "#error: MESSAGE",
			"#error payload is deliberately not macro-expanded")
			? 0
			: 1;

		Failures += Expect(
			PreprocessOk(
				"#define FIRST(a, b) a\n"
				"#define HASH '#'\n"
				"char comma = FIRST(',', ')');\n"
				"char close = FIRST(')', '(');\n"
				"char hash = HASH; char slash = '/';\n",
							{},
				Out)
				&& Out.find("char comma = ',';") != std::string::npos
				&& Out.find("char close = ')';") != std::string::npos
				&& Out.find("char hash = '#'; char slash = '/';") != std::string::npos,
			"character literals protect macro commas, parentheses, hash and slash tokens")
			? 0
			: 1;

		for (const auto& Unsupported : {
				 std::pair{"#warning use #error for a hard failure\n", "#warning"},
				std::pair{"#line 12\n", "#line"},
				std::pair{"#pragma once\n", "#pragma"}})
		{
			Error.clear();
			Failures += Expect(
				!PreprocessOk(Unsupported.first, {}, Out, &Error)
					&& Error.find("unsupported preprocessor directive: ") != std::string::npos
					&& Error.find(Unsupported.second) != std::string::npos,
				"unsupported preprocessor directives are identified by name")
				? 0
				: 1;
		}
	}

	{
		auto Budget = std::make_shared<VeroScript::FPreprocessBudget>();
		Budget->MaxOutputBytes = 4;
		VeroScript::FPreprocessContext Context;
		Context.Budget = Budget;
		VeroScript::FPreprocessorSession Exact("1234", {}, {}, Context, true);
		Failures += Expect(Exact.Advance() == VeroScript::EPreprocessStep::Complete && Exact.Source() == "1234",
		    "preprocessor output exactly at its limit succeeds") ? 0 : 1;
		VeroScript::FPreprocessorSession Excess("12345", {}, {}, Context, true);
		Failures += Expect(Excess.Advance() == VeroScript::EPreprocessStep::Error
		    && Excess.Error().find("limit exceeded") != std::string::npos
		    && Excess.Source().empty() && Excess.LineMap().OutByteToPhysical.empty(),
		    "preprocessor bounds output before publishing partial source or maps") ? 0 : 1;
	}
	{
		auto Budget = std::make_shared<VeroScript::FPreprocessBudget>();
		Budget->MaxWork = 17;
		VeroScript::FPreprocessContext Context;
		Context.Budget = Budget;
		VeroScript::FPreprocessorSession First("abc", {}, {}, Context);
		VeroScript::FPreprocessorSession Second("abc", {}, {}, Context);
		Failures += Expect(First.Advance() == VeroScript::EPreprocessStep::Complete
		    && Second.Advance() == VeroScript::EPreprocessStep::Error,
		    "files in one preprocessor request share consumed work") ? 0 : 1;
	}

	{
		auto Budget = std::make_shared<VeroScript::FPreprocessBudget>();
		Budget->MaxOutputBytes = 64;
		VeroScript::FPreprocessContext Context;
		Context.Budget = Budget;
		VeroScript::FPreprocessorSession Session(
		    "#define A(x) x x\n#define B A(A(A(A(12345678))))\nB\n", {}, {}, Context, true);
		Failures += Expect(Session.Advance() == VeroScript::EPreprocessStep::Error
		    && Session.Error().find("limit exceeded") != std::string::npos
		    && Session.Source().empty() && Session.LineMap().OutByteToPhysical.empty(),
		    "function macro argument prescans obey the same expansion budget") ? 0 : 1;
	}
	{
		auto Budget = std::make_shared<VeroScript::FPreprocessBudget>();
		VeroScript::FPreprocessContext Context;
		Context.Mode = VeroScript::EPreprocessMode::Module;
		Context.Budget = Budget;
		VeroScript::FPreprocessorSession Session("import \"library.vs\";\nMACRO\n", {}, {}, Context, true);
		VeroScript::FPublicMacro Macro;
		Macro.Name = "MACRO";
		Macro.Body = "9";
		Macro.OriginModule = Macro.SourceName = "library.vs";
		Macro.Line = Macro.Column = 1;
		Macro.DefinitionOrdinal = 1;
		Macro.DirectiveStartLine = Macro.DirectiveEndLine = 1;
		const bool Waiting = Session.Advance() == VeroScript::EPreprocessStep::NeedModule;
		Budget->MaxWork = Budget->Work + 1;
		Failures += Expect(Waiting && !Session.ResumeImport("library.vs", {Macro})
		    && Session.Error().find("limit exceeded") != std::string::npos && Session.Source().empty()
		    && Session.LineMap().OutByteToPhysical.empty(), "import resume preserves and charges the request budget") ? 0 : 1;
	}
	if (Failures != 0)
	{
		std::cerr << Failures << " failure(s)\n";
		return 1;
	}
	std::cout << "VeroScriptPreprocessorTests: ok\n";
	return 0;
}
