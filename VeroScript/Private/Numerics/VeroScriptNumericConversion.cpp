// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericsInternal.h"

namespace VeroScript::Numerics::Detail
{
	namespace
	{
		std::uint64_t Magnitude64(const FInteger& V)
		{
			if (V.Words.size() > 2)
			{
				Fail(ENumericStatus::OutOfRange, "integer range");
			}
			std::uint64_t R = V.Words.empty() ? 0 : V.Words[0];
			if (V.Words.size() == 2)
			{
				R |= static_cast<std::uint64_t>(V.Words[1]) << 32;
			}
			return R;
		}
	} // namespace
	void FromDouble(FArea& A, double Value, FInteger& N, FInteger& D)
	{
		static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559, "binary64 required");
		std::uint64_t Raw;
		std::memcpy(&Raw, &Value, sizeof(Raw));
		auto Exp = static_cast<unsigned>((Raw >> 52) & 2047);
		if (Exp == 2047)
		{
			Fail(ENumericStatus::InvalidArgument, "nonfinite floating input");
		}
		auto Fraction = Raw & ((std::uint64_t(1) << 52) - 1);
		if (Exp)
		{
			Fraction |= std::uint64_t(1) << 52;
		}
		N = Small(A, Fraction, (Raw >> 63) != 0);
		D = Small(A, 1);
		if (!Fraction)
		{
			return;
		}
		int Power = Exp ? static_cast<int>(Exp) - 1023 - 52 : -1074;
		if (Power >= 0)
		{
			N = Shift(N, static_cast<unsigned>(Power), true);
		}
		else
		{
			unsigned Zeros = 0;
			while (!(Fraction & 1) && Zeros < static_cast<unsigned>(-Power))
			{
				++Zeros;
				Fraction >>= 1;
			}
			N = Small(A, Fraction, N.Negative);
			D = Shift(D, static_cast<unsigned>(-Power) - Zeros, true);
		}
	}
	std::uint64_t FloatBits(const FInteger& Numerator, const FInteger& Denominator, unsigned P, int Emin,
							int Emax, bool Exact)
	{
		auto& A = *Numerator.Words.get_allocator().Area;
		if (Numerator.Words.empty())
		{
			return 0;
		}
		bool Negative = Numerator.Negative;
		auto N = Copy(Numerator), D = Copy(Denominator);
		N.Negative = D.Negative = false;
		int Exponent = static_cast<int>(Bits(N)) - static_cast<int>(Bits(D));
		std::uint64_t Sign = Negative ? std::uint64_t(1) << (P == 53 ? 63 : 31) : 0;
		if (Exponent > Emax + 1)
		{
			Fail(ENumericStatus::OutOfRange, "floating overflow");
		}
		if (Exponent < Emin - static_cast<int>(P))
		{
			if (Exact)
			{
				Fail(ENumericStatus::InexactConversion, "floating underflow");
			}
			return Sign;
		}
		if (Exponent >= 0)
		{
			if (CompareAbs(N, Shift(D, Exponent, true)) < 0)
			{
				--Exponent;
			}
		}
		else if (CompareAbs(Shift(N, static_cast<unsigned>(-Exponent), true), D) < 0)
		{
			--Exponent;
		}
		if (Exponent > Emax)
		{
			Fail(ENumericStatus::OutOfRange, "floating overflow");
		}
		int K = std::max(Exponent - static_cast<int>(P - 1), Emin - static_cast<int>(P - 1));
		if (K >= 0)
		{
			D = Shift(D, K, true);
		}
		else
		{
			N = Shift(N, static_cast<unsigned>(-K), true);
		}
		FInteger Q(A), R(A);
		DivMod(N, D, Q, R);
		auto Rounded = Magnitude64(Q);
		if (RoundUp(R, D, Negative, (Rounded & 1) != 0, ENumericRoundingMode::HalfEven))
		{
			++Rounded;
		}
		if (Rounded == (std::uint64_t(1) << P))
		{
			Rounded >>= 1;
			++Exponent;
		}
		if (Exponent > Emax)
		{
			Fail(ENumericStatus::OutOfRange, "floating overflow");
		}
		if (Exact && !R.Words.empty())
		{
			Fail(ENumericStatus::InexactConversion, "floating rounding");
		}
		auto Hidden = std::uint64_t(1) << (P - 1);
		std::uint64_t EncodedExponent =
			Rounded >= Hidden ? static_cast<std::uint64_t>(std::max(Exponent, Emin) - Emin + 1) : 0;
		return Sign | (EncodedExponent << (P - 1)) | (Rounded >= Hidden ? Rounded - Hidden : Rounded);
	}
} // namespace VeroScript::Numerics::Detail

namespace VeroScript::Numerics
{
	using namespace Detail;
	namespace
	{
		std::uint64_t ToUInt64(FArea& A, const FBigInteger& X)
		{
			A.Input(X);
			if (X.IsNegative() || X.BitLength() > 64)
			{
				Fail(ENumericStatus::OutOfRange, "uint64 range");
			}
			return static_cast<std::uint64_t>(FNumericAccess::Word(X, 0)) |
				   (static_cast<std::uint64_t>(FNumericAccess::Word(X, 1)) << 32);
		}
		std::int64_t ToInt64(FArea& A, const FBigInteger& X)
		{
			A.Input(X);
			if (X.BitLength() > 64)
			{
				Fail(ENumericStatus::OutOfRange, "int64 range");
			}
			auto N = static_cast<std::uint64_t>(FNumericAccess::Word(X, 0)) |
					 (static_cast<std::uint64_t>(FNumericAccess::Word(X, 1)) << 32);
			auto Limit = static_cast<std::uint64_t>(INT64_MAX) + (X.IsNegative() ? 1 : 0);
			if (N > Limit)
			{
				Fail(ENumericStatus::OutOfRange, "int64 range");
			}
			return X.IsNegative() ? N == (std::uint64_t(1) << 63) ? INT64_MIN : -static_cast<std::int64_t>(N)
								  : static_cast<std::int64_t>(N);
		}
		template <class T, class Value>
		bool ToFloating(const Value& X, T& Out, bool Exact, FNumericOperationContext& C,
						FNumericError& E) noexcept
		{
			return Boundary(C, E,
							[&](FArea& A)
							{
								A.Input(X);
								FInteger N(A), D(A);
								if constexpr (std::is_same_v<Value, FBigInteger>)
								{
									N = Read(A, X);
									D = Small(A, 1);
								}
								else
								{
									N = Read(A, X.Numerator());
									D = Read(A, X.Denominator());
								}
								static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
												  std::numeric_limits<float>::digits == 24,
											  "binary32 required");
								constexpr bool Double = std::is_same_v<T, double>;
								auto Bits = FloatBits(N, D, Double ? 53 : 24, Double ? -1022 : -126,
													  Double ? 1023 : 127, Exact);
								T Result;
								if constexpr (Double)
								{
									std::memcpy(&Result, &Bits, 8);
								}
								else
								{
									auto Raw = static_cast<std::uint32_t>(Bits);
									std::memcpy(&Result, &Raw, 4);
								}
								A.Check();
								Out = Result;
							});
		}
	} // namespace
	bool TryToInt64(const FBigInteger& X, std::int64_t& Out, FNumericOperationContext& C,
					FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							auto R = ToInt64(A, X);
							A.Check();
							Out = R;
						});
	}
	bool TryToUInt64(const FBigInteger& X, std::uint64_t& Out, FNumericOperationContext& C,
					 FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							auto R = ToUInt64(A, X);
							A.Check();
							Out = R;
						});
	}
	bool TryToInt64(const FRational& X, std::int64_t& Out, FNumericOperationContext& C,
					FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							if (!X.IsInteger())
							{
								Fail(ENumericStatus::InexactConversion, "fractional integer conversion");
							}
							auto R = ToInt64(A, X.Numerator());
							A.Check();
							Out = R;
						});
	}
	bool TryToUInt64(const FRational& X, std::uint64_t& Out, FNumericOperationContext& C,
					 FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							if (!X.IsInteger())
							{
								Fail(ENumericStatus::InexactConversion, "fractional integer conversion");
							}
							auto R = ToUInt64(A, X.Numerator());
							A.Check();
							Out = R;
						});
	}
	bool TryFromDouble(double X, FRational& Out, FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							FInteger N(A), D(A);
							FromDouble(A, X, N, D);
							auto R = PublishRational(std::move(N), std::move(D), true);
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryFromDouble(double X, FBigInteger& Out, bool Exact, FNumericOperationContext& C,
					   FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							std::uint64_t Raw;
							std::memcpy(&Raw, &X, sizeof(Raw));
							const auto Exponent = static_cast<unsigned>((Raw >> 52) & 2047);
							if (Exponent == 2047)
							{
								Fail(ENumericStatus::InvalidArgument, "nonfinite floating input");
							}
							auto Mantissa = Raw & ((std::uint64_t(1) << 52) - 1);
							if (Exponent)
							{
								Mantissa |= std::uint64_t(1) << 52;
							}
							const int Power = Exponent ? static_cast<int>(Exponent) - 1023 - 52 : -1074;
							bool Fractional = false;
							if (Power < 0)
							{
								const auto Drop = static_cast<unsigned>(-Power);
								Fractional = Drop >= 64 ? Mantissa != 0
														: (Mantissa & ((std::uint64_t(1) << Drop) - 1)) != 0;
								Mantissa = Drop >= 64 ? 0 : Mantissa >> Drop;
							}
							if (Exact && Fractional)
							{
								Fail(ENumericStatus::InexactConversion, "fractional floating input");
							}
							auto N = Small(A, Mantissa, (Raw >> 63) != 0);
							if (Power > 0)
							{
								N = Shift(N, static_cast<unsigned>(Power), true);
							}
							auto R = Publish(N);
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryToDouble(const FBigInteger& X, double& Out, bool Exact, FNumericOperationContext& C,
					 FNumericError& E) noexcept
	{
		return ToFloating(X, Out, Exact, C, E);
	}
	bool TryToDouble(const FRational& X, double& Out, bool Exact, FNumericOperationContext& C,
					 FNumericError& E) noexcept
	{
		return ToFloating(X, Out, Exact, C, E);
	}
	bool TryToFloat(const FBigInteger& X, float& Out, bool Exact, FNumericOperationContext& C,
					FNumericError& E) noexcept
	{
		return ToFloating(X, Out, Exact, C, E);
	}
	bool TryToFloat(const FRational& X, float& Out, bool Exact, FNumericOperationContext& C,
					FNumericError& E) noexcept
	{
		return ToFloating(X, Out, Exact, C, E);
	}
} // namespace VeroScript::Numerics
