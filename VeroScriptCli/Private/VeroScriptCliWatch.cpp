// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCliWatch.h"

#include "VeroScriptAst.h"
#include "VeroScriptEngine.h"
#include "VeroScriptHost.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptPath.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace VeroScript::Cli
{
	namespace
	{
		std::filesystem::path NormalizeScriptPath(const std::string_view ScriptPath)
		{
			const std::filesystem::path Input = PathUtil::FromUtf8(ScriptPath);
			std::error_code Error;
			const std::filesystem::path Canonical =
				std::filesystem::weakly_canonical(Input, Error);
			return Error ? Input.lexically_normal() : Canonical;
		}

		std::vector<std::filesystem::path> CollectWatchPaths(
			const std::filesystem::path& MainScriptPath, const std::vector<std::string_view>& ImportPaths,
			const std::vector<std::string_view>& Defines,
			const std::vector<FPredefinedMacro>& PredefinedMacros,
			const std::vector<std::string_view>& ImplicitImportPaths,
			const std::vector<std::string_view>& StdlibSearchPaths, bool bAllowCircularImports,
			bool bAllowProjectWideAccess, const FScriptCompileExtensionsOptions& ScriptExtensions,
			std::shared_ptr<const FModuleEnvironment> ModuleEnvironment)
		{
			std::vector<std::filesystem::path> Paths;
			Paths.push_back(MainScriptPath);

			VeroScriptModuleLoader Loader(PathUtil::ToUtf8(MainScriptPath), ModuleEnvironment);
			Loader.SetScriptCompileExtensions(ScriptExtensions);
			for (auto Path : ImportPaths)
			{
				Loader.AddImportPath(std::string(Path));
			}

			for (auto Path : StdlibSearchPaths)
			{
				Loader.AddStdlibSearchRoot(std::string(Path));
			}

			for (auto Path : ImplicitImportPaths)
			{
				Loader.AddImplicitImportPath(std::string(Path));
			}

			Loader.SetAllowCircularImports(bAllowCircularImports);
			Loader.SetAllowProjectWideAccess(bAllowProjectWideAccess);
			std::vector<std::string> Specs;
			for (auto Define : Defines)
			{
				Specs.emplace_back(Define);
			}

			Loader.SetDefines(std::move(Specs));
			Loader.SetPredefinedMacros(PredefinedMacros);
			auto Program = Loader.LoadMainModule();
			if (Program)
			{
				auto Prepared = VeroScriptProgramPreparation::Prepare(std::move(Program));
				if (Prepared.Succeeded())
				{
					for (const auto& Dependency : Prepared.Prepared->GetDependencies())
					{
						Paths.push_back(NormalizeScriptPath(Dependency.Path));
					}
				}
			}
			for (const std::string& LoadedPath : Loader.GetLoadedModulePaths())
			{
				std::filesystem::path Candidate = NormalizeScriptPath(LoadedPath);
				bool bAlreadyListed = false;
				for (const std::filesystem::path& Existing : Paths)
				{
					if (Existing == Candidate)
					{
						bAlreadyListed = true;
						break;
					}
				}
				if (!bAlreadyListed)
				{
					Paths.push_back(Candidate);
				}
			}

			return Paths;
		}

		struct FWatchStamp
		{
			bool operator==(const FWatchStamp &Other) const
			{
				return Path == Other.Path && Modified == Other.Modified && Size == Other.Size &&
					   bExists == Other.bExists && bValid == Other.bValid;
			}

			std::filesystem::path Path;

			std::filesystem::file_time_type Modified{};

			std::uintmax_t Size = 0;

			bool bExists = false, bValid = false;
		};
		std::vector<FWatchStamp> ReadWatchStamps(const std::vector<std::filesystem::path> &Paths)
		{
			std::vector<FWatchStamp> Result;
			for (const auto &Path : Paths)
			{
				FWatchStamp Stamp;
				Stamp.Path = Path;
				std::error_code Error;
				Stamp.bExists = std::filesystem::exists(Path, Error);
				if (!Error && Stamp.bExists)
				{
					Stamp.Modified = std::filesystem::last_write_time(Path, Error);
					if (!Error)
					{
						Stamp.Size = std::filesystem::file_size(Path, Error);
						Stamp.bValid = !Error;
					}
				}
				Result.push_back(std::move(Stamp));
			}
			return Result;
		}

		void PrintWatchBanner(
			const std::filesystem::path& MainScriptPath,
			const std::vector<std::filesystem::path>& WatchPaths)
		{
			std::cout << "Watching " << PathUtil::ToUtf8(MainScriptPath) << '\n';
			if (WatchPaths.size() > 1)
			{
				std::cout << "Also watching " << (WatchPaths.size() - 1) << " loaded module file(s)\n";
			}
			std::cout << "Press Ctrl+C to stop\n\n";
		}
	}

	int RunWatch(const std::string_view ScriptPath, const std::vector<std::string_view>& ImportPaths,
				 const std::vector<std::string_view>& ScriptArgs, const bool bExitOnFailure,
				 const bool bUseBytecodeCache, const std::vector<std::string_view>& Defines,
				 const std::string_view BytecodeCacheDirectory,
				 const std::vector<std::string_view>& StdlibSearchPaths,
				 const EExecutionBackend ExecutionBackend,
				 const std::vector<FPredefinedMacro>& PredefinedMacros,
				 const std::vector<std::string_view>& ImplicitImportPaths, const bool bAllowCircularImports,
				 const bool bAllowProjectWideAccess,
				 std::shared_ptr<const IJitBackendFactory> JitBackendFactory,
				 const FScriptCompileExtensionsOptions& ScriptExtensions,
				 std::shared_ptr<const FModuleEnvironment> ModuleEnvironment,
		std::shared_ptr<const IVmBackendFactory> VmBackendFactory, std::string VmBackendPath)
	{
		const std::filesystem::path MainScriptPath = NormalizeScriptPath(ScriptPath);
		std::error_code ExistsError;
		if (!std::filesystem::exists(MainScriptPath, ExistsError) || ExistsError)
		{
			std::cerr << "VeroScript: cannot open script: "
				<< PathUtil::ToUtf8(MainScriptPath) << '\n';
			return 2;
		}

		std::vector<std::filesystem::path> WatchPaths = CollectWatchPaths(
			MainScriptPath, ImportPaths, Defines, PredefinedMacros, ImplicitImportPaths, StdlibSearchPaths,
			bAllowCircularImports, bAllowProjectWideAccess, ScriptExtensions, ModuleEnvironment);
		PrintWatchBanner(MainScriptPath, WatchPaths);

		auto LastStamps = ReadWatchStamps(WatchPaths);
		bool bFirstRun = true;

		while (true)
		{
			const auto CurrentStamps = ReadWatchStamps(WatchPaths);
			if (bFirstRun || CurrentStamps != LastStamps)
			{
				if (!bFirstRun)
				{
					std::cout << "\n--- change detected, re-running ---\n\n";
				}
				else
				{
					bFirstRun = false;
				}

				LastStamps = CurrentStamps;

				std::ostringstream Output;
				std::ostringstream Errors;
				FEngineConfig Config;
				Config.ImportPaths = ImportPaths;
				Config.Defines = Defines;
				Config.PredefinedMacros = PredefinedMacros;
				Config.StdlibSearchPaths = StdlibSearchPaths;
				Config.ImplicitImportPaths = ImplicitImportPaths;
				Config.bAllowCircularImports = bAllowCircularImports;
				Config.bAllowProjectWideAccess = bAllowProjectWideAccess;
				Config.ScriptCompileExtensions=ScriptExtensions;
				Config.ModuleEnvironment = ModuleEnvironment;
				Config.ExecutionBackend = ExecutionBackend;
				Config.JitBackendFactory = JitBackendFactory;
				Config.VmBackendFactory = VmBackendFactory;
				Config.VmBackendPath = VmBackendFactory ? std::string{} : VmBackendPath;
				FStreamEngineHost Host(Output, Errors);
				Config.ScriptArgs = ScriptArgs;
				Config.bUseBytecodeCache = bUseBytecodeCache;
				Config.BytecodeCacheDirectory = BytecodeCacheDirectory;
				const FRunResult Result = VeroScriptEngine::RunFile(ScriptPath, Host, Config);

				if (!Output.str().empty())
				{
					std::cout << Output.str();
				}
				if (!Errors.str().empty())
				{
					std::cerr << Errors.str();
				}
				std::cout.flush();
				WatchPaths = CollectWatchPaths(MainScriptPath, ImportPaths, Defines, PredefinedMacros,
											   ImplicitImportPaths, StdlibSearchPaths, bAllowCircularImports,
											   bAllowProjectWideAccess, ScriptExtensions, ModuleEnvironment);
				// Keep pre-run stamps for existing files so changes during execution
				// are detected on the next poll. New dependencies get their own stamp.
				auto Refreshed = ReadWatchStamps(WatchPaths);
				for (auto& Stamp : Refreshed)
				{
					for (const auto& Before : LastStamps)
					{
						if (Stamp.Path == Before.Path)
						{
							Stamp = Before;
							break;
						}
					}
				}

				LastStamps = std::move(Refreshed);
				// Non-zero covers failures, int main returns, and intentional exit(N).
				// Both script-controlled cases keep bSuccess=true.
				if (!Result.bSuccess || Result.ExitCode != 0)
				{
					std::cerr << "VeroScript: exited with code " << Result.ExitCode << '\n';
					if (bExitOnFailure)
					{
						return Result.ExitCode;
					}
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(300));
		}
	}
}
