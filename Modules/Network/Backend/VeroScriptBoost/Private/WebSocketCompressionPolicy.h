// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptWebSocket.h"
#include <boost/beast/http.hpp>
#include <boost/beast/websocket/option.hpp>
#include <algorithm>
#include <string>
#include <string_view>

namespace VeroScript::WebSocket::Detail
{
	inline bool ValidCompressionOptions(const FOptions& O)
	{
		const auto& C = O.Compression;
		return C.ClientMaxWindowBits >= 9 && C.ClientMaxWindowBits <= 15 && C.ServerMaxWindowBits >= 9 &&
			   C.ServerMaxWindowBits <= 15 && C.Level >= 0 && C.Level <= 9 && C.MemoryLevel >= 1 && C.MemoryLevel <= 9;
	}
	inline bool CompressionToken(unsigned char C)
	{
		return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
			   std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(C)) != std::string_view::npos;
	}
	inline bool CompressionName(std::string_view A, std::string_view B)
	{
		if (A.size() != B.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.size(); ++I)
		{
			const auto C = static_cast<unsigned char>(A[I]);
			if ((C >= 'A' && C <= 'Z' ? C + 'a' - 'A' : C) != static_cast<unsigned char>(B[I]))
			{
				return false;
			}
		}
		return true;
	}
	// Parse a single extension completely, including quoted numeric values. No prefix acceptance,
	// unknown parameters, duplicate parameters or unconsumed comma-separated selections.
	inline bool ParseCompression(std::string_view Input, FCompressionInfo& Out, bool Offer, unsigned* Present = nullptr)
	{
		FCompressionInfo Value;
		Value.ClientWindowBits = Value.ServerWindowBits = 15;
		const auto Space = [&]
		{
			while (!Input.empty() && (Input.front() == ' ' || Input.front() == '\t'))
			{
				Input.remove_prefix(1);
			}
		};
		const auto Token = [&]()
		{
			std::size_t Size = 0;
			while (Size < Input.size() && CompressionToken(static_cast<unsigned char>(Input[Size])))
			{
				++Size;
			}
			const auto Result = Input.substr(0, Size);
			Input.remove_prefix(Size);
			return Result;
		};
		Space();
		if (!CompressionName(Token(), "permessage-deflate"))
		{
			return false;
		}
		unsigned Seen = 0;
		Space();
		while (!Input.empty())
		{
			if (Input.front() != ';')
			{
				return false;
			}
			Input.remove_prefix(1);
			Space();
			const auto Name = Token();
			const unsigned Bit = CompressionName(Name, "server_no_context_takeover")   ? 1u
								 : CompressionName(Name, "client_no_context_takeover") ? 2u
								 : CompressionName(Name, "server_max_window_bits")	   ? 4u
								 : CompressionName(Name, "client_max_window_bits")	   ? 8u
																					   : 0u;
			if (!Bit || (Seen & Bit))
			{
				return false;
			}
			Seen |= Bit;
			Space();
			const bool HasValue = !Input.empty() && Input.front() == '=';
			if (Bit <= 2)
			{
				if (HasValue)
				{
					return false;
				}
				if (Bit == 1)
				{
					Value.bServerNoContextTakeover = true;
				}
				else
				{
					Value.bClientNoContextTakeover = true;
				}
			}
			else if (!HasValue)
			{
				if (!Offer || Bit != 8)
				{
					return false;
				}
			}
			else
			{
				Input.remove_prefix(1);
				Space();
				char Digits[2];
				std::size_t Count = 0;
				if (!Input.empty() && Input.front() == '"')
				{
					Input.remove_prefix(1);
					while (!Input.empty() && Input.front() != '"')
					{
						if (Input.front() == '\\')
						{
							Input.remove_prefix(1);
						}
						if (Input.empty() || Count == 2 || Input.front() < '0' || Input.front() > '9')
						{
							return false;
						}
						Digits[Count++] = Input.front();
						Input.remove_prefix(1);
					}
					if (Input.empty())
					{
						return false;
					}
					Input.remove_prefix(1);
				}
				else
				{
					const auto Number = Token();
					if (Number.empty() || Number.size() > 2)
					{
						return false;
					}
					for (const char C : Number)
					{
						if (C < '0' || C > '9')
						{
							return false;
						}
						Digits[Count++] = C;
					}
				}
				if (!Count || (Count == 2 && Digits[0] != '1'))
				{
					return false;
				}
				const auto Bits = static_cast<unsigned>(Count == 1 ? Digits[0] - '0' : 10 + Digits[1] - '0');
				if (Bits < 8 || Bits > 15)
				{
					return false;
				}
				if (Bit == 4)
				{
					Value.ServerWindowBits = Bits;
				}
				else
				{
					Value.ClientWindowBits = Bits;
				}
			}
			Space();
		}
		Value.bEnabled = true;
		Out = Value;
		if (Present)
		{
			*Present = Seen;
		}
		return true;
	}
	inline std::string SelectCompressionOffer(std::string_view List, const FCompressionOptions& Options)
	{
		while (!List.empty())
		{
			// A comma in a quoted parameter is not an extension separator.
			bool Quoted = false, Escaped = false;
			std::size_t End = 0;
			for (; End < List.size(); ++End)
			{
				const auto C = List[End];
				if (Escaped)
				{
					Escaped = false;
					continue;
				}
				if (Quoted && C == '\\')
				{
					Escaped = true;
					continue;
				}
				if (C == '"')
				{
					Quoted = !Quoted;
				}
				else if (C == ',' && !Quoted)
				{
					break;
				}
			}
			if (Quoted || Escaped)
			{
				return {};
			}
			FCompressionInfo Offer;
			unsigned Present = 0;
			if (ParseCompression(List.substr(0, End), Offer, true, &Present) && Offer.ServerWindowBits >= 9 &&
				((Present & 8) || Options.ClientMaxWindowBits == 15))
			{
				const auto ServerBits = std::min<unsigned>(Offer.ServerWindowBits, Options.ServerMaxWindowBits);
				const auto ClientBits = std::min<unsigned>(Offer.ClientWindowBits, Options.ClientMaxWindowBits);
				std::string Result = "permessage-deflate";
				if ((Present & 4) || ServerBits < 15)
				{
					Result += "; server_max_window_bits=" + std::to_string(ServerBits);
				}
				if (Present & 8)
				{
					Result += "; client_max_window_bits=" + std::to_string(ClientBits);
				}
				if (Offer.bServerNoContextTakeover || Options.bServerNoContextTakeover)
				{
					Result += "; server_no_context_takeover";
				}
				if (Offer.bClientNoContextTakeover || Options.bClientNoContextTakeover)
				{
					Result += "; client_no_context_takeover";
				}
				return Result;
			}
			if (End == List.size())
			{
				break;
			}
			List.remove_prefix(End + 1);
		}
		return {};
	}
	inline bool ValidateCompressionResponse(const boost::beast::http::response<boost::beast::http::string_body>& R,
		const boost::beast::websocket::permessage_deflate& Offered)
	{
		const auto Field = boost::beast::http::field::sec_websocket_extensions;
		if (!R.count(Field))
		{
			return true;
		}
		const auto Text = R[Field];
		FCompressionInfo Selected;
		return Offered.client_enable && R.count(Field) == 1 &&
			   ParseCompression(std::string_view(Text.data(), Text.size()), Selected, false) &&
			   Selected.ClientWindowBits >= 9 &&
			   Selected.ServerWindowBits <= static_cast<unsigned>(Offered.server_max_window_bits) &&
			   (!Offered.server_no_context_takeover || Selected.bServerNoContextTakeover);
	}
} // namespace VeroScript::WebSocket::Detail

// The pinned Beast response path invokes this before constructing compression state.
#define VEROSCRIPT_NETWORK_VALIDATE_WEBSOCKET_EXTENSIONS(Response, Options)                                            \
	::VeroScript::WebSocket::Detail::ValidateCompressionResponse(Response, Options)
