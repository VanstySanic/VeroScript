// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptPath.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptWorkspaceSymbolsTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool HasSymbol(
		const std::vector<VeroScript::Tooling::FWorkspaceSymbol>& Symbols,
		const std::string& Name,
		const std::string& FileLabel)
	{
		for (const VeroScript::Tooling::FWorkspaceSymbol& Symbol : Symbols)
		{
			if (Symbol.Name == Name && Symbol.FileLabel == FileLabel)
			{
				return true;
			}
		}
		return false;
	}

	bool StubNative(
		const VeroScript::FNativeCallContext&,
		VeroScript::FValue&,
		std::string&)
	{
		return true;
	}
}

int main()
{
	using namespace VeroScript::Tooling;

	constexpr const char* Alpha = R"(
struct Point {
	int x;
	int Sum() { return x + x; }
}

void helper() {}
void main() {}
)";

	constexpr const char* Beta = R"(
export void exported_fn() {}
void main() {}
)";

	{
		std::vector<FDocumentOverlay> Documents;
		FDocumentOverlay AlphaDoc;
		AlphaDoc.Path = "alpha.vs";
		AlphaDoc.Source = Alpha;
		Documents.push_back(std::move(AlphaDoc));
		FDocumentOverlay BetaDoc;
		BetaDoc.Path = "beta.vs";
		BetaDoc.Source = Beta;
		Documents.push_back(std::move(BetaDoc));

		const FWorkspaceSymbolsResult Result =
			VeroScriptWorkspaceSymbols::CollectFromDocuments(Documents);
		if (!Expect(Result.bSuccess, "collect from documents"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.Symbols.size() >= 6, "flattened symbols from both files"))
		{
			std::cerr << "count=" << Result.Symbols.size() << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(HasSymbol(Result.Symbols, "Point", "alpha.vs"), "Point in alpha"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasSymbol(Result.Symbols, "Sum", "alpha.vs"), "nested Sum in alpha"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(HasSymbol(Result.Symbols, "exported_fn", "beta.vs"), "export in beta"))
		{
			return EXIT_FAILURE;
		}

		bool bSumHasContainer = false;
		for (const FWorkspaceSymbol& Symbol : Result.Symbols)
		{
			if (Symbol.Name == "Sum" && Symbol.ContainerName == "Point")
			{
				bSumHasContainer = true;
			}
		}
		if (!Expect(bSumHasContainer, "child symbol keeps container name"))
		{
			return EXIT_FAILURE;
		}

		int PrintCount = 0;
		for (const FWorkspaceSymbol& Symbol : Result.Symbols)
		{
			if (Symbol.Name == "print" && Symbol.FileLabel.empty())
			{
				++PrintCount;
			}
		}
		if (!Expect(PrintCount == 1, "soft print emitted once across documents"))
		{
			std::cerr << "printCount=" << PrintCount << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("alpha.vs", Alpha);
		Workspace.OpenDocument("beta.vs", Beta);

		const FWorkspaceSymbolsResult Result = Workspace.WorkspaceSymbols();
		if (!Expect(Result.bSuccess, "collect from workspace"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				HasSymbol(Result.Symbols, "helper", "alpha.vs")
					&& HasSymbol(Result.Symbols, "exported_fn", "beta.vs"),
				"workspace spans open documents"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Registry.RegisterNative(
				"host_add",
				VeroScript::ENativeType::Int,
				{VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
				StubNative,
				nullptr,
				ErrorId))
		{
			return Fail("register host_add for workspace symbols");
		}

		VeroScript::FHostTypeId CounterId;
		if (!Registry.RegisterType(
				"Counter",
				VeroScript::EHostTypeKind::Class,
				CounterId,
				ErrorId))
		{
			return Fail("register Counter for workspace symbols");
		}
		VeroScript::FHostMemberDesc Inc;
		Inc.Kind = VeroScript::EHostMemberKind::Method;
		Inc.Name = "inc";
		Inc.ReturnType = VeroScript::ENativeType::Void;
		if (!Registry.RegisterMember(CounterId, Inc, ErrorId))
		{
			return Fail("register Counter.inc for workspace symbols");
		}
		VeroScript::FHostMemberDesc Answer;
		Answer.Kind = VeroScript::EHostMemberKind::StaticConstant;
		Answer.Name = "Answer";
		Answer.ReturnType = VeroScript::ENativeType::Int;
		if (!Registry.RegisterMember(CounterId, Answer, ErrorId))
		{
			return Fail("register Counter.Answer for workspace symbols");
		}
		VeroScript::FGenericHostTypeTemplateId TemplateId;
		if (!Registry.RegisterGenericHostTypeTemplateMetadata(
				"HostPair", 2, TemplateId, ErrorId, "A host-provided generic pair."))
		{
			return Fail("register HostPair template for workspace symbols");
		}

		std::vector<FDocumentOverlay> Documents;
		FDocumentOverlay Doc;
		Doc.Path = "host.vs";
		Doc.Source = "void main() {}\n";
		Documents.push_back(std::move(Doc));

		FWorkspaceSymbolsOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		Options.bIncludeHostBindings = true;
		Options.bIncludeSoftBuiltins = false;
		const FWorkspaceSymbolsResult Result =
			VeroScriptWorkspaceSymbols::CollectFromDocuments(Documents, Options);
		if (!Expect(Result.bSuccess, "host binding workspace symbols"))
		{
			return EXIT_FAILURE;
		}
		bool bHasHostAdd = false;
		bool bHasCounter = false;
		bool bHasAnswer = false;
		bool bHasHostPair = false;
		for (const FWorkspaceSymbol& Symbol : Result.Symbols)
		{
			if (Symbol.Name == "host_add" && Symbol.FileLabel.empty())
			{
				bHasHostAdd = true;
			}
			if (Symbol.Name == "Counter" && Symbol.FileLabel.empty())
			{
				bHasCounter = true;
			}
			if (Symbol.Name == "Answer" && Symbol.ContainerName == "Counter")
			{
				bHasAnswer = Symbol.Kind == EOutlineSymbolKind::Constant;
			}
			if (Symbol.Name == "HostPair<T1, T2>" && Symbol.FileLabel.empty())
			{
				bHasHostPair = Symbol.Kind == EOutlineSymbolKind::TypeAlias;
			}
		}
		if (!Expect(bHasHostAdd, "host_add from Bindings"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(bHasCounter, "Counter from Bindings"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(bHasAnswer, "static constant from Bindings"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(bHasHostPair, "generic host template from Bindings"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Host symbols must still emit when there are no open documents.
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Registry.RegisterNative(
				"host_ping",
				VeroScript::ENativeType::Void,
				{},
				StubNative,
				nullptr,
				ErrorId))
		{
			return Fail("register host_ping");
		}
		VeroScriptWorkspace Workspace;
		FWorkspaceOptions WorkspaceOptions;
		WorkspaceOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		WorkspaceOptions.Bindings = &Registry;
		Workspace.SetOptions(WorkspaceOptions);
		const FWorkspaceSymbolsResult Result = Workspace.WorkspaceSymbols();
		if (!Expect(Result.bSuccess, "empty workspace with Bindings"))
		{
			return EXIT_FAILURE;
		}
		bool bHasPing = false;
		for (const FWorkspaceSymbol& Symbol : Result.Symbols)
		{
			if (Symbol.Name == "host_ping" && Symbol.FileLabel.empty())
			{
				bHasPing = true;
			}
		}
		if (!Expect(bHasPing, "host_ping without open documents"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Annotations-only catalogs must still surface soft stubs (no types/functions).
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Expect(
				Registry.RegisterAnnotation("VSTAG", "annotation (Editor)", ErrorId)
					&& Registry.RegisterAnnotation("@hook", "annotation ()", ErrorId),
				"register annotations-only catalog"))
		{
			return EXIT_FAILURE;
		}
		std::vector<FDocumentOverlay> Documents;
		FDocumentOverlay Doc;
		Doc.Path = "annot.vs";
		Doc.Source = "void main() {}\n";
		Documents.push_back(std::move(Doc));

		FWorkspaceSymbolsOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		Options.bIncludeHostBindings = true;
		Options.bIncludeSoftBuiltins = false;
		const FWorkspaceSymbolsResult Result =
			VeroScriptWorkspaceSymbols::CollectFromDocuments(Documents, Options);
		if (!Expect(Result.bSuccess, "annotations-only workspace symbols"))
		{
			return EXIT_FAILURE;
		}
		bool bHasTag = false;
		bool bHasHook = false;
		for (const FWorkspaceSymbol& Symbol : Result.Symbols)
		{
			bHasTag = bHasTag || Symbol.Name == "VSTAG";
			bHasHook = bHasHook || Symbol.Name == "@hook";
		}
		if (!Expect(bHasTag && bHasHook, "annotation stubs from Bindings without soft builtins"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// The same declaration may be observed through more than one imported
		// document. Workspace results retain one semantic identity, not duplicate
		// palette and hierarchy entries.
		std::vector<FDocumentOverlay> Documents;
		Documents.push_back(FDocumentOverlay{"shared.vs", "void Shared() {}\n"});
		Documents.push_back(FDocumentOverlay{"shared.vs", "void Shared() {}\n"});
		FWorkspaceSymbolsOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.bIncludeImports = false;
		Options.bIncludeSoftBuiltins = false;
		const FWorkspaceSymbolsResult Result =
			VeroScriptWorkspaceSymbols::CollectFromDocuments(Documents, Options);
		std::size_t SharedCount = 0;
		for (const FWorkspaceSymbol& Symbol : Result.Symbols)
		{
			SharedCount += Symbol.Name == "Shared" ? 1u : 0u;
		}
		if (!Expect(
			Result.bSuccess && SharedCount == 1u,
			"workspace symbols deduplicate identical declaration identities"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FWorkspaceSymbolsOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.bIncludeSoftBuiltins = false;
		int Checks = 0;
		Options.ShouldCancel = [&Checks]()
		{
			return ++Checks >= 4;
		};
		const auto Cancelled = VeroScriptWorkspaceSymbols::CollectFromDocuments(
			{{"cancel-index.vs", "void Small() {}\n"}}, Options);
		if (!Expect(Cancelled.bCancelled && Cancelled.Symbols.empty() && Checks >= 4,
			"workspace cancellation reaches module preparation and parsing, not just document boundaries"))
		{
			return EXIT_FAILURE;
		}
		Options.ShouldCancel = {};
		const auto Retried = VeroScriptWorkspaceSymbols::CollectFromDocuments(
			{{"cancel-index.vs", "void Small() {}\n"}}, Options);
		if (!Expect(Retried.bSuccess && !Retried.bCancelled
			&& HasSymbol(Retried.Symbols, "Small", "cancel-index.vs"),
			"cancelled workspace parsing does not poison a later query"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const auto Root = std::filesystem::temp_directory_path()
			/ ("vero-symbol-roots-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		const auto UnicodeDirectory = Root / VeroScript::PathUtil::FromUtf8("\xE6\xBA\x90\xE7\xA0\x81-\xF0\x9F\x98\x80");
		std::filesystem::create_directories(UnicodeDirectory);
		{
			std::ofstream File(UnicodeDirectory / "closed.vs", std::ios::binary);
			File << "void Closed() {}\n";
		}
		const std::string RootPath = VeroScript::PathUtil::ToUtf8(Root);
		FWorkspaceSymbolsOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.bIncludeSoftBuiltins = false;
		Options.WorkspaceRoots = {"", RootPath};
		VeroScriptWorkspace Workspace;
		const auto Result = VeroScriptWorkspaceSymbols::CollectFromWorkspace(Workspace, Options);
		std::error_code Error;
		std::filesystem::remove_all(Root, Error);
		if (!Expect(Result.bSuccess
			&& HasSymbol(Result.Symbols, "Closed", VeroScript::PathUtil::ToUtf8(UnicodeDirectory / "closed.vs")),
			"empty roots do not suppress later roots and Unicode subdirectories remain indexable"))
		{
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
