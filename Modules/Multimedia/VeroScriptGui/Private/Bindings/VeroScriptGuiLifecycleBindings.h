// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiScriptInternal.h"

namespace VeroScript::Gui::ScriptBindings
{
	bool RegisterWidgetGeometry(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
								std::string& Error);
} // namespace VeroScript::Gui::ScriptBindings
