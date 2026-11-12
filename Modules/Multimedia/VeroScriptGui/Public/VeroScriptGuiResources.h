// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptImage.h"
#include "VeroScriptFont.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace VeroScript::Gui
{
	// Identities are supplied by the owning VS/ML/CSS module. Readers may use files or host resources.
	using FResourceReader = std::function<bool(const std::string&, std::vector<std::uint8_t>&, std::string&)>;
	using FDecodedImage = Image::FDecodedImage;
	// Return false with an empty error to delegate an unrecognized format to the default decoder.
	using FImageDecoder = Image::FImageDecoder;
	using FResourceResolver = std::function<bool(const std::string&, const std::string&, std::string&, std::string&)>;
	bool ResolveGuiFileReference(const std::string& Requester, const std::string& Reference, std::string& Identity,
								 std::string& Error);
	bool ResolveGuiStyleImages(const std::string& Requester, const std::string& Value, const FResourceResolver& Resolve,
							   std::string& Result, std::string& Error);

	using FFontFace = Font::FFontFace;

	bool ValidateFontFace(const FFontFace& Font, std::string& Error);
	bool ReadGuiResourceFile(const std::string& Identity, std::vector<std::uint8_t>& Bytes, std::string& Error);
} // namespace VeroScript::Gui
