// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptAnalysisControl.h"
#include <string_view>
#include <unordered_map>

namespace VeroScript::Tooling::Detail
{
	// Candidate pruning only: callers still apply SymbolNameMatchesIdent and all
	// declaration, scope and visibility rules. The immutable tree must outlive us.
	class FSymbolLookupIndex
	{
	public:
		struct FEntry
		{
			const FDocumentSymbol* Symbol;
			const std::string* Container;
			bool bDeclarationPositionOnly;
			bool bTopLevel;
		};

		explicit FSymbolLookupIndex(const std::vector<FDocumentSymbol>& InSymbols) : Symbols(&InSymbols)
		{
			Append(InSymbols, nullptr, false);
		}
		bool Matches(const std::vector<FDocumentSymbol>& Candidate) const
		{
			return Symbols == &Candidate;
		}
		static bool Supports(const std::string_view Name)
		{
			// Qualified/application queries retain their original matching path.
			return !Name.empty() && Name.find_first_of(".<") == std::string_view::npos;
		}
		const std::vector<FEntry>* Find(const std::string_view Name) const
		{
			const auto Found = ByLeaf.find(Name);
			return Found == ByLeaf.end() ? nullptr : &Found->second;
		}

	private:
		void Append(const std::vector<FDocumentSymbol>& Nodes, const std::string* Container,
		    const bool bDeclarationOnly)
		{
			for (const auto& Symbol : Nodes)
			{
				if ((++Visited & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const std::string_view Name(Symbol.Name);
				const auto Stem = Name.substr(0, Name.find('<'));
				const auto Dot = Stem.rfind('.');
				const auto Leaf = Dot == std::string_view::npos ? Stem : Stem.substr(Dot + 1);
				ByLeaf[Leaf].push_back({&Symbol, Container, bDeclarationOnly, Container == nullptr});
				Append(Symbol.Children, &Symbol.Name, bDeclarationOnly || (Symbol.Kind == EDocumentSymbolKind::Enum && Symbol.bScopedEnum));
			}
		}
		const std::vector<FDocumentSymbol>* Symbols;
		std::unordered_map<std::string_view, std::vector<FEntry>> ByLeaf;
		std::size_t Visited = 0;
	};

	inline thread_local const FSymbolLookupIndex* ActiveSymbolLookupIndex = nullptr;
	class FSymbolLookupScope
	{
	public:
		explicit FSymbolLookupScope(const std::vector<FDocumentSymbol>& Symbols)
		    : Index(Symbols), Previous(ActiveSymbolLookupIndex)
		{
			ActiveSymbolLookupIndex = &Index;
		}
		~FSymbolLookupScope()
		{
			ActiveSymbolLookupIndex = Previous;
		}
		FSymbolLookupScope(const FSymbolLookupScope&) = delete;
		FSymbolLookupScope& operator=(const FSymbolLookupScope&) = delete;

	private:
		FSymbolLookupIndex Index;
		const FSymbolLookupIndex* Previous;
	};
}
