// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttp.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterHttpServerRequestStreams(
	    FState& S, FHostTypeId Api, FHostTypeId Server, FHostTypeId Exchange, FHostTypeId Operation, std::string& Error)
	{
		using T = ENativeType;
		using FResult = std::shared_ptr<const Http::FRequestBodyResult>;
		for (const bool CountOnly : {true, false})
		{
			if (!Member(
			        S,
			        Exchange,
			        CountOnly ? "header_count" : "header",
			        CountOnly ? T::Int64 : T::String,
			        CountOnly ? std::vector<T>{T::String} : std::vector<T>{T::String, T::Int64},
			        [CountOnly](const auto& C, auto& O, auto& E)
			{
				const auto Values =
				    This<FHandle<Http::FExchange>>(C).Value->Request().Headers.GetAll(String(C, 0));
				if (CountOnly)
				{
					O = static_cast<std::int64_t>(Values.size());
					return true;
				}
				const auto Index = Count(Integer(C, 1));
				if (Index >= Values.size())
				{
					E = "Request header index is out of range";
					return false;
				}
				O = Values[static_cast<std::size_t>(Index)];
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		FHostTypeId OptionsType, ResultType;
		if (!ValueType<Http::FServerOptions>(S, "http::server_options", OptionsType, Error) ||
		    !Type(S, "http::request_body_result", ResultType, Error))
		{
			return false;
		}
		if (!Member(
		        S,
		        Api,
		        "make_server_options",
		        T::HostObject,
		        {},
		        [&S](const auto&, auto& O, auto& E)
		{
			return Make(S, "http::server_options", Http::FServerOptions{}, O, E);
		},
		        Error,
		        "http::server_options",
		        {},
		        true))
		{
			return false;
		}
		if (!Member(S, OptionsType, "set_backend", T::Void, {T::String},
		        [](const auto& C, auto&, auto&)
		{
			This<Http::FServerOptions>(C).BackendId = String(C, 0);
			return true;
		},
		        Error, "", {}, false, true))
		{
			return false;
		}
		const char* Names[] = {"max_body_bytes",
		    "max_stream_body_bytes",
		    "request_stream_buffer_bytes",
		    "max_header_bytes",
		    "max_pending_requests",
		    "response_timeout_ms",
		    "handshake_timeout_ms"};
		for (unsigned I = 0; I < 7; ++I)
		{
			if (!Member(
			        S,
			        OptionsType,
			        Names[I],
			        T::Int64,
			        {},
			        [I](const auto& C, auto& O, auto& E)
			{
				const auto& V = This<Http::FServerOptions>(C);
				const std::uint64_t Counts[] = {V.MaxBodyBytes,
				    V.MaxStreamBodyBytes,
				    V.RequestStreamBufferBytes,
				    V.MaxHeaderBytes,
				    V.MaxPendingRequests,
				    static_cast<std::uint64_t>(V.ResponseTimeout.count()),
				    static_cast<std::uint64_t>(V.HandshakeTimeout.count())};
				if (Counts[I] > static_cast<std::uint64_t>(INT64_MAX))
				{
					E = "Server option exceeds int64";
					return false;
				}
				O = static_cast<std::int64_t>(Counts[I]);
				return true;
			},
			        Error))
			{
				return false;
			}
			const std::string Setter = std::string("set_") + Names[I];
			if (!Member(
			        S,
			        OptionsType,
			        Setter.c_str(),
			        T::Void,
			        {T::Int64},
			        [I](const auto& C, auto&, auto& E)
			{
				auto& V = This<Http::FServerOptions>(C);
				const auto Value = Count(Integer(C, 0));
				if ((I == 3 || I == 4) && Value > UINT32_MAX)
				{
					E = "Server option exceeds uint32";
					return false;
				}
				if ((I == 5 || I == 6) && Value > 365ULL * 24 * 60 * 60 * 1000)
				{
					E = "Server timeout exceeds 365 days";
					return false;
				}
				switch (I)
				{
				case 0:
					{
						V.MaxBodyBytes = Value;
						break;
					}
				case 1:
					{
						V.MaxStreamBodyBytes = Value;
						break;
					}
				case 2:
					{
						V.RequestStreamBufferBytes = Value;
						break;
					}
				case 3:
					{
						V.MaxHeaderBytes = static_cast<std::uint32_t>(Value);
						break;
					}
				case 4:
					{
						V.MaxPendingRequests = static_cast<std::uint32_t>(Value);
						break;
					}
				case 5:
					{
						V.ResponseTimeout = std::chrono::milliseconds(Value);
						break;
					}
				default:
					{
						V.HandshakeTimeout = std::chrono::milliseconds(Value);
						break;
					}
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
		if (!Member(
		        S,
		        OptionsType,
		        "stream_requests",
		        T::Bool,
		        {},
		        [](const auto& C, auto& O, auto&)
		{
			O = This<Http::FServerOptions>(C).bStreamRequests;
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        OptionsType,
		        "set_stream_requests",
		        T::Void,
		        {T::Bool},
		        [](const auto& C, auto&, auto&)
		{
			This<Http::FServerOptions>(C).bStreamRequests = std::get<bool>(C.Args[0]);
			return true;
		},
		        Error,
		        "",
		        {},
		        false,
		        true) ||
		    !Member(
		        S,
		        OptionsType,
		        "set_tls",
		        T::Void,
		        {T::HostObject},
		        [](const auto& C, auto&, auto&)
		{
			auto& V = This<Http::FServerOptions>(C);
			V.Tls = Object<Network::FTlsOptions>(C, 0);
			V.Tls.bEnabled = true;
			return true;
		},
		        Error,
		        "",
		        {"Network::tls_options"},
		        false,
		        true) ||
		    !Member(
		        S,
		        OptionsType,
		        "clear_tls",
		        T::Void,
		        {},
		        [](const auto& C, auto&, auto&)
		{
			This<Http::FServerOptions>(C).Tls = {};
			return true;
		},
		        Error,
		        "",
		        {},
		        false,
		        true) ||
		    !Member(
		        S,
		        Api,
		        "listen_with_options",
		        T::HostObject,
		        {T::HostObject, T::String, T::Int64, T::HostObject},
		        [&S](const auto& C, auto& O, auto& E)
		{
			auto Owner = ContextArg(C);
			if (!Lease(S, Owner, E))
			{
				return false;
			}
			Network::FError Failure;
			auto Server = Http::FServer::Listen(
			    Context(Owner), String(C, 1), Port(Integer(C, 2)), Failure, HttpServerBackend(S, Object<Http::FServerOptions>(C, 3), E));
			if (!Server)
			{
				E = Failure.Message;
				return false;
			}
			return Make(S, "http::server", FHandle<Http::FServer>{Owner, std::move(Server)}, O, E);
		},
		        Error,
		        "http::server",
		        {"Network::context", "", "", "http::server_options"},
		        true))
		{
			return false;
		}
		if (!Member(
		        S,
		        Exchange,
		        "has_body_stream",
		        T::Bool,
		        {},
		        [](const auto& C, auto& O, auto&)
		{
			O = static_cast<bool>(This<FHandle<Http::FExchange>>(C).Value->Request().BodyStream);
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Exchange,
		        "body_stream",
		        T::HostObject,
		        {},
		        [&S](const auto& C, auto& O, auto& E)
		{
			const auto& V = This<FHandle<Http::FExchange>>(C);
			const auto Body = V.Value->Request().BodyStream;
			if (!Body)
			{
				E = "HTTP exchange uses buffered request bodies";
				return false;
			}
			if (!Lease(S, V.Context, E))
			{
				return false;
			}
			const auto& Service = static_cast<FContextValue*>(V.Context->Payload)->Services;
			if (!Service)
			{
				E = "The Network extension does not provide the stream binding service";
				return false;
			}
			return Service->MakeReadable(V.Context, Body, O, E);
		},
		        Error,
		        "Network::readable_stream") ||
		    !Member(
		        S,
		        Exchange,
		        "body_operation",
		        T::HostObject,
		        {},
		        [&S](const auto& C, auto& O, auto& E)
		{
			const auto& V = This<FHandle<Http::FExchange>>(C);
			const auto Op = V.Value->Request().BodyOperation;
			if (!Op.IsValid())
			{
				E = "HTTP exchange uses buffered request bodies";
				return false;
			}
			return Make(S, "http::operation", FOperationValue{V.Context, Op}, O, E);
		},
		        Error,
		        "http::operation") ||
		    !Member(
		        S,
		        Operation,
		        "request_body_result",
		        T::HostObject,
		        {},
		        [&S](const auto& C, auto& O, auto& E)
		{
			return Make(S,
			    "http::request_body_result",
			    Result<Http::FRequestBodyResult>(This<FOperationValue>(C)),
			    O,
			    E);
		},
		        Error,
		        "http::request_body_result") ||
		    !Member(
		        S,
		        ResultType,
		        "body_bytes",
		        T::Int64,
		        {},
		        [](const auto& C, auto& O, auto& E)
		{
			const auto N = This<FResult>(C)->BodyBytes;
			if (N > static_cast<std::uint64_t>(INT64_MAX))
			{
				E = "Body byte count exceeds int64";
				return false;
			}
			O = static_cast<std::int64_t>(N);
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        ResultType,
		        "trailer_count",
		        T::Int64,
		        {},
		        [](const auto& C, auto& O, auto&)
		{
			O = static_cast<std::int64_t>(This<FResult>(C)->Trailers.Fields().size());
			return true;
		},
		        Error))
		{
			return false;
		}
		for (const bool Name : {false, true})
		{
			if (!Member(
			        S,
			        ResultType,
			        Name ? "trailer_name" : "trailer_value",
			        T::String,
			        {T::Int64},
			        [Name](const auto& C, auto& O, auto& E)
			{
				const auto I = Count(Integer(C, 0));
				const auto& Fields = This<FResult>(C)->Trailers.Fields();
				if (I >= Fields.size())
				{
					E = "Trailer index out of range";
					return false;
				}
				O = Name ? Fields[I].first : Fields[I].second;
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		if (!Member(
		        S,
		        Server,
		        "shutdown",
		        T::HostObject,
		        {T::Int64, T::Int64},
		        [&S](const auto& C, auto& O, auto& E)
		{
			const auto Grace = Integer(C, 0);
			if (Grace < 0 || Grace > 365LL * 24 * 60 * 60 * 1000)
			{
				E = "Shutdown grace period is out of range";
				return false;
			}
			const auto& V = This<FHandle<Http::FServer>>(C);
			return Start(
			    S,
			    "http::operation",
			    V.Context,
			    [&](auto&)
			{
				return V.Value->Shutdown(std::chrono::milliseconds(Grace), Options(Integer(C, 1)));
			},
			    O,
			    E);
		},
		        Error,
		        "http::operation",
		        {},
		        false,
		        true) ||
		    !Member(
		        S,
		        Operation,
		        "shutdown_forced",
		        T::Bool,
		        {},
		        [](const auto& C, auto& O, auto&)
		{
			O = Result<Http::FShutdownResult>(This<FOperationValue>(C))->bForced;
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Operation,
		        "shutdown_connections",
		        T::Int64,
		        {},
		        [](const auto& C, auto& O, auto& E)
		{
			const auto N = Result<Http::FShutdownResult>(This<FOperationValue>(C))->ClosedConnections;
			if (N > static_cast<std::uint64_t>(INT64_MAX))
			{
				E = "Shutdown connection count exceeds int64";
				return false;
			}
			O = static_cast<std::int64_t>(N);
			return true;
		},
		        Error))
		{
			return false;
		}

		return true;
	}
} // namespace VeroScript::NetworkBinding
