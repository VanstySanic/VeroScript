// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontLayout.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript::Font;
	void Require(bool Value, const char *Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	class FAnalysis final : public ITextLayoutAnalysis
	{
	public:
		FAnalysis(std::string Value, bool Rtl, bool Invalid, std::shared_ptr<std::size_t> Count)
		    : Text(std::move(Value)), bRtl(Rtl), bInvalid(Invalid), Calls(std::move(Count))
		{
		}
		const std::string &GetText() const noexcept override
		{
			return Text;
		}
		EDirection GetParagraphDirection(std::int64_t) const noexcept override
		{
			return bRtl ? EDirection::RightToLeft : EDirection::LeftToRight;
		}
		bool GetVisualRuns(std::int64_t Offset, std::int64_t Length, std::vector<FTextRun> &Result,
		    std::string &Error) const override
		{
			++*Calls;
			Error.clear();
			Result.clear();
			if (Length)
			{
				Result.push_back({Offset + (bInvalid ? 1 : 0), Length, std::uint8_t(bRtl ? 1 : 0), "Latn"});
			}
			return true;
		}

	private:
		std::string Text;
		bool bRtl, bInvalid;
		std::shared_ptr<std::size_t> Calls;
	};
	class FService final : public ITextLayoutService
	{
	public:
		explicit FService(bool Rtl = false, bool Invalid = false) : bRtl(Rtl), bInvalid(Invalid)
		{
		}
		const char *GetBackendId() const noexcept override
		{
			return "same-id";
		}
		std::shared_ptr<const ITextLayoutAnalysis> Analyze(std::string Text, EDirection Direction,
		    std::string &Error) const override
		{
			if (!ValidateLayoutText(Text, Direction, Error)) return {};
			++Analyses;
			Error.clear();
			return std::make_shared<FAnalysis>(std::move(Text), bRtl, bInvalid, Runs);
		}
		mutable std::size_t Analyses = 0;
		const std::shared_ptr<std::size_t> Runs = std::make_shared<std::size_t>(0);

	private:
		const bool bRtl, bInvalid;
	};
	class FFace final : public IFace
	{
	public:
		explicit FFace(std::vector<FShapedGlyph> Value) : Glyphs(std::move(Value))
		{
		}
		const FFaceInfo &GetInfo() const noexcept override
		{
			return Info;
		}
		std::uint32_t GetGlyphIndex(char32_t) const override
		{
			return 0;
		}
		bool GetMetrics(std::int32_t, FMetrics &, std::string &) const override
		{
			return false;
		}
		bool Rasterize(std::uint32_t, std::int32_t, FBitmap &, std::string &) const override
		{
			return false;
		}
		bool Shape(std::string_view Text, const FShapeOptions &Options, FShapedText &Result,
		    std::string &Error) const override
		{
			Require(Options.Layout && Options.Layout->Owns(Text), "shaping lost its validated context");
			Result = {Glyphs, Options.Direction};
			Error.clear();
			return true;
		}

	private:
		const std::vector<FShapedGlyph> Glyphs;
		FFaceInfo Info;
	};
	void CheckClusterSpacing()
	{
		std::string Error;
		// One base-plus-combining-mark cluster, followed by a separate character.
		const std::string Text = "a\u0301b";
		for (const bool Rtl : {false, true})
		{
			auto Layout = FTextLayout::Create(std::make_shared<FService>(Rtl), Text, EDirection::Auto, Error);
			Require(Layout != nullptr, "cluster-spacing analysis failed");
			const std::vector<FShapedGlyph> Glyphs = Rtl
			    ? std::vector<FShapedGlyph>{{12, 3, 128, 0, 0, 0}, {10, 0, 64, 0, 0, 0}, {11, 0, 0, 0, -64, 0}}
			    : std::vector<FShapedGlyph>{{10, 0, 64, 0, 0, 0}, {11, 0, 0, 0, -64, 0}, {12, 3, 128, 0, 0, 0}};
			auto Face = std::make_shared<FFace>(Glyphs);
			for (const double Spacing : {2.0, 0.0, -0.5, -4.0})
			{
				FTextLine Line;
				Require(Layout->ShapeLine(Face, {}, {}, 0, 4, Spacing, 1, Line, Error) && Error.empty(),
				    "cluster-spacing shape failed");
				// Per-glyph spacing incorrectly gives 3 + 3*Spacing and moves the attached mark.
				Require(Line.Width == std::max(0.0, 3 + 2 * Spacing) && Line.Text.Glyphs.size() == Glyphs.size() &&
				        Line.Text.Direction == (Rtl ? EDirection::RightToLeft : EDirection::LeftToRight),
				    "letter spacing was inserted inside a cluster or changed negative-width clamping");
				for (std::size_t I = 0; I < Glyphs.size(); ++I)
				{
					const auto &Actual = Line.Text.Glyphs[I];
					const auto &Expected = Glyphs[I];
					Require(Actual.Face == Face && Actual.Glyph.Glyph == Expected.Glyph &&
					        Actual.Glyph.Cluster == Expected.Cluster && Actual.Glyph.XAdvance == Expected.XAdvance &&
					        Actual.Glyph.YAdvance == Expected.YAdvance && Actual.Glyph.XOffset == Expected.XOffset &&
					        Actual.Glyph.YOffset == Expected.YOffset,
					    "spacing changed raw shaping metrics or glyph order");
				}
				Require(Line.Clusters.size() == 2, "combining glyph split its logical cluster");
				const auto Combined = std::find_if(Line.Clusters.begin(), Line.Clusters.end(),
				    [](const auto &Cluster)
				{
					return Cluster.ByteOffset == 0;
				});
				const auto Separate = std::find_if(Line.Clusters.begin(), Line.Clusters.end(),
				    [](const auto &Cluster)
				{
					return Cluster.ByteOffset == 3;
				});
				Require(Combined != Line.Clusters.end() && Combined->ByteLength == 3 &&
				        Separate != Line.Clusters.end() && Separate->ByteLength == 1,
				    "spacing changed UTF-8 cluster ranges");
				Require(Combined->Left == (Rtl ? std::min({2 + Spacing, 3 + Spacing, 3 + 2 * Spacing}) : std::min({0.0, 1.0, 1 + Spacing})) &&
				        Combined->Right == (Rtl ? std::max({2 + Spacing, 3 + Spacing, 3 + 2 * Spacing}) : std::max({0.0, 1.0, 1 + Spacing})) &&
				        Separate->Left == (Rtl ? std::min(0.0, 2 + Spacing) : std::min(1 + Spacing, 3 + 2 * Spacing)) &&
				        Separate->Right == (Rtl ? std::max(0.0, 2 + Spacing) : std::max(1 + Spacing, 3 + 2 * Spacing)),
				    "visual cluster edges disagree with cluster-end spacing");
				for (const auto Offset : {0, 3, 4})
				{
					const auto StartX = Rtl ? 3 + 2 * Spacing : 0;
					const auto EndX = Rtl ? 0 : 3 + 2 * Spacing;
					const double ExpectedX = Offset == 3 ? (Rtl ? 2 + Spacing : 1 + Spacing)
					    : Offset == 0                    ? StartX
					                                     : EndX;
					const auto Caret = std::find_if(Line.Carets.begin(), Line.Carets.end(),
					    [&](const auto &Value)
					{
						return Value.ByteOffset == Offset;
					});
					Require(Caret != Line.Carets.end() && Caret->X == ExpectedX &&
					        std::count_if(Line.Carets.begin(), Line.Carets.end(),
					            [&](const auto &Value)
					{
						return Value.ByteOffset == Offset;
					}) == 1,
					    "same-direction caret used cluster bounds instead of its logical pen position");
				}
			}
			FTextLine Previous;
			Previous.Width = 42;
			Previous.Text.Glyphs.push_back({Face, {99, 0, 64, 0, 0, 0}});
			Previous.Carets.push_back({3, 7, true});
			Previous.Clusters.push_back({0, 3, 7, 9, 0});
			auto Invalid = std::make_shared<FFace>(std::vector<FShapedGlyph>{{1, 2, 64, 0, 0, 0}});
			Require(!Layout->ShapeLine(Invalid, {}, {}, 0, 4, 2, 1, Previous, Error) &&
			        Error == "font.invalid_shape_result" && Previous.Width == 42 &&
			        Previous.Text.Glyphs.size() == 1 && Previous.Text.Glyphs[0].Glyph.Glyph == 99 &&
			        Previous.Carets.size() == 1 && Previous.Carets[0].ByteOffset == 3 && Previous.Carets[0].X == 7 &&
			        Previous.Clusters.size() == 1 && Previous.Clusters[0].Left == 7 && Previous.Clusters[0].Right == 9,
			    "failed cluster validation changed the previous line");
		}
	}
	void CheckInvisibleClusters()
	{
		std::string Error;
		for (const bool Rtl : {false, true})
		{
			for (const std::string Text : {"\u061c", "\u200b", "\u200c", "\u200d", "\u2060",
			         "\ufeff", "\ufe0f", "\U000e0100", "\u034f", "\u00ad", "\u180f", "\u200d\ufe0f"})
			{
				auto Controls = FTextLayout::Create(std::make_shared<FService>(Rtl), Text, EDirection::Auto, Error);
				auto Face = std::make_shared<FFace>(std::vector<FShapedGlyph>{{1, 0, 0, 0, 0, 0}});
				const auto End = static_cast<std::int64_t>(Text.size());
				FTextLine Line;
				Require(Controls && Controls->ShapeLine(Face, {}, {}, 0, End, 5, 1, Line, Error) &&
				        Line.Width == 0 && Line.Text.Glyphs.empty() && Line.Clusters.empty() && Line.Carets.size() == 2 &&
				        Line.Carets[0].ByteOffset == 0 && Line.Carets[0].X == 0 &&
				        Line.Carets[1].ByteOffset == End && Line.Carets[1].X == 0,
				    "invisible controls consumed letter spacing or lost logical endpoints");
			}
			// A shaping cluster beginning with a control must retain any visible text it also covers.
			for (const std::string Text : {"\u061ca", "\u200da", "a\ufe0f", "\u200da\ufe0f"})
			{
				auto Layout = FTextLayout::Create(std::make_shared<FService>(Rtl), Text, EDirection::Auto, Error);
				const std::vector<FShapedGlyph> Glyphs{{1, 0, 64, 0, 0, 0}, {2, 0, 0, 0, -64, 0}};
				auto Face = std::make_shared<FFace>(Glyphs);
				FTextLine Line;
				Require(Layout && Layout->ShapeLine(Face, {}, {}, 0, static_cast<std::int64_t>(Text.size()), 2, 1, Line, Error) &&
				        Line.Width == 3 && Line.Text.Glyphs.size() == Glyphs.size() && Line.Clusters.size() == 1 &&
				        Line.Clusters[0].ByteOffset == 0 && Line.Clusters[0].ByteLength == static_cast<std::int64_t>(Text.size()),
				    "format controls hid visible glyphs or inserted spacing within their shared cluster");
			}
		}
	}
}

int main()
{
	try
	{
		CheckClusterSpacing();
		CheckInvisibleClusters();
		std::string Error;
		Require(!FTextLayout::Create({}, "abc", EDirection::Auto, Error) &&
		        Error == "font.layout_service_unavailable",
		    "missing explicit layout service was silently replaced");
		auto Service = std::make_shared<FService>();
		Require(!FTextLayout::Create(Service, std::string("\xc0\x80", 2), EDirection::Auto, Error) &&
		        Service->Analyses == 0,
		    "invalid UTF-8 produced an analysis");
		Require(!FTextLayout::Create(Service, "abc", static_cast<EDirection>(255), Error) &&
		        Service->Analyses == 0,
		    "invalid direction produced an analysis");
		std::string Source = "abc";
		auto Snapshot = FTextLayout::Create(Service, Source, EDirection::Auto, Error);
		Require(Snapshot && Snapshot->GetText() == Source && Snapshot->Owns(Snapshot->GetText()) &&
		        !Snapshot->Owns(Source) && Snapshot->GetService() == Service,
		    "layout did not own its context/service");
		Source[0] = 'z';
		Require(Snapshot->GetText() == "abc", "source mutation changed an immutable snapshot");
		std::vector<FTextRun> Runs;
		Require(Snapshot->GetVisualRuns(0, 3, Runs, Error) && Runs.size() == 1 && Runs[0].Level == 0,
		    "mock visual runs failed");
		Require(Snapshot->GetVisualRuns(0, 3, Runs, Error) && *Service->Runs == 1,
		    "same-line query did not reuse the completed analysis");
		Require(!Snapshot->GetVisualRuns(-1, 1, Runs, Error) && Runs.size() == 1 && *Service->Runs == 1,
		    "invalid range reached the provider or changed output");
		auto Other = std::make_shared<FService>(true);
		auto OtherSnapshot = FTextLayout::Create(Other, "abc", EDirection::Auto, Error);
		Require(OtherSnapshot && OtherSnapshot->GetService() != Snapshot->GetService() &&
		        OtherSnapshot->GetVisualRuns(0, 3, Runs, Error) && Runs[0].Level == 1 &&
		        OtherSnapshot->GetParagraphDirection(0) == EDirection::RightToLeft,
		    "same backend ID reused another service instance/configuration's analysis");
		auto Invalid = FTextLayout::Create(std::make_shared<FService>(false, true), "abc", EDirection::Auto, Error);
		Require(Invalid && !Invalid->GetVisualRuns(0, 3, Runs, Error) &&
		        Error == "font.invalid_layout_result" && Runs.size() == 1 && Runs[0].Level == 1,
		    "invalid provider runs were committed");
		std::weak_ptr<ITextLayoutService> Lifetime = Service;
		Service.reset();
		Require(!Lifetime.expired() && Snapshot->GetVisualRuns(1, 2, Runs, Error),
		    "snapshot released its selected service instance");
		Snapshot.reset();
		Require(Lifetime.expired(), "destroyed snapshot retained its selected service");
		std::cout << "Backend-independent text layout contracts passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
