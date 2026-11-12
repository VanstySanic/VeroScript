// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptCallInterceptor.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptJit.h"
#include "VeroScriptMutation.h"
#include "VeroScriptNative.h"
#include "VeroScriptRun.h"

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	struct FProgram;
	struct FHotReloadRollback;
	struct FHotReloadState;
	class FPreparedProgram;
	class VeroScriptModuleLoader;
	class VeroScriptDebugger;

	namespace EngineInternal
	{
		void WriteError(std::ostream& ErrorStream, const FScriptError& Error);
		// Validate physical file/cache paths before filesystem normalization or I/O.
		bool ValidateFilePath(std::string_view Path, std::ostream& ErrorStream, FRunResult& OutFailure);
		int GetLoaderFailureExitCode(const FScriptError& Error);
		void ApplyImportPaths(
			VeroScriptModuleLoader& Loader,
			const std::vector<std::string_view>& ImportPaths);
		void ApplyImportPaths(
			VeroScriptModuleLoader& Loader,
			const std::vector<std::string>& ImportPaths);
		void ApplyStdlibSearchRoots(
			VeroScriptModuleLoader& Loader,
			const std::vector<std::string_view>& StdlibSearchPaths);

		bool CompileProgram(
			std::unique_ptr<FProgram> Program,
			std::ostream& ErrorStream,
			std::unique_ptr<FBytecodeModule>& OutModule,
			FRunResult& OutResult,
			const std::vector<FNativeFunction>* Natives = nullptr,
			const IHostBindingCatalog* Bindings = nullptr,
			const FAnnotationProcessingOptions* Processing = nullptr, const FPreparedProgram* Prepared = nullptr);

		FRunResult RunBytecodeModule(
			const FBytecodeModule& Module, std::ostream& OutputStream, std::ostream& ErrorStream,
			ERunMode RunMode, const std::vector<std::string_view>& ScriptArgs = {},
			const std::vector<FNativeFunction>* Natives = nullptr, std::istream* InputStream = nullptr,
			FHotReloadState* HotReloadState = nullptr, FHotReloadRollback* HotReloadRollback = nullptr,
			EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed, std::size_t MaxCallDepth = 0,
			const FJitPolicy* JitPolicy = nullptr, FJitStats* OutJitStats = nullptr,
			const FInstanceCallInterceptor* InstanceCallInterceptor = nullptr,
			std::uint64_t ProgramGeneration = 1, const FMutationCallback* MutationCallback = nullptr,
			const FExecutionLimits* ExecutionLimits = nullptr,
			const std::shared_ptr<FExecutionControl>* ExecutionControl = nullptr,
			const FScriptCapabilities* Capabilities = nullptr,
			const std::shared_ptr<const void>* ProgramOwner = nullptr, VeroScriptDebugger* Debugger = nullptr,
			const std::shared_ptr<const IJitBackendFactory>* JitBackendFactory = nullptr,
			std::shared_ptr<const FBytecodeModule> JitModuleOwner = {},
			const FMemoryLimits* MemoryLimits = nullptr, const IHostBindingCatalog* MemoryBindings = nullptr,
			const std::shared_ptr<I18n::FI18nServices>* I18nServices = nullptr,
			const I18n::FI18nLimits* I18nLimits = nullptr,
			std::shared_ptr<const FModuleEnvironment> Environment = {},
			const FStandardLibraryLimits* StandardLibraryLimits = nullptr,
			const FNumericLimits* NumericLimits = nullptr,
			std::shared_ptr<const IAotProgram> AotProgram = {},
			const FEngineConfig* VmConfig = nullptr);

		FRunResult CompileSourceToModule(std::string_view Source, std::string_view FileLabel,
										 const std::vector<std::string_view>& ImportPaths,
										 std::ostream& ErrorStream,
										 std::unique_ptr<FBytecodeModule>& OutModule,
										 const std::vector<FNativeFunction>* Natives = nullptr,
										 const std::vector<FSourceOverlay>* SourceOverlays = nullptr,
										 const std::vector<std::string_view>* Defines = nullptr,
										 const std::vector<FPredefinedMacro>* PredefinedMacros = nullptr,
										 const std::vector<std::string_view>* StdlibSearchPaths = nullptr,
										 const IHostBindingCatalog* Bindings = nullptr,
										 const std::vector<std::string_view>* ImplicitImportPaths = nullptr,
										 bool bAllowCircularImports = true,
										 bool bAllowProjectWideAccess = false, bool bUseBytecodeCache = false,
										 std::string_view BytecodeCacheDirectory = {},
										 const FAnnotationProcessingOptions* Processing = nullptr,
										 const FCompileProcessingOptions* CompileProcessing = nullptr,
										 const FScriptCompileExtensionsOptions* ScriptExtensions = nullptr,
										 std::shared_ptr<const FModuleEnvironment> Environment = {});

		FRunResult CompileFileToModule(std::string_view FilePath,
									   const std::vector<std::string_view>& ImportPaths,
									   bool bUseBytecodeCache, std::ostream& ErrorStream,
									   std::unique_ptr<FBytecodeModule>& OutModule,
									   const std::vector<FNativeFunction>* Natives = nullptr,
									   const std::vector<FSourceOverlay>* SourceOverlays = nullptr,
									   const std::vector<std::string_view>* Defines = nullptr,
									   const std::vector<FPredefinedMacro>* PredefinedMacros = nullptr,
									   std::string_view BytecodeCacheDirectory = {},
									   const std::vector<std::string_view>* StdlibSearchPaths = nullptr,
									   const IHostBindingCatalog* Bindings = nullptr,
									   const std::vector<std::string_view>* ImplicitImportPaths = nullptr,
									   bool bAllowCircularImports = true,
									   bool bAllowProjectWideAccess = false,
									   const FAnnotationProcessingOptions* Processing = nullptr,
									   const FCompileProcessingOptions* CompileProcessing = nullptr,
									   const FScriptCompileExtensionsOptions* ScriptExtensions = nullptr,
									   std::shared_ptr<const FModuleEnvironment> Environment = {});
	}
}
