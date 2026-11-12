// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonString.h"

#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	using VeroScript::JsonStringDetail::AppendEscaped;
	using VeroScript::JsonStringDetail::EControlEscapeStyle;
	using VeroScript::JsonStringDetail::Escape;

	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << Message << '\n';
		}
		return Condition;
	}
}

int main()
{
	bool Ok = true;
	std::string Controls;
	for (int Character = 0; Character < 0x20; ++Character)
	{
		Controls.push_back(static_cast<char>(Character));
	}
	const std::string_view UnicodeControls =
		R"JSON(\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\u0008\t\n\u000b\u000c\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f)JSON";
	const std::string_view ShortControls =
		R"JSON(\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f)JSON";
	Ok &= Expect(Escape(Controls, EControlEscapeStyle::Unicode) == UnicodeControls,
		"Unicode style must escape every control character");
	Ok &= Expect(Escape(Controls, EControlEscapeStyle::Short) == ShortControls,
		"Short style must preserve all existing JSON short escapes");

	for (const auto Style : {EControlEscapeStyle::Unicode, EControlEscapeStyle::Short})
	{
		Ok &= Expect(Escape({}, Style).empty(), "Empty views must be accepted");
		Ok &= Expect(Escape("plain text /", Style) == "plain text /",
			"Ordinary bytes and slash must remain unchanged");
		Ok &= Expect(Escape("\"\\", Style) == R"JSON(\"\\)JSON",
			"Quote and backslash must be escaped without adding outer quotes");
		const std::string Utf8 = "\xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80";
		Ok &= Expect(Escape(Utf8, Style) == Utf8, "UTF-8 bytes must be preserved");
		const std::string Bytes("\x7f\x80\xff", 3);
		Ok &= Expect(Escape(Bytes, Style) == Bytes,
			"UTF-8 validation must remain the caller's responsibility");
		const std::string EmbeddedNull("left\0right", 10);
		Ok &= Expect(Escape(EmbeddedNull, Style) == "left\\u0000right",
			"Embedded NUL must not truncate the input");
		const std::string Storage = "before:value\nafter";
		Ok &= Expect(Escape(std::string_view(Storage).substr(7, 6), Style) == "value\\n",
			"Views must respect their explicit length");

		std::string Out = "prefix:";
		AppendEscaped(Out, "value\n", Style);
		AppendEscaped(Out, {}, Style);
		Out += ":suffix";
		Ok &= Expect(Out == "prefix:value\\n:suffix",
			"Appending must preserve existing output");
		std::string Long(1024 * 1024, 'x');
		Ok &= Expect(Escape(Long, Style) == Long, "Long ordinary spans must be preserved");
		Long += "\n";
		Ok &= Expect(Escape(Long, Style) == Long.substr(0, Long.size() - 1) + "\\n",
			"Escapes after long spans must be retained");
		const std::string Backslashes(16384, '\\');
		Ok &= Expect(Escape(Backslashes, Style) == std::string(32768, '\\'),
			"Escape-dense inputs must not lose bytes");
	}
	return Ok ? 0 : 1;
}
