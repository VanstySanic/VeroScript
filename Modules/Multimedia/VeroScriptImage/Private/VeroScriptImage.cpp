// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptImage.h"

namespace VeroScript::Image
{
	namespace
	{
		bool Dimensions(std::int32_t Width, std::int32_t Height, const FDecodeOptions &Options)
		{
			return Width > 0 && Height > 0 && Width <= Options.MaxWidth && Height <= Options.MaxHeight &&
				   static_cast<std::uint64_t>(Width) * Height <= Options.MaxPixels &&
				   static_cast<std::uint64_t>(Width) * Height <= SIZE_MAX / 4;
		}
	} // namespace

	bool Validate(const FDecodedImage &Value, std::string &Error, const FDecodeOptions &Options)
	{
		Error.clear();
		if (!Dimensions(Value.Width, Value.Height, Options) ||
			Value.Rgba.size() != static_cast<std::uint64_t>(Value.Width) * Value.Height * 4)
		{
			Error = "image.invalid_pixels";
			return false;
		}
		return true;
	}

} // namespace VeroScript::Image
