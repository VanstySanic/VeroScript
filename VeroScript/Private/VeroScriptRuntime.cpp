// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptRuntime.h"
#include "NativeSupport/VeroScriptHostValueServices.h"
#include "VeroScriptTypeScope.h"
#include "VeroScriptVariantInternal.h"
#include "VeroScriptStandardLibraryServices.h"
#include "Text/VeroScriptTextInternal.h"
#include "VeroScriptConsole.h"
#include "VeroScriptPlatform.h"

#include "VeroScriptEnvironment.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptNative.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#elif defined(__linux__) || defined(__unix__)
#include <unistd.h>
#endif

namespace VeroScript::Runtime
{
	namespace
	{
		constexpr std::uint64_t FnvPrime = 1099511628211ull;

		template <typename TStorage>
		std::size_t ScalarContainerSize(const TStorage& Storage)
		{
			return std::visit([](const auto& Entries) -> std::size_t
			{
				if constexpr (std::is_same_v<std::decay_t<decltype(Entries)>, std::monostate>)
				{
					return 0;
				}
				else
				{
					return Entries.size();
				}
			}, Storage);
		}

		enum class EPrimitiveValueKind : std::uint8_t
		{
			None = 0,
			Bool,
			Int8,
			UInt8,
			Int16,
			UInt16,
			Int32,
			UInt32,
			Int64,
			UInt64,
			Float,
			Double,
			String,
			WString,
		};

		std::string_view TrimTypeName(std::string_view TypeName)
		{
			while (!TypeName.empty() && std::isspace(static_cast<unsigned char>(TypeName.front())))
			{
				TypeName.remove_prefix(1);
			}
			while (!TypeName.empty() && std::isspace(static_cast<unsigned char>(TypeName.back())))
			{
				TypeName.remove_suffix(1);
			}
			return TypeName;
		}

		EPrimitiveValueKind PrimitiveValueKind(std::string_view TypeName)
		{
			TypeName = TrimTypeName(TypeName);
			if (TypeName.rfind("const ref ", 0) == 0)
			{
				TypeName = TrimTypeName(TypeName.substr(10));
			}
			else if (TypeName.rfind("ref ", 0) == 0)
			{
				TypeName = TrimTypeName(TypeName.substr(4));
			}
			if (TypeName == "bool")
			{
				return EPrimitiveValueKind::Bool;
			}
			if (TypeName == "char" || TypeName == "int8")
			{
				return EPrimitiveValueKind::Int8;
			}
			if (TypeName == "unsigned char" || TypeName == "uint8")
			{
				return EPrimitiveValueKind::UInt8;
			}
			if (TypeName == "short" || TypeName == "int16")
			{
				return EPrimitiveValueKind::Int16;
			}
			if (TypeName == "unsigned short" || TypeName == "uint16")
			{
				return EPrimitiveValueKind::UInt16;
			}
			if (TypeName == "int" || TypeName == "int32")
			{
				return EPrimitiveValueKind::Int32;
			}
			if (TypeName == "unsigned" || TypeName == "unsigned int" || TypeName == "uint32")
			{
				return EPrimitiveValueKind::UInt32;
			}
			if (TypeName == "long" || TypeName == "int64")
			{
				return EPrimitiveValueKind::Int64;
			}
			if (TypeName == "unsigned long" || TypeName == "uint64")
			{
				return EPrimitiveValueKind::UInt64;
			}
			if (TypeName == "float")
			{
				return EPrimitiveValueKind::Float;
			}
			if (TypeName == "double")
			{
				return EPrimitiveValueKind::Double;
			}
			if (TypeName == "string")
			{
				return EPrimitiveValueKind::String;
			}
			if (TypeName == "wstring")
			{
				return EPrimitiveValueKind::WString;
			}
			return EPrimitiveValueKind::None;
		}

		bool TrySignedInteger(
		    const FValue& Value,
		    const std::int64_t Minimum,
		    const std::int64_t Maximum,
		    std::int64_t& OutValue)
		{
			if (const auto* Flag = std::get_if<bool>(&Value))
			{
				OutValue = *Flag ? 1 : 0;
				return true;
			}
			if (const auto* Int8 = std::get_if<std::int8_t>(&Value))
			{
				OutValue = *Int8;
			}
			else if (const auto* UInt8 = std::get_if<std::uint8_t>(&Value))
			{
				OutValue = *UInt8;
			}
			else if (const auto* Int16 = std::get_if<std::int16_t>(&Value))
			{
				OutValue = *Int16;
			}
			else if (const auto* UInt16 = std::get_if<std::uint16_t>(&Value))
			{
				OutValue = *UInt16;
			}
			else if (const auto* Int32 = std::get_if<std::int32_t>(&Value))
			{
				OutValue = *Int32;
			}
			else if (const auto* UInt32 = std::get_if<std::uint32_t>(&Value))
			{
				OutValue = *UInt32;
			}
			else if (const auto* Int64 = std::get_if<std::int64_t>(&Value))
			{
				OutValue = *Int64;
			}
			else if (const auto* UInt64 = std::get_if<std::uint64_t>(&Value))
			{
				if (*UInt64 > static_cast<std::uint64_t>(Maximum))
				{
					return false;
				}
				OutValue = static_cast<std::int64_t>(*UInt64);
			}
			else if (const auto* Float = std::get_if<float>(&Value))
			{
				if (!std::isfinite(*Float))
				{
					return false;
				}
				const long double Truncated = std::trunc(static_cast<long double>(*Float));
				if (Truncated < static_cast<long double>(Minimum) || (Maximum == std::numeric_limits<std::int64_t>::max() ? Truncated >= 9223372036854775808.0L : Truncated > static_cast<long double>(Maximum)))
				{
					return false;
				}
				OutValue = static_cast<std::int64_t>(Truncated);
			}
			else if (const auto* Double = std::get_if<double>(&Value))
			{
				if (!std::isfinite(*Double))
				{
					return false;
				}
				const long double Truncated = std::trunc(static_cast<long double>(*Double));
				if (Truncated < static_cast<long double>(Minimum) || (Maximum == std::numeric_limits<std::int64_t>::max() ? Truncated >= 9223372036854775808.0L : Truncated > static_cast<long double>(Maximum)))
				{
					return false;
				}
				OutValue = static_cast<std::int64_t>(Truncated);
			}
			else
			{
				return false;
			}
			return OutValue >= Minimum && OutValue <= Maximum;
		}

		bool TryUnsignedInteger(
		    const FValue& Value,
		    const std::uint64_t Maximum,
		    std::uint64_t& OutValue)
		{
			if (const auto* Flag = std::get_if<bool>(&Value))
			{
				OutValue = *Flag ? 1u : 0u;
				return true;
			}
			if (const auto* Int8 = std::get_if<std::int8_t>(&Value))
			{
				if (*Int8 < 0)
				{
					return false;
				}
				OutValue = static_cast<std::uint64_t>(*Int8);
			}
			else if (const auto* UInt8 = std::get_if<std::uint8_t>(&Value))
			{
				OutValue = *UInt8;
			}
			else if (const auto* Int16 = std::get_if<std::int16_t>(&Value))
			{
				if (*Int16 < 0)
				{
					return false;
				}
				OutValue = static_cast<std::uint64_t>(*Int16);
			}
			else if (const auto* UInt16 = std::get_if<std::uint16_t>(&Value))
			{
				OutValue = *UInt16;
			}
			else if (const auto* Int32 = std::get_if<std::int32_t>(&Value))
			{
				if (*Int32 < 0)
				{
					return false;
				}
				OutValue = static_cast<std::uint64_t>(*Int32);
			}
			else if (const auto* UInt32 = std::get_if<std::uint32_t>(&Value))
			{
				OutValue = *UInt32;
			}
			else if (const auto* Int64 = std::get_if<std::int64_t>(&Value))
			{
				if (*Int64 < 0)
				{
					return false;
				}
				OutValue = static_cast<std::uint64_t>(*Int64);
			}
			else if (const auto* UInt64 = std::get_if<std::uint64_t>(&Value))
			{
				OutValue = *UInt64;
			}
			else if (const auto* Float = std::get_if<float>(&Value))
			{
				if (!std::isfinite(*Float) || *Float < 0.0f)
				{
					return false;
				}
				const long double Truncated = std::trunc(static_cast<long double>(*Float));
				if (Maximum == std::numeric_limits<std::uint64_t>::max()
				        ? Truncated >= 18446744073709551616.0L
				        : Truncated > static_cast<long double>(Maximum))
				{
					return false;
				}
				OutValue = static_cast<std::uint64_t>(Truncated);
			}
			else if (const auto* Double = std::get_if<double>(&Value))
			{
				if (!std::isfinite(*Double) || *Double < 0.0)
				{
					return false;
				}
				const long double Truncated = std::trunc(static_cast<long double>(*Double));
				if (Maximum == std::numeric_limits<std::uint64_t>::max()
				        ? Truncated >= 18446744073709551616.0L
				        : Truncated > static_cast<long double>(Maximum))
				{
					return false;
				}
				OutValue = static_cast<std::uint64_t>(Truncated);
			}
			else
			{
				return false;
			}
			return OutValue <= Maximum;
		}
	}

	std::uint64_t Fnv1a64(const std::uint64_t Hash, const void* const Data, const std::size_t Size)
	{
		std::uint64_t Result = Hash;
		const auto* const Bytes = static_cast<const std::uint8_t*>(Data);
		for (std::size_t Index = 0; Index < Size; ++Index)
		{
			Result ^= static_cast<std::uint64_t>(Bytes[Index]);
			Result *= FnvPrime;
		}
		return Result;
	}

	std::uint64_t Fnv1a64(const std::uint64_t Hash, const std::string_view Text)
	{
		return Fnv1a64(Hash, Text.data(), Text.size());
	}

	bool TryGetInteger64(const FValue& Value, std::int64_t& OutInteger)
	{
		return IsIntegerValue(Value) && TrySignedInteger(Value, (std::numeric_limits<std::int64_t>::min)(), (std::numeric_limits<std::int64_t>::max)(), OutInteger);
	}

	bool TryGetSizeValue(const FValue& Value, std::size_t& OutSize)
	{
		if (!IsIntegerValue(Value))
		{
			return false;
		}
		std::uint64_t Size = 0;
		const std::uint64_t Maximum = (std::min)(static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()),
		    static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()));
		if (!TryUnsignedInteger(Value, Maximum, Size))
		{
			return false;
		}
		OutSize = static_cast<std::size_t>(Size);
		return true;
	}

	bool TryAppendStringValue(FValue& Target, const FValue& Suffix)
	{
		if (auto* Text = std::get_if<std::string>(&Target))
		{
			if (const auto* SuffixText = std::get_if<std::string>(&Suffix))
			{
				AppendString(*Text, *SuffixText);
				return true;
			}
		}
		if (auto* Text = std::get_if<std::u16string>(&Target))
		{
			if (const auto* SuffixText = std::get_if<std::u16string>(&Suffix))
			{
				AppendString(*Text, *SuffixText);
				return true;
			}
		}
		return false;
	}

	bool IsUnsignedTypeName(const std::string_view TypeName)
	{
		return TypeName == "unsigned" || TypeName == "unsigned int" || TypeName == "unsigned char" || TypeName == "unsigned short" || TypeName == "unsigned long" || TypeName == "uint8" || TypeName == "uint16" || TypeName == "uint32" || TypeName == "uint64";
	}

	bool IsPrimitiveValueTypeName(const std::string_view TypeName)
	{
		return PrimitiveValueKind(TypeName) != EPrimitiveValueKind::None;
	}

	bool ValueMatchesPrimitiveTypeName(
	    const FValue& Value,
	    const std::string_view TypeName)
	{
		switch (PrimitiveValueKind(TypeName))
		{
		case EPrimitiveValueKind::Bool:
			return std::holds_alternative<bool>(Value);
		case EPrimitiveValueKind::Int8:
			return std::holds_alternative<std::int8_t>(Value);
		case EPrimitiveValueKind::UInt8:
			return std::holds_alternative<std::uint8_t>(Value);
		case EPrimitiveValueKind::Int16:
			return std::holds_alternative<std::int16_t>(Value);
		case EPrimitiveValueKind::UInt16:
			return std::holds_alternative<std::uint16_t>(Value);
		case EPrimitiveValueKind::Int32:
			return std::holds_alternative<std::int32_t>(Value);
		case EPrimitiveValueKind::UInt32:
			return std::holds_alternative<std::uint32_t>(Value);
		case EPrimitiveValueKind::Int64:
			return std::holds_alternative<std::int64_t>(Value);
		case EPrimitiveValueKind::UInt64:
			return std::holds_alternative<std::uint64_t>(Value);
		case EPrimitiveValueKind::Float:
			return std::holds_alternative<float>(Value);
		case EPrimitiveValueKind::Double:
			return std::holds_alternative<double>(Value);
		case EPrimitiveValueKind::String:
			return std::holds_alternative<std::string>(Value);
		case EPrimitiveValueKind::WString:
			return std::holds_alternative<std::u16string>(Value);
		default:
			return false;
		}
	}

	bool CoerceValueToPrimitiveTypeName(
	    const std::string_view TypeName,
	    FValue& Value,
	    std::string& OutError)
	{
		OutError.clear();
		const EPrimitiveValueKind Kind = PrimitiveValueKind(TypeName);
		if (Kind == EPrimitiveValueKind::None)
		{
			OutError = "target is not a primitive value type";
			return false;
		}
		if (ValueMatchesPrimitiveTypeName(Value, TypeName))
		{
			return true;
		}
		if (Kind == EPrimitiveValueKind::Bool || Kind == EPrimitiveValueKind::String || Kind == EPrimitiveValueKind::WString)
		{
			OutError = "primitive value type mismatch";
			return false;
		}
		if (Kind == EPrimitiveValueKind::Float || Kind == EPrimitiveValueKind::Double)
		{
			if (const auto* Flag = std::get_if<bool>(&Value))
			{
				Value = Kind == EPrimitiveValueKind::Float
				    ? FValue{*Flag ? 1.0f : 0.0f}
				    : FValue{*Flag ? 1.0 : 0.0};
				return true;
			}
			if (!IsNumericValue(Value))
			{
				OutError = "expected numeric value";
				return false;
			}
			const double Number = ToDouble(Value);
			Value = Kind == EPrimitiveValueKind::Float
			    ? FValue{static_cast<float>(Number)}
			    : FValue{Number};
			return true;
		}

		if (Kind == EPrimitiveValueKind::Int8 || Kind == EPrimitiveValueKind::Int16 || Kind == EPrimitiveValueKind::Int32 || Kind == EPrimitiveValueKind::Int64)
		{
			std::int64_t Minimum = std::numeric_limits<std::int64_t>::min();
			std::int64_t Maximum = std::numeric_limits<std::int64_t>::max();
			if (Kind == EPrimitiveValueKind::Int8)
			{
				Minimum = std::numeric_limits<std::int8_t>::min();
				Maximum = std::numeric_limits<std::int8_t>::max();
			}
			else if (Kind == EPrimitiveValueKind::Int16)
			{
				Minimum = std::numeric_limits<std::int16_t>::min();
				Maximum = std::numeric_limits<std::int16_t>::max();
			}
			else if (Kind == EPrimitiveValueKind::Int32)
			{
				Minimum = std::numeric_limits<std::int32_t>::min();
				Maximum = std::numeric_limits<std::int32_t>::max();
			}
			std::int64_t Converted = 0;
			if (!TrySignedInteger(Value, Minimum, Maximum, Converted))
			{
				OutError = "signed integer conversion overflow or type mismatch";
				return false;
			}
			switch (Kind)
			{
			case EPrimitiveValueKind::Int8:
				{
					Value = static_cast<std::int8_t>(Converted);
					break;
				}
			case EPrimitiveValueKind::Int16:
				{
					Value = static_cast<std::int16_t>(Converted);
					break;
				}
			case EPrimitiveValueKind::Int32:
				{
					Value = static_cast<std::int32_t>(Converted);
					break;
				}
			default:
				{
					Value = Converted;
					break;
				}
			}
			return true;
		}

		std::uint64_t Maximum = std::numeric_limits<std::uint64_t>::max();
		if (Kind == EPrimitiveValueKind::UInt8)
		{
			Maximum = std::numeric_limits<std::uint8_t>::max();
		}
		else if (Kind == EPrimitiveValueKind::UInt16)
		{
			Maximum = std::numeric_limits<std::uint16_t>::max();
		}
		else if (Kind == EPrimitiveValueKind::UInt32)
		{
			Maximum = std::numeric_limits<std::uint32_t>::max();
		}
		std::uint64_t Converted = 0;
		if (!TryUnsignedInteger(Value, Maximum, Converted))
		{
			OutError = "unsigned integer conversion overflow or type mismatch";
			return false;
		}
		switch (Kind)
		{
		case EPrimitiveValueKind::UInt8:
			{
				Value = static_cast<std::uint8_t>(Converted);
				break;
			}
		case EPrimitiveValueKind::UInt16:
			{
				Value = static_cast<std::uint16_t>(Converted);
				break;
			}
		case EPrimitiveValueKind::UInt32:
			{
				Value = static_cast<std::uint32_t>(Converted);
				break;
			}
		default:
			{
				Value = Converted;
				break;
			}
		}
		return true;
	}

	double ToDouble(const FValue& Value)
	{
		if (const auto* DoubleValue = std::get_if<double>(&Value))
		{
			return *DoubleValue;
		}
		if (const auto* FloatValue = std::get_if<float>(&Value))
		{
			return static_cast<double>(*FloatValue);
		}
		if (const auto* IntValue = std::get_if<std::int32_t>(&Value))
		{
			return static_cast<double>(*IntValue);
		}
		if (const auto* UIntValue = std::get_if<std::uint32_t>(&Value))
		{
			return static_cast<double>(*UIntValue);
		}
		if (const auto* Int64Value = std::get_if<std::int64_t>(&Value))
		{
			return static_cast<double>(*Int64Value);
		}
		if (const auto* UInt64Value = std::get_if<std::uint64_t>(&Value))
		{
			return static_cast<double>(*UInt64Value);
		}
		if (const auto* Value8 = std::get_if<std::int8_t>(&Value))
		{
			return static_cast<double>(*Value8);
		}
		if (const auto* Value8 = std::get_if<std::uint8_t>(&Value))
		{
			return static_cast<double>(*Value8);
		}
		if (const auto* Value16 = std::get_if<std::int16_t>(&Value))
		{
			return static_cast<double>(*Value16);
		}
		if (const auto* Value16 = std::get_if<std::uint16_t>(&Value))
		{
			return static_cast<double>(*Value16);
		}
		// Preserve the existing precondition failure for non-numeric callers.
		return std::get<double>(Value);
	}

	std::int64_t ToSignedInteger64(const FValue& Value)
	{
		if (const auto* V = std::get_if<std::int32_t>(&Value))
		{
			return *V;
		}
		if (const auto* V = std::get_if<std::int8_t>(&Value))
		{
			return *V;
		}
		if (const auto* V = std::get_if<std::int16_t>(&Value))
		{
			return *V;
		}
		return std::get<std::int64_t>(Value);
	}

	std::uint64_t ToUnsignedInteger64(const FValue& Value)
	{
		if (const auto* V = std::get_if<std::uint32_t>(&Value))
		{
			return *V;
		}
		if (const auto* V = std::get_if<std::uint8_t>(&Value))
		{
			return *V;
		}
		if (const auto* V = std::get_if<std::uint16_t>(&Value))
		{
			return *V;
		}
		return std::get<std::uint64_t>(Value);
	}

	bool AddIntegerImmediate(FValue& Value, const std::int32_t Immediate)
	{
		const auto AddSigned = [&](auto& Stored) -> bool
		{
			using TValue = std::decay_t<decltype(Stored)>;
			const std::int64_t Wide = static_cast<std::int64_t>(Stored) + Immediate;
			if (Wide < static_cast<std::int64_t>(std::numeric_limits<TValue>::min()) || Wide > static_cast<std::int64_t>(std::numeric_limits<TValue>::max()))
			{
				return false;
			}
			Stored = static_cast<TValue>(Wide);
			return true;
		};
		if (auto* V = std::get_if<std::int32_t>(&Value))
		{
			return AddSigned(*V);
		}
		if (auto* V = std::get_if<std::int64_t>(&Value))
		{
			const std::int64_t WideImmediate = Immediate;
			if ((WideImmediate > 0 && *V > std::numeric_limits<std::int64_t>::max() - WideImmediate) || (WideImmediate < 0 && *V < std::numeric_limits<std::int64_t>::min() - WideImmediate))
			{
				return false;
			}
			*V += WideImmediate;
			return true;
		}
		if (auto* V = std::get_if<std::int8_t>(&Value))
		{
			return AddSigned(*V);
		}
		if (auto* V = std::get_if<std::int16_t>(&Value))
		{
			return AddSigned(*V);
		}
		if (auto* V = std::get_if<std::uint32_t>(&Value))
		{
			*V += static_cast<std::uint32_t>(Immediate);
			return true;
		}
		if (auto* V = std::get_if<std::uint64_t>(&Value))
		{
			*V += static_cast<std::uint64_t>(static_cast<std::int64_t>(Immediate));
			return true;
		}
		if (auto* V = std::get_if<std::uint8_t>(&Value))
		{
			*V = static_cast<std::uint8_t>(*V + static_cast<std::uint8_t>(Immediate));
			return true;
		}
		if (auto* V = std::get_if<std::uint16_t>(&Value))
		{
			*V = static_cast<std::uint16_t>(*V + static_cast<std::uint16_t>(Immediate));
			return true;
		}
		return false;
	}

	std::string ValueToString(const FValue& Value)
	{
		if (const auto* Box = std::get_if<FScriptVariantHandle>(&Value))
		{
			return *Box ? "variant<" + (*Box)->CanonicalTypeName + ">(" + ValueToString((*Box)->Value) + ")" : "variant()";
		}
		if (IsClosureReferenceCell(Value))
		{
			const FValue* const Visible = UnwrapClosureReferenceCell(&Value);
			return Visible ? ValueToString(*Visible) : "<invalid reference>";
		}
		if (const auto* Int8Value = std::get_if<std::int8_t>(&Value))
		{
			return std::to_string(static_cast<int>(*Int8Value));
		}
		if (const auto* UInt8Value = std::get_if<std::uint8_t>(&Value))
		{
			return std::to_string(static_cast<unsigned int>(*UInt8Value));
		}
		if (const auto* Int16Value = std::get_if<std::int16_t>(&Value))
		{
			return std::to_string(static_cast<int>(*Int16Value));
		}
		if (const auto* UInt16Value = std::get_if<std::uint16_t>(&Value))
		{
			return std::to_string(static_cast<unsigned int>(*UInt16Value));
		}
		if (const auto* IntValue = std::get_if<std::int32_t>(&Value))
		{
			return std::to_string(*IntValue);
		}
		if (const auto* UIntValue = std::get_if<std::uint32_t>(&Value))
		{
			return std::to_string(*UIntValue);
		}
		if (const auto* Int64Value = std::get_if<std::int64_t>(&Value))
		{
			return std::to_string(*Int64Value);
		}
		if (const auto* UInt64Value = std::get_if<std::uint64_t>(&Value))
		{
			return std::to_string(*UInt64Value);
		}
		if (const auto* DoubleValue = std::get_if<double>(&Value))
		{
			std::ostringstream Stream;
			Stream << std::setprecision(15) << *DoubleValue;
			return Stream.str();
		}
		if (const auto* FloatValue = std::get_if<float>(&Value))
		{
			std::ostringstream Stream;
			Stream << std::setprecision(std::numeric_limits<float>::max_digits10)
			       << *FloatValue;
			return Stream.str();
		}
		if (const auto* StringValue = std::get_if<std::string>(&Value))
		{
			return *StringValue;
		}
		if (const auto* WStringValue = std::get_if<std::u16string>(&Value))
		{
			return Utf16ToUtf8(*WStringValue);
		}
		if (const auto* BoolValue = std::get_if<bool>(&Value))
		{
			return *BoolValue ? "true" : "false";
		}
		if (const auto* EnumValue = std::get_if<FEnumValue>(&Value))
		{
			auto Display = StripTypeScopeMarkers(EnumValue->TypeName);
			const auto IdentityStart = FindLinkageIdentityStart(Display);
			if (IdentityStart != 0)
			{
				Display.erase(0, IdentityStart);
			}
			return Display + "(" + std::to_string(EnumValue->Value) + ")";
		}
		if (const auto* ArrayValue = std::get_if<FNativeArray>(&Value))
		{
			return ArrayValue->TypeName() + "[" + std::to_string(ArrayValue->Length()) + "]";
		}
		if (const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&Value))
		{
			if (!*DynamicArray)
			{
				return "array<?>";
			}
			return (*DynamicArray)->TypeName + "[" + std::to_string((*DynamicArray)->HasScalarElements() ? (*DynamicArray)->ScalarSize() : (*DynamicArray)->Elements.size()) + "]";
		}
		if (const auto* MapHandle = std::get_if<FMapHandle>(&Value))
		{
			if (!*MapHandle)
			{
				return "map<?,?>";
			}
			if ((*MapHandle)->ScalarEntries.index() != 0)
			{
				return (*MapHandle)->TypeName + "[" + std::to_string(ScalarContainerSize((*MapHandle)->ScalarEntries)) + "]";
			}
			const std::size_t Size = (*MapHandle)->KeyTypeName == "string"
			    ? (*MapHandle)->StrEntries.size()
			    : ((*MapHandle)->KeyTypeName == "wstring"
			              ? (*MapHandle)->WStrEntries.size()
			              : ((*MapHandle)->KeyTypeName == "int64"
			                        ? (*MapHandle)->Int64Entries.size()
			                        : ((*MapHandle)->KeyTypeName == "uint64"
			                                  ? (*MapHandle)->UInt64Entries.size()
			                                  : ((*MapHandle)->KeyTypeName == "unsigned int"
			                                            ? (*MapHandle)->UIntEntries.size()
			                                            : (((*MapHandle)->KeyTypeName == "int" || (*MapHandle)->KeyTypeName == "char")
			                                                      ? (*MapHandle)->IntEntries.size()
			                                                      : (!(*MapHandle)->EnumEntries.empty()
			                                                                ? (*MapHandle)->EnumEntries.size()
			                                                                : (*MapHandle)->HostEntries.size()))))));
			return (*MapHandle)->TypeName + "[" + std::to_string(Size) + "]";
		}
		if (const auto* SetHandle = std::get_if<FSetHandle>(&Value))
		{
			if (!*SetHandle)
			{
				return "set<?>";
			}
			if ((*SetHandle)->ScalarElements.index() != 0)
			{
				return (*SetHandle)->TypeName + "[" + std::to_string(ScalarContainerSize((*SetHandle)->ScalarElements)) + "]";
			}
			const std::size_t Size = (*SetHandle)->ElementTypeName == "string"
			    ? (*SetHandle)->StrElements.size()
			    : ((*SetHandle)->ElementTypeName == "wstring"
			              ? (*SetHandle)->WStrElements.size()
			              : ((*SetHandle)->ElementTypeName == "int64"
			                        ? (*SetHandle)->Int64Elements.size()
			                        : ((*SetHandle)->ElementTypeName == "uint64"
			                                  ? (*SetHandle)->UInt64Elements.size()
			                                  : ((*SetHandle)->ElementTypeName == "unsigned int"
			                                            ? (*SetHandle)->UIntElements.size()
			                                            : (((*SetHandle)->ElementTypeName == "int" || (*SetHandle)->ElementTypeName == "char")
			                                                      ? (*SetHandle)->IntElements.size()
			                                                      : (!(*SetHandle)->EnumElements.empty()
			                                                                ? (*SetHandle)->EnumElements.size()
			                                                                : (*SetHandle)->HostElements.size()))))));
			return (*SetHandle)->TypeName + "[" + std::to_string(Size) + "]";
		}
		if (const auto* QueueHandle = std::get_if<FQueueHandle>(&Value))
		{
			if (!*QueueHandle)
			{
				return "queue<?>";
			}
			return (*QueueHandle)->TypeName + "[" + std::to_string((*QueueHandle)->Elements.size()) + "]";
		}
		if (const auto* StackHandle = std::get_if<FStackHandle>(&Value))
		{
			if (!*StackHandle)
			{
				return "stack<?>";
			}
			return (*StackHandle)->TypeName + "[" + std::to_string((*StackHandle)->Elements.size()) + "]";
		}
		if (const auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(&Value))
		{
			if (!*OrderedMapHandle)
			{
				return "ordered_map<?,?>";
			}
			const std::size_t Size = (*OrderedMapHandle)->KeyTypeName == "string"
			    ? (*OrderedMapHandle)->StrEntries.size()
			    : ((*OrderedMapHandle)->KeyTypeName == "wstring"
			              ? (*OrderedMapHandle)->WStrEntries.size()
			              : ((*OrderedMapHandle)->KeyTypeName == "int64"
			                        ? (*OrderedMapHandle)->Int64Entries.size()
			                        : ((*OrderedMapHandle)->KeyTypeName == "uint64"
			                                  ? (*OrderedMapHandle)->UInt64Entries.size()
			                                  : ((*OrderedMapHandle)->KeyTypeName == "unsigned int"
			                                            ? (*OrderedMapHandle)->UIntEntries.size()
			                                            : (((*OrderedMapHandle)->KeyTypeName == "int" || (*OrderedMapHandle)->KeyTypeName == "char")
			                                                      ? (*OrderedMapHandle)->IntEntries.size()
			                                                      : (!(*OrderedMapHandle)->EnumEntries.empty()
			                                                                ? (*OrderedMapHandle)->EnumEntries.size()
			                                                                : (*OrderedMapHandle)->HostEntries.size()))))));
			return (*OrderedMapHandle)->TypeName + "[" + std::to_string(Size) + "]";
		}
		if (const auto* DequeHandle = std::get_if<FDequeHandle>(&Value))
		{
			if (!*DequeHandle)
			{
				return "deque<?>";
			}
			return (*DequeHandle)->TypeName + "[" + std::to_string((*DequeHandle)->Elements.size()) + "]";
		}
		if (const auto* ObjectHandle = std::get_if<FObjectHandle>(&Value))
		{
			if (!*ObjectHandle)
			{
				return "nullptr";
			}
			std::string Text = (*ObjectHandle)->GetTypeName();
			if ((*ObjectHandle)->GetHostBase())
			{
				const FHostObject& Host = *(*ObjectHandle)->GetHostBase();
				Text += " : ";
				Text += Host.TypeName.empty() ? "hostobject" : (Host.TypeName + "@host");
			}
			return Text;
		}
		if (const auto* ChannelHandle = std::get_if<FChannelHandle>(&Value))
		{
			if (!*ChannelHandle)
			{
				return "channel<?>";
			}
			std::string TypeName;
			std::string ElementTypeName;
			(*ChannelHandle)->GetTypeNames(TypeName, ElementTypeName);
			return TypeName.empty() ? "channel<?>" : TypeName;
		}
		if (const auto* FunctionHandle = std::get_if<FFunctionHandle>(&Value))
		{
			if (!*FunctionHandle)
			{
				return "fn(?)";
			}
			return (*FunctionHandle)->TypeName.empty() ? "fn(?)" : (*FunctionHandle)->TypeName;
		}
		if (const auto* HostHandle = std::get_if<FHostObjectHandle>(&Value))
		{
			if (!*HostHandle)
			{
				return "null";
			}
			std::string Text = (*HostHandle)->TypeName.empty()
			    ? "hostobject"
			    : ((*HostHandle)->TypeName + "@host");
			if (const FObjectHandle Owner = (*HostHandle)->ScriptOwner.lock())
			{
				Text += "->";
				Text += Owner->GetTypeName().empty() ? "?" : Owner->GetTypeName();
			}
			return Text;
		}
		return {};
	}

	bool WriteValue(std::ostream& OutputStream, const FValue& Value)
	{
		if (const auto* WStringValue = std::get_if<std::u16string>(&Value))
		{
			#ifdef _WIN32
			HANDLE Console = nullptr;
			if (&OutputStream == &std::cout)
			{
				Console = GetStdHandle(STD_OUTPUT_HANDLE);
			}
			else if (&OutputStream == &std::cerr)
			{
				Console = GetStdHandle(STD_ERROR_HANDLE);
			}
			if (Console != nullptr)
			{
				DWORD ConsoleMode = 0;
				if (Console != INVALID_HANDLE_VALUE && GetConsoleMode(Console, &ConsoleMode))
				{
					std::wstring Wide;
					Wide.reserve(WStringValue->size());
					for (const char16_t Unit : *WStringValue)
					{
						Wide.push_back(static_cast<wchar_t>(Unit));
					}
					DWORD Written = 0;
					if (WriteConsoleW(
					        Console,
					        Wide.data(),
					        static_cast<DWORD>(Wide.size()),
					        &Written,
					        nullptr))
					{
						return true;
					}
				}
			}
			#endif
			OutputStream << Utf16ToUtf8(*WStringValue);
			return static_cast<bool>(OutputStream);
		}

		OutputStream << ValueToString(Value);
		return static_cast<bool>(OutputStream);
	}

	bool WriteValueLine(std::ostream& OutputStream, const FValue& Value)
	{
		// Keep the wide Windows console path whole: mixing WriteConsoleW for the
		// payload with a narrow iostream newline can lose the line break when the
		// two output paths use different buffering modes.
		if (const auto* WStringValue = std::get_if<std::u16string>(&Value))
		{
			#ifdef _WIN32
			HANDLE Console = nullptr;
			if (&OutputStream == &std::cout)
			{
				Console = GetStdHandle(STD_OUTPUT_HANDLE);
			}
			else if (&OutputStream == &std::cerr)
			{
				Console = GetStdHandle(STD_ERROR_HANDLE);
			}
			if (Console != nullptr)
			{
				DWORD ConsoleMode = 0;
				if (Console != INVALID_HANDLE_VALUE && GetConsoleMode(Console, &ConsoleMode))
				{
					std::wstring Wide;
					Wide.reserve(WStringValue->size() + 1);
					for (const char16_t Unit : *WStringValue)
					{
						Wide.push_back(static_cast<wchar_t>(Unit));
					}
					Wide.push_back(L'\n');
					DWORD Written = 0;
					if (WriteConsoleW(
					        Console,
					        Wide.data(),
					        static_cast<DWORD>(Wide.size()),
					        &Written,
					        nullptr))
					{
						return true;
					}
				}
			}
			#endif
			OutputStream << Utf16ToUtf8(*WStringValue) << '\n';
			OutputStream.flush();
			return static_cast<bool>(OutputStream);
		}
		if (!WriteValue(OutputStream, Value))
		{
			return false;
		}
		OutputStream << '\n';
		OutputStream.flush();
		return static_cast<bool>(OutputStream);
	}

	bool IsInteractiveInput(const std::istream& InputStream)
	{
		return Console::IsInputTerminal(InputStream);
	}
	bool IsInteractiveOutput(const std::ostream& OutputStream)
	{
		return Console::Query(OutputStream).bOutputTerminal;
	}
	bool ClearInteractiveOutput(std::ostream& OutputStream)
	{
		return Console::Execute(OutputStream, Console::EOperation::Clear);
	}

	FValue ConstantToValue(const FBytecodeModule& Module, const FBytecodeConstant& Constant)
	{
		switch (Constant.Kind)
		{
		case EConstantKind::I8:
			return Constant.I8;
		case EConstantKind::U8:
			return Constant.U8;
		case EConstantKind::I16:
			return Constant.I16;
		case EConstantKind::U16:
			return Constant.U16;
		case EConstantKind::I32:
			return Constant.I32;
		case EConstantKind::U32:
			return Constant.U32;
		case EConstantKind::I64:
			return Constant.I64;
		case EConstantKind::U64:
			return Constant.U64;
		case EConstantKind::F64:
			return Constant.F64;
		case EConstantKind::F32:
			return Constant.F32;
		case EConstantKind::Bool:
			return Constant.Bool;
		case EConstantKind::Str:
			{
				if (Constant.StringIndex >= Module.StringPool.size())
				{
					// Fail closed: loader validates; tolerate corrupt in-memory modules.
					return std::string{};
				}
				return Module.StringPool[Constant.StringIndex];
			}
		case EConstantKind::WStr:
			{
				if (Constant.StringIndex >= Module.WStringPool.size())
				{
					return std::u16string{};
				}
				return Module.WStringPool[Constant.StringIndex];
			}
		case EConstantKind::Enum:
			{
				if (Constant.StringIndex >= Module.StringPool.size())
				{
					return FEnumValue{};
				}
				return FEnumValue{EnumValueTypeName(Module, Module.StringPool[Constant.StringIndex]), Constant.I64};
			}
		default:
			return std::int32_t{0};
		}
	}

	bool IsNativeArrayTypeName(const std::string_view TypeName)
	{
		if (IsDynamicArrayTypeName(TypeName))
		{
			return false;
		}

		const std::size_t OpenBracket = TypeName.find('[');
		return OpenBracket != std::string_view::npos && TypeName.size() > OpenBracket + 2 && TypeName.back() == ']';
	}

	bool IsDynamicArrayTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 7 && TypeName.compare(0, 6, "array<") == 0 && TypeName.back() == '>';
	}

	bool IsMapTypeName(const std::string_view TypeName)
	{
		return (TypeName.size() > 5 && TypeName.compare(0, 4, "map<") == 0 && TypeName.back() == '>') || IsMultiMapTypeName(TypeName);
	}

	bool IsMultiMapTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 10 && TypeName.compare(0, 9, "multimap<") == 0 && TypeName.back() == '>';
	}

	bool IsSetTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 5 && TypeName.compare(0, 4, "set<") == 0 && TypeName.back() == '>';
	}

	bool IsQueueTypeName(const std::string_view TypeName)
	{
		return (TypeName.size() > 7 && TypeName.compare(0, 6, "queue<") == 0 && TypeName.back() == '>') || IsPriorityQueueTypeName(TypeName);
	}

	bool IsPriorityQueueTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 16 && TypeName.compare(0, 15, "priority_queue<") == 0 && TypeName.back() == '>';
	}

	bool IsMultiSetTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 10 && TypeName.compare(0, 9, "multiset<") == 0 && TypeName.back() == '>';
	}

	bool IsOrderedMultiSetTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 18 && TypeName.compare(0, 17, "ordered_multiset<") == 0 && TypeName.back() == '>';
	}

	bool IsAllowedPriorityQueueElementType(const std::string_view TypeName)
	{
		return TypeName == "bool" || TypeName == "char" || TypeName == "short" || TypeName == "int" || TypeName == "unsigned int" || TypeName == "long" || TypeName == "int64" || TypeName == "uint64" || TypeName == "unsigned long" || TypeName == "string" || TypeName == "wstring";
	}

	bool IsStackTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 7 && TypeName.compare(0, 6, "stack<") == 0 && TypeName.back() == '>';
	}

	bool IsOrderedMapTypeName(const std::string_view TypeName)
	{
		return (TypeName.size() > 13 && TypeName.compare(0, 12, "ordered_map<") == 0 && TypeName.back() == '>') || IsOrderedMultiMapTypeName(TypeName);
	}

	bool IsOrderedMultiMapTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 18 && TypeName.compare(0, 17, "ordered_multimap<") == 0 && TypeName.back() == '>';
	}

	bool IsDequeTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 7 && TypeName.compare(0, 6, "deque<") == 0 && TypeName.back() == '>';
	}

	bool IsChannelTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 9 && TypeName.compare(0, 8, "channel<") == 0 && TypeName.back() == '>';
	}

	bool IsMulticastDelegateTypeName(const std::string_view TypeName)
	{
		std::string_view Simple = TypeName.substr(FindLinkageIdentityStart(TypeName));
		const std::size_t Hash = Simple.find('#');
		const std::size_t Angle = Simple.find('<');
		const std::size_t Suffix = std::min(Hash, Angle);
		if (Suffix != std::string_view::npos)
		{
			Simple = Simple.substr(0, Suffix);
		}
		const std::size_t ColonSep = Simple.rfind("::");
		if (ColonSep != std::string_view::npos)
		{
			Simple = Simple.substr(ColonSep + 2);
		}
		return Simple == "multicast_delegate";
	}

	bool IsFunctionTypeName(const std::string_view TypeName)
	{
		return TypeName.size() > 5 && TypeName.compare(0, 3, "fn(") == 0 && TypeName.find(")->") != std::string_view::npos;
	}

	bool IsPairTypeName(const std::string_view TypeName)
	{
		std::string_view Simple = TypeName.substr(FindLinkageIdentityStart(TypeName));
		const std::size_t Hash = Simple.find('#');
		if (Hash != std::string_view::npos)
		{
			Simple = Simple.substr(0, Hash);
		}
		return Simple.size() > 6 && Simple.compare(0, 5, "pair<") == 0 && Simple.back() == '>';
	}

	bool ParseFunctionType(
	    const std::string_view TypeName,
	    std::vector<std::string>& OutParamTypes,
	    std::string& OutReturnType)
	{
		OutParamTypes.clear();
		OutReturnType.clear();
		if (!IsFunctionTypeName(TypeName))
		{
			return false;
		}

		const std::size_t OpenParen = 2;
		if (OpenParen >= TypeName.size() || TypeName[OpenParen] != '(')
		{
			return false;
		}

		int AngleDepth = 0;
		int ParenDepth = 0;
		std::size_t ParamStart = OpenParen + 1;
		std::size_t ArrowAt = std::string_view::npos;
		for (std::size_t Index = OpenParen; Index + 2 < TypeName.size(); ++Index)
		{
			const char Character = TypeName[Index];
			if (Character == '<')
			{
				++AngleDepth;
			}
			else if (Character == '>' && (Index == 0 || TypeName[Index - 1] != '-'))
			{
				// `->` belongs to a nested callable signature, not to generic
				// angle-bracket nesting (for example fn(fn()->void)->void).
				--AngleDepth;
				if (AngleDepth < 0)
				{
					return false;
				}
			}
			else if (Character == '(')
			{
				++ParenDepth;
			}
			else if (Character == ')')
			{
				--ParenDepth;
				if (ParenDepth < 0)
				{
					return false;
				}
				if (ParenDepth == 0 && AngleDepth == 0 && Index + 2 < TypeName.size() && TypeName[Index + 1] == '-' && TypeName[Index + 2] == '>')
				{
					ArrowAt = Index;
					break;
				}
			}
			else if (Character == ',' && ParenDepth == 1 && AngleDepth == 0)
			{
				const std::string_view Param = TypeName.substr(ParamStart, Index - ParamStart);
				if (Param.empty())
				{
					return false;
				}
				OutParamTypes.emplace_back(Param);
				ParamStart = Index + 1;
			}
		}

		if (ArrowAt == std::string_view::npos || ArrowAt < OpenParen + 1)
		{
			return false;
		}

		if (ParamStart < ArrowAt)
		{
			const std::string_view Param = TypeName.substr(ParamStart, ArrowAt - ParamStart);
			if (!Param.empty())
			{
				OutParamTypes.emplace_back(Param);
			}
		}

		OutReturnType.assign(TypeName.substr(ArrowAt + 3));
		return !OutReturnType.empty();
	}

	std::string MakeFunctionTypeName(
	    const std::vector<std::string>& ParamTypes,
	    const std::string_view ReturnType)
	{
		std::string Result = "fn(";
		for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
		{
			if (Index > 0)
			{
				Result += ',';
			}
			Result += ParamTypes[Index];
		}
		Result += ")->";
		Result.append(ReturnType);
		return Result;
	}

	bool IsReferenceTypeName(const std::string_view TypeName)
	{
		return TypeName.rfind("ref ", 0) == 0 || TypeName.rfind("const ref ", 0) == 0;
	}

	bool IsConstReferenceTypeName(const std::string_view TypeName)
	{
		return TypeName.rfind("const ref ", 0) == 0;
	}

	std::string_view StripReferenceTypeName(const std::string_view TypeName)
	{
		if (TypeName.rfind("const ref ", 0) == 0)
		{
			return TypeName.substr(10);
		}
		if (TypeName.rfind("ref ", 0) == 0)
		{
			return TypeName.substr(4);
		}
		return TypeName;
	}

	bool IsCommonContainerTypeName(const std::string_view TypeName)
	{
		return IsMapTypeName(TypeName) || IsSetTypeName(TypeName) || IsMultiSetTypeName(TypeName) || IsOrderedMultiSetTypeName(TypeName) || IsQueueTypeName(TypeName) || IsStackTypeName(TypeName) || IsOrderedMapTypeName(TypeName) || IsDequeTypeName(TypeName);
	}

	bool ParseDynamicArrayElementType(const std::string_view TypeName, std::string& OutElementType)
	{
		if (!IsDynamicArrayTypeName(TypeName))
		{
			return false;
		}
		const std::size_t PrefixLength =
		    IsPriorityQueueTypeName(TypeName) ? 15u : 6u;
		OutElementType.assign(
		    TypeName.substr(PrefixLength, TypeName.size() - PrefixLength - 1));
		return !OutElementType.empty() && OutElementType != "void";
	}

	namespace
	{
		bool SplitGenericArgs(
		    const std::string_view Inner,
		    std::string& OutFirst,
		    std::string& OutSecond)
		{
			int Depth = 0;
			int ParenthesisDepth = 0;
			for (std::size_t Index = 0; Index < Inner.size(); ++Index)
			{
				const char Character = Inner[Index];
				if (Character == '<')
				{
					++Depth;
				}
				else if (Character == '>' && (Index == 0 || Inner[Index - 1] != '-'))
				{
					--Depth;
					if (Depth < 0)
					{
						return false;
					}
				}
				else if (Character == '(')
				{
					++ParenthesisDepth;
				}
				else if (Character == ')')
				{
					if (--ParenthesisDepth < 0)
					{
						return false;
					}
				}
				else if (Character == ',' && Depth == 0 && ParenthesisDepth == 0)
				{
					OutFirst.assign(Inner.substr(0, Index));
					OutSecond.assign(Inner.substr(Index + 1));
					return !OutFirst.empty() && !OutSecond.empty();
				}
			}
			return false;
		}

		bool IsValidContainerElementType(const std::string_view ElementType)
		{
			return !ElementType.empty() && ElementType != "void";
		}
	}

	bool IsAllowedMapOrSetKeyType(const std::string_view KeyType)
	{
		return KeyType == "char" || KeyType == "int" || KeyType == "unsigned int" || KeyType == "int64" || KeyType == "uint64" || KeyType == "string" || KeyType == "wstring";
	}

	bool SplitMapType(
	    const std::string_view TypeName,
	    std::string& OutKeyType,
	    std::string& OutValueType)
	{
		if (!IsMapTypeName(TypeName))
		{
			return false;
		}
		const std::size_t PrefixLength = IsMultiMapTypeName(TypeName) ? 9 : 4;
		const std::string_view Inner =
		    TypeName.substr(PrefixLength, TypeName.size() - PrefixLength - 1);
		if (!SplitGenericArgs(Inner, OutKeyType, OutValueType))
		{
			return false;
		}
		return !OutKeyType.empty() && IsValidContainerElementType(OutValueType);
	}

	bool ParsePairType(
	    const std::string_view TypeName,
	    std::string& OutFirstType,
	    std::string& OutSecondType)
	{
		OutFirstType.clear();
		OutSecondType.clear();
		std::string_view Simple = TypeName.substr(FindLinkageIdentityStart(TypeName));
		if (Simple.size() <= 6 || Simple.compare(0, 5, "pair<") != 0 || Simple.back() != '>')
		{
			return false;
		}
		const std::string_view Inner = Simple.substr(5, Simple.size() - 6);
		return SplitGenericArgs(Inner, OutFirstType, OutSecondType) && !OutFirstType.empty() && !OutSecondType.empty();
	}

	std::string MakePairTypeName(const std::string_view FirstType, const std::string_view SecondType)
	{
		std::string Result = "pair<";
		Result.append(FirstType);
		Result.push_back(',');
		Result.append(SecondType);
		Result.push_back('>');
		return Result;
	}

	bool ParseMapType(
	    const std::string_view TypeName,
	    std::string& OutKeyType,
	    std::string& OutValueType)
	{
		if (!SplitMapType(TypeName, OutKeyType, OutValueType))
		{
			return false;
		}
		if (IsMultiMapTypeName(TypeName))
		{
			OutValueType = "array<" + OutValueType + ">";
		}
		// Semantic key validation needs the host binding catalog and is performed
		// by the compiler. Runtime parsing remains structural.
		return true;
	}

	bool SplitSetElementType(const std::string_view TypeName, std::string& OutElementType)
	{
		if (!IsSetTypeName(TypeName))
		{
			return false;
		}
		OutElementType.assign(TypeName.substr(4, TypeName.size() - 5));
		return IsValidContainerElementType(OutElementType);
	}

	bool ParseSetElementType(const std::string_view TypeName, std::string& OutElementType)
	{
		return SplitSetElementType(TypeName, OutElementType);
	}

	bool ParseMultiSetElementType(
	    const std::string_view TypeName,
	    std::string& OutElementType)
	{
		std::size_t PrefixLength = 0;
		if (IsMultiSetTypeName(TypeName))
		{
			PrefixLength = 9;
		}
		else if (IsOrderedMultiSetTypeName(TypeName))
		{
			PrefixLength = 17;
		}
		else
		{
			return false;
		}
		OutElementType.assign(
		    TypeName.substr(PrefixLength, TypeName.size() - PrefixLength - 1));
		return IsValidContainerElementType(OutElementType);
	}

	bool ParseQueueElementType(const std::string_view TypeName, std::string& OutElementType)
	{
		if (!IsQueueTypeName(TypeName))
		{
			return false;
		}
		const std::size_t PrefixLength =
		    IsPriorityQueueTypeName(TypeName) ? 15u : 6u;
		OutElementType.assign(
		    TypeName.substr(PrefixLength, TypeName.size() - PrefixLength - 1));
		return IsValidContainerElementType(OutElementType);
	}

	bool ParseStackElementType(const std::string_view TypeName, std::string& OutElementType)
	{
		if (!IsStackTypeName(TypeName))
		{
			return false;
		}
		OutElementType.assign(TypeName.substr(6, TypeName.size() - 7));
		return IsValidContainerElementType(OutElementType);
	}

	bool IsAllowedChannelElementType(const std::string_view ElementType)
	{
		// Keep runtime, native metadata and interop validation on one canonical
		// rule so a channel type cannot be accepted by one boundary and rejected
		// by another.
		return NativeChannelElementTypeIsValid(ElementType);
	}

	bool ParseChannelElementType(const std::string_view TypeName, std::string& OutElementType)
	{
		if (!IsChannelTypeName(TypeName))
		{
			return false;
		}
		OutElementType.assign(TypeName.substr(8, TypeName.size() - 9));
		return IsAllowedChannelElementType(OutElementType);
	}

	bool SplitOrderedMapType(
	    const std::string_view TypeName,
	    std::string& OutKeyType,
	    std::string& OutValueType)
	{
		if (!IsOrderedMapTypeName(TypeName))
		{
			return false;
		}
		const std::size_t PrefixLength = IsOrderedMultiMapTypeName(TypeName) ? 17 : 12;
		const std::string_view Inner =
		    TypeName.substr(PrefixLength, TypeName.size() - PrefixLength - 1);
		if (!SplitGenericArgs(Inner, OutKeyType, OutValueType))
		{
			return false;
		}
		return !OutKeyType.empty() && IsValidContainerElementType(OutValueType);
	}

	bool ParseOrderedMapType(
	    const std::string_view TypeName,
	    std::string& OutKeyType,
	    std::string& OutValueType)
	{
		if (!SplitOrderedMapType(TypeName, OutKeyType, OutValueType))
		{
			return false;
		}
		if (IsOrderedMultiMapTypeName(TypeName))
		{
			OutValueType = "array<" + OutValueType + ">";
		}
		return true;
	}

	bool ParseDequeElementType(const std::string_view TypeName, std::string& OutElementType)
	{
		if (!IsDequeTypeName(TypeName))
		{
			return false;
		}
		OutElementType.assign(TypeName.substr(6, TypeName.size() - 7));
		return IsValidContainerElementType(OutElementType);
	}

	bool IsScriptArgsArrayTypeName(const std::string_view TypeName)
	{
		return TypeName == "string[]";
	}

	FNativeArray CreateScriptArgsArray(const std::vector<std::string_view>& Args)
	{
		FNativeArray Array;
		Array.TypeName() = "string[]";
		Array.ElementTypeName() = "string";
		Array.Length() = static_cast<std::uint32_t>(Args.size());
		Array.Storage().Elements.reserve(Args.size());
		for (const std::string_view Arg : Args)
		{
			Array.Storage().Elements.push_back(std::string(Arg));
		}
		return Array;
	}

	namespace
	{
		constexpr std::size_t MaxFormatWidth = 1024;

		struct FFormatControl
		{
			StandardLibrary::FServiceContext Context;
			StandardLibrary::FServiceResult* Failure;
			StandardLibrary::FServiceResult Last;
			StandardLibrary::Detail::FWorkMeter Meter;
			FFormatControl(const StandardLibrary::FServiceContext* C, StandardLibrary::FServiceResult* F)
			    : Context(C ? *C : StandardLibrary::FServiceContext{}), Failure(F), Meter(Context)
			{
				if (Failure)
				{
					*Failure = {};
				}
			}
			bool Check(StandardLibrary::FServiceResult R)
			{
				if (!R)
				{
					Last = R;
					if (Failure)
					{
						*Failure = R;
					}
				}
				return static_cast<bool>(Last);
			}
			bool Tick()
			{
				return Check(Meter.Add());
			}
			bool Finish()
			{
				return Check(Meter.Flush());
			}
			bool Append(std::string& Out, std::string_view Text)
			{
				if (Text.size() > Out.max_size() - Out.size()) return Check({StandardLibrary::EServiceStatus::LimitExceeded, 0});
				return Check(Context.CheckText(Out.size() + Text.size())) && Check(StandardLibrary::Detail::Append(Out, Text, Context));
			}
			bool Byte(std::string& Out, char C)
			{
				if (Out.size() == Out.max_size() || Out.size() >= static_cast<std::uint64_t>(INT64_MAX) || (Context.Limits.MaxTextBytes && Out.size() >= Context.Limits.MaxTextBytes))
				{
					return Check({StandardLibrary::EServiceStatus::LimitExceeded, 0});
				}
				Out += C;
				return Tick();
			}
		};

		bool ParseDecimalUInt(
		    const std::string& Text,
		    std::size_t& Index,
		    std::size_t& OutValue,
		    std::string& OutError, FFormatControl& Control)
		{
			if (Index >= Text.size() || Text[Index] < '0' || Text[Index] > '9')
			{
				OutError = "format: expected digits";
				return false;
			}
			std::size_t Value = 0;
			while (Index < Text.size() && Text[Index] >= '0' && Text[Index] <= '9')
			{
				if (!Control.Tick()) return false;
				const std::size_t Digit = static_cast<std::size_t>(Text[Index] - '0');
				if (Value > (MaxFormatWidth - Digit) / 10)
				{
					OutError = "format: width/precision out of range";
					return false;
				}
				Value = Value * 10 + Digit;
				++Index;
			}
			OutValue = Value;
			return true;
		}
	}

	bool TryParseInt32(const std::string_view Text, std::int32_t& OutValue, std::string& OutError)
	{
		FValue Parsed;
		if (!StandardLibrary::ParseScalar(Text, "int", 10, false, Parsed))
		{
			OutError = Text.empty() ? "expected int, got empty token" : "invalid int token";
			return false;
		}
		OutValue = std::get<std::int32_t>(Parsed);
		return true;
	}

	bool TryParseDouble(const std::string_view Text, double& OutValue, std::string& OutError)
	{
		FValue Parsed;
		if (!StandardLibrary::ParseScalar(Text, "double", 10, false, Parsed))
		{
			OutError = Text.empty() ? "expected double, got empty token" : "invalid double token";
			return false;
		}
		OutValue = std::get<double>(Parsed);
		return true;
	}

	namespace
	{
		bool ApplyFormatSpec(
		    const std::string& Input,
		    char Align,
		    std::size_t Width,
		    bool bHasPrecision,
		    std::size_t Precision,
		    char TypeCode,
		    std::string& Out,
		    std::string& OutError, FFormatControl& Control)
		{
			std::string Value;
			if (!Control.Check(StandardLibrary::Repeat(Input, 1, Value, Control.Context))) return false;
			if (TypeCode == 'x' || TypeCode == 'X')
			{
				FValue IntValue;
				const bool Negative = !Value.empty() && Value.front() == '-';
				if (!Control.Check(StandardLibrary::ParseScalar(Value, Negative ? "int64" : "uint64", 10, false, IntValue, Control.Context)))
				{
					OutError = "format: {:x} requires integer value";
					return false;
				}
				std::ostringstream Stream;
				Stream << std::hex;
				if (TypeCode == 'X')
				{
					Stream << std::uppercase;
				}
				const auto Unsigned = Negative ? static_cast<std::uint64_t>(std::get<std::int64_t>(IntValue))
				                               : std::get<std::uint64_t>(IntValue);
				Stream << Unsigned;
				Value = Stream.str();
			}
			else if (bHasPrecision)
			{
				FValue Parsed;
				if (!Control.Check(StandardLibrary::ParseScalar(Value, "double", 10, false, Parsed, Control.Context)))
				{
					OutError = "format: precision requires numeric value";
					return false;
				}
				std::ostringstream Stream;
				Stream.setf(std::ios::fixed, std::ios::floatfield);
				Stream << std::setprecision(static_cast<int>(Precision)) << std::get<double>(Parsed);
				Value = Stream.str();
			}

			if (Width > MaxFormatWidth)
			{
				OutError = "format: width out of range";
				return false;
			}
			if (Width <= Value.size())
			{
				Out = std::move(Value);
				return true;
			}
			const std::size_t Pad = Width - Value.size();
			if (Align == '<')
			{
				Out = std::move(Value);
				Out.append(Pad, ' ');
			}
			else if (Align == '^')
			{
				const std::size_t Left = Pad / 2;
				const std::size_t Right = Pad - Left;
				Out.assign(Left, ' ');
				if (!Control.Append(Out, Value)) return false;
				Out.append(Right, ' ');
			}
			else
			{
				Out.assign(Pad, ' ');
				if (!Control.Append(Out, Value)) return false;
			}
			return true;
		}
	}

	bool TryFormatString(
	    const std::string& Fmt,
	    const std::vector<std::string>& Args,
	    std::string& OutResult,
	    std::string& OutError,
	    const StandardLibrary::FServiceContext* Context, StandardLibrary::FServiceResult* Failure)
	{
		FFormatControl Control(Context, Failure);
		if (!Control.Check(Control.Context.CheckText(Fmt.size()))) return false;
		OutResult.clear();
		OutResult.reserve(Fmt.size());
		std::size_t AutoIndex = 0;
		for (std::size_t Index = 0; Index < Fmt.size();)
		{
			if (!Control.Tick()) return false;
			const char Ch = Fmt[Index];
			if (Ch == '{')
			{
				if (Index + 1 >= Fmt.size())
				{
					OutError = "format: trailing '{'";
					return false;
				}
				if (Fmt[Index + 1] == '{')
				{
					if (!Control.Byte(OutResult, '{')) return false;
					Index += 2;
					continue;
				}

				std::size_t Cursor = Index + 1;
				std::size_t ArgIndex = AutoIndex;
				bool bExplicitIndex = false;
				if (Cursor < Fmt.size() && Fmt[Cursor] >= '0' && Fmt[Cursor] <= '9')
				{
					if (!ParseDecimalUInt(Fmt, Cursor, ArgIndex, OutError, Control))
					{
						return false;
					}
					bExplicitIndex = true;
				}

				char Align = '>';
				std::size_t Width = 0;
				bool bHasWidth = false;
				bool bHasPrecision = false;
				std::size_t Precision = 0;
				char TypeCode = 0;
				if (Cursor < Fmt.size() && Fmt[Cursor] == ':')
				{
					++Cursor;
					if (Cursor < Fmt.size() && (Fmt[Cursor] == '<' || Fmt[Cursor] == '>' || Fmt[Cursor] == '^'))
					{
						Align = Fmt[Cursor];
						++Cursor;
					}
					if (Cursor < Fmt.size() && Fmt[Cursor] >= '0' && Fmt[Cursor] <= '9')
					{
						if (!ParseDecimalUInt(Fmt, Cursor, Width, OutError, Control))
						{
							return false;
						}
						bHasWidth = true;
						if (Width > MaxFormatWidth)
						{
							OutError = "format: width out of range";
							return false;
						}
					}
					if (Cursor < Fmt.size() && Fmt[Cursor] == '.')
					{
						++Cursor;
						if (!ParseDecimalUInt(Fmt, Cursor, Precision, OutError, Control))
						{
							return false;
						}
						bHasPrecision = true;
						if (Precision > MaxFormatWidth)
						{
							OutError = "format: precision out of range";
							return false;
						}
					}
					if (Cursor < Fmt.size() && (Fmt[Cursor] == 'x' || Fmt[Cursor] == 'X'))
					{
						TypeCode = Fmt[Cursor];
						++Cursor;
					}
				}

				if (Cursor >= Fmt.size() || Fmt[Cursor] != '}')
				{
					OutError = "format: invalid placeholder";
					return false;
				}
				++Cursor;

				if (ArgIndex >= Args.size())
				{
					OutError = "format: not enough arguments";
					return false;
				}
				std::string Formatted;
				if (!ApplyFormatSpec(
				        Args[ArgIndex],
				        Align,
				        bHasWidth ? Width : 0,
				        bHasPrecision,
				        Precision,
				        TypeCode,
				        Formatted,
				        OutError, Control))
				{
					return false;
				}
				if (!Control.Append(OutResult, Formatted)) return false;
				if (!bExplicitIndex)
				{
					++AutoIndex;
				}
				Index = Cursor;
				continue;
			}
			if (Ch == '}')
			{
				if (Index + 1 < Fmt.size() && Fmt[Index + 1] == '}')
				{
					if (!Control.Byte(OutResult, '}')) return false;
					Index += 2;
					continue;
				}
				OutError = "format: unmatched '}'";
				return false;
			}
			if (!Control.Byte(OutResult, Ch)) return false;
			++Index;
		}
		return Control.Finish();
	}

	bool TryPrintfFormatString(
	    const std::string& Fmt,
	    const std::vector<std::string>& Args,
	    std::string& OutResult,
	    std::string& OutError,
	    const StandardLibrary::FServiceContext* Context, StandardLibrary::FServiceResult* Failure)
	{
		FFormatControl Control(Context, Failure);
		if (!Control.Check(Control.Context.CheckText(Fmt.size()))) return false;
		const auto ParseSigned = [&OutError, &Control](
		                             std::string_view Text,
		                             std::int64_t& OutValue) -> bool
		{
			FValue Parsed;
			if (!Control.Check(StandardLibrary::ParseScalar(Text, "int64", 10, false, Parsed, Control.Context)))
			{
				OutError = "formatf: integer conversion requires an integer value";
				return false;
			}
			OutValue = std::get<std::int64_t>(Parsed);
			return true;
		};
		const auto ParseUnsigned = [&OutError, &Control](
		                               std::string_view Text,
		                               std::uint64_t& OutValue) -> bool
		{
			FValue Parsed;
			if (!Control.Check(StandardLibrary::ParseScalar(Text, "uint64", 10, false, Parsed, Control.Context)))
			{
				OutError = "formatf: unsigned conversion requires a non-negative integer";
				return false;
			}
			OutValue = std::get<std::uint64_t>(Parsed);
			return true;
		};
		const auto UnsignedDigits = [](std::uint64_t Value, const unsigned Base,
		                                const bool bUppercase) -> std::string
		{
			const char* const Alphabet = bUppercase ? "0123456789ABCDEF" : "0123456789abcdef";
			char Buffer[65]{};
			char* Cursor = Buffer + sizeof(Buffer);
			do
			{
				*--Cursor = Alphabet[Value % Base];
				Value /= Base;
			}
			while (Value != 0);
			return std::string(Cursor, Buffer + sizeof(Buffer));
		};
		const auto ApplyWidth = [](
		                            std::string Value,
		                            const std::size_t Width,
		                            const bool bLeft,
		                            const bool bZero,
		                            const std::size_t PrefixLength) -> std::string
		{
			if (Width <= Value.size())
			{
				return Value;
			}
			const std::size_t Padding = Width - Value.size();
			if (bLeft)
			{
				Value.append(Padding, ' ');
				return Value;
			}
			if (bZero)
			{
				Value.insert(std::min(PrefixLength, Value.size()), Padding, '0');
				return Value;
			}
			Value.insert(0, Padding, ' ');
			return Value;
		};

		OutResult.clear();
		OutResult.reserve(Fmt.size());
		std::size_t ArgIndex = 0;
		for (std::size_t Index = 0; Index < Fmt.size();)
		{
			if (Fmt[Index] != '%')
			{
				if (!Control.Byte(OutResult, Fmt[Index++])) return false;
				continue;
			}
			if (Index + 1 >= Fmt.size())
			{
				OutError = "formatf: trailing '%'";
				return false;
			}
			if (Fmt[Index + 1] == '%')
			{
				if (!Control.Byte(OutResult, '%')) return false;
				Index += 2;
				continue;
			}

			std::size_t Cursor = Index + 1;
			bool bLeft = false;
			bool bPlus = false;
			bool bSpace = false;
			bool bAlternate = false;
			bool bZero = false;
			for (; Cursor < Fmt.size(); ++Cursor)
			{
				if (!Control.Tick()) return false;
				switch (Fmt[Cursor])
				{
				case '-':
					{
						bLeft = true;
						continue;
					}
				case '+':
					{
						bPlus = true;
						continue;
					}
				case ' ':
					{
						bSpace = true;
						continue;
					}
				case '#':
					{
						bAlternate = true;
						continue;
					}
				case '0':
					{
						bZero = true;
						continue;
					}
				default:
					{
						break;
					}
				}
				break;
			}

			std::size_t Width = 0;
			if (Cursor < Fmt.size() && Fmt[Cursor] == '*')
			{
				OutError = "formatf: dynamic width '*' is not supported";
				return false;
			}
			if (Cursor < Fmt.size() && Fmt[Cursor] >= '0' && Fmt[Cursor] <= '9')
			{
				if (!ParseDecimalUInt(Fmt, Cursor, Width, OutError, Control) || Width > MaxFormatWidth)
				{
					OutError = "formatf: width out of range";
					return false;
				}
			}

			bool bHasPrecision = false;
			std::size_t Precision = 0;
			if (Cursor < Fmt.size() && Fmt[Cursor] == '.')
			{
				bHasPrecision = true;
				++Cursor;
				if (Cursor < Fmt.size() && Fmt[Cursor] == '*')
				{
					OutError = "formatf: dynamic precision '*' is not supported";
					return false;
				}
				if (Cursor >= Fmt.size() || Fmt[Cursor] < '0' || Fmt[Cursor] > '9' || !ParseDecimalUInt(Fmt, Cursor, Precision, OutError, Control) || Precision > MaxFormatWidth)
				{
					OutError = "formatf: precision out of range";
					return false;
				}
			}

			// Integer length modifiers are accepted for source compatibility.
			// VeroScript values already carry full-width numeric text, so they do
			// not alter conversion or ABI behavior.
			if (Cursor < Fmt.size() && (Fmt[Cursor] == 'h' || Fmt[Cursor] == 'l'))
			{
				const char Modifier = Fmt[Cursor++];
				if (Cursor < Fmt.size() && Fmt[Cursor] == Modifier)
				{
					++Cursor;
				}
			}
			else if (Cursor < Fmt.size() && (Fmt[Cursor] == 'j' || Fmt[Cursor] == 'z' || Fmt[Cursor] == 't'))
			{
				++Cursor;
			}

			if (Cursor >= Fmt.size())
			{
				OutError = "formatf: incomplete conversion";
				return false;
			}
			const char Conversion = Fmt[Cursor++];
			const std::string_view Supported = "diuoxXfFeEgGcs";
			if (Supported.find(Conversion) == std::string_view::npos)
			{
				OutError = std::string("formatf: unsupported conversion '%") + Conversion + "'";
				return false;
			}
			if ((Conversion == 'c' || Conversion == 's') && (bPlus || bSpace || bAlternate || bZero))
			{
				OutError =
				    "formatf: only '-' and width are valid flags for %c/%s";
				return false;
			}
			if (Conversion == 'c' && bHasPrecision)
			{
				OutError = "formatf: precision is not valid for %c";
				return false;
			}
			if (ArgIndex >= Args.size())
			{
				OutError = "formatf: not enough arguments";
				return false;
			}
			const std::string& Argument = Args[ArgIndex++];
			std::string Formatted;
			std::size_t PrefixLength = 0;
			bool bNumeric = false;
			bool bIntegerConversion = false;

			if (Conversion == 'd' || Conversion == 'i')
			{
				std::int64_t Value = 0;
				if (!ParseSigned(Argument, Value))
				{
					return false;
				}
				const bool bNegative = Value < 0;
				const std::uint64_t Magnitude = bNegative
				    ? 0ull - static_cast<std::uint64_t>(Value)
				    : static_cast<std::uint64_t>(Value);
				std::string Digits = UnsignedDigits(Magnitude, 10, false);
				if (bHasPrecision && Precision == 0 && Magnitude == 0)
				{
					Digits.clear();
				}
				if (bHasPrecision && Digits.size() < Precision)
				{
					Digits.insert(0, Precision - Digits.size(), '0');
				}
				const char Sign = bNegative ? '-' : (bPlus ? '+' : (bSpace ? ' ' : '\0'));
				if (Sign != '\0')
				{
					Formatted += Sign;
					PrefixLength = 1;
				}
				Formatted += Digits;
				bNumeric = true;
				bIntegerConversion = true;
			}
			else if (Conversion == 'u' || Conversion == 'o' || Conversion == 'x' || Conversion == 'X')
			{
				std::uint64_t Value = 0;
				if (!ParseUnsigned(Argument, Value))
				{
					return false;
				}
				const unsigned Base = Conversion == 'o'
				    ? 8u
				    : ((Conversion == 'x' || Conversion == 'X') ? 16u : 10u);
				std::string Digits = UnsignedDigits(Value, Base, Conversion == 'X');
				if (bHasPrecision && Precision == 0 && Value == 0)
				{
					Digits.clear();
				}
				if (bHasPrecision && Digits.size() < Precision)
				{
					Digits.insert(0, Precision - Digits.size(), '0');
				}
				if (bAlternate && Value != 0 && (Conversion == 'x' || Conversion == 'X'))
				{
					Formatted = Conversion == 'X' ? "0X" : "0x";
					PrefixLength = 2;
				}
				else if (bAlternate && Conversion == 'o' && (Digits.empty() || Digits.front() != '0'))
				{
					Formatted = "0";
					PrefixLength = 1;
				}
				Formatted += Digits;
				bNumeric = true;
				bIntegerConversion = true;
			}
			else if (Conversion == 'f' || Conversion == 'F' || Conversion == 'e' || Conversion == 'E' || Conversion == 'g' || Conversion == 'G')
			{
				FValue Parsed;
				if (!Control.Check(StandardLibrary::ParseScalar(Argument, "double", 10, false, Parsed, Control.Context)))
				{
					OutError = "formatf: floating conversion requires a finite numeric value";
					return false;
				}
				std::ostringstream Stream;
				if (Conversion == 'f' || Conversion == 'F')
				{
					Stream << std::fixed;
				}
				else if (Conversion == 'e' || Conversion == 'E')
				{
					Stream << std::scientific;
				}
				else
				{
					Stream << std::defaultfloat;
				}
				if (Conversion == 'F' || Conversion == 'E' || Conversion == 'G')
				{
					Stream << std::uppercase;
				}
				if (bAlternate)
				{
					Stream << std::showpoint;
				}
				if (bPlus)
				{
					Stream << std::showpos;
				}
				Stream << std::setprecision(
				              static_cast<int>(bHasPrecision ? Precision : 6u))
				       << std::get<double>(Parsed);
				Formatted = Stream.str();
				if (!bPlus && bSpace && (Formatted.empty() || Formatted.front() != '-'))
				{
					Formatted.insert(Formatted.begin(), ' ');
				}
				PrefixLength = !Formatted.empty() && (Formatted.front() == '-' || Formatted.front() == '+' || Formatted.front() == ' ')
				    ? 1u
				    : 0u;
				bNumeric = true;
			}
			else if (Conversion == 'c')
			{
				std::int64_t Code = 0;
				if (ParseSigned(Argument, Code))
				{
					if (Code < 0 || Code > 255)
					{
						OutError = "formatf: %c value is outside byte range";
						return false;
					}
					Formatted.assign(1, static_cast<char>(Code));
				}
				else
				{
					if (!Control.Last && !Control.Last.IsParseFailure()) return false;
					Control.Last = {};
					if (Failure)
					{
						*Failure = {};
					}
					OutError.clear();
					if (Argument.size() != 1)
					{
						OutError = "formatf: %c requires an integer or one-byte string";
						return false;
					}
					if (!Control.Check(StandardLibrary::Repeat(Argument, 1, Formatted, Control.Context))) return false;
				}
			}
			else
			{
				if (!Control.Check(StandardLibrary::Repeat(Argument, 1, Formatted, Control.Context))) return false;
				if (bHasPrecision && Formatted.size() > Precision)
				{
					Formatted.resize(Precision);
				}
			}

			Formatted = ApplyWidth(
			    std::move(Formatted),
			    Width,
			    bLeft,
			    bZero && bNumeric && (!bHasPrecision || !bIntegerConversion) && !bLeft,
			    PrefixLength);
			if (!Control.Append(OutResult, Formatted)) return false;
			Index = Cursor;
		}
		return Control.Finish();
	}

	bool ParseNativeArrayType(
	    const std::string_view TypeName,
	    std::string& OutElementType,
	    std::uint32_t& OutLength)
	{
		if (IsScriptArgsArrayTypeName(TypeName))
		{
			return false;
		}

		// Leftmost peel: "int[2][3]" → element "int[3]", length 2 (C++ outer-to-inner).
		const std::size_t OpenBracket = TypeName.find('[');
		if (OpenBracket == std::string_view::npos || OpenBracket == 0)
		{
			return false;
		}
		const std::size_t CloseBracket = TypeName.find(']', OpenBracket + 1);
		if (CloseBracket == std::string_view::npos || CloseBracket <= OpenBracket + 1)
		{
			return false;
		}

		const std::string_view SizeText = TypeName.substr(OpenBracket + 1, CloseBracket - OpenBracket - 1);
		if (SizeText.empty())
		{
			return false;
		}
		for (const char Character : SizeText)
		{
			if (Character < '0' || Character > '9')
			{
				return false;
			}
		}

		std::uint64_t ParsedLength = 0;
		for (const char Character : SizeText)
		{
			ParsedLength = ParsedLength * 10 + static_cast<std::uint64_t>(Character - '0');
			if (ParsedLength > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}
		}
		if (ParsedLength == 0)
		{
			return false;
		}

		OutElementType.clear();
		OutElementType.append(TypeName.data(), OpenBracket);
		OutElementType.append(
		    TypeName.data() + CloseBracket + 1,
		    TypeName.size() - (CloseBracket + 1));
		OutLength = static_cast<std::uint32_t>(ParsedLength);
		return true;
	}

	bool ValidateNativeArrayTypeLimits(const std::string_view TypeName, std::string* OutError)
	{
		if (!IsNativeArrayTypeName(TypeName))
		{
			return true;
		}

		std::uint64_t TotalElements = 1;
		std::size_t DimensionCount = 0;
		std::string Remaining(TypeName);
		while (IsNativeArrayTypeName(Remaining))
		{
			++DimensionCount;
			if (DimensionCount > MaxNativeArrayDimensions)
			{
				if (OutError)
				{
					*OutError = "native array dimension count exceeds limit";
				}
				return false;
			}
			std::string ElementType;
			std::uint32_t Length = 0;
			if (!ParseNativeArrayType(Remaining, ElementType, Length))
			{
				if (OutError)
				{
					*OutError = "invalid native array type";
				}
				return false;
			}
			if (Length > MaxNativeArrayDimensionLength)
			{
				if (OutError)
				{
					*OutError = "native array size exceeds limit";
				}
				return false;
			}
			TotalElements *= static_cast<std::uint64_t>(Length);
			if (TotalElements > MaxNativeArrayTotalElements)
			{
				if (OutError)
				{
					*OutError = "native array size exceeds limit";
				}
				return false;
			}
			Remaining = std::move(ElementType);
		}
		return true;
	}

	namespace
	{
		constexpr std::size_t MaxRecursiveValueDepth = 128;

		struct FValueCloneContext
		{
			std::size_t Depth = 0;
			std::unordered_set<const FScriptObject*> ActiveStructs;
		};

		class FValueTraversalDepthScope
		{
		public:
			explicit FValueTraversalDepthScope(std::size_t& InDepth)
			{
				if (InDepth < MaxRecursiveValueDepth)
				{
					Depth = &InDepth;
					++*Depth;
				}
			}

			~FValueTraversalDepthScope()
			{
				if (Depth)
				{
					--*Depth;
				}
			}

			explicit operator bool() const noexcept
			{
				return Depth != nullptr;
			}

		private:
			std::size_t* Depth = nullptr;
		};

		class FActiveStructCloneScope
		{
		public:
			FActiveStructCloneScope(FValueCloneContext& InContext, const FScriptObject& Object)
			    : Context(InContext), ObjectAddress(&Object), bInserted(Context.ActiveStructs.insert(ObjectAddress).second)
			{
			}

			~FActiveStructCloneScope()
			{
				if (bInserted)
				{
					Context.ActiveStructs.erase(ObjectAddress);
				}
			}

			explicit operator bool() const noexcept
			{
				return bInserted;
			}

		private:
			FValueCloneContext& Context;
			const FScriptObject* ObjectAddress = nullptr;
			bool bInserted = false;
		};

		bool TryInvokeStampedHostClone(const FHostObjectHandle& Source,
		    FHostObjectHandle& OutCloned)
		{
			// Source commonly aliases a value stored in a VM frame or container. A host
			// clone callback is allowed to reenter the session, which can reallocate
			// those owners and invalidate the reference passed to this helper. Keep a
			// strong handle locally before crossing the callback boundary, and use that
			// stable handle for all post-callback metadata reads.
			const FHostObjectHandle StableSource = Source;
			if (!StableSource || StableSource->CloneCallback == nullptr)
			{
				return false;
			}
			const FHostValueCallback Callback = StableSource->CloneCallback;
			FValue Arg = StableSource;
			FValue OutReturn;
			std::string OutError;
			FNativeCallContext Context;
			Context.Args = &Arg;
			Context.ArgCount = 1;
			InheritHostValueServices(Context);
			Context.UserData = StableSource->CloneUserData;
			Context.HostThis = StableSource->Payload;
			bool bCallbackOk = false;
			try
			{
				if (Context.ScriptInvoker)
				{
					Context.ScriptInvoker->NotifyExecutionCommitted();
				}
				bCallbackOk = Callback(Context, OutReturn, OutError);
			}
			catch (...)
			{
				// Host callbacks are an ABI boundary. A foreign C++ exception must
				// never escape value cloning into the VM/runtime.
				return false;
			}
			if (!bCallbackOk)
			{
				return false;
			}
			const auto* const ClonedHandle = std::get_if<FHostObjectHandle>(&OutReturn);
			if (!ClonedHandle || !*ClonedHandle)
			{
				return false;
			}
			(*ClonedHandle)->CloneCallback = StableSource->CloneCallback;
			(*ClonedHandle)->CloneUserData = StableSource->CloneUserData;
			(*ClonedHandle)->EqualsCallback = StableSource->EqualsCallback;
			(*ClonedHandle)->EqualsUserData = StableSource->EqualsUserData;
			(*ClonedHandle)->HashCallback = StableSource->HashCallback;
			(*ClonedHandle)->HashUserData = StableSource->HashUserData;
			(*ClonedHandle)->CloneOwner = StableSource->CloneOwner;
			(*ClonedHandle)->EqualsOwner = StableSource->EqualsOwner;
			(*ClonedHandle)->HashOwner = StableSource->HashOwner;
			(*ClonedHandle)->GenericTemplateId = StableSource->GenericTemplateId;
			(*ClonedHandle)->GenericTypeArguments = StableSource->GenericTypeArguments;
			OutCloned = *ClonedHandle;
			return true;
		}

		FObjectHandle CloneStructWithContext(const FScriptObject& Object,
		    FValueCloneContext& Context);

		bool TryCloneValueWithContext(const FValue& Value, FValue& Out, FValueCloneContext& Context)
		{
			FValueTraversalDepthScope DepthScope(Context.Depth);
			if (!DepthScope)
			{
				return false;
			}

			// Cloning may invoke a stamped host callback from a nested struct/array.
			// Such callbacks may reenter the VM and relocate the frame/container that
			// owns Value. A local FValue snapshot keeps the current source value alive.
			const FValue StableValue = Value;
			if (IsClosureReferenceCell(StableValue))
			{
				const FValue* const Visible = UnwrapClosureReferenceCell(&StableValue);
				return Visible && TryCloneValueWithContext(*Visible, Out, Context);
			}
			// Value types: script structs (recursive), fixed native arrays, and host structs
			// with a stamped `@clone`. Class / unstamped host handles keep reference semantics.
			if (const auto* HostHandle = std::get_if<FHostObjectHandle>(&StableValue))
			{
				if (*HostHandle && (*HostHandle)->CloneCallback)
				{
					FHostObjectHandle Cloned;
					if (!TryInvokeStampedHostClone(*HostHandle, Cloned))
					{
						return false;
					}
					Out = std::move(Cloned);
					return true;
				}
				Out = StableValue;
				return true;
			}
			if (const auto* ObjectHandle = std::get_if<FObjectHandle>(&StableValue))
			{
				if (*ObjectHandle && (*ObjectHandle)->Kind == EScriptObjectKind::Struct)
				{
					FObjectHandle Cloned = CloneStructWithContext(**ObjectHandle, Context);
					if (!Cloned)
					{
						return false;
					}
					Out = std::move(Cloned);
					return true;
				}
				Out = StableValue;
				return true;
			}
			if (const auto* NativeArray = std::get_if<FNativeArray>(&StableValue))
			{
				FNativeArray Copy;
				Copy.TypeName() = NativeArray->TypeName();
				Copy.ElementTypeName() = NativeArray->ElementTypeName();
				Copy.Length() = NativeArray->Length();
				Copy.Storage().Elements.reserve(NativeArray->Storage().Elements.size());
				for (const FValue& Element : NativeArray->Storage().Elements)
				{
					FValue ElementCopy;
					if (!TryCloneValueWithContext(Element, ElementCopy, Context))
					{
						return false;
					}
					Copy.Storage().Elements.push_back(std::move(ElementCopy));
				}
				Out = std::move(Copy);
				return true;
			}
			Out = StableValue;
			return true;
		}

		FObjectHandle CloneStructWithContext(const FScriptObject& Object,
		    FValueCloneContext& Context)
		{
			FActiveStructCloneScope ActiveScope(Context, Object);
			if (!ActiveScope)
			{
				// Recursive value containment cannot be represented by script struct
				// semantics. Reject malformed host-created graphs instead of recursing.
				return nullptr;
			}

			// Snapshot every source member before the first host callback. A stamped
			// field clone may reenter the session, mutate the source object, or release
			// its last external owner while this operation is in progress.
			const EScriptObjectKind Kind = Object.Kind;
			const std::string TypeName = Object.GetTypeName();
			const std::uint64_t RuntimeTypeGeneration = Object.RuntimeTypeGeneration;
			const void* const RuntimeTypeModule = Object.RuntimeTypeModule;
			const std::uint16_t RuntimeTypeIndex = Object.RuntimeTypeIndex;
			const std::vector<FValue> Fields = Object.Fields;
			const FHostObjectHandle HostBase = Object.GetHostBase();

			auto Cloned = std::make_shared<FScriptObject>();
			Cloned->Kind = Kind;
			Cloned->SetTypeName(TypeName);
			Cloned->RuntimeTypeGeneration = RuntimeTypeGeneration;
			Cloned->ProgramOwner = Object.ProgramOwner;
			Cloned->DescriptorData = Object.DescriptorData;
			Cloned->RuntimeTypeModule = RuntimeTypeModule;
			Cloned->RuntimeTypeIndex = RuntimeTypeIndex;
			Cloned->Fields.reserve(Fields.size());
			for (const FValue& FieldValue : Fields)
			{
				FValue FieldCopy;
				if (!TryCloneValueWithContext(FieldValue, FieldCopy, Context))
				{
					return nullptr;
				}
				Cloned->Fields.push_back(std::move(FieldCopy));
			}
			if (!HostBase)
			{
				return Cloned;
			}
			// Class HostBase: shared identity (reference semantics).
			if (Kind == EScriptObjectKind::Class)
			{
				Cloned->SetHostBase(HostBase);
				return Cloned;
			}
			// Struct HostBase: must deep-copy via stamped @clone (never share / never drop).
			FHostObjectHandle ClonedHost;
			if (!TryInvokeStampedHostClone(HostBase, ClonedHost))
			{
				return nullptr;
			}
			Cloned->SetHostBase(std::move(ClonedHost));
			Cloned->GetHostBase()->ScriptOwner = Cloned;
			return Cloned;
		}
	}

	bool TryCloneValue(const FValue& Value, FValue& Out)
	{
		FValueCloneContext Context;
		return TryCloneValueWithContext(Value, Out, Context);
	}

	namespace
	{
		enum class EValueCompareNodeKind : std::uint8_t
		{
			Struct,
			Function,
			Variant,
		};

		struct FValueCompareNode
		{
			bool operator==(const FValueCompareNode& Other) const noexcept
			{
				return Left == Other.Left && Right == Other.Right && Kind == Other.Kind;
			}

			const void* Left = nullptr;
			const void* Right = nullptr;
			EValueCompareNodeKind Kind = EValueCompareNodeKind::Struct;
		};

		struct FValueCompareNodeHash
		{
			std::size_t operator()(const FValueCompareNode& Node) const noexcept
			{
				const std::size_t LeftHash = std::hash<const void*>{}(Node.Left);
				const std::size_t RightHash = std::hash<const void*>{}(Node.Right);
				std::size_t Result = LeftHash;
				Result ^= RightHash + static_cast<std::size_t>(0x9e3779b9u) + (Result << 6u) + (Result >> 2u);
				Result ^= static_cast<std::size_t>(Node.Kind) + static_cast<std::size_t>(0x9e3779b9u) + (Result << 6u) + (Result >> 2u);
				return Result;
			}
		};

		struct FValueCompareContext
		{
			std::size_t Depth = 0;
			std::unordered_set<FValueCompareNode, FValueCompareNodeHash> ActiveNodes;
		};

		class FActiveValueCompareScope
		{
		public:
			FActiveValueCompareScope(FValueCompareContext& InContext,
			    const FValueCompareNode InNode)
			    : Context(InContext), Node(InNode), bInserted(Context.ActiveNodes.insert(Node).second)
			{
			}

			~FActiveValueCompareScope()
			{
				if (bInserted)
				{
					Context.ActiveNodes.erase(Node);
				}
			}

			explicit operator bool() const noexcept
			{
				return bInserted;
			}

		private:
			FValueCompareContext& Context;
			FValueCompareNode Node;
			bool bInserted = false;
		};
	}

	static bool ValuesEqualWithContext(const FBytecodeModule& Module,
	    const FValue& Left,
	    const FValue& Right,
	    FValueCompareContext& Context)
	{
		FValueTraversalDepthScope DepthScope(Context.Depth);
		if (!DepthScope)
		{
			return false;
		}
		const FValue* const VisibleLeft = UnwrapClosureReferenceCell(&Left);
		const FValue* const VisibleRight = UnwrapClosureReferenceCell(&Right);
		if (!VisibleLeft || !VisibleRight)
		{
			return false;
		}
		if (VisibleLeft != &Left || VisibleRight != &Right)
		{
			return ValuesEqualWithContext(Module, *VisibleLeft, *VisibleRight, Context);
		}
		const auto IsNullObjectReference = [](const FValue& Value)
		{
			if (const auto* Script = std::get_if<FObjectHandle>(&Value))
			{
				return !*Script;
			}
			if (const auto* Host = std::get_if<FHostObjectHandle>(&Value))
			{
				return !*Host;
			}
			return false;
		};
		if (Left.index() != Right.index())
		{
			// `nullptr` is an empty script-object handle while a failed host
			// Cast<T> is an empty host-object handle. Both are one language null.
			return IsNullObjectReference(Left) && IsNullObjectReference(Right);
		}
		if (const auto* Box = std::get_if<FScriptVariantHandle>(&Left))
		{
			const auto& Other = std::get<FScriptVariantHandle>(Right);
			if (!*Box || !Other) return !*Box && !Other;
			if ((*Box)->CanonicalTypeName != Other->CanonicalTypeName) return false;
			FActiveValueCompareScope Active(Context, {Box->get(), Other.get(), EValueCompareNodeKind::Variant});
			return !Active || ValuesEqualWithContext(Module, (*Box)->Value, Other->Value, Context);
		}

		if (const auto* LeftInt8 = std::get_if<std::int8_t>(&Left))
		{
			return *LeftInt8 == std::get<std::int8_t>(Right);
		}
		if (const auto* LeftUInt8 = std::get_if<std::uint8_t>(&Left))
		{
			return *LeftUInt8 == std::get<std::uint8_t>(Right);
		}
		if (const auto* LeftInt16 = std::get_if<std::int16_t>(&Left))
		{
			return *LeftInt16 == std::get<std::int16_t>(Right);
		}
		if (const auto* LeftUInt16 = std::get_if<std::uint16_t>(&Left))
		{
			return *LeftUInt16 == std::get<std::uint16_t>(Right);
		}
		if (const auto* LeftInt = std::get_if<std::int32_t>(&Left))
		{
			return *LeftInt == std::get<std::int32_t>(Right);
		}
		if (const auto* LeftUInt = std::get_if<std::uint32_t>(&Left))
		{
			return *LeftUInt == std::get<std::uint32_t>(Right);
		}
		if (const auto* LeftInt64 = std::get_if<std::int64_t>(&Left))
		{
			return *LeftInt64 == std::get<std::int64_t>(Right);
		}
		if (const auto* LeftUInt64 = std::get_if<std::uint64_t>(&Left))
		{
			return *LeftUInt64 == std::get<std::uint64_t>(Right);
		}
		if (const auto* LeftDouble = std::get_if<double>(&Left))
		{
			return *LeftDouble == std::get<double>(Right);
		}
		if (const auto* LeftFloat = std::get_if<float>(&Left))
		{
			return *LeftFloat == std::get<float>(Right);
		}
		if (const auto* LeftString = std::get_if<std::string>(&Left))
		{
			return *LeftString == std::get<std::string>(Right);
		}
		if (const auto* LeftWString = std::get_if<std::u16string>(&Left))
		{
			return *LeftWString == std::get<std::u16string>(Right);
		}
		if (const auto* LeftBool = std::get_if<bool>(&Left))
		{
			return *LeftBool == std::get<bool>(Right);
		}
		if (const auto* LeftEnum = std::get_if<FEnumValue>(&Left))
		{
			const FEnumValue& RightEnum = std::get<FEnumValue>(Right);
			return LeftEnum->TypeName == RightEnum.TypeName && LeftEnum->Value == RightEnum.Value;
		}
		if (const auto* LeftObject = std::get_if<FObjectHandle>(&Left))
		{
			const auto* RightObject = std::get_if<FObjectHandle>(&Right);
			if (!RightObject)
			{
				return false;
			}
			if (!*LeftObject || !*RightObject)
			{
				return *LeftObject == *RightObject;
			}
			if ((*LeftObject)->Kind != (*RightObject)->Kind)
			{
				return false;
			}
			if ((*LeftObject)->Kind == EScriptObjectKind::Class)
			{
				return *LeftObject == *RightObject;
			}
			FActiveValueCompareScope ActiveScope(Context,
			    FValueCompareNode{
			        LeftObject->get(), RightObject->get(), EValueCompareNodeKind::Struct});
			if (!ActiveScope)
			{
				// The only valid repeated comparison is the same malformed host-created
				// struct on both sides; distinct cyclic value graphs compare unequal.
				return *LeftObject == *RightObject;
			}
			// A nested host equality callback can reenter and mutate either source.
			// Compare immutable entry snapshots instead of live field vectors.
			const std::string LeftTypeName = (*LeftObject)->GetTypeName();
			const std::string RightTypeName = (*RightObject)->GetTypeName();
			const FHostObjectHandle LeftHostBase = (*LeftObject)->GetHostBase();
			const FHostObjectHandle RightHostBase = (*RightObject)->GetHostBase();
			const std::vector<FValue> LeftFields = (*LeftObject)->Fields;
			const std::vector<FValue> RightFields = (*RightObject)->Fields;
			if (LeftTypeName != RightTypeName)
			{
				return false;
			}
			// HostBase is opaque: compare handle identity (no host value-Equals yet).
			// Presence mismatch or distinct instances => unequal, even if script fields match.
			if (LeftHostBase != RightHostBase)
			{
				return false;
			}
			std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
			for (std::size_t Index = 0; Index < Module.Types.size(); ++Index)
			{
				if (Module.Types[Index].Name == LeftTypeName)
				{
					TypeIndex = static_cast<std::uint16_t>(Index);
					break;
				}
			}
			if (TypeIndex == static_cast<std::uint16_t>(-1))
			{
				return false;
			}
			const FBytecodeTypeDesc& TypeDesc = Module.Types[TypeIndex];
			if (LeftFields.size() != RightFields.size() || LeftFields.size() < TypeDesc.Fields.size())
			{
				return false;
			}
			for (std::size_t FieldIndex = 0; FieldIndex < TypeDesc.Fields.size(); ++FieldIndex)
			{
				if (!ValuesEqualWithContext(
				        Module, LeftFields[FieldIndex], RightFields[FieldIndex], Context))
				{
					return false;
				}
			}
			return true;
		}
		if (const auto* LeftArray = std::get_if<FNativeArray>(&Left))
		{
			const auto* RightArray = std::get_if<FNativeArray>(&Right);
			if (!RightArray || LeftArray->Length() != RightArray->Length() || LeftArray->Storage().Elements.size() != RightArray->Storage().Elements.size())
			{
				return false;
			}
			const FNativeArray StableLeft = *LeftArray;
			const FNativeArray StableRight = *RightArray;
			for (std::size_t Index = 0; Index < StableLeft.Storage().Elements.size(); ++Index)
			{
				if (!ValuesEqualWithContext(Module,
				        StableLeft.Storage().Elements[Index],
				        StableRight.Storage().Elements[Index],
				        Context))
				{
					return false;
				}
			}
			return true;
		}
		if (const auto* LeftDynamic = std::get_if<FDynamicArrayHandle>(&Left))
		{
			const auto* RightDynamic = std::get_if<FDynamicArrayHandle>(&Right);
			if (!RightDynamic)
			{
				return false;
			}
			return *LeftDynamic == *RightDynamic;
		}
		if (const auto* LeftMap = std::get_if<FMapHandle>(&Left))
		{
			const auto* RightMap = std::get_if<FMapHandle>(&Right);
			if (!RightMap)
			{
				return false;
			}
			return *LeftMap == *RightMap;
		}
		if (const auto* LeftSet = std::get_if<FSetHandle>(&Left))
		{
			const auto* RightSet = std::get_if<FSetHandle>(&Right);
			if (!RightSet)
			{
				return false;
			}
			return *LeftSet == *RightSet;
		}
		if (const auto* LeftQueue = std::get_if<FQueueHandle>(&Left))
		{
			const auto* RightQueue = std::get_if<FQueueHandle>(&Right);
			if (!RightQueue)
			{
				return false;
			}
			return *LeftQueue == *RightQueue;
		}
		if (const auto* LeftStack = std::get_if<FStackHandle>(&Left))
		{
			const auto* RightStack = std::get_if<FStackHandle>(&Right);
			if (!RightStack)
			{
				return false;
			}
			return *LeftStack == *RightStack;
		}
		if (const auto* LeftOrderedMap = std::get_if<FOrderedMapHandle>(&Left))
		{
			const auto* RightOrderedMap = std::get_if<FOrderedMapHandle>(&Right);
			if (!RightOrderedMap)
			{
				return false;
			}
			return *LeftOrderedMap == *RightOrderedMap;
		}
		if (const auto* LeftDeque = std::get_if<FDequeHandle>(&Left))
		{
			const auto* RightDeque = std::get_if<FDequeHandle>(&Right);
			if (!RightDeque)
			{
				return false;
			}
			return *LeftDeque == *RightDeque;
		}
		if (const auto* LeftChannel = std::get_if<FChannelHandle>(&Left))
		{
			const auto* RightChannel = std::get_if<FChannelHandle>(&Right);
			if (!RightChannel)
			{
				return false;
			}
			return *LeftChannel == *RightChannel;
		}
		if (const auto* LeftFunction = std::get_if<FFunctionHandle>(&Left))
		{
			const auto* RightFunction = std::get_if<FFunctionHandle>(&Right);
			if (!RightFunction)
			{
				return false;
			}
			if (*LeftFunction == *RightFunction)
			{
				return true;
			}
			if (!*LeftFunction || !*RightFunction)
			{
				return false;
			}
			FActiveValueCompareScope ActiveScope(Context,
			    FValueCompareNode{
			        LeftFunction->get(), RightFunction->get(), EValueCompareNodeKind::Function});
			if (!ActiveScope)
			{
				return false;
			}
			if ((*LeftFunction)->FunctionIndex != (*RightFunction)->FunctionIndex || (*LeftFunction)->ProgramGeneration != (*RightFunction)->ProgramGeneration || (*LeftFunction)->FunctionName != (*RightFunction)->FunctionName || (*LeftFunction)->TypeName != (*RightFunction)->TypeName || (*LeftFunction)->Captures.size() != (*RightFunction)->Captures.size() || (*LeftFunction)->CapturedThis != (*RightFunction)->CapturedThis)
			{
				return false;
			}
			const std::vector<FValue> LeftCaptures = (*LeftFunction)->Captures;
			const std::vector<FValue> RightCaptures = (*RightFunction)->Captures;
			for (std::size_t Index = 0; Index < LeftCaptures.size(); ++Index)
			{
				const FValue& LeftCapture = LeftCaptures[Index];
				const FValue& RightCapture = RightCaptures[Index];
				if (IsReadonlyClosureCell(LeftCapture) || IsReadonlyClosureCell(RightCapture))
				{
					const auto* LeftValue = UnwrapClosureReferenceCell(&LeftCapture);
					const auto* RightValue = UnwrapClosureReferenceCell(&RightCapture);
					if (!LeftValue || !RightValue || !ValuesEqualWithContext(Module, *LeftValue, *RightValue, Context)) return false;
					continue;
				}
				if (IsClosureReferenceCell(LeftCapture) || IsClosureReferenceCell(RightCapture))
				{
					const auto* LeftCell = std::get_if<FDynamicArrayHandle>(&LeftCapture);
					const auto* RightCell = std::get_if<FDynamicArrayHandle>(&RightCapture);
					if (!LeftCell || !RightCell || *LeftCell != *RightCell)
					{
						return false;
					}
					continue;
				}
				if (!ValuesEqualWithContext(Module, LeftCapture, RightCapture, Context))
				{
					return false;
				}
			}
			return true;
		}
		if (const auto* LeftHost = std::get_if<FHostObjectHandle>(&Left))
		{
			const auto* RightHost = std::get_if<FHostObjectHandle>(&Right);
			if (!RightHost)
			{
				return false;
			}
			return FHostObjectValueEqual{}(*LeftHost, *RightHost);
		}
		return false;
	}

	bool ValuesEqual(const FBytecodeModule& Module, const FValue& Left, const FValue& Right)
	{
		FValueCompareContext Context;
		return ValuesEqualWithContext(Module, Left, Right, Context);
	}

	FObjectHandle CloneStruct(const FScriptObject& Object)
	{
		FValueCloneContext Context;
		FValueTraversalDepthScope DepthScope(Context.Depth);
		return DepthScope ? CloneStructWithContext(Object, Context) : nullptr;
	}

	void RebuildModuleIndices(FBytecodeModule& Module)
	{
		Module.FunctionIndices.clear();
		for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
		{
			Module.FunctionIndices.emplace(Module.Functions[Index].Name, static_cast<std::uint16_t>(Index));
		}
		Module.GlobalSlots.clear();
		for (std::size_t Index = 0; Index < Module.GlobalNames.size(); ++Index)
		{
			Module.GlobalSlots.emplace(Module.GlobalNames[Index], static_cast<std::uint16_t>(Index));
		}
	}

	bool Utf8ToUtf16(const std::string_view Text, std::u16string& OutValue, std::string* OutError)
	{
		OutValue.clear();
		OutValue.reserve(Text.size());
		const auto Fail = [&](const char* Message) -> bool
		{
			OutValue.clear();
			if (OutError)
			{
				*OutError = Message;
			}
			return false;
		};
		for (std::size_t Index = 0; Index < Text.size();)
		{
			const auto Byte = static_cast<unsigned char>(Text[Index]);
			if (Byte < 0x80u)
			{
				OutValue.push_back(static_cast<char16_t>(Byte));
				++Index;
				continue;
			}
			if ((Byte & 0xE0u) == 0xC0u && Index + 1 < Text.size())
			{
				const auto B1 = static_cast<unsigned char>(Text[Index + 1]);
				if ((B1 & 0xC0u) == 0x80u)
				{
					const std::uint32_t CodePoint =
					    (static_cast<std::uint32_t>(Byte & 0x1Fu) << 6) | static_cast<std::uint32_t>(B1 & 0x3Fu);
					if (CodePoint < 0x80u)
					{
						return Fail("invalid UTF-8 sequence");
					}
					OutValue.push_back(static_cast<char16_t>(CodePoint));
					Index += 2;
					continue;
				}
			}
			if ((Byte & 0xF0u) == 0xE0u && Index + 2 < Text.size())
			{
				const auto B1 = static_cast<unsigned char>(Text[Index + 1]);
				const auto B2 = static_cast<unsigned char>(Text[Index + 2]);
				if ((B1 & 0xC0u) == 0x80u && (B2 & 0xC0u) == 0x80u)
				{
					const std::uint32_t CodePoint =
					    (static_cast<std::uint32_t>(Byte & 0x0Fu) << 12) | (static_cast<std::uint32_t>(B1 & 0x3Fu) << 6) | static_cast<std::uint32_t>(B2 & 0x3Fu);
					if (CodePoint < 0x800u || (CodePoint >= 0xD800u && CodePoint <= 0xDFFFu))
					{
						return Fail("invalid UTF-8 sequence");
					}
					OutValue.push_back(static_cast<char16_t>(CodePoint));
					Index += 3;
					continue;
				}
			}
			if ((Byte & 0xF8u) == 0xF0u && Index + 3 < Text.size())
			{
				const auto B1 = static_cast<unsigned char>(Text[Index + 1]);
				const auto B2 = static_cast<unsigned char>(Text[Index + 2]);
				const auto B3 = static_cast<unsigned char>(Text[Index + 3]);
				if ((B1 & 0xC0u) == 0x80u && (B2 & 0xC0u) == 0x80u && (B3 & 0xC0u) == 0x80u)
				{
					std::uint32_t CodePoint =
					    (static_cast<std::uint32_t>(Byte & 0x07u) << 18) | (static_cast<std::uint32_t>(B1 & 0x3Fu) << 12) | (static_cast<std::uint32_t>(B2 & 0x3Fu) << 6) | static_cast<std::uint32_t>(B3 & 0x3Fu);
					if (CodePoint < 0x10000u || CodePoint > 0x10FFFFu)
					{
						return Fail("invalid UTF-8 sequence");
					}
					CodePoint -= 0x10000u;
					OutValue.push_back(static_cast<char16_t>(0xD800u + (CodePoint >> 10)));
					OutValue.push_back(static_cast<char16_t>(0xDC00u + (CodePoint & 0x3FFu)));
					Index += 4;
					continue;
				}
			}
			return Fail("invalid UTF-8 sequence");
		}
		return true;
	}

	std::string Utf16ToUtf8(const std::u16string_view Text)
	{
		std::string Result;
		Result.reserve(Text.size());
		for (std::size_t Index = 0; Index < Text.size(); ++Index)
		{
			std::uint32_t CodePoint = static_cast<std::uint32_t>(Text[Index]);
			if (CodePoint >= 0xD800u && CodePoint <= 0xDBFFu && Index + 1 < Text.size())
			{
				const std::uint32_t Low = static_cast<std::uint32_t>(Text[Index + 1]);
				if (Low >= 0xDC00u && Low <= 0xDFFFu)
				{
					CodePoint = 0x10000u + ((CodePoint - 0xD800u) << 10) + (Low - 0xDC00u);
					++Index;
				}
			}
			if (CodePoint < 0x80u)
			{
				Result.push_back(static_cast<char>(CodePoint));
			}
			else if (CodePoint < 0x800u)
			{
				Result.push_back(static_cast<char>(0xC0u | (CodePoint >> 6)));
				Result.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));
			}
			else if (CodePoint < 0x10000u)
			{
				Result.push_back(static_cast<char>(0xE0u | (CodePoint >> 12)));
				Result.push_back(static_cast<char>(0x80u | ((CodePoint >> 6) & 0x3Fu)));
				Result.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));
			}
			else
			{
				Result.push_back(static_cast<char>(0xF0u | (CodePoint >> 18)));
				Result.push_back(static_cast<char>(0x80u | ((CodePoint >> 12) & 0x3Fu)));
				Result.push_back(static_cast<char>(0x80u | ((CodePoint >> 6) & 0x3Fu)));
				Result.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));
			}
		}
		return Result;
	}

	std::string GetExecutableDirectory()
	{
		std::string ExecutablePath;
		#ifdef _WIN32
		std::wstring WidePath(260, L'\0');
		DWORD Length = 0;
		for (;;)
		{
			SetLastError(ERROR_SUCCESS);
			Length = GetModuleFileNameW(
			    nullptr,
			    WidePath.data(),
			    static_cast<DWORD>(WidePath.size()));
			if (Length == 0)
			{
				return {};
			}
			if (static_cast<std::size_t>(Length) < WidePath.size())
			{
				WidePath.resize(static_cast<std::size_t>(Length));
				break;
			}
			if (WidePath.size() >= 32768)
			{
				return {};
			}
			WidePath.resize(std::min<std::size_t>(WidePath.size() * 2, 32768));
		}
		std::u16string Utf16Path;
		Utf16Path.reserve(WidePath.size());
		for (const wchar_t Character : WidePath)
		{
			Utf16Path.push_back(static_cast<char16_t>(Character));
		}
		ExecutablePath = Utf16ToUtf8(Utf16Path);
		if (ExecutablePath.empty() && !WidePath.empty())
		{
			return {};
		}
		#elif defined(__APPLE__)
		char Probe = '\0';
		std::uint32_t RequiredSize = 1;
		if (_NSGetExecutablePath(&Probe, &RequiredSize) == 0 || RequiredSize <= 1)
		{
			return {};
		}
		std::vector<char> Buffer(RequiredSize);
		if (_NSGetExecutablePath(Buffer.data(), &RequiredSize) != 0)
		{
			return {};
		}
		ExecutablePath.assign(Buffer.data());
		#elif defined(__linux__)
		char Buffer[4096];
		const ssize_t Length = readlink("/proc/self/exe", Buffer, sizeof(Buffer) - 1);
		if (Length <= 0)
		{
			return {};
		}
		ExecutablePath.assign(Buffer, static_cast<std::size_t>(Length));
		#else
		return {};
		#endif

		#ifdef _WIN32
		for (char& Character : ExecutablePath)
		{
			if (Character == '\\')
			{
				Character = '/';
			}
		}
		#endif
		const std::size_t SlashIndex = ExecutablePath.find_last_of('/');
		if (SlashIndex == std::string::npos)
		{
			return {};
		}
		return ExecutablePath.substr(0, SlashIndex == 0 ? 1 : SlashIndex);
	}

	std::string GetUserCacheDirectory()
	{
		// The app supplies its sandbox/cache directory through embedding config.
		if (GetHostPlatformInfo().bMobile) return {};
		const auto GetEnvironmentVariable = [](const char* const Name) -> std::string
		{
			const auto Value = Environment::GetUtf8(Name);
			return Value ? *Value : std::string{};
		};
		const auto AppendRoot = [](std::string Root, const std::string_view Suffix)
		{
			while (Root.size() > 1 && (Root.back() == '/'
			#ifdef _WIN32
			                              || Root.back() == '\\'
			#endif
			                              ))
			{
				Root.pop_back();
			}
			if (Root.empty())
			{
				return std::string{};
			}
			#ifdef _WIN32
			for (char& Character : Root)
			{
				if (Character == '\\')
				{
					Character = '/';
				}
			}
			#endif
			if (Root.back() != '/')
			{
				Root.push_back('/');
			}
			Root.append(Suffix);
			return Root;
		};

		#ifdef _WIN32
		return AppendRoot(GetEnvironmentVariable("LOCALAPPDATA"), "VeroScript/cache");
		#elif defined(__APPLE__)
		return AppendRoot(GetEnvironmentVariable("HOME"), "Library/Caches/VeroScript/cache");
		#else
		const std::string XdgCacheHome = GetEnvironmentVariable("XDG_CACHE_HOME");
		// XDG Base Directory requires an absolute path; ignore malformed relative values.
		if (!XdgCacheHome.empty() && XdgCacheHome.front() == '/')
		{
			return AppendRoot(XdgCacheHome, "VeroScript/cache");
		}
		return AppendRoot(GetEnvironmentVariable("HOME"), ".cache/VeroScript/cache");
		#endif
	}
}

std::size_t VeroScript::FHostObjectKeyHash::operator()(
    const FHostObjectHandle& Handle) const noexcept
{
	if (!Handle)
	{
		return 0;
	}
	if (Handle->HashCallback && Handle->EqualsCallback)
	{
		try
		{
			const FHostValueCallback Callback = Handle->HashCallback;
			const FValue Arg = Handle;
			FNativeCallContext Context;
			Context.Args = &Arg;
			Context.ArgCount = 1;
			Context.UserData = Handle->HashUserData;
			Context.HostThis = Handle->Payload;
			FValue Result;
			std::string Error;
			if (Callback(Context, Result, Error))
			{
				if (const auto* Value = std::get_if<std::uint64_t>(&Result))
				{
					return static_cast<std::size_t>(*Value);
				}
				if (const auto* Value = std::get_if<std::int64_t>(&Result))
				{
					return static_cast<std::size_t>(*Value);
				}
				if (const auto* Value = std::get_if<std::uint32_t>(&Result))
				{
					return static_cast<std::size_t>(*Value);
				}
				if (const auto* Value = std::get_if<std::int32_t>(&Result))
				{
					return static_cast<std::size_t>(*Value);
				}
			}
		}
		catch (...)
		{
		}
		// A registered value hash that fails must not fall back to identity: doing
		// so would violate equal-values-have-equal-hashes. Zero is slow but correct.
		return 0;
	}
	return std::hash<const FHostObject*>{}(Handle.get());
}

bool VeroScript::FHostObjectKeyEqual::operator()(
    const FHostObjectHandle& Left,
    const FHostObjectHandle& Right) const noexcept
{
	if (Left == Right)
	{
		return true;
	}
	if (!Left || !Right || Left->TypeId != Right->TypeId || Left->TypeName != Right->TypeName || !Left->EqualsCallback || !Left->HashCallback || !Right->EqualsCallback || !Right->HashCallback || Left->EqualsCallback != Right->EqualsCallback || Left->EqualsUserData != Right->EqualsUserData || Left->HashCallback != Right->HashCallback || Left->HashUserData != Right->HashUserData)
	{
		return false;
	}
	try
	{
		const FHostValueCallback Callback = Left->EqualsCallback;
		const FValue Args[] = {Left, Right};
		FNativeCallContext Context;
		Context.Args = Args;
		Context.ArgCount = 2;
		Context.UserData = Left->EqualsUserData;
		Context.HostThis = Left->Payload;
		FValue Result;
		std::string Error;
		return Callback(Context, Result, Error) && std::holds_alternative<bool>(Result) && std::get<bool>(Result);
	}
	catch (...)
	{
		return false;
	}
}

bool VeroScript::FHostObjectValueEqual::operator()(
    const FHostObjectHandle& Left, const FHostObjectHandle& Right) const noexcept
{
	if (Left == Right) return true;
	if (!Left || !Right || Left->TypeId != Right->TypeId || Left->TypeName != Right->TypeName ||
	    Left->GenericTemplateId != Right->GenericTemplateId ||
	    Left->GenericTypeArguments != Right->GenericTypeArguments || !Left->EqualsCallback ||
	    Left->EqualsCallback != Right->EqualsCallback || Left->EqualsUserData != Right->EqualsUserData)
	{
		return false;
	}

	try
	{
		// The values retain protocol/module owners through the entire callback.
		const FValue Args[] = {Left, Right};
		const auto Callback = Left->EqualsCallback;
		FNativeCallContext C;
		C.Args = Args;
		C.ArgCount = 2;
		C.UserData = Left->EqualsUserData;
		C.HostThis = Left->Payload;
		FValue Out;
		std::string Error;
		return Callback(C, Out, Error) && std::holds_alternative<bool>(Out) && std::get<bool>(Out);
	}
	catch (...)
	{
		return false;
	}
}
