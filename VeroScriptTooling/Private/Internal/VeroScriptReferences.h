// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"
#include "VeroScriptAnnotationProcessing.h"

#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptSymbolQuery.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	struct FReferenceHit
	{
		std::string FileLabel;
		std::uint32_t Line = 1;
		std::uint32_t StartColumn = 1;
		std::uint32_t EndColumn = 1;
		bool bIsDefinition = false;
		// True when the occurrence is an assignment / compound / ++/-- target.
		bool bIsWrite = false;
	};

	struct FReferencesOptions
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

	struct FReferencesResult
	{
		bool bSuccess = false;
		FScriptError Error;
		std::vector<FScriptError> Diagnostics;
		bool bHasToken = false;
		std::string TokenText;
		std::vector<FReferenceHit> References;
	};

	/**
	 * Find references for the identifier under the caret.
	 * Identifier hits are re-resolved to the caret definition (homonyms dropped).
	 * Operator queries remain lexical for binary/unary ops; typed `()` / `[]` use-sites
	 * are included when the receiver resolves to the queried operator declaration.
	 */
	class VeroScriptReferences
	{
	public:
		static FReferencesResult FindAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FReferencesOptions& Options = {});

		static FReferencesResult FindAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FReferencesOptions& Options = {});
	};
}
