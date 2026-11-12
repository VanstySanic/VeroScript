// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "WebSocketBinding.h"
#include "VeroScriptWebSocket.h"
#include "VeroScriptNetworkTls.h"
#include "WebSocketProtocols.h"

namespace VeroScript::NetworkBinding
{
	struct FControlValue
	{
		std::optional<WebSocket::FControlEvent> Value;
	};

	bool RegisterWebSocketOptions(
	    FState& S, FHostTypeId Api, FHostTypeId Connection, FHostTypeId Op, std::string& Error)
	{
		using T = ENativeType;
		using O = WebSocket::FOptions;
		FHostTypeId OptionsType, Control;
		if (!ValueType<O>(S, "websocket::options", OptionsType, Error) ||
		    !Type(S, "websocket::control_event", Control, Error) ||
		    !RegisterWebSocketCompression(S, Api, OptionsType, Connection, Error))
		{
			return false;
		}
		if (!Member(
		        S,
		        Api,
		        "make_options",
		        T::HostObject,
		        {},
		        [&S](const auto&, auto& Out, auto& E)
		{
			return Make(S, "websocket::options", O{}, Out, E);
		},
		        Error,
		        "websocket::options",
		        {},
		        true))
		{
			return false;
		}
		if (!Member(S, OptionsType, "set_backend", T::Void, {T::String},
		        [](const auto& C, auto&, auto&)
		{
			This<O>(C).BackendId = String(C, 0);
			return true;
		},
		        Error, "", {}, false, true))
		{
			return false;
		}
		if (!Member(
		        S,
		        OptionsType,
		        "set_proxy",
		        T::Void,
		        {T::String, T::String},
		        [](const auto& C, auto&, auto& E)
		{
			if (!CheckWork(C, String(C, 0).size() + String(C, 1).size(), E))
			{
				return false;
			}
			if (String(C, 0).size() > 4096 || String(C, 1).size() > 4096)
			{
				E = "WebSocket proxy options exceed their limit";
				return false;
			}
			auto Copy = This<O>(C);
			Copy.ProxyUrl = String(C, 0);
			Copy.ProxyAuthorization = String(C, 1);
			This<O>(C) = std::move(Copy);
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
		        "clear_proxy",
		        T::Void,
		        {},
		        [](const auto& C, auto&, auto&)
		{
			This<O>(C).ProxyUrl.clear();
			This<O>(C).ProxyAuthorization.clear();
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
		        "proxy_url",
		        T::String,
		        {},
		        [](const auto& C, auto& Out, auto&)
		{
			Out = This<O>(C).ProxyUrl;
			return true;
		},
		        Error))
		{
			return false;
		}
		if (!Member(
		        S,
		        Api,
		        "accept_upgrade",
		        T::HostObject,
		        {T::HostObject, T::HostObject},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			const auto& Source = Object<FUpgradeSourceValue>(C, 0);
			if (!CheckWork(C, Source.Value->Info().RequestHead.size(), E))
			{
				return false;
			}
			return Start(
			    S,
			    "websocket::operation",
			    Source.Context,
			    [&](auto& Context)
			{
				return WebSocket::FConnection::AcceptUpgrade(Context, Source.Value, WebSocketBackend(S, Object<O>(C, 1), true, E));
			},
			    Out,
			    E);
		},
		        Error,
		        "websocket::operation",
		        {"Network::upgrade_source", "websocket::options"},
		        true))
		{
			return false;
		}
		if (!Member(
		        S,
		        OptionsType,
		        "add_subprotocol",
		        T::Void,
		        {T::String},
		        [](const auto& C, auto&, auto& E)
		{
			auto Values = This<O>(C).Subprotocols;
			Values.push_back(String(C, 0));
			if (!WebSocket::Detail::ValidProtocols(Values, false))
			{
				E = "Invalid or duplicate WebSocket subprotocol";
				return false;
			}
			This<O>(C).Subprotocols = std::move(Values);
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
		        "clear_subprotocols",
		        T::Void,
		        {},
		        [](const auto& C, auto&, auto&)
		{
			This<O>(C).Subprotocols.clear();
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
		        "set_require_subprotocol",
		        T::Void,
		        {T::Bool},
		        [](const auto& C, auto&, auto&)
		{
			This<O>(C).bRequireSubprotocol = std::get<bool>(C.Args[0]);
			return true;
		},
		        Error,
		        "",
		        {},
		        false,
		        true) ||
		    !Member(
		        S,
		        Connection,
		        "subprotocol",
		        T::String,
		        {},
		        [](const auto& C, auto& Out, auto&)
		{
			Out = This<FHandle<WebSocket::FConnection>>(C).Value->Subprotocol();
			return true;
		},
		        Error))
		{
			return false;
		}
		for (const char* Name : {"set_receive_fragments", "set_compression"})
		{
			if (!Member(
			        S,
			        OptionsType,
			        Name,
			        T::Void,
			        {T::Bool},
			        [Name](const auto& C, auto&, auto&)
			{
				auto& Value = This<O>(C);
				if (std::string_view(Name) == "set_receive_fragments")
				{
					Value.bReceiveFragments = std::get<bool>(C.Args[0]);
				}
				else
				{
					Value.bCompression = std::get<bool>(C.Args[0]);
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
		        "set_limits",
		        T::Void,
		        {T::Int64, T::Int64, T::Int64},
		        [](const auto& C, auto&, auto& E)
		{
			const auto Message = Integer(C, 0), Queue = Integer(C, 1), Count = Integer(C, 2);
			if (Message <= 0 || Queue < Message || Count <= 0 || Count > UINT32_MAX)
			{
				E = "Invalid WebSocket limits";
				return false;
			}
			auto& V = This<O>(C);
			V.MaxMessageBytes = static_cast<std::uint64_t>(Message);
			V.MaxQueuedBytes = static_cast<std::uint64_t>(Queue);
			V.MaxQueuedMessages = static_cast<std::uint32_t>(Count);
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
		        "set_timeout",
		        T::Void,
		        {T::Int64},
		        [](const auto& C, auto&, auto&)
		{
			This<O>(C).Operation = Options(Integer(C, 0));
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
		        "set_idle_timeout",
		        T::Void,
		        {T::Int64, T::Bool},
		        [](const auto& C, auto&, auto& E)
		{
			const auto Ms = Integer(C, 0);
			if (Ms != -1 && (Ms <= 0 || Ms > 365LL * 24 * 60 * 60 * 1000))
			{
				E = "Invalid WebSocket idle timeout";
				return false;
			}
			auto& V = This<O>(C);
			V.IdleTimeout = Ms == -1 ? std::nullopt
			                         : std::optional<std::chrono::milliseconds>(std::chrono::milliseconds(Ms));
			V.bKeepAlivePings = std::get<bool>(C.Args[1]);
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
			This<O>(C).Tls = Object<Network::FTlsOptions>(C, 0);
			return true;
		},
		        Error,
		        "",
		        {"Network::tls_options"},
		        false,
		        true))
		{
			return false;
		}
		if (!Member(
		        S,
		        Api,
		        "connect_with_options",
		        T::HostObject,
		        {T::HostObject, T::String, T::HostObject},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			return Start(
			    S,
			    "websocket::operation",
			    ContextArg(C),
			    [&](auto& Context)
			{
				return WebSocket::FConnection::Connect(Context, String(C, 1), WebSocketBackend(S, Object<O>(C, 2), false, E));
			},
			    Out,
			    E);
		},
		        Error,
		        "websocket::operation",
		        {"Network::context", "", "websocket::options"},
		        true) ||
		    !Member(
		        S,
		        Api,
		        "listen_with_options",
		        T::HostObject,
		        {T::HostObject, T::String, T::Int64, T::HostObject},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			auto Owner = ContextArg(C);
			if (!Lease(S, Owner, E))
			{
				return false;
			}
			Network::FError Failure;
			auto Value = WebSocket::FServer::Listen(
			    Context(Owner), String(C, 1), Port(Integer(C, 2)), Failure, WebSocketBackend(S, Object<O>(C, 3), true, E));
			if (!Value)
			{
				E = Failure.Message.empty() ? "Cannot create WebSocket server" : Failure.Message;
				return false;
			}
			return Make(S, "websocket::server", FHandle<WebSocket::FServer>{Owner, std::move(Value)}, Out, E);
		},
		        Error,
		        "websocket::server",
		        {"Network::context", "", "", "websocket::options"},
		        true))
		{
			return false;
		}
		for (const bool Text : {false, true})
		{
			if (!Member(
			        S,
			        Connection,
			        Text ? "send_text_fragment" : "send_binary_fragment",
			        T::HostObject,
			        {T::String, T::Bool, T::Int64},
			        [&S, Text](const auto& C, auto& Out, auto& E)
			{
				auto& V = This<FHandle<WebSocket::FConnection>>(C);
				return Start(
				    S,
				    "websocket::operation",
				    V.Context,
				    [&](auto&)
				{
					return V.Value->SendFragment(
					    {Bytes(String(C, 0)), Text, std::get<bool>(C.Args[1])}, Options(Integer(C, 2)));
				},
				    Out,
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
		if (!Member(
		        S,
		        Connection,
		        "ping",
		        T::HostObject,
		        {T::String, T::Int64},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			auto& V = This<FHandle<WebSocket::FConnection>>(C);
			return Start(
			    S,
			    "websocket::operation",
			    V.Context,
			    [&](auto&)
			{
				return V.Value->Ping(String(C, 0), Options(Integer(C, 1)));
			},
			    Out,
			    E);
		},
		        Error,
		        "websocket::operation",
		        {},
		        false,
		        true) ||
		    !Member(
		        S,
		        Op,
		        "is_first",
		        T::Bool,
		        {},
		        [](const auto& C, auto& Out, auto&)
		{
			Out = Result<WebSocket::FMessage>(This<FOperationValue>(C))->bFirst;
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Op,
		        "is_final",
		        T::Bool,
		        {},
		        [](const auto& C, auto& Out, auto&)
		{
			Out = Result<WebSocket::FMessage>(This<FOperationValue>(C))->bFinal;
			return true;
		},
		        Error) ||
		    !Member(
		        S,
		        Connection,
		        "try_control",
		        T::HostObject,
		        {},
		        [&S](const auto& C, auto& Out, auto& E)
		{
			WebSocket::FControlEvent Event;
			FControlValue Value;
			if (This<FHandle<WebSocket::FConnection>>(C).Value->TryTakeControl(Event))
			{
				Value.Value = std::move(Event);
			}
			return Make(S, "websocket::control_event", std::move(Value), Out, E);
		},
		        Error,
		        "websocket::control_event",
		        {},
		        false,
		        true))
		{
			return false;
		}
		for (const char* Name : {"has_value", "is_ping", "payload"})
		{
			if (!Member(
			        S,
			        Control,
			        Name,
			        std::string_view(Name) == "payload" ? T::String : T::Bool,
			        {},
			        [Name](const auto& C, auto& Out, auto&)
			{
				const auto& V = This<FControlValue>(C).Value;
				if (std::string_view(Name) == "payload")
				{
					Out = V ? V->Payload : std::string{};
				}
				else if (std::string_view(Name) == "has_value")
				{
					Out = V.has_value();
				}
				else
				{
					Out = V && V->Type == WebSocket::EControlType::Ping;
				}
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::NetworkBinding
