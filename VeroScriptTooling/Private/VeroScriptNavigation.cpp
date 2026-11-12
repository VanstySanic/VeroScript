// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNavigation.h"

#include "Internal/VeroScriptOperatorLex.h"
#include "Internal/VeroScriptStandardNamespaceMetadata.h"
#include "Internal/VeroScriptSymbolQuery.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptPath.h"
#include "VeroScriptSymbolKindMap.h"
#include "VeroScriptOverlayUtil.h"

#include <filesystem>
#include <unordered_set>
#include <vector>

namespace VeroScript::Tooling
{
	namespace
	{
		std::string FindStandardNamespaceModule(
			const Detail::FStandardNamespaceMetadata& Metadata,
			const FNavigationOptions& Options)
		{
			if (Metadata.Module == "builtin")
			{
				return {};
			}

			const std::filesystem::path ModulePath = VeroScript::PathUtil::FromUtf8(Metadata.Module);
			const auto FindAtRoot = [&ModulePath](const std::string_view SearchPath) -> std::string
			{
				if (SearchPath.empty())
				{
					return {};
				}
				const std::filesystem::path Root = VeroScript::PathUtil::FromUtf8(SearchPath);
				const std::filesystem::path Direct = Root / ModulePath;
				std::error_code Error;
				if (std::filesystem::is_regular_file(Direct, Error))
				{
					return VeroScript::PathUtil::ToUtf8(Direct.lexically_normal());
				}

				// A workspace may configure `.../stdlib` itself as the search
				// path, while package metadata is rooted at `stdlib/...`.
				if (ModulePath.has_parent_path()
					&& ModulePath.parent_path() == "stdlib")
				{
					const std::filesystem::path NestedRoot = Root / ModulePath.filename();
					Error.clear();
					if (std::filesystem::is_regular_file(NestedRoot, Error))
					{
						return VeroScript::PathUtil::ToUtf8(NestedRoot.lexically_normal());
					}
				}
				return {};
			};
			for (const std::string_view ImportPath : Options.ImportPaths)
			{
				if (const std::string Found = FindAtRoot(ImportPath); !Found.empty())
				{
					return Found;
				}
			}
			// ModuleLoader checks import paths before explicit stdlib roots.
			// AddStdlibSearchRoot normalizes and ignores already-added roots;
			// later distinct roots are prepended. A repeated root must not
			// change priority (A, B, A still searches B before A).
			std::vector<std::string> StdlibRoots;
			StdlibRoots.reserve(Options.StdlibSearchPaths.size());
			std::unordered_set<std::string> AddedRoots;
			for (const std::string_view Root : Options.StdlibSearchPaths)
			{
				std::string Normalized = VeroScript::PathUtil::Normalize(std::string(Root));
				if (!Normalized.empty() && AddedRoots.insert(Normalized).second)
				{
					StdlibRoots.push_back(std::move(Normalized));
				}
			}
			for (auto Root = StdlibRoots.rbegin(); Root != StdlibRoots.rend(); ++Root)
			{
				if (const std::string Found = FindAtRoot(*Root); !Found.empty())
				{
					return Found;
				}
			}
			return {};
		}

		FNavigationResult FromQueryResult(
			const Detail::FSymbolQueryResult& In,
			const std::string_view FallbackFileLabel,
			const FNavigationOptions& Options)
		{
			FNavigationResult Result;
			Result.bSuccess = In.bSuccess;
			if (!In.bSuccess)
			{
				AppendToolingDiagnostics(
					Result.Diagnostics,
					In.Error,
					In.Diagnostics,
					FallbackFileLabel,
					"navigation query failed");
				return Result;
			}

			Result.bHasToken = In.bHasToken;
			Result.TokenText = In.TokenText;
			Result.TokenRange.StartLine = In.TokenLine;
			Result.TokenRange.StartColumn = In.TokenColumn;
			Result.TokenRange.EndLine = In.TokenLine;
			Result.TokenRange.EndColumn = In.TokenEndColumn;

			Result.Definitions.reserve(In.Definitions.size());
			for (const Detail::FSymbolQueryHit& Hit : In.Definitions)
			{
				// Standard-library namespaces are soft symbols for resolution, but
				// their metadata can still point at a configured stdlib module.
				// Builtin-only namespaces intentionally remain hover-only: no fake
				// file URI is emitted when the host has no physical declaration.
				if (Hit.Kind == Detail::EDocumentSymbolKind::Namespace && Hit.bSoftBuiltin)
				{
					const Detail::FStandardNamespaceMetadata* const Metadata =
						Detail::FindStandardNamespaceMetadata(Hit.Name);
					if (Metadata == nullptr)
					{
						continue;
					}
					const std::string ModulePath = FindStandardNamespaceModule(*Metadata, Options);
					if (ModulePath.empty())
					{
						continue;
					}
					FNavigationHit Mapped;
					Mapped.Kind = MapDocumentSymbolKind(Hit.Kind);
					Mapped.Name = Detail::FormatNamespaceQualifiedName(Hit.Name);
					Mapped.Detail = Detail::FormatStandardNamespaceDetail(*Metadata);
					Mapped.ContainerName = Detail::FormatNamespaceQualifiedName(Metadata->ParentName);
					Mapped.FileLabel = ModulePath;
					Mapped.DefinitionRange.StartLine = 1;
					Mapped.DefinitionRange.StartColumn = 1;
					Mapped.DefinitionRange.EndLine = 1;
					Mapped.DefinitionRange.EndColumn = 1;
					Mapped.bExported = true;
					Result.Definitions.push_back(std::move(Mapped));
					continue;
				}

				// Soft / host builtins have no source location — omit from Go-to.
				if (Hit.bSoftBuiltin || Hit.DefLine == 0)
				{
					continue;
				}
				FNavigationHit Mapped;
				Mapped.Kind = MapDocumentSymbolKind(Hit.Kind);
				Mapped.Name = Detail::FormatOperatorDisplayName(Hit.Name);
				Mapped.Detail = Hit.Detail;
				Mapped.ContainerName = Hit.ContainerName;
				Mapped.FileLabel = Hit.FileLabel;
				Mapped.DefinitionRange.StartLine = Hit.DefLine;
				Mapped.DefinitionRange.StartColumn = Hit.DefColumn;
				Mapped.DefinitionRange.EndLine = Hit.DefLine;
				Mapped.DefinitionRange.EndColumn = Hit.DefColumn;
				Mapped.bExported = Hit.bExported;
				Mapped.SourceImplementation = Hit.SourceImplementation;
				Mapped.GeneratedOrigin = Hit.GeneratedOrigin;
				Result.Definitions.push_back(std::move(Mapped));
			}
			return Result;
		}

		Detail::FSymbolQueryOptions ToQueryOptions(const FNavigationOptions& Options)
		{
			Detail::FSymbolQueryOptions Out;
			Out.ImportPaths = Options.ImportPaths;
			Out.StdlibSearchPaths = Options.StdlibSearchPaths;
			Out.ImplicitImportPaths = Options.ImplicitImportPaths;
			Out.bAllowCircularImports = Options.bAllowCircularImports;
			Out.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			Out.Defines = Options.Defines;
			Out.PredefinedMacros = Options.PredefinedMacros;
			Out.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
			Out.Bindings = Options.Bindings;
			Out.AnnotationProcessing = Options.AnnotationProcessing;
			Out.CompileProcessing = Options.CompileProcessing;
			Out.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			Out.ModuleEnvironment = Options.ModuleEnvironment;
			return Out;
		}

		Detail::FTextPosition ToEnginePosition(const FTextPosition Position)
		{
			return Detail::FTextPosition{Position.Line, Position.Column};
		}
	}

	FNavigationResult VeroScriptNavigation::QueryAtSource(
		const std::string_view Source,
		const FTextPosition Position,
		const std::string_view FileLabel,
		const FNavigationOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FNavigationResult>(Failure);
			}

			return QueryAtSource(Source, Position, FileLabel, Effective);
		}

		return FromQueryResult(
			Detail::VeroScriptSymbolQuery::QueryAtSource(
				Source,
				ToEnginePosition(Position),
				FileLabel,
				ToQueryOptions(Options)),
			FileLabel,
			Options);
	}

	FNavigationResult VeroScriptNavigation::QueryAtFile(
		const std::string_view FilePath,
		const FTextPosition Position,
		const FNavigationOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FNavigationResult>(Failure);
			}

			return QueryAtFile(FilePath, Position, Effective);
		}

		return FromQueryResult(
			Detail::VeroScriptSymbolQuery::QueryAtFile(
				FilePath,
				ToEnginePosition(Position),
				ToQueryOptions(Options)),
			FilePath,
			Options);
	}
}
