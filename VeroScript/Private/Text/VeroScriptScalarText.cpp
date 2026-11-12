// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardLibraryServices.h"
#include "VeroScriptFloatParsing.h"

#include <charconv>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace VeroScript::StandardLibrary
{
	namespace
	{
		constexpr std::size_t WorkBlock = 1024;
		using EStatus = EServiceStatus;
		struct FScalarType
		{
			unsigned Bits;
			bool Unsigned;
			bool Floating;
			bool Boolean;
		};
		FScalarType ScalarType(std::string_view Type) noexcept
		{
			if (Type == "bool") return {1, false, false, true};
			if (Type == "float") return {32, false, true, false};
			if (Type == "double") return {64, false, true, false};
			if (Type == "char" || Type == "int8") return {8, false, false, false};
			if (Type == "unsigned char" || Type == "uint8") return {8, true, false, false};
			if (Type == "short" || Type == "int16") return {16, false, false, false};
			if (Type == "unsigned short" || Type == "uint16") return {16, true, false, false};
			if (Type == "int" || Type == "int32") return {32, false, false, false};
			if (Type == "unsigned int" || Type == "uint32") return {32, true, false, false};
			if (Type == "long" || Type == "int64") return {64, false, false, false};
			if (Type == "unsigned long" || Type == "uint64") return {64, true, false, false};
			return {};
		}
		unsigned Digit(unsigned char C) noexcept
		{
			return C >= '0' && C <= '9' ? C - '0' : C >= 'a' && C <= 'z' ? C - 'a' + 10
			    : C >= 'A' && C <= 'Z'                                   ? C - 'A' + 10
			                                                             : 255;
		}
		FServiceResult Tick(const FServiceContext& Context, std::size_t Index, std::size_t Size)
		{
			return Index % WorkBlock == 0 ? Context.Check(std::min(WorkBlock, Size - Index)) : FServiceResult{};
		}
		template <class T>
		FServiceResult ParseFloat(std::string_view Text, bool Negative, std::size_t Start,
		    FValue& Output, const FServiceContext& Context)
		{
			// Every binary32/64 rounding boundary has a terminating decimal expansion
			// shorter than 2048 significant digits. Retain that prefix and one sticky
			// digit, so conversion performs target-width rounding on bounded input.
			constexpr std::size_t PrefixLimit = 2048;
			std::string Digits;
			Digits.reserve(PrefixLimit + 1);
			std::size_t Position = Start, DigitCount = 0, IntegerDigits = 0, First = 0;
			bool Point = false, Nonzero = false, Sticky = false;
			if (Start && Text.size())
			{
				if (auto R = Tick(Context, 0, Text.size()); !R) return R;
			}
			for (; Position < Text.size(); ++Position)
			{
				if (auto R = Tick(Context, Position, Text.size()); !R) return R;
				const char C = Text[Position];
				if (C == '.' && !Point)
				{
					Point = true;
					continue;
				}
				if (C < '0' || C > '9')
				{
					break;
				}
				if (!Point)
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
			if (!DigitCount) return {EStatus::InvalidText, Position};
			bool ExponentNegative = false;
			std::uint64_t Exponent = 0;
			bool ExponentOverflow = false;
			if (Position < Text.size() && (Text[Position] == 'e' || Text[Position] == 'E'))
			{
				++Position;
				if (Position < Text.size() && (Text[Position] == '+' || Text[Position] == '-'))
				{
					ExponentNegative = Text[Position++] == '-';
				}
				const auto Begin = Position;
				for (; Position < Text.size() && Text[Position] >= '0' && Text[Position] <= '9'; ++Position)
				{
					if (auto R = Tick(Context, Position, Text.size()); !R) return R;
					const unsigned D = Text[Position] - '0';
					if (Exponent > (UINT64_MAX - D) / 10)
					{
						ExponentOverflow = true;
					}
					else if (!ExponentOverflow)
					{
						Exponent = Exponent * 10 + D;
					}
				}
				if (Begin == Position) return {EStatus::InvalidText, Position};
			}
			if (Position != Text.size()) return {EStatus::InvalidText, Position};
			if (!Nonzero)
			{
				if (auto R = Context.Check(); !R) return R;
				Output = Negative ? -T{0} : T{0};
				return {};
			}
			if (ExponentOverflow) return {EStatus::OutOfRange, Position};
			const std::int64_t Offset = static_cast<std::int64_t>(IntegerDigits) - static_cast<std::int64_t>(First) - 1;
			// Combine signed magnitudes before narrowing: a large exponent may cancel
			// a long run of leading fractional zeros (or a long integer mantissa).
			const bool OffsetNegative = Offset < 0;
			const auto OffsetMagnitude = static_cast<std::uint64_t>(OffsetNegative ? -Offset : Offset);
			bool CombinedNegative = ExponentNegative;
			std::uint64_t Magnitude = 0;
			if (ExponentNegative == OffsetNegative)
			{
				if (Exponent > UINT64_MAX - OffsetMagnitude) return {EStatus::OutOfRange, Position};
				Magnitude = Exponent + OffsetMagnitude;
			}
			else if (Exponent >= OffsetMagnitude)
			{
				Magnitude = Exponent - OffsetMagnitude;
			}
			else
			{
				Magnitude = OffsetMagnitude - Exponent;
				CombinedNegative = OffsetNegative;
			}
			if (Magnitude > 4096) return {EStatus::OutOfRange, Position};
			std::string Canonical;
			Canonical.reserve(Digits.size() + 16);
			if (Negative)
			{
				Canonical += '-';
			}
			Canonical += Digits.front();
			Canonical += '.';
			Canonical.append(Digits.data() + 1, Digits.size() - 1);
			if (Sticky)
			{
				Canonical += '1';
			}
			Canonical += 'e';
			if (CombinedNegative)
			{
				Canonical += '-';
			}
			Canonical += std::to_string(Magnitude);
			T Value{};
			const auto R = FloatParsingDetail::FromCharsDecimal(Canonical, Value);
			if (R.ec != std::errc{} || R.ptr != Canonical.data() + Canonical.size() || !std::isfinite(Value))
			{
				return {EStatus::OutOfRange, Position};
			}
			if (auto Checked = Context.Check(Canonical.size()); !Checked) return Checked;
			Output = Value;
			return {};
		}
	}

	bool IsAsciiWhitespace(unsigned char C) noexcept
	{
		return C == 32 || (C >= 9 && C <= 13);
	}
	bool IsScalarParseType(std::string_view Type, bool IntegerOnly) noexcept
	{
		const auto T = ScalarType(Type);
		return T.Bits && (!IntegerOnly || (!T.Floating && !T.Boolean));
	}
	bool CheckedNumericAdd(const FValue& Left, const FValue& Right, FValue& Output)
	{
		return std::visit([&](const auto& A) -> bool
		{
			using T = std::decay_t<decltype(A)>;
			if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
			{
				const auto* B = std::get_if<T>(&Right);
				if (!B) return false;
				if constexpr (std::is_integral_v<T>)
				{
					if constexpr (std::is_signed_v<T>)
					{
						if ((*B > 0 && A > std::numeric_limits<T>::max() - *B) || (*B < 0 && A < std::numeric_limits<T>::min() - *B))
						{
							return false;
						}
					}
					else if (A > std::numeric_limits<T>::max() - *B)
					{
						return false;
					}
				}
				Output = static_cast<T>(A + *B);
				return true;
			}
			return false;
		}, Left);
	}
	FServiceResult ParseScalar(std::string_view Text, std::string_view Type, int Base,
	    bool TrimWhitespace, FValue& Output, const FServiceContext& Context)
	{
		const auto T = ScalarType(Type);
		if (!T.Bits || Base < 2 || Base > 36 || ((T.Floating || T.Boolean) && Base != 10))
		{
			return {EStatus::InvalidArgument, 0};
		}
		if (auto R = Context.CheckText(Text.size()); !R) return R;
		std::size_t Begin = 0, End = Text.size();
		if (TrimWhitespace)
		{
			while (Begin < End && IsAsciiWhitespace(static_cast<unsigned char>(Text[Begin])))
			{
				if (auto R = Tick(Context, Begin++, Text.size()); !R) return R;
			}
			while (End > Begin && IsAsciiWhitespace(static_cast<unsigned char>(Text[End - 1])))
			{
				if (auto R = Tick(Context, --End, Text.size()); !R) return R;
			}
		}
		if (Begin == End) return {EStatus::InvalidText, Begin};
		const auto Body = Text.substr(Begin, End - Begin);
		if (T.Boolean)
		{
			if (Body != "true" && Body != "false") return {EStatus::InvalidText, Begin};
			if (auto R = Context.Check(Body.size()); !R) return R;
			Output = Body == "true";
			return {};
		}
		const bool Negative = Body.front() == '-';
		std::size_t Position = Body.front() == '+' || Negative ? 1 : 0;
		if (Position == Body.size() || (Negative && T.Unsigned)) return {EStatus::InvalidText, Begin};
		if (T.Floating)
		{
			auto Result = T.Bits == 32 ? ParseFloat<float>(Body, Negative, Position, Output, Context)
			                           : ParseFloat<double>(Body, Negative, Position, Output, Context);
			Result.Offset += Begin;
			return Result;
		}
		const std::uint64_t Limit = T.Unsigned ? (T.Bits == 64 ? UINT64_MAX : (UINT64_C(1) << T.Bits) - 1)
		                                       : (UINT64_C(1) << (T.Bits - 1)) - (Negative ? 0 : 1);
		std::uint64_t Magnitude = 0;
		bool Overflow = false;
		if (Position)
		{
			if (auto R = Tick(Context, 0, Body.size()); !R) return R;
		}
		for (; Position < Body.size(); ++Position)
		{
			if (auto R = Tick(Context, Position, Body.size()); !R) return R;
			const auto D = Digit(static_cast<unsigned char>(Body[Position]));
			if (D >= static_cast<unsigned>(Base)) return {EStatus::InvalidText, Begin + Position};
			if (Magnitude > (Limit - D) / static_cast<unsigned>(Base))
			{
				Overflow = true;
			}
			else if (!Overflow)
			{
				Magnitude = Magnitude * static_cast<unsigned>(Base) + D;
			}
		}
		if (Overflow) return {EStatus::OutOfRange, Begin + Position};
		if (auto R = Context.Check(); !R) return R;
		if (T.Unsigned)
		{
			switch (T.Bits)
			{
			case 8:
				{
					Output = static_cast<std::uint8_t>(Magnitude);
					break;
				}
			case 16:
				{
					Output = static_cast<std::uint16_t>(Magnitude);
					break;
				}
			case 32:
				{
					Output = static_cast<std::uint32_t>(Magnitude);
					break;
				}
			default:
				{
					Output = Magnitude;
					break;
				}
			}
		}
		else
		{
			const auto Value = Negative && Magnitude ? -static_cast<std::int64_t>(Magnitude - 1) - 1
			                                         : static_cast<std::int64_t>(Magnitude);
			switch (T.Bits)
			{
			case 8:
				{
					Output = static_cast<std::int8_t>(Value);
					break;
				}
			case 16:
				{
					Output = static_cast<std::int16_t>(Value);
					break;
				}
			case 32:
				{
					Output = static_cast<std::int32_t>(Value);
					break;
				}
			default:
				{
					Output = Value;
					break;
				}
			}
		}
		return {};
	}
}
