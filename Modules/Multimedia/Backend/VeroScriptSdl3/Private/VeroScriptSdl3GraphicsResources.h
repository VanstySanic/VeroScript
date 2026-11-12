// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSdl3Platform.h"
#include "VeroScriptGraphics2D.h"

namespace VeroScript::Sdl3
{
	struct FDeviceRetirement final : FRetirement
	{
		void Release() noexcept override;
		SDL_GPUDevice* Native = nullptr;
	};
	struct FDeviceState
	{
		FDeviceState(std::shared_ptr<FRuntime> Runtime, Graphics::FDeviceOptions Options);
		~FDeviceState();
		bool Check(std::string& Error) const;
		std::shared_ptr<FRuntime> Runtime;
		std::unique_ptr<FDeviceRetirement> Retirement;
		Graphics::FDeviceOptions Options;
		std::string Driver;
		std::uint64_t Resources = 0;
		std::uint64_t Generation = 0;
		std::atomic<bool> bOpen{false};
		std::atomic<bool> bInvalidated{false};
	};
	struct FTextureRetirement final : FRetirement
	{
		void Release() noexcept override;
		std::shared_ptr<FDeviceState> Device;
		SDL_GPUTexture* Native = nullptr;
	};
	struct FSurfaceRetirement final : FRetirement
	{
		void Release() noexcept override;
		std::shared_ptr<FDeviceState> Device;
		std::shared_ptr<FWindowState> Window;
	};
	struct FCommand
	{
		explicit FCommand(SDL_GPUCommandBuffer* Value);
		~FCommand();
		FCommand(const FCommand&) = delete;
		FCommand& operator=(const FCommand&) = delete;
		SDL_GPUCommandBuffer* Take() noexcept;
		SDL_GPUCommandBuffer* Native = nullptr;
		bool bSwapchainAcquired = false;
	};
	class FTexture final : public Graphics::ITexture
	{
	public:
		//~ Begin Graphics::ITexture Interface
		~FTexture() override;
		bool IsOpen() const noexcept override;
		Graphics::FTextureOptions GetOptions() const noexcept override;
		bool Close(std::string& Error) override;
		//~ End Graphics::ITexture Interface
		FTexture(std::shared_ptr<FDeviceState> Device, Graphics::FTextureOptions Options);
		std::unique_ptr<FTextureRetirement> Retirement;
		Graphics::FTextureOptions Options;
		std::atomic<bool> bOpen{false};
	};
	class FSurface final : public Graphics::ISurface
	{
	public:
		//~ Begin Graphics::ISurface Interface
		~FSurface() override;
		bool IsOpen() const noexcept override;
		std::uint64_t GetWindowId() const noexcept override;
		bool Close(std::string& Error) override;
		//~ End Graphics::ISurface Interface
		explicit FSurface(std::shared_ptr<FDeviceState> Device);
		std::unique_ptr<FSurfaceRetirement> Retirement;
		std::uint64_t WindowId = 0;
		std::atomic<bool> bOpen{false};
	};
	std::shared_ptr<Graphics::ISampler> CreateSamplerImplementation(const std::shared_ptr<FDeviceState>& State,
																	const Graphics::FSamplerOptions& Options,
																	std::string& Error);
	bool UpdateTextureImplementation(const std::shared_ptr<FDeviceState>& State,
									 const std::shared_ptr<Graphics::ITexture>& Texture, std::int64_t X, std::int64_t Y,
									 std::int64_t Width, std::int64_t Height, const std::vector<std::uint8_t>& Pixels,
									 std::string& Error);
	SDL_GPUSampler* GetNativeSampler(const std::shared_ptr<Graphics::ISampler>& Sampler,
									 const std::shared_ptr<FDeviceState>& State, std::string& Error);
}
