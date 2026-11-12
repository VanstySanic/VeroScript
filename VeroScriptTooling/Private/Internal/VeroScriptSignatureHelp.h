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
	struct FSignatureParameter
	{
		std::string Label;
	};

	struct FSignatureInformation
	{
		std::string Label;
		std::vector<FSignatureParameter> Parameters;
		std::string Documentation;
	};

	// Shared by loaded queries and host-catalog recovery.
	FSignatureInformation MakeAnnotationSignature(const VeroScript::FHostAnnotationDesc& Annotation);

	struct FSignatureHelpOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FSourceOverlay> SourceOverlays;
		// Optional In-catalog host bindings (not owned).
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FSignatureHelpResult
	{
		bool bSuccess = false;
		FScriptError Error;
		std::vector<FScriptError> Diagnostics;
		bool bActive = false;
		std::uint32_t ActiveSignature = 0;
		std::uint32_t ActiveParameter = 0;
		std::vector<FSignatureInformation> Signatures;
	};

	/**
	 * Signature help at a caret inside a call argument list.
	 * Uses lexer call-site detection (works for incomplete `foo(a,`); loads symbols with
	 * soft recovery of inserting `);` at the caret when needed.
	 */
	class VeroScriptSignatureHelp
	{
	public:
		static FSignatureHelpResult HelpAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FSignatureHelpOptions& Options = {});

		static FSignatureHelpResult HelpAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FSignatureHelpOptions& Options = {});
	};
}
