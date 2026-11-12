// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptOutline.h"

#include "Internal/VeroScriptDocumentSymbols.h"
#include "Internal/VeroScriptOperatorLex.h"
#include "Internal/VeroScriptPreprocessorQuery.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptSymbolKindMap.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPath.h"

#include <fstream>
#include <sstream>

namespace VeroScript::Tooling
{
	namespace
	{
		FOutlineSymbol MapSymbol(const Detail::FDocumentSymbol& In)
		{
			FOutlineSymbol Out;
			Out.Kind = MapDocumentSymbolKind(In.Kind);
			Out.Name = Detail::FormatOperatorDisplayName(In.Name);
			Out.Detail = In.Detail;
			Out.FileLabel = In.FileLabel;
			Out.Range.StartLine = In.StartLine;
			Out.Range.StartColumn = In.StartColumn;
			Out.Range.EndLine = In.EndLine;
			Out.Range.EndColumn = In.EndColumn;
			Out.bExported = In.bExported;
			Out.SourceImplementation = In.SourceImplementation;
			Out.GeneratedOrigin = In.GeneratedOrigin;
			Out.DeclarationRange = In.DeclarationRange;
			Out.Children.reserve(In.Children.size());
			for (const Detail::FDocumentSymbol& Child : In.Children)
			{
				Out.Children.push_back(MapSymbol(Child));
			}
			return Out;
		}

		FOutlineResult FromDocumentResult(
			const Detail::FDocumentSymbolResult& In,
			const std::string_view FallbackFileLabel)
		{
			FOutlineResult Result;
			Result.bSuccess = In.bSuccess;
			if (!In.bSuccess)
			{
				AppendToolingDiagnostics(
					Result.Diagnostics,
					In.Error,
					In.Diagnostics,
					FallbackFileLabel,
					"outline failed");
				return Result;
			}
			Result.Symbols.reserve(In.Symbols.size());
			for (const Detail::FDocumentSymbol& Symbol : In.Symbols)
			{
				Result.Symbols.push_back(MapSymbol(Symbol));
			}
			return Result;
		}

		Detail::FDocumentSymbolOptions ToDocumentOptions(const FOutlineOptions& Options)
		{
			Detail::FDocumentSymbolOptions Out;
			Out.ImportPaths = Options.ImportPaths;
			Out.StdlibSearchPaths = Options.StdlibSearchPaths;
			Out.ImplicitImportPaths = Options.ImplicitImportPaths;
			Out.bAllowCircularImports = Options.bAllowCircularImports;
			Out.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			Out.Defines = Options.Defines;
			Out.PredefinedMacros = Options.PredefinedMacros;
			Out.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
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
	}

	FOutlineResult VeroScriptOutline::CollectFromSource(
		const std::string_view Source,
		const std::string_view FileLabel,
		const FOutlineOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FOutlineResult>(Failure);
			}

			return CollectFromSource(Source, FileLabel, Effective);
		}

		Detail::FDocumentSymbolResult DocumentResult =
			Detail::VeroScriptDocumentSymbols::CollectFromSource(
				Source,
				FileLabel,
				ToDocumentOptions(Options));
		if (DocumentResult.bSuccess)
		{
			Detail::RemapDocumentSymbolsToPhysical(
				Source, Options.Defines, Options.PredefinedMacros, FileLabel,
				DocumentResult.Symbols, &DocumentResult.PreprocessLineMap);
		}
		FOutlineResult Result = FromDocumentResult(DocumentResult, FileLabel);
		if (!Result.Diagnostics.empty())
		{
			Detail::RemapAllToolingDiagnostics(
				Result.Diagnostics,
				Source, FileLabel, Options);
		}
		return Result;
	}

	FOutlineResult VeroScriptOutline::CollectFromFile(
		const std::string_view FilePath,
		const FOutlineOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FOutlineResult>(Failure);
			}

			return CollectFromFile(FilePath, Effective);
		}

		Detail::FDocumentSymbolResult DocumentResult =
			Detail::VeroScriptDocumentSymbols::CollectFromFile(
				FilePath,
				ToDocumentOptions(Options));
		std::string Source;
		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		if (Input)
		{
			std::ostringstream Buffer;
			Buffer << Input.rdbuf();
			Source = Buffer.str();
		}
		if (DocumentResult.bSuccess && !Source.empty())
		{
			Detail::RemapDocumentSymbolsToPhysical(
				Source, Options.Defines, Options.PredefinedMacros, FilePath, DocumentResult.Symbols,
				&DocumentResult.PreprocessLineMap);
		}
		FOutlineResult Result = FromDocumentResult(DocumentResult, FilePath);
		if (!Result.Diagnostics.empty() && !Source.empty())
		{
			Detail::RemapAllToolingDiagnostics(
				Result.Diagnostics,
				Source, FilePath, Options);
		}
		return Result;
	}
}
