// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGraphics.h"
#include "VeroScriptHostBinding.h"
#include <functional>

namespace VeroScript::Graphics
{
	using FServiceFactory = std::function<std::shared_ptr<IService>(const std::string& Backend, std::string& Error)>;
	// Platform script types must already be registered. A backend resolver is supplied by the embedding host.
	bool RegisterScriptBindings(FHostBindingRegistry& Registry, FServiceFactory Factory, std::string& Error);
}
