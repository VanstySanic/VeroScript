// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiEffects.h"
#include <RmlUi/Core/Colour.h>
#include <RmlUi/Core/Vector2.h>
#include <algorithm>
#include <cmath>

namespace VeroScript::RmlUi
{
	bool CompileEffects(const Rml::String& Name, const Rml::Dictionary& Parameters,
						std::vector<Graphics::FEffect2D>& Effects, std::string& Error)
	{
		Error.clear();
		Graphics::FEffect2D Effect;
		if (Name == "blur" || Name == "drop-shadow")
		{
			const auto Sigma = Rml::Get(Parameters, "sigma", 0.f);
			if (!std::isfinite(Sigma) || Sigma < 0 || Sigma > (Name == "drop-shadow" ? 64.f / 3 : 64.f))
			{
				Error = "rmlui.invalid_blur_sigma";
				return false;
			}
			if (Name == "blur")
			{
				const auto Passes = std::max(1, static_cast<int>(std::ceil(9 * Sigma * Sigma / (64 * 64))));
				Effect.Type = Graphics::EEffect2D::Blur;
				Effect.Radius = 3 * Sigma / std::sqrt(static_cast<float>(Passes));
				for (int I = 0; I < Passes; ++I)
				{
					Effect.bVertical = false;
					Effects.push_back(Effect);
					Effect.bVertical = true;
					Effects.push_back(Effect);
				}
				return true;
			}
			Effect.Type = Graphics::EEffect2D::Shadow;
			Effect.Radius = 3 * Sigma;
			const auto Offset = Rml::Get(Parameters, "offset", Rml::Vector2f(0));
			Effect.OffsetX = Offset.x;
			Effect.OffsetY = Offset.y;
			const auto Color = Rml::Get(Parameters, "color", Rml::Colourb(0, 0, 0, 255));
			const auto Decode = [](Rml::byte Value)
			{
				const float V = Value / 255.f;
				return V <= .04045f ? V / 12.92f : std::pow((V + .055f) / 1.055f, 2.4f);
			};
			Effect.Color = {Decode(Color.red), Decode(Color.green), Decode(Color.blue), Color.alpha / 255.f};
		}
		else
		{
			const auto Value = Rml::Get(Parameters, "value", 1.f);
			if (!std::isfinite(Value) || std::abs(Value) > 1000000 || (Name != "hue-rotate" && Value < 0))
			{
				Error = "rmlui.invalid_filter_value";
				return false;
			}
			Effect.Type = Graphics::EEffect2D::ColorMatrix;
			auto& M = Effect.Matrix;
			if (Name == "opacity")
			{
				M[18] = std::clamp(Value, 0.f, 1.f);
			}
			else if (Name == "brightness")
			{
				M[0] = M[6] = M[12] = Value;
			}
			else if (Name == "contrast")
			{
				M[0] = M[6] = M[12] = Value;
				M[4] = M[9] = M[14] = .5f - .5f * Value;
			}
			else if (Name == "invert")
			{
				const auto V = std::clamp(Value, 0.f, 1.f);
				M[0] = M[6] = M[12] = 1 - 2 * V;
				M[4] = M[9] = M[14] = V;
			}
			else if (Name == "grayscale" || Name == "saturate" || Name == "sepia")
			{
				const float V = Name == "saturate" ? 1 - Value : std::clamp(Value, 0.f, 1.f);
				const float Sepia[9]{.393f, .769f, .189f, .349f, .686f, .168f, .272f, .534f, .131f};
				const float Gray[3]{.2126f, .7152f, .0722f};
				for (int Row = 0; Row < 3; ++Row)
				{
					for (int Column = 0; Column < 3; ++Column)
					{
						M[Row * 5 + Column] = V * (Name == "sepia" ? Sepia[Row * 3 + Column] : Gray[Column]) +
											  (Row == Column ? 1 - V : 0);
					}
				}
			}
			else if (Name == "hue-rotate")
			{
				const auto C = std::cos(Value), S = std::sin(Value);
				M[0] = .213f + .787f * C - .213f * S;
				M[1] = .715f - .715f * C - .715f * S;
				M[2] = .072f - .072f * C + .928f * S;
				M[5] = .213f - .213f * C + .143f * S;
				M[6] = .715f + .285f * C + .140f * S;
				M[7] = .072f - .072f * C - .283f * S;
				M[10] = .213f - .213f * C - .787f * S;
				M[11] = .715f - .715f * C + .715f * S;
				M[12] = .072f + .928f * C + .072f * S;
			}
			else
			{
				Error = "rmlui.unsupported_filter: " + Name;
				return false;
			}
		}
		if (!Graphics::ValidateEffect2D(Effect, Error))
		{
			return false;
		}
		Effects.push_back(std::move(Effect));
		return true;
	}
} // namespace VeroScript::RmlUi
