// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPlatformBindingState.h"

namespace VeroScript::Platform::BindingDetail
{
	bool RegisterInputBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FState>& State,
							   std::string& Error);
}
