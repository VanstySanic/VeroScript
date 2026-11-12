// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptDebugger.h"
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

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(const std::string& Source)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "debug.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "debug.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
		return Compiler.Compile(*Program);
	}
}

int main()
{

	{
		const auto Module = CompileSource("int visits=0;\nint init(){++visits;return 9;}\nvoid main(){\n"
		                                  "static int count=init();\n++count;\nprint(count);\n}\n");
		if (!Expect(static_cast<bool>(Module), "debug local static fixture compiles")) return EXIT_FAILURE;
		std::ostringstream Output, Errors;
		VeroScript::VeroScriptVm Vm(Output, std::cin, Errors);
		VeroScript::Test::Configure(Vm);
		VeroScript::VeroScriptDebugger Debugger;
		Debugger.SetBreakpoint("debug.vs", 4);
		Debugger.SetBreakpoint("debug.vs", 5);
		bool SawUninitialized = false, SawInitialized = false, Correct = true;
		Debugger.SetStopCallback([&](auto& Active, auto)
		{
			for (const auto& Variable : Active.GetLocals())
			{
				if (Variable.Name != "count")
				{
					continue;
				}
				if (!Variable.bHasRawValue)
				{
					SawUninitialized = true;
					Correct &= (Variable.Value == "<uninitialized>" || Variable.Value == "<initializing>") && !Active.Evaluate("count").bOk;
				}
				else
				{
					SawInitialized = true;
					Correct &= Variable.Value == "9" && Variable.bReadOnly && Active.Evaluate("count + 1").Value == "10";
				}
			}
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		Vm.SetDebugger(&Debugger);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script) && SawUninitialized && SawInitialized && Correct && Output.str() == "10\n",
		        "debug local statics without executing an initializer"))
		{
			return EXIT_FAILURE;
		}
	}

	const std::string Source =
	    "void main() {\n"
	    "    int x = 1;\n"
	    "    int y = 2;\n"
	    "    print(x + y);\n"
	    "}\n";

	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile"))
	{
		return EXIT_FAILURE;
	}

	bool bFoundMainDebug = false;
	std::uint32_t BreakLine = 0;
	for (const VeroScript::FBytecodeFunction& Function : Module->Functions)
	{
		const std::size_t UnitSep = Function.Name.rfind('\x1F');
		const std::string LeafName =
		    UnitSep == std::string::npos ? Function.Name : Function.Name.substr(UnitSep + 1);
		const bool bIsMain =
		    LeafName == "main" || LeafName.rfind("main#", 0) == 0;
		if (!bIsMain)
		{
			continue;
		}
		if (!Expect(!Function.LocalNames.empty(), "main should have LocalNames"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::FDebugLine& Line : Function.DebugLines)
		{
			if (Line.Line >= 2)
			{
				bFoundMainDebug = true;
				if (Line.Line > BreakLine)
				{
					BreakLine = Line.Line;
				}
			}
		}
	}
	if (!Expect(bFoundMainDebug && BreakLine != 0, "expected debug line mapping in main"))
	{
		return EXIT_FAILURE;
	}

	std::ostringstream Output;
	std::ostringstream Errors;
	VeroScript::VeroScriptVm Vm(Output, std::cin, Errors);
	VeroScript::Test::Configure(Vm);
	VeroScript::VeroScriptDebugger Debugger;
	int StopCount = 0;
	std::uint32_t StoppedLine = 0;
	std::string LocalX;
	Debugger.SetBreakpoint("debug.vs", BreakLine);
	Debugger.SetStopCallback(
	    [&](VeroScript::VeroScriptDebugger& Active, VeroScript::EDebugStopReason Reason)
	{
		++StopCount;
		const auto Stack = Active.GetStackTrace();
		if (!Stack.empty())
		{
			StoppedLine = Stack.front().Line;
		}
		const auto Locals = Active.GetLocals(0);
		for (const auto& Local : Locals)
		{
			if (Local.Name == "x")
			{
				LocalX = Local.Value;
			}
		}
		(void)Reason;
		Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
	});
	Vm.SetDebugger(&Debugger);
	if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script), "execute with debugger"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(StopCount >= 1, "should stop at breakpoint"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(StoppedLine == BreakLine, "stopped on expected breakpoint line"))
	{
		std::cerr << "line=" << StoppedLine << " expected=" << BreakLine << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(LocalX == "1", "local x should be 1"))
	{
		std::cerr << "x=" << LocalX << '\n';
		return EXIT_FAILURE;
	}
	if (!Expect(Output.str() == "3\n", "output after continue"))
	{
		std::cerr << Output.str();
		return EXIT_FAILURE;
	}

	// A declaration whose initializer starts on the next line must be pausable
	// before the initializer executes, without losing the initializer line.
	{
		const std::string MultilineSource =
		    "int marker = 0;\n"
		    "int mark() {\n"
		    "    print(\"init\"); marker = 1;\n"
		    "    return 2;\n"
		    "}\n"
		    "void main() {\n"
		    "    double elapsed_us =\n"
		    "        mark() * 1000000.0;\n"
		    "    print(marker);\n"
		    "}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> MultilineModule =
		    CompileSource(MultilineSource);
		if (!Expect(static_cast<bool>(MultilineModule), "compile multiline declaration"))
		{
			return EXIT_FAILURE;
		}
		bool bDeclarationMapped = false;
		bool bInitializerMapped = false;
		for (const VeroScript::FBytecodeFunction& Function : MultilineModule->Functions)
		{
			if (Function.Name.find("main") == std::string::npos)
			{
				continue;
			}
			for (const VeroScript::FDebugLine& Line : Function.DebugLines)
			{
				bDeclarationMapped = bDeclarationMapped || Line.Line == 7;
				bInitializerMapped = bInitializerMapped || Line.Line == 8;
			}
		}
		if (!Expect(bDeclarationMapped && bInitializerMapped,
		        "multiline declaration and initializer must both have debug mappings"))
		{
			return EXIT_FAILURE;
		}

		for (const std::uint32_t TargetLine : {7u, 8u})
		{
			std::ostringstream MultilineOutput;
			std::ostringstream MultilineErrors;
			VeroScript::VeroScriptVm MultilineVm(
			    MultilineOutput, std::cin, MultilineErrors);
			VeroScript::Test::Configure(MultilineVm);
			VeroScript::VeroScriptDebugger MultilineDebugger;
			int Hits = 0;
			bool bBeforeInitializer = false;
			bool bCorrectLine = false;
			MultilineDebugger.SetBreakpoint("debug.vs", TargetLine);
			MultilineDebugger.SetStopCallback(
			    [&](VeroScript::VeroScriptDebugger& Active,
			        VeroScript::EDebugStopReason)
			{
				++Hits;
				const auto Stack = Active.GetStackTrace();
				bCorrectLine = bCorrectLine || (!Stack.empty() && Stack.front().Line == TargetLine);
				bBeforeInitializer = bBeforeInitializer || MultilineOutput.str().empty();
				Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
			});
			MultilineVm.SetDebugger(&MultilineDebugger);
			if (!Expect(
			        MultilineVm.Execute(*MultilineModule, VeroScript::ERunMode::Script),
			        "execute multiline declaration") ||
			    !Expect(Hits >= 1 && bCorrectLine && bBeforeInitializer,
			        "multiline breakpoint must stop before initializer") ||
			    !Expect(MultilineOutput.str() == "init\n1\n",
			        "multiline initializer must execute once"))
			{
				std::cerr << "target=" << TargetLine << " hits=" << Hits
				          << " line=" << bCorrectLine
				          << " before=" << bBeforeInitializer << '\n';
				std::cerr << MultilineErrors.str();
				return EXIT_FAILURE;
			}
		}
		for (const VeroScript::EExecutionBackend Backend :
		    {VeroScript::EExecutionBackend::Vm,
		        VeroScript::EExecutionBackend::Mixed})
		{
			std::ostringstream BackendOutput;
			std::ostringstream BackendErrors;
			VeroScript::VeroScriptVm BackendVm(
			    BackendOutput, std::cin, BackendErrors);
			VeroScript::Test::Configure(BackendVm);
			BackendVm.SetExecutionBackend(Backend);
			if (!Expect(BackendVm.Execute(*MultilineModule, VeroScript::ERunMode::Script),
			        "execute multiline declaration on requested backend") ||
			    !Expect(BackendOutput.str() == "init\n1\n",
			        "multiline declaration backend output"))
			{
				std::cerr << BackendErrors.str();
				return EXIT_FAILURE;
			}
		}
	}

	// The expression root can be on the declaration line while its first
	// executable child is on the following line (unary operand or call argument).
	{
		const std::string NestedSource =
		    "int count = 0;\n"
		    "int tick() { print(\"tick\"); count += 1; return 2; }\n"
		    "int identity(int value) { return value; }\n"
		    "void main() {\n"
		    "    int first = -\n"
		    "        tick();\n"
		    "    int second = identity(\n"
		    "        tick());\n"
		    "    print(count);\n"
		    "}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> NestedModule =
		    CompileSource(NestedSource);
		if (!Expect(static_cast<bool>(NestedModule), "compile nested multiline declaration"))
		{
			return EXIT_FAILURE;
		}
		for (const std::uint32_t TargetLine : {5u, 7u})
		{
			std::ostringstream NestedOutput;
			std::ostringstream NestedErrors;
			VeroScript::VeroScriptVm NestedVm(NestedOutput, std::cin, NestedErrors);
			VeroScript::Test::Configure(NestedVm);
			VeroScript::VeroScriptDebugger NestedDebugger;
			bool bStoppedBeforeOperand = false;
			NestedDebugger.SetBreakpoint("debug.vs", TargetLine);
			NestedDebugger.SetStopCallback(
			    [&](VeroScript::VeroScriptDebugger& Active,
			        VeroScript::EDebugStopReason)
			{
				const auto Stack = Active.GetStackTrace();
				const std::string ExpectedPriorOutput =
				    TargetLine == 5 ? "" : "tick\n";
				bStoppedBeforeOperand = bStoppedBeforeOperand || (!Stack.empty() && Stack.front().Line == TargetLine && NestedOutput.str() == ExpectedPriorOutput);
				Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
			});
			NestedVm.SetDebugger(&NestedDebugger);
			if (!Expect(NestedVm.Execute(*NestedModule, VeroScript::ERunMode::Script),
			        "execute nested multiline declaration") ||
			    !Expect(bStoppedBeforeOperand,
			        "nested multiline declaration must stop before operand") ||
			    !Expect(NestedOutput.str() == "tick\ntick\n2\n",
			        "nested multiline initializers execute once"))
			{
				std::cerr << "line=" << TargetLine << '\n'
				          << NestedErrors.str();
				return EXIT_FAILURE;
			}
		}
	}

	// The pause marker is not a script operation and must not consume a host
	// instruction budget relative to the equivalent single-line declaration.
	{
		const auto InlineModule = CompileSource(
		    "void main() {\n    int value = 1;\n    print(value);\n}\n");
		const auto WrappedModule = CompileSource(
		    "void main() {\n    int value =\n        1;\n    print(value);\n}\n");
		if (!Expect(static_cast<bool>(InlineModule) && static_cast<bool>(WrappedModule),
		        "compile instruction-budget declarations"))
		{
			return EXIT_FAILURE;
		}
		const auto MinimumBudget = [](const VeroScript::FBytecodeModule& Candidate)
		{
			for (std::uint64_t Budget = 1; Budget <= 64; ++Budget)
			{
				std::ostringstream Output;
				std::ostringstream Errors;
				VeroScript::VeroScriptVm Vm(Output, std::cin, Errors);
				VeroScript::Test::Configure(Vm);
				VeroScript::FExecutionLimits Limits;
				Limits.MaxInstructions = Budget;
				Vm.SetExecutionLimits(Limits);
				if (Vm.Execute(Candidate, VeroScript::ERunMode::Script) && Output.str() == "1\n")
				{
					return Budget;
				}
			}
			return std::uint64_t{0};
		};
		const std::uint64_t InlineBudget = MinimumBudget(*InlineModule);
		const std::uint64_t WrappedBudget = MinimumBudget(*WrappedModule);
		if (!Expect(InlineBudget != 0 && InlineBudget == WrappedBudget,
		        "multiline debug pause must not change instruction budget"))
		{
			std::cerr << "inline=" << InlineBudget
			          << " wrapped=" << WrappedBudget << '\n';
			return EXIT_FAILURE;
		}
		auto ExternalNops = CompileSource(
		    "void main() {\n    int value = 1;\n    print(value);\n}\n");
		if (!Expect(static_cast<bool>(ExternalNops), "compile external Nop budget suite"))
		{
			return EXIT_FAILURE;
		}
		bool bInserted = false;
		for (VeroScript::FBytecodeFunction& Function : ExternalNops->Functions)
		{
			const std::size_t UnitSep = Function.Name.rfind('\x1F');
			const std::string LeafName = UnitSep == std::string::npos
			    ? Function.Name
			    : Function.Name.substr(UnitSep + 1);
			if (LeafName == "main" || LeafName.rfind("main#", 0) == 0)
			{
				Function.Code.insert(Function.Code.begin(), 3,
				    static_cast<std::uint8_t>(VeroScript::EOpcode::Nop));
				Function.DebugLines.insert(Function.DebugLines.begin(), 3,
				    VeroScript::FDebugLine{});
				bInserted = true;
				break;
			}
		}
		if (!Expect(bInserted && MinimumBudget(*ExternalNops) == InlineBudget + 3,
		        "unmapped external Nops must still consume the instruction budget"))
		{
			return EXIT_FAILURE;
		}
	}

	// Attaching a debugger must not change instruction limits for local throws.
	{
		const auto ThrowModule = CompileSource(
		    "void main() { int code = 7; try { throw code; } "
		    "catch (int value) { print(value); } }");
		if (!Expect(static_cast<bool>(ThrowModule), "compile governed local throw"))
		{
			return EXIT_FAILURE;
		}
		const auto MinimumBudget = [&](const bool bAttachDebugger)
		{
			for (std::uint64_t Budget = 1; Budget <= 64; ++Budget)
			{
				std::ostringstream Output;
				std::ostringstream Errors;
				VeroScript::VeroScriptVm Vm(Output, std::cin, Errors);
				VeroScript::Test::Configure(Vm);
				VeroScript::FExecutionLimits Limits;
				Limits.MaxInstructions = Budget;
				Vm.SetExecutionLimits(Limits);
				VeroScript::VeroScriptDebugger Debugger;
				Debugger.SetRunMode(VeroScript::EDebugRunMode::Continue);
				if (bAttachDebugger)
				{
					Vm.SetDebugger(&Debugger);
				}
				if (Vm.Execute(*ThrowModule, VeroScript::ERunMode::Script) && Output.str() == "7\n")
				{
					return Budget;
				}
			}
			return std::uint64_t{0};
		};
		const auto FastBudget = MinimumBudget(false);
		const auto DebugBudget = MinimumBudget(true);
		if (!Expect(FastBudget != 0 && FastBudget == DebugBudget,
		        "local throw must consume the same instruction budget with and without a debugger"))
		{
			std::cerr << "fast=" << FastBudget << " debug=" << DebugBudget << '\n';
			return EXIT_FAILURE;
		}
	}

	// Module initialization uses the same declaration compiler path.
	{
		const std::string GlobalSource =
		    "int make() { print(\"init\"); return 7; }\n"
		    "int value =\n"
		    "    make();\n"
		    "void main() { print(value); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> GlobalModule =
		    CompileSource(GlobalSource);
		if (!Expect(static_cast<bool>(GlobalModule), "compile multiline global"))
		{
			return EXIT_FAILURE;
		}
		bool bGlobalDeclarationMapped = false;
		bool bGlobalInitializerMapped = false;
		for (const VeroScript::FDebugLine& Line : GlobalModule->InitDebugLines)
		{
			bGlobalDeclarationMapped = bGlobalDeclarationMapped || Line.Line == 2;
			bGlobalInitializerMapped = bGlobalInitializerMapped || Line.Line == 3;
		}
		if (!Expect(bGlobalDeclarationMapped && bGlobalInitializerMapped,
		        "multiline global must retain both debug lines"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream GlobalOutput;
		std::ostringstream GlobalErrors;
		VeroScript::VeroScriptVm GlobalVm(GlobalOutput, std::cin, GlobalErrors);
		VeroScript::Test::Configure(GlobalVm);
		VeroScript::VeroScriptDebugger GlobalDebugger;
		bool bStoppedBeforeInit = false;
		GlobalDebugger.SetBreakpoint("debug.vs", 2);
		GlobalDebugger.SetStopCallback(
		    [&](VeroScript::VeroScriptDebugger& Active,
		        VeroScript::EDebugStopReason)
		{
			const auto Stack = Active.GetStackTrace();
			bStoppedBeforeInit = bStoppedBeforeInit || (!Stack.empty() && Stack.front().Line == 2 && GlobalOutput.str().empty());
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		GlobalVm.SetDebugger(&GlobalDebugger);
		if (!Expect(GlobalVm.Execute(*GlobalModule, VeroScript::ERunMode::Script),
		        "execute multiline global") ||
		    !Expect(bStoppedBeforeInit,
		        "multiline global breakpoint must precede initializer") ||
		    !Expect(GlobalOutput.str() == "init\n7\n",
		        "multiline global initializer must execute once"))
		{
			std::cerr << GlobalErrors.str();
			return EXIT_FAILURE;
		}
	}

	// Reference and brace-container declarations compile through dedicated
	// initializer paths, which must still expose the initializer's own line.
	{
		const auto SpecializedModule = CompileSource(
		    "void main() {\n"
		    "    int source = 3;\n"
		    "    int& alias =\n"
		    "        source;\n"
		    "    array<int> values =\n"
		    "        {1, 2};\n"
		    "    print(alias);\n"
		    "    print(values[0]);\n"
		    "}\n");
		if (!Expect(static_cast<bool>(SpecializedModule),
		        "compile specialized multiline declarations"))
		{
			return EXIT_FAILURE;
		}
		bool Seen[4] = {};
		bool bContainerCreationOnInitializerLine = false;
		for (const VeroScript::FBytecodeFunction& Function : SpecializedModule->Functions)
		{
			for (std::size_t Ip = 0; Ip < Function.DebugLines.size(); ++Ip)
			{
				const VeroScript::FDebugLine& Line = Function.DebugLines[Ip];
				if (Line.Line >= 3 && Line.Line <= 6)
				{
					Seen[Line.Line - 3] = true;
				}
				bContainerCreationOnInitializerLine =
				    bContainerCreationOnInitializerLine || (Line.Line == 6 && Ip < Function.Code.size() && static_cast<VeroScript::EOpcode>(Function.Code[Ip]) == VeroScript::EOpcode::NewDynamicArray);
			}
		}
		if (!Expect(Seen[0] && Seen[1] && Seen[2] && Seen[3] && bContainerCreationOnInitializerLine,
		        "specialized declaration and initializer lines must be mapped"))
		{
			std::cerr << "mapped lines: " << Seen[0] << Seen[1]
			          << Seen[2] << Seen[3] << '\n';
			return EXIT_FAILURE;
		}
		std::ostringstream SpecializedOutput;
		std::ostringstream SpecializedErrors;
		VeroScript::VeroScriptVm SpecializedVm(
		    SpecializedOutput, std::cin, SpecializedErrors);
		VeroScript::Test::Configure(SpecializedVm);
		VeroScript::VeroScriptDebugger SpecializedDebugger;
		std::vector<std::uint32_t> StoppedLines;
		SpecializedDebugger.SetBreakpoint("debug.vs", 4);
		SpecializedDebugger.SetBreakpoint("debug.vs", 6);
		SpecializedDebugger.SetStopCallback(
		    [&](VeroScript::VeroScriptDebugger& Active,
		        VeroScript::EDebugStopReason)
		{
			const auto Stack = Active.GetStackTrace();
			if (!Stack.empty() && SpecializedOutput.str().empty())
			{
				StoppedLines.push_back(Stack.front().Line);
			}
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		SpecializedVm.SetDebugger(&SpecializedDebugger);
		if (!Expect(SpecializedVm.Execute(
		                *SpecializedModule, VeroScript::ERunMode::Script),
		        "execute specialized multiline declarations") ||
		    !Expect(StoppedLines == std::vector<std::uint32_t>{4, 6},
		        "specialized initializer breakpoints stop before execution") ||
		    !Expect(SpecializedOutput.str() == "3\n1\n",
		        "specialized initializers keep script behavior"))
		{
			std::cerr << SpecializedErrors.str();
			return EXIT_FAILURE;
		}
	}

	// A brace element is compiled through a scratch bytecode stream. Source
	// mappings for a nested multiline expression must survive that append.
	{
		const auto NestedContainerModule = CompileSource(
		    "int identity(int value) { return value; }\n"
		    "void main() {\n"
		    "    array<int> values = {\n"
		    "        identity(\n"
		    "            2)\n"
		    "    };\n"
		    "    print(values[0]);\n"
		    "}\n");
		if (!Expect(static_cast<bool>(NestedContainerModule),
		        "compile nested multiline brace element"))
		{
			return EXIT_FAILURE;
		}
		bool bCallLine = false;
		bool bArgumentLine = false;
		for (const VeroScript::FBytecodeFunction& Function : NestedContainerModule->Functions)
		{
			for (const VeroScript::FDebugLine& Line : Function.DebugLines)
			{
				bCallLine = bCallLine || Line.Line == 4;
				bArgumentLine = bArgumentLine || Line.Line == 5;
			}
		}
		if (!Expect(bCallLine && bArgumentLine,
		        "nested brace element must retain call and argument lines"))
		{
			std::cerr << "call=" << bCallLine << " argument=" << bArgumentLine << '\n';
			return EXIT_FAILURE;
		}
		std::ostringstream NestedContainerOutput;
		std::ostringstream NestedContainerErrors;
		VeroScript::VeroScriptVm NestedContainerVm(
		    NestedContainerOutput, std::cin, NestedContainerErrors);
		VeroScript::Test::Configure(NestedContainerVm);
		VeroScript::VeroScriptDebugger NestedContainerDebugger;
		std::vector<std::uint32_t> StoppedLines;
		NestedContainerDebugger.SetBreakpoint("debug.vs", 4);
		NestedContainerDebugger.SetBreakpoint("debug.vs", 5);
		NestedContainerDebugger.SetStopCallback(
		    [&](VeroScript::VeroScriptDebugger& Active,
		        VeroScript::EDebugStopReason)
		{
			const auto Stack = Active.GetStackTrace();
			if (!Stack.empty() && NestedContainerOutput.str().empty())
			{
				StoppedLines.push_back(Stack.front().Line);
			}
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		NestedContainerVm.SetDebugger(&NestedContainerDebugger);
		if (!Expect(NestedContainerVm.Execute(
		                *NestedContainerModule, VeroScript::ERunMode::Script),
		        "execute nested multiline brace element") ||
		    !Expect(StoppedLines.size() >= 2 && StoppedLines[0] == 4 && StoppedLines[1] == 5,
		        "nested brace call and argument must both be pausable") ||
		    !Expect(NestedContainerOutput.str() == "2\n",
		        "nested brace element must execute once"))
		{
			std::cerr << "stopped:";
			for (const std::uint32_t Line : StoppedLines)
			{
				std::cerr << ' ' << Line;
			}
			std::cerr << " output=" << NestedContainerOutput.str() << '\n';
			std::cerr << NestedContainerErrors.str();
			return EXIT_FAILURE;
		}
		const auto InlineContainerModule = CompileSource(
		    "int identity(int value) { return value; }\n"
		    "void main() { array<int> values = {identity(2)}; print(values[0]); }\n");
		if (!Expect(static_cast<bool>(InlineContainerModule),
		        "compile inline brace-element budget control"))
		{
			return EXIT_FAILURE;
		}
		const auto MinimumBudget = [](const VeroScript::FBytecodeModule& Candidate)
		{
			for (std::uint64_t Budget = 1; Budget <= 128; ++Budget)
			{
				std::ostringstream Output;
				std::ostringstream Errors;
				VeroScript::VeroScriptVm Vm(Output, std::cin, Errors);
				VeroScript::Test::Configure(Vm);
				VeroScript::FExecutionLimits Limits;
				Limits.MaxInstructions = Budget;
				Vm.SetExecutionLimits(Limits);
				if (Vm.Execute(Candidate, VeroScript::ERunMode::Script) && Output.str() == "2\n")
				{
					return Budget;
				}
			}
			return std::uint64_t{0};
		};
		const std::uint64_t InlineBudget = MinimumBudget(*InlineContainerModule);
		if (!Expect(InlineBudget != 0 && InlineBudget == MinimumBudget(*NestedContainerModule),
		        "nested brace debug marker must not change instruction budget"))
		{
			return EXIT_FAILURE;
		}
		for (const VeroScript::EExecutionBackend Backend :
		    {VeroScript::EExecutionBackend::Vm,
		        VeroScript::EExecutionBackend::Mixed})
		{
			std::ostringstream BackendOutput;
			std::ostringstream BackendErrors;
			VeroScript::VeroScriptVm BackendVm(
			    BackendOutput, std::cin, BackendErrors);
			VeroScript::Test::Configure(BackendVm);
			BackendVm.SetExecutionBackend(Backend);
			if (!Expect(BackendVm.Execute(
			                *NestedContainerModule, VeroScript::ERunMode::Script) &&
			            BackendOutput.str() == "2\n",
			        "nested brace initializer behaves across execution backends"))
			{
				std::cerr << BackendErrors.str();
				return EXIT_FAILURE;
			}
		}
	}

	// Evaluate / SetLocal / conditional breakpoint (2.47 P1).
	{
		std::ostringstream EvalOutput;
		std::ostringstream EvalErrors;
		VeroScript::VeroScriptVm EvalVm(EvalOutput, std::cin, EvalErrors);
		VeroScript::Test::Configure(EvalVm);
		VeroScript::VeroScriptDebugger EvalDbg;
		std::string EvalXPlus;
		std::string AfterSet;
		int CondStops = 0;
		EvalDbg.SetBreakpoint("debug.vs", BreakLine, "x == 1");
		EvalDbg.SetStopCallback(
		    [&](VeroScript::VeroScriptDebugger& Active, VeroScript::EDebugStopReason)
		{
			++CondStops;
			const VeroScript::FDebugEvaluateResult Plus =
			    Active.Evaluate("x + 1", 0);
			if (Plus.bOk)
			{
				EvalXPlus = Plus.Value;
			}
			const VeroScript::FDebugEvaluateResult Set =
			    Active.SetLocal("x", "5", 0);
			if (Set.bOk)
			{
				for (const auto& Local : Active.GetLocals(0))
				{
					if (Local.Name == "x")
					{
						AfterSet = Local.Value;
					}
				}
			}
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		EvalVm.SetDebugger(&EvalDbg);
		if (!Expect(EvalVm.Execute(*Module, VeroScript::ERunMode::Script), "execute eval suite"))
		{
			std::cerr << EvalErrors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(CondStops == 1, "conditional breakpoint should stop once when x==1"))
		{
			std::cerr << "stops=" << CondStops << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(EvalXPlus == "2", "Evaluate x+1"))
		{
			std::cerr << "x+1=" << EvalXPlus << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(AfterSet == "5", "SetLocal x=5"))
		{
			std::cerr << "x=" << AfterSet << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(EvalOutput.str() == "7\n", "output uses updated x"))
		{
			std::cerr << EvalOutput.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream SkipOutput;
		std::ostringstream SkipErrors;
		VeroScript::VeroScriptVm SkipVm(SkipOutput, std::cin, SkipErrors);
		VeroScript::Test::Configure(SkipVm);
		VeroScript::VeroScriptDebugger SkipDbg;
		int SkipStops = 0;
		SkipDbg.SetBreakpoint("debug.vs", BreakLine, "x == 99");
		SkipDbg.SetStopCallback(
		    [&](VeroScript::VeroScriptDebugger& Active, VeroScript::EDebugStopReason)
		{
			++SkipStops;
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		SkipVm.SetDebugger(&SkipDbg);
		if (!Expect(SkipVm.Execute(*Module, VeroScript::ERunMode::Script), "execute skip cond"))
		{
			std::cerr << SkipErrors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(SkipStops == 0, "false condition should not stop"))
		{
			std::cerr << "stops=" << SkipStops << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(SkipOutput.str() == "3\n", "skip-cond output"))
		{
			std::cerr << SkipOutput.str();
			return EXIT_FAILURE;
		}
	}

	// StepIn before Execute must arm (regression for SetRunMode without AttachVm).
	{
		std::ostringstream StepOutput;
		std::ostringstream StepErrors;
		VeroScript::VeroScriptVm StepVm(StepOutput, std::cin, StepErrors);
		VeroScript::Test::Configure(StepVm);
		VeroScript::VeroScriptDebugger StepDbg;
		int StepStops = 0;
		StepDbg.SetRunMode(VeroScript::EDebugRunMode::StepIn);
		StepDbg.SetStopCallback(
		    [&](VeroScript::VeroScriptDebugger& Active, VeroScript::EDebugStopReason Reason)
		{
			++StepStops;
			if (!Expect(Reason == VeroScript::EDebugStopReason::Step, "step reason"))
			{
				Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
				return;
			}
			if (StepStops >= 3)
			{
				Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
			}
			else
			{
				Active.SetRunMode(VeroScript::EDebugRunMode::StepIn);
			}
		});
		StepVm.SetDebugger(&StepDbg);
		if (!Expect(StepVm.Execute(*Module, VeroScript::ERunMode::Script), "execute with StepIn"))
		{
			std::cerr << StepErrors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(StepStops >= 3, "StepIn before Execute should stop multiple times"))
		{
			std::cerr << "stops=" << StepStops << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(StepOutput.str() == "3\n", "StepIn run output"))
		{
			std::cerr << StepOutput.str();
			return EXIT_FAILURE;
		}
	}

	// Strong enum container children remain structured and writable through DAP primitives.
	{
		const std::string EnumContainerSource =
		    "enum class Color { Red = 1, Green = 2 }\n"
		    "class Holder { public: int value = 7; "
		    "map<Color,int> values = map<Color,int>(); "
		    "int fixed[2] = {3, 4}; "
		    "Holder() { values[Color.Red] = 5; } }\n"
		    "void main() {\n"
		    "    map<Color,int> scores = map<Color,int>();\n"
		    "    scores[Color.Red] = 10;\n"
		    "    set<Color> selected = set<Color>();\n"
		    "    selected.add(Color.Red);\n"
		    "    ordered_map<Color,int> ordered = ordered_map<Color,int>();\n"
		    "    ordered[Color.Green] = 20;\n"
		    "    Holder holder = Holder();\n"
		    "    set<char> byte_set = {'\\xFF',};\n"
		    "    print(scores[Color.Red]);\n"
		    "    print(byte_set.has('A'));\n"
		    "}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> EnumModule =
		    CompileSource(EnumContainerSource);
		if (!Expect(static_cast<bool>(EnumModule), "compile enum containers"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream EnumOutput;
		std::ostringstream EnumErrors;
		VeroScript::VeroScriptVm EnumVm(EnumOutput, std::cin, EnumErrors);
		VeroScript::Test::Configure(EnumVm);
		VeroScript::VeroScriptDebugger EnumDebugger;
		bool bMapExpanded = false;
		bool bSetExpanded = false;
		bool bOrderedMapExpanded = false;
		bool bMapEdited = false;
		bool bSetEdited = false;
		bool bEnumEvaluated = false;
		bool bMapEvaluated = false;
		bool bObjectEvaluated = false;
		bool bMutationRejected = false;
		bool bObjectFieldBarrier = false;
		bool bContainerBarrier = false;
		bool bFixedArrayFieldEdited = false;
		bool bStaleMapEditRejected = false;
		bool bCharacterSetExpanded = false;
		bool bCharacterSetEdited = false;
		bool bInsideDebugWrite = false;
		std::string MapEditError;
		EnumVm.SetMutationCallback(
		    [&](const VeroScript::FMutationEvent& Event)
		{
			if (!bInsideDebugWrite)
			{
				return;
			}
			bObjectFieldBarrier = bObjectFieldBarrier || (Event.FieldName == "value" && Event.Kind == VeroScript::EMutationKind::FieldWrite);
			bContainerBarrier = bContainerBarrier || (Event.FieldName == "values" && Event.Kind == VeroScript::EMutationKind::ContainerSet);
		});
		EnumDebugger.SetBreakpoint("debug.vs", 12);
		EnumDebugger.SetStopCallback(
		    [&](VeroScript::VeroScriptDebugger& Active, VeroScript::EDebugStopReason)
		{
			for (const VeroScript::FDebugVariable& Local : Active.GetLocals(0))
			{
				const std::vector<VeroScript::FDebugVariable> Children =
				    Active.GetChildren(Local);
				if (Local.Name == "scores" && Children.size() == 1)
				{
					bMapExpanded = Children.front().Name == "[Color(1)]";
					const VeroScript::FDebugEvaluateResult Edit =
					    Active.SetChild(Children.front(), "99", 0);
					bMapEdited = Edit.bOk;
					MapEditError = Edit.Error;
				}
				else if (Local.Name == "selected" && Children.size() == 1)
				{
					bSetExpanded = Children.front().Value == "Color(1)";
					const VeroScript::FDebugEvaluateResult Edit =
					    Active.SetChild(Children.front(), "Color.Green", 0);
					bSetEdited = Edit.bOk;
				}
				else if (Local.Name == "ordered" && Children.size() == 1)
				{
					bOrderedMapExpanded = Children.front().Name == "[Color(2)]";
					VeroScript::FDebugVariable Stale = Children.front();
					if (const auto* Owner =
					        std::get_if<VeroScript::FOrderedMapHandle>(&Stale.OwnerValue);
					    Owner && *Owner)
					{
						(*Owner)->EnumEntries.erase(
						    std::get<VeroScript::FEnumValue>(Stale.TargetKey));
						const VeroScript::FDebugEvaluateResult Edit =
						    Active.SetChild(Stale, "77", 0);
						bStaleMapEditRejected = !Edit.bOk && (*Owner)->EnumEntries.empty();
					}
				}
				else if (Local.Name == "byte_set" && Children.size() == 1)
				{
					bCharacterSetExpanded = Children.front().Value == "-1";
					const VeroScript::FDebugEvaluateResult Edit =
					    Active.SetChild(Children.front(), "'A'", 0);
					bCharacterSetEdited = Edit.bOk;
				}
				else if (Local.Name == "holder")
				{
					for (const VeroScript::FDebugVariable& Field : Children)
					{
						if (Field.Name == "value")
						{
							bInsideDebugWrite = true;
							(void)Active.SetChild(Field, "8", 0);
							bInsideDebugWrite = false;
						}
						else if (Field.Name == "values")
						{
							const auto Entries = Active.GetChildren(Field);
							if (!Entries.empty())
							{
								bInsideDebugWrite = true;
								(void)Active.SetChild(Entries.front(), "6", 0);
								bInsideDebugWrite = false;
							}
						}
						else if (Field.Name == "fixed")
						{
							const auto Elements = Active.GetChildren(Field);
							if (!Elements.empty())
							{
								bInsideDebugWrite = true;
								const auto Edit = Active.SetChild(Elements.front(), "9", 0);
								bInsideDebugWrite = false;
								bFixedArrayFieldEdited = Edit.bOk;
							}
						}
					}
				}
			}
			const VeroScript::FDebugEvaluateResult EnumEval =
			    Active.Evaluate("Color.Green == Color.Green", 0);
			bEnumEvaluated = EnumEval.bOk && EnumEval.Value == "true";
			const VeroScript::FDebugEvaluateResult MapEval =
			    Active.Evaluate("scores[Color.Red]", 0);
			bMapEvaluated = MapEval.bOk && MapEval.Value == "99";
			const VeroScript::FDebugEvaluateResult ObjectEval =
			    Active.Evaluate("  holder  ", 0);
			bObjectEvaluated = ObjectEval.bOk && ObjectEval.bHasRawValue && std::holds_alternative<VeroScript::FObjectHandle>(ObjectEval.RawValue);
			const VeroScript::FDebugEvaluateResult Mutation =
			    Active.Evaluate("scores . clear ()", 0);
			bMutationRejected = !Mutation.bOk && Mutation.Error.find("read-only") != std::string::npos;
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		EnumVm.SetDebugger(&EnumDebugger);
		if (!Expect(
		        EnumVm.Execute(*EnumModule, VeroScript::ERunMode::Script),
		        "execute enum container debugger suite"))
		{
			std::cerr << EnumErrors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
		        bMapExpanded && bSetExpanded && bOrderedMapExpanded && bMapEdited && bSetEdited && bEnumEvaluated && bMapEvaluated && bObjectEvaluated && bMutationRejected && bObjectFieldBarrier && bContainerBarrier && bStaleMapEditRejected && bFixedArrayFieldEdited && bCharacterSetExpanded && bCharacterSetEdited,
		        "enum container debugger expansion and edit"))
		{
			std::cerr << "map=" << bMapExpanded
			          << " set=" << bSetExpanded
			          << " ordered=" << bOrderedMapExpanded
			          << " edited=" << bMapEdited
			          << " set-edited=" << bSetEdited
			          << " enum-eval=" << bEnumEvaluated
			          << " map-eval=" << bMapEvaluated
			          << " object-eval=" << bObjectEvaluated
			          << " mutation-rejected=" << bMutationRejected
			          << " field-barrier=" << bObjectFieldBarrier
			          << " container-barrier=" << bContainerBarrier
			          << " stale-map=" << bStaleMapEditRejected
			          << " fixed-field=" << bFixedArrayFieldEdited
			          << " char-set=" << bCharacterSetExpanded
			          << " char-set-edited=" << bCharacterSetEdited
			          << " error=" << MapEditError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        EnumOutput.str() == "99\ntrue\n",
		        "enum map and character set debugger edit output"))
		{
			std::cerr << EnumOutput.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::string DoWhileSource =
		    "void main() {\n"
		    "    int value = 0;\n"
		    "    do {\n"
		    "        value++;\n"
		    "    } while (value < 2);\n"
		    "    print(value);\n"
		    "}\n";
		std::unique_ptr<VeroScript::FBytecodeModule> DoWhileModule =
		    CompileSource(DoWhileSource);
		if (!Expect(static_cast<bool>(DoWhileModule), "compile do-while debugger suite"))
		{
			return EXIT_FAILURE;
		}

		std::ostringstream DoWhileOutput;
		std::ostringstream DoWhileErrors;
		VeroScript::VeroScriptVm DoWhileVm(DoWhileOutput, std::cin, DoWhileErrors);
		VeroScript::Test::Configure(DoWhileVm);
		VeroScript::VeroScriptDebugger DoWhileDebugger;
		int ConditionStops = 0;
		bool bStoppedOnCondition = false;
		DoWhileDebugger.SetBreakpoint("debug.vs", 5);
		DoWhileDebugger.SetStopCallback(
		    [&](VeroScript::VeroScriptDebugger& Active, VeroScript::EDebugStopReason)
		{
			++ConditionStops;
			const auto Stack = Active.GetStackTrace();
			bStoppedOnCondition = bStoppedOnCondition || (!Stack.empty() && Stack.front().Line == 5);
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		DoWhileVm.SetDebugger(&DoWhileDebugger);
		if (!Expect(
		        DoWhileVm.Execute(*DoWhileModule, VeroScript::ERunMode::Script),
		        "execute do-while debugger suite") ||
		    !Expect(
		        ConditionStops >= 1 && bStoppedOnCondition,
		        "do-while tail condition must retain a breakpoint location") ||
		    !Expect(DoWhileOutput.str() == "2\n", "do-while debugger output"))
		{
			std::cerr << DoWhileErrors.str();
			return EXIT_FAILURE;
		}
	}

	for (const bool AllowAddresses : {true, false})
	{
		auto MemoryModule = CompileSource("void main(){\n int x=1;\n ref int alias=x;\n auto bytes=new "
		                                  "std::memory::buffer(8);\n print(x);\n}\n");
		if (!Expect(static_cast<bool>(MemoryModule), "memory debug compile"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream O, E;
		VeroScript::VeroScriptVm M(O, std::cin, E);
		VeroScript::Test::Configure(M);
		VeroScript::FScriptCapabilities Capabilities;
		Capabilities.bAllowMemoryAddress = AllowAddresses;
		M.SetCapabilities(Capabilities);
		VeroScript::VeroScriptDebugger D;
		bool Checked = false, MemoryOk = true;
		D.SetBreakpoint("debug.vs", 5);
		D.SetStopCallback(
		    [&](auto& Active, auto)
		{
			std::optional<std::uint64_t> X, A;
			for (const auto& V : Active.GetLocals(0))
			{
				if (V.Name == "x")
				{
					X = V.Memory.StorageAddress;
				}
				if (V.Name == "alias")
				{
					A = V.Memory.StorageAddress;
				}
				if (V.Name == "bytes")
				{
					MemoryOk &= V.Memory.ObjectId.value_or(0) != 0 && V.Memory.ByteLength == 8;
					const auto Children = Active.GetChildren(V);
					MemoryOk &= Children.size() == (AllowAddresses ? 5 : 2);
					bool LengthFound = false, IdentityFound = false;
					for (const auto& Child : Children)
					{
						MemoryOk &= !Child.bHasRawValue && !Active.SetChild(Child, "0").bOk;
						if (Child.Name == "[byte_length]")
						{
							LengthFound = Child.Value == "8";
						}
						if (Child.Name == "[object_id]")
						{
							IdentityFound = Child.Value == std::to_string(*V.Memory.ObjectId);
						}
					}
					MemoryOk &= LengthFound && IdentityFound;
				}
			}
			MemoryOk &= X.has_value() == AllowAddresses && X == A;
			const auto Eval = Active.Evaluate("bytes");
			VeroScript::FDebugVariable Evaluated;
			Evaluated.RawValue = Eval.RawValue;
			Evaluated.bHasRawValue = Eval.bHasRawValue;
			MemoryOk &= Eval.bOk && Active.GetChildren(Evaluated).size() == (AllowAddresses ? 4 : 2);
			Checked = true;
			Active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});
		M.SetDebugger(&D);
		if (!Expect(M.Execute(*MemoryModule, VeroScript::ERunMode::Script) && Checked && MemoryOk,
		        "debug real alias and region metadata"))
		{
			return EXIT_FAILURE;
		}
	}
	std::cout << "VeroScriptDebuggerTests passed\n";
	return EXIT_SUCCESS;
}
