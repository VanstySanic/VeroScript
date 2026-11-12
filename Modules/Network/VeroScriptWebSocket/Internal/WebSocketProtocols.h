// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace VeroScript::WebSocket::Detail
{
	inline bool ProtocolToken(std::string_view Text)
	{
		if (Text.empty())
		{
			return false;
		}
		for (const auto C : Text)
		{
			if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
					std::string_view("!#$%&'*+-.^_`|~").find(C) != std::string_view::npos))
			{
				return false;
			}
		}
		return true;
	}
	inline bool ValidProtocols(const std::vector<std::string>& Values, bool Required)
	{
		if ((Required && Values.empty()) || Values.size() > 128)
		{
			return false;
		}
		std::size_t Bytes = 0;
		for (auto It = Values.begin(); It != Values.end(); ++It)
		{
			const auto Separator = It == Values.begin() ? 0u : 2u;
			if (!ProtocolToken(*It) || Separator > 16384 - Bytes || It->size() > 16384 - Bytes - Separator ||
				std::find(Values.begin(), It, *It) != It)
			{
				return false;
			}
			Bytes += It->size() + Separator;
		}
		return true;
	}
	inline bool ParseProtocols(std::string_view Text, std::vector<std::string>& Values)
	{
		if (Text.size() > 16384)
		{
			return false;
		}
		for (;;)
		{
			const auto Comma = Text.find(',');
			auto Token = Text.substr(0, Comma);
			while (!Token.empty() && (Token.front() == ' ' || Token.front() == '\t'))
			{
				Token.remove_prefix(1);
			}
			while (!Token.empty() && (Token.back() == ' ' || Token.back() == '\t'))
			{
				Token.remove_suffix(1);
			}
			if (!ProtocolToken(Token) || Values.size() >= 128 ||
				std::find(Values.begin(), Values.end(), Token) != Values.end())
			{
				return false;
			}
			Values.emplace_back(Token);
			if (Comma == Text.npos)
			{
				return true;
			}
			Text.remove_prefix(Comma + 1);
		}
	}
} // namespace VeroScript::WebSocket::Detail
