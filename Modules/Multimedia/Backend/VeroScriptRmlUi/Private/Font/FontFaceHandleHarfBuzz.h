// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

// Adapted from RmlUi 6.3, MIT license; see ThirdParty/RmlUi/LICENSE.txt.

#include "FontEngineDefault/FontTypes.h"
#include "FontFaceLayer.h"
#include "FontGlyph.h"
#include "LanguageData.h"
#include "VeroScriptFont.h"
#include "VeroScriptFontLayout.h"
#include <RmlUi/Core.h>
#include <unordered_map>
#include <list>

using Rml::byte;
using Rml::Character;
using Rml::Colourb;
using Rml::FontEffect;
using Rml::FontEffectList;
using Rml::FontFaceHandleFreetype;
using Rml::FontGlyph;
using Rml::FontMetrics;
using Rml::Pair;
using Rml::Queue;
using Rml::RenderManager;
using Rml::SharedPtr;
using Rml::SmallUnorderedMap;
using Rml::Span;
using Rml::String;
using Rml::StringView;
using Rml::TextShapingContext;
using Rml::TexturedMeshList;
using Rml::UniquePtr;
using Rml::UnorderedMap;
using Rml::Vector;
using Rml::Vector2f;
using Rml::Vector2i;

class FontFaceHandleHarfBuzz : public Rml::NonCopyMoveable
{
public:
	FontFaceHandleHarfBuzz();
	~FontFaceHandleHarfBuzz();

	bool Initialize(FontFaceHandleFreetype face, int font_size, bool load_default_glyphs);

	const FontMetrics &GetFontMetrics() const;

	const FontGlyphMap &GetGlyphs() const;

	/// Returns the width a string will take up if rendered with this handle.
	/// @param[in] string The string to measure.
	/// @param[in] text_shaping_context Extra parameters that provide context for text shaping.
	/// @param[in] registered_languages A list of languages registered in the font engine interface.
	/// @param[in] prior_character The optionally-specified character that immediately precedes the string. This may
	/// have an impact on the string width due to kerning.
	/// @return The width, in pixels, this string will occupy if rendered with this handle.
	int GetStringWidth(StringView string, const TextShapingContext &text_shaping_context,
					   const LanguageDataMap &registered_languages, Character prior_character = Character::Null);
	bool GetStringLayout(StringView string, const TextShapingContext& context, const LanguageDataMap& languages,
		Rml::TextLineLayout& result);

	/// Generates, if required, the layer configuration for a given list of font effects.
	/// @param[in] font_effects The list of font effects to generate the configuration for.
	/// @return The index to use when generating geometry using this configuration.
	int GenerateLayerConfiguration(const FontEffectList &font_effects);
	/// Generates the texture data for a layer (for the texture database).
	/// @param[out] texture_data The generated texture data.
	/// @param[out] texture_dimensions The dimensions of the texture.
	/// @param[in] font_effect The font effect used for the layer.
	/// @param[in] texture_id The index of the texture within the layer to generate.
	/// @param[in] handle_version The version of the handle data. Function returns false if out of date.
	bool GenerateLayerTexture(Vector<byte> &texture_data, Vector2i &texture_dimensions, const FontEffect *font_effect,
							  int texture_id, int handle_version) const;

	/// Generates the geometry required to render a single line of text.
	/// @param[in] render_manager The render manager responsible for rendering the string.
	/// @param[out] mesh_list A list to place the new meshes into.
	/// @param[in] string The string to render.
	/// @param[in] position The position of the baseline of the first character to render.
	/// @param[in] colour The colour to render the text.
	/// @param[in] opacity The opacity of the text, should be applied to font effects.
	/// @param[in] text_shaping_context Extra parameters that provide context for text shaping.
	/// @param[in] registered_languages A list of languages registered in the font engine interface.
	/// @param[in] layer_configuration Face configuration index to use for generating string.
	/// @return The width, in pixels, of the string geometry.
	int GenerateString(RenderManager &render_manager, TexturedMeshList &mesh_list, StringView string, Vector2f position,
					   ColourbPremultiplied colour, float opacity, const TextShapingContext &text_shaping_context,
					   const LanguageDataMap &registered_languages, int layer_configuration = 0);

	/// Version is changed whenever the layers are dirtied, requiring regeneration of string geometry.
	int GetVersion() const;

private:
	FontGlyphIndex GetOrAppendAtlasGlyph(const std::shared_ptr<VeroScript::Font::IFace> &face,
										 FontGlyphIndex source_index, Character character);
	const FontGlyph *GetGlyph(FontGlyphIndex glyph_index, Character &character) const;

	// Regenerate layers if dirty, such as after adding new glyphs.
	bool UpdateLayersOnDirty();

	// Create a new layer from the given font effect if it does not already exist.
	FontFaceLayer *GetOrCreateLayer(const SharedPtr<const FontEffect> &font_effect);

	// (Re-)generate a layer in this font face handle; throw if its atlas cannot be generated.
	void GenerateLayer(FontFaceLayer *layer);

	/// Shape text through the shared CPU font module.
	/// @param[in] string  The string currently being measured/rendered.
	/// @param[in] text_shaping_context  Extra parameters that provide context for text shaping.
	/// @param[in] registered_languages  A list of languages registered in the font engine interface.
	VeroScript::Font::FTextLine ShapeText(StringView text, const TextShapingContext &context,
											const LanguageDataMap &languages);
	struct FShapedCache
	{
		std::weak_ptr<const VeroScript::Font::FTextLayout> Layout;
		std::int64_t Offset = 0, Length = 0, LineOffset = 0, LineLength = 0;
		String Language, Script;
		bool bKerning = false;
		double LetterSpacing = 0;
		std::size_t FallbackCount = 0, Bytes = 0;
		VeroScript::Font::FTextLine Value;
	};
	std::list<FShapedCache> shaped_cache;
	std::size_t shaped_cache_bytes = 0;
	struct FWidthCache
	{
		String Text, Paragraph, Language, Script;
		Rml::Style::Direction Direction = Rml::Style::Direction::Auto;
		float LetterSpacing = 0;
		int Offset = 0, Length = -1, LineOffset = -1, LineLength = -1;
		bool bKerning = false;
		std::size_t FallbackCount = 0, Hash = 0, Bytes = 0;
		int Width = 0;
		std::weak_ptr<const String> Owner;
	};
	// Layout repeatedly measures identical runs while resolving flex constraints.
	// Cache only widths here, independently of the larger glyph/caret snapshots.
	std::list<FWidthCache> width_cache;
	std::unordered_multimap<std::size_t, std::list<FWidthCache>::iterator> width_index;
	std::size_t width_cache_bytes = 0;
	std::weak_ptr<const String> width_paragraph_owner;
	std::size_t width_paragraph_hash = 0;

	FontGlyphMap glyphs;
	using GlyphKey = std::pair<std::shared_ptr<VeroScript::Font::IFace>, FontGlyphIndex>;
	struct GlyphKeyHash
	{
		std::size_t operator()(const GlyphKey &key) const noexcept
		{
			const auto face = std::hash<const void *>{}(key.first.get());
			const auto glyph = std::hash<FontGlyphIndex>{}(key.second);
			return face ^ (glyph + 0x9e3779b9u + (face << 6) + (face >> 2));
		}
	};
	// Own selected faces and keep atlas IDs stable across shaping contexts and new fallback registrations.
	std::unordered_map<GlyphKey, FontGlyphIndex, GlyphKeyHash> atlas_indices;
	std::uint64_t next_atlas_index = 1;

	struct EffectLayerPair
	{
		const FontEffect *font_effect;
		UniquePtr<FontFaceLayer> layer;
	};
	using FontLayerMap = Vector<EffectLayerPair>;
	using FontLayerCache = SmallUnorderedMap<size_t, FontFaceLayer *>;
	using LayerConfiguration = Vector<FontFaceLayer *>;
	using LayerConfigurationList = Vector<LayerConfiguration>;

	// The list of all font layers, index by the effect that instanced them.
	FontFaceLayer *base_layer;
	FontLayerMap layers;
	// Each font layer that generated geometry or textures, indexed by the font-effect's fingerprint key.
	FontLayerCache layer_cache;

	bool is_layers_dirty = false;
	int version = 0;

	// All configurations currently in use on this handle. New configurations will be generated as required.
	LayerConfigurationList layer_configurations;

	FontMetrics metrics;

	FontFaceHandleFreetype ft_face;
};
