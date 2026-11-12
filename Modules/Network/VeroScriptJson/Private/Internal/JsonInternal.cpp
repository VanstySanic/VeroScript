// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonInternal.h"
#include "VeroScriptFloatFormatting.h"
#include <cmath>
#include <cstring>

namespace VeroScript::Json::Detail
{
	[[noreturn]] void Fail(EError Code, const char* Message)
	{
		throw FFailure{Code, Message};
	}

	void SetError(FError& Error, EError Code, const char* Message) noexcept
	{
		Error.Code = Code;
		try
		{
			Error.MessageId = Error.Id();
			Error.Message = Message;
		}
		catch (...)
		{
			Error.MessageId.clear();
			Error.Message.clear();
		}
	}

	void CopyError(FError& Out, const FError& Error) noexcept
	{
		if (&Out == &Error)
		{
			return;
		}
		Out.Code = Error.Code;
		Out.bHasSource = Error.bHasSource;
		Out.ByteOffset = Error.ByteOffset;
		Out.Line = Error.Line;
		Out.Column = Error.Column;
		Out.PatchOperation = Error.PatchOperation;
		try
		{
			Out.MessageId = Error.MessageId;
			Out.Message = Error.Message;
			Out.InstancePointer = Error.InstancePointer;
		}
		catch (...)
		{
			Out.MessageId.clear();
			Out.Message.clear();
			Out.InstancePointer.clear();
		}
	}

	bool CheckWork(FWork Work, std::uint64_t Units)
	{
		try
		{
			return !Work.Check || Work.Check(Work.UserData, Units);
		}
		catch (const std::bad_alloc&)
		{
			throw;
		}
		catch (...)
		{
			Fail(EError::Callback, "Work callback threw an exception");
		}
	}

	FBudget::FBudget(const FLimits& InLimits, FWork InWork) : Limits(InLimits), Work(InWork)
	{
	}

	void FBudget::Check(std::uint64_t Units)
	{
		if (Units > Limits.WorkUnits - std::min(Used, Limits.WorkUnits))
		{
			Fail(EError::Limit, "Work limit exceeded");
		}
		Used += Units;
		if (!CheckWork(Work, Units))
		{
			Fail(EError::Cancelled, "Operation cancelled");
		}
	}

	void ValidateUtf8(std::string_view Text)
	{
		for (std::size_t I = 0; I < Text.size();)
		{
			auto A = static_cast<unsigned char>(Text[I++]);
			if (A < 0x80)
			{
				continue;
			}
			unsigned Count = A >= 0xc2 && A <= 0xdf ? 1 : A >= 0xe0 && A <= 0xef ? 2 : A >= 0xf0 && A <= 0xf4 ? 3 : 0;
			if (!Count || Count > Text.size() - I)
			{
				Fail(EError::InvalidUtf8, "Invalid UTF-8 sequence");
			}
			std::uint32_t Scalar = A & (0x7f >> Count);
			for (unsigned J = 0; J < Count; ++J)
			{
				auto B = static_cast<unsigned char>(Text[I++]);
				if ((B & 0xc0) != 0x80)
				{
					Fail(EError::InvalidUtf8, "Invalid UTF-8 continuation");
				}
				Scalar = (Scalar << 6) | (B & 0x3f);
			}
			if (Scalar < (Count == 1	  ? 0x80u
							 : Count == 2 ? 0x800u
										  : 0x10000u) ||
				Scalar > 0x10ffff || (Scalar >= 0xd800 && Scalar <= 0xdfff))
			{
				Fail(EError::InvalidUtf8, "Invalid Unicode scalar");
			}
		}
	}

	FDecimal Decimal(std::string_view Text, const FLimits& Limits)
	{
		if (Text.empty())
		{
			Fail(EError::Syntax, "Empty number token");
		}
		if (Text.size() > Limits.NumberBytes)
		{
			Fail(EError::Limit, "Number token is empty or exceeds its limit");
		}
		FDecimal Out;
		std::size_t I = 0;
		Out.bNegative = Text[I] == '-';
		I += Out.bNegative;
		auto Digit = [&]()
		{
			return I < Text.size() && Text[I] >= '0' && Text[I] <= '9';
		};
		if (!Digit())
		{
			Fail(EError::Syntax, "Expected integer part");
		}
		if (Text[I] == '0')
		{
			Out.Digits += Text[I++];
			if (Digit())
			{
				Fail(EError::Syntax, "Leading zero in number");
			}
		}
		else
		{
			while (Digit())
			{
				Out.Digits += Text[I++];
			}
		}
		if (I < Text.size() && Text[I] == '.')
		{
			++I;
			if (!Digit())
			{
				Fail(EError::Syntax, "Expected fraction digits");
			}
			while (Digit())
			{
				Out.Digits += Text[I++];
				--Out.Exponent;
			}
		}
		if (I < Text.size() && (Text[I] == 'e' || Text[I] == 'E'))
		{
			++I;
			bool bNegative = I < Text.size() && Text[I] == '-';
			if (I < Text.size() && (Text[I] == '+' || Text[I] == '-'))
			{
				++I;
			}
			if (!Digit())
			{
				Fail(EError::Syntax, "Expected exponent digits");
			}
			std::uint64_t Exponent = 0;
			while (Digit())
			{
				unsigned D = static_cast<unsigned>(Text[I++] - '0');
				if (Exponent > Limits.Exponent / 10 || (Exponent == Limits.Exponent / 10 && D > Limits.Exponent % 10) ||
					Exponent > static_cast<std::uint64_t>(INT64_MAX) / 10)
				{
					Fail(EError::Limit, "Number exponent exceeds its limit");
				}
				Exponent = Exponent * 10 + D;
			}
			if (Exponent > static_cast<std::uint64_t>(INT64_MAX) - Out.Digits.size())
			{
				Fail(EError::Limit, "Number exponent exceeds addressable range");
			}
			Out.Exponent += bNegative ? -static_cast<std::int64_t>(Exponent) : static_cast<std::int64_t>(Exponent);
		}
		if (I != Text.size())
		{
			Fail(EError::Syntax, "Invalid number suffix");
		}
		auto First = Out.Digits.find_first_not_of('0');
		if (First == std::string::npos)
		{
			Out = {};
			Out.Digits = "0";
			return Out;
		}
		Out.Digits.erase(0, First);
		while (Out.Digits.size() > 1 && Out.Digits.back() == '0')
		{
			Out.Digits.pop_back();
			++Out.Exponent;
		}
		return Out;
	}

	int Compare(const FDecimal& A, const FDecimal& B)
	{
		if (A.bNegative != B.bNegative)
		{
			return A.bNegative ? -1 : 1;
		}
		int Sign = A.bNegative ? -1 : 1;
		if (A.Digits == "0")
		{
			return B.Digits == "0" ? 0 : -Sign;
		}
		if (B.Digits == "0")
		{
			return Sign;
		}
		auto AOrder = A.Exponent + static_cast<std::int64_t>(A.Digits.size());
		auto BOrder = B.Exponent + static_cast<std::int64_t>(B.Digits.size());
		if (AOrder != BOrder)
		{
			return (AOrder < BOrder ? -1 : 1) * Sign;
		}
		for (std::size_t I = 0, End = std::max(A.Digits.size(), B.Digits.size()); I < End; ++I)
		{
			char ADigit = I < A.Digits.size() ? A.Digits[I] : '0';
			char BDigit = I < B.Digits.size() ? B.Digits[I] : '0';
			if (ADigit != BDigit)
			{
				return (ADigit < BDigit ? -1 : 1) * Sign;
			}
		}
		return 0;
	}

	std::string DoubleText(double Value)
	{
		if (!std::isfinite(Value))
		{
			Fail(EError::Range, "Non-finite number");
		}
		char Buffer[64];
		auto Result = FloatFormattingDetail::ToCharsDecimal(Buffer, Buffer + sizeof(Buffer), Value);
		if (Result.ec != std::errc{})
		{
			Fail(EError::Range, "Cannot encode number");
		}
		return {Buffer, Result.ptr};
	}

	std::string ExactDoubleText(double Value)
	{
		std::uint64_t Bits;
		std::memcpy(&Bits, &Value, sizeof(Bits));
		unsigned ExponentBits = static_cast<unsigned>((Bits >> 52) & 2047);
		std::uint64_t Mantissa = Bits & ((std::uint64_t{1} << 52) - 1);
		int Exponent = ExponentBits ? static_cast<int>(ExponentBits) - 1023 - 52 : -1074;
		if (ExponentBits)
		{
			Mantissa |= std::uint64_t{1} << 52;
		}
		std::string Digits = std::to_string(Mantissa);
		unsigned Factor = Exponent >= 0 ? 2 : 5;
		for (int I = 0; I < std::abs(Exponent); ++I)
		{
			unsigned Carry = 0;
			for (auto J = Digits.size(); J-- > 0;)
			{
				unsigned V = static_cast<unsigned>(Digits[J] - '0') * Factor + Carry;
				Digits[J] = static_cast<char>('0' + V % 10);
				Carry = V / 10;
			}
			if (Carry)
			{
				Digits.insert(Digits.begin(), static_cast<char>('0' + Carry));
			}
		}
		return ((Bits >> 63) ? "-" : "") + Digits + (Exponent < 0 ? "e" + std::to_string(Exponent) : "");
	}

	const FNode& FAccess::Get(const FJsonValue& Value)
	{
		static const FNode Null;
		return Value.Node ? *Value.Node : Null;
	}

	std::uint64_t TreeDepth(const FNode& Node)
	{
		std::uint64_t Depth = 1;
		for (const auto& Child : Node.Array)
		{
			Depth = std::max(Depth, FAccess::Get(Child).Depth + 1);
		}
		for (const auto& Child : Node.Object)
		{
			Depth = std::max(Depth, FAccess::Get(Child.second).Depth + 1);
		}
		if (Depth > 256)
		{
			Fail(EError::Limit, "DOM depth exceeds 256");
		}
		return Depth;
	}

	FJsonValue FAccess::Make(FNode Node)
	{
		Node.Depth = TreeDepth(Node);
		if (Node.Kind == EKind::Object && !Node.ObjectIndex)
		{
			auto Index = std::make_shared<std::unordered_map<std::string, std::size_t>>();
			Index->reserve(Node.Object.size());
			for (std::size_t I = 0; I < Node.Object.size(); ++I)
			{
				Index->emplace(Node.Object[I].first, I);
			}
			Node.ObjectIndex = std::move(Index);
		}
		FJsonValue Out;
		Out.Node = std::make_shared<const FNode>(std::move(Node));
		return Out;
	}

	std::size_t FindMember(const FNode& Node, std::string_view Key)
	{
		if (Node.ObjectIndex)
		{
			auto It = Node.ObjectIndex->find(std::string(Key));
			return It == Node.ObjectIndex->end() ? Node.Object.size() : It->second;
		}
		for (std::size_t I = 0; I < Node.Object.size(); ++I)
		{
			if (Node.Object[I].first == Key)
			{
				return I;
			}
		}
		return Node.Object.size();
	}

	bool Equal(const FJsonValue& A, const FJsonValue& B, FBudget& Budget)
	{
		Budget.Check();
		const auto& AN = FAccess::Get(A);
		const auto& BN = FAccess::Get(B);
		if (AN.Kind != BN.Kind)
		{
			return false;
		}
		switch (AN.Kind)
		{
		case EKind::Null:
			{
				return true;
			}
		case EKind::Boolean:
			{
				return AN.Boolean == BN.Boolean;
			}
		case EKind::Number:
			{
				Budget.Check(AN.Text.size() + BN.Text.size());
				return Compare(AN.Decimal, BN.Decimal) == 0;
			}
		case EKind::String:
			{
				Budget.Check(AN.Text.size() + BN.Text.size());
				return AN.Text == BN.Text;
			}
		case EKind::Array:
			{
				if (AN.Array.size() != BN.Array.size())
				{
					return false;
				}
				for (std::size_t I = 0; I < AN.Array.size(); ++I)
				{
					if (!Equal(AN.Array[I], BN.Array[I], Budget))
					{
						return false;
					}
				}
				return true;
			}
		case EKind::Object:
			{
				if (AN.Object.size() != BN.Object.size())
				{
					return false;
				}
				for (const auto& Member : AN.Object)
				{
					Budget.Check(1 + Member.first.size());
					auto Index = FindMember(BN, Member.first);
					if (Index == BN.Object.size() || !Equal(Member.second, BN.Object[Index].second, Budget))
					{
						return false;
					}
				}
				return true;
			}
		default:
			{
				break;
			}
		}
		return false;
	}
} // namespace VeroScript::Json::Detail
