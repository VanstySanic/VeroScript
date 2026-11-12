// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttp.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterHttpTls(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		using FServer = Http::FServer;
		using FOptions = Http::FServerOptions;
		const char* Name = "http::server";
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
						FServer::Listen(Context(Owner), String(C, 1), Port(Integer(C, 2)), Failure, HttpServerBackend(S, std::move(Options), E));
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
		return true;
	}
} // namespace VeroScript::NetworkBinding
