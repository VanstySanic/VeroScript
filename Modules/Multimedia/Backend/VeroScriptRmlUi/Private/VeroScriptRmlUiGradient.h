// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGraphics2D.h"
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Vector2.h>

namespace VeroScript::RmlUi
{
	struct FGradient
	{
		Rml::String Kind;
		Rml::Vector2f Origin, Direction;
		Rml::ColorStopList Stops;
		float Angle = 0;
		bool bRepeating = false;
	};
	bool CompileGradient(const Rml::String& Name, const Rml::Dictionary& Parameters, FGradient& Gradient,
						 std::string& Error);
	bool RasterizeGradient(const FGradient& Gradient, std::vector<Graphics::FVertex2D>& Vertices, std::int32_t& Width,
						   std::int32_t& Height, std::vector<std::uint8_t>& Pixels, std::string& Error);
	bool TryLinearGradient(const FGradient& Gradient, std::vector<Graphics::FVertex2D>& Vertices);
} // namespace VeroScript::RmlUi
