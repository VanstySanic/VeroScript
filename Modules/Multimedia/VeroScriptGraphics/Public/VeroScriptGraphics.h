// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript::Platform
{
	class IWindow;
}

namespace VeroScript::Graphics
{
	inline constexpr const char* ServiceId = "graphics";
	inline constexpr const char* ContractId = "veroscript.graphics";
	inline constexpr char ModuleVersion[] = "0.1.1";
	inline constexpr const char* ContractVersion = ModuleVersion;
	class IRenderer2D;
	class IDevice;
	class IDeviceRequest;
	class IReadbackRequest;

	enum class ERequestStatus : std::uint8_t
	{
		Pending,
		Succeeded,
		Failed,
		Cancelled
	};

	enum class ETextureFormat : std::uint8_t
	{
		Rgba8Unorm,
		Rgba8Srgb
	};

	enum class EFilter : std::uint8_t
	{
		Nearest,
		Linear
	};
	enum class EAddressMode : std::uint8_t
	{
		Clamp,
		Repeat,
		Mirror
	};

	struct FColor
	{
		float R = 0, G = 0, B = 0, A = 1;
	};

	struct FTextureOptions
	{
		std::int64_t Width = 1, Height = 1;
		ETextureFormat Format = ETextureFormat::Rgba8Unorm;
	};

	struct FDeviceOptions
	{
		std::string Driver;
		std::uint64_t MaxTextureBytes = 256 * 1024 * 1024;
		bool bDebug = false;
		std::uint64_t MaxGeometryBytes = 64 * 1024 * 1024;
	};

	struct FSamplerOptions
	{
		EFilter Filter = EFilter::Linear;
		EAddressMode AddressU = EAddressMode::Clamp, AddressV = EAddressMode::Clamp;
	};

	/** Advanced masks and filters are separate capabilities from basic GUI drawing. */
	struct FCapabilities
	{
		bool bOffscreenClear = false, bReadback = false, bWindowPresent = false;
		bool bGuiBasic = false, bClipMasks = false, bFilters = false;
		bool bGuiTransform = false;
		bool bDeviceRecovery = false;
	};

	bool ValidateColor(const FColor& Color, std::string& Error);
	bool ValidateTextureOptions(const FTextureOptions& Options, std::uint64_t ByteLimit, std::uint64_t& Bytes,
								std::string& Error);
	bool ValidateSamplerOptions(const FSamplerOptions& Options, std::string& Error);

	class ISampler
	{
	public:
		virtual ~ISampler() = default;
		virtual bool IsOpen() const noexcept = 0;
		virtual FSamplerOptions GetOptions() const noexcept = 0;
		virtual bool Close(std::string& Error) = 0;
	};

	class ITexture
	{
	public:
		virtual ~ITexture() = default;
		virtual bool IsOpen() const noexcept = 0;
		virtual FTextureOptions GetOptions() const noexcept = 0;
		virtual bool Close(std::string& Error) = 0;
	};

	class ISurface
	{
	public:
		virtual ~ISurface() = default;
		virtual bool IsOpen() const noexcept = 0;
		virtual std::uint64_t GetWindowId() const noexcept = 0;
		virtual bool Close(std::string& Error) = 0;
	};

	class IDevice
	{
	public:
		virtual ~IDevice() = default;
		virtual bool IsOpen() const noexcept = 0;
		virtual std::string GetDriver() const = 0;
		virtual FCapabilities GetCapabilities() const noexcept = 0;
		virtual std::shared_ptr<ITexture> CreateTexture(const FTextureOptions& Options, std::string& Error) = 0;
		virtual std::shared_ptr<ISurface> CreateSurface(std::shared_ptr<Platform::IWindow> Window,
														std::string& Error) = 0;
		// Linear clear values are passed to the attachment; its format determines storage conversion.
		// Alpha is not premultiplied automatically.
		virtual bool ClearTexture(const std::shared_ptr<ITexture>& Texture, const FColor& Color,
								  std::string& Error) = 0;
		// Non-blocking acquisition: a valid but unavailable surface succeeds with bPresented=false.
		virtual bool Present(const std::shared_ptr<ISurface>& Surface, const FColor& Color, bool& bPresented,
							 std::string& Error) = 0;
		// Synchronous diagnostic readback. Output is tightly packed RGBA; failure preserves Result.
		virtual bool ReadTexture(const std::shared_ptr<ITexture>& Texture, std::vector<std::uint8_t>& Result,
								 std::string& Error) = 0;
		virtual bool WaitIdle(std::string& Error) = 0;
		// Close requires textures, surfaces, samplers, geometries and renderers to be closed first.
		virtual bool Close(std::string& Error) = 0;
		virtual std::shared_ptr<ISampler> CreateSampler(const FSamplerOptions& Options, std::string& Error) = 0;
		// Tightly packed RGBA bytes. The region uses texture pixels; byte count must match exactly.
		virtual bool UpdateTexture(const std::shared_ptr<ITexture>& Texture, std::int64_t X, std::int64_t Y,
								   std::int64_t Width, std::int64_t Height, const std::vector<std::uint8_t>& Pixels,
								   std::string& Error) = 0;
		virtual std::shared_ptr<IRenderer2D> CreateRenderer2D(std::string& Error) = 0;
		virtual std::uint64_t GetGeneration() const noexcept;
		// Stops submissions and invalidates existing resources; resource Close remains legal.
		virtual bool Invalidate(std::string& Error);
		// Poll on the owner thread. Providers may complete synchronously; WebGPU uses asynchronous mapping.
		virtual std::shared_ptr<IReadbackRequest> ReadTextureAsync(const std::shared_ptr<ITexture>& Texture,
																   std::string& Error);
		// Progress native asynchronous work and report deferred errors without blocking the host loop.
		virtual bool Poll(std::string& Error);
	};
	class IDeviceRequest
	{
	public:
		virtual ~IDeviceRequest() = default;
		virtual ERequestStatus Poll(std::string& Error) = 0;
		virtual std::shared_ptr<IDevice> GetDevice(std::string& Error) const = 0;
		virtual bool Cancel(std::string& Error) = 0;
	};
	class IReadbackRequest
	{
	public:
		virtual ~IReadbackRequest() = default;
		virtual ERequestStatus Poll(std::string& Error) = 0;
		virtual bool GetPixels(std::vector<std::uint8_t>& Pixels, std::string& Error) const = 0;
		virtual bool Cancel(std::string& Error) = 0;
	};

	class IService
	{
	public:
		virtual ~IService() = default;
		virtual std::shared_ptr<IDevice> CreateDevice(const FDeviceOptions& Options, std::string& Error) = 0;
		// Creates the replacement before invalidating Previous. Assets must be recreated on the returned device.
		virtual std::shared_ptr<IDevice> RecoverDevice(const std::shared_ptr<IDevice>& Previous,
													   const FDeviceOptions& Options, std::string& Error);
		// Default providers complete immediately; asynchronous providers override this entry.
		virtual std::shared_ptr<IDeviceRequest> CreateDeviceAsync(const FDeviceOptions& Options, std::string& Error);
		virtual std::shared_ptr<IDeviceRequest> RecoverDeviceAsync(const std::shared_ptr<IDevice>& Previous,
																   const FDeviceOptions& Options, std::string& Error);
	};
}
