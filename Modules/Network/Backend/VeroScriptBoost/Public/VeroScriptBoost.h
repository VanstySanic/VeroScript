// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetworkBackend.h"

namespace VeroScript::Http::Backend
{
	class IServerService;
}
namespace VeroScript::WebSocket::Backend
{
	class IService;
}

namespace VeroScript::Boost
{
	std::shared_ptr<Network::Backend::IService> CreateNetworkService();
	std::shared_ptr<Http::Backend::IServerService> CreateHttpServerService();
	std::shared_ptr<WebSocket::Backend::IService> CreateWebSocketService();
} // namespace VeroScript::Boost
