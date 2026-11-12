// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGraphics2D.h"
#include "VeroScriptFontLayout.h"
#include "VeroScriptGuiControls.h"
#include "VeroScriptGuiPaint.h"
#include "VeroScriptGuiResources.h"
#include "VeroScriptGuiTypes.h"
#include "VeroScriptGuiVersion.h"
#include <optional>
#include <vector>

namespace VeroScript::Gui
{
	inline constexpr const char* ServiceId = "gui";
	inline constexpr const char* ContractId = "veroscript.gui";
	inline constexpr const char *ContractVersion = Version;

	struct FBackendNode
	{
		std::uint64_t Id = 0, Parent = 0;
		FResolvedProperties Properties;
		FPaintCallback Paint;
		FControlOptions Control;
	};

	struct FLayoutBox
	{
		std::uint64_t Id = 0;
		FRect Border, Content;
		// Extents share Content's coordinate space; padding is not scrollable overflow.
		double ScrollWidth = 0, ScrollHeight = 0, ScrollX = 0, ScrollY = 0;
	};

	struct FCapabilities
	{
		bool bLayout = false, bRendering = false, bText = false;
		bool bClipMasks = false, bFilters = false, bGroupOpacity = false;
		bool bScrolling = false, bTextEditing = false;
		bool bExtendedControls = false;
	};

	/** One owner thread. Synchronize consumes a complete parent-before-child value snapshot. */
	class IBackendContext
	{
	public:
		virtual ~IBackendContext() = default;
		virtual FCapabilities GetCapabilities() const noexcept = 0;
		virtual bool Synchronize(const FViewport& Viewport, const std::vector<FBackendNode>& Nodes,
								 std::string& Error) = 0;
		virtual bool Update(double ElapsedSeconds, std::vector<FLayoutBox>& Layout, std::string& Error) = 0;
		virtual bool Render(bool& bPresented, std::string& Error) = 0;
		virtual bool Close(std::string& Error) = 0;
		// nullopt delegates geometry hit testing to the framework's rectangular fallback.
		virtual std::optional<bool> Contains(std::uint64_t, double, double) const
		{
			return {};
		}
		virtual bool SetResourceReader(FResourceReader, std::string& Error)
		{
			// Backends without external resources may accept this optional configuration unused.
			Error.clear();
			return true;
		}
		virtual bool LoadFont(const FFontFace&, std::string& Error)
		{
			Error = "gui.fonts_unavailable";
			return false;
		}
		virtual bool FocusTextControl(std::uint64_t Id, const FTextInputServices &, std::string &Error)
		{
			if (Id)
			{
				Error = "gui.text_editing_unavailable";
				return false;
			}
			return true;
		}
		virtual bool EditText(std::uint64_t, const Platform::FEvent &, FTextEditResult &, std::string &Error)
		{
			Error = "gui.text_editing_unavailable";
			return false;
		}
		virtual bool IsTextComposing(std::uint64_t) const
		{
			return false;
		}
		virtual bool SetTextSelection(std::uint64_t, std::int64_t, std::int64_t, std::string &Error)
		{
			Error = "gui.text_editing_unavailable";
			return false;
		}
	};

	struct FBackendOptions
	{
		std::shared_ptr<Graphics::IDevice> Device;
		std::shared_ptr<Graphics::ITexture> Texture;
		std::shared_ptr<Graphics::ISurface> Surface;
		Graphics::FRenderOptions2D RenderOptions;
		std::uint64_t MaxCompositionBytes = 256 * 1024 * 1024;
		// Provider implementations retain this lease through deferred cleanup.
		std::shared_ptr<const void> Owner;
		FResourceReader ReadResource;
		FImageDecoder DecodeImage;
		std::shared_ptr<Image::IService> Images;
		std::shared_ptr<Font::IService> Fonts;
		std::shared_ptr<Font::ITextLayoutService> TextLayouts;
	};
	class IBackendService
	{
	public:
		virtual ~IBackendService() = default;
		virtual std::unique_ptr<IBackendContext> Create(const FBackendOptions& Options, std::string& Error) = 0;
		virtual bool CollectRetired(std::string& Error)
		{
			Error.clear();
			return true;
		}
	};
} // namespace VeroScript::Gui
