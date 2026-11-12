// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include <algorithm>
#include <charconv>

namespace VeroScript::Packages
{
	using namespace Detail;
	bool FPackageManager::Combine(std::shared_ptr<const FInstalledEnvironment> Base,
	    std::shared_ptr<const FInstalledEnvironment> Overlay,
	    std::shared_ptr<const FInstalledEnvironment>& Out, FDiagnostic& E) const
	{
		try
		{
			Out.reset();
			E = {};
			if (!Base)
			{
				Out = std::move(Overlay);
				return true;
			}
			if (!Overlay)
			{
				Out = std::move(Base);
				return true;
			}
			if (TargetJson(Base->Impl->Layout.Target) != TargetJson(Overlay->Impl->Layout.Target))
			{
				Fail("combine-target", "base and overlay environments target different core/ABI contexts");
			}
			struct FPool final : ICandidateRepository
			{
				//~ Begin ICandidateRepository Interface
				J Source(const J& D) const override
				{
					auto I = Aliases.find(D.value("source", ""));
					if (I == Aliases.end())
					{
						Fail("combine-source", "combined dependency has no installed logical source");
					}
					return {{"id", I->second}};
				}

				std::vector<FCandidate> Candidates(const std::string& Name, const J& D, const J&) override
				{
					auto S = Source(D).at("id").get<std::string>();
					std::vector<FCandidate> R;
					for (const auto& C : Nodes[Name])
					{
						if (C.Source.at("id") == S)
						{
							R.push_back(C);
						}
					}
					std::stable_sort(R.begin(), R.end(),
					    [&](const FCandidate& A, const FCandidate& B)
					{
						auto P = Preferred.find(Name);
						if (P != Preferred.end())
						{
							bool X = A.Artifact["sha256"] == P->second,
							     Y = B.Artifact["sha256"] == P->second;
							if (X != Y)
							{
								return X;
							}
						}
						return CompareVersions(A.Manifest["version"], B.Manifest["version"]) > 0;
					});
					return R;
				}
				//~ End ICandidateRepository Interface

				std::map<std::string, std::string> Aliases, Preferred;

				std::map<std::string, std::vector<FCandidate>> Nodes;
			} Pool;
			struct FOrigin
			{
				const FInstalledEnvironment::FImpl* Data;
				const FInstalledPackage* Package;
			};
			std::map<std::string, std::vector<FOrigin>> Origins;
			const auto Alias = [&](const std::string& Id)
			{
				auto A = "installed-" + Hash(Id).substr(0, 16);
				Pool.Aliases[A] = Id;
				return A;
			};
			const auto Add = [&](const FInstalledEnvironment::FImpl& Data, bool IsOverlay)
			{
				std::map<std::string, std::string> SourceIds;
				for (const auto& P : Data.Packages)
				{
					SourceIds[P.Name] = P.SourceId;
				}
				for (const auto& P : Data.Packages)
				{
					FCandidate C;
					C.Manifest = Parse(P.ManifestJson, {});
					C.Source = {{"id", P.SourceId}};
					C.Artifact = Data.Profile.at("packages").at(P.Name).value("artifact", J{});
					if (C.Artifact.is_null())
					{
						for (const auto& N : Data.Lock.at("packages"))
						{
							if (N.at("name") == P.Name)
							{
								C.Artifact = N.at("artifact");
							}
						}
					}
					C.OwnerId = P.TextDomain;
					C.Content = U8Path(P.Path);
					auto D = C.Manifest.value("dependencies", J::object());
					const std::set<std::string> InstalledDependencies(P.Dependencies.begin(), P.Dependencies.end());
					for (auto I = D.begin(); I != D.end();)
					{
						// Frozen snapshots can intentionally omit compile/development
						// dependencies, notably after a runtime deployment export.
						if (!InstalledDependencies.count(I.key()))
						{
							I = D.erase(I);
							continue;
						}
						if (!SourceIds.count(I.key()))
						{
							Fail("combine-incomplete", "installed dependency identity is missing", I.key());
						}
						for (const auto* K : {"archive", "directory", "url"})
						{
							I.value().erase(K);
						}
						I.value()["source"] = Alias(SourceIds.at(I.key()));
						++I;
					}
					C.Manifest["dependencies"] = D;
					auto& Candidates = Pool.Nodes[P.Name];
					if (IsOverlay)
					{
						// Keep the overlay's frozen context first when immutable
						// artifact and version preferences otherwise tie.
						Candidates.insert(Candidates.begin(), std::move(C));
					}
					else
					{
						Candidates.push_back(std::move(C));
					}
					Origins[P.Name].push_back({&Data, &P});
					if (!IsOverlay)
					{
						Pool.Preferred[P.Name] = P.Sha256;
					}
				}
			};
			Add(*Base->Impl, false);
			Add(*Overlay->Impl, true);
			auto Project = Base->Impl->Project;
			auto OverlayRoots = Overlay->Impl->Project.value("dependencies", J::object());
			auto BaseRoots = Base->Impl->Project.value("dependencies", J::object());
			const auto RootSpecs = [&](const FInstalledEnvironment::FImpl& Data)
			{
				auto Roots = Data.Project.value("dependencies", J::object());
				for (auto I = Roots.begin(); I != Roots.end();)
				{
					if (!DependencyApplies(I.value(), Data.Project, I.key(), Data.Layout.Target,
					        Data.Lock.value("development", false)))
					{
						I = Roots.erase(I);
						continue;
					}
					auto P = std::find_if(Data.Packages.begin(), Data.Packages.end(),
					    [&](const FInstalledPackage& N)
					{
						return N.Name == I.key();
					});
					if (P == Data.Packages.end())
					{
						Fail("combine-root", "installed environment is missing a declared root", I.key());
					}
					for (const auto* K : {"archive", "directory", "url"})
					{
						I.value().erase(K);
					}
					I.value()["source"] = Alias(P->SourceId);
					I.value()["version"] = P->Version;
					++I;
				}
				return Roots;
			};
			Project["dependencies"] = RootSpecs(*Base->Impl);
			Project["packages"] = Project.value("packages", J::object());
			for (auto I = OverlayRoots.begin(); I != OverlayRoots.end(); ++I)
			{
				Project["dependencies"].erase(I.key());
				Project["packages"].erase(I.key());
			}
			auto Replacements = RootSpecs(*Overlay->Impl);
			for (auto I = Replacements.begin(); I != Replacements.end(); ++I)
			{
				Project["dependencies"][I.key()] = I.value();
				auto P = std::find_if(Overlay->Impl->Packages.begin(), Overlay->Impl->Packages.end(),
				    [&](const FInstalledPackage& N)
				{
					return N.Name == I.key();
				});
				if (P != Overlay->Impl->Packages.end())
				{
					Pool.Preferred[I.key()] = P->Sha256;
				}
			}
			auto OverlayStates = Overlay->Impl->Project.value("packages", J::object());
			for (auto I = OverlayStates.begin(); I != OverlayStates.end(); ++I)
			{
				if (OverlayRoots.contains(I.key()) || !BaseRoots.contains(I.key()))
				{
					Project["packages"][I.key()] = I.value();
				}
			}
			for (auto I = OverlayRoots.begin(); I != OverlayRoots.end(); ++I)
			{
				auto& State = Project["packages"][I.key()];
				if (!State.contains("configuration"))
				{
					State["configuration"] = J::object();
				}
			}
			auto OverlayServices = Overlay->Impl->Project.value("services", J::object());
			for (auto I = OverlayServices.begin(); I != OverlayServices.end(); ++I)
			{
				Project["services"][I.key()] = I.value();
			}
			FClientOptions Options;
			Options.Offline = true;
			Options.Development = Base->Impl->Lock.value("development", false) ||
			    Overlay->Impl->Lock.value("development", false);
			FWork W{Options};
			auto Graph = Solve(Project, J{{"packages", J::array()}}, Pool, Overlay->Impl->Layout, W, true);
			struct FContextNode
			{
				std::string SourceId, Sha256, Version, OwnerId;
				std::set<std::string> Dependencies;
			};
			using FContextNodes = std::map<std::string, FContextNode>;
			using FContextHashes = std::map<std::string, std::string>;
			const auto ContextHashes = [&](const FContextNodes& Nodes)
			{
				FContextHashes Hashes;
				std::map<std::string, std::size_t> Remaining;
				std::map<std::string, std::vector<std::string>> Dependents;
				std::vector<std::string> Ready;
				for (const auto& N : Nodes)
				{
					W.Check();
					Remaining[N.first] = N.second.Dependencies.size();
					if (N.second.Dependencies.empty())
					{
						Ready.push_back(N.first);
					}
					for (const auto& D : N.second.Dependencies)
					{
						if (!Nodes.count(D))
						{
							Fail("combine-incomplete", "installed dependency context is incomplete", D);
						}
						Dependents[D].push_back(N.first);
					}
				}
				// Hash dependency-first without recursive traversal or expanded closures.
				while (!Ready.empty())
				{
					W.Check();
					const auto Name = std::move(Ready.back());
					Ready.pop_back();
					const auto& N = Nodes.at(Name);
					J Dependencies = J::object();
					for (const auto& D : N.Dependencies)
					{
						Dependencies[D] = Hashes.at(D);
					}
					Hashes[Name] = Id(J{{"source", N.SourceId}, {"sha256", N.Sha256}, {"version", N.Version},
					    {"owner", N.OwnerId}, {"dependencies", std::move(Dependencies)}});
					for (const auto& D : Dependents[Name])
					{
						if (--Remaining.at(D) == 0)
						{
							Ready.push_back(D);
						}
					}
				}
				if (Hashes.size() != Nodes.size())
				{
					Fail("combine-incomplete", "installed dependency context contains a cycle");
				}
				return Hashes;
			};
			FContextNodes SelectedNodes;
			for (const auto& N : Graph.Nodes)
			{
				SelectedNodes.emplace(N.first,
				    FContextNode{N.second.Source.at("id").get<std::string>(),
				        N.second.Artifact.at("sha256").get<std::string>(),
				        N.second.Manifest.at("version").get<std::string>(), N.second.OwnerId,
				        Graph.Edges.at(N.first)});
			}
			const auto SelectedContexts = ContextHashes(SelectedNodes);
			std::map<const FInstalledEnvironment::FImpl*, FContextHashes> InstalledContexts;
			const auto InstalledContext = [&](const FInstalledEnvironment::FImpl* Origin, const std::string& Name) -> const std::string&
			{
				auto Found = InstalledContexts.find(Origin);
				if (Found == InstalledContexts.end())
				{
					FContextNodes Nodes;
					for (const auto& P : Origin->Packages)
					{
						Nodes.emplace(P.Name, FContextNode{P.SourceId, P.Sha256, P.Version, P.TextDomain, std::set<std::string>(P.Dependencies.begin(), P.Dependencies.end())});
					}
					Found = InstalledContexts.emplace(Origin, ContextHashes(Nodes)).first;
				}
				return Found->second.at(Name);
			};
			std::map<const FInstalledPackage*, std::map<std::string, std::string>> LibraryLinkages;
			const auto LibraryLinkage = [&](const FInstalledPackage* Provider, const std::string& Id) -> std::string
			{
				auto Found = LibraryLinkages.find(Provider);
				if (Found == LibraryLinkages.end())
				{
					std::map<std::string, std::string> Entries;
					const auto Manifest = Parse(Provider->ManifestJson, {});
					for (const auto& Library : Manifest.value("entries", J::object()).value("libraries", J::array()))
					{
						Entries.emplace(Library.at("id").get<std::string>(), Library.at("linkage").get<std::string>());
					}
					Found = LibraryLinkages.emplace(Provider, std::move(Entries)).first;
				}
				const auto Entry = Found->second.find(Id);
				return Entry == Found->second.end() ? std::string{} : Entry->second;
			};
			const auto LoaderProvidersMatch = [&](const FOrigin& Origin, const J& Manifest)
			{
				const auto Loaders = Parse(Origin.Package->LoadersJson, {});
				if (Loaders.empty()) return true;
				const auto Entries = Manifest.value("entries", J::object());
				const std::set<std::string> PreviousDependencies(
				    Origin.Package->Dependencies.begin(), Origin.Package->Dependencies.end());
				const auto& SelectedDependencies = Graph.Edges.at(Origin.Package->Name);
				for (const auto* Kind : {"native", "backends"})
				{
					for (const auto& Entry : Entries.value(Kind, J::array()))
					{
						// The receipt identifies entries actually prepared by PrepareLoaders.
						const auto LoaderKey = std::string(Kind) + ":" + (std::string_view(Kind) == "backends" ? Entry.at("service").get<std::string>() + ":" : "") + Entry.at("id").get<std::string>();
						if (!Loaders.contains(LoaderKey))
						{
							continue;
						}
						for (const auto& Ref : Entry.value("libraries", std::vector<std::string>{}))
						{
							W.Check();
							const auto Colon = Ref.find(':');
							if (Colon == std::string::npos)
							{
								continue;
							}
							const auto Name = Ref.substr(0, Colon);
							if (Name == Origin.Package->Name)
							{
								continue;
							}
							const bool HadEdge = PreviousDependencies.count(Name) != 0;
							const bool HasEdge = SelectedDependencies.count(Name) != 0;
							// Deployment can embed a loader library while pruning its
							// compile-only provider. Preserve that frozen, self-contained view.
							if (!HadEdge && !HasEdge)
							{
								continue;
							}
							if (HadEdge != HasEdge) return false;
							const auto Selected = Graph.Nodes.find(Name);
							const auto Providers = Origins.find(Name);
							if (Selected == Graph.Nodes.end() || Providers == Origins.end()) return false;
							const FInstalledPackage* Before = nullptr;
							const FInstalledPackage* After = nullptr;
							for (const auto& Provider : Providers->second)
							{
								if (Provider.Data == Origin.Data)
								{
									Before = Provider.Package;
								}
								if (Selected->second.Source.at("id") == Provider.Package->SourceId &&
								    Selected->second.Artifact.at("sha256") == Provider.Package->Sha256)
								{
									After = Provider.Package;
								}
							}
							if (!Before || !After) return false;
							const auto Id = Ref.substr(Colon + 1);
							const auto PreviousLinkage = LibraryLinkage(Before, Id);
							const auto SelectedLinkage = LibraryLinkage(After, Id);
							if (PreviousLinkage.empty() || PreviousLinkage != SelectedLinkage) return false;
							// Static libraries are already linked into this package's
							// unchanged artifact and are absent from the loader view.
							if (PreviousLinkage == "dynamic" &&
							    (Before->SourceId != After->SourceId || Before->Sha256 != After->Sha256))
							{
								return false;
							}
						}
					}
				}
				return true;
			};
			auto Data = std::make_shared<FInstalledEnvironment::FImpl>();
			Data->Owners = {Base->Impl, Overlay->Impl};
			Data->Layout = Overlay->Impl->Layout;
			Data->Project = Project;
			Data->Lock = {{"packages", J::array()}, {"development", Options.Development}};
			Data->Profile = {{"packages", J::object()}};
			for (const auto& Node : Graph.Nodes)
			{
				const auto& C = Node.second;
				auto Desired = Project.value("packages", J::object())
				                   .value(Node.first, J::object())
				                   .value("configuration", J{});
				const FOrigin* Selected = nullptr;
				bool HasArtifactOrigin = false;
				for (const auto& Origin : Origins.at(Node.first))
				{
					if (C.Source.at("id") == Origin.Package->SourceId &&
					    C.Artifact.at("sha256") == Origin.Package->Sha256)
					{
						HasArtifactOrigin = true;
						const bool ConfigurationMatches = Desired.is_null() ||
						    Parse(Origin.Package->ConfigurationJson, {}) == Desired;
						if ((NeedsInstallationProcessing(C.Manifest) &&
						        (!ConfigurationMatches || InstalledContext(Origin.Data, Node.first) != SelectedContexts.at(Node.first))) ||
						    !LoaderProvidersMatch(Origin, C.Manifest))
						{
							continue;
						}
						if (!Selected)
						{
							Selected = &Origin;
						}
						if (!Desired.is_null() && ConfigurationMatches)
						{
							Selected = &Origin;
						}
					}
				}
				if (!Selected)
				{
					if (HasArtifactOrigin)
					{
						Fail("combine-processing",
						    "effective configuration or dependencies have no matching installation receipt", Node.first);
					}
					Fail("combine-content", "chosen artifact has no installed receipt", Node.first);
				}
				auto P = *Selected->Package;
				auto M = Parse(P.ManifestJson, {});
				if (!Desired.is_null() && Parse(P.ConfigurationJson, {}) != Desired)
				{
					if (NeedsInstallationProcessing(M))
					{
						Fail("combine-processing",
						    "effective configuration has no matching installation receipt", P.Name);
					}
					P.ConfigurationJson = Desired.dump();
				}
				P.Enabled = Graph.Active.count(P.Name) != 0;
				P.Dependencies.assign(Graph.Edges[P.Name].begin(), Graph.Edges[P.Name].end());
				P.RuntimeDependencies.clear();
				for (const auto& N : P.Dependencies)
				{
					auto Usage = C.Manifest.at("dependencies")
					                 .at(N)
					                 .value("usage", std::vector<std::string>{"runtime"});
					if (std::find(Usage.begin(), Usage.end(), "runtime") != Usage.end())
					{
						P.RuntimeDependencies.push_back(N);
					}
				}
				P.Usage.assign(Graph.Usage[P.Name].begin(), Graph.Usage[P.Name].end());
				Data->Packages.push_back(P);
				Data->Profile["packages"][P.Name] = Selected->Data->Profile.at("packages").at(P.Name);
				Data->Profile["packages"][P.Name]["artifact"] = C.Artifact;
				for (const auto& L : Selected->Data->Lock.at("packages"))
				{
					if (L.at("name") == P.Name)
					{
						auto Effective = L;
						Effective["dependencies"] = Graph.Edges[P.Name];
						Effective["usage"] = Graph.Usage[P.Name];
						Data->Lock["packages"].push_back(std::move(Effective));
						break;
					}
				}
			}
			Data->Composition = {{"base",
			                         {{"root", Base->Impl->Layout.Root},
			                             {"generation", Base->Impl->Generation},
			                             {"fingerprint", Base->Impl->Fingerprint}}},
			    {"overlay",
			        {{"root", Overlay->Impl->Layout.Root},
			            {"generation", Overlay->Impl->Generation},
			            {"fingerprint", Overlay->Impl->Fingerprint}}}};
			Data->Fingerprint =
			    Id(J{{"sources", Data->Composition}, {"configuration", Project}, {"graph", Data->Lock}});
			auto H = Data->Fingerprint.substr(0, 16);
			std::from_chars(H.data(), H.data() + H.size(), Data->Generation, 16);
			Out = std::shared_ptr<const FInstalledEnvironment>(new FInstalledEnvironment(std::move(Data)));
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Diagnostic;
			Out.reset();
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "environment-combine";
			E.Message = X.what();
			Out.reset();
			return false;
		}
	}
} // namespace VeroScript::Packages
