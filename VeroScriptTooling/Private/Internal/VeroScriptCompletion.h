// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"
#include "VeroScriptAnnotationProcessing.h"

#include "VeroScriptDiagnostics.h"
#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptSymbolQuery.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	struct FCompletionItem
	{
		EDocumentSymbolKind Kind = EDocumentSymbolKind::Function;
		std::string Label;
		std::string Detail;
		std::string InsertText;
		bool bInsertTextSnippet = false;
		bool bSoftBuiltin = false;
		bool bOverrideMember = false;
		bool bImplementsInterfaceMember = false;
		// Lexical scope distance for local values; 255 for non-locals.
		std::uint8_t ScopeDistance = 255;
		/**
		 * Primary completion match tier: exact, case-insensitive exact, prefix,
		 * word-boundary/camel, contains, subsequence, or no query. Lower is better.
		 */
		std::uint8_t MatchQuality = 6;
		/**
		 * Secondary contextual rank. Lower sorts earlier after MatchQuality.
		 * Assigned by FinalizeCompletionItems from kind + completion context.
		 */
		std::uint16_t SortPriority = 500;
		// Qualified namespace shown separately from the leaf label in LSP.
		std::string LabelDescription;
		std::string Documentation;
		// Optional client matching text; empty uses Label.
		std::string FilterText;
	};

	struct FCompletionOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FSourceOverlay> SourceOverlays;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FCompletionResult
	{
		bool bSuccess = false;
		FScriptError Error;
		std::vector<FScriptError> Diagnostics;
		std::string Prefix;
		std::uint32_t ReplaceLine = 1;
		std::uint32_t ReplaceStartColumn = 1;
		std::uint32_t ReplaceEndColumn = 1;
		bool bHasReplaceRange = false;
		std::vector<FCompletionItem> Items;
	};

	/**
	 * Identifier completion at a caret position (prefix filter over locals + document symbols).
	 * After '.', completes members of the receiver type (locals' type Detail or named type).
	 * If the buffer does not parse, insertion of ';' after the identifier under the caret
	 * or '_;' after a trailing '.' is tried once for mid-edit buffers.
	 */
	class VeroScriptCompletion
	{
	public:
		static FCompletionResult CompleteAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FCompletionOptions& Options = {});

		static FCompletionResult CompleteAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FCompletionOptions& Options = {});
	};
}
