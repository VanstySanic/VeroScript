// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptLocale.h"
#include "VeroScriptCompile.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPath.h"
#include "VeroScriptTooling.h"
#include "VeroScriptWorkspace.h"
#include "../Internal/VeroScriptSymbolQuery.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <memory>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptWorkspaceTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool FindPosition(
	    const std::string_view Source,
	    const std::string_view Needle,
	    VeroScript::Tooling::FTextPosition& OutPosition,
	    const std::size_t SearchFrom = 0)
	{
		const std::size_t Offset = Source.find(Needle, SearchFrom);
		if (Offset == std::string_view::npos)
		{
			return false;
		}
		OutPosition = {1, 1};
		for (std::size_t Index = 0; Index < Offset; ++Index)
		{
			if (Source[Index] == '\n')
			{
				++OutPosition.Line;
				OutPosition.Column = 1;
			}
			else
			{
				++OutPosition.Column;
			}
		}
		return true;
	}
}

int main()
{
	{
		using namespace VeroScript;
		using namespace VeroScript::Tooling;
		struct FGetter final : IAnnotationProcessor
		{
			void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
			{
				std::string Name;
				if (!Context.ReadDependency("tooling-metadata", Name))
				{
					Name = "Read";
				}

				Output.SubmitMethod(
				    Context.Owner->Id, EMemberAccess::Public, "int " + Name + "() const {return value;}");
			}
		};
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		std::string Error;
		FAnnotationProcessorDesc Desc{"@Getter", "workspace.test.getter", "1", "",
		    AnnotationTargetMask(EAnnotationTarget::Field), 1, 0, []
		{
			return std::make_unique<FGetter>();
		}};
		if (!Expect(Registry->Register(Desc, Error), "register workspace annotation fixture"))
		{
			return EXIT_FAILURE;
		}
		Registry->Freeze();
		std::string Metadata = "Read";
		bool Exists = true, Throw = false, Cancel = false;
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.AnnotationProcessing.Registry = Registry;
		Options.AnnotationProcessing.ReadDependency = [&](std::string_view, std::string& Content)
		{
			if (Throw)
			{
				throw std::runtime_error("metadata reader unavailable");
			}

			if (Cancel)
			{
				throw FAnalysisCancelled();
			}

			if (Exists)
			{
				Content = Metadata;
			}

			return Exists;
		};
		VeroScriptWorkspace Workspace;
		Workspace.SetOptions(Options);
		Workspace.OpenDocument("annotation-cache.vs", "struct A {@Getter int value=4;};void main(){A a=A();a.Read();a.Changed();}");
		const auto Initial = Workspace.SemanticTokensDocument("annotation-cache.vs");
		if (!Expect(Initial.bSuccess && Detail::IsSemanticQuerySnapshotCacheable(Initial.Snapshot) &&
		            Detail::IsSemanticQuerySnapshotCurrent(Initial.Snapshot),
		        "stable annotation snapshot can be cached"))
		{
			return EXIT_FAILURE;
		}

		Metadata = "Changed";
		if (!Expect(!Detail::IsSemanticQuerySnapshotCurrent(Initial.Snapshot), "changed processor metadata invalidates its snapshot")) return EXIT_FAILURE;
		const auto Changed = Workspace.SemanticTokensDocument("annotation-cache.vs");
		Exists = false;
		if (!Expect(!Detail::IsSemanticQuerySnapshotCurrent(Changed.Snapshot), "removed processor dependency invalidates snapshot")) return EXIT_FAILURE;
		const auto Missing = Workspace.SemanticTokensDocument("annotation-cache.vs");
		Exists = true;
		if (!Expect(!Detail::IsSemanticQuerySnapshotCurrent(Missing.Snapshot), "newly created processor dependency invalidates snapshot")) return EXIT_FAILURE;
		Throw = true;
		if (!Expect(!Detail::IsSemanticQuerySnapshotCurrent(Changed.Snapshot), "failed metadata read does not reuse stale annotation snapshot")) return EXIT_FAILURE;
		Throw = false;
		Cancel = true;
		if (!Expect(!Detail::IsSemanticQuerySnapshotCurrent(Changed.Snapshot),
		        "cancelled dependency validation outside a request invalidates safely"))
		{
			return EXIT_FAILURE;
		}

		Cancel = false;
		auto Volatile = std::make_shared<FAnnotationProcessorRegistry>();
		Desc.Cacheable = false;
		if (!Expect(Volatile->Register(Desc, Error), "register volatile annotation fixture")) return EXIT_FAILURE;
		Volatile->Freeze();
		Options.AnnotationProcessing.Registry = Volatile;
		Workspace.SetOptions(Options);
		const auto Fresh = Workspace.SemanticTokensDocument("annotation-cache.vs");
		if (!Expect(Fresh.bSuccess && !Detail::IsSemanticQuerySnapshotCacheable(Fresh.Snapshot) &&
		            Detail::IsSemanticQuerySnapshotCurrent(Fresh.Snapshot),
		        "volatile processor results are valid but cannot be cached"))
		{
			return EXIT_FAILURE;
		}
	}
	using namespace VeroScript::Tooling;

	// Diagnostics are now localized; pin English so message assertions are stable.
	(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");

	{
		VeroScriptWorkspace Workspace;
		const std::string Source =
		    "struct State { const int value; int other;\n"
		    "State(int value) : value(value), other(0) {}\n"
		    "int read() { return value + other; } };\n";
		Workspace.OpenDocument("readonly-semantic.vs", Source);
		const auto Semantic = Workspace.SemanticTokensDocument("readonly-semantic.vs");
		bool bCorrect = Semantic.bSuccess;
		std::size_t Offset = 0;
		const std::pair<EOutlineSymbolKind, std::uint32_t> Expected[] = {
		    {EOutlineSymbolKind::Field, SemanticTokenModifiers::Readonly},
		    {EOutlineSymbolKind::Variable, SemanticTokenModifiers::None},
		    {EOutlineSymbolKind::Field, SemanticTokenModifiers::Readonly},
		    {EOutlineSymbolKind::Variable, SemanticTokenModifiers::None},
		    {EOutlineSymbolKind::Field, SemanticTokenModifiers::Readonly}};
		for (const auto& [Kind, Modifiers] : Expected)
		{
			FTextPosition Position;
			if (!FindPosition(Source, "value", Position, Offset))
			{
				bCorrect = false;
				break;
			}
			Offset = Source.find("value", Offset) + 5;
			bCorrect &= std::any_of(Semantic.Tokens.begin(), Semantic.Tokens.end(),
			    [&](const FSemanticToken& Token)
			{
				return Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column && Token.Kind == Kind && Token.Modifiers == Modifiers;
			});
		}
		bCorrect &= FSemanticToken{}.Modifiers == SemanticTokenModifiers::None;
		if (!Expect(bCorrect, "public semantic tokens preserve readonly fields and distinguish same-name parameters"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("buffers/main.vs", "int value = 1;");
		Workspace.OpenDocument("buffers/other.vs", "int other = 2;");
		FDocumentOverlay Document;
		if (!Expect(Workspace.TryGetDocument("buffers/./main.vs", Document) && Document.Source == "int value = 1;",
		        "single-document lookup accepts normalized paths"))
		{
			return EXIT_FAILURE;
		}
		Workspace.UpdateDocument("buffers/main.vs", "int value = 3;");
		Workspace.CloseDocument("buffers/main.vs");
		if (!Expect(Document.Source == "int value = 1;" && !Workspace.TryGetDocument("buffers/main.vs", Document) && Document.Source == "int value = 1;",
		        "single-document copies survive updates/close; misses preserve the output"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// `mutable` is contextual: the field modifier is a keyword, while an
		// unrelated local declaration may still use the same spelling as its name.
		constexpr std::string_view Source =
		    "struct MutableState {\n"
		    "    mutable int cache;\n"
		    "    void update() { int mutable = 1; cache = mutable; }\n"
		    "};\n";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("mutable-semantic.vs", std::string(Source));
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("mutable-semantic.vs");
		FTextPosition ModifierPosition;
		FTextPosition LocalPosition;
		const std::size_t LocalOffset = Source.find("int mutable");
		if (!Expect(
		        FindPosition(Source, "mutable", ModifierPosition) && LocalOffset != std::string_view::npos && FindPosition(Source, "mutable", LocalPosition, LocalOffset),
		        "locate contextual mutable semantic tokens"))
		{
			return EXIT_FAILURE;
		}
		const auto KindAt = [&Semantic](const FTextPosition Position)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column)
				{
					return Token.Kind;
				}
			}
			return EOutlineSymbolKind::Struct;
		};
		if (!Expect(
		        Semantic.bSuccess && KindAt(ModifierPosition) == EOutlineSymbolKind::Keyword && KindAt(LocalPosition) == EOutlineSymbolKind::Variable,
		        "contextual mutable modifier and identifier retain distinct semantic kinds"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Host macro spellings are semantic types, while PascalCase parameters and
		// locals remain values. Casing must not participate in classification.
		VeroScriptWorkspace Workspace;
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Defines = {"FString=string"};
		Workspace.SetOptions(Options);
		Workspace.OpenDocument(
		    "semantic.vs",
		    "void Send(FString Message) { FString Copy = Message; }\n");

		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("semantic.vs");
		if (!Expect(Semantic.bSuccess, "semantic tokens should parse host type aliases"))
		{
			return EXIT_FAILURE;
		}
		const auto KindAt = [&Semantic](const std::uint32_t Column)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == 1 && Token.Range.StartColumn == Column)
				{
					return Token.Kind;
				}
			}
			return EOutlineSymbolKind::Keyword;
		};
		if (!Expect(
		        KindAt(11) == EOutlineSymbolKind::TypeAlias && KindAt(19) == EOutlineSymbolKind::Variable && KindAt(30) == EOutlineSymbolKind::TypeAlias && KindAt(38) == EOutlineSymbolKind::Variable && KindAt(45) == EOutlineSymbolKind::Variable,
		        "FString and PascalCase values should receive distinct semantic kinds"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult TypeAliasHover =
		    Workspace.HoverDocument("semantic.vs", FTextPosition{1, 11});
		if (!Expect(
		        TypeAliasHover.Contents.find("type alias FString") != std::string::npos,
		        "initial type macro hover should match its semantic type kind"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Declaration names and their uses must retain the declared symbol kind;
		// values are not inferred from spelling or capitalization.
		constexpr const char* Source =
		    "class Hero {}\n"
		    "struct Coordinate {}\n"
		    "void Use(Hero Actor, Coordinate Value) {}\n";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("declaration-kinds.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("declaration-kinds.vs");
		const auto KindAt = [&Semantic](const FTextPosition Position)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column)
				{
					return Token.Kind;
				}
			}
			return EOutlineSymbolKind::Keyword;
		};
		FTextPosition ClassDeclaration;
		FTextPosition StructDeclaration;
		FTextPosition ClassUse;
		FTextPosition ClassValue;
		FTextPosition StructUse;
		FTextPosition StructValue;
		const std::size_t UseOffset = std::string_view(Source).find("void Use");
		if (!Expect(
		        FindPosition(Source, "Hero", ClassDeclaration) && FindPosition(Source, "Coordinate", StructDeclaration) && FindPosition(Source, "Hero", ClassUse, UseOffset) && FindPosition(Source, "Actor", ClassValue) && FindPosition(Source, "Coordinate", StructUse, UseOffset) && FindPosition(Source, "Value", StructValue),
		        "locate declaration-kind semantic tokens") ||
		    !Expect(
		        Semantic.bSuccess && KindAt(ClassDeclaration) == EOutlineSymbolKind::Class && KindAt(StructDeclaration) == EOutlineSymbolKind::Struct && KindAt(ClassUse) == EOutlineSymbolKind::Class && KindAt(ClassValue) == EOutlineSymbolKind::Variable && KindAt(StructUse) == EOutlineSymbolKind::Struct && KindAt(StructValue) == EOutlineSymbolKind::Variable,
		        "class, struct, and value semantic kinds should remain distinct"))
		{
			std::cerr
			    << "semantic success=" << Semantic.bSuccess
			    << " class declaration=" << static_cast<int>(KindAt(ClassDeclaration))
			    << " struct declaration=" << static_cast<int>(KindAt(StructDeclaration))
			    << " class use=" << static_cast<int>(KindAt(ClassUse))
			    << " class value=" << static_cast<int>(KindAt(ClassValue))
			    << " struct use=" << static_cast<int>(KindAt(StructUse))
			    << " struct value=" << static_cast<int>(KindAt(StructValue))
			    << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Namespace declaration names and every component of a qualified host type
		// need semantic identity even though single-token lookup cannot resolve the
		// composite spelling.
		VeroScript::FHostBindingRegistry Registry;
		VeroScript::FHostTypeId EndPlayReasonTypeId;
		std::string ErrorId;
		if (!Expect(
		        Registry.RegisterType(
		            "EEndPlayReason::Type",
		            VeroScript::EHostTypeKind::Enum,
		            EndPlayReasonTypeId,
		            ErrorId) &&
		            Registry.RegisterTypeAlias(
		                "TEnumAsByte",
		                {"T"},
		                "T",
		                ErrorId,
		                "Byte-backed enum source spelling."),
		        "register qualified host enum and generic host type alias"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* Source =
		    "namespace a {\n"
		    "void End(TEnumAsByte<EEndPlayReason::Type> Reason) {}\n"
		    "}\n";
		VeroScriptWorkspace Workspace;
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		Workspace.SetOptions(Options);
		Workspace.OpenDocument("qualified-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("qualified-semantic.vs");
		const auto KindAt = [&Semantic](const FTextPosition Position)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column)
				{
					return Token.Kind;
				}
			}
			return EOutlineSymbolKind::Keyword;
		};
		FTextPosition NamespacePos;
		FTextPosition AliasPos;
		FTextPosition QualifiedRootPos;
		FTextPosition QualifiedLeafPos;
		if (!Expect(
		        FindPosition(Source, "a {", NamespacePos) && FindPosition(Source, "TEnumAsByte", AliasPos) && FindPosition(Source, "EEndPlayReason", QualifiedRootPos) && FindPosition(Source, "Type", QualifiedLeafPos),
		        "locate namespace and qualified type semantic tokens") ||
		    !Expect(
		        Semantic.bSuccess && KindAt(NamespacePos) == EOutlineSymbolKind::Namespace && KindAt(AliasPos) == EOutlineSymbolKind::TypeAlias && KindAt(QualifiedRootPos) == EOutlineSymbolKind::Enum && KindAt(QualifiedLeafPos) == EOutlineSymbolKind::Enum,
		        "namespace, host aliases, and qualified host type components receive semantic kinds"))
		{
			std::cerr
			    << "semantic success=" << Semantic.bSuccess
			    << " namespace=" << static_cast<int>(KindAt(NamespacePos))
			    << " alias=" << static_cast<int>(KindAt(AliasPos))
			    << " qualified root=" << static_cast<int>(KindAt(QualifiedRootPos))
			    << " qualified leaf=" << static_cast<int>(KindAt(QualifiedLeafPos))
			    << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// Labels are semantic identifiers at both their declaration and goto use.
		// A same-spelled local remains a value and must not inherit label styling.
		constexpr const char* Source =
		    "void main() {\n"
		    "\tint again = 0;\n"
		    "again:\n"
		    "\tagain = again + 1;\n"
		    "\tgoto again;\n"
		    "}\n";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("label-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("label-semantic.vs");
		const auto KindAt = [&Semantic](const FTextPosition Position)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column)
				{
					return Token.Kind;
				}
			}
			return EOutlineSymbolKind::Keyword;
		};
		FTextPosition LocalDeclaration;
		FTextPosition LabelDeclaration;
		FTextPosition GotoReference;
		const std::size_t LabelOffset = std::string_view(Source).find("again:");
		const std::size_t GotoOffset = std::string_view(Source).find("goto again");
		if (!Expect(
		        FindPosition(Source, "again = 0", LocalDeclaration) && FindPosition(Source, "again:", LabelDeclaration, LabelOffset) && FindPosition(Source, "again", GotoReference, GotoOffset + 5),
		        "locate label semantic tokens") ||
		    !Expect(
		        Semantic.bSuccess && KindAt(LocalDeclaration) == EOutlineSymbolKind::Variable && KindAt(LabelDeclaration) == EOutlineSymbolKind::Label && KindAt(GotoReference) == EOutlineSymbolKind::Label,
		        "label declaration and goto reference should be distinct from values"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Current reference spellings, int main, and capture entries must keep their
		// semantic identity in the editor rather than falling back to plain text.
		constexpr std::string_view Source = R"(
int GlobalValue = 1;
int& get_ref() { return GlobalValue; }
int main(string[] args) {
	int& alias = get_ref();
	function()->int update = [&, args]() -> int {
		alias += len(args);
		return alias;
	};
	return update();
}
)";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("reference-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("reference-semantic.vs");
		const auto HasKind = [&Semantic](
		                         const FTextPosition Position,
		                         const EOutlineSymbolKind Kind)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column && Token.Kind == Kind)
				{
					return true;
				}
			}
			return false;
		};
		FTextPosition MainName;
		FTextPosition AliasDeclaration;
		FTextPosition CaptureArg;
		FTextPosition CapturedAliasUse;
		const std::size_t LambdaOffset = Source.find("[&, args]");
		if (!Expect(
		        FindPosition(Source, "main", MainName) && FindPosition(Source, "alias =", AliasDeclaration) && FindPosition(Source, "args", CaptureArg, LambdaOffset) && FindPosition(Source, "alias +=", CapturedAliasUse, LambdaOffset),
		        "locate int-main/reference semantic tokens") ||
		    !Expect(
		        Semantic.bSuccess && HasKind(MainName, EOutlineSymbolKind::Function) && HasKind(AliasDeclaration, EOutlineSymbolKind::Variable) && HasKind(CaptureArg, EOutlineSymbolKind::Variable) && HasKind(CapturedAliasUse, EOutlineSymbolKind::Variable),
		        "int main and reference captures retain semantic kinds"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* Source =
		    "namespace Library { int value = 1; }\n"
		    "using namespace Library;\n";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("using-namespace-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("using-namespace-semantic.vs");
		FTextPosition Target;
		if (!Expect(FindPosition(Source, "Library;", Target),
		        "locate using namespace semantic target"))
		{
			return EXIT_FAILURE;
		}
		bool bHasNamespaceTarget = false;
		for (const FSemanticToken& Token : Semantic.Tokens)
		{
			if (Token.Range.StartLine == Target.Line && Token.Range.StartColumn == Target.Column && Token.Kind == EOutlineSymbolKind::Namespace)
			{
				bHasNamespaceTarget = true;
				break;
			}
		}
		if (!Expect(
		        Semantic.bSuccess && bHasNamespaceTarget,
		        "using namespace target receives namespace semantic color"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Qualifier segments carry namespace semantics while the final callable
		// keeps its own kind. This covers both the reserved std root and nested
		// standard namespaces used by the IDE colorizer.
		constexpr std::string_view Source = R"(
void main() {
	int magnitude = std::abs(-1);
	std::io::pause();
}
)";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("standard-namespace-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("standard-namespace-semantic.vs");
		const auto HasKind = [&Semantic](
		                         const FTextPosition Position,
		                         const EOutlineSymbolKind Kind)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column && Token.Kind == Kind)
				{
					return true;
				}
			}
			return false;
		};
		FTextPosition StdAbs;
		FTextPosition Abs;
		FTextPosition StdIo;
		FTextPosition Io;
		FTextPosition Pause;
		const std::size_t AbsOffset = Source.find("std::abs");
		const std::size_t IoOffset = Source.find("std::io");
		if (!Expect(
		        FindPosition(Source, "std::abs", StdAbs) && FindPosition(Source, "abs", Abs, AbsOffset) && FindPosition(Source, "std::io", StdIo) && FindPosition(Source, "io", Io, IoOffset) && FindPosition(Source, "pause", Pause, IoOffset),
		        "locate standard namespace semantic components") ||
		    !Expect(
		        Semantic.bSuccess && HasKind(StdAbs, EOutlineSymbolKind::Namespace) && HasKind(Abs, EOutlineSymbolKind::Function) && HasKind(StdIo, EOutlineSymbolKind::Namespace) && HasKind(Io, EOutlineSymbolKind::Namespace) && HasKind(Pause, EOutlineSymbolKind::Function),
		        "standard namespace qualifiers and leaves use distinct semantic kinds"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// While the user is still typing a bare namespace expression, recover the
		// missing terminator and color the confirmed namespace without requiring a
		// refresh or a completed statement.
		constexpr std::string_view Source = R"(
void main() {
	std
}
)";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("unfinished-standard-namespace-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("unfinished-standard-namespace-semantic.vs");
		FTextPosition Std;
		bool bHasNamespace = false;
		if (FindPosition(Source, "std", Std))
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Std.Line && Token.Range.StartColumn == Std.Column && Token.Kind == EOutlineSymbolKind::Namespace)
				{
					bHasNamespace = true;
					break;
				}
			}
		}
		if (!Expect(
		        Semantic.bSuccess && bHasNamespace,
		        "unfinished bare standard namespace retains semantic color"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A nested standard path must preserve every confirmed segment even if the
		// final member is incomplete.  Do not infer these colors from casing.
		constexpr std::string_view CompleteSource =
		    "void main() { std::chrono::now(); }\n";
		constexpr std::string_view IncompleteSource =
		    "void main() { std::chrono:: }\n";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("chrono-complete-semantic.vs", CompleteSource);
		Workspace.OpenDocument("chrono-incomplete-semantic.vs", IncompleteSource);
		const FSemanticTokensResult Complete =
		    Workspace.SemanticTokensDocument("chrono-complete-semantic.vs");
		const FSemanticTokensResult Incomplete =
		    Workspace.SemanticTokensDocument("chrono-incomplete-semantic.vs");
		const auto HasKind = [](
		                         const FSemanticTokensResult& Tokens,
		                         const FTextPosition Position,
		                         const EOutlineSymbolKind Kind)
		{
			for (const FSemanticToken& Token : Tokens.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column && Token.Kind == Kind)
				{
					return true;
				}
			}
			return false;
		};
		FTextPosition CompleteStd;
		FTextPosition CompleteChrono;
		FTextPosition Now;
		FTextPosition IncompleteStd;
		FTextPosition IncompleteChrono;
		const std::size_t CompletePath = CompleteSource.find("std::chrono::now");
		const std::size_t IncompletePath = IncompleteSource.find("std::chrono::");
		if (!Expect(
		        CompletePath != std::string_view::npos && IncompletePath != std::string_view::npos && FindPosition(CompleteSource, "std", CompleteStd, CompletePath) && FindPosition(CompleteSource, "chrono", CompleteChrono, CompletePath) && FindPosition(CompleteSource, "now", Now, CompletePath) && FindPosition(IncompleteSource, "std", IncompleteStd, IncompletePath) && FindPosition(IncompleteSource, "chrono", IncompleteChrono, IncompletePath),
		        "locate complete and incomplete chrono semantic segments") ||
		    !Expect(
		        Complete.bSuccess && HasKind(Complete, CompleteStd, EOutlineSymbolKind::Namespace) && HasKind(Complete, CompleteChrono, EOutlineSymbolKind::Namespace) && HasKind(Complete, Now, EOutlineSymbolKind::Function) && Incomplete.bSuccess && HasKind(Incomplete, IncompleteStd, EOutlineSymbolKind::Namespace) && HasKind(Incomplete, IncompleteChrono, EOutlineSymbolKind::Namespace),
		        "chrono semantic tokens retain namespace segments during incomplete input"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr std::string_view Source = R"(
template<T, U = int>
class PairBox {
public:
	T first;
	U second;
}
void main() { PairBox<string> value = PairBox<string>(); }
)";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("template-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("template-semantic.vs");
		FTextPosition TemplateT;
		FTextPosition FieldT;
		FTextPosition FieldU;
		FTextPosition ClassUse;
		const bool bFoundPositions =
		    FindPosition(Source, "T,", TemplateT) && FindPosition(Source, "T first", FieldT) && FindPosition(Source, "U second", FieldU) && FindPosition(Source, "PairBox<string>", ClassUse);
		const auto HasKind = [&Semantic](
		                         const FTextPosition Position,
		                         const EOutlineSymbolKind Kind)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column && Token.Kind == Kind)
				{
					return true;
				}
			}
			return false;
		};
		if (!Expect(
		        Semantic.bSuccess && bFoundPositions && HasKind(TemplateT, EOutlineSymbolKind::TypeAlias) && HasKind(FieldT, EOutlineSymbolKind::TypeAlias) && HasKind(FieldU, EOutlineSymbolKind::TypeAlias) && HasKind(ClassUse, EOutlineSymbolKind::Class),
		        "template parameters and defaulted generic uses receive semantic kinds"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr std::string_view Source = R"(
class LegacyBox<T> { public: T value; }
namespace N { class Result {} }
N.Result Identity<T>(T value) { return N.Result(); }
)";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("generic-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("generic-semantic.vs");
		FTextPosition ClassParameter;
		FTextPosition ClassField;
		FTextPosition FunctionParameter;
		const bool bFoundPositions =
		    FindPosition(Source, "T> {", ClassParameter) && FindPosition(Source, "T value", ClassField) && FindPosition(Source, "T>(T", FunctionParameter);
		const auto HasTypeAlias = [&Semantic](const FTextPosition Position)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column && Token.Kind == EOutlineSymbolKind::TypeAlias)
				{
					return true;
				}
			}
			return false;
		};
		if (!Expect(
		        Semantic.bSuccess && bFoundPositions && HasTypeAlias(ClassParameter) && HasTypeAlias(ClassField) && HasTypeAlias(FunctionParameter),
		        "suffix generic parameters retain semantic type coloring"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Host annotations, their structured arguments, and predefined macros must
		// retain their physical-source kinds instead of inheriting Function from
		// the expanded AST.
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		std::vector<VeroScript::FHostAnnotationArgumentDesc> Arguments;
		Arguments.push_back(VeroScript::FHostAnnotationArgumentDesc{
		    "BlueprintCallable",
		    "flag",
		    "Makes the function callable from Blueprint graphs."});
		Arguments.push_back(VeroScript::FHostAnnotationArgumentDesc{
		    "meta",
		    "metadata list",
		    "Supplies nested host metadata."});
		Arguments.push_back(VeroScript::FHostAnnotationArgumentDesc{
		    "DisplayName",
		    "string",
		    "Overrides the displayed function name."});
		if (!Expect(
		        Registry.RegisterAnnotation(
		            "UFUNCTION",
		            "reflection specifier",
		            ErrorId,
		            "Exposes a function to Unreal reflection.",
		            std::move(Arguments)),
		        "register structured UFUNCTION metadata"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FHostTypeId NoiseTypeId;
		if (!Expect(
		        Registry.RegisterType(
		            "HostNoise",
		            VeroScript::EHostTypeKind::Class,
		            NoiseTypeId,
		            ErrorId),
		        "register host type used to detect contextual completion pollution"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FPredefinedMacro LogMacro;
		LogMacro.Name = "UE_LOG";
		LogMacro.Parameters = {"Category", "Verbosity", "Format"};
		LogMacro.Body = "print(Format)";
		LogMacro.SourceName = "Unreal Engine";
		VeroScriptWorkspace Workspace;
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Registry;
		Options.PredefinedMacros.push_back(std::move(LogMacro));
		Options.Defines.push_back("FHostNoise=HostNoise");
		Workspace.SetOptions(Options);
		constexpr const char* Source =
		    "UFUNCTION(\n"
		    "    BlueprintCallable\n"
		    ")\n"
		    "void Run() { FHostNoise value = nullptr; UE_LOG(LogTemp, Display, \"hello\"); }\n";
		Workspace.OpenDocument("ue-metadata.vs", Source);

		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("ue-metadata.vs");
		auto KindAt = [&Semantic](const FTextPosition Position)
		{
			for (const FSemanticToken& Token : Semantic.Tokens)
			{
				if (Token.Range.StartLine == Position.Line && Token.Range.StartColumn == Position.Column)
				{
					return Token.Kind;
				}
			}
			return EOutlineSymbolKind::Keyword;
		};
		FTextPosition AnnotationPos;
		FTextPosition ArgumentPos;
		FTextPosition MacroPos;
		FTextPosition HostTypeAliasPos;
		if (!Expect(
		        FindPosition(Source, "UFUNCTION", AnnotationPos) && FindPosition(Source, "BlueprintCallable", ArgumentPos) && FindPosition(Source, "UE_LOG", MacroPos) && FindPosition(Source, "FHostNoise", HostTypeAliasPos),
		        "locate UE metadata tokens") ||
		    !Expect(
		        Semantic.bSuccess && KindAt(AnnotationPos) == EOutlineSymbolKind::Annotation && KindAt(ArgumentPos) == EOutlineSymbolKind::AnnotationArgument && KindAt(MacroPos) == EOutlineSymbolKind::Macro && KindAt(HostTypeAliasPos) == EOutlineSymbolKind::TypeAlias,
		        "annotations, macros, and host-type aliases receive semantic kinds"))
		{
			std::cerr
			    << "semantic success=" << Semantic.bSuccess
			    << " annotation=" << static_cast<int>(KindAt(AnnotationPos))
			    << " argument=" << static_cast<int>(KindAt(ArgumentPos))
			    << " macro=" << static_cast<int>(KindAt(MacroPos))
			    << " host alias=" << static_cast<int>(KindAt(HostTypeAliasPos))
			    << '\n';
			return EXIT_FAILURE;
		}

		const FHoverResult AnnotationHover =
		    Workspace.HoverDocument("ue-metadata.vs", AnnotationPos);
		const FHoverResult ArgumentHover =
		    Workspace.HoverDocument("ue-metadata.vs", ArgumentPos);
		const FHoverResult MacroHover =
		    Workspace.HoverDocument("ue-metadata.vs", MacroPos);
		const FHoverResult HostTypeAliasHover =
		    Workspace.HoverDocument("ue-metadata.vs", HostTypeAliasPos);
		if (!Expect(
		        AnnotationHover.Contents.find("annotation UFUNCTION") != std::string::npos && AnnotationHover.Contents.find("Unreal reflection") != std::string::npos && ArgumentHover.Contents.find("annotation argument UFUNCTION.BlueprintCallable") != std::string::npos && ArgumentHover.Contents.find("Blueprint graphs") != std::string::npos && MacroHover.Contents.find("macro UE_LOG") != std::string::npos && MacroHover.Contents.find("Predefined macro from Unreal Engine") != std::string::npos && HostTypeAliasHover.Contents.find("type alias FHostNoise") != std::string::npos,
		        "hover exposes annotation, macro, and semantic type-alias information"))
		{
			std::cerr
			    << "annotation hover: " << AnnotationHover.Contents
			    << "\nargument hover: " << ArgumentHover.Contents
			    << "\nmacro hover: " << MacroHover.Contents
			    << "\nhost alias hover: " << HostTypeAliasHover.Contents
			    << '\n';
			return EXIT_FAILURE;
		}

		constexpr const char* ValueSource =
		    "UFUNCTION(Category = BlueprintCallable)\n"
		    "void Other() {}\n";
		Workspace.OpenDocument("ue-metadata-value.vs", ValueSource);
		FTextPosition ValuePos;
		if (!Expect(
		        FindPosition(ValueSource, "BlueprintCallable", ValuePos),
		        "locate annotation value token"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult ValueHover =
		    Workspace.HoverDocument("ue-metadata-value.vs", ValuePos);
		if (!Expect(
		        ValueHover.Contents.find("UFUNCTION.BlueprintCallable") == std::string::npos,
		        "annotation values must not be mistaken for argument names"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* NestedMetadataSource =
		    "UFUNCTION(meta=(DisplayName=\"Run Test\"))\n"
		    "void NestedMetadata() {}\n";
		Workspace.OpenDocument("ue-nested-metadata.vs", NestedMetadataSource);
		FTextPosition NestedDisplayNamePos;
		if (!Expect(
		        FindPosition(
		            NestedMetadataSource,
		            "DisplayName",
		            NestedDisplayNamePos),
		        "locate nested annotation metadata key"))
		{
			return EXIT_FAILURE;
		}
		const FSemanticTokensResult NestedSemantic =
		    Workspace.SemanticTokensDocument("ue-nested-metadata.vs");
		bool bNestedDisplayNameIsArgument = false;
		for (const FSemanticToken& Token : NestedSemantic.Tokens)
		{
			bNestedDisplayNameIsArgument |=
			    Token.Range.StartLine == NestedDisplayNamePos.Line && Token.Range.StartColumn == NestedDisplayNamePos.Column && Token.Kind == EOutlineSymbolKind::AnnotationArgument;
		}
		const FHoverResult NestedDisplayNameHover =
		    Workspace.HoverDocument(
		        "ue-nested-metadata.vs",
		        NestedDisplayNamePos);
		if (!Expect(
		        NestedSemantic.bSuccess && bNestedDisplayNameIsArgument && NestedDisplayNameHover.Contents.find("annotation argument UFUNCTION.meta.DisplayName") != std::string::npos && NestedDisplayNameHover.Contents.find("displayed function name") != std::string::npos,
		        "nested annotation metadata receives semantic color and hover"))
		{
			std::cerr
			    << "nested metadata hover: "
			    << NestedDisplayNameHover.Contents
			    << '\n';
			return EXIT_FAILURE;
		}

		constexpr const char* NestedCompletionSource =
		    "UFUNCTION(meta=(Dis))\n"
		    "void PendingNestedMetadata() {}\n";
		Workspace.OpenDocument(
		    "ue-nested-metadata-completion.vs",
		    NestedCompletionSource);
		FTextPosition NestedCompletionPos;
		if (!Expect(
		        FindPosition(
		            NestedCompletionSource,
		            "Dis",
		            NestedCompletionPos),
		        "locate nested metadata completion prefix"))
		{
			return EXIT_FAILURE;
		}
		NestedCompletionPos.Column += 3;
		const FCompletionResult NestedCompletion =
		    Workspace.CompleteAtDocument(
		        "ue-nested-metadata-completion.vs",
		        NestedCompletionPos);
		bool bNestedHasDisplayName = false;
		for (const FCompletionItem& Item : NestedCompletion.Items)
		{
			bNestedHasDisplayName |= Item.Label == "DisplayName" && Item.Kind == EOutlineSymbolKind::AnnotationArgument;
		}
		if (!Expect(
		        NestedCompletion.bSuccess && bNestedHasDisplayName,
		        "nested annotation metadata completion uses parent catalog"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* CompletionSource =
		    "UFUNCTION(\n"
		    "    \n"
		    ")\n"
		    "void Pending() {}\n";
		Workspace.OpenDocument("ue-metadata-completion.vs", CompletionSource);
		const FCompletionResult ArgumentCompletion =
		    Workspace.CompleteAtDocument(
		        "ue-metadata-completion.vs",
		        FTextPosition{2, 5});
		bool bHasBlueprintCallable = false;
		bool bHasHostNoise = false;
		for (const FCompletionItem& Item : ArgumentCompletion.Items)
		{
			bHasBlueprintCallable |= Item.Label == "BlueprintCallable" && Item.Kind == EOutlineSymbolKind::AnnotationArgument;
			bHasHostNoise |= Item.Label == "HostNoise";
		}
		if (!Expect(
		        ArgumentCompletion.bSuccess && bHasBlueprintCallable && !bHasHostNoise,
		        "annotation argument completion should be structured and context-exclusive"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* GeneralCompletionSource =
		    "void main() { HostN }\n";
		Workspace.OpenDocument(
		    "host-general-completion.vs",
		    GeneralCompletionSource);
		FTextPosition HostPrefixPos;
		if (!Expect(
		        FindPosition(GeneralCompletionSource, "HostN", HostPrefixPos),
		        "locate general host completion prefix"))
		{
			return EXIT_FAILURE;
		}
		HostPrefixPos.Column += 5;
		const FCompletionResult GeneralHostCompletion =
		    Workspace.CompleteAtDocument(
		        "host-general-completion.vs",
		        HostPrefixPos);
		bool bGeneralHasHostNoise = false;
		for (const FCompletionItem& Item : GeneralHostCompletion.Items)
		{
			bGeneralHasHostNoise |= Item.Label == "HostNoise";
		}
		if (!Expect(
		        GeneralHostCompletion.bSuccess && bGeneralHasHostNoise,
		        "general completion should retain host top-level vocabulary"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* ExpressionAnnotationSource =
		    "void main() {\n"
		    "    UF\n"
		    "}\n";
		Workspace.OpenDocument(
		    "annotation-expression-completion.vs",
		    ExpressionAnnotationSource);
		FTextPosition AnnotationPrefixPos;
		if (!Expect(
		        FindPosition(ExpressionAnnotationSource, "UF", AnnotationPrefixPos),
		        "locate expression annotation prefix"))
		{
			return EXIT_FAILURE;
		}
		AnnotationPrefixPos.Column += 2;
		const FCompletionResult ExpressionAnnotationCompletion =
		    Workspace.CompleteAtDocument(
		        "annotation-expression-completion.vs",
		        AnnotationPrefixPos);
		bool bLeakedAnnotation = false;
		for (const FCompletionItem& Item : ExpressionAnnotationCompletion.Items)
		{
			bLeakedAnnotation |= Item.Label == "UFUNCTION";
		}
		if (!Expect(
		        ExpressionAnnotationCompletion.bSuccess && !bLeakedAnnotation,
		        "annotation vocabulary must not leak into expression completion"))
		{
			return EXIT_FAILURE;
		}

		constexpr const char* TopLevelAnnotationSource =
		    "void helper() {}\n"
		    "UF\n";
		Workspace.OpenDocument(
		    "annotation-top-level-completion.vs",
		    TopLevelAnnotationSource);
		FTextPosition TopLevelAnnotationPos;
		if (!Expect(
		        FindPosition(TopLevelAnnotationSource, "UF", TopLevelAnnotationPos),
		        "locate top-level annotation prefix"))
		{
			return EXIT_FAILURE;
		}
		TopLevelAnnotationPos.Column += 2;
		const FCompletionResult TopLevelAnnotationCompletion =
		    Workspace.CompleteAtDocument(
		        "annotation-top-level-completion.vs",
		        TopLevelAnnotationPos);
		bool bHasTopLevelAnnotation = false;
		for (const FCompletionItem& Item : TopLevelAnnotationCompletion.Items)
		{
			bHasTopLevelAnnotation |= Item.Label == "UFUNCTION";
		}
		if (!Expect(
		        TopLevelAnnotationCompletion.bSuccess && bHasTopLevelAnnotation,
		        "top-level annotations remain available after a completed function"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Navigation follows the compiler's unqualified-call priority: a matching
		// free function wins, otherwise an inherited method is implicit `this`.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "implicit_this.vs",
		    "int Free(int value) { return value + 100; }\n"
		    "class Base {\n"
		    "public:\n"
		    "    int Target(int value) { return value + 1; }\n"
		    "}\n"
		    "class Derived : Base {\n"
		    "public:\n"
		    "    int Run() { return Target(1) + Free(1); }\n"
		    "}\n");

		const FNavigationResult Method = Workspace.NavigateDocument(
		    "implicit_this.vs",
		    FTextPosition{8, 25});
		if (!Expect(
		        Method.bSuccess && !Method.Definitions.empty() && Method.Definitions.front().Name == "Target" && Method.Definitions.front().Kind == EOutlineSymbolKind::Method,
		        "bare inherited method should navigate as implicit this"))
		{
			return EXIT_FAILURE;
		}

		const FNavigationResult Free = Workspace.NavigateDocument(
		    "implicit_this.vs",
		    FTextPosition{8, 37});
		if (!Expect(
		        Free.bSuccess && !Free.Definitions.empty() && Free.Definitions.front().Name == "Free" && Free.Definitions.front().Kind == EOutlineSymbolKind::Function,
		        "free function should keep priority over implicit this"))
		{
			return EXIT_FAILURE;
		}

		Workspace.OpenDocument(
		    "implicit_this_static.vs",
		    "class StaticContext {\n"
		    "public:\n"
		    "    int Target(int value) { return value; }\n"
		    "    static int Bad() { return Target(1); }\n"
		    "}\n");
		const FNavigationResult StaticCall = Workspace.NavigateDocument(
		    "implicit_this_static.vs",
		    FTextPosition{4, 32});
		if (!Expect(
		        StaticCall.bSuccess && StaticCall.Definitions.empty(),
		        "static method must not navigate through implicit this"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "alpha.vs",
		    "void helper() {}\nvoid main() { helper(); }\n");
		Workspace.OpenDocument(
		    "beta.vs",
		    "void main() { print(1); }\n");

		if (!Expect(Workspace.ListDocuments().size() == 2, "two open documents"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Workspace.HasDocument("alpha.vs"), "has alpha"))
		{
			return EXIT_FAILURE;
		}

		const FAnalyzeResult AllOk = Workspace.AnalyzeAll();
		if (!Expect(AllOk.bSuccess, "valid workspace should analyze cleanly"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(AllOk.Diagnostics.empty(), "no diagnostics expected"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "a.vs",
		    "void broken_a() { print(;\nvoid ok_a() { print(1); }\n");
		Workspace.OpenDocument(
		    "b.vs",
		    "void broken_b() { print(;\nvoid ok_b() { print(2); }\n");

		const FAnalyzeResult Aggregated = Workspace.AnalyzeAll();
		if (!Expect(!Aggregated.bSuccess, "broken workspace should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Aggregated.Diagnostics.size() >= 2, "at least one diagnostic per broken file"))
		{
			std::cerr << "VeroScriptWorkspaceTests: got " << Aggregated.Diagnostics.size()
			          << " diagnostics\n";
			return EXIT_FAILURE;
		}

		bool bSawA = false;
		bool bSawB = false;
		for (const FDiagnostic& Diagnostic : Aggregated.Diagnostics)
		{
			if (Diagnostic.FileLabel.find("a.vs") != std::string::npos)
			{
				bSawA = true;
			}
			if (Diagnostic.FileLabel.find("b.vs") != std::string::npos)
			{
				bSawB = true;
			}
		}
		if (!Expect(bSawA && bSawB, "diagnostics should cover both documents"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Overlay: importer sees open buffer of callee module, not disk.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "lib/math.vs",
		    "export void add(int a, int b) { print(a + b); }\n");
		Workspace.OpenDocument(
		    "app.vs",
		    "import \"math.vs\";\nvoid main() { add(1, 2); }\n");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back("lib");
		Workspace.SetOptions(Options);

		const FAnalyzeResult App = Workspace.AnalyzeDocument("app.vs");
		if (!Expect(App.bSuccess, "import via overlay should analyze"))
		{
			for (const FDiagnostic& Diagnostic : App.Diagnostics)
			{
				std::cerr << "  " << Diagnostic.FileLabel << ": " << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		// Overlay-backed navigation / outline / completion.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "lib/math.vs",
		    "export void add(int a, int b) { print(a + b); }\n");
		Workspace.OpenDocument(
		    "app.vs",
		    "import \"math.vs\";\nvoid main() { add(1, 2); }\n");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back("lib");
		Workspace.SetOptions(Options);

		const FOutlineResult Outline = Workspace.OutlineDocument("app.vs");
		if (!Expect(Outline.bSuccess, "outline through overlay should succeed"))
		{
			return EXIT_FAILURE;
		}
		bool bHasSourceImport = false;
		for (const FOutlineSymbol& Symbol : Outline.Symbols)
		{
			bHasSourceImport = bHasSourceImport || (Symbol.Kind == EOutlineSymbolKind::Import && Symbol.Name == "math.vs");
		}
		if (!Expect(
		        bHasSourceImport,
		        "outline should preserve imports written in the source document"))
		{
			return EXIT_FAILURE;
		}

		// caret on `add` in main()
		const FTextPosition AddPos{2, 16};
		const FNavigationResult Nav = Workspace.NavigateDocument("app.vs", AddPos);
		if (!Expect(Nav.bSuccess && Nav.bHasToken, "navigate add via overlay"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!Nav.Definitions.empty() && Nav.Definitions.front().Name == "add", "nav hit add"))
		{
			std::cerr << "VeroScriptWorkspaceTests: nav defs=" << Nav.Definitions.size()
			          << " token=[" << Nav.TokenText << "]\n";
			return EXIT_FAILURE;
		}

		const FHoverResult Hover = Workspace.HoverDocument("app.vs", AddPos);
		if (!Expect(Hover.bSuccess && !Hover.Contents.empty(), "hover add via overlay"))
		{
			return EXIT_FAILURE;
		}

		const FCompletionResult Complete = Workspace.CompleteAtDocument(
		    "app.vs",
		    FTextPosition{2, 15});
		if (!Expect(Complete.bSuccess, "complete via overlay"))
		{
			return EXIT_FAILURE;
		}
		bool bHasAdd = false;
		for (const FCompletionItem& Item : Complete.Items)
		{
			if (Item.Label == "add")
			{
				bHasAdd = true;
				break;
			}
		}
		if (!Expect(bHasAdd, "completion should include overlay export add"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Overlay keyed with absolute-style path still matches relative resolved import.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "D:/proj/lib/math.vs",
		    "export void add(int a, int b) { print(a + b); }\n");
		Workspace.OpenDocument(
		    "app.vs",
		    "import \"math.vs\";\nvoid main() { add(1, 2); }\n");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back("lib");
		Workspace.SetOptions(Options);

		const FAnalyzeResult App = Workspace.AnalyzeDocument("app.vs");
		if (!Expect(App.bSuccess, "absolute overlay key should match relative resolve"))
		{
			for (const FDiagnostic& Diagnostic : App.Diagnostics)
			{
				std::cerr << "  " << Diagnostic.FileLabel << ": " << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}

		const FNavigationResult NavAbs = Workspace.NavigateDocument("app.vs", FTextPosition{2, 16});
		if (!Expect(
		        NavAbs.bSuccess && !NavAbs.Definitions.empty() && NavAbs.Definitions.front().Name == "add" && NavAbs.Definitions.front().FileLabel == "D:/proj/lib/math.vs",
		        "nav FileLabel should use absolute overlay key"))
		{
			if (!NavAbs.Definitions.empty())
			{
				std::cerr << "VeroScriptWorkspaceTests: FileLabel=["
				          << NavAbs.Definitions.front().FileLabel << "]\n";
			}
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptWorkspace Workspace;
		const FAnalyzeResult Missing = Workspace.AnalyzeDocument("gone.vs");
		if (!Expect(!Missing.bSuccess, "missing document should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        !Missing.Diagnostics.empty() && Missing.Diagnostics.front().Message.find("not open") != std::string::npos,
		        "missing document message"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Multi-diagnostic parse recovery in a single buffer.
		const FAnalyzeResult Multi = VeroScriptAnalyzer::AnalyzeSource(
		    "void bad1() { print(;\n"
		    "void good() { print(1); }\n"
		    "void bad2() { print(;\n",
		    "multi.vs");
		if (!Expect(!Multi.bSuccess, "multi parse errors should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Multi.Diagnostics.size() >= 2, "parse recovery should report multiple errors"))
		{
			std::cerr << "VeroScriptWorkspaceTests: multi-diag count="
			          << Multi.Diagnostics.size() << '\n';
			for (const FDiagnostic& Diagnostic : Multi.Diagnostics)
			{
				std::cerr << "  L" << Diagnostic.Range.StartLine << ": "
				          << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
	}

	{
		// Import visibility: only direct-import (+ re-export) modules' exports are queryable.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "util.vs",
		    "export void Secret() { print(1); }\n");
		Workspace.OpenDocument(
		    "a.vs",
		    "import \"util.vs\";\nvoid main() { Secret(); }\n");
		Workspace.OpenDocument(
		    "b.vs",
		    "void main() { print(2); }\n");

		const FNavigationResult NavA = Workspace.NavigateDocument("a.vs", FTextPosition{2, 16});
		if (!Expect(
		        NavA.bSuccess && !NavA.Definitions.empty() && NavA.Definitions.front().Name == "Secret",
		        "a.vs should navigate to imported Secret"))
		{
			return EXIT_FAILURE;
		}

		const FCompletionResult CompleteB = Workspace.CompleteAtDocument("b.vs", FTextPosition{1, 14});
		if (!Expect(CompleteB.bSuccess, "b.vs complete should succeed"))
		{
			return EXIT_FAILURE;
		}
		for (const FCompletionItem& Item : CompleteB.Items)
		{
			if (Item.Label == "Secret")
			{
				return Fail("b.vs must not complete invisible util.Secret");
			}
		}
	}

	{
		// Tooling uses the same transitive re-export closure as runtime compilation.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "lib.vs",
		    "export void Shared() { print(1); }\n"
		    "void InternalOnly() { print(2); }\n");
		Workspace.OpenDocument(
		    "facade.vs",
		    "export import \"lib.vs\";\n");
		Workspace.OpenDocument(
		    "app.vs",
		    "import \"facade.vs\";\nvoid main() { Shared(); }\n");

		const FAnalyzeResult Analysis = Workspace.AnalyzeDocument("app.vs");
		if (!Expect(Analysis.bSuccess, "workspace should analyze a re-exported function"))
		{
			for (const FDiagnostic& Diagnostic : Analysis.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
		const FNavigationResult Navigation =
		    Workspace.NavigateDocument("app.vs", FTextPosition{2, 16});
		if (!Expect(
		        Navigation.bSuccess && !Navigation.Definitions.empty() && Navigation.Definitions.front().Name == "Shared" && Navigation.Definitions.front().FileLabel.find("lib.vs") != std::string::npos,
		        "workspace navigation should follow export import"))
		{
			return EXIT_FAILURE;
		}

		Workspace.OpenDocument(
		    "private_use.vs",
		    "import \"facade.vs\";\nvoid main() { InternalOnly(); }\n");
		const FAnalyzeResult PrivateAnalysis =
		    Workspace.AnalyzeDocument("private_use.vs");
		if (!Expect(
		        !PrivateAnalysis.bSuccess,
		        "workspace must keep non-exported forwarded declarations private"))
		{
			return EXIT_FAILURE;
		}

		Workspace.OpenDocument(
		    "include_facade.vs",
		    "#include \"lib.vs\"\n");
		Workspace.OpenDocument(
		    "include_app.vs",
		    "import \"include_facade.vs\";\nvoid main() { Shared(); }\n");
		const FAnalyzeResult IncludeAnalysis =
		    Workspace.AnalyzeDocument("include_app.vs");
		if (!Expect(
		        IncludeAnalysis.bSuccess,
		        "workspace should apply include re-export semantics"))
		{
			for (const FDiagnostic& Diagnostic : IncludeAnalysis.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			return EXIT_FAILURE;
		}
		const FNavigationResult IncludeNavigation =
		    Workspace.NavigateDocument("include_app.vs", FTextPosition{2, 16});
		if (!Expect(
		        IncludeNavigation.bSuccess && !IncludeNavigation.Definitions.empty() && IncludeNavigation.Definitions.front().Name == "Shared" && IncludeNavigation.Definitions.front().FileLabel.find("lib.vs") != std::string::npos,
		        "workspace navigation should follow include re-export"))
		{
			return EXIT_FAILURE;
		}
		const FHoverResult IncludeHover =
		    Workspace.HoverDocument("include_app.vs", FTextPosition{2, 16});
		if (!Expect(
		        IncludeHover.bSuccess && IncludeHover.Contents.find("function Shared") != std::string::npos && IncludeHover.Contents.find("lib.vs") != std::string::npos,
		        "workspace hover should resolve through include re-export"))
		{
			return EXIT_FAILURE;
		}
		const FCompletionResult IncludeCompletion =
		    Workspace.CompleteAtDocument("include_app.vs", FTextPosition{2, 18});
		bool bCompletesShared = false;
		bool bCompletesInternal = false;
		for (const FCompletionItem& Item : IncludeCompletion.Items)
		{
			bCompletesShared = bCompletesShared || Item.Label == "Shared";
			bCompletesInternal = bCompletesInternal || Item.Label == "InternalOnly";
		}
		if (!Expect(
		        IncludeCompletion.bSuccess && bCompletesShared && !bCompletesInternal,
		        "include re-export completion should expose only exported declarations"))
		{
			return EXIT_FAILURE;
		}
		const FReferencesResult IncludeReferences =
		    Workspace.FindReferencesDocument("include_app.vs", FTextPosition{2, 16});
		bool bHasIncludeDefinition = false;
		for (const FReferenceHit& Hit : IncludeReferences.References)
		{
			bHasIncludeDefinition = bHasIncludeDefinition || Hit.FileLabel.find("lib.vs") != std::string::npos;
		}
		if (!Expect(
		        IncludeReferences.bSuccess && IncludeReferences.References.size() >= 2 && bHasIncludeDefinition,
		        "workspace references should cross include re-export"))
		{
			return EXIT_FAILURE;
		}
		const FSemanticTokensResult IncludeSemantic =
		    Workspace.SemanticTokensDocument("include_app.vs");
		bool bHasSharedFunctionToken = false;
		for (const FSemanticToken& Token : IncludeSemantic.Tokens)
		{
			bHasSharedFunctionToken = bHasSharedFunctionToken || (Token.Range.StartLine == 2 && Token.Range.StartColumn == 15 && Token.Kind == EOutlineSymbolKind::Function);
		}
		if (!Expect(
		        IncludeSemantic.bSuccess && bHasSharedFunctionToken,
		        "workspace semantic tokens should classify include re-export uses"))
		{
			return EXIT_FAILURE;
		}
		Workspace.OpenDocument(
		    "include_private_use.vs",
		    "import \"include_facade.vs\";\nvoid main() { InternalOnly(); }\n");
		const FAnalyzeResult IncludePrivateAnalysis =
		    Workspace.AnalyzeDocument("include_private_use.vs");
		if (!Expect(
		        !IncludePrivateAnalysis.bSuccess,
		        "include re-export must keep target private declarations hidden"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Cross-document find references via Workspace overlays.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "lib/math.vs",
		    "export void add(int a, int b) { print(a + b); }\n");
		Workspace.OpenDocument(
		    "app.vs",
		    "import \"math.vs\";\nvoid main() { add(1, 2); add(3, 4); }\n");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back("lib");
		Workspace.SetOptions(Options);

		const FReferencesResult Refs = Workspace.FindReferencesDocument("app.vs", FTextPosition{2, 16});
		if (!Expect(Refs.bSuccess && Refs.TokenText == "add", "cross-doc refs token"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Refs.References.size() >= 3, "def + two calls across overlays"))
		{
			std::cerr << "VeroScriptWorkspaceTests: refs count=" << Refs.References.size() << '\n';
			for (const FReferenceHit& Hit : Refs.References)
			{
				std::cerr << "  " << Hit.FileLabel << " L" << Hit.Range.StartLine
				          << " def=" << Hit.bIsDefinition << '\n';
			}
			return EXIT_FAILURE;
		}

		bool bSawLibDef = false;
		int AppHits = 0;
		for (const FReferenceHit& Hit : Refs.References)
		{
			if (Hit.FileLabel.find("math.vs") != std::string::npos && Hit.bIsDefinition)
			{
				bSawLibDef = true;
			}
			if (Hit.FileLabel.find("app.vs") != std::string::npos)
			{
				++AppHits;
			}
		}
		if (!Expect(bSawLibDef, "should mark definition in math.vs"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(AppHits >= 2, "should find both calls in app.vs"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Homonym in an unrelated open buffer must not appear as a reference.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(
		    "lib/math.vs",
		    "export void add(int a, int b) { print(a + b); }\n");
		Workspace.OpenDocument(
		    "app.vs",
		    "import \"math.vs\";\nvoid main() { add(1, 2); }\n");
		Workspace.OpenDocument(
		    "noise.vs",
		    "void add(int x, int y) { print(x); }\nvoid main() { add(9, 9); }\n");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back("lib");
		Workspace.SetOptions(Options);

		const FReferencesResult Refs = Workspace.FindReferencesDocument("app.vs", FTextPosition{2, 16});
		if (!Expect(Refs.bSuccess && Refs.TokenText == "add", "homonym filter token"))
		{
			return EXIT_FAILURE;
		}
		for (const FReferenceHit& Hit : Refs.References)
		{
			if (Hit.FileLabel.find("noise.vs") != std::string::npos)
			{
				return Fail("unrelated open noise.vs must not appear in add refs");
			}
		}
		bool bSawMath = false;
		bool bSawApp = false;
		for (const FReferenceHit& Hit : Refs.References)
		{
			if (Hit.FileLabel.find("math.vs") != std::string::npos)
			{
				bSawMath = true;
			}
			if (Hit.FileLabel.find("app.vs") != std::string::npos)
			{
				bSawApp = true;
			}
		}
		if (!Expect(bSawMath && bSawApp, "refs should still cover math + app"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Definition on disk (not open) still appears in FindReferences via SymbolQuery FileLabel.
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-refs-disk";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot / "lib");
		{
			std::ofstream Lib(TempRoot / "lib" / "math.vs");
			Lib << "export void add(int a, int b) { print(a + b); }\n";
		}

		VeroScriptWorkspace Workspace;
		const std::string AppPath = (TempRoot / "app.vs").string();
		const std::string LibPath = (TempRoot / "lib").string();
		Workspace.OpenDocument(
		    AppPath,
		    "import \"math.vs\";\nvoid main() { add(1, 2); }\n");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImportPaths.push_back(LibPath);
		Workspace.SetOptions(Options);

		const FReferencesResult Refs = Workspace.FindReferencesDocument(AppPath, FTextPosition{2, 16});
		if (!Expect(Refs.bSuccess && Refs.TokenText == "add", "disk-def refs token"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		bool bSawDiskDef = false;
		for (const FReferenceHit& Hit : Refs.References)
		{
			if (Hit.bIsDefinition && Hit.FileLabel.find("math.vs") != std::string::npos)
			{
				bSawDiskDef = true;
			}
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(bSawDiskDef, "should scan on-disk definition file"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Every IDE query must resolve a bare instance-method call through the same
		// implicit-this fallback as the compiler. Ordinary free functions still win
		// over equally named methods, matching runtime call priority.
		constexpr const char* Source = R"(
int Shadowed(int Value) { return Value + 100; }
class Base {
public:
	int Member(int Value) { return Value; }
	int Shadowed(int Value) { return Value + 1; }
}
class Derived : Base {
public:
	int Run() {
		return Member(1) + this.Member(2)
			+ Shadowed(3) + this.Shadowed(4);
	}
}
)";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("implicit-this.vs", Source);

		FTextPosition BareMember;
		FTextPosition ExplicitMember;
		FTextPosition BareShadowed;
		FTextPosition ExplicitShadowed;
		if (!Expect(
		        FindPosition(Source, "Member(1)", BareMember) && FindPosition(Source, "Member(2)", ExplicitMember) && FindPosition(Source, "Shadowed(3)", BareShadowed) && FindPosition(Source, "Shadowed(4)", ExplicitShadowed),
		        "locate implicit-this workspace query sites"))
		{
			return EXIT_FAILURE;
		}

		const FNavigationResult BareNav =
		    Workspace.NavigateDocument("implicit-this.vs", BareMember);
		const FNavigationResult ExplicitNav =
		    Workspace.NavigateDocument("implicit-this.vs", ExplicitMember);
		if (!Expect(
		        BareNav.bSuccess && !BareNav.Definitions.empty() && BareNav.Definitions.front().Kind == EOutlineSymbolKind::Method && BareNav.Definitions.front().Name == "Member" && ExplicitNav.bSuccess && !ExplicitNav.Definitions.empty() && ExplicitNav.Definitions.front().DefinitionRange.StartLine == BareNav.Definitions.front().DefinitionRange.StartLine,
		        "bare and explicit calls navigate to the same inherited method"))
		{
			return EXIT_FAILURE;
		}

		const FHoverResult Hover =
		    Workspace.HoverDocument("implicit-this.vs", BareMember);
		if (!Expect(
		        Hover.bSuccess && !Hover.Definitions.empty() && Hover.Definitions.front().Kind == EOutlineSymbolKind::Method && Hover.Contents.find("Member") != std::string::npos,
		        "hover resolves bare implicit-this method"))
		{
			return EXIT_FAILURE;
		}

		const FReferencesResult References =
		    Workspace.FindReferencesDocument("implicit-this.vs", BareMember);
		if (!Expect(
		        References.bSuccess && References.References.size() == 3,
		        "references include method declaration, bare call, and explicit call"))
		{
			std::cerr << "VeroScriptWorkspaceTests: implicit Member refs="
			          << References.References.size() << '\n';
			return EXIT_FAILURE;
		}

		const FDocumentHighlightResult Highlights =
		    Workspace.HighlightDocument("implicit-this.vs", BareMember);
		if (!Expect(
		        Highlights.bSuccess && Highlights.Highlights.size() == 3,
		        "document highlights include all implicit-this method occurrences"))
		{
			return EXIT_FAILURE;
		}

		const FRenameResult Rename =
		    Workspace.RenameDocument("implicit-this.vs", BareMember, "RenamedMember");
		if (!Expect(
		        Rename.bSuccess && Rename.bCanRename && Rename.OldName == "Member" && Rename.Edits.size() == 3,
		        "rename updates declaration, bare call, and explicit call"))
		{
			return EXIT_FAILURE;
		}

		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("implicit-this.vs");
		bool bBareMemberIsMethod = false;
		for (const FSemanticToken& Token : Semantic.Tokens)
		{
			if (Token.Range.StartLine == BareMember.Line && Token.Range.StartColumn == BareMember.Column && Token.Kind == EOutlineSymbolKind::Method)
			{
				bBareMemberIsMethod = true;
				break;
			}
		}
		if (!Expect(
		        Semantic.bSuccess && bBareMemberIsMethod,
		        "semantic tokens classify bare implicit-this call as a method"))
		{
			return EXIT_FAILURE;
		}

		const FNavigationResult BareShadowedNav =
		    Workspace.NavigateDocument("implicit-this.vs", BareShadowed);
		const FNavigationResult ExplicitShadowedNav =
		    Workspace.NavigateDocument("implicit-this.vs", ExplicitShadowed);
		if (!Expect(
		        BareShadowedNav.bSuccess && !BareShadowedNav.Definitions.empty() && BareShadowedNav.Definitions.front().Kind == EOutlineSymbolKind::Function && BareShadowedNav.Definitions.front().ContainerName.empty() && ExplicitShadowedNav.bSuccess && !ExplicitShadowedNav.Definitions.empty() && ExplicitShadowedNav.Definitions.front().Kind == EOutlineSymbolKind::Method,
		        "ordinary free function shadows implicit method but not explicit receiver"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Workspace analysis and navigation must use the same session-level implicit
		// import and project-internal visibility rules as runtime compilation.
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-implicit-import";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::string MainPath = (TempRoot / "main.vs").generic_string();
		const std::string LibPath = (TempRoot / "shared.vs").generic_string();
		constexpr std::string_view LibSource =
		    "export int public_value() { return 7; }\n"
		    "int internal_value() { return 8; }\n";

		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(LibPath, LibSource);
		Workspace.OpenDocument(
		    MainPath,
		    "void main() { print(public_value()); }\n");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImplicitImportPaths.push_back(LibPath);
		Workspace.SetOptions(Options);

		const FAnalyzeResult PublicAnalysis = Workspace.AnalyzeDocument(MainPath);
		if (!Expect(
		        PublicAnalysis.bSuccess,
		        "workspace implicit import should expose exported declarations"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		const FOutlineResult PublicOutline = Workspace.OutlineDocument(MainPath);
		bool bHasImplicitImportNode = false;
		for (const FOutlineSymbol& Symbol : PublicOutline.Symbols)
		{
			bHasImplicitImportNode = bHasImplicitImportNode || Symbol.Kind == EOutlineSymbolKind::Import;
		}
		if (!Expect(
		        PublicOutline.bSuccess && !bHasImplicitImportNode,
		        "outline should hide configuration-level implicit imports"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		const FNavigationResult PublicNav =
		    Workspace.NavigateDocument(MainPath, FTextPosition{1, 21});
		if (!Expect(
		        PublicNav.bSuccess && !PublicNav.Definitions.empty() && PublicNav.Definitions.front().FileLabel.find("shared.vs") != std::string::npos,
		        "workspace navigation should find implicit imported declaration"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		Workspace.UpdateDocument(
		    MainPath,
		    "void main() { print(internal_value()); }\n");
		const FAnalyzeResult PrivateAnalysis = Workspace.AnalyzeDocument(MainPath);
		if (!Expect(
		        !PrivateAnalysis.bSuccess,
		        "workspace implicit imports should preserve export by default"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		Options.bAllowProjectWideAccess = true;
		Workspace.SetOptions(Options);
		const FAnalyzeResult ProjectAnalysis = Workspace.AnalyzeDocument(MainPath);
		const FNavigationResult ProjectNav =
		    Workspace.NavigateDocument(MainPath, FTextPosition{1, 21});
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(
		        ProjectAnalysis.bSuccess && ProjectNav.bSuccess && !ProjectNav.Definitions.empty() && ProjectNav.Definitions.front().Name == "internal_value",
		        "workspace project-wide access should expose internal declarations"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Binary libraries must enter every tooling query through the same module
		// loader path used by runtime compilation. This covers IDE-configured
		// implicit .vslib dependencies without exposing their implementation source.
		std::error_code RemoveError;
		const std::filesystem::path TempRoot =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-binary-package";
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path PackagePath =
		    TempRoot / "Tooling.Library.vslib";
		const std::string PackagePathString = PackagePath.generic_string();
		const std::string MainPath =
		    (TempRoot / "main.vs").generic_string();

		VeroScript::FCompileResult Library =
		    VeroScript::VeroScriptCompile::CompileSource(
		        "namespace package::api {\n"
		        "export struct Value { int amount; }\n"
		        "export struct Box<T> {\n"
		        "  T value;\n"
		        "  Box(T initial) { value = initial; }\n"
		        "  T get() const { return value; }\n"
		        "}\n"
		        "export int shared = 40;\n"
		        "}\n",
		        (TempRoot / "library.vs").generic_string());
		VeroScript::FPackageBuildOptions PackageOptions;
		PackageOptions.Kind = VeroScript::EPackageKind::Library;
		PackageOptions.PackageName = "Tooling.Library";
		std::vector<std::uint8_t> PackageBytes;
		std::string PackageError;
		if (!Expect(
		        Library.bSuccess && Library.Module && VeroScript::BuildPackage(*Library.Module, PackageOptions, PackageBytes, PackageError),
		        "build tooling binary-library fixture"))
		{
			std::cerr << PackageError << '\n';
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		{
			std::ofstream Output(PackagePath, std::ios::binary | std::ios::trunc);
			Output.write(
			    reinterpret_cast<const char*>(PackageBytes.data()),
			    static_cast<std::streamsize>(PackageBytes.size()));
			if (!Expect(
			        Output.good(),
			        "write tooling binary-library fixture"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}

		constexpr std::string_view ImportCompletionSource =
		    "import \"Tool\";\n";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(MainPath, ImportCompletionSource);
		FTextPosition ImportCompletionPosition;
		const bool bFoundImportPrefix =
		    FindPosition(ImportCompletionSource, "Tool", ImportCompletionPosition);
		ImportCompletionPosition.Column += 4;
		const FCompletionResult ImportCompletion =
		    Workspace.CompleteAtDocument(MainPath, ImportCompletionPosition);
		bool bCompletesBinaryPackage = false;
		for (const FCompletionItem& Item : ImportCompletion.Items)
		{
			bCompletesBinaryPackage |=
			    Item.Label == "Tooling.Library.vslib" && Item.Detail == "binary package";
		}
		if (!Expect(
		        bFoundImportPrefix && ImportCompletion.bSuccess && bCompletesBinaryPackage,
		        "import completion should include binary-library packages"))
		{
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		constexpr std::string_view MainSource =
		    "void main() {\n"
		    "  package::api::Value value();\n"
		    "  package::api::Box<int> box(40);\n"
		    "  value.amount = 2;\n"
		    "  print(value.amount + package::api::shared + box.get());\n"
		    "}\n";
		Workspace.UpdateDocument(MainPath, MainSource);
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImplicitImportPaths.push_back(PackagePathString);
		Workspace.SetOptions(Options);
		const FAnalyzeResult Analysis = Workspace.AnalyzeDocument(MainPath);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument(MainPath);
		FTextPosition TypePosition;
		const bool bFoundType =
		    FindPosition(MainSource, "Value", TypePosition);
		bool bTypeToken = false;
		for (const FSemanticToken& Token : Semantic.Tokens)
		{
			bTypeToken |=
			    Token.Range.StartLine == TypePosition.Line && Token.Range.StartColumn == TypePosition.Column && Token.Kind == EOutlineSymbolKind::Struct;
		}
		const FHoverResult Hover =
		    Workspace.HoverDocument(MainPath, TypePosition);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(
		        Analysis.bSuccess && Semantic.bSuccess && bFoundType && bTypeToken && Hover.Contents.find("struct package.api.Value") != std::string::npos,
		        "workspace should analyze and classify implicit binary-library types"))
		{
			for (const FDiagnostic& Diagnostic : Analysis.Diagnostics)
			{
				std::cerr << Diagnostic.Message << '\n';
			}
			std::cerr
			    << "semantic=" << Semantic.bSuccess
			    << " foundType=" << bFoundType
			    << " typeToken=" << bTypeToken
			    << " hover=" << Hover.Contents << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		// C-style cast sugar must retain the target type's semantic identity so
		// IDE coloring and hover do not regress merely because the parser lowers
		// the expression to the existing cast<T> intrinsic.
		constexpr std::string_view Source =
		    "struct Token { explicit Token(int value) {} };\n"
		    "void main() { int source = 1; Token value = (Token)source; (void)value; }\n";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("c-style-cast-semantic.vs", Source);
		const FSemanticTokensResult Semantic =
		    Workspace.SemanticTokensDocument("c-style-cast-semantic.vs");
		FTextPosition CastTypePosition;
		const bool bFoundCastType =
		    FindPosition(Source, "(Token)source", CastTypePosition);
		++CastTypePosition.Column;
		bool bCastTypeToken = false;
		for (const FSemanticToken& Token : Semantic.Tokens)
		{
			bCastTypeToken |=
			    Token.Range.StartLine == CastTypePosition.Line && Token.Range.StartColumn == CastTypePosition.Column && Token.Kind == EOutlineSymbolKind::Struct;
		}
		const FHoverResult Hover = Workspace.HoverDocument(
		    "c-style-cast-semantic.vs", CastTypePosition);
		if (!Expect(
		        Semantic.bSuccess && bFoundCastType && bCastTypeToken && Hover.bSuccess && Hover.Contents.find("struct Token") != std::string::npos,
		        "C-style cast targets should preserve semantic color and hover"))
		{
			std::cerr
			    << "semantic=" << Semantic.bSuccess
			    << " found=" << bFoundCastType
			    << " token=" << bCastTypeToken
			    << " hover=" << Hover.Contents << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		constexpr std::string_view Source =
		    "interface IValue { virtual int Get() = 0; }\n"
		    "class Value : public IValue { int Get() { return 1; } }\n";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("cancel-implementation.vs", Source);
		FTextPosition Position;
		if (!Expect(FindPosition(Source, "Get", Position), "locate implementation target"))
		{
			return EXIT_FAILURE;
		}
		FWorkspaceSymbolsOptions SymbolOptions;
		SymbolOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		SymbolOptions.ShouldCancel = []()
		{
			return true;
		};
		const FNavigationResult Cancelled = Workspace.ImplementationDocument(
		    "cancel-implementation.vs", Position, SymbolOptions);
		if (!Expect(
		        Cancelled.bCancelled && Cancelled.Definitions.empty(),
		        "implementation lookup propagates cooperative workspace cancellation"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Function arrows and nested commas remain part of a generic type argument
		// when locating implementations across a specialized inheritance chain.
		for (const bool bIndirect : {false, true})
		{
			const std::string Argument = bIndirect ? "fn(int,int)->int" : "handle<fn(int,int)->int>";
			const std::string Source =
			    "class Base<T>{public:virtual int Apply(T value){return 1;}};\n"
			    "class Middle<U>:public Base<U>{};\n"
			    "class Child:public " +
			    (bIndirect ? std::string("Middle") : std::string("Base")) + "<" + Argument + ">{public:int Apply(" + Argument + " value) override{return 2;}};\nvoid main(){}";
			VeroScriptWorkspace Workspace;
			Workspace.OpenDocument("callable-generic-implementation.vs", Source);
			FTextPosition Position;
			if (!Expect(FindPosition(Source, "Apply", Position), "locate generic virtual method")) return EXIT_FAILURE;
			const auto Implementations = Workspace.ImplementationDocument("callable-generic-implementation.vs", Position);
			const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Source, "callable-generic-implementation.vs");
			if (!Expect(Compiled.bSuccess && Implementations.bSuccess &&
			            Implementations.Definitions.size() == 1 &&
			            Implementations.Definitions.front().DefinitionRange.StartLine == 3,
			        "compiled callable generic inheritance retains its override implementation"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const std::string DefaultSource : {
		         "class Base<T,U=int>{public:T payload;virtual U Apply(U value){return value;}};\n"
		         "class Child:public Base<string>{public:int Apply(int value) override{return 2;}};\nvoid main(){}",
		         "class Base<T=string,U=int>{public:T payload;virtual U Apply(U value){return value;}};\n"
		         "class Child:public Base<>{public:int Apply(int value) override{return 2;}};\nvoid main(){}"})
		{
			VeroScriptWorkspace DefaultWorkspace;
			DefaultWorkspace.OpenDocument("default-implementation.vs", DefaultSource);
			FTextPosition DefaultTarget;
			if (!Expect(FindPosition(DefaultSource, "Apply", DefaultTarget), "locate default generic target")) return EXIT_FAILURE;
			const auto DefaultImplementations = DefaultWorkspace.ImplementationDocument("default-implementation.vs", DefaultTarget);
			if (!Expect(VeroScript::VeroScriptCompile::CompileSource(DefaultSource, "default-implementation.vs")
			                .bSuccess &&
			            DefaultImplementations.Definitions.size() == 1 &&
			            DefaultImplementations.Definitions.front().DefinitionRange.StartLine == 2,
			        "generic defaults participate in implementation signature substitution"))
			{
				return EXIT_FAILURE;
			}
		}

		const std::string CyclicSource =
		    "class Root{public:virtual int Apply(int x){return x;}};\n"
		    "class Loop<T>:public Loop<array<T>>{public:int Apply(int x){return x;}};\nvoid main(){}";
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("expanding-cycle.vs", CyclicSource);
		FTextPosition Target;
		if (!Expect(FindPosition(CyclicSource, "Apply", Target), "locate cycle test target")) return EXIT_FAILURE;
		const auto Implementations = Workspace.ImplementationDocument("expanding-cycle.vs", Target);
		if (!Expect(Implementations.Definitions.empty() && !Implementations.bCancelled,
		        "invalid expanding generic cycles terminate without recursion or invented implementations"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Standard-library roots must not be flattened into ordinary import paths:
		// their module provenance controls visibility, namespace metadata and docs.
		const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path Root = std::filesystem::temp_directory_path() / ("veroscript-workspace-stdlib-" + std::to_string(Unique));
		std::error_code Error;
		std::filesystem::create_directories(Root, Error);
		{
			std::ofstream Module(Root / "workspace_custom.vs", std::ios::binary | std::ios::trunc);
			Module << "export int workspace_stdlib_value() { return 73; }\n";
		}
		VeroScriptWorkspace Workspace;
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.StdlibSearchPaths.push_back(Root.generic_string());
		Workspace.SetOptions(Options);
		Workspace.OpenDocument(
		    "workspace-stdlib-main.vs",
		    "import \"workspace_custom.vs\";\n"
		    "int main() { return workspace_stdlib_value(); }\n");
		const FAnalyzeResult Analysis = Workspace.AnalyzeDocument("workspace-stdlib-main.vs");
		std::filesystem::remove_all(Root, Error);
		if (!Expect(Analysis.bSuccess, "workspace preserves custom standard-library roots"))
		{
			return EXIT_FAILURE;
		}
	}

	#ifdef _WIN32
	{
		// Windows URI/file-system casing differences must not let a disk snapshot
		// replace the editor's newer in-memory overlay for the same document.
		std::error_code Error;
		const std::filesystem::path Root =
		    std::filesystem::temp_directory_path() / "VeroScriptTooling-overlay-case";
		std::filesystem::remove_all(Root, Error);
		std::filesystem::create_directories(Root, Error);
		const std::filesystem::path DiskPath = Root / "MixedCase.vs";
		{
			std::ofstream Output(DiskPath, std::ios::binary | std::ios::trunc);
			Output << "void DiskOnly() {}\n";
		}

		std::string OverlayPath = DiskPath.generic_string();
		std::transform(
		    OverlayPath.begin(), OverlayPath.end(), OverlayPath.begin(),
		    [](const unsigned char Character)
		{
			return static_cast<char>(std::tolower(Character));
		});
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument(OverlayPath, "void OverlayOnly() {}\n");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.WorkspaceRoots.push_back(Root.generic_string());
		Workspace.SetOptions(Options);
		const FWorkspaceSymbolsResult Symbols = Workspace.WorkspaceSymbols();
		bool bHasOverlay = false;
		bool bHasDisk = false;
		for (const FWorkspaceSymbol& Symbol : Symbols.Symbols)
		{
			bHasOverlay |= Symbol.Name == "OverlayOnly";
			bHasDisk |= Symbol.Name == "DiskOnly";
		}
		std::filesystem::remove_all(Root, Error);
		if (!Expect(
		        Symbols.bSuccess && bHasOverlay && !bHasDisk,
		        "workspace indexing preserves case-insensitive in-memory overlay precedence"))
		{
			return EXIT_FAILURE;
		}
	}
	#endif

	{
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("cancel-analysis.vs", "int main() { return 1; }\n");
		FSemanticTokensOptions Control;
		Control.ShouldCancel = []()
		{
			return true;
		};
		const auto Cancelled = Workspace.SemanticTokensDocument("cancel-analysis.vs", Control);
		int Checks = 0;
		Control.ShouldCancel = [&Checks]()
		{
			return ++Checks >= 8;
		};
		std::string Source;
		for (int Index = 0; Index < 500; ++Index)
		{
			Source += "int Function" + std::to_string(Index) + "(int x) { return x + 1; }\n";
		}
		Workspace.UpdateDocument("cancel-analysis.vs", Source);
		const auto DuringWork = Workspace.SemanticTokensDocument("cancel-analysis.vs", Control);
		Workspace.UpdateDocument("cancel-analysis.vs", "int main() { return 1; }\n");
		Control.ShouldCancel = {};
		Control.RecordStage = [](std::string_view, double)
		{
			throw std::runtime_error("observer");
		};
		const auto Normal = Workspace.SemanticTokensDocument("cancel-analysis.vs", Control);
		if (!Expect(Cancelled.bCancelled && Cancelled.Tokens.empty() && DuringWork.bCancelled && Checks >= 8 && DuringWork.Tokens.empty() && Normal.bSuccess && !Normal.bCancelled && !Normal.Tokens.empty(),
		        "cooperative cancellation stops analysis and restores scoped state"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// Editing an import after it was parsed must invalidate that AST even
		// when the edit happens before the semantic snapshot is constructed.
		const auto Root = std::filesystem::temp_directory_path() / ("VeroScriptTooling-semantic-disk-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Root);
		const auto Dependency = Root / "dep.vs";
		{
			std::ofstream Output(Dependency, std::ios::binary);
			Output << "export struct Thing { int member; }\n";
		}
		VeroScriptWorkspace Workspace;
		const auto MainPath = (Root / "main.vs").generic_string();
		Workspace.OpenDocument(MainPath,
		    "import \"dep.vs\";\nThing Echo(Thing value) { return value; }\n");
		bool bChanged = false;
		FSemanticTokensOptions Control;
		Control.RecordStage = [&](std::string_view Name, double)
		{
			if (Name == "load" && !bChanged)
			{
				std::ofstream Output(Dependency, std::ios::binary | std::ios::trunc);
				Output << "export enum Thing { Changed }\n";
				bChanged = true;
			}
		};
		const auto Stale = Workspace.SemanticTokensDocument(MainPath, Control);
		const bool bStaleIsCurrent = Detail::IsSemanticQuerySnapshotCurrent(Stale.Snapshot);
		const auto Fresh = Workspace.SemanticTokensDocument(MainPath);
		const bool bFreshIsCurrent = Detail::IsSemanticQuerySnapshotCurrent(Fresh.Snapshot);
		std::error_code Error;
		std::filesystem::remove_all(Root, Error);
		if (!Expect(bChanged && Stale.bSuccess && Stale.Snapshot && !bStaleIsCurrent && Fresh.bSuccess && bFreshIsCurrent,
		        "disk stamps must describe the imported bytes at read time, not after parsing"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// The iterative core loader must preserve cyclic editor overlays and
		// recover when a dependency is temporarily invalid during an edit.
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("cycle/main.vs",
		    "import \"helper.vs\";\n"
		    "export int seed() { return 40; }\n"
		    "void main() { print(answer()); }\n");
		const std::string Helper =
		    "import \"main.vs\";\n"
		    "export int answer() { return seed() + 2; }\n";
		Workspace.OpenDocument("cycle/helper.vs", Helper);
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.bAllowCircularImports = true;
		Workspace.SetOptions(Options);
		const auto Analyzed = Workspace.AnalyzeDocument("cycle/main.vs");
		const auto Navigation = Workspace.NavigateDocument("cycle/main.vs", {3, 22});
		const auto Tokens = Workspace.SemanticTokensDocument("cycle/main.vs");
		if (!Expect(Analyzed.bSuccess && Tokens.bSuccess && !Tokens.Tokens.empty() && Navigation.bSuccess && !Navigation.Definitions.empty() && Navigation.Definitions.front().FileLabel == "cycle/helper.vs" && Navigation.Definitions.front().Name == "answer",
		        "cyclic overlays preserve analysis, semantic tokens and imported navigation"))
		{
			return EXIT_FAILURE;
		}
		Workspace.UpdateDocument("cycle/helper.vs", "export int answer( {");
		const auto Invalid = Workspace.AnalyzeDocument("cycle/main.vs");
		Workspace.UpdateDocument("cycle/helper.vs", Helper);
		const auto Restored = Workspace.AnalyzeDocument("cycle/main.vs");
		const auto RestoredTokens = Workspace.SemanticTokensDocument("cycle/main.vs");
		const auto RestoredNavigation = Workspace.NavigateDocument("cycle/main.vs", {3, 22});
		const bool bSameTokens = Tokens.Tokens.size() == RestoredTokens.Tokens.size() && std::equal(Tokens.Tokens.begin(), Tokens.Tokens.end(), RestoredTokens.Tokens.begin(), [](const FSemanticToken& Before, const FSemanticToken& After)
		{
			return Before.Kind == After.Kind && Before.Modifiers == After.Modifiers && Before.Range.StartLine == After.Range.StartLine && Before.Range.StartColumn == After.Range.StartColumn && Before.Range.EndLine == After.Range.EndLine && Before.Range.EndColumn == After.Range.EndColumn;
		});
		if (!Expect(!Invalid.bSuccess && !Invalid.Diagnostics.empty() && Restored.bSuccess && Restored.Diagnostics.empty() && RestoredTokens.bSuccess && bSameTokens && RestoredNavigation.bSuccess && !RestoredNavigation.Definitions.empty() && RestoredNavigation.Definitions.front().FileLabel == "cycle/helper.vs" && RestoredNavigation.Definitions.front().Name == "answer",
		        "repairing a cyclic import clears stale parse errors and semantic state"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// A request can select a compiler VM without overriding extension roots
		// or policy. That selection must not be overwritten by workspace defaults.
		const auto Suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
		const auto ExtensionPath = std::filesystem::temp_directory_path() / ("vero-vm-selection-" + Suffix + ".vs");
		struct FRemoveFixture
		{
			std::filesystem::path Path;
			~FRemoveFixture()
			{
				std::error_code Error;
				std::filesystem::remove(Path, Error);
			}
		} RemoveFixture{ExtensionPath};
		{
			std::ofstream File(ExtensionPath, std::ios::binary);
			File << "import \"compiler.vs\";export void register_extension(std::compiler::registration reg){}";
			if (!Expect(File.good(), "write compiler VM selection fixture")) return EXIT_FAILURE;
		}
		const std::string Label = VeroScript::PathUtil::ToUtf8(ExtensionPath.parent_path() / ("vm-selection-main-" + Suffix + ".vs"));
		const std::string Source = "import compiler \"" + ExtensionPath.filename().string() + "\";int VmSelected(){return 7;}";
		bool Passed = true;
		for (const bool BadWorkspace : {false, true})
		{
			VeroScriptWorkspace Workspace;
			FWorkspaceOptions Options;
			if (BadWorkspace)
			{
				Options.ScriptCompileExtensions.VmBackendPath = "__tooling_missing_compiler_vm__.dll";
			}
			else
			{
				Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			}
			Workspace.SetOptions(Options);
			Workspace.OpenDocument(Label, Source);
			FWorkspaceSymbolsOptions Request;
			Request.bIncludeSoftBuiltins = false;
			if (!BadWorkspace)
			{
				const auto Control = Workspace.WorkspaceSymbols(Request);
				if (!Expect(Control.bSuccess && !Control.Symbols.empty(), "compiler fixture works with workspace VM defaults"))
				{
					for (const auto& Diagnostic : Control.Diagnostics)
					{
						std::cerr << Diagnostic.Message << '\n';
					}
					return EXIT_FAILURE;
				}
			}
			if (BadWorkspace)
			{
				Request.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			}
			else
			{
				Request.ScriptCompileExtensions.VmBackendPath = "__tooling_missing_compiler_vm__.dll";
			}
			const auto Result = Workspace.WorkspaceSymbols(Request);
			const bool Found = std::any_of(Result.Symbols.begin(), Result.Symbols.end(),
			    [](const auto& Symbol)
			{
				return Symbol.Name == "VmSelected";
			});
			Passed = Expect(BadWorkspace ? Result.bSuccess && Found : !Result.bSuccess && !Result.Diagnostics.empty(),
			             BadWorkspace ? "explicit compiler VM factory overrides workspace path" : "explicit compiler VM path overrides workspace factory") &&
			    Passed;
			Request.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			Request.ScriptCompileExtensions.VmBackendPath = "__tooling_missing_compiler_vm__.dll";
			const auto Conflict = Workspace.WorkspaceSymbols(Request);
			Passed = Expect(!Conflict.bSuccess && !Conflict.Diagnostics.empty(),
			             "conflicting compiler VM request is rejected rather than replaced by defaults") &&
			    Passed;
		}
		VeroScriptWorkspace Ordinary;
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Ordinary.SetOptions(Options);
		Ordinary.OpenDocument("vm-selection/ordinary.vs", "int Ordinary(){return 7;}");
		const auto Symbols = Ordinary.WorkspaceSymbols();
		Passed = Expect(Symbols.bSuccess && Symbols.bCacheable,
		             "selecting an unused compiler VM preserves ordinary workspace symbol caching") &&
		    Passed;
		const auto Tokens = Ordinary.SemanticTokensDocument("vm-selection/ordinary.vs");
		std::size_t Loads = 0;
		{
			VeroScript::FAnalysisControlScope Control([]
			{
				return false;
			}, [&](const std::string_view Stage, double)
			{
				if (Stage == "load")
				{
					++Loads;
				}
			});
			{
				Detail::FSemanticQueryScope Scope(Tokens.Snapshot);
				const auto Hover = Ordinary.HoverDocument("vm-selection/ordinary.vs", {1, 6});
				Passed = Expect(Tokens.bSuccess && Hover.bSuccess && Hover.Contents.find("Ordinary") != std::string::npos && Loads == 0, "an unused selected compiler VM preserves interactive AST reuse") && Passed;
			}
			const auto ColdHover = Ordinary.HoverDocument("vm-selection/ordinary.vs", {1, 6});
			Passed = Expect(ColdHover.bSuccess && Loads > 0, "load observer detects a query outside the cached AST scope") && Passed;
		}
		if (!Passed) return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
