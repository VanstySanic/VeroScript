// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptComplete.h"

#include "Internal/VeroScriptCompletion.h"
#include "Internal/VeroScriptStandardNamespaceMetadata.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptSymbolKindMap.h"
#include "VeroScriptOverlayUtil.h"

namespace VeroScript::Tooling
{
	namespace
	{
		FCompletionResult FromEngine(
			const Detail::FCompletionResult& In,
			const std::string_view FallbackFileLabel)
		{
			FCompletionResult Result;
			Result.bSuccess = In.bSuccess;
			if (!In.bSuccess)
			{
				AppendToolingDiagnostics(
					Result.Diagnostics,
					In.Error,
					In.Diagnostics,
					FallbackFileLabel,
					"completion failed");
				return Result;
			}

			Result.Prefix = In.Prefix;
			Result.bHasReplaceRange = In.bHasReplaceRange;
			Result.ReplaceRange.StartLine = In.ReplaceLine;
			Result.ReplaceRange.StartColumn = In.ReplaceStartColumn;
			Result.ReplaceRange.EndLine = In.ReplaceLine;
			Result.ReplaceRange.EndColumn = In.ReplaceEndColumn;

			Result.Items.reserve(In.Items.size());
			for (const Detail::FCompletionItem& Item : In.Items)
			{
				FCompletionItem Mapped;
				const bool bImport = Item.Kind == Detail::EDocumentSymbolKind::Import;
				Mapped.Kind = MapDocumentSymbolKind(Item.Kind);
				Mapped.Label = bImport ? Item.Label : Detail::FormatStandardSourceSpelling(Item.Label);
				Mapped.LabelDescription = Detail::FormatStandardSourceSpelling(Item.LabelDescription);
				Mapped.Detail = Detail::FormatStandardSourceSpelling(Item.Detail);
				Mapped.Documentation = Item.Documentation;
				const auto& InsertText = Item.InsertText.empty() ? Item.Label : Item.InsertText;
				Mapped.InsertText = bImport ? InsertText : Detail::FormatStandardSourceSpelling(InsertText);
				Mapped.FilterText = Item.FilterText;
				Mapped.bInsertTextSnippet = Item.bInsertTextSnippet;
				Mapped.bSoftBuiltin = Item.bSoftBuiltin;
				Mapped.bOverrideMember = Item.bOverrideMember;
				Mapped.bImplementsInterfaceMember = Item.bImplementsInterfaceMember;
				Mapped.SortPriority = Item.SortPriority;
				Mapped.MatchQuality = Item.MatchQuality;
				Result.Items.push_back(std::move(Mapped));
			}

			return Result;
		}

		Detail::FCompletionOptions ToEngineOptions(const FCompletionOptions& Options)
		{
			Detail::FCompletionOptions Out;
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

	FCompletionResult VeroScriptComplete::CompleteAtSource(
		const std::string_view Source,
		const FTextPosition Position,
		const std::string_view FileLabel,
		const FCompletionOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FCompletionResult>(Failure);
			}

			return CompleteAtSource(Source, Position, FileLabel, Effective);
		}

		return FromEngine(
			Detail::VeroScriptCompletion::CompleteAtSource(
				Source,
				ToEnginePosition(Position),
				FileLabel,
				ToEngineOptions(Options)),
			FileLabel);
	}

	FCompletionResult VeroScriptComplete::CompleteAtFile(
		const std::string_view FilePath,
		const FTextPosition Position,
		const FCompletionOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FCompletionResult>(Failure);
			}

			return CompleteAtFile(FilePath, Position, Effective);
		}

		return FromEngine(
			Detail::VeroScriptCompletion::CompleteAtFile(
				FilePath,
				ToEnginePosition(Position),
				ToEngineOptions(Options)),
			FilePath);
	}
}
