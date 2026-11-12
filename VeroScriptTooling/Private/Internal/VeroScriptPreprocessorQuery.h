// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptToolingTypes.h"
#include "VeroScriptProcessingSourceMap.h"

#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling::Detail
{
// Complete module processing uses the core resolver, overlays and configuration.
template <typename TOptions>
bool BuildModulePreprocessorLineMap(std::string_view Source, std::string_view Path,
									const TOptions &Options, FPreprocessLineMap &OutMap,
									std::string &OutError)
{
	VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
	ApplyModuleLoadingOptions(Loader, Options);
	ApplyDefines(Loader, Options.Defines);
	ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
	for (const auto& Overlay : Options.SourceOverlays)
	{
		Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
	}

	ApplyHostTypeNames(Loader, Options.Bindings);
	auto Program = Loader.LoadSourceModule(Source, Path);
	if (Program)
	{
		ApplyProcessingSourceMaps(Program->SourceMetadata.get(), Program->PreprocessLineMaps);
	}

	const auto Map = Loader.GetPreprocessLineMap(Path);
	OutMap = Map ? *Map : FPreprocessLineMap{};
	if (Program)
	{
		const auto Projected = Program->PreprocessLineMaps.find(PathUtil::IdentityKey(std::string(Path)));
		if (Projected != Program->PreprocessLineMaps.end())
		{
			OutMap = Projected->second;
		}
	}
	if (Loader.HasError())
	{
		OutError = Loader.GetError().Message;
	}

	return Map && !Map->PreprocessedSource.empty();
}

inline const FPreprocessLineMap *ProgramPreprocessorLineMap(const FProgram &Program,
															std::string_view Path)
{
	const auto It = Program.PreprocessLineMaps.find(PathUtil::IdentityKey(std::string(Path)));
	return It == Program.PreprocessLineMaps.end() ? nullptr : &It->second;
}
	/**
	 * Run PreprocessSource with tooling Defines and return the line map.
	 * Returns false on preprocess error (OutError filled); OutMap may be partial.
	 */
	bool BuildPreprocessorLineMap(
		std::string_view Source,
		const std::vector<std::string_view>& Defines,
		const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
		std::string_view LogicalPath,
		VeroScript::FPreprocessLineMap& OutMap,
		std::string& OutError);

	bool BuildPreprocessorLineMap(
		std::string_view Source,
		const std::vector<std::string_view>& Defines,
		const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
		VeroScript::FPreprocessLineMap& OutMap,
		std::string& OutError);

	bool BuildPreprocessorLineMap(
		std::string_view Source,
		const std::vector<std::string_view>& Defines,
		VeroScript::FPreprocessLineMap& OutMap,
		std::string& OutError);

	// True when Line (1-based) is active non-directive code; unknown lines treated as active.
	bool IsActiveCodeLine(
		const VeroScript::FPreprocessLineMap& Map,
		std::uint32_t Line);

	/**
	 * Last macro still in effect at the physical caret (1-based).
	 * Uses activation coordinates and emitted offsets, including imported
	 * public macros and undef events; succeeds only for an active definition.
	 * Pass 0 (default) to resolve at end of file.
	 */
	bool FindActiveMacroDef(const VeroScript::FPreprocessLineMap &Map, std::string_view Name,
							VeroScript::FPreprocessMacroDef &OutDef,
							std::uint32_t AtPhysicalLine = 0, std::uint32_t AtPhysicalColumn = 0);

	// Map physical caret to preprocessed AST space; false → use Physical unchanged.
	bool MapCaretPhysicalToPreprocessed(
		const VeroScript::FPreprocessLineMap& Map,
		std::uint32_t PhysicalLine,
		std::uint32_t PhysicalColumn,
		std::uint32_t& OutPreprocessedLine,
		std::uint32_t& OutPreprocessedColumn);

	bool MapPhysicalLineToPreprocessedLine(
		const VeroScript::FPreprocessLineMap& Map,
		std::uint32_t PhysicalLine,
		std::uint32_t& OutPreprocessedLine);

	bool MapPreprocessedLineToPhysicalLine(
		const VeroScript::FPreprocessLineMap& Map,
		std::uint32_t PreprocessedLine,
		std::uint32_t& OutPhysicalLine);

	// Map one preprocessed position to physical; line-only fallback when column map fails.
	bool TryRemapPreprocessedToPhysical(
		const VeroScript::FPreprocessLineMap& Map,
		std::uint32_t PreprocessedLine,
		std::uint32_t PreprocessedColumn,
		std::uint32_t& OutPhysicalLine,
		std::uint32_t& OutPhysicalColumn);

	// Strict half-open ranges need the last included byte, rather than mapping End
	// as a point (EOF has no map entry and emitted newlines use column 1).
	// Reuse one mapper per document; source views must outlive it.
	class FPreprocessedRangeMapper
	{
	public:
		FPreprocessedRangeMapper(const VeroScript::FPreprocessLineMap& Map,
			std::string_view PhysicalSource);
		bool TryRemap(const FTextRange& Range, FTextRange& OutPhysical) const;

	private:
		const VeroScript::FPreprocessLineMap& Map;
		std::string_view PhysicalSource;
		std::vector<std::size_t> PreprocessedLines, PhysicalLines;
	};

	// Remap outline/document symbol ranges for symbols in FileLabel (AST → physical).
	void RemapDocumentSymbolsToPhysical(
		std::string_view Source, const std::vector<std::string_view> &Defines,
		const std::vector<VeroScript::FPredefinedMacro> &PredefinedMacros,
		std::string_view FileLabel, std::vector<FDocumentSymbol> &Symbols,
		const FPreprocessLineMap *PreparedMap = nullptr);

	// Remap analyzer/tooling diagnostics for FileLabel (AST → physical).
	void RemapToolingDiagnosticsToPhysical(
		std::string_view Source,
		const std::vector<std::string_view>& Defines,
		const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
		std::string_view FileLabel,
		std::vector<FDiagnostic>& Diagnostics);

	/**
	 * Remap each diagnostic using source for its FileLabel (primary buffer, overlays, or disk).
	 */
	using FModuleLineMapResolver = std::function<bool(std::string_view, std::string_view,
		FPreprocessLineMap&, std::string&)>;
	void RemapAllToolingDiagnostics(
		std::vector<FDiagnostic>& Diagnostics,
		const std::vector<std::string_view>& Defines,
		const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
		std::string_view PrimarySource,
		std::string_view PrimaryFileLabel,
		const std::vector<FDocumentOverlay>& SourceOverlays,
		const FModuleLineMapResolver& Resolver = {});

	template <typename TOptions>
	void RemapAllToolingDiagnostics(std::vector<FDiagnostic>& Diagnostics,
		std::string_view Source, std::string_view Path, const TOptions& Options)
	{
		RemapAllToolingDiagnostics(Diagnostics, Options.Defines, Options.PredefinedMacros, Source, Path,
			Options.SourceOverlays,
			[&](std::string_view Text, std::string_view Label, FPreprocessLineMap& Map, std::string& Error)
			{
				return BuildModulePreprocessorLineMap(Text, Label, Options, Map, Error);
			});
	}
}
