// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"
#include "VeroScriptModuleMeta.h"

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
	    std::string* OutError = nullptr,
	    std::unique_ptr<VeroScript::FBytecodeModule>* OutModule = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "ffo.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "ffo.vs");
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
		if (OutModule)
		{
			*OutModule = std::move(Module);
		}
		return true;
	}

	bool CompileFails(const std::string& Source, std::string* OutError = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "ffo.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "ffo.vs");
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
	            "void show() { print(\"0\"); }\n"
	            "void show(int x) { print(x); }\n"
	            "void show(string s) { print(s); }\n"
	            "void main() {\n"
	            "    show();\n"
	            "    show(7);\n"
	            "    show(\"ok\");\n"
	            "}\n",
	            Output,
	            &Error),
	        "arity + type free-function overloads") ||
	    !Expect(Output == "0\n7\nok\n", "overload output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// A partially explicit template argument list must not select an otherwise
	// incompatible exact-arity template before value-parameter scoring.
	if (!Expect(
	        Run(
	            "T choose<T>(T left, T right) { return left; }\n"
	            "U choose<T, U>(T left, U right) { return right; }\n"
	            "void main() {\n"
	            "    print(choose<int>(1, \"explicit\"));\n"
	            "    print(choose(2, \"inferred\"));\n"
	            "}\n",
	            Output,
	            &Error),
	        "generic overload value-parameter scoring") ||
	    !Expect(
	        Output == "explicit\ninferred\n",
	        "generic overload scoring output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
	        CompileFails(
	            "void show(int x) { print(x); }\n"
	            "void show(int y) { print(y); }\n"
	            "void main() {}\n",
	            &Error),
	        "same signature rejected"))
	{
		std::cerr << "expected same-signature fail\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
	        Run(
	            "class Base {}\n"
	            "class Derived : public Base {}\n"
	            "void take(Base b) { print(\"base\"); }\n"
	            "void take(Derived d) { print(\"derived\"); }\n"
	            "void main() {\n"
	            "    take(Derived());\n"
	            "    take(Base());\n"
	            "}\n",
	            Output,
	            &Error),
	        "subtype prefers exact free overload") ||
	    !Expect(Output == "derived\nbase\n", "subtype free overload output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
	        CompileFails(
	            "class A {}\n"
	            "class B {}\n"
	            "void f(A a) {}\n"
	            "void f(B b) {}\n"
	            "void main() {\n"
	            "    f(nullptr);\n"
	            "}\n",
	            &Error),
	        "ambiguous nullptr call rejected"))
	{
		std::cerr << "expected ambiguous fail, got success\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
	        Run(
	            "T id<T>(T x) { return x; }\n"
	            "U pick<T, U>(T a, U b) { return b; }\n"
	            "void main() {\n"
	            "    print(id<int>(9));\n"
	            "    print(pick<int, string>(1, \"ok\"));\n"
	            "    print(pick<int>(2, \"partial\"));\n"
	            "    print(id(3));\n"
	            "}\n",
	            Output,
	            &Error),
	        "generic free-function type-param arity overload") ||
	    !Expect(Output == "9\nok\npartial\n3\n", "generic arity overload output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
	        CompileFails(
	            "T id<T>(T x) { return x; }\n"
	            "T id<T>(T y) { return y; }\n"
	            "void main() {}\n",
	            &Error),
	        "same generic type-param arity rejected"))
	{
		std::cerr << "expected same generic arity fail\n";
		return EXIT_FAILURE;
	}

	// Generic declared first must not shadow a later non-generic with disjoint arity.
	if (!Expect(
	        Run(
	            "T id<T>(T x) { return x; }\n"
	            "void id() { print(\"0\"); }\n"
	            "void main() {\n"
	            "    id();\n"
	            "    print(id(3));\n"
	            "}\n",
	            Output,
	            &Error),
	        "generic then non-generic free overload") ||
	    !Expect(Output == "0\n3\n", "generic/non-generic order output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
	        Run(
	            "void id() { print(\"0\"); }\n"
	            "T id<T>(T x) { return x; }\n"
	            "void main() {\n"
	            "    id();\n"
	            "    print(id(3));\n"
	            "}\n",
	            Output,
	            &Error),
	        "non-generic then generic free overload") ||
	    !Expect(Output == "0\n3\n", "non-generic/generic order output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Overlapping value arity: concrete wins without explicit TypeArgs; <T> forces generic.
	if (!Expect(
	        Run(
	            "void show(int x) { print(\"c\"); }\n"
	            "T show<T>(T x) { print(\"g\"); return x; }\n"
	            "void main() {\n"
	            "    show(1);\n"
	            "    show<int>(1);\n"
	            "}\n",
	            Output,
	            &Error),
	        "concrete preferred over overlapping generic") ||
	    !Expect(Output == "c\ng\n", "concrete-vs-generic output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	std::unique_ptr<VeroScript::FBytecodeModule> GenericModule;
	if (!Expect(Run(
	                "T build<T>(string s) { return cast<T>(1); } "
	                "U build<U>(string s, int n) { return cast<U>(n); } "
	                "void main(){print(build<int>(\"x\")); print(build<int>(\"x\",7)); print(build<int>(\"x\"));}",
	                Output, &Error, &GenericModule),
	        "generic overloads retain distinct value parameter signatures") ||
	    !Expect(Output == "1\n7\n1\n", "generic specialization cache distinguishes overloads"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	std::vector<VeroScript::FFreeFunctionInfo> GenericFunctions;
	VeroScript::ModuleMeta::EnumerateFreeFunctions(*GenericModule, GenericFunctions, false);
	std::size_t GenericCount = 0;
	for (const auto& Function : GenericFunctions)
	{
		if (Function.Name != "build")
		{
			continue;
		}
		++GenericCount;
		const std::vector<std::string> Expected = Function.ParamCount == 1
		    ? std::vector<std::string>{"string"}
		    : std::vector<std::string>{"string", "int"};
		if (!Expect(Function.ParamTypeNames == Expected && Function.ReturnTypeName == "int",
		        "generic metadata reports instantiated value parameters, not generic linkage arguments"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!Expect(GenericCount == 2, "both generic overloads are discoverable")) return EXIT_FAILURE;
	if (!Expect(!Run("T duplicate<T>(T n){return n;} U duplicate<U>(U n){return n;} void main(){}",
	                Output, &Error),
	        "renaming generic parameters cannot create a duplicate overload"))
	{
		return EXIT_FAILURE;
	}
	std::cout << "VeroScriptFreeFunctionOverloadTests passed\n";
	return EXIT_SUCCESS;
}
