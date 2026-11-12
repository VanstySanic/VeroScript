// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJson.h"
#include "JsonInternal.h"
#include "VeroScriptFloatParsing.h"
#include <cmath>

namespace VeroScript::Json
{
	using namespace Detail;

	FError::operator bool() const noexcept
	{
		return Code != EError::None;
	}

	const char* FError::Id() const noexcept
	{
		static constexpr const char* Ids[] = {"",
		    "json.syntax",
		    "json.utf8",
		    "json.duplicate_key",
		    "json.type",
		    "json.range",
		    "json.missing",
		    "json.pointer",
		    "json.patch",
		    "json.limit",
		    "json.cancelled",
		    "json.allocation",
		    "json.state",
		    "json.sink",
		    "json.unsupported",
		    "json.callback",
		    "json.schema",
		    "json.resource",
		    "json.cycle"};
		const auto Index = static_cast<unsigned>(Code);
		return Index < sizeof(Ids) / sizeof(*Ids) ? Ids[Index] : "json.unknown";
	}

	FJsonValue::FJsonValue() noexcept = default;

	FJsonValue::FJsonValue(bool Value)
	{
		FNode N;
		N.Kind = EKind::Boolean;
		N.Boolean = Value;
		*this = FAccess::Make(std::move(N));
	}

	FJsonValue::FJsonValue(std::int64_t Value)
	{
		FNode N;
		N.Kind = EKind::Number;
		N.Text = std::to_string(Value);
		N.Decimal = Decimal(N.Text, {});
		*this = FAccess::Make(std::move(N));
	}

	FJsonValue::FJsonValue(std::uint64_t Value)
	{
		FNode N;
		N.Kind = EKind::Number;
		N.Text = std::to_string(Value);
		N.Decimal = Decimal(N.Text, {});
		*this = FAccess::Make(std::move(N));
	}

	FJsonValue FJsonValue::Array()
	{
		FNode N;
		N.Kind = EKind::Array;
		return FAccess::Make(std::move(N));
	}

	FJsonValue FJsonValue::Object()
	{
		FNode N;
		N.Kind = EKind::Object;
		return FAccess::Make(std::move(N));
	}

	bool FJsonValue::TryString(std::string_view Text, FJsonValue& Out, FError& Error)
	{
		return Guard(Error,
		    [&]
		{
			ValidateUtf8(Text);
			FNode N;
			N.Kind = EKind::String;
			N.Text = Text;
			Out = FAccess::Make(std::move(N));
		});
	}

	bool FJsonValue::TryNumber(std::string_view Text, FJsonValue& Out, FError& Error, const FLimits& Limits)
	{
		return Guard(Error,
		    [&]
		{
			FNode N;
			N.Kind = EKind::Number;
			N.Decimal = Decimal(Text, Limits);
			N.Text = Text;
			Out = FAccess::Make(std::move(N));
		});
	}

	bool FJsonValue::TryDouble(double Value, FJsonValue& Out, FError& Error)
	{
		return Guard(Error,
		    [&]
		{
			FNode N;
			N.Kind = EKind::Number;
			N.Text = DoubleText(Value);
			N.Decimal = Decimal(N.Text, {});
			Out = FAccess::Make(std::move(N));
		});
	}

	EKind FJsonValue::Kind() const noexcept
	{
		return FAccess::Get(*this).Kind;
	}

	bool FJsonValue::TryBoolean(bool& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Boolean)
			{
				Fail(EError::Type, "Expected boolean");
			}
			Out = FAccess::Get(*this).Boolean;
		});
	}

	bool FJsonValue::TryString(std::string& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::String)
			{
				Fail(EError::Type, "Expected string");
			}
			Out = FAccess::Get(*this).Text;
		});
	}

	bool FJsonValue::TryNumberText(std::string& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Number)
			{
				Fail(EError::Type, "Expected number");
			}
			Out = FAccess::Get(*this).Text;
		});
	}

	static std::uint64_t UnsignedInteger(const FDecimal& Number, std::uint64_t Maximum)
	{
		if (Number.Exponent < 0)
		{
			Fail(EError::Range, "Number is not an integer");
		}
		if (Number.Digits.size() > 20 || Number.Exponent > 20 ||
		    Number.Digits.size() + static_cast<std::uint64_t>(Number.Exponent) > 20)
		{
			Fail(EError::Range, "Integer is outside target range");
		}
		std::uint64_t Out = 0;
		for (std::size_t I = 0, Count = Number.Digits.size() + static_cast<std::size_t>(Number.Exponent); I < Count;
		    ++I)
		{
			unsigned Digit = I < Number.Digits.size() ? static_cast<unsigned>(Number.Digits[I] - '0') : 0;
			if (Out > Maximum / 10 || (Out == Maximum / 10 && Digit > Maximum % 10))
			{
				Fail(EError::Range, "Integer is outside target range");
			}
			Out = Out * 10 + Digit;
		}
		return Out;
	}

	bool FJsonValue::TryInt64(std::int64_t& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Number)
			{
				Fail(EError::Type, "Expected number");
			}
			const auto& N = FAccess::Get(*this).Decimal;
			auto Magnitude = UnsignedInteger(N, static_cast<std::uint64_t>(INT64_MAX) + N.bNegative);
			Out = N.bNegative
			    ? (Magnitude == (std::uint64_t{1} << 63) ? INT64_MIN : -static_cast<std::int64_t>(Magnitude))
			    : static_cast<std::int64_t>(Magnitude);
		});
	}

	bool FJsonValue::TryUInt64(std::uint64_t& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Number)
			{
				Fail(EError::Type, "Expected number");
			}
			const auto& N = FAccess::Get(*this).Decimal;
			if (N.bNegative)
			{
				Fail(EError::Range, "Negative unsigned integer");
			}
			Out = UnsignedInteger(N, UINT64_MAX);
		});
	}

	bool FJsonValue::TryDouble(double& Out, FError& Error, bool bExact) const
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Number)
			{
				Fail(EError::Type, "Expected number");
			}
			const auto& N = FAccess::Get(*this);
			double Result = 0;
			if (N.Decimal.Digits == "0")
			{
				Out = N.Text.front() == '-' ? -0.0 : 0.0;
				return;
			}
			auto Converted = FloatParsingDetail::FromCharsDecimal(N.Text, Result);
			if (Converted.ec != std::errc{} || Converted.ptr != N.Text.data() + N.Text.size() ||
			    !std::isfinite(Result) || (Result == 0 && N.Decimal.Digits != "0"))
			{
				Fail(EError::Range, "Number cannot be represented as binary64");
			}
			if (bExact && Compare(N.Decimal, Decimal(ExactDoubleText(Result), {})) != 0)
			{
				Fail(EError::Range, "Conversion to binary64 is inexact");
			}
			Out = Result;
		});
	}

	bool FJsonValue::TrySize(std::int64_t& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Array && Kind() != EKind::Object)
			{
				Fail(EError::Type, "Expected array or object");
			}
			const auto& N = FAccess::Get(*this);
			auto Size = Kind() == EKind::Array ? N.Array.size() : N.Object.size();
			if (Size > static_cast<std::uint64_t>(INT64_MAX))
			{
				Fail(EError::Range, "Container exceeds int64 range");
			}
			Out = static_cast<std::int64_t>(Size);
		});
	}

	bool FJsonValue::TryAt(std::int64_t Index, FJsonValue& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			const auto& N = FAccess::Get(*this);
			if (N.Kind != EKind::Array)
			{
				Fail(EError::Type, "Expected array");
			}
			if (Index < 0 || static_cast<std::uint64_t>(Index) >= N.Array.size())
			{
				Fail(EError::Range, "Array index outside range");
			}
			Out = N.Array[static_cast<std::size_t>(Index)];
		});
	}

	bool FJsonValue::TryGet(std::string_view Key, FJsonValue& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			const auto& N = FAccess::Get(*this);
			if (N.Kind != EKind::Object)
			{
				Fail(EError::Type, "Expected object");
			}
			auto I = FindMember(N, Key);
			if (I == N.Object.size())
			{
				Fail(EError::Missing, "Object member does not exist");
			}
			Out = N.Object[I].second;
		});
	}

	bool FJsonValue::TryContains(std::string_view Key, bool& Out, FError& Error) const
	{
		return Guard(Error, [&]
		{
			const auto& N = FAccess::Get(*this);
			if (N.Kind != EKind::Object)
			{
				Fail(EError::Type, "Expected object");
			}
			Out = FindMember(N, Key) != N.Object.size();
		});
	}

	bool FJsonValue::TryKeys(std::vector<std::string>& Out, FError& Error) const
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Object)
			{
				Fail(EError::Type, "Expected object");
			}
			std::vector<std::string> Keys;
			for (const auto& Pair : FAccess::Get(*this).Object)
			{
				Keys.push_back(Pair.first);
			}
			Out = std::move(Keys);
		});
	}

	bool FJsonValue::TrySet(std::string_view Key, const FJsonValue& Value, FError& Error)
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Object)
			{
				Fail(EError::Type, "Expected object");
			}
			ValidateUtf8(Key);
			auto N = FAccess::Get(*this);
			auto I = FindMember(N, Key);
			if (I == N.Object.size())
			{
				N.Object.emplace_back(Key, Value);
				N.ObjectIndex.reset();
			}
			else
			{
				N.Object[I].second = Value;
			}
			*this = FAccess::Make(std::move(N));
		});
	}

	bool FJsonValue::TryErase(std::string_view Key, FError& Error)
	{
		return Guard(Error,
		    [&]
		{
			if (Kind() != EKind::Object)
			{
				Fail(EError::Type, "Expected object");
			}
			auto N = FAccess::Get(*this);
			auto I = FindMember(N, Key);
			if (I == N.Object.size())
			{
				Fail(EError::Missing, "Object member does not exist");
			}
			N.Object.erase(N.Object.begin() + I);
			N.ObjectIndex.reset();
			*this = FAccess::Make(std::move(N));
		});
	}

	static void ModifyArray(FJsonValue& Self, std::int64_t Index, const FJsonValue* Value, bool bInsert)
	{
		if (Self.Kind() != EKind::Array)
		{
			Fail(EError::Type, "Expected array");
		}
		auto N = FAccess::Get(Self);
		if (Index < 0 || static_cast<std::uint64_t>(Index) > N.Array.size() ||
		    (!bInsert && static_cast<std::uint64_t>(Index) == N.Array.size()))
		{
			Fail(EError::Range, "Array index outside range");
		}
		auto It = N.Array.begin() + static_cast<std::size_t>(Index);
		if (bInsert)
		{
			N.Array.insert(It, *Value);
		}
		else if (Value)
		{
			*It = *Value;
		}
		else
		{
			N.Array.erase(It);
		}
		Self = FAccess::Make(std::move(N));
	}

	bool FJsonValue::TryInsert(std::int64_t Index, const FJsonValue& Value, FError& Error)
	{
		return Guard(Error,
		    [&]
		{
			ModifyArray(*this, Index, &Value, true);
		});
	}

	bool FJsonValue::TryReplace(std::int64_t Index, const FJsonValue& Value, FError& Error)
	{
		return Guard(Error,
		    [&]
		{
			ModifyArray(*this, Index, &Value, false);
		});
	}

	bool FJsonValue::TryErase(std::int64_t Index, FError& Error)
	{
		return Guard(Error,
		    [&]
		{
			ModifyArray(*this, Index, nullptr, false);
		});
	}

	bool FJsonValue::TryPush(const FJsonValue& Value, FError& Error)
	{
		std::int64_t Size;
		if (!TrySize(Size, Error))
		{
			return false;
		}
		return TryInsert(Size, Value, Error);
	}

	bool FJsonValue::TryEquals(
	    const FJsonValue& Other, bool& Out, FError& Error, const FLimits& Limits, FWork Work) const
	{
		return Guard(Error,
		    [&]
		{
			FBudget Budget(Limits, Work);
			bool Result = Equal(*this, Other, Budget);
			Out = Result;
		});
	}
} // namespace VeroScript::Json
