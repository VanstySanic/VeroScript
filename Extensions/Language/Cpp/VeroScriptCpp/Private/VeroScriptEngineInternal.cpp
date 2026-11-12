// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEngineInternal.h"
#include "VeroScriptCompile.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPlatform.h"

#include "VeroScriptAst.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptJitBuild.h"
#include "VeroScriptLocale.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptVersion.h"
#include "VeroScriptVm.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>

namespace VeroScript::EngineInternal
{
	void WriteError(std::ostream& ErrorStream, const FScriptError& Error)
	{
		Locale::InitializeModuleFromEnvironment("Extensions/Language/Cpp/VeroScriptCpp");
		if (!Error.Location.FileLabel.empty())
		{
			ErrorStream << Error.Location.FileLabel << ':'
			            << Error.Location.Line << ':'
			            << Error.Location.Column << ": ";
		}
		ErrorStream << Locale::FormatDiagnosticMessage(
		                   Error.MessageId,
		                   Error.MessageArgs,
		                   Error.Message)
		            << '\n';
	}

	int GetLoaderFailureExitCode(const FScriptError& Error)
	{
		if (Error.MessageId == "diag.cannot_open_script" || Error.MessageId == "diag.cannot_open_import" || Error.MessageId == "diag.cannot_resolve_import")
		{
			return 2;
		}
		const std::string_view Message = Error.Message;
		if (Message.rfind("cannot open script:", 0) == 0 || Message.rfind("cannot open import:", 0) == 0 || Message.rfind("cannot resolve import:", 0) == 0)
		{
			return 2;
		}
		return 3;
	}

	bool ValidateFilePath(const std::string_view Path, std::ostream& ErrorStream, FRunResult& OutFailure)
	{
		if (Path.find('\0') == std::string_view::npos) return true;
		OutFailure = {};
		OutFailure.ExitCode = 2;
		OutFailure.Error.Message = "file paths cannot contain NUL characters";
		OutFailure.Error.Location.FileLabel = "<file>";
		OutFailure.Diagnostics = {OutFailure.Error};
		WriteError(ErrorStream, OutFailure.Error);
		return false;
	}

	void ApplyImportPaths(
	    VeroScriptModuleLoader& Loader,
	    const std::vector<std::string_view>& ImportPaths)
	{
		for (const std::string_view ImportPath : ImportPaths)
		{
			Loader.AddImportPath(std::string(ImportPath));
		}
	}

	void ApplyImportPaths(
	    VeroScriptModuleLoader& Loader,
	    const std::vector<std::string>& ImportPaths)
	{
		for (const std::string& ImportPath : ImportPaths)
		{
			Loader.AddImportPath(ImportPath);
		}
	}

	void ApplyStdlibSearchRoots(
	    VeroScriptModuleLoader& Loader,
	    const std::vector<std::string_view>& StdlibSearchPaths)
	{
		for (const std::string_view Root : StdlibSearchPaths)
		{
			if (!Root.empty())
			{
				Loader.AddStdlibSearchRoot(std::string(Root));
			}
		}
	}

	void ApplyHostTypeNames(
	    VeroScriptModuleLoader& Loader,
	    const IHostBindingCatalog* const Bindings)
	{
		if (!Bindings)
		{
			return;
		}
		std::unordered_set<std::string> TypeNames;
		for (const FHostTypeDesc& Type : Bindings->GetTypes())
		{
			if (!Type.Name.empty())
			{
				TypeNames.insert(Type.Name);
			}
		}
		for (const FHostTypeAliasDesc& Alias : Bindings->GetTypeAliases())
		{
			if (!Alias.Name.empty())
			{
				TypeNames.insert(Alias.Name);
			}
		}
		for (const FGenericHostTypeTemplateDesc& Template :
		    Bindings->GetGenericTypeTemplates())
		{
			if (!Template.Name.empty())
			{
				TypeNames.insert(Template.Name);
			}
		}
		if (!TypeNames.empty())
		{
			Loader.SetHostTypeNames(std::move(TypeNames));
		}
	}

	bool BindingCatalogAffectsCompilation(
	    const IHostBindingCatalog* const Bindings)
	{
		return Bindings != nullptr && (!Bindings->GetNativeFunctions().empty() || !Bindings->GetFunctions().empty() || !Bindings->GetTypes().empty() || !Bindings->GetTypeAliases().empty() || !Bindings->GetGenericTypeTemplates().empty());
	}

	static void ApplyCompileProcessing(VeroScriptModuleLoader& Loader,
	    const FCompileProcessingOptions* Processing, const FAnnotationProcessingOptions* Annotations,
	    const IHostBindingCatalog* Bindings)
	{
		auto Options = Processing ? *Processing : FCompileProcessingOptions{};
		// Preserve raised budgets for existing annotation-only hosts. An explicitly
		// configured general processor registry/budget continues to govern both APIs.
		if (Annotations)
		{
			const FCompileProcessingOptions Defaults;
			const FAnnotationProcessingOptions AnnotationDefaults;
			if (!Options.Registry && Options.MaxOperations == Defaults.MaxOperations)
			{
				Options.MaxOperations = Annotations->MaxOperations;
			}

			else if (Annotations->MaxOperations != AnnotationDefaults.MaxOperations)
			{
				Options.MaxOperations = std::min(Options.MaxOperations, Annotations->MaxOperations);
			}

			if (!Options.Registry && Options.MaxSourceBytes == Defaults.MaxSourceBytes)
			{
				Options.MaxSourceBytes = Annotations->MaxSourceBytes;
			}

			else if (Annotations->MaxSourceBytes != AnnotationDefaults.MaxSourceBytes)
			{
				Options.MaxSourceBytes = std::min(Options.MaxSourceBytes, Annotations->MaxSourceBytes);
			}
		}
		if (Bindings)
		{
			for (auto Definition : GetAnnotationDefinitions(*Bindings))
			{
				Options.AnnotationDefinitions.push_back(std::move(Definition));
			}
		}

		Loader.SetCompileProcessing(std::move(Options));
	}

	static void FailPreparation(FRunResult& Result, FProgramPreparationResult Preparation, std::ostream& ErrorStream)
	{
		Result.bSuccess = false;
		Result.ExitCode = 3;
		Result.Diagnostics = std::move(Preparation.Diagnostics);
		const auto Primary = std::find_if(Result.Diagnostics.begin(), Result.Diagnostics.end(),
		    [](const auto& Diagnostic)
		{
			return Diagnostic.Severity == ECompileDiagnosticSeverity::Error;
		});
		if (Primary != Result.Diagnostics.end())
		{
			Result.Error = *Primary;
		}

		else
		{
			Result.Error.Message = "declaration preparation failed";
			Result.Diagnostics.push_back(Result.Error);
		}
		for (const auto& Diagnostic : Result.Diagnostics)
		{
			WriteError(ErrorStream, Diagnostic);
		}
	}

	bool CompileProgram(
	    std::unique_ptr<FProgram> Program,
	    std::ostream& ErrorStream,
	    std::unique_ptr<FBytecodeModule>& OutModule,
	    FRunResult& OutResult,
	    const std::vector<FNativeFunction>* const Natives,
	    const IHostBindingCatalog* const Bindings,
	    const FAnnotationProcessingOptions* Processing, const FPreparedProgram* Prepared)
	{
		FProgramPreparationResult Preparation;
		if (!Prepared)
		{
			Preparation = VeroScriptProgramPreparation::Prepare(std::move(Program),
			    Processing ? *Processing : FAnnotationProcessingOptions{}, Natives, Bindings);
			if (!Preparation.Succeeded())
			{
				FailPreparation(OutResult, std::move(Preparation), ErrorStream);
				return false;
			}
			Prepared = Preparation.Prepared.get();
			OutResult.Diagnostics = Preparation.Diagnostics;
		}
		VeroScriptCompiler Compiler;
		OutModule = Compiler.CompilePrepared(*Prepared, Natives, Bindings);
		if (Compiler.HasError() || !OutModule)
		{
			OutResult.bSuccess = false;
			OutResult.ExitCode = 3;
			auto Warnings = std::move(OutResult.Diagnostics);
			OutResult.Diagnostics = Compiler.GetDiagnostics();
			if (OutResult.Diagnostics.empty())
			{
				OutResult.Diagnostics.push_back(Compiler.GetError());
			}
			OutResult.Error = OutResult.Diagnostics.front();
			OutResult.Diagnostics.insert(OutResult.Diagnostics.begin(), Warnings.begin(), Warnings.end());
			for (const FScriptError& Diagnostic : OutResult.Diagnostics)
			{
				WriteError(ErrorStream, Diagnostic);
			}
			return false;
		}
		const auto& Diagnostics = Compiler.GetDiagnostics();
		OutResult.Diagnostics.insert(OutResult.Diagnostics.end(), Diagnostics.begin(), Diagnostics.end());
		return true;
	}

	void ApplySourceOverlays(
	    VeroScriptModuleLoader& Loader,
	    const std::vector<FSourceOverlay>* const SourceOverlays)
	{
		if (SourceOverlays == nullptr)
		{
			return;
		}
		for (const FSourceOverlay& Overlay : *SourceOverlays)
		{
			Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
		}
	}

	void ApplyDefines(
	    VeroScriptModuleLoader& Loader,
	    const std::vector<std::string_view>* const Defines)
	{
		if (Defines == nullptr || Defines->empty())
		{
			return;
		}
		std::vector<std::string> Owned;
		Owned.reserve(Defines->size());
		for (const std::string_view Define : *Defines)
		{
			if (!Define.empty())
			{
				Owned.emplace_back(Define);
			}
		}
		Loader.SetDefines(std::move(Owned));
	}

	void ApplyPredefinedMacros(
	    VeroScriptModuleLoader& Loader,
	    const std::vector<FPredefinedMacro>* const Macros)
	{
		if (Macros != nullptr)
		{
			Loader.SetPredefinedMacros(*Macros);
		}
	}

	void FailFromLoader(FRunResult& Result, VeroScriptModuleLoader& Loader, std::ostream& ErrorStream)
	{
		Result.Diagnostics = Loader.GetDiagnostics();
		if (Result.Diagnostics.empty())
		{
			if (Loader.HasError())
			{
				Result.Diagnostics.push_back(Loader.GetError());
			}
			else
			{
				Result.Diagnostics.push_back(
				    MakeDiagnostic("diag.failed_to_load_module"));
			}
		}
		const auto Primary = std::find_if(Result.Diagnostics.begin(), Result.Diagnostics.end(),
		    [](const auto& D)
		{
			return D.Severity == ECompileDiagnosticSeverity::Error;
		});
		Result.Error = Primary == Result.Diagnostics.end() ? Result.Diagnostics.front() : *Primary;
		Result.ExitCode = Loader.HasError() ? GetLoaderFailureExitCode(Result.Error) : 2;
		for (const FScriptError& Diagnostic : Result.Diagnostics)
		{
			WriteError(ErrorStream, Diagnostic);
		}
	}

	static FRunResult CompileLoadedProgram(
	    VeroScriptModuleLoader& Loader,
	    std::unique_ptr<FProgram> Program,
	    const std::string_view FilePath,
	    const std::vector<std::string_view>& ImportPaths,
	    const bool bUseBytecodeCache,
	    const std::string_view BytecodeCacheDirectory,
	    std::ostream& ErrorStream,
	    std::unique_ptr<FBytecodeModule>& OutModule,
	    const std::vector<FNativeFunction>* const Natives,
	    const IHostBindingCatalog* Bindings,
	    const FAnnotationProcessingOptions* Processing = nullptr)
	{
		// Empty session catalogs have no dispatch ownership to preserve. Their
		// allocation addresses must not invalidate an otherwise reusable cache.
		if (!BindingCatalogAffectsCompilation(Bindings))
		{
			Bindings = nullptr;
		}

		FRunResult Result;
		FProgramPreparationResult Preparation;
		std::string InputFingerprint;
		const bool bCanReadBeforePreparation = bUseBytecodeCache && Program && VeroScriptProgramPreparation::TryGetCacheInputFingerprint(*Program, Processing ? *Processing : FAnnotationProcessingOptions{}, InputFingerprint, Natives, Bindings);
		const auto Prepare = [&]()
		{
			Preparation = VeroScriptProgramPreparation::Prepare(std::move(Program),
			    Processing ? *Processing : FAnnotationProcessingOptions{}, Natives, Bindings);
			if (!Preparation.Succeeded())
			{
				FailPreparation(Result, std::move(Preparation), ErrorStream);
				return false;
			}
			Result.Diagnostics = Preparation.Diagnostics;
			return true;
		};
		if (!bCanReadBeforePreparation && !Prepare())
		{
			return Result;
		}
		// Defines are already folded into Loader fingerprint (normalized, once per load).
		std::uint64_t SourceFingerprint = Loader.GetSourceFingerprint();
		// Cached code must not bypass stricter declaration/default-argument
		// validation while the public product/bytecode versions remain 0.1.1.
		SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, "declaration-preparation-6");
		SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, "compile-processing-source-metadata-1");
		if (bCanReadBeforePreparation)
		{
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, InputFingerprint);
		}
		else if (Preparation.Prepared)
		{
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, Preparation.Prepared->GetFingerprint());
		}

		for (const std::string_view ImportPath : ImportPaths)
		{
			SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, ImportPath);
		}
		const std::uint64_t ModuleResolutionFingerprint = Loader.GetModuleResolutionFingerprint();
		SourceFingerprint = Runtime::Fnv1a64(
		    SourceFingerprint,
		    &ModuleResolutionFingerprint,
		    sizeof(ModuleResolutionFingerprint));
		const auto HashVersion = [&](const FSemanticVersion& Version)
		{
			SourceFingerprint =
			    Runtime::Fnv1a64(SourceFingerprint, &Version.Major, sizeof(Version.Major));
			SourceFingerprint =
			    Runtime::Fnv1a64(SourceFingerprint, &Version.Minor, sizeof(Version.Minor));
			SourceFingerprint =
			    Runtime::Fnv1a64(SourceFingerprint, &Version.Patch, sizeof(Version.Patch));
		};
		HashVersion(VEROSCRIPT_BC_VERSION);
		HashVersion(VEROSCRIPT_VERSION);
		// Compiler changes must expire cached code even when product/bytecode
		// versions stay unchanged. These IDs are generated from the Core sources.
		SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, VEROSCRIPT_CORE_IMPLEMENTATION_ID);
		SourceFingerprint = Runtime::Fnv1a64(SourceFingerprint, VEROSCRIPT_JIT_PUBLIC_ABI);
		std::string ResolvedCacheDirectory;
		bool bUsingPlatformDefaultCache = false;
		if (!BytecodeCacheDirectory.empty())
		{
			ResolvedCacheDirectory.assign(BytecodeCacheDirectory);
		}
		else if (!GetHostPlatformInfo().bMobile)
		{
			if (const auto EnvCache = Environment::GetUtf8("VERO_CACHE_DIR");
			    EnvCache && !EnvCache->empty())
			{
				ResolvedCacheDirectory = *EnvCache;
			}
			if (ResolvedCacheDirectory.empty())
			{
				ResolvedCacheDirectory = Runtime::GetUserCacheDirectory();
				bUsingPlatformDefaultCache = !ResolvedCacheDirectory.empty();
			}
		}
		const std::string CachePath = VeroScriptBytecodeIO::GetCachePathForScript(
		    FilePath,
		    ResolvedCacheDirectory);
		const std::string LocalFallbackCachePath = bUsingPlatformDefaultCache
		    ? VeroScriptBytecodeIO::GetCachePathForScript(FilePath)
		    : std::string{};
		// The cache fingerprint does not encode an arbitrary host catalog. Reusing a
		// module compiled with different functions, types, aliases, or generic host
		// templates can silently retain stale overload and runtime-type decisions.
		const bool bAllowCache = bUseBytecodeCache && (!Preparation.Prepared || Preparation.Prepared->IsCacheable()) && (!GetHostPlatformInfo().bMobile || !ResolvedCacheDirectory.empty()) && (!Natives || Natives->empty()) && !BindingCatalogAffectsCompilation(Bindings);
		if (bAllowCache)
		{
			std::string CacheError;
			OutModule = VeroScriptBytecodeIO::ReadFile(CachePath, SourceFingerprint, CacheError);
			// Accept an existing project-local cache during migration and when the
			// platform user-cache directory is temporarily unavailable.
			if (!OutModule && !LocalFallbackCachePath.empty())
			{
				OutModule = VeroScriptBytecodeIO::ReadFile(
				    LocalFallbackCachePath,
				    SourceFingerprint,
				    CacheError);
			}
		}

		if (!OutModule)
		{
			if (!Preparation.Prepared && !Prepare())
			{
				return Result;
			}
			if (!CompileProgram(nullptr, ErrorStream, OutModule, Result, Natives, Bindings, Processing, Preparation.Prepared.get()))
			{
				return Result;
			}

			if (bAllowCache && Preparation.Prepared->IsCacheable())
			{
				std::string CacheWriteError;
				bool bCacheWritten =
				    VeroScriptBytecodeIO::WriteFile(
				        CachePath, *OutModule, SourceFingerprint, CacheWriteError);
				if (!bCacheWritten && !LocalFallbackCachePath.empty())
				{
					bCacheWritten = VeroScriptBytecodeIO::WriteFile(
					    LocalFallbackCachePath,
					    *OutModule,
					    SourceFingerprint,
					    CacheWriteError);
				}
				if (!bCacheWritten)
				{
					ErrorStream << FilePath << ": warning: failed to write bytecode cache";
					if (!CacheWriteError.empty())
					{
						ErrorStream << ": " << CacheWriteError;
					}
					ErrorStream << '\n';
				}
			}
		}

		for (const auto& Diagnostic : Result.Diagnostics)
		{
			WriteError(ErrorStream, Diagnostic);
		}

		Result.bSuccess = true;
		Result.ExitCode = 0;
		return Result;
	}

	FRunResult CompileSourceToModule(const std::string_view Source, const std::string_view FileLabel,
	    const std::vector<std::string_view>& ImportPaths,
	    std::ostream& ErrorStream, std::unique_ptr<FBytecodeModule>& OutModule,
	    const std::vector<FNativeFunction>* const Natives,
	    const std::vector<FSourceOverlay>* const SourceOverlays,
	    const std::vector<std::string_view>* const Defines,
	    const std::vector<FPredefinedMacro>* const PredefinedMacros,
	    const std::vector<std::string_view>* const StdlibSearchPaths,
	    const IHostBindingCatalog* const Bindings,
	    const std::vector<std::string_view>* const ImplicitImportPaths,
	    const bool bAllowCircularImports, const bool bAllowProjectWideAccess,
	    const bool bUseBytecodeCache,
	    const std::string_view BytecodeCacheDirectory,
	    const FAnnotationProcessingOptions* Processing,
	    const FCompileProcessingOptions* CompileProcessing,
	    const FScriptCompileExtensionsOptions* ScriptExtensions,
	    std::shared_ptr<const FModuleEnvironment> Environment)
	{

		if (Environment)
		{
			FCompileOptions O;
			O.ModuleEnvironment = Environment;
			O.ImportPaths = ImportPaths;
			if (Defines)
			{
				O.Defines = *Defines;
			}
			if (PredefinedMacros)
			{
				O.PredefinedMacros = *PredefinedMacros;
			}
			if (SourceOverlays)
			{
				O.SourceOverlays = *SourceOverlays;
			}
			if (StdlibSearchPaths)
			{
				O.StdlibSearchPaths = *StdlibSearchPaths;
			}
			if (ImplicitImportPaths)
			{
				O.ImplicitImportPaths = *ImplicitImportPaths;
			}
			O.bAllowCircularImports = bAllowCircularImports;
			O.bAllowProjectWideAccess = bAllowProjectWideAccess;
			O.Bindings = Bindings;
			O.Natives = Natives;
			if (Processing)
			{
				O.AnnotationProcessing = *Processing;
			}
			if (CompileProcessing)
			{
				O.CompileProcessing = *CompileProcessing;
			}
			if (ScriptExtensions)
			{
				O.ScriptCompileExtensions = *ScriptExtensions;
			}
			auto C = VeroScriptCompile::CompileSource(Source, FileLabel, O);
			FRunResult R;
			R.bSuccess = C.bSuccess;
			R.ExitCode = C.bSuccess ? 0 : 3;
			R.Error = C.Error;
			R.Diagnostics = std::move(C.Diagnostics);
			OutModule = std::move(C.Module);
			for (const auto& D : R.Diagnostics)
			{
				WriteError(ErrorStream, D);
			}
			return R;
		}
		Locale::InitializeModuleFromEnvironment("Extensions/Language/Cpp/VeroScriptCpp");
		FRunResult Result;
		if (bUseBytecodeCache && !ValidateFilePath(BytecodeCacheDirectory, ErrorStream, Result))
		{
			OutModule.reset();
			return Result;
		}

		VeroScriptModuleLoader Loader("");
		ApplyCompileProcessing(Loader, CompileProcessing, Processing, Bindings);
		if (ScriptExtensions)
		{
			Loader.SetScriptCompileExtensions(*ScriptExtensions);
		}

		ApplyImportPaths(Loader, ImportPaths);
		if (ImplicitImportPaths != nullptr)
		{
			for (const std::string_view Path : *ImplicitImportPaths)
			{
				Loader.AddImplicitImportPath(std::string(Path));
			}
		}
		Loader.SetAllowCircularImports(bAllowCircularImports);
		Loader.SetAllowProjectWideAccess(bAllowProjectWideAccess);
		if (StdlibSearchPaths != nullptr)
		{
			ApplyStdlibSearchRoots(Loader, *StdlibSearchPaths);
		}
		ApplySourceOverlays(Loader, SourceOverlays);
		ApplyDefines(Loader, Defines);
		ApplyPredefinedMacros(Loader, PredefinedMacros);
		ApplyHostTypeNames(Loader, Bindings);
		std::unique_ptr<FProgram> Program = Loader.LoadSourceModule(Source, FileLabel);
		if (Loader.HasError() || !Program)
		{
			FailFromLoader(Result, Loader, ErrorStream);
			if (!Loader.HasError() && Result.Error.MessageId == "diag.failed_to_load_module")
			{
				FillDiagnostic(Result.Error, "diag.failed_to_load_source_module");
				if (!Result.Diagnostics.empty())
				{
					Result.Diagnostics.front() = Result.Error;
				}
			}
			return Result;
		}

		// Source execution opts into persistent storage only with an explicit root.
		// Reuse the file path's fingerprint, validation and safe recompilation logic
		// without replacing the live session or its cancellation token.
		if (bUseBytecodeCache && !BytecodeCacheDirectory.empty())
		{
			return CompileLoadedProgram(Loader, std::move(Program), FileLabel, ImportPaths, true,
			    BytecodeCacheDirectory, ErrorStream, OutModule, Natives, Bindings, Processing);
		}
		if (!CompileProgram(std::move(Program), ErrorStream, OutModule, Result, Natives, Bindings, Processing))
		{
			return Result;
		}

		for (const auto& Diagnostic : Result.Diagnostics)
		{
			WriteError(ErrorStream, Diagnostic);
		}

		Result.bSuccess = true;
		Result.ExitCode = 0;
		return Result;
	}

	FRunResult CompileFileToModule(const std::string_view FilePath,
	    const std::vector<std::string_view>& ImportPaths,
	    const bool bUseBytecodeCache, std::ostream& ErrorStream,
	    std::unique_ptr<FBytecodeModule>& OutModule,
	    const std::vector<FNativeFunction>* const Natives,
	    const std::vector<FSourceOverlay>* const SourceOverlays,
	    const std::vector<std::string_view>* const Defines,
	    const std::vector<FPredefinedMacro>* const PredefinedMacros,
	    const std::string_view BytecodeCacheDirectory,
	    const std::vector<std::string_view>* const StdlibSearchPaths,
	    const IHostBindingCatalog* const Bindings,
	    const std::vector<std::string_view>* const ImplicitImportPaths,
	    const bool bAllowCircularImports, const bool bAllowProjectWideAccess,
	    const FAnnotationProcessingOptions* Processing,
	    const FCompileProcessingOptions* CompileProcessing,
	    const FScriptCompileExtensionsOptions* ScriptExtensions,
	    std::shared_ptr<const FModuleEnvironment> Environment)
	{
		FRunResult PathFailure;
		if (!ValidateFilePath(FilePath, ErrorStream, PathFailure))
		{
			OutModule.reset();
			return PathFailure;
		}

		if (Environment)
		{
			FCompileOptions O;
			O.ModuleEnvironment = Environment;
			O.ImportPaths = ImportPaths;
			if (Defines)
			{
				O.Defines = *Defines;
			}
			if (PredefinedMacros)
			{
				O.PredefinedMacros = *PredefinedMacros;
			}
			if (SourceOverlays)
			{
				O.SourceOverlays = *SourceOverlays;
			}
			if (StdlibSearchPaths)
			{
				O.StdlibSearchPaths = *StdlibSearchPaths;
			}
			if (ImplicitImportPaths)
			{
				O.ImplicitImportPaths = *ImplicitImportPaths;
			}
			O.bAllowCircularImports = bAllowCircularImports;
			O.bAllowProjectWideAccess = bAllowProjectWideAccess;
			O.Bindings = Bindings;
			O.Natives = Natives;
			if (Processing)
			{
				O.AnnotationProcessing = *Processing;
			}
			if (CompileProcessing)
			{
				O.CompileProcessing = *CompileProcessing;
			}
			if (ScriptExtensions)
			{
				O.ScriptCompileExtensions = *ScriptExtensions;
			}
			auto C = VeroScriptCompile::CompileFile(FilePath, O);
			FRunResult R;
			R.bSuccess = C.bSuccess;
			R.ExitCode = C.bSuccess ? 0 : 3;
			R.Error = C.Error;
			R.Diagnostics = std::move(C.Diagnostics);
			OutModule = std::move(C.Module);
			for (const auto& D : R.Diagnostics)
			{
				WriteError(ErrorStream, D);
			}
			return R;
		}
		Locale::InitializeModuleFromEnvironment("Extensions/Language/Cpp/VeroScriptCpp");
		FRunResult Result;
		if (bUseBytecodeCache && !ValidateFilePath(BytecodeCacheDirectory, ErrorStream, Result))
		{
			OutModule.reset();
			return Result;
		}
		VeroScriptModuleLoader Loader(FilePath);
		ApplyCompileProcessing(Loader, CompileProcessing, Processing, Bindings);
		if (ScriptExtensions)
		{
			Loader.SetScriptCompileExtensions(*ScriptExtensions);
		}

		ApplyImportPaths(Loader, ImportPaths);
		if (ImplicitImportPaths != nullptr)
		{
			for (const std::string_view Path : *ImplicitImportPaths)
			{
				Loader.AddImplicitImportPath(std::string(Path));
			}
		}
		Loader.SetAllowCircularImports(bAllowCircularImports);
		Loader.SetAllowProjectWideAccess(bAllowProjectWideAccess);
		if (StdlibSearchPaths != nullptr)
		{
			ApplyStdlibSearchRoots(Loader, *StdlibSearchPaths);
		}
		ApplySourceOverlays(Loader, SourceOverlays);
		ApplyDefines(Loader, Defines);
		ApplyPredefinedMacros(Loader, PredefinedMacros);
		ApplyHostTypeNames(Loader, Bindings);
		std::unique_ptr<FProgram> Program = Loader.LoadMainModule();
		if (Loader.HasError() || !Program)
		{
			FailFromLoader(Result, Loader, ErrorStream);
			return Result;
		}

		return CompileLoadedProgram(Loader, std::move(Program), FilePath, ImportPaths,
		    bUseBytecodeCache, BytecodeCacheDirectory, ErrorStream, OutModule, Natives, Bindings, Processing);
	}

	FRunResult RunBytecodeModule(
	    const FBytecodeModule& Module, std::ostream& OutputStream, std::ostream& ErrorStream,
	    const ERunMode RunMode, const std::vector<std::string_view>& ScriptArgs,
	    const std::vector<FNativeFunction>* const Natives, std::istream* const InputStream,
	    FHotReloadState* const HotReloadState, FHotReloadRollback* const HotReloadRollback,
	    const EExecutionBackend ExecutionBackend, const std::size_t MaxCallDepth,
	    const FJitPolicy* const JitPolicy, FJitStats* const OutJitStats,
	    const FInstanceCallInterceptor* const InstanceCallInterceptor, const std::uint64_t ProgramGeneration,
	    const FMutationCallback* const MutationCallback, const FExecutionLimits* const ExecutionLimits,
	    const std::shared_ptr<FExecutionControl>* const ExecutionControl,
	    const FScriptCapabilities* const Capabilities, const std::shared_ptr<const void>* const ProgramOwner,
	    VeroScriptDebugger* const Debugger,
	    const std::shared_ptr<const IJitBackendFactory>* JitBackendFactory,
	    std::shared_ptr<const FBytecodeModule> JitModuleOwner, const FMemoryLimits* MemoryLimits,
	    const IHostBindingCatalog* MemoryBindings, const std::shared_ptr<I18n::FI18nServices>* I18nServices,
	    const I18n::FI18nLimits* I18nLimits, std::shared_ptr<const FModuleEnvironment> Environment,
	    const FStandardLibraryLimits* StandardLibraryLimits,
	    const FNumericLimits* NumericLimits, std::shared_ptr<const IAotProgram> AotProgram,
	    const FEngineConfig* VmConfig)
	{
		FRunResult Result;
		FEngineConfig SelectedBackends;
		if ((VmConfig && (VmConfig->FallbackVmBackendFactory || !VmConfig->FallbackVmBackendPath.empty())) || (Environment && (Environment->GetDescription().ExecutionBackends.Vm || Environment->GetDescription().ExecutionBackends.Jit)))
		{
			auto Request = VmConfig ? *VmConfig : FEngineConfig{};
			Request.ModuleEnvironment = Environment;
			Request.ExecutionBackend = ExecutionBackend;
			if (JitBackendFactory)
			{
				Request.JitBackendFactory = *JitBackendFactory;
			}
			std::string Failure;
			if (!ResolveExecutionBackends(Request, SelectedBackends, Failure))
			{
				Result.ExitCode = 1;
				FillFromEnglishMessage(Result.Error, std::move(Failure));
				WriteError(ErrorStream, Result.Error);
				return Result;
			}
			VmConfig = &SelectedBackends;
			JitBackendFactory = &SelectedBackends.JitBackendFactory;
		}
		if (VmConfig && VmConfig->bDisableJitBackend)
		{
			JitBackendFactory = nullptr;
		}
		FNumericLimits NormalizedNumericLimits;
		FNumericError NumericError;
		if (NumericLimits && !Numerics::ValidateLimits(*NumericLimits, NormalizedNumericLimits, NumericError))
		{
			Result.ExitCode = 1;
			Result.Error.MessageId = NumericError.MessageId;
			Result.Error.MessageArgs = {NumericError.Reason ? NumericError.Reason : ""};
			Result.Error.Message = Locale::FormatEnglish(Result.Error.MessageId, {Result.Error.MessageArgs.front()});
			Result.Diagnostics = {Result.Error};
			WriteError(ErrorStream, Result.Error);
			return Result;
		}
		VeroScriptVm Vm(
		    OutputStream,
		    InputStream != nullptr ? *InputStream : std::cin,
		    ErrorStream);
		std::string AotError;
		if (VmConfig)
		{
			if ((VmConfig->VmBackendFactory && !VmConfig->VmBackendPath.empty()) || (VmConfig->bClearVmBackend && (VmConfig->VmBackendFactory || !VmConfig->VmBackendPath.empty())))
			{
				Result.ExitCode = 1;
				FillFromEnglishMessage(Result.Error, "VM factory, path and clear settings are exclusive");
				return Result;
			}
			const bool Ok = VmConfig->VmBackendFactory
			    ? Vm.SetVmBackendFactory(VmConfig->VmBackendFactory, AotError)
			    : Vm.SetVmBackendPath(VmConfig->VmBackendPath, AotError);
			if (!Ok)
			{
				Result.ExitCode = 1;
				FillFromEnglishMessage(Result.Error, std::move(AotError));
				return Result;
			}
		}
		if (!Vm.SetAotProgram(std::move(AotProgram), AotError))
		{
			Result.ExitCode = 1;
			FillFromEnglishMessage(Result.Error, std::move(AotError));
			return Result;
		}

		if (JitBackendFactory)
		{
			std::string ConfigurationError;
			if (!Vm.SetJitBackendFactory(*JitBackendFactory, ConfigurationError) || !Vm.SetJitProgramOwner(std::move(JitModuleOwner), ConfigurationError))
			{
				Result.ExitCode = 1;
				FillFromEnglishMessage(Result.Error, std::move(ConfigurationError));
				Result.Diagnostics = {Result.Error};
				WriteError(ErrorStream, Result.Error);
				return Result;
			}
		}
		Vm.SetMaxCallDepth(MaxCallDepth);
		if (StandardLibraryLimits)
		{
			Vm.SetStandardLibraryLimits(*StandardLibraryLimits);
		}
		if (NumericLimits)
		{
			Vm.SetNumericLimits(NormalizedNumericLimits);
		}
		std::string EnvironmentError;
		if (!Vm.SetModuleEnvironment(Environment, EnvironmentError))
		{
			Result.ExitCode = 1;
			FillFromEnglishMessage(Result.Error, std::move(EnvironmentError));
			WriteError(ErrorStream, Result.Error);
			return Result;
		}
		Vm.SetMemoryHostBindings(MemoryBindings ? MemoryBindings
		        : Environment                   ? Environment->GetDescription().Bindings.get()
		                                        : nullptr);
		if ((I18nServices && *I18nServices) || !Environment || (I18nLimits && I18nLimits->MaxOutputBytes))
		{
			Vm.SetI18nServices(I18nServices && *I18nServices ? *I18nServices
			        : Environment                            ? Vm.GetI18nServices()
			                                                 : nullptr,
			    I18nLimits ? *I18nLimits : I18n::FI18nLimits{});
		}
		if (MemoryLimits && (MemoryLimits->MaxBufferBytes || MemoryLimits->MaxRetainedBytes))
		{
			Vm.SetMemoryContext(std::make_shared<FMemoryContext>(*MemoryLimits), true);
		}
		if (ExecutionLimits)
		{
			Vm.SetExecutionLimits(*ExecutionLimits);
		}
		if (ExecutionControl)
		{
			Vm.SetExecutionControl(*ExecutionControl);
		}
		if (Capabilities)
		{
			Vm.SetCapabilities(*Capabilities);
		}
		if (JitPolicy)
		{
			Vm.SetJitPolicy(*JitPolicy);
		}
		Vm.SetExecutionBackend(ExecutionBackend);
		Vm.SetDebugger(Debugger);
		if (InstanceCallInterceptor)
		{
			Vm.SetInstanceCallInterceptor(*InstanceCallInterceptor);
		}
		if (MutationCallback)
		{
			Vm.SetMutationCallback(*MutationCallback);
		}
		Vm.SetProgramGeneration(ProgramGeneration);
		if (ProgramOwner)
		{
			Vm.SetProgramOwner(*ProgramOwner);
		}
		const bool bExecuted = HotReloadState != nullptr
		    ? Vm.ExecutePersistent(
		          Module,
		          RunMode,
		          *HotReloadState,
		          HotReloadRollback,
		          ScriptArgs,
		          Natives)
		    : Vm.Execute(Module, RunMode, ScriptArgs, Natives);
		if (OutJitStats)
		{
			*OutJitStats = Vm.GetJitStats();
		}
		if (!bExecuted)
		{
			Result.ExitCode = 1;
			Result.Error = Vm.GetError();
			Result.Diagnostics = {Result.Error};
			const auto& Cleanup = Vm.GetCleanupDiagnostics();
			Result.Diagnostics.insert(Result.Diagnostics.end(), Cleanup.begin(), Cleanup.end());
			if (Vm.RequiresReset())
			{
				Result.Diagnostics.push_back(MakeDiagnostic("diag.execution_state_reset_required"));
			}
			for (const auto& Diagnostic : Result.Diagnostics)
			{
				WriteError(ErrorStream, Diagnostic);
			}
			return Result;
		}

		Result.bSuccess = true;
		Result.bExitRequested = Vm.WasExitRequested();
		Result.ExitCode = Vm.GetProcessExitCode();
		return Result;
	}
}
