// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptModuleVisibility.h"
#include "VeroScriptAnalysisControl.h"

namespace VeroScript::ModuleVisibility
{
	FImportIndex::FImportIndex(const std::vector<FImportDecl>& Imports,
	    const std::vector<FImportDecl>& AdditionalImports)
	{
		const auto AddImports = [&](const std::vector<FImportDecl>& Edges)
		{
			for (std::size_t Position = 0; Position < Edges.size(); ++Position)
			{
				if ((Position & 255u) == 0u)
				{
					CheckAnalysisCancellation();
				}
				const auto& Import = Edges[Position];
				if (!Import.ResolvedPath.empty())
				{
					ImportsByImporter[Import.ImporterModulePath].push_back({Import.ResolvedPath, Import.bReExport});
				}
			}
		};
		AddImports(Imports);
		AddImports(AdditionalImports);
	}

	std::unordered_set<std::string> FImportIndex::BuildVisibleSet(const std::string& ModulePath,
	    std::size_t& PathBytes) const
	{
		std::unordered_set<std::string> Visible{ModulePath};
		PathBytes = 2 * (sizeof(std::string) + ModulePath.size());
		std::vector<const std::string*> Pending;
		const auto AppendImports = [&](const std::string& Importer, bool bReExportOnly)
		{
			CheckAnalysisCancellation();
			const auto Edges = ImportsByImporter.find(Importer);
			if (Edges == ImportsByImporter.end()) return;
			for (const auto& Edge : Edges->second)
			{
				if ((!bReExportOnly || Edge.bReExport) && Visible.insert(Edge.ModulePath).second)
				{
					PathBytes += sizeof(std::string) + Edge.ModulePath.size();
					Pending.push_back(&Edge.ModulePath);
				}
			}
		};
		AppendImports(ModulePath, false);
		while (!Pending.empty())
		{
			const auto* Importer = Pending.back();
			Pending.pop_back();
			AppendImports(*Importer, true);
		}

		return Visible;
	}

	std::unordered_set<std::string> FImportIndex::CollectVisibleFrom(const std::string& ModulePath) const
	{
		std::size_t PathBytes = 0;
		return BuildVisibleSet(ModulePath, PathBytes);
	}

	const std::unordered_set<std::string>& FImportIndex::VisibleFrom(const std::string& ModulePath) const
	{
		const auto Cached = VisibleByImporter.find(ModulePath);
		if (Cached != VisibleByImporter.end()) return Cached->second;
		if (RecentOverflow && RecentOverflow->ModulePath == ModulePath) return RecentOverflow->Paths;

		std::size_t PathBytes = 0;
		auto Visible = BuildVisibleSet(ModulePath, PathBytes);

		// Broad graphs can repeat the same closure across many importers. Bound
		// retained path storage and still reuse the most recent oversized lookup.
		constexpr std::size_t MaxCachedPathBytes = 8u * 1024u * 1024u;
		if (PathBytes <= MaxCachedPathBytes - CachedPathBytes)
		{
			const auto Result = VisibleByImporter.emplace(ModulePath, std::move(Visible));
			if (Result.second)
			{
				CachedPathBytes += PathBytes;
			}
			return Result.first->second;
		}
		RecentOverflow.emplace(FVisibilityEntry{ModulePath, std::move(Visible)});
		return RecentOverflow->Paths;
	}
}
