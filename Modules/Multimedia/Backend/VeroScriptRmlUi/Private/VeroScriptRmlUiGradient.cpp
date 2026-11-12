// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRmlUiGradient.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace VeroScript::RmlUi
{
	bool CompileGradient(const Rml::String& Name, const Rml::Dictionary& Parameters, FGradient& G, std::string& Error)
	{
		Error.clear();
		if (Name != "linear-gradient" && Name != "radial-gradient" && Name != "conic-gradient")
		{
			Error = "rmlui.unsupported_shader: " + Name;
			return false;
		}
		G.Kind = Name;
		G.bRepeating = Rml::Get(Parameters, "repeating", false);
		G.Stops = Rml::Get(Parameters, "color_stop_list", Rml::ColorStopList{});
		if (G.Stops.empty() || G.Stops.size() > 1024)
		{
			Error = "rmlui.invalid_gradient_stops";
			return false;
		}
		float Previous = -std::numeric_limits<float>::infinity();
		for (const auto& Stop : G.Stops)
		{
			if (Stop.position.unit != Rml::Unit::NUMBER || !std::isfinite(Stop.position.number) ||
			    std::abs(Stop.position.number) > 16777216 || Stop.position.number < Previous)
			{
				Error = "rmlui.invalid_gradient_stops";
				return false;
			}
			Previous = Stop.position.number;
		}
		G.Origin = Rml::Get(Parameters, Name == "linear-gradient" ? "p0" : "center", Rml::Vector2f(0));
		G.Direction = Name == "linear-gradient" ? Rml::Get(Parameters, "p1", Rml::Vector2f(1)) - G.Origin
		                                        : Rml::Get(Parameters, "radius", Rml::Vector2f(1));
		G.Angle = Rml::Get(Parameters, "angle", 0.f);
		if (!std::isfinite(G.Origin.x) || !std::isfinite(G.Origin.y) || !std::isfinite(G.Direction.x) ||
		    !std::isfinite(G.Direction.y) || !std::isfinite(G.Angle) || std::abs(G.Origin.x) > 16777216 ||
		    std::abs(G.Origin.y) > 16777216 || std::abs(G.Direction.x) > 16777216 ||
		    std::abs(G.Direction.y) > 16777216 ||
		    (Name == "radial-gradient" && (G.Direction.x <= 0 || G.Direction.y <= 0)))
		{
			Error = "rmlui.invalid_gradient_geometry";
			return false;
		}
		return true;
	}

	bool RasterizeGradient(const FGradient& G, std::vector<Graphics::FVertex2D>& Vertices, std::int32_t& Width,
	    std::int32_t& Height, std::vector<std::uint8_t>& Pixels, std::string& Error)
	{
		if (Vertices.empty())
		{
			Error = "rmlui.empty_gradient_geometry";
			return false;
		}
		float Left = Vertices[0].U, Right = Left, Top = Vertices[0].V, Bottom = Top;
		for (const auto& V : Vertices)
		{
			Left = std::min(Left, V.U);
			Right = std::max(Right, V.U);
			Top = std::min(Top, V.V);
			Bottom = std::max(Bottom, V.V);
		}
		const float W = Right - Left, H = Bottom - Top;
		if (!std::isfinite(W) || !std::isfinite(H) || W < 0 || H < 0 || W > 8192 || H > 8192)
		{
			Error = "rmlui.gradient_extent_limit";
			return false;
		}
		Width = std::max(1, static_cast<int>(std::ceil(W)));
		Height = std::max(1, static_cast<int>(std::ceil(H)));
		if (static_cast<std::uint64_t>(Width) * Height > 16 * 1024 * 1024)
		{
			Error = "rmlui.gradient_texture_limit";
			return false;
		}
		std::vector<std::array<float, 4>> Colors;
		for (const auto& Stop : G.Stops)
		{
			const auto C = Stop.color;
			const auto Decode = [A = C.alpha / 255.f](Rml::byte B)
			{
				const float V = A > 0 ? std::clamp(B / (255.f * A), 0.f, 1.f) : 0;
				return A * (V <= .04045f ? V / 12.92f : std::pow((V + .055f) / 1.055f, 2.4f));
			};
			Colors.push_back({Decode(C.red), Decode(C.green), Decode(C.blue), C.alpha / 255.f});
		}
		Pixels.resize(static_cast<std::size_t>(Width) * Height * 4);
		for (int Y = 0; Y < Height; ++Y)
		{
			for (int X = 0; X < Width; ++X)
			{
				const float DX = Left + (X + .5f) * W / Width - G.Origin.x;
				const float DY = Top + (Y + .5f) * H / Height - G.Origin.y;
				float T = 0;
				if (G.Kind == "linear-gradient")
				{
					const float Length = G.Direction.x * G.Direction.x + G.Direction.y * G.Direction.y;
					T = Length > 0 ? (DX * G.Direction.x + DY * G.Direction.y) / Length : 1;
				}
				else if (G.Kind == "radial-gradient")
				{
					T = std::hypot(DX / G.Direction.x, DY / G.Direction.y);
				}
				else
				{
					const float C = std::cos(G.Angle), S = std::sin(G.Angle);
					T = .5f + std::atan2(-(C * DX + S * DY), -S * DX + C * DY) / 6.28318530718f;
				}
				const float First = G.Stops.front().position.number, Last = G.Stops.back().position.number;
				if (G.bRepeating && Last > First)
				{
					T = First + (T - First) - std::floor((T - First) / (Last - First)) * (Last - First);
				}
				const auto Upper = std::upper_bound(G.Stops.begin(), G.Stops.end(), T, [](float Value, const auto& Stop)
				{
					return Value < Stop.position.number;
				});
				const auto B =
				    Upper == G.Stops.end() ? G.Stops.size() - 1 : static_cast<std::size_t>(Upper - G.Stops.begin());
				const auto A = B == 0 ? 0 : B - 1;
				const float Extent = G.Stops[B].position.number - G.Stops[A].position.number;
				const float Mix = Extent > 0 ? std::clamp((T - G.Stops[A].position.number) / Extent, 0.f, 1.f) : 1;
				std::array<float, 4> Color;
				for (int I = 0; I < 4; ++I)
				{
					Color[I] = Colors[A][I] + Mix * (Colors[B][I] - Colors[A][I]);
				}
				const auto At = (static_cast<std::size_t>(Y) * Width + X) * 4;
				for (int I = 0; I < 3; ++I)
				{
					const float Straight = Color[3] > 0 ? std::clamp(Color[I] / Color[3], 0.f, 1.f) : 0;
					const float Encoded =
					    Straight <= .0031308f ? 12.92f * Straight : 1.055f * std::pow(Straight, 1.f / 2.4f) - .055f;
					Pixels[At + I] =
					    static_cast<std::uint8_t>(std::lround(std::clamp(Encoded * Color[3], 0.f, 1.f) * 255));
				}
				Pixels[At + 3] = static_cast<std::uint8_t>(std::lround(std::clamp(Color[3], 0.f, 1.f) * 255));
			}
		}
		for (auto& V : Vertices)
		{
			V.U = W > 0 ? (V.U - Left) / W : 0;
			V.V = H > 0 ? (V.V - Top) / H : 0;
		}
		return true;
	}

	bool TryLinearGradient(const FGradient& G, std::vector<Graphics::FVertex2D>& Vertices)
	{
		if (G.Kind != "linear-gradient" || G.bRepeating || G.Stops.size() != 2 || Vertices.empty()) return false;
		const auto First = G.Stops[0].position.number, Last = G.Stops[1].position.number;
		const auto Length = G.Direction.x * G.Direction.x + G.Direction.y * G.Direction.y;
		if (!(Last > First) || !(Length > 0)) return false;
		const auto Tint = Vertices.front().Color;
		const auto Position = [&](const Graphics::FVertex2D& Vertex)
		{
			return ((Vertex.U - G.Origin.x) * G.Direction.x + (Vertex.V - G.Origin.y) * G.Direction.y) / Length;
		};
		for (const auto& Vertex : Vertices)
		{
			const auto At = Position(Vertex);
			if (!std::isfinite(At) || At < First || At > Last || Vertex.Color.R != Tint.R || Vertex.Color.G != Tint.G ||
			    Vertex.Color.B != Tint.B || Vertex.Color.A != Tint.A)
			{
				return false;
			}
		}
		std::array<Graphics::FColor, 2> Colors;
		for (std::size_t Index = 0; Index < Colors.size(); ++Index)
		{
			const auto Color = G.Stops[Index].color;
			const float Alpha = Color.alpha / 255.f;
			const auto Decode = [Alpha](Rml::byte Byte)
			{
				const float Value = Alpha > 0 ? std::clamp(Byte / (255.f * Alpha), 0.f, 1.f) : 0;
				return Alpha * (Value <= .04045f ? Value / 12.92f : std::pow((Value + .055f) / 1.055f, 2.4f));
			};
			Colors[Index] = {Decode(Color.red), Decode(Color.green), Decode(Color.blue), Alpha};
		}
		// An unclamped two-stop ramp and a uniform tint are affine over each
		// triangle. Interpolating premultiplied linear vertex colors is exact.
		for (auto& Vertex : Vertices)
		{
			const auto Mix = (Position(Vertex) - First) / (Last - First);
			Vertex.Color = {(Colors[0].R + Mix * (Colors[1].R - Colors[0].R)) * Tint.R,
			    (Colors[0].G + Mix * (Colors[1].G - Colors[0].G)) * Tint.G,
			    (Colors[0].B + Mix * (Colors[1].B - Colors[0].B)) * Tint.B,
			    (Colors[0].A + Mix * (Colors[1].A - Colors[0].A)) * Tint.A};
		}
		return true;
	}
} // namespace VeroScript::RmlUi
