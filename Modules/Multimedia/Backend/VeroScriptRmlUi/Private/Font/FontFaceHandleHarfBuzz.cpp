// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Adapted from RmlUi 6.3, MIT license; see ThirdParty/RmlUi/LICENSE.txt.

#include "FontFaceHandleHarfBuzz.h"
#include "FontEngineDefault/FreeTypeInterface.h"
#include "FontFaceLayer.h"
#include "FontProvider.h"
#include "FreeTypeInterface.h"
#include "VeroScriptRmlUiFontBridge.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <unordered_set>

static bool IsKerningEnabled(const TextShapingContext &text_shaping_context, int font_size)
{
	static constexpr int minimum_font_size_to_enable_kerning = 14;

	switch (text_shaping_context.font_kerning)
	{
	case Style::FontKerning::Normal:
		return true;
	case Style::FontKerning::None:
		return false;
	default:
		return font_size >= minimum_font_size_to_enable_kerning;
	}
}

FontFaceHandleHarfBuzz::FontFaceHandleHarfBuzz()
{
	base_layer = nullptr;
	metrics = {};
	ft_face = 0;
}

FontFaceHandleHarfBuzz::~FontFaceHandleHarfBuzz() = default;

bool FontFaceHandleHarfBuzz::Initialize(FontFaceHandleFreetype face, int font_size, bool load_default_glyphs)
{
	ft_face = face;
	RMLUI_ASSERTMSG(layer_configurations.empty(), "Initialize must only be called once.");
	if (!FreeType::InitialiseFaceHandle(ft_face, font_size, glyphs, metrics))
	{
		return false;
	}
	if (load_default_glyphs)
	{
		const auto primary = VeroScript::RmlUi::GetSharedFontFace(ft_face);
		for (char32_t c = 32; c <= 126; ++c)
		{
			const auto index = primary->GetGlyphIndex(c);
			if (index)
			{
				GetOrAppendAtlasGlyph(primary, index, static_cast<Character>(c));
			}
		}
	}
	base_layer = GetOrCreateLayer(nullptr);
	layer_configurations.push_back(LayerConfiguration{base_layer});
	return true;
}

const FontMetrics &FontFaceHandleHarfBuzz::GetFontMetrics() const
{
	return metrics;
}

const FontGlyphMap &FontFaceHandleHarfBuzz::GetGlyphs() const
{
	return glyphs;
}

int FontFaceHandleHarfBuzz::GetStringWidth(StringView string, const TextShapingContext &context,
    const LanguageDataMap &languages, Character /*prior_character*/)
{
	const auto &owner = context.paragraph_owner;
	if (owner && !context.paragraph_text.empty() &&
	    (context.paragraph_text.begin() != owner->data() || context.paragraph_text.size() != owner->size()))
	{
		throw std::runtime_error("rmlui.font_invalid_paragraph_context");
	}
	// Inline/flex formatting can replace the paragraph owner while measuring the
	// same immutable contents. Widths depend on contents and shaping context.
	const auto paragraph = owner ? StringView(*owner) : context.paragraph_text;
	const auto language = languages.find(context.language);
	const String script = language == languages.end() ? String() : language->second.script_code;
	const auto fallback_count = FontProvider::GetFallbackFaceCount();
	const bool kerning = IsKerningEnabled(context, metrics.size);
	// Word-by-word measurement of one immutable paragraph must not hash its
	// complete contents again for every candidate line break.
	std::size_t paragraph_hash = width_paragraph_hash;
	if (!owner || width_paragraph_owner.lock() != owner)
	{
		paragraph_hash = std::hash<std::string_view>{}(std::string_view(paragraph.begin(), paragraph.size()));
		width_paragraph_owner = owner;
		width_paragraph_hash = paragraph_hash;
	}
	std::size_t hash = 0;
	const auto combine = [&](const auto &value)
	{
		using T = std::decay_t<decltype(value)>;
		hash ^= std::hash<T>{}(value) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
	};
	combine(std::string_view(string.begin(), string.size()));
	combine(paragraph_hash);
	combine(context.language);
	combine(script);
	combine(static_cast<int>(context.text_direction));
	combine(context.letter_spacing);
	combine(context.paragraph_offset);
	combine(context.paragraph_length);
	combine(context.line_offset);
	combine(context.line_length);
	combine(kerning);
	combine(fallback_count);
	const auto range = width_index.equal_range(hash);
	for (auto it = range.first; it != range.second; ++it)
	{
		const auto entry = it->second;
		if (entry->Text.size() == string.size() && std::equal(string.begin(), string.end(), entry->Text.begin()) &&
		    ((owner && entry->Owner.lock() == owner) ||
		        (entry->Paragraph.size() == paragraph.size() &&
		            std::equal(paragraph.begin(), paragraph.end(), entry->Paragraph.begin()))) &&
		    entry->Language == context.language && entry->Script == script &&
		    entry->Direction == context.text_direction && entry->LetterSpacing == context.letter_spacing &&
		    entry->Offset == context.paragraph_offset && entry->Length == context.paragraph_length &&
		    entry->LineOffset == context.line_offset && entry->LineLength == context.line_length &&
		    entry->bKerning == kerning && entry->FallbackCount == fallback_count)
		{
			width_cache.splice(width_cache.end(), width_cache, entry);
			entry->Owner = owner;
			return entry->Width;
		}
	}
	const auto shaped = ShapeText(string, context, languages);
	const int width = static_cast<int>(std::clamp(std::round(shaped.Width), 0.0,
	    static_cast<double>(std::numeric_limits<int>::max())));
	const auto bytes = sizeof(FWidthCache) + string.size() + paragraph.size() + context.language.size() + script.size();
	constexpr std::size_t budget = 512 * 1024;
	if (bytes <= budget)
	{
		while (!width_cache.empty() && (width_cache.size() >= 1024 || bytes > budget - width_cache_bytes))
		{
			const auto oldest = width_cache.begin();
			const auto entries = width_index.equal_range(oldest->Hash);
			for (auto it = entries.first; it != entries.second; ++it)
			{
				if (it->second == oldest)
				{
					width_index.erase(it);
					break;
				}
			}
			width_cache_bytes -= oldest->Bytes;
			width_cache.erase(oldest);
		}
		width_cache.push_back({String(string.begin(), string.end()), String(paragraph.begin(), paragraph.end()),
		    context.language, script, context.text_direction, context.letter_spacing,
		    context.paragraph_offset, context.paragraph_length, context.line_offset, context.line_length,
		    kerning, fallback_count, hash, bytes, width, owner});
		try
		{
			width_index.emplace(hash, std::prev(width_cache.end()));
		}
		catch (...)
		{
			width_cache.pop_back();
			throw;
		}
		width_cache_bytes += bytes;
	}
	return width;
}

bool FontFaceHandleHarfBuzz::GetStringLayout(StringView string, const TextShapingContext &context,
    const LanguageDataMap &languages, Rml::TextLineLayout &result)
{
	const auto shaped = ShapeText(string, context, languages);
	const int offset = context.paragraph_owner || !context.paragraph_text.empty() ? context.paragraph_offset : 0;
	Rml::TextLineLayout candidate;
	candidate.width = static_cast<float>(shaped.Width);
	candidate.base_direction = shaped.Text.Direction == VeroScript::Font::EDirection::RightToLeft
	    ? Rml::Style::Direction::Rtl
	    : Rml::Style::Direction::Ltr;
	candidate.carets.reserve(shaped.Carets.size());
	for (const auto &caret : shaped.Carets)
	{
		candidate.carets.push_back({static_cast<int>(caret.ByteOffset - offset), static_cast<float>(caret.X),
		    caret.bTrailing ? Rml::TextAffinity::Trailing : Rml::TextAffinity::Leading});
	}
	candidate.spans.reserve(shaped.Clusters.size());
	for (const auto &cluster : shaped.Clusters)
	{
		candidate.spans.push_back({static_cast<int>(cluster.ByteOffset - offset),
		    static_cast<int>(cluster.ByteOffset + cluster.ByteLength - offset),
		    static_cast<float>(cluster.Left), static_cast<float>(cluster.Right)});
	}
	result = std::move(candidate);
	return true;
}

int FontFaceHandleHarfBuzz::GenerateLayerConfiguration(const FontEffectList &font_effects)
{
	if (font_effects.empty())
	{
		return 0;
	}

	// Check each existing configuration for a match with this arrangement of effects.
	int configuration_index = 1;
	for (; configuration_index < (int)layer_configurations.size(); ++configuration_index)
	{
		const LayerConfiguration &configuration = layer_configurations[configuration_index];

		// Check the size is correct. For a match, there should be one layer in the configuration
		// plus an extra for the base layer.
		if (configuration.size() != font_effects.size() + 1)
		{
			continue;
		}

		// Check through each layer, checking it was created by the same effect as the one we're
		// checking.
		size_t effect_index = 0;
		for (size_t i = 0; i < configuration.size(); ++i)
		{
			// Skip the base layer ...
			if (configuration[i]->GetFontEffect() == nullptr)
			{
				continue;
			}

			// If the ith layer's effect doesn't match the equivalent effect, then this
			// configuration can't match.
			if (configuration[i]->GetFontEffect() != font_effects[effect_index].get())
			{
				break;
			}

			// Check the next one ...
			++effect_index;
		}

		if (effect_index == font_effects.size())
		{
			return configuration_index;
		}
	}

	// No match, so we have to generate a new layer configuration.
	LayerConfiguration layer_configuration;

	bool added_base_layer = false;

	for (size_t i = 0; i < font_effects.size(); ++i)
	{
		if (!added_base_layer && font_effects[i]->GetLayer() == FontEffect::Layer::Front)
		{
			layer_configuration.push_back(base_layer);
			added_base_layer = true;
		}

		FontFaceLayer *new_layer = GetOrCreateLayer(font_effects[i]);
		layer_configuration.push_back(new_layer);
	}

	// Add the base layer now if we still haven't added it.
	if (!added_base_layer)
	{
		layer_configuration.push_back(base_layer);
	}

	layer_configurations.push_back(std::move(layer_configuration));
	return (int)(layer_configurations.size() - 1);
}

bool FontFaceHandleHarfBuzz::GenerateLayerTexture(Vector<byte> &texture_data, Vector2i &texture_dimensions,
    const FontEffect *font_effect, int texture_id,
    int handle_version) const
{
	if (handle_version != version)
	{
		RMLUI_ERRORMSG("While generating font layer texture: Handle version mismatch in texture vs font-face.");
		return false;
	}

	auto it = std::find_if(layers.begin(), layers.end(),
	    [font_effect](const EffectLayerPair &pair)
	{
		return pair.font_effect == font_effect;
	});

	if (it == layers.end())
	{
		RMLUI_ERRORMSG("While generating font layer texture: Layer id not found.");
		return false;
	}

	return it->layer->GenerateTexture(texture_data, texture_dimensions, texture_id, glyphs);
}

int FontFaceHandleHarfBuzz::GenerateString(RenderManager &render_manager, TexturedMeshList &mesh_list,
    StringView string, const Vector2f position,
    const ColourbPremultiplied colour, const float opacity,
    const TextShapingContext &context, const LanguageDataMap &languages,
    const int layer_configuration_index)
{
	RMLUI_ASSERT(layer_configuration_index >= 0);
	RMLUI_ASSERT(layer_configuration_index < (int)layer_configurations.size());
	// Populate every selected face/glyph before regenerating atlas textures.
	const auto shaped = ShapeText(string, context, languages);
	UpdateLayersOnDirty();
	std::unordered_map<std::uint32_t, std::size_t> cluster_ends;
	if (context.letter_spacing != 0)
	{
		cluster_ends.reserve(shaped.Text.Glyphs.size());
		for (std::size_t i = 0; i < shaped.Text.Glyphs.size(); ++i)
		{
			cluster_ends[shaped.Text.Glyphs[i].Glyph.Cluster] = i;
		}
	}
	std::unordered_set<std::uint32_t> selected_clusters;
	if (context.selection_begin >= 0 && context.selection_end > context.selection_begin)
	{
		const auto offset = context.paragraph_owner || !context.paragraph_text.empty() ? context.paragraph_offset : 0;
		const auto begin = static_cast<std::int64_t>(offset) + context.selection_begin;
		const auto end = static_cast<std::int64_t>(offset) + context.selection_end;
		for (const auto &cluster : shaped.Clusters)
		{
			if (cluster.ByteOffset < end && cluster.ByteOffset + cluster.ByteLength > begin)
			{
				selected_clusters.insert(static_cast<std::uint32_t>(cluster.ByteOffset));
			}
		}
	}
	const auto &configuration = layer_configurations[layer_configuration_index];
	const int num_geometries =
	    std::accumulate(configuration.begin(), configuration.end(), 0,
	        [](int sum, const FontFaceLayer *layer)
	{
		return sum + layer->GetNumTextures();
	});
	mesh_list.resize(num_geometries);
	int geometry_index = 0;
	double line_width = 0;
	for (auto *layer : configuration)
	{
		const int num_textures = layer->GetNumTextures();
		if (!num_textures)
		{
			continue;
		}
		const auto layer_colour = layer == base_layer ? colour : layer->GetColour(opacity);
		for (int i = 0; i < num_textures; ++i)
		{
			mesh_list[geometry_index + i].texture = layer->GetTexture(render_manager, i);
		}
		line_width = 0;
		double line_y = 0;
		for (std::size_t i = 0; i < shaped.Text.Glyphs.size(); ++i)
		{
			const auto &value = shaped.Text.Glyphs[i].Glyph;
			Character character = Character::Null;
			const auto *glyph = GetGlyph(value.Glyph, character);
			if (!glyph)
			{
				continue;
			}
			auto glyph_colour = layer_colour;
			if (layer == base_layer && selected_clusters.count(value.Cluster))
			{
				glyph_colour = context.selection_colour;
			}
			if (layer == base_layer && glyph->color_format == ColorFormat::RGBA8)
			{
				glyph_colour = ColourbPremultiplied(glyph_colour.alpha, glyph_colour.alpha);
			}
			// Font shaping uses an upward Y axis; GUI mesh coordinates point down.
			const Vector2f origin{position.x + static_cast<float>(line_width + value.XOffset / 64.0),
			    position.y + static_cast<float>(line_y - value.YOffset / 64.0)};
			layer->GenerateGeometry(&mesh_list[geometry_index], value.Glyph, character, origin, glyph_colour);
			// Match Font::FTextLayout: spacing follows the complete cluster, keeping attached marks in place.
			const auto spacing = context.letter_spacing != 0 && cluster_ends.at(value.Cluster) == i
			    ? context.letter_spacing
			    : 0.f;
			line_width += (value.Glyph ? value.XAdvance / 64.0 : glyph->advance) + spacing;
			line_y -= value.YAdvance / 64.0;
		}
		geometry_index += num_textures;
	}
	return static_cast<int>(std::clamp(std::round(shaped.Width), 0.0,
	    static_cast<double>(std::numeric_limits<int>::max())));
}

int FontFaceHandleHarfBuzz::GetVersion() const
{
	return version;
}

FontGlyphIndex FontFaceHandleHarfBuzz::GetOrAppendAtlasGlyph(
    const std::shared_ptr<VeroScript::Font::IFace> &face, FontGlyphIndex source_index, Character character)
{
	if (!source_index)
	{
		return 0;
	}
	const GlyphKey key{face, source_index};
	const auto existing = atlas_indices.find(key);
	if (existing != atlas_indices.end())
	{
		return existing->second;
	}
	if (next_atlas_index > std::numeric_limits<FontGlyphIndex>::max())
	{
		throw std::runtime_error("rmlui.font_glyph_limit");
	}
	const auto index = static_cast<FontGlyphIndex>(next_atlas_index);
	const auto entry = atlas_indices.emplace(key, index).first;
	try
	{
		if (!FreeType::AppendGlyph(*face, metrics.size, source_index, index, character, glyphs))
		{
			throw std::runtime_error("rmlui.font_rasterize_failed");
		}
	}
	catch (...)
	{
		atlas_indices.erase(entry);
		throw;
	}
	++next_atlas_index;
	is_layers_dirty = base_layer != nullptr;
	return index;
}

const FontGlyph *FontFaceHandleHarfBuzz::GetGlyph(FontGlyphIndex index, Character &character) const
{
	const auto found = glyphs.find(index);
	if (found == glyphs.end())
	{
		return nullptr;
	}
	character = index ? found->second.character : Character::Replacement;
	return &found->second.bitmap;
}

bool FontFaceHandleHarfBuzz::UpdateLayersOnDirty()
{
	bool result = false;

	// If we are dirty, regenerate all the layers and increment the version
	if (is_layers_dirty && base_layer)
	{
		++version;

		// Regenerate all the layers.
		// Note: The layer regeneration needs to happen in the order in which the layers were created,
		// otherwise we may end up cloning a layer which has not yet been regenerated. This means trouble!
		for (auto &pair : layers)
		{
			GenerateLayer(pair.layer.get());
		}

		// Failed regeneration must remain dirty so the next request retries instead of losing glyphs.
		is_layers_dirty = false;
		result = true;
	}

	return result;
}

FontFaceLayer *FontFaceHandleHarfBuzz::GetOrCreateLayer(const SharedPtr<const FontEffect> &font_effect)
{
	// Search for the font effect layer first, it may have been instanced before as part of a different configuration.
	const FontEffect *font_effect_ptr = font_effect.get();
	auto it = std::find_if(layers.begin(), layers.end(), [font_effect_ptr](const EffectLayerPair &pair)
	{
		return pair.font_effect == font_effect_ptr;
	});

	if (it != layers.end())
	{
		return it->layer.get();
	}

	// No existing effect matches, generate a new layer for the effect.
	auto layer = Rml::MakeUnique<FontFaceLayer>(font_effect);
	auto *result = layer.get();
	layers.push_back(EffectLayerPair{font_effect_ptr, std::move(layer)});
	try
	{
		GenerateLayer(result);
	}
	catch (...)
	{
		if (font_effect)
		{
			const auto fingerprint = font_effect->GetFingerprint();
			const auto cached = layer_cache.find(fingerprint);
			if (cached != layer_cache.end() && cached->second == result)
			{
				layer_cache.erase(fingerprint);
			}
		}
		layers.pop_back();
		throw;
	}
	return result;
}

void FontFaceHandleHarfBuzz::GenerateLayer(FontFaceLayer *layer)
{
	RMLUI_ASSERT(layer);
	const FontEffect *font_effect = layer->GetFontEffect();
	bool result = false;

	if (!font_effect)
	{
		result = layer->Generate(this);
	}
	else
	{
		// Determine which, if any, layer the new layer should copy its geometry and textures from.
		FontFaceLayer *clone = nullptr;
		bool clone_glyph_origins = true;
		String generation_key;
		size_t fingerprint = font_effect->GetFingerprint();

		if (!font_effect->HasUniqueTexture())
		{
			clone = base_layer;
			clone_glyph_origins = false;
		}
		else
		{
			auto cache_iterator = layer_cache.find(fingerprint);
			if (cache_iterator != layer_cache.end() && cache_iterator->second != layer)
			{
				clone = cache_iterator->second;
			}
		}

		// Create a new layer.
		result = layer->Generate(this, clone, clone_glyph_origins);

		// Cache the layer in the layer cache if it generated its own textures (ie, didn't clone).
		if (result && !clone)
		{
			layer_cache[fingerprint] = layer;
		}
	}

	if (!result)
	{
		throw std::runtime_error("rmlui.font_atlas_failed");
	}
}

VeroScript::Font::FTextLine FontFaceHandleHarfBuzz::ShapeText(StringView text, const TextShapingContext &context,
    const LanguageDataMap &languages)
{
	using namespace VeroScript;
	if (!std::isfinite(context.letter_spacing))
	{
		throw std::runtime_error("rmlui.font_invalid_letter_spacing");
	}
	if ((context.line_offset < 0) != (context.line_length < 0))
	{
		throw std::runtime_error("rmlui.font_invalid_paragraph_context");
	}
	Font::FShapeOptions options;
	options.PixelSize = metrics.size;
	options.Language = context.language;
	options.bKerning = IsKerningEnabled(context, metrics.size);
	options.Direction = context.text_direction == Rml::Style::Direction::Rtl ? Font::EDirection::RightToLeft
	    : context.text_direction == Rml::Style::Direction::Ltr               ? Font::EDirection::LeftToRight
	                                                                         : Font::EDirection::Auto;
	const auto language = languages.find(context.language);
	if (language != languages.end())
	{
		options.Script = language->second.script_code;
	}
	const bool contextual = context.paragraph_owner || !context.paragraph_text.empty();
	const auto paragraph = contextual ? context.paragraph_text : text;
	const auto layout = FontProvider::GetTextLayout(paragraph, context.paragraph_owner, options.Direction);
	const auto &source = layout->GetText();
	const std::int64_t offset = contextual ? context.paragraph_offset : 0;
	const std::int64_t length = context.paragraph_length >= 0 ? context.paragraph_length : text.size();
	if (offset < 0 || length != static_cast<std::int64_t>(text.size()) ||
	    static_cast<std::uint64_t>(offset) > source.size() || static_cast<std::uint64_t>(length) > source.size() - offset ||
	    !std::equal(text.begin(), text.end(), source.begin() + offset))
	{
		throw std::runtime_error("rmlui.font_invalid_paragraph_context");
	}
	const auto line_offset = context.line_offset >= 0 ? context.line_offset : offset;
	const auto line_length = context.line_length >= 0 ? context.line_length : length;
	const auto primary = VeroScript::RmlUi::GetSharedFontFace(ft_face);
	const auto fallbacks = FontProvider::GetFallbackFaces();
	for (auto it = shaped_cache.begin(); it != shaped_cache.end();)
	{
		const auto owner = it->Layout.lock();
		if (!owner)
		{
			shaped_cache_bytes -= it->Bytes;
			it = shaped_cache.erase(it);
			continue;
		}
		if (owner == layout && it->Offset == offset && it->Length == length &&
		    it->LineOffset == line_offset && it->LineLength == line_length &&
		    it->Language == options.Language && it->Script == options.Script &&
		    it->bKerning == options.bKerning && it->LetterSpacing == context.letter_spacing &&
		    it->FallbackCount == fallbacks.size())
		{
			shaped_cache.splice(shaped_cache.end(), shaped_cache, it);
			return shaped_cache.back().Value;
		}
		++it;
	}
	Font::FTextLine result;
	std::string error;
	if (!layout->ShapeLine(primary, fallbacks, options, line_offset, line_length, context.letter_spacing,
	        glyphs.at(0).bitmap.advance, result, error, offset, length))
	{
		throw std::runtime_error("rmlui.font_shape_failed: " + error);
	}
	for (auto &item : result.Text.Glyphs)
	{
		const auto character = Rml::StringUtilities::ToCharacter(source.data() + item.Glyph.Cluster, source.data() + source.size());
		item.Glyph.Glyph = GetOrAppendAtlasGlyph(item.Face, item.Glyph.Glyph, character);
	}
	// Each sized face owns its atlas IDs. Entries borrow paragraph identity and are
	// invalidated by paragraph/service changes or by additional fallback registrations.
	const auto bytes = sizeof(FShapedCache) + options.Language.size() + options.Script.size() +
	    result.Text.Glyphs.size() * sizeof(decltype(result.Text.Glyphs)::value_type) +
	    result.Clusters.size() * sizeof(decltype(result.Clusters)::value_type) +
	    result.Carets.size() * sizeof(decltype(result.Carets)::value_type);
	constexpr std::size_t CacheBudget = 2 * 1024 * 1024;
	if (bytes <= CacheBudget)
	{
		while (!shaped_cache.empty() && (shaped_cache.size() >= 256 || shaped_cache_bytes + bytes > CacheBudget))
		{
			shaped_cache_bytes -= shaped_cache.front().Bytes;
			shaped_cache.pop_front();
		}
		shaped_cache.push_back({layout, offset, length, line_offset, line_length, options.Language, options.Script,
		    options.bKerning, context.letter_spacing, fallbacks.size(), bytes, result});
		shaped_cache_bytes += bytes;
	}
	return result;
}
