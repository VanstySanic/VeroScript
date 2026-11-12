// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiContext.h"
#include "VeroScriptGraphics.h"
#include <atomic>
#include <chrono>

namespace VeroScript::Gui
{
	struct FApplicationOptions
	{
		Platform::FWindowOptions Window;
		Graphics::FDeviceOptions Device;
		// Empty inherits the environment; "auto" selects a candidate and "none" disables.
		std::string PlatformBackend, GraphicsBackend, GuiBackend;
		std::string ImageBackend, FontBackend, TextLayoutBackend;
		// Maximum idle frame interval; Run subtracts the preceding frame work before waiting.
		std::int64_t WaitMilliseconds = 16;
		std::size_t MaxEventsPerFrame = 4096;
		bool bRendering = true;
	};

	struct FApplicationFactories
	{
		std::function<std::shared_ptr<Platform::IService>(const std::string&, std::string&)> Platform;
		std::function<std::shared_ptr<Graphics::IService>(const std::string&, std::string&)> Graphics;
		Image::FServiceFactory Image;
		Font::FServiceFactory Font;
		Font::FTextLayoutServiceFactory TextLayout;
		std::function<std::unique_ptr<IBackendContext>(const std::string&, const FBackendOptions&, std::string&)> Gui;
		std::function<std::string(std::string_view)> DefaultBackend;
	};

	/** Optional standalone assembly. A host driving existing windows uses FContext directly.
	 * Each application has
	 * one event pump and owns its window/surface/device. Factories may
	 * share their provider implementation, but
	 * must return resources with compatible contracts. */
	class FApplication final : public std::enable_shared_from_this<FApplication>
	{
	public:
		~FApplication();
		FApplication(const FApplication&) = delete;
		FApplication& operator=(const FApplication&) = delete;
		static std::shared_ptr<FApplication> Create(const FApplicationOptions& Options,
													const FApplicationFactories& Factories, std::string& Error);
		static bool CollectRetired(std::string& Error);
		bool SetRoot(std::shared_ptr<FWidget> Root, std::string& Error);
		std::shared_ptr<FContext> GetContext() const;
		std::shared_ptr<Platform::IWindow> GetWindow() const;
		using FBeforeUpdate = std::function<bool(std::string&)>;
		// The callback is borrowed only for this invocation, after events and before GUI evaluation.
		bool Tick(std::string& Error, const FBeforeUpdate& BeforeUpdate = {});
		bool Run(std::string& Error, const FBeforeUpdate& BeforeUpdate = {});
		void RequestStop() noexcept;
		// Provider cleanup exceptions become errors; unfinished resources remain owned for retry.
		bool Close(std::string& Error);
		bool IsOpen() const noexcept;

	private:
		explicit FApplication(FApplicationOptions Options);
		bool Check(std::string& Error) const;
		bool RefreshMetrics(std::string& Error);
		bool Route(const Platform::FEvent& Event, std::string& Error);
		bool TickFrame(const Platform::FEvent* First, std::string& Error, const FBeforeUpdate& BeforeUpdate);
		bool CloseOwned(std::string& Error);
		double Elapsed() const;

		std::thread::id Thread;
		FApplicationOptions Options;
		std::chrono::steady_clock::time_point Started;
		std::shared_ptr<Platform::IService> PlatformService;
		std::shared_ptr<Graphics::IService> GraphicsService;
		std::shared_ptr<Platform::IWindow> Window;
		std::shared_ptr<Graphics::IDevice> Device;
		std::shared_ptr<Graphics::ISurface> Surface;
		std::shared_ptr<FContext> Context;
		std::atomic<bool> bStopRequested{false};
		bool bInFrame = false, bRunning = false, bDrawable = false, bSuspended = false, bOwnsPumpLease = false;
	};
} // namespace VeroScript::Gui
