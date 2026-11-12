// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript::Image
{
	struct FDecodedImage
	{
		std::int32_t Width = 0, Height = 0;
		// Tightly packed RGBA8, sRGB color channels and straight alpha.
		std::vector<std::uint8_t> Rgba;
	};

	struct FDecodeOptions
	{
		std::int32_t MaxWidth = 16384, MaxHeight = 16384;
		std::uint64_t MaxPixels = 16 * 1024 * 1024, MaxSourceBytes = 64 * 1024 * 1024;
	};

	// False with an empty error delegates an unrecognized format to the default decoder.
	using FImageDecoder =
		std::function<bool(const std::string &, const std::vector<std::uint8_t> &, FDecodedImage &, std::string &)>;

	inline constexpr const char *ServiceId = "image";
	inline constexpr const char *ContractId = "veroscript.image";
	inline constexpr const char *ContractVersion = "0.1.0";
	class IService
	{
	public:
		virtual ~IService() = default;
		virtual const char *GetBackendId() const noexcept = 0;
		// Failure preserves Result. Animated-format support is implementation dependent.
		virtual bool Decode(const std::vector<std::uint8_t> &Bytes, FDecodedImage &Result, std::string &Error,
							const FDecodeOptions &Options = {}) const = 0;
	};
	// Null with an empty error means an optional service is not configured.
	// A configured provider must report failures through Error.
	using FServiceFactory = std::function<std::shared_ptr<IService>(const std::string &, std::string &)>;
	bool Validate(const FDecodedImage &Value, std::string &Error, const FDecodeOptions &Options = {});
} // namespace VeroScript::Image
