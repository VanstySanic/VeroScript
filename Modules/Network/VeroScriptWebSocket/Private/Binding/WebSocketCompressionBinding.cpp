// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "WebSocketBinding.h"
#include "VeroScriptWebSocket.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterWebSocketCompression(
		FState& S, FHostTypeId Api, FHostTypeId Options, FHostTypeId Connection, std::string& Error)
	{
		using T = ENativeType;
		using O = WebSocket::FCompressionOptions;
		using I = WebSocket::FCompressionInfo;
		FHostTypeId Compression, Info;
		if (!ValueType<O>(S, "websocket::compression_options", Compression, Error) ||
			!ValueType<I>(S, "websocket::compression_info", Info, Error) ||
			!Member(
				S,
				Api,
				"compression_supported",
				T::Bool,
				{},
				[&S](const auto&, auto& Out, auto& E)
				{
					auto Backend = BackendService<WebSocket::Backend::IService>(S, WebSocket::Backend::ClientServiceId, "", WebSocket::Backend::ContractId, E);
					if (!Backend) return false;
					Out = Backend->CompressionSupported();
					return true;
				},
				Error,
				"",
				{},
				true) ||
			!Member(
				S,
				Api,
				"make_compression_options",
				T::HostObject,
				{},
				[&S](const auto&, auto& Out, auto& E)
				{
					return Make(S, "websocket::compression_options", O{}, Out, E);
				},
				Error,
				"websocket::compression_options",
				{},
				true) ||
			!Member(
				S,
				Options,
				"set_compression_options",
				T::Void,
				{T::HostObject},
				[](const auto& C, auto&, auto&)
				{
					This<WebSocket::FOptions>(C).Compression = Object<O>(C, 0);
					return true;
				},
				Error,
				"",
				{"websocket::compression_options"},
				false,
				true) ||
			!Member(
				S,
				Options,
				"compression_options",
				T::HostObject,
				{},
				[&S](const auto& C, auto& Out, auto& E)
				{
					return Make(S, "websocket::compression_options", This<WebSocket::FOptions>(C).Compression, Out, E);
				},
				Error,
				"websocket::compression_options") ||
			!Member(
				S,
				Connection,
				"compression",
				T::HostObject,
				{},
				[&S](const auto& C, auto& Out, auto& E)
				{
					return Make(S,
						"websocket::compression_info",
						This<FHandle<WebSocket::FConnection>>(C).Value->CompressionInfo(),
						Out,
						E);
				},
				Error,
				"websocket::compression_info"))
		{
			return false;
		}
		using FNumber = std::int32_t O::*;
		const std::pair<const char*, FNumber> Numbers[] = {{"client_window_bits", &O::ClientMaxWindowBits},
			{"server_window_bits", &O::ServerMaxWindowBits},
			{"level", &O::Level},
			{"memory_level", &O::MemoryLevel}};
		for (const auto& Item : Numbers)
		{
			if (!Member(
					S,
					Compression,
					Item.first,
					T::Int64,
					{},
					[Field = Item.second](const auto& C, auto& Out, auto&)
					{
						Out = static_cast<std::int64_t>(This<O>(C).*Field);
						return true;
					},
					Error) ||
				!Member(
					S,
					Compression,
					("set_" + std::string(Item.first)).c_str(),
					T::Void,
					{T::Int64},
					[Field = Item.second](const auto& C, auto&, auto&)
					{
						const auto Value = Integer(C, 0);
						const auto Min = Field == &O::Level ? 0 : Field == &O::MemoryLevel ? 1 : 9;
						const auto Max = Field == &O::Level || Field == &O::MemoryLevel ? 9 : 15;
						if (Value < Min || Value > Max)
						{
							throw std::range_error("WebSocket compression parameter is out of range");
						}
						This<O>(C).*Field = static_cast<std::int32_t>(Value);
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
		for (const bool Client : {true, false})
		{
			const auto Name = Client ? "client_no_context_takeover" : "server_no_context_takeover";
			if (!Member(
					S,
					Compression,
					Name,
					T::Bool,
					{},
					[Client](const auto& C, auto& Out, auto&)
					{
						const auto& V = This<O>(C);
						Out = Client ? V.bClientNoContextTakeover : V.bServerNoContextTakeover;
						return true;
					},
					Error) ||
				!Member(
					S,
					Compression,
					("set_" + std::string(Name)).c_str(),
					T::Void,
					{T::Bool},
					[Client](const auto& C, auto&, auto&)
					{
						auto& V = This<O>(C);
						(Client ? V.bClientNoContextTakeover : V.bServerNoContextTakeover) = std::get<bool>(C.Args[0]);
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
		for (const std::string Name : {"enabled",
				 "client_window_bits",
				 "server_window_bits",
				 "client_no_context_takeover",
				 "server_no_context_takeover"})
		{
			const bool Number = Name == "client_window_bits" || Name == "server_window_bits";
			if (!Member(
					S,
					Info,
					Name.c_str(),
					Number ? T::Int64 : T::Bool,
					{},
					[Name](const auto& C, auto& Out, auto&)
					{
						const auto& V = This<I>(C);
						if (Name == "enabled")
						{
							Out = V.bEnabled;
						}
						else if (Name == "client_window_bits")
						{
							Out = static_cast<std::int64_t>(V.ClientWindowBits);
						}
						else if (Name == "server_window_bits")
						{
							Out = static_cast<std::int64_t>(V.ServerWindowBits);
						}
						else if (Name == "client_no_context_takeover")
						{
							Out = V.bClientNoContextTakeover;
						}
						else
						{
							Out = V.bServerNoContextTakeover;
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
