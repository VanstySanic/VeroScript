// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VeroScript::ModuleVisibility
{
	// One snapshot of a resolved import graph. Lookups on the same analysis
	// thread reuse the export closure for each referencing module.
	class FImportIndex
	{
	public:
		explicit FImportIndex(const std::vector<FImportDecl>& Imports,
		    const std::vector<FImportDecl>& AdditionalImports = {});

		// Parser construction takes ownership of a one-use visibility set.
		std::unordered_set<std::string> CollectVisibleFrom(const std::string& ModulePath) const;

		// Consume the result before the next lookup, which may replace the overflow entry.
		const std::unordered_set<std::string>& VisibleFrom(const std::string& ModulePath) const;

	private:
		struct FEdge
		{
			std::string ModulePath;
			bool bReExport;
		};

		struct FVisibilityEntry
		{
			std::string ModulePath;
			std::unordered_set<std::string> Paths;
		};

		std::unordered_set<std::string> BuildVisibleSet(const std::string& ModulePath,
		    std::size_t& PathBytes) const;

		std::unordered_map<std::string, std::vector<FEdge>> ImportsByImporter;
		mutable std::unordered_map<std::string, std::unordered_set<std::string>> VisibleByImporter;
		mutable std::size_t CachedPathBytes = 0;
		mutable std::optional<FVisibilityEntry> RecentOverflow;
	};

	inline std::string ProjectVisibilityKey(const std::string_view ModulePath)
	{
		std::string Key("\x1Fproject:");
		Key.append(ModulePath);
		return Key;
	}

	/**
	 * Modules whose exported symbols are visible from FromModulePath:
	 * From itself, each directly imported module, and the re-export closure
	 * of those imports (via export import).
	 */
	inline void CollectVisibleModulePaths(
		const std::vector<FImportDecl>& Imports,
		const std::string& FromModulePath,
		std::unordered_set<std::string>& OutVisible)
	{
		OutVisible.clear();
		OutVisible.insert(FromModulePath);

		std::vector<std::string> ReExportRoots;
		for (const FImportDecl& ImportDecl : Imports)
		{
			if (ImportDecl.ImporterModulePath != FromModulePath || ImportDecl.ResolvedPath.empty())
			{
				continue;
			}
			OutVisible.insert(ImportDecl.ResolvedPath);
			ReExportRoots.push_back(ImportDecl.ResolvedPath);
		}

		std::unordered_set<std::string> Visited;
		std::vector<std::string> Queue = std::move(ReExportRoots);
		while (!Queue.empty())
		{
			const std::string Current = std::move(Queue.back());
			Queue.pop_back();
			if (!Visited.insert(Current).second)
			{
				continue;
			}
			for (const FImportDecl& ImportDecl : Imports)
			{
				if (!ImportDecl.bReExport
					|| ImportDecl.ImporterModulePath != Current
					|| ImportDecl.ResolvedPath.empty())
				{
					continue;
				}
				OutVisible.insert(ImportDecl.ResolvedPath);
				Queue.push_back(ImportDecl.ResolvedPath);
			}
		}
	}

	inline bool IsExportedModuleVisible(
		const std::vector<FImportDecl>& Imports,
		const std::string& SymbolModulePath,
		const std::string& ReferenceModulePath)
	{
		if (SymbolModulePath == ReferenceModulePath || SymbolModulePath.empty() || ReferenceModulePath.empty())
		{
			return true;
		}
		std::unordered_set<std::string> Visible;
		CollectVisibleModulePaths(Imports, ReferenceModulePath, Visible);
		return Visible.find(SymbolModulePath) != Visible.end();
	}

	inline bool IsProjectInternalVisible(
		const FProgram& Program,
		const std::string& SymbolModulePath,
		const std::string& ReferenceModulePath)
	{
		return Program.ProjectVisibleModulePaths.find(SymbolModulePath)
				!= Program.ProjectVisibleModulePaths.end()
			&& Program.ProjectVisibleModulePaths.find(ReferenceModulePath)
				!= Program.ProjectVisibleModulePaths.end();
	}

	inline bool CanAccessSymbol(
		const FProgram& Program,
		const std::string& SymbolModulePath,
		const bool bExported,
		const std::string& ReferenceModulePath,
		const FImportIndex* Index = nullptr)
	{
		if (SymbolModulePath == ReferenceModulePath
			|| SymbolModulePath.empty()
			|| ReferenceModulePath.empty()
			|| IsProjectInternalVisible(Program, SymbolModulePath, ReferenceModulePath))
		{
			return true;
		}
		return bExported && (Index
			? Index->VisibleFrom(ReferenceModulePath).count(SymbolModulePath) != 0
			: IsExportedModuleVisible(Program.Imports, SymbolModulePath, ReferenceModulePath));
	}
}
