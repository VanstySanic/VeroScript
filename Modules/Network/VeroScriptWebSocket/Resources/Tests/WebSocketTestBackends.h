// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBoost.h"
#include "VeroScriptWebSocketBackend.h"
#include "VeroScriptWebSocketReconnect.h"

namespace VeroScript::WebSocket::Test
{
	inline Network::FOperation Connect(Network::FContext& Context, std::string Url, FOptions Options = {})
	{
		Options.Service = Boost::CreateWebSocketService();
		return FConnection::Connect(Context, std::move(Url), std::move(Options));
	}
	inline std::shared_ptr<FServer> Listen(Network::FContext& Context, std::string Address, std::uint16_t Port,
	    Network::FError& Error, FOptions Options = {})
	{
		Options.Service = Boost::CreateWebSocketService();
		return FServer::Listen(Context, std::move(Address), Port, Error, std::move(Options));
	}
	inline Network::FOperation AcceptUpgrade(Network::FContext& Context,
	    std::shared_ptr<Network::FUpgradeSource> Source, FOptions Options = {})
	{
		Options.Service = Boost::CreateWebSocketService();
		return FConnection::AcceptUpgrade(Context, std::move(Source), std::move(Options));
	}
	inline FReconnectTask ConnectWithRetry(Network::FContext& Context, std::string Url,
	    FReconnectPolicy Policy = {}, FOptions Options = {})
	{
		Options.Service = Boost::CreateWebSocketService();
		return WebSocket::ConnectWithRetry(Context, std::move(Url), Policy, std::move(Options));
	}
	inline bool CompressionSupported()
	{
		return Boost::CreateWebSocketService()->CompressionSupported();
	}
}
