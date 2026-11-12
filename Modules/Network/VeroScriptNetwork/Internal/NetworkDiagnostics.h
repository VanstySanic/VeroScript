// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"

namespace VeroScript::Network::Detail
{
	// A synchronized snapshot includes listeners, transports and other registered I/O resources.
	std::uint64_t ResourceCount(const std::shared_ptr<FContext>& Context);
} // namespace VeroScript::Network::Detail
