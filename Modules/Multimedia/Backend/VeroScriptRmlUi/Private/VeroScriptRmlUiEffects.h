// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGraphics2D.h"
#include <RmlUi/Core/Dictionary.h>

namespace VeroScript::RmlUi
{
	bool CompileEffects(const Rml::String& Name, const Rml::Dictionary& Parameters,
						std::vector<Graphics::FEffect2D>& Effects, std::string& Error);
}
