// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPlatformApi.h"
#include <functional>

namespace VeroScript::Gui
{
	/** Optional owner-thread bridge. Callbacks must not mutate GUI state.
	 * The context restores a visible default cursor and releases its mouse capture on detach/close. */
	struct FPointerServices
	{
		std::function<bool(Platform::ECursor Shape, bool bVisible, std::string &Error)> SetCursor;
		std::function<bool(bool bEnabled, std::string &Error)> SetMouseCapture;
	};
} // namespace VeroScript::Gui
