// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumerics.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <cstdlib>
#include <atomic>
#include <thread>

namespace
{
	thread_local std::int64_t AllocationCountdown = -1;
}
void* operator new(std::size_t Size)
{
	if (AllocationCountdown == 0)
	{
		throw std::bad_alloc();
	}
	if (AllocationCountdown > 0)
	{
		--AllocationCountdown;
	}
	if (auto P = std::malloc(Size ? Size : 1))
	{
		return P;
	}
	throw std::bad_alloc();
}
void operator delete(void* P) noexcept
{
	std::free(P);
}
void operator delete(void* P, std::size_t) noexcept
{
	std::free(P);
}
void* operator new[](std::size_t Size)
{
	return ::operator new(Size);
}
void operator delete[](void* P) noexcept
{
	std::free(P);
}
void operator delete[](void* P, std::size_t) noexcept
{
	std::free(P);
}

using namespace VeroScript;
using namespace VeroScript::Numerics;

namespace
{
	unsigned Checks = 0;
	void Expect(bool Value, const char* Message)
	{
		++Checks;
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	FNumericOperationContext Context;
	FNumericError Error;
	void Require(bool Ok)
	{
		if (!Ok)
		{
			throw std::runtime_error(std::string(Error.MessageId ? Error.MessageId : "numeric error") + ": " +
			    (Error.Reason ? Error.Reason : "") + " at " +
			    std::to_string(Error.Offset));
		}
	}
	FBigInteger Big(std::string_view S)
	{
		FBigInteger R;
		Require(TryParse(S, R, Context, Error));
		return R;
	}
	FRational Rat(std::string_view S)
	{
		FRational R;
		Require(TryParse(S, R, Context, Error));
		return R;
	}
	template <class T>
	std::string Text(const T& X)
	{
		std::string R;
		Require(TryToString(X, R, Context, Error));
		return R;
	}
	template <class T>
	T Binary(ENumericBinaryOperation Op, const T& X, const T& Y)
	{
		T R;
		Require(TryBinary(Op, X, Y, R, Context, Error));
		return R;
	}
	void SharedHashes()
	{
		const auto Value = Binary(ENumericBinaryOperation::Multiply,
		    Big("123456789012345678901234567890123456789012345678901234567890"), Big("-37"));
		const auto Independent = Big(Text(Value));
		const auto Expected = Independent.Hash();
		std::atomic<bool> Correct{true};
		std::vector<std::thread> Readers;
		for (unsigned Index = 0; Index < 8; ++Index)
		{
			Readers.emplace_back([&]()
			{
				for (unsigned Iteration = 0; Iteration < 256; ++Iteration)
				{
					if (Value.Hash() != Expected || !(Value == Independent))
					{
						Correct.store(false, std::memory_order_relaxed);
					}
				}
			});
		}
		for (auto& Reader : Readers)
		{
			Reader.join();
		}
		Expect(Correct.load(), "shared immutable numeric hash changed across readers");
		Expect(Value == Big(Text(Value)), "unhashed equivalent integer comparison");
	}

	void Values()
	{
		static_assert(std::is_nothrow_default_constructible_v<FBigInteger> &&
		    std::is_nothrow_copy_constructible_v<FBigInteger> &&
		    std::is_nothrow_move_constructible_v<FBigInteger> &&
		    std::is_nothrow_copy_assignable_v<FBigInteger> &&
		    std::is_nothrow_move_assignable_v<FBigInteger>);
		static_assert(std::is_nothrow_default_constructible_v<FRational> &&
		    std::is_nothrow_copy_constructible_v<FRational> &&
		    std::is_nothrow_move_constructible_v<FRational> &&
		    std::is_nothrow_copy_assignable_v<FRational> &&
		    std::is_nothrow_move_assignable_v<FRational>);
		FBigInteger Z;
		FRational R;
		Expect(Z.IsZero() && R.IsZero() && R.Denominator() == Big("1"), "default zeros");
		auto X = Big("18446744073709551616"), Copy = X;
		auto Moved = std::move(X);
		Expect(X.IsZero() && Moved == Copy && Copy.Hash() == Moved.Hash(), "integer move and value copy");
		Moved = std::move(Moved);
		Expect(Moved == Copy, "self move");
		auto Q = Rat("-2/-4"), QCopy = Q;
		auto QMoved = std::move(Q);
		Expect(Q.IsZero() && QMoved == QCopy && Text(QCopy) == "1/2", "rational normalization and move");
		QMoved = std::move(QMoved);
		Expect(QMoved == QCopy && QCopy.Hash() == Rat("2/4").Hash(), "rational self move and hash");
		Expect(Text(Big(" -0000 ")) == "0", "negative zero");
	}
	void Arithmetic()
	{
		std::mt19937_64 Random(0x4568549);
		for (unsigned I = 0; I < 500; ++I)
		{
			auto X = static_cast<std::int64_t>(Random() % 2000000000) - 1000000000;
			auto Y = static_cast<std::int64_t>(Random() % 2000000000) - 1000000000;
			if (!Y)
			{
				Y = 1;
			}
			auto A = Big(std::to_string(X)), B = Big(std::to_string(Y));
			for (auto Op : {ENumericBinaryOperation::Add, ENumericBinaryOperation::Subtract,
			         ENumericBinaryOperation::Multiply, ENumericBinaryOperation::Divide,
			         ENumericBinaryOperation::Remainder, ENumericBinaryOperation::And,
			         ENumericBinaryOperation::Or, ENumericBinaryOperation::Xor})
			{
				std::int64_t Expected = Op == ENumericBinaryOperation::Add ? X + Y
				    : Op == ENumericBinaryOperation::Subtract              ? X - Y
				    : Op == ENumericBinaryOperation::Multiply              ? X * Y
				    : Op == ENumericBinaryOperation::Divide                ? X / Y
				    : Op == ENumericBinaryOperation::Remainder             ? X % Y
				    : Op == ENumericBinaryOperation::And                   ? X & Y
				    : Op == ENumericBinaryOperation::Or                    ? X | Y
				                                                           : X ^ Y;
				Expect(Text(Binary(Op, A, B)) == std::to_string(Expected), "integer differential arithmetic");
			}
		}
		for (auto A : {"18446744073709551616", "-340282366920938463463374607431768211455",
		         "79228162514264337593543950335"})
		{
			for (auto B : {"4294967296", "-18446744073709551617", "9223372036854775809"})
			{
				auto X = Big(A), Y = Big(B);
				FBigInteger Q, R;
				Require(TryDivMod(X, Y, Q, R, Context, Error));
				Expect(Binary(ENumericBinaryOperation::Add, Binary(ENumericBinaryOperation::Multiply, Q, Y),
				           R) == X,
				    "multi-limb division identity");
			}
		}
		const auto B = Big("4294967296"), D = Big("39614081257132168826836754435");
		auto V = Binary(
		    ENumericBinaryOperation::Subtract,
		    Binary(ENumericBinaryOperation::Multiply, D, Binary(ENumericBinaryOperation::Add, B, Big("1"))),
		    Big("1"));
		FBigInteger Q, R;
		Require(TryDivMod(V, D, Q, R, Context, Error));
		Expect(Q == B && R == Binary(ENumericBinaryOperation::Subtract, D, Big("1")),
		    "Algorithm D add-back regression");
		Require(TryShift(Big("-3"), 1, false, R, Context, Error));
		Expect(Text(R) == "-2", "negative arithmetic right shift");
		Require(TryShift(Big("-3"), INT64_MAX, false, R, Context, Error));
		Expect(Text(R) == "-1", "huge right shift");
		Require(TryNot(Big("-3"), R, Context, Error));
		Expect(Text(R) == "2", "infinite complement");
		Require(TryGcd(Big("-60"), Big("24"), R, Context, Error));
		Expect(Text(R) == "12", "gcd");
		Require(TryLcm(Big("-60"), Big("24"), R, Context, Error));
		Expect(Text(R) == "120", "lcm");
		Require(TryPow(Big("7"), 80, R, Context, Error));
		Expect(Text(R) == "40536215597144386832065866109016673800875222251012083746192454448001",
		    "integer power");
		Expect(Text(Binary(ENumericBinaryOperation::Add, Rat("1/3"), Rat("1/6"))) == "1/2",
		    "rational addition");
		Expect(Text(Binary(ENumericBinaryOperation::Remainder, Rat("-7/3"), Rat("2/3"))) == "-1/3",
		    "rational remainder");
		FRational P;
		Require(TryPow(Rat("-1"), INT64_MIN, P, Context, Error));
		Expect(Text(P) == "1", "minimum rational exponent");
	}
	void Conversion()
	{
		for (int Base = 2; Base <= 36; ++Base)
		{
			auto X = Big("-340282366920938463463374607431768211455");
			std::string S;
			Require(TryToString(X, S, Context, Error, Base));
			FBigInteger Y;
			Require(TryParse(S, Y, Context, Error, Base));
			Expect(X == Y, "base roundtrip");
		}
		Expect(Text(Rat(" 1.2000e-3 ")) == "3/2500", "exact decimal parse");
		Expect(Text(Rat("0e9223372036854775807")) == "0", "zero huge exponent");
		std::string S;
		Require(TryToDecimal(Rat("-1/200"), 2, ENumericRoundingMode::HalfEven, S, Context, Error));
		Expect(S == "0.00", "rounded zero has no sign");
		Require(TryToDecimal(Rat("9999/1000"), 2, ENumericRoundingMode::HalfEven, S, Context, Error));
		Expect(S == "10.00", "decimal carry");
		Require(TryToDecimal(Rat("1/3"), 30, ENumericRoundingMode::HalfEven, S, Context, Error));
		Expect(S == "0.333333333333333333333333333333", "repeating decimal");
		FBigInteger B;
		Require(TryRound(Rat("-5/2"), ENumericRoundingMode::HalfEven, B, Context, Error));
		Expect(Text(B) == "-2", "negative half even");
		Require(TryRound(Rat("-5/2"), ENumericRoundingMode::HalfAwayFromZero, B, Context, Error));
		Expect(Text(B) == "-3", "negative half away");
		const char* Positive[] = {"2", "2", "3", "3", "2", "3"};
		const char* Negative[] = {"-2", "-3", "-2", "-3", "-2", "-3"};
		for (unsigned I = 0; I < 6; ++I)
		{
			const auto Mode = static_cast<ENumericRoundingMode>(I);
			Require(TryRound(Rat("5/2"), Mode, B, Context, Error));
			Expect(Text(B) == Positive[I], "positive rounding mode");
			Require(TryRound(Rat("-5/2"), Mode, B, Context, Error));
			Expect(Text(B) == Negative[I], "negative rounding mode");
			Require(TryToDecimal(Rat("-5/2"), 0, Mode, S, Context, Error));
			Expect(S == Negative[I], "decimal rounding mode");
		}
		FRational F;
		Require(TryFromDouble(0.1, F, Context, Error));
		Expect(Text(F) == "3602879701896397/36028797018963968", "exact stored floating value");
		double D = 0;
		Require(TryToDouble(F, D, true, Context, Error));
		Expect(D == 0.1, "exact floating roundtrip");
		Require(TryToDouble(Big("9007199254740993"), D, false, Context, Error));
		Expect(D == 9007199254740992.0, "binary64 tie even");
		float Single = 0;
		Require(TryToFloat(Big("16777217"), Single, false, Context, Error));
		Expect(Single == 16777216.0f, "binary32 tie even");
		for (double Input : {std::numeric_limits<double>::max(), std::numeric_limits<double>::min(),
		         std::numeric_limits<double>::denorm_min(), -0.1, -1.0, 0.0})
		{
			Require(TryFromDouble(Input, F, Context, Error));
			Require(TryToDouble(F, D, true, Context, Error));
			Expect(D == Input, "floating extremes exact roundtrip");
		}
		std::int64_t Signed = 0;
		std::uint64_t Unsigned = 0;
		Require(TryToInt64(Big("-9223372036854775808"), Signed, Context, Error));
		Expect(Signed == INT64_MIN, "int64 minimum");
		Require(TryToUInt64(Big("18446744073709551615"), Unsigned, Context, Error));
		Expect(Unsigned == UINT64_MAX, "uint64 maximum");
	}
	void UnbalancedMultiplication()
	{
		const auto Large = Big(std::string(3000, '9'));
		const auto Three = Big("3");
		const auto Expected = Big("2" + std::string(2999, '9') + "7");
		std::uint64_t PreviousUnits = 0;
		for (const bool Reverse : {false, true})
		{
			std::uint64_t Units = 0;
			FNumericOperationContext Measured;
			Measured.Work = {[](void* State, const std::uint64_t Count)
			{
				Expect(Count <= 1024, "bounded multiplication work chunk");
				*static_cast<std::uint64_t*>(State) += Count;
				return true;
			}, &Units};
			auto Result = Big("7");
			Require(TryBinary(ENumericBinaryOperation::Multiply, Reverse ? Three : Large,
			    Reverse ? Large : Three, Result, Measured, Error));
			Expect(Result == Expected, "large times small product");
			if (Reverse)
			{
				Expect(Units == PreviousUnits, "multiplication operand order preserves work units");
			}
			PreviousUnits = Units;
			Measured = {};
			Measured.Limits.MaxWorkUnits = Units - 1;
			Result = Big("7");
			Expect(!TryBinary(ENumericBinaryOperation::Multiply, Reverse ? Three : Large,
			           Reverse ? Large : Three, Result, Measured, Error) &&
			        Error.Status == ENumericStatus::LimitExceeded && Result == Big("7"),
			    "multiplication work limit preserves output");
		}
	}

	void DirectNumericResults()
	{
		const auto Base = Big("4294967296");
		for (const auto& Integer : {Big("18446744073709551616"), Big("79228162514264337593543950335"),
		         Big("-79228162514264337593543950335"), Big(std::string(3000, '9'))})
		{
			for (const auto& Factor : {Big("0"), Big("1"), Big("-1"), Big("3"), Big("-3"),
			         Big("4294967295"), Big("-4294967295")})
			{
				FBigInteger Magnitude;
				Require(TryAbs(Factor, Magnitude, Context, Error));
				// Both products use multi-limb factors, providing an independent
				// arithmetic path for the direct single-limb result and its hash.
				auto Expected = Binary(ENumericBinaryOperation::Subtract,
				    Binary(ENumericBinaryOperation::Multiply, Integer,
				        Binary(ENumericBinaryOperation::Add, Base, Magnitude)),
				    Binary(ENumericBinaryOperation::Multiply, Integer, Base));
				if (Factor.IsNegative())
				{
					Require(TryNegate(Expected, Expected, Context, Error));
				}
				for (const bool Reverse : {false, true})
				{
					auto Left = Reverse ? Factor : Integer;
					const auto Right = Reverse ? Integer : Factor;
					Require(TryBinary(ENumericBinaryOperation::Multiply, Left, Right, Left, Context, Error));
					Expect(Left == Expected && Left.Hash() == Expected.Hash(),
					    "single-limb multiplication preserves signs, carries, aliasing and value hash");
				}
			}
		}

		const auto Large = Big(std::string(3000, '9')), Three = Big("3"), Seed = Big("7");
		for (const auto& Factor : {Big("0"), Big("1"), Big("-1")})
		{
			FBigInteger Result;
			AllocationCountdown = 0;
			const bool Success = TryBinary(ENumericBinaryOperation::Multiply, Large, Factor,
			    Result, Context, Error);
			AllocationCountdown = -1;
			Expect(Success && (Factor.IsZero() ? Result.IsZero() : Result.BitLength() == Large.BitLength()),
			    "zero and unit factors reuse immutable magnitudes without allocation");
		}
		for (std::int64_t Failure = 0; Failure < 2; ++Failure)
		{
			auto Result = Seed;
			AllocationCountdown = Failure;
			const bool Success = TryBinary(ENumericBinaryOperation::Multiply, Large, Three,
			    Result, Context, Error);
			AllocationCountdown = -1;
			Expect(!Success && Error.Status == ENumericStatus::OutOfMemory && Result == Seed,
			    "direct multiplication allocation failure preserves output");
		}
		FNumericOperationContext Bounded;
		Bounded.Limits.MaxScratchBytes = 1;
		auto Result = Seed;
		Expect(!TryBinary(ENumericBinaryOperation::Multiply, Large, Three, Result, Bounded, Error) && Error.Status == ENumericStatus::LimitExceeded && Result == Seed,
		    "direct immutable result remains charged to the scratch limit");
		Bounded = {};
		Bounded.Limits.MaxValueBits = 65;
		Expect(!TryBinary(ENumericBinaryOperation::Multiply, Big("18446744073709551616"), Three,
		           Result, Bounded, Error) &&
		        Error.Status == ENumericStatus::LimitExceeded && Result == Seed,
		    "direct multiplication enforces final result bit limit");
		Expect(!TryBinary(ENumericBinaryOperation::Multiply, Large, Big("0"), Result, Bounded, Error) && Error.Status == ENumericStatus::LimitExceeded && Result == Seed,
		    "zero factor shortcut still validates the other input");
		std::uint64_t Charged = 0;
		Bounded = {};
		Bounded.Work = {[](void* State, const std::uint64_t Units)
		{
			Expect(Units <= 1024, "direct multiplication bounds cancellation work chunks");
			auto& Total = *static_cast<std::uint64_t*>(State);
			Total += Units;
			return Total < 100;
		}, &Charged};
		Expect(!TryBinary(ENumericBinaryOperation::Multiply, Large, Three, Result, Bounded, Error) && Error.Status == ENumericStatus::Interrupted && Charged >= 100 && Result == Seed,
		    "direct multiplication cancellation preserves output");

		Expect(Binary(ENumericBinaryOperation::Add, Rat("1/6"), Rat("1/6")) == Rat("1/3"),
		    "same-denominator addition reduces its result");
		Expect(Binary(ENumericBinaryOperation::Subtract, Rat("1/6"), Rat("5/6")) == Rat("-2/3"),
		    "same-denominator subtraction reduces a negative result");
		Expect(Binary(ENumericBinaryOperation::Add, Rat("18446744073709551616"), Rat("-1")) == Rat("18446744073709551615"), "integral rational addition preserves large numerators");
		auto Fraction = Rat("1/18446744073709551616");
		Require(TryBinary(ENumericBinaryOperation::Add, Fraction, Fraction, Fraction, Context, Error));
		Expect(Fraction == Rat("1/9223372036854775808"),
		    "same-denominator addition supports large denominators and aliased output");
		Require(TryBinary(ENumericBinaryOperation::Subtract, Fraction, Fraction, Fraction, Context, Error));
		Expect(Fraction.IsZero() && Fraction.Denominator() == Big("1"),
		    "same-denominator subtraction produces canonical zero");
	}

	void NativeNumericFastPaths()
	{
		for (unsigned Bit = 0; Bit < 64; ++Bit)
		{
			const auto Magnitude = std::uint64_t{1} << Bit;
			FBigInteger Value;
			Require(TryFromUInt64(Magnitude, Value, Context, Error));
			Expect(Value.BitLength() == Bit + 1, "native integer bit-length boundary");
			Require(TryFromUInt64(Magnitude - 1, Value, Context, Error));
			Expect(Value.BitLength() == Bit, "native integer bit-length lower boundary");
		}

		const auto Large = Big("18446744073709551616");
		for (const auto& X : {Big("0"), Big("1"), Big("-1"), Large, Big("-18446744073709551616")})
		{
			for (const auto& Y : {Big("0"), Big("1"), Big("-1"), Large, Big("-18446744073709551616")})
			{
				int Compared = 7;
				Require(TryCompare(X, Y, Compared, Context, Error));
				const auto Difference = Binary(ENumericBinaryOperation::Subtract, X, Y);
				Expect(Compared == Difference.Sign(), "immutable comparison matches subtraction sign");
				auto Aliased = Y;
				Require(TryBinary(ENumericBinaryOperation::Subtract, X, Aliased, Aliased, Context, Error));
				Expect(Aliased == Difference, "subtraction allows output to alias the right input");
				Expect(Binary(ENumericBinaryOperation::Add, Difference, Y) == X,
				    "signed subtraction preserves addition identity");
			}
		}

		FBigInteger Wide;
		Require(TryShift(Big("1"), 32768, true, Wide, Context, Error));
		FNumericOperationContext Bounded;
		Bounded.Limits.MaxScratchBytes = 1;
		int Compared = 7;
		AllocationCountdown = 0;
		const bool ComparedWithoutAllocation = TryCompare(Wide, Wide, Compared, Bounded, Error);
		AllocationCountdown = -1;
		Expect(ComparedWithoutAllocation && Compared == 0,
		    "immutable comparison neither allocates nor copies input magnitudes");
		Bounded.Limits.MaxWorkUnits = 1024;
		Compared = 7;
		Expect(!TryCompare(Wide, Wide, Compared, Bounded, Error) && Error.Status == ENumericStatus::LimitExceeded && Compared == 7,
		    "immutable comparison work limit preserves output");
		Bounded = {};
		Bounded.Limits.MaxValueBits = 64;
		Expect(!TryCompare(Wide, Big("0"), Compared, Bounded, Error) && Error.Status == ENumericStatus::LimitExceeded && Compared == 7,
		    "immutable comparison validates inputs before sign or size shortcuts");
		std::uint64_t Charged = 0;
		Bounded = {};
		Bounded.Work = {[](void* State, std::uint64_t Units)
		{
			auto& Total = *static_cast<std::uint64_t*>(State);
			Total += Units;
			return Total < 1025;
		}, &Charged};
		Expect(!TryCompare(Wide, Wide, Compared, Bounded, Error) && Error.Status == ENumericStatus::Interrupted && Charged >= 1025 && Compared == 7,
		    "immutable comparison cancellation preserves output");

		std::mt19937_64 Random(0xC0F31E);
		for (unsigned Index = 0; Index < 200; ++Index)
		{
			const auto X = Random(), Y = Random();
			FBigInteger Left, Right, Result;
			Require(TryFromUInt64(X, Left, Context, Error));
			Require(TryFromUInt64(Y, Right, Context, Error));
			Require(TryGcd(Left, Right, Result, Context, Error));
			std::uint64_t Native;
			Require(TryToUInt64(Result, Native, Context, Error));
			Expect(Native == std::gcd(X, Y), "native-word gcd differential");
		}
		for (const auto& X : {Big("0"), Big("-18446744073709551615"), Wide})
		{
			FBigInteger Result;
			Require(TryGcd(X, Big("0"), Result, Context, Error));
			FBigInteger Absolute;
			Require(TryAbs(X, Absolute, Context, Error));
			Expect(Result == Absolute, "gcd with zero remains a nonnegative magnitude");
			const auto Scaled = Binary(ENumericBinaryOperation::Multiply, X, Big("1000003"));
			Require(TryGcd(Scaled, Big("1000003"), Result, Context, Error));
			Expect(Result == Big("1000003"), "large by single-word gcd exact factor");
			Require(TryGcd(Big("1000003"), Scaled, Result, Context, Error));
			Expect(Result == Big("1000003"), "single-word gcd operand symmetry");
		}
		FBigInteger Result = Big("7");
		Bounded = {};
		Charged = 0;
		Bounded.Work = {[](void* State, std::uint64_t Units)
		{
			auto& Total = *static_cast<std::uint64_t*>(State);
			Total += Units;
			return Total < 9;
		}, &Charged};
		Expect(!TryGcd(Large, Big("3"), Result, Bounded, Error) && Error.Status == ENumericStatus::Interrupted && Charged >= 9 && Result == Big("7"),
		    "single-word gcd cancellation preserves output");
		Charged = 0;
		Expect(!TryGcd(Big("18446744073709551615"), Big("12200160415121876738"), Result,
		           Bounded, Error) &&
		        Error.Status == ENumericStatus::Interrupted && Charged >= 9 && Result == Big("7"),
		    "native-word gcd cancellation preserves output");

		for (const auto& Fraction : {Rat("0"), Rat("-7/13"), Rat("18446744073709551616/17")})
		{
			Expect(Fraction.Hash() == (Fraction.Numerator().Hash() * 1099511628211ull ^ Fraction.Denominator().Hash()), "rational hash retains canonical value contract");
			FRational Rebuilt;
			Require(TryMakeRational(Fraction.Numerator(), Fraction.Denominator(), Rebuilt, Context, Error));
			Expect(Rebuilt == Fraction, "coprime rational publication preserves normalized values");
		}
	}

	void Failures()
	{
		auto B = Big("7");
		auto R = Rat("2/3");
		Expect(!TryParse("1/0", R, Context, Error) && Error.Status == ENumericStatus::InvalidText &&
		        Text(R) == "2/3",
		    "text zero denominator preserves output");
		Expect(!TryParse("0xFF", B, Context, Error, 16) && Error.Status == ENumericStatus::InvalidText &&
		        Text(B) == "7",
		    "strict base parse");
		Expect(!TryParse("1e9223372036854775808", R, Context, Error) &&
		        Error.Status == ENumericStatus::LimitExceeded,
		    "exponent range");
		Expect(!TryBinary(ENumericBinaryOperation::Divide, B, Big("0"), B, Context, Error) &&
		        Error.Status == ENumericStatus::DivisionByZero && Text(B) == "7",
		    "division output transaction");
		Expect(!TryDivMod(B, Big("2"), B, B, Context, Error) &&
		        Error.Status == ENumericStatus::InvalidArgument && Text(B) == "7",
		    "divmod output alias");
		FNumericOperationContext Small;
		Small.Limits.MaxValueBits = 8;
		Expect(!TryParse("256", B, Small, Error) && Error.Status == ENumericStatus::LimitExceeded &&
		        Text(B) == "7",
		    "value bit limit");
		Require(TryParse("255", B, Small, Error));
		Expect(Text(B) == "255", "value bit boundary");
		Small.Limits.MaxScratchBytes = 1;
		Expect(!TryParse("1234", B, Small, Error) && Error.Status == ENumericStatus::LimitExceeded,
		    "scratch allocation limit");
		Small = {};
		Small.Work = {[](void*, std::uint64_t)
		{
			return false;
		}, nullptr};
		Expect(!TryParse("1234", B, Small, Error) && Error.Status == ENumericStatus::Interrupted,
		    "cancellation");
		double D = 5;
		Expect(!TryToDouble(Big("9007199254740993"), D, true, Context, Error) &&
		        Error.Status == ENumericStatus::InexactConversion && D == 5,
		    "exact conversion preserves output");
		Expect(!TryToDouble(Rat("1/3"), D, true, Context, Error) &&
		        Error.Status == ENumericStatus::InexactConversion,
		    "inexact fraction");
		std::uint64_t Charged = 0;
		FNumericOperationContext Cancel;
		Cancel.Work = {[](void* State, std::uint64_t Units)
		{
			auto& Total = *static_cast<std::uint64_t*>(State);
			Total += Units;
			return Total < 1000;
		}, &Charged};
		auto Preserved = Big("7");
		Expect(!TryPow(Big("3"), 20000, Preserved, Cancel, Error) && Error.Status == ENumericStatus::Interrupted && Charged >= 1000 && Preserved == Big("7"), "mid-operation cancellation preserves output");
	}
	void LongRationalText()
	{
		for (const std::size_t Padding : {4094u, 4095u, 4096u})
		{
			const auto Numerator = std::string(Padding, '0') + "1/";
			Expect(Text(Rat(Numerator + "2")) == "1/2", "fraction separator at text block boundary");
			const auto Invalid = Numerator + std::string(4097, ' ') + "0";
			auto Preserved = Rat("2/3");
			const bool Parsed = TryParse(Invalid, Preserved, Context, Error);
			Expect(!Parsed && Error.Status == ENumericStatus::InvalidText &&
			        Error.Offset == Invalid.size() - 1 && Preserved == Rat("2/3"),
			    "long zero denominator reports source offset and preserves output");
		}
		const auto Input = std::string(12288, '0') + "1/2";
		auto Preserved = Rat("2/3");
		std::uint64_t Charged = 0;
		FNumericOperationContext Cancel;
		Cancel.Work =
		    {[](void* State, std::uint64_t Units)
		{
			auto& Total = *static_cast<std::uint64_t*>(State);
			Total += Units;
			return Total < 300;
		},
		        &Charged};
		Expect(!TryParse(Input, Preserved, Cancel, Error) &&
		        Error.Status == ENumericStatus::Interrupted && Charged >= 300 &&
		        Preserved == Rat("2/3"),
		    "long rational text cancellation preserves output");
		Expect(Text(Rat(std::string(12288, '0') + ".5")) == "1/2",
		    "long decimal text without fraction separator");
	}
	void FloatingIntegerInputs()
	{
		FNumericOperationContext Narrow;
		Narrow.Limits.MaxValueBits = 1;
		FBigInteger Integer;
		Require(TryFromDouble(std::numeric_limits<double>::denorm_min(), Integer, false, Narrow, Error));
		Expect(Integer.IsZero(), "tiny floating input truncates without a huge denominator");
		Require(TryFromDouble(-1.9, Integer, false, Narrow, Error));
		Expect(Text(Integer) == "-1", "narrow context checks truncated value size");
		std::mt19937_64 Random(0xF10A7);
		for (unsigned I = 0; I < 1000; ++I)
		{
			const auto Bits = Random();
			double Input;
			std::memcpy(&Input, &Bits, sizeof(Input));
			if (!std::isfinite(Input))
			{
				continue;
			}
			Require(TryFromDouble(Input, Integer, false, Context, Error));
			double Result;
			Require(TryToDouble(Integer, Result, true, Context, Error));
			Expect(Result == std::trunc(Input), "floating to integer truncation differential");
		}
	}
	void AllocationFailures()
	{
		auto Seed = Big("1234567890123456789012345678901234567890");
		auto Fraction = Rat("1234567890123456789012345678901234567890/7919");
		AllocationCountdown = 0;
		{
			auto Copy = Seed;
			auto Moved = std::move(Copy);
			Copy = Moved;
			Moved.Swap(Copy);
			auto R = Fraction;
			auto S = std::move(R);
			R = S;
			R.Swap(S);
		}
		AllocationCountdown = -1;
		for (unsigned Operation = 0; Operation < 3; ++Operation)
		{
			bool Completed = false;
			for (unsigned Failure = 0; Failure < 300; ++Failure)
			{
				auto IntegerOutput = Seed;
				auto RationalOutput = Fraction;
				std::string TextOutput = "unchanged";
				AllocationCountdown = Failure;
				const bool Success = Operation == 0 ? TryPow(Seed, 4, IntegerOutput, Context, Error)
				    : Operation == 1
				    ? TryBinary(ENumericBinaryOperation::Add, Fraction, Fraction,
				          RationalOutput, Context, Error)
				    : TryToDecimal(Fraction, 40, ENumericRoundingMode::HalfEven,
				          TextOutput, Context, Error);
				AllocationCountdown = -1;
				if (Success)
				{
					Completed = true;
					break;
				}
				Expect(Error.Status == ENumericStatus::OutOfMemory, "allocation failure reports OOM");
				Expect(IntegerOutput == Seed && RationalOutput == Fraction && TextOutput == "unchanged",
				    "allocation failure preserves outputs");
			}
			Expect(Completed, "allocation injection reaches successful operation");
		}
	}
	void Oracle()
	{
		std::string Line;
		while (std::getline(std::cin, Line))
		{
			try
			{
				std::istringstream Input(Line);
				std::string Kind, X, Y;
				unsigned Op;
				Input >> Kind >> Op >> X >> Y;
				if (Kind == "b")
				{
					std::cout << Text(Binary(static_cast<ENumericBinaryOperation>(Op), Big(X), Big(Y)))
					          << '\n';
				}
				else if (Kind == "d" || Kind == "f")
				{
					if (Kind == "d")
					{
						double V;
						Require(TryToDouble(Rat(X), V, false, Context, Error));
						std::uint64_t Bits;
						std::memcpy(&Bits, &V, sizeof(Bits));
						std::cout << Bits << '\n';
					}
					else
					{
						float V;
						Require(TryToFloat(Rat(X), V, false, Context, Error));
						std::uint32_t Bits;
						std::memcpy(&Bits, &V, sizeof(Bits));
						std::cout << Bits << '\n';
					}
				}
				else
				{
					std::cout << Text(Binary(static_cast<ENumericBinaryOperation>(Op), Rat(X), Rat(Y)))
					          << '\n';
				}
			}
			catch (const std::exception& E)
			{
				std::cout << "ERROR " << E.what() << '\n';
			}
		}
	}
} // namespace
int main(int Argc, char**)
{
	try
	{
		if (Argc > 1)
		{
			Oracle();
			return 0;
		}
		Values();
		SharedHashes();
		Arithmetic();
		UnbalancedMultiplication();
		DirectNumericResults();
		NativeNumericFastPaths();
		Conversion();
		Failures();
		LongRationalText();
		AllocationFailures();
		FloatingIntegerInputs();
		std::cout << Checks << " numeric checks passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << "Numeric test failure after " << Checks << " checks: " << E.what() << '\n';
		return 1;
	}
}
