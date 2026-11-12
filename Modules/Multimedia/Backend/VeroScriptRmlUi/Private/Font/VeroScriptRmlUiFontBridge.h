// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "FontEngineDefault/FreeTypeInterface.h"
#include "VeroScriptFont.h"
#include "VeroScriptFontLayout.h"

namespace VeroScript::RmlUi
{
	Font::IFace &GetFontFace(Rml::FontFaceHandleFreetype Handle);
	std::shared_ptr<Font::IFace> GetSharedFontFace(Rml::FontFaceHandleFreetype Handle);
	bool UseFontServices(std::shared_ptr<Font::IService> Fonts,
		std::shared_ptr<Font::ITextLayoutService> TextLayouts, std::string &Error);
	const std::shared_ptr<Font::ITextLayoutService> &GetTextLayoutService();
	void ReleaseFontServices();
} // namespace VeroScript::RmlUi
