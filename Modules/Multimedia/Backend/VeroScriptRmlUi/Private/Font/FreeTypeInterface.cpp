// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Adapted from RmlUi 6.3, MIT license; see ThirdParty/RmlUi/LICENSE.txt.

#include "FreeTypeInterface.h"
#include "VeroScriptRmlUiFontBridge.h"
#include <algorithm>
#include <cstring>

namespace FreeType
{
	bool AppendGlyph(VeroScript::Font::IFace &face, int size, FontGlyphIndex index, FontGlyphIndex atlas_index,
					 Character character, FontGlyphMap &glyphs)
	{
		if (!index)
		{
			return false;
		}
		VeroScript::Font::FBitmap bitmap;
		std::string error;
		if (!face.Rasterize(index, size, bitmap, error) ||
			!VeroScript::Font::Validate(bitmap, error))
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "Glyph rasterization failed: %s", error.c_str());
			return false;
		}
		Rml::FontGlyph glyph;
		glyph.bearing = {bitmap.BearingX, bitmap.BearingY};
		glyph.advance = bitmap.Advance;
		glyph.bitmap_dimensions = {bitmap.Width, bitmap.Height};
		glyph.color_format = bitmap.bColor ? Rml::ColorFormat::RGBA8 : Rml::ColorFormat::A8;
		if (!bitmap.Pixels.empty())
		{
			glyph.bitmap_owned_data.reset(new Rml::byte[bitmap.Pixels.size()]);
			std::memcpy(glyph.bitmap_owned_data.get(), bitmap.Pixels.data(), bitmap.Pixels.size());
			glyph.bitmap_data = glyph.bitmap_owned_data.get();
		}
		return glyphs.emplace(atlas_index, FontGlyphData{std::move(glyph), character}).second;
	}
	bool InitialiseFaceHandle(FontFaceHandleFreetype face, int size, FontGlyphMap &glyphs, FontMetrics &metrics)
	{
		VeroScript::Font::FMetrics value;
		std::string error;
		if (!VeroScript::RmlUi::GetFontFace(face).GetMetrics(size, value, error))
		{
			return false;
		}
		metrics.size = size;
		metrics.ascent = static_cast<float>(value.Ascent);
		metrics.descent = static_cast<float>(value.Descent);
		metrics.line_spacing = static_cast<float>(value.LineSpacing);
		metrics.x_height = static_cast<float>(value.XHeight);
		metrics.underline_position = static_cast<float>(value.UnderlinePosition);
		metrics.underline_thickness = static_cast<float>(value.UnderlineThickness);
		metrics.has_ellipsis = value.bHasEllipsis;
		// Shaping reports missing glyphs as zero, independently of the font's U+FFFD glyph index.
		constexpr FontGlyphIndex replacement_glyph_index = 0;
		auto it = glyphs.find(replacement_glyph_index);
		if (it == glyphs.end())
		{
			Rml::FontGlyph glyph;
			glyph.bitmap_dimensions = {std::max(1, size / 3), std::max(1, (size * 2) / 3)};
			glyph.advance = glyph.bitmap_dimensions.x + 2;
			glyph.bearing = {1, glyph.bitmap_dimensions.y};

			glyph.bitmap_owned_data.reset(new Rml::byte[glyph.bitmap_dimensions.x * glyph.bitmap_dimensions.y]);
			glyph.bitmap_data = glyph.bitmap_owned_data.get();

			for (int y = 0; y < glyph.bitmap_dimensions.y; y++)
			{
				for (int x = 0; x < glyph.bitmap_dimensions.x; x++)
				{
					constexpr int stroke = 1;
					int i = y * glyph.bitmap_dimensions.x + x;
					bool near_edge = (x < stroke || x >= glyph.bitmap_dimensions.x - stroke || y < stroke ||
									  y >= glyph.bitmap_dimensions.y - stroke);
					glyph.bitmap_owned_data[i] = (near_edge ? 0xdd : 0);
				}
			}

			glyphs[replacement_glyph_index].bitmap = std::move(glyph);
			glyphs[replacement_glyph_index].character = Rml::Character::Replacement;
		}
		return true;
	}

} // namespace FreeType
