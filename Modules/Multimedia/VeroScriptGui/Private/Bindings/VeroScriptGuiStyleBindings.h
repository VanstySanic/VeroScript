// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiScriptInternal.h"

namespace VeroScript::Gui::ScriptBindings
{
	bool RegisterStyleBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
							   std::string& Error);
}
