// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAst.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompile.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPath.h"
#include "VeroScriptVm.h"
#include "../Processing/VeroScriptSourceMetadataIO.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
	int Failures = 0;
	void Check(bool OK, const char* Message)
	{
		if (!OK)
		{
			++Failures;
			std::cerr << "FAIL: " << Message << '\n';
		}
	}
	VeroScript::FCompileOptions Options()
	{
		VeroScript::FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.SourceOverlays = {
		    {"macro-tests/base.vs",
		        "#define PRIVATE 999\n#define_export VALUE 21\n#define_export ADD(x, ...) ((x) + VALUE "
		        "__VA_OPT__(+ __VA_ARGS__))\nexport int value() { return VALUE; }\n"},
		    {"macro-tests/facade.vs", "export import \"base.vs\";\n"},
		    {"macro-tests/ordinary.vs", "import \"base.vs\";\n"}};
		return O;
	}
	void Run(std::string Source, int Expected, VeroScript::FCompileOptions O = Options())
	{
		auto R = VeroScript::VeroScriptCompile::CompileSource(Source, "macro-tests/main.vs", O);
		if (!R.bSuccess)
		{
			std::cerr << R.Error.Message << '\n';
		}

		Check(R.bSuccess, "compile macro import program");
		if (!R.bSuccess)
		{
			return;
		}

		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		const bool OK = Vm.Execute(*R.Module, VeroScript::ERunMode::Script);
		Check(OK && Output.str() == std::to_string(Expected) + "\n", "macro import execution result");
	}
} // namespace

int main()
{
	using namespace VeroScript;
	{
		for (const auto* Source : {"  import \"\";", "  import \"bad\\0path.vs\";"})
		{
			const auto Result = VeroScriptCompile::CompileSource(Source, "macro-tests/invalid-path.vs");
			Check(!Result.bSuccess && Result.Error.bPhysicalLocation &&
			        Result.Error.Location.Line == 1 && Result.Error.Location.Column == 3,
			    "invalid import path keeps preprocessor diagnostic through module loading");
		}
	}
	{
		std::vector<FPublicMacro> LargeInterface;
		for (int I = 0; I < 15; ++I)
		{
			FPublicMacro M;
			M.Name = "BIG_" + std::to_string(10 + I);
			M.Body.assign(MaxPublicMacroBodyBytes, '1');
			M.OriginModule = "macro-tests/budget.vs";
			M.DefinitionOrdinal = I + 1;
			LargeInterface.push_back(std::move(M));
		}
		FPreprocessContext Context;
		Context.LogicalPath = "macro-tests/main.vs";
		Context.Mode = EPreprocessMode::Module;
		FPreprocessorSession Session(
		    "import \"budget.vs\";\nimport \"budget.vs\";\n#undef BIG_10\n"
		    "import \"replacement.vs\";\nimport \"overflow.vs\";\n",
		    {}, {}, Context);
		Check(Session.Advance() == EPreprocessStep::NeedModule &&
		        Session.ResumeImport("budget.vs", LargeInterface),
		    "initial imported macro budget fits");
		Check(Session.Advance() == EPreprocessStep::NeedModule &&
		        Session.ResumeImport("budget.vs", LargeInterface),
		    "duplicate import does not consume budget");
		FPublicMacro Replacement = LargeInterface.front();
		Replacement.Name = "REPLACEMENT";
		Replacement.OriginModule = "macro-tests/replacement.vs";
		Check(Session.Advance() == EPreprocessStep::NeedModule &&
		        Session.ResumeImport("replacement.vs", {Replacement}),
		    "undef releases imported macro budget");
		Replacement.Name = "OVERFLOW";
		Check(Session.Advance() == EPreprocessStep::NeedModule &&
		        !Session.ResumeImport("overflow.vs", {Replacement}) &&
		        Session.Advance() == EPreprocessStep::Error,
		    "combined imported macro table remains bounded after incremental accounting");
	}
	{
		const auto Root = std::filesystem::temp_directory_path() /
		    ("vero-macro-snapshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		const auto Search = Root / "search";
		std::filesystem::create_directories(Search);
		const auto Shadow = Root / "dependency.vs";
		const auto Fallback = Search / "dependency.vs";
		const auto WriteModule = [](const auto& Path, int Value)
		{
			std::ofstream Out(Path);
			Out << "#define_export VALUE " << Value << "\nexport int value(){return VALUE;}\n";
		};
		WriteModule(Fallback, 21);
		const auto SearchPath = VeroScript::PathUtil::ToUtf8(Search);
		const auto EntryPath = VeroScript::PathUtil::ToUtf8(Root / "main.vs");
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.ImportPaths = {SearchPath};
		const auto* Source = "import \"dependency.vs\"; int main(){print(VALUE + value());return 0;}";
		FCompileResult First;
		bool Created = false;
		{
			FAnalysisControlScope Scope(
			    []
			{
				return false;
			},
			    [&](std::string_view Stage, double)
			{
				if (Stage == "parse" && !Created)
				{
					// An edit after dependency preparation must affect the next compile.
					WriteModule(Shadow, 22);
					Created = true;
				}
			});
			First = VeroScriptCompile::CompileSource(Source, EntryPath, O);
		}
		Check(Created && First.bSuccess, "import resolution remains bound to the prepared source snapshot");
		if (!First.bSuccess)
		{
			std::cerr << First.Error.Message << '\n';
		}

		for (int I = 0; I < 2; ++I)
		{
			auto Result = I == 0 ? std::move(First) : VeroScriptCompile::CompileSource(Source, EntryPath, O);
			std::ostringstream Output;
			auto Vm = std::make_unique<VeroScriptVm>(Output);
			VeroScript::Test::Configure(*Vm);
			Check(Result.bSuccess && Vm->Execute(*Result.Module, ERunMode::Script) &&
			        Output.str() == (I == 0 ? "42\n" : "44\n"),
			    "one compile uses one macro/declaration snapshot and the next compile sees edits");
		}
		std::error_code EC;
		std::filesystem::remove(Shadow, EC);
		std::filesystem::remove(Fallback, EC);
		std::filesystem::remove(Search, EC);
		std::filesystem::remove(Root, EC);
	}
	Run("import \"base.vs\"; int main() { print(ADD(VALUE)); return 0; }", 42);
	Run("import r\"base.vs\"; int main() { print(VALUE); return 0; }", 21);
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.SourceOverlays = {{"macro-tests/hash.vs", "#define_export HASH #\n"}};
		Run("import \"hash.vs\";\n#define INNER_STRING(x) #x\n#define STRING(x) INNER_STRING(x)\n"
		    "int main(){print(STRING(HASH) == \"#\" ? 42 : 0);return 0;}",
		    42, O);
	}
	Run("import\n\"facade.vs\"\n#ifdef PRIVATE\n#error private macro leaked\n#endif\nint main() { "
	    "print(ADD(1, 20)); return 0; }",
	    42);
	Run("#include \"base.vs\"\nint main() { print(VALUE); return 0; }", 21);
	Run("#define LOAD import \"base.vs\"; int answer = VALUE;\nLOAD\nint main() { print(answer); "
	    "return 0; }",
	    21);
	Run("#define LOAD(x) import x; int answer = VALUE;\nLOAD(\"base.vs\")\nint main() { "
	    "print(answer); return 0; }",
	    21);
	Run("#define IGNORE(x)\nIGNORE(import \"missing.vs\")\nint main() { print(42); return 0; }",
	    42);
	Run("#define ID(x) x\nID(import \"base.vs\";) int main(){print(VALUE);return 0;}", 21);
	Run("#define CAT(x,y) x ## y\nCAT(im,port) \"base.vs\"; int main(){print(VALUE);return 0;}", 21);
	Run("import \"base.vs\";\n#undef VALUE\nimport \"base.vs\"; int main() { print(VALUE); return "
	    "0; }",
	    21);
	Run("export import \"base.vs\"; import \"facade.vs\"; int main() { print(VALUE); return 0; }",
	    21);
	{
		// Only a complete emitted top-level import head may request a module.
		for (const auto* Source :
		    {"void f(){import \"missing.vs\";}", "x = import \"missing.vs\";",
		        "@tag import \"missing.vs\";", "template<T> import \"missing.vs\";",
		        "static import \"missing.vs\";", "import export import \"missing.vs\";",
		        "#define DROP(x)\nDROP(import \"missing.vs\")", "\"import \\\"missing.vs\\\"\";",
		        "#define TEXT(x) #x\nTEXT(import \"missing.vs\")",
		        "#define LOAD import \"missing.vs\"\n#if LOAD\n#endif"})
		{
			FPreprocessContext Context;
			Context.Mode = EPreprocessMode::Module;
			int Reads = 0;
			Context.ResolveModule =
			    [&](FPreprocessImport&, std::vector<FPublicMacro>&, std::string&)
			{
				++Reads;
				return true;
			};
			std::string Out, Error;
			PreprocessSource(Source, {}, {}, Context, Out, Error);
			Check(Reads == 0, "invalid or discarded import performs no module I/O");
		}
		FPreprocessResult Result;
		FPreprocessContext Context;
		Context.Result = &Result;
		std::string Out, Error;
		Check(PreprocessSource("#define_export X 1\nimport \"missing.vs\";", {}, {}, Context, Out,
		          Error) &&
		        Result.bComplete && !Result.bModuleImportsResolved &&
		        Result.PublicMacros.size() == 1,
		    "standalone processing preserves imports and reports unresolved interface");
		Context.Mode = EPreprocessMode::Module;
		Check(!PreprocessSource("#define_export X 1\nimport \"missing.vs\";", {}, {}, Context, Out,
		          Error) &&
		        !Result.bComplete && Result.PublicMacros.empty() &&
		        Result.Diagnostic.bPhysicalLocation &&
		        !Result.Diagnostic.Message.empty(),
		    "module preprocessing without resolver fails without publishing partial interface");
		FPreprocessorSession Session("\n\n\n  import \"library.vs\";", {}, {}, Context);
		Check(Session.Advance() == EPreprocessStep::NeedModule, "suspend before invalid interface");
		FPublicMacro Invalid;
		Invalid.Name = "VALUE";
		Invalid.Body = "42";
		Invalid.OriginModule = "library.vs";
		Check(!Session.ResumeImport("library.vs", {Invalid}) &&
		        Session.Advance() == EPreprocessStep::Error && Session.ErrorLine() == 4 &&
		        Session.Result().Diagnostic.Location.Line == 4 &&
		        Session.Result().Diagnostic.Location.Column == 3,
		    "invalid imported interface reports the pending import position");
		Context.ResolveModule = [&](FPreprocessImport& Import, std::vector<FPublicMacro>& Macros,
		                            std::string&)
		{
			Import.ResolvedPath = "library.vs";
			Macros = {Invalid};
			return true;
		};
		std::uint32_t ErrorLine = 0;
		Check(!PreprocessSource("\n\n\n  import \"library.vs\";", {}, {}, Context, Out, Error,
		          &ErrorLine) &&
		        ErrorLine == 4 && Result.Diagnostic.Location.Line == 4 &&
		        Result.Diagnostic.Location.Column == 3,
		    "preprocessor wrapper preserves the import failure position");
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.SourceOverlays = {
		    {"macro-tests/helpers.vs", "#define PRIVATE_HELPER 999\n#define_export USE_HELPER "
		                               "PRIVATE_HELPER\n#define_export CALL_LINE __LINE__\n"}};
		Run("import \"helpers.vs\";\n#define PRIVATE_HELPER 40\nint main(){print(USE_HELPER + "
		    "CALL_LINE - 1);return 0;}\n",
		    42, O);
		O.ImplicitImportPaths = {"macro-tests/helpers.vs"};
		Run("#ifdef PRIVATE_HELPER\n#error private macro leaked\n#endif\n"
		    "#define PRIVATE_HELPER 40\nint main(){print(USE_HELPER + CALL_LINE - 3);return 0;}",
		    42, O);
		auto R = VeroScriptCompile::CompileSource("int main(){return CALL_LINE;}", "macro-tests/main.vs", O);
		Check(R.bSuccess && R.Module->PublicMacros.empty(), "implicit imports do not reexport public macros");
		O.SourceOverlays = {{"macro-tests/source.vslib", "#define_export VALUE 42\n"}};
		O.ImplicitImportPaths.clear();
		Run("import \"source.vslib\"; int main(){print(VALUE);return 0;}", 42, O);
	}
	{
		auto O = Options();
		O.ImplicitImportPaths = {"macro-tests/facade.vs", "macro-tests/base.vs", "macro-tests/base.vs"};
		O.SourceOverlays.push_back({"macro-tests/consumer.vs",
		    "#ifdef PRIVATE\n#error private macro leaked\n#endif\n"
		    "export int consume(){return ADD(VALUE);}"});
		Run("import \"consumer.vs\"; int main(){print(consume());return 0;}", 42, O);
		Run("#undef VALUE\n#define VALUE 42\nint main(){print(VALUE);return 0;}", 42, O);
		Run("#undef VALUE\nimport \"base.vs\"; int main(){print(ADD(VALUE));return 0;}", 42, O);
		auto R = VeroScriptCompile::CompileSource("int main(){return VALUE;}", "macro-tests/main.vs", O);
		Check(R.bSuccess, "complete implicit interface is available from the first source token");
		if (R.bSuccess)
		{
			VeroScriptModuleLoader Loader("macro-tests/main.vs");
			for (const auto& P : O.ImplicitImportPaths)
			{
				Loader.AddImplicitImportPath(std::string(P));
			}
			for (const auto& Overlay : O.SourceOverlays)
			{
				Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
			}
			Check(bool(Loader.LoadSourceModule("int main(){return VALUE;}", "macro-tests/main.vs")),
			    "prepare implicit macro fingerprint");
			const auto Fingerprint = Loader.GetSourceFingerprint();
			O.SourceOverlays.front().Source = "#define_export VALUE 22\n#define_export ADD(x) ((x) + VALUE)\n";
			Loader.SetSourceOverlay(O.SourceOverlays.front().Path, O.SourceOverlays.front().Source);
			auto Changed = Loader.LoadSourceModule("int main(){return VALUE;}", "macro-tests/main.vs");
			Check(Changed && Loader.GetSourceFingerprint() != Fingerprint,
			    "implicit macro source changes invalidate the source fingerprint");
			Run("int main(){print(ADD(VALUE));return 0;}", 44, O);
		}
		O.SourceOverlays.push_back({"macro-tests/conflict.vs", "#define_export VALUE 22\n"});
		O.ImplicitImportPaths.push_back("macro-tests/conflict.vs");
		R = VeroScriptCompile::CompileSource("int main(){return 0;}", "macro-tests/main.vs", O);
		Check(!R.bSuccess && R.Error.bPhysicalLocation && R.Error.Location.Line == 1 &&
		        R.Error.Message.find("conflicting imported macro") != std::string::npos,
		    "conflicting implicit macro origins are rejected at the consumer entry");
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.SourceOverlays = {{"macro-tests/a.vs", "#define_export A 21\n"},
		    {"macro-tests/b.vs", "import \"a.vs\";\n#define_export B(x) ((x) + A)\n"}};
		O.ImplicitImportPaths = {"macro-tests/b.vs", "macro-tests/a.vs"};
		Run("int main(){print(B(A));return 0;}", 42, O);
		O.Defines = {"A=21"};
		auto R = VeroScriptCompile::CompileSource("int main(){return 0;}", "macro-tests/main.vs", O);
		Check(!R.bSuccess, "host predefined macros retain ordinary import conflict rules");
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.ImplicitImportPaths = {"macro-tests/globals.vs"};
		O.SourceOverlays = {{"macro-tests/globals.vs", "#define_export ENABLED 1\n#define_export ANSWER 42\n"},
		    {"macro-tests/a.vs", "#if ENABLED\nimport \"b.vs\";\n#else\n#error missing implicit macro\n#endif\n"
		                         "export int from_a(){return from_b();}"},
		    {"macro-tests/b.vs", "import \"a.vs\"; export int from_b(){return ANSWER;}"}};
		Run("import \"a.vs\"; int main(){print(from_a());return 0;}", 42, O);
	}
	{
		auto O = Options();
		auto Provider = VeroScriptCompile::CompileSource(O.SourceOverlays.front().Source,
		    "macro-tests/base.vs", O);
		Check(Provider.bSuccess, "prepare implicit preprocessor interface");
		if (Provider.bSuccess)
		{
			FPreprocessContext Context{"macro-tests/main.vs"};
			Context.Mode = EPreprocessMode::Module;
			FPreprocessorSession S("int x=VALUE;", {}, {}, Context, true);
			Check(S.ImportImplicitMacros(Provider.Module->PublicMacros) &&
			        S.ImportImplicitMacros(Provider.Module->PublicMacros) && S.Advance() == EPreprocessStep::Complete &&
			        S.Source() == "int x=21;" && S.Result().Imports.empty() && S.Result().PublicMacros.empty(),
			    "implicit macro seeding is idempotent and adds no synthetic source or export edges");
			bool Mapped = false;
			for (const auto& M : S.LineMap().ActiveMacros)
			{
				if (M.Name == "VALUE")
				{
					Mapped = M.bImported && !M.bPublic && M.ActivationOffset == 0 &&
					    M.ActivationLine == 1 && M.ActivationColumn == 1 && M.SourceName == "macro-tests/base.vs";
				}
			}
			Check(Mapped && S.LineMap().OutByteToPhysical.size() == S.Source().size(),
			    "implicit macro source locations and activation timeline remain available to tooling");
			Check(!S.ImportImplicitMacros(Provider.Module->PublicMacros), "implicit seeding requires an unstarted session");
		}
	}
	{
		// Every suspended macro rescan remains on the heap while imports descend.
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		for (int I = 0; I < 110; ++I)
		{
			std::string Source;
			if (I < 109)
			{
				for (int N = 0; N < 480; ++N)
				{
					Source += "#define M" + std::to_string(N) + " M" + std::to_string(N + 1) + "\n";
				}

				Source += "#define M480 export import \"d" + std::to_string(I + 1) + ".vs\";\nM0\n";
			}
			else
			{
				Source = "#define_export DEEP 42\n";
			}

			O.SourceOverlays.push_back({"macro-tests/d" + std::to_string(I) + ".vs", std::move(Source)});
		}
		Run("import \"d0.vs\"; int main(){print(DEEP);return 0;}", 42, std::move(O));
	}
	{
		auto O = Options();
		O.SourceOverlays.push_back({"macro-tests/other.vs", "#define_export VALUE 21\n"});
		for (const auto& Source :
		    {"import \"base.vs\"; import \"other.vs\";", "#define VALUE 21\nimport \"base.vs\";",
		        "import \"base.vs\";\n#define VALUE 21\n"})
		{
			auto R = VeroScriptCompile::CompileSource(Source, "macro-tests/main.vs", O);
			Check(!R.bSuccess, "distinct origins and local redefinition conflict");
		}
		Run("import \"base.vs\";\n#undef VALUE\n#define VALUE 42\nint main(){print(VALUE);return "
		    "0;}",
		    42, O);
	}
	{
		VeroScriptModuleLoader L("");
		L.SetBuildPreprocessLineMaps(true);
		L.AddImportPath("macro-tests/search");
		Check(!L.LoadSourceModule("#define LOCAL 42\nimport \"missing.vs\";", "macro-tests/main.vs"),
		    "missing dependency rejects module");
		const auto Partial = L.GetPreprocessLineMap("macro-tests/main.vs");
		Check(Partial && !Partial->PreprocessedSource.empty() && !Partial->ActiveMacros.empty(),
		    "failed module retains emitted prefix and macro timeline for tooling");
		bool WatchesSearchCandidate = false;
		for (const auto& Path : L.GetLoadedModulePaths())
		{
			WatchesSearchCandidate |= Path.find("macro-tests/search/missing.vs") != std::string::npos;
		}

		Check(
		    WatchesSearchCandidate, "failed import retains missing search-path candidate for watch recovery");
		L.SetSourceOverlay("macro-tests/base.vs", "#define_export VALUE 21\n");
		for (const auto& Source :
		    {"import \"base.vs\";", "export import \"base.vs\";\n#undef VALUE\n",
		        "#define_export VALUE 1\n#define VALUE 2\n",
		        "#define_export VALUE 1\n#undef VALUE\n"})
		{
			auto P = L.LoadSourceModule(Source, "macro-tests/main.vs");
			Check(P && P->PublicMacros.empty(), "root final public table is authoritative");
		}
		auto P = L.LoadSourceModule("export import \"base.vs\"; import \"base.vs\";",
		    "macro-tests/main.vs");
		Check(P && P->PublicMacros.size() == 1, "ordinary repeat preserves reexport visibility");
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.SourceOverlays = {{"macro-tests/a.vs", "import \"b.vs\";\nexport int a(){return 1;}"},
		    {"macro-tests/b.vs", "import \"a.vs\";\nexport int b(){return 2;}"}};
		Run("import \"a.vs\"; int main(){print(a());return 0;}", 1, O);
		O.SourceOverlays[1].Source = "#if 0\n#define_export UNUSED 1\n#endif\nimport \"a.vs\";";
		auto R = VeroScriptCompile::CompileSource("import \"a.vs\";", "macro-tests/main.vs", O);
		Check(!R.bSuccess && R.Error.Message.find("public macro interface") != std::string::npos,
		    "uncertain cyclic macro interface rejected");
	}
	{
		// A packaged interface survives deletion of all original source files.
		auto O = Options();
		O.SourceOverlays.front().Source =
		    "#define_export VALUE 21\n#define_export ADD(x, ...) ((x) + VALUE __VA_OPT__(+ __VA_ARGS__))\n"
		    "int initialized = 0;\ninitialized += 1;\n"
		    "export int value(){return VALUE + initialized - 1;}\n";
		auto Library = VeroScriptCompile::CompileSource("export import \"base.vs\";",
		    "macro-tests/library.vs", O);
		Check(Library.bSuccess && Library.Module->PublicMacros.size() == 2,
		    "library compiled public table");
		if (Library.bSuccess)
		{
			FPackageBuildOptions Build;
			Build.Kind = EPackageKind::Library;
			Build.PackageName = "macro-lib";
			Build.PackageVersion = 7;
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			Check(BuildPackage(*Library.Module, Build, Bytes, Error),
			    "build macro library package");
			FPackageData Package;
			Check(ReadPackage(FByteView(Bytes), Package, Error), "read macro library package");
			{
				auto InvalidBytes = Bytes;
				const std::string Origin = "<vslib:macro-lib@7:macro-";
				const auto Start = std::search(InvalidBytes.begin(), InvalidBytes.end(),
				    Origin.begin(), Origin.end());
				Check(Start != InvalidBytes.end(), "portable origin is present in macro library");
				if (Start != InvalidBytes.end())
				{
					const std::string Unowned = "macro-bad";
					std::copy(Unowned.begin(), Unowned.end(), Start + 7);
					FPackageData Invalid;
					Check(!ReadPackage(FByteView(InvalidBytes), Invalid, Error),
					    "package reader rejects an unlinked macro owner");
				}
			}
			for (const auto* Origin : {"", "<vslib:broken>",
			         "<vslib:absent@7:macro-0123456789abcdef>",
			         "<vslib:macro-lib@07:macro-0123456789abcdef>"})
			{
				FBytecodeModule Invalid = *Library.Module;
				Invalid.PublicMacros.front().OriginModule = Origin;
				std::vector<std::uint8_t> InvalidBytes{1};
				Check(!BuildPackage(Invalid, Build, InvalidBytes, Error) &&
				        InvalidBytes.empty() && !Error.empty(),
				    "package writer rejects invalid or unowned macro provenance");
			}
			Check(Package.Module &&
			        Package.Module->PublicMacros[0].SourceName.empty() == Build.bStripDebugInfo,
			    "library debug-location stripping follows build option");
			const auto Path =
			    std::filesystem::temp_directory_path() / "vero-macro-module-tests.vslib";
			{
				std::ofstream Out(Path, std::ios::binary);
				Out.write(reinterpret_cast<const char*>(Bytes.data()),
				    static_cast<std::streamsize>(Bytes.size()));
			}
			const auto UTF8 = VeroScript::PathUtil::ToUtf8(Path);
			Run("import \"" + UTF8 + "\"; int main(){print(ADD(value()));return 0;}", 42, {});
			{
				FCompileOptions ImplicitLibrary = VeroScript::Test::MakeCompileOptions();
				ImplicitLibrary.ImplicitImportPaths = {UTF8};
				Run("int main(){print(ADD(value()));return 0;}", 42, ImplicitLibrary);
			}
			{
				auto BinaryRoot = VeroScriptCompile::CompileFile(UTF8);
				Check(BinaryRoot.bSuccess && BinaryRoot.Module->PublicMacros.size() == 2 &&
				        SamePublicMacroDefinition(Package.Module->PublicMacros.front(),
				            BinaryRoot.Module->PublicMacros.front()),
				    "binary library root retains its public macro interface");
				if (BinaryRoot.bSuccess)
				{
					FPackageBuildOptions RootBuild = Build;
					RootBuild.PackageName = "macro-binary-root";
					std::vector<std::uint8_t> RootBytes;
					FPackageData RootPackage;
					const bool Built = BuildPackage(*BinaryRoot.Module, RootBuild, RootBytes, Error) &&
					    ReadPackage(FByteView(RootBytes), RootPackage, Error);
					Check(Built && RootPackage.Module->PublicMacros.size() == 2 &&
					        SamePublicMacroDefinition(Package.Module->PublicMacros.front(),
					            RootPackage.Module->PublicMacros.front()),
					    "repackaged binary root keeps original macro ownership and identity");
					if (Built)
					{
						const auto RootPath = VeroScript::PathUtil::ToUtf8(Path) + ".root.vslib";
						{
							std::ofstream Out(RootPath, std::ios::binary);
							Out.write(reinterpret_cast<const char*>(RootBytes.data()),
							    static_cast<std::streamsize>(RootBytes.size()));
						}
						Run("import \"" + RootPath + "\"; import \"" + UTF8 +
						        "\"; int main(){print(ADD(value()));return 0;}",
						    42, {});
						Run("import \"" + UTF8 + "\"; import \"" + RootPath +
						        "\"; int main(){print(ADD(value()));return 0;}",
						    42, {});
						std::error_code EC;
						std::filesystem::remove(RootPath, EC);
					}
				}
				FCompileOptions ImplicitRoot = VeroScript::Test::MakeCompileOptions();
				ImplicitRoot.ImplicitImportPaths = {UTF8};
				auto ImplicitBinary = VeroScriptCompile::CompileFile(UTF8, ImplicitRoot);
				Check(ImplicitBinary.bSuccess && ImplicitBinary.Module->PublicMacros.size() == 2,
				    "binary root interface survives implicit-cohort preparation");
			}
			{
				auto GenericOptions = O;
				GenericOptions.SourceOverlays.front().Source +=
				    "export T identity<T>(T x){return x;}\n";
				auto Generic = VeroScriptCompile::CompileSource("export import \"base.vs\";",
				    "macro-tests/generic-library.vs", GenericOptions);
				FPackageBuildOptions GenericBuild = Build;
				GenericBuild.PackageName = "macro-generic";
				std::vector<std::uint8_t> GenericBytes;
				const bool Built = Generic.bSuccess &&
				    BuildPackage(*Generic.Module, GenericBuild, GenericBytes, Error);
				Check(Built, "build original generic macro library");
				if (Built)
				{
					const auto GenericPath = UTF8 + ".generic.vslib";
					{
						std::ofstream Out(GenericPath, std::ios::binary);
						Out.write(reinterpret_cast<const char*>(GenericBytes.data()),
						    static_cast<std::streamsize>(GenericBytes.size()));
					}
					auto Root = VeroScriptCompile::CompileFile(GenericPath);
					GenericBuild.PackageName = "macro-generic-root";
					std::vector<std::uint8_t> RootBytes;
					const bool Repacked = Root.bSuccess &&
					    BuildPackage(*Root.Module, GenericBuild, RootBytes, Error);
					Check(Repacked, "repackage binary root with retained generic templates");
					if (Repacked)
					{
						const auto RootPath = GenericPath + ".root.vslib";
						{
							std::ofstream Out(RootPath, std::ios::binary);
							Out.write(reinterpret_cast<const char*>(RootBytes.data()),
							    static_cast<std::streamsize>(RootBytes.size()));
						}
						Run("import \"" + RootPath + "\"; import \"" + GenericPath +
						        "\"; int main(){print(ADD(identity<int>(value())));return 0;}",
						    42, {});
						std::error_code EC;
						std::filesystem::remove(RootPath, EC);
					}
					std::error_code EC;
					std::filesystem::remove(GenericPath, EC);
				}
			}
			auto Facade = VeroScriptCompile::CompileSource("export import \"" + UTF8 + "\";",
			    "macro-tests/frozen-facade.vs");
			Check(Facade.bSuccess, "compile facade from binary macro interface");
			if (Facade.bSuccess)
			{
				FPackageBuildOptions FacadeBuild = Build;
				FacadeBuild.PackageName = "macro-facade";
				std::vector<std::uint8_t> FacadeBytes;
				Check(BuildPackage(*Facade.Module, FacadeBuild, FacadeBytes, Error),
				    "repackage dependency macro interface");
				FPackageData Repacked;
				Check(ReadPackage(FByteView(FacadeBytes), Repacked, Error) &&
				        SamePublicMacroDefinition(Package.Module->PublicMacros[0],
				            Repacked.Module->PublicMacros[0]),
				    "repackaged dependency macro retains original identity");
				const auto FacadePath = Path.string() + ".facade.vslib";
				{
					std::ofstream Out(FacadePath, std::ios::binary);
					Out.write(reinterpret_cast<const char*>(FacadeBytes.data()),
					    static_cast<std::streamsize>(FacadeBytes.size()));
				}
				Run("import \"" + UTF8 + "\"; import \"" +
				        VeroScript::PathUtil::ToUtf8(std::filesystem::path(FacadePath)) +
				        "\"; int main(){print(ADD(value()));return 0;}",
				    42, {});
				Run("import \"" + VeroScript::PathUtil::ToUtf8(std::filesystem::path(FacadePath)) +
				        "\"; import \"" + UTF8 + "\"; int main(){print(ADD(value()));return 0;}",
				    42, {});
				std::error_code EC;
				std::filesystem::remove(FacadePath, EC);
			}
			std::error_code EC;
			std::filesystem::remove(Path, EC);
			std::vector<std::uint8_t> Cache;
			Check(VeroScriptBytecodeIO::SerializeBytecode(*Library.Module, 123, Cache, Error),
			    "serialize macro cache");
			auto Read = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Cache), 123, Error);
			Check(Read && Read->PublicMacros.size() == 2, "macro cache roundtrip");
			{
				FBytecodeModule WithLargeMetadata = *Library.Module;
				auto Metadata = std::make_shared<FSourceMetadataSnapshot>(*Library.Module->SourceMetadata);
				Metadata->Documents.front().Text.append(17 * 1024 * 1024, ' ');
				WithLargeMetadata.SourceMetadata = std::move(Metadata);
				std::vector<std::uint8_t> LargeCache;
				Check(VeroScriptBytecodeIO::SerializeBytecode(WithLargeMetadata, 123, LargeCache, Error),
				    "serialize source metadata larger than the macro table budget");
				const auto LargeRead = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(LargeCache), 123, Error);
				Check(LargeRead && LargeRead->PublicMacros.size() == 2 &&
				        LargeRead->SourceMetadata->Documents.front().Text.size() > MaxPublicMacroTableBytes,
				    "macro table budget excludes the following source metadata section");
			}
			if (Read)
			{
				std::vector<std::uint8_t> MetadataBytes;
				Check(Read->SourceMetadata && EncodeSourceMetadata(*Read->SourceMetadata, MetadataBytes, Error), "source metadata footer encodes");
				std::vector<std::uint8_t> Header;
				const auto U32 = [&](const std::uint32_t Value)
				{
					for (unsigned Shift = 0; Shift < 32; Shift += 8)
					{
						Header.push_back(static_cast<std::uint8_t>(Value >> Shift));
					}
				};
				U32(static_cast<std::uint32_t>(Read->PublicMacros.size()));
				U32(static_cast<std::uint32_t>(Read->PublicMacros.front().Name.size()));
				Header.insert(Header.end(), Read->PublicMacros.front().Name.begin(), Read->PublicMacros.front().Name.end());
				const auto Table = std::search(Cache.begin(), Cache.end(), Header.begin(), Header.end());
				Check(Table != Cache.end(), "serialized public macro table is present");
				if (Table == Cache.end()) return EXIT_FAILURE;
				const auto Tail = static_cast<std::size_t>(Table - Cache.begin());
				auto BadFlags = Cache;
				BadFlags[Tail + 8 + Read->PublicMacros.front().Name.size()] = 2;
				Check(!VeroScriptBytecodeIO::DeserializeBytecode(FByteView(BadFlags), 123, Error),
				    "noncanonical macro bool flag rejected");
				auto BadCount = Cache;
				BadCount[Tail] = 1;
				BadCount[Tail + 1] = 0;
				BadCount[Tail + 2] = 1;
				BadCount[Tail + 3] = 0;
				Check(!VeroScriptBytecodeIO::DeserializeBytecode(FByteView(BadCount), 123, Error),
				    "oversized macro count rejected before reserve");
				auto OldMagic = Cache;
				OldMagic[3] = 2;
				Check(!VeroScriptBytecodeIO::DeserializeBytecode(FByteView(OldMagic), 123, Error),
				    "old bytecode layout rejected");
				FPreprocessContext Context;
				Context.LogicalPath = "macro-tests/main.vs";
				Context.Mode = EPreprocessMode::Module;
				FPreprocessorSession Session("#define VALUE 21\nimport \"base.vs\";", {}, {}, Context, true);
				Check(Session.Advance() == EPreprocessStep::NeedModule && !Session.ResumeImport("macro-tests/base.vs", Read->PublicMacros) && Session.Advance() == EPreprocessStep::Error, "whole import batch conflict fails session");
				bool Partial = false;
				for (const auto& Event : Session.LineMap().ActiveMacros)
				{
					Partial |= Event.bImported;
				}

				Check(!Partial && Session.Result().PublicMacros.empty(),
				    "failed batch exposes no partial macro events");
			}
			for (bool Strip : {false, true})
			{
				auto AppModule = VeroScriptCompile::CompileSource(
				    "#define_export APP_VALUE 42\nint main(){return 0;}", "macro-tests/app.vs");
				FPackageBuildOptions App;
				App.PackageName = "macro-app";
				App.bStripDebugInfo = Strip;
				std::vector<std::uint8_t> AppBytes;
				FPackageData AppData;
				Check(
				    AppModule.bSuccess && BuildPackage(*AppModule.Module, App, AppBytes, Error) &&
				        ReadPackage(FByteView(AppBytes), AppData, Error) &&
				        AppData.Module->PublicMacros.empty() &&
				        AppModule.Module->PublicMacros.size() == 1,
				    "application package clears copied interface without mutating compiled module");
			}
			if (!Cache.empty())
			{
				Cache.resize(Cache.size() - 1);
				Check(!VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Cache), 123, Error),
				    "truncated macro tail rejected");
			}
		}
	}
	std::cout << "VeroScriptMacroModuleTests: " << Failures << " failure(s)\n";
	return Failures ? 1 : 0;
}
