// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cerrno>
#include <algorithm>
#include <cfenv>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <locale.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace VeroScript::FloatParsingDetail
{
	template <class T, class = void>
	struct THasFloatFromChars : std::false_type
	{
	};
	template <class T>
	struct THasFloatFromChars<T, std::void_t<decltype(std::from_chars(std::declval<const char*>(), std::declval<const char*>(), std::declval<T&>(), std::chars_format::general))>> : std::true_type
	{
	};

	class FPreserveErrno
	{
	public:
		~FPreserveErrno()
		{
			errno = Previous;
		}

	private:
		int Previous = errno;
	};

	class FNearestRounding
	{
	public:
		FNearestRounding()
		    : Previous(std::fegetround()), Ready(Previous != -1 && (Previous == FE_TONEAREST || std::fesetround(FE_TONEAREST) == 0))
		{
		}
		~FNearestRounding()
		{
			if (Ready && Previous != FE_TONEAREST)
			{
				std::fesetround(Previous);
			}
		}
		bool IsReady() const
		{
			return Ready;
		}

	private:
		int Previous;
		bool Ready;
	};

	#if !defined(_WIN32)
	class FNumericLocale
	{
	public:
		FNumericLocale() : Value(newlocale(LC_NUMERIC_MASK, "C", nullptr))
		{
		}
		~FNumericLocale()
		{
			if (Value)
			{
				freelocale(Value);
			}
		}
		locale_t Get() const
		{
			return Value;
		}

	private:
		locale_t Value;
	};

	class FScopedNumericLocale
	{
	public:
		explicit FScopedNumericLocale(locale_t Locale)
		    : Previous(Locale ? uselocale(Locale) : nullptr)
		{
		}
		~FScopedNumericLocale()
		{
			if (Previous)
			{
				uselocale(Previous);
			}
		}
		bool IsReady() const
		{
			return Previous != nullptr;
		}

	private:
		locale_t Previous;
	};
	#endif

	// Some libc++ releases provide integer charconv without floating-point
	// from_chars. Keep the fallback private and parse directly to T to avoid
	// double rounding at binary32 midpoints.
	template <class T>
	std::from_chars_result FromCharsDecimalFallback(std::string_view Text, T& Output)
	{
		static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
		FPreserveErrno SavedErrno;
		const char* const Begin = Text.data();
		if (Text.empty()) return {Begin, std::errc::invalid_argument};
		const char* const End = Begin + Text.size();
		std::size_t Position = Text.front() == '-' ? 1 : 0;
		// Every binary32/64 rounding boundary has a terminating decimal
		// expansion shorter than this prefix. A sticky digit distinguishes an
		// exact boundary from a nonzero tail while bounding the C conversion.
		constexpr std::size_t PrefixLimit = 2048;
		std::string Digits;
		Digits.reserve(std::min(Text.size(), PrefixLimit));
		std::size_t DigitCount = 0, IntegerDigits = 0, First = 0;
		bool Fractional = false, Nonzero = false, Sticky = false;
		const auto ScanDigits = [&]
		{
			for (; Position < Text.size() && Text[Position] >= '0' && Text[Position] <= '9'; ++Position)
			{
				const char C = Text[Position];
				if (!Fractional)
				{
					++IntegerDigits;
				}
				if (!Nonzero && C != '0')
				{
					First = DigitCount;
					Nonzero = true;
				}
				++DigitCount;
				if (Nonzero)
				{
					if (Digits.size() < PrefixLimit)
					{
						Digits += C;
					}
					else
					{
						Sticky |= C != '0';
					}
				}
			}
		};
		ScanDigits();
		if (Position < Text.size() && Text[Position] == '.')
		{
			++Position;
			Fractional = true;
			ScanDigits();
		}
		if (!DigitCount) return {Begin, std::errc::invalid_argument};
		bool ExponentNegative = false, ExponentOverflow = false;
		std::uint64_t Exponent = 0;
		if (Position < Text.size() && (Text[Position] == 'e' || Text[Position] == 'E'))
		{
			++Position;
			if (Position < Text.size() && (Text[Position] == '+' || Text[Position] == '-'))
			{
				ExponentNegative = Text[Position++] == '-';
			}
			const auto ExponentBegin = Position;
			for (; Position < Text.size() && Text[Position] >= '0' && Text[Position] <= '9'; ++Position)
			{
				const unsigned Digit = Text[Position] - '0';
				if (Exponent > (std::numeric_limits<std::uint64_t>::max() - Digit) / 10)
				{
					ExponentOverflow = true;
				}
				else if (!ExponentOverflow)
				{
					Exponent = Exponent * 10 + Digit;
				}
			}
			if (Position == ExponentBegin) return {Begin, std::errc::invalid_argument};
		}
		if (Position != Text.size()) return {Begin, std::errc::invalid_argument};
		if (!Nonzero)
		{
			Output = Text.front() == '-' ? -T{0} : T{0};
			return {End, std::errc{}};
		}
		if (ExponentOverflow) return {End, std::errc::result_out_of_range};
		const bool OffsetNegative = First >= IntegerDigits;
		const std::uint64_t Offset = OffsetNegative ? First - IntegerDigits + 1 : IntegerDigits - First - 1;
		bool CombinedNegative = ExponentNegative;
		std::uint64_t Magnitude;
		if (ExponentNegative == OffsetNegative)
		{
			if (Exponent > std::numeric_limits<std::uint64_t>::max() - Offset) return {End, std::errc::result_out_of_range};
			Magnitude = Exponent + Offset;
		}
		else if (Exponent >= Offset)
		{
			Magnitude = Exponent - Offset;
		}
		else
		{
			Magnitude = Offset - Exponent;
			CombinedNegative = OffsetNegative;
		}
		if (Magnitude > 4096) return {End, std::errc::result_out_of_range};
		std::string Terminated;
		Terminated.reserve(Digits.size() + 16);
		if (Text.front() == '-')
		{
			Terminated += '-';
		}
		Terminated += Digits.front();
		Terminated += '.';
		Terminated.append(Digits.data() + 1, Digits.size() - 1);
		if (Sticky)
		{
			Terminated += '1';
		}
		Terminated += 'e';
		if (CombinedNegative)
		{
			Terminated += '-';
		}
		Terminated += std::to_string(Magnitude);

		T Value;
		#if defined(_WIN32)
		// Supported Windows standard libraries already provide float charconv.
		// UCRT strto functions discard distant nonzero tails, so retain charconv
		// after bounded canonicalization even when this path is tested directly.
		const auto Parsed = std::from_chars(Terminated.data(), Terminated.data() + Terminated.size(),
		    Value, std::chars_format::general);
		if (Parsed.ec != std::errc{}) return {Parsed.ec == std::errc::result_out_of_range ? End : Begin, Parsed.ec};
		if (Parsed.ptr != Terminated.data() + Terminated.size()) return {Begin, std::errc::invalid_argument};
		#else
		FNearestRounding Rounding;
		static const FNumericLocale Locale;
		if (!Rounding.IsReady() || !Locale.Get()) return {Begin, std::errc::invalid_argument};
		char* ParsedEnd = nullptr;
		// uselocale is available before Android API 26, unlike strtof_l/strtod_l.
		// It affects this thread only and leaves the process locale unchanged.
		FScopedNumericLocale ScopedLocale(Locale.Get());
		if (!ScopedLocale.IsReady()) return {Begin, std::errc::invalid_argument};
		if constexpr (std::is_same_v<T, float>)
		{
			Value = std::strtof(Terminated.c_str(), &ParsedEnd);
		}
		else
		{
			Value = std::strtod(Terminated.c_str(), &ParsedEnd);
		}
		if (ParsedEnd != Terminated.c_str() + Terminated.size()) return {Begin, std::errc::invalid_argument};
		#endif
		// C libraries may set ERANGE for a correctly rounded nonzero subnormal.
		if (!std::isfinite(Value) || (Nonzero && Value == T{0})) return {End, std::errc::result_out_of_range};
		Output = Value;
		return {End, std::errc{}};
	}

	template <class T>
	std::from_chars_result FromCharsDecimal(std::string_view Text, T& Output)
	{
		if (Text.empty()) return {Text.data(), std::errc::invalid_argument};
		if constexpr (THasFloatFromChars<T>::value)
		{
			return std::from_chars(Text.data(), Text.data() + Text.size(), Output, std::chars_format::general);
		}
		else
		{
			return FromCharsDecimalFallback(Text, Output);
		}
	}
}
