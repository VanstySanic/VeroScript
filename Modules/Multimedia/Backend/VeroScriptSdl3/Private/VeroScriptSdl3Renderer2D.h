// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSdl3GraphicsResources.h"

namespace VeroScript::Sdl3
{
	std::shared_ptr<Graphics::IRenderer2D> CreateRenderer2DImplementation(const std::shared_ptr<FDeviceState>& State,
																		  std::string& Error);
}
