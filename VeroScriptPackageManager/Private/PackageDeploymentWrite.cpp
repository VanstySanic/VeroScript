// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include <algorithm>

namespace VeroScript::Packages
{
	bool FInstalledEnvironment::Deploy(const std::string& OutputPath,
	    const std::vector<std::string>& Usage, const std::vector<std::string>& Roots,
	    std::string& Snapshot, FDiagnostic& E, const FClientOptions& Options) const
	{
		return DeployForTarget(HostTargetContext(), OutputPath, Usage, Roots, Snapshot, E, Options);
	}

	bool FInstalledEnvironment::DeployForTarget(const FTargetContext& Target, const std::string& OutputPath,
	    const std::vector<std::string>& Usage, const std::vector<std::string>& Roots,
	    std::string& Snapshot, FDiagnostic& E, const FClientOptions& Options) const
	{
		using namespace Detail;
		Snapshot.clear();
		E = {};
		try
		{
			FWork W{Options};
			if (OutputPath.empty() || !U8Path(OutputPath).is_absolute())
			{
				Fail("deploy-output", "deployment output must be absolute");
			}
			const auto Output = U8Path(PathUtil::Normalize(OutputPath));
			NoLinks(Output, true);
			if (fs::exists(Output))
			{
				Fail("output-exists", "deploy will not overwrite a directory");
			}
			const auto TargetIdentity = TargetJson(Target);
			if (TargetIdentity != TargetJson(Impl->Layout.Target))
			{
				Fail("deploy-cross-target", "deployment export target must match the validated installed environment");
			}
			std::set<std::string> SelectedUsage(Usage.begin(), Usage.end());
			if (!SelectedUsage.count("runtime") || SelectedUsage.size() != Usage.size() ||
			    std::any_of(Usage.begin(), Usage.end(), [](const auto& U)
			{
				return U != "runtime" && U != "compile";
			}))
			{
				Fail("deploy-usage", "deployment usage must be runtime or runtime and compile");
			}
			std::map<std::string, const FInstalledPackage*> Packages;
			for (const auto& P : Impl->Packages)
			{
				if (P.Enabled)
				{
					Packages.emplace(P.Name, &P);
				}
			}
			std::set<std::string> RootSet;
			if (!Roots.empty())
			{
				for (const auto& N : Roots)
				{
					if (!Packages.count(N) || !RootSet.insert(N).second)
					{
						Fail("deploy-root", "deployment root is absent, disabled or duplicated", N);
					}
				}
			}
			else
			{
				const auto Declared = Impl->Project.value("dependencies", J::object());
				for (auto I = Declared.begin(); I != Declared.end(); ++I)
				{
					const auto U = I.value().value("usage", std::vector<std::string>{"runtime"});
					if (Packages.count(I.key()) &&
					    DependencyApplies(I.value(), Impl->Project, I.key(), Impl->Layout.Target, Impl->Lock.value("development", false)) &&
					    std::any_of(U.begin(), U.end(), [&](const auto& V)
					{
						return SelectedUsage.count(V);
					}))
					{
						RootSet.insert(I.key());
					}
				}
			}
			std::map<std::string, std::set<std::string>> Uses, Edges;
			std::function<void(const std::string&, const std::string&, std::size_t)> Visit;
			Visit = [&](const std::string& N, const std::string& U, std::size_t Depth)
			{
				W.Check();
				if (Depth > 256)
				{
					Fail("deploy-depth", "deployment dependencies exceed safe nesting");
				}
				if (!Packages.count(N))
				{
					Fail("deployment-incomplete", "deployment dependency is absent or disabled", N);
				}
				if (!Uses[N].insert(U).second) return;
				const auto& P = *Packages.at(N);
				const auto M = Parse(P.ManifestJson, Options.Limits);
				const auto Dependencies = M.value("dependencies", J::object());
				for (const auto& D : P.Dependencies)
				{
					const auto EdgeUsage = Dependencies.at(D).value("usage", std::vector<std::string>{"runtime"});
					for (const auto& EU : EdgeUsage)
					{
						const auto Effective = EU == "runtime" ? U : EU;
						if (SelectedUsage.count(Effective))
						{
							Edges[N].insert(D);
							Visit(D, Effective, Depth + 1);
						}
					}
				}
			};
			for (const auto& N : RootSet)
			{
				for (const auto& U : Packages.at(N)->Usage)
				{
					if (SelectedUsage.count(U))
					{
						Visit(N, U, 0);
					}
				}
				if (!Uses.count(N))
				{
					Fail("deploy-root-usage", "deployment root does not have the requested usage", N);
				}
			}
			FTempDirectory Temp(Output.parent_path());
			const auto Root = Temp.Path / "deployment";
			fs::create_directories(Root);
			J D{{"format", "veroscript-deployment"}, {"formatVersion", FormatVersion},
			    {"target", TargetJson(Impl->Layout.Target)}, {"usage", Usage}, {"roots", RootSet},
			    {"services", Impl->Project.value("services", J::object())}, {"packages", J::array()}, {"applications", J::array()}};
			std::map<std::string, J> Locked;
			for (const auto& P : Impl->Lock.at("packages"))
			{
				Locked.emplace(P.at("name").get<std::string>(), P);
			}
			for (const auto& Selection : Uses)
			{
				const auto& P = *Packages.at(Selection.first);
				const auto& Record = Impl->Profile.at("packages").at(P.Name);
				const auto Rel = fs::path("packages") / P.Name / (P.Version + "-" + P.Sha256);
				const auto Gen = fs::path("generated") / P.Name;
				CopyInventory(U8Path(P.Path), Root / Rel, Record.at("inventory"), W);
				CopyInventory(U8Path(P.GeneratedPath), Root / Gen, Record.at("generatedInventory"), W);
				std::vector<std::string> Runtime;
				for (const auto& N : P.RuntimeDependencies)
				{
					if (Edges[P.Name].count(N))
					{
						Runtime.push_back(N);
					}
				}
				D["packages"].push_back({{"name", P.Name}, {"path", PathUtil::ToUtf8(Rel)}, {"generated", PathUtil::ToUtf8(Gen)},
				    {"configuration", Parse(P.ConfigurationJson, Options.Limits)}, {"loaders", Parse(P.LoadersJson, Options.Limits)},
				    {"inventory", Record.at("inventory")}, {"generatedInventory", Record.at("generatedInventory")},
				    {"identity", P.Sha256}, {"sourceId", P.SourceId}, {"dependencies", Edges[P.Name]},
				    {"runtimeDependencies", Runtime}, {"usage", Selection.second}, {"textDomain", P.TextDomain},
				    {"artifact", Locked.at(P.Name).at("artifact")}});
			}
			Write(Root / ".vero-lease", "");
			Write(Root / "veroscript-deployment.json", D.dump(2));
			if (TargetIdentity != TargetJson(HostTargetContext()))
			{
				// Validate the staged closure using the target's rules before publishing.
				// The validation snapshot releases its directory lease before the rename.
				std::shared_ptr<const FInstalledEnvironment> Validated;
				auto ValidationOptions = Options;
				ValidationOptions.CheckCancelled = [&W]() { W.Check(); };
				if (!FPackageManager{}.ReadDeployment(NativePathText(Root), Target, Validated, E, ValidationOptions)) return false;
			}
			W.Check();
			NoLinks(Output, true);
			MovePathNoReplace(Root, Output);
			Snapshot = D.dump(2);
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			return false;
		}
		catch (const std::exception& X)
		{
			E = {"deploy", {}, X.what(), {}, {}, {}};
			return false;
		}
	}
}
