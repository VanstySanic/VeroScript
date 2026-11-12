// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiTypes.h"
#include "VeroScriptGraphics2D.h"
#include <thread>

namespace VeroScript::Gui
{
	struct FPaintMesh
	{
		std::vector<Graphics::FVertex2D> Vertices;
		std::vector<std::uint32_t> Indices;
		std::string Image;
		bool bClip = false;
		FRect Clip;
	};

	// A borrowed, owner-thread canvas in content-local logical pixels. Painting cannot mutate the widget tree.
	class FPaintContext
	{
	public:
		explicit FPaintContext(FRect Bounds);
		bool FillRect(const FRect& Rect, const Graphics::FColor& Color, std::string& Error);
		bool DrawLine(double X1, double Y1, double X2, double Y2, double Thickness, const Graphics::FColor& Color,
					  std::string& Error);
		bool DrawImage(const FRect& Rect, std::string Image, const Graphics::FColor& Tint, std::string& Error);
		bool DrawMesh(std::vector<Graphics::FVertex2D> Vertices, std::vector<std::uint32_t> Indices, std::string Image,
					  std::string& Error);
		bool SetTransform(const Graphics::FTransform2D& Transform, std::string& Error);
		bool PushClip(const FRect& Rect, std::string& Error);
		bool PopClip(std::string& Error);
		bool Finish(std::string& Error);
		const FRect& GetBounds() const noexcept;
		const std::vector<FPaintMesh>& GetMeshes() const noexcept;

	private:
		bool Check(std::string& Error) const;
		FRect Bounds;
		std::thread::id Thread;
		Graphics::FTransform2D Transform;
		std::vector<FRect> Clips;
		std::vector<FPaintMesh> Meshes;
		std::uint64_t Bytes = 0;
		bool bFinished = false;
	};
	using FPaintCallback = std::function<bool(FPaintContext&, std::string&)>;
	bool PaintMeshesEqual(const std::vector<FPaintMesh>& Left, const std::vector<FPaintMesh>& Right);
} // namespace VeroScript::Gui
