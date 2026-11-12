// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptWorkspaceSymbols.h"

#include "Internal/VeroScriptOperatorLex.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptOutline.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPath.h"
#include "VeroScriptWorkspace.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript::Tooling
{
	namespace
	{
		bool IsSoftLikeSymbol(const FOutlineSymbol& Symbol)
		{
			return Symbol.FileLabel.empty() && Symbol.Range.StartLine == 0;
		}

		void FlattenOutlineSymbol(
			const FOutlineSymbol& Symbol,
			const std::string& ContainerName,
			std::vector<FWorkspaceSymbol>& OutSymbols,
			const bool bSkipSoftDuplicates)
		{
			VeroScript::CheckAnalysisCancellation();
			const bool bSoftLike = IsSoftLikeSymbol(Symbol);
			if (bSkipSoftDuplicates && bSoftLike)
			{
				for (const FWorkspaceSymbol& Existing : OutSymbols)
				{
					if (Existing.FileLabel.empty()
						&& Existing.Range.StartLine == 0
						&& Existing.Name == Detail::FormatOperatorDisplayName(Symbol.Name)
						&& Existing.ContainerName == ContainerName)
					{
						return;
					}
				}
			}

			FWorkspaceSymbol Flat;
			Flat.Kind = Symbol.Kind;
			Flat.Name = Detail::FormatOperatorDisplayName(Symbol.Name);
			Flat.Detail = Symbol.Detail;
			Flat.ContainerName = ContainerName;
			Flat.FileLabel = Symbol.FileLabel;
			Flat.Range = Symbol.Range;
			Flat.bExported = Symbol.bExported;
			Flat.SourceImplementation = Symbol.SourceImplementation;
			Flat.GeneratedOrigin = Symbol.GeneratedOrigin;
			OutSymbols.push_back(std::move(Flat));

			for (const FOutlineSymbol& Child : Symbol.Children)
			{
				FlattenOutlineSymbol(Child, Symbol.Name, OutSymbols, bSkipSoftDuplicates);
			}
		}

		FOutlineOptions ToOutlineOptions(
			const FWorkspaceSymbolsOptions& Options,
			const std::vector<FDocumentOverlay>& Documents)
		{
			FOutlineOptions Out;
			Out.ImportPaths = Options.ImportPaths;
			Out.StdlibSearchPaths = Options.StdlibSearchPaths;
			Out.ImplicitImportPaths = Options.ImplicitImportPaths;
			Out.bAllowCircularImports = Options.bAllowCircularImports;
			Out.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			Out.Defines = Options.Defines;
			Out.PredefinedMacros = Options.PredefinedMacros;
			Out.SourceOverlays = Documents.empty() ? Options.SourceOverlays : Documents;
			Out.bIncludeImports = Options.bIncludeImports;
			Out.bIncludeImportedExports = Options.bIncludeImportedExports;
			Out.bIncludeSoftBuiltins = Options.bIncludeSoftBuiltins;
			Out.bIncludeHostBindings = Options.bIncludeHostBindings;
			Out.Bindings = Options.Bindings;
			Out.AnnotationProcessing = Options.AnnotationProcessing;
			Out.CompileProcessing = Options.CompileProcessing;
			Out.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			Out.ModuleEnvironment = Options.ModuleEnvironment;
			return Out;
		}

		std::string NormalizeDocumentPath(std::string Path)
		{
			return VeroScript::PathUtil::Normalize(std::move(Path));
		}

		bool IsCancelled(const FWorkspaceSymbolsOptions& Options)
		{
			return Options.ShouldCancel && Options.ShouldCancel();
		}

		std::string WorkspaceSymbolIdentityKey(
			const FWorkspaceSymbol& Symbol,
			std::unordered_map<std::string, std::string>& PathKeys)
		{
			std::string FileKey;
			if (!Symbol.FileLabel.empty())
			{
				const auto [It, bInserted] = PathKeys.emplace(Symbol.FileLabel, std::string{});
				if (bInserted)
				{
					It->second = VeroScript::PathUtil::IdentityKey(Symbol.FileLabel);
				}
				FileKey = It->second;
			}
			return std::to_string(static_cast<unsigned>(Symbol.Kind)) + '|'
				+ FileKey
				+ '|' + std::to_string(Symbol.Range.StartLine) + ':'
				+ std::to_string(Symbol.Range.StartColumn) + '|'
				+ std::to_string(Symbol.Range.EndLine) + ':'
				+ std::to_string(Symbol.Range.EndColumn) + '|'
				+ Symbol.ContainerName + '|' + Symbol.Name;
		}

		bool RemoveDuplicateSymbols(
			std::vector<FWorkspaceSymbol>& Symbols,
			const FWorkspaceSymbolsOptions& Options)
		{
			std::unordered_map<std::string, std::size_t> Seen;
			std::unordered_map<std::string, std::string> PathKeys;
			std::size_t WriteIndex = 0;
			for (std::size_t ReadIndex = 0; ReadIndex < Symbols.size(); ++ReadIndex)
			{
				if ((ReadIndex & 0x7fu) == 0u && IsCancelled(Options))
				{
					return false;
				}
				const auto [Existing, bInserted] = Seen.emplace(
					WorkspaceSymbolIdentityKey(Symbols[ReadIndex], PathKeys), WriteIndex);
				if (!bInserted)
				{
					// A declaration-only view can be indexed before the module that
					// supplies its body. Keep that body's owning source position.
					if (Symbols[Existing->second].SourceImplementation.FileLabel.empty())
					{
						Symbols[Existing->second].SourceImplementation =
							std::move(Symbols[ReadIndex].SourceImplementation);
					}

					continue;
				}
				if (WriteIndex != ReadIndex)
				{
					Symbols[WriteIndex] = std::move(Symbols[ReadIndex]);
				}
				++WriteIndex;
			}
			Symbols.resize(WriteIndex);
			return true;
		}

		bool IsIgnoredWorkspaceDirectory(const std::filesystem::path& Path)
		{
			const std::string Name = VeroScript::PathUtil::ToUtf8(Path.filename());
			return Name == ".git" || Name == ".verocache" || Name == "build"
				|| Name == "dist" || Name == "Binaries" || Name == "Intermediate";
		}

		void AddOrReplaceDocument(
			std::vector<FDocumentOverlay>& Documents,
			std::unordered_map<std::string, std::size_t>& DocumentIndices,
			FDocumentOverlay Document)
		{
			Document.Path = NormalizeDocumentPath(std::move(Document.Path));
			const std::string IdentityKey = VeroScript::PathUtil::IdentityKey(Document.Path);
			if (const auto Existing = DocumentIndices.find(IdentityKey);
				Existing != DocumentIndices.end())
			{
				Documents[Existing->second] = std::move(Document);
				return;
			}
			DocumentIndices.emplace(IdentityKey, Documents.size());
			Documents.push_back(std::move(Document));
		}

		bool AppendWorkspaceRootDocuments(
			const FWorkspaceSymbolsOptions& Options,
			std::vector<FDocumentOverlay>& Documents,
			std::unordered_map<std::string, std::size_t>& DocumentIndices)
		{
			if (Options.WorkspaceRoots.empty() || Documents.size() >= Options.MaxWorkspaceFiles)
			{
				return !IsCancelled(Options);
			}

			for (const std::string_view RootView : Options.WorkspaceRoots)
			{
				if (IsCancelled(Options))
				{
					return false;
				}
				if (Documents.size() >= Options.MaxWorkspaceFiles)
				{
					break;
				}
				if (RootView.empty())
				{
					continue;
				}
				std::error_code Error;
				const std::filesystem::path Root(
					VeroScript::PathUtil::FromUtf8(std::string(RootView)));
				if (!std::filesystem::is_directory(Root, Error) || Error)
				{
					continue;
				}
				std::filesystem::recursive_directory_iterator Iterator(
					Root,
					std::filesystem::directory_options::skip_permission_denied,
					Error);
				const std::filesystem::recursive_directory_iterator End;
				while (!Error && Iterator != End && Documents.size() < Options.MaxWorkspaceFiles)
				{
					if (IsCancelled(Options))
					{
						return false;
					}
					const std::filesystem::directory_entry Entry = *Iterator;
					if (Entry.is_directory(Error) && !Error && IsIgnoredWorkspaceDirectory(Entry.path()))
					{
						Iterator.disable_recursion_pending();
					}
					else if (Entry.is_regular_file(Error) && !Error
						&& VeroScript::PathUtil::ToUtf8(Entry.path().extension()) == ".vs")
					{
						const std::string Path = NormalizeDocumentPath(
							VeroScript::PathUtil::ToUtf8(Entry.path()));
						if (!DocumentIndices.count(VeroScript::PathUtil::IdentityKey(Path)))
						{
							const std::uintmax_t Size = Entry.file_size(Error);
							if (!Error && Size <= Options.MaxWorkspaceFileBytes
								&& Size <= static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
							{
								std::ifstream Input(Entry.path(), std::ios::binary);
								if (Input)
								{
									// Bound the actual read too. A file can grow after
									// file_size(); iterator construction reads to EOF
									// and would bypass the configured allocation limit.
									std::string Source(static_cast<std::size_t>(Size), '\0');
									Input.read(Source.data(), static_cast<std::streamsize>(Size));
									if (Input.gcount() == static_cast<std::streamsize>(Size)
										&& Input.peek() == std::char_traits<char>::eof())
									{
										AddOrReplaceDocument(Documents, DocumentIndices,
											FDocumentOverlay{Path, std::move(Source)});
									}
								}
							}
						}
					}
					Error.clear();
					Iterator.increment(Error);
				}
			}
			return !IsCancelled(Options);
		}

		void AppendGlobalCatalogSymbols(
			const FWorkspaceSymbolsOptions& Options,
			std::vector<FWorkspaceSymbol>& OutSymbols)
		{
			if (Options.ModuleEnvironment) return;
			const bool bWantSoft = Options.bIncludeSoftBuiltins;
			const bool bWantHost = Options.bIncludeHostBindings && Options.Bindings;
			if (!bWantSoft && !bWantHost)
			{
				return;
			}

			FOutlineOptions GlobalOptions = ToOutlineOptions(Options, {});
			GlobalOptions.bIncludeSoftBuiltins = bWantSoft;
			GlobalOptions.bIncludeHostBindings = bWantHost;
			GlobalOptions.bIncludeImports = false;
			const FOutlineResult Globals = VeroScriptOutline::CollectFromSource(
				"void __vs_workspace_symbol_anchor() {}\n",
				"<workspace>",
				GlobalOptions);
			if (!Globals.bSuccess)
			{
				return;
			}
			for (const FOutlineSymbol& Symbol : Globals.Symbols)
			{
				if (!IsSoftLikeSymbol(Symbol))
				{
					continue;
				}
				FlattenOutlineSymbol(Symbol, {}, OutSymbols, true);
			}
		}
	}

	FWorkspaceSymbolsResult VeroScriptWorkspaceSymbols::CollectFromDocuments(
		const std::vector<FDocumentOverlay>& Documents,
		const FWorkspaceSymbolsOptions& Options)
	try
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FWorkspaceSymbolsResult>(Failure);
			}

			return CollectFromDocuments(Documents, Effective);
		}

		const auto* const Previous = VeroScript::ActiveAnalysisControl;
		VeroScript::FAnalysisControlScope Control([&Options, Previous]()
		{
			return IsCancelled(Options) || (Previous && Previous->ShouldCancel && Previous->ShouldCancel());
		}, Previous ? Previous->RecordStage : std::function<void(std::string_view, double)>{});
		VeroScript::CheckAnalysisCancellation();
		FWorkspaceSymbolsResult Result;
		Result.bSuccess = true;
		Result.bCacheable = !Options.AnnotationProcessing.Registry &&
			IsDefaultCompileProcessing(Options.CompileProcessing) &&
			IsDefaultScriptCompileExtensionProcessing(Options.ScriptCompileExtensions) &&
			std::none_of(Documents.begin(), Documents.end(),
				[](const auto& Document)
				{
					return HasScriptCompileExtensionImport(Document.Source);
				});
		FOutlineOptions OutlineOptions = ToOutlineOptions(Options, Documents);
		// Soft / host catalog symbols are emitted once after documents so a failed
		// first document cannot suppress them.
		OutlineOptions.bIncludeSoftBuiltins = Options.ModuleEnvironment && Options.bIncludeSoftBuiltins;
		OutlineOptions.bIncludeHostBindings = Options.ModuleEnvironment && Options.bIncludeHostBindings;

		for (const FDocumentOverlay& Document : Documents)
		{
			if (IsCancelled(Options))
			{
				Result.bCancelled = true;
				return Result;
			}
			const FOutlineResult Outline = VeroScriptOutline::CollectFromSource(
				Document.Source,
				Document.Path,
				OutlineOptions);
			if (!Outline.bSuccess)
			{
				Result.bSuccess = false;
				Result.Diagnostics.insert(
					Result.Diagnostics.end(),
					Outline.Diagnostics.begin(),
					Outline.Diagnostics.end());
				continue;
			}

			for (const FOutlineSymbol& Symbol : Outline.Symbols)
			{
				FlattenOutlineSymbol(Symbol, {}, Result.Symbols, true);
			}
		}

		if (!RemoveDuplicateSymbols(Result.Symbols, Options))
		{
			Result.bCancelled = true;
			Result.Symbols.clear();
			return Result;
		}
		AppendGlobalCatalogSymbols(Options, Result.Symbols);
		return Result;
	}
	catch (const VeroScript::FAnalysisCancelled&)
	{
		FWorkspaceSymbolsResult Result;
		Result.bCancelled = true;
		return Result;
	}

	FWorkspaceSymbolsResult VeroScriptWorkspaceSymbols::CollectFromWorkspace(
		const VeroScriptWorkspace& Workspace,
		const FWorkspaceSymbolsOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FWorkspaceSymbolsResult>(Failure);
			}

			return CollectFromWorkspace(Workspace, Effective);
		}

		std::vector<FDocumentOverlay> Documents = Workspace.SnapshotDocuments();
		if (IsCancelled(Options))
		{
			FWorkspaceSymbolsResult Result;
			Result.bCancelled = true;
			return Result;
		}
		std::unordered_map<std::string, std::size_t> DocumentIndices;
		DocumentIndices.reserve(Documents.size() + Options.SourceOverlays.size());
		for (std::size_t Index = 0; Index < Documents.size(); ++Index)
		{
			FDocumentOverlay& Document = Documents[Index];
			Document.Path = NormalizeDocumentPath(std::move(Document.Path));
			DocumentIndices.emplace(VeroScript::PathUtil::IdentityKey(Document.Path), Index);
		}
		// Explicit source overlays are an in-memory view of a closed document and
		// must win over both disk and an older open-document snapshot.
		for (const FDocumentOverlay& Overlay : Options.SourceOverlays)
		{
			if (Options.ModuleEnvironment &&
				!Options.ModuleEnvironment->CanAccessOwner(
					Options.ModuleEnvironment->GetDescription().EntryOwnerId,
					Options.ModuleEnvironment->GetSourceOwner(Overlay.Path)))
			{
				continue;
			}

			if (IsCancelled(Options))
			{
				FWorkspaceSymbolsResult Result;
				Result.bCancelled = true;
				return Result;
			}
			AddOrReplaceDocument(Documents, DocumentIndices, Overlay);
		}
		if (!AppendWorkspaceRootDocuments(Options, Documents, DocumentIndices))
		{
			FWorkspaceSymbolsResult Result;
			Result.bCancelled = true;
			return Result;
		}
		return CollectFromDocuments(Documents, Options);
	}
}
