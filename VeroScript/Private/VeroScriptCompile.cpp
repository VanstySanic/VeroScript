// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompile.h"
#include "ConstantEvaluation/VeroScriptConstantRequest.h"

#include "VeroScriptAst.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"

#include <unordered_set>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace VeroScript
{
	namespace
	{
		FCompileResult Fail(std::vector<FScriptError> Diagnostics)
		{
			FCompileResult Result;
			if (Diagnostics.empty())
			{
				Diagnostics.push_back(MakeDiagnostic("diag.compile_failed"));
			}
			const auto Primary = std::find_if(Diagnostics.begin(), Diagnostics.end(),
			    [](const auto& E)
			{
				return E.Severity == ECompileDiagnosticSeverity::Error;
			});
			Result.Error = Primary == Diagnostics.end() ? Diagnostics.front() : *Primary;
			Result.Diagnostics = std::move(Diagnostics);
			return Result;
		}

		FCompileResult Fail(FScriptError Error)
		{
			return Fail(std::vector<FScriptError>{std::move(Error)});
		}

		void ApplyOverlays(VeroScriptModuleLoader& Loader, const FCompileOptions& Options)
		{
			for (const FSourceOverlay& Overlay : Options.SourceOverlays)
			{
				Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
			}
		}

		void ApplyDefines(VeroScriptModuleLoader& Loader, const FCompileOptions& Options)
		{
			std::vector<std::string> Defines;
			Defines.reserve(Options.Defines.size());
			for (const std::string_view Define : Options.Defines)
			{
				if (!Define.empty())
				{
					Defines.emplace_back(Define);
				}
			}
			Loader.SetDefines(std::move(Defines));
			Loader.SetPredefinedMacros(Options.PredefinedMacros);
		}

		void ApplyHostTypeNames(VeroScriptModuleLoader& Loader, const IHostBindingCatalog* Bindings)
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

		FCompileResult FailFromLoader(
		    VeroScriptModuleLoader& Loader,
		    const std::string_view FallbackMessageId)
		{
			std::vector<FScriptError> Diagnostics = Loader.GetDiagnostics();
			if (Diagnostics.empty())
			{
				if (Loader.HasError())
				{
					Diagnostics.push_back(Loader.GetError());
				}
				else
				{
					Diagnostics.push_back(MakeDiagnostic(FallbackMessageId));
				}
			}
			return Fail(std::move(Diagnostics));
		}

		const std::vector<FNativeFunction>* ResolveNatives(const FCompileOptions& Options)
		{
			if (Options.Bindings)
			{
				return &Options.Bindings->GetNativeFunctions();
			}
			return Options.Natives;
		}

		FCompileResult CompileProgram(
		    std::unique_ptr<FProgram> Program,
		    const FAnnotationProcessingOptions& Processing,
		    const std::vector<FNativeFunction>* Natives,
		    const IHostBindingCatalog* Bindings, const FConstantEvaluationLimits& Limits)
		{
			if (Natives)
			{
				std::unordered_set<std::string> Signatures;
				for (const FNativeFunction& Native : *Natives)
				{
					if (!Signatures.insert(std::to_string(Native.Name.size()) + ':' + Native.Name + NativeParameterSignature(Native)).second)
					{
						return Fail(MakeDiagnostic("diag.registernative_duplicate_name", {}, {Native.Name}));
					}
					if (!NativeParamFlagsAreValid(Native))
					{
						return Fail(MakeDiagnostic(
						    "diag.invalid_native_parameter_flags",
						    {},
						    {Native.Name}));
					}
					if (!NativeFunctionContainerMetadataIsValid(Native))
					{
						return Fail(MakeDiagnostic(
						    "diag.invalid_native_container_metadata",
						    {},
						    {Native.Name}));
					}
				}
			}
			Program->ConstantEvaluationLimits = CurrentConstantEvaluationRequest() ? CurrentConstantEvaluationRequest()->Limits : Limits;
			auto Preparation = VeroScriptProgramPreparation::Prepare(std::move(Program), Processing, Natives, Bindings);
			if (!Preparation.Succeeded()) return Fail(std::move(Preparation.Diagnostics));
			VeroScriptCompiler Compiler;
			std::unique_ptr<FBytecodeModule> Module = Compiler.CompilePrepared(*Preparation.Prepared, Natives, Bindings);
			if (Compiler.HasError() || !Module)
			{
				std::vector<FScriptError> Diagnostics = Compiler.GetDiagnostics();
				if (Diagnostics.empty())
				{
					Diagnostics.push_back(Compiler.GetError());
				}
				Diagnostics.insert(Diagnostics.begin(), Preparation.Diagnostics.begin(), Preparation.Diagnostics.end());
				return Fail(std::move(Diagnostics));
			}
			FCompileResult Result;
			Result.bSuccess = true;
			Result.Module = std::move(Module);
			Result.PreparedProgram = std::move(Preparation.Prepared);
			Result.Diagnostics = std::move(Preparation.Diagnostics);
			return Result;
		}
	}

	FCompileResult VeroScriptCompile::CompileSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    const FCompileOptions& Options)
	{
		FConstantEvaluationRequestScope ConstantRequest(Options.ConstantEvaluationLimits);
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!Options.ModuleEnvironment->Apply(Effective, Failure))
			{
				FScriptError E;
				E.Message = Failure;
				return Fail(std::move(E));
			}
			return CompileSource(Source, FileLabel, Effective);
		}
		VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
		auto Processing = Options.CompileProcessing;
		if (Options.Bindings)
		{
			for (auto Definition : GetAnnotationDefinitions(*Options.Bindings))
			{
				Processing.AnnotationDefinitions.push_back(std::move(Definition));
			}
		}

		Loader.SetScriptCompileExtensions(Options.ScriptCompileExtensions);
		Loader.SetCompileProcessing(std::move(Processing));
		Loader.SetBuildPreprocessLineMaps(Options.bBuildPreprocessLineMaps);
		for (const std::string_view StdlibPath : Options.StdlibSearchPaths)
		{
			Loader.AddStdlibSearchRoot(std::string(StdlibPath));
		}
		for (const std::string_view ImportPath : Options.ImportPaths)
		{
			Loader.AddImportPath(std::string(ImportPath));
		}
		for (const std::string_view ImplicitImportPath : Options.ImplicitImportPaths)
		{
			Loader.AddImplicitImportPath(std::string(ImplicitImportPath));
		}
		Loader.SetAllowCircularImports(Options.bAllowCircularImports);
		Loader.SetAllowProjectWideAccess(Options.bAllowProjectWideAccess);
		ApplyOverlays(Loader, Options);
		ApplyDefines(Loader, Options);
		ApplyHostTypeNames(Loader, Options.Bindings);
		std::unique_ptr<FProgram> Program = Loader.LoadSourceModule(Source, FileLabel);
		if (Loader.HasError() || !Program)
		{
			return FailFromLoader(Loader, "diag.failed_to_load_source_module");
		}
		auto Result = CompileProgram(std::move(Program), Options.AnnotationProcessing, ResolveNatives(Options), Options.Bindings, Options.ConstantEvaluationLimits);
		Result.LoadedModulePaths.assign(Loader.GetLoadedModulePaths().begin(), Loader.GetLoadedModulePaths().end());
		std::sort(Result.LoadedModulePaths.begin(), Result.LoadedModulePaths.end());
		if (Options.bBuildPreprocessLineMaps)
		{
			for (const auto& Path : Loader.GetLoadedModulePaths())
			{
				if (const auto Map = Loader.GetPreprocessLineMap(Path))
				{
					Result.PreprocessLineMaps.emplace(PathUtil::IdentityKey(Path), *Map);
				}
			}
		}

		return Result;
	}

	FCompileResult VeroScriptCompile::CompileFile(
	    const std::string_view FilePath,
	    const FCompileOptions& Options)
	{
		FConstantEvaluationRequestScope ConstantRequest(Options.ConstantEvaluationLimits);
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!Options.ModuleEnvironment->Apply(Effective, Failure))
			{
				FScriptError E;
				E.Message = Failure;
				return Fail(std::move(E));
			}
			return CompileFile(FilePath, Effective);
		}
		VeroScriptModuleLoader Loader(FilePath, Options.ModuleEnvironment);
		auto Processing = Options.CompileProcessing;
		if (Options.Bindings)
		{
			for (auto Definition : GetAnnotationDefinitions(*Options.Bindings))
			{
				Processing.AnnotationDefinitions.push_back(std::move(Definition));
			}
		}

		Loader.SetScriptCompileExtensions(Options.ScriptCompileExtensions);
		Loader.SetCompileProcessing(std::move(Processing));
		Loader.SetBuildPreprocessLineMaps(Options.bBuildPreprocessLineMaps);
		for (const std::string_view StdlibPath : Options.StdlibSearchPaths)
		{
			Loader.AddStdlibSearchRoot(std::string(StdlibPath));
		}
		for (const std::string_view ImportPath : Options.ImportPaths)
		{
			Loader.AddImportPath(std::string(ImportPath));
		}
		for (const std::string_view ImplicitImportPath : Options.ImplicitImportPaths)
		{
			Loader.AddImplicitImportPath(std::string(ImplicitImportPath));
		}
		Loader.SetAllowCircularImports(Options.bAllowCircularImports);
		Loader.SetAllowProjectWideAccess(Options.bAllowProjectWideAccess);
		ApplyOverlays(Loader, Options);
		ApplyDefines(Loader, Options);
		ApplyHostTypeNames(Loader, Options.Bindings);
		std::unique_ptr<FProgram> Program = Loader.LoadMainModule();
		if (Loader.HasError() || !Program)
		{
			return FailFromLoader(Loader, "diag.failed_to_load_module");
		}
		auto Result = CompileProgram(std::move(Program), Options.AnnotationProcessing, ResolveNatives(Options), Options.Bindings, Options.ConstantEvaluationLimits);
		Result.LoadedModulePaths.assign(Loader.GetLoadedModulePaths().begin(), Loader.GetLoadedModulePaths().end());
		std::sort(Result.LoadedModulePaths.begin(), Result.LoadedModulePaths.end());
		if (Options.bBuildPreprocessLineMaps)
		{
			for (const auto& Path : Loader.GetLoadedModulePaths())
			{
				if (const auto Map = Loader.GetPreprocessLineMap(Path))
				{
					Result.PreprocessLineMaps.emplace(PathUtil::IdentityKey(Path), *Map);
				}
			}
		}

		return Result;
	}
}
