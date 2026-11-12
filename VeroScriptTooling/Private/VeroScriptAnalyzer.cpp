// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAnalyzer.h"

#include "Internal/VeroScriptPreprocessorQuery.h"
#include "Internal/VeroScriptToolingCompile.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPath.h"

#include <fstream>
#include <sstream>

namespace VeroScript::Tooling
{
	namespace
	{
		FAnalyzeResult FromCompileResult(
			const VeroScript::FCompileResult& CompileResult,
			const std::string_view Source,
			const std::string_view FallbackFileLabel,
			const FAnalyzeOptions& Options)
		{
			FAnalyzeResult Result;
			Result.bSuccess = CompileResult.bSuccess;
			if (CompileResult.PreparedProgram)
			{
				Result.GeneratedDocuments = CompileResult.PreparedProgram->GetGeneratedDocuments();
			}

			if (!CompileResult.Diagnostics.empty())
			{
				Result.Diagnostics.reserve(CompileResult.Diagnostics.size());
				for (const VeroScript::FScriptError& Error : CompileResult.Diagnostics)
				{
					Result.Diagnostics.push_back(
						ToToolingDiagnostic(Error, FallbackFileLabel, "analysis failed"));
				}
			}
			else if (!CompileResult.bSuccess)
			{
				Result.Diagnostics.push_back(
					ToToolingDiagnostic(CompileResult.Error, FallbackFileLabel, "analysis failed"));
			}

			Detail::RemapAllToolingDiagnostics(
				Result.Diagnostics,
				Options.Defines, Options.PredefinedMacros, Source, FallbackFileLabel, Options.SourceOverlays,
				[&](std::string_view, std::string_view Label, FPreprocessLineMap& Map, std::string&)
				{
					const auto It = CompileResult.PreprocessLineMaps.find(PathUtil::IdentityKey(std::string(Label)));
					if (It == CompileResult.PreprocessLineMaps.end()) return false;
					Map = It->second;
					return true;
				});
			return Result;
		}

		VeroScript::FCompileOptions MakeCompileOptions(const FAnalyzeOptions& Options)
		{
			VeroScript::FCompileOptions CompileOptions;
			CompileOptions.ImportPaths = Options.ImportPaths;
			CompileOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
			CompileOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
			CompileOptions.bAllowCircularImports = Options.bAllowCircularImports;
			CompileOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			CompileOptions.Defines = Options.Defines;
			CompileOptions.PredefinedMacros = Options.PredefinedMacros;
			CompileOptions.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
			CompileOptions.Bindings = Options.Bindings;
			CompileOptions.AnnotationProcessing = Options.AnnotationProcessing;
			CompileOptions.CompileProcessing = Options.CompileProcessing;
			CompileOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			CompileOptions.ModuleEnvironment = Options.ModuleEnvironment;
			return CompileOptions;
		}
	}

	FAnalyzeResult VeroScriptAnalyzer::AnalyzeSource(
		const std::string_view Source,
		const std::string_view FileLabel,
		const FAnalyzeOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FAnalyzeResult>(Failure);
			}

			return AnalyzeSource(Source, FileLabel, Effective);
		}

		return FromCompileResult(
			Detail::CompileToolingSource(
				Source,
				FileLabel,
				MakeCompileOptions(Options)),
			Source,
			FileLabel,
			Options);
	}

	FAnalyzeResult VeroScriptAnalyzer::AnalyzeFile(
		const std::string_view FilePath,
		const FAnalyzeOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FAnalyzeResult>(Failure);
			}

			return AnalyzeFile(FilePath, Effective);
		}

		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		std::string Source;
		if (Input)
		{
			std::ostringstream Buffer;
			Buffer << Input.rdbuf();
			Source = Buffer.str();
		}
		return FromCompileResult(
			Detail::CompileToolingFile(FilePath, MakeCompileOptions(Options)),
			Source,
			FilePath,
			Options);
	}
}
