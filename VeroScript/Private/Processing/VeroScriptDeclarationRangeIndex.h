// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAnalysisControl.h"
#include "VeroScriptSourceMetadata.h"
#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace VeroScript
{
	// Build once after adding all ranges within one metadata collection.
	// Ordinals preserve the original
	// declaration order when equally narrow source ranges contain a syntax node.
	class FDeclarationRangeIndex
	{
	public:
		void Add(const FSourceRange& Range, const std::size_t Ordinal)
		{
			if ((Ordinal & 255u) == 0)
			{
				CheckAnalysisCancellation();
			}
			Documents[Range.SourceId].push_back({Range.Begin, Range.End, Ordinal, 0});
		}

		void Build()
		{
			for (auto& Document : Documents)
			{
				CheckAnalysisCancellation();
				auto& Entries = Document.second;
				std::sort(Entries.begin(), Entries.end(), [](const FEntry& Left, const FEntry& Right)
				{
					return Left.Begin != Right.Begin ? Left.Begin < Right.Begin : Left.Ordinal < Right.Ordinal;
				});
				std::size_t Maximum = 0;
				std::size_t Visited = 0;
				for (auto& Entry : Entries)
				{
					if ((Visited++ & 255u) == 0)
					{
						CheckAnalysisCancellation();
					}
					Maximum = std::max(Maximum, Entry.End);
					Entry.MaximumEnd = Maximum;
				}
			}
		}

		std::optional<std::size_t> FindContaining(const FSourceRange& Range) const
		{
			const auto Document = Documents.find(Range.SourceId);
			if (Document == Documents.end()) return {};
			const auto& Entries = Document->second;
			auto Position = std::upper_bound(Entries.begin(), Entries.end(), Range.Begin,
			    [](const std::size_t Begin, const FEntry& Entry)
			{
				return Begin < Entry.Begin;
			});
			std::optional<std::size_t> Result;
			std::size_t Width = std::numeric_limits<std::size_t>::max();
			std::size_t Visited = 0;
			while (Position != Entries.begin())
			{
				const auto& Entry = *--Position;
				if (Entry.MaximumEnd < Range.End)
				{
					break;
				}
				if ((Visited++ & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}
				if (Entry.End >= Range.End)
				{
					const auto CandidateWidth = Entry.End - Entry.Begin;
					if (CandidateWidth < Width || (Result && CandidateWidth == Width && Entry.Ordinal < *Result))
					{
						Result = Entry.Ordinal;
						Width = CandidateWidth;
					}
				}
			}
			return Result;
		}

	private:
		struct FEntry
		{
			std::size_t Begin, End, Ordinal, MaximumEnd;
		};

		std::unordered_map<std::string, std::vector<FEntry>> Documents;
	};
}
