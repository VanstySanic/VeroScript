// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptFontBackend.h"
#include "VeroScriptRmlUi.h"
#include "VeroScriptStb.h"

namespace VeroScript
{
	inline RmlUi::FOptions GuiTestResourceOptions(RmlUi::FOptions Options = {})
	{
		static const auto Fonts = FontBackend::CreateFontService();
		static const auto TextLayouts = FontBackend::CreateTextLayoutService();
		if (!Options.Images)
		{
			Options.Images = Stb::CreateImageService();
		}
		if (!Options.Fonts)
		{
			Options.Fonts = Fonts;
		}
		if (!Options.TextLayouts)
		{
			Options.TextLayouts = TextLayouts;
		}
		return Options;
	}
} // namespace VeroScript
