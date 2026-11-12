// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFloatFormatting.h"
#include <array>
#include <clocale>
#include <iostream>
#include <stdexcept>

using namespace VeroScript::FloatFormattingDetail;

template <class T>
static void Check(T Value)
{
	if (!std::isfinite(Value)) return;
	for (const auto Format : {EDecimalFormat::Shortest, EDecimalFormat::General})
	{
		char Actual[512], Expected[512];
		const auto Result = ToCharsDecimalFallback(Actual, Actual + sizeof(Actual), Value, Format);
		if (Result.ec != std::errc{})
		{
			throw std::runtime_error("floating formatting failed");
		}
		T Parsed{};
		const auto Roundtrip = VeroScript::FloatParsingDetail::FromCharsDecimal(
			std::string_view(Actual, Result.ptr - Actual), Parsed);
		if (Roundtrip.ec != std::errc{} || Roundtrip.ptr != Result.ptr || Parsed != Value ||
			std::signbit(Parsed) != std::signbit(Value))
		{
			throw std::runtime_error("floating formatting lost bits");
		}
		if constexpr (THasFloatToChars<T>::value)
		{
			const auto Native = Format == EDecimalFormat::Shortest ? std::to_chars(Expected, Expected + sizeof(Expected), Value)
				: std::to_chars(Expected, Expected + sizeof(Expected), Value, std::chars_format::general);
			if (Native.ec != std::errc{} || std::string_view(Actual, Result.ptr - Actual) != std::string_view(Expected, Native.ptr - Expected))
			{
				std::cerr << "value " << std::hexfloat << Value << ": actual " << std::string_view(Actual, Result.ptr - Actual)
					<< ", native " << std::string_view(Expected, Native.ptr - Expected) << '\n';
				throw std::runtime_error("floating formatting changed canonical output");
			}
		}
	}
	for (int Precision : {1, 3, std::numeric_limits<T>::max_digits10})
	{
		char Actual[128], Expected[128];
		const auto Result = ToCharsDecimalFallback(Actual, Actual + sizeof(Actual), Value, EDecimalFormat::General, Precision);
		if (Result.ec != std::errc{})
		{
			throw std::runtime_error("precision formatting failed");
		}
		if constexpr (THasFloatToChars<T>::value)
		{
			const auto Native = std::to_chars(Expected, Expected + sizeof(Expected), Value, std::chars_format::general, Precision);
			if (Native.ec != std::errc{} || std::string_view(Actual, Result.ptr - Actual) != std::string_view(Expected, Native.ptr - Expected))
			{
				throw std::runtime_error("precision formatting changed canonical output");
			}
		}
	}
}

int main()
{
	try
	{
		for (double Value : {0.0, -0.0, 0.1, -0.1, 1e4, 1e5, 1e6, 1e-4, 1e-5, 0x1p60,
			std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min()})
		{
			Check(Value);
			Check(static_cast<float>(Value));
		}
		for (int Exponent = -1074; Exponent <= 1023; ++Exponent)
		{
			const double Value = std::ldexp(1.0, Exponent);
			for (double Neighbor : {Value, -Value, std::nextafter(Value, 0.0), std::nextafter(Value, INFINITY)})
			{
				Check(Neighbor);
			}
		}
		for (int Exponent = -149; Exponent <= 127; ++Exponent)
		{
			const float Value = std::ldexp(1.0f, Exponent);
			for (float Neighbor : {Value, -Value, std::nextafter(Value, 0.0f), std::nextafter(Value, INFINITY)})
			{
				Check(Neighbor);
			}
		}
		std::uint64_t Bits = 0x123456789abcdef0ULL;
		for (int I = 0; I < 20000; ++I)
		{
			Bits ^= Bits << 13;
			Bits ^= Bits >> 7;
			Bits ^= Bits << 17;
			double Double;
			std::memcpy(&Double, &Bits, sizeof(Double));
			Check(Double);
			const auto Low = static_cast<std::uint32_t>(Bits);
			float Float;
			std::memcpy(&Float, &Low, sizeof(Float));
			Check(Float);
		}
		char Text[64];
		for (auto Case : {std::pair<double, const char*>{0.1, "0.1"}, {-0.0, "-0"}, {0x1p60, "1152921504606846976"}})
		{
			const auto Result = ToCharsDecimalFallback(Text, Text + sizeof(Text), Case.first);
			if (Result.ec != std::errc{} || std::string_view(Text, Result.ptr - Text) != Case.second)
			{
				throw std::runtime_error("fixed floating formatting contract failed");
			}
		}
		if (ToCharsDecimalFallback(Text, Text, 0.1).ec != std::errc::value_too_large)
		{
			throw std::runtime_error("short output buffer accepted");
		}
		const std::string Locale = std::setlocale(LC_NUMERIC, nullptr);
		for (const char* Name : {"fr_FR.UTF-8", "de_DE.UTF-8", "French_France.1252"})
		{
			if (!std::setlocale(LC_NUMERIC, Name))
			{
				continue;
			}
			const auto Result = ToCharsDecimalFallback(Text, Text + sizeof(Text), 0.1);
			const bool Invariant = Result.ec == std::errc{} && std::string_view(Text, Result.ptr - Text) == "0.1";
			std::setlocale(LC_NUMERIC, Locale.c_str());
			if (!Invariant)
			{
				throw std::runtime_error("formatting depends on the process locale");
			}
			break;
		}
		const int Rounding = std::fegetround();
		if (std::fesetround(FE_UPWARD) == 0)
		{
			errno = EDOM;
			const auto Result = ToCharsDecimalFallback(Text, Text + sizeof(Text), 0.1);
			const bool Preserved = Result.ec == std::errc{} && std::string_view(Text, Result.ptr - Text) == "0.1" &&
				std::fegetround() == FE_UPWARD && errno == EDOM;
			std::fesetround(Rounding);
			if (!Preserved)
			{
				throw std::runtime_error("formatting changed the caller's rounding mode or errno");
			}
		}
		std::cout << "Portable floating formatting, canonical output and roundtrips passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
