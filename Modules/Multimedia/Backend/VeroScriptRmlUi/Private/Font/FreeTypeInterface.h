// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

// Adapted from RmlUi 6.3, MIT license; see ThirdParty/RmlUi/LICENSE.txt.

#include "FontEngineDefault/FontTypes.h"
#include "FontGlyph.h"
#include "VeroScriptFont.h"
#include <RmlUi/Core.h>

using Rml::Character;
using Rml::FontFaceHandleFreetype;
using Rml::FontMetrics;

namespace FreeType
{

	// Initializes face metrics and the replacement glyph for a given font size.
	bool InitialiseFaceHandle(FontFaceHandleFreetype face, int font_size, FontGlyphMap &glyphs, FontMetrics &metrics);

	// Build a new glyph representing the given glyph index and append to 'glyphs'.
	bool AppendGlyph(VeroScript::Font::IFace &face, int font_size, FontGlyphIndex source_index,
					 FontGlyphIndex atlas_index, Character character, FontGlyphMap &glyphs);

} // namespace FreeType
