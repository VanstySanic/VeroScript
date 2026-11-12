// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>

namespace VeroScript::JsonStringDetail
{
	// Both styles use \n, \r and \t. This choice preserves the caller's
	// spelling of backspace and form feed without changing the JSON value.
	enum class EControlEscapeStyle
	{
		Unicode,
		Short
	};

	// Append string contents without surrounding quotes or UTF-8 validation.
	// Text must not refer to Out's storage, which appending may reallocate.
	inline void AppendEscaped(
		std::string& Out,
		const std::string_view Text,
		const EControlEscapeStyle ControlStyle)
	{
		constexpr char Hex[] = "0123456789abcdef";
		for (const unsigned char Character : Text)
		{
			switch (Character)
			{
			case '"':
				{
					Out += "\\\"";
					break;
				}
			case '\\':
				{
					Out += "\\\\";
					break;
				}
			case '\n':
				{
					Out += "\\n";
					break;
				}
			case '\r':
				{
					Out += "\\r";
					break;
				}
			case '\t':
				{
					Out += "\\t";
					break;
				}
			case '\b':
			case '\f':
				{
					if (ControlStyle == EControlEscapeStyle::Short)
					{
						Out.push_back('\\');
						Out.push_back(Character == '\b' ? 'b' : 'f');
						break;
					}
					[[fallthrough]];
				}
			default:
				{
					if (Character < 0x20u)
					{
						Out += "\\u00";
						Out.push_back(Hex[Character >> 4]);
						Out.push_back(Hex[Character & 0x0fu]);
					}
					else
					{
						Out.push_back(static_cast<char>(Character));
					}
					break;
				}
			}
		}
	}

	inline std::string Escape(
		const std::string_view Text,
		const EControlEscapeStyle ControlStyle)
	{
		std::string Out;
		Out.reserve(Text.size());
		AppendEscaped(Out, Text, ControlStyle);
		return Out;
	}
}
