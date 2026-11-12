// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include "VeroScriptPath.h"
#include <algorithm>

namespace VeroScript::Packages
{
	std::shared_ptr<const void> AcquireDeploymentDirectoryLease(const std::string& Path, bool Exclusive,
	    FDiagnostic& E, const FClientOptions& O)
	{
		try
		{
			using namespace Detail;
			const auto Root = Absolute(Path, {});
			NoLinks(Root);
			NoLinks(Root / ".vero-lease");
			FWork W{O};
			const auto Identity = PathUtil::IdentityKey(NativePathText(Root));
			auto Lease = std::make_shared<FFileLock>(U8Path(DefaultCacheRoot()) / "deployment-leases" / (Hash(Identity) + ".lock"), Exclusive, true, false, &W);
			E = {};
			return Lease;
		}
		catch (const Detail::FFailure& X)
		{
			E = X.Diagnostic;
			return {};
		}
		catch (const std::exception& X)
		{
			E.Code = "deployment-lease";
			E.Message = X.what();
			return {};
		}
	}
	bool FPackageManager::LoadDeployment(const std::string& Path,
	    std::shared_ptr<const FInstalledEnvironment>& Out, FDiagnostic& E,
	    const FClientOptions& O) const
	{
		return ReadDeployment(Path, HostTargetContext(), Out, E, O);
	}

	bool FPackageManager::ReadDeployment(const std::string& Path, const FTargetContext& ExpectedTarget,
	    std::shared_ptr<const FInstalledEnvironment>& Out, FDiagnostic& E,
	    const FClientOptions& O) const
	{
		try
		{
			using namespace Detail;
			// Path can belong to the snapshot being replaced through Out.
			const std::string RequestedPath = Path;
			E = {};
			Out.reset();
			auto Root = Absolute(RequestedPath, {});
			NoLinks(Root);
			FWork W{O};
			auto Lease = AcquireDeploymentDirectoryLease(NativePathText(Root), false, E, O);
			if (!Lease) return false;
			auto D = ReadRecord(Root / "veroscript-deployment.json", W);
			Keys(D, {"format", "formatVersion", "target", "roots", "services", "packages", "applications", "usage"},
			    "deployment");
			if (D.value("format", "") != "veroscript-deployment" ||
			    D.value("formatVersion", "") != FormatVersion ||
			    D.at("target") != TargetJson(ExpectedTarget))
			{
				Fail("deployment-target", "deployment format or target does not match the expected target");
			}
			auto Data = std::make_shared<FInstalledEnvironment::FImpl>();
			const auto DeploymentUsage = D.at("usage").get<std::vector<std::string>>();
			const std::set<std::string> UniqueUsage(DeploymentUsage.begin(), DeploymentUsage.end());
			if (!UniqueUsage.count("runtime") || UniqueUsage.size() != DeploymentUsage.size() ||
			    std::any_of(DeploymentUsage.begin(), DeploymentUsage.end(), [](const auto& U)
			{
				return U != "runtime" && U != "compile";
			}) ||
			    !D.at("packages").is_array() || D.at("packages").size() > O.Limits.MaxPackages || !D.at("roots").is_array())
			{
				Fail("deployment-usage", "deployment contains an invalid usage or package set");
			}
			Data->Owners.push_back(std::move(Lease));
			Data->Layout.Scope = EEnvironmentScope::Project;
			Data->Layout.Root = NativePathText(Root);
			Data->Layout.Management = NativePathText(Root);
			Data->Layout.Packages = NativePathText(Root / "packages");
			Data->Layout.Target = ReadTarget(D["target"]);
			Data->Layout.Host = HostTargetContext();
			const bool bForeignTarget = D.at("target") != TargetJson(Data->Layout.Host);
			Data->Project = NewProject("deployment");
			Data->Project["services"] = D.value("services", J::object());
			Data->Profile = {{"packages", J::object()}};
			Data->Lock = {{"format", "veroscript-lock"}, {"formatVersion", FormatVersion},
			    {"target", D.at("target")}, {"packages", J::array()}};
			std::set<std::string> Names;
			std::size_t Edges = 0, RuntimeEdges = 0;
			for (const auto& R : D.at("packages"))
			{
				W.Check();
				Keys(R, {"name", "path", "generated", "configuration", "loaders", "inventory", "generatedInventory", "identity", "sourceId", "dependencies", "runtimeDependencies", "usage", "textDomain", "artifact"}, "deployment.package");
				const auto& Dependencies = R.at("dependencies");
				const auto& RuntimeDependencies = R.at("runtimeDependencies");
				const auto& Configuration = R.at("configuration");
				if (!Configuration.is_object())
				{
					Fail("manifest-type", "configuration must be object", "deployment.package.configuration");
				}
				if (!Dependencies.is_array() || !RuntimeDependencies.is_array())
				{
					Fail("deployment-dependency", "deployment dependencies must be arrays");
				}
				// Runtime edges are a subset, not an additional charge against the
				// graph budget. Bound both arrays before copying them into vectors.
				if (Dependencies.size() > O.Limits.MaxEdges - Edges ||
				    RuntimeDependencies.size() > O.Limits.MaxEdges - RuntimeEdges)
				{
					Fail("deployment-limit", "deployment dependency graph exceeds its edge budget");
				}
				Edges += Dependencies.size();
				RuntimeEdges += RuntimeDependencies.size();
				auto Rel = R.at("path").get<std::string>(), Gen = R.at("generated").get<std::string>();
				CheckPath(Rel);
				CheckPath(Gen);
				auto PackageRoot = Root / U8Path(Rel), Generated = Root / U8Path(Gen);
				if (!Under(PackageRoot, Root) || !Under(Generated, Root))
				{
					Fail("deployment-path", "deployment entry escapes its root");
				}
				VerifyInventory(PackageRoot, R.at("inventory"), W);
				VerifyInventory(Generated, R.at("generatedInventory"), W);
				if (bForeignTarget)
				{
					VerifyLoaders(Generated, R.at("loaders"), W);
				}
				auto M = Manifest(
				    Parse(Read(PackageRoot / "veroscript-package.json", O.Limits.MaxManifestBytes, &W), O.Limits));
				if (!Digest(R.at("identity").get<std::string>()) ||
				    R.at("identity") != R.at("artifact").at("sha256") ||
				    R.at("artifact").at("manifestSha256") !=
				        HashFile(PackageRoot / "veroscript-package.json", W, O.Limits.MaxManifestBytes) ||
				    !MatchesTarget(M.value("target", J::object()), Data->Layout.Target))
				{
					Fail("deployment-identity", "deployment artifact identity or target differs");
				}
				const auto Requirements = M.value("requires", J::object());
				if (Requirements.contains("core") && !Satisfies(Data->Layout.Target.Core, Requirements.at("core")))
				{
					Fail("core-version", "deployment package requires a different Core version", R.at("name"));
				}
				FInstalledPackage P;
				P.Name = R.at("name");
				P.Version = M.at("version");
				if (M.at("name") != P.Name || !Names.insert(P.Name).second)
				{
					Fail("deployment-package", "deployment contains a duplicate or mismatched package");
				}
				P.Path = NativePathText(PackageRoot);
				P.GeneratedPath = NativePathText(Generated);
				P.ManifestJson = M.dump();
				P.ConfigurationJson = Configuration.dump();
				Data->Project["packages"][P.Name]["configuration"] = Configuration;
				P.LoadersJson = R.at("loaders").dump();
				P.SourceId = R.at("sourceId");
				P.Sha256 = R.at("identity");
				P.ArtifactId = R.at("artifact").at("id");
				P.TextDomain = R.at("textDomain");
				P.Dependencies = R.at("dependencies").get<std::vector<std::string>>();
				P.RuntimeDependencies = R.at("runtimeDependencies").get<std::vector<std::string>>();
				Strings(R.at("usage"), "deployment.package.usage");
				P.Usage = R.at("usage").get<std::vector<std::string>>();
				const auto& AllowedUsage = DeploymentUsage;
				if (P.Usage.empty() || std::any_of(P.Usage.begin(), P.Usage.end(), [&](const auto& U)
				{
					return (U != "runtime" && U != "compile") || std::find(AllowedUsage.begin(), AllowedUsage.end(), U) == AllowedUsage.end();
				}))
				{
					Fail("deployment-usage", "deployment contains an invalid package usage");
				}
				P.Enabled = true;
				Data->Profile["packages"][P.Name] = {{"inventory", R["inventory"]},
				    {"generatedInventory", R["generatedInventory"]},
				    {"artifact", R["artifact"]}};
				Data->Lock["packages"].push_back({{"name", P.Name},
				    {"version", P.Version},
				    {"artifact", R["artifact"]},
				    {"manifest", M},
				    {"source", {{"id", P.SourceId}}},
				    {"ownerId", P.TextDomain},
				    {"dependencies", P.Dependencies},
				    {"usage", P.Usage}});
				Data->Packages.push_back(std::move(P));
			}
			std::map<std::string, const FInstalledPackage*> ByName;
			for (const auto& P : Data->Packages)
			{
				W.Check();
				ByName.emplace(P.Name, &P);
			}
			for (const auto& P : Data->Packages)
			{
				W.Check();
				const auto M = Parse(P.ManifestJson, O.Limits);
				const auto Declared = M.value("dependencies", J::object());
				std::set<std::string> Seen, RuntimeSeen;
				for (const auto& N : P.Dependencies)
				{
					W.Check();
					if (!Seen.insert(N).second)
					{
						Fail("deployment-dependency", "deployment contains a duplicate dependency", N);
					}
					const auto Package = ByName.find(N);
					if (Package == ByName.end() || !Declared.contains(N))
					{
						Fail("deployment-incomplete", "deployment is missing a runtime dependency", N);
					}
					if (!Satisfies(Package->second->Version, Declared.at(N).at("version")))
					{
						Fail("deployment-dependency", "deployment dependency version differs from its manifest", N);
					}
				}
				for (const auto& N : P.RuntimeDependencies)
				{
					W.Check();
					if (!RuntimeSeen.insert(N).second || !Seen.count(N))
					{
						Fail("deployment-dependency", "runtime dependency is duplicated or absent from the selected closure", N);
					}
				}
			}
			const auto Graph = GraphFromLock(Data->Lock, Data->Project, W, false);
			// Optional selection is represented by retained optional edges. A package
			// present for another root does not by itself select an optional edge.
			for (const auto& Pair : Graph.Nodes)
			{
				W.Check();
				const auto Declared = Pair.second.Manifest.value("dependencies", J::object());
				for (const auto& N : Graph.Edges.at(Pair.first))
				{
					if (Declared.at(N).value("optional", false))
					{
						Data->Project["packages"][N]["enabled"] = true;
					}
				}
			}
			for (const auto& P : Data->Packages)
			{
				W.Check();
				std::set<std::string> Expected, Runtime;
				const auto Declared = Graph.Nodes.at(P.Name).Manifest.value("dependencies", J::object());
				for (auto I = Declared.begin(); I != Declared.end(); ++I)
				{
					W.Check();
					if (!DependencyApplies(I.value(), Data->Project, I.key(), Data->Layout.Target, false))
					{
						continue;
					}
					for (const auto& U : I.value().value("usage", std::vector<std::string>{"runtime"}))
					{
						for (const auto& ParentUsage : P.Usage)
						{
							const auto Effective = U == "runtime" ? ParentUsage : U;
							if (!UniqueUsage.count(Effective))
							{
								continue;
							}
							if (!Graph.Edges.at(P.Name).count(I.key()))
							{
								Fail("deployment-incomplete", "deployment is missing a required dependency", I.key());
							}
							if (I.value().contains("sha256") && I.value().at("sha256") != Graph.Nodes.at(I.key()).Artifact.at("sha256"))
							{
								Fail("deployment-dependency", "deployment dependency digest differs from its manifest", I.key());
							}
							if (!Graph.Usage.at(I.key()).count(Effective))
							{
								Fail("deployment-usage", "deployment dependency is missing a required usage", I.key());
							}
							Expected.insert(I.key());
							if (U == "runtime")
							{
								Runtime.insert(I.key());
							}
						}
					}
				}
				if (Expected != Graph.Edges.at(P.Name))
				{
					Fail("deployment-dependency", "deployment contains a dependency outside its selected usage", P.Name);
				}
				if (Runtime != std::set<std::string>(P.RuntimeDependencies.begin(), P.RuntimeDependencies.end()))
				{
					Fail("deployment-dependency", "runtime dependencies differ from the selected manifest closure", P.Name);
				}
			}
			std::set<std::string> Roots;
			for (const auto& N : D.at("roots"))
			{
				W.Check();
				String(N, "deployment.roots");
				if (!Names.count(N.get<std::string>()) || !Roots.insert(N.get<std::string>()).second)
				{
					Fail("deployment-root", "deployment root is absent from its package set");
				}
				const auto* P = ByName.at(N.get<std::string>());
				Data->Project["dependencies"][N.get<std::string>()] = {
				    {"version", P->Version}, {"source", "deployment"}, {"usage", P->Usage}};
			}
			Data->Generation = 1;
			Data->Fingerprint = Id(D);
			Out = std::shared_ptr<const FInstalledEnvironment>(new FInstalledEnvironment(std::move(Data)));
			return true;
		}
		catch (const Detail::FFailure& X)
		{
			E = X.Diagnostic;
			Out.reset();
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "deployment-read";
			E.Message = X.what();
			Out.reset();
			return false;
		}
	}
} // namespace VeroScript::Packages
