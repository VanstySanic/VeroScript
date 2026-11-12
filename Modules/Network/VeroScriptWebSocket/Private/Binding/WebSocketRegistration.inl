// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in the owning extension registration namespace.

bool Register(FState& S, std::string& Error)
{
	using T = ENativeType;
	FHostTypeId Api, Connection, Server, Op;
	if (!Type(S, "websocket", Api, Error) || !Type(S, "websocket::connection", Connection, Error) ||
		!Type(S, "websocket::server", Server, Error) || !Type(S, "websocket::operation", Op, Error) ||
		!OperationMembers(S, Op, Error, "websocket::operation") || !RegisterWebSocketTls(S, Api, Error) ||
		!RegisterWebSocketOptions(S, Api, Connection, Op, Error) || !RegisterWebSocketReconnect(S, Api, Error))
	{
		return false;
	}
	if (!Member(
			S,
			Api,
			"connect",
			T::HostObject,
			{T::HostObject, T::String, T::Int64},
			[&S](const auto& C, auto& O, auto& E)
			{
				FOptions Opt;
				Opt.Operation = Options(Integer(C, 2));
				return Start(
					S,
					"websocket::operation",
					ContextArg(C),
					[&](auto& Context)
					{
						return FConnection::Connect(Context, String(C, 1), WebSocketBackend(S, std::move(Opt), false, E));
					},
					O,
					E);
			},
			Error,
			"websocket::operation",
			{"Network::context", "", ""},
			true) ||
		!Member(
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
				auto Value = FServer::Listen(Context(Owner), String(C, 1), Port(Integer(C, 2)), Failure, WebSocketBackend(S, {}, true, E));
				if (!Value)
				{
					E = Failure.Message;
					return false;
				}
				return Make(S, "websocket::server", FHandle<FServer>{Owner, Value}, O, E);
			},
			Error,
			"websocket::server",
			{"Network::context", "", ""},
			true))
	{
		return false;
	}
	if (!Member(
			S,
			Op,
			"connection",
			T::HostObject,
			{},
			[&S](const auto& C, auto& O, auto& E)
			{
				auto& V = This<FOperationValue>(C);
				return Make(S,
					"websocket::connection",
					FHandle<FConnection>{V.Context, *Result<std::shared_ptr<FConnection>>(V)},
					O,
					E);
			},
			Error,
			"websocket::connection") ||
		!Member(
			S,
			Op,
			"data",
			T::String,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = BytesText(Result<FMessage>(This<FOperationValue>(C))->Bytes);
				return true;
			},
			Error) ||
		!Member(
			S,
			Op,
			"is_text",
			T::Bool,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = Result<FMessage>(This<FOperationValue>(C))->bText;
				return true;
			},
			Error) ||
		!Member(
			S,
			Op,
			"clean",
			T::Bool,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = Result<FCloseInfo>(This<FOperationValue>(C))->bClean;
				return true;
			},
			Error) ||
		!Member(
			S,
			Op,
			"close_code",
			T::Int64,
			{},
			[](const auto& C, auto& O, auto&)
			{
				O = static_cast<std::int64_t>(Result<FCloseInfo>(This<FOperationValue>(C))->Code);
				return true;
			},
			Error))
	{
		return false;
	}
	for (const bool bText : {false, true})
	{
		if (!Member(
				S,
				Connection,
				bText ? "send_text" : "send_binary",
				T::HostObject,
				{T::String, T::Int64},
				[&S, bText](const auto& C, auto& O, auto& E)
				{
					auto& V = This<FHandle<FConnection>>(C);
					return Start(
						S,
						"websocket::operation",
						V.Context,
						[&](auto&)
						{
							return V.Value->Send({Bytes(String(C, 0)), bText}, Options(Integer(C, 1)));
						},
						O,
						E);
				},
				Error,
				"websocket::operation",
				{},
				false,
				true))
		{
			return false;
		}
	}
	return Member(
			   S,
			   Connection,
			   "receive",
			   T::HostObject,
			   {T::Int64},
			   [&S](const auto& C, auto& O, auto& E)
			   {
				   auto& V = This<FHandle<FConnection>>(C);
				   return Start(
					   S,
					   "websocket::operation",
					   V.Context,
					   [&](auto&)
					   {
						   return V.Value->Receive(Options(Integer(C, 0)));
					   },
					   O,
					   E);
			   },
			   Error,
			   "websocket::operation",
			   {},
			   false,
			   true) &&
		   Member(
			   S,
			   Connection,
			   "close",
			   T::HostObject,
			   {T::Int64, T::String, T::Int64},
			   [&S](const auto& C, auto& O, auto& E)
			   {
				   auto& V = This<FHandle<FConnection>>(C);
				   return Start(
					   S,
					   "websocket::operation",
					   V.Context,
					   [&](auto&)
					   {
						   return V.Value->Close(Port(Integer(C, 0)), String(C, 1), Options(Integer(C, 2)));
					   },
					   O,
					   E);
			   },
			   Error,
			   "websocket::operation",
			   {},
			   false,
			   true) &&
		   Member(
			   S,
			   Connection,
			   "closed",
			   T::Bool,
			   {},
			   [](const auto& C, auto& O, auto&)
			   {
				   O = This<FHandle<FConnection>>(C).Value->CloseInfo().bClosed;
				   return true;
			   },
			   Error) &&
		   Member(
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
			   "accept",
			   T::HostObject,
			   {T::Int64},
			   [&S](const auto& C, auto& O, auto& E)
			   {
				   auto& V = This<FHandle<FServer>>(C);
				   return Start(
					   S,
					   "websocket::operation",
					   V.Context,
					   [&](auto&)
					   {
						   return V.Value->Accept(Options(Integer(C, 0)));
					   },
					   O,
					   E);
			   },
			   Error,
			   "websocket::operation",
			   {},
			   false,
			   true) &&
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
			   true);
}
