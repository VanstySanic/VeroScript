// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSymbolQuery.h"
#include "VeroScriptAnalysisControl.h"
#include <algorithm>
#include <limits>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	// Request-owned callable ranges. Preserve the original narrowest-line-span,
	// latest-start, first-original-entry tie policy, including overlapping ranges.
	class FSourceRangeIndex
	{
	public:
		static constexpr std::size_t NotFound = std::numeric_limits<std::size_t>::max();
		FSourceRangeIndex() = default;
		explicit FSourceRangeIndex(const std::vector<FTextRange>& Ranges)
		{
			Entries.reserve(Ranges.size());
			for (std::size_t Index = 0; Index < Ranges.size(); ++Index)
			{
				if ((Index & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto& Range = Ranges[Index];
				Entries.push_back({Key(Range.StartLine, Range.StartColumn),
				    Key(Range.EndLine, Range.EndColumn), Range.EndLine - Range.StartLine, Index});
			}
			std::size_t SortWork = 0;
			std::sort(Entries.begin(), Entries.end(), [&SortWork](const auto& Left, const auto& Right)
			{
				if ((++SortWork & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				return Left.Start < Right.Start;
			});
			PrefixEnd.reserve(Entries.size());
			std::uint64_t End = 0;
			std::size_t Work = 0;
			for (const auto& Entry : Entries)
			{
				if ((++Work & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				End = std::max(End, Entry.End);
				PrefixEnd.push_back(End);
			}
		}
		std::size_t Find(const FTextPosition Position) const
		{
			const auto Point = Key(Position.Line, Position.Column);
			auto Last = std::upper_bound(Entries.begin(), Entries.end(), Point,
			    [](const auto Value, const FEntry& Entry)
			{
				return Value < Entry.Start;
			});
			const FEntry* Best = nullptr;
			std::size_t Work = 0;
			while (Last != Entries.begin())
			{
				const auto Index = static_cast<std::size_t>(--Last - Entries.begin());
				if (PrefixEnd[Index] <= Point)
				{
					break;
				}
				if ((++Work & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto& Entry = *Last;
				if (Point >= Entry.End)
				{
					continue;
				}
				if (!Best || Entry.LineSpan < Best->LineSpan || (Entry.LineSpan == Best->LineSpan && (Entry.Start > Best->Start || (Entry.Start == Best->Start && Entry.Ordinal < Best->Ordinal))))
				{
					Best = &Entry;
				}
			}
			return Best ? Best->Ordinal : NotFound;
		}

	private:
		static std::uint64_t Key(const std::uint32_t Line, const std::uint32_t Column)
		{
			return (static_cast<std::uint64_t>(Line) << 32u) | Column;
		}
		struct FEntry
		{
			std::uint64_t Start, End;
			std::uint32_t LineSpan;
			std::size_t Ordinal;
		};
		std::vector<FEntry> Entries;
		std::vector<std::uint64_t> PrefixEnd;
	};
}
