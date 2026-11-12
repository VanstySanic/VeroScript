// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRmlUi.h"
#include "VeroScriptGraphicsComposition2D.h"
#include <map>

namespace VeroScript::RmlUi
{
	struct FClipMask
	{
		Graphics::FDrawList2D Draws;
		bool bInvert = false;
	};
	/** Frame-local layer identities; only the final completed image reaches the host target. */
	class FCompositionFrame
	{
	public:
		bool Initialize(const FOptions& Options, std::shared_ptr<Graphics::IRenderer2D> Renderer,
						std::shared_ptr<Graphics::ISampler> Sampler, std::string& Error);
		void Begin(std::int64_t Width, std::int64_t Height);
		bool IsAvailable() const noexcept;
		bool HasWork() const noexcept;
		bool AddDraw(const Graphics::FDrawList2D& Draws, std::string& Error, const std::vector<FClipMask>& Masks = {});
		std::uintptr_t Push(std::string& Error);
		bool Pop(std::string& Error);
		bool Capture(std::shared_ptr<Graphics::ITexture> Target, const Graphics::FRect& Region, bool Clip,
					 const Graphics::FRect& Scissor, std::string& Error);
		bool Composite(std::uintptr_t Source, std::uintptr_t Destination, bool Replace,
					   std::vector<Graphics::FEffect2D> Effects, bool Clip, const Graphics::FRect& Scissor,
					   std::string& Error, const std::vector<FClipMask>& Masks = {});
		bool Submit(bool& Presented, std::string& Error);
		void Cancel();
		bool Close(std::string& Error);

	private:
		struct FCommand
		{
			std::uintptr_t Source = 0, Destination = 0;
			Graphics::FDrawList2D Draws;
			std::vector<Graphics::FEffect2D> Effects;
			std::vector<FClipMask> Masks;
			Graphics::FRect Scissor;
			bool bComposite = false, bReplace = false, bClip = false;
			std::shared_ptr<Graphics::ITexture> CaptureTarget;
			Graphics::FRect CaptureRegion;
		};
		bool Prepare(std::string& Error);
		bool Quad(Graphics::FDrawList2D& Draws, std::shared_ptr<Graphics::ITexture> Texture, bool Replace, bool Clip,
				  const Graphics::FRect& Scissor, std::string& Error, const Graphics::FEffect2D* Effect = nullptr);
		bool ClearLayer(std::size_t Index, std::vector<bool>& Initialized, std::string& Error);
		bool RenderDraw(const std::shared_ptr<Graphics::ITexture>& Target, const Graphics::FDrawList2D& Draws,
						const std::vector<FClipMask>& Masks, const Graphics::FRenderOptions2D& Options,
						Graphics::IRenderSubmission2D* Submission, std::string& Error);
		FOptions Options;
		std::shared_ptr<Graphics::IRenderer2D> Renderer;
		std::shared_ptr<Graphics::ISampler> Sampler;
		std::shared_ptr<Graphics::ICompositor2D> Compositor;
		std::shared_ptr<Graphics::IGeometry2D> Fullscreen;
		std::vector<std::shared_ptr<Graphics::ITexture>> Pool;
		std::vector<FCommand> Commands;
		std::vector<std::uintptr_t> Stack{0};
		std::int64_t Width = 0, Height = 0, PoolWidth = 0, PoolHeight = 0;
		std::size_t LayerCount = 1;
		bool bAvailable = false;
		bool bDirectMasks = false;
	};
} // namespace VeroScript::RmlUi
