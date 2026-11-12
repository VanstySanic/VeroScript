// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptWorkspace.h"
#include "VeroScriptAnalysisControl.h"

#include "VeroScriptAnalyzer.h"
#include "VeroScriptCallHelp.h"
#include "VeroScriptComplete.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptDocumentHighlight.h"
#include "VeroScriptFindReferences.h"
#include "VeroScriptFormat.h"
#include "VeroScriptHover.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptNavigation.h"
#include "VeroScriptOutline.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptRename.h"
#include "VeroScriptSymbolKindMap.h"
#include "VeroScriptWorkspaceSymbols.h"
#include "Internal/VeroScriptSymbolQuery.h"
#include "Internal/VeroScriptTypeDetail.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript::Tooling
{
	namespace
	{
		std::string NormalizePathKey(std::string Path)
		{
			return VeroScript::PathUtil::Normalize(std::move(Path));
		}

		bool SameDocumentPath(const std::string& Left, const std::string& Right)
		{
			return VeroScript::PathUtil::Equal(Left, Right);
		}

		std::vector<FDocumentOverlay>::iterator FindDocument(
		    std::vector<FDocumentOverlay>& Documents,
		    const std::string& Path)
		{
			return std::find_if(
			    Documents.begin(),
			    Documents.end(),
			    [&Path](const FDocumentOverlay& Document)
			{
				return SameDocumentPath(Document.Path, Path);
			});
		}

		std::vector<FDocumentOverlay>::const_iterator FindDocument(
		    const std::vector<FDocumentOverlay>& Documents,
		    const std::string& Path)
		{
			return std::find_if(
			    Documents.begin(),
			    Documents.end(),
			    [&Path](const FDocumentOverlay& Document)
			{
				return SameDocumentPath(Document.Path, Path);
			});
		}

		FAnalyzeResult MissingDocumentResult(const std::string& Path)
		{
			FAnalyzeResult Result;
			FScriptError Error;
			FillDiagnostic(Error, "diag.document_is_not_open_in_workspace", FSourceLocation{Path, 1, 1});
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, Path));
			return Result;
		}

		bool IsTypeSymbolKind(const EOutlineSymbolKind Kind)
		{
			return Kind == EOutlineSymbolKind::Class || Kind == EOutlineSymbolKind::Struct || Kind == EOutlineSymbolKind::Interface;
		}

		std::string SymbolStem(std::string_view Name)
		{
			const std::size_t Generic = Name.find('<');
			Name = Name.substr(0, Generic);
			while (!Name.empty() && std::isspace(static_cast<unsigned char>(Name.back())))
			{
				Name.remove_suffix(1);
			}
			return std::string(Name);
		}

		std::string_view TrimView(std::string_view Value);

		std::string SubstituteTypeParameters(
		    const std::string_view Type,
		    const std::vector<std::string>& Parameters,
		    const std::vector<std::string>& Arguments)
		{
			if (Parameters.empty() || Parameters.size() != Arguments.size())
			{
				return std::string(Type);
			}
			std::unordered_map<std::string, std::string> Replacements;
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				Replacements.emplace(Parameters[Index], Arguments[Index]);
			}

			std::string Result;
			Result.reserve(Type.size());
			for (std::size_t Index = 0; Index < Type.size();)
			{
				const unsigned char Character = static_cast<unsigned char>(Type[Index]);
				if (!std::isalpha(Character) && Type[Index] != '_')
				{
					Result.push_back(Type[Index++]);
					continue;
				}
				std::size_t End = Index + 1;
				while (End < Type.size())
				{
					const unsigned char Next = static_cast<unsigned char>(Type[End]);
					if (!std::isalnum(Next) && Type[End] != '_')
					{
						break;
					}
					++End;
				}
				const std::string Identifier(Type.substr(Index, End - Index));
				if (const auto Replacement = Replacements.find(Identifier);
				    Replacement != Replacements.end())
				{
					Result += Replacement->second;
				}
				else
				{
					Result += Identifier;
				}
				Index = End;
			}
			return Result;
		}

		std::vector<std::string> SpecializedGenericArguments(
		    const std::string_view Type, const std::string_view Declaration)
		{
			auto Arguments = Detail::GenericArgumentsFromDetail(Type);
			// A bare generic name is not an application in the language. Defaults
			// fill omitted trailing arguments of an explicit application.
			if (Type.find('<') == std::string_view::npos || Type.empty() || Type.back() != '>') return Arguments;
			const auto Formals = Detail::GenericArgumentsFromDetail(Declaration, false);
			auto BoundParameters = Detail::GenericArgumentsFromDetail(Declaration);
			while (Arguments.size() < Formals.size())
			{
				const auto Default = Formals[Arguments.size()].find('=');
				if (Default == std::string::npos)
				{
					break;
				}

				BoundParameters.resize(Arguments.size());
				const auto DefaultType =
				    TrimView(std::string_view(Formals[Arguments.size()]).substr(Default + 1));
				if (DefaultType.empty())
				{
					break;
				}

				Arguments.push_back(SubstituteTypeParameters(DefaultType, BoundParameters, Arguments));
				// The next default may refer to the parameter just bound.
				BoundParameters = Detail::GenericArgumentsFromDetail(Declaration);
			}
			return Arguments;
		}

		bool SameSymbolStem(const std::string_view Left, const std::string_view Right)
		{
			return SymbolStem(Left) == SymbolStem(Right);
		}

		std::string_view TrimView(std::string_view Value)
		{
			while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.front())))
			{
				Value.remove_prefix(1);
			}
			while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.back())))
			{
				Value.remove_suffix(1);
			}
			return Value;
		}

		bool ContainsWord(const std::string_view Value, const std::string_view Word)
		{
			std::size_t Position = Value.find(Word);
			while (Position != std::string_view::npos)
			{
				const bool bLeftBoundary = Position == 0 || (!std::isalnum(static_cast<unsigned char>(Value[Position - 1])) && Value[Position - 1] != '_');
				const std::size_t End = Position + Word.size();
				const bool bRightBoundary = End == Value.size() || (!std::isalnum(static_cast<unsigned char>(Value[End])) && Value[End] != '_');
				if (bLeftBoundary && bRightBoundary)
				{
					return true;
				}
				Position = Value.find(Word, Position + 1);
			}
			return false;
		}

		struct FMethodSignatureKey
		{
			std::vector<std::string> ParameterTypes;
			bool bConst = false;
			bool bVirtual = false;
			bool bOverride = false;
			bool bAbstract = false;
			bool bFinal = false;
			bool bValid = false;
		};

		FMethodSignatureKey MethodSignatureFromDetail(
		    const std::string_view Detail,
		    const std::string_view MethodName = {})
		{
			FMethodSignatureKey Result;
			std::size_t Open = std::string_view::npos;
			if (!MethodName.empty())
			{
				std::size_t Name = Detail.find(MethodName);
				while (Name != std::string_view::npos)
				{
					const bool bLeftBoundary = Name == 0 || (!std::isalnum(static_cast<unsigned char>(Detail[Name - 1])) && Detail[Name - 1] != '_');
					std::size_t After = Name + MethodName.size();
					const bool bRightBoundary = After == Detail.size() || (!std::isalnum(static_cast<unsigned char>(Detail[After])) && Detail[After] != '_');
					while (After < Detail.size() && std::isspace(static_cast<unsigned char>(Detail[After])))
					{
						++After;
					}
					if (bLeftBoundary && bRightBoundary && After < Detail.size() && Detail[After] == '(')
					{
						Open = After;
						break;
					}
					Name = Detail.find(MethodName, Name + 1);
				}
			}
			if (Open == std::string_view::npos)
			{
				Open = Detail.find('(');
			}
			if (Open == std::string_view::npos)
			{
				return Result;
			}

			std::size_t Close = std::string_view::npos;
			int ParenthesisDepth = 0;
			for (std::size_t Index = Open; Index < Detail.size(); ++Index)
			{
				if (Detail[Index] == '(')
				{
					++ParenthesisDepth;
				}
				else if (Detail[Index] == ')' && --ParenthesisDepth == 0)
				{
					Close = Index;
					break;
				}
			}
			if (Close == std::string_view::npos)
			{
				return Result;
			}

			const std::string_view Parameters = Detail.substr(Open + 1, Close - Open - 1);
			for (auto Parameter : Detail::SplitTypeDetailList(Parameters))
			{
				if (const auto Default = Parameter.find('='); Default != std::string_view::npos)
				{
					Parameter = TrimView(Parameter.substr(0, Default));
				}

				const auto NameSeparator = Parameter.find_last_of(" \t\r\n");
				if (NameSeparator == std::string_view::npos) return Result;
				const auto Type = TrimView(Parameter.substr(0, NameSeparator));
				if (Type.empty()) return Result;
				Result.ParameterTypes.emplace_back(Type);
			}

			std::string_view Head = Detail.substr(0, Open);
			// Host binding details place annotations before the declaration. Exclude
			// them from method modifiers so annotation arguments such as `final=true`
			// cannot change implementation-navigation semantics.
			if (const std::size_t LastAnnotation = Head.rfind(']');
			    LastAnnotation != std::string_view::npos)
			{
				Head = Head.substr(LastAnnotation + 1);
			}
			std::string_view Qualifiers = Detail.substr(Close + 1);
			if (const std::size_t Annotation = Qualifiers.find('[');
			    Annotation != std::string_view::npos)
			{
				Qualifiers = Qualifiers.substr(0, Annotation);
			}
			Result.bConst = ContainsWord(Qualifiers, "const");
			Result.bVirtual = ContainsWord(Head, "virtual");
			Result.bOverride = ContainsWord(Head, "override") || ContainsWord(Qualifiers, "override");
			Result.bAbstract = ContainsWord(Head, "abstract") || ContainsWord(Qualifiers, "abstract");
			Result.bFinal = ContainsWord(Head, "final") || ContainsWord(Qualifiers, "final");
			Result.bValid = true;
			return Result;
		}
	}

	void VeroScriptWorkspace::SetOptions(const FWorkspaceOptions& InOptions)
	{
		Options = InOptions;
		bDefaultWorkspaceSymbolIndexDirty = true;
	}

	void VeroScriptWorkspace::OpenDocument(const std::string_view Path, const std::string_view Source)
	{
		const std::string Key = NormalizePathKey(std::string(Path));
		const auto Existing = FindDocument(Documents, Key);
		if (Existing != Documents.end())
		{
			Existing->Source.assign(Source);
			bDefaultWorkspaceSymbolIndexDirty = true;
			return;
		}
		FDocumentOverlay Document;
		Document.Path = Key;
		Document.Source.assign(Source);
		Documents.push_back(std::move(Document));
		bDefaultWorkspaceSymbolIndexDirty = true;
	}

	void VeroScriptWorkspace::UpdateDocument(const std::string_view Path, const std::string_view Source)
	{
		OpenDocument(Path, Source);
	}

	void VeroScriptWorkspace::CloseDocument(const std::string_view Path)
	{
		const std::string Key = NormalizePathKey(std::string(Path));
		const auto Existing = FindDocument(Documents, Key);
		if (Existing != Documents.end())
		{
			Documents.erase(Existing);
			bDefaultWorkspaceSymbolIndexDirty = true;
		}
	}

	bool VeroScriptWorkspace::HasDocument(const std::string_view Path) const
	{
		const std::string Key = NormalizePathKey(std::string(Path));
		return FindDocument(Documents, Key) != Documents.end();
	}

	bool VeroScriptWorkspace::TryGetDocument(
	    const std::string_view Path,
	    FDocumentOverlay& OutDocument) const
	{
		const std::string Key = NormalizePathKey(std::string(Path));
		const auto Existing = FindDocument(Documents, Key);
		if (Existing == Documents.end())
		{
			return false;
		}
		OutDocument = *Existing;
		return true;
	}

	std::vector<std::string> VeroScriptWorkspace::ListDocuments() const
	{
		std::vector<std::string> Paths;
		Paths.reserve(Documents.size());
		for (const FDocumentOverlay& Document : Documents)
		{
			Paths.push_back(Document.Path);
		}
		return Paths;
	}

	std::vector<FDocumentOverlay> VeroScriptWorkspace::SnapshotDocuments() const
	{
		return Documents;
	}

	FAnalyzeResult VeroScriptWorkspace::AnalyzeDocument(const std::string_view Path) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			return MissingDocumentResult(NormalizePathKey(std::string(Path)));
		}

		return VeroScriptAnalyzer::AnalyzeSource(
		    Document.Source,
		    Document.Path,
		    MakeAnalyzeOptions());
	}

	FAnalyzeResult VeroScriptWorkspace::AnalyzeAll() const
	{
		return AnalyzeAll({});
	}

	FAnalyzeResult VeroScriptWorkspace::AnalyzeAll(
	    const std::function<void(std::string_view, const FAnalyzeResult&)>& OnDocument) const
	{
		FAnalyzeResult Aggregated;
		Aggregated.bSuccess = true;
		const FAnalyzeOptions AnalyzeOptions = MakeAnalyzeOptions();
		for (const FDocumentOverlay& Document : Documents)
		{
			const FAnalyzeResult One = VeroScriptAnalyzer::AnalyzeSource(
			    Document.Source,
			    Document.Path,
			    AnalyzeOptions);
			if (OnDocument)
			{
				OnDocument(Document.Path, One);
			}
			for (const auto& Doc : One.GeneratedDocuments)
			{
				if (std::none_of(Aggregated.GeneratedDocuments.begin(), Aggregated.GeneratedDocuments.end(),
				        [&](const auto& X)
				{
					return X.FileLabel == Doc.FileLabel;
				}))
				{
					Aggregated.GeneratedDocuments.push_back(Doc);
				}
			}

			if (!One.bSuccess)
			{
				Aggregated.bSuccess = false;
			}
			for (const FDiagnostic& Diagnostic : One.Diagnostics)
			{
				const bool bDuplicate = std::any_of(
				    Aggregated.Diagnostics.begin(),
				    Aggregated.Diagnostics.end(),
				    [&Diagnostic](const FDiagnostic& Existing)
				{
					return Existing.FileLabel == Diagnostic.FileLabel && Existing.Range.StartLine == Diagnostic.Range.StartLine && Existing.Range.StartColumn == Diagnostic.Range.StartColumn && Existing.Message == Diagnostic.Message;
				});
				if (!bDuplicate)
				{
					Aggregated.Diagnostics.push_back(Diagnostic);
				}
			}
		}
		return Aggregated;
	}

	FOutlineResult VeroScriptWorkspace::OutlineDocument(const std::string_view Path) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FOutlineResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptOutline::CollectFromSource(
		    Document.Source,
		    Document.Path,
		    MakeOutlineOptions());
	}

	FSemanticTokensResult VeroScriptWorkspace::SemanticTokensDocument(
	    const std::string_view Path) const
	{
		return SemanticTokensDocument(Path, {});
	}

	FSemanticTokensResult VeroScriptWorkspace::SemanticTokensDocument(
	    const std::string_view Path, const FSemanticTokensOptions& ControlOptions) const
	{
		try
		{
			FAnalysisControlScope Control(ControlOptions.ShouldCancel, ControlOptions.RecordStage);
			CheckAnalysisCancellation();
			FDocumentOverlay Document;
			if (!TryGetDocument(Path, Document))
			{
				FSemanticTokensResult Result;
				Result.Diagnostics =
				    MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
				return Result;
			}

			Detail::FSymbolQueryOptions QueryOptions;
			FillImportPaths(QueryOptions.ImportPaths);
			FillStdlibSearchPaths(QueryOptions.StdlibSearchPaths);
			FillImplicitImportPaths(QueryOptions.ImplicitImportPaths);
			QueryOptions.bAllowCircularImports = Options.bAllowCircularImports;
			QueryOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			FillDefines(QueryOptions.Defines);
			QueryOptions.PredefinedMacros = Options.PredefinedMacros;
			QueryOptions.SourceOverlays.reserve(Documents.size());
			for (const FDocumentOverlay& Overlay : Documents)
			{
				QueryOptions.SourceOverlays.push_back(
				    VeroScript::FSourceOverlay{Overlay.Path, Overlay.Source});
			}
			QueryOptions.Bindings = Options.Bindings;
			QueryOptions.AnnotationProcessing = Options.AnnotationProcessing;
			QueryOptions.CompileProcessing = Options.CompileProcessing;
			QueryOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			QueryOptions.ModuleEnvironment = Options.ModuleEnvironment;
			const Detail::FSemanticSymbolResult Internal =
			    Detail::VeroScriptSymbolQuery::SemanticTokensAtSource(
			        Document.Source,
			        Document.Path,
			        QueryOptions);

			FSemanticTokensResult Result;
			Result.bSuccess = Internal.bSuccess;
			Result.Dependencies = Internal.Dependencies;
			Result.Snapshot = Internal.Snapshot;
			Result.Tokens.reserve(Internal.Tokens.size());
			for (const Detail::FSemanticSymbolToken& Token : Internal.Tokens)
			{
				FSemanticToken Mapped;
				Mapped.Kind = MapDocumentSymbolKind(Token.Kind);
				Mapped.Modifiers = Token.Modifiers;
				Mapped.Range.StartLine = Token.Line;
				Mapped.Range.StartColumn = Token.Column;
				Mapped.Range.EndLine = Token.Line;
				Mapped.Range.EndColumn = Token.EndColumn;
				Result.Tokens.push_back(std::move(Mapped));
			}
			for (const FScriptError& Diagnostic : Internal.Diagnostics)
			{
				Result.Diagnostics.push_back(ToToolingDiagnostic(Diagnostic, Document.Path));
			}
			return Result;
		}
		catch (const FAnalysisCancelled&)
		{
			FSemanticTokensResult Result;
			Result.bCancelled = true;
			return Result;
		}
	}

	FNavigationResult VeroScriptWorkspace::NavigateDocument(
	    const std::string_view Path,
	    const FTextPosition Position) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FNavigationResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptNavigation::QueryAtSource(
		    Document.Source,
		    Position,
		    Document.Path,
		    MakeNavigationOptions());
	}

	FNavigationResult VeroScriptWorkspace::ImplementationDocument(
	    const std::string_view Path,
	    const FTextPosition Position) const
	{
		return ImplementationDocument(Path, Position, {});
	}

	FNavigationResult VeroScriptWorkspace::ImplementationDocument(
	    const std::string_view Path,
	    const FTextPosition Position,
	    const FWorkspaceSymbolsOptions& SymbolOptions) const
	try
	{
		const auto* const Previous = ActiveAnalysisControl;
		FAnalysisControlScope Control([&SymbolOptions, Previous]()
		{
			return (SymbolOptions.ShouldCancel && SymbolOptions.ShouldCancel()) ||
			    (Previous && Previous->ShouldCancel && Previous->ShouldCancel());
		}, Previous ? Previous->RecordStage : std::function<void(std::string_view, double)>{});
		CheckAnalysisCancellation();
		FNavigationResult Result = NavigateDocument(Path, Position);
		if (!Result.bSuccess || Result.Definitions.empty())
		{
			return Result;
		}

		// Keep declaration identity for override lookup, and use the separately
		// bound body position for explicit/generated source implementations.
		const auto Targets = std::move(Result.Definitions);
		Result.Definitions = {};
		const FNavigationHit& Target = Targets.front();
		const bool bTargetIsType = IsTypeSymbolKind(Target.Kind);
		const FMethodSignatureKey TargetSignature =
		    MethodSignatureFromDetail(Target.Detail, Target.Name);
		const bool bTargetMethodCanHaveImplementations = Target.Kind == EOutlineSymbolKind::Method && TargetSignature.bValid && !TargetSignature.bFinal && (TargetSignature.bVirtual || TargetSignature.bOverride || TargetSignature.bAbstract);
		const auto AppendImplementation =
		    [&Result](FNavigationHit Hit, const FSourceImplementationLocation& Location)
		{
			if (!Location.FileLabel.empty())
			{
				Hit.FileLabel = Location.FileLabel;
				Hit.DefinitionRange = {Location.Line, Location.Column, Location.Line, Location.Column};
				Hit.GeneratedOrigin = Location.GeneratedOrigin;
			}
			const auto Duplicate = std::find_if(Result.Definitions.begin(), Result.Definitions.end(),
			    [&Hit](const auto& Existing)
			{
				return Existing.FileLabel == Hit.FileLabel &&
				    Existing.DefinitionRange.StartLine == Hit.DefinitionRange.StartLine &&
				    Existing.DefinitionRange.StartColumn == Hit.DefinitionRange.StartColumn;
			});
			if (Duplicate == Result.Definitions.end())
			{
				Result.Definitions.push_back(std::move(Hit));
			}
		};
		const auto AppendSourceImplementations = [&]()
		{
			for (const auto& Declaration : Targets)
			{
				CheckAnalysisCancellation();
				if (!Declaration.SourceImplementation.FileLabel.empty())
				{
					AppendImplementation(Declaration, Declaration.SourceImplementation);
				}
			}
		};
		if (!bTargetIsType && !bTargetMethodCanHaveImplementations)
		{
			// Free/non-virtual callables already carry their source body. Avoid
			// indexing the entire workspace for this direct lookup.
			CheckAnalysisCancellation();
			AppendSourceImplementations();
			return Result;
		}
		auto ImplementationOptions = SymbolOptions;
		ImplementationOptions.bIncludeImportedExports = true;
		const FWorkspaceSymbolsResult Symbols = WorkspaceSymbols(ImplementationOptions);
		if (Symbols.bCancelled)
		{
			Result.bCancelled = true;
			return Result;
		}
		Result.Diagnostics.insert(
		    Result.Diagnostics.end(), Symbols.Diagnostics.begin(), Symbols.Diagnostics.end());
		AppendSourceImplementations();

		std::unordered_map<std::string, std::vector<std::string>> ParentsByType;
		std::unordered_map<std::string, std::string> DeclaredTypeByStem;
		std::unordered_set<std::string> AmbiguousTypeStems;
		for (const FWorkspaceSymbol& Symbol : Symbols.Symbols)
		{
			CheckAnalysisCancellation();
			if (IsTypeSymbolKind(Symbol.Kind))
			{
				const std::string Stem = SymbolStem(Symbol.Name);
				const auto [It, bInserted] = ParentsByType.emplace(
				    Stem, Detail::BaseTypesFromDetail(Symbol.Detail));
				if (!bInserted)
				{
					// The compact workspace outline does not yet retain a fully-qualified
					// owner for every type.  Returning a possibly unrelated implementation
					// is worse than returning no implementation, so refuse ambiguous type
					// stems until the index can distinguish their namespace identities.
					AmbiguousTypeStems.insert(Stem);
				}
				else
				{
					DeclaredTypeByStem.emplace(Stem, Symbol.Name);
				}
			}
		}

		const auto ResolveAncestorSpec = [&ParentsByType,
		                                     &DeclaredTypeByStem,
		                                     &AmbiguousTypeStems](const std::string_view Candidate, const std::string_view Base)
		    -> std::optional<std::string>
		{
			const std::string BaseStem = SymbolStem(Base);
			if (AmbiguousTypeStems.find(SymbolStem(Candidate)) != AmbiguousTypeStems.end() || AmbiguousTypeStems.find(BaseStem) != AmbiguousTypeStems.end())
			{
				return std::nullopt;
			}
			struct FPendingType
			{
				std::string Spec;
				bool bLeaving = false;
			};
			std::vector<FPendingType> Pending{{std::string(Candidate), false}};
			std::unordered_set<std::string> Visited, ActiveStems;
			while (!Pending.empty())
			{
				CheckAnalysisCancellation();
				const auto Current = std::move(Pending.back());
				Pending.pop_back();
				const auto CurrentStem = SymbolStem(Current.Spec);
				if (Current.bLeaving)
				{
					ActiveStems.erase(CurrentStem);
					continue;
				}
				// Declaration cycles remain cycles when their generic arguments grow
				// on every edge. Track the active declaration path as well as complete
				// specializations, and use an explicit stack for deeply nested hierarchies.
				if (AmbiguousTypeStems.count(CurrentStem) || ActiveStems.count(CurrentStem) ||
				    !Visited.insert(Current.Spec).second)
				{
					continue;
				}

				if (SameSymbolStem(CurrentStem, BaseStem)) return Current.Spec;
				const auto Parents = ParentsByType.find(CurrentStem);
				const auto Declaration = DeclaredTypeByStem.find(CurrentStem);
				if (Parents == ParentsByType.end() || Declaration == DeclaredTypeByStem.end())
				{
					continue;
				}

				const auto Parameters = Detail::GenericArgumentsFromDetail(Declaration->second);
				const auto Arguments = SpecializedGenericArguments(Current.Spec, Declaration->second);
				ActiveStems.insert(CurrentStem);
				Pending.push_back({Current.Spec, true});
				// Reverse insertion preserves declaration-order depth-first lookup.
				for (auto Parent = Parents->second.rbegin(); Parent != Parents->second.rend(); ++Parent)
				{
					Pending.push_back({SubstituteTypeParameters(*Parent, Parameters, Arguments), false});
				}
			}
			return std::nullopt;
		};

		const auto DerivesFrom = [&ResolveAncestorSpec](const std::string_view Candidate,
		                             const std::string_view Base)
		{
			const std::optional<std::string> Ancestor = ResolveAncestorSpec(Candidate, Base);
			return Ancestor.has_value() && !SameSymbolStem(Candidate, Base);
		};

		for (const FWorkspaceSymbol& Symbol : Symbols.Symbols)
		{
			CheckAnalysisCancellation();
			bool bMatch = false;
			if (bTargetIsType)
			{
				bMatch = IsTypeSymbolKind(Symbol.Kind) && !SameSymbolStem(Symbol.Name, Target.Name) && DerivesFrom(Symbol.Name, Target.Name);
			}
			else if (bTargetMethodCanHaveImplementations)
			{
				// Most workspace symbols cannot implement this member. Avoid parsing
				// their signatures or traversing their owners' inheritance chains.
				if (Symbol.Kind != Target.Kind || !SameSymbolStem(Symbol.Name, Target.Name))
				{
					continue;
				}

				const FMethodSignatureKey CandidateSignature =
				    MethodSignatureFromDetail(Symbol.Detail, Symbol.Name);
				std::vector<std::string> SpecializedTargetParameters = TargetSignature.ParameterTypes;
				std::optional<std::string> Ancestor;
				if (!Symbol.ContainerName.empty() && !Target.ContainerName.empty())
				{
					Ancestor = ResolveAncestorSpec(Symbol.ContainerName, Target.ContainerName);
				}
				if (Ancestor.has_value())
				{
					const std::string TargetStem = SymbolStem(Target.ContainerName);
					const auto TargetDeclaration = DeclaredTypeByStem.find(TargetStem);
					if (TargetDeclaration != DeclaredTypeByStem.end())
					{
						const std::vector<std::string> Parameters =
						    Detail::GenericArgumentsFromDetail(TargetDeclaration->second);
						const std::vector<std::string> Arguments = SpecializedGenericArguments(*Ancestor, TargetDeclaration->second);
						for (std::string& ParameterType : SpecializedTargetParameters)
						{
							ParameterType = SubstituteTypeParameters(
							    ParameterType, Parameters, Arguments);
						}
					}
				}
				bMatch = Symbol.Kind == Target.Kind && SameSymbolStem(Symbol.Name, Target.Name) && TargetSignature.bValid && CandidateSignature.bValid && CandidateSignature.ParameterTypes == SpecializedTargetParameters && CandidateSignature.bConst == TargetSignature.bConst && Ancestor.has_value() && !SameSymbolStem(Symbol.ContainerName, Target.ContainerName);
			}
			if (!bMatch || Symbol.FileLabel.empty() || Symbol.Range.StartLine == 0)
			{
				continue;
			}
			FNavigationHit Hit;
			Hit.Kind = Symbol.Kind;
			Hit.Name = Symbol.Name;
			Hit.Detail = Symbol.Detail;
			Hit.ContainerName = Symbol.ContainerName;
			Hit.FileLabel = Symbol.FileLabel;
			Hit.DefinitionRange = Symbol.Range;
			Hit.bExported = Symbol.bExported;
			Hit.SourceImplementation = Symbol.SourceImplementation;
			Hit.GeneratedOrigin = Symbol.GeneratedOrigin;
			AppendImplementation(std::move(Hit), Symbol.SourceImplementation);
		}
		return Result;
	}
	catch (const FAnalysisCancelled&)
	{
		FNavigationResult Cancelled;
		Cancelled.bCancelled = true;
		return Cancelled;
	}

	FHoverResult VeroScriptWorkspace::HoverDocument(
	    const std::string_view Path,
	    const FTextPosition Position) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FHoverResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptHover::HoverAtSource(
		    Document.Source,
		    Position,
		    Document.Path,
		    MakeHoverOptions());
	}

	FCompletionResult VeroScriptWorkspace::CompleteAtDocument(
	    const std::string_view Path,
	    const FTextPosition Position) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FCompletionResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptComplete::CompleteAtSource(
		    Document.Source,
		    Position,
		    Document.Path,
		    MakeCompletionOptions());
	}

	FSignatureHelpResult VeroScriptWorkspace::SignatureHelpDocument(
	    const std::string_view Path,
	    const FTextPosition Position) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FSignatureHelpResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptCallHelp::HelpAtSource(
		    Document.Source,
		    Position,
		    Document.Path,
		    MakeSignatureHelpOptions());
	}

	FReferencesResult VeroScriptWorkspace::FindReferencesDocument(
	    const std::string_view Path,
	    const FTextPosition Position) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FReferencesResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptFindReferences::FindAtSource(
		    Document.Source,
		    Position,
		    Document.Path,
		    MakeReferencesOptions());
	}

	FDocumentHighlightResult VeroScriptWorkspace::HighlightDocument(
	    const std::string_view Path,
	    const FTextPosition Position) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FDocumentHighlightResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptDocumentHighlight::HighlightsAtSource(
		    Document.Source,
		    Position,
		    Document.Path,
		    MakeDocumentHighlightOptions());
	}

	FRenameResult VeroScriptWorkspace::CanRenameDocument(
	    const std::string_view Path,
	    const FTextPosition Position) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FRenameResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptRename::CanRenameAtSource(
		    Document.Source,
		    Position,
		    Document.Path,
		    MakeRenameOptions());
	}

	FRenameResult VeroScriptWorkspace::RenameDocument(
	    const std::string_view Path,
	    const FTextPosition Position,
	    const std::string_view NewName) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FRenameResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		return VeroScriptRename::PrepareAtSource(
		    Document.Source,
		    Position,
		    NewName,
		    Document.Path,
		    MakeRenameOptions());
	}

	FFormatResult VeroScriptWorkspace::FormatDocument(const std::string_view Path) const
	{
		return FormatDocument(Path, 0);
	}

	FFormatResult VeroScriptWorkspace::FormatDocument(
	    const std::string_view Path,
	    const std::uint32_t IndentWidth) const
	{
		return FormatDocument(Path, IndentWidth, true);
	}

	FFormatResult VeroScriptWorkspace::FormatDocument(
	    const std::string_view Path,
	    const std::uint32_t IndentWidth,
	    const bool bInsertSpaces) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FFormatResult Result;
			Result.Diagnostics = MissingDocumentResult(NormalizePathKey(std::string(Path))).Diagnostics;
			return Result;
		}
		FFormatOptions FormatOptions = MakeFormatOptions();
		FormatOptions.bInsertSpaces = bInsertSpaces;
		if (IndentWidth != 0)
		{
			FormatOptions.IndentWidth = IndentWidth;
		}
		return VeroScriptFormat::FormatSource(Document.Source, FormatOptions);
	}

	FRangeFormatResult VeroScriptWorkspace::FormatRangeDocument(
	    const std::string_view Path,
	    const FTextRange Range) const
	{
		return FormatRangeDocument(Path, Range, 0);
	}

	FRangeFormatResult VeroScriptWorkspace::FormatRangeDocument(
	    const std::string_view Path,
	    const FTextRange Range,
	    const std::uint32_t IndentWidth) const
	{
		return FormatRangeDocument(Path, Range, IndentWidth, true);
	}

	FRangeFormatResult VeroScriptWorkspace::FormatRangeDocument(
	    const std::string_view Path,
	    const FTextRange Range,
	    const std::uint32_t IndentWidth,
	    const bool bInsertSpaces) const
	{
		FDocumentOverlay Document;
		if (!TryGetDocument(Path, Document))
		{
			FRangeFormatResult Result;
			Result.Diagnostics = MissingDocumentResult(
			    NormalizePathKey(std::string(Path)))
			                         .Diagnostics;
			return Result;
		}

		FFormatOptions FormatOptions = MakeFormatOptions();
		FormatOptions.bInsertSpaces = bInsertSpaces;
		if (IndentWidth != 0)
		{
			FormatOptions.IndentWidth = IndentWidth;
		}
		return VeroScriptFormat::FormatRangeSource(Document.Source, Range, FormatOptions);
	}

	FWorkspaceSymbolsResult VeroScriptWorkspace::WorkspaceSymbols(
	    const FWorkspaceSymbolsOptions& InOptions) const
	{
		// A cache hit must still honour a request that was cancelled before it
		// reached the query.  The callback is otherwise intentionally not part of
		// the cache key: it is control flow, not a semantic indexing option.
		if (InOptions.ShouldCancel && InOptions.ShouldCancel())
		{
			FWorkspaceSymbolsResult Cancelled;
			Cancelled.bCancelled = true;
			return Cancelled;
		}
		const FWorkspaceSymbolsOptions DefaultSymbolOptions;
		const bool bDefaultRequest = InOptions.ImportPaths.empty() && InOptions.StdlibSearchPaths.empty() &&
		    InOptions.WorkspaceRoots.empty() && InOptions.Defines.empty() &&
		    InOptions.PredefinedMacros.empty() && InOptions.SourceOverlays.empty() &&
		    InOptions.ImplicitImportPaths.empty() && !InOptions.Bindings && !InOptions.ModuleEnvironment &&
		    !Options.ModuleEnvironment
		    // Processor dependencies can change without a document/configuration event.
		    // Rebuild generated symbols, just as interactive semantic queries do.
		    && !InOptions.AnnotationProcessing.Registry && !Options.AnnotationProcessing.Registry &&
		    IsDefaultCompileProcessing(InOptions.CompileProcessing) && !Options.CompileProcessing.Registry &&
		    IsDefaultScriptCompileExtensionProcessing(InOptions.ScriptCompileExtensions) &&
		    IsDefaultScriptCompileExtensionProcessing(Options.ScriptCompileExtensions) &&
		    std::none_of(Documents.begin(), Documents.end(),
		        [](const auto& D)
		{
			return HasScriptCompileExtensionImport(D.Source);
		}) &&
		    InOptions.bIncludeImports && !InOptions.bIncludeImportedExports &&
		    InOptions.bIncludeSoftBuiltins && InOptions.bIncludeHostBindings &&
		    InOptions.bAllowCircularImports && !InOptions.bAllowProjectWideAccess &&
		    InOptions.MaxWorkspaceFiles == DefaultSymbolOptions.MaxWorkspaceFiles &&
		    InOptions.MaxWorkspaceFileBytes == DefaultSymbolOptions.MaxWorkspaceFileBytes;
		if (bDefaultRequest && !bDefaultWorkspaceSymbolIndexDirty)
		{
			return DefaultWorkspaceSymbolIndex;
		}
		FWorkspaceSymbolsOptions Merged = InOptions;
		if (!Merged.ModuleEnvironment)
		{
			Merged.ModuleEnvironment = Options.ModuleEnvironment;
		}

		if (Merged.WorkspaceRoots.empty())
		{
			for (const std::string& Root : Options.WorkspaceRoots)
			{
				Merged.WorkspaceRoots.push_back(Root);
			}
		}
		if (Merged.ImportPaths.empty())
		{
			FillImportPaths(Merged.ImportPaths);
		}
		if (Merged.StdlibSearchPaths.empty())
		{
			FillStdlibSearchPaths(Merged.StdlibSearchPaths);
		}
		if (Merged.ImplicitImportPaths.empty())
		{
			FillImplicitImportPaths(Merged.ImplicitImportPaths);
		}
		Merged.bAllowCircularImports = Options.bAllowCircularImports;
		Merged.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		// A default-constructed query inherits the workspace policy. An explicit
		// smaller request is allowed, but an individual caller must never bypass
		// the configured server safeguards.
		Merged.MaxWorkspaceFiles = InOptions.MaxWorkspaceFiles == DefaultSymbolOptions.MaxWorkspaceFiles
		    ? Options.MaxWorkspaceFiles
		    : (std::min)(InOptions.MaxWorkspaceFiles, Options.MaxWorkspaceFiles);
		Merged.MaxWorkspaceFileBytes = InOptions.MaxWorkspaceFileBytes == DefaultSymbolOptions.MaxWorkspaceFileBytes
		    ? Options.MaxWorkspaceFileBytes
		    : (std::min)(InOptions.MaxWorkspaceFileBytes, Options.MaxWorkspaceFileBytes);
		if (Merged.Defines.empty())
		{
			FillDefines(Merged.Defines);
		}
		if (Merged.PredefinedMacros.empty())
		{
			Merged.PredefinedMacros = Options.PredefinedMacros;
		}
		if (!Merged.Bindings)
		{
			Merged.Bindings = Options.Bindings;
		}
		if (!Merged.AnnotationProcessing.Registry)
		{
			Merged.AnnotationProcessing = Options.AnnotationProcessing;
		}
		if (IsDefaultCompileProcessing(Merged.CompileProcessing))
		{
			Merged.CompileProcessing = Options.CompileProcessing;
		}

		if (IsDefaultScriptCompileExtensions(Merged.ScriptCompileExtensions))
		{
			Merged.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		}

		FWorkspaceSymbolsResult Result = VeroScriptWorkspaceSymbols::CollectFromWorkspace(*this, Merged);
		if (bDefaultRequest && Result.bSuccess && !Result.bCancelled && Result.bCacheable)
		{
			DefaultWorkspaceSymbolIndex = Result;
			bDefaultWorkspaceSymbolIndexDirty = false;
		}
		return Result;
	}

	std::vector<std::string> VeroScriptWorkspace::ResolveLoadedModulePaths(
	    const std::string_view MainFilePath,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths)
	{
		return VeroScript::VeroScriptModuleLoader::ResolveLoadedModulePaths(
		    MainFilePath,
		    ImportPaths,
		    Defines,
		    PredefinedMacros,
		    ImplicitImportPaths);
	}

	void VeroScriptWorkspace::FillImportPaths(std::vector<std::string_view>& OutImportPaths) const
	{
		OutImportPaths.clear();
		OutImportPaths.reserve(Options.ImportPaths.size());
		for (const std::string& ImportPath : Options.ImportPaths)
		{
			OutImportPaths.emplace_back(ImportPath);
		}
	}

	void VeroScriptWorkspace::FillStdlibSearchPaths(
	    std::vector<std::string_view>& OutPaths) const
	{
		OutPaths.clear();
		OutPaths.reserve(Options.StdlibSearchPaths.size());
		for (const std::string& Path : Options.StdlibSearchPaths)
		{
			OutPaths.emplace_back(Path);
		}
	}

	void VeroScriptWorkspace::FillImplicitImportPaths(
	    std::vector<std::string_view>& OutImplicitImportPaths) const
	{
		OutImplicitImportPaths.clear();
		OutImplicitImportPaths.reserve(Options.ImplicitImportPaths.size());
		for (const std::string& ImportPath : Options.ImplicitImportPaths)
		{
			OutImplicitImportPaths.emplace_back(ImportPath);
		}
	}

	void VeroScriptWorkspace::FillDefines(std::vector<std::string_view>& OutDefines) const
	{
		OutDefines.clear();
		OutDefines.reserve(Options.Defines.size());
		for (const std::string& Define : Options.Defines)
		{
			OutDefines.emplace_back(Define);
		}
	}

	FAnalyzeOptions VeroScriptWorkspace::MakeAnalyzeOptions() const
	{
		FAnalyzeOptions AnalyzeOptions;
		FillImportPaths(AnalyzeOptions.ImportPaths);
		FillStdlibSearchPaths(AnalyzeOptions.StdlibSearchPaths);
		FillImplicitImportPaths(AnalyzeOptions.ImplicitImportPaths);
		AnalyzeOptions.bAllowCircularImports = Options.bAllowCircularImports;
		AnalyzeOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(AnalyzeOptions.Defines);
		AnalyzeOptions.SourceOverlays = Documents;
		AnalyzeOptions.PredefinedMacros = Options.PredefinedMacros;
		AnalyzeOptions.Bindings = Options.Bindings;
		AnalyzeOptions.AnnotationProcessing = Options.AnnotationProcessing;
		AnalyzeOptions.CompileProcessing = Options.CompileProcessing;
		AnalyzeOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		AnalyzeOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return AnalyzeOptions;
	}

	FOutlineOptions VeroScriptWorkspace::MakeOutlineOptions() const
	{
		FOutlineOptions OutlineOptions;
		FillImportPaths(OutlineOptions.ImportPaths);
		FillStdlibSearchPaths(OutlineOptions.StdlibSearchPaths);
		FillImplicitImportPaths(OutlineOptions.ImplicitImportPaths);
		OutlineOptions.bAllowCircularImports = Options.bAllowCircularImports;
		OutlineOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(OutlineOptions.Defines);
		OutlineOptions.SourceOverlays = Documents;
		OutlineOptions.PredefinedMacros = Options.PredefinedMacros;
		OutlineOptions.Bindings = Options.Bindings;
		OutlineOptions.AnnotationProcessing = Options.AnnotationProcessing;
		OutlineOptions.CompileProcessing = Options.CompileProcessing;
		OutlineOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		OutlineOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return OutlineOptions;
	}

	FNavigationOptions VeroScriptWorkspace::MakeNavigationOptions() const
	{
		FNavigationOptions NavigationOptions;
		FillImportPaths(NavigationOptions.ImportPaths);
		FillStdlibSearchPaths(NavigationOptions.StdlibSearchPaths);
		FillImplicitImportPaths(NavigationOptions.ImplicitImportPaths);
		NavigationOptions.bAllowCircularImports = Options.bAllowCircularImports;
		NavigationOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(NavigationOptions.Defines);
		NavigationOptions.SourceOverlays = Documents;
		NavigationOptions.PredefinedMacros = Options.PredefinedMacros;
		NavigationOptions.Bindings = Options.Bindings;
		NavigationOptions.AnnotationProcessing = Options.AnnotationProcessing;
		NavigationOptions.CompileProcessing = Options.CompileProcessing;
		NavigationOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		NavigationOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return NavigationOptions;
	}

	FHoverOptions VeroScriptWorkspace::MakeHoverOptions() const
	{
		FHoverOptions HoverOptions;
		FillImportPaths(HoverOptions.ImportPaths);
		FillStdlibSearchPaths(HoverOptions.StdlibSearchPaths);
		FillImplicitImportPaths(HoverOptions.ImplicitImportPaths);
		HoverOptions.bAllowCircularImports = Options.bAllowCircularImports;
		HoverOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(HoverOptions.Defines);
		HoverOptions.SourceOverlays = Documents;
		HoverOptions.PredefinedMacros = Options.PredefinedMacros;
		HoverOptions.Bindings = Options.Bindings;
		HoverOptions.AnnotationProcessing = Options.AnnotationProcessing;
		HoverOptions.CompileProcessing = Options.CompileProcessing;
		HoverOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		HoverOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return HoverOptions;
	}

	FCompletionOptions VeroScriptWorkspace::MakeCompletionOptions() const
	{
		FCompletionOptions CompletionOptions;
		FillImportPaths(CompletionOptions.ImportPaths);
		FillStdlibSearchPaths(CompletionOptions.StdlibSearchPaths);
		FillImplicitImportPaths(CompletionOptions.ImplicitImportPaths);
		CompletionOptions.bAllowCircularImports = Options.bAllowCircularImports;
		CompletionOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(CompletionOptions.Defines);
		CompletionOptions.SourceOverlays = Documents;
		CompletionOptions.PredefinedMacros = Options.PredefinedMacros;
		CompletionOptions.Bindings = Options.Bindings;
		CompletionOptions.AnnotationProcessing = Options.AnnotationProcessing;
		CompletionOptions.CompileProcessing = Options.CompileProcessing;
		CompletionOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		CompletionOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return CompletionOptions;
	}

	FSignatureHelpOptions VeroScriptWorkspace::MakeSignatureHelpOptions() const
	{
		FSignatureHelpOptions SignatureOptions;
		FillImportPaths(SignatureOptions.ImportPaths);
		FillStdlibSearchPaths(SignatureOptions.StdlibSearchPaths);
		FillImplicitImportPaths(SignatureOptions.ImplicitImportPaths);
		SignatureOptions.bAllowCircularImports = Options.bAllowCircularImports;
		SignatureOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(SignatureOptions.Defines);
		SignatureOptions.SourceOverlays = Documents;
		SignatureOptions.PredefinedMacros = Options.PredefinedMacros;
		SignatureOptions.Bindings = Options.Bindings;
		SignatureOptions.AnnotationProcessing = Options.AnnotationProcessing;
		SignatureOptions.CompileProcessing = Options.CompileProcessing;
		SignatureOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		SignatureOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return SignatureOptions;
	}

	FReferencesOptions VeroScriptWorkspace::MakeReferencesOptions() const
	{
		FReferencesOptions ReferencesOptions;
		FillImportPaths(ReferencesOptions.ImportPaths);
		FillStdlibSearchPaths(ReferencesOptions.StdlibSearchPaths);
		FillImplicitImportPaths(ReferencesOptions.ImplicitImportPaths);
		ReferencesOptions.bAllowCircularImports = Options.bAllowCircularImports;
		ReferencesOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(ReferencesOptions.Defines);
		ReferencesOptions.SourceOverlays = Documents;
		ReferencesOptions.PredefinedMacros = Options.PredefinedMacros;
		ReferencesOptions.Bindings = Options.Bindings;
		ReferencesOptions.AnnotationProcessing = Options.AnnotationProcessing;
		ReferencesOptions.CompileProcessing = Options.CompileProcessing;
		ReferencesOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		ReferencesOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return ReferencesOptions;
	}

	FDocumentHighlightOptions VeroScriptWorkspace::MakeDocumentHighlightOptions() const
	{
		FDocumentHighlightOptions HighlightOptions;
		FillImportPaths(HighlightOptions.ImportPaths);
		FillStdlibSearchPaths(HighlightOptions.StdlibSearchPaths);
		FillImplicitImportPaths(HighlightOptions.ImplicitImportPaths);
		HighlightOptions.bAllowCircularImports = Options.bAllowCircularImports;
		HighlightOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(HighlightOptions.Defines);
		HighlightOptions.SourceOverlays = Documents;
		HighlightOptions.PredefinedMacros = Options.PredefinedMacros;
		HighlightOptions.Bindings = Options.Bindings;
		HighlightOptions.AnnotationProcessing = Options.AnnotationProcessing;
		HighlightOptions.CompileProcessing = Options.CompileProcessing;
		HighlightOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		HighlightOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return HighlightOptions;
	}

	FWorkspaceSymbolsOptions VeroScriptWorkspace::MakeWorkspaceSymbolsOptions() const
	{
		FWorkspaceSymbolsOptions SymbolsOptions;
		for (const std::string& Root : Options.WorkspaceRoots)
		{
			SymbolsOptions.WorkspaceRoots.push_back(Root);
		}
		FillImportPaths(SymbolsOptions.ImportPaths);
		FillStdlibSearchPaths(SymbolsOptions.StdlibSearchPaths);
		FillImplicitImportPaths(SymbolsOptions.ImplicitImportPaths);
		SymbolsOptions.bAllowCircularImports = Options.bAllowCircularImports;
		SymbolsOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		SymbolsOptions.MaxWorkspaceFiles = Options.MaxWorkspaceFiles;
		SymbolsOptions.MaxWorkspaceFileBytes = Options.MaxWorkspaceFileBytes;
		FillDefines(SymbolsOptions.Defines);
		SymbolsOptions.PredefinedMacros = Options.PredefinedMacros;
		SymbolsOptions.bIncludeSoftBuiltins = true;
		SymbolsOptions.bIncludeHostBindings = Options.Bindings != nullptr;
		SymbolsOptions.Bindings = Options.Bindings;
		SymbolsOptions.AnnotationProcessing = Options.AnnotationProcessing;
		SymbolsOptions.CompileProcessing = Options.CompileProcessing;
		SymbolsOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		SymbolsOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return SymbolsOptions;
	}

	FRenameOptions VeroScriptWorkspace::MakeRenameOptions() const
	{
		FRenameOptions RenameOptions;
		FillImportPaths(RenameOptions.ImportPaths);
		FillStdlibSearchPaths(RenameOptions.StdlibSearchPaths);
		FillImplicitImportPaths(RenameOptions.ImplicitImportPaths);
		RenameOptions.bAllowCircularImports = Options.bAllowCircularImports;
		RenameOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		FillDefines(RenameOptions.Defines);
		RenameOptions.SourceOverlays = Documents;
		RenameOptions.PredefinedMacros = Options.PredefinedMacros;
		RenameOptions.Bindings = Options.Bindings;
		RenameOptions.AnnotationProcessing = Options.AnnotationProcessing;
		RenameOptions.CompileProcessing = Options.CompileProcessing;
		RenameOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		RenameOptions.ModuleEnvironment = Options.ModuleEnvironment;
		return RenameOptions;
	}

	FFormatOptions VeroScriptWorkspace::MakeFormatOptions() const
	{
		FFormatOptions FormatOptions;
		FillDefines(FormatOptions.Defines);
		FormatOptions.PredefinedMacros = Options.PredefinedMacros;
		return FormatOptions;
	}
}
