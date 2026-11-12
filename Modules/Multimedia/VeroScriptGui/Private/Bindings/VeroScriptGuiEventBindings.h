// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiScriptInternal.h"

namespace VeroScript::Gui::ScriptBindings
{
	bool DispatchScriptEvent(const std::shared_ptr<FBridge>& Bridge, std::int64_t Connection, FEvent& Event,
							 std::string& Error);
} // namespace VeroScript::Gui::ScriptBindings
