// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontBackend.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
	void CheckZeroWidthText(const std::shared_ptr<VeroScript::Font::IFace> &Face)
	{
		using namespace VeroScript::Font;
		auto Service = VeroScript::FontBackend::CreateTextLayoutService();
		for (const auto Direction : {EDirection::LeftToRight, EDirection::RightToLeft})
		{
			const auto Shape = [&](const std::string &Text)
			{
				std::string Error;
				auto Layout = FTextLayout::Create(Service, Text, Direction, Error);
				FTextLine Line;
				if (!Layout || !Layout->ShapeLine(Face, {}, {}, 0, static_cast<std::int64_t>(Text.size()), 4, 8, Line, Error))
				{
					throw std::runtime_error("zero-width text shaping: " + Error);
				}
				return Line;
			};
			const auto Baseline = Shape("A");
			for (const std::string Control : {"\u200b", "\u200c", "\u200d", "\u2060", "\ufeff",
			         "\ufe0f", "\U000e0100", "\u034f", "\u00ad", "\u180f"})
			{
				const auto Empty = Shape(Control);
				if (Empty.Width != 0 || !Empty.Text.Glyphs.empty() || !Empty.Clusters.empty() || Empty.Carets.size() != 2 ||
				    Empty.Carets.front().ByteOffset != 0 ||
				    Empty.Carets.back().ByteOffset != static_cast<std::int64_t>(Control.size()))
				{
					throw std::runtime_error("zero-width format control acquired width or lost its logical range");
				}
				if (Shape(Control + "A").Width != Baseline.Width || Shape("A" + Control).Width != Baseline.Width)
				{
					throw std::runtime_error("format control changed adjacent visible text's width");
				}
			}
		}
	}
} // namespace

int main()
{
	try
	{
		using namespace VeroScript::Font;
		std::string Error;
		auto Service = VeroScript::FontBackend::CreateFontService();
		auto Bytes = GetDefaultFontData();
		auto Face = Service->Load(Bytes, 0, Error);
		if (!Face || Face->GetInfo().Family.empty())
		{
			throw std::runtime_error("independent font load: " + Error);
		}
		Bytes.clear();
		CheckZeroWidthText(Face);
		FMetrics Metrics;
		FBitmap Bitmap;
		if (!Face->GetMetrics(24, Metrics, Error) || Metrics.Ascent <= 0 || Metrics.LineSpacing <= 0 ||
		    !Face->Rasterize(Face->GetGlyphIndex(U'A'), 24, Bitmap, Error) || Bitmap.Pixels.empty())
		{
			throw std::runtime_error("font metrics / rasterization / owned source: " + Error);
		}
		FShapeOptions Options;
		Options.PixelSize = 24;
		FShapedText Shaped;
		if (!Face->Shape("office", Options, Shaped, Error) || Shaped.Glyphs.empty() || Shaped.Glyphs.size() >= 6)
		{
			throw std::runtime_error("HarfBuzz ligature shaping: " + Error);
		}
		const auto Count = Shaped.Glyphs.size();
		FResolvedText Resolved;
		if (!ShapeWithFallback(Face, {nullptr, Face}, "office", Options, Resolved, Error) ||
		    Resolved.Glyphs.size() != Count || Resolved.Glyphs.front().Face != Face)
		{
			throw std::runtime_error("resolved glyph face ownership: " + Error);
		}
		if (ShapeWithFallback({}, {}, "text", Options, Resolved, Error) || Error.empty())
		{
			throw std::runtime_error("missing primary face accepted");
		}
		if (Face->Shape(std::string("\xc0\x80", 2), Options, Shaped, Error) || Error.empty() ||
		    Shaped.Glyphs.size() != Count)
		{
			throw std::runtime_error("invalid UTF-8 / transactional output");
		}
		Options.Direction = EDirection::RightToLeft;
		if (!Face->Shape("abc", Options, Shaped, Error) || Shaped.Direction != EDirection::RightToLeft ||
		    Shaped.Glyphs.front().Cluster != 2)
		{
			throw std::runtime_error("explicit text direction: " + Error);
		}
		if (Face->GetMetrics(-1, Metrics, Error) || Service->Load({}, 0, Error))
		{
			throw std::runtime_error("invalid font options accepted");
		}
		std::atomic<bool> Success{true};
		std::vector<std::thread> Workers;
		for (int I = 0; I < 4; ++I)
		{
			Workers.emplace_back(
			    [&, I]
			{
				FShapeOptions Local;
				Local.PixelSize = 12 + I * 8;
				for (int J = 0; J < 30; ++J)
				{
					std::string Failure;
					FShapedText Text;
					if (!Face->Shape("shared font face", Local, Text, Failure) || Text.Glyphs.empty())
					{
						Success = false;
					}
				}
			});
		}
		for (auto &Worker : Workers)
		{
			Worker.join();
		}
		if (!Success)
		{
			throw std::runtime_error("shared face / mixed sizes failed");
		}
		std::cout << "Independent Font tests passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
