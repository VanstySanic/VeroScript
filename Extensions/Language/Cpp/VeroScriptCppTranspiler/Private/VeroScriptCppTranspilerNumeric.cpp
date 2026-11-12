// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppTranspilerNumeric.h"
#include "VeroScriptCppFrontendInteger.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace VeroScript::CppTranspiler::Detail
{
	bool NormalizeNumberLiteral(const std::string_view Text, std::string& Output, std::string& Error)
	{
		Output.clear();
		Error.clear();
		const bool Hex = Text.size() > 2 && Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X');
		const bool Binary = Text.size() > 2 && Text[0] == '0' && (Text[1] == 'b' || Text[1] == 'B');
		const bool Floating = Text.find('.') != std::string_view::npos
			|| Text.find_first_of(Hex ? "pP" : Binary ? "" : "eE") != std::string_view::npos;
		if (!Floating)
		{
			CppFrontend::Detail::FIntegerLiteral Integer;
			if (!CppFrontend::Detail::ParseIntegerLiteral(Text, Integer, Error)) return false;
			Output = std::to_string(Integer.Value)
				+ (Integer.Width == 64 ? (Integer.bUnsigned ? "UL" : "L") : (Integer.bUnsigned ? "U" : ""));
			return true;
		}
		std::string Number(Text);
		const auto Digit = [&](const char C)
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
		if (Hex || Binary || (!Number.empty() && (Number.back() == 'l' || Number.back() == 'L')))
		{
			Error = "hexadecimal floating literals and long double literals have no supported VeroScript mapping";
			return false;
		}
		std::size_t I = 0, Digits = 0;
		const auto ConsumeDigits = [&]()
		{
			while (I < Number.size() && Number[I] >= '0' && Number[I] <= '9')
			{
				++I;
				++Digits;
			}
		};
		ConsumeDigits();
		if (I < Number.size() && Number[I] == '.')
		{
			++I;
			ConsumeDigits();
		}
		const bool Mantissa = Digits != 0;
		bool Exponent = true;
		if (I < Number.size() && (Number[I] == 'e' || Number[I] == 'E'))
		{
			++I;
			if (I < Number.size() && (Number[I] == '+' || Number[I] == '-'))
			{
				++I;
			}

			Digits = 0;
			ConsumeDigits();
			Exponent = Digits != 0;
		}
		if (I < Number.size() && (Number[I] == 'f' || Number[I] == 'F'))
		{
			++I;
		}

		if (!Mantissa || !Exponent || I != Number.size())
		{
			Error = "unsupported C++ floating literal: " + std::string(Text);
			return false;
		}
		Output = std::move(Number);
		return true;
	}
}
