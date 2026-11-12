// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAst.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
	using namespace VeroScript;

	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Condition;
	}

	bool RejectExpression(const std::string& Source)
	{
		VeroScriptLexer Lexer(Source, "boundary.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "boundary.vs");
		const auto Program = Parser.ParseProgram();
		return Expect(!Program && Parser.HasError()
			&& Parser.GetError().Message.find("expression chain is too long") != std::string::npos,
			"excessive right-associative expression must fail without overflowing the stack");
	}

	bool TestAssignment()
	{
		for (const std::string_view Operator : {"=", "+="})
		{
			std::string Source = "void main(){int x=0;";
			for (int Index = 0; Index < 10000; ++Index)
			{
				Source += "x";
				Source += Operator;
			}
			Source += "1;}";
			if (!RejectExpression(Source))
			{
				return false;
			}
		}
		return true;
	}

	bool TestConditional()
	{
		std::string Source = "void main(){int x=";
		for (int Index = 0; Index < 10000; ++Index)
		{
			Source += "true?1:";
		}
		return RejectExpression(Source + "0;}");
	}

	bool TestNamespaces()
	{
		for (const std::string_view Prefix : {"namespace n{", "namespace {"})
		{
			std::string Source;
			for (int Index = 0; Index < 4096; ++Index)
			{
				Source += Prefix;
			}
			Source += std::string(4096, '}');
			VeroScriptLexer Lexer(Source, "namespaces.vs");
			VeroScriptParser Parser(Lexer.Tokenize(), "namespaces.vs");
			const auto Program = Parser.ParseProgram();
			if (!Expect(!Program && Parser.HasError()
				&& Parser.GetError().Message.find("namespace nested too deeply") != std::string::npos,
				"named and anonymous namespace nesting must be bounded"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestInitializers()
	{
		for (const std::string_view Type : {"array<A>", "map<int,A>"})
		{
			std::string Source = "using A=" + std::string(Type) + "; void main(){A value=";
			for (int Index = 0; Index < 4096; ++Index)
			{
				Source += Type == "array<A>" ? "{" : "{0:";
			}
			Source += std::string(4096, '}') + ";}";
			VeroScriptLexer Lexer(Source, "initializers.vs");
			VeroScriptParser Parser(Lexer.Tokenize(), "initializers.vs");
			const auto Program = Parser.ParseProgram();
			if (!Expect(!Program && Parser.HasError()
				&& Parser.GetError().Message.find("initializer nested too deeply") != std::string::npos,
				"recursive aliases must not bypass brace initializer nesting limits"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestOtherNesting()
	{
		const std::string Prefix = "void main(){int value=";
		for (const std::string& Source : {
			Prefix + std::string(10000, '(') + "1" + std::string(10000, ')') + ";}",
			Prefix + std::string(10000, '!') + "1;}",
			"void main(){" + std::string(10000, '{') + std::string(10000, '}') + "}"})
		{
			VeroScriptLexer Lexer(Source, "other-nesting.vs");
			VeroScriptParser Parser(Lexer.Tokenize(), "other-nesting.vs");
			const auto Program = Parser.ParseProgram();
			if (!Expect(!Program && Parser.HasError(), "existing recursion limits remain effective"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestPreflightCancellation()
	{
		std::string Source = "void main(){bool value=";
		for (int Index = 0; Index < 100000; ++Index)
		{
			Source += "x<";
		}
		Source += "x;}";
		VeroScriptLexer Lexer(Source, "preflight-cancel.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "preflight-cancel.vs");
		int Checks = 0;
		try
		{
			// The first check enters ParseProgram; the second must be reached
			// during preflight, before traversing this hostile comparison tail.
			FAnalysisControlScope Control([&Checks]()
										  {
											  return ++Checks == 2;
										  });
			(void)Parser.ParseProgram();
		}
		catch (const FAnalysisCancelled&)
		{
			return Expect(!Parser.HasError(), "preflight cancellation remains distinct from syntax errors");
		}
		return Expect(false, "preflight must observe analysis cancellation");
	}

	bool TestImports()
	{
		VeroScriptModuleLoader Loader("");
		for (int Index = 0; Index < 1024; ++Index)
		{
			Loader.SetSourceOverlay("deep/" + std::to_string(Index) + ".vs",
				Index == 1023 ? "" : "import \"" + std::to_string(Index + 1) + ".vs\";\n");
		}
		const auto Program = Loader.LoadSourceModule("import \"0.vs\";", "deep/main.vs");
		return Expect(!Program && Loader.HasError()
			&& Loader.GetError().Message.find("import nesting limit exceeded") != std::string::npos,
			"excessive import depth must produce a diagnostic");
	}

	bool TestImportBoundaries()
	{
		VeroScriptModuleLoader Loader("");
		for (int Index = 0; Index < 127; ++Index)
		{
			Loader.SetSourceOverlay("limits/" + std::to_string(Index) + ".vs",
				Index == 126 ? "" : "import \"" + std::to_string(Index + 1) + ".vs\";");
		}
		// The entry module and its 127 dependencies exactly fill the depth budget.
		auto AtLimit = Loader.LoadSourceModule("import \"0.vs\";", "limits/main.vs");
		if (!Expect(AtLimit && Loader.GetLoadedModulePaths().size() == 128,
			"import depth boundary is inclusive"))
		{
			return false;
		}
		Loader.SetSourceOverlay("limits/126.vs", "import \"0.vs\";");
		Loader.SetAllowCircularImports(true);
		auto Cycle = Loader.LoadSourceModule("import \"0.vs\";", "limits/main.vs");
		if (!Expect(Cycle && Loader.GetLoadedModulePaths().size() == 128,
			"allowed cycle does not count its back-edge as another import level"))
		{
			return false;
		}
		Loader.SetAllowCircularImports(false);
		if (!Expect(!Loader.LoadSourceModule("import \"0.vs\";", "limits/main.vs")
			&& Loader.GetError().MessageId == "diag.circular_import",
			"disallowed cycle retains its specific diagnostic"))
		{
			return false;
		}
		std::string Wide;
		for (int Index = 0; Index < 160; ++Index)
		{
			const std::string Name = "wide-" + std::to_string(Index) + ".vs";
			Loader.SetSourceOverlay("limits/" + Name, "");
			Wide += "import \"" + Name + "\";";
		}
		auto Breadth = Loader.LoadSourceModule(Wide, "limits/main.vs");
		return Expect(Breadth && Loader.GetLoadedModulePaths().size() == 161,
			"import breadth is independent of nesting and failed loads can be retried");
	}

	bool RunProgram(const FProgram& Program, const std::string_view Expected)
	{
		VeroScriptCompiler Compiler;
		const auto Module = Compiler.Compile(Program);
		if (!Expect(static_cast<bool>(Module), "compile loaded program"))
		{
			return false;
		}
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		return Expect(Vm.Execute(*Module, ERunMode::Script) && Output.str() == Expected,
			"loaded program retains its own sources and paths");
	}

	bool TestBorrowedLoadArguments()
	{
		VeroScriptModuleLoader Loader("");
		const std::string Source = "void main(){print(47);}";
		const std::string Label = "borrowed/" + std::string(200, 'a') + ".vs";
		auto First = Loader.LoadSourceModule(Source, Label);
		if (!Expect(First && Loader.GetLoadedModulePaths().size() == 1, "prepare borrowed module path"))
		{
			return false;
		}
		// Public string_view arguments can borrow metadata owned by this loader.
		auto FromPath = Loader.LoadSourceModule(Source, *Loader.GetLoadedModulePaths().begin());
		if (!Expect(FromPath && FromPath->EntryModulePath == Label, "copy borrowed path before resetting load state")
			|| !RunProgram(*FromPath, "47\n"))
		{
			return false;
		}
		if (!Expect(!Loader.LoadSourceModule("void main(){", Label), "prepare borrowed diagnostic location"))
		{
			return false;
		}
		auto FromDiagnostic = Loader.LoadSourceModule(Source, Loader.GetError().Location.FileLabel);
		if (!Expect(FromDiagnostic && FromDiagnostic->EntryModulePath == Label,
			"copy borrowed diagnostic label before clearing errors"))
		{
			return false;
		}
		// A diagnostic argument is owned by the loader too. Use valid source as
		// an unresolved import name to exercise the Source argument's lifetime.
		const std::string BorrowedSource = std::string(200, ' ') + Source;
		if (!Expect(!Loader.LoadSourceModule("import \"" + BorrowedSource + "\";", Label)
			&& !Loader.GetError().MessageArgs.empty(), "prepare borrowed source storage"))
		{
			return false;
		}
		auto FromSource = Loader.LoadSourceModule(Loader.GetError().MessageArgs.front(), Label);
		return Expect(static_cast<bool>(FromSource), "copy borrowed source before clearing errors")
			&& RunProgram(*FromSource, "47\n");
	}

	bool TestImportOrder()
	{
		VeroScriptModuleLoader Loader("order/main.vs");
		Loader.SetSourceOverlay("order/common.vs", "print(\"common\");");
		Loader.SetSourceOverlay("order/left.vs", "import \"common.vs\";print(\"left\");");
		Loader.SetSourceOverlay("order/right.vs", "import \"common.vs\";print(\"right\");");
		auto Diamond = Loader.LoadSourceModule(
			"import \"left.vs\";import \"right.vs\";void main(){print(\"main\");}", "order/main.vs");
		if (!Expect(static_cast<bool>(Diamond), "diamond graph loads")
			|| !RunProgram(*Diamond, "common\nleft\nright\nmain\n"))
		{
			return false;
		}
		Loader.SetSourceOverlay("order/a.vs",
			"import \"b.vs\";export int from_a(){return from_b();}print(\"a\");");
		Loader.SetSourceOverlay("order/b.vs",
			"import \"a.vs\";export int from_b(){return 42;}print(\"b\");");
		auto Cycle = Loader.LoadSourceModule(
			"import \"a.vs\";void main(){print(from_a());}", "order/main.vs");
		if (!Expect(static_cast<bool>(Cycle), "cyclic graph retains cross-module functions")
			|| !RunProgram(*Cycle, "b\na\n42\n"))
		{
			return false;
		}
		Loader.SetSourceOverlay("order/a.vs", "import \"common.vs\";print(\"a\");");
		Loader.SetSourceOverlay("order/b.vs", "import \"a.vs\";print(\"b\");");
		Loader.AddImplicitImportPath("b.vs");
		Loader.AddImplicitImportPath("a.vs");
		auto Cohort = Loader.LoadSourceModule("void main(){print(\"main\");}", "order/main.vs");
		return Expect(static_cast<bool>(Cohort), "implicit cohort loads in stable order")
			&& RunProgram(*Cohort, "common\na\nb\nmain\n");
	}

	bool TestMixedNesting()
	{
		std::string Source = "using A=array<A>;";
		for (int Index = 0; Index < 127; ++Index)
		{
			Source += "namespace {";
		}
		Source += "void main(){" + std::string(15, '{') + "A value="
			+ std::string(127, '{') + std::string(127, '(') + std::string(127, '!')
			+ "1" + std::string(127, ')') + std::string(127, '}') + ";"
			+ std::string(15, '}') + "}" + std::string(127, '}');
		VeroScriptLexer Lexer(Source, "mixed-nesting.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "mixed-nesting.vs");
		const auto Program = Parser.ParseProgram();
		if (!Expect(static_cast<bool>(Program) && !Parser.HasError(),
			"combined nesting within individual limits remains stack safe"))
		{
			return false;
		}
		// Parsing the deepest dependency shares the native stack with its callers.
		VeroScriptModuleLoader Loader("");
		for (int Index = 0; Index < 127; ++Index)
		{
			Loader.SetSourceOverlay("mixed/" + std::to_string(Index) + ".vs",
				Index == 126 ? Source : "import \"" + std::to_string(Index + 1) + ".vs\";");
		}
		const auto Imported = Loader.LoadSourceModule("import \"0.vs\";", "mixed/main.vs");
		return Expect(Imported && !Loader.HasError(),
			"deep imports and deep syntax share a safe native stack budget");
	}

	bool TestTraversalRetry()
	{
		VeroScriptModuleLoader Loader("retry/main.vs");
		const std::string Leaf = "export int answer(){return 42;}";
		for (int Index = 0; Index < 16; ++Index)
		{
			Loader.SetSourceOverlay("retry/" + std::to_string(Index) + ".vs",
				Index == 15 ? Leaf : "export import \"" + std::to_string(Index + 1) + ".vs\";");
		}
		const std::string Source = "import \"0.vs\";void main(){print(answer());}";
		bool Cancel = false;
		try
		{
			FAnalysisControlScope Control([&Cancel]()
										  {
											  return Cancel;
										  },
										  [&Cancel](const std::string_view Stage, double)
										  {
											  if (Stage == "parse")
											  {
												  Cancel = true;
											  }
										  });
			(void)Loader.LoadSourceModule(Source, "retry/main.vs");
			return Expect(false, "cancel traversal after its first dependency was parsed");
		}
		catch (const FAnalysisCancelled &)
		{
		}
		if (!Expect(Cancel && !Loader.HasError() && Loader.GetLoadedModulePaths().size() == 17,
					"traversal cancellation retains prepared dependencies and stays distinct from "
					"parse errors"))
		{
			return false;
		}
		auto Retried = Loader.LoadSourceModule(Source, "retry/main.vs");
		if (!Expect(static_cast<bool>(Retried), "cancelled dependency traversal can be retried")
			|| !RunProgram(*Retried, "42\n"))
		{
			return false;
		}
		Loader.SetSourceOverlay("retry/15.vs", "export int answer( {");
		if (!Expect(!Loader.LoadSourceModule(Source, "retry/main.vs") && Loader.HasError(),
			"syntax failure inside an active dependency traversal returns diagnostics"))
		{
			return false;
		}
		Loader.SetSourceOverlay("retry/15.vs", Leaf);
		auto Recovered = Loader.LoadSourceModule(Source, "retry/main.vs");
		return Expect(Recovered && !Loader.HasError(), "failed dependency traversal can be retried")
			&& RunProgram(*Recovered, "42\n") && RunProgram(*Retried, "42\n");
	}

	bool TestSourceLabels()
	{
		VeroScriptModuleLoader Loader("labels/main.vslib");
		Loader.SetSourceOverlay("labels/helper.vs", "export int answer(){return 42;}");
		const std::string Source = "import \"helper.vs\";void main(){print(answer());}";
		auto Program = Loader.LoadSourceModule(Source, "labels/main.vslib");
		if (!Expect(static_cast<bool>(Program), "explicit source remains source regardless of label extension")
			|| !RunProgram(*Program, "42\n"))
		{
			return false;
		}
		Loader.SetSourceOverlay("labels/main.vslib", Source);
		Loader.SetSourceOverlay("labels/observer.vs", "import \"main.vslib\";");
		Loader.AddImplicitImportPath("observer.vs");
		auto WithImplicit = Loader.LoadSourceModule(Source, "labels/main.vslib");
		return Expect(static_cast<bool>(WithImplicit), "implicit dependency can refer to the explicit source entry")
			&& RunProgram(*WithImplicit, "42\n");
	}

	bool TestEntrySourcePriority()
	{
		for (const std::string_view Extension : {".vs", ".vslib"})
		{
			const std::string Name = "main" + std::string(Extension);
			const std::string Path = "priority/" + Name;
			VeroScriptModuleLoader Loader(Path);
			Loader.SetSourceOverlay(Path, "void main(){print(13);}");
			Loader.SetSourceOverlay("priority/observer.vs", "import \"" + Name + "\";");
			Loader.AddImplicitImportPath("observer.vs");
			auto First = Loader.LoadSourceModule("void main(){print(42);}", Path);
			if (!Expect(static_cast<bool>(First), "explicit entry loads through an implicit back-reference")
				|| !RunProgram(*First, "42\n"))
			{
				return false;
			}
			const auto Fingerprint = Loader.GetSourceFingerprint();
			auto Second = Loader.LoadSourceModule("void main(){print(43);}", Path);
			if (!Expect(Second && Loader.GetSourceFingerprint() != Fingerprint,
				"entry source changes affect the fingerprint even with a stale overlay")
				|| !RunProgram(*Second, "43\n") || !RunProgram(*First, "42\n"))
			{
				return false;
			}
			Loader.SetSourceOverlay(Path, "$");
			auto Valid = Loader.LoadSourceModule("void main(){print(44);}", Path);
			if (!Expect(static_cast<bool>(Valid), "stale invalid entry overlay cannot replace explicit source")
				|| !RunProgram(*Valid, "44\n"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestInMemoryEntryImport()
	{
		for (const std::string_view Extension : {".vs", ".vslib"})
		{
			const std::string Name = "main" + std::string(Extension);
			const std::string Path = "analysis-memory-entry/" + Name;
			VeroScriptModuleLoader Loader(Path);
			Loader.SetSourceOverlay("analysis-memory-entry/helper.vs",
				"import \"" + Name + "\";export int answer(){return seed()+2;}");
			const std::string Source = "import \"helper.vs\";export int seed(){return 40;}"
				"void main(){print(answer());}";
			auto Program = Loader.LoadSourceModule(Source, Path);
			if (!Expect(Program && !Loader.HasError(), "dependency resolves the in-memory entry without a duplicate overlay")
				|| !RunProgram(*Program, "42\n"))
			{
				return false;
			}
			std::string Resolved;
			if (!Expect(!Loader.ResolveImportPath(Name, "analysis-memory-entry", Resolved),
				"completed loads do not expose the entry as a permanent source overlay"))
			{
				return false;
			}
			Loader.SetAllowCircularImports(false);
			if (!Expect(!Loader.LoadSourceModule(Source, Path)
				&& Loader.GetError().MessageId == "diag.circular_import",
				"strict in-memory cycles report circular imports instead of missing files"))
			{
				return false;
			}
			Loader.SetAllowCircularImports(true);
			Loader.AddImplicitImportPath("helper.vs");
			auto Implicit = Loader.LoadSourceModule(Source, Path);
			if (!Expect(static_cast<bool>(Implicit), "implicit module can import the in-memory entry")
				|| !RunProgram(*Implicit, "42\n") || !RunProgram(*Program, "42\n"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestReuse()
	{
		VeroScriptModuleLoader Loader("reuse/main.vs");
		Loader.SetDefines({"RESULT=40"});
		Loader.SetSourceOverlay("reuse/value.vs", "export int value(){return RESULT;}\n");
		const std::string Source = "import \"value.vs\"; void main(){print(value());}";
		auto First = Loader.LoadSourceModule(Source, "reuse/main.vs");
		if (!Expect(static_cast<bool>(First), "first load succeeds"))
		{
			return false;
		}
		const auto FirstFingerprint = Loader.GetSourceFingerprint();
		Loader.SetDefines({"RESULT=42"});
		auto Second = Loader.LoadSourceModule(Source, "reuse/main.vs");
		if (!Expect(Second && !Loader.HasError()
			&& FirstFingerprint != Loader.GetSourceFingerprint(), "repeat load uses current configuration")
			|| !RunProgram(*First, "40\n") || !RunProgram(*Second, "42\n"))
		{
			return false;
		}
		if (!Expect(!Loader.LoadSourceModule("import \"missing.vs\";", "reuse/main.vs")
			&& Loader.HasError(), "failed load records diagnostics"))
		{
			return false;
		}
		Loader.SetSourceOverlay("reuse/value.vs", "export int value(){return 43;}\n");
		auto Third = Loader.LoadSourceModule(Source, "reuse/main.vs");
		if (!Expect(Third && !Loader.HasError() && Loader.GetDiagnostics().empty(),
			"successful retry clears errors and stale prepared modules") || !RunProgram(*Third, "43\n"))
		{
			return false;
		}
		try
		{
			int Checks = 0;
			FAnalysisControlScope Control([&Checks]()
										  {
											  return ++Checks == 5;
										  });
			(void)Loader.LoadSourceModule(Source, "reuse/main.vs");
			return Expect(false, "analysis cancellation must propagate");
		}
		catch (const FAnalysisCancelled&)
		{
		}
		auto Retried = Loader.LoadSourceModule(Source, "reuse/main.vs");
		if (!Expect(static_cast<bool>(Retried), "cancelled load can be retried")
			|| !RunProgram(*Retried, "43\n"))
		{
			return false;
		}
		Loader.SetSourceOverlay("reuse/main.vs", Source);
		auto FromMain = Loader.LoadMainModule();
		auto MainAgain = Loader.LoadMainModule();
		return Expect(FromMain && MainAgain, "file entry point also supports repeat loads")
			&& RunProgram(*FromMain, "43\n") && RunProgram(*MainAgain, "43\n");
	}

	bool TestAssociativity()
	{
		VeroScriptModuleLoader Loader("");
		auto Program = Loader.LoadSourceModule(
			"void main(){int a=0;int b=0;a=b=2; a+=b+=3;"
			"print(a);print(b);print(false?0:true?7:9);}", "associativity.vs");
		if (!Expect(static_cast<bool>(Program), "ordinary right-associative expressions parse")
			|| !RunProgram(*Program, "7\n5\n7\n"))
		{
			return false;
		}
		std::string Wide = "namespace one{namespace {int answer(){return 42;}}}"
			"void main(){array<int> values={";
		for (int Index = 0; Index < 1024; ++Index)
		{
			if (Index != 0)
			{
				Wide += ',';
			}
			Wide += "1+1";
		}
		Wide += "};print(len(values));print(one::answer());}";
		auto WideProgram = Loader.LoadSourceModule(Wide, "wide.vs");
		return Expect(static_cast<bool>(WideProgram), "wide initializers and ordinary namespaces remain valid")
			&& RunProgram(*WideProgram, "1024\n42\n");
	}

	int BenchmarkPreflight(const int Count)
	{
		std::string Source = "void main(){int x=0; bool result=";
		for (int Index = 0; Index < Count; ++Index)
		{
			Source += "x<";
		}
		Source += "x;}";
		VeroScriptLexer Lexer(Source, "comparison.vs");
		const auto& Tokens = Lexer.Tokenize();
		const auto Start = std::chrono::steady_clock::now();
		VeroScriptParser Parser(Tokens, "comparison.vs");
		(void)Parser.ParseProgram();
		const double Milliseconds = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - Start).count();
		std::cout << Count << ',' << Milliseconds << '\n';
		return Parser.HasError() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
}

int main(const int Argc, const char* const* Argv)
{
	const std::string_view Selection = Argc > 1 ? Argv[1] : "all";
	if (Selection == "--benchmark-preflight" && Argc == 3)
	{
		return BenchmarkPreflight(std::atoi(Argv[2]));
	}
	bool Ok = true;
	if (Selection == "all" || Selection == "assignment")
	{
		Ok = TestAssignment() && Ok;
	}
	if (Selection == "all" || Selection == "conditional")
	{
		Ok = TestConditional() && Ok;
	}
	if (Selection == "all" || Selection == "namespaces")
	{
		Ok = TestNamespaces() && Ok;
	}
	if (Selection == "all" || Selection == "initializers")
	{
		Ok = TestInitializers() && Ok;
	}
	if (Selection == "all" || Selection == "other-nesting")
	{
		Ok = TestOtherNesting() && Ok;
	}
	if (Selection == "all" || Selection == "preflight-cancellation")
	{
		Ok = TestPreflightCancellation() && Ok;
	}
	if (Selection == "all" || Selection == "imports")
	{
		Ok = TestImports() && Ok;
	}
	if (Selection == "all" || Selection == "import-boundaries")
	{
		Ok = TestImportBoundaries() && Ok;
	}
	if (Selection == "all" || Selection == "reuse")
	{
		Ok = TestReuse() && Ok;
	}
	if (Selection == "all" || Selection == "borrowed-load-arguments")
	{
		Ok = TestBorrowedLoadArguments() && Ok;
	}
	if (Selection == "all" || Selection == "import-order")
	{
		Ok = TestImportOrder() && Ok;
	}
	if (Selection == "all" || Selection == "mixed-nesting")
	{
		Ok = TestMixedNesting() && Ok;
	}
	if (Selection == "all" || Selection == "traversal-retry")
	{
		Ok = TestTraversalRetry() && Ok;
	}
	if (Selection == "all" || Selection == "source-labels")
	{
		Ok = TestSourceLabels() && Ok;
	}
	if (Selection == "all" || Selection == "entry-source-priority")
	{
		Ok = TestEntrySourcePriority() && Ok;
	}
	if (Selection == "all" || Selection == "in-memory-entry-import")
	{
		Ok = TestInMemoryEntryImport() && Ok;
	}
	if (Selection == "all" || Selection == "associativity")
	{
		Ok = TestAssociativity() && Ok;
	}
	return Ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
