// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingCompile.h"
#include "VeroScriptAst.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptProcessingSourceMap.h"
#include <algorithm>
#include <unordered_set>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		void SetFailure(FCompileResult& Result, std::vector<FScriptError> Diagnostics)
		{
			if (Diagnostics.empty())
			{
				Diagnostics.push_back(MakeDiagnostic("diag.compile_failed"));
			}

			const auto Primary = std::find_if(Diagnostics.begin(), Diagnostics.end(),
				[](const auto& Error)
				{
					return Error.Severity == ECompileDiagnosticSeverity::Error;
				});
			Result.Error = Primary == Diagnostics.end() ? Diagnostics.front() : *Primary;
			Result.Diagnostics = std::move(Diagnostics);
		}

		FCompileResult Compile(std::string_view Source, std::string_view Path,
			const FCompileOptions& Options, bool FromFile, FSourceColumnMaps* SourceColumns, FVirtualSourceMap* VirtualSources)
		{
			if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
			{
				auto Effective = Options;
				std::string Failure;
				if (!Options.ModuleEnvironment->Apply(Effective, Failure))
				{
					FCompileResult Result;
					FScriptError Error;
					Error.Message = Failure;
					SetFailure(Result, {std::move(Error)});
					return Result;
				}
				return Compile(Source, Path, Effective, FromFile, SourceColumns, VirtualSources);
			}
			if (SourceColumns)
			{
				SourceColumns->clear();
			}

			if (VirtualSources)
			{
				VirtualSources->clear();
			}

			VeroScriptModuleLoader Loader(FromFile ? Path : std::string_view{}, Options.ModuleEnvironment);
			ApplyModuleLoadingOptions(Loader, Options);
			ApplyEngineOverlays(Loader, Options.SourceOverlays);
			ApplyDefines(Loader, Options.Defines);
			ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
			ApplyHostTypeNames(Loader, Options.Bindings);
			auto Program = FromFile ? Loader.LoadMainModule() : Loader.LoadSourceModule(Source, Path);
			FCompileResult Result;
			if (Program)
			{
				const auto InlineEntry = FromFile ? std::string{} : PathUtil::IdentityKey(std::string(Path));
				std::unordered_set<std::string> OverlayPaths;
				if (VirtualSources)
				{
					for (const auto& Overlay : Options.SourceOverlays)
					{
						OverlayPaths.insert(PathUtil::IdentityKey(Overlay.Path));
					}
				}

				Result.PreprocessLineMaps = Program->PreprocessLineMaps;
				// Source editing is complete before declaration preparation. Capture the
				// projection here: a failed preparation need not return a prepared program.
				ApplyProcessingSourceMaps(Program->SourceMetadata.get(), Result.PreprocessLineMaps);
				if (Program->SourceMetadata)
				{
					for (const auto& Document : Program->SourceMetadata->Documents)
					{
						if (Document.Id == Document.ModuleId + ":raw")
						{
							const auto Key = PathUtil::IdentityKey(Document.ModuleId);
							if (SourceColumns)
							{
								SourceColumns->emplace(Key, FSourceColumns::Capture(Document.Text));
							}

							if (VirtualSources)
							{
								const auto* Module = Options.ModuleEnvironment ? Options.ModuleEnvironment->FindSource(Document.ModuleId) : nullptr;
								// Identical source is already owned by the frozen environment.
								// LoadSource can supply an unsaved main buffer with a physical
								// label too. Its disk contents are not the compiled program.
								if (((!FromFile && Key == InlineEntry) ||
										Document.ModuleId.rfind("veroscript-module:/", 0) == 0 ||
										OverlayPaths.count(Key) || (Module && Module->Source)) &&
									(!Module || !Module->Source || *Module->Source != Document.Text))
								{
									VirtualSources->emplace(Key, Document.Text);
								}
							}
						}
					}
				}

				if (SourceColumns && !FromFile)
				{
					const std::string Key = PathUtil::IdentityKey(std::string(Path));
					if (SourceColumns->find(Key) == SourceColumns->end())
					{
						SourceColumns->emplace(Key, FSourceColumns::Capture(Source));
					}
				}
			}
			else
			{
				for (const auto& LoadedPath : Loader.GetLoadedModulePaths())
				{
					if (const auto Map = Loader.GetPreprocessLineMap(LoadedPath))
					{
						Result.PreprocessLineMaps.emplace(PathUtil::IdentityKey(LoadedPath), *Map);
					}
				}

				if (const auto Map = Loader.GetPreprocessLineMap(Path))
				{
					Result.PreprocessLineMaps.emplace(PathUtil::IdentityKey(std::string(Path)), *Map);
				}
			}
			if (Loader.HasError() || !Program)
			{
				auto Diagnostics = Loader.GetDiagnostics();
				if (Diagnostics.empty())
				{
					Diagnostics.push_back(Loader.HasError()
							? Loader.GetError()
							: MakeDiagnostic(FromFile ? "diag.failed_to_load_module"
													  : "diag.failed_to_load_source_module"));
				}

				SetFailure(Result, std::move(Diagnostics));
				return Result;
			}
			const auto Natives = Options.Bindings ? &Options.Bindings->GetNativeFunctions() : Options.Natives;
			if (Natives)
			{
				for (const auto& Native : *Natives)
				{
					const auto Invalid = !NativeParamFlagsAreValid(Native) ? "diag.invalid_native_parameter_flags"
						: !NativeFunctionContainerMetadataIsValid(Native) ? "diag.invalid_native_container_metadata" : "";
					if (*Invalid)
					{
						SetFailure(Result, {MakeDiagnostic(Invalid, {}, {Native.Name})});
						return Result;
					}
				}
			}

			// Use the same preparation entry point as VeroScriptCompile, including its
			// legacy annotation adapter and the loader's general processing session.
			auto Preparation = VeroScriptProgramPreparation::Prepare(std::move(Program),
				Options.AnnotationProcessing, Natives, Options.Bindings);
			if (!Preparation.Succeeded())
			{
				SetFailure(Result, std::move(Preparation.Diagnostics));
				return Result;
			}
			Result.PreparedProgram = std::move(Preparation.Prepared);
			VeroScriptCompiler Compiler;
			Result.Module = Compiler.CompilePrepared(*Result.PreparedProgram, Natives, Options.Bindings);
			if (Compiler.HasError() || !Result.Module)
			{
				auto Diagnostics = Compiler.GetDiagnostics();
				if (Diagnostics.empty())
				{
					Diagnostics.push_back(Compiler.GetError());
				}

				Diagnostics.insert(
					Diagnostics.begin(), Preparation.Diagnostics.begin(), Preparation.Diagnostics.end());
				SetFailure(Result, std::move(Diagnostics));
				Result.Module.reset();
				return Result;
			}
			Result.bSuccess = true;
			Result.Diagnostics = std::move(Preparation.Diagnostics);
			return Result;
		}
	}
	FCompileResult CompileToolingSource(std::string_view Source, std::string_view FileLabel,
		const FCompileOptions& Options, FSourceColumnMaps* SourceColumns, FVirtualSourceMap* VirtualSources)
	{
		return Compile(Source, FileLabel, Options, false, SourceColumns, VirtualSources);
	}
	FCompileResult CompileToolingFile(std::string_view FilePath, const FCompileOptions& Options,
		FSourceColumnMaps* SourceColumns, FVirtualSourceMap* VirtualSources)
	{
		return Compile({}, FilePath, Options, true, SourceColumns, VirtualSources);
	}
}
