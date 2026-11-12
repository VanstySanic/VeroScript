// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontLayout.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <mutex>
#include <numeric>
#include <unordered_map>

namespace VeroScript::Font
{
	namespace
	{
		bool Utf8(std::string_view Text)
		{
			for (std::size_t I = 0; I < Text.size();)
			{
				const auto Lead = static_cast<unsigned char>(Text[I++]);
				if (Lead < 128)
				{
					continue;
				}
				const int Count = Lead >= 0xc2 && Lead <= 0xdf ? 1 : Lead >= 0xe0 && Lead <= 0xef ? 2
				    : Lead >= 0xf0 && Lead <= 0xf4                                                ? 3
				                                                                                  : -1;
				if (Count < 0 || Text.size() - I < static_cast<std::size_t>(Count)) return false;
				std::uint32_t Code = Lead & (0x7f >> Count);
				for (int J = 0; J < Count; ++J)
				{
					const auto Next = static_cast<unsigned char>(Text[I++]);
					if ((Next & 0xc0) != 0x80) return false;
					Code = (Code << 6) | (Next & 63);
				}
				if (Code < (Count == 1 ? 0x80u : Count == 2 ? 0x800u
				                                            : 0x10000u) ||
				    Code > 0x10ffff ||
				    (Code >= 0xd800 && Code <= 0xdfff))
				{
					return false;
				}
			}
			return true;
		}
		bool Boundary(std::string_view Text, std::size_t Offset)
		{
			return Offset <= Text.size() &&
			    (Offset == Text.size() || (static_cast<unsigned char>(Text[Offset]) & 0xc0) != 0x80);
		}
		char32_t Codepoint(std::string_view Text, std::size_t Offset)
		{
			const auto Lead = static_cast<unsigned char>(Text[Offset++]);
			const unsigned Count = Lead < 0x80 ? 0 : Lead < 0xe0 ? 1
			    : Lead < 0xf0                                    ? 2
			                                                     : 3;
			char32_t Result = Count ? Lead & (0x7f >> Count) : Lead;
			for (unsigned I = 0; I < Count; ++I)
			{
				Result = (Result << 6) | (static_cast<unsigned char>(Text[Offset++]) & 63);
			}
			return Result;
		}
		bool Invisible(char32_t Code)
		{
			return Code < 0x20 || (Code >= 0x7f && Code <= 0x9f) || Code == 0x00ad ||
			    Code == 0x034f || Code == 0x061c || (Code >= 0x180b && Code <= 0x180f) ||
			    (Code >= 0x200b && Code <= 0x200f) || (Code >= 0x2028 && Code <= 0x202e) ||
			    (Code >= 0x2060 && Code <= 0x206f) || (Code >= 0xfe00 && Code <= 0xfe0f) ||
			    Code == 0xfeff || (Code >= 0xe0100 && Code <= 0xe01ef);
		}
	} // namespace

	bool ValidateLayoutText(std::string_view Text, EDirection BaseDirection, std::string &Error)
	{
		Error.clear();
		if (Text.size() > INT32_MAX || !Utf8(Text) ||
		    (BaseDirection != EDirection::Auto && BaseDirection != EDirection::LeftToRight &&
		        BaseDirection != EDirection::RightToLeft))
		{
			Error = "font.invalid_text";
			return false;
		}
		return true;
	}

	struct FTextLayout::FImpl
	{
		struct FLineRuns
		{
			std::int64_t ByteOffset = 0, ByteLength = 0;
			std::vector<FTextRun> Visual;
			std::vector<std::size_t> Logical;
		};
		std::shared_ptr<const FLineRuns> ResolveLine(std::int64_t ByteOffset, std::int64_t ByteLength,
		    std::string &Error) const;
		// Keep both the selected instance/configuration and its immutable owned analysis alive.
		std::shared_ptr<ITextLayoutService> Service;
		std::shared_ptr<const ITextLayoutAnalysis> Analysis;
		mutable std::mutex LineMutex;
		mutable std::vector<std::shared_ptr<const FLineRuns>> Lines;
	};

	std::shared_ptr<const FTextLayout> FTextLayout::Create(const std::shared_ptr<ITextLayoutService> &Service,
	    std::string Text, EDirection BaseDirection, std::string &Error)
	{
		Error.clear();
		if (!Service)
		{
			Error = "font.layout_service_unavailable";
			return {};
		}
		// The selected service validates input; do not rescan the paragraph in this facade.
		auto Analysis = Service->Analyze(std::move(Text), BaseDirection, Error);
		if (!Analysis)
		{
			if (Error.empty())
			{
				Error = "font.layout_analysis_failed";
			}
			return {};
		}
		auto Value = std::make_shared<FImpl>();
		Value->Service = Service;
		Value->Analysis = std::move(Analysis);
		return std::shared_ptr<const FTextLayout>(new FTextLayout(std::move(Value)));
	}

	FTextLayout::~FTextLayout() = default;

	const std::string &FTextLayout::GetText() const noexcept
	{
		return Impl->Analysis->GetText();
	}

	bool FTextLayout::Owns(std::string_view Text) const noexcept
	{
		const auto &Owned = GetText();
		return Text.data() == Owned.data() && Text.size() == Owned.size();
	}

	const std::shared_ptr<ITextLayoutService> &FTextLayout::GetService() const noexcept
	{
		return Impl->Service;
	}

	EDirection FTextLayout::GetParagraphDirection(std::int64_t ByteOffset) const noexcept
	{
		return Impl->Analysis->GetParagraphDirection(ByteOffset);
	}

	std::shared_ptr<const FTextLayout::FImpl::FLineRuns> FTextLayout::FImpl::ResolveLine(
	    std::int64_t ByteOffset, std::int64_t ByteLength, std::string &Error) const
	{
		Error.clear();
		const auto &Text = Analysis->GetText();
		if (ByteOffset < 0 || ByteLength < 0 || static_cast<std::uint64_t>(ByteOffset) > Text.size() ||
		    static_cast<std::uint64_t>(ByteLength) > Text.size() - static_cast<std::size_t>(ByteOffset) ||
		    !Boundary(Text, static_cast<std::size_t>(ByteOffset)) ||
		    !Boundary(Text, static_cast<std::size_t>(ByteOffset + ByteLength)))
		{
			Error = "font.invalid_text_range";
			return {};
		}
		{
			std::lock_guard<std::mutex> Lock(LineMutex);
			for (auto It = Lines.begin(); It != Lines.end(); ++It)
			{
				if ((*It)->ByteOffset == ByteOffset && (*It)->ByteLength == ByteLength)
				{
					auto Existing = *It;
					Lines.erase(It);
					Lines.push_back(Existing);
					return Existing;
				}
			}
		}
		auto Candidate = std::make_shared<FLineRuns>();
		Candidate->ByteOffset = ByteOffset;
		Candidate->ByteLength = ByteLength;
		if (!Analysis->GetVisualRuns(ByteOffset, ByteLength, Candidate->Visual, Error)) return {};
		// Validate third-party/provider run boundaries before common shaping dereferences the text.
		for (const auto &Run : Candidate->Visual)
		{
			if (Run.ByteOffset < ByteOffset || Run.ByteLength <= 0 || Run.ByteOffset > ByteOffset + ByteLength ||
			    Run.ByteLength > ByteOffset + ByteLength - Run.ByteOffset || Run.Level > 126 ||
			    !Boundary(Text, static_cast<std::size_t>(Run.ByteOffset)) ||
			    !Boundary(Text, static_cast<std::size_t>(Run.ByteOffset + Run.ByteLength)))
			{
				Error = "font.invalid_layout_result";
				return {};
			}
		}
		Candidate->Logical.resize(Candidate->Visual.size());
		std::iota(Candidate->Logical.begin(), Candidate->Logical.end(), std::size_t{0});
		std::sort(Candidate->Logical.begin(), Candidate->Logical.end(), [&](auto A, auto B)
		{
			return Candidate->Visual[A].ByteOffset < Candidate->Visual[B].ByteOffset;
		});
		auto End = ByteOffset;
		for (const auto Index : Candidate->Logical)
		{
			const auto &Run = Candidate->Visual[Index];
			if (Run.ByteOffset != End)
			{
				Error = "font.invalid_layout_result";
				return {};
			}
			End += Run.ByteLength;
		}
		if (End != ByteOffset + ByteLength)
		{
			Error = "font.invalid_layout_result";
			return {};
		}
		{
			std::lock_guard<std::mutex> Lock(LineMutex);
			for (const auto &Existing : Lines)
			{
				if (Existing->ByteOffset == ByteOffset && Existing->ByteLength == ByteLength) return Existing;
			}
			if (Lines.size() == 4)
			{
				Lines.erase(Lines.begin());
			}
			Lines.push_back(Candidate);
		}
		return Candidate;
	}
	bool FTextLayout::GetVisualRuns(std::int64_t ByteOffset, std::int64_t ByteLength,
	    std::vector<FTextRun> &Result, std::string &Error) const
	{
		const auto Runs = Impl->ResolveLine(ByteOffset, ByteLength, Error);
		if (!Runs) return false;
		Result = Runs->Visual;
		return true;
	}

	bool FTextLayout::ShapeLine(const std::shared_ptr<IFace> &Primary,
	    const std::vector<std::shared_ptr<IFace>> &Fallbacks, const FShapeOptions &Options,
	    std::int64_t ByteOffset, std::int64_t ByteLength, double LetterSpacing, double ReplacementAdvance,
	    FTextLine &Result, std::string &Error, std::int64_t FragmentOffset, std::int64_t FragmentLength) const
	{
		const auto &Text = GetText();
		if (!Primary || !std::isfinite(LetterSpacing) || !std::isfinite(ReplacementAdvance) || ReplacementAdvance < 0)
		{
			Error = "font.invalid_line_options";
			return false;
		}
		const auto Runs = Impl->ResolveLine(ByteOffset, ByteLength, Error);
		if (!Runs) return false;
		if (FragmentOffset == -1 && FragmentLength == -1)
		{
			FragmentOffset = ByteOffset;
			FragmentLength = ByteLength;
		}
		if (FragmentOffset < ByteOffset || FragmentLength < 0 || FragmentOffset > ByteOffset + ByteLength ||
		    FragmentLength > ByteOffset + ByteLength - FragmentOffset ||
		    !Boundary(Text, static_cast<std::size_t>(FragmentOffset)) ||
		    !Boundary(Text, static_cast<std::size_t>(FragmentOffset + FragmentLength)))
		{
			Error = "font.invalid_text_range";
			return false;
		}
		FTextLine Candidate;
		Candidate.Text.Direction = GetParagraphDirection(ByteOffset);
		std::vector<std::size_t> Selected;
		if (FragmentOffset == ByteOffset && FragmentLength == ByteLength)
		{
			Selected.resize(Runs->Visual.size());
			std::iota(Selected.begin(), Selected.end(), std::size_t{0});
		}
		else
		{
			auto It = std::lower_bound(Runs->Logical.begin(), Runs->Logical.end(), FragmentOffset,
			    [&](auto Index, auto Offset)
			{
				return Runs->Visual[Index].ByteOffset + Runs->Visual[Index].ByteLength <= Offset;
			});
			for (; It != Runs->Logical.end() && Runs->Visual[*It].ByteOffset < FragmentOffset + FragmentLength; ++It)
			{
				Selected.push_back(*It);
			}
			std::sort(Selected.begin(), Selected.end());
		}
		for (const auto Index : Selected)
		{
			auto Run = Runs->Visual[Index];
			const auto Begin = std::max(Run.ByteOffset, FragmentOffset);
			const auto End = std::min(Run.ByteOffset + Run.ByteLength, FragmentOffset + FragmentLength);
			if (Begin >= End)
			{
				continue;
			}
			Run.ByteOffset = Begin;
			Run.ByteLength = End - Begin;
			auto ShapeOptions = Options;
			ShapeOptions.Layout = shared_from_this();
			ShapeOptions.Direction = Run.Level & 1 ? EDirection::RightToLeft : EDirection::LeftToRight;
			ShapeOptions.ByteOffset = Run.ByteOffset;
			ShapeOptions.ByteLength = Run.ByteLength;
			if (!Run.Script.empty() && Run.Script != "Zyyy" && Run.Script != "Zinh" && Run.Script != "Zzzz")
			{
				ShapeOptions.Script = Run.Script;
			}
			FResolvedText Shaped;
			if (!ShapeWithFallback(Primary, Fallbacks, Text, ShapeOptions, Shaped, Error)) return false;
			struct FClusterState
			{
				std::size_t LastGlyph = 0, OutputIndex = 0;
				std::uint32_t End = 0;
				bool bVisible = false, bInvisible = false;
			};
			std::unordered_map<std::uint32_t, FClusterState> Clusters;
			std::vector<std::uint32_t> Boundaries;
			Boundaries.reserve(Shaped.Glyphs.size() + 1);
			for (std::size_t I = 0; I < Shaped.Glyphs.size(); ++I)
			{
				const auto Offset = Shaped.Glyphs[I].Glyph.Cluster;
				Boundaries.push_back(Offset);
				Clusters[Offset].LastGlyph = I;
			}
			Boundaries.push_back(static_cast<std::uint32_t>(Run.ByteOffset + Run.ByteLength));
			std::sort(Boundaries.begin(), Boundaries.end());
			Boundaries.erase(std::unique(Boundaries.begin(), Boundaries.end()), Boundaries.end());
			const auto RunStartX = Candidate.Width;
			for (std::size_t I = 0; I < Shaped.Glyphs.size(); ++I)
			{
				auto &Value = Shaped.Glyphs[I];
				const auto Offset = Value.Glyph.Cluster;
				auto &State = Clusters.at(Offset);
				if (!State.bVisible && !State.bInvisible)
				{
					State.End = *std::upper_bound(Boundaries.begin(), Boundaries.end(), Offset);
					State.bInvisible = true;
					// A joiner/selector can share a cluster with visible text. Test the complete
					// logical range once, rather than dropping every glyph from its first code point.
					for (auto Position = Offset; Position < State.End; ++Position)
					{
						if (Boundary(Text, Position) && !Invisible(Codepoint(Text, Position)))
						{
							State.bInvisible = false;
							break;
						}
					}
				}
				if (State.bInvisible)
				{
					Candidate.Carets.push_back({Offset, Candidate.Width, false});
					Candidate.Carets.push_back({State.End, Candidate.Width, true});
					continue;
				}
				// Keep attached glyphs at the shaper's positions; space only after the complete cluster.
				const auto Advance = (Value.Glyph.Glyph ? Value.Glyph.XAdvance / 64.0 : ReplacementAdvance) +
				    (State.LastGlyph == I ? LetterSpacing : 0);
				const auto Left = std::min(Candidate.Width, Candidate.Width + Advance);
				const auto Right = std::max(Candidate.Width, Candidate.Width + Advance);
				if (!std::isfinite(Left) || !std::isfinite(Right))
				{
					Error = "font.invalid_line_metrics";
					return false;
				}
				if (!State.bVisible)
				{
					State.OutputIndex = Candidate.Clusters.size();
					State.bVisible = true;
					Candidate.Clusters.push_back({Offset, State.End - Offset, Left, Right, Run.Level});
					Candidate.Carets.push_back({Run.Level & 1 ? State.End : Offset, Candidate.Width, (Run.Level & 1) != 0});
				}
				else
				{
					auto &Cluster = Candidate.Clusters[State.OutputIndex];
					Cluster.Left = std::min(Cluster.Left, Left);
					Cluster.Right = std::max(Cluster.Right, Right);
				}
				Candidate.Width += Advance;
				if (State.LastGlyph == I)
				{
					const auto &Cluster = Candidate.Clusters[State.OutputIndex];
					Candidate.Carets.push_back({Run.Level & 1 ? Offset : Cluster.ByteOffset + Cluster.ByteLength,
					    Candidate.Width, (Run.Level & 1) == 0});
				}
				Candidate.Text.Glyphs.push_back(std::move(Value));
			}
			// Shapers may omit default-ignorable glyphs entirely. Preserve the logical
			// endpoints nevertheless, including fragments consisting solely of controls.
			Candidate.Carets.push_back({Run.ByteOffset, Run.Level & 1 ? Candidate.Width : RunStartX, false});
			Candidate.Carets.push_back({Run.ByteOffset + Run.ByteLength, Run.Level & 1 ? RunStartX : Candidate.Width, true});
		}
		if (Candidate.Carets.empty())
		{
			Candidate.Carets.push_back({FragmentOffset, 0, false});
			Candidate.Carets.push_back({FragmentOffset + FragmentLength, 0, true});
		}
		std::sort(Candidate.Carets.begin(), Candidate.Carets.end(), [](const auto &A, const auto &B)
		{
			if (A.X != B.X) return A.X < B.X;
			if (A.ByteOffset != B.ByteOffset) return A.ByteOffset < B.ByteOffset;
			return A.bTrailing < B.bTrailing;
		});
		Candidate.Carets.erase(std::unique(Candidate.Carets.begin(), Candidate.Carets.end(), [](const auto &A, const auto &B)
		{
			return A.X == B.X && A.ByteOffset == B.ByteOffset;
		}),
		    Candidate.Carets.end());
		Candidate.Width = std::max(0.0, Candidate.Width);
		Result = std::move(Candidate);
		return true;
	}

	FTextLayout::FTextLayout(std::shared_ptr<const FImpl> Value) : Impl(std::move(Value))
	{
	}

} // namespace VeroScript::Font
