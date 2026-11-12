// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptFont.h"
#include "VeroScriptFontLayout.h"
namespace VeroScript::FontBackend
{
	inline constexpr const char *BackendId = "font-backend";
	std::shared_ptr<Font::IService> CreateFontService();
	std::shared_ptr<Font::ITextLayoutService> CreateTextLayoutService();
}
