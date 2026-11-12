// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptApplicationPackages.h"
#include "VeroScriptPath.h"
#include "json/json.hpp"
#include <algorithm>
#include <filesystem>

namespace VeroScript::Application
{
	void ConfigurePackageApplicationServices(FApplicationServices& S,
	    std::shared_ptr<const Packages::FInstalledEnvironment> External, Packages::FCoreEnvironmentOptions Base,
	    std::function<void(std::vector<FNativeServiceProviderDesc>, std::shared_ptr<const FModuleEnvironment>,
	        FApplicationBackendSelector)>
	        ProvidersReady)
	{
		if (!Base.ArtifactDecoder && S.Protection)
		{
			Base.ArtifactDecoder = S.Protection;
		}
		const auto Build = [Base, Analysis = S.AnalysisOnly, ProvidersReady = std::move(ProvidersReady)](std::shared_ptr<const Packages::FInstalledEnvironment> Installed,
		                       bool Compile, const std::string& Services, std::shared_ptr<const FModuleEnvironment>& Out, std::string& Error)
		{
			try
			{
				if (!Installed)
				{
					Error = "application has no explicitly selected package environment";
					return false;
				}
				auto Options = Base;
				Options.AnalysisOnly = Analysis;
				Options.RuntimeOnly = !Compile;
				const auto Selections = nlohmann::json::parse(Services);
				if (!Selections.is_object())
				{
					Error = "application service selections must be an object";
					return false;
				}
				for (auto I = Selections.begin(); I != Selections.end(); ++I)
				{
					if (!I.value().is_string())
					{
						Error = "application service selection must be a backend identifier";
						return false;
					}
					if (!Options.SelectedBackends.count(I.key()))
					{
						Options.SelectedBackends.emplace(I.key(), I.value());
					}
				}
				if (Options.EntryRoot.empty())
				{
					Options.EntryRoot = Installed->GetLayout().Root;
				}
				Packages::FDiagnostic E;
				std::vector<FNativeServiceProviderDesc> Providers;
				if (!Installed->BuildCoreEnvironment(Options, Out, E, ProvidersReady ? &Providers : nullptr))
				{
					Error = E.Code + ": " + E.Message;
					return false;
				}
				if (ProvidersReady)
				{
					ProvidersReady(std::move(Providers), Out,
					    [Environment = Out](std::string_view Role)
					{
						return ResolveBackendSelection(Role, {}, Environment->GetDescription().SelectedBackends);
					});
				}
				return true;
			}
			catch (const std::exception& E)
			{
				Error = E.what();
				return false;
			}
		};
		S.LoadEnvironment = [Build](const std::string& Root, bool Compile, const std::string& Services,
		                        std::shared_ptr<const FModuleEnvironment>& Out, std::string& Error)
		{
			// ZIP content has no empty directories. Recreate only empty generated roots
			// explicitly declared by the deployment, inside the materialized cache.
			try
			{
				FArtifactData Record;
				if (!ReadArtifactFile({PathUtil::ToUtf8((PathUtil::FromUtf8(Root) / "veroscript-deployment.json")), "generic", {}}, {4ull << 20, {}}, Record, Error)) return false;
				const auto Descriptor = nlohmann::json::parse(Record.Bytes);
				if (Descriptor.at("format") != "veroscript-deployment" || Descriptor.at("formatVersion") != "0.1.1" ||
				    !Descriptor.at("packages").is_array() || Descriptor.at("packages").size() > 4096)
				{
					Error = "invalid deployment descriptor";
					return false;
				}
				if (Compile)
				{
					const auto Usage = Descriptor.at("usage").get<std::vector<std::string>>();
					if (std::find(Usage.begin(), Usage.end(), "compile") == Usage.end())
					{
						Error = "source applications require a deployment retaining compile usage";
						return false;
					}
				}
				for (const auto& P : Descriptor.at("packages"))
				{
					if (!P.at("generatedInventory").is_array() || !P.at("generatedInventory").empty())
					{
						continue;
					}
					const auto Relative = P.at("generated").get<std::string>();
					if (!ValidateContentPath(Relative, Error)) return false;
					const auto Directory = PathUtil::FromUtf8(PathUtil::ToUtf8((PathUtil::FromUtf8(Root) / PathUtil::FromUtf8(Relative))));
					FDiagnostic E;
					if (!CheckPhysicalPath(PathUtil::ToUtf8(Directory), true, E))
					{
						Error = E.Message;
						return false;
					}
					std::filesystem::create_directories(Directory);
					if (!CheckPhysicalPath(PathUtil::ToUtf8(Directory), false, E))
					{
						Error = E.Message;
						return false;
					}
				}
			}
			catch (...)
			{
				Error = "cannot prepare declared deployment directories";
				return false;
			}
			Packages::FDiagnostic E;
			std::shared_ptr<const Packages::FInstalledEnvironment> Installed;
			if (!Packages::FPackageManager().LoadDeployment(Root, Installed, E))
			{
				Error = E.Code + ": " + E.Message;
				return false;
			}
			return Build(std::move(Installed), Compile, Services, Out, Error);
		};
		if (External)
		{
			S.LoadExternalEnvironment = [Build, External = std::move(External)](bool Compile,
			                                const std::string& Services, std::shared_ptr<const FModuleEnvironment>& Out, std::string& Error)
			{
				return Build(External, Compile, Services, Out, Error);
			};
		}
	}
}
