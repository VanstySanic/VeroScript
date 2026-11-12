// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#if VEROSCRIPT_SDL3_GPU
#include "VeroScriptSdl3Renderer2D.h"
#endif
#include "VeroScriptGraphics.h"
#include "Graphics/VeroScriptSdl3GraphicsBackend.h"
#include <cstring>
#include <limits>

namespace VeroScript::Sdl3
{
	namespace
	{
		Graphics::FDeviceOptions ResolveDeviceOptions(const Graphics::FDeviceOptions& Requested)
		{
			auto Options = Requested;
			if (Options.Driver.empty())
			{
				#if defined(SDL_PLATFORM_EMSCRIPTEN) && VEROSCRIPT_SDL3_WEBGPU
				Options.Driver = "webgpu";
				#elif !VEROSCRIPT_SDL3_GPU
				#if defined(SDL_PLATFORM_WINDOWS) && VEROSCRIPT_SDL3_D3D11
				Options.Driver = "direct3d11";
				#elif VEROSCRIPT_SDL3_OPENGL
				#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_EMSCRIPTEN) || defined(SDL_PLATFORM_IOS) || \
    defined(SDL_PLATFORM_TVOS)
				Options.Driver = "opengles";
				#else
				Options.Driver = "opengl";
				#endif
				#elif VEROSCRIPT_SDL3_WEBGPU
				Options.Driver = "webgpu";
				#endif
				#endif
			}
			return Options;
		}
		#if VEROSCRIPT_SDL3_GPU
		bool RecordClear(FCommand& Command, SDL_GPUTexture* Texture, const Graphics::FColor& Color, std::string& Error)
		{
			SDL_GPUColorTargetInfo Target{};
			Target.texture = Texture;
			Target.clear_color = {Color.R, Color.G, Color.B, Color.A};
			Target.load_op = SDL_GPU_LOADOP_CLEAR;
			Target.store_op = SDL_GPU_STOREOP_STORE;
			auto* Pass = SDL_BeginGPURenderPass(Command.Native, &Target, 1, nullptr);
			if (!Pass)
			{
				return SdlError("begin clear pass", Error);
			}
			SDL_EndGPURenderPass(Pass);
			return true;
		}

		class FDevice final : public Graphics::IDevice
		{
		public:
			//~ Begin Graphics::IDevice Interface
			bool IsOpen() const noexcept override
			{
				return State->bOpen.load();
			}
			std::string GetDriver() const override
			{
				return State->Driver;
			}
			Graphics::FCapabilities GetCapabilities() const noexcept override
			{
				return {true, true, true, true, true, true, true, true};
			}

			std::shared_ptr<Graphics::ITexture> CreateTexture(const Graphics::FTextureOptions& Options,
			    std::string& Error) override
			{
				if (!State->Check(Error))
				{
					return {};
				}
				std::uint64_t Bytes = 0;
				if (!Graphics::ValidateTextureOptions(Options, State->Options.MaxTextureBytes, Bytes, Error))
				{
					return {};
				}
				SDL_GPUTextureCreateInfo Info{};
				Info.type = SDL_GPU_TEXTURETYPE_2D;
				Info.format = Options.Format == Graphics::ETextureFormat::Rgba8Srgb
				    ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
				    : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
				Info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
				Info.width = static_cast<Uint32>(Options.Width);
				Info.height = static_cast<Uint32>(Options.Height);
				Info.layer_count_or_depth = 1;
				Info.num_levels = 1;
				Info.sample_count = SDL_GPU_SAMPLECOUNT_1;
				auto Texture = std::make_shared<FTexture>(State, Options);
				Texture->Retirement->Native = SDL_CreateGPUTexture(State->Retirement->Native, &Info);
				if (!Texture->Retirement->Native)
				{
					SdlError("create texture", Error);
					return {};
				}
				++State->Resources;
				Texture->bOpen.store(true);
				Error.clear();
				return Texture;
			}

			std::shared_ptr<Graphics::ISurface> CreateSurface(std::shared_ptr<Platform::IWindow> Window,
			    std::string& Error) override
			{
				if (!State->Check(Error))
				{
					return {};
				}
				const auto Concrete = std::dynamic_pointer_cast<FWindow>(Window);
				if (!Concrete || Concrete->GetState()->Runtime != State->Runtime)
				{
					Error = "graphics.incompatible_window: expected a window from this SDL runtime";
					return {};
				}
				auto NativeWindow = Concrete->GetState();
				if (!NativeWindow->Check(Error))
				{
					return {};
				}
				if (NativeWindow->Claims || NativeWindow->OpenGlContextClaims)
				{
					Error = "graphics.surface_in_use: window already has a graphics surface";
					return {};
				}
				auto Surface = std::make_shared<FSurface>(State);
				if (!SDL_ClaimWindowForGPUDevice(State->Retirement->Native, NativeWindow->Retirement->Native))
				{
					SdlError("claim window for GPU", Error);
					return {};
				}
				Surface->Retirement->Window = NativeWindow;
				Surface->WindowId = NativeWindow->Id;
				++NativeWindow->Claims;
				++State->Resources;
				NativeWindow->Claim = Surface->Retirement.get();
				Surface->bOpen.store(true);
				// sRGB attachments keep blending and clear values in the public linear color space.
				if (!SDL_WindowSupportsGPUSwapchainComposition(State->Retirement->Native,
				        NativeWindow->Retirement->Native,
				        SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR))
				{
					Error = "graphics.unsupported_surface: linear SDR composition is unavailable";
					return {};
				}
				if (!SDL_SetGPUSwapchainParameters(State->Retirement->Native, NativeWindow->Retirement->Native,
				        SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, SDL_GPU_PRESENTMODE_VSYNC))
				{
					SdlError("configure linear SDR surface", Error);
					return {};
				}
				Error.clear();
				return Surface;
			}

			bool ClearTexture(const std::shared_ptr<Graphics::ITexture>& Texture, const Graphics::FColor& Color,
			    std::string& Error) override
			{
				if (!State->Check(Error) || !Graphics::ValidateColor(Color, Error))
				{
					return false;
				}
				const auto Concrete = GetTexture(Texture, Error);
				if (!Concrete)
				{
					return false;
				}
				FCommand Command{SDL_AcquireGPUCommandBuffer(State->Retirement->Native)};
				if (!Command.Native)
				{
					return SdlError("acquire clear command", Error);
				}
				if (!RecordClear(Command, Concrete->Retirement->Native, Color, Error))
				{
					return false;
				}
				if (!SDL_SubmitGPUCommandBuffer(Command.Take()))
				{
					return SdlError("submit clear", Error);
				}
				Error.clear();
				return true;
			}

			bool Present(const std::shared_ptr<Graphics::ISurface>& Surface, const Graphics::FColor& Color,
			    bool& bPresented, std::string& Error) override
			{
				bPresented = false;
				if (!State->Check(Error) || !Graphics::ValidateColor(Color, Error))
				{
					return false;
				}
				const auto Concrete = std::dynamic_pointer_cast<FSurface>(Surface);
				if (!Concrete || !Concrete->IsOpen() || Concrete->Retirement->Device != State)
				{
					Error = "graphics.invalid_surface: surface is closed or belongs to another device";
					return false;
				}
				FCommand Command{SDL_AcquireGPUCommandBuffer(State->Retirement->Native)};
				if (!Command.Native)
				{
					return SdlError("acquire present command", Error);
				}
				SDL_GPUTexture* Target = nullptr;
				if (!SDL_AcquireGPUSwapchainTexture(Command.Native, Concrete->Retirement->Window->Retirement->Native,
				        &Target, nullptr, nullptr))
				{
					return SdlError("acquire swapchain", Error);
				}
				if (!Target)
				{
					bPresented = false;
					Error.clear();
					return true;
				}
				Command.bSwapchainAcquired = true;
				if (!RecordClear(Command, Target, Color, Error))
				{
					return false;
				}
				if (!SDL_SubmitGPUCommandBuffer(Command.Take()))
				{
					return SdlError("submit present", Error);
				}
				bPresented = true;
				Error.clear();
				return true;
			}

			bool ReadTexture(const std::shared_ptr<Graphics::ITexture>& Texture, std::vector<std::uint8_t>& Result,
			    std::string& Error) override
			{
				if (!State->Check(Error))
				{
					return false;
				}
				const auto Concrete = GetTexture(Texture, Error);
				if (!Concrete)
				{
					return false;
				}
				std::uint64_t Bytes = 0;
				if (!Graphics::ValidateTextureOptions(Concrete->Options, State->Options.MaxTextureBytes, Bytes, Error))
				{
					return false;
				}
				const auto RowBytes = static_cast<std::uint64_t>(Concrete->Options.Width) * 4;
				const auto RowPitch = (RowBytes + 255) & ~std::uint64_t{255};
				const auto TransferBytes = RowPitch * static_cast<std::uint64_t>(Concrete->Options.Height);
				if (TransferBytes > State->Options.MaxTextureBytes ||
				    TransferBytes > std::numeric_limits<Uint32>::max())
				{
					Error = "graphics.resource_limit: aligned readback exceeds the transfer byte limit";
					return false;
				}
				std::vector<std::uint8_t> Copy(static_cast<std::size_t>(Bytes));
				SDL_GPUTransferBufferCreateInfo Info{};
				Info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
				Info.size = static_cast<Uint32>(TransferBytes);
				auto* NativeDevice = State->Retirement->Native;
				const auto DeleteBuffer = [NativeDevice](SDL_GPUTransferBuffer* Buffer)
				{
					SDL_ReleaseGPUTransferBuffer(NativeDevice, Buffer);
				};
				std::unique_ptr<SDL_GPUTransferBuffer, decltype(DeleteBuffer)> Buffer(
				    SDL_CreateGPUTransferBuffer(NativeDevice, &Info), DeleteBuffer);
				if (!Buffer)
				{
					return SdlError("create readback buffer", Error);
				}
				FCommand Command{SDL_AcquireGPUCommandBuffer(NativeDevice)};
				if (!Command.Native)
				{
					return SdlError("acquire readback command", Error);
				}
				auto* Pass = SDL_BeginGPUCopyPass(Command.Native);
				if (!Pass)
				{
					return SdlError("begin readback copy", Error);
				}
				SDL_GPUTextureRegion Source{};
				Source.texture = Concrete->Retirement->Native;
				Source.w = static_cast<Uint32>(Concrete->Options.Width);
				Source.h = static_cast<Uint32>(Concrete->Options.Height);
				Source.d = 1;
				SDL_GPUTextureTransferInfo Destination{};
				Destination.transfer_buffer = Buffer.get();
				Destination.pixels_per_row = static_cast<Uint32>(RowPitch / 4);
				Destination.rows_per_layer = Source.h;
				SDL_DownloadFromGPUTexture(Pass, &Source, &Destination);
				SDL_EndGPUCopyPass(Pass);
				auto* Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(Command.Take());
				if (!Fence)
				{
					return SdlError("submit readback", Error);
				}
				const bool bComplete = SDL_WaitForGPUFences(NativeDevice, true, &Fence, 1);
				SDL_ReleaseGPUFence(NativeDevice, Fence);
				if (!bComplete)
				{
					return SdlError("wait for readback", Error);
				}
				void* Data = SDL_MapGPUTransferBuffer(NativeDevice, Buffer.get(), false);
				if (!Data)
				{
					return SdlError("map readback", Error);
				}
				for (std::uint64_t Row = 0; Row < Source.h; ++Row)
				{
					std::memcpy(Copy.data() + Row * RowBytes, static_cast<const std::uint8_t*>(Data) + Row * RowPitch,
					    static_cast<std::size_t>(RowBytes));
				}
				SDL_UnmapGPUTransferBuffer(NativeDevice, Buffer.get());
				Result.swap(Copy);
				Error.clear();
				return true;
			}

			bool WaitIdle(std::string& Error) override
			{
				if (!State->Check(Error))
				{
					return false;
				}
				State->Runtime->Drain();
				if (!SDL_WaitForGPUIdle(State->Retirement->Native))
				{
					return SdlError("wait for GPU", Error);
				}
				Error.clear();
				return true;
			}

			bool Close(std::string& Error) override
			{
				if (!State->Runtime->CheckThread(Error))
				{
					return false;
				}
				State->Runtime->Drain();
				if (State->Resources)
				{
					Error = "graphics.resources_alive: close graphics resources before the device";
					return false;
				}
				State->Retirement->Release();
				State->bOpen.store(false);
				Error.clear();
				return true;
			}
			std::shared_ptr<Graphics::ISampler> CreateSampler(const Graphics::FSamplerOptions& Options,
			    std::string& Error) override
			{
				return CreateSamplerImplementation(State, Options, Error);
			}
			bool UpdateTexture(const std::shared_ptr<Graphics::ITexture>& Texture, std::int64_t X, std::int64_t Y,
			    std::int64_t Width, std::int64_t Height, const std::vector<std::uint8_t>& Pixels,
			    std::string& Error) override
			{
				return UpdateTextureImplementation(State, Texture, X, Y, Width, Height, Pixels, Error);
			}
			std::shared_ptr<Graphics::IRenderer2D> CreateRenderer2D(std::string& Error) override
			{
				return CreateRenderer2DImplementation(State, Error);
			}
			std::uint64_t GetGeneration() const noexcept override
			{
				return State->Generation;
			}
			bool Invalidate(std::string& Error) override
			{
				if (!State->Runtime->CheckThread(Error))
				{
					return false;
				}
				State->Runtime->Drain();
				if (!State->Retirement->Native)
				{
					Error = "graphics.closed_device: a closed device cannot be invalidated";
					return false;
				}
				State->bInvalidated.store(true);
				State->bOpen.store(false);
				Error.clear();
				return true;
			}
			bool Poll(std::string& Error) override
			{
				if (!State->Check(Error))
				{
					return false;
				}
				State->Runtime->Drain();
				Error.clear();
				return true;
			}
			//~ End Graphics::IDevice Interface

			explicit FDevice(std::shared_ptr<FDeviceState> InState) : State(std::move(InState))
			{
			}

		private:
			std::shared_ptr<FTexture> GetTexture(const std::shared_ptr<Graphics::ITexture>& Texture,
			    std::string& Error) const
			{
				auto Concrete = std::dynamic_pointer_cast<FTexture>(Texture);
				if (!Concrete || !Concrete->IsOpen() || Concrete->Retirement->Device != State)
				{
					Error = "graphics.invalid_texture: texture is closed or belongs to another device";
					return {};
				}
				return Concrete;
			}
			std::shared_ptr<FDeviceState> State;
		};

		#endif
		class FGraphicsService final : public Graphics::IService
		{
		public:
			~FGraphicsService() override
			{
				Runtime->Retire(std::move(Lease));
			}
			std::shared_ptr<Graphics::IDevice> CreateDevice(const Graphics::FDeviceOptions& Requested,
			    std::string& Error) override
			{
				const auto Options = ResolveDeviceOptions(Requested);
				if (!Runtime->CheckThread(Error))
				{
					return {};
				}
				Runtime->Drain();
				// SDL_GPU's native drivers need SDL's video driver, even for offscreen targets.
				if (!Runtime->EnsureVideo(Error))
				{
					return {};
				}
				if (Options.Driver.find('\0') != std::string::npos || !Options.MaxTextureBytes ||
				    !Options.MaxGeometryBytes)
				{
					Error = "graphics.invalid_device: invalid driver or zero resource byte limit";
					return {};
				}
				if (Options.Driver == "direct3d11" || Options.Driver == "direct3d11-warp" ||
				    Options.Driver == "opengl" || Options.Driver == "opengles" || Options.Driver == "webgpu")
				{
					return CreateAdditionalDevice(Runtime, Options, Error);
				}
				#if VEROSCRIPT_SDL3_GPU
				auto State = std::make_shared<FDeviceState>(Runtime, Options);
				State->Generation = Runtime->NextDeviceGeneration(Error);
				if (!State->Generation)
				{
					return {};
				}
				const auto Formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
				    SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_MSL |
				    SDL_GPU_SHADERFORMAT_METALLIB;
				State->Retirement->Native = SDL_CreateGPUDevice(
				    Formats, Options.bDebug, Options.Driver.empty() ? nullptr : Options.Driver.c_str());
				if (!State->Retirement->Native)
				{
					SdlError("create GPU device", Error);
					return {};
				}
				State->bOpen.store(true);
				State->Driver = SDL_GetGPUDeviceDriver(State->Retirement->Native);
				Error.clear();
				return std::make_shared<FDevice>(std::move(State));
				#else
				Error = "graphics.unsupported_driver: SDL_GPU is disabled; select an enabled graphics API";
				return {};
				#endif
			}
			std::shared_ptr<Graphics::IDeviceRequest> CreateDeviceAsync(const Graphics::FDeviceOptions& Options,
			    std::string& Error) override
			{
				const auto Effective = ResolveDeviceOptions(Options);
				if (Effective.Driver != "webgpu")
				{
					return Graphics::IService::CreateDeviceAsync(Effective, Error);
				}
				if (!Runtime->CheckThread(Error) || !Runtime->EnsureVideo(Error))
				{
					return {};
				}
				Runtime->Drain();
				if (!Options.MaxTextureBytes || !Options.MaxGeometryBytes)
				{
					Error = "graphics.invalid_device: zero resource byte limit";
					return {};
				}
				return CreateWebGpuDeviceAsync(Runtime, Effective, Error);
			}

			explicit FGraphicsService(std::shared_ptr<FRuntime> InRuntime)
			    : Runtime(std::move(InRuntime)), Lease(MakeServiceLease(Runtime))
			{
			}

		private:
			std::shared_ptr<FRuntime> Runtime;
			std::unique_ptr<FRetirement> Lease;
		};
	}

	std::shared_ptr<Graphics::IService> CreateGraphicsImplementation(std::shared_ptr<const void> CodeOwner,
	    std::string& Error)
	{
		auto Runtime = AcquireRuntime(std::move(CodeOwner), Error);
		if (!Runtime)
		{
			return {};
		}
		Error.clear();
		return std::make_shared<FGraphicsService>(std::move(Runtime));
	}
}
