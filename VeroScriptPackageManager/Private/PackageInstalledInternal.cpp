// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include "PackageSignatures.h"
#include "PackageSources.h"
#include <algorithm>
#include <random>

namespace VeroScript::Packages::Detail
{
	void ConfigureStorage(FEnvironmentLayout& L, const FEnvironmentInput& Input, const J& P, const J& S)
	{
		if (Input.PackagesDirectory.empty())
		{
			auto Settings = P.value("packageManager", J::object());
			if (Settings.contains("directory"))
			{
				L.Packages = NativePathText(Absolute(Settings["directory"], L.Root));
			}
			else if (S.contains("packageRoot"))
			{
				L.Packages = NativePathText(Absolute(S["packageRoot"], L.Root));
			}
		}
		NoLinks(U8Path(L.Packages), true);
		for (const auto& Reserved :
		    {U8Path(L.Requirements), U8Path(L.State), U8Path(L.Management) / "transaction.json",
		        ManagementRoot(L) / "manager.lock", U8Path(L.Packages) / ".vero-owner.json",
		        U8Path(L.Packages) / ".vero-store.lock"})
		{
			if (PathUtil::Equal(L.Lockfile, NativePathText(Reserved)))
			{
				Fail("layout-conflict", "lockfile overlaps an environment record or synchronization file",
				    L.Lockfile);
			}
		}
	}
	fs::path ManagementRoot(const FEnvironmentLayout& L)
	{
		return U8Path(L.Management);
	}
	std::string ProfileKey(const FEnvironmentLayout& L)
	{
		return Hash(PathReference(U8Path(L.Lockfile), U8Path(L.Root)));
	}
	J ReadRecord(const fs::path& P, const FWork& W)
	{
		auto Limits = W.Options.Limits;
		auto Name = NativePathText(P.filename());
		auto Budget = Name == "veroscript-project.json" || Name == "veroscript-package.json" || Name == "environment.json" ? Limits.MaxManifestBytes
		    : Name == "transaction.json"                                                                                   ? Limits.MaxJournalBytes
		                                                                                                                   : Limits.MaxStateBytes;
		Limits.MaxManifestBytes = Budget;
		return Parse(Read(P, Budget, &W), Limits);
	}
	J NewProject(const std::string& Name)
	{
		return {
		    {"format", "veroscript-project"}, {"formatVersion", FormatVersion}, {"name", Name},
		    {"version", FormatVersion}, {"dependencies", J::object()}, {"packages", J::object()}};
	}
	J LoadRequirements(const FEnvironmentLayout& L, const FWork& W, bool AllowMissing)
	{
		if (!fs::exists(U8Path(L.Requirements)))
		{
			if (!AllowMissing)
			{
				Fail("requirements-missing", "environment has no requirements; install a "
				                             "package explicitly first");
			}
			return NewProject("project-" + Hash(L.Root).substr(0, 16));
		}
		return Project(ReadRecord(U8Path(L.Requirements), W));
	}
	J LoadState(const FEnvironmentLayout& L, const FWork& W)
	{
		if (!fs::exists(U8Path(L.State)))
		{
			return {{"format", "veroscript-installation"},
			    {"formatVersion", FormatVersion},
			    {"generation", 0},
			    {"profiles", J::object()},
			    {"retired", J::array()}};
		}
		auto S = ReadRecord(U8Path(L.State), W);
		Keys(S,
		    {"format", "formatVersion", "environmentId", "generation", "profiles", "retired", "packageRoot"},
		    "state");
		if (S.value("format", "") != "veroscript-installation" ||
		    S.value("formatVersion", "") != FormatVersion || !S.at("profiles").is_object() ||
		    !S.at("generation").is_number_unsigned())
		{
			Fail("state-format", "invalid installation state");
		}
		return S;
	}
	std::string StoreOwner(const FEnvironmentLayout& L, const J& State, const FWork& W)
	{
		auto Marker = U8Path(L.Packages) / ".vero-owner.json";
		if (fs::exists(Marker))
		{
			auto O = ReadRecord(Marker, W);
			if (O.value("format", "") != "veroscript-package-store" ||
			    O.value("formatVersion", "") != FormatVersion ||
			    O.value("management", "") != PathReference(ManagementRoot(L), U8Path(L.Packages)))
			{
				Fail("store-owner", "package directory belongs to another management environment");
			}
			if (State.contains("environmentId") && State["environmentId"] != O.at("environmentId"))
			{
				Fail("store-owner", "installation state does not own its package store");
			}
			return O.at("environmentId");
		}
		if (State.contains("environmentId"))
		{
			Fail("store-owner", "package store ownership record is missing; recovery "
			                    "or reinstall is required");
		}
		return Hash("veroscript-package-owner:0.1.1:" + NativePathText(ManagementRoot(L)));
	}
	FGraph GraphFromLock(const J& L, const J& Project, const FWork& W, bool ActivateGraph)
	{
		W.Check();
		if (L.value("format", "") != "veroscript-lock" || L.value("formatVersion", "") != FormatVersion)
		{
			Fail("lock-format", "expected veroscript-lock 0.1.1");
		}
		const auto& Packages = L.at("packages");
		if (!Packages.is_array())
		{
			Fail("lock-invalid", "locked packages must be an array");
		}
		if (Packages.size() > W.Options.Limits.MaxPackages)
		{
			Fail("graph-limit", "dependency graph exceeds its package or edge budget");
		}
		const auto Target = ReadTarget(L.at("target"));
		const bool Development = L.value("development", false);
		std::size_t EdgeCount = 0;
		const auto AddEdges = [&](std::size_t Count)
		{
			if (Count > W.Options.Limits.MaxEdges - EdgeCount)
			{
				Fail("edge-limit", "dependency edge budget exhausted");
			}
			EdgeCount += Count;
		};
		if (auto Roots = Project.find("dependencies"); Roots != Project.end())
		{
			for (auto I = Roots->begin(); I != Roots->end(); ++I)
			{
				W.Check();
				if (DependencyApplies(I.value(), Project, I.key(), Target, Development))
				{
					AddEdges(1);
				}
			}
		}
		FGraph G;
		for (const auto& P : Packages)
		{
			W.Check();
			FCandidate C;
			auto N = P.at("name").get<std::string>();
			if (!Name(N) || G.Nodes.count(N))
			{
				Fail("lock-invalid", "invalid or duplicate locked package name");
			}
			C.Manifest = Manifest(P.at("manifest"));
			C.Source = P.at("source");
			C.Artifact = P.at("artifact");
			C.OwnerId = P.at("ownerId");
			if (C.Manifest["name"] != N || C.Manifest["version"] != P.at("version") ||
			    !Digest(C.Artifact.at("sha256").get<std::string>()) ||
			    !Digest(C.Artifact.at("manifestSha256").get<std::string>()))
			{
				Fail("lock-invalid", "locked manifest identity is inconsistent");
			}
			G.Nodes.emplace(N, std::move(C));
			const auto& Dependencies = P.at("dependencies");
			if (!Dependencies.is_array())
			{
				Fail("lock-invalid", "locked dependencies must be an array");
			}
			// Charge the original array before conversion; duplicates must not
			// hide unbounded input work behind a small deduplicated edge set.
			AddEdges(Dependencies.size());
			auto& Edges = G.Edges[N];
			for (const auto& Dependency : Dependencies)
			{
				W.Check();
				if (!Dependency.is_string() || !Edges.insert(Dependency.get<std::string>()).second)
				{
					Fail("lock-invalid", "invalid or duplicate locked dependency");
				}
			}
			G.Usage[N] = P.at("usage").get<std::set<std::string>>();
		}
		std::map<std::string, std::size_t> Heights;
		std::set<std::string> Active;
		const auto MaxDepth = std::min<std::size_t>(W.Options.Limits.MaxDependencyDepth, 256);
		std::function<std::size_t(const std::string&)> Visit = [&](const std::string& N) -> std::size_t
		{
			W.Check();
			if (auto Height = Heights.find(N); Height != Heights.end())
			{
				return Height->second;
			}
			if (!G.Nodes.count(N))
			{
				Fail("lock-invalid", "locked dependency is absent", N);
			}
			if (!Active.insert(N).second)
			{
				Fail("dependency-cycle", "locked dependency graph contains a cycle", N);
			}
			if (Active.size() > MaxDepth)
			{
				Fail("dependency-depth", "dependency graph exceeds its nesting budget");
			}
			std::size_t Height = 1;
			for (const auto& D : G.Edges[N])
			{
				const auto ChildHeight = Visit(D);
				if (ChildHeight >= MaxDepth)
				{
					Fail("dependency-depth", "dependency graph exceeds its nesting budget");
				}
				Height = std::max(Height, ChildHeight + 1);
			}
			Active.erase(N);
			Heights.emplace(N, Height);
			G.Order.push_back(N);
			return Height;
		};
		for (const auto& N : G.Nodes)
		{
			Visit(N.first);
		}
		if (ActivateGraph)
		{
			W.Check();
			Activate(G, Project, Target, Development);
		}
		return G;
	}
	std::shared_ptr<FInstalledEnvironment::FImpl> ReadInstalled(const FEnvironmentLayout& L, FWork& W)
	{
		W.Check();
		const auto JournalPath = U8Path(L.Management) / "transaction.json";
		if (fs::exists(JournalPath))
		{
			const auto Journal = ReadRecord(JournalPath, W);
			if (!Journal.is_object() || Journal.value("phase", J{}) != "committed")
			{
				Fail("recovery-required", "installation transaction needs writer recovery");
			}
			ValidateTransactionJournal(L, Journal, W);
			for (const auto& Record : Journal.at("writes"))
			{
				W.Check();
				if (!TransactionRecordMatches(L, Record, true, W))
				{
					Fail("recovery-required", "committed transaction is not fully applied",
					    Record.at("path").get<std::string>());
				}
			}
		}
		auto P = LoadRequirements(L, W, false), State = LoadState(L, W);
		if (!fs::exists(U8Path(L.Lockfile)))
		{
			Fail("environment-uninstalled", "requirements have no installed profile for this lockfile");
		}
		auto Lock = ReadRecord(U8Path(L.Lockfile), W);
		return BuildInstalled(L, W, P, State, Lock, StoreOwner(L, State, W));
	}
	std::shared_ptr<FInstalledEnvironment::FImpl> BuildInstalled(const FEnvironmentLayout& L, FWork& W,
	    const J& P, const J& State, const J& Lock,
	    const std::string& Owner)
	{
		auto Key = ProfileKey(L);
		if (!State.at("profiles").contains(Key))
		{
			Fail("environment-uninstalled", "requirements have no installed profile for this lockfile");
		}
		auto Profile = State.at("profiles").at(Key);
		if (Profile.at("lockSha256") != Id(Lock) || Profile.at("target") != TargetJson(L.Target) ||
		    Lock.at("target") != TargetJson(L.Target))
		{
			Fail("environment-stale", "lock or target differs from installed profile");
		}
		if (Absolute(State.at("packageRoot"), L.Root) != U8Path(L.Packages))
		{
			Fail("store-location", "package directory differs from committed management state");
		}

		auto Settings = P;
		Settings["_lockedSources"] = Lock.at("sources");
		const auto Registries = RegistrySettings(Settings, W.Options);
		if (Lock.at("requirementsFingerprint") != RequirementsFingerprint(P, L, Registries, Lock.value("development", false), Lock.at("packages")))
		{
			Fail("lock-stale", "requirements, logical sources or optional selections "
			                   "changed; run install");
		}
		auto G = GraphFromLock(Lock, P, W);
		for (const auto& N : G.Nodes)
		{
			if (N.second.Artifact.contains("signatures"))
			{
				ValidateDistributionSignatures(N.second.Artifact.at("signatures"), N.second.Artifact.at("sha256"), N.second.Artifact.at("size"), W);
			}
		}
		auto Data = std::make_shared<FInstalledEnvironment::FImpl>();
		Data->Layout = L;
		Data->Project = P;
		Data->Lock = Lock;
		Data->Profile = Profile;
		Data->Generation = State.at("generation");
		for (auto I = Profile.at("packages").begin(); I != Profile.at("packages").end(); ++I)
		{
			const auto& Record = I.value();
			auto C = G.Nodes.find(I.key());
			if (C == G.Nodes.end())
			{
				Fail("state-invalid", "installed receipt has no corresponding locked package");
			}
			auto Relative = Record.at("path").get<std::string>();
			CheckPath(Relative);
			auto Root = U8Path(L.Packages) / U8Path(Relative);
			if (!Under(Root, U8Path(L.Packages)))
			{
				Fail("state-path", "receipt path escapes package root");
			}
			NoLinks(Root);
			auto Lease = std::make_shared<FFileLock>(Root / ".vero-lease", false, false, true, &W);
			auto Receipt = ReadRecord(Root / ".vero-receipt.json", W);
			if (Receipt.at("environmentId") != Owner || Receipt.at("artifact") != C->second.Artifact ||
			    Receipt.at("manifestSha256") != C->second.Artifact.at("manifestSha256") ||
			    Receipt.at("inventory") != Record.at("inventory"))
			{
				Fail("receipt-invalid", "immutable package receipt ownership or identity differs");
			}
			VerifyInventory(Root, Record["inventory"], W);
			if (Manifest(ReadRecord(Root / "veroscript-package.json", W)) != C->second.Manifest ||
			    C->second.Artifact.at("manifestSha256") !=
			        HashFile(Root / "veroscript-package.json", W, W.Options.Limits.MaxManifestBytes))
			{
				Fail("manifest-identity", "installed manifest differs from locked metadata");
			}
			auto GenRelative = Record.at("generated").get<std::string>();
			CheckPath(GenRelative);
			auto Gen = U8Path(L.Management) / U8Path(GenRelative);
			if (!Under(Gen, U8Path(L.Management)))
			{
				Fail("state-path", "generated directory escapes management root");
			}
			auto GeneratedLease = std::make_shared<FFileLock>(Gen / ".vero-lease", false, false, true, &W);
			auto GenReceipt = ReadRecord(Gen / ".vero-receipt.json", W);
			if (GenReceipt.at("environmentId") != Owner ||
			    GenReceipt.at("inventory") != Record.at("generatedInventory"))
			{
				Fail("receipt-invalid", "generated receipt ownership or inventory differs");
			}
			VerifyInventory(Gen, Record.at("generatedInventory"), W);
			VerifyLoaders(Gen, Record.value("loaders", J::object()), W);
			auto Configuration = P.value("packages", J::object())
			                         .value(I.key(), J::object())
			                         .value("configuration", J::object());
			if (Configuration != Record.value("configuration", J::object()) &&
			    NeedsInstallationProcessing(C->second.Manifest))
			{
				Fail("processing-required",
				    "package configuration changed; installation processing is required", I.key());
			}
			FInstalledPackage N;
			N.Name = I.key();
			N.Version = C->second.Manifest["version"];
			N.SourceId = C->second.Source["id"];
			N.ArtifactId = C->second.Artifact["id"];
			N.Sha256 = C->second.Artifact["sha256"];
			N.Path = NativePathText(Root);
			N.GeneratedPath = NativePathText(Gen);
			N.LoadersJson = Record.value("loaders", J::object()).dump();
			N.ManifestJson = C->second.Manifest.dump();
			N.ConfigurationJson = Configuration.dump();
			N.TextDomain = C->second.OwnerId;
			N.Dependencies.assign(G.Edges[N.Name].begin(), G.Edges[N.Name].end());
			N.Usage.assign(G.Usage[N.Name].begin(), G.Usage[N.Name].end());
			N.Enabled = G.Active.count(N.Name) != 0;
			for (const auto& Dependency : N.Dependencies)
			{
				auto Spec = C->second.Manifest.value("dependencies", J::object()).at(Dependency);
				auto Usage = Spec.value("usage", std::vector<std::string>{"runtime"});
				if (std::find(Usage.begin(), Usage.end(), "runtime") != Usage.end())
				{
					N.RuntimeDependencies.push_back(Dependency);
				}
			}
			Data->Owners.push_back(std::move(Lease));
			Data->Owners.push_back(std::move(GeneratedLease));
			Data->Packages.push_back(std::move(N));
		}
		if (Data->Packages.size() != G.Nodes.size())
		{
			Fail("state-invalid", "locked graph is not fully installed");
		}
		auto Identity = Profile;
		Identity.erase("generation");
		for (auto& N : Identity["packages"])
		{
			N.erase("processing");
		}
		Data->Fingerprint = Id(J{{"profile", Identity},
		    {"root", L.Root},
		    {"storage", L.Packages},
		    {"packages", P.value("packages", J::object())},
		    {"services", P.value("services", J::object())},
		    {"target", TargetJson(L.Target)}});
		return Data;
	}
} // namespace VeroScript::Packages::Detail
