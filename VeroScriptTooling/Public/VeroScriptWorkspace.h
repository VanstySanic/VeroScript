// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling
{
	/**
	 * Open document store for multi-file analysis and queries.
	 * Overlays open buffers onto ModuleLoader so imports see editor content, not stale disk.
	 */
	class VeroScriptWorkspace
	{
	public:
		void SetOptions(const FWorkspaceOptions& Options);
		const FWorkspaceOptions& GetOptions() const
		{
			return Options;
		}

		void OpenDocument(std::string_view Path, std::string_view Source);
		void UpdateDocument(std::string_view Path, std::string_view Source);
		void CloseDocument(std::string_view Path);
		bool HasDocument(std::string_view Path) const;
		// Copy one open document. A missing path leaves OutDocument unchanged.
		bool TryGetDocument(std::string_view Path, FDocumentOverlay& OutDocument) const;
		std::vector<std::string> ListDocuments() const;
		// Snapshot of open buffers (path + source) for workspace-wide queries.
		std::vector<FDocumentOverlay> SnapshotDocuments() const;

		FAnalyzeResult AnalyzeDocument(std::string_view Path) const;
		FAnalyzeResult AnalyzeAll() const;
		// Called synchronously once per entry with borrowed data valid only during
		// the callback. The observer must not modify this workspace's documents.
		FAnalyzeResult AnalyzeAll(
			const std::function<void(std::string_view, const FAnalyzeResult&)>& OnDocument) const;

		FOutlineResult OutlineDocument(std::string_view Path) const;
		FSemanticTokensResult SemanticTokensDocument(std::string_view Path) const;
		FSemanticTokensResult SemanticTokensDocument(
			std::string_view Path, const FSemanticTokensOptions& QueryOptions) const;
		FNavigationResult NavigateDocument(std::string_view Path, FTextPosition Position) const;
		// Finds concrete derived/override declarations for a type or member at Position.
		// Unlike NavigateDocument this never aliases implementation to definition.
		FNavigationResult ImplementationDocument(std::string_view Path, FTextPosition Position) const;
		FNavigationResult ImplementationDocument(
			std::string_view Path,
			FTextPosition Position,
			const FWorkspaceSymbolsOptions& SymbolOptions) const;
		FHoverResult HoverDocument(std::string_view Path, FTextPosition Position) const;
		FCompletionResult CompleteAtDocument(std::string_view Path, FTextPosition Position) const;
		FSignatureHelpResult SignatureHelpDocument(std::string_view Path, FTextPosition Position) const;
		FReferencesResult FindReferencesDocument(std::string_view Path, FTextPosition Position) const;
		FDocumentHighlightResult HighlightDocument(std::string_view Path, FTextPosition Position) const;
		FRenameResult CanRenameDocument(std::string_view Path, FTextPosition Position) const;
		FRenameResult RenameDocument(
			std::string_view Path,
			FTextPosition Position,
			std::string_view NewName) const;
		FFormatResult FormatDocument(std::string_view Path) const;
		// A width of zero keeps the workspace default.
		FFormatResult FormatDocument(std::string_view Path, std::uint32_t IndentWidth) const;
		FFormatResult FormatDocument(
			std::string_view Path, std::uint32_t IndentWidth, bool bInsertSpaces) const;
		FRangeFormatResult FormatRangeDocument(
			std::string_view Path,
			FTextRange Range) const;
		FRangeFormatResult FormatRangeDocument(
			std::string_view Path,
			FTextRange Range,
			std::uint32_t IndentWidth) const;
		FRangeFormatResult FormatRangeDocument(
			std::string_view Path,
			FTextRange Range,
			std::uint32_t IndentWidth,
			bool bInsertSpaces) const;

		FWorkspaceSymbolsResult WorkspaceSymbols(const FWorkspaceSymbolsOptions& Options = {}) const;

		static std::vector<std::string> ResolveLoadedModulePaths(
			std::string_view MainFilePath,
			const std::vector<std::string_view>& ImportPaths = {},
			const std::vector<std::string_view>& Defines = {},
			const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros = {},
			const std::vector<std::string_view>& ImplicitImportPaths = {});

	private:
		void FillImportPaths(std::vector<std::string_view>& OutImportPaths) const;
		void FillStdlibSearchPaths(std::vector<std::string_view>& OutPaths) const;
		void FillImplicitImportPaths(
			std::vector<std::string_view>& OutImplicitImportPaths) const;
		void FillDefines(std::vector<std::string_view>& OutDefines) const;
		FAnalyzeOptions MakeAnalyzeOptions() const;
		FOutlineOptions MakeOutlineOptions() const;
		FNavigationOptions MakeNavigationOptions() const;
		FHoverOptions MakeHoverOptions() const;
		FCompletionOptions MakeCompletionOptions() const;
		FSignatureHelpOptions MakeSignatureHelpOptions() const;
		FReferencesOptions MakeReferencesOptions() const;
		FDocumentHighlightOptions MakeDocumentHighlightOptions() const;
		FWorkspaceSymbolsOptions MakeWorkspaceSymbolsOptions() const;
		FRenameOptions MakeRenameOptions() const;
		FFormatOptions MakeFormatOptions() const;

		FWorkspaceOptions Options;
		std::vector<FDocumentOverlay> Documents;
		// The default workspace-symbol request is used by LSP's workspace/symbol.
		// Cache it between document/configuration changes instead of rescanning the
		// workspace for every query keystroke.
		mutable bool bDefaultWorkspaceSymbolIndexDirty = true;
		mutable FWorkspaceSymbolsResult DefaultWorkspaceSymbolIndex;
	};
}
