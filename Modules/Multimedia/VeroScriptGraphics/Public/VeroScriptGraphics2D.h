// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGraphics.h"
#include <array>

namespace VeroScript::Graphics
{
	class IGeometry2D;

	enum class EBlendMode : std::uint8_t
	{
		Opaque,
		StraightAlpha,
		PremultipliedAlpha,
		Additive
	};

	enum class EVertexColorMode : std::uint8_t
	{
		Straight,
		Premultiplied
	};

	enum class EEffect2D : std::uint8_t
	{
		None,
		AlphaMask,
		Blur,
		ColorMatrix,
		Shadow
	};

	struct FVertex2D
	{
		float X = 0, Y = 0, U = 0, V = 0;
		// Linear RGBA; its alpha encoding is selected by FDraw2D::VertexColorMode.
		FColor Color{1, 1, 1, 1};
	};

	struct FRect
	{
		std::int64_t X = 0, Y = 0, Width = 0, Height = 0;
	};

	struct FTransform2D
	{
		// Column-major homogeneous matrix. Positions are measured in viewport pixels, Y downward.
		std::array<float, 16> Values{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	};

	/** Effects sample in linear premultiplied space; radius is bounded to 64 source pixels per pass. */
	struct FEffect2D
	{
		EEffect2D Type = EEffect2D::None;
		std::shared_ptr<ITexture> Mask;
		float Radius = 0;
		float OffsetX = 0, OffsetY = 0;
		bool bVertical = false, bInvert = false, bSourcePremultiplied = true;
		FColor Color{0, 0, 0, 1};
		// Row-major 4x5 RGBA transform; each row's last value is its offset.
		std::array<float, 20> Matrix{1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
	};

	struct FDraw2D
	{
		std::shared_ptr<IGeometry2D> Geometry;
		// Null texture selects solid white; null sampler selects linear/clamp.
		std::shared_ptr<ITexture> Texture;
		std::shared_ptr<ISampler> Sampler;
		FTransform2D Transform;
		FColor Tint{1, 1, 1, 1};
		EBlendMode Blend = EBlendMode::StraightAlpha;
		EVertexColorMode VertexColorMode = EVertexColorMode::Straight;
		bool bClip = false;
		FRect Clip;
		FEffect2D Effect;
	};

	struct FRenderOptions2D
	{
		// Clear applies to the whole target; disable it to preserve previous contents.
		bool bClear = true;
		FColor ClearColor{0, 0, 0, 0};
		bool bViewport = false;
		FRect Viewport;
	};
	bool ValidateRect(const FRect& Rect, std::string& Error);
	// Inputs must have passed ValidateRect.
	FRect IntersectRects(const FRect& Left, const FRect& Right);
	bool ValidateTransform(const FTransform2D& Transform, std::string& Error);
	bool ValidateEffect2D(const FEffect2D& Effect, std::string& Error);
	bool ValidateGeometry2D(const std::vector<FVertex2D>& Vertices, const std::vector<std::uint32_t>& Indices,
							std::uint64_t ByteLimit, std::string& Error);
	class IGeometry2D
	{
	public:
		virtual ~IGeometry2D() = default;
		virtual bool IsOpen() const noexcept = 0;
		virtual std::int64_t GetVertexCount() const noexcept = 0;
		virtual std::int64_t GetIndexCount() const noexcept = 0;
		virtual bool Close(std::string& Error) = 0;
	};
	/** CPU command builder. Commands retain resources and snapshot the current drawing state. */
	class FDrawList2D
	{
	public:
		void Clear();
		bool SetTransform(const FTransform2D& Value, std::string& Error);
		bool SetTint(const FColor& Value, std::string& Error);
		bool SetBlendMode(EBlendMode Value, std::string& Error);
		// Premultiplied input remains premultiplied during vertex interpolation. Tint is always straight.
		bool SetVertexColorMode(EVertexColorMode Value, std::string& Error);
		bool SetEffect(const FEffect2D& Value, std::string& Error);
		void SetTexture(std::shared_ptr<ITexture> Texture, std::shared_ptr<ISampler> Sampler = {});
		bool PushClip(const FRect& Rect, std::string& Error);
		bool PopClip(std::string& Error);
		bool AddGeometry(std::shared_ptr<IGeometry2D> Geometry, std::string& Error);
		const std::vector<FDraw2D>& GetCommands() const noexcept;

	private:
		FDraw2D State;
		std::vector<FRect> Clips;
		std::vector<FDraw2D> Commands;
	};
	class IRenderer2D
	{
	public:
		virtual ~IRenderer2D() = default;
		virtual bool IsOpen() const noexcept = 0;
		virtual std::shared_ptr<IGeometry2D> CreateGeometry(const std::vector<FVertex2D>& Vertices,
															const std::vector<std::uint32_t>& Indices,
															std::string& Error) = 0;
		// Validate all commands before recording; feedback sampling of the target is rejected.
		virtual bool RenderTexture(const std::shared_ptr<ITexture>& Target, const FDrawList2D& List,
								   const FRenderOptions2D& Options, std::string& Error) = 0;
		virtual bool RenderSurface(const std::shared_ptr<ISurface>& Target, const FDrawList2D& List,
								   const FRenderOptions2D& Options, bool& bPresented, std::string& Error) = 0;
		virtual bool Close(std::string& Error) = 0;
	};
	/** Optional submission scope for consecutive calls on one renderer. Rendering
	 * order and validation are unchanged; EndSubmission submits the recorded work.
	 * Calls stay on the owner thread. Nested scopes are rejected. CancelSubmission
	 * discards pending work, except an acquired swapchain image must be submitted
	 * as required by the provider. Resources must remain open until the scope ends.
	 * During a scope bPresented reports acquisition; successful EndSubmission
	 * confirms submission. Other renderers must consume these outputs after end. */
	class IRenderSubmission2D
	{
	public:
		virtual ~IRenderSubmission2D() = default;
		virtual bool BeginSubmission(std::string& Error) = 0;
		virtual bool EndSubmission(std::string& Error) = 0;
		virtual bool CancelSubmission(std::string& Error) = 0;
	};
}
