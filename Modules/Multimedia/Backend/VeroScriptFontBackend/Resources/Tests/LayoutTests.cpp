// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontBackend.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
	using namespace VeroScript::Font;
	void Require(bool Value, const std::string &Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	void AppendUtf8(std::string &Text, std::uint32_t C)
	{
		if (C < 0x80)
		{
			Text += static_cast<char>(C);
		}
		else if (C < 0x800)
		{
			Text += static_cast<char>(0xc0 | (C >> 6));
			Text += static_cast<char>(0x80 | (C & 63));
		}
		else if (C < 0x10000)
		{
			Text += static_cast<char>(0xe0 | (C >> 12));
			Text += static_cast<char>(0x80 | ((C >> 6) & 63));
			Text += static_cast<char>(0x80 | (C & 63));
		}
		else
		{
			Text += static_cast<char>(0xf0 | (C >> 18));
			Text += static_cast<char>(0x80 | ((C >> 12) & 63));
			Text += static_cast<char>(0x80 | ((C >> 6) & 63));
			Text += static_cast<char>(0x80 | (C & 63));
		}
	}
	class FFace final : public IFace
	{
	public:
		const FFaceInfo &GetInfo() const noexcept override
		{
			return Info;
		}
		std::uint32_t GetGlyphIndex(char32_t C) const override
		{
			return static_cast<std::uint32_t>(C) + 1;
		}
		bool GetMetrics(std::int32_t, FMetrics &, std::string &) const override
		{
			return false;
		}
		bool Rasterize(std::uint32_t, std::int32_t, FBitmap &, std::string &) const override
		{
			return false;
		}
		bool Shape(std::string_view Text, const FShapeOptions &Options, FShapedText &Result, std::string &Error) const override
		{
			Require(Options.Layout && Options.Layout->Owns(Text), "shaping rescans an unowned paragraph context");
			Calls.push_back(Options);
			Contexts.emplace_back(Text);
			Result = {};
			Result.Direction = Options.Direction;
			const auto End = Options.ByteOffset + Options.ByteLength;
			for (auto I = Options.ByteOffset; I < End;)
			{
				const auto Offset = static_cast<std::uint32_t>(I);
				const auto Lead = static_cast<unsigned char>(Text[static_cast<std::size_t>(I++)]);
				const auto Count = Lead < 128 ? 0 : Lead < 0xe0 ? 1
				    : Lead < 0xf0                               ? 2
				                                                : 3;
				std::uint32_t C = Count ? Lead & (0x7f >> Count) : Lead;
				for (int J = 0; J < Count; ++J)
				{
					C = (C << 6) | (static_cast<unsigned char>(Text[static_cast<std::size_t>(I++)]) & 63);
				}
				Result.Glyphs.push_back({C + 1, Offset, 64, 0, 0, 0});
			}
			if (Options.Direction == EDirection::RightToLeft)
			{
				std::reverse(Result.Glyphs.begin(), Result.Glyphs.end());
			}
			Error.clear();
			return true;
		}
		mutable std::vector<FShapeOptions> Calls;
		mutable std::vector<std::string> Contexts;

	private:
		FFaceInfo Info;
	};
	std::string VisualText(const FTextLine &Line)
	{
		std::string Text;
		for (const auto &Value : Line.Text.Glyphs)
		{
			AppendUtf8(Text, Value.Glyph.Glyph - 1);
		}
		return Text;
	}
	FTextLine Shape(const std::shared_ptr<const FTextLayout> &Layout, const std::shared_ptr<FFace> &Face,
	    std::int64_t Begin, std::int64_t Length, double Spacing = 0)
	{
		std::string Error;
		FTextLine Line;
		Require(Layout->ShapeLine(Face, {}, {}, Begin, Length, Spacing, 1, Line, Error), Error);
		return Line;
	}
	void CheckLines()
	{
		std::string Error;
		auto Face = std::make_shared<FFace>();
		const std::string Text = "abc אבג 123";
		auto Layout = FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), Text, EDirection::Auto, Error);
		Require(Layout != nullptr, Error);
		const auto Line = Shape(Layout, Face, 0, static_cast<std::int64_t>(Text.size()), .5);
		Require(VisualText(Line) == "abc 123 גבא" && Line.Width == 16.5, "mixed-direction order or spacing changed");
		std::vector<double> Edges;
		for (const auto &Caret : Line.Carets)
		{
			if (Caret.ByteOffset == 4)
			{
				Edges.push_back(Caret.X);
			}
		}
		Require(Edges == std::vector<double>{6, 16.5}, "direction boundary lost its two visual caret positions");
		Require(std::any_of(Face->Calls.begin(), Face->Calls.end(), [](const auto &Call)
		{
			return Call.Direction == EDirection::RightToLeft && Call.Script == "Hebr";
		}),
		    "script or direction itemization was omitted");
		for (const auto &Context : Face->Contexts)
		{
			Require(Context == Text, "a run lost its surrounding logical text");
		}

		const std::string Wrapped = "אבג 123 xyz";
		auto Paragraph = FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), Wrapped, EDirection::Auto, Error);
		Require(Paragraph != nullptr && Paragraph->GetParagraphDirection(7) == EDirection::RightToLeft, "wrapped line lost its paragraph base direction");
		Require(VisualText(Shape(Paragraph, Face, 7, 7)) == "xyz 123", "soft wrapping reinterpreted the line as a new paragraph");
		Require(VisualText(Shape(Paragraph, Face, 0, 7)) == " גבא", "line L1 did not reset trailing whitespace");
		FTextLine Fragment;
		Require(Paragraph->ShapeLine(Face, {}, {}, 7, 7, 0, 1, Fragment, Error, 7, 4) &&
		        VisualText(Fragment) == " 123",
		    "styled fragment lost the complete line's L1 levels");
		Require(Fragment.Text.Glyphs.front().Glyph.Cluster == 10, "fragment clusters no longer address the original paragraph");

		for (const auto &Case : std::vector<std::pair<std::string, std::string>>{
		         {"A\u2067אב\u2069B", "AבאB"}, {"A\u202Ebc\u202CB", "AcbB"}})
		{
			auto Controls = FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), Case.first, EDirection::Auto, Error);
			Require(Controls != nullptr, Error);
			const auto Actual = Shape(Controls, Face, 0, static_cast<std::int64_t>(Case.first.size()));
			Require(VisualText(Actual) == Case.second && Actual.Width == 4, "directional controls became visible glyphs or consumed width");
		}
		for (const std::string TextWithControls : {"abc\u200F", "\u200Fabc", "\u2067אב\u2069", "\u061C"})
		{
			auto Controls = FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), TextWithControls, EDirection::Auto, Error);
			Require(Controls != nullptr, Error);
			const auto Actual = Shape(Controls, Face, 0, static_cast<std::int64_t>(TextWithControls.size()));
			Require(std::any_of(Actual.Carets.begin(), Actual.Carets.end(), [](const auto &Caret)
			{
				return Caret.ByteOffset == 0;
			}) && std::any_of(Actual.Carets.begin(), Actual.Carets.end(), [&](const auto &Caret)
			{
				return Caret.ByteOffset == static_cast<std::int64_t>(TextWithControls.size());
			}),
			    "invisible directional controls removed a logical line endpoint");
		}
	}
	void CheckErrors()
	{
		std::string Error;
		auto Face = std::make_shared<FFace>();
		Require(!FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), std::string("\xed\xa0\x80", 3), EDirection::Auto, Error), "invalid UTF-8 surrogate accepted");
		Require(!FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), "x", static_cast<EDirection>(255), Error), "invalid base direction accepted");
		auto Layout = FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), "אב\nx", EDirection::Auto, Error);
		Require(Layout != nullptr, Error);
		FTextLine Previous;
		Previous.Width = 42;
		Require(!Layout->ShapeLine(Face, {}, {}, 1, 1, 0, 1, Previous, Error) && Previous.Width == 42,
		    "non-boundary range accepted or failure modified output");
		Require(!Layout->ShapeLine(Face, {}, {}, 0, 6, 0, 1, Previous, Error) && Previous.Width == 42,
		    "line crossed a paragraph boundary");
		Require(!Layout->ShapeLine(Face, {}, {}, 0, 4, 0, 1, Previous, Error, 0, 5), "fragment escaped its line");
		Require(!Layout->ShapeLine(Face, {}, {}, 0, 4, std::numeric_limits<double>::infinity(), 1, Previous, Error), "nonfinite spacing accepted");
		Require(!Layout->ShapeLine(Face, {}, {}, std::numeric_limits<std::int64_t>::max(), 1, 0, 1, Previous, Error), "overflowing offset accepted");
		auto Empty = FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), {}, EDirection::RightToLeft, Error);
		Require(Empty != nullptr, Error);
		const auto Line = Shape(Empty, Face, 0, 0);
		Require(Line.Width == 0 && Line.Carets.size() == 1 && Line.Carets.front().ByteOffset == 0 &&
		        Line.Text.Direction == EDirection::RightToLeft,
		    "empty line lost its caret or direction");
		auto Trailing = FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), "אב\n", EDirection::Auto, Error);
		Require(Trailing && Trailing->GetParagraphDirection(static_cast<std::int64_t>(Trailing->GetText().size())) == EDirection::LeftToRight,
		    "trailing empty paragraph inherited the previous paragraph's automatic direction");
		FShapeOptions Foreign;
		Foreign.Layout = Empty;
		FResolvedText Unused;
		const auto Before = Face->Calls.size();
		Require(!ShapeWithFallback(Face, {}, std::string("\xff", 1), Foreign, Unused, Error) && Face->Calls.size() == Before,
		    "an unrelated validated layout bypassed UTF-8 validation");
	}
	void CheckUnicodeCorpus(const char *Path)
	{
		std::ifstream Input(Path);
		Require(static_cast<bool>(Input), "cannot read vendored Unicode conformance corpus");
		std::string Source;
		std::size_t Count = 0;
		// Bound the default regression cost, including the official bracket, override and isolate examples.
		while (Count < 512 && std::getline(Input, Source))
		{
			const auto First = Source.find_first_not_of(" \t\r");
			if (First == std::string::npos || Source[First] == '#')
			{
				continue;
			}
			std::vector<std::string> Fields;
			std::istringstream Row(Source);
			for (std::string Field; std::getline(Row, Field, ';');)
			{
				Fields.push_back(std::move(Field));
			}
			Require(Fields.size() == 5, "invalid Unicode corpus row");
			std::istringstream Codes(Fields[0]);
			std::string Text;
			std::vector<std::size_t> Offsets;
			for (std::uint32_t Code; Codes >> std::hex >> Code;)
			{
				Offsets.push_back(Text.size());
				AppendUtf8(Text, Code);
			}
			std::vector<std::string> Levels;
			std::istringstream LevelInput(Fields[3]);
			for (std::string Level; LevelInput >> Level;)
			{
				Levels.push_back(std::move(Level));
			}
			std::vector<std::size_t> Expected;
			std::istringstream ExpectedInput(Fields[4]);
			for (std::size_t Index; ExpectedInput >> Index;)
			{
				Expected.push_back(Index);
			}
			const auto Direction = std::stoi(Fields[1]);
			std::string Error;
			auto Layout = FTextLayout::Create(VeroScript::FontBackend::CreateTextLayoutService(), Text, Direction == 0 ? EDirection::LeftToRight : Direction == 1 ? EDirection::RightToLeft
			                                                                                                                                                      : EDirection::Auto,
			    Error);
			Require(Layout != nullptr && Levels.size() == Offsets.size(), "invalid conformance paragraph");
			Require((Layout->GetParagraphDirection(0) == EDirection::RightToLeft ? 1 : 0) == std::stoi(Fields[2]), "paragraph base level differs from Unicode corpus");
			std::vector<FTextRun> Runs;
			Require(Layout->GetVisualRuns(0, static_cast<std::int64_t>(Text.size()), Runs, Error), Error);
			std::vector<std::size_t> Actual;
			for (const auto &Run : Runs)
			{
				std::vector<std::size_t> Piece;
				for (auto I = std::lower_bound(Offsets.begin(), Offsets.end(), static_cast<std::size_t>(Run.ByteOffset));
				    I != Offsets.end() && *I < static_cast<std::size_t>(Run.ByteOffset + Run.ByteLength); ++I)
				{
					const auto Index = static_cast<std::size_t>(I - Offsets.begin());
					if (Levels[Index] != "x")
					{
						Piece.push_back(Index);
					}
				}
				if (Run.Level & 1)
				{
					std::reverse(Piece.begin(), Piece.end());
				}
				Actual.insert(Actual.end(), Piece.begin(), Piece.end());
			}
			Require(Actual == Expected, "visual run order differs from Unicode corpus");
			++Count;
		}
		Require(Count == 512, "Unicode conformance coverage was silently skipped");
	}
}

int main(int Argc, char **Argv)
{
	try
	{
		CheckLines();
		CheckErrors();
		if (Argc > 1)
		{
			CheckUnicodeCorpus(Argv[1]);
		}
		std::cout << "Font paragraph and line layout tests passed\n";
		return 0;
	}
	catch (const std::exception &Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
