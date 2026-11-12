// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCliBuild.h"
#include "VeroScriptBuild.h"
#include "VeroScriptPath.h"
#if defined(VEROSCRIPT_RUNTIME_HAS_VM)
#include "VeroScriptBytecodeVmAssembly.h"
#endif
#include <filesystem>
#include <iostream>
#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
#include "VeroScriptPackageManager.h"
#include "VeroScriptApplicationPackages.h"
#endif

namespace VeroScript::Cli
{
	namespace
	{
		class FPlanningCrypto final : public Application::ICryptoProvider
		{
		public:
			std::string Identity;
			std::string GetIdentity() const override
			{
				return Identity;
			}
			bool Random(std::vector<std::uint8_t>&, std::string& E) const override
			{
				return Deny(E);
			}
			bool Encrypt(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&,
			    const std::vector<std::uint8_t>&, std::vector<std::uint8_t>&, std::string& E) const override
			{
				return Deny(E);
			}
			bool Decrypt(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&,
			    const std::vector<std::uint8_t>&, std::vector<std::uint8_t>&, std::string& E) const override
			{
				return Deny(E);
			}
			bool Sign(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, std::vector<std::uint8_t>&, std::string& E) const override
			{
				return Deny(E);
			}
			bool Verify(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, std::string& E) const override
			{
				return Deny(E);
			}

		private:
			static bool Deny(std::string& E)
			{
				E = "cryptographic execution is disabled during planning";
				return false;
			}
		};
	}
	int RunBuild(const std::vector<std::string>& Arguments, const std::string& Startup, const std::string& Executable)
	{
		(void)Executable;
		namespace fs = std::filesystem;
		try
		{
			Build::FBuildRequest Request;
			Request.ProjectRoot = Startup;
			Build::FServices Services;
			#if defined(VEROSCRIPT_RUNTIME_HAS_VM)
			Services.Compile.ScriptCompileExtensions.VmBackendPath = BytecodeVm::RuntimeLibraryPath(Executable);
			#endif
			Services.Application = Application::CreateApplicationServices();
			for (const auto& Root : Services.Application.StandardLibraryPaths)
			{
				Services.Compile.StdlibSearchPaths.emplace_back(Root);
			}
			Services.HostPlatformJson = Application::HostApplicationTargetJson();
			Services.LauncherTemplate = PathUtil::ToUtf8(PathUtil::FromUtf8(PathUtil::ExecutableDirectory()) /
			#if defined(_WIN32)
			    "veroscript-launcher.exe"
			#else
			    "veroscript-launcher"
			#endif
			    );
			if (!fs::is_regular_file(PathUtil::FromUtf8(Services.LauncherTemplate)))
			{
				Services.LauncherTemplate.clear();
			}
			bool PlanOnly = false, Json = false, ExplicitPath = false;
			std::set<std::string> Seen;
			std::string Crypto, Keyring;
			const auto Absolute = [&](const std::string& S)
			{
				const auto P = PathUtil::FromUtf8(S);
				return PathUtil::ToUtf8((P.is_absolute() ? P : PathUtil::FromUtf8(Startup) / P).lexically_normal());
			};
			for (std::size_t I = 0; I < Arguments.size(); ++I)
			{
				const auto& A = Arguments[I];
				if (A.empty())
				{
					throw std::runtime_error("empty build argument");
				}
				if (A[0] != '-')
				{
					Request.Targets.push_back(A);
					continue;
				}
				if (A == "--target" || A.rfind("--target=", 0) == 0)
				{
					if (A == "--target")
					{
						if (++I >= Arguments.size() || Arguments[I].empty())
						{
							throw std::runtime_error("missing build target");
						}
						Request.Targets.push_back(Arguments[I]);
					}
					else
					{
						Request.Targets.push_back(A.substr(9));
					}
					continue;
				}
				if (!Seen.insert(A).second)
				{
					throw std::runtime_error("duplicate build option: " + A);
				}
				if (A == "--plan")
				{
					PlanOnly = true;
					continue;
				}
				if (A == "--json")
				{
					Json = true;
					continue;
				}
				if (A == "--allow-process")
				{
					Services.AllowProcess = true;
					continue;
				}
				if (A == "--allow-script")
				{
					Services.AllowScript = true;
					continue;
				}
				if (A != "--path" && A != "--build-file" && A != "--profile" && A != "--output-directory" && A != "--crypto-library" && A != "--keyring")
				{
					throw std::runtime_error("unknown build option: " + A);
				}
				if (++I >= Arguments.size() || Arguments[I].empty())
				{
					throw std::runtime_error("missing value for " + A);
				}
				const auto& V = Arguments[I];
				if (A == "--path")
				{
					Request.ProjectRoot = Absolute(V);
					ExplicitPath = true;
				}
				else if (A == "--build-file")
				{
					Request.BuildFile = Absolute(V);
				}
				else if (A == "--profile")
				{
					Request.Profile = V;
				}
				else if (A == "--output-directory")
				{
					Request.OutputDirectory = Absolute(V);
				}
				else if (A == "--crypto-library")
				{
					Crypto = Absolute(V);
				}
				else if (A == "--keyring")
				{
					Keyring = Absolute(V);
				}
			}
			if (!ExplicitPath && !Request.BuildFile.empty())
			{
				Request.ProjectRoot = PathUtil::ToUtf8(PathUtil::FromUtf8(Request.BuildFile).parent_path());
			}
			if (Request.BuildFile.empty())
			{
				Request.BuildFile = PathUtil::ToUtf8(PathUtil::FromUtf8(Request.ProjectRoot) / "veroscript-build.json");
			}
			if (!Crypto.empty() || !Keyring.empty())
			{
				if (Crypto.empty() || Keyring.empty())
				{
					throw std::runtime_error("protection requires --crypto-library and --keyring");
				}
				Application::FDiagnostic E;
				std::shared_ptr<const Application::ICryptoProvider> Library;
				if (PlanOnly)
				{
					std::string Hash;
					if (!Application::FileSha256(Crypto, Hash, E))
					{
						throw std::runtime_error(E.Code + ": " + E.Message);
					}
					auto Planning = std::make_shared<FPlanningCrypto>();
					Planning->Identity = "libsodium:" + Hash;
					Library = std::move(Planning);
				}
				else
				{
					Library = Application::LoadSodiumCrypto(Crypto, E);
				}
				if (!Library)
				{
					throw std::runtime_error(E.Code + ": " + E.Message);
				}
				auto Keys = Application::LoadProtectionKeys(Keyring, E);
				if (!Keys)
				{
					throw std::runtime_error(E.Code + ": " + E.Message);
				}
				Services.Protection = std::make_shared<Application::FProtection>(std::move(Library), std::move(Keys), Application::FTrustPolicy{});
				Services.Application.Protection = Services.Protection;
			}
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			std::shared_ptr<const Packages::FInstalledEnvironment> Installed;
			if (fs::exists(PathUtil::FromUtf8(Request.ProjectRoot) / "veroscript.lock"))
			{
				Packages::FEnvironmentInput Input;
				Input.Root = Request.ProjectRoot;
				Input.WorkingDirectory = Startup;
				Packages::FDiagnostic E;
				if (!Packages::FPackageManager().Load(Input, Installed, E))
				{
					throw std::runtime_error(E.Code + ": " + E.Message);
				}
				Packages::FCoreEnvironmentOptions Options;
				Options.ScriptCompileExtensions = Services.Compile.ScriptCompileExtensions;
				Options.EntryRoot = Request.ProjectRoot;
				Options.AnalysisOnly = PlanOnly;
				if (!Installed->BuildCoreEnvironment(Options, Services.Compile.ModuleEnvironment, E))
				{
					throw std::runtime_error(E.Code + ": " + E.Message);
				}
				Services.Identity = Installed->GetFingerprint() + ":" + Services.Compile.ModuleEnvironment->GetCompileFingerprint();
				Services.Deploy = [Installed](const auto& Usage, const auto& Roots, const std::string& Output, Build::FDiagnostic& Error)
				{
					Packages::FDiagnostic E;
					std::string Snapshot;
					if (!Installed->Deploy(Output, Usage, Roots, Snapshot, E))
					{
						Error = {E.Code, E.Message, {}, E.Field, E.Stage};
						return false;
					}
					return true;
				};
				Services.LeaseDeployment = [](const std::string& Directory, Build::FDiagnostic& Error)
				{
					Packages::FDiagnostic E;
					auto Lease = Packages::AcquireDeploymentDirectoryLease(Directory, true, E, {});
					if (!Lease)
					{
						Error = {E.Code, E.Message, {}, E.Field, E.Stage};
					}
					return Lease;
				};
				Application::ConfigurePackageApplicationServices(Services.Application, Installed);
			}
			Services.Pack = [Startup](const std::string& Source, const std::string& Output, Build::FDiagnostic& Error)
			{
				Packages::FRequest Q;
				Q.Operation = Packages::EOperation::Pack;
				Q.Environment.Root = Source;
				Q.Environment.WorkingDirectory = Startup;
				Q.Arguments = {Source};
				Q.OutputPath = Output;
				Packages::FResult Result;
				Packages::FDiagnostic E;
				if (!Packages::FPackageManager().Execute(Q, Result, E))
				{
					Error = {E.Code, E.Message, {}, E.Field, E.Stage};
					return false;
				}
				return true;
			};
			#else
			(void)Executable;
			#endif
			Services.Log = [Json](const std::string& Target, const std::string& Text)
			{
				if (!Json)
				{
					std::cerr << "[" << Target << "] " << Text << '\n';
				}
			};
			std::shared_ptr<const Build::FBuildPlan> Plan;
			Build::FDiagnostic E;
			if (!Build::FBuilder::Plan(Request, Services, Plan, E))
			{
				throw std::runtime_error(E.Code + " [" + E.Target + "]: " + E.Message + (E.Path.empty() ? "" : " (" + E.Path + ")"));
			}
			if (PlanOnly)
			{
				std::cout << Plan->ToJson() << '\n';
				return 0;
			}
			Build::FBuildResult Result;
			if (!Build::FBuilder::Execute(*Plan, Result, E))
			{
				throw std::runtime_error(E.Code + " [" + E.Target + "]: " + E.Message + (E.Path.empty() ? "" : " (" + E.Path + ")"));
			}
			std::cout << Result.ToJson() << '\n';
			return 0;
		}
		catch (const std::exception& E)
		{
			std::cerr << "VeroScript build: " << E.what() << '\n';
			return 2;
		}
	}
}
