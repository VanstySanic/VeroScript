// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in the owning extension registration namespace.

bool Register(FState& S, std::string& Error)
{
	using T = ENativeType;
	FHostTypeId Api, Ctx, Op, Tcp, Listener, Udp;
	if (!Type(S, "Network", Api, Error) || !Type(S, "Network::context", Ctx, Error) || !Type(S, "Network::operation", Op, Error) ||
	    !Type(S, "Network::tcp_socket", Tcp, Error) || !Type(S, "Network::tcp_listener", Listener, Error) ||
	    !Type(S, "Network::udp_socket", Udp, Error) || !OperationMembers(S, Op, Error, "Network::operation") ||
	    !RegisterNetworkUrl(S, Api, Error) || !RegisterNetworkStreams(S, Api, Error) || !RegisterNetworkTls(S, Api, Error) ||
	    !RegisterNetworkFiles(S, Api, Error) || !RegisterNetworkUpgrade(S, Error))
	{
		return false;
	}
	if (!Member(
	        S,
	        Ctx,
	        "dispatch",
	        T::Int64,
	        {T::Int64},
	        [](const auto& C, auto& Out, auto& E)
	{
		const auto Limit = Integer(C, 0);
		if (Limit < 0 || Limit > 1024 * 1024 || !C.ScriptInvoker)
		{
			E = "Invalid completion dispatch limit or invocation context";
			return false;
		}
		auto& Value = This<FContextValue>(C);
		if (!Value.Value || !Value.Services)
		{
			E = "Network context is null";
			return false;
		}
		std::size_t Count = 0;
		const bool Ok =
		    Value.Services->Dispatch(*Value.Value, *C.ScriptInvoker, static_cast<std::size_t>(Limit), Count, E);
		Out = static_cast<std::int64_t>(Count);
		return Ok;
	},
	        Error,
	        "",
	        {},
	        false,
	        true) ||
	    !Member(
	        S,
	        Api,
	        "create_context",
	        T::HostObject,
	        {T::Bool},
	        [&S](const auto& C, auto& O, auto& E)
	{
		if (!C.ScriptInvoker)
		{
			E = "Network context creation requires a script invocation";
			return false;
		}
		FContextOptions Options;
		Options.Mode = std::get<bool>(C.Args[0]) ? EDriveMode::HostDriven : EDriveMode::NativeBackground;
		auto Backend = BackendService<Network::Backend::IService>(S, Network::Backend::ServiceId, "", Network::Backend::ContractId, E);
		if (!Backend) return false;
		auto Value = std::make_shared<FContext>(std::move(Backend), Options);
		Network::FError Failure;
		if (!Value->RetainCodeOwner(S.CodeOwner.lock(), Failure))
		{
			E = Failure.Message;
			return false;
		}
		auto Services = MakeNetworkBindingService(S, Value, *C.ScriptInvoker, E);
		return Services && Make(S, "Network::context", FContextValue{Value, std::move(Services)}, O, E);
	},
	        Error,
	        "Network::context",
	        {},
	        true))
	{
		return false;
	}
	if (!Member(S, Api, "create_context_with_backend", T::HostObject, {T::String, T::Bool},
	        [&S](const auto& C, auto& O, auto& E)
	{
		if (!C.ScriptInvoker)
		{
			E = "Network context creation requires a script invocation";
			return false;
		}
		auto Backend = BackendService<Network::Backend::IService>(S, Network::Backend::ServiceId, String(C, 0), Network::Backend::ContractId, E);
		if (!Backend) return false;
		FContextOptions Options;
		Options.Mode = std::get<bool>(C.Args[1]) ? EDriveMode::HostDriven : EDriveMode::NativeBackground;
		auto Value = std::make_shared<FContext>(std::move(Backend), Options);
		Network::FError Failure;
		if (!Value->RetainCodeOwner(S.CodeOwner.lock(), Failure))
		{
			E = Failure.Message;
			return false;
		}
		auto Services = MakeNetworkBindingService(S, Value, *C.ScriptInvoker, E);
		return Services && Make(S, "Network::context", FContextValue{Value, std::move(Services)}, O, E);
	}, Error, "Network::context", {}, true))
	{
		return false;
	}
	for (const char* Name : {"retained_bytes", "peak_retained_bytes", "memory_limit"})
	{
		if (!Member(
		        S,
		        Ctx,
		        Name,
		        T::Int64,
		        {},
		        [Name](const auto& C, auto& O, auto& E)
		{
			const auto Usage = This<FContextValue>(C).Value->MemoryUsage();
			const auto Bytes = std::string_view(Name) == "retained_bytes" ? Usage.RetainedBytes
			    : std::string_view(Name) == "peak_retained_bytes"         ? Usage.PeakRetainedBytes
			                                                              : Usage.LimitBytes;
			if (Bytes > static_cast<std::uint64_t>(INT64_MAX))
			{
				E = "Network byte count exceeds int64";
				return false;
			}
			O = static_cast<std::int64_t>(Bytes);
			return true;
		},
		        Error))
		{
			return false;
		}
	}
	if (!Member(
	        S,
	        Ctx,
	        "shutdown",
	        T::Void,
	        {},
	        [](const auto& C, auto&, auto&)
	{
		This<FContextValue>(C).Value->Shutdown();
		return true;
	},
	        Error,
	        "",
	        {},
	        false,
	        true) ||
	    !Member(
	        S,
	        Ctx,
	        "closed",
	        T::Bool,
	        {},
	        [](const auto& C, auto& O, auto&)
	{
		O = This<FContextValue>(C).Value->IsClosed();
		return true;
	},
	        Error) ||
	    !Member(
	        S,
	        Ctx,
	        "poll",
	        T::Int64,
	        {},
	        [](const auto& C, auto& O, auto& E)
	{
		Network::FError Failure;
		O = static_cast<std::int64_t>(This<FContextValue>(C).Value->PollIo(Failure));
		E = Failure.Message;
		return !Failure;
	},
	        Error,
	        "",
	        {},
	        false,
	        true))
	{
		return false;
	}
	for (const bool bConnect : {false, true})
	{
		if (!Member(
		        S,
		        Api,
		        bConnect ? "connect" : "resolve",
		        T::HostObject,
		        {T::HostObject, T::String, T::Int64, T::Int64},
		        [&S, bConnect](const auto& C, auto& O, auto& E)
		{
			return Start(
			    S,
			    "Network::operation",
			    ContextArg(C),
			    [&](auto& Context)
			{
				return bConnect
				    ? FTcpSocket::Connect(
				          Context, String(C, 1), Port(Integer(C, 2)), Options(Integer(C, 3)))
				    : Resolve(Context, String(C, 1), Port(Integer(C, 2)), Options(Integer(C, 3)));
			},
			    O,
			    E);
		},
		        Error,
		        "Network::operation",
		        {"Network::context", "", "", ""},
		        true))
		{
			return false;
		}
	}
	for (const bool bUdp : {false, true})
	{
		if (!Member(
		        S,
		        Api,
		        bUdp ? "bind_udp" : "listen",
		        T::HostObject,
		        {T::HostObject, T::String, T::Int64},
		        [&S, bUdp](const auto& C, auto& O, auto& E)
		{
			auto Owner = ContextArg(C);
			if (!Lease(S, Owner, E))
			{
				return false;
			}
			Network::FError Failure;
			if (bUdp)
			{
				auto Value = FUdpSocket::Bind(Context(Owner), String(C, 1), Port(Integer(C, 2)), Failure);
				if (!Value)
				{
					E = Failure.Message;
					return false;
				}
				return Make(S, "Network::udp_socket", FHandle<FUdpSocket>{Owner, Value}, O, E);
			}
			auto Value = FTcpListener::Listen(Context(Owner), String(C, 1), Port(Integer(C, 2)), Failure);
			if (!Value)
			{
				E = Failure.Message;
				return false;
			}
			return Make(S, "Network::tcp_listener", FHandle<FTcpListener>{Owner, Value}, O, E);
		},
		        Error,
		        bUdp ? "Network::udp_socket" : "Network::tcp_listener",
		        {"Network::context", "", ""},
		        true))
		{
			return false;
		}
	}
	if (!Member(
	        S,
	        Op,
	        "socket",
	        T::HostObject,
	        {},
	        [&S](const auto& C, auto& O, auto& E)
	{
		auto& Value = This<FOperationValue>(C);
		return Make(S,
		    "Network::tcp_socket",
		    FHandle<FTcpSocket>{Value.Context, *Result<std::shared_ptr<FTcpSocket>>(Value)},
		    O,
		    E);
	},
	        Error,
	        "Network::tcp_socket") ||
	    !Member(
	        S,
	        Op,
	        "data",
	        T::String,
	        {},
	        [](const auto& C, auto& O, auto&)
	{
		auto& Op = This<FOperationValue>(C);
		if (auto R = Op.Value.template Result<FReadResult>())
		{
			O = BytesText(R->Bytes);
			return true;
		}
		O = BytesText(Result<FDatagram>(Op)->Bytes);
		return true;
	},
	        Error) ||
	    !Member(
	        S,
	        Op,
	        "eof",
	        T::Bool,
	        {},
	        [](const auto& C, auto& O, auto&)
	{
		O = Result<FReadResult>(This<FOperationValue>(C))->bEnd;
		return true;
	},
	        Error) ||
	    !Member(
	        S,
	        Op,
	        "truncated",
	        T::Bool,
	        {},
	        [](const auto& C, auto& O, auto&)
	{
		O = Result<FDatagram>(This<FOperationValue>(C))->bTruncated;
		return true;
	},
	        Error) ||
	    !Member(
	        S,
	        Op,
	        "original_bytes",
	        T::Int64,
	        {},
	        [](const auto& C, auto& O, auto&)
	{
		O = static_cast<std::int64_t>(Result<FDatagram>(This<FOperationValue>(C))->OriginalBytes);
		return true;
	},
	        Error))
	{
		return false;
	}
	if (!Member(
	        S,
	        Op,
	        "address_count",
	        T::Int64,
	        {},
	        [](const auto& C, auto& O, auto&)
	{
		O = static_cast<std::int64_t>(Result<std::vector<FAddress>>(This<FOperationValue>(C))->size());
		return true;
	},
	        Error) ||
	    !Member(
	        S,
	        Op,
	        "address",
	        T::String,
	        {T::Int64},
	        [](const auto& C, auto& O, auto&)
	{
		auto R = Result<std::vector<FAddress>>(This<FOperationValue>(C));
		const auto I = Count(Integer(C, 0));
		if (I >= R->size())
		{
			throw std::range_error("Address index is out of range");
		}
		O = (*R)[static_cast<std::size_t>(I)].Address;
		return true;
	},
	        Error))
	{
		return false;
	}
	for (const bool bListener : {false, true})
	{
		auto Id = bListener ? Listener : Udp;
		if (!Member(
		        S,
		        Id,
		        "port",
		        T::Int64,
		        {},
		        [bListener](const auto& C, auto& O, auto&)
		{
			O = static_cast<std::int64_t>(bListener ? This<FHandle<FTcpListener>>(C).Value->Port()
			                                        : This<FHandle<FUdpSocket>>(C).Value->Port());
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Id,
		        "close",
		        T::Void,
		        {},
		        [bListener](const auto& C, auto&, auto&)
		{
			if (bListener)
			{
				This<FHandle<FTcpListener>>(C).Value->Close();
			}
			else
			{
				This<FHandle<FUdpSocket>>(C).Value->Close();
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
	return Member(
	           S,
	           Listener,
	           "accept",
	           T::HostObject,
	           {T::Int64},
	           [&S](const auto& C, auto& O, auto& E)
	{
		auto& V = This<FHandle<FTcpListener>>(C);
		return Start(
		    S,
		    "Network::operation",
		    V.Context,
		    [&](auto&)
		{
			return V.Value->Accept(Options(Integer(C, 0)));
		},
		    O,
		    E);
	},
	           Error,
	           "Network::operation",
	           {},
	           false,
	           true) &&
	    Member(
	        S,
	        Tcp,
	        "read",
	        T::HostObject,
	        {T::Int64, T::Int64},
	        [&S](const auto& C, auto& O, auto& E)
	{
		auto& V = This<FHandle<FTcpSocket>>(C);
		return Start(
		    S,
		    "Network::operation",
		    V.Context,
		    [&](auto&)
		{
			return V.Value->Read(Count(Integer(C, 0)), Options(Integer(C, 1)));
		},
		    O,
		    E);
	},
	        Error,
	        "Network::operation",
	        {},
	        false,
	        true) &&
	    Member(
	        S,
	        Tcp,
	        "write",
	        T::HostObject,
	        {T::String, T::Int64},
	        [&S](const auto& C, auto& O, auto& E)
	{
		auto& V = This<FHandle<FTcpSocket>>(C);
		return Start(
		    S,
		    "Network::operation",
		    V.Context,
		    [&](auto&)
		{
			return V.Value->Write(Bytes(String(C, 0)), Options(Integer(C, 1)));
		},
		    O,
		    E);
	},
	        Error,
	        "Network::operation",
	        {},
	        false,
	        true) &&
	    Member(
	        S,
	        Tcp,
	        "close",
	        T::Void,
	        {},
	        [](const auto& C, auto&, auto&)
	{
		This<FHandle<FTcpSocket>>(C).Value->Close();
		return true;
	},
	        Error,
	        "",
	        {},
	        false,
	        true) &&
	    Member(
	        S,
	        Udp,
	        "send_to",
	        T::HostObject,
	        {T::String, T::String, T::Int64, T::Int64},
	        [&S](const auto& C, auto& O, auto& E)
	{
		auto& V = This<FHandle<FUdpSocket>>(C);
		return Start(
		    S,
		    "Network::operation",
		    V.Context,
		    [&](auto&)
		{
			return V.Value->SendTo(
			    Bytes(String(C, 0)), {String(C, 1), Port(Integer(C, 2)), false}, Options(Integer(C, 3)));
		},
		    O,
		    E);
	},
	        Error,
	        "Network::operation",
	        {},
	        false,
	        true) &&
	    Member(
	        S,
	        Udp,
	        "receive",
	        T::HostObject,
	        {T::Int64, T::Int64},
	        [&S](const auto& C, auto& O, auto& E)
	{
		auto& V = This<FHandle<FUdpSocket>>(C);
		return Start(
		    S,
		    "Network::operation",
		    V.Context,
		    [&](auto&)
		{
			return V.Value->Receive(Count(Integer(C, 0)), Options(Integer(C, 1)));
		},
		    O,
		    E);
	},
	        Error,
	        "Network::operation",
	        {},
	        false,
	        true);
}
