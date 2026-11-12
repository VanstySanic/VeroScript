// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptWebSocket.h"

namespace VeroScript::WebSocket::Detail
{
	// Coordinate independent attempts without retaining a borrowed public facade.
	struct FConnectionAccess
	{
		static Network::FOperation Connect(const std::shared_ptr<Network::Detail::FContext>&, std::string, FOptions);
	};
} // namespace VeroScript::WebSocket::Detail
