// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

namespace VeroScript::Network::UrlDetail
{
	inline int Hex(unsigned char C)
	{
		if (C >= '0' && C <= '9')
		{
			return C - '0';
		}
		if (C >= 'a' && C <= 'f')
		{
			return C - 'a' + 10;
		}
		if (C >= 'A' && C <= 'F')
		{
			return C - 'A' + 10;
		}
		return -1;
	}

	inline bool ValidText(std::string_view Text)
	{
		for (std::size_t I = 0; I < Text.size(); ++I)
		{
			const auto C = static_cast<unsigned char>(Text[I]);
			if (C <= 0x20 || C == 0x7f || C == '\\')
			{
				return false;
			}
			if (C == '%')
			{
				if (Text.size() - I < 3 || Hex(Text[I + 1]) < 0 || Hex(Text[I + 2]) < 0)
				{
					return false;
				}
				I += 2;
			}
		}
		return true;
	}
} // namespace VeroScript::Network::UrlDetail
