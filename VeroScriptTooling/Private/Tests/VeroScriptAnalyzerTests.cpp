// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptPath.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptAnalyzerTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool EndsWith(const std::string& Value, const std::string& Suffix)
	{
		return Value.size() >= Suffix.size()
			&& Value.compare(Value.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
	}
}

int main()
{
	using namespace VeroScript::Tooling;

	{
		const FAnalyzeResult Ok = VeroScriptAnalyzer::AnalyzeSource(
			"void main() { print(1); }",
			"<ok>");
		if (!Expect(Ok.bSuccess, "valid source should analyze successfully"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Ok.Diagnostics.empty(), "valid source should have no diagnostics"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const FAnalyzeResult InternalBuiltin = VeroScriptAnalyzer::AnalyzeSource(
			"void main() { print(__sqrt(4.0)); }",
			"<internal-builtin>");
		if (!Expect(
			!InternalBuiltin.bSuccess,
			"internal builtins should be rejected in ordinary source"))
		{
			return EXIT_FAILURE;
		}

		bool bFoundReservedDiagnostic = false;
		for (const FDiagnostic& Diagnostic : InternalBuiltin.Diagnostics)
		{
			if (Diagnostic.Code
					== "diag.internal_builtin_is_reserved_for_standard_library_implementation"
				|| Diagnostic.Message.find("reserved for standard library implementation")
					!= std::string::npos)
			{
				bFoundReservedDiagnostic = true;
				break;
			}
		}
		if (!Expect(
			bFoundReservedDiagnostic,
			"internal builtin rejection should preserve the core diagnostic"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const FAnalyzeResult GlobalNamespaceOnly = VeroScriptAnalyzer::AnalyzeSource(
			"void main() { int value = read_int(); print(value); }",
			"<namespace-only-global>");
		if (!Expect(
			!GlobalNamespaceOnly.bSuccess,
			"namespace-owned standard functions should be rejected globally"))
		{
			return EXIT_FAILURE;
		}

		bool bFoundNamespaceDiagnostic = false;
		for (const FDiagnostic& Diagnostic : GlobalNamespaceOnly.Diagnostics)
		{
			if (Diagnostic.Code == "diag.function_requires_its_standard_namespace")
			{
				bFoundNamespaceDiagnostic = true;
				break;
			}
		}
		if (!Expect(
			bFoundNamespaceDiagnostic,
			"global namespace-only call should preserve the standard namespace diagnostic"))
		{
			return EXIT_FAILURE;
		}

		const FAnalyzeResult QualifiedNamespaceOnly = VeroScriptAnalyzer::AnalyzeSource(
			"import \"io.vs\";\n"
			"void main() { int value = std::io::read_int(); print(value); }",
			"<namespace-only-qualified>");
		if (!Expect(
			QualifiedNamespaceOnly.bSuccess,
			"qualified namespace-only standard function should analyze successfully"))
		{
			for (const FDiagnostic& Diagnostic : QualifiedNamespaceOnly.Diagnostics)
			{
				std::cerr << Diagnostic.Code << ": " << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path Utf8Root =
			std::filesystem::temp_directory_path()
			/ VeroScript::PathUtil::FromUtf8(
				"veroscript-tooling-\xE4\xB8\xAD\xE6\x96\x87");
		std::error_code Error;
		std::filesystem::remove_all(Utf8Root, Error);
		std::filesystem::create_directories(Utf8Root, Error);
		const std::filesystem::path Utf8File =
			Utf8Root / VeroScript::PathUtil::FromUtf8(
				"\xE6\x96\x87\xE4\xBB\xB6.vs");
		{
			std::ofstream Output(Utf8File, std::ios::binary);
			Output << "void main() { print(1); }\n";
		}
		const FAnalyzeResult Result = VeroScriptAnalyzer::AnalyzeFile(
			VeroScript::PathUtil::ToUtf8(Utf8File));
		if (!Expect(
				!Error && Result.bSuccess,
				"AnalyzeFile should accept a UTF-8 path"))
		{
			for (const FDiagnostic& Diagnostic : Result.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
		std::filesystem::remove_all(Utf8Root, Error);
	}

	{
		const FAnalyzeResult Bad = VeroScriptAnalyzer::AnalyzeSource(
			"void main() { print(;",
			"<bad>");
		if (!Expect(!Bad.bSuccess, "invalid source should fail analysis"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Bad.Diagnostics.size() >= 1, "failure reports at least one diagnostic"))
		{
			return EXIT_FAILURE;
		}
		const FDiagnostic& Diagnostic = Bad.Diagnostics.front();
		if (!Expect(
				Diagnostic.Severity == EDiagnosticSeverity::Error,
				"parse failure should be Error severity"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!Diagnostic.Message.empty(), "diagnostic should carry a message"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Diagnostic.FileLabel == "<bad>", "diagnostic should preserve file label"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Diagnostic.Range.StartLine >= 1, "diagnostic line should be 1-based"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const FAnalyzeResult TypeBad = VeroScriptAnalyzer::AnalyzeSource(
			"void main() { int x = \"a\"; }",
			"<type>");
		if (!Expect(!TypeBad.bSuccess, "type error should fail analysis"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(TypeBad.Diagnostics.size() == 1, "type error should report one diagnostic"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(TypeBad.Diagnostics.front().FileLabel == "<type>", "type error file label"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const FAnalyzeResult MultiType = VeroScriptAnalyzer::AnalyzeSource(
			"void badA() { int x = \"a\"; }\n"
			"void ok() { print(1); }\n"
			"void badB() { int y = \"b\"; }\n"
			"void main() { badA(); badB(); ok(); }\n",
			"<multi-type>");
		if (!Expect(!MultiType.bSuccess, "multi type errors should fail analysis"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				MultiType.Diagnostics.size() >= 2,
				"compiler recovery should report multiple type diagnostics"))
		{
			std::cerr << "VeroScriptAnalyzerTests: multi-type count="
				<< MultiType.Diagnostics.size() << '\n';
			for (const FDiagnostic& Diagnostic : MultiType.Diagnostics)
			{
				std::cerr << "  L" << Diagnostic.Range.StartLine << ": "
					<< Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		const FAnalyzeResult MultiCtor = VeroScriptAnalyzer::AnalyzeSource(
			"class Point {\n"
			"    public:\n"
			"  Point() { int x = \"a\"; }\n"
			"  Point(int n) { int y = \"b\"; }\n"
			"}\n"
			"void main() { Point p = new Point(); }\n",
			"<multi-ctor>");
		if (!Expect(!MultiCtor.bSuccess, "multi ctor type errors should fail analysis"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				MultiCtor.Diagnostics.size() >= 2,
				"constructor recovery should report multiple diagnostics"))
		{
			std::cerr << "VeroScriptAnalyzerTests: multi-ctor count="
				<< MultiCtor.Diagnostics.size() << '\n';
			for (const FDiagnostic& Diagnostic : MultiCtor.Diagnostics)
			{
				std::cerr << "  L" << Diagnostic.Range.StartLine << ": "
					<< Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScriptTooling-analyze-import";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream BadImport(TempRoot / "bad_import.vs");
			BadImport << "export int helper() { return (; }\n";
		}
		{
			std::ofstream MainFile(TempRoot / "main.vs");
			MainFile << "import \"./bad_import.vs\";\nvoid main() { helper(); }\n";
		}

		const std::string MainPath = (TempRoot / "main.vs").string();
		const FAnalyzeResult ImportBad = VeroScriptAnalyzer::AnalyzeFile(MainPath);
		if (!Expect(!ImportBad.bSuccess, "import parse error should fail analysis"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		if (!Expect(ImportBad.Diagnostics.size() == 1, "import parse error should report one diagnostic"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		const std::string& FileLabel = ImportBad.Diagnostics.front().FileLabel;
		if (!Expect(
				EndsWith(FileLabel, "bad_import.vs"),
				"import diagnostic should point at the imported file"))
		{
			std::cerr << "VeroScriptAnalyzerTests: got FileLabel=[" << FileLabel << "]\n";
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	{
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScriptTooling-analyze-import-macro";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream BadImport(TempRoot / "bad_macro.vs");
			BadImport << "#define BAD int x = \"a\";\n"
				<< "void pad() {}\n"
				<< "export void helper() { BAD }\n";
		}
		{
			std::ofstream MainFile(TempRoot / "main.vs");
			MainFile << "import \"./bad_macro.vs\";\nvoid main() { helper(); }\n";
		}

		const std::string MainPath = (TempRoot / "main.vs").string();
		const FAnalyzeResult ImportBad = VeroScriptAnalyzer::AnalyzeFile(MainPath);
		if (!Expect(!ImportBad.bSuccess, "import macro type error should fail analysis"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		if (!Expect(ImportBad.Diagnostics.size() >= 1, "import macro error reports diagnostic"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		const FDiagnostic& Diagnostic = ImportBad.Diagnostics.front();
		if (!Expect(
				EndsWith(Diagnostic.FileLabel, "bad_macro.vs"),
				"import macro diagnostic points at imported file"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		if (!Expect(
				Diagnostic.Range.StartLine == 3,
				"import macro diagnostic line is physical in imported file"))
		{
			std::cerr << "line=" << Diagnostic.Range.StartLine << '\n';
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	{
		FAnalyzeOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Defines.push_back("FEATURE");
		const FAnalyzeResult WithDefine = VeroScriptAnalyzer::AnalyzeSource(
			"#ifdef FEATURE\n"
			"void main() { print(1); }\n"
			"#else\n"
			"void main() { int x = \"bad\"; }\n"
			"#endif\n",
			"<defines>",
			Options);
		if (!Expect(WithDefine.bSuccess, "Analyze should honor Defines / -D"))
		{
			for (const FDiagnostic& Diagnostic : WithDefine.Diagnostics)
			{
				std::cerr << "  " << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* MacroBad = R"(
#define BAD int x = "a";
void pad() {}
void main() { BAD }
)";
		const FAnalyzeResult MacroType = VeroScriptAnalyzer::AnalyzeSource(
			MacroBad,
			"<macro-diag>");
		if (!Expect(!MacroType.bSuccess, "macro-expanded type error should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(MacroType.Diagnostics.size() >= 1, "macro type error reports diagnostic"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				MacroType.Diagnostics.front().Range.StartLine == 4,
				"macro type diagnostic line is physical"))
		{
			std::cerr << "line=" << MacroType.Diagnostics.front().Range.StartLine << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
				Registry.RegisterNative(
					"host_add",
					VeroScript::ENativeType::Int,
					{VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
					[](const VeroScript::FNativeCallContext&,
						VeroScript::FValue&,
						std::string&)
					{
						return true;
					},
					nullptr,
					ErrorId),
				"register host_add for analyze"))
		{
			return EXIT_FAILURE;
		}

		FAnalyzeOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FAnalyzeResult WithBindings = VeroScriptAnalyzer::AnalyzeSource(
			"void main() { int x = host_add(1, 2); }",
			"<host-ok>",
			Options);
		if (!Expect(WithBindings.bSuccess, "Analyze should accept RegisterNative via Bindings"))
		{
			return EXIT_FAILURE;
		}

		const FAnalyzeResult Without = VeroScriptAnalyzer::AnalyzeSource(
			"void main() { int x = host_add(1, 2); }",
			"<host-missing>");
		if (!Expect(!Without.bSuccess, "Analyze should reject unknown host_add without Bindings"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		VeroScript::FHostTypeId ActorId;
		if (!Expect(
				Registry.RegisterType("AActor", VeroScript::EHostTypeKind::Class, ActorId, ErrorId),
				"RegisterType AActor for analyze inherit"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc GetValue;
		GetValue.Kind = VeroScript::EHostMemberKind::Method;
		GetValue.Name = "get_value";
		GetValue.ReturnType = VeroScript::ENativeType::Int;
		GetValue.Callback =
			[](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
			{
				return true;
			};
		if (!Expect(Registry.RegisterMember(ActorId, GetValue, ErrorId), "AActor.get_value"))
		{
			return EXIT_FAILURE;
		}

		FAnalyzeOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		const FAnalyzeResult NotInheritable = VeroScriptAnalyzer::AnalyzeSource(
			"class MyActor : AActor {}\nvoid main() {}\n",
			"<host-inherit-denied>",
			Options);
		if (!Expect(
				!NotInheritable.bSuccess,
				"Analyze should reject inherit from non-inheritable host"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(
				Registry.SetTypeInheritable(ActorId, true, ErrorId),
				"SetTypeInheritable AActor"))
		{
			return EXIT_FAILURE;
		}
		const FAnalyzeResult Ok = VeroScriptAnalyzer::AnalyzeSource(
			"class MyActor : AActor {\n"
			"public:\n"
			"\tMyActor() {}\n"
			"}\n"
			"void main() { MyActor a = MyActor(); int x = a.get_value(); }\n",
			"<host-inherit-ok>",
			Options);
		if (!Expect(Ok.bSuccess, "Analyze should accept inheritable host subclass"))
		{
			for (const auto& Diag : Ok.Diagnostics)
			{
				std::cerr << Diag.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		const FAnalyzeResult Ok = VeroScriptAnalyzer::AnalyzeSource(
			"UCLASS()\n"
			"class CombatActor {\n"
			"public:\n"
			"\tUPROPERTY(EditAnywhere)\n"
			"\tint Amount = 0;\n"
			"}\n"
			"void main() { print(1); }\n",
			"<decl-meta-ok>");
		if (!Expect(Ok.bSuccess, "annotated declarations should analyze"))
		{
			for (const auto& Diag : Ok.Diagnostics)
			{
				std::cerr << Diag.Code << ": " << Diag.Message << '\n';
			}
			return EXIT_FAILURE;
		}

		const FAnalyzeResult OnNamespace = VeroScriptAnalyzer::AnalyzeSource(
			"UCLASS()\n"
			"namespace N { }\n"
			"void main() {}\n",
			"<decl-meta-namespace>");
		if (!Expect(!OnNamespace.bSuccess, "annotations on namespace should fail"))
		{
			return EXIT_FAILURE;
		}
		bool bFoundNamespaceDiag = false;
		for (const FDiagnostic& Diag : OnNamespace.Diagnostics)
		{
			if (Diag.Code == "diag.annotations_are_not_supported_on_namespace"
				|| Diag.Message.find("annotations are not supported on namespace")
					!= std::string::npos)
			{
				bFoundNamespaceDiag = true;
				break;
			}
		}
		if (!Expect(bFoundNamespaceDiag, "namespace annotation diagnostic MessageId/Message"))
		{
			for (const auto& Diag : OnNamespace.Diagnostics)
			{
				std::cerr << Diag.Code << ": " << Diag.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		const auto Memory = VeroScriptAnalyzer::AnalyzeSource(
			"void main(){auto b=new std::memory::buffer(16);"
			"auto v=b.slice(0,8);auto r=b.readonly_slice(0,8);"
			"std::memory::write<int>(v,0,7,std::memory::byte_order::little);"
			"int value=std::memory::read<int>(r,0,std::memory::byte_order::little);"
			"auto bytes=std::memory::to_array(b);"
			"auto copy=std::memory::from_array(bytes);"
			"auto clone=std::memory::clone(copy);print(value);}",
			"<memory-standard-analysis>");
		if (!Expect(Memory.bSuccess, "new memory APIs compile through tooling without an external catalog"))
		{
			for (const auto& Diagnostic : Memory.Diagnostics)
			{
				std::cerr << Diagnostic.Code << ": " << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		const auto Memory = VeroScriptAnalyzer::AnalyzeSource(
			"using namespace std::memory;void main(){auto b=new buffer(8);"
			"auto c=std::memory::clone(b);print(c.size());}", "<memory-accepted-completion>");
		const auto Chains = VeroScriptAnalyzer::AnalyzeSource(
			"using namespace std::memory;void main(){auto b=new buffer(8);auto empty=view();"
			"std::memory::clone(b).resize(16);"
			"auto count=b.slice(0,4).slice(0,2).size();"
			"auto bytes=std::memory::to_array(b);auto copy=bytes;copy.push(1);print(count);}",
			"<memory-postfix-analysis>");
		if (!Expect(Memory.bSuccess && Chains.bSuccess, "accepted memory completion and call chains compile"))
		{
			for (const auto& Diagnostic : Chains.Diagnostics)
			{
				std::cerr << Diagnostic.Code << ": " << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		FAnalyzeOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths = {"macro-analyze/search"};
		Options.SourceOverlays = {{"macro-analyze/search/library.vs", "#define_export VALUE 42\n"}};
		const auto Result = VeroScriptAnalyzer::AnalyzeSource(
			"import \\\n\"library.vs\";\nvoid main(){\nint result = NO_SUCH_NAME;\n}\n",
			"macro-analyze/main.vs", Options);
		if (!Expect(!Result.bSuccess && !Result.Diagnostics.empty() &&
					Result.Diagnostics.front().Range.StartLine == 4,
				"macro-import diagnostic mapping uses custom import paths and continued source"))
		{
			return EXIT_FAILURE;
		}
		Options.ImplicitImportPaths = {"library.vs"};
		const auto Implicit = VeroScriptAnalyzer::AnalyzeSource(
			"int main(){return VALUE;}", "macro-analyze/main.vs", Options);
		if (!Expect(Implicit.bSuccess, "analysis shares implicit macro interfaces with runtime compilation"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const auto Result = VeroScriptAnalyzer::AnalyzeSource(
			"#define PREFIX ;;;;;;;;;;;;;;;;;\nPREFIX import \"missing.vs\";\n",
			"macro-analyze/entry.vs");
		if (!Expect(!Result.bSuccess && !Result.Diagnostics.empty() &&
					Result.Diagnostics.front().bPhysicalLocation &&
					Result.Diagnostics.front().Range.StartLine == 2 &&
					Result.Diagnostics.front().Range.StartColumn == 8,
				"import failure keeps its physical column after preceding macro expansion"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const auto Result = VeroScriptAnalyzer::AnalyzeSource(
			"#define PREFIX ;;;;;;;;;;;;;;;;;\nPREFIX import \"\";\n", "macro-analyze/invalid-path.vs");
		if (!Expect(!Result.bSuccess && !Result.Diagnostics.empty() &&
					Result.Diagnostics.front().bPhysicalLocation &&
					Result.Diagnostics.front().Range.StartLine == 2 &&
					Result.Diagnostics.front().Range.StartColumn == 8,
				"invalid import path retains original column through module loading and tooling"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		FAnalyzeOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.SourceOverlays = {{"macro-analyze/library.vs", "#define_export VALUE 42\n"}};
		const auto Result = VeroScriptAnalyzer::AnalyzeSource(
			"#define VALUE 1\n#define PREFIX ;;;;;;;;;;;;;;;;;\n"
			"PREFIX import \"library.vs\";\n", "macro-analyze/main.vs", Options);
		if (!Expect(!Result.bSuccess && !Result.Diagnostics.empty() &&
					Result.Diagnostics.front().bPhysicalLocation &&
					Result.Diagnostics.front().Range.StartLine == 3 &&
					Result.Diagnostics.front().Range.StartColumn == 8,
				"macro conflict diagnostic retains physical import location"))
		{
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
