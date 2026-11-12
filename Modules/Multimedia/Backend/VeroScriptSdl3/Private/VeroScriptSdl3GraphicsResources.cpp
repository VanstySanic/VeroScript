// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3GraphicsResources.h"
#include <cstring>
#include <limits>

namespace VeroScript::Sdl3
{
	namespace
	{
		struct FSamplerRetirement final : FRetirement
		{
			void Release() noexcept override
			{
				if (Native)
				{
					SDL_ReleaseGPUSampler(Device->Retirement->Native, Native);
					Native = nullptr;
					--Device->Resources;
				}
			}
			std::shared_ptr<FDeviceState> Device;
			SDL_GPUSampler* Native = nullptr;
		};
		class FSampler final : public Graphics::ISampler
		{
		public:
			//~ Begin Graphics::ISampler Interface
			~FSampler() override
			{
				Retirement->Runtime->Retire(std::move(Retirement));
			}
			bool IsOpen() const noexcept override
			{
				return bOpen.load() && Retirement->Device->bOpen.load();
			}
			Graphics::FSamplerOptions GetOptions() const noexcept override
			{
				return Options;
			}
			bool Close(std::string& Error) override
			{
				if (!Retirement->Runtime->CheckThread(Error))
				{
					return false;
				}
				Retirement->Release();
				bOpen.store(false);
				Error.clear();
				return true;
			}
			//~ End Graphics::ISampler Interface

			FSampler(const std::shared_ptr<FDeviceState>& State, Graphics::FSamplerOptions InOptions)
			    : Retirement(std::make_unique<FSamplerRetirement>()), Options(InOptions)
			{
				Retirement->Runtime = State->Runtime;
				Retirement->Device = State;
			}
			std::unique_ptr<FSamplerRetirement> Retirement;
			Graphics::FSamplerOptions Options;
			std::atomic<bool> bOpen{false};
		};
	}
	void FDeviceRetirement::Release() noexcept
	{
		if (Native)
		{
			SDL_DestroyGPUDevice(Native);
			Native = nullptr;
		}
	}
	FDeviceState::FDeviceState(std::shared_ptr<FRuntime> R, Graphics::FDeviceOptions O)
	    : Runtime(std::move(R)), Retirement(std::make_unique<FDeviceRetirement>()), Options(std::move(O))
	{
		Retirement->Runtime = Runtime;
	}
	FDeviceState::~FDeviceState()
	{
		Runtime->Retire(std::move(Retirement));
	}
	bool FDeviceState::Check(std::string& Error) const
	{
		if (!Runtime->CheckThread(Error))
		{
			return false;
		}
		if (!bOpen.load())
		{
			Error = bInvalidated.load() ? "graphics.device_invalidated: recreate resources on a replacement device"
			                            : "graphics.closed_device: device is closed";
			return false;
		}
		return true;
	}
	void FTextureRetirement::Release() noexcept
	{
		if (Native)
		{
			SDL_ReleaseGPUTexture(Device->Retirement->Native, Native);
			Native = nullptr;
			--Device->Resources;
		}
	}
	void FSurfaceRetirement::Release() noexcept
	{
		if (Window)
		{
			if (Window->Claim == this)
			{
				Window->Claim = nullptr;
			}
			if (Window->Retirement->Native)
			{
				SDL_ReleaseWindowFromGPUDevice(Device->Retirement->Native, Window->Retirement->Native);
			}
			--Window->Claims;
			--Device->Resources;
			Window.reset();
		}
	}
	FCommand::FCommand(SDL_GPUCommandBuffer* Value) : Native(Value)
	{
	}
	FCommand::~FCommand()
	{
		if (Native)
		{
			// SDL requires submission after acquisition of a swapchain texture.
			if (bSwapchainAcquired)
			{
				SDL_SubmitGPUCommandBuffer(Native);
			}
			else
			{
				SDL_CancelGPUCommandBuffer(Native);
			}
		}
	}
	SDL_GPUCommandBuffer* FCommand::Take() noexcept
	{
		auto* Result = Native;
		Native = nullptr;
		return Result;
	}
	FTexture::~FTexture()
	{
		Retirement->Runtime->Retire(std::move(Retirement));
	}
	bool FTexture::IsOpen() const noexcept
	{
		return bOpen.load() && Retirement->Device->bOpen.load();
	}
	Graphics::FTextureOptions FTexture::GetOptions() const noexcept
	{
		return Options;
	}
	bool FTexture::Close(std::string& Error)
	{
		if (!Retirement->Runtime->CheckThread(Error))
		{
			return false;
		}
		Retirement->Release();
		bOpen.store(false);
		Error.clear();
		return true;
	}
	FTexture::FTexture(std::shared_ptr<FDeviceState> Device, Graphics::FTextureOptions O)
	    : Retirement(std::make_unique<FTextureRetirement>()), Options(O)
	{
		Retirement->Device = std::move(Device);
		Retirement->Runtime = Retirement->Device->Runtime;
	}
	FSurface::~FSurface()
	{
		Retirement->Runtime->Retire(std::move(Retirement));
	}
	bool FSurface::IsOpen() const noexcept
	{
		return bOpen.load() && Retirement->Device->bOpen.load() && Retirement->Window &&
		    Retirement->Window->bOpen.load();
	}
	std::uint64_t FSurface::GetWindowId() const noexcept
	{
		return WindowId;
	}
	bool FSurface::Close(std::string& Error)
	{
		if (!Retirement->Runtime->CheckThread(Error))
		{
			return false;
		}
		Retirement->Release();
		bOpen.store(false);
		Error.clear();
		return true;
	}
	FSurface::FSurface(std::shared_ptr<FDeviceState> Device) : Retirement(std::make_unique<FSurfaceRetirement>())
	{
		Retirement->Device = std::move(Device);
		Retirement->Runtime = Retirement->Device->Runtime;
	}
	std::shared_ptr<Graphics::ISampler> CreateSamplerImplementation(const std::shared_ptr<FDeviceState>& State,
	    const Graphics::FSamplerOptions& Options,
	    std::string& Error)
	{
		if (!State->Check(Error) || !Graphics::ValidateSamplerOptions(Options, Error))
		{
			return {};
		}
		auto Sampler = std::make_shared<FSampler>(State, Options);
		SDL_GPUSamplerCreateInfo Info{};
		Info.min_filter = Info.mag_filter =
		    Options.Filter == Graphics::EFilter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
		Info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		const auto Address = [](Graphics::EAddressMode Mode)
		{
			return Mode == Graphics::EAddressMode::Repeat ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
			    : Mode == Graphics::EAddressMode::Mirror  ? SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT
			                                              : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		};
		Info.address_mode_u = Address(Options.AddressU);
		Info.address_mode_v = Address(Options.AddressV);
		Info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		Sampler->Retirement->Native = SDL_CreateGPUSampler(State->Retirement->Native, &Info);
		if (!Sampler->Retirement->Native)
		{
			SdlError("create sampler", Error);
			return {};
		}
		++State->Resources;
		Sampler->bOpen.store(true);
		Error.clear();
		return Sampler;
	}
	bool UpdateTextureImplementation(const std::shared_ptr<FDeviceState>& State,
	    const std::shared_ptr<Graphics::ITexture>& Texture, std::int64_t X, std::int64_t Y,
	    std::int64_t Width, std::int64_t Height, const std::vector<std::uint8_t>& Pixels,
	    std::string& Error)
	{
		if (!State->Check(Error))
		{
			return false;
		}
		const auto Resource = std::dynamic_pointer_cast<FTexture>(Texture);
		if (!Resource || !Resource->IsOpen() || Resource->Retirement->Device != State)
		{
			Error = "graphics.invalid_texture: expected an open texture from this device";
			return false;
		}
		const auto Size = Resource->Options;
		if (X < 0 || Y < 0 || Width <= 0 || Height <= 0 || X > Size.Width || Y > Size.Height ||
		    Width > Size.Width - X || Height > Size.Height - Y)
		{
			Error = "graphics.invalid_upload: region is outside the texture";
			return false;
		}
		const auto RowBytes = static_cast<std::uint64_t>(Width) * 4;
		const auto Pitch = (RowBytes + 255) & ~std::uint64_t{255};
		const auto Bytes = Pitch * static_cast<std::uint64_t>(Height);
		if (Pixels.size() != RowBytes * Height || Bytes > State->Options.MaxTextureBytes ||
		    Bytes > std::numeric_limits<Uint32>::max())
		{
			Error = "graphics.invalid_upload: byte count or aligned transfer exceeds limits";
			return false;
		}
		auto* Device = State->Retirement->Native;
		const auto Delete = [Device](SDL_GPUTransferBuffer* Value)
		{
			SDL_ReleaseGPUTransferBuffer(Device, Value);
		};
		SDL_GPUTransferBufferCreateInfo Info{};
		Info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		Info.size = static_cast<Uint32>(Bytes);
		std::unique_ptr<SDL_GPUTransferBuffer, decltype(Delete)> Buffer(SDL_CreateGPUTransferBuffer(Device, &Info),
		    Delete);
		if (!Buffer)
		{
			return SdlError("create upload transfer", Error);
		}
		void* Data = SDL_MapGPUTransferBuffer(Device, Buffer.get(), false);
		if (!Data)
		{
			return SdlError("map upload transfer", Error);
		}
		for (std::int64_t Row = 0; Row < Height; ++Row)
		{
			std::memcpy(static_cast<std::uint8_t*>(Data) + Row * Pitch, Pixels.data() + Row * RowBytes,
			    static_cast<std::size_t>(RowBytes));
		}
		SDL_UnmapGPUTransferBuffer(Device, Buffer.get());
		FCommand Command{SDL_AcquireGPUCommandBuffer(Device)};
		if (!Command.Native)
		{
			return SdlError("acquire upload command", Error);
		}
		auto* Pass = SDL_BeginGPUCopyPass(Command.Native);
		if (!Pass)
		{
			return SdlError("begin upload copy", Error);
		}
		SDL_GPUTextureTransferInfo Source{};
		Source.transfer_buffer = Buffer.get();
		Source.pixels_per_row = static_cast<Uint32>(Pitch / 4);
		Source.rows_per_layer = static_cast<Uint32>(Height);
		SDL_GPUTextureRegion Target{};
		Target.texture = Resource->Retirement->Native;
		Target.x = static_cast<Uint32>(X);
		Target.y = static_cast<Uint32>(Y);
		Target.w = static_cast<Uint32>(Width);
		Target.h = static_cast<Uint32>(Height);
		Target.d = 1;
		SDL_UploadToGPUTexture(Pass, &Source, &Target, false);
		SDL_EndGPUCopyPass(Pass);
		if (!SDL_SubmitGPUCommandBuffer(Command.Take()))
		{
			return SdlError("submit texture upload", Error);
		}
		Error.clear();
		return true;
	}
	SDL_GPUSampler* GetNativeSampler(const std::shared_ptr<Graphics::ISampler>& Sampler,
	    const std::shared_ptr<FDeviceState>& State, std::string& Error)
	{
		const auto Concrete = std::dynamic_pointer_cast<FSampler>(Sampler);
		if (!Concrete || !Concrete->IsOpen() || Concrete->Retirement->Device != State)
		{
			Error = "graphics.invalid_sampler: expected an open sampler from this device";
			return nullptr;
		}
		return Concrete->Retirement->Native;
	}
}
