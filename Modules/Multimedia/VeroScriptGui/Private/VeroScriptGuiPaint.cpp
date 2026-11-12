// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiPaint.h"
#include <algorithm>
#include <cmath>

namespace VeroScript::Gui
{
	namespace
	{
		bool ValidRect(const FRect& R)
		{
			return std::isfinite(R.X) && std::isfinite(R.Y) && std::isfinite(R.Width) && std::isfinite(R.Height) &&
			    R.Width >= 0 && R.Height >= 0 && std::abs(R.X) <= 16777216 && std::abs(R.Y) <= 16777216 &&
			    R.Width <= 16777216 && R.Height <= 16777216;
		}
	} // namespace
	FPaintContext::FPaintContext(FRect InBounds) : Bounds(InBounds), Thread(std::this_thread::get_id())
	{
	}
	bool FPaintContext::FillRect(const FRect& R, const Graphics::FColor& Color, std::string& Error)
	{
		return DrawImage(R, {}, Color, Error);
	}
	bool FPaintContext::DrawLine(double X1, double Y1, double X2, double Y2, double Thickness,
	    const Graphics::FColor& Color, std::string& Error)
	{
		if (!Check(Error))
		{
			return false;
		}
		if (!std::isfinite(Thickness) || Thickness <= 0 || Thickness > 16777216 || !std::isfinite(X1) ||
		    !std::isfinite(Y1) || !std::isfinite(X2) || !std::isfinite(Y2))
		{
			Error = "gui.invalid_paint_line";
			return false;
		}
		const double Length = std::hypot(X2 - X1, Y2 - Y1);
		if (Length == 0)
		{
			return FillRect({X1 - Thickness / 2, Y1 - Thickness / 2, Thickness, Thickness}, Color, Error);
		}
		const double DX = -(Y2 - Y1) * Thickness / (2 * Length), DY = (X2 - X1) * Thickness / (2 * Length);
		return DrawMesh({{static_cast<float>(X1 + DX), static_cast<float>(Y1 + DY), 0, 0, Color},
		                    {static_cast<float>(X2 + DX), static_cast<float>(Y2 + DY), 1, 0, Color},
		                    {static_cast<float>(X2 - DX), static_cast<float>(Y2 - DY), 1, 1, Color},
		                    {static_cast<float>(X1 - DX), static_cast<float>(Y1 - DY), 0, 1, Color}},
		    {0, 1, 2, 0, 2, 3}, {}, Error);
	}
	bool FPaintContext::DrawImage(const FRect& R, std::string Image, const Graphics::FColor& Color, std::string& Error)
	{
		if (!Check(Error) || !ValidRect(R) || !Graphics::ValidateColor(Color, Error))
		{
			if (Error.empty())
			{
				Error = "gui.invalid_paint_rect";
			}
			return false;
		}
		const auto X = static_cast<float>(R.X), Y = static_cast<float>(R.Y), W = static_cast<float>(R.Width),
		           H = static_cast<float>(R.Height);
		return DrawMesh(
		    {{X, Y, 0, 0, Color}, {X + W, Y, 1, 0, Color}, {X + W, Y + H, 1, 1, Color}, {X, Y + H, 0, 1, Color}},
		    {0, 1, 2, 0, 2, 3}, std::move(Image), Error);
	}
	bool FPaintContext::DrawMesh(std::vector<Graphics::FVertex2D> Vertices, std::vector<std::uint32_t> Indices,
	    std::string Image, std::string& Error)
	{
		if (!Check(Error) || !ValidateGuiText(Image) ||
		    !Graphics::ValidateGeometry2D(Vertices, Indices, 16 * 1024 * 1024, Error))
		{
			if (Error.empty())
			{
				Error = "gui.invalid_paint_mesh";
			}
			return false;
		}
		const auto Size = Vertices.size() * sizeof(Graphics::FVertex2D) + Indices.size() * sizeof(std::uint32_t);
		if (Meshes.size() >= 4096 || Bytes + Size > 16 * 1024 * 1024)
		{
			Error = "gui.paint_budget_exceeded";
			return false;
		}
		const auto& M = Transform.Values;
		for (auto& V : Vertices)
		{
			const float W = M[3] * V.X + M[7] * V.Y + M[15];
			const float X = M[0] * V.X + M[4] * V.Y + M[12], Y = M[1] * V.X + M[5] * V.Y + M[13];
			if (!std::isfinite(W) || std::abs(W) < 1e-6f || !std::isfinite(X / W) || !std::isfinite(Y / W) ||
			    std::abs(X / W) > 16777216 || std::abs(Y / W) > 16777216)
			{
				Error = "gui.invalid_paint_projection";
				return false;
			}
			V.X = X / W;
			V.Y = Y / W;
		}
		FPaintMesh Mesh{std::move(Vertices), std::move(Indices), std::move(Image)};
		if (!Clips.empty())
		{
			Mesh.bClip = true;
			Mesh.Clip = Clips.back();
		}
		Meshes.push_back(std::move(Mesh));
		Bytes += Size;
		return true;
	}
	bool FPaintContext::SetTransform(const Graphics::FTransform2D& Value, std::string& Error)
	{
		if (!Check(Error) || !Graphics::ValidateTransform(Value, Error))
		{
			return false;
		}
		Transform = Value;
		return true;
	}
	bool FPaintContext::PushClip(const FRect& Rect, std::string& Error)
	{
		if (!Check(Error))
		{
			return false;
		}
		if (!ValidRect(Rect) || Clips.size() >= 24)
		{
			Error = "gui.invalid_paint_clip";
			return false;
		}
		auto R = Rect;
		if (!Clips.empty())
		{
			const auto& P = Clips.back();
			R.X = std::max(P.X, Rect.X);
			R.Y = std::max(P.Y, Rect.Y);
			R.Width = std::max(0.0, std::min(P.X + P.Width, Rect.X + Rect.Width) - R.X);
			R.Height = std::max(0.0, std::min(P.Y + P.Height, Rect.Y + Rect.Height) - R.Y);
		}
		Clips.push_back(R);
		return true;
	}
	bool FPaintContext::PopClip(std::string& Error)
	{
		if (!Check(Error))
		{
			return false;
		}
		if (Clips.empty())
		{
			Error = "gui.unbalanced_paint_clip";
			return false;
		}
		Clips.pop_back();
		return true;
	}
	bool FPaintContext::Finish(std::string& Error)
	{
		if (!Check(Error))
		{
			return false;
		}
		bFinished = true;
		if (!Clips.empty())
		{
			Error = "gui.unbalanced_paint_clip";
			return false;
		}
		return true;
	}
	const FRect& FPaintContext::GetBounds() const noexcept
	{
		return Bounds;
	}
	const std::vector<FPaintMesh>& FPaintContext::GetMeshes() const noexcept
	{
		return Meshes;
	}
	bool FPaintContext::Check(std::string& Error) const
	{
		Error.clear();
		if (bFinished || Thread != std::this_thread::get_id())
		{
			Error = bFinished ? "gui.paint_context_expired" : "gui.wrong_thread";
			return false;
		}
		return true;
	}
	bool PaintMeshesEqual(const std::vector<FPaintMesh>& Left, const std::vector<FPaintMesh>& Right)
	{
		if (Left.size() != Right.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < Left.size(); ++I)
		{
			const auto& A = Left[I];
			const auto& B = Right[I];
			if (A.Image != B.Image || A.bClip != B.bClip || A.Clip.X != B.Clip.X || A.Clip.Y != B.Clip.Y ||
			    A.Clip.Width != B.Clip.Width || A.Clip.Height != B.Clip.Height || A.Indices != B.Indices ||
			    A.Vertices.size() != B.Vertices.size())
			{
				return false;
			}
			for (std::size_t J = 0; J < A.Vertices.size(); ++J)
			{
				const auto& X = A.Vertices[J];
				const auto& Y = B.Vertices[J];
				if (X.X != Y.X || X.Y != Y.Y || X.U != Y.U || X.V != Y.V || !PropertyValuesEqual(X.Color, Y.Color))
				{
					return false;
				}
			}
		}
		return true;
	}
} // namespace VeroScript::Gui
