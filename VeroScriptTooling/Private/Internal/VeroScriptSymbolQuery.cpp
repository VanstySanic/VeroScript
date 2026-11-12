// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPreparedQuery.h"
#include "VeroScriptBindingVisibility.h"

#include "VeroScriptSymbolQuery.h"
#include "VeroScriptToolingTypes.h"
#include "VeroScriptQueryLexicalSnapshot.h"
#include "VeroScriptSymbolLookupIndex.h"
#include "VeroScriptSourceRangeIndex.h"
#include "VeroScriptAnalysisControl.h"

#include "VeroScriptAst.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptTemplateQuery.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptNamespaceLookup.h"
#include "VeroScriptMemory.h"
#include "VeroScriptCompletion.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptLexer.h"
#include "VeroScriptImportPath.h"
#include "VeroScriptLocale.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptNative.h"
#include "VeroScriptOperatorLex.h"
#include "Parser/VeroScriptParserDetail.h"
#include "Parser/VeroScriptParserBindings.h"
#include "VeroScriptPath.h"
#include "VeroScriptPreprocessorQuery.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptSourceDocumentation.h"
#include "VeroScriptSourceMetadataQuery.h"
#include "VeroScriptSoftBuiltins.h"
#include "VeroScriptStandardNamespaceMetadata.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptParser.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptTypeHierarchy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	struct FSemanticQuerySnapshot
	{
		std::string Source;
		std::string FileLabel;
		// Program locations/string views refer to storage owned by the loader.
		std::unique_ptr<VeroScriptModuleLoader> Loader;
		std::unique_ptr<FProgram> Program;
		FDocumentSymbolResult Symbols;
		FPreprocessLineMap LineMap;
		std::vector<VeroScriptModuleLoader::FLoadedFileStamp> DiskStamps;
		bool bAllowInteractiveReuse = true;
		FAnnotationProcessingOptions AnnotationProcessing;
		FCompileProcessingOptions CompileProcessing;
		FScriptCompileExtensionsOptions ScriptCompileExtensions;
		bool bHasScriptExtensions = false;
		std::unordered_set<std::string> ScriptBootstrapDependencies;
		std::unordered_map<std::string, std::string> DependencyOverlays;
	};
	bool IsSemanticQuerySnapshotCacheable(const std::shared_ptr<const FSemanticQuerySnapshot>& Snapshot)
	{
		return Snapshot && Snapshot->Program && Snapshot->Program->bDeclarationPreparationCacheable
		    // The script task owns a reader combining code inputs with resource
		    // inputs. It is closed after preparation. A caller's custom reader alone
		    // cannot reconstruct that policy, so keep these results fresh.
		    && !(Snapshot->bHasScriptExtensions && (Snapshot->CompileProcessing.ReadDependency || Snapshot->AnnotationProcessing.ReadDependency));
	}
	bool IsSemanticQuerySnapshotCurrent(const std::shared_ptr<const FSemanticQuerySnapshot>& Snapshot)
	{
		if (!Snapshot)
		{
			return false;
		}
		// The host reader can describe files, missing files or virtual metadata.
		// Recheck the inputs actually observed by preparation, rather than only
		// the loader's disk stamps. Cacheability is separate from result validity:
		// a volatile processor's fresh result can still be returned to its caller.
		if (Snapshot->Program && (Snapshot->bHasScriptExtensions || Snapshot->AnnotationProcessing.ReadDependency || Snapshot->CompileProcessing.ReadDependency))
		{
			try
			{
				for (const auto& Dependency : Snapshot->Program->AnnotationDependencies)
				{
					CheckAnalysisCancellation();
					std::string Content;
					if (Snapshot->CompileProcessing.CheckCancelled)
					{
						Snapshot->CompileProcessing.CheckCancelled();
					}

					if (Snapshot->AnnotationProcessing.CheckCancelled)
					{
						Snapshot->AnnotationProcessing.CheckCancelled();
					}

					// The core uses the general reader when both registries are present.
					const auto& Reader = Snapshot->CompileProcessing.ReadDependency
					    ? Snapshot->CompileProcessing.ReadDependency
					    : Snapshot->AnnotationProcessing.ReadDependency;
					bool Exists = false;
					if (Reader)
					{
						// Bootstrap code uses the loader/overlays, not the caller's
						// resource reader. Its disk inputs are checked below. Custom
						// reader script results are never reused as cached snapshots.
						if (Snapshot->bHasScriptExtensions &&
						    Snapshot->ScriptBootstrapDependencies.count(
						        PathUtil::IdentityKey(Dependency.Path)))
						{
							continue;
						}

						Exists = Reader(Dependency.Path, Content);
					}
					else if (const auto Overlay =
					             Snapshot->DependencyOverlays.find(PathUtil::IdentityKey(Dependency.Path));
					    Overlay != Snapshot->DependencyOverlays.end())
					{
						Content = Overlay->second;
						Exists = true;
					}
					else
					{
						std::ifstream Input(PathUtil::FromUtf8(Dependency.Path), std::ios::binary);
						if (Input)
						{
							// Compare bounded chunks against the observed input. A file
							// that grew after preparation must not allocate an unbounded
							// validation buffer or delay cooperative cancellation.
							std::array<char, 8192> Buffer{};
							std::size_t Offset = 0;
							while (Input)
							{
								CheckAnalysisCancellation();
								Input.read(Buffer.data(), Buffer.size());
								const auto Count = static_cast<std::size_t>(Input.gcount());
								if (Count > Dependency.Content.size() - Offset ||
								    Dependency.Content.compare(Offset, Count, Buffer.data(), Count) != 0)
								{
									return false;
								}

								Offset += Count;
							}
							if (Input.bad() || !Dependency.bExists || Offset != Dependency.Content.size()) return false;
							continue;
						}
					}
					CheckAnalysisCancellation();
					if (Exists != Dependency.bExists || Content != Dependency.Content)
					{
						return false;
					}
				}
			}
			catch (const FAnalysisCancelled&)
			{
				// Completion publication also validates snapshots outside a request
				// control scope. A cancelled host reader invalidates that snapshot;
				// it must not escape and terminate the ordered protocol worker.
				if (ActiveAnalysisControl)
				{
					throw;
				}

				return false;
			}
			catch (...)
			{
				return false;
			}
		}
		for (const auto& Stamp : Snapshot->DiskStamps)
		{
			if (!Stamp.bValid)
			{
				return false;
			}
			std::error_code Error;
			const auto Path = PathUtil::FromUtf8(Stamp.Path);
			const auto Modified = std::filesystem::last_write_time(Path, Error);
			if (Error || Modified != Stamp.Modified)
			{
				return false;
			}
			const auto Size = std::filesystem::file_size(Path, Error);
			if (Error || Size != Stamp.Size)
			{
				return false;
			}
		}
		return true;
	}
	thread_local const FSemanticQuerySnapshot* ActiveSemanticSnapshot = nullptr;
	FSemanticQueryScope::FSemanticQueryScope(std::shared_ptr<const FSemanticQuerySnapshot> InSnapshot)
	    : Snapshot(std::move(InSnapshot)), Previous(ActiveSemanticSnapshot)
	{
		ActiveSemanticSnapshot = Snapshot.get();
	}
	FSemanticQueryScope::~FSemanticQueryScope()
	{
		ActiveSemanticSnapshot = Previous;
	}

	static bool HasMatchingSemanticSnapshot(std::string_view Source, std::string_view FileLabel,
	    const std::shared_ptr<const FModuleEnvironment>& Environment)
	{
		return ActiveSemanticSnapshot && ActiveSemanticSnapshot->bAllowInteractiveReuse && ActiveSemanticSnapshot->Program && ActiveSemanticSnapshot->Program->ModuleEnvironment == Environment && ActiveSemanticSnapshot->Source == Source && PathUtil::Equal(ActiveSemanticSnapshot->FileLabel, std::string(FileLabel));
	}

	namespace
	{
		class FNamespaceQueryScope;
		thread_local FNamespaceQueryScope* ActiveNamespaceQueryScope = nullptr;

		// A semantic request resolves many tokens against the same immutable program.
		// Build the namespace/import indexes lazily and retain them only in that request.
		class FNamespaceQueryScope
		{
		public:
			FNamespaceQueryScope(const FProgram& Program, const std::vector<FDocumentSymbol>& Symbols)
			    : Program(Program), Symbols(Symbols), Previous(ActiveNamespaceQueryScope)
			{
				ActiveNamespaceQueryScope = this;
			}
			~FNamespaceQueryScope()
			{
				ActiveNamespaceQueryScope = Previous;
			}
			FNamespaceQueryScope(const FNamespaceQueryScope&) = delete;
			FNamespaceQueryScope& operator=(const FNamespaceQueryScope&) = delete;
			bool Matches(const FProgram& Candidate, const std::vector<FDocumentSymbol>& CandidateSymbols) const
			{
				return &Program == &Candidate && &Symbols == &CandidateSymbols;
			}
			std::vector<std::string> Bind(const std::string& Name, const std::string& Scope, const bool bIncludeTypes)
			{
				if (!Names)
				{
					FAnalysisStage Stage("namespace-index");
					Names = std::make_unique<FNamespaceLookup>(Program);
					for (const auto& Symbol : Symbols)
					{
						if (Symbol.bSoftBuiltin)
						{
							Names->Add(Symbol.Name, {}, true, false);
						}
						// Visible types also own a qualified prefix in Core lookup.
						if (Symbol.Kind == EDocumentSymbolKind::Class || Symbol.Kind == EDocumentSymbolKind::Struct
						    || Symbol.Kind == EDocumentSymbolKind::Interface || Symbol.Kind == EDocumentSymbolKind::Enum
						    || Symbol.Kind == EDocumentSymbolKind::TypeAlias)
						{
							Types.insert(StripGenericArgs(Symbol.Name));
						}
					}
					Imports = std::make_unique<ModuleVisibility::FImportIndex>(Program.Imports);
				}
				const std::string LookupName = !Name.empty() && Name.front() == '.' ? "::" + Name.substr(1) : Name;
				return BindNamespacePath(LookupName, Scope, [&](const std::string& Candidate)
				{
					return (bIncludeTypes && Types.count(StripGenericArgs(Candidate)) != 0)
					    || StandardLibrary::IsNamespace(Candidate)
					    || Names->Contains(Program, Candidate, Program.EntryModulePath, Imports.get());
				});
			}

		private:
			const FProgram& Program;
			const std::vector<FDocumentSymbol>& Symbols;
			FNamespaceQueryScope* Previous;
			std::unique_ptr<FNamespaceLookup> Names;
			std::unique_ptr<ModuleVisibility::FImportIndex> Imports;
			std::unordered_set<std::string> Types;
		};

		std::vector<std::string> BindQueryNamespacePath(const FProgram& Program,
		    const std::vector<FDocumentSymbol>& Symbols, const std::string& Name, const std::string& Scope,
		    const bool bIncludeTypes = false)
		{
			if (ActiveNamespaceQueryScope && ActiveNamespaceQueryScope->Matches(Program, Symbols))
			{
				return ActiveNamespaceQueryScope->Bind(Name, Scope, bIncludeTypes);
			}
			FNamespaceQueryScope QueryScope(Program, Symbols);
			return QueryScope.Bind(Name, Scope, bIncludeTypes);
		}
		std::vector<std::string> BindQueryQualifiedPath(const FProgram& Program,
		    const std::vector<FDocumentSymbol>& Symbols, const std::string& Name,
		    const std::string& Scope, const FTextPosition Position)
		{
			auto Targets = BindQueryNamespacePath(Program, Symbols, Name, Scope, true);
			if (!Targets.empty() || Name.rfind("::", 0) == 0 || (!Name.empty() && Name.front() == '.')) return Targets;
			for (const auto& Directive : Program.UsingNamespaces)
			{
				if (Directive.ModulePath != Program.EntryModulePath || (!Directive.ScopeNamespace.empty() && Scope != Directive.ScopeNamespace && Scope.rfind(Directive.ScopeNamespace + ".", 0) != 0) || Position.Line < Directive.Location.Line || (Position.Line == Directive.Location.Line && Position.Column < Directive.Location.Column))
				{
					continue;
				}
				for (const auto& Target : BindQueryNamespacePath(Program, Symbols, Directive.NamespaceName, Directive.ScopeNamespace))
				{
					const auto Candidates = BindQueryNamespacePath(Program, Symbols, Name, Target, true);
					Targets.insert(Targets.end(), Candidates.begin(), Candidates.end());
				}
			}
			return Targets;
		}

		#include "SymbolQuery/CompletionCore.inl"
		#include "SymbolQuery/MacrosAndNamespaces.inl"
		#include "SymbolQuery/MemberCompletion.inl"
		#include "SymbolQuery/LexicalResolution.inl"
		#include "SymbolQuery/LocalScopes.inl"
		#include "SymbolQuery/FilteringAndRecovery.inl"
		#include "SymbolQuery/ReceiverTypes.inl"
		#include "SymbolQuery/GenericAndQuery.inl"

	}

	std::string_view StripDeclMetaDisplay(const std::string_view Detail)
	{
		std::string_view View = Detail;
		const auto Trim = [](std::string_view& Value)
		{
			while (!Value.empty() && (Value.front() == ' ' || Value.front() == '\t'))
			{
				Value.remove_prefix(1);
			}
			while (!Value.empty() && (Value.back() == ' ' || Value.back() == '\t'))
			{
				Value.remove_suffix(1);
			}
		};
		const auto FindMatchingClose = [](const std::string_view Text, const std::size_t Open) -> std::size_t
		{
			if (Open >= Text.size() || Text[Open] != '[')
			{
				return std::string_view::npos;
			}
			int BracketDepth = 0;
			int ParenDepth = 0;
			for (std::size_t Index = Open; Index < Text.size(); ++Index)
			{
				const char Char = Text[Index];
				if (Char == '[' && ParenDepth == 0)
				{
					++BracketDepth;
				}
				else if (Char == ']' && ParenDepth == 0)
				{
					--BracketDepth;
					if (BracketDepth == 0)
					{
						return Index;
					}
				}
				else if (Char == '(')
				{
					++ParenDepth;
				}
				else if (Char == ')' && ParenDepth > 0)
				{
					--ParenDepth;
				}
			}
			return std::string_view::npos;
		};
		const auto FindMatchingOpen = [&FindMatchingClose](const std::string_view Text) -> std::size_t
		{
			if (Text.empty() || Text.back() != ']')
			{
				return std::string_view::npos;
			}
			for (std::size_t Start = Text.size(); Start > 0; --Start)
			{
				const std::size_t Candidate = Start - 1;
				if (Text[Candidate] != '[')
				{
					continue;
				}
				if (FindMatchingClose(Text, Candidate) == Text.size() - 1)
				{
					return Candidate;
				}
			}
			return std::string_view::npos;
		};

		for (;;)
		{
			Trim(View);
			if (View.empty())
			{
				break;
			}
			if (View.front() == '[')
			{
				const std::size_t Close = FindMatchingClose(View, 0);
				if (Close == std::string_view::npos)
				{
					break;
				}
				View.remove_prefix(Close + 1);
				continue;
			}
			if (View.back() == ']')
			{
				const std::size_t Open = FindMatchingOpen(View);
				// Display annotations are separated from the type/signature.
				// An attached suffix is part of a native array type (`T[Count]`).
				if (Open == std::string_view::npos || Open == 0 || (View[Open - 1] != ' ' && View[Open - 1] != '\t'))
				{
					break;
				}
				View = View.substr(0, Open);
				continue;
			}
			break;
		}
		Trim(View);
		return View;
	}

	std::string TypeNameFromDetail(const std::string_view Detail)
	{
		std::string_view View = StripDeclMetaDisplay(Detail);
		// Detail is a display surface. Accept both the current `T&` spelling and
		// legacy/internal qualifier prefixes when recovering the semantic type.
		bool bRemovedQualifier = true;
		while (bRemovedQualifier)
		{
			bRemovedQualifier = false;
			for (const std::string_view Prefix : {std::string_view("const "), std::string_view("constexpr "), std::string_view("ref "), std::string_view("static ")})
			{
				if (View.size() >= Prefix.size() && View.compare(0, Prefix.size(), Prefix) == 0)
				{
					View.remove_prefix(Prefix.size());
					bRemovedQualifier = true;
					break;
				}
			}
		}
		int Depth = 0, ParenDepth = 0, BracketDepth = 0;
		std::size_t End = View.size();
		for (std::size_t Index = 0; Index < View.size(); ++Index)
		{
			const char Char = View[Index];
			if (Char == '[')
			{
				++BracketDepth;
				continue;
			}
			if (Char == ']' && BracketDepth > 0)
			{
				--BracketDepth;
				continue;
			}
			if (BracketDepth > 0)
			{
				continue;
			}
			if (Char == '<')
			{
				++Depth;
				continue;
			}
			if (Char == '>' && (Index == 0 || View[Index - 1] != '-'))
			{
				if (Depth > 0)
				{
					--Depth;
				}
				continue;
			}
			if (Char == '(' && (Depth > 0 || ParenDepth > 0 || (Index >= 2 && View.substr(Index - 2, 2) == "fn")))
			{
				++ParenDepth;
				continue;
			}
			if (Char == ')' && ParenDepth > 0)
			{
				--ParenDepth;
				continue;
			}
			if (Depth == 0 && ParenDepth == 0 && BracketDepth == 0 && (Char == ' ' || Char == '('))
			{
				// A callable's result may contain a qualifier separated by spaces.
				// Keep `fn()->const T&` intact, then stop before the declaration name.
				std::size_t WordStart = Index;
				while (WordStart > 0 && std::isalpha(static_cast<unsigned char>(View[WordStart - 1])))
				{
					--WordStart;
				}

				const auto Word = View.substr(WordStart, Index - WordStart);
				if (Char == ' ' && (Word == "const" || Word == "ref"))
				{
					continue;
				}

				End = Index;
				break;
			}
		}
		while (End > 0 && View[End - 1] == '&' && View.compare(0, 3, "fn(") != 0)
		{
			--End;
		}
		return std::string(View.substr(0, End));
	}

	namespace
	{
		FSymbolQueryResult QueryWithLoadSource(
		    const std::string_view LoadSource,
		    const std::string_view OriginalSource,
		    const FTextPosition Position,
		    const std::string_view FileLabel,
		    const FSymbolQueryOptions& Options,
		    std::shared_ptr<const FSemanticQuerySnapshot>* const OutSnapshot = nullptr)
		{
			if (LoadSource == OriginalSource && HasMatchingSemanticSnapshot(OriginalSource, FileLabel, Options.ModuleEnvironment))
			{
				return QueryLoaded(*ActiveSemanticSnapshot->Program, ActiveSemanticSnapshot->Symbols,
				    OriginalSource, FileLabel, Position, Options.ImportPaths, Options.Defines,
				    Options.PredefinedMacros, Options.Bindings, &ActiveSemanticSnapshot->LineMap);
			}
			FDocumentSymbolOptions SymbolOptions;
			SymbolOptions.ImportPaths = Options.ImportPaths;
			SymbolOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
			SymbolOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
			SymbolOptions.bAllowCircularImports = Options.bAllowCircularImports;
			SymbolOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			SymbolOptions.Defines = Options.Defines;
			SymbolOptions.PredefinedMacros = Options.PredefinedMacros;
			SymbolOptions.SourceOverlays = Options.SourceOverlays;
			SymbolOptions.bIncludeImports = true;
			SymbolOptions.bIncludeImportedExports = true;
			SymbolOptions.bIncludeSoftBuiltins = true;
			SymbolOptions.Bindings = Options.Bindings;
			SymbolOptions.AnnotationProcessing = Options.AnnotationProcessing;
			SymbolOptions.CompileProcessing = Options.CompileProcessing;
			SymbolOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			SymbolOptions.ModuleEnvironment = Options.ModuleEnvironment;
			SymbolOptions.bIncludeHostBindings = Options.Bindings != nullptr;

			auto Loader = std::make_unique<VeroScriptModuleLoader>("", Options.ModuleEnvironment);
			ApplyModuleLoadingOptions(*Loader, Options);
			VeroScript::Tooling::ApplyHostTypeNames(*Loader, Options.Bindings);
			ApplyDefines(*Loader, Options.Defines);
			ApplyPredefinedMacros(*Loader, Options.PredefinedMacros);
			for (const FSourceOverlay& Overlay : Options.SourceOverlays)
			{
				Loader->SetSourceOverlay(Overlay.Path, Overlay.Source);
			}

			std::unique_ptr<FProgram> Program = Loader->LoadSourceModule(LoadSource, FileLabel);
			if (Program)
			{
				FScriptError Error;
				if (!Detail::PrepareQueryProgram(Program, Options, &Error)) return Fail(std::move(Error));
			}
			if (Loader->HasError() || !Program)
			{
				if (Loader->HasError())
				{
					return FailFromLoader(*Loader);
				}
				FScriptError Error;
				FillDiagnostic(
				    Error,
				    "diag.failed_to_load_source_module",
				    FSourceLocation{std::string(FileLabel), 1, 1});
				return Fail(std::move(Error));
			}

			FDocumentSymbolResult SymbolsResult =
			    VeroScriptDocumentSymbols::CollectFromProgram(*Program, SymbolOptions);
			auto Result = QueryLoaded(*Program, SymbolsResult, OriginalSource, FileLabel, Position,
			    Options.ImportPaths, Options.Defines, Options.PredefinedMacros,
			    Options.Bindings, ProgramPreprocessorLineMap(*Program, FileLabel));
			if (OutSnapshot && SymbolsResult.bSuccess && Program->bDeclarationPreparationCacheable && LoadSource == OriginalSource && !Program->ScriptCompileExtensionTask && Program->AnnotationDependencies.empty() && !Options.AnnotationProcessing.Registry && !Options.CompileProcessing.Registry && IsDefaultScriptCompileExtensionProcessing(Options.ScriptCompileExtensions) && !HasScriptCompileExtensionImport(OriginalSource))
			{
				auto Snapshot = std::make_shared<FSemanticQuerySnapshot>();
				Snapshot->Source = OriginalSource;
				Snapshot->FileLabel = FileLabel;
				Snapshot->DiskStamps = Loader->GetLoadedFileStamps();
				if (const auto* Map = ProgramPreprocessorLineMap(*Program, FileLabel))
				{
					Snapshot->LineMap = *Map;
				}
				else
				{
					std::string Error;
					if (!BuildModulePreprocessorLineMap(OriginalSource, FileLabel, Options, Snapshot->LineMap, Error))
					{
						return Result;
					}
				}
				Snapshot->Loader = std::move(Loader);
				Snapshot->Program = std::move(Program);
				Snapshot->Symbols = std::move(SymbolsResult);
				*OutSnapshot = std::move(Snapshot);
			}
			return Result;
		}

		bool TryQuerySoftBuiltinLexically(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    const VeroScript::IHostBindingCatalog* const Bindings,
		    FSymbolQueryResult& Out)
		{
			std::string TokenText;
			std::uint32_t TokenLine = 0;
			std::uint32_t TokenColumn = 0;
			std::uint32_t TokenEndColumn = 0;
			if (!FindQueryTokenAt(
			        Source,
			        FileLabel,
			        Position,
			        TokenText,
			        TokenLine,
			        TokenColumn,
			        TokenEndColumn) ||
			    TokenText.empty())
			{
				return false;
			}

			FMemberCompletionContext MemberContext;
			if (FindMemberCompletionContext(Source, FileLabel, Position, MemberContext) && MemberContext.bActive && (!MemberContext.bNamespacePrefix || MemberContext.bPostfixReceiver))
			{
				// A complex receiver such as values[0] or make() has no qualified
				// identifier path. Do not treat its member as a top-level builtin.
				return false;
			}

			FProgram EmptyProgram;
			std::vector<FDocumentSymbol> SoftSymbols;
			AppendSoftBuiltinSymbols(EmptyProgram, SoftSymbols);
			AppendHostAnnotationSymbols(Bindings, SoftSymbols);

			// Preserve a namespace-qualified spelling even when another part of the
			// document cannot be parsed. A leaf-name fallback would hover std::print
			// as print, or attach print's detail to an unrelated receiver.
			std::string Qualified = QualifiedIdentifierAt(
			    Source, FileLabel, TokenLine, TokenColumn);
			if (!Qualified.empty() && Qualified.front() == '.')
			{
				Qualified.erase(0, 1);
			}
			const std::string_view QueryName = Qualified.empty()
			    ? std::string_view(TokenText)
			    : std::string_view(Qualified);
			// Lexical fallback only resolves top-level soft functions (print/to_string/…).
			// Member names like push stay on the recovered-parse path to avoid wrong Detail.
			const FDocumentSymbol* Match = nullptr;
			for (const FDocumentSymbol& Symbol : SoftSymbols)
			{
				if ((Symbol.Kind == EDocumentSymbolKind::Function || Symbol.Kind == EDocumentSymbolKind::Annotation) && Symbol.Name == QueryName)
				{
					Match = &Symbol;
					break;
				}
			}
			if (!Match)
			{
				return false;
			}

			Out = FSymbolQueryResult{};
			Out.bSuccess = true;
			Out.bHasToken = true;
			Out.TokenText = TokenText;
			Out.TokenLine = TokenLine;
			Out.TokenColumn = TokenColumn;
			Out.TokenEndColumn = TokenEndColumn;

			FSymbolQueryHit Hit;
			Hit.Kind = Match->Kind;
			Hit.Name = Match->Name;
			Hit.Detail = Match->Detail;
			Hit.Documentation = Match->Documentation;
			Hit.bSoftBuiltin = true;
			Hit.bExported = Match->bExported;
			Hit.DefLine = Match->StartLine;
			Hit.DefColumn = Match->StartColumn;
			Out.Definitions.push_back(std::move(Hit));
			return true;
		}
	} // namespace

	FSymbolQueryResult VeroScriptSymbolQuery::QueryAtSource(const std::string_view Source,
	    const FTextPosition Position, const std::string_view FileLabel,
	    const FSymbolQueryOptions& RequestedOptions,
	    std::shared_ptr<const FSemanticQuerySnapshot>* const OutSnapshot)
	{
		if (OutSnapshot)
		{
			OutSnapshot->reset();
		}

		auto Options = RequestedOptions;
		std::string EnvironmentError;
		if (!ResolveToolingModuleEnvironment(Options, EnvironmentError))
		{
			return ToolingModuleEnvironmentFailure<FSymbolQueryResult>(EnvironmentError);
		}

		const auto StandardBindings = StandardLibrary::MergeBindingCatalog(Options.Bindings, nullptr);
		Options.Bindings = StandardBindings.get();
		FQueryLexicalSnapshot LexicalSnapshot(Source, FileLabel);
		std::size_t Caret = 0;
		FImportPathCompletionContext ImportContext;
		if (FindSourceOffset(Source, Position.Line, Position.Column, Caret) && FindImportPathCompletionContext(Source, Position, ImportContext))
		{
			// Import navigation only needs resolution. In particular, opening an
			// extension path should not execute its processors or parse unrelated
			// incomplete business code before returning the target.
			FSymbolQueryResult Result;
			Result.bSuccess = true;
			Result.bHasToken = true;
			Result.TokenText = ImportContext.PathText;
			Result.TokenLine = ImportContext.ReplaceLine;
			Result.TokenColumn = ImportContext.ReplaceStartColumn;
			Result.TokenEndColumn = ImportContext.ReplaceEndColumn;
			std::string Decoded, Resolved;
			if (!DecodeImportPathLiteral(
			        (ImportContext.bRawString ? "r\"" : "\"") + ImportContext.PathText + "\"",
			        ImportContext.bCompilerExtension, Decoded))
			{
				return Result;
			}

			VeroScriptModuleLoader Resolver("", Options.ModuleEnvironment);
			ApplyModuleLoadingOptions(Resolver, Options);
			for (const auto& Overlay : Options.SourceOverlays)
			{
				Resolver.SetSourceOverlay(Overlay.Path, {});
			}

			const auto Directory = PathUtil::ToUtf8(PathUtil::FromUtf8(FileLabel).parent_path());
			bool Found = false;
			try
			{
				Found = Resolver.ResolveImportPath(Decoded, Directory, Resolved, FileLabel);
			}
			catch (const FAnalysisCancelled&)
			{
				throw;
			}
			catch (const std::exception&)
			{
				return Result;
			}
			if (Found)
			{
				FSymbolQueryHit Hit;
				Hit.Name = Decoded;
				Hit.Kind = EDocumentSymbolKind::Import;
				Hit.Detail = ImportContext.bCompilerExtension ? "compilation extension"
				    : ImportContext.bReExport                 ? "re-exported module"
				                                              : "module";
				if (ImportContext.bReExport)
				{
					Hit.Documentation =
					    "Forwards declarations exported by the target module. Private declarations remain hidden.";
				}

				Hit.FileLabel = std::move(Resolved);
				Hit.DefLine = Hit.DefColumn = 1;
				Result.Definitions.push_back(std::move(Hit));
			}
			return Result;
		}
		FSymbolQueryResult Result =
		    QueryWithLoadSource(Source, Source, Position, FileLabel, Options, OutSnapshot);
		if (Result.bSuccess)
		{
			return Result;
		}
		const FSymbolQueryResult PrimaryFailure = Result;

		// Mirror completion soft-recovery so mid-edit / broken buffers still hover.
		std::string Recovered;
		auto TryQueryRecovered = [&](const std::string& Candidate) -> bool
		{
			Result = QueryWithLoadSource(Candidate, Source, Position, FileLabel, Options);
			return Result.bSuccess;
		};

		bool bTriedSemicolonRecover = false;
		auto TrySemicolonRecover = [&]() -> bool
		{
			if (bTriedSemicolonRecover)
			{
				return false;
			}
			bTriedSemicolonRecover = true;
			return TryRecoverSourceAtExpectedSemicolon(
			           Source,
			           FileLabel,
			           PrimaryFailure.Diagnostics,
			           Recovered) &&
			    TryQueryRecovered(Recovered);
		};

		if (DiagnosticsSuggestMissingSemicolon(FileLabel, PrimaryFailure.Diagnostics) && TrySemicolonRecover())
		{
			return Result;
		}

		if (TryRecoverSourceAfterIdentifier(Source, FileLabel, Position, Recovered) && TryQueryRecovered(Recovered))
		{
			return Result;
		}
		if (TryRecoverSourceAfterMemberDot(Source, FileLabel, Position, Recovered) && TryQueryRecovered(Recovered))
		{
			return Result;
		}
		if (TryRecoverSourceAfterTrailingQualifiedScope(Source, FileLabel, Recovered) && TryQueryRecovered(Recovered))
		{
			return Result;
		}
		if (TryRecoverSourceAfterGoto(Source, FileLabel, Position, Recovered) && TryQueryRecovered(Recovered))
		{
			return Result;
		}
		if (TryRecoverSourceAfterCapture(Source, FileLabel, Position, Recovered) && TryQueryRecovered(Recovered))
		{
			return Result;
		}

		if (TrySemicolonRecover())
		{
			return Result;
		}
		// Use the same bounded executable-prefix recovery as completion. A later
		// broken declaration must not discard a valid member's hover or navigation.
		FQueryLexer PrefixLexer(Source, FileLabel);
		if (FindSourceOffset(Source, Position.Line, Position.Column, Caret) && TryRecoverExecutablePrefix(Source, PrefixLexer.Tokenize(), Position, Caret, Recovered))
		{
			// Closing an unfinished lambda also closes its variable initializer.
			// Mirror completion's bounded suffix repair before abandoning a valid
			// local hover/definition because unrelated code after the caret is broken.
			for (int Attempt = 0; Attempt < 3; ++Attempt)
			{
				if (TryQueryRecovered(Recovered)) return Result;
				std::string Next;
				if (!TryRecoverSourceAtExpectedSemicolon(Recovered, FileLabel, Result.Diagnostics, Next) || Next == Recovered)
				{
					break;
				}
				Recovered = std::move(Next);
			}
		}

		// Still unrecoverable: soft-builtin lexical fallback for print/to_string/len.
		const auto QueryBindings = SourceMetadataQueryBindings(Options.Bindings, Options.CompileProcessing.AnnotationDefinitions);
		if (TryQuerySoftBuiltinLexically(Source, FileLabel, Position, QueryBindings.get(), Result))
		{
			return Result;
		}

		return PrimaryFailure;
	}

	FSymbolQueryResult VeroScriptSymbolQuery::QueryAtFile(
	    const std::string_view FilePath,
	    const FTextPosition Position,
	    const FSymbolQueryOptions& Options)
	{
		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		if (!Input)
		{
			FScriptError Error;
			FillDiagnostic(
			    Error,
			    "diag.cannot_open_script",
			    FSourceLocation{std::string(FilePath), 1, 1},
			    {FilePath});
			return Fail(std::move(Error));
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		const std::string Source = Buffer.str();

		// Use the same in-memory snapshot for tokens, locals, and document symbols.
		return QueryAtSource(Source, Position, FilePath, Options);
	}

	FSymbolQueryResult VeroScriptSymbolQuery::QueryInProgram(
	    const FProgram& Program, const FDocumentSymbolResult& Symbols,
	    const std::string_view Source, const std::string_view FileLabel, const FTextPosition Position,
	    const IHostBindingCatalog* Bindings, const FPreprocessLineMap& LineMap)
	{
		return QueryLoaded(Program, Symbols, Source, FileLabel, Position, {}, {}, {}, Bindings, &LineMap);
	}

	std::string VeroScriptSymbolQuery::ResolveMemberReceiverTypeInProgram(
	    const FProgram& Program, const FDocumentSymbolResult& Symbols,
	    const std::string_view Source, const std::string_view FileLabel,
	    const FTextPosition PhysicalPosition, const FTextPosition AstPosition, const IHostBindingCatalog* Bindings)
	{
		FMemberCompletionContext Context;
		return FindMemberCompletionContext(Source, FileLabel, PhysicalPosition, Context)
		    ? ResolveReceiverExpressionType(Program, Symbols, AstPosition, Context.ReceiverExpression,
		          Bindings, ResolveEnclosingNamespaceName(Program, AstPosition,
		              FindActiveNamespaceAt(Source, FileLabel, PhysicalPosition)))
		    : std::string{};
	}

	FSemanticSymbolResult VeroScriptSymbolQuery::SemanticTokensAtSource(const std::string_view Source,
	    const std::string_view FileLabel, const FSymbolQueryOptions& RequestedOptions)
	{
		auto Options = RequestedOptions;
		std::string EnvironmentError;
		if (!ResolveToolingModuleEnvironment(Options, EnvironmentError))
		{
			return ToolingModuleEnvironmentFailure<FSemanticSymbolResult>(EnvironmentError);
		}

		const auto StandardBindings = StandardLibrary::MergeBindingCatalog(Options.Bindings, nullptr);
		Options.Bindings = StandardBindings.get();
		FQueryLexicalSnapshot LexicalSnapshot(Source, FileLabel);
		FDocumentSymbolOptions SymbolOptions;
		SymbolOptions.ImportPaths = Options.ImportPaths;
		SymbolOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
		SymbolOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
		SymbolOptions.bAllowCircularImports = Options.bAllowCircularImports;
		SymbolOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		SymbolOptions.Defines = Options.Defines;
		SymbolOptions.PredefinedMacros = Options.PredefinedMacros;
		SymbolOptions.SourceOverlays = Options.SourceOverlays;
		SymbolOptions.bIncludeImports = true;
		SymbolOptions.bIncludeImportedExports = true;
		SymbolOptions.bIncludeSoftBuiltins = true;
		SymbolOptions.Bindings = Options.Bindings;
		SymbolOptions.AnnotationProcessing = Options.AnnotationProcessing;
		SymbolOptions.CompileProcessing = Options.CompileProcessing;
		SymbolOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		SymbolOptions.ModuleEnvironment = Options.ModuleEnvironment;
		SymbolOptions.bIncludeHostBindings = Options.Bindings != nullptr;

		const auto MakeLoader = [&Options]()
		{
			auto Loader = std::make_unique<VeroScriptModuleLoader>("", Options.ModuleEnvironment);
			ApplyModuleLoadingOptions(*Loader, Options);
			VeroScript::Tooling::ApplyHostTypeNames(*Loader, Options.Bindings);
			ApplyDefines(*Loader, Options.Defines);
			ApplyPredefinedMacros(*Loader, Options.PredefinedMacros);
			for (const FSourceOverlay& Overlay : Options.SourceOverlays)
			{
				Loader->SetSourceOverlay(Overlay.Path, Overlay.Source);
			}
			return Loader;
		};

		auto Loader = MakeLoader();
		std::vector<std::string> ScriptBootstrapDependencies;
		bool bRecoveredProgram = false;
		std::unique_ptr<FProgram> Program = Loader->LoadSourceModule(Source, FileLabel);
		if (Program)
		{
			FScriptError Error;
			FSemanticSymbolResult PreparationFailed;
			if (!Detail::PrepareQueryProgram(Program, Options, &Error,
			        &PreparationFailed.Diagnostics, &PreparationFailed.Dependencies, &ScriptBootstrapDependencies))
			{
				PreparationFailed.Error = Error;
				return PreparationFailed;
			}
		}
		if (Loader->HasError() || !Program)
		{
			FSemanticSymbolResult Failed;
			if (Loader->HasError())
			{
				Failed.Error = Loader->GetError();
				Failed.Diagnostics = Loader->GetDiagnostics();
			}

			// Semantic tokens are a full-document response. Recover narrowly scoped
			// mid-edit states (a missing terminator or a trailing `::`) so confirmed
			// namespace segments remain colored without publishing a guessed leaf.
			// Other parse failures still return null and let the IDE keep its last
			// coherent semantic snapshot.
			std::string Recovered;
			const bool bRecoveredTrailingScope =
			    TryRecoverSourceAfterTrailingQualifiedScope(Source, FileLabel, Recovered);
			if (!bRecoveredTrailingScope && !TryRecoverSourceAtExpectedSemicolon(Source, FileLabel, Failed.Diagnostics, Recovered))
			{
				return Failed;
			}
			auto RecoveredLoader = MakeLoader();
			Program = RecoveredLoader->LoadSourceModule(Recovered, FileLabel);
			if (Program)
			{
				FScriptError Error;
				FSemanticSymbolResult PreparationFailed;
				if (!Detail::PrepareQueryProgram(Program, Options, &Error,
				        &PreparationFailed.Diagnostics, &PreparationFailed.Dependencies, &ScriptBootstrapDependencies))
				{
					PreparationFailed.Error = Error;
					return PreparationFailed;
				}
			}
			if (RecoveredLoader->HasError() || !Program)
			{
				return Failed;
			}
			Loader = std::move(RecoveredLoader);
			bRecoveredProgram = true;
		}

		auto Snapshot = std::make_shared<FSemanticQuerySnapshot>();
		Snapshot->DiskStamps = Loader->GetLoadedFileStamps();
		FDocumentSymbolResult SymbolsResult;
		{
			FAnalysisStage Stage("symbols");
			CheckAnalysisCancellation();
			SymbolsResult = VeroScriptDocumentSymbols::CollectFromProgram(*Program, SymbolOptions);
		}
		if (!SymbolsResult.bSuccess)
		{
			FSemanticSymbolResult Failed;
			Failed.Error = SymbolsResult.Error;
			Failed.Diagnostics = SymbolsResult.Diagnostics;
			return Failed;
		}

		FAnalysisStage QueryStage("query");
		CheckAnalysisCancellation();
		// Macro aliases such as FString=string and TArray=array are physical-source
		// type spellings even though the preprocessed AST contains only the target.
		std::unordered_set<std::string> TypeMacroNames;
		for (const std::string_view Define : Options.Defines)
		{
			const std::size_t Equals = Define.find('=');
			if (Equals == std::string_view::npos)
			{
				continue;
			}
			std::string_view Name = Define.substr(0, Equals);
			std::string_view Replacement = Define.substr(Equals + 1);
			while (!Name.empty() && std::isspace(static_cast<unsigned char>(Name.back())))
			{
				Name.remove_suffix(1);
			}
			while (!Replacement.empty() && std::isspace(static_cast<unsigned char>(Replacement.front())))
			{
				Replacement.remove_prefix(1);
			}
			const std::size_t End = Replacement.find_first_of(" \t\r\n<*&");
			const std::string_view Head = Replacement.substr(0, End);
			if (!Name.empty() && IsKnownTypeName(Head, SymbolsResult.Symbols, Options.Bindings))
			{
				TypeMacroNames.emplace(Name);
			}
		}

		VeroScriptLexer Lexer(Source, FileLabel);
		const std::vector<FToken>& Lexed = Lexer.Tokenize();
		VeroScript::FPreprocessLineMap LineMap;
		std::string PreprocessError;
		if (const auto PreparedMap = ProgramPreprocessorLineMap(*Program, FileLabel))
		{
			LineMap = *PreparedMap;
		}

		else
		{
			BuildModulePreprocessorLineMap(Source, FileLabel, Options, LineMap, PreprocessError);
		}

		FSemanticSymbolResult Result;
		Result.bSuccess = true;
		Result.Dependencies.assign(
		    Loader->GetLoadedModulePaths().begin(), Loader->GetLoadedModulePaths().end());
		for (const auto& D : Program->AnnotationDependencies)
		{
			Result.Dependencies.push_back(D.Path);
		}

		Result.Tokens.reserve(Lexed.size());
		std::unordered_map<std::size_t, EDocumentSymbolKind> CompositeKinds;
		CollectCompositeSemanticKinds(
		    *Program,
		    Lexed,
		    SymbolsResult.Symbols,
		    FileLabel,
		    Options.Bindings,
		    CompositeKinds);
		std::unordered_map<std::size_t, FGenericTypeParameterToken> GenericTokens;
		CollectGenericTypeParameterTokens(Lexed, GenericTokens);
		std::unordered_set<std::uint64_t> MutableFieldLocations;
		const auto CollectMutableFields = [&](const auto& Types)
		{
			for (const auto& Type : Types)
			{
				if (Type.ModulePath != Program->EntryModulePath)
				{
					continue;
				}
				for (const VeroScript::FFieldDecl& Field : Type.Fields)
				{
					if (!Field.bIsMutable)
					{
						continue;
					}
					std::uint32_t Line = Field.Location.Line;
					std::uint32_t Column = Field.Location.Column;
					(void)TryRemapPreprocessedToPhysical(
					    LineMap, Line, Column, Line, Column);
					MutableFieldLocations.insert(
					    (static_cast<std::uint64_t>(Line) << 32u) | Column);
				}
			}
		};
		CollectMutableFields(Program->Structs);
		CollectMutableFields(Program->Classes);
		const auto SourceBindings = SourceMetadataQueryBindings(Options.Bindings, Program->SourceMetadata.get());
		const auto SemanticBindings = ScopedQueryBindings(*Program, SourceBindings.get(), FileLabel);
		FCallableQueryScope CallableScope(*Program);
		FSymbolLookupScope SymbolScope(SymbolsResult.Symbols);
		FNamespaceQueryScope NamespaceScope(*Program, SymbolsResult.Symbols);
		for (std::size_t TokenIndex = 0; TokenIndex < Lexed.size(); ++TokenIndex)
		{
			if ((TokenIndex & 31u) == 0)
			{
				CheckAnalysisCancellation();
			}
			const FToken& Token = Lexed[TokenIndex];
			if (Token.Kind != ETokenKind::Identifier)
			{
				continue;
			}

			FSemanticSymbolToken Semantic;
			Semantic.Line = Token.Location.Line;
			Semantic.Column = Token.Location.Column;
			Semantic.EndColumn =
			    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());

			if (Token.Lexeme == "mutable" && MutableFieldLocations.find((static_cast<std::uint64_t>(Token.Location.Line) << 32u) | Token.Location.Column) != MutableFieldLocations.end())
			{
				Semantic.Kind = EDocumentSymbolKind::Keyword;
				Result.Tokens.push_back(std::move(Semantic));
				continue;
			}

			if (TypeMacroNames.find(std::string(Token.Lexeme)) != TypeMacroNames.end())
			{
				Semantic.Kind = EDocumentSymbolKind::TypeAlias;
				Result.Tokens.push_back(std::move(Semantic));
				continue;
			}

			if (const auto Generic = GenericTokens.find(TokenIndex); Generic != GenericTokens.end())
			{
				Semantic.Kind = Generic->second.bValue ? EDocumentSymbolKind::Constant : EDocumentSymbolKind::TypeAlias;
				Result.Tokens.push_back(std::move(Semantic));
				continue;
			}

			if (const auto Composite = CompositeKinds.find(TokenIndex);
			    Composite != CompositeKinds.end())
			{
				Semantic.Kind = Composite->second;
				Result.Tokens.push_back(std::move(Semantic));
				continue;
			}

			const VeroScript::FHostAnnotationDesc* EnclosingAnnotation = nullptr;
			if (FindHostAnnotationArgumentAt(
			        Source,
			        Token.Location.Line,
			        Token.Location.Column,
			        Token.Lexeme,
			        Options.Bindings,
			        EnclosingAnnotation))
			{
				Semantic.Kind = EDocumentSymbolKind::AnnotationArgument;
				Result.Tokens.push_back(std::move(Semantic));
				continue;
			}

			const FSymbolQueryResult Query = QueryLoaded(
			    *Program,
			    SymbolsResult,
			    Source,
			    FileLabel,
			    FTextPosition{Token.Location.Line, Token.Location.Column},
			    Options.ImportPaths,
			    Options.Defines,
			    Options.PredefinedMacros,
			    SemanticBindings.get(),
			    &LineMap,
			    false);
			if (!Query.bSuccess || Query.Definitions.empty())
			{
				continue;
			}
			Semantic.Kind = Query.Definitions.front().Kind;
			Semantic.Modifiers = Query.Definitions.front().bReadOnly
			    ? SemanticTokenModifiers::Readonly
			    : SemanticTokenModifiers::None;
			Result.Tokens.push_back(std::move(Semantic));
		}
		Snapshot->Source = Source;
		Snapshot->AnnotationProcessing = Options.AnnotationProcessing;
		Snapshot->CompileProcessing = Options.CompileProcessing;
		Snapshot->ScriptCompileExtensions = Options.ScriptCompileExtensions;
		Snapshot->bHasScriptExtensions = !Options.ScriptCompileExtensions.Roots.empty() || HasScriptCompileExtensionImport(Source);
		Snapshot->ScriptBootstrapDependencies.insert(ScriptBootstrapDependencies.begin(), ScriptBootstrapDependencies.end());
		std::unordered_set<std::string> DependencyPaths;
		for (const auto& D : Program->AnnotationDependencies)
		{
			DependencyPaths.insert(PathUtil::IdentityKey(D.Path));
		}

		for (const auto& Overlay : Options.SourceOverlays)
		{
			if (DependencyPaths.count(PathUtil::IdentityKey(Overlay.Path)))
			{
				Snapshot->DependencyOverlays[PathUtil::IdentityKey(Overlay.Path)] = Overlay.Source;
			}
		}

		Snapshot->FileLabel = FileLabel;
		Snapshot->Loader = std::move(Loader);
		Snapshot->Program = std::move(Program);
		Snapshot->Symbols = std::move(SymbolsResult);
		Snapshot->LineMap = std::move(LineMap);
		// Recovered coloring can be cached, but point queries must keep their own
		// caret-specific recovery rather than treating the patched AST as original.
		Snapshot->bAllowInteractiveReuse = !bRecoveredProgram && !Options.AnnotationProcessing.Registry && !Options.CompileProcessing.Registry && IsDefaultScriptCompileExtensionProcessing(Options.ScriptCompileExtensions) && !HasScriptCompileExtensionImport(Source);
		Result.Snapshot = std::move(Snapshot);
		return Result;
	}

	void VeroScriptSymbolQuery::CollectVisibleLocalsAt(
	    const FProgram& Program,
	    const FTextPosition Position,
	    std::vector<FSymbolQueryHit>& OutHits)
	{
		CollectVisibleLocals(Program, Position, OutHits);
	}

	void VeroScriptSymbolQuery::CollectVisibleLocalsAt(
	    const FProgram& Program,
	    const std::string_view Source,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
	    const FTextPosition PhysicalPosition,
	    std::vector<FSymbolQueryHit>& OutHits)
	{
		VeroScript::FPreprocessLineMap OwnedLineMap;
		const auto Snapshot = ProgramPreprocessorLineMap(Program, Program.EntryModulePath);
		if (!Snapshot)
		{
			std::string PreprocessError;
			BuildPreprocessorLineMap(Source, Defines, PredefinedMacros, Program.EntryModulePath,
			    OwnedLineMap, PreprocessError);
		}
		const auto& LineMap = Snapshot ? *Snapshot : OwnedLineMap;
		FTextPosition AstPosition = PhysicalPosition;
		std::uint32_t AstLine = 0;
		std::uint32_t AstColumn = 0;
		if (MapCaretPhysicalToPreprocessed(
		        LineMap,
		        PhysicalPosition.Line,
		        PhysicalPosition.Column,
		        AstLine,
		        AstColumn))
		{
			AstPosition.Line = AstLine;
			AstPosition.Column = AstColumn;
		}
		CollectVisibleLocals(Program, AstPosition, OutHits);
	}

	std::string VeroScriptSymbolQuery::ResolveReceiverTypeInProgram(
	    const FProgram& Program,
	    const FDocumentSymbolResult& SymbolsResult,
	    const FTextPosition Position,
	    const std::string_view ReceiverName)
	{
		return ResolveReceiverTypeName(
		    Program,
		    SymbolsResult,
		    Position,
		    std::string(ReceiverName));
	}

	std::string VeroScriptSymbolQuery::ResolveReceiverTypeAtPhysical(
	    const FProgram& Program,
	    const FDocumentSymbolResult& SymbolsResult,
	    const std::string_view Source,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
	    const FTextPosition PhysicalPosition,
	    const std::string_view ReceiverName)
	{
		VeroScript::FPreprocessLineMap OwnedLineMap;
		const auto Snapshot = ProgramPreprocessorLineMap(Program, Program.EntryModulePath);
		if (!Snapshot)
		{
			std::string PreprocessError;
			BuildPreprocessorLineMap(Source, Defines, PredefinedMacros, Program.EntryModulePath,
			    OwnedLineMap, PreprocessError);
		}
		const auto& LineMap = Snapshot ? *Snapshot : OwnedLineMap;
		FTextPosition AstPosition = PhysicalPosition;
		std::uint32_t AstLine = 0;
		std::uint32_t AstColumn = 0;
		if (MapCaretPhysicalToPreprocessed(
		        LineMap,
		        PhysicalPosition.Line,
		        PhysicalPosition.Column,
		        AstLine,
		        AstColumn))
		{
			AstPosition.Line = AstLine;
			AstPosition.Column = AstColumn;
		}

		if (ReceiverName == "this")
		{
			return ResolveReceiverTypeName(
			    Program,
			    SymbolsResult,
			    AstPosition,
			    std::string(ReceiverName));
		}

		std::vector<FSymbolQueryHit> Locals;
		CollectVisibleLocals(Program, AstPosition, Locals);
		return ResolveReceiverTypeName(
		    Program,
		    SymbolsResult,
		    AstPosition,
		    std::string(ReceiverName),
		    &Locals);
	}

	std::string VeroScriptSymbolQuery::ResolveEnclosingNamespaceName(
	    const FProgram& Program, const FTextPosition Position, const std::string_view LexicalNamespace)
	{
		std::vector<FCallableScope> Callables;
		const auto* Scope = FindEnclosingCallable(Program, Position, Callables);
		return Scope && Scope->Body && !Scope->Body->DefinitionModulePath.empty()
		    ? Scope->Body->DefinitionNamespace : std::string(LexicalNamespace);
	}

	std::string VeroScriptSymbolQuery::ResolveEnclosingTypeName(
	    const FProgram& Program,
	    const FTextPosition Position)
	{
		std::vector<FCallableScope> Callables;
		const FCallableScope* Scope = FindEnclosingCallable(Program, Position, Callables);
		std::string Owner = Scope ? Scope->ContainerName : std::string{};
		FTextPosition Start = Scope ? FTextPosition{Scope->StartLine, Scope->StartColumn} : FTextPosition{0, 0};
		// Field initializers and type-body declarations have type access even
		// outside a callable. A local type can also be nested inside a function.
		const auto Visit = [&](const auto& Types)
		{
			for (const auto& Type : Types)
			{
				const auto& Range = Type.DeclMeta.SourceRange;
				const auto& Begin = Range.Position.Begin;
				const auto& End = Range.Position.End;
				if (Type.ModulePath != Program.EntryModulePath || Range.End <= Range.Begin || Begin.FileLabel != Program.EntryModulePath || End.FileLabel != Begin.FileLabel || Begin.Line > Position.Line || (Begin.Line == Position.Line && Begin.Column > Position.Column) || End.Line < Position.Line || (End.Line == Position.Line && End.Column <= Position.Column) || Begin.Line < Start.Line || (Begin.Line == Start.Line && Begin.Column < Start.Column))
				{
					continue;
				}
				Owner = Type.Name;
				Start = {Begin.Line, Begin.Column};
			}
		};
		Visit(Program.Classes);
		Visit(Program.Structs);
		Visit(Program.Interfaces);
		return Owner;
	}

	bool VeroScriptSymbolQuery::ResolveEnclosingCallableAccessContext(
	    const FProgram& Program,
	    const FTextPosition Position,
	    FCallableAccessContext& OutContext)
	{
		OutContext = {};
		std::vector<FCallableScope> Callables;
		const FCallableScope* const Scope = FindEnclosingCallable(Program, Position, Callables);
		if (!Scope)
		{
			return false;
		}
		OutContext.Name = Scope->Name;
		OutContext.OwnerTypeName = Scope->ContainerName;
		OutContext.ReturnType = Scope->ReturnType;
		OutContext.bIsConstMethod = Scope->bIsConstMethod;
		OutContext.ModulePath = Scope->DeclarationModulePath;
		OutContext.bExported = Scope->bExported;
		if (Scope->Parameters)
		{
			OutContext.ParameterTypes.reserve(Scope->Parameters->size());
			for (const FFunctionParam& Param : *Scope->Parameters)
			{
				OutContext.ParameterTypes.push_back(CallableParameterTypeName(Param));
			}
		}
		return true;
	}

	std::string VeroScriptSymbolQuery::ResolveImplicitThisTypeName(
	    const FProgram& Program,
	    const FTextPosition Position)
	{
		return ResolveImplicitThisOwnerName(Program, Position);
	}

	FCompletionResult VeroScriptCompletion::CompleteAtSource(const std::string_view Source,
	    const FTextPosition Position, const std::string_view FileLabel,
	    const FCompletionOptions& RequestedOptions)
	{
		auto Options = RequestedOptions;
		std::string EnvironmentError;
		if (!ResolveToolingModuleEnvironment(Options, EnvironmentError))
		{
			return ToolingModuleEnvironmentFailure<FCompletionResult>(EnvironmentError);
		}

		const auto StandardBindings = StandardLibrary::MergeBindingCatalog(Options.Bindings, nullptr);
		Options.Bindings = StandardBindings.get();
		Locale::InitializeModuleFromEnvironment("VeroScriptTooling");
		FQueryLexicalSnapshot LexicalSnapshot(Source, FileLabel);
		return FCompletionAccess::CompleteAtSource(Source, Position, FileLabel, Options);
	}

	FCompletionResult VeroScriptCompletion::CompleteAtFile(const std::string_view FilePath,
	    const FTextPosition Position, const FCompletionOptions& RequestedOptions)
	{
		auto Options = RequestedOptions;
		std::string EnvironmentError;
		if (!ResolveToolingModuleEnvironment(Options, EnvironmentError))
		{
			return ToolingModuleEnvironmentFailure<FCompletionResult>(EnvironmentError);
		}

		const auto StandardBindings = StandardLibrary::MergeBindingCatalog(Options.Bindings, nullptr);
		Options.Bindings = StandardBindings.get();
		Locale::InitializeModuleFromEnvironment("VeroScriptTooling");
		return FCompletionAccess::CompleteAtFile(FilePath, Position, Options);
	}
}
