// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Adapted from RmlUi 6.3, MIT license; see ThirdParty/RmlUi/LICENSE.txt.

#include "FontProvider.h"
#include "FontEngineDefault/../ComputeProperty.h"
#include "FontFace.h"
#include "FontFaceHandleHarfBuzz.h"
#include "FontFamily.h"
#include "VeroScriptRmlUiFontBridge.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>

static FontProvider *g_font_provider = nullptr;

bool FontProvider::Initialise()
{
	RMLUI_ASSERT(!g_font_provider);
	if (!Rml::FreeType::Initialise())
	{
		return false;
	}
	g_font_provider = new FontProvider;
	return true;
}

void FontProvider::Shutdown()
{
	RMLUI_ASSERT(g_font_provider);
	delete g_font_provider;
	g_font_provider = nullptr;
	Rml::FreeType::Shutdown();
}

FontFaceHandleHarfBuzz *FontProvider::GetFontFaceHandle(const String &family, Style::FontStyle style,
    Style::FontWeight weight, int size)
{
	RMLUI_ASSERTMSG(family == Rml::StringUtilities::ToLower(family),
	    "Font family name must be converted to lowercase before entering here.");

	FontFamilyMap &families = Get().font_families;

	auto it = families.find(family);
	if (it == families.end())
	{
		return nullptr;
	}

	return it->second->GetFaceHandle(style, weight, size);
}

bool FontProvider::LoadFontFace(const String &file_name, int face_index, bool fallback_face, Style::FontWeight weight)
{
	Rml::FileInterface *file_interface = Rml::GetFileInterface();
	Rml::FileHandle handle = file_interface->Open(file_name);

	if (!handle)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load font face from %s, could not open file.",
		    file_name.c_str());
		return false;
	}

	size_t length = file_interface->Length(handle);

	auto buffer_ptr = UniquePtr<byte[]>(new byte[length]);
	byte *buffer = buffer_ptr.get();
	file_interface->Read(buffer, length, handle);
	file_interface->Close(handle);

	bool result = Get().LoadFontFace({buffer, length}, face_index, fallback_face, std::move(buffer_ptr), file_name, {},
	    Style::FontStyle::Normal, weight);

	return result;
}

bool FontProvider::LoadFontFace(Span<const byte> data, int face_index, const String &font_family,
    Style::FontStyle style, Style::FontWeight weight, bool fallback_face)
{
	const String source = "memory";

	bool result = Get().LoadFontFace(data, face_index, fallback_face, nullptr, source, font_family, style, weight);

	return result;
}

std::vector<std::shared_ptr<VeroScript::Font::IFace>> FontProvider::GetFallbackFaces()
{
	std::vector<std::shared_ptr<VeroScript::Font::IFace>> result;
	const auto &faces = Get().fallback_font_faces;
	result.reserve(faces.size());
	for (const auto *face : faces)
	{
		result.push_back(face->GetSharedFace());
	}
	return result;
}

std::size_t FontProvider::GetFallbackFaceCount()
{
	return Get().fallback_font_faces.size();
}

std::shared_ptr<const VeroScript::Font::FTextLayout> FontProvider::GetTextLayout(
    Rml::StringView text, const std::shared_ptr<const String> &owner, VeroScript::Font::EDirection direction)
{
	const auto service = VeroScript::RmlUi::GetTextLayoutService();
	if (owner && !text.empty() && (text.begin() != owner->data() || text.size() != owner->size()))
	{
		throw std::runtime_error("rmlui.font_invalid_paragraph_context");
	}
	if (owner)
	{
		text = *owner;
	}
	// Paragraphs are immutable values. Formatting can create a new owner for the
	// same text, and unowned measurement spans need the same analysis reuse.
	auto &provider = Get();
	if (owner && provider.recent_paragraph_owner.lock() == owner &&
	    provider.recent_paragraph_direction == direction)
	{
		if (auto layout = provider.recent_paragraph_layout; layout && layout->GetService() == service)
		{
			return layout;
		}
	}
	const auto remember = [&](const auto &layout)
	{
		provider.recent_paragraph_owner = owner;
		provider.recent_paragraph_layout = layout;
		provider.recent_paragraph_direction = direction;
	};
	auto &entries = provider.paragraph_layouts;
	const auto hash = std::hash<std::string_view>{}(std::string_view(text.begin(), text.size())) ^
	    (std::hash<const void *>{}(service.get()) << 1) ^ static_cast<std::size_t>(direction);
	const auto range = provider.paragraph_index.equal_range(hash);
	for (auto it = range.first; it != range.second; ++it)
	{
		const auto entry = it->second;
		const auto &source = entry->layout->GetText();
		if (entry->direction == direction && entry->layout->GetService() == service &&
		    source.size() == text.size() && std::equal(text.begin(), text.end(), source.begin()))
		{
			entries.splice(entries.end(), entries, entry);
			remember(entry->layout);
			return entry->layout;
		}
	}
	std::string error;
	auto layout = VeroScript::Font::FTextLayout::Create(service, String(text.begin(), text.end()), direction, error);
	if (!layout)
	{
		throw std::runtime_error("rmlui.font_layout_failed: " + error);
	}
	constexpr std::size_t budget = 8 * 1024 * 1024;
	if (text.size() <= budget)
	{
		while (!entries.empty() && (entries.size() >= 2048 || text.size() > budget - provider.paragraph_bytes))
		{
			const auto oldest = entries.begin();
			const auto items = provider.paragraph_index.equal_range(oldest->hash);
			for (auto it = items.first; it != items.second; ++it)
			{
				if (it->second == oldest)
				{
					provider.paragraph_index.erase(it);
					break;
				}
			}
			provider.paragraph_bytes -= oldest->layout->GetText().size();
			entries.erase(oldest);
		}
		entries.push_back({direction, layout, hash});
		try
		{
			provider.paragraph_index.emplace(hash, std::prev(entries.end()));
		}
		catch (...)
		{
			entries.pop_back();
			throw;
		}
		provider.paragraph_bytes += text.size();
	}
	remember(layout);
	return layout;
}

void FontProvider::ReleaseFontResources()
{
	RMLUI_ASSERT(g_font_provider);
	g_font_provider->paragraph_layouts.clear();
	g_font_provider->paragraph_index.clear();
	g_font_provider->paragraph_bytes = 0;
	g_font_provider->recent_paragraph_owner.reset();
	g_font_provider->recent_paragraph_layout.reset();
	for (auto &name_family : g_font_provider->font_families)
	{
		name_family.second->ReleaseFontResources();
	}
}

FontProvider::FontProvider()
{
	RMLUI_ASSERT(!g_font_provider);
}

FontProvider::~FontProvider()
{
	RMLUI_ASSERT(g_font_provider == this);
}

FontProvider &FontProvider::Get()
{
	RMLUI_ASSERT(g_font_provider);
	return *g_font_provider;
}

bool FontProvider::LoadFontFace(Span<const byte> data, int face_index, bool fallback_face,
    UniquePtr<byte[]> face_memory, const String &source, String font_family,
    Style::FontStyle style, Style::FontWeight weight)
{
	using Style::FontWeight;

	Vector<Rml::FaceVariation> face_variations;
	if (!Rml::FreeType::GetFaceVariations(data, face_variations, face_index))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR,
		    "Failed to load font face from '%s': Invalid or unsupported font face file format.",
		    source.c_str());
		return false;
	}

	Vector<Rml::FaceVariation> load_variations;
	if (face_variations.empty())
	{
		load_variations.push_back(Rml::FaceVariation{Style::FontWeight::Auto, 0, 0});
	}
	else
	{
		// Iterate through all the face variations and pick the ones to load. The list is already sorted by (weight,
		// width). When weight is set to 'auto' we load all the weights of the face. However, we only want to load one
		// width for each weight.
		for (auto it = face_variations.begin(); it != face_variations.end();)
		{
			if (weight != FontWeight::Auto && it->weight != weight)
			{
				++it;
				continue;
			}

			// We don't currently have any way for users to select widths, so we search for a regular (medium) value
			// here.
			constexpr int search_width = 100;
			const FontWeight current_weight = it->weight;

			int best_width_distance = Rml::Math::Absolute((int)it->width - search_width);
			auto it_best_width = it;

			// Search forward to find the best 'width' with the same weight.
			for (++it; it != face_variations.end(); ++it)
			{
				if (it->weight != current_weight)
				{
					break;
				}

				const int width_distance = Rml::Math::Absolute((int)it->width - search_width);
				if (width_distance < best_width_distance)
				{
					best_width_distance = width_distance;
					it_best_width = it;
				}
			}

			load_variations.push_back(*it_best_width);
		}
	}

	if (load_variations.empty())
	{
		Rml::Log::Message(Rml::Log::LT_ERROR,
		    "Failed to load font face from '%s': Could not locate face with weight %d.", source.c_str(),
		    (int)weight);
		return false;
	}

	for (const Rml::FaceVariation &variation : load_variations)
	{
		FontFaceHandleFreetype ft_face =
		    Rml::FreeType::LoadFace(data, source, face_index, variation.named_instance_index);
		if (!ft_face)
		{
			return false;
		}

		if (font_family.empty())
		{
			Rml::FreeType::GetFaceStyle(ft_face, &font_family, &style, nullptr);
		}
		if (weight == FontWeight::Auto)
		{
			Rml::FreeType::GetFaceStyle(ft_face, nullptr, nullptr, &weight);
		}

		const FontWeight variation_weight = (variation.weight == FontWeight::Auto ? weight : variation.weight);
		const String font_face_description = Rml::GetFontFaceDescription(font_family, style, variation_weight);

		if (!AddFace(ft_face, font_family, style, variation_weight, fallback_face, std::move(face_memory)))
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load font face %s from '%s'.",
			    font_face_description.c_str(), source.c_str());
			return false;
		}

		Rml::Log::Message(Rml::Log::LT_INFO, "Loaded font face %s from '%s'.", font_face_description.c_str(),
		    source.c_str());
	}

	return true;
}

bool FontProvider::AddFace(FontFaceHandleFreetype face, const String &family, Style::FontStyle style,
    Style::FontWeight weight, bool fallback_face, UniquePtr<byte[]> face_memory)
{
	if (family.empty() || weight == Style::FontWeight::Auto)
	{
		return false;
	}

	String family_lower = Rml::StringUtilities::ToLower(family);
	FontFamily *font_family = nullptr;
	auto it = font_families.find(family_lower);
	if (it != font_families.end())
	{
		font_family = (FontFamily *)it->second.get();
	}
	else
	{
		auto font_family_ptr = Rml::MakeUnique<FontFamily>(family_lower);
		font_family = font_family_ptr.get();
		font_families[family_lower] = std::move(font_family_ptr);
	}

	FontFace *font_face_result = font_family->AddFace(face, style, weight, std::move(face_memory));

	if (font_face_result && fallback_face)
	{
		auto it_fallback_face = std::find(fallback_font_faces.begin(), fallback_font_faces.end(), font_face_result);
		if (it_fallback_face == fallback_font_faces.end())
		{
			fallback_font_faces.push_back(font_face_result);
		}
	}

	return static_cast<bool>(font_face_result);
}
