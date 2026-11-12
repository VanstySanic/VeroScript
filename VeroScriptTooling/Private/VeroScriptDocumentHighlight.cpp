// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptDocumentHighlight.h"

#include "VeroScriptFindReferences.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptOverlayUtil.h"

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

		FReferencesOptions ToReferencesOptions(const FDocumentHighlightOptions& Options)
		{
			FReferencesOptions Out;
			Out.ImportPaths = Options.ImportPaths;
			Out.StdlibSearchPaths = Options.StdlibSearchPaths;
			Out.ImplicitImportPaths = Options.ImplicitImportPaths;
			Out.bAllowCircularImports = Options.bAllowCircularImports;
			Out.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			Out.Defines = Options.Defines;
			Out.PredefinedMacros = Options.PredefinedMacros;
			Out.SourceOverlays = Options.SourceOverlays;
			Out.Bindings = Options.Bindings;
			Out.AnnotationProcessing = Options.AnnotationProcessing;
			Out.CompileProcessing = Options.CompileProcessing;
			Out.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			Out.ModuleEnvironment = Options.ModuleEnvironment;
			return Out;
		}

		FDocumentHighlightResult FromReferences(
			const FReferencesResult& References,
			const std::string_view FileLabel)
		{
			FDocumentHighlightResult Result;
			Result.bSuccess = References.bSuccess;
			Result.Diagnostics = References.Diagnostics;
			Result.bHasToken = References.bHasToken;
			Result.TokenText = References.TokenText;
			if (!References.bSuccess)
			{
				return Result;
			}

			const std::string CurrentFile(FileLabel);
			Result.Highlights.reserve(References.References.size());
			for (const FReferenceHit& Hit : References.References)
			{
				if (!SameDocumentPath(Hit.FileLabel, CurrentFile))
				{
					continue;
				}
				FDocumentHighlight Highlight;
				Highlight.Range = Hit.Range;
				Highlight.Kind = Hit.bIsWrite
					? EDocumentHighlightKind::Write
					: (Hit.bIsDefinition
						? EDocumentHighlightKind::Text
						: EDocumentHighlightKind::Read);
				Result.Highlights.push_back(std::move(Highlight));
			}
			return Result;
		}
	}

	FDocumentHighlightResult VeroScriptDocumentHighlight::HighlightsAtSource(
		const std::string_view Source,
		const FTextPosition Position,
		const std::string_view FileLabel,
		const FDocumentHighlightOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FDocumentHighlightResult>(Failure);
			}

			return HighlightsAtSource(Source, Position, FileLabel, Effective);
		}

		return FromReferences(
			VeroScriptFindReferences::FindAtSource(
				Source,
				Position,
				FileLabel,
				ToReferencesOptions(Options)),
			FileLabel);
	}

	FDocumentHighlightResult VeroScriptDocumentHighlight::HighlightsAtFile(
		const std::string_view FilePath,
		const FTextPosition Position,
		const FDocumentHighlightOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FDocumentHighlightResult>(Failure);
			}

			return HighlightsAtFile(FilePath, Position, Effective);
		}

		return FromReferences(
			VeroScriptFindReferences::FindAtFile(
				FilePath,
				Position,
				ToReferencesOptions(Options)),
			FilePath);
	}
}
