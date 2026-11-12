// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNative.h"

namespace VeroScript
{
	std::shared_ptr<void> JoinCallbackOwners(std::shared_ptr<void> Module, std::shared_ptr<void> Bridge)
	{
		if (!Module) return Bridge;
		if (!Bridge) return Module;
		if (!Module.owner_before(Bridge) && !Bridge.owner_before(Module)) return Module;
		struct FOwners
		{
			std::shared_ptr<void> Module, Bridge;
		};
		return std::make_shared<FOwners>(FOwners{std::move(Module), std::move(Bridge)});
	}
}
