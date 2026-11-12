// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include <algorithm>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		bool SelectedOptional(const J& P, const std::string& N)
		{
			return P.value("packages", J::object()).value(N, J::object()).value("enabled", false);
		}
		bool Enabled(const J& P, const std::string& N)
		{
			return P.value("packages", J::object()).value(N, J::object()).value("enabled", true);
		}
		bool When(const J& D, const FTargetContext& T)
		{
			return MatchesTarget(D.value("when", J::object()), T);
		}
		bool Relevant(const J& D, bool Development)
		{
			auto U = D.value("usage", std::vector<std::string>{"runtime"});
			return std::find(U.begin(), U.end(), "runtime") != U.end() ||
				   std::find(U.begin(), U.end(), "compile") != U.end() || Development;
		}
		struct Requirement
		{
			J Spec;
			std::string Parent;
		};
		bool Sort(FGraph& G, std::vector<std::string>& Cycle, std::size_t MaxDepth)
		{
			std::map<std::string, unsigned> State;
			std::map<std::string, std::size_t> Heights;
			std::vector<std::string> Stack;
			G.Order.clear();
			std::function<bool(const std::string&)> Visit = [&](const std::string& N)
			{
				if (State[N] == 2)
				{
					return true;
				}
				if (State[N] == 1)
				{
					auto I = std::find(Stack.begin(), Stack.end(), N);
					Cycle.assign(I, Stack.end());
					Cycle.push_back(N);
					return false;
				}
				if (Stack.size() >= MaxDepth)
				{
					Fail("dependency-depth", "dependency graph exceeds its nesting budget");
				}
				State[N] = 1;
				Stack.push_back(N);
				std::size_t Height = 1;
				for (const auto& D : G.Edges[N])
				{
					if (!Visit(D))
					{
						return false;
					}
					const auto ChildHeight = Heights.at(D);
					if (ChildHeight >= MaxDepth)
					{
						Fail("dependency-depth", "dependency graph exceeds its nesting budget");
					}
					Height = std::max(Height, ChildHeight + 1);
				}
				Stack.pop_back();
				State[N] = 2;
				Heights.emplace(N, Height);
				G.Order.push_back(N);
				return true;
			};
			for (const auto& N : G.Nodes)
			{
				if (!Visit(N.first))
				{
					return false;
				}
			}
			return true;
		}
	} // namespace
	FGraph Solve(const J& P, const J& Previous, ICandidateRepository& Repository,
				 const FEnvironmentLayout& Layout, FWork& Work, const bool InstalledOnly)
	{
		FGraph Result;
		std::map<std::string, FCandidate> Chosen;
		std::map<std::string, J> Locked;
		for (const auto& L : Previous.value("packages", J::array()))
		{
			Locked[L.at("name")] = L;
		}
		FFailure Last("dependency-conflict", "no package version satisfies all dependency constraints");
		struct FDecision
		{
			std::string Name;
			std::vector<FCandidate> Candidates;
			std::size_t Next = 0;
		};
		FDecision Pending;
		auto Evaluate = [&]() -> int
		{
			Work.Check();
			if (++Work.Steps > Work.Options.Limits.MaxSolverSteps)
			{
				Fail("solver-limit", "dependency solver step budget exhausted");
			}
			std::map<std::string, std::vector<Requirement>> Needs;
			std::map<std::string, std::set<std::string>> Edges;
			std::size_t Requirements = 0;
			const auto Roots = P.value("dependencies", J::object());
			const auto Add = [&](const std::string& Name, const J& D, const std::string& Parent)
			{
				if (!DependencyApplies(D, P, Name, Layout.Target, Work.Options.Development))
				{
					return;
				}
				if (++Requirements > Work.Options.Limits.MaxEdges)
				{
					Fail("edge-limit", "dependency edge budget exhausted");
				}
				auto Effective = D;
				// An explicit project source also constrains source-unspecified
				// transitive requirements. Their versions and hashes still apply.
				if (!Parent.empty() && !D.contains("source") && !D.contains("archive") && !D.contains("directory") &&
					!D.contains("url") && Roots.contains(Name) &&
					DependencyApplies(Roots.at(Name), P, Name, Layout.Target, Work.Options.Development))
				{
					for (const auto* Key : {"source", "archive", "directory", "url"})
					{
						if (Roots.at(Name).contains(Key))
						{
							Effective[Key] = Roots.at(Name).at(Key);
						}
					}
				}
				Needs[Name].push_back({std::move(Effective), Parent});
				if (!Parent.empty())
				{
					Edges[Parent].insert(Name);
				}
			};
			for (auto I = Roots.begin(); I != Roots.end(); ++I)
			{
				Add(I.key(), I.value(), {});
			}
			std::set<std::string> Expanded;
			for (;;)
			{
				bool More = false;
				std::vector<std::string> Keys;
				for (const auto& N : Needs)
				{
					Keys.push_back(N.first);
				}
				for (const auto& N : Keys)
				{
					auto C = Chosen.find(N);
					if (C == Chosen.end() || !Expanded.insert(N).second)
					{
						continue;
					}
					More = true;
					auto D = C->second.Manifest.value("dependencies", J::object());
					for (auto I = D.begin(); I != D.end(); ++I)
					{
						Add(I.key(), I.value(), N);
					}
				}
				if (!More)
				{
					break;
				}
			}
			std::size_t EdgeCount = 0;
			for (const auto& E : Edges)
			{
				EdgeCount += E.second.size();
			}
			if (Needs.size() > Work.Options.Limits.MaxPackages || EdgeCount > Work.Options.Limits.MaxEdges)
			{
				Fail("graph-limit", "dependency graph exceeds its package or edge budget");
			}
			for (const auto& N : Needs)
			{
				auto First = Repository.Source(N.second.front().Spec);
				for (const auto& R : N.second)
				{
					if (Repository.Source(R.Spec)["id"] != First["id"])
					{
						Last = FFailure("source-conflict", "dependency requires multiple logical sources",
										N.first);
						Last.Diagnostic.DependencyChain = {N.second.front().Parent, R.Parent, N.first};
						return -1;
					}
				}
				auto C = Chosen.find(N.first);
				if (C != Chosen.end())
				{
					for (const auto& R : N.second)
					{
						if (C->second.Source["id"] != First["id"] ||
							!Satisfies(C->second.Manifest["version"], R.Spec["version"]) ||
							(R.Spec.contains("sha256") && R.Spec["sha256"] != C->second.Artifact["sha256"]))
						{
							return -1;
						}
					}
				}
			}
			for (const auto& N : Needs)
			{
				if (!Chosen.count(N.first))
				{
					auto Combined = N.second.front().Spec;
					std::string Range;
					for (const auto& R : N.second)
					{
						if (R.Spec.at("version") == "*")
						{
							continue;
						}
						if (!Range.empty())
						{
							Range += ' ';
						}
						Range += R.Spec.at("version").get<std::string>();
					}
					Combined["version"] = Range.empty() ? "*" : Range;
					auto Candidates = Repository.Candidates(N.first, Combined,
															Locked.count(N.first) ? Locked[N.first] : J{});
					Pending = {N.first, {}, 0};
					for (auto& C : Candidates)
					{
						Work.Check();
						std::string V = C.Manifest["version"];
						// Pruning/composition can only reuse installed artifacts. The
						// opt-in for selecting new prereleases must not reject them.
						bool PreAllowed = InstalledOnly || Work.Options.AllowPrerelease;
						for (const auto& R : N.second)
						{
							PreAllowed = PreAllowed || R.Spec.at("version").get<std::string>().find('-') !=
														   std::string::npos;
						}
						if (V.find('-') != std::string::npos && !PreAllowed)
						{
							continue;
						}
						bool Match = true;
						for (const auto& R : N.second)
						{
							Match = Match && Satisfies(V, R.Spec["version"]) &&
									(!R.Spec.contains("sha256") || R.Spec["sha256"] == C.Artifact["sha256"]);
						}
						if (Match)
						{
							Pending.Candidates.push_back(std::move(C));
						}
					}
					Last.Diagnostic.Package = N.first;
					return 0;
				}
			}
			FGraph G;
			G.Edges = std::move(Edges);
			for (const auto& N : Needs)
			{
				G.Nodes.try_emplace(N.first);
			}
			std::vector<std::string> Cycle;
			if (!Sort(G, Cycle, std::min<std::size_t>(Work.Options.Limits.MaxDependencyDepth, 256)))
			{
				Last = FFailure("dependency-cycle", "package dependencies contain a cycle");
				Last.Diagnostic.DependencyChain = Cycle;
				return -1;
			}
			// Reject invalid metadata graphs before downloading and expanding their artifacts.
			for (const auto& N : Needs)
			{
				auto& C = Chosen.at(N.first);
				Repository.PrepareCandidate(C);
				G.Nodes.at(N.first) = C;
			}
			for (auto I = Roots.begin(); I != Roots.end(); ++I)
			{
				if (G.Nodes.count(I.key()) &&
					DependencyApplies(I.value(), P, I.key(), Layout.Target, Work.Options.Development))
				{
					for (const auto& U : I.value().value("usage", std::vector<std::string>{"runtime"}))
					{
						if (U != "development" || Work.Options.Development)
						{
							G.Usage[I.key()].insert(U);
						}
					}
				}
			}
			for (auto I = G.Order.rbegin(); I != G.Order.rend(); ++I)
			{
				auto D = G.Nodes.at(*I).Manifest.value("dependencies", J::object());
				for (const auto& N : G.Edges[*I])
				{
					for (const auto& U : D.at(N).value("usage", std::vector<std::string>{"runtime"}))
					{
						if (U == "development" && !Work.Options.Development)
						{
							continue;
						}
						if (U == "runtime")
						{
							G.Usage[N].insert(G.Usage[*I].begin(), G.Usage[*I].end());
						}
						else
						{
							G.Usage[N].insert(U);
						}
					}
				}
			}
			try
			{
				Activate(G, P, Layout.Target, Work.Options.Development);
			}
			catch (const FFailure& X)
			{
				Last = X;
				return -1;
			}
			Result = std::move(G);
			return 1;
		};
		std::vector<FDecision> Decisions;
		for (;;)
		{
			auto Status = Evaluate();
			if (Status == 1)
			{
				return Result;
			}
			if (Status == 0)
			{
				Decisions.push_back(std::move(Pending));
			}
			bool Advanced = false;
			while (!Decisions.empty())
			{
				auto& D = Decisions.back();
				Chosen.erase(D.Name);
				if (D.Next < D.Candidates.size())
				{
					Chosen[D.Name] = D.Candidates[D.Next++];
					Advanced = true;
					break;
				}
				Decisions.pop_back();
			}
			if (!Advanced)
			{
				throw Last;
			}
		}
	}
}
