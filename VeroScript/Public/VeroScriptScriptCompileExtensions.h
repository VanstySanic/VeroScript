// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include <map>

namespace VeroScript
{
	struct FCompileOptions;
	struct FCompileResult;
	class IHostBindingCatalog;
	class FExecutionControl;
	class FPreparedProgram;
	class IVmBackendFactory;
	struct FScriptCompileExtensionRoot
	{
		std::string Path;
		std::string RegistrationEntry;
	};
	// Compilation extensions are explicitly enabled code, executed on an isolated VM.
	// These controls are execution/resource policy, not a complete security sandbox.
	struct FScriptCompileExtensionsOptions
	{
		std::vector<FScriptCompileExtensionRoot> Roots;
		bool bAllowSourceImports = true;
		std::vector<std::string> AllowedRoots;
		std::map<std::string, std::string> Configuration;
		const IHostBindingCatalog* Bindings = nullptr;
		bool bBindingsDeterministic = false;
		std::string BindingConfiguration;
		std::shared_ptr<FExecutionControl> ExecutionControl;
		std::uint64_t MaxInstructions = 1000000;
		std::uint64_t TimeoutMilliseconds = 2000;
		std::size_t MaxRoots = 64, MaxDependencyDepth = 32, MaxRegistrations = 4096;
		std::size_t MaxBridgeBytes = 16 * 1024 * 1024, MaxBridgeValues = 262144;
		std::shared_ptr<const IVmBackendFactory> VmBackendFactory;
		std::string VmBackendPath;
	};
	// A single preparation owns this service. Its generated profile and dependencies
	// are plain values; prepared programs never retain the execution service.
	class IScriptCompileExtensionTask
	{
	public:
		virtual ~IScriptCompileExtensionTask() = default;
		virtual const FCompileProcessingOptions& GetProcessingOptions() const = 0;
		virtual bool Close(std::vector<FScriptError>& Diagnostics) = 0;
		// Check after initialization/registration/factories and before business
		// callbacks. The source graph identity remains the caller's cache key.
		virtual bool MatchesPreparedProgram(const FPreparedProgram&, const IHostBindingCatalog* = nullptr) = 0;
	};
	std::shared_ptr<IScriptCompileExtensionTask> LoadScriptCompileExtensions(std::string_view Source,
																			 std::string_view FileLabel,
																			 const FCompileOptions& Options,
																			 std::vector<FScriptError>& Diagnostics);
	std::string GetScriptCompileServiceFingerprint();
	// Compile the extension's library code without initializing it or registering
	// processors. Pair the returned module with a CompileExtension package manifest.
	FCompileResult CompileScriptCompileExtensionSource(std::string_view Source, std::string_view FileLabel,
													   const FCompileOptions& Options,
													   std::string_view RegistrationEntry = {});
	// Stable prefix recognition shared by the loader and interactive tools.
	bool HasScriptCompileExtensionImport(std::string_view Source);
	// Compile-only catalog for analyzing extension source; callbacks are stubs.
	std::shared_ptr<const IHostBindingCatalog> GetScriptCompileServiceCatalog();
} // namespace VeroScript
