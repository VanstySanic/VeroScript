// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkBindingSupport.h"
#include "VeroScriptHttpBackend.h"

namespace VeroScript::Http
{
	struct FResponse;
	struct FAttempt;
} // namespace VeroScript::Http

namespace VeroScript::NetworkBinding
{
	inline std::shared_ptr<Http::Backend::IClientService> HttpClientBackend(FState& S, std::string_view Backend, std::string& Error)
	{
		return BackendService<Http::Backend::IClientService>(S, Http::Backend::ClientServiceId, Backend,
		    Http::Backend::ClientContractId, Error);
	}
	inline Http::FServerOptions HttpServerBackend(FState& S, Http::FServerOptions Options, std::string& Error)
	{
		Options.Service = BackendService<Http::Backend::IServerService>(S, Http::Backend::ServerServiceId,
		    Options.BackendId, Http::Backend::ServerContractId, Error);
		if (!Options.Service)
		{
			throw std::runtime_error(Error);
		}
		return Options;
	}
	struct FHttpResponseValue
	{
		std::shared_ptr<const Http::FResponse> Value;
	};
	struct FHttpAttemptValue
	{
		std::shared_ptr<const void> Owner;
		const Http::FAttempt* Value = nullptr;
	};
	bool RegisterHttpSse(FState& S, FHostTypeId Api, std::string& Error);
	bool RegisterHttpSseClient(FState& S, FHostTypeId Api, std::string& Error);
	bool RegisterHttpServerResponses(FState& S, FHostTypeId Api, FHostTypeId Exchange, std::string& Error);
	bool RegisterHttpServerStreams(FState&, FHostTypeId, std::string&);
	bool RegisterHttpServerRequestStreams(FState&, FHostTypeId, FHostTypeId, FHostTypeId, FHostTypeId, std::string&);
	bool RegisterHttpRouter(FState&, FHostTypeId, FHostTypeId, std::string&);
	bool RegisterHttpBody(FState& S, FHostTypeId Api, FHostTypeId Request, std::string& Error);
	bool RegisterHttpMultipart(FState&, FHostTypeId, std::string&);
	bool RegisterHttpClientOptions(FState& S, FHostTypeId Api, FHostTypeId Client, FHostTypeId Request,
	    FHostTypeId Response, FHostTypeId Operation, std::string& Error);
	bool RegisterHttpTrace(FState&, FHostTypeId, FHostTypeId, std::string&);
	bool RegisterHttpUpgrade(FState&, FHostTypeId, std::string&);
	bool RegisterHttpFiles(FState&, FHostTypeId, FHostTypeId, FHostTypeId, std::string&);
	bool RegisterHttpTls(FState& S, FHostTypeId Api, std::string& Error);
	bool RegisterHttpStreams(
	    FState& S, FHostTypeId Client, FHostTypeId Response, FHostTypeId Request, std::string& Error);
} // namespace VeroScript::NetworkBinding
