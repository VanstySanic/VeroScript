// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHostBinding.h"
#include "VeroScriptImage.h"
#include <functional>

namespace VeroScript::Image
{
	using FResourceReader = std::function<bool(const std::string &, std::vector<std::uint8_t> &, std::string &)>;
	bool RegisterScriptBindings(FHostBindingRegistry &Registry, FServiceFactory Factory, std::string &Error,
								FResourceReader Reader = {});
} // namespace VeroScript::Image
