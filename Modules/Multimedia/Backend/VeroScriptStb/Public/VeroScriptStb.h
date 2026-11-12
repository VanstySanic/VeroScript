// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptImage.h"

namespace VeroScript::Stb
{
	// The Stb module can implement additional public services independently.
	std::shared_ptr<Image::IService> CreateImageService();
} // namespace VeroScript::Stb
