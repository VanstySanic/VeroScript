// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontBackend.h"
#include <SheenBidi/SheenBidi.h>
#include <algorithm>
#include <iterator>
#include <type_traits>

namespace VeroScript::FontBackend
{
	namespace
	{
		using namespace Font;
		std::string ScriptTag(SBScript Script)
		{
			const auto Tag = SBScriptGetUnicodeTag(Script);
			if (!Tag) return {};
			return {static_cast<char>(Tag >> 24), static_cast<char>(Tag >> 16),
			    static_cast<char>(Tag >> 8), static_cast<char>(Tag)};
		}
		class FAnalysis final : public ITextLayoutAnalysis
		{
		public:
			~FAnalysis() override
			{
				for (const auto Paragraph : Paragraphs)
				{
					SBParagraphRelease(Paragraph);
				}
				if (Algorithm)
				{
					SBAlgorithmRelease(Algorithm);
				}
			}
			const std::string &GetText() const noexcept override
			{
				return Text;
			}
			EDirection GetParagraphDirection(std::int64_t ByteOffset) const noexcept override
			{
				if (ByteOffset >= 0 && static_cast<std::uint64_t>(ByteOffset) <= Text.size() &&
				    !(static_cast<std::uint64_t>(ByteOffset) == Text.size() && bTrailingEmptyParagraph))
				{
					if (const auto Paragraph = FindParagraph(static_cast<std::size_t>(ByteOffset)))
					{
						return SBParagraphGetBaseLevel(Paragraph) & 1 ? EDirection::RightToLeft : EDirection::LeftToRight;
					}
				}
				return BaseDirection == EDirection::RightToLeft ? EDirection::RightToLeft : EDirection::LeftToRight;
			}
			bool GetVisualRuns(std::int64_t ByteOffset, std::int64_t ByteLength,
			    std::vector<FTextRun> &Result, std::string &Error) const override
			{
				Error.clear();
				const auto Boundary = [&](std::size_t Offset)
				{
					return Offset <= Text.size() && (Offset == Text.size() || (static_cast<unsigned char>(Text[Offset]) & 0xc0) != 0x80);
				};
				if (ByteOffset < 0 || ByteLength < 0 || static_cast<std::uint64_t>(ByteOffset) > Text.size() ||
				    static_cast<std::uint64_t>(ByteLength) > Text.size() - static_cast<std::size_t>(ByteOffset) ||
				    !Boundary(static_cast<std::size_t>(ByteOffset)) || !Boundary(static_cast<std::size_t>(ByteOffset + ByteLength)))
				{
					Error = "font.invalid_text_range";
					return false;
				}
				std::vector<FTextRun> Candidate;
				if (!ByteLength)
				{
					Result = std::move(Candidate);
					return true;
				}
				const auto Paragraph = FindParagraph(static_cast<std::size_t>(ByteOffset));
				if (!Paragraph || static_cast<std::uint64_t>(ByteOffset + ByteLength) > SBParagraphGetOffset(Paragraph) + SBParagraphGetLength(Paragraph))
				{
					Error = "font.line_crosses_paragraph";
					return false;
				}
				std::unique_ptr<std::remove_pointer_t<SBLineRef>, decltype(&SBLineRelease)>
				    Line(SBParagraphCreateLine(Paragraph, static_cast<SBUInteger>(ByteOffset),
				             static_cast<SBUInteger>(ByteLength)),
				        SBLineRelease);
				if (!Line)
				{
					Error = "font.layout_allocation_failed";
					return false;
				}
				const auto *Runs = SBLineGetRunsPtr(Line.get());
				for (SBUInteger I = 0; I < SBLineGetRunCount(Line.get()); ++I)
				{
					const auto &Run = Runs[I];
					std::vector<FTextRun> Pieces;
					auto It = std::upper_bound(Scripts.begin(), Scripts.end(), Run.offset,
					    [](std::size_t Position, const FScriptRun &Script)
					{
						return Position < Script.Begin;
					});
					if (It != Scripts.begin())
					{
						--It;
					}
					for (; It != Scripts.end() && It->Begin < Run.offset + Run.length; ++It)
					{
						const auto Begin = std::max(It->Begin, static_cast<std::size_t>(Run.offset));
						const auto End = std::min(It->End, static_cast<std::size_t>(Run.offset + Run.length));
						if (Begin < End)
						{
							Pieces.push_back({static_cast<std::int64_t>(Begin),
							    static_cast<std::int64_t>(End - Begin), Run.level, It->Script});
						}
					}
					if (Run.level & 1)
					{
						std::reverse(Pieces.begin(), Pieces.end());
					}
					Candidate.insert(Candidate.end(), std::make_move_iterator(Pieces.begin()), std::make_move_iterator(Pieces.end()));
				}
				Result = std::move(Candidate);
				return true;
			}
			static std::shared_ptr<const FAnalysis> Create(std::string Text, EDirection BaseDirection, std::string &Error)
			{
				if (!ValidateLayoutText(Text, BaseDirection, Error)) return {};
				auto Value = std::make_shared<FAnalysis>();
				Value->Text = std::move(Text);
				Value->BaseDirection = BaseDirection;
				if (!Value->Text.empty())
				{
					const SBCodepointSequence Sequence{SBStringEncodingUTF8, Value->Text.data(), Value->Text.size()};
					Value->Algorithm = SBAlgorithmCreate(&Sequence);
					if (!Value->Algorithm)
					{
						Error = "font.layout_allocation_failed";
						return {};
					}
					auto Last = static_cast<SBUInteger>(Value->Text.size());
					Value->bTrailingEmptyParagraph =
					    SBCodepointGetBidiType(SBCodepointSequenceGetCodepointBefore(&Sequence, &Last)) == SBBidiTypeB;
					std::unique_ptr<std::remove_pointer_t<SBScriptLocatorRef>, decltype(&SBScriptLocatorRelease)>
					    Locator(SBScriptLocatorCreate(), SBScriptLocatorRelease);
					if (!Locator)
					{
						Error = "font.layout_allocation_failed";
						return {};
					}
					const SBLevel Level = BaseDirection == EDirection::Auto ? SBLevelDefaultLTR
					    : BaseDirection == EDirection::RightToLeft          ? 1
					                                                        : 0;
					for (std::size_t Offset = 0; Offset < Value->Text.size();)
					{
						auto Paragraph = SBAlgorithmCreateParagraph(Value->Algorithm, Offset, Value->Text.size() - Offset, Level);
						if (!Paragraph)
						{
							Error = "font.layout_allocation_failed";
							return {};
						}
						std::unique_ptr<std::remove_pointer_t<SBParagraphRef>, decltype(&SBParagraphRelease)>
						    Owned(Paragraph, SBParagraphRelease);
						const auto Length = SBParagraphGetLength(Paragraph);
						if (!Length || Length > Value->Text.size() - Offset)
						{
							Error = "font.invalid_paragraph";
							return {};
						}
						Value->Paragraphs.push_back(Paragraph);
						Owned.release();
						const SBCodepointSequence Part{SBStringEncodingUTF8, Value->Text.data() + Offset, Length};
						SBScriptLocatorLoadCodepoints(Locator.get(), &Part);
						while (SBScriptLocatorMoveNext(Locator.get()))
						{
							const auto *Run = SBScriptLocatorGetAgent(Locator.get());
							Value->Scripts.push_back({Offset + Run->offset, Offset + Run->offset + Run->length, ScriptTag(Run->script)});
						}
						Offset += Length;
					}
				}
				return Value;
			}

		private:
			struct FScriptRun
			{
				std::size_t Begin = 0, End = 0;
				std::string Script;
			};
			SBParagraphRef FindParagraph(std::size_t Offset) const
			{
				const auto It = std::upper_bound(Paragraphs.begin(), Paragraphs.end(), Offset,
				    [](std::size_t Position, SBParagraphRef Paragraph)
				{
					return Position < SBParagraphGetOffset(Paragraph);
				});
				return It == Paragraphs.begin() ? nullptr : *std::prev(It);
			}
			std::string Text;
			EDirection BaseDirection = EDirection::Auto;
			SBAlgorithmRef Algorithm = nullptr;
			bool bTrailingEmptyParagraph = false;
			std::vector<SBParagraphRef> Paragraphs;
			std::vector<FScriptRun> Scripts;
		};
		class FLayoutService final : public Font::ITextLayoutService
		{
		public:
			const char *GetBackendId() const noexcept override
			{
				return BackendId;
			}
			std::shared_ptr<const Font::ITextLayoutAnalysis> Analyze(std::string Text, EDirection Direction,
			    std::string &Error) const override
			{
				return FAnalysis::Create(std::move(Text), Direction, Error);
			}
		};
	} // namespace
	std::shared_ptr<Font::ITextLayoutService> CreateTextLayoutService()
	{
		return std::make_shared<FLayoutService>();
	}
} // namespace VeroScript::FontBackend
