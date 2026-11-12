// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericsInternal.h"

namespace VeroScript::Numerics::Detail
{
	FRational PublishRational(FInteger N, FInteger D, bool Reduced)
	{
		auto& A = *N.Words.get_allocator().Area;
		if (D.Words.empty())
		{
			Fail(ENumericStatus::DivisionByZero, "zero denominator");
		}
		if (N.Words.empty())
		{
			return {};
		}
		if (D.Negative)
		{
			N.Negative = !N.Negative;
			D.Negative = false;
		}
		// Integral values are already reduced; other coprime values need no division.
		if (!Reduced && !(D.Words.size() == 1 && D.Words[0] == 1))
		{
			auto G = Gcd(Copy(N), Copy(D));
			if (G.Words.size() != 1 || G.Words[0] != 1)
			{
				N = DivideExact(N, G);
				D = DivideExact(D, G);
			}
		}
		// Both payloads are prepared before publishing the rational as one value.
		auto Numerator = Publish(N), Denominator = Publish(D);
		A.Reserve(sizeof(FRationalData));
		auto R = std::make_shared<FRationalData>();
		R->Numerator = std::move(Numerator);
		R->Denominator = std::move(Denominator);
		A.Check();
		return FNumericAccess::Rational(std::move(R));
	}
	bool RoundUp(const FInteger& R, const FInteger& D, bool Negative, bool Odd, ENumericRoundingMode Mode)
	{
		if (static_cast<unsigned>(Mode) > static_cast<unsigned>(ENumericRoundingMode::HalfAwayFromZero))
		{
			Fail(ENumericStatus::InvalidArgument, "rounding mode");
		}
		if (R.Words.empty())
		{
			return false;
		}
		switch (Mode)
		{
		case ENumericRoundingMode::TowardZero:
		{
			return false;
		}
		case ENumericRoundingMode::Floor:
		{
			return Negative;
		}
		case ENumericRoundingMode::Ceil:
		{
			return !Negative;
		}
		case ENumericRoundingMode::AwayFromZero:
		{
			return true;
		}
		default:
		{
			auto Twice = Shift(R, 1, true);
			auto Compare = CompareAbs(Twice, D);
			return Compare > 0 || (!Compare && (Mode == ENumericRoundingMode::HalfAwayFromZero || Odd));
		}
		}
	}
} // namespace VeroScript::Numerics::Detail

namespace VeroScript::Numerics
{
	using namespace Detail;
	bool TryMakeRational(const FBigInteger& X, const FBigInteger& Y, FRational& Out,
						 FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							auto R = PublishRational(Read(A, X), Read(A, Y));
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryBinary(ENumericBinaryOperation Op, const FRational& X, const FRational& Y, FRational& Out,
				   FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							A.Input(Y);
							auto N1 = Read(A, X.Numerator()), D1 = Read(A, X.Denominator());
							auto N2 = Read(A, Y.Numerator()), D2 = Read(A, Y.Denominator());
							FInteger N(A), D(A);
							bool Reduced = true;
							switch (Op)
							{
							case ENumericBinaryOperation::Add:
							case ENumericBinaryOperation::Subtract:
							{
								if (CompareAbs(D1, D2) == 0)
								{
									N = Op == ENumericBinaryOperation::Add ? Add(N1, N2) : Subtract(N1, N2);
									D = std::move(D1);
									Reduced = false;
									break;
								}
								auto G = Gcd(Copy(D1), Copy(D2));
								auto B = DivideExact(D1, G), DPart = DivideExact(D2, G);
								auto L = Multiply(N1, DPart), R = Multiply(N2, B);
								auto T = Op == ENumericBinaryOperation::Add ? Add(L, R) : Subtract(L, R);
								if (T.Words.empty())
								{
									FRational Zero;
									A.Check();
									Out.Swap(Zero);
									return;
								}
								auto H = Gcd(Copy(T), Copy(G));
								N = DivideExact(T, H);
								D = Multiply(B, DivideExact(D2, H));
								break;
							}
							case ENumericBinaryOperation::Multiply:
							case ENumericBinaryOperation::Divide:
							{
								if (Op == ENumericBinaryOperation::Divide)
								{
									if (N2.Words.empty())
									{
										Fail(ENumericStatus::DivisionByZero, "rational division");
									}
									std::swap(N2, D2);
								}
								auto G = Gcd(Copy(N1), Copy(D2)), H = Gcd(Copy(N2), Copy(D1));
								N = Multiply(DivideExact(N1, G), DivideExact(N2, H));
								D = Multiply(DivideExact(D1, H), DivideExact(D2, G));
								break;
							}
							case ENumericBinaryOperation::Remainder:
							{
								if (N2.Words.empty())
								{
									Fail(ENumericStatus::DivisionByZero, "rational remainder");
								}
								auto L = Multiply(N1, D2), R = Multiply(D1, N2);
								FInteger Q(A);
								DivMod(L, R, Q, N);
								D = Multiply(D1, D2);
								Reduced = false;
								break;
							}
							default:
							{
								Fail(ENumericStatus::InvalidArgument, "rational operation");
							}
							}
							auto Result = PublishRational(std::move(N), std::move(D), Reduced);
							A.Check();
							Out.Swap(Result);
						});
	}
	bool TryCompare(const FRational& X, const FRational& Y, int& Out, FNumericOperationContext& C,
					FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							A.Input(Y);
							auto L = Multiply(Read(A, X.Numerator()), Read(A, Y.Denominator()));
							auto R = Multiply(Read(A, Y.Numerator()), Read(A, X.Denominator()));
							auto Result = Compare(L, R);
							A.Check();
							Out = Result;
						});
	}
	bool TryNegate(const FRational& X, FRational& Out, FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							FRational R;
							if (!X.IsZero())
							{
								FReservation Reservation(A, sizeof(FRationalData));
								auto D = std::make_shared<FRationalData>();
								D->Numerator = FNumericAccess::Negate(X.Numerator());
								D->Denominator = X.Denominator();
								R = FNumericAccess::Rational(std::move(D));
							}
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryAbs(const FRational& X, FRational& Out, FNumericOperationContext& C, FNumericError& E) noexcept
	{
		if (X.IsNegative())
		{
			return TryNegate(X, Out, C, E);
		}
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							auto R = X;
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryReciprocal(const FRational& X, FRational& Out, FNumericOperationContext& C,
					   FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							if (X.IsZero())
							{
								Fail(ENumericStatus::DivisionByZero, "reciprocal");
							}
							FReservation Reservation(A, sizeof(FRationalData));
							auto D = std::make_shared<FRationalData>();
							D->Numerator =
								X.IsNegative() ? FNumericAccess::Negate(X.Denominator()) : X.Denominator();
							D->Denominator =
								X.IsNegative() ? FNumericAccess::Negate(X.Numerator()) : X.Numerator();
							auto R = FNumericAccess::Rational(std::move(D));
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryPow(const FRational& X, std::int64_t Count, FRational& Out, FNumericOperationContext& C,
				FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							if (Count < 0 && X.IsZero())
							{
								Fail(ENumericStatus::DivisionByZero, "negative power of zero");
							}
							auto N = Read(A, Count < 0 ? X.Denominator() : X.Numerator());
							auto D = Read(A, Count < 0 ? X.Numerator() : X.Denominator());
							auto Magnitude = Count < 0 ? 0ull - static_cast<std::uint64_t>(Count)
													   : static_cast<std::uint64_t>(Count);
							auto R = PublishRational(Pow(std::move(N), Magnitude),
													 Pow(std::move(D), Magnitude), true);
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryRound(const FRational& X, ENumericRoundingMode Mode, FBigInteger& Out,
				  FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							if (static_cast<unsigned>(Mode) >
								static_cast<unsigned>(ENumericRoundingMode::HalfAwayFromZero))
							{
								Fail(ENumericStatus::InvalidArgument, "rounding mode");
							}
							if (X.IsInteger())
							{
								auto R = X.Numerator();
								A.Check();
								Out.Swap(R);
								return;
							}
							auto N = Read(A, X.Numerator()), D = Read(A, X.Denominator());
							FInteger Q(A), R(A);
							DivMod(N, D, Q, R);
							if (RoundUp(R, D, X.IsNegative(), !Q.Words.empty() && (Q.Words[0] & 1), Mode))
							{
								AddSmall(Q, 1);
							}
							Q.Negative = X.IsNegative() && !Q.Words.empty();
							auto Value = Publish(Q);
							A.Check();
							Out.Swap(Value);
						});
	}
	bool TryHasFiniteDecimal(const FRational& X, bool& Out, FNumericOperationContext& C,
							 FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							auto D = Read(A, X.Denominator());
							std::uint64_t Zeros = 0;
							for (auto W : D.Words)
							{
								A.Check(1);
								if (!W)
								{
									Zeros += 32;
									continue;
								}
								while (!(W & 1))
								{
									++Zeros;
									W >>= 1;
								}
								break;
							}
							D = Shift(D, Zeros, false);
							while (!(D.Words.size() == 1 && D.Words[0] == 1))
							{
								auto Candidate = Copy(D);
								if (!DivSmall(Candidate, 1220703125u))
								{
									D = std::move(Candidate);
									continue;
								}
								Candidate = Copy(D);
								if (DivSmall(Candidate, 5))
								{
									A.Check();
									Out = false;
									return;
								}
								D = std::move(Candidate);
							}
							A.Check();
							Out = true;
						});
	}
} // namespace VeroScript::Numerics
