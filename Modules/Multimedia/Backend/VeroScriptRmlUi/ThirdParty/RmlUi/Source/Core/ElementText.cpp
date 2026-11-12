#include "../../Include/RmlUi/Core/ElementText.h"
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/Core.h"
#include "../../Include/RmlUi/Core/ElementDocument.h"
#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/Event.h"
#include "../../Include/RmlUi/Core/FontEngineInterface.h"
#include "../../Include/RmlUi/Core/MeshUtilities.h"
#include "../../Include/RmlUi/Core/Profiling.h"
#include "../../Include/RmlUi/Core/Property.h"
#include "../../Include/RmlUi/Core/RenderManager.h"
#include "../../Include/RmlUi/Core/TextShapingContext.h"
#include "ComputeProperty.h"
#include "ElementDefinition.h"
#include "ElementStyle.h"
#include "TransformState.h"
#include <algorithm>
#include <limits>

namespace Rml {

static bool BuildToken(String& token, const char*& token_begin, const char* string_end, bool first_token, bool collapse_white_space,
	bool break_at_endline, Style::TextTransform text_transformation, bool decode_escape_characters,
	bool unicode_line_breaks = false,
	ElementText::TextLayoutSource* mapping = nullptr, const char* source_begin = nullptr, int output_offset = 0);
static bool LastToken(const char* token_begin, const char* string_end, bool collapse_white_space, bool break_at_endline,
	bool unicode_line_breaks = false);

static int RoundDownToIntegerClamped(float value)
{
	constexpr int clamp = (1 << std::numeric_limits<float>::digits);
	constexpr float clamp_f = float{clamp};
	if (value >= clamp_f)
		return clamp;
	if (value <= -clamp_f)
		return -clamp;
	return static_cast<int>(value);
}

struct TextOverflowResolved {
	bool enabled = false;
	float overflow_width = 0.f;
	String overflow_text;
};

static TextOverflowResolved ResolveTextOverflow(Element* parent, FontFaceHandle font_face_handle)
{
	if (!parent)
		return {};

	const auto& parent_computed = parent->GetComputedValues();
	if (parent_computed.overflow_x() == Style::Overflow::Visible && parent_computed.overflow_y() == Style::Overflow::Visible)
		return {};

	const Style::TextOverflow text_overflow = parent_computed.text_overflow();
	if (text_overflow == Style::TextOverflow::Clip)
		return {};

	const Box& box = parent->GetBox();
	const BoxArea clip_area = parent->GetClipArea();

	auto AccumulateRightSideEdgesUpTo = [](const Box& box, BoxArea up_to_area) -> float {
		float result = 0;
		for (int i = (int)up_to_area; i < int(BoxArea::Content); i++)
			result += box.GetEdge((BoxArea)i, BoxEdge::Right);
		return result;
	};

	const float overflow_width = parent->GetScrollLeft() + box.GetSize().x + AccumulateRightSideEdgesUpTo(box, clip_area);

	constexpr char ellipsis_chars[] = "\xE2\x80\xA6"; // U+2026
	constexpr char dots_chars[] = "...";

	String overflow_text = (text_overflow == Style::TextOverflow::String
			? parent->GetComputedValues().text_overflow_string()
			: (GetFontEngineInterface()->GetFontMetrics(font_face_handle).has_ellipsis ? ellipsis_chars : dots_chars));

	return TextOverflowResolved{
		true,
		overflow_width,
		std::move(overflow_text),
	};
}

void LogMissingFontFace(Element* element)
{
	const String font_family_property = element->GetProperty<String>("font-family");
	if (font_family_property.empty())
	{
		Log::Message(Log::LT_WARNING, "No font face defined. Missing 'font-family' property. On element %s", element->GetAddress().c_str());
	}
	else
	{
		const ComputedValues& computed = element->GetComputedValues();
		const String font_face_description = GetFontFaceDescription(font_family_property, computed.font_style(), computed.font_weight());
		Log::Message(Log::LT_WARNING,
			"No font face defined. Ensure (1) that Context::Update is run after new elements are constructed, before Context::Render, "
			"and (2) that the specified font face %s has been successfully loaded. "
			"Please see previous log messages for all successfully loaded fonts. On element %s",
			font_face_description.c_str(), element->GetAddress().c_str());
	}
}

RMLUI_RTTI_Define(ElementText)

ElementText::ElementText(const String& tag) :
	Element(tag), colour(255, 255, 255), opacity(1), font_handle_version(0), geometry_dirty(true), dirty_layout_on_change(true),
	generated_decoration(Style::TextDecoration::None), decoration_property(Style::TextDecoration::None), font_effects_dirty(true),
	font_effects_handle(0)
{}

ElementText::~ElementText() {}

void ElementText::SetText(const String& _text)
{
	if (text != _text)
	{
		text = _text;
		layout_text_valid = false;
		layout_paragraph.reset();
		layout_paragraph_offset = 0;
		layout_paragraph_shifts.clear();

		if (dirty_layout_on_change)
			DirtyLayout();
	}
}

const String& ElementText::GetText() const
{
	return text;
}

void ElementText::OnRender()
{
	RMLUI_ZoneScoped;

	FontFaceHandle font_face_handle = GetFontFaceHandle();
	if (font_face_handle == 0)
		return;

	RenderManager& render_manager = GetContext()->GetRenderManager();

	// If our font effects have potentially changed, update it and force a geometry generation if necessary.
	if (font_effects_dirty && UpdateFontEffects())
		geometry_dirty = true;

	// Dirty geometry if font version has changed.
	int new_version = GetFontEngineInterface()->GetVersion(font_face_handle);
	if (new_version != font_handle_version)
	{
		font_handle_version = new_version;
		geometry_dirty = true;
	}

	// Regenerate the geometry if the colour or font configuration has altered.
	if (geometry_dirty)
		GenerateGeometry(render_manager, font_face_handle);

	// Regenerate text decoration if necessary.
	if (decoration_property != generated_decoration)
	{
		if (decoration_property == Style::TextDecoration::None)
		{
			decoration.reset();
		}
		else
		{
			Mesh mesh;
			if (decoration)
				mesh = decoration->Release(Geometry::ReleaseMode::ClearMesh);
			else
				decoration = MakeUnique<Geometry>();

			GenerateDecoration(mesh, font_face_handle);
			*decoration = GetRenderManager()->MakeGeometry(std::move(mesh));
		}

		generated_decoration = decoration_property;
	}

	const Vector2f translation = GetAbsoluteOffset();

	bool render = true;

	// Do a visibility test against the scissor region to avoid unnecessary render calls. Instead of handling
	// culling in complicated transform cases, for simplicity we always proceed to render if one is detected.
	Rectanglei scissor_region = render_manager.GetScissorRegion();
	if (!scissor_region.Valid())
		scissor_region = Rectanglei::FromSize(render_manager.GetViewport());

	if (!GetTransformState() || !GetTransformState()->GetTransform())
	{
		const FontMetrics& font_metrics = GetFontEngineInterface()->GetFontMetrics(GetFontFaceHandle());
		const int ascent = Math::RoundUpToInteger(font_metrics.ascent);
		const int descent = Math::RoundUpToInteger(font_metrics.descent);

		render = false;
		for (const Line& line : lines)
		{
			const Vector2i baseline = Vector2i(translation + line.position);
			const Rectanglei line_region = Rectanglei::FromCorners(baseline - Vector2i(0, ascent), baseline + Vector2i(line.width, descent));

			if (line_region.Valid() && scissor_region.Intersects(line_region))
			{
				render = true;
				break;
			}
		}
	}

	if (render)
	{
		for (size_t i = 0; i < geometry.size(); ++i)
			geometry[i].geometry.Render(translation, geometry[i].texture);
	}

	if (decoration)
		decoration->Render(translation);
}

const ElementText::TextLayoutSource& ElementText::GetLayoutText(bool decode_escape_characters)
{
	const auto& computed = GetComputedValues();
	const auto white_space = computed.white_space();
	const auto transformation = computed.text_transform();
	if (layout_text_valid && layout_text_decode == decode_escape_characters && layout_white_space == white_space &&
		layout_text_transform == transformation)
		return layout_text;

	layout_text = {};
	layout_paragraph.reset();
	layout_paragraph_offset = 0;
	layout_paragraph_shifts.clear();
	layout_text.source_to_text.resize(text.size() + 1);
	const bool collapse = white_space == Style::WhiteSpace::Normal || white_space == Style::WhiteSpace::Nowrap ||
		white_space == Style::WhiteSpace::Preline;
	const bool break_endline = white_space == Style::WhiteSpace::Pre || white_space == Style::WhiteSpace::Prewrap ||
		white_space == Style::WhiteSpace::Preline;
	const char* cursor = text.data();
	const char* end = cursor + text.size();
	while (cursor != end)
	{
		String token;
		BuildToken(token, cursor, end, false, collapse, break_endline, transformation, decode_escape_characters, true,
			&layout_text, text.data(), (int)layout_text.text.size());
		layout_text.text += token;
	}
	layout_text_decode = decode_escape_characters;
	layout_white_space = white_space;
	layout_text_transform = transformation;
	layout_text_valid = true;
	return layout_text;
}

void ElementText::SetLayoutParagraph(SharedPtr<const String> paragraph, int node_offset, Style::Direction paragraph_direction,
	Vector<TextParagraphShift> shifts)
{
	layout_paragraph = std::move(paragraph);
	layout_paragraph_offset = node_offset;
	layout_paragraph_direction = paragraph_direction;
	layout_paragraph_shifts = std::move(shifts);
}

bool ElementText::GenerateLine(String& line, int& line_length, float& line_width, int line_begin, float maximum_line_width, float right_spacing_width,
	bool trim_whitespace_prefix, bool decode_escape_characters, bool allow_empty, int* paragraph_offset_out)
{
	RMLUI_ZoneScoped;
	RMLUI_ASSERT(maximum_line_width >= 0.f);

	FontFaceHandle font_face_handle = GetFontFaceHandle();

	// Initialise the output variables.
	line.clear();
	line_length = 0;
	line_width = 0;
	if (paragraph_offset_out)
		*paragraph_offset_out = 0;

	// Bail if we don't have a valid font face.
	if (font_face_handle == 0)
	{
		LogMissingFontFace(GetParentNode() ? GetParentNode() : this);
		return true;
	}

	// Determine how we are processing white-space while formatting the text.
	using namespace Style;
	const auto& computed = GetComputedValues();
	const WhiteSpace white_space_property = computed.white_space();
	const bool collapse_white_space =
		(white_space_property == WhiteSpace::Normal || white_space_property == WhiteSpace::Nowrap || white_space_property == WhiteSpace::Preline);
	const bool break_at_line =
		(white_space_property == WhiteSpace::Normal || white_space_property == WhiteSpace::Prewrap || white_space_property == WhiteSpace::Preline);
	const bool break_at_endline =
		(white_space_property == WhiteSpace::Pre || white_space_property == WhiteSpace::Prewrap || white_space_property == WhiteSpace::Preline);

	const TextShapingContext text_shaping_context{computed.language(), computed.direction(), computed.font_kerning(), computed.letter_spacing()};
	TextTransform text_transform_property = computed.text_transform();
	WordBreak word_break = computed.word_break();

	FontEngineInterface* font_engine_interface = GetFontEngineInterface();
	if (layout_font_engine != font_engine_interface)
	{
		TextLineLayout probe;
		has_visual_text_layout = font_engine_interface->GetStringLayout(font_face_handle, {}, text_shaping_context, probe);
		layout_font_engine = font_engine_interface;
	}
	const TextLayoutSource* normalized = has_visual_text_layout ? &GetLayoutText(decode_escape_characters) : nullptr;
	if (normalized && !layout_paragraph)
		SetLayoutParagraph(MakeShared<const String>(normalized->text), 0, computed.direction());
	auto ParagraphOffset = [&](const String& candidate) {
		int offset = normalized ? normalized->source_to_text[size_t(line_begin)] : 0;
		if (normalized && collapse_white_space && trim_whitespace_prefix && size_t(offset) < normalized->text.size() &&
			normalized->text[size_t(offset)] == ' ' && (candidate.empty() || candidate.front() != ' '))
			++offset;
		const auto shift = std::upper_bound(layout_paragraph_shifts.begin(), layout_paragraph_shifts.end(), offset,
			[](int position, const TextParagraphShift& item) { return position < item.text_offset; });
		if (shift != layout_paragraph_shifts.begin())
			offset += (shift - 1)->added_bytes;
		return layout_paragraph_offset + offset;
	};
	if (paragraph_offset_out && normalized)
		*paragraph_offset_out = ParagraphOffset({});
	const bool measure_complete_line = normalized && (!break_at_line || maximum_line_width >= float(std::numeric_limits<int>::max()));
	auto IsNormalizedBoundary = [&](const char* position) {
		if (!normalized || position == text.data() || position == text.data() + text.size())
			return true;
		const auto offset = size_t(position - text.data());
		// Both bytes are source boundaries, but CRLF is one indivisible line terminator.
		return !(position[-1] == '\r' && *position == '\n') && (static_cast<unsigned char>(*position) & 0xc0) != 0x80 &&
			normalized->source_to_text[offset] != normalized->source_to_text[offset - 1];
	};
	auto MeasureToken = [&](const String& token, Character prior_character) {
		if (!normalized)
			return font_engine_interface->GetStringWidth(font_face_handle, token, text_shaping_context, prior_character);
		const String candidate = line + token;
		auto context = text_shaping_context;
		context.paragraph_owner = layout_paragraph;
		context.paragraph_text = *layout_paragraph;
		context.paragraph_offset = ParagraphOffset(candidate);
		context.paragraph_length = (int)candidate.size();
		context.text_direction = layout_paragraph_direction;
		if (paragraph_offset_out)
			*paragraph_offset_out = context.paragraph_offset;
		// Measure the complete candidate from the same paragraph, preserving joins across token boundaries.
		return font_engine_interface->GetStringWidth(font_face_handle, candidate, context) - (int)line_width;
	};

	// Starting at the line_begin character, we generate sections of the text (we'll call them tokens) depending on the
	// white-space parsing parameters. Each section is then appended to the line if it can fit. If not, or if an
	// endline is found (and we're processing them), then the line is ended. kthxbai!
	const char* token_begin = text.c_str() + line_begin;
	const char* string_end = text.c_str() + text.size();
	while (token_begin != string_end)
	{
		String token;
		const char* next_token_begin = token_begin;
		Character previous_codepoint = Character::Null;
		if (!line.empty())
			previous_codepoint =
				StringUtilities::ToCharacter(StringUtilities::SeekBackwardUTF8(&line.back(), line.data()), line.data() + line.size());

		// Generate the next token and determine its pixel-length.
		bool break_line = BuildToken(token, next_token_begin, string_end, line.empty() && trim_whitespace_prefix, collapse_white_space,
			break_at_endline, text_transform_property, decode_escape_characters, has_visual_text_layout);
		int token_width = measure_complete_line ? 0 : MeasureToken(token, previous_codepoint);

		// If we're breaking to fit a line box, check if the token can fit on the line before we add it.
		if (break_at_line && !measure_complete_line)
		{
			const bool is_last_token = LastToken(next_token_begin, string_end, collapse_white_space, break_at_endline, has_visual_text_layout);
			int max_token_width = RoundDownToIntegerClamped(maximum_line_width - (is_last_token ? line_width + right_spacing_width : line_width));

			if (token_width > max_token_width)
			{
				if (word_break == WordBreak::BreakAll || (word_break == WordBreak::BreakWord && line.empty()))
				{
					// Try to break up the word
					max_token_width = RoundDownToIntegerClamped(maximum_line_width - line_width);
					const int token_max_size = int(next_token_begin - token_begin);
					const char* partial_string_end = token_begin + token_max_size;

					// @performance: Can be made much faster. Use string width heuristics and logarithmic search.
					while (true)
					{
						partial_string_end = StringUtilities::SeekBackwardUTF8(partial_string_end - 1, token_begin);
						while (partial_string_end != token_begin && !IsNormalizedBoundary(partial_string_end))
							partial_string_end = StringUtilities::SeekBackwardUTF8(partial_string_end - 1, token_begin);

						bool force_loop_break_at_end = false;
						if (partial_string_end == token_begin)
						{
							// Not even the first character of the token fits. Let it overflow onto the next line if we can.
							if (allow_empty || !line.empty())
								return false;

							// Continue by forcing the first character to be consumed, even though it will overflow.
							partial_string_end = StringUtilities::SeekForwardUTF8(token_begin + 1, token_begin + token_max_size);
							while (partial_string_end != token_begin + token_max_size && !IsNormalizedBoundary(partial_string_end))
								partial_string_end = StringUtilities::SeekForwardUTF8(partial_string_end + 1, token_begin + token_max_size);
							force_loop_break_at_end = true;
						}

						token.clear();
						next_token_begin = token_begin;
						BuildToken(token, next_token_begin, partial_string_end, line.empty() && trim_whitespace_prefix, collapse_white_space,
							break_at_endline, text_transform_property, decode_escape_characters, has_visual_text_layout);
						// Collapsed prefix whitespace may consume source without producing a glyph. A forced first
						// character must include a complete decoded entity/codepoint, never an entity's source bytes.
						while (normalized && force_loop_break_at_end && token.empty() && partial_string_end != token_begin + token_max_size)
						{
							partial_string_end = StringUtilities::SeekForwardUTF8(partial_string_end + 1, token_begin + token_max_size);
							while (partial_string_end != token_begin + token_max_size && !IsNormalizedBoundary(partial_string_end))
								partial_string_end = StringUtilities::SeekForwardUTF8(partial_string_end + 1, token_begin + token_max_size);
							next_token_begin = token_begin;
							BuildToken(token, next_token_begin, partial_string_end, line.empty() && trim_whitespace_prefix, collapse_white_space,
								break_at_endline, text_transform_property, decode_escape_characters, true);
						}
						token_width = MeasureToken(token, previous_codepoint);

						if (force_loop_break_at_end || token_width <= max_token_width)
							break;
					}

					break_line = true;
				}
				else if (allow_empty || !line.empty())
				{
					// Let the token overflow into the next line.
					return false;
				}
			}
		}

		// The token can fit on the end of the line, so add it onto the end and increment our width and length counters.
		line += token;
		line_length += (int)(next_token_begin - token_begin);
		line_width += token_width;

		// Break out of the loop if an endline was forced.
		if (break_line && (allow_empty || !line.empty()))
		{
			if (measure_complete_line)
				line_width = float(MeasureToken({}, Character::Null));
			return false;
		}

		// Set the beginning of the next token.
		token_begin = next_token_begin;
	}

	if (measure_complete_line)
		line_width = float(MeasureToken({}, Character::Null));
	return true;
}

void ElementText::ClearLines()
{
	RMLUI_ZoneScoped;
	lines.clear();
	generated_decoration = Style::TextDecoration::None;
	geometry_dirty = true;
}

void ElementText::AddLine(Vector2f line_position, String line)
{
	if (font_effects_dirty)
		UpdateFontEffects();

	lines.emplace_back(std::move(line), line_position);

	geometry_dirty = true;
}

void ElementText::AddLine(Vector2f line_position, String line, SharedPtr<const String> paragraph, int paragraph_offset,
	int selection_begin, int selection_end, ColourbPremultiplied selection_colour, int line_offset, int line_length)
{
	AddLine(line_position, std::move(line));
	auto& added = lines.back();
	added.paragraph = std::move(paragraph);
	added.paragraph_offset = paragraph_offset;
	added.line_offset = line_offset;
	added.line_length = line_length;
	added.paragraph_direction = added.paragraph == layout_paragraph ? layout_paragraph_direction : GetComputedValues().direction();
	added.selection_begin = selection_begin;
	added.selection_end = selection_end;
	added.selection_colour = selection_colour;
}

void ElementText::SuppressAutoLayout()
{
	dirty_layout_on_change = false;
}

void ElementText::OnPropertyChange(const PropertyIdSet& changed_properties)
{
	RMLUI_ZoneScoped;

	Element::OnPropertyChange(changed_properties);

	bool colour_changed = false;
	bool font_face_changed = false;
	auto& computed = GetComputedValues();

	if (changed_properties.Contains(PropertyId::Color) || changed_properties.Contains(PropertyId::Opacity))
	{
		const float new_opacity = computed.opacity();
		const bool opacity_changed = opacity != new_opacity;

		ColourbPremultiplied new_colour = computed.color().ToPremultiplied(new_opacity);
		colour_changed = colour != new_colour;

		if (colour_changed)
		{
			colour = new_colour;
		}
		if (opacity_changed)
		{
			opacity = new_opacity;
			font_effects_dirty = true;
			geometry_dirty = true;
		}
	}

	if (changed_properties.Contains(PropertyId::FontFamily) ||      //
		changed_properties.Contains(PropertyId::FontWeight) ||      //
		changed_properties.Contains(PropertyId::FontStyle) ||       //
		changed_properties.Contains(PropertyId::FontSize) ||        //
		changed_properties.Contains(PropertyId::FontKerning) ||     //
		changed_properties.Contains(PropertyId::LetterSpacing) ||   //
		changed_properties.Contains(PropertyId::RmlUi_Language) ||  //
		changed_properties.Contains(PropertyId::RmlUi_Direction) || //
		changed_properties.Contains(PropertyId::TextOverflow))
	{
		font_face_changed = true;
		geometry_dirty = true;

		font_effects_handle = 0;
		font_effects_dirty = true;
		font_handle_version = 0;
	}

	if (changed_properties.Contains(PropertyId::FontEffect))
	{
		font_effects_dirty = true;
	}

	if (changed_properties.Contains(PropertyId::TextDecoration))
	{
		decoration_property = computed.text_decoration();
		if (decoration && decoration_property == Style::TextDecoration::None)
			decoration.reset();
	}

	if (font_face_changed)
	{
		// We have to let our document know we need to be regenerated.
		if (dirty_layout_on_change)
			DirtyLayout();
	}
	else if (colour_changed)
	{
		// Force the geometry to be regenerated.
		geometry_dirty = true;

		// Re-colour the decoration geometry.
		if (decoration)
		{
			Mesh mesh = decoration->Release();
			for (Vertex& vertex : mesh.vertices)
				vertex.colour = colour;

			if (RenderManager* render_manager = GetRenderManager())
				*decoration = render_manager->MakeGeometry(std::move(mesh));
		}
	}
}

void ElementText::GetRML(String& content)
{
	content += StringUtilities::EncodeRml(text);
}

bool ElementText::UpdateFontEffects()
{
	RMLUI_ZoneScoped;

	if (GetFontFaceHandle() == 0)
		return false;

	static const FontEffectList empty_font_effects;

	// Fetch the font-effect for this text element
	const FontEffectList* font_effects = &empty_font_effects;
	if (GetComputedValues().has_font_effect())
	{
		if (const Property* p = GetProperty(PropertyId::FontEffect))
			if (FontEffectsPtr effects = p->Get<FontEffectsPtr>())
				font_effects = &effects->list;
	}

	// Request a font layer configuration to match this set of effects. If this is different from
	// our old configuration, then return true to indicate we'll need to regenerate geometry.
	FontEffectsHandle new_font_effects_handle = GetFontEngineInterface()->PrepareFontEffects(GetFontFaceHandle(), *font_effects);
	// Preserve retry state when a font atlas/effect cannot be prepared.
	font_effects_dirty = false;
	if (new_font_effects_handle != font_effects_handle)
	{
		font_effects_handle = new_font_effects_handle;
		return true;
	}

	return false;
}

void ElementText::GenerateGeometry(RenderManager& render_manager, const FontFaceHandle font_face_handle)
{
	RMLUI_ZoneScopedC(0xD2691E);

	const TextOverflowResolved text_overflow = ResolveTextOverflow(GetParentNode(), font_face_handle);

	const auto& computed = GetComputedValues();
	const TextShapingContext base_context{computed.language(), computed.direction(), computed.font_kerning(), computed.letter_spacing()};
	auto LineContext = [&](const Line& line) {
		auto context = base_context;
		if (line.paragraph)
		{
			context.paragraph_owner = line.paragraph;
			context.paragraph_text = *line.paragraph;
			context.paragraph_offset = line.paragraph_offset;
			context.paragraph_length = (int)line.text.size();
			context.line_offset = line.line_offset;
			context.line_length = line.line_length;
			context.text_direction = line.paragraph_direction;
		}
		context.selection_begin = line.selection_begin;
		context.selection_end = line.selection_end;
		context.selection_colour = line.selection_colour;
		return context;
	};

	TexturedMeshList mesh_list;
	mesh_list.reserve(geometry.size());

	for (Line& line : lines)
	{
		const auto text_shaping_context = LineContext(line);
		line.width = GetFontEngineInterface()->GenerateString(render_manager, font_face_handle, font_effects_handle, line.text, line.position, colour,
			opacity, text_shaping_context, mesh_list);
	}

	const auto text_overflows_on_line = [&](const Line& line) { return line.position.x + line.width > text_overflow.overflow_width; };
	if (text_overflow.enabled && std::any_of(lines.begin(), lines.end(), text_overflows_on_line))
	{
		mesh_list.clear();

		for (Line& line : lines)
		{
			if (line.text.empty())
				continue;

			String abbreviated_text;
			StringView text_submit_view = line.text;
			auto AbbreviatedContext = [&](const String& candidate) {
				auto context = LineContext(line);
				if (line.paragraph)
				{
					String paragraph = *line.paragraph;
					paragraph.replace(size_t(line.paragraph_offset), line.text.size(), candidate);
					context.paragraph_owner = MakeShared<const String>(std::move(paragraph));
					context.paragraph_text = *context.paragraph_owner;
					context.paragraph_length = (int)candidate.size();
					if (context.line_length >= 0)
						context.line_length += (int)candidate.size() - (int)line.text.size();
				}
				return context;
			};
			StringIteratorU8 view(line.text, line.text.size());
			--view;

			// If we have text overflow, reduce the string one character at a time, append the ellipsis or custom
			// string, and try again until it fits. @performance Can be improved by e.g. logarithmic search. Consider
			// combining it with the word-breaking algorithm of 'GenerateLine'.
			for (; text_overflows_on_line(line) && view && view.get() != line.text.c_str(); --view)
			{
				abbreviated_text.reserve(line.text.size() + text_overflow.overflow_text.size());
				abbreviated_text.assign(line.text.c_str(), view.get());
				abbreviated_text.append(text_overflow.overflow_text);
				const auto context = AbbreviatedContext(abbreviated_text);
				line.width = GetFontEngineInterface()->GetStringWidth(font_face_handle, abbreviated_text, context);
				text_submit_view = abbreviated_text;
			}

			const auto text_shaping_context = abbreviated_text.empty() ? LineContext(line) : AbbreviatedContext(abbreviated_text);
			line.width = GetFontEngineInterface()->GenerateString(render_manager, font_face_handle, font_effects_handle, text_submit_view,
				line.position, colour, opacity, text_shaping_context, mesh_list);
		}
	}

	// Apply the new geometry and textures. Reuse the old geometry if the mesh matches, which can be relatively common
	// where the layout is changed in a way that does not visually affect this element.
	geometry.resize(mesh_list.size());
	for (size_t i = 0; i < mesh_list.size(); i++)
	{
		if (!geometry[i].geometry || geometry[i].geometry.GetMesh() != mesh_list[i].mesh)
			geometry[i].geometry = render_manager.MakeGeometry(std::move(mesh_list[i].mesh));

		geometry[i].texture = mesh_list[i].texture;
	}

	generated_decoration = Style::TextDecoration::None;
	geometry_dirty = false;
}

void ElementText::GenerateDecoration(Mesh& mesh, const FontFaceHandle font_face_handle)
{
	RMLUI_ZoneScopedC(0xA52A2A);
	RMLUI_ASSERT(decoration);

	const FontMetrics& metrics = GetFontEngineInterface()->GetFontMetrics(font_face_handle);

	float offset = 0.f;
	switch (decoration_property)
	{
	case Style::TextDecoration::Underline: offset = metrics.underline_position; break;
	case Style::TextDecoration::Overline: offset = -1.1f * metrics.ascent; break;
	case Style::TextDecoration::LineThrough: offset = -0.65f * metrics.x_height; break;
	case Style::TextDecoration::None: return;
	}

	for (const Line& line : lines)
	{
		const Vector2f position = {line.position.x, line.position.y + offset};
		const Vector2f size = {(float)line.width, metrics.underline_thickness};
		MeshUtilities::GenerateLine(mesh, position, size, colour);
	}
}

static bool BuildToken(String& token, const char*& token_begin, const char* string_end, bool first_token, bool collapse_white_space,
	bool break_at_endline, Style::TextTransform text_transformation, bool decode_escape_characters,
	bool unicode_line_breaks, ElementText::TextLayoutSource* mapping, const char* source_begin, int output_offset)
{
	RMLUI_ASSERT(token_begin != string_end);
	auto MapConsumed = [&](const char* begin, const char* end, int before, int after) {
		if (!mapping)
			return;
		for (const char* p = begin; p < end; ++p)
			mapping->source_to_text[size_t(p - source_begin)] = output_offset + before;
		mapping->source_to_text[size_t(end - source_begin)] = output_offset + after;
	};

	// Long paragraphs commonly contain many short tokens. Reserving the whole remaining paragraph for
	// each token repeatedly allocates a large buffer during both normalization and line measurement.
	token.reserve(std::min<size_t>(size_t(string_end - token_begin), 128) + token.size());

	// Check what the first character of the token is; all we need to know is if it is white-space or not.
	bool parsing_white_space = StringUtilities::IsWhitespace(*token_begin);

	// Loop through the string from the token's beginning until we find an end to the token. This can occur in various
	// places, depending on the white-space processing;
	//  - at the end of a section of non-white-space characters,
	//  - at the end of a section of white-space characters, if we're not collapsing white-space,
	//  - at an endline token, if we're breaking on endlines.
	while (token_begin != string_end)
	{
		// Keep original UTF-8 offsets while ending the physical line at Unicode separators.
		// ASCII newlines retain the existing CSS white-space collapsing behavior.
		if (unicode_line_breaks)
		{
			const auto c = static_cast<unsigned char>(*token_begin);
			const int length = !break_at_endline && (c == '\r' || c == '\n') ? 0 :
				(int)StringUtilities::GetLineBreakLength(token_begin, string_end);
			if (length)
			{
				const int before = (int)token.size();
				token.append(token_begin, size_t(length));
				for (int i = 0; i < length; ++i)
					MapConsumed(token_begin + i, token_begin + i + 1, before + i, before + i + 1);
				if (mapping && !(length == 3 && c == 0xe2 && static_cast<unsigned char>(token_begin[2]) == 0xa8))
					mapping->paragraph_breaks.push_back(output_offset + (int)token.size());
				token_begin += length;
				return true;
			}
		}
		bool force_non_whitespace = false;
		char character = *token_begin;

		const char* escape_begin = token_begin;
		const int output_before = (int)token.size();

		// Check for an ampersand; if we find one, we've got an HTML escaped character.
		if (decode_escape_characters && character == '&')
		{
			// Find the terminating ';'.
			while (token_begin != string_end && *token_begin != ';')
				++token_begin;

			// If we couldn't find the ';', print the token like normal text.
			if (token_begin == string_end)
			{
				token_begin = escape_begin;
			}
			// We could find a ';', parse the escape code. If the escape code is recognised, set the parsed character
			// to the appropriate one. If it is a non-breaking space, prevent it being picked up as whitespace. If it
			// is not recognised, print the token like normal text.
			else
			{
				String escape_code(escape_begin + 1, token_begin);

				if (escape_code == "lt")
					character = '<';
				else if (escape_code == "gt")
					character = '>';
				else if (escape_code == "amp")
					character = '&';
				else if (escape_code == "quot")
					character = '"';
				else if (escape_code == "nbsp")
				{
					character = ' ';
					force_non_whitespace = true;
				}
				else
					token_begin = escape_begin;
			}
		}

		// Check for an endline token; if we're breaking on endlines and we find one, then return true to indicate a
		// forced break.
		if (break_at_endline && character == '\n')
		{
			token += '\n';
			token_begin++;
			MapConsumed(escape_begin, token_begin, output_before, (int)token.size());
			return true;
		}

		// If we've transitioned from white-space characters to non-white-space characters, or vice-versa, then check
		// if should terminate the token; if we're not collapsing white-space, then yes (as sections of white-space are
		// non-breaking), otherwise only if we've transitioned from characters to white-space.
		bool white_space = !force_non_whitespace && StringUtilities::IsWhitespace(character);
		if (white_space != parsing_white_space)
		{
			if (!collapse_white_space)
			{
				// Restore pointer to the beginning of the escaped token, if we processed an escape code.
				token_begin = escape_begin;
				return false;
			}

			// We're collapsing white-space; we only tokenise words, not white-space, so we're only done tokenising
			// once we've begun parsing non-white-space and then found white-space.
			if (!parsing_white_space)
			{
				// However, if we are the last non-whitespace character in the string, and there are trailing
				// whitespace characters after this token, then we need to append a single space to the end of this
				// token.
				if (token_begin != string_end && LastToken(token_begin, string_end, collapse_white_space, break_at_endline, unicode_line_breaks))
				{
					token += ' ';
					if (mapping)
					{
						MapConsumed(escape_begin, string_end, output_before, (int)token.size());
						token_begin = string_end;
					}
				}

				return false;
			}

			// We've transitioned from white-space to non-white-space, so we append a single white-space character.
			if (!first_token)
				token += ' ';
			if (mapping)
				mapping->source_to_text[size_t(escape_begin - source_begin)] = output_offset + (int)token.size();

			parsing_white_space = false;
		}

		// If the current character is white-space, we'll append a space character to the token if we're not collapsing
		// sections of white-space.
		if (white_space)
		{
			if (!collapse_white_space)
				token += ' ';
		}
		else
		{
			if (text_transformation == Style::TextTransform::Uppercase)
			{
				if (character >= 'a' && character <= 'z')
					character += ('A' - 'a');
			}
			else if (text_transformation == Style::TextTransform::Lowercase)
			{
				if (character >= 'A' && character <= 'Z')
					character -= ('A' - 'a');
			}

			token += character;
		}

		++token_begin;
		// A collapsed prefix space belongs before this non-whitespace character.
		const int mapped_before = !white_space && mapping ? output_offset + (int)token.size() - 1 : output_offset + output_before;
		MapConsumed(escape_begin, token_begin, mapped_before - output_offset, (int)token.size());
	}

	return false;
}

static bool LastToken(const char* token_begin, const char* string_end, bool collapse_white_space, bool break_at_endline, bool unicode_line_breaks)
{
	bool last_token = (token_begin == string_end);
	if (collapse_white_space && !last_token)
	{
		last_token = true;
		const char* character = token_begin;

		while (character != string_end)
		{
			if (!StringUtilities::IsWhitespace(*character) ||
				(break_at_endline && (*character == '\n' || (unicode_line_breaks && *character == '\r'))))
			{
				last_token = false;
				break;
			}

			character++;
		}
	}

	return last_token;
}

} // namespace Rml
