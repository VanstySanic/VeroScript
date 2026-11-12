// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptJit.h"
#include "VeroScriptLexer.h"
#include "VeroScriptLocale.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"
#include "../VeroScriptHotReloadInternal.h"

#include "VeroScriptAst.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef VEROSCRIPT_EXCEPTION_PLAN_CHECK
#define VEROSCRIPT_EXCEPTION_PLAN_CHECK(Module, Kind) true
#define VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm) ((void)0)
#endif

namespace
{
	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(
	    const std::string& Source,
	    std::string& OutError);

	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "VeroScriptExceptionTests: " << Message << '\n';
		}
		return Condition;
	}

	bool ExpectOutput(const std::string_view Source, const std::string_view Expected,
	    const VeroScript::EExecutionBackend Backend, const int Mode = 0, const int ExpectedExitCode = 0)
	{
		std::ostringstream Output;
		std::string CompileError;
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
		    CompileSource(std::string(Source), CompileError);
		if (!Module)
		{
			std::cerr << CompileError << '\n';
			return false;
		}
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		Vm.SetExecutionBackend(Backend);
		VeroScript::VeroScriptDebugger Debugger;
		Debugger.SetRunMode(VeroScript::EDebugRunMode::Continue);
		if (Mode == 1)
		{
			Vm.SetDebugger(&Debugger);
		}
		if (Mode == 2)
		{
			VeroScript::FExecutionLimits Limits;
			Limits.MaxInstructions = 10000;
			Vm.SetExecutionLimits(Limits);
		}
		const bool bOk = Vm.Execute(*Module, VeroScript::ERunMode::Script);
		if (!bOk || Output.str() != Expected || Vm.GetProcessExitCode() != ExpectedExitCode)
		{
			std::cerr << "backend=" << static_cast<int>(Backend)
			          << " mode=" << Mode
			          << "\nerror:\n"
			          << Vm.GetError().Message
			          << "\nexpected:\n"
			          << Expected
			          << "actual:\n"
			          << Output.str();
			return false;
		}
		return true;
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(
	    const std::string& Source,
	    std::string& OutError)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "<exceptions>");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			OutError = "lexer failed";
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "<exceptions>");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (Parser.HasError() || !Program)
		{
			OutError = "parser failed: " + Parser.GetError().Message + " at " + std::to_string(Parser.GetError().Location.Line) + ":" + std::to_string(Parser.GetError().Location.Column);
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (Compiler.HasError() || !Module)
		{
			OutError = Compiler.GetError().Message;
			return nullptr;
		}
		return Module;
	}
}

int main()
{
	(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
	const std::string TrivialAggregateCleanupSource = R"vs(
class Base { public: int value=0; };
weak<Base> watched=weak<Base>();
bool throwCleanup=false;
class Plain:Base {};
class Trace:Base {
public:
	Trace(int id){value=id;}
	~Trace(){
		Base live=watched.lock();
		print(value*10+(live==nullptr?0:1));
		if(throwCleanup && value==5) throw 77;
	}
};
void release_mixed(bool fail){
	array<Base> values=array<Base>();
	Base shared=Plain();
	watched=weak<Base>(shared);
	values.push(Trace(3));
	values.push(shared);
	values.push(Trace(4));
	values.push(shared);
	values.push(Trace(5));
	shared=nullptr;
	if(fail) throw 9;
}
void main(){
	release_mixed(false);
	if(watched.lock()==nullptr) print(8); else print(0);
	try{release_mixed(true);}catch(int e){print(e);}
	if(watched.lock()==nullptr) print(8); else print(0);
	throwCleanup=true;
	try{release_mixed(true);}catch(int e){print(e);}
	if(watched.lock()==nullptr) print(8); else print(0);
}
)vs";
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		for (int Mode = 0; Mode < 3; ++Mode)
		{
			if (!Expect(ExpectOutput(TrivialAggregateCleanupSource,
			        "51\n41\n30\n8\n51\n41\n30\n9\n8\n51\n41\n30\n77\n8\n", Backend, Mode),
			        "aggregate cleanup preserves aliases, weak expiration, reverse destructor order and replacement exceptions"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	const std::string Source =
	    "class Trace { public: ~Trace() { print(\"trace-destroy\"); } };\n"
	    "class GenericCtor<T> { public: T value; GenericCtor() { try { throw 1; } catch (int) {} } };\n"
	    "void fail() { throw \"boom\"; }\n"
	    "void bridge_fail() { fail(); }\n"
	    "void fail_owned() { Trace trace(); throw \"owned\"; }\n"
	    "int with_return() {\n"
	    "    try { return 5; } finally { print(\"return-finally\"); }\n"
	    "}\n"
	    "void main() {\n"
	    "    try { throw 6; } catch (int) { print(6); }\n"
	    "    try { throw 7; } catch (int value) { print(value); }\n"
	    "    try { throw 8; } catch (string ignored) { print(0); } catch (...) { print(8); }\n"
	    "    try { throw 11; } catch (const int& value) { print(value); } finally { print(12); }\n"
	    "    try { fail_owned(); } catch (string message) { print(message); }\n"
	    "    try { bridge_fail(); } catch (string message) { print(message); }\n"
	    "    try { try { throw 20; } catch (int value) { value = 21; throw; } }\n"
	    "    catch (int value) { print(value); }\n"
	    "    try { try { throw 22; } catch (int& value) { value = 23; throw; } }\n"
	    "    catch (int value) { print(value); }\n"
	    "    try { try { throw 24; } catch (ref int value) { value = 25; throw; } }\n"
	    "    catch (int value) { print(value); }\n"
	    "    try { try { throw 26; } catch (int& value) { GenericCtor<int> item(); throw; } }\n"
	    "    catch (int value) { print(value); }\n"
	    "    try {\n"
	    "        try { fail(); } catch (int ignored) { print(0); } finally { print(\"inner-finally\"); }\n"
	    "    } catch (string message) { print(message); }\n"
	    "    try {\n"
	    "        try { throw 9; } catch (int value) { print(value); throw; }\n"
	    "    } catch (int value) { print(value + 1); }\n"
	    "    int i = 0;\n"
	    "    while (i < 3) {\n"
	    "        try {\n"
	    "            i = i + 1;\n"
	    "            if (i == 2) { continue; }\n"
	    "            if (i == 3) { break; }\n"
	    "        } finally { print(i); }\n"
	    "    }\n"
	    "    print(with_return());\n"
	    "}\n";
	const std::string Expected =
	    "6\n7\n8\n11\n12\ntrace-destroy\nowned\nboom\n20\n23\n25\n26\ninner-finally\nboom\n9\n10\n1\n2\n3\nreturn-finally\n5\n";
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		if (!Expect(ExpectOutput(Source, Expected, Backend), "exception execution mismatch"))
		{
			return EXIT_FAILURE;
		}
	}

	const std::string ReentrantFinallySource =
	    "void deep(int n) { if (n > 0) { deep(n - 1); } }\n"
	    "class Trace { public: ~Trace() { deep(100); "
	    "try { throw 66; } catch (int value) { if (value != 66) { print(0); } } print(77); } };\n"
	    "Trace pending_return() { try { return Trace(); } finally { throw 9; } }\n"
	    "void pending_throw() { try { throw Trace(); } finally { throw 10; } }\n"
	    "void indirect_throw() { throw 11; }\n"
	    "Trace pending_indirect() { try { return Trace(); } finally { indirect_throw(); } }\n"
	    "void main() {\n"
	    "    try { pending_return(); } catch (int value) { print(value); }\n"
	    "    try { pending_throw(); } catch (int value) { print(value); }\n"
	    "    try { pending_indirect(); } catch (int value) { print(value); }\n"
	    "}\n";
	const std::string CleanupThrowSource =
	    "void deep(int n) { if (n > 0) { deep(n - 1); } }\n"
	    "class Old { public: ~Old() { deep(100); "
	    "try { throw 77; } catch (int) {} print(78); } };\n"
	    "class Bad { public: ~Bad() { throw 66; } };\n"
	    "Bad replaced() { try { return Bad(); } finally { throw 9; } }\n"
	    "void fail() { Bad local(); throw Old(); }\n"
	    "void main() {\n"
	    "    try { replaced(); } catch (int value) { print(value); }\n"
	    "    try { fail(); } catch (int value) { print(value); } catch (...) { print(0); }\n"
	    "}\n";
	const std::string RefReplacementSource =
	    "void deep(int n) { if (n > 0) { deep(n - 1); } } "
	    "class Trace { public: ~Trace() { deep(100); print(78); } }; "
	    "void replace(ref Trace value) { value = Trace(); } "
	    "void main() { Trace value(); replace(ref value); print(79); }";
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		for (int Mode = 0; Mode < 3; ++Mode)
		{
			if (!Expect(ExpectOutput("class Trace { public: ~Trace(){print(77);exit(7);} }; "
			                         "void fail(){Trace value=Trace();throw 9;} "
			                         "void main(){try{fail();}catch(int e){print(e);} print(99);}",
			                "77\n", Backend, Mode, 7),
			        "exit during exception cleanup stops handler resolution and preserves the exit code"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(ExpectOutput("class Trace { public: ~Trace(){print(77);exit(7);} }; "
			                         "void fail(){Trace value=Trace();throw 9;} "
			                         "void main(){try{fail();}finally{print(99);}}",
			                "77\n", Backend, Mode, 7),
			        "exit during exception cleanup does not resume a removed finally frame"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(ExpectOutput("class Trace { public: ~Trace(){print(77);exit(7);} }; "
			                         "class Owner { public: Trace value=Trace(); int y=([]()->int{throw 9;})(); }; "
			                         "void main(){try{Owner value=Owner();}catch(int e){print(e);}}",
			                "77\n", Backend, Mode, 7),
			        "exit during incomplete-object cleanup supersedes its script exception"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(
			        ExpectOutput("void deep(int n) { if (n>0) deep(n-1); } "
			                     "class First { public: ~First(){deep(25);try{throw 66;}catch(int){} print(1);} }; "
			                     "class Second { public: ~Second(){deep(25);try{throw 66;}catch(int){} print(2);} }; "
			                     "class Owner { public: First a=First(); Second b=Second(); "
			                     "int y=([]()->int{throw 9;})(); ~Owner(){print(99);} }; "
			                     "void main(){try{Owner o=Owner();}catch(int e){print(e);} }",
			            "2\n1\n9\n", Backend, Mode),
			        "failed field initialization releases completed fields and preserves the exception"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(ExpectOutput("class First { public: ~First(){try{throw 77;}catch(int){} print(1);} }; "
			                         "class Second { public: ~Second(){print(2);throw 66;} }; "
			                         "class Owner { public: First a=First(); Second b=Second(); "
			                         "int y=([]()->int{throw 9;})(); ~Owner(){print(99);} }; "
			                         "void main(){try{Owner o=Owner();}catch(int e){print(e);} }",
			                "2\n1\n66\n", Backend, Mode),
			        "later completed-field cleanup preserves an uncaught destructor exception"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(ExpectOutput(RefReplacementSource, "78\n79\n78\n", Backend, Mode),
			        "ref replacement must reacquire frame storage after reentrant destruction"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(ExpectOutput(ReentrantFinallySource, "77\n9\n77\n10\n77\n11\n", Backend, Mode),
			        "reentrant destructors must safely replace parked finally values"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(ExpectOutput(CleanupThrowSource, "66\n78\n66\n", Backend, Mode),
			        "uncaught cleanup exceptions must reach outer handlers without being overwritten"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(ExpectOutput("class Error { public: ~Error(){print(3);exit(7);} }; "
			                         "class Thrower { public: ~Thrower(){print(2);throw 66;} }; "
			                         "void main(){Thrower value=Thrower();throw Error();}",
			                "2\n3\n", Backend, Mode, 7),
			        "exit from a superseded exception object's destructor completes cleanup successfully"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	const std::string NestedFinallySource =
	    "void fail_nested() { throw 12; }\n"
	    "int pending() {\n"
	    "    try { return 5; } finally {\n"
	    "        if (false) { print(0); }\n"
	    "        int count = 0; while (count < 2) { print(count); count = count + 1; }\n"
	    "        try { throw 9; } catch (int value) { print(value); }\n"
	    "        try { fail_nested(); } catch (int value) { print(value); }\n"
	    "        try { print(13); } finally { if (false) { print(0); } print(14); }\n"
	    "        try { throw 15; } finally { print(16); }\n"
	    "    }\n"
	    "}\n"
	    "int preserved() {\n"
	    "    try { return 6; } finally {\n"
	    "        if (false) { print(0); }\n"
	    "        try { try { throw 17; } finally { print(18); } }\n"
	    "        catch (int value) { print(value); }\n"
	    "    }\n"
	    "}\n"
	    "void preserved_throw() {\n"
	    "    try { throw 19; } finally {\n"
	    "        try { throw 20; } catch (int value) { print(value); }\n"
	    "    }\n"
	    "}\n"
	    "void main() {\n"
	    "    try { print(pending()); } catch (int value) { print(value); }\n"
	    "    print(preserved());\n"
	    "    try { preserved_throw(); } catch (int value) { print(value); }\n"
	    "}\n";
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		for (int Mode = 0; Mode < 3; ++Mode)
		{
			if (!Expect(ExpectOutput(NestedFinallySource, "0\n1\n9\n12\n13\n14\n16\n15\n18\n17\n6\n20\n19\n", Backend, Mode),
			        "control flow inside finally must preserve enclosing transfers"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	std::string ReentrantFatalError;
	std::unique_ptr<VeroScript::FBytecodeModule> ReentrantFatalModule = CompileSource(
	    "void deep(int n) { if (n > 0) { deep(n - 1); } } "
	    "class Trace { public: ~Trace() { deep(100); "
	    "try { throw 66; } catch (int) {} print(77); } }; "
	    "Trace fatal() { try { return Trace(); } finally { "
	    "try { int zero = 0; print(1 / zero); } finally { print(88); } } } "
	    "void main() { fatal(); }",
	    ReentrantFatalError);
	if (!Expect(static_cast<bool>(ReentrantFatalModule), "compile reentrant fatal-finally fixture"))
	{
		std::cerr << ReentrantFatalError << '\n';
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		std::ostringstream FatalOutput;
		VeroScript::VeroScriptVm FatalVm(FatalOutput);
		VeroScript::Test::Configure(FatalVm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(FatalVm);
		FatalVm.SetExecutionBackend(Backend);
		if (!Expect(!FatalVm.Execute(*ReentrantFatalModule, VeroScript::ERunMode::Script) && FatalOutput.str() == "88\n77\n" && FatalVm.GetError().Message.find("division by zero") != std::string::npos,
		        "nested fatal finally must release parked returns and retain the original failure"))
		{
			std::cerr << FatalVm.GetError().Message << '\n'
			          << FatalOutput.str();
			return EXIT_FAILURE;
		}
	}
	std::string UncaughtCleanupError;
	const auto UncaughtCleanupModule = CompileSource(
	    "void deep(int n) { if (n > 0) { deep(n - 1); } } "
	    "class Old { public: ~Old() { deep(100); "
	    "try { throw 77; } catch (int) {} print(78); } }; "
	    "class Bad { public: ~Bad() { throw 66; } }; "
	    "void main() { Bad local(); throw Old(); }",
	    UncaughtCleanupError);
	const auto RecoveryModule = CompileSource("void main() { print(79); }", UncaughtCleanupError);
	const auto UncaughtObjectModule = CompileSource(
	    "void deep(int n) { if (n > 0) { deep(n - 1); } } "
	    "class Old { public: ~Old() { deep(100); "
	    "try { throw 77; } catch (int) {} print(78); } }; "
	    "void main() { throw Old(); }",
	    UncaughtCleanupError);
	const auto GlobalCleanupModule = CompileSource(
	    "class Old { public: ~Old() { print(78); } }; "
	    "class Bad { public: ~Bad() { throw Old(); } }; "
	    "Bad global(); void main() { print(1); }",
	    UncaughtCleanupError);
	const auto NestedCleanupModule = CompileSource(
	    "class Inner { public: ~Inner() { try { throw 77; } catch (int) {} print(78); } }; "
	    "class Outer { public: ~Outer() { print(80); throw Inner(); } }; "
	    "void main() { throw Outer(); }",
	    UncaughtCleanupError);
	if (!Expect(UncaughtCleanupModule && RecoveryModule && UncaughtObjectModule && GlobalCleanupModule && NestedCleanupModule,
	        "compile uncaught cleanup/recovery fixtures"))
	{
		std::cerr << UncaughtCleanupError << '\n';
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		for (int EntryMode = 0; EntryMode < 2; ++EntryMode)
		{
			for (const auto& Fixture : {
			         std::pair{UncaughtObjectModule.get(), std::string{"78\n"}},
			         std::pair{GlobalCleanupModule.get(), std::string{"1\n78\n"}},
			         std::pair{NestedCleanupModule.get(), std::string{"80\n78\n"}}})
			{
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
				Vm.SetExecutionBackend(Backend);
				const bool bOk = EntryMode == 0
				    ? Vm.Execute(*Fixture.first, VeroScript::ERunMode::Script)
				    : Vm.ExecuteFunction(*Fixture.first,
				          static_cast<std::uint16_t>(Fixture.first->MainFunctionIndex), {});
				if (!Expect(!bOk && Vm.HasError() && Output.str() == Fixture.second,
				        "final teardown must isolate and release all uncaught exception objects") ||
				    !Expect(Vm.Execute(*RecoveryModule, VeroScript::ERunMode::Script) && !Vm.HasError() && Output.str() == Fixture.second + "79\n",
				        "VM must recover after uncaught object/global/nested cleanup exceptions"))
				{
					return EXIT_FAILURE;
				}
			}
		}
		std::ostringstream CleanupOutput;
		VeroScript::VeroScriptVm CleanupVm(CleanupOutput);
		VeroScript::Test::Configure(CleanupVm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(CleanupVm);
		CleanupVm.SetExecutionBackend(Backend);
		if (!Expect(!CleanupVm.Execute(*UncaughtCleanupModule, VeroScript::ERunMode::Script) && CleanupVm.HasError() && CleanupOutput.str() == "78\n",
		        "uncaught teardown must release the previous exception from stable storage") ||
		    !Expect(CleanupVm.Execute(*RecoveryModule, VeroScript::ERunMode::Script) && !CleanupVm.HasError() && CleanupOutput.str() == "78\n79\n",
		        "VM must recover after an uncaught cleanup exception"))
		{
			return EXIT_FAILURE;
		}
	}
	std::string RefCleanupError;
	const auto RefCleanupModule = CompileSource(
	    "void deep(int n) { if (n > 0) { deep(n - 1); } } "
	    "struct Trace { public: int value = 1; ~Trace() { deep(100); print(78); } }; "
	    "void fail(ref Trace left, ref Trace right) { throw 9; } "
	    "void noop(ref Trace left, ref Trace right) {} "
	    "function(ref Trace, ref Trace)->void get_fail() { return fail; } "
	    "function(ref Trace, ref Trace)->void get_noop() { return noop; } "
	    "void main() {}",
	    RefCleanupError);
	if (!Expect(static_cast<bool>(RefCleanupModule), "compile ref cleanup fixture"))
	{
		return EXIT_FAILURE;
	}
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		for (const bool bCallable : {false, true})
		{
			for (const bool bFail : {false, true})
			{
				std::ostringstream Output;
				VeroScript::VeroScriptVm FactoryVm(Output);
				VeroScript::Test::Configure(FactoryVm);
				VeroScript::FObjectHandle Left;
				VeroScript::FObjectHandle Right;
				if (!Expect(FactoryVm.ConstructObject(*RefCleanupModule, "Trace", {}, Left, nullptr) && FactoryVm.ConstructObject(*RefCleanupModule, "Trace", {}, Right, nullptr),
				        "construct ref entry fixture"))
				{
					return EXIT_FAILURE;
				}
				VeroScript::FValue CallableValue;
				if (!Expect(FactoryVm.ExecuteFreeFunction(*RefCleanupModule,
				                bFail ? "get_fail" : "get_noop", {}, nullptr, nullptr, &CallableValue),
				        "obtain ref entry callable"))
				{
					return EXIT_FAILURE;
				}
				const auto Callable = std::get<VeroScript::FFunctionHandle>(CallableValue);
				Output.str("");
				VeroScript::VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
				Vm.SetExecutionBackend(Backend);
				Vm.SetProgramGeneration(Callable->ProgramGeneration);
				Vm.SetProgramOwner(Callable->ProgramOwner);
				std::vector<VeroScript::FValue> Args{std::move(Left), std::move(Right)};
				const auto* const OriginalLeft = std::get<VeroScript::FObjectHandle>(Args[0]).get();
				const auto* const OriginalRight = std::get<VeroScript::FObjectHandle>(Args[1]).get();
				const std::string Name = bFail ? "fail" : "noop";
				const bool bOk = bCallable
				    ? Vm.ExecuteCallable(*RefCleanupModule, Callable, Args, nullptr,
				          nullptr, nullptr, nullptr, &Args)
				    : Vm.ExecuteFreeFunction(*RefCleanupModule, Name, Args, nullptr,
				          nullptr, nullptr, nullptr, &Args);
				if (!Expect(bOk == !bFail && Vm.HasError() == bFail && Output.str() == "78\n78\n" && (!bFail || (std::get<VeroScript::FObjectHandle>(Args[0]).get() == OriginalLeft && std::get<VeroScript::FObjectHandle>(Args[1]).get() == OriginalRight)),
				        "ref entry cleanup/write-back must survive reentrant destructors") ||
				    !Expect(Vm.Execute(*RecoveryModule, VeroScript::ERunMode::Script) && !Vm.HasError() && Output.str() == "78\n78\n79\n",
				        "VM must recover after ref entry cleanup/write-back"))
				{
					std::cerr << "backend=" << static_cast<int>(Backend)
					          << " callable=" << bCallable << " fail=" << bFail
					          << " success=" << bOk << " type=" << Callable->TypeName << '\n';
					std::cerr << Vm.GetError().Message << '\n'
					          << Output.str();
					return EXIT_FAILURE;
				}
			}
		}
	}
	std::string CompileError;
	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source, CompileError);
	if (!Expect(static_cast<bool>(Module), "failed to compile serialization source"))
	{
		std::cerr << CompileError << '\n';
		return EXIT_FAILURE;
	}
	std::vector<std::uint8_t> Bytes;
	std::string Error;
	if (!Expect(
	        VeroScript::VeroScriptBytecodeIO::SerializeBytecode(*Module, 77, Bytes, Error),
	        "failed to serialize exception regions"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	std::unique_ptr<VeroScript::FBytecodeModule> Loaded =
	    VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(
	        VeroScript::FByteView(Bytes), 77, Error);
	if (!Expect(static_cast<bool>(Loaded), "failed to deserialize exception regions"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	std::ostringstream LoadedOutput;
	VeroScript::VeroScriptVm Vm(LoadedOutput);
	VeroScript::Test::Configure(Vm);
	if (!Expect(Vm.Execute(*Loaded, VeroScript::ERunMode::Script), "loaded module failed"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(LoadedOutput.str() == Expected, "loaded module output mismatch"))
	{
		return EXIT_FAILURE;
	}

	bool bValidatedCorruptRegion = false;
	for (VeroScript::FBytecodeFunction& Function : Module->Functions)
	{
		for (VeroScript::FBytecodeExceptionRegion& Region : Function.ExceptionRegions)
		{
			if (Region.Catches.empty())
			{
				continue;
			}
			const std::uint32_t SavedHandlerOffset = Region.Catches.front().HandlerOffset;
			Region.Catches.front().HandlerOffset = Region.BeginOffset;
			std::vector<std::uint8_t> InvalidBytes;
			std::string InvalidRegionError;
			const bool bSerialized = VeroScript::VeroScriptBytecodeIO::SerializeBytecode(
			    *Module, 77, InvalidBytes, InvalidRegionError);
			Region.Catches.front().HandlerOffset = SavedHandlerOffset;
			if (!Expect(!bSerialized,
			        "serializer must reject a catch target inside its protected range"))
			{
				return EXIT_FAILURE;
			}
			bValidatedCorruptRegion = true;
			break;
		}
		if (bValidatedCorruptRegion)
		{
			break;
		}
	}
	if (!Expect(bValidatedCorruptRegion, "test module must contain a typed handler"))
	{
		return EXIT_FAILURE;
	}

	if (!VEROSCRIPT_EXCEPTION_PLAN_CHECK(*Module, "safe")) return EXIT_FAILURE;

	const std::string CatchLoopSource =
	    "int work(int limit) { int total = 0; "
	    "for (int i = 0; i < limit; i++) { "
	    "try { throw i; } catch (int value) { total += value; } } "
	    "return total; } void main() { print(work(1000)); }";
	for (const VeroScript::EExecutionBackend Backend : {
	         VeroScript::EExecutionBackend::Vm,
	         VeroScript::EExecutionBackend::Mixed})
	{
		if (!Expect(
		        ExpectOutput(CatchLoopSource, "499500\n", Backend),
		        "typed catch accumulator loop parity"))
		{
			return EXIT_FAILURE;
		}
	}
	std::unique_ptr<VeroScript::FBytecodeModule> CatchLoopModule =
	    CompileSource(CatchLoopSource, CompileError);
	if (!Expect(static_cast<bool>(CatchLoopModule), "compile typed catch accumulator loop") || !VEROSCRIPT_EXCEPTION_PLAN_CHECK(*CatchLoopModule, "loop"))
	{
		return EXIT_FAILURE;
	}

	std::unique_ptr<VeroScript::FBytecodeModule> GeneralCatchModule = CompileSource(
	    "int work() { try { throw 7; } catch (int value) { return value; } "
	    "return 0; } void main() { print(work()); }",
	    CompileError);
	if (!Expect(static_cast<bool>(GeneralCatchModule), "compile general catch fallback"))
	{
		return EXIT_FAILURE;
	}
	if (!VEROSCRIPT_EXCEPTION_PLAN_CHECK(*GeneralCatchModule, "general")) return EXIT_FAILURE;

	std::string InvalidError;
	std::unique_ptr<VeroScript::FBytecodeModule> InvalidRethrow =
	    CompileSource("void main() { throw; }", InvalidError);
	if (!Expect(!InvalidRethrow, "bare throw outside catch must fail"))
	{
		return EXIT_FAILURE;
	}
	std::string InvalidRefThrowError;
	std::unique_ptr<VeroScript::FBytecodeModule> InvalidRefThrow = CompileSource(
	    "void main() { int value = 1; throw ref value; }",
	    InvalidRefThrowError);
	if (!Expect(
	        !InvalidRefThrow,
	        "throwing a ref binding must be rejected before it reaches the VM"))
	{
		std::cerr << InvalidRefThrowError << '\n';
		return EXIT_FAILURE;
	}
	std::string InvalidCtorRethrowError;
	std::unique_ptr<VeroScript::FBytecodeModule> InvalidCtorRethrow = CompileSource(
	    "class Invalid<T> { public: T value; Invalid() { throw; } }; "
	    "void main() { try { throw 1; } catch (int) { Invalid<int> value(); } }",
	    InvalidCtorRethrowError);
	if (!Expect(
	        !InvalidCtorRethrow && InvalidCtorRethrowError.find("bare throw") != std::string::npos,
	        "a lazily instantiated constructor must not inherit its caller's catch context"))
	{
		return EXIT_FAILURE;
	}
	for (const std::string_view InvalidSource : {
	         "int f() { try { return 1; } finally { return 2; } }",
	         "void main() { while (true) { try {} finally { break; } } }",
	         "void main() { try {} finally { goto done; done: print(1); } }",
	         "void main() { try { throw 1; } catch (int value) { goto sibling; } "
	         "catch (string text) { sibling: print(text); } }"})
	{
		std::string InvalidControlError;
		if (!Expect(!CompileSource(std::string(InvalidSource), InvalidControlError),
		        "control flow must not escape or cross exception-handler scopes"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed})
	{
		for (const bool bExitDuringCleanup : {false, true})
		{
			std::string PartialCompileError;
			const std::string PartialSource =
			    std::string(bExitDuringCleanup ? "class First { public: ~First(){print(1);exit(7);} }; class Second { "
			                                     "public: ~Second(){print(2);throw 66;} }; "
			                                   : "class First { public: ~First(){try{throw 77;}catch(int){} print(1);} "
			                                     "}; class Second { public: ~Second(){print(2);} }; ") +
			    "class Owner { public: First a=First(); Second b=Second(); int y=1/0; ~Owner(){print(99);} }; void "
			    "main(){Owner o=Owner();}";
			auto PartialModule = CompileSource(PartialSource, PartialCompileError);
			std::ostringstream Output;
			VeroScript::VeroScriptVm PartialVm(Output);
			VeroScript::Test::Configure(PartialVm);
			VEROSCRIPT_EXCEPTION_BACKEND_SETUP(PartialVm);
			PartialVm.SetExecutionBackend(Backend);
			const bool bOk = PartialModule && PartialVm.Execute(*PartialModule, VeroScript::ERunMode::Script);
			if (!Expect(!bOk && Output.str() == "2\n1\n" && PartialVm.GetError().MessageId == "diag.division_by_zero",
			        "runtime failure during field initialization releases completed fields and retains the "
			        "original error"))
			{
				std::cerr << PartialCompileError << '\n'
				          << PartialVm.GetError().Message << '\n'
				          << Output.str();
				return EXIT_FAILURE;
			}
		}
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed})
	{
		for (int ScopeKind = 0; ScopeKind < 3; ++ScopeKind)
		{
			for (int CleanupMode = 0; CleanupMode < 4; ++CleanupMode)
			{
				const std::string FirstCleanup = CleanupMode == 0 ? "try{throw 77;}catch(int){} print(1);"
				    : CleanupMode == 1                            ? "print(1);throw 88;"
				    : CleanupMode == 2                            ? "print(1);exit(7);"
				                                                  : "print(1);int zero=0;print(1/zero);";
				const std::string ReplacementDeclarations = "class First { public: ~First(){" + FirstCleanup +
				    "} };\n"
				    "class Second { public: ~Second(){print(2);throw 66;} };\n";
				const std::string ReplacementSource =
				    ReplacementDeclarations +
				    (ScopeKind == 0          ? "class Owner { public: First a=First(); Second b=Second();\n"
				                               "int y=([]()->int{throw 9;})(); };\nvoid main(){Owner value=Owner();}\n"
				            : ScopeKind == 1 ? "First a=First(); Second b=Second();\nvoid main(){throw 9;}\n"
				                             : "class Owner { public: First a=First(); Second b=Second(); };\n"
				                               "void main(){Owner value=Owner();throw 9;}\n");
				std::string ReplacementCompileError;
				auto ReplacementModule = CompileSource(ReplacementSource, ReplacementCompileError);
				std::ostringstream ReplacementOutput;
				VeroScript::VeroScriptVm ReplacementVm(ReplacementOutput);
				VeroScript::Test::Configure(ReplacementVm);
				VEROSCRIPT_EXCEPTION_BACKEND_SETUP(ReplacementVm);
				ReplacementVm.SetExecutionBackend(Backend);
				const bool bReplacementOk =
				    ReplacementModule && ReplacementVm.Execute(*ReplacementModule, VeroScript::ERunMode::Script);
				const auto& ReplacementError = ReplacementVm.GetError();
				const bool bExpectedResult =
				    CleanupMode == 2
				    ? bReplacementOk && ReplacementVm.GetProcessExitCode() == 7 && !ReplacementVm.HasError()
				    : !bReplacementOk && ReplacementError.Location.Line == (CleanupMode == 0 ? 2u : 1u) &&
				        (CleanupMode == 3 ? ReplacementError.MessageId == "diag.division_by_zero"
				                          : ReplacementError.bUncaughtScriptException);
				if (!Expect(ReplacementModule && ReplacementOutput.str() == "2\n1\n" && bExpectedResult,
				        "cleanup diagnostics follow replacement throws, exit, and runtime faults"))
				{
					std::cerr << "backend=" << static_cast<int>(Backend) << " scope=" << ScopeKind
					          << " cleanup=" << CleanupMode << '\n'
					          << ReplacementCompileError << '\n'
					          << ReplacementError.Message << '\n'
					          << ReplacementError.Location.Line << '\n'
					          << ReplacementOutput.str();
					return EXIT_FAILURE;
				}
			}
		}
	}
	for (const bool bCallableCleanup : {false, true})
	{
		for (const int ExceptionCleanup : {0, 1, 2})
		{
			std::string CleanupCompileError;
			const std::string ExceptionDestructor = ExceptionCleanup == 1 ? "exit(7);"
			    : ExceptionCleanup == 2                                   ? "int zero=0;print(1/zero);"
			                                                              : "";
			auto CleanupModule = CompileSource(
			    "class CleanupError { public: ~CleanupError(){print(3);" + ExceptionDestructor +
			        "} }; "
			        "class Broken { public: ~Broken(){print(2);throw CleanupError();} }; " +
			        (bCallableCleanup
			                ? "fn()->void make(){Broken value=Broken();return [value]()->void{if(value==nullptr)return;};}"
			                  "void main(){}"
			                : "Broken value=Broken();void main(){}"),
			    CleanupCompileError);
			std::ostringstream CleanupOutput;
			VeroScript::VeroScriptVm CleanupVm(CleanupOutput);
			VeroScript::Test::Configure(CleanupVm);
			VEROSCRIPT_EXCEPTION_BACKEND_SETUP(CleanupVm);
			VeroScript::FHotReloadState CleanupState;
			bool bInitialized = false;
			bool bCleanupOk = false;
			if (CleanupModule && bCallableCleanup)
			{
				VeroScript::FValue Returned;
				bInitialized = CleanupVm.ExecuteFreeFunction(*CleanupModule, "make", {}, nullptr, nullptr, &Returned);
				auto* Callable = std::get_if<VeroScript::FFunctionHandle>(&Returned);
				if (bInitialized && Callable && *Callable)
				{
					std::vector<VeroScript::FFunctionHandle> Callables;
					Callables.push_back(std::move(*Callable));
					Returned = std::int32_t{0};
					bCleanupOk = CleanupVm.DiscardRetainedCallables(*CleanupModule, Callables);
				}
				else
				{
					bInitialized = false;
				}
			}
			else if (CleanupModule)
			{
				bInitialized =
				    CleanupVm.ExecutePersistent(*CleanupModule, VeroScript::ERunMode::Script, CleanupState, nullptr);
				bCleanupOk = CleanupVm.DiscardPersistentState(*CleanupModule, CleanupState);
			}
			const bool bExpectedResult =
			    ExceptionCleanup == 1
			    ? bCleanupOk && !CleanupVm.HasError()
			    : !bCleanupOk && (ExceptionCleanup == 2 ? CleanupVm.GetError().MessageId == "diag.division_by_zero" : CleanupVm.GetError().bUncaughtScriptException);
			if (!Expect(bInitialized && bExpectedResult && CleanupOutput.str() == "2\n3\n",
			        "standalone state and callable cleanup drain exception objects, preserving exit and runtime "
			        "faults"))
			{
				std::cerr << "callable=" << bCallableCleanup << " exception cleanup=" << ExceptionCleanup << '\n'
				          << CleanupCompileError << '\n'
				          << CleanupVm.GetError().Message << '\n'
				          << CleanupOutput.str();
				return EXIT_FAILURE;
			}
		}
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed})
	{
		std::string ReturnCleanupCompileError;
		auto ReturnCleanupModule =
		    CompileSource("class Returned { public: ~Returned(){print(3);int zero=0;print(1/zero);} }; "
		                  "class Local { public: ~Local(){print(2);throw 66;} }; "
		                  "Returned work(){Local value=Local();return Returned();} "
		                  "void main(){try{work();}catch(...){print(99);}finally{print(88);}}",
		        ReturnCleanupCompileError);
		std::ostringstream ReturnCleanupOutput;
		VeroScript::VeroScriptVm ReturnCleanupVm(ReturnCleanupOutput);
		VeroScript::Test::Configure(ReturnCleanupVm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(ReturnCleanupVm);
		ReturnCleanupVm.SetExecutionBackend(Backend);
		if (!Expect(ReturnCleanupModule &&
		            !ReturnCleanupVm.Execute(*ReturnCleanupModule, VeroScript::ERunMode::Script) &&
		            ReturnCleanupOutput.str() == "2\n3\n88\n" &&
		            ReturnCleanupVm.GetError().MessageId == "diag.division_by_zero",
		        "a returned object's cleanup fault supersedes a provisional local-destructor exception"))
		{
			std::cerr << ReturnCleanupCompileError << '\n'
			          << ReturnCleanupVm.GetError().Message << '\n'
			          << ReturnCleanupOutput.str();
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Definitions =
		    "int count=0;"
		    "class Item{public:int id;Item(int value){id=value;}~Item(){print(id);}};"
		    "Item retained=Item(10);";
		std::string Error;
		std::shared_ptr<const VeroScript::FBytecodeModule> Original =
		    CompileSource(Definitions + "void main(){retained=Item(++count);print(100+count);}", Error);
		std::shared_ptr<const VeroScript::FBytecodeModule> Replacement =
		    CompileSource("int inserted=90;" + Definitions +
		            "void main(){retained=Item(++count);print(200+count);}",
		        Error);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		if (!Expect(Original && Replacement && Vm.SetJitProgramOwner(Original, Error) &&
		            Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State),
		        "owned persistent state captures an initialized generation"))
		{
			return EXIT_FAILURE;
		}
		const auto* FirstSlots = State.Globals.data();
		if (!Expect(Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) &&
		            State.Globals.data() == FirstSlots && Output.str() == "10\n101\n1\n102\n",
		        "warm execution retains storage without adding an owner to replaced globals"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error, nullptr, 2) &&
		            Vm.SetJitProgramOwner(Replacement, Error),
		        "owned persistent state stages a replacement slot layout"))
		{
			return EXIT_FAILURE;
		}
		Vm.SetProgramGeneration(2);
		if (!Expect(Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback),
		        "owned persistent state installs the replacement generation"))
		{
			return EXIT_FAILURE;
		}
		const auto* ReplacementSlots = State.Globals.data();
		if (!Expect(Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State) &&
		            State.Globals.data() == ReplacementSlots && Output.str() == "10\n101\n1\n102\n2\n203\n3\n204\n" &&
		            Vm.DiscardPersistentState(*Replacement, State),
		        "warm execution reuses replacement slots after initializing the new layout"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Definitions =
		    "int shared=10;int next(){static int count=0;return ++count;}"
		    "int lazy(){static int count=7;return ++count;}"
		    "class Cycle{public:Cycle link;~Cycle(){print(77);}};";
		const std::string Body =
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}"
		    "~Item(){if(id==2){print(next());shared+=5;print(shared);print(lazy());transient();}}};"
		    "class Root{public:Item item;Root(){item=Item(1);}};Root root=Root();"
		    "void main(){print(next());print(shared);if(shared>10){print(lazy());}}";
		std::string CompileError;
		auto Original = CompileSource(Definitions + "void transient(){}" + Body, CompileError);
		auto Replacement = CompileSource("int inserted=777;" + Definitions + "void transient(){static Cycle value=Cycle();value.link=value;}" + Body + "int fail(){root.item=Item(2);throw 7;}int broken=fail();", CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error) && !Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback) && Vm.RollbackHotReloadState(*Replacement, State, Rollback) && !Vm.RequiresReset() && !State.bExecutionResetRequired && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && Output.str() == "1\n10\n2\n15\n8\n77\n3\n15\n9\n",
		        "rollback destructors use restored globals and statics across moved slots, retaining only old declarations"))
		{
			std::cerr << CompileError << '\n'
			          << Error << '\n'
			          << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Vm.DiscardPersistentState(*Original, State), "rollback callback state remains reusable"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Definitions =
		    "class Cycle{public:Cycle link;~Cycle(){print(77);}};"
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}"
		    "~Item(){if(id==2){print(root.value);root.value+=1;makeTemporary();}}};";
		const std::string OriginalRoot =
		    "class Root{public:int value;Item item;Root(){value=42;item=Item(1);}};";
		const std::string ReplacementRoot =
		    "class Root{public:int inserted;int value;Item item;Cycle temporary;"
		    "Root(){inserted=777;value=42;item=Item(1);}};";
		const std::string Tail = "Root root=Root();void main(){print(root.value);}";
		std::string CompileError;
		auto Original = CompileSource(Definitions + OriginalRoot + "void makeTemporary(){}" + Tail, CompileError);
		auto Replacement = CompileSource(Definitions + ReplacementRoot + "void makeTemporary(){root.temporary=Cycle();root.temporary.link=root.temporary;}" + Tail + "int fail(){root.value=999;root.item=Item(2);throw 7;}int broken=fail();", CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && State.Globals.size() == 1, "rollback layout tests initialize their retained object"))
		{
			std::cerr << CompileError << '\n'
			          << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		const auto* RootIdentity = std::get<VeroScript::FObjectHandle>(State.Globals[0]).get();
		if (!Expect(VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error) && !Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback) && Vm.RollbackHotReloadState(*Replacement, State, Rollback) && std::get<VeroScript::FObjectHandle>(State.Globals[0]).get() == RootIdentity && RootIdentity->Fields.size() == 2 && std::get<std::int32_t>(RootIdentity->Fields[0]) == 43 && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && Output.str() == "42\n42\n77\n43\n",
		        "rollback destructors read and write restored fields through replacement slots without retaining new cycles"))
		{
			std::cerr << Error << '\n'
			          << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Vm.DiscardPersistentState(*Original, State), "rollback object layout returns to the old schema"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const int CallableCase : {0, 1, 2})
	{
		const std::string OriginalCallback = CallableCase == 0
		    ? "int get(){return 42;}fn()->int callback=get;"
		    : CallableCase == 1
		    ? "fn()->int make(){return [left=1,right=10]()mutable->int{left+=1;right+=10;return left*100+right;};}"
		      "fn()->int callback=make();"
		    : "int calls=0;fn()->int make(){int left=1;int right=10;"
		      "return [left,right]()->int{calls+=1;return left*100+right+calls;};}"
		      "fn()->int callback=make();";
		const std::string ReplacementCallback = CallableCase == 2
		    ? "int calls=0;fn()->int make(){int left=1;int right=10;"
		      "return [right,left]()->int{calls+=1;return left*100+right+calls;};}"
		      "fn()->int callback=make();"
		    : OriginalCallback;
		const std::string Body =
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}"
		    "~Item(){if(id==2){print(callback());}}};"
		    "class Root{public:Item item;Root(){item=Item(1);}};Root root=Root();"
		    "void main(){print(callback());}";
		std::string CompileError;
		auto Original = CompileSource(OriginalCallback + Body, CompileError);
		auto Replacement = CompileSource("int inserted(){return 99;}" + ReplacementCallback + Body + "int fail(){callback();root.item=Item(2);throw 7;}int broken=fail();", CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error, nullptr, 2),
		        "rollback callable tests migrate retained functions into a shifted generation"))
		{
			std::cerr << "callable case=" << CallableCase << '\n'
			          << CompileError << '\n'
			          << Error
			          << '\n'
			          << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		Vm.SetProgramGeneration(2);
		if (!Expect(!Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback) && Vm.RollbackHotReloadState(*Replacement, State, Rollback),
		        "replacement destructors call restored captures through replacement function identities"))
		{
			std::cerr << "callable case=" << CallableCase << '\n'
			          << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
		Vm.SetProgramGeneration(1);
		const std::string Expected = CallableCase == 0 ? "42\n42\n42\n"
		    : CallableCase == 1                        ? "220\n330\n440\n"
		                                               : "111\n112\n113\n";
		if (!Expect(Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && Output.str() == Expected && Vm.DiscardPersistentState(*Original, State),
		        "rollback restores old callable identity and capture order while retaining destructor updates"))
		{
			std::cerr << "callable case=" << CallableCase << '\n'
			          << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Definitions =
		    "int publications=0;"
		    "class Cycle{public:Cycle link;int id;Cycle(){id=0;}Cycle(int value){id=value;}"
		    "~Cycle(){if(id==1){publish();}print(77);}};"
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}~Item(){if(id==2){publish();}}};";
		const std::string OriginalRoot =
		    "class Root{public:int value;Item item;Root(){value=42;item=Item(1);}};";
		const std::string ReplacementRoot =
		    "class Root{public:int inserted;int value;Item item;Cycle temporary;"
		    "Root(){inserted=777;value=42;item=Item(1);}};";
		const std::string OriginalCallback =
		    "fn()->int make(){int left=1;int right=10;return [left,right]()->int{return left*100+right;};}";
		const std::string ReplacementCallback =
		    "fn()->int make(){int left=1;int right=10;return [right,left]()->int{return left*100+right;};}";
		const std::string Roots =
		    "Root root=Root();fn()->int callback=make();"
		    "variant boxedRoot=root;variant boxedCallback=callback;"
		    "class Holder{public:Root slot;fn()->int callable;Holder(){slot=root;callable=callback;}};Holder holder=Holder();";
		const std::string Main =
		    "void main(){assert(root.value==42);assert(holder.slot.value==42);"
		    "Root recoveredRoot=cast<Root>(boxedRoot);fn()->int recoveredCallback=cast<fn()->int>(boxedCallback);"
		    "assert(recoveredRoot.value==42);assert(recoveredCallback()==110);"
		    "fn()->int memberCallback=holder.callable;assert(callback()==110);assert(memberCallback()==110);"
		    "print(publications);print(root.value);print(callback());}";
		std::string CompileError;
		auto Original = CompileSource(Definitions + OriginalRoot + OriginalCallback + Roots + "void publish(){}" + Main, CompileError);
		auto Replacement = CompileSource("int inserted(){return 99;}" + Definitions + ReplacementRoot + ReplacementCallback + Roots + "void publish(){publications+=1;root=Root();root.value=40+publications;"
		                                                                                                                              "if(publications==1){root.temporary=Cycle(1);root.temporary.link=root.temporary;}"
		                                                                                                                              "holder.slot=root;callback=make();holder.callable=callback;boxedRoot=root;boxedCallback=callback;}" +
		        Main + "int fail(){root.item=Item(2);throw 7;}int broken=fail();",
		    CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		Vm.SetProgramGeneration(17);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error, nullptr, 23),
		        "rollback survivor tests migrate old roots with object and callable aliases"))
		{
			std::cerr << CompileError << '\n'
			          << Error << '\n'
			          << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		Vm.SetProgramGeneration(23);
		if (!Expect(!Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback) && Vm.RollbackHotReloadState(*Replacement, State, Rollback) && !Vm.RequiresReset() && State.ProgramGeneration == 17,
		        "rollback drains new fields and enrols survivors created by successive destructor callbacks"))
		{
			std::cerr << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
		VeroScript::FObjectHandle Root;
		VeroScript::FObjectHandle Holder;
		VeroScript::FFunctionHandle Callback;
		for (std::size_t Slot = 0; Slot < State.GlobalNames.size(); ++Slot)
		{
			const auto& Name = State.GlobalNames[Slot];
			if (Name.find("root") != std::string::npos)
			{
				Root = std::get<VeroScript::FObjectHandle>(State.Globals[Slot]);
			}
			if (Name.find("holder") != std::string::npos)
			{
				Holder = std::get<VeroScript::FObjectHandle>(State.Globals[Slot]);
			}
			if (Name.find("callback") != std::string::npos)
			{
				Callback = std::get<VeroScript::FFunctionHandle>(State.Globals[Slot]);
			}
		}
		if (!Expect(Root && Holder && Callback && Root->Fields.size() == 2 && std::get<VeroScript::FObjectHandle>(Holder->Fields[0]) == Root && std::get<VeroScript::FFunctionHandle>(Holder->Fields[1]) == Callback && Callback->ProgramGeneration == 17,
		        "new objects and callables keep identity across global and field aliases while returning to the old schema"))
		{
			return EXIT_FAILURE;
		}
		Root.reset();
		Holder.reset();
		Callback.reset();
		Vm.SetProgramGeneration(17);
		if (!Expect(Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && Output.str() == "0\n42\n110\n77\n2\n42\n110\n" && Vm.DiscardPersistentState(*Original, State),
		        "new surviving closures restore capture order and the old module remains reusable after every cleanup pass"))
		{
			std::cerr << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Definitions =
		    "class Cycle{public:Cycle link;~Cycle(){publish();}};"
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}~Item(){if(id==2){publish();}}};";
		const std::string OriginalRoot = "class Root{public:Item item;Root(){item=Item(1);}};";
		const std::string ReplacementRoot = "class Root{public:Item item;Cycle temporary;Root(){item=Item(1);}};";
		const std::string Tail = "Root root=Root();void main(){}";
		std::string CompileError;
		auto Original = CompileSource(Definitions + OriginalRoot + Tail + "void publish(){}", CompileError);
		auto Replacement = CompileSource(Definitions + ReplacementRoot + Tail + "void publish(){root=Root();root.temporary=Cycle();root.temporary.link=root.temporary;}"
		                                                                        "int fail(){root.item=Item(2);throw 7;}int broken=fail();",
		    CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error) && !Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback),
		        "recursive temporary survivor cleanup starts with an allowed failed migration"))
		{
			std::cerr << CompileError << '\n'
			          << Error << '\n'
			          << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		Vm.SetExecutionLimits({128, 0});
		if (!Expect(!Vm.RollbackHotReloadState(*Replacement, State, Rollback) && Vm.RequiresReset() && State.bExecutionResetRequired && Vm.GetError().MessageId == "diag.runtime_execution_instruction_limit_exceeded" && Vm.DiscardPersistentState(*Original, State),
		        "successive temporary-cycle destructors share one rollback budget and cannot restart it on each graph scan"))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Item =
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}~Item(){if(id==2){print(42);}}};";
		const std::string Tail = "Root root=Root();void main(){}";
		std::string CompileError;
		auto Original = CompileSource(Item + "class Root{public:Item item;Root(){item=Item(1);}};" + Tail, CompileError);
		auto Replacement = CompileSource(Item + "struct ReplacementOnly{public:int value;~ReplacementOnly(){print(88);}};"
		                                        "class Root{public:Item item;ReplacementOnly temporary;Root(){item=Item(1);}};" +
		        Tail + "int fail(){root.item=Item(2);throw 7;}int broken=fail();",
		    CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error) && !Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback) && Vm.RollbackHotReloadState(*Replacement, State, Rollback) && !Vm.RequiresReset() && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && Output.str() == "42\n88\n88\n" && Vm.DiscardPersistentState(*Original, State),
		        "replacement-only types in temporary new fields are drained rather than mistaken for old-state survivors"))
		{
			std::cerr << CompileError << '\n'
			          << Error << '\n'
			          << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
	}
	{
		const std::string Definitions =
		    "class Base{public:int value;Base(){value=42;}};"
		    "class Cycle{public:Cycle link;~Cycle(){holder.value=Base();print(88);}};"
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}~Item(){if(id==2){publish();}}};";
		const std::string OriginalHolder =
		    "class Holder{public:Base value;Item item;Holder(){value=Base();item=Item(1);}};";
		const std::string ReplacementHolder =
		    "class ReplacementOnly:public Base{public:ReplacementOnly(){value=777;}};"
		    "class Holder{public:Base value;Item item;Cycle temporary;Holder(){value=Base();item=Item(1);}};";
		const std::string Tail = "Holder holder=Holder();void main(){assert(holder.value.value==42);print(holder.value.value);}";
		std::string CompileError;
		auto Original = CompileSource(Definitions + OriginalHolder + Tail + "void publish(){}", CompileError);
		auto Replacement = CompileSource(Definitions + ReplacementHolder + Tail + "void publish(){holder.value=ReplacementOnly();holder.temporary=Cycle();holder.temporary.link=holder.temporary;}"
		                                                                          "int fail(){holder.item=Item(2);throw 7;}int broken=fail();",
		    CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error) && !Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback) && Vm.RollbackHotReloadState(*Replacement, State, Rollback) && !Vm.RequiresReset() && !State.bExecutionResetRequired && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && Output.str() == "42\n88\n42\n" && Vm.DiscardPersistentState(*Original, State),
		        "temporary destructors may repair an intermediate replacement-only survivor before final graph validation"))
		{
			std::cerr << CompileError << '\n'
			          << Error << '\n'
			          << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
	}
	for (const bool bNewType : {false, true})
	{
		const std::string Definitions =
		    "int get(){return 42;}fn()->int callback=get;variant stored=0;"
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}~Item(){if(id==2){publish();}}};"
		    "class Root{public:Item item;Root(){item=Item(1);}};Root root=Root();void main(){}";
		std::string CompileError;
		auto Original = CompileSource(Definitions + "void publish(){}", CompileError);
		auto Replacement = CompileSource(Definitions + (bNewType ? "class ReplacementOnly{public:int value;};void publish(){stored=ReplacementOnly();}" : "int replacementOnly(){return 99;}void publish(){callback=replacementOnly;}") + "int fail(){root.item=Item(2);throw 7;}int broken=fail();", CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error) && !Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback) && !Vm.RollbackHotReloadState(*Replacement, State, Rollback) && Vm.RequiresReset() && State.bExecutionResetRequired && Vm.GetError().Message.find("hot reload cleanup") != std::string::npos,
		        "rollback rejects surviving replacement-only types and callables instead of publishing an unusable old state"))
		{
			std::cerr << CompileError << '\n'
			          << Error << '\n'
			          << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(!Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && Vm.GetError().MessageId == "diag.execution_state_reset_required" && Vm.DiscardPersistentState(*Original, State),
		        "an unrepresentable rollback survivor requires reset before reuse and still permits final cleanup"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const bool bAlreadyRequiresReset : {false, true})
	{
		const std::string Source =
		    "class Item{public:int id;Item(){id=0;}Item(int value){id=value;}"
		    "~Item(){if(id==2){try{for(;;){}}finally{for(;;){}}}}};"
		    "class Root{public:Item item;Root(){item=Item(1);}};Root root=Root();void main(){}";
		std::string CompileError;
		auto Original = CompileSource(Source, CompileError);
		auto Replacement = CompileSource(Source + "int fail(){root.item=Item(2);throw 7;}int broken=fail();", CompileError);
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm);
		VeroScript::FHotReloadState State;
		VeroScript::FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Original && Replacement && Vm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && VeroScript::MigrateHotReloadState(State, *Replacement, &Rollback, Error) && !Vm.ExecutePersistent(*Replacement, VeroScript::ERunMode::Script, State, &Rollback),
		        "rollback reset tests retain a failed replacement"))
		{
			std::cerr << CompileError << '\n'
			          << Error << '\n'
			          << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		State.bExecutionResetRequired = bAlreadyRequiresReset;
		Vm.SetExecutionLimits({32, 0});
		const bool bRolledBack = Vm.RollbackHotReloadState(*Replacement, State, Rollback);
		if (!Expect((bAlreadyRequiresReset || (!bRolledBack && Vm.GetError().MessageId == "diag.runtime_execution_instruction_limit_exceeded")) && Vm.RequiresReset() && State.bExecutionResetRequired,
		        "rollback preserves existing reset requirements and bounded cleanup exhaustion"))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::VeroScriptVm ReuseVm(Output);
		VeroScript::Test::Configure(ReuseVm);
		if (!Expect(!ReuseVm.ExecutePersistent(*Original, VeroScript::ERunMode::Script, State) && ReuseVm.GetError().MessageId == "diag.execution_state_reset_required" && ReuseVm.DiscardPersistentState(*Original, State),
		        "rollback cleanup reset requirement survives transfer to a fresh VM"))
		{
			std::cerr << ReuseVm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string RollbackCompileError;
		auto RollbackModule = CompileSource("class Broken{public:~Broken(){for(int i=0;i<50000;++i){}}};"
		                                    "class Owner{public:Broken value;};Owner owner=Owner();"
		                                    "void setup(){owner.value=Broken();}void main(){}",
		    RollbackCompileError);
		std::ostringstream RollbackOutput;
		VeroScript::VeroScriptVm InitializationVm(RollbackOutput);
		VeroScript::Test::Configure(InitializationVm);
		VeroScript::FHotReloadState State;
		if (!Expect(RollbackModule &&
		            InitializationVm.ExecutePersistent(
		                *RollbackModule, VeroScript::ERunMode::Script, State, nullptr) &&
		            State.Globals.size() == 1,
		        "rollback cleanup deadline test initializes its retained state"))
		{
			return EXIT_FAILURE;
		}

		auto* Owner = std::get_if<VeroScript::FObjectHandle>(&State.Globals[0]);
		if (!Expect(Owner && *Owner && (*Owner)->Fields.size() == 1,
		        "rollback cleanup deadline test resolves its retained object"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FHotReloadRollback Rollback;
		Rollback.PreviousState = State;
		Rollback.Objects.push_back({Owner->get(), (*Owner)->Fields, false, false});
		if (!Expect(InitializationVm.ExecuteFreeFunction(*RollbackModule, "setup", {}, &State),
		        "rollback cleanup deadline test creates a replacement field"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::VeroScriptVm RollbackVm(RollbackOutput);
		VeroScript::Test::Configure(RollbackVm);
		VEROSCRIPT_EXCEPTION_BACKEND_SETUP(RollbackVm);
		RollbackVm.SetExecutionLimits({0, 1});
		if (!Expect(!RollbackVm.RollbackHotReloadState(*RollbackModule, State, Rollback) &&
		            RollbackVm.GetError().MessageId == "diag.execution_time_limit_exceeded",
		        "hot-reload rollback applies its configured timeout to replacement-field destructors"))
		{
			std::cerr << RollbackCompileError << '\n'
			          << RollbackVm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(InitializationVm.DiscardPersistentState(*RollbackModule, State),
		        "rollback timeout preserves the restored state for subsequent cleanup"))
		{
			return EXIT_FAILURE;
		}
	}
	std::string FatalCompileError;
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed})
	{
		for (const bool bNestedFault : {false, true})
		{
			const std::string FaultSource =
			    bNestedFault ? "class Thrower { public: ~Thrower(){print(2);throw 66;} };\n"
			                   "void fail(){Thrower value=Thrower();int zero=0;print(1/zero);}\n"
			                   "void main(){try{fail();}catch(...){print(99);}finally{print(88);}}\n"
			                 : "class First { public: ~First(){print(1);} };\n"
			                   "class Second { public: ~Second(){print(2);throw 66;} };\n"
			                   "class Owner { public: First a=First();Second b=Second();int y=1/0; };\n"
			                   "void main(){try{Owner o=Owner();}catch(...){print(99);}finally{print(88);}}\n";
			std::string FaultCompileError;
			auto FaultModule = CompileSource(FaultSource, FaultCompileError);
			std::ostringstream FaultOutput;
			VeroScript::VeroScriptVm FaultVm(FaultOutput);
			VeroScript::Test::Configure(FaultVm);
			VEROSCRIPT_EXCEPTION_BACKEND_SETUP(FaultVm);
			FaultVm.SetExecutionBackend(Backend);
			if (!Expect(FaultModule && !FaultVm.Execute(*FaultModule, VeroScript::ERunMode::Script) &&
			            FaultOutput.str() == (bNestedFault ? "2\n88\n" : "2\n1\n88\n") &&
			            FaultVm.GetError().MessageId == "diag.division_by_zero",
			        "cleanup throws cannot make a runtime fault catchable or suppress its finally"))
			{
				std::cerr << FaultCompileError << '\n'
				          << FaultVm.GetError().Message << '\n'
				          << FaultOutput.str();
				return EXIT_FAILURE;
			}
		}
	}
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed})
	{
		for (int FinallyBehavior = 0; FinallyBehavior < 4; ++FinallyBehavior)
		{
			const std::string FinallyBody = FinallyBehavior == 0 ? "throw 66;"
			    : FinallyBehavior == 1                           ? "try{secondary();}catch(int e){print(e);}"
			    : FinallyBehavior == 2                           ? "int zero=0;print(1/zero);"
			                                                     : "exit(7);";
			const std::string ParkedFaultSource =
			    "void fail(){int zero=0;print(1/zero);}\nvoid secondary(){throw 66;}\n"
			    "void main(){try{try{fail();}finally{" +
			    FinallyBody + "}}catch(...){print(99);}finally{print(88);}}\n";
			std::string ParkedFaultCompileError;
			auto ParkedFaultModule = CompileSource(ParkedFaultSource, ParkedFaultCompileError);
			for (int Mode = 0; Mode < 3; ++Mode)
			{
				std::ostringstream ParkedFaultOutput;
				VeroScript::VeroScriptVm ParkedFaultVm(ParkedFaultOutput);
				VeroScript::Test::Configure(ParkedFaultVm);
				VEROSCRIPT_EXCEPTION_BACKEND_SETUP(ParkedFaultVm);
				ParkedFaultVm.SetExecutionBackend(Backend);
				VeroScript::VeroScriptDebugger ParkedFaultDebugger;
				ParkedFaultDebugger.SetRunMode(VeroScript::EDebugRunMode::Continue);
				if (Mode == 1)
				{
					ParkedFaultVm.SetDebugger(&ParkedFaultDebugger);
				}

				if (Mode == 2)
				{
					ParkedFaultVm.SetExecutionLimits({10000, 0});
				}

				const std::string_view ExpectedFaultOutput = FinallyBehavior == 1 ? "66\n88\n"
				    : FinallyBehavior == 3                                        ? ""
				                                                                  : "88\n";
				if (!Expect(
				        ParkedFaultModule && !ParkedFaultVm.Execute(*ParkedFaultModule, VeroScript::ERunMode::Script) &&
				            ParkedFaultOutput.str() == ExpectedFaultOutput &&
				            ParkedFaultVm.GetError().MessageId == "diag.division_by_zero" &&
				            ParkedFaultVm.GetError().Location.Line == 1,
				        "a parked runtime fault survives an escaping finally transfer while allowing inner catches"))
				{
					std::cerr << "finally=" << FinallyBehavior << " mode=" << Mode << '\n'
					          << ParkedFaultCompileError << '\n'
					          << ParkedFaultVm.GetError().Message << '\n'
					          << ParkedFaultVm.GetError().Location.Line << '\n'
					          << ParkedFaultOutput.str();
					return EXIT_FAILURE;
				}
			}
		}
	}
	std::unique_ptr<VeroScript::FBytecodeModule> FatalModule = CompileSource(
	    "void main() { int zero = 0; try { print(1 / zero); } catch (...) { print(99); } }",
	    FatalCompileError);
	std::ostringstream FatalOutput;
	VeroScript::VeroScriptVm FatalVm(FatalOutput);
	VeroScript::Test::Configure(FatalVm);
	const bool bFatalOk = FatalModule && FatalVm.Execute(*FatalModule, VeroScript::ERunMode::Script);
	if (!Expect(!bFatalOk && FatalOutput.str().empty(),
	        "runtime faults must not be swallowed by script catch"))
	{
		return EXIT_FAILURE;
	}

	std::string FatalFinallyCompileError;
	std::unique_ptr<VeroScript::FBytecodeModule> FatalFinallyModule = CompileSource(
	    "void runtime_fail() { int zero = 0; print(1 / zero); } "
	    "void main() { try { runtime_fail(); } finally { print(88); } }",
	    FatalFinallyCompileError);
	std::ostringstream FatalFinallyOutput;
	VeroScript::VeroScriptVm FatalFinallyVm(FatalFinallyOutput);
	VeroScript::Test::Configure(FatalFinallyVm);
	const bool bFatalFinallyOk = FatalFinallyModule && FatalFinallyVm.Execute(*FatalFinallyModule, VeroScript::ERunMode::Script);
	if (!Expect(!bFatalFinallyOk && FatalFinallyOutput.str() == "88\n",
	        "finally must run while a non-catchable runtime fault propagates"))
	{
		return EXIT_FAILURE;
	}

	std::string UncaughtFinallyCompileError;
	std::unique_ptr<VeroScript::FBytecodeModule> UncaughtFinallyModule = CompileSource(
	    "void fail_uncaught() { throw 77; }\n"
	    "void main() { try { fail_uncaught(); } finally { print(89); } }\n",
	    UncaughtFinallyCompileError);
	std::ostringstream UncaughtFinallyOutput;
	VeroScript::VeroScriptVm UncaughtFinallyVm(UncaughtFinallyOutput);
	VeroScript::Test::Configure(UncaughtFinallyVm);
	const bool bUncaughtFinallyOk = UncaughtFinallyModule && UncaughtFinallyVm.Execute(*UncaughtFinallyModule, VeroScript::ERunMode::Script);
	if (!Expect(
	        !bUncaughtFinallyOk && UncaughtFinallyOutput.str() == "89\n" && UncaughtFinallyVm.GetError().Location.Line == 1,
	        "uncaught exception diagnostics must retain the original throw site through finally"))
	{
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
