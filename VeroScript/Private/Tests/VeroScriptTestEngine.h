// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptEngine.h"
#include "VeroScriptTestVm.h"

namespace VeroScript::Test
{
	// Existing tests explicitly assemble the selected interpreter at their host boundary.
	class FEngine : public VeroScriptEngine
	{
	public:
		using VeroScriptEngine::CheckSource;
		static FRunResult CheckSource(
			std::string_view Source,
			std::string_view FileLabel,
			IEngineHost& Host,
			const FEngineConfig& Config = {})
		{
			FEngineConfig Selected = Config;
			Configure(Selected);
			return VeroScriptEngine::CheckSource(Source, FileLabel, Host, Selected);
		}
		using VeroScriptEngine::CheckFile;
		static FRunResult CheckFile(
			std::string_view FilePath,
			IEngineHost& Host,
			const FEngineConfig& Config = {})
		{
			FEngineConfig Selected = Config;
			Configure(Selected);
			return VeroScriptEngine::CheckFile(FilePath, Host, Selected);
		}
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
			bool bAllowProjectWideAccess = false)
		{
			return VeroScriptEngine::RunSource(Source, FileLabel, OutputStream, ErrorStream, RunMode, ImportPaths, ScriptArgs, InputStream, Defines, StdlibSearchPaths, ExecutionBackend, PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, {}, VmFactory());
		}
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
			const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory)
		{
			return VeroScriptEngine::RunSource(Source, FileLabel, OutputStream, ErrorStream, RunMode, ImportPaths, ScriptArgs, InputStream, Defines, StdlibSearchPaths, ExecutionBackend, PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, JitBackendFactory, VmFactory());
		}
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
			const std::shared_ptr<const IVmBackendFactory>& VmBackendFactory)
		{
			return VeroScriptEngine::RunSource(Source, FileLabel, OutputStream, ErrorStream, RunMode, ImportPaths, ScriptArgs, InputStream, Defines, StdlibSearchPaths, ExecutionBackend, PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, JitBackendFactory, VmBackendFactory ? VmBackendFactory : VmFactory());
		}
		static FRunResult RunSource(
			std::string_view Source,
			std::string_view FileLabel,
			IEngineHost& Host,
			ERunMode RunMode = ERunMode::Script,
			const FEngineConfig& Config = {})
		{
			FEngineConfig Selected = Config;
			Configure(Selected);
			return VeroScriptEngine::RunSource(Source, FileLabel, Host, RunMode, Selected);
		}
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
			bool bAllowProjectWideAccess = false)
		{
			return VeroScriptEngine::RunFile(FilePath, OutputStream, ErrorStream, ImportPaths, ScriptArgs, bUseBytecodeCache, InputStream, Defines, BytecodeCacheDirectory, StdlibSearchPaths, ExecutionBackend, Bindings, PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, {}, VmFactory());
		}
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
			const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory)
		{
			return VeroScriptEngine::RunFile(FilePath, OutputStream, ErrorStream, ImportPaths, ScriptArgs, bUseBytecodeCache, InputStream, Defines, BytecodeCacheDirectory, StdlibSearchPaths, ExecutionBackend, Bindings, PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, JitBackendFactory, VmFactory());
		}
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
			const std::shared_ptr<const IVmBackendFactory>& VmBackendFactory)
		{
			return VeroScriptEngine::RunFile(FilePath, OutputStream, ErrorStream, ImportPaths, ScriptArgs, bUseBytecodeCache, InputStream, Defines, BytecodeCacheDirectory, StdlibSearchPaths, ExecutionBackend, Bindings, PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, JitBackendFactory, VmBackendFactory ? VmBackendFactory : VmFactory());
		}
		static FRunResult RunFile(
			std::string_view FilePath,
			IEngineHost& Host,
			const FEngineConfig& Config = {})
		{
			FEngineConfig Selected = Config;
			Configure(Selected);
			return VeroScriptEngine::RunFile(FilePath, Host, Selected);
		}
	};
}
