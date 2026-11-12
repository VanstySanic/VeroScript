// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkBindingSupport.h"
#include "VeroScriptWebSocketBackend.h"

namespace VeroScript::NetworkBinding
{
	inline WebSocket::FOptions WebSocketBackend(FState& S, WebSocket::FOptions Options, bool Server, std::string& Error)
	{
		Options.Service = BackendService<WebSocket::Backend::IService>(S,
		    Server ? WebSocket::Backend::ServerServiceId : WebSocket::Backend::ClientServiceId,
		    Options.BackendId, WebSocket::Backend::ContractId, Error);
		if (!Options.Service)
		{
			throw std::runtime_error(Error);
		}
		return Options;
	}
	bool RegisterWebSocketTls(FState& S, FHostTypeId Api, std::string& Error);
	bool RegisterWebSocketReconnect(FState&, FHostTypeId, std::string&);
	bool RegisterWebSocketCompression(FState&, FHostTypeId, FHostTypeId, FHostTypeId, std::string&);
	bool RegisterWebSocketOptions(
	    FState& S, FHostTypeId Api, FHostTypeId Connection, FHostTypeId Operation, std::string& Error);
} // namespace VeroScript::NetworkBinding
