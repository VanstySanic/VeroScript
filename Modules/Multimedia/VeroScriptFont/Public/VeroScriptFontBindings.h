// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptFont.h"
#include "VeroScriptHostBinding.h"
#include <functional>

namespace VeroScript::Font
{
	using FResourceReader = std::function<bool(const std::string &, std::vector<std::uint8_t> &, std::string &)>;
	bool RegisterScriptBindings(FHostBindingRegistry &Registry, FServiceFactory Factory, std::string &Error,
								FResourceReader Reader = {});
} // namespace VeroScript::Font
