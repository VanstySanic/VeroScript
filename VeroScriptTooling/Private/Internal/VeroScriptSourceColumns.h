// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAnalysisControl.h"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	// Sparse column corrections retain the loaded source's coordinate system,
	// without keeping another complete copy of every source document.
	struct FSourceColumns
	{
		struct FCorrection
		{
			std::uint32_t ByteColumn = 1;
			std::uint32_t RemovedBytes = 0;
		};
		static FSourceColumns Capture(std::string_view Source)
		{
			FSourceColumns Result;
			std::uint32_t Line = 1, Column = 1, Removed = 0;
			std::size_t NextCancellationCheck = 0;
			// The lexer/preprocessor exclude a leading encoding BOM from columns.
			const bool HasBom = Source.size() >= 3
				&& static_cast<unsigned char>(Source[0]) == 0xef
				&& static_cast<unsigned char>(Source[1]) == 0xbb
				&& static_cast<unsigned char>(Source[2]) == 0xbf;
			for (std::size_t Index = HasBom ? 3 : 0; Index < Source.size();)
			{
				if (Index >= NextCancellationCheck)
				{
					CheckAnalysisCancellation();
					NextCancellationCheck = Index + 2048;
				}
				const auto Lead = static_cast<unsigned char>(Source[Index]);
				if (Lead == '\n')
				{
					++Line;
					Column = 1;
					Removed = 0;
					++Index;
					continue;
				}
				std::size_t Length = Lead >= 0xc2 && Lead <= 0xdf ? 2
					: Lead >= 0xe0 && Lead <= 0xef ? 3 : Lead >= 0xf0 && Lead <= 0xf4 ? 4 : 1;
				std::uint32_t Code = Lead & (Length == 2 ? 0x1f : Length == 3 ? 0x0f : Length == 4 ? 0x07 : 0xff);
				bool Valid = Length <= Source.size() - Index;
				for (std::size_t I = 1; Valid && I < Length; ++I)
				{
					const auto Next = static_cast<unsigned char>(Source[Index + I]);
					Valid = (Next & 0xc0) == 0x80;
					Code = (Code << 6) | (Next & 0x3f);
				}
				Valid = Valid &&
					(Length == 1 ||
						(Code >= (Length == 2		   ? 0x80u
										 : Length == 3 ? 0x800u
													   : 0x10000u) &&
							Code <= 0x10ffff && (Code < 0xd800 || Code > 0xdfff)));
				if (!Valid)
				{
					Length = 1;
				}

				Column += static_cast<std::uint32_t>(Length);
				if (Length > 1)
				{
					Removed += static_cast<std::uint32_t>(Length) - (Code > 0xffff ? 2u : 1u);
					Result.Lines[Line].push_back({Column, Removed});
				}
				Index += Length;
			}
			return Result;
		}

		std::uint32_t Utf16Column(std::uint32_t Line, std::uint32_t ByteColumn) const
		{
			const auto Found = Lines.find(Line);
			if (Found == Lines.end()) return ByteColumn;
			const auto& Corrections = Found->second;
			const auto End = std::upper_bound(Corrections.begin(), Corrections.end(), ByteColumn,
				[](std::uint32_t Column, const FCorrection& Correction)
				{
					return Column < Correction.ByteColumn;
				});
			return End == Corrections.begin() ? ByteColumn : ByteColumn - std::prev(End)->RemovedBytes;
		}

		std::unordered_map<std::uint32_t, std::vector<FCorrection>> Lines;
	};
	using FSourceColumnMaps = std::unordered_map<std::string, FSourceColumns>;
}
