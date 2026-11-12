// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGraphics2D.h"

namespace VeroScript::Graphics
{
	enum class ECompositionCommand2D : std::uint8_t
	{
		Draw,
		BeginLayer,
		EndLayer,
		BeginMask,
		EndMask,
		Effect,
		BackdropBlur
	};

	struct FCompositionCommand2D
	{
		ECompositionCommand2D Type = ECompositionCommand2D::Draw;
		FDrawList2D Draws;
		FEffect2D Effect;
		float Opacity = 1;
		bool bInvert = false;
	};

	struct FCompositorOptions2D
	{
		std::uint64_t MaxPooledBytes = 256 * 1024 * 1024;
		std::int64_t MaxDepth = 32;
		// sRGB storage preserves dark-color precision while attachment blending remains linear.
		ETextureFormat IntermediateFormat = ETextureFormat::Rgba8Srgb;
	};

	/** Records immutable drawing snapshots. Mask scopes are isolated layers intersected at composition time. */
	class FComposition2D
	{
	public:
		void Clear();
		bool AddDraw(const FDrawList2D& Draws, std::string& Error);
		bool BeginLayer(float Opacity, std::string& Error);
		bool EndLayer(std::string& Error);
		bool PushMask(const FDrawList2D& Mask, bool bInvert, std::string& Error);
		bool PopMask(std::string& Error);
		// Effects always read premultiplied composition contents, ignoring bSourcePremultiplied.
		// Blur is separable: add horizontal and vertical effects, or use AddBlur for both.
		bool AddEffect(const FEffect2D& Effect, std::string& Error);
		bool AddBlur(float Radius, std::string& Error);
		// Replaces the current backdrop inside Mask with its blurred image, preserving alpha correctly.
		bool AddBackdropBlur(const FDrawList2D& Mask, float Radius, std::string& Error);
		bool IsBalanced() const noexcept;
		const std::vector<FCompositionCommand2D>& GetCommands() const noexcept;

	private:
		bool Begin(FCompositionCommand2D Command, bool bMask, std::string& Error);
		bool End(bool bMask, std::string& Error);
		std::vector<FCompositionCommand2D> Commands;
		std::vector<bool> Scopes;
		std::size_t DrawCount = 0;
	};

	class ICompositor2D
	{
	public:
		virtual ~ICompositor2D() = default;
		virtual bool IsOpen() const noexcept = 0;
		virtual std::int64_t GetPooledBytes() const noexcept = 0;
		// Composition targets contain premultiplied pixels. ClearColor is straight linear RGBA and is
		// premultiplied by the compositor; loading an existing texture requires premultiplied contents.
		virtual bool RenderTexture(const std::shared_ptr<ITexture>& Target, const FComposition2D& Composition,
								   const FRenderOptions2D& Options, std::string& Error) = 0;
		// Extents are drawable pixels; refresh them from Platform after size/scale changes.
		virtual bool RenderSurface(const std::shared_ptr<ISurface>& Target, std::int64_t Width, std::int64_t Height,
								   const FComposition2D& Composition, const FRenderOptions2D& Options, bool& bPresented,
								   std::string& Error) = 0;
		virtual bool Trim(std::string& Error) = 0;
		virtual bool Close(std::string& Error) = 0;
	};
	// Shared public composition implementation; the device still supplies every GPU operation.
	std::shared_ptr<ICompositor2D> CreateCompositor2D(std::shared_ptr<IDevice> Device,
													  const FCompositorOptions2D& Options, std::string& Error);
}
