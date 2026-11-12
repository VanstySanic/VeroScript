// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptFloatParsing.h"
#include <cstdio>
#include <cstring>

namespace VeroScript::FloatFormattingDetail
{
	enum class EDecimalFormat
	{
		Shortest, General
	};

	struct FDecimal
	{
		std::uint64_t Mantissa = 0;
		int Exponent = 0;
	};

	template <class T, class = void>
	struct THasFloatToChars : std::false_type
	{
	};
	template <class T>
	struct THasFloatToChars<T, std::void_t<decltype(std::to_chars(std::declval<char*>(),
		std::declval<char*>(), std::declval<T>(), std::chars_format::general))>> : std::true_type
	{
	};

	inline int PrintDecimal(char* Buffer, std::size_t Size, const char* Format, int Precision, double Value)
	{
		#if defined(_WIN32)
		struct FLocale
		{
			~FLocale()
			{
				if (Value)
				{
					_free_locale(Value);
				}
			}
			_locale_t Value = _create_locale(LC_NUMERIC, "C");
		};
		static const FLocale Locale;
		return Locale.Value ? _snprintf_s_l(Buffer, Size, _TRUNCATE, Format, Locale.Value, Precision, Value) : -1;
		#else
		static const FloatParsingDetail::FNumericLocale Locale;
		FloatParsingDetail::FScopedNumericLocale Scope(Locale.Get());
		return Scope.IsReady() ? std::snprintf(Buffer, Size, Format, Precision, Value) : -1;
		#endif
	}

	inline std::to_chars_result CopyDecimal(char* First, char* Last, const char* Text, std::size_t Size)
	{
		if (Size > static_cast<std::size_t>(Last - First)) return {Last, std::errc::value_too_large};
		std::memcpy(First, Text, Size);
		return {First + Size, {}};
	}

	template <class T>
	bool DecimalCandidate(T Value, int Precision, FDecimal& Output)
	{
		char Text[64];
		const int Size = PrintDecimal(Text, sizeof(Text), "%.*e", Precision - 1, static_cast<double>(Value));
		if (Size <= 0 || Size >= static_cast<int>(sizeof(Text))) return false;
		const char* Exponent = std::strchr(Text, 'e');
		if (!Exponent) return false;
		std::uint64_t Mantissa = 0;
		for (const char* P = Text; P != Exponent; ++P)
		{
			if (*P >= '0' && *P <= '9')
			{
				Mantissa = Mantissa * 10 + static_cast<unsigned>(*P - '0');
			}
		}
		int Power = 0;
		++Exponent;
		if (*Exponent == '+')
		{
			++Exponent;
		}
		const auto Parsed = std::from_chars(Exponent, Text + Size, Power);
		if (Parsed.ec != std::errc{} || Parsed.ptr != Text + Size) return false;
		Power -= Precision - 1;
		const auto Matches = [&](std::uint64_t Digits)
		{
			char Candidate[64];
			char* End = Candidate;
			if (std::signbit(Value))
			{
				*End++ = '-';
			}
			const auto Number = std::to_chars(End, Candidate + sizeof(Candidate) - 12, Digits);
			if (Number.ec != std::errc{}) return false;
			End = Number.ptr;
			*End++ = 'e';
			const auto Scale = std::to_chars(End, Candidate + sizeof(Candidate), Power);
			if (Scale.ec != std::errc{}) return false;
			End = Scale.ptr;
			T Roundtrip{};
			const auto Result = FloatParsingDetail::FromCharsDecimal(
				std::string_view(Candidate, static_cast<std::size_t>(End - Candidate)), Roundtrip);
			if (Result.ec != std::errc{} || Result.ptr != End || Roundtrip != Value) return false;
			Output = {Digits, Power};
			return true;
		};
		// At binary exponent boundaries the rounding interval is asymmetric.
		// The closest decimal can miss it while an adjacent decimal fits.
		return Matches(Mantissa) || (Mantissa > 1 && Matches(Mantissa - 1)) || Matches(Mantissa + 1);
	}

	// Bounded compatibility path for standard libraries without floating charconv.
	// Decimal grids are nested: binary search finds the fewest significant digits
	// that round back to the original float/double, retaining its original type.
	template <class T>
	std::to_chars_result ToCharsDecimalFallback(char* First, char* Last, T Value,
		EDecimalFormat Format = EDecimalFormat::Shortest, int Precision = 0)
	{
		static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
		if (!std::isfinite(Value) || Precision < 0 || Precision > std::numeric_limits<T>::max_digits10)
		{
			return {First, std::errc::invalid_argument};
		}
		FloatParsingDetail::FPreserveErrno Errno;
		FloatParsingDetail::FNearestRounding Rounding;
		if (!Rounding.IsReady()) return {First, std::errc::invalid_argument};
		char Text[512];
		if (Precision)
		{
			const int Size = PrintDecimal(Text, sizeof(Text), "%.*g", Precision, static_cast<double>(Value));
			if (Size <= 0 || Size >= static_cast<int>(sizeof(Text))) return {First, std::errc::invalid_argument};
			return CopyDecimal(First, Last, Text, static_cast<std::size_t>(Size));
		}
		if (Value == T{0}) return CopyDecimal(First, Last, std::signbit(Value) ? "-0" : "0", std::signbit(Value) ? 2 : 1);
		const T IntegerLimit = Format == EDecimalFormat::General ? T{999999} : T{99999};
		if (Value >= -IntegerLimit && Value <= IntegerLimit && std::trunc(Value) == Value)
		{
			return std::to_chars(First, Last, static_cast<std::int64_t>(Value));
		}
		int Low = 1, High = std::numeric_limits<T>::max_digits10;
		FDecimal Decimal;
		while (Low < High)
		{
			const int Middle = (Low + High) / 2;
			if (DecimalCandidate(Value, Middle, Decimal))
			{
				High = Middle;
			}
			else
			{
				Low = Middle + 1;
			}
		}
		if (!DecimalCandidate(Value, Low, Decimal)) return {First, std::errc::invalid_argument};
		while (Decimal.Mantissa % 10 == 0)
		{
			Decimal.Mantissa /= 10;
			++Decimal.Exponent;
		}
		char Digits[32];
		const int Count = static_cast<int>(std::to_chars(Digits, Digits + sizeof(Digits), Decimal.Mantissa).ptr - Digits);
		const int Point = Count + Decimal.Exponent;
		const int Exponent = Point - 1;
		const int FixedSize = Point <= 0 ? 2 - Point + Count : Point >= Count ? Point : Count + 1;
		const int Magnitude = Exponent < 0 ? -Exponent : Exponent;
		const int ScientificSize = Count + (Count > 1 ? 1 : 0) + 2 + (Magnitude >= 100 ? 3 : 2);
		const bool Fixed = Format == EDecimalFormat::General ? Exponent >= -4 && Exponent < 6 : FixedSize <= ScientificSize;
		if (Fixed && Decimal.Exponent >= 0)
		{
			// Fixed integer notation has spare digits without increasing its size.
			// Choose the exact integer, e.g. 2^60 rather than its rounded 16-digit decimal.
			const int Size = PrintDecimal(Text, sizeof(Text), "%.*f", 0, static_cast<double>(Value));
			if (Size <= 0 || Size >= static_cast<int>(sizeof(Text))) return {First, std::errc::invalid_argument};
			return CopyDecimal(First, Last, Text, static_cast<std::size_t>(Size));
		}
		char* End = Text;
		if (std::signbit(Value))
		{
			*End++ = '-';
		}
		if (Fixed)
		{
			if (Point <= 0)
			{
				*End++ = '0';
				*End++ = '.';
				for (int I = 0; I < -Point; ++I)
				{
					*End++ = '0';
				}
			}
			for (int I = 0; I < Count; ++I)
			{
				if (I == Point && Point > 0)
				{
					*End++ = '.';
				}
				*End++ = Digits[I];
			}
		}
		else
		{
			*End++ = Digits[0];
			if (Count > 1)
			{
				*End++ = '.';
			}
			for (int I = 1; I < Count; ++I)
			{
				*End++ = Digits[I];
			}
			*End++ = 'e';
			*End++ = Exponent < 0 ? '-' : '+';
			if (Magnitude < 10)
			{
				*End++ = '0';
			}
			End = std::to_chars(End, Text + sizeof(Text), Magnitude).ptr;
		}
		return CopyDecimal(First, Last, Text, static_cast<std::size_t>(End - Text));
	}

	template <class T>
	std::to_chars_result ToCharsDecimal(char* First, char* Last, T Value)
	{
		if constexpr (THasFloatToChars<T>::value)
		{
			return std::to_chars(First, Last, Value);
		}
		else
		{
			return ToCharsDecimalFallback(First, Last, Value);
		}
	}
	template <class T>
	std::to_chars_result ToCharsGeneral(char* First, char* Last, T Value)
	{
		if constexpr (THasFloatToChars<T>::value)
		{
			return std::to_chars(First, Last, Value, std::chars_format::general);
		}
		else
		{
			return ToCharsDecimalFallback(First, Last, Value, EDecimalFormat::General);
		}
	}
	template <class T>
	std::to_chars_result ToCharsGeneral(char* First, char* Last, T Value, int Precision)
	{
		if constexpr (THasFloatToChars<T>::value)
		{
			return std::to_chars(First, Last, Value, std::chars_format::general, Precision);
		}
		else
		{
			return ToCharsDecimalFallback(First, Last, Value, EDecimalFormat::General, Precision);
		}
	}
}
