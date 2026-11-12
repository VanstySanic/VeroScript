// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericsInternal.h"

namespace VeroScript::Numerics::Detail
{
	int CompareAbs(const FInteger& X, const FInteger& Y)
	{
		if (X.Words.size() != Y.Words.size())
		{
			return X.Words.size() < Y.Words.size() ? -1 : 1;
		}
		auto& A = *X.Words.get_allocator().Area;
		for (std::size_t I = X.Words.size(); I; --I)
		{
			if (!((X.Words.size() - I) % 1024))
			{
				A.Check(std::min<std::size_t>(1024, I));
			}
			if (X.Words[I - 1] != Y.Words[I - 1])
			{
				return X.Words[I - 1] < Y.Words[I - 1] ? -1 : 1;
			}
		}
		return 0;
	}
	int Compare(const FInteger& X, const FInteger& Y)
	{
		if (X.Negative != Y.Negative)
		{
			return X.Negative ? -1 : 1;
		}
		return CompareAbs(X, Y) * (X.Negative ? -1 : 1);
	}
	namespace
	{
		int CompareValues(FArea& A, const FBigInteger& X, const FBigInteger& Y)
		{
			A.Input(X);
			A.Input(Y);
			if (X.IsNegative() != Y.IsNegative())
			{
				return X.IsNegative() ? -1 : 1;
			}
			const auto LeftBits = X.BitLength(), RightBits = Y.BitLength();
			int Result = 0;
			if (LeftBits != RightBits)
			{
				Result = LeftBits < RightBits ? -1 : 1;
			}
			else
			{
				const auto Size = FNumericAccess::Size(X);
				for (std::size_t I = Size; I; --I)
				{
					if (!((Size - I) % 1024))
					{
						A.Check(std::min<std::size_t>(1024, I));
					}
					const auto Left = FNumericAccess::Word(X, I - 1);
					const auto Right = FNumericAccess::Word(Y, I - 1);
					if (Left != Right)
					{
						Result = Left < Right ? -1 : 1;
						break;
					}
				}
			}
			return X.IsNegative() ? -Result : Result;
		}
		FBigInteger MultiplyWordValue(FArea& A, const FBigInteger& X, const std::uint32_t Factor,
			const bool Negative)
		{
			if (!Factor) return {};
			if (Factor == 1)
			{
				return X.IsNegative() == Negative ? X : FNumericAccess::Negate(X);
			}
			A.Bits(X.BitLength() + WordBits(Factor) - 1);
			const auto Size = FNumericAccess::Size(X);
			const auto* Words = FNumericAccess::Words(X);
			// Build a private immutable payload directly. Copying the input into
			// scratch and copying the product again would each traverse every limb.
			A.Reserve(sizeof(FBigIntegerData) + (Size + 1) * sizeof(std::uint32_t));
			auto Data = std::make_shared<FBigIntegerData>();
			Data->Words.reserve(Size + 1);
			const auto AppendWord = [&](const std::uint32_t Word)
			{
				Data->Words.push_back(Word);
			};
			std::uint64_t Carry = 0;
			for (std::size_t Index = 0; Index < Size; ++Index)
			{
				if (!(Index % 256))
				{
					// Retain the canonical work cost even when the hash is deferred.
					A.Check(std::min<std::size_t>(256, Size - Index) * 3);
				}
				const auto Product = static_cast<std::uint64_t>(Words[Index]) * Factor + Carry;
				AppendWord(static_cast<std::uint32_t>(Product));
				Carry = Product >> 32;
			}
			if (Carry)
			{
				A.Check(2);
				AppendWord(static_cast<std::uint32_t>(Carry));
			}
			Data->Bits = (Data->Words.size() - 1) * 32 + WordBits(Data->Words.back());
			A.Bits(Data->Bits, true);
			A.Check();
			return FNumericAccess::Large(std::move(Data), Negative);
		}
		FInteger AddAbs(const FInteger& X, const FInteger& Y)
		{
			auto& A = *X.Words.get_allocator().Area;
			FInteger R(A);
			auto N = std::max(X.Words.size(), Y.Words.size());
			Resize(R.Words, N + 1);
			std::uint64_t Carry = 0;
			for (std::size_t I = 0; I < N; ++I)
			{
				if (!(I % 1024))
				{
					A.Check(std::min<std::size_t>(1024, N - I));
				}
				auto V = Carry + (I < X.Words.size() ? X.Words[I] : 0) +
						 static_cast<std::uint64_t>(I < Y.Words.size() ? Y.Words[I] : 0);
				R.Words[I] = static_cast<std::uint32_t>(V);
				Carry = V >> 32;
			}
			R.Words[N] = static_cast<std::uint32_t>(Carry);
			Normalize(R);
			return R;
		}
		FInteger SubAbs(const FInteger& X, const FInteger& Y)
		{
			auto& A = *X.Words.get_allocator().Area;
			FInteger R(A);
			Resize(R.Words, X.Words.size());
			std::uint64_t Borrow = 0;
			for (std::size_t I = 0; I < X.Words.size(); ++I)
			{
				if (!(I % 1024))
				{
					A.Check(std::min<std::size_t>(1024, X.Words.size() - I));
				}
				auto YWord = Borrow + static_cast<std::uint64_t>(I < Y.Words.size() ? Y.Words[I] : 0);
				R.Words[I] = static_cast<std::uint32_t>(X.Words[I] - YWord);
				Borrow = X.Words[I] < YWord;
			}
			Normalize(R);
			return R;
		}
	} // namespace
	FInteger Add(const FInteger& X, const FInteger& Y)
	{
		if (X.Negative == Y.Negative)
		{
			auto R = AddAbs(X, Y);
			R.Negative = X.Negative && !R.Words.empty();
			return R;
		}
		auto C = CompareAbs(X, Y);
		auto R = C >= 0 ? SubAbs(X, Y) : SubAbs(Y, X);
		R.Negative = !R.Words.empty() && (C >= 0 ? X.Negative : Y.Negative);
		return R;
	}
	FInteger Subtract(const FInteger& X, const FInteger& Y)
	{
		if (X.Negative != Y.Negative)
		{
			auto R = AddAbs(X, Y);
			R.Negative = X.Negative && !R.Words.empty();
			return R;
		}
		const auto C = CompareAbs(X, Y);
		auto R = C >= 0 ? SubAbs(X, Y) : SubAbs(Y, X);
		R.Negative = !R.Words.empty() && (C >= 0 ? X.Negative : !X.Negative);
		return R;
	}
	FInteger Multiply(const FInteger& X, const FInteger& Y)
	{
		auto& A = *X.Words.get_allocator().Area;
		FInteger R(A);
		if (X.Words.empty() || Y.Words.empty())
		{
			return R;
		}
		A.Bits(Bits(X) + Bits(Y) - 1);
		Resize(R.Words, X.Words.size() + Y.Words.size());
		// Iterate the shorter operand outside. Work-unit accounting remains the
		// exact limb product; small factors no longer poll once per output limb.
		const auto& Outer = X.Words.size() <= Y.Words.size() ? X.Words : Y.Words;
		const auto& Inner = X.Words.size() <= Y.Words.size() ? Y.Words : X.Words;
		for (std::size_t I = 0; I < Outer.size(); ++I)
		{
			std::uint64_t Carry = 0;
			for (std::size_t J = 0; J < Inner.size(); ++J)
			{
				if (!(J % 512))
				{
					A.Check(std::min<std::size_t>(512, Inner.size() - J));
				}
				auto V = static_cast<std::uint64_t>(Outer[I]) * Inner[J] + R.Words[I + J] + Carry;
				R.Words[I + J] = static_cast<std::uint32_t>(V);
				Carry = V >> 32;
			}
			R.Words[I + Inner.size()] = static_cast<std::uint32_t>(Carry);
		}
		R.Negative = X.Negative != Y.Negative;
		Normalize(R);
		return R;
	}
	void AddSmall(FInteger& X, std::uint32_t N)
	{
		auto& A = *X.Words.get_allocator().Area;
		std::uint64_t Carry = N;
		std::size_t I = 0;
		while (Carry && I < X.Words.size())
		{
			if (!(I % 1024))
			{
				A.Check(std::min<std::size_t>(1024, X.Words.size() - I));
			}
			Carry += X.Words[I];
			X.Words[I++] = static_cast<std::uint32_t>(Carry);
			Carry >>= 32;
		}
		if (Carry)
		{
			auto Size = X.Words.size();
			Resize(X.Words, Size + 1);
			X.Words[Size] = static_cast<std::uint32_t>(Carry);
		}
		Normalize(X);
	}
	void MulSmall(FInteger& X, std::uint32_t N)
	{
		auto& A = *X.Words.get_allocator().Area;
		if (!N)
		{
			X.Words.clear();
			X.Negative = false;
			return;
		}
		std::uint64_t Carry = 0;
		for (std::size_t I = 0; I < X.Words.size(); ++I)
		{
			if (!(I % 1024))
			{
				A.Check(std::min<std::size_t>(1024, X.Words.size() - I));
			}
			auto V = static_cast<std::uint64_t>(X.Words[I]) * N + Carry;
			X.Words[I] = static_cast<std::uint32_t>(V);
			Carry = V >> 32;
		}
		if (Carry)
		{
			auto Size = X.Words.size();
			Resize(X.Words, Size + 1);
			X.Words[Size] = static_cast<std::uint32_t>(Carry);
		}
		Normalize(X);
	}
	std::uint32_t DivSmall(FInteger& X, std::uint32_t N)
	{
		if (!N)
		{
			Fail(ENumericStatus::DivisionByZero, "division");
		}
		auto& A = *X.Words.get_allocator().Area;
		std::uint64_t R = 0;
		for (std::size_t I = X.Words.size(); I; --I)
		{
			if (!((X.Words.size() - I) % 1024))
			{
				A.Check(std::min<std::size_t>(1024, I));
			}
			auto V = (R << 32) | X.Words[I - 1];
			X.Words[I - 1] = static_cast<std::uint32_t>(V / N);
			R = V % N;
		}
		Normalize(X);
		return static_cast<std::uint32_t>(R);
	}
	FInteger Shift(const FInteger& X, std::uint64_t Count, bool Left, bool Arithmetic)
	{
		auto& A = *X.Words.get_allocator().Area;
		FInteger R(A);
		if (X.Words.empty())
		{
			return R;
		}
		if (Left)
		{
			if (Count > 2 * A.Limits.MaxValueBits + 64 - Bits(X))
			{
				Fail(ENumericStatus::LimitExceeded, "shift bits");
			}
		}
		else if (Count >= Bits(X))
		{
			return Small(A, Arithmetic && X.Negative ? 1 : 0, Arithmetic && X.Negative);
		}
		auto Whole = static_cast<std::size_t>(Count / 32);
		auto Part = static_cast<unsigned>(Count % 32);
		bool Discarded = false;
		if (Left)
		{
			Resize(R.Words, X.Words.size() + Whole + (Part ? 1 : 0));
			std::uint64_t Carry = 0;
			for (std::size_t I = 0; I < X.Words.size(); ++I)
			{
				if (!(I % 1024))
				{
					A.Check(std::min<std::size_t>(1024, X.Words.size() - I));
				}
				auto V = (static_cast<std::uint64_t>(X.Words[I]) << Part) | Carry;
				R.Words[I + Whole] = static_cast<std::uint32_t>(V);
				Carry = V >> 32;
			}
			if (Part)
			{
				R.Words.back() = static_cast<std::uint32_t>(Carry);
			}
		}
		else
		{
			Resize(R.Words, X.Words.size() - Whole);
			for (std::size_t I = 0; I < X.Words.size(); ++I)
			{
				if (!(I % 1024))
				{
					A.Check(std::min<std::size_t>(1024, X.Words.size() - I));
				}
				if (I < Whole)
				{
					Discarded |= X.Words[I] != 0;
				}
				else
				{
					R.Words[I - Whole] = X.Words[I] >> Part;
					if (Part && I + 1 < X.Words.size())
					{
						R.Words[I - Whole] |= X.Words[I + 1] << (32 - Part);
					}
				}
			}
			if (Part)
			{
				Discarded |= (X.Words[Whole] & ((std::uint32_t(1) << Part) - 1)) != 0;
			}
		}
		Normalize(R);
		if (!Left && Arithmetic && X.Negative && Discarded)
		{
			AddSmall(R, 1);
		}
		R.Negative = X.Negative && !R.Words.empty();
		return R;
	}
	void DivMod(const FInteger& X, const FInteger& Y, FInteger& Q, FInteger& R)
	{
		if (Y.Words.empty())
		{
			Fail(ENumericStatus::DivisionByZero, "division");
		}
		auto& A = *X.Words.get_allocator().Area;
		A.Check();
		if (CompareAbs(X, Y) < 0)
		{
			Q = FInteger(A);
			R = Copy(X);
			return;
		}
		if (Y.Words.size() == 1)
		{
			Q = Copy(X);
			auto Rem = DivSmall(Q, Y.Words[0]);
			Q.Negative = !Q.Words.empty() && (X.Negative != Y.Negative);
			R = Small(A, Rem, X.Negative);
			return;
		}
		unsigned S = 0;
		for (auto High = Y.Words.back(); !(High & 0x80000000u); High <<= 1)
		{
			++S;
		}
		auto U = Shift(X, S, true), V = Shift(Y, S, true);
		U.Negative = V.Negative = false;
		auto N = V.Words.size(), M = X.Words.size() - N;
		Resize(U.Words, X.Words.size() + 1);
		Q = FInteger(A);
		Resize(Q.Words, M + 1);
		constexpr std::uint64_t Base = std::uint64_t(1) << 32;
		for (std::size_t Pos = M + 1; Pos; --Pos)
		{
			auto J = Pos - 1;
			A.Check(1);
			std::uint64_t Guess, Rem;
			if (U.Words[J + N] == V.Words[N - 1])
			{
				Guess = Base - 1;
				Rem = static_cast<std::uint64_t>(U.Words[J + N - 1]) + V.Words[N - 1];
			}
			else
			{
				auto High = (static_cast<std::uint64_t>(U.Words[J + N]) << 32) | U.Words[J + N - 1];
				Guess = High / V.Words[N - 1];
				Rem = High % V.Words[N - 1];
			}
			while (Rem < Base && Guess * V.Words[N - 2] > (Rem << 32) + U.Words[J + N - 2])
			{
				--Guess;
				Rem += V.Words[N - 1];
			}
			std::uint64_t Borrow = 0;
			for (std::size_t I = 0; I < N; ++I)
			{
				if (!(I % 512))
				{
					A.Check(std::min<std::size_t>(512, N - I));
				}
				auto Product = Guess * V.Words[I] + Borrow;
				auto Low = static_cast<std::uint32_t>(Product);
				Borrow = (Product >> 32) + (U.Words[J + I] < Low);
				U.Words[J + I] -= Low;
			}
			bool Negative = U.Words[J + N] < Borrow;
			U.Words[J + N] -= static_cast<std::uint32_t>(Borrow);
			if (Negative)
			{
				--Guess;
				std::uint64_t Carry = 0;
				for (std::size_t I = 0; I < N; ++I)
				{
					if (!(I % 512))
					{
						A.Check(std::min<std::size_t>(512, N - I));
					}
					auto Sum = static_cast<std::uint64_t>(U.Words[J + I]) + V.Words[I] + Carry;
					U.Words[J + I] = static_cast<std::uint32_t>(Sum);
					Carry = Sum >> 32;
				}
				U.Words[J + N] += static_cast<std::uint32_t>(Carry);
			}
			Q.Words[J] = static_cast<std::uint32_t>(Guess);
		}
		Resize(U.Words, N);
		Normalize(U);
		R = Shift(U, S, false);
		R.Negative = X.Negative && !R.Words.empty();
		Q.Negative = X.Negative != Y.Negative;
		Normalize(Q);
	}
	FInteger DivideExact(const FInteger& X, const FInteger& Y)
	{
		auto& A = *X.Words.get_allocator().Area;
		FInteger Q(A), R(A);
		DivMod(X, Y, Q, R);
		if (!R.Words.empty())
		{
			Fail(ENumericStatus::InternalError, "nonexact division");
		}
		return Q;
	}
	FInteger Gcd(FInteger X, FInteger Y)
	{
		auto& A = *X.Words.get_allocator().Area;
		if (X.Words.size() < Y.Words.size())
		{
			std::swap(X, Y);
		}
		if (X.Words.size() <= 2 && Y.Words.size() <= 2)
		{
			const auto Magnitude = [](const FInteger& V)
			{
				return (V.Words.empty() ? std::uint64_t{0} : V.Words[0])
					| (V.Words.size() == 2 ? static_cast<std::uint64_t>(V.Words[1]) << 32 : 0);
			};
			auto Left = Magnitude(X), Right = Magnitude(Y);
			while (Right)
			{
				A.Check(1);
				const auto Remainder = Left % Right;
				Left = Right;
				Right = Remainder;
			}
			return Small(A, Left);
		}
		if (Y.Words.size() == 1)
		{
			if (Y.Words[0] == 1)
			{
				return Small(A, 1);
			}
			// A single-word divisor needs only a remainder, not a copied quotient.
			std::uint64_t Remainder = 0;
			for (std::size_t I = X.Words.size(); I; --I)
			{
				if (!((X.Words.size() - I) % 1024))
				{
					A.Check(std::min<std::size_t>(1024, I));
				}
				Remainder = ((Remainder << 32) | X.Words[I - 1]) % Y.Words[0];
			}
			auto Left = Y.Words[0], Right = static_cast<std::uint32_t>(Remainder);
			while (Right)
			{
				A.Check(1);
				const auto Next = Left % Right;
				Left = Right;
				Right = Next;
			}
			return Small(A, Left);
		}
		X.Negative = Y.Negative = false;
		while (!Y.Words.empty())
		{
			FInteger Q(A), R(A);
			DivMod(X, Y, Q, R);
			X = std::move(Y);
			Y = std::move(R);
		}
		return X;
	}
	FInteger Pow(FInteger X, std::uint64_t N)
	{
		auto& A = *X.Words.get_allocator().Area;
		if (!N)
		{
			return Small(A, 1);
		}
		if (X.Words.empty())
		{
			return X;
		}
		if (Bits(X) == 1)
		{
			return Small(A, 1, X.Negative && (N & 1));
		}
		FInteger R = Small(A, 1);
		while (N)
		{
			if (N & 1)
			{
				R = Multiply(R, X);
			}
			N >>= 1;
			if (N)
			{
				X = Multiply(X, X);
			}
		}
		return R;
	}
	FInteger Bitwise(const FInteger& X, const FInteger& Y, ENumericBinaryOperation Op)
	{
		auto& A = *X.Words.get_allocator().Area;
		auto N = std::max(X.Words.size(), Y.Words.size()) + 1;
		FInteger R(A);
		Resize(R.Words, N);
		std::uint64_t XC = X.Negative, YC = Y.Negative;
		for (std::size_t I = 0; I < N; ++I)
		{
			if (!(I % 512))
			{
				A.Check(std::min<std::size_t>(512, N - I));
			}
			std::uint64_t WX = I < X.Words.size() ? X.Words[I] : 0;
			std::uint64_t WY = I < Y.Words.size() ? Y.Words[I] : 0;
			if (X.Negative)
			{
				WX = static_cast<std::uint32_t>(~WX) + XC;
				XC = WX >> 32;
			}
			if (Y.Negative)
			{
				WY = static_cast<std::uint32_t>(~WY) + YC;
				YC = WY >> 32;
			}
			R.Words[I] = static_cast<std::uint32_t>(Op == ENumericBinaryOperation::And	? WX & WY
													: Op == ENumericBinaryOperation::Or ? WX | WY
																						: WX ^ WY);
		}
		R.Negative = (R.Words.back() & 0x80000000u) != 0;
		if (R.Negative)
		{
			std::uint64_t Carry = 1;
			for (std::size_t I = 0; I < N; ++I)
			{
				if (!(I % 1024))
				{
					A.Check(std::min<std::size_t>(1024, N - I));
				}
				auto V = static_cast<std::uint64_t>(static_cast<std::uint32_t>(~R.Words[I])) + Carry;
				R.Words[I] = static_cast<std::uint32_t>(V);
				Carry = V >> 32;
			}
		}
		Normalize(R);
		return R;
	}
} // namespace VeroScript::Numerics::Detail

namespace VeroScript::Numerics
{
	using namespace Detail;
	bool TryFromInt64(std::int64_t V, FBigInteger& Out, FNumericOperationContext& C,
					  FNumericError& E) noexcept
	{
		return Boundary(
			C, E,
			[&](FArea& A)
			{
				auto R = Publish(Small(
					A, V < 0 ? 0ull - static_cast<std::uint64_t>(V) : static_cast<std::uint64_t>(V), V < 0));
				A.Check();
				Out.Swap(R);
			});
	}
	bool TryFromUInt64(std::uint64_t V, FBigInteger& Out, FNumericOperationContext& C,
					   FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							auto R = Publish(Small(A, V));
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryBinary(ENumericBinaryOperation Op, const FBigInteger& X, const FBigInteger& Y, FBigInteger& Out,
				   FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							const auto LeftSize = FNumericAccess::Size(X), RightSize = FNumericAccess::Size(Y);
							if (Op == ENumericBinaryOperation::Multiply &&
								((LeftSize > 2 && RightSize <= 1) || (RightSize > 2 && LeftSize <= 1)))
							{
								A.Input(X);
								A.Input(Y);
								const auto& Large = LeftSize > 2 ? X : Y;
								const auto Factor = FNumericAccess::Word(LeftSize > 2 ? Y : X, 0);
								auto Next = MultiplyWordValue(A, Large, Factor, X.IsNegative() != Y.IsNegative());
								A.Check();
								Out.Swap(Next);
								return;
							}
							auto L = Read(A, X), R = Read(A, Y);
							FInteger Value(A), Rem(A);
							switch (Op)
							{
							case ENumericBinaryOperation::Add:
							{
								Value = Add(L, R);
								break;
							}
							case ENumericBinaryOperation::Subtract:
							{
								Value = Subtract(L, R);
								break;
							}
							case ENumericBinaryOperation::Multiply:
							{
								Value = Multiply(L, R);
								break;
							}
							case ENumericBinaryOperation::Divide:
							{
								DivMod(L, R, Value, Rem);
								break;
							}
							case ENumericBinaryOperation::Remainder:
							{
								DivMod(L, R, Rem, Value);
								break;
							}
							case ENumericBinaryOperation::And:
							case ENumericBinaryOperation::Or:
							case ENumericBinaryOperation::Xor:
							{
								Value = Bitwise(L, R, Op);
								break;
							}
							default:
							{
								Fail(ENumericStatus::InvalidArgument, "binary operation");
							}
							}
							auto Next = Publish(Value);
							A.Check();
							Out.Swap(Next);
						});
	}
	bool TryDivMod(const FBigInteger& X, const FBigInteger& Y, FBigInteger& Q, FBigInteger& R,
				   FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							if (&Q == &R)
							{
								Fail(ENumericStatus::InvalidArgument, "aliased quotient and remainder");
							}
							auto L = Read(A, X), D = Read(A, Y);
							FInteger Quotient(A), Remainder(A);
							DivMod(L, D, Quotient, Remainder);
							auto NQ = Publish(Quotient), NR = Publish(Remainder);
							A.Check();
							Q.Swap(NQ);
							R.Swap(NR);
						});
	}
	bool TryCompare(const FBigInteger& X, const FBigInteger& Y, int& Out, FNumericOperationContext& C,
					FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							auto N = CompareValues(A, X, Y);
							A.Check();
							Out = N;
						});
	}
	bool TryNegate(const FBigInteger& X, FBigInteger& Out, FNumericOperationContext& C,
				   FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							auto R = FNumericAccess::Negate(X);
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryAbs(const FBigInteger& X, FBigInteger& Out, FNumericOperationContext& C,
				FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							A.Input(X);
							auto R = X.IsNegative() ? FNumericAccess::Negate(X) : X;
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryNot(const FBigInteger& X, FBigInteger& Out, FNumericOperationContext& C,
				FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							auto L = Read(A, X);
							L.Negative = !L.Negative && !L.Words.empty();
							auto R = Publish(Subtract(L, Small(A, 1)));
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryShift(const FBigInteger& X, std::int64_t Count, bool Left, FBigInteger& Out,
				  FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							if (Count < 0)
							{
								Fail(ENumericStatus::InvalidArgument, "negative shift");
							}
							auto R =
								Publish(Shift(Read(A, X), static_cast<std::uint64_t>(Count), Left, true));
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryGcd(const FBigInteger& X, const FBigInteger& Y, FBigInteger& Out, FNumericOperationContext& C,
				FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							auto R = Publish(Gcd(Read(A, X), Read(A, Y)));
							A.Check();
							Out.Swap(R);
						});
	}
	bool TryLcm(const FBigInteger& X, const FBigInteger& Y, FBigInteger& Out, FNumericOperationContext& C,
				FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							auto L = Read(A, X), R = Read(A, Y);
							FInteger V(A);
							if (!L.Words.empty() && !R.Words.empty())
							{
								V = Multiply(DivideExact(L, Gcd(Copy(L), Copy(R))), R);
							}
							V.Negative = false;
							auto N = Publish(V);
							A.Check();
							Out.Swap(N);
						});
	}
	bool TryPow(const FBigInteger& X, std::int64_t N, FBigInteger& Out, FNumericOperationContext& C,
				FNumericError& E) noexcept
	{
		return Boundary(C, E,
						[&](FArea& A)
						{
							if (N < 0)
							{
								Fail(ENumericStatus::InvalidArgument, "negative integer exponent");
							}
							auto R = Publish(Pow(Read(A, X), static_cast<std::uint64_t>(N)));
							A.Check();
							Out.Swap(R);
						});
	}
} // namespace VeroScript::Numerics
