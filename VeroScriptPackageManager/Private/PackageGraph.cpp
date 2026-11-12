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
	}

	bool DependencyApplies(const J& D, const J& Project, const std::string& Name,
						   const FTargetContext& Target, bool Development)
	{
		return When(D, Target) && Relevant(D, Development) &&
			   (!D.value("optional", false) || SelectedOptional(Project, Name));
	}
	void Activate(FGraph& G, const J& P, const FTargetContext& Target, bool Development)
	{
		G.Active.clear();
		std::vector<std::string> Chain;
		std::function<void(const std::string&)> Visit = [&](const std::string& N)
		{
			if (!Enabled(P, N))
			{
				FFailure F("disabled-dependency", "an active package requires a disabled dependency", N);
				F.Diagnostic.DependencyChain = Chain;
				F.Diagnostic.DependencyChain.push_back(N);
				throw F;
			}
			if (!G.Active.insert(N).second)
			{
				return;
			}
			if (Chain.size() >= 256)
			{
				Fail("dependency-depth", "active dependency graph exceeds safe nesting");
			}
			Chain.push_back(N);
			for (const auto& D : G.Edges[N])
			{
				Visit(D);
			}
			Chain.pop_back();
		};
		auto Roots = P.value("dependencies", J::object());
		for (auto I = Roots.begin(); I != Roots.end(); ++I)
		{
			if (G.Nodes.count(I.key()) && Enabled(P, I.key()) &&
				DependencyApplies(I.value(), P, I.key(), Target, Development))
			{
				Visit(I.key());
			}
		}
		std::map<std::string, std::string> Modules, Backends;
		for (const auto& N : G.Active)
		{
			const auto& M = G.Nodes.at(N).Manifest;
			auto Conflicts = M.value("conflicts", J::object());
			for (auto I = Conflicts.begin(); I != Conflicts.end(); ++I)
			{
				if (G.Active.count(I.key()) && Satisfies(G.Nodes.at(I.key()).Manifest["version"], I.value()))
				{
					FFailure F("package-conflict", "active packages declare a conflict", N);
					F.Diagnostic.DependencyChain = {N, I.key()};
					throw F;
				}
			}
			auto Entries = M.value("entries", J::object());
			auto Public = Entries.value("modules", J::object());
			for (auto I = Public.begin(); I != Public.end(); ++I)
			{
				auto Key = I.key();
				std::transform(Key.begin(), Key.end(), Key.begin(),
					[](unsigned char C)
					{
						return char(std::tolower(C));
					});
				if (!Modules.emplace(Key, N).second)
				{
					Fail("module-conflict", "active packages export the same module", I.key());
				}
			}
			for (const auto& B : Entries.value("backends", J::array()))
			{
				auto Key = B.at("service").get<std::string>() + ":" + B.at("id").get<std::string>();
				if (!Backends.emplace(Key, N).second)
				{
					Fail("backend-conflict", "multiple active packages provide the same backend", Key);
				}
			}
		}
	}
	std::string RequirementsFingerprint(const J& P, const FEnvironmentLayout& L, const J& Registries,
										bool Development, const J& LockedPackages)
	{
		J Selected = J::array();
		auto Packages = P.value("packages", J::object());
		auto Need = P.value("dependencies", J::object());
		std::set<std::string> Optional;
		const auto FindOptional = [&](const J& D)
		{
			for (auto I = D.begin(); I != D.end(); ++I)
			{
				if (I.value().value("optional", false))
				{
					Optional.insert(I.key());
				}
			}
		};
		FindOptional(Need);
		for (const auto& N : LockedPackages)
		{
			FindOptional(N.at("manifest").value("dependencies", J::object()));
		}
		for (auto I = Packages.begin(); I != Packages.end(); ++I)
		{
			if (Optional.count(I.key()) && I.value().value("enabled", false))
			{
				Selected.push_back(I.key());
			}
		}
		J Sources = J::object();
		for (auto I = Registries.begin(); I != Registries.end(); ++I)
		{
			Sources[I.key()] = {{"id", I.value().at("id")}, {"api", I.value().at("api")}};
		}
		return Id(J{{"dependencies", Need},
					{"selectedOptional", Selected},
					{"target", TargetJson(L.Target)},
					{"development", Development},
					{"sources", Sources}});
	}
	J LockJson(const FGraph& G, const J& P, const FEnvironmentLayout& L, const J& Registries,
			   bool Development)
	{
		J R = {{"format", "veroscript-lock"},	 {"formatVersion", FormatVersion},
			   {"target", TargetJson(L.Target)}, {"development", Development},
			   {"sources", J::object()},		 {"packages", J::array()}};
		for (auto I = Registries.begin(); I != Registries.end(); ++I)
		{
			R["sources"][I.key()] = {{"id", I.value().at("id")}, {"api", I.value().at("api")}};
		}
		for (const auto& Pair : G.Nodes)
		{
			const auto& C = Pair.second;
			R["packages"].push_back({{"name", Pair.first},
									 {"version", C.Manifest["version"]},
									 {"source", C.Source},
									 {"ownerId", C.OwnerId},
									 {"usage", G.Usage.at(Pair.first)},
									 {"dependencies", G.Edges.count(Pair.first) ? G.Edges.at(Pair.first)
																				: std::set<std::string>{}},
									 {"artifact", C.Artifact},
									 {"manifest", C.Manifest}});
		}
		R["requirementsFingerprint"] = RequirementsFingerprint(P, L, Registries, Development, R["packages"]);
		return R;
	}
} // namespace VeroScript::Packages::Detail
