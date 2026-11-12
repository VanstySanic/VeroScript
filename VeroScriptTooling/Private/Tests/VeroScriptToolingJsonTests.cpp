// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingJson.h"

#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	using VeroScript::Tooling::Detail::FJson;
	using VeroScript::Tooling::Detail::ParseJson;
	using VeroScript::Tooling::Detail::ToJsonString;

	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << Message << '\n';
		}
		return Condition;
	}

	bool ExpectError(const std::string_view Text, const std::string_view Message)
	{
		try
		{
			ParseJson(std::string(Text));
		}
		catch (const std::runtime_error& Error)
		{
			return Expect(Error.what() == Message, "JSON error text changed");
		}
		return Expect(false, "Malformed JSON was accepted");
	}
}

int main()
{
	bool Ok = true;
	const auto Root = ParseJson(
		R"JSON({"z":[null,true,false,12.5,-2e3],"s":"\"\\\/\b\f\n\r\t\u0000\uD83D\uDE00","i":42,"fraction":1.5})JSON");
	const std::string Expected = std::string("\"\\/\b\f\n\r\t\0", 9) + "\xf0\x9f\x98\x80";
	Ok &= Expect(Root.GetString("s") == Expected, "Escapes and UTF-16 pairs must decode identically");
	Ok &= Expect(Root.GetInt("i") == 42, "Integer JSON fields must remain readable");
	Ok &= Expect(Root.GetInt("fraction", 7) == 7, "Fractional fields must not narrow to integers");
	Ok &= Expect(Root.GetInt("missing", 9) == 9, "Missing fields must preserve the default");
	Ok &= Expect(Root.GetInt("s", 11) == 11, "Non-numeric fields must preserve the default");
	const auto* Values = Root.Find("z");
	Ok &= Expect(Values && Values->ArrayPtr() && Values->ArrayPtr()->size() == 5,
		"Arrays must preserve all values");
	if (Values && Values->ArrayPtr() && !Values->ArrayPtr()->empty())
	{
		Ok &= Expect(Values->ArrayPtr()->front().IsNull(), "Null values must remain distinguishable");
	}
	const auto Encoded = ToJsonString(Root);
	Ok &= Expect(ParseJson(Encoded).GetString("s") == Expected,
		"Serialization must round-trip decoded strings");
	Ok &= Expect(ToJsonString(ParseJson(R"JSON({"z":1,"a":2})JSON")) == R"JSON({"a":2,"z":1})JSON",
		"Object key order must remain deterministic");
	Ok &= Expect(ToJsonString(FJson::String(std::string("\0\b\f\n\r\t\"\\", 8))) ==
		R"JSON("\u0000\b\f\n\r\t\"\\")JSON", "Wire strings must preserve short escape spelling");
	Ok &= Expect(ToJsonString(ParseJson(" [ true, false, null, -12.5e2 ] ")) == "[true,false,null,-1250]",
		"Value types and number serialization must remain unchanged");

	for (const double Number : {
		std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(),
		static_cast<double>(std::numeric_limits<int>::max()) + 1.0,
		static_cast<double>(std::numeric_limits<int>::min()) - 1.0})
	{
		Ok &= Expect(FJson::Object({{"value", FJson::Number(Number)}}).GetInt("value", 17) == 17,
			"Unrepresentable integers must preserve the default");
	}
	const std::pair<std::string_view, std::string_view> Errors[] = {
		{"", "invalid number"},
		{"01", "invalid leading zero in number"},
		{"1.", "invalid number fraction"},
		{"1e+", "invalid number exponent"},
		{"1e9999", "invalid number"},
		{"null false", "trailing JSON content"},
		{R"JSON({"a":1,"a":2})JSON", "duplicate object key"},
		{R"JSON("\x")JSON", "invalid escape"},
		{R"JSON("\u12x4")JSON", "invalid unicode escape"},
		{R"JSON("\uD800")JSON", "incomplete UTF-16 surrogate pair"},
		{R"JSON("\uDC00")JSON", "lone UTF-16 low surrogate"},
		{R"JSON("\uD800\u0001")JSON", "invalid UTF-16 low surrogate"},
		{"\"line\nbreak\"", "unescaped control character in string"},
		{"\"unterminated", "unterminated string"}
	};
	for (const auto& Error : Errors)
	{
		Ok &= ExpectError(Error.first, Error.second);
	}
	const auto Deep = std::string(255, '[') + '0' + std::string(255, ']');
	Ok &= Expect(ToJsonString(ParseJson(Deep)) == Deep, "Existing depth boundary must remain accepted");
	Ok &= ExpectError('[' + Deep + ']', "JSON nested too deeply");
	return Ok ? 0 : 1;
}
