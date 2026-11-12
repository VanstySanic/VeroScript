// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptProgramPreparation.h"
#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"

#include "VeroScriptBytecode.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptNative.h"
#include "VeroScriptPreprocessor.h"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VeroScript
{
	struct FCompileExtensionManifest;
	class FModuleEnvironment;
	struct FCompileOptions
	{
		std::vector<std::string_view> ImportPaths;
		// `-D` / `#define` initial macros (`NAME` or `NAME=value`).
		std::vector<std::string_view> Defines;
		std::vector<FPredefinedMacro> PredefinedMacros;
		/**
		 * Prefer Bindings when both are set: Bindings->GetNativeFunctions() is used.
		 * Direct Natives remains for callers that only have a function table.
		 */
		const std::vector<FNativeFunction>* Natives = nullptr;
		const IHostBindingCatalog* Bindings = nullptr;
		std::vector<FSourceOverlay> SourceOverlays;
		/**
		 * Modules implicitly imported by every module in this compilation.
		 * These are ordinary imports: only exported declarations are visible and
		 * they are not re-exported. Completed public macro interfaces are available
		 * from the start of consumer source. Providers and their explicit dependencies
		 * establish these interfaces first; macro dependencies between providers
		 * require explicit imports, without implicit cyclic macro evaluation.
		 *
		 * Kept at the end to preserve positional aggregate initialization of the
		 * pre-existing public options.
		 */
		std::vector<std::string_view> ImplicitImportPaths;
		// Permit cyclic explicit/implicit import graphs (resolved in declaration phases).
		bool bAllowCircularImports = true;
		/**
		 * Entry + implicit-import modules may access one another's non-exported
		 * declarations. Project-external modules still require export/import.
		 */
		bool bAllowProjectWideAccess = false;
		// Extra standard-library roots. Kept distinct from ordinary import paths so
		// standard-library visibility, metadata and sibling localization stay intact.
		std::vector<std::string_view> StdlibSearchPaths;
		bool bBuildPreprocessLineMaps = false;
		FAnnotationProcessingOptions AnnotationProcessing;
		FCompileProcessingOptions CompileProcessing;
		FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const FModuleEnvironment> ModuleEnvironment;
		FConstantEvaluationLimits ConstantEvaluationLimits;
	};

	struct FCompileResult
	{
		bool bSuccess = false;
		FScriptError Error;
		std::vector<FScriptError> Diagnostics;
		std::unique_ptr<FBytecodeModule> Module;
		std::unordered_map<std::string, FPreprocessLineMap> PreprocessLineMaps;
		std::shared_ptr<const FPreparedProgram> PreparedProgram;
		// Exact modules read by the loader, including imported binary libraries.
		std::vector<std::string> LoadedModulePaths;
		// Producer dependencies for a compilation-extension package manifest.
		std::vector<FScriptCompileExtensionRoot> CompileExtensionDependencies;
		std::shared_ptr<const FCompileExtensionManifest> CompileExtensionManifest;
	};

	/**
	 * Compile source/file to bytecode for tooling (debug, analysis).
	 * Does not execute the script.
	 */
	class VeroScriptCompile
	{
	public:
		static FCompileResult CompileSource(
			std::string_view Source,
			std::string_view FileLabel = "<source>",
			const FCompileOptions& Options = {});

		static FCompileResult CompileFile(
			std::string_view FilePath,
			const FCompileOptions& Options = {});
	};
}
