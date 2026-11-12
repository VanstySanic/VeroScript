// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFindReferences.h"

#include "Internal/VeroScriptReferences.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptOverlayUtil.h"

namespace VeroScript::Tooling
{
	namespace
	{
		FReferencesResult FromEngine(
			const Detail::FReferencesResult& In,
			const std::string_view FallbackFileLabel)
		{
			FReferencesResult Result;
			Result.bSuccess = In.bSuccess;
			if (!In.bSuccess)
			{
				AppendToolingDiagnostics(
					Result.Diagnostics,
					In.Error,
					In.Diagnostics,
					FallbackFileLabel,
					"find references failed");
				return Result;
			}

			Result.bHasToken = In.bHasToken;
			Result.TokenText = In.TokenText;
			Result.References.reserve(In.References.size());
			for (const Detail::FReferenceHit& Hit : In.References)
			{
				FReferenceHit Mapped;
				Mapped.FileLabel = Hit.FileLabel.empty() ? std::string(FallbackFileLabel) : Hit.FileLabel;
				Mapped.Range.StartLine = Hit.Line;
				Mapped.Range.StartColumn = Hit.StartColumn;
				Mapped.Range.EndLine = Hit.Line;
				Mapped.Range.EndColumn = Hit.EndColumn;
				Mapped.bIsDefinition = Hit.bIsDefinition;
				Mapped.bIsWrite = Hit.bIsWrite;
				Result.References.push_back(std::move(Mapped));
			}
			return Result;
		}

		Detail::FReferencesOptions ToEngineOptions(const FReferencesOptions& Options)
		{
			Detail::FReferencesOptions Out;
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

	FReferencesResult VeroScriptFindReferences::FindAtSource(
		const std::string_view Source,
		const FTextPosition Position,
		const std::string_view FileLabel,
		const FReferencesOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FReferencesResult>(Failure);
			}

			return FindAtSource(Source, Position, FileLabel, Effective);
		}

		return FromEngine(
			Detail::VeroScriptReferences::FindAtSource(
				Source,
				ToEnginePosition(Position),
				FileLabel,
				ToEngineOptions(Options)),
			FileLabel);
	}

	FReferencesResult VeroScriptFindReferences::FindAtFile(
		const std::string_view FilePath,
		const FTextPosition Position,
		const FReferencesOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FReferencesResult>(Failure);
			}

			return FindAtFile(FilePath, Position, Effective);
		}

		return FromEngine(
			Detail::VeroScriptReferences::FindAtFile(
				FilePath,
				ToEnginePosition(Position),
				ToEngineOptions(Options)),
			FilePath);
	}
}
