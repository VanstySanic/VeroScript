// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericsInternal.h"

namespace VeroScript::Numerics::Detail
{
	namespace
	{
		bool Whitespace(unsigned char C) noexcept
		{
			return C == 32 || (C >= 9 && C <= 13);
		}
		int Digit(unsigned char C) noexcept
		{
			return C >= '0' && C <= '9' ? C - '0'
			    : C >= 'a' && C <= 'z'  ? C - 'a' + 10
			    : C >= 'A' && C <= 'Z'  ? C - 'A' + 10
			                            : -1;
		}
		void CheckInput(FArea& A, std::string_view S)
		{
			A.Text(S.size(), true);
			for (std::size_t I = 0; I < S.size(); I += 4096)
			{
				A.Check((std::min<std::size_t>(4096, S.size() - I) + 63) / 64);
			}
		}
		std::string_view Trim(FArea& A, std::string_view S, std::uint64_t& Offset)
		{
			std::size_t First = 0, Last = S.size();
			while (First < Last && Whitespace(S[First]))
			{
				if (!(First % 4096))
				{
					A.Check(64);
				}
				++First;
			}
			while (Last > First && Whitespace(S[Last - 1]))
			{
				if (!((S.size() - Last) % 4096))
				{
					A.Check(64);
				}
				--Last;
			}
			Offset += First;
			return S.substr(First, Last - First);
		}
		void AppendUInt(FText& Out, std::uint32_t Value, unsigned Width = 0, unsigned Base = 10)
		{
			char Buffer[32];
			unsigned N = 0;
			do
			{
				Buffer[N++] = "0123456789abcdefghijklmnopqrstuvwxyz"[Value % Base];
				Value /= Base;
			}
			while (Value);
			while (N < Width)
			{
				Buffer[N++] = '0';
			}
			while (N)
			{
				Append(Out, Buffer[--N]);
			}
		}
		std::uint64_t Exponent(FArea& A, std::string_view S, std::size_t& I, bool& Negative,
		    std::uint64_t Offset)
		{
			Negative = false;
			if (I < S.size() && (S[I] == '+' || S[I] == '-'))
			{
				Negative = S[I++] == '-';
			}
			auto Start = I;
			std::uint64_t Value = 0;
			auto Limit = static_cast<std::uint64_t>(INT64_MAX) + (Negative ? 1 : 0);
			bool Overflow = false;
			while (I < S.size() && S[I] >= '0' && S[I] <= '9')
			{
				if (!(I % 4096))
				{
					A.Check(64);
				}
				auto D = static_cast<unsigned>(S[I] - '0');
				if (Value > (Limit - D) / 10)
				{
					Overflow = true;
				}
				if (!Overflow)
				{
					Value = Value * 10 + D;
				}
				++I;
			}
			if (Start == I)
			{
				Fail(ENumericStatus::InvalidText, "exponent requires digits", Offset + I);
			}
			if (I != S.size())
			{
				Fail(ENumericStatus::InvalidText, "trailing exponent characters", Offset + I);
			}
			if (Overflow)
			{
				Fail(ENumericStatus::LimitExceeded, "exponent range", Offset + Start);
			}
			return Value;
		}
	} // namespace
	FInteger ParseInteger(FArea& A, std::string_view S, int Base, std::uint64_t Offset)
	{
		if (Base < 2 || Base > 36)
		{
			Fail(ENumericStatus::InvalidArgument, "base");
		}
		CheckInput(A, S);
		S = Trim(A, S, Offset);
		bool Negative = false;
		std::size_t I = 0;
		if (!S.empty() && (S[0] == '+' || S[0] == '-'))
		{
			Negative = S[0] == '-';
			++I;
		}
		if (I == S.size())
		{
			Fail(ENumericStatus::InvalidText, "integer requires digits", Offset + I);
		}
		auto Start = I;
		for (; I < S.size(); ++I)
		{
			if (!(I % 4096))
			{
				A.Check(64);
			}
			auto D = Digit(S[I]);
			if (D < 0 || D >= Base)
			{
				Fail(ENumericStatus::InvalidText, "integer digit", Offset + I);
			}
		}
		while (Start < S.size() && S[Start] == '0')
		{
			if (!(Start % 4096))
			{
				A.Check(64);
			}
			++Start;
		}
		FInteger R(A);
		if (Start == S.size())
		{
			return R;
		}
		if ((Base & (Base - 1)) == 0)
		{
			unsigned Width = 0;
			for (auto N = Base; N > 1; N >>= 1)
			{
				++Width;
			}
			auto First = static_cast<unsigned>(Digit(S[Start]));
			unsigned FirstBits = 0;
			while (First)
			{
				++FirstBits;
				First >>= 1;
			}
			auto Count = static_cast<std::uint64_t>(S.size() - Start - 1) * Width + FirstBits;
			A.Bits(Count);
			Resize(R.Words, static_cast<std::size_t>((Count + 31) / 32));
			std::uint64_t Position = 0;
			for (I = S.size(); I > Start; --I)
			{
				if (!((S.size() - I) % 4096))
				{
					A.Check(64);
				}
				auto D = static_cast<std::uint32_t>(Digit(S[I - 1]));
				auto Word = static_cast<std::size_t>(Position / 32);
				auto Shift = static_cast<unsigned>(Position % 32);
				R.Words[Word] |= D << Shift;
				if (Shift + Width > 32 && Word + 1 < R.Words.size())
				{
					R.Words[Word + 1] |= D >> (32 - Shift);
				}
				Position += Width;
			}
		}
		else
		{
			unsigned Width = 1;
			std::uint32_t Power = Base;
			while (Power <= UINT32_MAX / static_cast<unsigned>(Base))
			{
				Power *= Base;
				++Width;
			}
			I = Start;
			while (I < S.size())
			{
				auto N = std::min<std::size_t>(Width, S.size() - I);
				std::uint32_t Chunk = 0, Multiplier = 1;
				A.Check(1);
				for (std::size_t J = 0; J < N; ++J)
				{
					Chunk = Chunk * Base + Digit(S[I + J]);
					Multiplier *= Base;
				}
				MulSmall(R, Multiplier);
				AddSmall(R, Chunk);
				I += N;
			}
		}
		R.Negative = Negative;
		Normalize(R);
		return R;
	}
	FRational ParseRational(FArea& A, std::string_view S)
	{
		CheckInput(A, S);
		std::uint64_t Offset = 0;
		S = Trim(A, S, Offset);
		if (S.empty())
		{
			Fail(ENumericStatus::InvalidText, "rational requires digits", Offset);
		}
		std::size_t Slash = std::string_view::npos;
		for (std::size_t Begin = 0; Begin < S.size();)
		{
			const auto Count = std::min<std::size_t>(4096, S.size() - Begin);
			A.Check((Count + 63) / 64);
			const auto Found = S.substr(Begin, Count).find('/');
			if (Found != std::string_view::npos)
			{
				Slash = Begin + Found;
				break;
			}
			Begin += Count;
		}
		if (Slash != std::string_view::npos)
		{
			auto N = ParseInteger(A, S.substr(0, Slash), 10, Offset);
			auto D = ParseInteger(A, S.substr(Slash + 1), 10, Offset + Slash + 1);
			if (D.Words.empty())
			{
				auto Begin = Slash + 1;
				while (Begin < S.size() && Whitespace(S[Begin]))
				{
					if (!((Begin - Slash - 1) % 4096))
					{
						A.Check(64);
					}
					++Begin;
				}
				Fail(ENumericStatus::InvalidText, "zero denominator", Offset + Begin);
			}
			return PublishRational(std::move(N), std::move(D));
		}
		bool Negative = false;
		std::size_t I = 0;
		if (S[I] == '+' || S[I] == '-')
		{
			Negative = S[I++] == '-';
		}
		FText Digits{FAllocator<char>(&A)};
		std::uint64_t Fraction = 0;
		bool Dot = false;
		while (I < S.size())
		{
			if (!(I % 4096))
			{
				A.Check(64);
			}
			char C = S[I];
			if (C >= '0' && C <= '9')
			{
				Digits.push_back(C);
				if (Dot)
				{
					++Fraction;
				}
				++I;
			}
			else if (C == '.' && !Dot)
			{
				Dot = true;
				++I;
			}
			else
			{
				break;
			}
		}
		if (Digits.empty())
		{
			Fail(ENumericStatus::InvalidText, "rational requires digits", Offset + I);
		}
		bool NegativeExponent = false;
		std::uint64_t Exp = 0;
		if (I < S.size() && (S[I] == 'e' || S[I] == 'E'))
		{
			++I;
			Exp = Exponent(A, S, I, NegativeExponent, Offset);
		}
		if (I != S.size())
		{
			Fail(ENumericStatus::InvalidText, "trailing rational characters", Offset + I);
		}
		std::size_t Begin = 0, End = Digits.size();
		while (Begin < End && Digits[Begin] == '0')
		{
			if (!(Begin % 4096))
			{
				A.Check(64);
			}
			++Begin;
		}
		if (Begin == End)
		{
			return {};
		}
		while (End > Begin && Digits[End - 1] == '0')
		{
			if (!((Digits.size() - End) % 4096))
			{
				A.Check(64);
			}
			--End;
		}
		auto Trailing = static_cast<std::uint64_t>(Digits.size() - End);
		// Keep scale arithmetic unsigned so even the minimum signed exponent is safe.
		auto Bound = A.Limits.MaxInputBytes + 2 * A.Limits.MaxValueBits + 64;
		if (Exp > Bound)
		{
			Fail(ENumericStatus::LimitExceeded, "decimal exponent");
		}
		const auto Positive = (NegativeExponent ? 0 : Exp) + Trailing;
		const auto NegativeScale = (NegativeExponent ? Exp : 0) + Fraction;
		const auto Count = Positive >= NegativeScale ? Positive - NegativeScale : NegativeScale - Positive;
		if (Count > 2 * A.Limits.MaxValueBits + 64)
		{
			Fail(ENumericStatus::LimitExceeded, "decimal scale");
		}
		auto N = ParseInteger(A, std::string_view(Digits.data() + Begin, End - Begin), 10);
		N.Negative = Negative;
		auto D = Small(A, 1);
		if (Count)
		{
			auto Power = Pow(Small(A, 10), Count);
			if (Positive > NegativeScale)
			{
				N = Multiply(N, Power);
			}
			else
			{
				D = std::move(Power);
			}
		}
		return PublishRational(std::move(N), std::move(D));
	}
	void Append(FText& Out, char C)
	{
		auto& A = *Out.get_allocator().Area;
		A.Text(Out.size() + 1);
		if (!(Out.size() % 64))
		{
			A.Check(1);
		}
		Out.push_back(C);
	}
	void AppendZeros(FText& Out, std::uint64_t Count)
	{
		auto& A = *Out.get_allocator().Area;
		if (Count > A.Limits.MaxOutputBytes - Out.size())
		{
			Fail(ENumericStatus::LimitExceeded, "decimal output");
		}
		while (Count)
		{
			auto N = std::min<std::uint64_t>(Count, 4096);
			A.Check((N + 63) / 64);
			Out.insert(Out.end(), static_cast<std::size_t>(N), '0');
			Count -= N;
		}
	}
	void FormatInteger(const FInteger& V, FText& Out, int Base)
	{
		if (Base < 2 || Base > 36)
		{
			Fail(ENumericStatus::InvalidArgument, "base");
		}
		auto& A = *V.Words.get_allocator().Area;
		if (V.Words.empty())
		{
			Append(Out, '0');
			return;
		}
		if (V.Negative)
		{
			Append(Out, '-');
		}
		if (!(Base & (Base - 1)))
		{
			unsigned Width = 0;
			for (auto N = Base; N > 1; N >>= 1)
			{
				++Width;
			}
			auto Count = (Bits(V) + Width - 1) / Width;
			if (Count > A.Limits.MaxOutputBytes - Out.size())
			{
				Fail(ENumericStatus::LimitExceeded, "integer output");
			}
			for (auto I = Count; I; --I)
			{
				auto Position = (I - 1) * Width;
				auto Word = static_cast<std::size_t>(Position / 32);
				auto Shift = static_cast<unsigned>(Position % 32);
				std::uint32_t D = V.Words[Word] >> Shift;
				if (Shift + Width > 32 && Word + 1 < V.Words.size())
				{
					D |= V.Words[Word + 1] << (32 - Shift);
				}
				Append(Out, "0123456789abcdefghijklmnopqrstuvwxyz"[D & (Base - 1)]);
			}
			return;
		}
		std::uint32_t Power = Base;
		unsigned Width = 1;
		while (Power <= UINT32_MAX / static_cast<unsigned>(Base))
		{
			Power *= Base;
			++Width;
		}
		auto X = Copy(V);
		FWords Chunks{FAllocator<std::uint32_t>(&A)};
		while (!X.Words.empty())
		{
			Chunks.push_back(DivSmall(X, Power));
		}
		for (auto I = Chunks.size(); I; --I)
		{
			AppendUInt(Out, Chunks[I - 1], I == Chunks.size() ? 0 : Width, Base);
		}
	}
	std::string PublishText(const FText& Text)
	{
		auto& A = *Text.get_allocator().Area;
		A.Text(Text.size());
		FReservation Reservation(A, Text.size() + 1);
		std::string R;
		R.reserve(Text.size());
		for (std::size_t I = 0; I < Text.size(); I += 4096)
		{
			auto Count = std::min<std::size_t>(4096, Text.size() - I);
			A.Check((Count + 63) / 64);
			R.append(Text.data() + I, Count);
		}
		A.Check();
		return R;
	}
} // namespace VeroScript::Numerics::Detail

namespace VeroScript::Numerics
{
	using namespace Detail;
	bool TryParse(std::string_view S, FBigInteger& Out, FNumericOperationContext& C, FNumericError& E,
	    int Base) noexcept
	{
		return Boundary(C, E,
		    [&](FArea& A)
		{
			auto R = Publish(ParseInteger(A, S, Base));
			A.Check();
			Out.Swap(R);
		});
	}
	bool TryParse(std::string_view S, FRational& Out, FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
		    [&](FArea& A)
		{
			auto R = ParseRational(A, S);
			A.Check();
			Out.Swap(R);
		});
	}
	bool TryToString(const FBigInteger& X, std::string& Out, FNumericOperationContext& C, FNumericError& E,
	    int Base) noexcept
	{
		return Boundary(C, E,
		    [&](FArea& A)
		{
			FText R{FAllocator<char>(&A)};
			FormatInteger(Read(A, X), R, Base);
			auto N = PublishText(R);
			Out.swap(N);
		});
	}
	bool TryToString(const FRational& X, std::string& Out, FNumericOperationContext& C,
	    FNumericError& E) noexcept
	{
		return Boundary(C, E,
		    [&](FArea& A)
		{
			A.Input(X);
			FText R{FAllocator<char>(&A)};
			FormatInteger(Read(A, X.Numerator()), R);
			if (!X.IsInteger())
			{
				Append(R, '/');
				FormatInteger(Read(A, X.Denominator()), R);
			}
			auto N = PublishText(R);
			Out.swap(N);
		});
	}
	bool TryToDecimal(const FRational& X, std::int64_t Digits, ENumericRoundingMode Mode, std::string& Out,
	    FNumericOperationContext& C, FNumericError& E) noexcept
	{
		return Boundary(C, E,
		    [&](FArea& A)
		{
			A.Input(X);
			if (Digits < 0 ||
			    static_cast<unsigned>(Mode) >
			        static_cast<unsigned>(ENumericRoundingMode::HalfAwayFromZero))
			{
				Fail(ENumericStatus::InvalidArgument, "decimal arguments");
			}
			if (static_cast<std::uint64_t>(Digits) >= A.Limits.MaxOutputBytes)
			{
				Fail(ENumericStatus::LimitExceeded, "decimal output");
			}
			auto N = Read(A, X.Numerator()), D = Read(A, X.Denominator());
			N.Negative = false;
			FInteger Q(A), R(A);
			DivMod(N, D, Q, R);
			FText Text{FAllocator<char>(&A)};
			FormatInteger(Q, Text);
			if (Digits)
			{
				Append(Text, '.');
			}
			std::uint64_t Left = static_cast<std::uint64_t>(Digits);
			while (Left)
			{
				if (R.Words.empty())
				{
					AppendZeros(Text, Left);
					break;
				}
				auto Width = static_cast<unsigned>(std::min<std::uint64_t>(Left, 9));
				std::uint32_t Power = 1;
				for (unsigned I = 0; I < Width; ++I)
				{
					Power *= 10;
				}
				MulSmall(R, Power);
				FInteger Chunk(A), Next(A);
				DivMod(R, D, Chunk, Next);
				AppendUInt(Text, Chunk.Words.empty() ? 0 : Chunk.Words[0], Width);
				R = std::move(Next);
				Left -= Width;
			}
			if (RoundUp(R, D, X.IsNegative(), (Text.back() - '0') & 1, Mode))
			{
				bool Carry = true;
				for (std::size_t I = Text.size(); I && Carry; --I)
				{
					if (!((Text.size() - I) % 4096))
					{
						A.Check(64);
					}
					if (Text[I - 1] == '.')
					{
						continue;
					}
					if (Text[I - 1] == '9')
					{
						Text[I - 1] = '0';
					}
					else
					{
						++Text[I - 1];
						Carry = false;
					}
				}
				if (Carry)
				{
					FText Next{FAllocator<char>(&A)};
					Append(Next, '1');
					for (auto Ch : Text)
					{
						Append(Next, Ch);
					}
					Text = std::move(Next);
				}
			}
			bool Nonzero = false;
			for (std::size_t I = 0; I < Text.size(); ++I)
			{
				if (!(I % 4096))
				{
					A.Check(64);
				}
				if (Text[I] >= '1' && Text[I] <= '9')
				{
					Nonzero = true;
					break;
				}
			}
			if (X.IsNegative() && Nonzero)
			{
				FText Signed{FAllocator<char>(&A)};
				Append(Signed, '-');
				for (auto Ch : Text)
				{
					Append(Signed, Ch);
				}
				Text = std::move(Signed);
			}
			auto Result = PublishText(Text);
			Out.swap(Result);
		});
	}
} // namespace VeroScript::Numerics
