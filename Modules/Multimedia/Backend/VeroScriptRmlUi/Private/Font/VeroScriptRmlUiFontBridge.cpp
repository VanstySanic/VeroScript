// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiFontBridge.h"
#include <RmlUi/Core/Log.h>

namespace VeroScript::RmlUi
{
	namespace
	{
		std::shared_ptr<Font::IService> FontService;
		std::shared_ptr<Font::ITextLayoutService> TextLayoutService;
	}
	bool UseFontServices(std::shared_ptr<Font::IService> Fonts,
		std::shared_ptr<Font::ITextLayoutService> TextLayouts, std::string &Error)
	{
		if (!Fonts || !Fonts->GetBackendId() || !*Fonts->GetBackendId())
		{
			Error = "rmlui.font_service_required";
			return false;
		}
		if (!TextLayouts || !TextLayouts->GetBackendId() || !*TextLayouts->GetBackendId())
		{
			Error = "rmlui.text_layout_service_required";
			return false;
		}
		// RmlUi owns one global font registry/atlas. Backend names do not identify immutable configurations.
		if (FontService && FontService != Fonts)
		{
			Error = "rmlui.font_backend_in_use";
			return false;
		}
		if (TextLayoutService && TextLayoutService != TextLayouts)
		{
			Error = "rmlui.text_layout_backend_in_use";
			return false;
		}
		if (!FontService)
		{
			FontService = std::move(Fonts);
			TextLayoutService = std::move(TextLayouts);
		}
		return true;
	}
	const std::shared_ptr<Font::ITextLayoutService> &GetTextLayoutService()
	{
		return TextLayoutService;
	}
	void ReleaseFontServices()
	{
		FontService.reset();
		TextLayoutService.reset();
	}

	Font::IFace &GetFontFace(Rml::FontFaceHandleFreetype Handle)
	{
		return **reinterpret_cast<std::shared_ptr<Font::IFace> *>(Handle);
	}
	std::shared_ptr<Font::IFace> GetSharedFontFace(Rml::FontFaceHandleFreetype Handle)
	{
		return *reinterpret_cast<std::shared_ptr<Font::IFace> *>(Handle);
	}
} // namespace VeroScript::RmlUi

// Compatibility with the RmlUi-derived family/atlas adapter. No native font types cross this bridge.
namespace Rml::FreeType
{
	bool Initialise()
	{
		return true;
	}
	void Shutdown()
	{
	}
	bool GetFaceVariations(Span<const byte> Data, Vector<FaceVariation> &Result, int Index)
	{
		std::vector<VeroScript::Font::FVariation> Values;
		std::string Error;
		if (!VeroScript::RmlUi::FontService->GetVariations({Data.begin(), Data.end()}, Index, Values, Error))
		{
			return false;
		}
		for (const auto &Value : Values)
		{
			Result.push_back({static_cast<Style::FontWeight>(Value.Weight), static_cast<std::uint16_t>(Value.Width),
							  Value.NamedInstance});
		}
		return true;
	}
	FontFaceHandleFreetype LoadFace(Span<const byte> Data, const String &Source, int Index, int Instance)
	{
		std::string Error;
		auto Face = VeroScript::RmlUi::FontService->Load({Data.begin(), Data.end()}, Index, Error, Instance);
		if (!Face)
		{
			Log::Message(Log::LT_ERROR, "Font load failed: %s: %s", Source.c_str(), Error.c_str());
			return 0;
		}
		return reinterpret_cast<FontFaceHandleFreetype>(new std::shared_ptr<VeroScript::Font::IFace>(std::move(Face)));
	}
	bool ReleaseFace(FontFaceHandleFreetype Handle)
	{
		delete reinterpret_cast<std::shared_ptr<VeroScript::Font::IFace> *>(Handle);
		return true;
	}
	void GetFaceStyle(FontFaceHandleFreetype Handle, String *Family, Style::FontStyle *StyleValue,
					  Style::FontWeight *Weight)
	{
		const auto &Info = VeroScript::RmlUi::GetFontFace(Handle).GetInfo();
		if (Family)
		{
			*Family = Info.Family;
		}
		if (StyleValue)
		{
			*StyleValue = Info.bItalic ? Style::FontStyle::Italic : Style::FontStyle::Normal;
		}
		if (Weight)
		{
			*Weight = static_cast<Style::FontWeight>(Info.Weight);
		}
	}
} // namespace Rml::FreeType
