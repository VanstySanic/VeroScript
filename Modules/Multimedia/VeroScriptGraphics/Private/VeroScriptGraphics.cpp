// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGraphics.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

namespace VeroScript::Graphics
{
	namespace
	{
		struct FRequestThread
		{
			bool Check(std::string& Error) const
			{
				if (std::this_thread::get_id() != Owner)
				{
					Error = "graphics.wrong_thread: request requires its owner thread";
					return false;
				}
				Error.clear();
				return true;
			}
			std::thread::id Owner = std::this_thread::get_id();
			ERequestStatus Status = ERequestStatus::Succeeded;
		};
		class FDeviceResult final : public IDeviceRequest
		{
		public:
			ERequestStatus Poll(std::string& E) override
			{
				return Thread.Check(E) ? Thread.Status : ERequestStatus::Failed;
			}
			std::shared_ptr<IDevice> GetDevice(std::string& E) const override
			{
				if (!Thread.Check(E))
				{
					return {};
				}
				if (Thread.Status != ERequestStatus::Succeeded)
				{
					E = "graphics.request_cancelled: device request was cancelled";
					return {};
				}
				return Device;
			}
			bool Cancel(std::string& E) override
			{
				if (!Thread.Check(E))
				{
					return false;
				}
				Device.reset();
				Thread.Status = ERequestStatus::Cancelled;
				return true;
			}
			explicit FDeviceResult(std::shared_ptr<IDevice> D) : Device(std::move(D))
			{
			}

		private:
			FRequestThread Thread;
			std::shared_ptr<IDevice> Device;
		};
		class FRecoveryRequest final : public IDeviceRequest
		{
		public:
			ERequestStatus Poll(std::string& E) override
			{
				if (!Thread.Check(E))
				{
					return ERequestStatus::Failed;
				}
				if (Thread.Status != ERequestStatus::Pending)
				{
					E = Error;
					return Thread.Status;
				}
				Thread.Status = Request->Poll(E);
				if (Thread.Status == ERequestStatus::Succeeded)
				{
					auto Candidate = Request->GetDevice(E);
					if (!Candidate || !Previous->Invalidate(E))
					{
						if (Candidate)
						{
							std::string Ignored;
							Candidate->Close(Ignored);
						}
						Thread.Status = ERequestStatus::Failed;
					}
					else
					{
						Device = std::move(Candidate);
					}
				}
				if (Thread.Status != ERequestStatus::Pending)
				{
					Error = E;
					Previous.reset();
				}
				return Thread.Status;
			}
			std::shared_ptr<IDevice> GetDevice(std::string& E) const override
			{
				if (!Thread.Check(E))
				{
					return {};
				}
				if (Thread.Status != ERequestStatus::Succeeded)
				{
					E = Error.empty() ? "graphics.request_not_ready: poll recovery first" : Error;
					return {};
				}
				return Device;
			}
			bool Cancel(std::string& E) override
			{
				if (!Thread.Check(E) || !Request->Cancel(E))
				{
					return false;
				}
				Device.reset();
				Previous.reset();
				Error.clear();
				Thread.Status = ERequestStatus::Cancelled;
				return true;
			}
			FRecoveryRequest(std::shared_ptr<IDevice> P, std::shared_ptr<IDeviceRequest> R)
			    : Previous(std::move(P)), Request(std::move(R))
			{
				Thread.Status = ERequestStatus::Pending;
			}

		private:
			FRequestThread Thread;
			std::shared_ptr<IDevice> Previous, Device;
			std::shared_ptr<IDeviceRequest> Request;
			std::string Error;
		};
		class FReadbackResult final : public IReadbackRequest
		{
		public:
			ERequestStatus Poll(std::string& E) override
			{
				return Thread.Check(E) ? Thread.Status : ERequestStatus::Failed;
			}
			bool GetPixels(std::vector<std::uint8_t>& Out, std::string& E) const override
			{
				if (!Thread.Check(E))
				{
					return false;
				}
				if (Thread.Status != ERequestStatus::Succeeded)
				{
					E = "graphics.request_cancelled: readback was cancelled";
					return false;
				}
				Out = Pixels;
				return true;
			}
			bool Cancel(std::string& E) override
			{
				if (!Thread.Check(E))
				{
					return false;
				}
				Pixels.clear();
				Thread.Status = ERequestStatus::Cancelled;
				return true;
			}
			explicit FReadbackResult(std::vector<std::uint8_t> P) : Pixels(std::move(P))
			{
			}

		private:
			FRequestThread Thread;
			std::vector<std::uint8_t> Pixels;
		};
	}
	bool ValidateColor(const FColor& Color, std::string& Error)
	{
		for (const float Component : {Color.R, Color.G, Color.B, Color.A})
		{
			if (!std::isfinite(Component) || Component < 0 || Component > 1)
			{
				Error = "graphics.invalid_color: RGBA components must be finite and in 0..1";
				return false;
			}
		}
		Error.clear();
		return true;
	}

	bool ValidateTextureOptions(const FTextureOptions& Options, std::uint64_t ByteLimit, std::uint64_t& Bytes,
	    std::string& Error)
	{
		if (Options.Width <= 0 || Options.Height <= 0 || Options.Width > std::numeric_limits<std::uint32_t>::max() ||
		    Options.Height > std::numeric_limits<std::uint32_t>::max() ||
		    (Options.Format != ETextureFormat::Rgba8Unorm && Options.Format != ETextureFormat::Rgba8Srgb))
		{
			Error = "graphics.invalid_texture: invalid dimensions or format";
			return false;
		}
		const auto Width = static_cast<std::uint64_t>(Options.Width);
		const auto Height = static_cast<std::uint64_t>(Options.Height);
		const auto Limit =
		    std::min<std::uint64_t>(ByteLimit, std::min<std::uint64_t>(std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::uint32_t>::max()));
		if (Width > Limit / 4 || Height > Limit / (Width * 4))
		{
			Error = "graphics.resource_limit: texture exceeds the configured or transfer-buffer byte limit";
			return false;
		}
		Bytes = Width * Height * 4;
		Error.clear();
		return true;
	}
	bool ValidateSamplerOptions(const FSamplerOptions& Options, std::string& Error)
	{
		if (Options.Filter > EFilter::Linear || Options.AddressU > EAddressMode::Mirror ||
		    Options.AddressV > EAddressMode::Mirror)
		{
			Error = "graphics.invalid_sampler: unknown filter or address mode";
			return false;
		}
		Error.clear();
		return true;
	}
	std::uint64_t IDevice::GetGeneration() const noexcept
	{
		return 0;
	}
	bool IDevice::Invalidate(std::string& Error)
	{
		Error = "graphics.unsupported_recovery: device invalidation is unavailable";
		return false;
	}
	std::shared_ptr<IReadbackRequest> IDevice::ReadTextureAsync(const std::shared_ptr<ITexture>& Texture,
	    std::string& Error)
	{
		std::vector<std::uint8_t> Pixels;
		if (!ReadTexture(Texture, Pixels, Error))
		{
			return {};
		}
		return std::make_shared<FReadbackResult>(std::move(Pixels));
	}
	bool IDevice::Poll(std::string& Error)
	{
		if (!IsOpen())
		{
			Error = "graphics.closed_device: device is closed or invalidated";
			return false;
		}
		Error.clear();
		return true;
	}
	std::shared_ptr<IDevice> IService::RecoverDevice(const std::shared_ptr<IDevice>& Previous,
	    const FDeviceOptions& Options, std::string& Error)
	{
		if (!Previous)
		{
			Error = "graphics.invalid_device: recovery requires the previous device";
			return {};
		}
		auto Replacement = CreateDevice(Options, Error);
		if (!Replacement)
		{
			return {};
		}
		if (!Previous->Invalidate(Error))
		{
			std::string Ignored;
			Replacement->Close(Ignored);
			return {};
		}
		Error.clear();
		return Replacement;
	}
	std::shared_ptr<IDeviceRequest> IService::CreateDeviceAsync(const FDeviceOptions& Options, std::string& Error)
	{
		auto Device = CreateDevice(Options, Error);
		if (!Device)
		{
			return {};
		}
		return std::make_shared<FDeviceResult>(std::move(Device));
	}
	std::shared_ptr<IDeviceRequest> IService::RecoverDeviceAsync(const std::shared_ptr<IDevice>& Previous,
	    const FDeviceOptions& Options, std::string& Error)
	{
		if (!Previous)
		{
			Error = "graphics.invalid_device: recovery requires the previous device";
			return {};
		}
		auto Request = CreateDeviceAsync(Options, Error);
		if (!Request)
		{
			return {};
		}
		return std::make_shared<FRecoveryRequest>(Previous, std::move(Request));
	}
}
