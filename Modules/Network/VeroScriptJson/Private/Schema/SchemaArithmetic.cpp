// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "SchemaArithmetic.h"

namespace VeroScript::Json::Detail
{
	bool IsInteger(const FDecimal& Value) noexcept
	{
		return Value.Exponent >= 0;
	}

	static void Divide(std::string& Digits, unsigned Divisor, FBudget& Budget)
	{
		Budget.Check(Digits.size());
		unsigned Remainder = 0;
		for (char& Digit : Digits)
		{
			const unsigned Current = Remainder * 10 + static_cast<unsigned>(Digit - '0');
			Digit = static_cast<char>('0' + Current / Divisor);
			Remainder = Current % Divisor;
		}
		if (Digits.front() == '0' && Digits.size() > 1)
		{
			Digits.erase(0, 1);
		}
	}

	static bool AtLeast(const std::string& A, const std::string& B)
	{
		return A.size() != B.size() ? A.size() > B.size() : A >= B;
	}

	static void Subtract(std::string& A, const std::string& B, FBudget& Budget)
	{
		Budget.Check(A.size());
		unsigned Borrow = 0;
		std::size_t J = B.size();
		for (auto I = A.size(); I-- > 0;)
		{
			const unsigned Subtrahend = Borrow + (J ? static_cast<unsigned>(B[--J] - '0') : 0);
			const unsigned Digit = static_cast<unsigned>(A[I] - '0');
			Borrow = Digit < Subtrahend;
			A[I] = static_cast<char>('0' + Digit + Borrow * 10 - Subtrahend);
		}
		const auto First = A.find_first_not_of('0');
		if (First == std::string::npos)
		{
			A = "0";
		}
		else if (First)
		{
			A.erase(0, First);
		}
	}

	bool IsMultiple(const FDecimal& Value, const FDecimal& Divisor, FBudget& Budget)
	{
		Budget.Check();
		if (Divisor.bNegative || Divisor.Digits == "0")
		{
			Fail(EError::Range, "multipleOf must be positive");
		}
		if (Value.Digits == "0")
		{
			return true;
		}
		if (Value.Exponent < Divisor.Exponent)
		{
			// Normalized coefficients contain no trailing zero. A negative exponent
			// difference therefore leaves an uncancellable factor of ten below the line.
			return false;
		}
		const auto Power = static_cast<std::uint64_t>(Value.Exponent) - static_cast<std::uint64_t>(Divisor.Exponent);
		std::string Denominator = Divisor.Digits;
		for (std::uint64_t I = 0; I < Power && (Denominator.back() - '0') % 2 == 0; ++I)
		{
			Divide(Denominator, 2, Budget);
		}
		for (std::uint64_t I = 0; I < Power && Denominator.back() == '5'; ++I)
		{
			Divide(Denominator, 5, Budget);
		}
		if (Denominator == "1")
		{
			return true;
		}
		if (!AtLeast(Value.Digits, Denominator))
		{
			return false;
		}
		std::string Remainder;
		Remainder.reserve(Denominator.size() + 1);
		for (const char Digit : Value.Digits)
		{
			Budget.Check(Denominator.size());
			if (Remainder == "0")
			{
				Remainder.clear();
			}
			Remainder += Digit;
			while (AtLeast(Remainder, Denominator))
			{
				Subtract(Remainder, Denominator, Budget);
			}
		}
		return Remainder == "0";
	}
} // namespace VeroScript::Json::Detail
