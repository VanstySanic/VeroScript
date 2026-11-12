// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

/**
 * Out (script -> other languages) codegen contracts - language-agnostic.
 *
 * Core owns the backend interface; concrete emitters live in adapter modules
 * (for example VeroScriptCppAot and VeroScriptCSharpAot).
 *
 * Distinct from In (host binding): data flows the other way.
 */

#include "VeroScriptHostBinding.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"

#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	struct FCodegenOptions
	{
		std::vector<std::string> ImportPaths;
		std::vector<std::string> Defines;
		std::vector<FPredefinedMacro> PredefinedMacros;
		/**
		 * Optional host binding metadata for emitters that need host API stubs.
		 * CppAot prepends typed `extern "C"` declarations for free host functions when set,
		 * and compiles the script with the same Bindings for host type names.
		 */
		const IHostBindingCatalog* Bindings = nullptr;
		// Appended to preserve positional aggregate initialization of older callers.
		std::vector<std::string> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		FAnnotationProcessingOptions AnnotationProcessing;
		// Appended so existing aggregate initializers retain their meaning.
		FCompileProcessingOptions CompileProcessing;
		FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::vector<FSourceOverlay> SourceOverlays;
		std::vector<std::string> StdlibSearchPaths;
		std::shared_ptr<const FModuleEnvironment> ModuleEnvironment;
	};

	// Normalizes the same immutable environment as bytecode compilation.
	bool ApplyCodegenModuleEnvironment(FCodegenOptions&, std::string& Error);
	struct FProgram;
	// Shared preparation for source emitters. The private copy uses definition
	// parameter names; declaration identity and lexical body context are retained.
	std::unique_ptr<FProgram> PrepareCodegenProgram(const FProgram&,
		const FAnnotationProcessingOptions&, const IHostBindingCatalog*, std::string& Error);
	std::unique_ptr<FProgram> PrepareCodegenProgram(std::unique_ptr<FProgram>,
		const FAnnotationProcessingOptions&, const IHostBindingCatalog*, std::string& Error);
	struct FCodegenResult
	{
		bool bSuccess = false;
		std::string Output;
		std::string Error;
	};

	/**
	 * Pluggable AOT / source emitter. One implementation per target language.
	 */
	class ICodegenBackend
	{
	public:
		virtual ~ICodegenBackend() = default;

		// Stable backend id, e.g. "cpp" or "csharp".
		virtual std::string_view GetName() const = 0;

		// Default output extension including the dot, e.g. ".cpp".
		virtual std::string_view GetFileExtension() const = 0;

		virtual bool EmitFromSource(
			std::string_view Source,
			std::string_view SourceLabel,
			const FCodegenOptions& Options,
			FCodegenResult& OutResult) = 0;

		virtual bool EmitFromFile(
			std::string_view ScriptPath,
			const FCodegenOptions& Options,
			FCodegenResult& OutResult) = 0;
	};
}
