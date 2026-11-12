// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGraphics2D.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace VeroScript::Graphics
{
	void FDrawList2D::Clear()
	{
		Commands.clear();
		Clips.clear();
		State = {};
	}

	bool FDrawList2D::SetTransform(const FTransform2D& Value, std::string& Error)
	{
		if (!ValidateTransform(Value, Error))
		{
			return false;
		}
		State.Transform = Value;
		return true;
	}

	bool FDrawList2D::SetTint(const FColor& Value, std::string& Error)
	{
		if (!ValidateColor(Value, Error))
		{
			return false;
		}
		State.Tint = Value;
		return true;
	}

	bool FDrawList2D::SetBlendMode(EBlendMode Value, std::string& Error)
	{
		if (Value > EBlendMode::Additive)
		{
			Error = "graphics.invalid_blend: unknown mode";
			return false;
		}
		State.Blend = Value;
		Error.clear();
		return true;
	}

	bool FDrawList2D::SetVertexColorMode(EVertexColorMode Value, std::string& Error)
	{
		if (Value > EVertexColorMode::Premultiplied)
		{
			Error = "graphics.invalid_vertex_color_mode: unknown input encoding";
			return false;
		}
		State.VertexColorMode = Value;
		Error.clear();
		return true;
	}

	bool FDrawList2D::SetEffect(const FEffect2D& Value, std::string& Error)
	{
		if (!ValidateEffect2D(Value, Error))
		{
			return false;
		}
		State.Effect = Value;
		return true;
	}

	void FDrawList2D::SetTexture(std::shared_ptr<ITexture> Texture, std::shared_ptr<ISampler> Sampler)
	{
		State.Texture = std::move(Texture);
		State.Sampler = std::move(Sampler);
	}
	bool ValidateEffect2D(const FEffect2D& Effect, std::string& Error)
	{
		if (Effect.Type > EEffect2D::Shadow || !std::isfinite(Effect.Radius) || Effect.Radius < 0 ||
			Effect.Radius > 64 || !std::isfinite(Effect.OffsetX) || !std::isfinite(Effect.OffsetY) ||
			std::abs(Effect.OffsetX) > 1000000 || std::abs(Effect.OffsetY) > 1000000 ||
			!ValidateColor(Effect.Color, Error) ||
			(Effect.Type == EEffect2D::AlphaMask && (!Effect.Mask || !Effect.Mask->IsOpen())))
		{
			Error = "graphics.invalid_effect: invalid kind, mask, color or radius outside 0..64";
			return false;
		}
		for (const auto Value : Effect.Matrix)
		{
			if (!std::isfinite(Value) || std::abs(Value) > 1000000)
			{
				Error = "graphics.invalid_effect: color matrix coefficients must be finite in -1000000..1000000";
				return false;
			}
		}
		Error.clear();
		return true;
	}
	bool FDrawList2D::PushClip(const FRect& Rect, std::string& Error)
	{
		if (!ValidateRect(Rect, Error))
		{
			return false;
		}
		if (Clips.size() >= 65536)
		{
			Error = "graphics.resource_limit: clip stack is full";
			return false;
		}
		Clips.push_back(Clips.empty() ? Rect : IntersectRects(Clips.back(), Rect));
		State.bClip = true;
		State.Clip = Clips.back();
		return true;
	}
	bool FDrawList2D::PopClip(std::string& Error)
	{
		if (Clips.empty())
		{
			Error = "graphics.clip_underflow: no clip to restore";
			return false;
		}
		Clips.pop_back();
		State.bClip = !Clips.empty();
		State.Clip = State.bClip ? Clips.back() : FRect{};
		Error.clear();
		return true;
	}
	bool FDrawList2D::AddGeometry(std::shared_ptr<IGeometry2D> Geometry, std::string& Error)
	{
		if (!Geometry || !Geometry->IsOpen())
		{
			Error = "graphics.invalid_geometry: geometry is null or closed";
			return false;
		}
		if (Commands.size() >= 65536)
		{
			Error = "graphics.resource_limit: too many drawing commands";
			return false;
		}
		auto Draw = State;
		Draw.Geometry = std::move(Geometry);
		Commands.push_back(std::move(Draw));
		Error.clear();
		return true;
	}
	const std::vector<FDraw2D>& FDrawList2D::GetCommands() const noexcept
	{
		return Commands;
	}
	bool ValidateRect(const FRect& Rect, std::string& Error)
	{
		const auto Min = std::numeric_limits<std::int32_t>::min(), Max = std::numeric_limits<std::int32_t>::max();
		if (Rect.X < Min || Rect.X > Max || Rect.Y < Min || Rect.Y > Max || Rect.Width < 0 || Rect.Width > Max ||
			Rect.Height < 0 || Rect.Height > Max || Rect.X + Rect.Width > Max || Rect.Y + Rect.Height > Max)
		{
			Error = "graphics.invalid_rect: coordinates and nonnegative extents must fit int32";
			return false;
		}
		Error.clear();
		return true;
	}
	FRect IntersectRects(const FRect& Left, const FRect& Right)
	{
		const auto X = std::max(Left.X, Right.X), Y = std::max(Left.Y, Right.Y);
		return {X, Y, std::max<std::int64_t>(0, std::min(Left.X + Left.Width, Right.X + Right.Width) - X),
				std::max<std::int64_t>(0, std::min(Left.Y + Left.Height, Right.Y + Right.Height) - Y)};
	}
	bool ValidateTransform(const FTransform2D& Transform, std::string& Error)
	{
		for (float Value : Transform.Values)
		{
			if (!std::isfinite(Value))
			{
				Error = "graphics.invalid_transform: matrix must be finite";
				return false;
			}
		}
		Error.clear();
		return true;
	}
	bool ValidateGeometry2D(const std::vector<FVertex2D>& Vertices, const std::vector<std::uint32_t>& Indices,
							std::uint64_t ByteLimit, std::string& Error)
	{
		const auto Limit = std::min<std::uint64_t>(ByteLimit, std::numeric_limits<std::uint32_t>::max());
		if (Vertices.empty() || Indices.empty() || Indices.size() % 3 || Vertices.size() > Limit / sizeof(FVertex2D) ||
			Indices.size() > (Limit - Vertices.size() * sizeof(FVertex2D)) / sizeof(std::uint32_t))
		{
			Error = "graphics.invalid_geometry: expected nonempty indexed triangles within byte limit";
			return false;
		}
		for (const auto& V : Vertices)
		{
			if (!std::isfinite(V.X) || !std::isfinite(V.Y) || !std::isfinite(V.U) || !std::isfinite(V.V) ||
				!ValidateColor(V.Color, Error))
			{
				Error = "graphics.invalid_geometry: vertices must be finite with normalized colors";
				return false;
			}
		}
		for (auto Index : Indices)
		{
			if (Index >= Vertices.size())
			{
				Error = "graphics.invalid_geometry: index out of bounds";
				return false;
			}
		}
		Error.clear();
		return true;
	}
}
