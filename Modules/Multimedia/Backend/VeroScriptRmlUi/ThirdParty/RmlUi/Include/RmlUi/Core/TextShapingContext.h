#pragma once

#include "StyleTypes.h"
#include "StringUtilities.h"
#include "Types.h"

namespace Rml {

/*
    Data extracted from the properties of an element to help provide context for text shaping and spacing.
*/
struct TextShapingContext {
	const String& language;
	Style::Direction text_direction = Style::Direction::Auto;
	Style::FontKerning font_kerning = Style::FontKerning::Auto;
	float letter_spacing = 0.0f; // Measured in pixels.

	// VeroScript integration: the complete logical paragraph and this line's UTF-8 byte range.
	// Borrowed until the synchronous font-engine operation returns. Empty paragraph_text uses string itself.
	StringView paragraph_text;
	// Stable immutable identity; paragraph_text must view this entire string, or be empty.
	SharedPtr<const String> paragraph_owner;
	int paragraph_offset = 0;
	int paragraph_length = -1;
	// An inline style fragment may occupy only part of a line. L1/L2 always use this complete line range.
	int line_offset = -1;
	int line_length = -1;
	// Optional selected logical byte range relative to the line, with an exclusive end.
	int selection_begin = -1;
	int selection_end = -1;
	ColourbPremultiplied selection_colour;
};

enum class TextAffinity { Leading, Trailing };

struct TextCaret {
	int byte_offset = 0;
	float x = 0.f;
	TextAffinity affinity = TextAffinity::Leading;
};

struct TextVisualSpan {
	int byte_begin = 0;
	int byte_end = 0;
	float left = 0.f;
	float right = 0.f;
};

struct TextLineLayout {
	float width = 0.f;
	Style::Direction base_direction = Style::Direction::Ltr;
	Vector<TextCaret> carets; // Visual order, retaining distinct affinities at directional boundaries.
	Vector<TextVisualSpan> spans; // Logical byte ranges mapped to visual intervals.
};

struct TextBidiRun {
	int byte_offset = 0;
	int byte_length = 0;
	uint8_t level = 0;
};

} // namespace Rml
