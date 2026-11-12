// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in the owning extension registration namespace.

using FResponseValue = FHttpResponseValue;

bool Register(FState& S, std::string& Error)
{
	using T = ENativeType;
	FHostTypeId Api, Client, Request, Response, Server, Exchange, Op;
	if (!Type(S, "http", Api, Error) || !Type(S, "http::client", Client, Error) ||
		!ValueType<FRequest>(S, "http::request", Request, Error) || !Type(S, "http::response", Response, Error) ||
		!Type(S, "http::server", Server, Error) || !Type(S, "http::exchange", Exchange, Error) ||
		!Type(S, "http::operation", Op, Error) || !OperationMembers(S, Op, Error, "http::operation") ||
		!RegisterHttpSse(S, Api, Error) || !RegisterHttpBody(S, Api, Request, Error) ||
		!RegisterHttpStreams(S, Client, Response, Request, Error) || !RegisterHttpMultipart(S, Api, Error) ||
		!RegisterHttpTls(S, Api, Error) || !RegisterHttpServerResponses(S, Api, Exchange, Error) ||
		!RegisterHttpServerStreams(S, Exchange, Error) ||
		!RegisterHttpServerRequestStreams(S, Api, Server, Exchange, Op, Error) ||
		!RegisterHttpClientOptions(S, Api, Client, Request, Response, Op, Error) ||
		!RegisterHttpRouter(S, Api, Exchange, Error) || !RegisterHttpFiles(S, Api, Client, Request, Error) ||
		!RegisterHttpUpgrade(S, Exchange, Error))
	{
		return false;
	}
	if (!Member(
			S,
			Api,
			"create_client",
			T::HostObject,
			{T::HostObject},
			[&S](const auto& C, auto& O, auto& E)
			{
				auto Owner = ContextArg(C);
				if (!Lease(S, Owner, E))
				{
					return false;
				}
				auto Backend = HttpClientBackend(S, "", E);
				if (!Backend) return false;
				return Make(S, "http::client", FHandle<FClient>{Owner,
					std::make_shared<FClient>(Context(Owner), std::move(Backend))}, O, E);
			},
			Error,
			"http::client",
			{"Network::context"},
			true) ||
		!Member(
			S,
			Api,
			"create_request",
			T::HostObject,
			{T::String, T::String},
			[&S](const auto& C, auto& O, auto& E)
			{
				FRequest R;
				R.Method = String(C, 0);
				R.Url = String(C, 1);
				return Make(S, "http::request", std::move(R), O, E);
			},
			Error,
			"http::request",
			{},
			true))
	{
		return false;
	}
	for (const auto& Name : {"set_body", "set_proxy", "set_ca_file"})
	{
		const bool bBody = std::string_view(Name) == "set_body";
		const bool bCa = std::string_view(Name) == "set_ca_file";
		if (!Member(
				S,
				Request,
				Name,
				T::Void,
				{T::String},
				[bBody, bCa](const auto& C, auto&, auto&)
				{
					auto& R = This<FRequest>(C);
					if (bBody)
					{
						R.Body = Bytes(String(C, 0));
						R.Multipart.reset();
						R.Upload.reset();
						R.UploadFactory = {};
						R.UploadLength.reset();
					}
					else if (bCa)
					{
						R.CaFile = String(C, 0);
					}
					else
					{
						R.Proxy = String(C, 0);
					}
					return true;
				},
				Error,
				"",
				{},
				false,
				true))
		{
			return false;
		}
	}
	for (const auto& Name : {"set_timeout", "set_body_limit"})
	{
		const bool bTimeout = std::string_view(Name) == "set_timeout";
		if (!Member(
				S,
				Request,
				Name,
				T::Void,
				{T::Int64},
				[bTimeout](const auto& C, auto&, auto&)
				{
					auto& R = This<FRequest>(C);
					if (bTimeout)
					{
						R.Operation = Options(Integer(C, 0));
					}
					else
					{
						R.MaxBodyBytes = Count(Integer(C, 0));
					}
					return true;
				},
				Error,
				"",
				{},
				false,
				true))
		{
			return false;
		}
	}
	for (const auto& Name : {"follow_redirects", "require_http2"})
	{
		const bool bRedirects = std::string_view(Name) == "follow_redirects";
		if (!Member(
				S,
				Request,
				Name,
				T::Void,
				{T::Bool},
				[bRedirects](const auto& C, auto&, auto&)
				{
					auto& R = This<FRequest>(C);
					if (bRedirects)
					{
						R.bFollowRedirects = std::get<bool>(C.Args[0]);
					}
					else
					{
						R.bRequireHttp2 = std::get<bool>(C.Args[0]);
					}
					return true;
				},
				Error,
				"",
				{},
				false,
				true))
		{
			return false;
		}
	}
	for (const bool bSet : {false, true})
	{
		if (!Member(
				S,
				Request,
				bSet ? "set_header" : "add_header",
				T::Void,
				{T::String, T::String},
				[bSet](const auto& C, auto&, auto& E)
				{
					Network::FError Failure;
					auto& H = This<FRequest>(C).Headers;
					const bool Success = bSet ? H.TrySet(String(C, 0), String(C, 1), Failure)
											  : H.TryAdd(String(C, 0), String(C, 1), Failure);
					E = Failure.Message;
					return Success;
				},
				Error,
				"",
				{},
				false,
				true))
		{
			return false;
		}
	}
	if (!Member(
			S,
			Client,
			"send",
			T::HostObject,
			{T::HostObject},
			[&S](const auto& C, auto& O, auto& E)
			{
				auto& V = This<FHandle<FClient>>(C);
				return Start(
					S,
					"http::operation",
					V.Context,
					[&](auto&)
					{
						return V.Value->Send(Object<FRequest>(C, 0));
					},
					O,
					E);
			},
			Error,
			"http::operation",
			{"http::request"},
			false,
			true) ||
		!Member(
			S,
			Client,
			"close",
			T::Void,
			{},
			[](const auto& C, auto&, auto&)
			{
				This<FHandle<FClient>>(C).Value->Close();
				return true;
			},
			Error,
			"",
			{},
			false,
			true) ||
		!Member(
			S,
			Client,
			"tls_supported",
			T::Bool,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = This<FHandle<FClient>>(C).Value->Capabilities().bTls;
				return true;
			},
			Error) ||
		!Member(
			S,
			Client,
			"http2_supported",
			T::Bool,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = This<FHandle<FClient>>(C).Value->Capabilities().bHttp2;
				return true;
			},
			Error))
	{
		return false;
	}
	if (!Member(
			S,
			Op,
			"response",
			T::HostObject,
			{},
			[&S](const auto& C, auto& O, auto& E)
			{
				return Make(S, "http::response", FResponseValue{Result<FResponse>(This<FOperationValue>(C))}, O, E);
			},
			Error,
			"http::response") ||
		!Member(
			S,
			Response,
			"status",
			T::Int64,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = static_cast<std::int64_t>(This<FResponseValue>(C).Value->Status);
				return true;
			},
			Error) ||
		!Member(
			S,
			Response,
			"body",
			T::String,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = BytesText(This<FResponseValue>(C).Value->Body);
				return true;
			},
			Error) ||
		!Member(
			S,
			Response,
			"effective_url",
			T::String,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = This<FResponseValue>(C).Value->EffectiveUrl;
				return true;
			},
			Error))
	{
		return false;
	}
	if (!Member(
			S,
			Response,
			"header_count",
			T::Int64,
			{T::String},
			[](const auto& C, auto& O, auto&)
			{
				O = static_cast<std::int64_t>(This<FResponseValue>(C).Value->Headers.GetAll(String(C, 0)).size());
				return true;
			},
			Error) ||
		!Member(
			S,
			Response,
			"header",
			T::String,
			{T::String, T::Int64},
			[](const auto& C, auto& O, auto&)
			{
				auto Values = This<FResponseValue>(C).Value->Headers.GetAll(String(C, 0));
				const auto I = Count(Integer(C, 1));
				if (I >= Values.size())
				{
					throw std::range_error("Header index is out of range");
				}
				O = Values[static_cast<std::size_t>(I)];
				return true;
			},
			Error))
	{
		return false;
	}
	if (!Member(
			S,
			Api,
			"listen",
			T::HostObject,
			{T::HostObject, T::String, T::Int64},
			[&S](const auto& C, auto& O, auto& E)
			{
				auto Owner = ContextArg(C);
				if (!Lease(S, Owner, E))
				{
					return false;
				}
				Network::FError Failure;
				auto Value = FServer::Listen(Context(Owner), String(C, 1), Port(Integer(C, 2)), Failure, HttpServerBackend(S, {}, E));
				if (!Value)
				{
					E = Failure.Message;
					return false;
				}
				return Make(S, "http::server", FHandle<FServer>{Owner, Value}, O, E);
			},
			Error,
			"http::server",
			{"Network::context", "", ""},
			true))
	{
		return false;
	}
	return Member(
			   S,
			   Server,
			   "port",
			   T::Int64,
			   {},
			   [](const auto& C, auto& O, auto&)
			   {
				   O = static_cast<std::int64_t>(This<FHandle<FServer>>(C).Value->Port());
				   return true;
			   },
			   Error) &&
		   Member(
			   S,
			   Server,
			   "close",
			   T::Void,
			   {},
			   [](const auto& C, auto&, auto&)
			   {
				   This<FHandle<FServer>>(C).Value->Close();
				   return true;
			   },
			   Error,
			   "",
			   {},
			   false,
			   true) &&
		   Member(
			   S,
			   Server,
			   "accept",
			   T::HostObject,
			   {T::Int64},
			   [&S](const auto& C, auto& O, auto& E)
			   {
				   auto& V = This<FHandle<FServer>>(C);
				   return Start(
					   S,
					   "http::operation",
					   V.Context,
					   [&](auto&)
					   {
						   return V.Value->Accept(Options(Integer(C, 0)));
					   },
					   O,
					   E);
			   },
			   Error,
			   "http::operation",
			   {},
			   false,
			   true) &&
		   Member(
			   S,
			   Op,
			   "exchange",
			   T::HostObject,
			   {},
			   [&S](const auto& C, auto& O, auto& E)
			   {
				   auto& V = This<FOperationValue>(C);
				   return Make(S,
					   "http::exchange",
					   FHandle<FExchange>{V.Context, *Result<std::shared_ptr<FExchange>>(V)},
					   O,
					   E);
			   },
			   Error,
			   "http::exchange") &&
		   Member(
			   S,
			   Exchange,
			   "target",
			   T::String,
			   {},
			   [](const auto& C, auto& O, auto&)
			   {
				   O = This<FHandle<FExchange>>(C).Value->Request().Target;
				   return true;
			   },
			   Error) &&
		   Member(
			   S,
			   Exchange,
			   "method",
			   T::String,
			   {},
			   [](const auto& C, auto& O, auto&)
			   {
				   O = This<FHandle<FExchange>>(C).Value->Request().Method;
				   return true;
			   },
			   Error) &&
		   Member(
			   S,
			   Exchange,
			   "body",
			   T::String,
			   {},
			   [](const auto& C, auto& O, auto&)
			   {
				   O = BytesText(This<FHandle<FExchange>>(C).Value->Request().Body);
				   return true;
			   },
			   Error) &&
		   Member(
			   S,
			   Exchange,
			   "reply",
			   T::HostObject,
			   {T::Int64, T::String, T::Int64},
			   [&S](const auto& C, auto& O, auto& E)
			   {
				   const auto Status = Integer(C, 0);
				   if (Status < 100 || Status > 599)
				   {
					   throw std::range_error("HTTP status is out of range");
				   }
				   FResponse R;
				   R.Status = static_cast<std::uint32_t>(Status);
				   R.Body = Bytes(String(C, 1));
				   auto& V = This<FHandle<FExchange>>(C);
				   return Start(
					   S,
					   "http::operation",
					   V.Context,
					   [&](auto&)
					   {
						   return V.Value->Reply(std::move(R), Options(Integer(C, 2)));
					   },
					   O,
					   E);
			   },
			   Error,
			   "http::operation",
			   {},
			   false,
			   true);
}
