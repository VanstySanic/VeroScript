// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

// Adapted from RmlUi 6.3, MIT license; see ThirdParty/RmlUi/LICENSE.txt.

#include <RmlUi/Core.h>

using FontGlyphIndex = uint32_t;

struct FontGlyphData
{
	Rml::FontGlyph bitmap;
	Rml::Character character;
};

using FontGlyphMap = Rml::UnorderedMap<FontGlyphIndex, FontGlyphData>;
