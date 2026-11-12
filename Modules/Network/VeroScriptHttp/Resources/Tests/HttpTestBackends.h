// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBoost.h"
#include "VeroScriptCurl.h"
#include "VeroScriptHttpBackend.h"
#include "VeroScriptHttpSseClient.h"

namespace VeroScript::Http::Test
{
	inline std::shared_ptr<FServer> Listen(Network::FContext& Context, std::string Address, std::uint16_t Port,
		Network::FError& Error, FServerOptions Options = {})
	{
		Options.Service = Boost::CreateHttpServerService();
		return FServer::Listen(Context, std::move(Address), Port, Error, std::move(Options));
	}
	inline std::shared_ptr<FSseClient> ConnectSse(Network::FContext& Context, FSseClientOptions Options, Network::FError& Error)
	{
		Options.Service = Curl::CreateHttpClientService();
		return FSseClient::Connect(Context, std::move(Options), Error);
	}
}
