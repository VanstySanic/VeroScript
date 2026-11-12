// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptProcessingSourceMap.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptPath.h"
#include <algorithm>
#include <array>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		std::vector<std::size_t> SourceLines(const FSourceDocument& Document)
		{
			if (!Document.LineOffsets.empty()) return Document.LineOffsets;
			std::vector<std::size_t> Lines{0};
			for (std::size_t I = 0; I < Document.Text.size(); ++I)
			{
				if ((I & 0x7ffu) == 0)
				{
					CheckAnalysisCancellation();
				}

				if (Document.Text[I] == '\n')
				{
					Lines.push_back(I + 1);
				}
			}
			return Lines;
		}
		void ProjectRawLineActivity(const FSourceDocument& Raw, const FSourceDocument& Accepted,
			const std::vector<std::size_t>& RawLines, FPreprocessLineMap& Map)
		{
			const auto AcceptedLines = SourceLines(Accepted);
			if (RawLines == AcceptedLines &&
				std::all_of(Accepted.Mappings.begin(), Accepted.Mappings.end(),
					[&](const auto& M)
					{
						return M.Origin.GenerationId.empty() && M.Origin.Range.SourceId == Raw.Id &&
							M.Begin == M.Origin.Range.Begin && M.End == M.Origin.Range.End;
					}))
			{
				return;
			}

			std::vector<bool> Activity(RawLines.size() + 1, false);
			for (const auto& Mapping : Accepted.Mappings)
			{
				if (Mapping.Origin.Range.SourceId != Raw.Id)
				{
					continue;
				}

				const bool Linear = Mapping.Origin.GenerationId.empty() &&
					Mapping.End - Mapping.Begin == Mapping.Origin.Range.End - Mapping.Origin.Range.Begin;
				for (auto Begin = Mapping.Begin; Begin < Mapping.End;)
				{
					CheckAnalysisCancellation();
					const auto Line = static_cast<std::size_t>(std::upper_bound(AcceptedLines.begin(), AcceptedLines.end(), Begin) - AcceptedLines.begin());
					const auto End = Line < AcceptedLines.size() ? std::min(Mapping.End, AcceptedLines[Line]) : Mapping.End;
					if (Line < Map.PhysicalLineActiveCode.size() && Map.PhysicalLineActiveCode[Line])
					{
						const auto Original = Mapping.Origin.Range.Begin + (Linear ? Begin - Mapping.Begin : 0);
						const auto Last = Original + (Linear ? End - Begin - 1 : 0);
						const auto FirstLine = std::upper_bound(RawLines.begin(), RawLines.end(), Original) - RawLines.begin();
						const auto LastLine =
							std::upper_bound(RawLines.begin(), RawLines.end(), Last) - RawLines.begin();
						for (auto I = FirstLine; I <= LastLine; ++I)
						{
							Activity[I] = true;
						}
					}
					Begin = End;
				}
			}
			Map.PhysicalLineActiveCode = std::move(Activity);
		}
	}
	void ApplyProcessingSourceMaps(const FSourceMetadataSnapshot* Metadata,
		std::unordered_map<std::string, FPreprocessLineMap>& Maps)
	{
		if (!Metadata || Maps.empty()) return;
		std::unordered_map<std::string_view, const FSourceDocument*> Documents;
		for (const auto& Doc : Metadata->Documents)
		{
			Documents.emplace(Doc.Id, &Doc);
		}

		for (const auto& Doc : Metadata->Documents)
		{
			if (Doc.Id != Doc.ModuleId)
			{
				continue;
			}

			const auto Input = Documents.find(Doc.Id + ":active-input");
			const auto Raw = Documents.find(Doc.Id + ":raw");
			if (Input == Documents.end() || Raw == Documents.end())
			{
				continue;
			}

			const auto& InputMappings = Input->second->Mappings;
			const bool ActiveUnchanged = Doc.Mappings.size() == InputMappings.size() &&
				std::equal(Doc.Mappings.begin(), Doc.Mappings.end(), InputMappings.begin(),
					[](const auto& A, const auto& B)
					{
						return A.Begin == B.Begin && A.End == B.End &&
							A.Origin.Range.SourceId == B.Origin.Range.SourceId &&
							A.Origin.Range.Begin == B.Origin.Range.Begin &&
							A.Origin.Range.End == B.Origin.Range.End &&
							A.Origin.GenerationId == B.Origin.GenerationId;
					});
			const auto Accepted = Documents.find(Doc.Id + ":accepted");
			const bool RawUnchanged = Accepted != Documents.end() &&
				std::all_of(Accepted->second->Mappings.begin(), Accepted->second->Mappings.end(),
					[&](const auto& M)
					{
						return M.Origin.GenerationId.empty() && M.Origin.Range.SourceId == Raw->second->Id &&
							M.Begin == M.Origin.Range.Begin && M.End == M.Origin.Range.End;
					}) &&
				(Raw->second->Text.empty() && Accepted->second->Text.empty()
						? Raw->second->LineOffsets == Accepted->second->LineOffsets
						: Raw->second->Text == Accepted->second->Text);
			if (ActiveUnchanged && RawUnchanged)
			{
				continue;
			}

			const auto Existing = Maps.find(PathUtil::IdentityKey(Doc.ModuleId));
			if (Existing == Maps.end())
			{
				continue;
			}

			auto& Map = Existing->second;
			const auto Lines = SourceLines(*Raw->second);
			if (!RawUnchanged && Accepted != Documents.end())
			{
				ProjectRawLineActivity(*Raw->second, *Accepted->second, Lines, Map);
			}

			if (ActiveUnchanged)
			{
				continue;
			}

			std::vector<FPreprocessSourcePos> Positions(Map.PreprocessedSource.size());
			// Resolve contiguous identity intervals together. This avoids allocating
			// an origin chain per byte and scales with text plus mapping boundaries.
			for (std::size_t Begin = 0; Begin < Positions.size();)
			{
				CheckAnalysisCancellation();
				const FSourceDocument* Current = &Doc;
				std::size_t Offset = Begin, Count = Positions.size() - Begin;
				bool Linear = true;
				std::array<const FSourceDocument*, 64> Seen{};
				std::size_t Depth = 0;
				FPreprocessSourcePos Anchor{1, 1};
				while (Current && !Current->Mappings.empty() && Depth < Seen.size())
				{
					if (std::find(Seen.begin(), Seen.begin() + Depth, Current) != Seen.begin() + Depth)
					{
						break;
					}

					Seen[Depth++] = Current;
					const auto It =
						std::upper_bound(Current->Mappings.begin(), Current->Mappings.end(), Offset,
							[](auto Value, const auto& M)
							{
								return Value < M.Begin;
							});
					if (It == Current->Mappings.begin() || Offset >= std::prev(It)->End)
					{
						break;
					}

					const auto& Mapping = *std::prev(It);
					if (Linear)
					{
						Count = std::min(Count, Mapping.End - Offset);
					}

					const bool Identity = Mapping.Origin.GenerationId.empty() &&
						Mapping.End - Mapping.Begin == Mapping.Origin.Range.End - Mapping.Origin.Range.Begin;
					Offset = Mapping.Origin.Range.Begin + (Identity ? Offset - Mapping.Begin : 0);
					Linear = Linear && Identity;
					Anchor = {Mapping.Origin.Range.Position.Begin.Line, Mapping.Origin.Range.Position.Begin.Column};
					const auto Next = Documents.find(Mapping.Origin.Range.SourceId);
					Current = Next == Documents.end() ? nullptr : Next->second;
				}
				std::size_t Line = Current == Raw->second
					? static_cast<std::size_t>(std::upper_bound(Lines.begin(), Lines.end(), Offset) - Lines.begin() - 1) : 0;
				for (std::size_t I = 0; I < Count; ++I)
				{
					if ((I & 0x7ffu) == 0)
					{
						CheckAnalysisCancellation();
					}

					if (Current == Raw->second)
					{
						const auto At = Offset + (Linear ? I : 0);
						while (Line + 1 < Lines.size() && Lines[Line + 1] <= At)
						{
							++Line;
						}

						Positions[Begin + I] = {static_cast<std::uint32_t>(Line + 1),
							static_cast<std::uint32_t>(At - Lines[Line] + 1)};
					}
					else
					{
						Positions[Begin + I] = Anchor;
					}
				}
				Begin += Count;
			}
			Map.OutByteToPhysical = std::move(Positions);
		}
	}
}
