// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiBackend.h"
#include "VeroScriptGraphics2D.h"

namespace VeroScript::RmlUi
{
	/** Targets and device are borrowed. Close the GUI before closing them. */
	struct FOptions
	{
		std::shared_ptr<Graphics::IDevice> Device;
		std::shared_ptr<Graphics::ITexture> Texture;
		std::shared_ptr<Graphics::ISurface> Surface;
		Graphics::FRenderOptions2D RenderOptions;
		std::uint64_t MaxCompositionBytes = 256 * 1024 * 1024;
		std::shared_ptr<const void> Owner;
		Gui::FResourceReader ReadResource;
		Gui::FImageDecoder DecodeImage;
		std::shared_ptr<Image::IService> Images;
		std::shared_ptr<Font::IService> Fonts;
		std::shared_ptr<Font::ITextLayoutService> TextLayouts;
	};

	/** No device creates an explicit layout-only backend. No windows or devices are created. */
	std::unique_ptr<Gui::IBackendContext> CreateBackend(const FOptions& Options, std::string& Error);
	/** Reclaims retired backends on their creator thread; failed closes remain queued for retry. */
	bool CollectRetired(std::string& Error);
} // namespace VeroScript::RmlUi
