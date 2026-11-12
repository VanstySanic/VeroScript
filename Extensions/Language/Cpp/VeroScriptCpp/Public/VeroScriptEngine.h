// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHost.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptRun.h"

#include <iosfwd>
#include <memory>
#include <string_view>
#include <vector>

namespace VeroScript
{
	/**
	 * High-level entry: lex/parse/compile/run a script file or source buffer.
	 * ostream overloads keep CLI behavior (including wide console print).
	 * IEngineHost overloads are the stable in-process embedding surface.
	 * Script compile extensions inherit Config.ExecutionControl when their own
	 * control is null; an explicit compile control is used independently.
	 * Lives in VeroScriptCpp (C++ host linkage).
	 */
	class VeroScriptEngine
	{
	public:
		static FRunResult CheckSource(
			std::string_view Source,
			std::string_view FileLabel,
			std::ostream& ErrorStream,
			const std::vector<std::string_view>& ImportPaths = {},
			const std::vector<std::string_view>& Defines = {},
			const std::vector<FPredefinedMacro>& PredefinedMacros = {},
			const std::vector<std::string_view>& ImplicitImportPaths = {},
			bool bAllowCircularImports = true,
			bool bAllowProjectWideAccess = false);

		static FRunResult CheckSource(
			std::string_view Source,
			std::string_view FileLabel,
			IEngineHost& Host,
			const FEngineConfig& Config = {});

		static FRunResult CheckFile(
			std::string_view FilePath,
			std::ostream& ErrorStream,
			const std::vector<std::string_view>& ImportPaths = {},
			const std::vector<std::string_view>& Defines = {},
			std::string_view BytecodeCacheDirectory = {},
			const std::vector<std::string_view>& StdlibSearchPaths = {},
			const IHostBindingCatalog* Bindings = nullptr,
			const std::vector<FPredefinedMacro>& PredefinedMacros = {},
			const std::vector<std::string_view>& ImplicitImportPaths = {},
			bool bAllowCircularImports = true,
			bool bAllowProjectWideAccess = false);

		static FRunResult CheckFile(
			std::string_view FilePath,
			IEngineHost& Host,
			const FEngineConfig& Config = {});

		// Stream entries accept an explicit backend factory just like FEngineConfig.
		// Pure Core does not select an interpreter. Use FEngineConfig or the explicit VM overload.
		static FRunResult RunSource(
			std::string_view Source,
			std::string_view FileLabel,
			std::ostream& OutputStream,
			std::ostream& ErrorStream,
			ERunMode RunMode = ERunMode::Script,
			const std::vector<std::string_view>& ImportPaths = {},
			const std::vector<std::string_view>& ScriptArgs = {},
			std::istream* InputStream = nullptr,
			const std::vector<std::string_view>& Defines = {},
			const std::vector<std::string_view>& StdlibSearchPaths = {},
			EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed,
			const std::vector<FPredefinedMacro>& PredefinedMacros = {},
			const std::vector<std::string_view>& ImplicitImportPaths = {},
			bool bAllowCircularImports = true,
			bool bAllowProjectWideAccess = false);

		static FRunResult RunSource(
			std::string_view Source,
			std::string_view FileLabel,
			std::ostream& OutputStream,
			std::ostream& ErrorStream,
			ERunMode RunMode,
			const std::vector<std::string_view>& ImportPaths,
			const std::vector<std::string_view>& ScriptArgs,
			std::istream* InputStream,
			const std::vector<std::string_view>& Defines,
			const std::vector<std::string_view>& StdlibSearchPaths,
			EExecutionBackend ExecutionBackend,
			const std::vector<FPredefinedMacro>& PredefinedMacros,
			const std::vector<std::string_view>& ImplicitImportPaths,
			bool bAllowCircularImports,
			bool bAllowProjectWideAccess,
			const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory);

		static FRunResult RunSource(
			std::string_view Source,
			std::string_view FileLabel,
			std::ostream& OutputStream,
			std::ostream& ErrorStream,
			ERunMode RunMode,
			const std::vector<std::string_view>& ImportPaths,
			const std::vector<std::string_view>& ScriptArgs,
			std::istream* InputStream,
			const std::vector<std::string_view>& Defines,
			const std::vector<std::string_view>& StdlibSearchPaths,
			EExecutionBackend ExecutionBackend,
			const std::vector<FPredefinedMacro>& PredefinedMacros,
			const std::vector<std::string_view>& ImplicitImportPaths,
			bool bAllowCircularImports,
			bool bAllowProjectWideAccess,
			const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory,
			const std::shared_ptr<const IVmBackendFactory>& VmBackendFactory);

		static FRunResult RunSource(
			std::string_view Source,
			std::string_view FileLabel,
			IEngineHost& Host,
			ERunMode RunMode = ERunMode::Script,
			const FEngineConfig& Config = {});

		static FRunResult RunFile(
			std::string_view FilePath,
			std::ostream& OutputStream,
			std::ostream& ErrorStream,
			const std::vector<std::string_view>& ImportPaths = {},
			const std::vector<std::string_view>& ScriptArgs = {},
			bool bUseBytecodeCache = true,
			std::istream* InputStream = nullptr,
			const std::vector<std::string_view>& Defines = {},
			std::string_view BytecodeCacheDirectory = {},
			const std::vector<std::string_view>& StdlibSearchPaths = {},
			EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed,
			const IHostBindingCatalog* Bindings = nullptr,
			const std::vector<FPredefinedMacro>& PredefinedMacros = {},
			const std::vector<std::string_view>& ImplicitImportPaths = {},
			bool bAllowCircularImports = true,
			bool bAllowProjectWideAccess = false);

		static FRunResult RunFile(
			std::string_view FilePath,
			std::ostream& OutputStream,
			std::ostream& ErrorStream,
			const std::vector<std::string_view>& ImportPaths,
			const std::vector<std::string_view>& ScriptArgs,
			bool bUseBytecodeCache,
			std::istream* InputStream,
			const std::vector<std::string_view>& Defines,
			std::string_view BytecodeCacheDirectory,
			const std::vector<std::string_view>& StdlibSearchPaths,
			EExecutionBackend ExecutionBackend,
			const IHostBindingCatalog* Bindings,
			const std::vector<FPredefinedMacro>& PredefinedMacros,
			const std::vector<std::string_view>& ImplicitImportPaths,
			bool bAllowCircularImports,
			bool bAllowProjectWideAccess,
			const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory);

		static FRunResult RunFile(
			std::string_view FilePath,
			std::ostream& OutputStream,
			std::ostream& ErrorStream,
			const std::vector<std::string_view>& ImportPaths,
			const std::vector<std::string_view>& ScriptArgs,
			bool bUseBytecodeCache,
			std::istream* InputStream,
			const std::vector<std::string_view>& Defines,
			std::string_view BytecodeCacheDirectory,
			const std::vector<std::string_view>& StdlibSearchPaths,
			EExecutionBackend ExecutionBackend,
			const IHostBindingCatalog* Bindings,
			const std::vector<FPredefinedMacro>& PredefinedMacros,
			const std::vector<std::string_view>& ImplicitImportPaths,
			bool bAllowCircularImports,
			bool bAllowProjectWideAccess,
			const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory,
			const std::shared_ptr<const IVmBackendFactory>& VmBackendFactory);

		static FRunResult RunFile(
			std::string_view FilePath,
			IEngineHost& Host,
			const FEngineConfig& Config = {});
	};
}
