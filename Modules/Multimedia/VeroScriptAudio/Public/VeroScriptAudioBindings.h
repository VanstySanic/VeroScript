// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAudio.h"
#include "VeroScriptHostBinding.h"
#include <functional>

namespace VeroScript::Audio
{
	using FServiceFactory = std::function<std::shared_ptr<IService>(const std::string& Backend, std::string& Error)>;
	// Register before publishing the catalog. The factory must not strongly capture its owning environment.
	bool RegisterScriptBindings(FHostBindingRegistry& Registry, FServiceFactory Factory, std::string& Error);
}
