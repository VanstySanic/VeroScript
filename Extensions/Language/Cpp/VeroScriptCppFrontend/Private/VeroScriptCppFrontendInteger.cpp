// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppFrontendInteger.h"
#include <algorithm>
#include <charconv>
#include <climits>

namespace VeroScript::CppFrontend::Detail
{
	bool ParseIntegerLiteral(std::string_view Text, FIntegerLiteral& Out, std::string& Error)
	{
		Out = {};
		Error.clear();
		std::string Number(Text);
		const bool Hex = Number.size() > 2 && Number[0] == '0' && (Number[1] == 'x' || Number[1] == 'X');
		const bool Binary = Number.size() > 2 && Number[0] == '0' && (Number[1] == 'b' || Number[1] == 'B');
		const auto Digit = [&](char C)
		{
			return (C >= '0' && C <= '9') || (Hex && ((C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F')));
		};
		for (std::size_t I = 0; I < Number.size(); ++I)
		{
			if (Number[I] == '\'' &&
				(I == 0 || I + 1 == Number.size() || !Digit(Number[I - 1]) || !Digit(Number[I + 1])))
			{
				Error = "invalid C++ digit separator: " + std::string(Text);
				return false;
			}
		}

		Number.erase(std::remove(Number.begin(), Number.end(), '\''), Number.end());
		const int Base = Hex ? 16 : Binary ? 2 : Number.size() > 1 && Number[0] == '0' ? 8 : 10;
		const std::size_t Begin = Hex || Binary ? 2 : 0;
		std::uint64_t Value = 0;
		const auto Parsed = std::from_chars(Number.data() + Begin, Number.data() + Number.size(), Value, Base);
		if (Parsed.ec != std::errc{} || Parsed.ptr == Number.data() + Begin)
		{
			Error = "invalid or out-of-range C++ integer literal: " + std::string(Text);
			return false;
		}
		std::string Suffix(Parsed.ptr, static_cast<const char*>(Number.data() + Number.size()));
		for (std::size_t I = 1; I < Suffix.size(); ++I)
		{
			if ((Suffix[I - 1] == 'l' || Suffix[I - 1] == 'L') && (Suffix[I] == 'l' || Suffix[I] == 'L') &&
				Suffix[I - 1] != Suffix[I])
			{
				Error = "invalid C++ long long literal suffix: " + std::string(Text);
				return false;
			}
		}

		std::transform(Suffix.begin(), Suffix.end(), Suffix.begin(),
			[](char C)
			{
				return static_cast<char>(C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C);
			});
		if (!Suffix.empty() && Suffix != "u" && Suffix != "l" && Suffix != "ll" && Suffix != "ul" &&
			Suffix != "lu" && Suffix != "ull" && Suffix != "llu")
		{
			Error = "unsupported C++ integer literal suffix: " + std::string(Text);
			return false;
		}
		const bool Unsigned = Suffix.find('u') != std::string::npos;
		const unsigned Longs = static_cast<unsigned>(std::count(Suffix.begin(), Suffix.end(), 'l'));
		const auto Select = [&](std::uint64_t Maximum, unsigned Width, bool UnsignedType)
		{
			if (Value > Maximum) return false;
			Out = {Value, Width, UnsignedType};
			return true;
		};
		if (Longs == 0)
		{
			if (!Unsigned && Select(std::numeric_limits<int>::max(), sizeof(int) * CHAR_BIT, false)) return true;
			if ((Unsigned || Base != 10) && Select(std::numeric_limits<unsigned int>::max(), sizeof(unsigned int) * CHAR_BIT, true)) return true;
		}
		if (Longs < 2)
		{
			if (!Unsigned && Select(std::numeric_limits<long>::max(), sizeof(long) * CHAR_BIT, false)) return true;
			if ((Unsigned || Base != 10) && Select(std::numeric_limits<unsigned long>::max(), sizeof(unsigned long) * CHAR_BIT, true)) return true;
		}
		if (!Unsigned && Select(std::numeric_limits<long long>::max(), sizeof(long long) * CHAR_BIT, false)) return true;
		if ((Unsigned || Base != 10) && Select(std::numeric_limits<unsigned long long>::max(), sizeof(unsigned long long) * CHAR_BIT, true)) return true;
		Error = "C++ integer literal exceeds its permitted types: " + std::string(Text);
		return false;
	}
}
