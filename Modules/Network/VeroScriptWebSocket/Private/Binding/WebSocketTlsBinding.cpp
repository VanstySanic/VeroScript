// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "WebSocketBinding.h"
#include "VeroScriptWebSocket.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterWebSocketTls(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		using FServer = WebSocket::FServer;
		using FOptions = WebSocket::FOptions;
		const char* Name = "websocket::server";
		if (!Member(
				S,
				Api,
				"listen_tls",
				T::HostObject,
				{T::HostObject, T::String, T::Int64, T::HostObject},
				[&S, Name](const auto& C, auto& O, auto& E)
				{
					auto Owner = ContextArg(C);
					if (!Lease(S, Owner, E))
					{
						return false;
					}
					FOptions Options;
					Options.Tls = Object<Network::FTlsOptions>(C, 3);
					Options.Tls.bEnabled = true;
					Network::FError Failure;
					auto Server =
						FServer::Listen(Context(Owner), String(C, 1), Port(Integer(C, 2)), Failure, WebSocketBackend(S, std::move(Options), true, E));
					if (!Server)
					{
						E = Failure.Message;
						return false;
					}
					return Make(S, Name, FHandle<FServer>{Owner, std::move(Server)}, O, E);
				},
				Error,
				Name,
				{"Network::context", "", "", "Network::tls_options"},
				true))
		{
			return false;
		}
		return Member(
			S,
			Api,
			"connect_tls",
			T::HostObject,
			{T::HostObject, T::String, T::HostObject, T::Int64},
			[&S](const auto& C, auto& O, auto& E)
			{
				WebSocket::FOptions Value;
				Value.Tls = Object<Network::FTlsOptions>(C, 2);
				Value.Tls.bEnabled = true;
				Value.Operation = Options(Integer(C, 3));
				return Start(
					S,
					"websocket::operation",
					ContextArg(C),
					[&](auto& Context)
					{
						return WebSocket::FConnection::Connect(Context, String(C, 1), WebSocketBackend(S, std::move(Value), false, E));
					},
					O,
					E);
			},
			Error,
			"websocket::operation",
			{"Network::context", "", "Network::tls_options", ""},
			true);
	}
} // namespace VeroScript::NetworkBinding
