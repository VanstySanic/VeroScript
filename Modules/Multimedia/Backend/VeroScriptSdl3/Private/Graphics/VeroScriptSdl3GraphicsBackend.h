// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "../VeroScriptSdl3Platform.h"
#include "VeroScriptGraphics2D.h"

namespace VeroScript::Sdl3
{
	struct ITextureData
	{
		virtual ~ITextureData() = default;
	};
	struct ISamplerData
	{
		virtual ~ISamplerData() = default;
	};
	struct IGeometryData
	{
		virtual ~IGeometryData() = default;
	};
	struct ISurfaceData
	{
		virtual ~ISurfaceData() = default;
	};
	struct FBackendDraw
	{
		const Graphics::FDraw2D* Command = nullptr;
		IGeometryData* Geometry = nullptr;
		ITextureData* Texture = nullptr;
		ITextureData* Mask = nullptr;
		ISamplerData* Sampler = nullptr;
		Graphics::FTextureOptions Source;
		Graphics::FRect Clip;
	};
	struct alignas(16) FVertexUniform2D
	{
		Graphics::FTransform2D Transform;
		float Viewport[4]{};
	};
	struct alignas(16) FFragmentUniform2D
	{
		Graphics::FColor Tint;
		float Options[4]{};
		float Filter[4]{};
		Graphics::FColor EffectColor;
		float Rows[4][4]{};
		float Bias[4]{};
		float Output[4]{};
	};
	static_assert(sizeof(FVertexUniform2D) == 80 && sizeof(FFragmentUniform2D) == 160);
	/** Backend objects and native commands are confined to the SDL runtime owner thread. */
	class IGraphicsBackend
	{
	public:
		virtual ~IGraphicsBackend() = default;
		virtual const char* GetDriver() const noexcept = 0;
		virtual std::unique_ptr<ITextureData> CreateTexture(const Graphics::FTextureOptions&, std::string&) = 0;
		virtual std::unique_ptr<ISamplerData> CreateSampler(const Graphics::FSamplerOptions&, std::string&) = 0;
		virtual std::unique_ptr<IGeometryData> CreateGeometry(const std::vector<Graphics::FVertex2D>&,
		    const std::vector<std::uint32_t>&, std::string&) = 0;
		virtual std::unique_ptr<ISurfaceData> CreateSurface(SDL_Window*, std::string&) = 0;
		virtual bool UpdateTexture(ITextureData&, const Graphics::FRect&, const std::vector<std::uint8_t>&,
		    std::string&) = 0;
		virtual bool ReadTexture(ITextureData&, std::vector<std::uint8_t>&, std::string&) = 0;
		virtual bool Render(ITextureData*, ISurfaceData*, const Graphics::FRenderOptions2D&, const Graphics::FRect&,
		    const std::vector<FBackendDraw>&, bool&, std::string&) = 0;
		virtual bool WaitIdle(std::string&) = 0;
		virtual bool Poll(std::string& Error)
		{
			Error.clear();
			return true;
		}
		virtual bool IsLost() const noexcept
		{
			return false;
		}
		virtual void SetLossFlag(std::weak_ptr<std::atomic<bool>>)
		{
		}
		virtual std::shared_ptr<Graphics::IReadbackRequest> ReadTextureAsync(ITextureData&, std::string& Error)
		{
			Error = "graphics.unsupported_async_readback: use the device's diagnostic readback";
			return {};
		}
	};
	FVertexUniform2D MakeVertexUniform(const FBackendDraw&, const Graphics::FRect&);
	FFragmentUniform2D MakeFragmentUniform(const FBackendDraw&);
	std::shared_ptr<Graphics::IDevice> CreateBackendDevice(const std::shared_ptr<FRuntime>&,
	    const Graphics::FDeviceOptions&,
	    std::unique_ptr<IGraphicsBackend>, std::string&);
	std::shared_ptr<Graphics::IDevice> CreateAdditionalDevice(const std::shared_ptr<FRuntime>&,
	    const Graphics::FDeviceOptions&, std::string&);
	std::unique_ptr<IGraphicsBackend> CreateOpenGlBackend(const std::shared_ptr<FRuntime>&, const Graphics::FDeviceOptions&, std::string&);
	std::unique_ptr<IGraphicsBackend> CreateD3d11Backend(const Graphics::FDeviceOptions&, std::string&);
	std::shared_ptr<Graphics::IDeviceRequest> CreateWebGpuDeviceAsync(const std::shared_ptr<FRuntime>&,
	    const Graphics::FDeviceOptions&, std::string&);
}
