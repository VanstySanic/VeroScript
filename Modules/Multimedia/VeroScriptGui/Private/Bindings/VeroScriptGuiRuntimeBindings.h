// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiScriptInternal.h"

namespace VeroScript::Gui::ScriptBindings
{
	struct FContextView
	{
		std::shared_ptr<FContext> Context;
		// Keeps an application's window/device alive while a context view exists.
		std::shared_ptr<FApplication> Application;
	};
} // namespace VeroScript::Gui::ScriptBindings
