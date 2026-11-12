// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

// Adapted from RmlUi 6.3, MIT license; see ThirdParty/RmlUi/LICENSE.txt.

#include <RmlUi/Core.h>

enum class TextFlowDirection
{
	LeftToRight,
	RightToLeft,
};

struct LanguageData
{
	Rml::String script_code;
	TextFlowDirection text_flow_direction;
};

using LanguageDataMap = Rml::UnorderedMap<Rml::String, LanguageData>;
