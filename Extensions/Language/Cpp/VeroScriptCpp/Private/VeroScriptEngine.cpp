// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEngine.h"

#include "VeroScriptBytecode.h"
#include "VeroScriptEngineInternal.h"
#include "VeroScriptHost.h"

#include <memory>
#if defined(VEROSCRIPT_WITH_APPLICATION)
#include "VeroScriptSession.h"
#include "VeroScriptApplication.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#endif

namespace VeroScript
{
	#if defined(VEROSCRIPT_WITH_APPLICATION)
	static bool IsApplicationFile(std::string_view File)
	{
		// The common compile entry reports invalid paths; do not probe a prefix here.
		if (File.find('\0') != std::string_view::npos) return false;
		const auto Path = PathUtil::FromUtf8(File);
		auto Extension = PathUtil::ToUtf8(Path.extension());
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C)
		{
			return static_cast<char>(std::tolower(C));
		});
		std::error_code E;
		return Extension == ".vsapp" || Extension == ".zip" || Path.filename() == "veroscript-app.json" || std::filesystem::is_directory(Path, E);
	}
	#endif
	static FScriptCompileExtensionsOptions CompileExtensionsFor(const FEngineConfig& Config)
	{
		auto Options = Config.ScriptCompileExtensions;
		if (!Options.VmBackendFactory && Options.VmBackendPath.empty())
		{
			Options.VmBackendFactory = Config.VmBackendFactory;
			Options.VmBackendPath = Config.VmBackendPath;
			if (!Options.VmBackendFactory && Options.VmBackendPath.empty() && !Config.bClearVmBackend)
			{
				Options.VmBackendFactory = Config.FallbackVmBackendFactory;
				Options.VmBackendPath = Config.FallbackVmBackendPath;
			}
		}
		if (!Options.ExecutionControl)
		{
			Options.ExecutionControl = Config.ExecutionControl;
		}

		return Options;
	}

	static FRunResult WithCompileDiagnostics(FRunResult Result, const FRunResult& Compilation)
	{
		Result.Diagnostics.insert(Result.Diagnostics.begin(), Compilation.Diagnostics.begin(), Compilation.Diagnostics.end());
		return Result;
	}

	FRunResult VeroScriptEngine::CheckSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    std::ostream& ErrorStream,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		std::unique_ptr<FBytecodeModule> BytecodeModule;
		return EngineInternal::CompileSourceToModule(
		    Source,
		    FileLabel,
		    ImportPaths,
		    ErrorStream,
		    BytecodeModule,
		    nullptr,
		    nullptr,
		    &Defines,
		    &PredefinedMacros,
		    nullptr,
		    nullptr,
		    &ImplicitImportPaths,
		    bAllowCircularImports,
		    bAllowProjectWideAccess);
	}

	FRunResult VeroScriptEngine::CheckSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    IEngineHost& Host,
	    const FEngineConfig& Config)
	{
		FHostStreamBridge Bridge(Host);
		const auto ScriptExtensions = CompileExtensionsFor(Config);
		std::unique_ptr<FBytecodeModule> BytecodeModule;
		return EngineInternal::CompileSourceToModule(
		    Source, FileLabel, Config.ImportPaths, Bridge.ErrorStream(), BytecodeModule, nullptr,
		    &Config.SourceOverlays, &Config.Defines, &Config.PredefinedMacros, &Config.StdlibSearchPaths,
		    nullptr, &Config.ImplicitImportPaths, Config.bAllowCircularImports,
		    Config.bAllowProjectWideAccess, false, {}, &Config.AnnotationProcessing,
		    &Config.CompileProcessing, &ScriptExtensions, Config.ModuleEnvironment);
	}
	FRunResult VeroScriptEngine::CheckFile(
	    const std::string_view FilePath,
	    std::ostream& ErrorStream,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& Defines,
	    const std::string_view BytecodeCacheDirectory,
	    const std::vector<std::string_view>& StdlibSearchPaths,
	    const IHostBindingCatalog* const Bindings,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		std::unique_ptr<FBytecodeModule> BytecodeModule;
		return EngineInternal::CompileFileToModule(
		    FilePath,
		    ImportPaths,
		    false,
		    ErrorStream,
		    BytecodeModule,
		    Bindings != nullptr ? &Bindings->GetNativeFunctions() : nullptr,
		    nullptr,
		    &Defines,
		    &PredefinedMacros,
		    BytecodeCacheDirectory,
		    &StdlibSearchPaths,
		    Bindings,
		    &ImplicitImportPaths,
		    bAllowCircularImports,
		    bAllowProjectWideAccess);
	}

	FRunResult VeroScriptEngine::CheckFile(
	    const std::string_view FilePath,
	    IEngineHost& Host,
	    const FEngineConfig& Config)
	{
		FHostStreamBridge Bridge(Host);
		const auto ScriptExtensions = CompileExtensionsFor(Config);
		std::unique_ptr<FBytecodeModule> BytecodeModule;
		return EngineInternal::CompileFileToModule(
		    FilePath, Config.ImportPaths, false, Bridge.ErrorStream(), BytecodeModule, nullptr,
		    &Config.SourceOverlays, &Config.Defines, &Config.PredefinedMacros, Config.BytecodeCacheDirectory,
		    &Config.StdlibSearchPaths, nullptr, &Config.ImplicitImportPaths, Config.bAllowCircularImports,
		    Config.bAllowProjectWideAccess, &Config.AnnotationProcessing, &Config.CompileProcessing,
		    &ScriptExtensions, Config.ModuleEnvironment);
	}

	FRunResult VeroScriptEngine::RunSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    std::ostream& OutputStream,
	    std::ostream& ErrorStream,
	    const ERunMode RunMode,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& ScriptArgs,
	    std::istream* const InputStream,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<std::string_view>& StdlibSearchPaths,
	    const EExecutionBackend ExecutionBackend,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		return RunSource(Source, FileLabel, OutputStream, ErrorStream, RunMode,
		    ImportPaths, ScriptArgs, InputStream, Defines, StdlibSearchPaths, ExecutionBackend,
		    PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, {}, {});
	}

	FRunResult VeroScriptEngine::RunSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    std::ostream& OutputStream,
	    std::ostream& ErrorStream,
	    const ERunMode RunMode,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& ScriptArgs,
	    std::istream* const InputStream,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<std::string_view>& StdlibSearchPaths,
	    const EExecutionBackend ExecutionBackend,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess,
	    const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory)
	{
		return RunSource(Source, FileLabel, OutputStream, ErrorStream, RunMode, ImportPaths, ScriptArgs, InputStream, Defines, StdlibSearchPaths, ExecutionBackend, PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, JitBackendFactory, {});
	}

	FRunResult VeroScriptEngine::RunSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    std::ostream& OutputStream,
	    std::ostream& ErrorStream,
	    const ERunMode RunMode,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& ScriptArgs,
	    std::istream* const InputStream,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<std::string_view>& StdlibSearchPaths,
	    const EExecutionBackend ExecutionBackend,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess,
	    const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory,
	    const std::shared_ptr<const IVmBackendFactory>& VmBackendFactory)
	{
		FEngineConfig VmConfig;
		VmConfig.VmBackendFactory = VmBackendFactory;
		FScriptCompileExtensionsOptions VmCompileConfig;
		VmCompileConfig.VmBackendFactory = VmBackendFactory;
		std::unique_ptr<FBytecodeModule> BytecodeModule;
		const FRunResult CompileResult = EngineInternal::CompileSourceToModule(
		    Source,
		    FileLabel,
		    ImportPaths,
		    ErrorStream,
		    BytecodeModule,
		    nullptr,
		    nullptr,
		    &Defines,
		    &PredefinedMacros,
		    &StdlibSearchPaths,
		    nullptr,
		    &ImplicitImportPaths,
		    bAllowCircularImports,
		    bAllowProjectWideAccess, false, {}, nullptr, nullptr, &VmCompileConfig);
		if (!CompileResult.bSuccess || !BytecodeModule)
		{
			return CompileResult;
		}

		return WithCompileDiagnostics(EngineInternal::RunBytecodeModule(
		                                  *BytecodeModule,
		                                  OutputStream,
		                                  ErrorStream,
		                                  RunMode,
		                                  ScriptArgs,
		                                  nullptr,
		                                  InputStream,
		                                  nullptr,
		                                  nullptr,
		                                  ExecutionBackend,
		                                  0, nullptr, nullptr, nullptr, 1, nullptr, nullptr, nullptr, nullptr,
		                                  nullptr, nullptr, &JitBackendFactory, {}, nullptr, nullptr, nullptr, nullptr, {}, nullptr, nullptr, {}, &VmConfig),
		    CompileResult);
	}

	FRunResult VeroScriptEngine::RunSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    IEngineHost& Host,
	    const ERunMode RunMode,
	    const FEngineConfig& Config)
	{
		FHostStreamBridge Bridge(Host);
		const auto ScriptExtensions = CompileExtensionsFor(Config);
		std::unique_ptr<FBytecodeModule> BytecodeModule;
		const FRunResult CompileResult = EngineInternal::CompileSourceToModule(
		    Source, FileLabel, Config.ImportPaths, Bridge.ErrorStream(), BytecodeModule, nullptr,
		    &Config.SourceOverlays, &Config.Defines, &Config.PredefinedMacros, &Config.StdlibSearchPaths,
		    nullptr, &Config.ImplicitImportPaths, Config.bAllowCircularImports,
		    Config.bAllowProjectWideAccess, Config.bUseBytecodeCache, Config.BytecodeCacheDirectory,
		    &Config.AnnotationProcessing, &Config.CompileProcessing, &ScriptExtensions,
		    Config.ModuleEnvironment);
		if (!CompileResult.bSuccess || !BytecodeModule)
		{
			return CompileResult;
		}

		return WithCompileDiagnostics(
		    EngineInternal::RunBytecodeModule(
		        *BytecodeModule, Bridge.OutputStream(), Bridge.ErrorStream(), RunMode, Config.ScriptArgs,
		        nullptr, Config.InputStream, nullptr, nullptr, Config.ExecutionBackend, Config.MaxCallDepth,
		        nullptr, nullptr, nullptr, 1, nullptr, &Config.ExecutionLimits, &Config.ExecutionControl,
		        &Config.Capabilities, nullptr, nullptr,
		        Config.JitBackendFactory ? &Config.JitBackendFactory : nullptr, {}, &Config.MemoryLimits,
		        nullptr, &Config.I18nServices, &Config.I18nLimits, Config.ModuleEnvironment, &Config.StandardLibraryLimits, &Config.NumericLimits, Config.AotProgram, &Config),
		    CompileResult);
	}

	FRunResult VeroScriptEngine::RunFile(
	    const std::string_view FilePath,
	    std::ostream& OutputStream,
	    std::ostream& ErrorStream,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& ScriptArgs,
	    const bool bUseBytecodeCache,
	    std::istream* const InputStream,
	    const std::vector<std::string_view>& Defines,
	    const std::string_view BytecodeCacheDirectory,
	    const std::vector<std::string_view>& StdlibSearchPaths,
	    const EExecutionBackend ExecutionBackend,
	    const IHostBindingCatalog* const Bindings,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		return RunFile(FilePath, OutputStream, ErrorStream, ImportPaths, ScriptArgs,
		    bUseBytecodeCache, InputStream, Defines, BytecodeCacheDirectory, StdlibSearchPaths,
		    ExecutionBackend, Bindings, PredefinedMacros, ImplicitImportPaths,
		    bAllowCircularImports, bAllowProjectWideAccess, {}, {});
	}

	FRunResult VeroScriptEngine::RunFile(
	    const std::string_view FilePath,
	    std::ostream& OutputStream,
	    std::ostream& ErrorStream,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& ScriptArgs,
	    const bool bUseBytecodeCache,
	    std::istream* const InputStream,
	    const std::vector<std::string_view>& Defines,
	    const std::string_view BytecodeCacheDirectory,
	    const std::vector<std::string_view>& StdlibSearchPaths,
	    const EExecutionBackend ExecutionBackend,
	    const IHostBindingCatalog* const Bindings,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess,
	    const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory)
	{
		return RunFile(FilePath, OutputStream, ErrorStream, ImportPaths, ScriptArgs, bUseBytecodeCache, InputStream, Defines, BytecodeCacheDirectory, StdlibSearchPaths, ExecutionBackend, Bindings, PredefinedMacros, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess, JitBackendFactory, {});
	}

	FRunResult VeroScriptEngine::RunFile(
	    const std::string_view FilePath,
	    std::ostream& OutputStream,
	    std::ostream& ErrorStream,
	    const std::vector<std::string_view>& ImportPaths,
	    const std::vector<std::string_view>& ScriptArgs,
	    const bool bUseBytecodeCache,
	    std::istream* const InputStream,
	    const std::vector<std::string_view>& Defines,
	    const std::string_view BytecodeCacheDirectory,
	    const std::vector<std::string_view>& StdlibSearchPaths,
	    const EExecutionBackend ExecutionBackend,
	    const IHostBindingCatalog* const Bindings,
	    const std::vector<FPredefinedMacro>& PredefinedMacros,
	    const std::vector<std::string_view>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess,
	    const std::shared_ptr<const IJitBackendFactory>& JitBackendFactory,
	    const std::shared_ptr<const IVmBackendFactory>& VmBackendFactory)
	{
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		if (IsApplicationFile(FilePath))
		{
			FEngineConfig Config;
			Config.ImportPaths = ImportPaths;
			Config.ScriptArgs = ScriptArgs;
			Config.bUseBytecodeCache = bUseBytecodeCache;
			Config.InputStream = InputStream;
			Config.Defines = Defines;
			Config.BytecodeCacheDirectory = BytecodeCacheDirectory;
			Config.StdlibSearchPaths = StdlibSearchPaths;
			Config.ExecutionBackend = ExecutionBackend;
			Config.PredefinedMacros = PredefinedMacros;
			Config.ImplicitImportPaths = ImplicitImportPaths;
			Config.bAllowCircularImports = bAllowCircularImports;
			Config.bAllowProjectWideAccess = bAllowProjectWideAccess;
			Config.JitBackendFactory = JitBackendFactory;
			Config.VmBackendFactory = VmBackendFactory;
			if (Bindings)
			{
				FModuleEnvironmentDesc D;
				D.Bindings = std::shared_ptr<const IHostBindingCatalog>(Bindings, [](const IHostBindingCatalog*)
				{
				});
				std::string E;
				Config.ModuleEnvironment = FModuleEnvironment::Create(std::move(D), E);
				if (!Config.ModuleEnvironment)
				{
					FRunResult R;
					R.ExitCode = 2;
					R.Error.Message = E;
					return R;
				}
			}
			FStreamEngineHost Host(OutputStream, ErrorStream);
			VeroScriptSession Session(Host);
			std::string ConfigurationError;
			if (!Session.ApplyConfig(Config, &ConfigurationError))
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error.Message = std::move(ConfigurationError);
				return Result;
			}
			const auto Loaded = Session.LoadFile(FilePath);
			return Loaded.bSuccess ? WithCompileDiagnostics(Session.Run(), Loaded) : Loaded;
		}
		#endif
		FEngineConfig VmConfig;
		VmConfig.VmBackendFactory = VmBackendFactory;
		FScriptCompileExtensionsOptions VmCompileConfig;
		VmCompileConfig.VmBackendFactory = VmBackendFactory;
		std::unique_ptr<FBytecodeModule> BytecodeModule;
		const std::vector<FNativeFunction>* const Natives =
		    Bindings != nullptr ? &Bindings->GetNativeFunctions() : nullptr;
		const FRunResult CompileResult = EngineInternal::CompileFileToModule(
		    FilePath,
		    ImportPaths,
		    bUseBytecodeCache,
		    ErrorStream,
		    BytecodeModule,
		    Natives,
		    nullptr,
		    &Defines,
		    &PredefinedMacros,
		    BytecodeCacheDirectory,
		    &StdlibSearchPaths,
		    Bindings,
		    &ImplicitImportPaths,
		    bAllowCircularImports,
		    bAllowProjectWideAccess, nullptr, nullptr, &VmCompileConfig);
		if (!CompileResult.bSuccess || !BytecodeModule)
		{
			return CompileResult;
		}

		return WithCompileDiagnostics(EngineInternal::RunBytecodeModule(
		                                  *BytecodeModule,
		                                  OutputStream,
		                                  ErrorStream,
		                                  ERunMode::Script,
		                                  ScriptArgs,
		                                  Natives,
		                                  InputStream,
		                                  nullptr,
		                                  nullptr,
		                                  ExecutionBackend,
		                                  0, nullptr, nullptr, nullptr, 1, nullptr, nullptr, nullptr, nullptr,
		                                  nullptr, nullptr, &JitBackendFactory, {}, nullptr, Bindings, nullptr, nullptr, {}, nullptr, nullptr, {}, &VmConfig),
		    CompileResult);
	}

	FRunResult VeroScriptEngine::RunFile(
	    const std::string_view FilePath,
	    IEngineHost& Host,
	    const FEngineConfig& Config)
	{
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		if (IsApplicationFile(FilePath))
		{
			VeroScriptSession Session(Host);
			std::string ConfigurationError;
			if (!Session.ApplyConfig(Config, &ConfigurationError))
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error.Message = std::move(ConfigurationError);
				return Result;
			}
			const auto Loaded = Session.LoadFile(FilePath);
			return Loaded.bSuccess ? WithCompileDiagnostics(Session.Run(), Loaded) : Loaded;
		}
		#endif
		FHostStreamBridge Bridge(Host);
		const auto ScriptExtensions = CompileExtensionsFor(Config);
		std::unique_ptr<FBytecodeModule> BytecodeModule;
		const FRunResult CompileResult = EngineInternal::CompileFileToModule(
		    FilePath, Config.ImportPaths, Config.bUseBytecodeCache, Bridge.ErrorStream(), BytecodeModule,
		    nullptr, &Config.SourceOverlays, &Config.Defines, &Config.PredefinedMacros,
		    Config.BytecodeCacheDirectory, &Config.StdlibSearchPaths, nullptr, &Config.ImplicitImportPaths,
		    Config.bAllowCircularImports, Config.bAllowProjectWideAccess, &Config.AnnotationProcessing,
		    &Config.CompileProcessing, &ScriptExtensions, Config.ModuleEnvironment);
		if (!CompileResult.bSuccess || !BytecodeModule)
		{
			return CompileResult;
		}

		return WithCompileDiagnostics(
		    EngineInternal::RunBytecodeModule(
		        *BytecodeModule, Bridge.OutputStream(), Bridge.ErrorStream(), ERunMode::Script,
		        Config.ScriptArgs, nullptr, Config.InputStream, nullptr, nullptr, Config.ExecutionBackend,
		        Config.MaxCallDepth, nullptr, nullptr, nullptr, 1, nullptr, &Config.ExecutionLimits,
		        &Config.ExecutionControl, &Config.Capabilities, nullptr, nullptr,
		        Config.JitBackendFactory ? &Config.JitBackendFactory : nullptr, {}, &Config.MemoryLimits,
		        nullptr, &Config.I18nServices, &Config.I18nLimits, Config.ModuleEnvironment, &Config.StandardLibraryLimits, &Config.NumericLimits, Config.AotProgram, &Config),
		    CompileResult);
	}
}
