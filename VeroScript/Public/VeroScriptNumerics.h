// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace VeroScript
{
	struct FNumericAccess;
	struct FBigIntegerData;
	struct FRationalData;

	enum class ENumericStatus : std::uint8_t
	{
		Ok,
		InvalidArgument,
		InvalidText,
		DivisionByZero,
		OutOfRange,
		InexactConversion,
		LimitExceeded,
		OutOfMemory,
		Interrupted,
		TypeMismatch,
		InternalError
	};

	enum class ENumericRoundingMode : std::uint8_t
	{
		TowardZero,
		Floor,
		Ceil,
		AwayFromZero,
		HalfEven,
		HalfAwayFromZero
	};

	enum class ENumericBinaryOperation : std::uint8_t
	{
		Add,
		Subtract,
		Multiply,
		Divide,
		Remainder,
		And,
		Or,
		Xor
	};

	struct FNumericError
	{
		ENumericStatus Status = ENumericStatus::Ok;
		const char* MessageId = nullptr;
		const char* Reason = nullptr;
		std::uint64_t Offset = 0;
	};

	/** Zero selects a finite default. Limits describe numeric work, not the entire VM heap. */
	struct FNumericLimits
	{
		std::uint64_t MaxValueBits = 0;
		std::uint64_t MaxInputBytes = 0;
		std::uint64_t MaxOutputBytes = 0;
		std::uint64_t MaxScratchBytes = 0;
		std::uint64_t MaxWorkUnits = 0;
	};

	struct FNumericWork
	{
		bool (*Check)(void*, std::uint64_t WorkUnits) = nullptr;
		void* UserData = nullptr;
	};

	struct FNumericOperationContext
	{
		FNumericLimits Limits;
		FNumericWork Work;
	};

	/** Immutable integer payload; copies and moves never allocate. A moved-from value is zero. */
	class FBigInteger final
	{
		friend struct FNumericAccess;

	  public:
		FBigInteger() noexcept;
		FBigInteger(const FBigInteger&) noexcept;
		FBigInteger& operator=(const FBigInteger&) noexcept;
		FBigInteger(FBigInteger&&) noexcept;
		FBigInteger& operator=(FBigInteger&&) noexcept;
		~FBigInteger() noexcept;
		void Swap(FBigInteger&) noexcept;
		bool IsZero() const noexcept;
		bool IsNegative() const noexcept;
		int Sign() const noexcept;
		std::uint64_t BitLength() const noexcept;
		std::uint64_t Hash() const noexcept;
		bool operator==(const FBigInteger&) const noexcept;
		bool operator!=(const FBigInteger&) const noexcept;

	  private:
		std::shared_ptr<const FBigIntegerData> Data;
		std::uint64_t Small = 0;
		bool Negative = false;
	};

	/** Always reduced, with a positive denominator. The empty payload denotes 0/1. */
	class FRational final
	{
		friend struct FNumericAccess;

	  public:
		FRational() noexcept;
		FRational(const FRational&) noexcept;
		FRational& operator=(const FRational&) noexcept;
		FRational(FRational&&) noexcept;
		FRational& operator=(FRational&&) noexcept;
		~FRational() noexcept;
		void Swap(FRational&) noexcept;
		FBigInteger Numerator() const noexcept;
		FBigInteger Denominator() const noexcept;
		bool IsZero() const noexcept;
		bool IsNegative() const noexcept;
		bool IsInteger() const noexcept;
		int Sign() const noexcept;
		std::uint64_t Hash() const noexcept;
		bool operator==(const FRational&) const noexcept;
		bool operator!=(const FRational&) const noexcept;

	  private:
		std::shared_ptr<const FRationalData> Data;
	};

	namespace Numerics
	{
		inline constexpr std::uint64_t HardMaxValueBits = 1048576;
		bool ValidateLimits(const FNumericLimits&, FNumericLimits& Normalized, FNumericError&) noexcept;
		bool TryFromInt64(std::int64_t, FBigInteger&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryFromUInt64(std::uint64_t, FBigInteger&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryParse(std::string_view, FBigInteger&, FNumericOperationContext&, FNumericError&,
					  int Base = 10) noexcept;
		bool TryParse(std::string_view, FRational&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryMakeRational(const FBigInteger&, const FBigInteger&, FRational&, FNumericOperationContext&,
							 FNumericError&) noexcept;
		bool TryBinary(ENumericBinaryOperation, const FBigInteger&, const FBigInteger&, FBigInteger&,
					   FNumericOperationContext&, FNumericError&) noexcept;
		bool TryBinary(ENumericBinaryOperation, const FRational&, const FRational&, FRational&,
					   FNumericOperationContext&, FNumericError&) noexcept;
		bool TryDivMod(const FBigInteger&, const FBigInteger&, FBigInteger& Quotient, FBigInteger& Remainder,
					   FNumericOperationContext&, FNumericError&) noexcept;
		bool TryCompare(const FBigInteger&, const FBigInteger&, int&, FNumericOperationContext&,
						FNumericError&) noexcept;
		bool TryCompare(const FRational&, const FRational&, int&, FNumericOperationContext&,
						FNumericError&) noexcept;
		bool TryNegate(const FBigInteger&, FBigInteger&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryNegate(const FRational&, FRational&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryAbs(const FBigInteger&, FBigInteger&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryAbs(const FRational&, FRational&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryNot(const FBigInteger&, FBigInteger&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryShift(const FBigInteger&, std::int64_t Count, bool Left, FBigInteger&,
					  FNumericOperationContext&, FNumericError&) noexcept;
		bool TryGcd(const FBigInteger&, const FBigInteger&, FBigInteger&, FNumericOperationContext&,
					FNumericError&) noexcept;
		bool TryLcm(const FBigInteger&, const FBigInteger&, FBigInteger&, FNumericOperationContext&,
					FNumericError&) noexcept;
		bool TryPow(const FBigInteger&, std::int64_t, FBigInteger&, FNumericOperationContext&,
					FNumericError&) noexcept;
		bool TryPow(const FRational&, std::int64_t, FRational&, FNumericOperationContext&,
					FNumericError&) noexcept;
		bool TryReciprocal(const FRational&, FRational&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryRound(const FRational&, ENumericRoundingMode, FBigInteger&, FNumericOperationContext&,
					  FNumericError&) noexcept;
		bool TryHasFiniteDecimal(const FRational&, bool&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryToString(const FBigInteger&, std::string&, FNumericOperationContext&, FNumericError&,
						 int Base = 10) noexcept;
		bool TryToString(const FRational&, std::string&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryToDecimal(const FRational&, std::int64_t Digits, ENumericRoundingMode, std::string&,
						  FNumericOperationContext&, FNumericError&) noexcept;
		bool TryToInt64(const FBigInteger&, std::int64_t&, FNumericOperationContext&,
						FNumericError&) noexcept;
		bool TryToUInt64(const FBigInteger&, std::uint64_t&, FNumericOperationContext&,
						 FNumericError&) noexcept;
		bool TryToInt64(const FRational&, std::int64_t&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryToUInt64(const FRational&, std::uint64_t&, FNumericOperationContext&,
						 FNumericError&) noexcept;
		bool TryFromDouble(double, FBigInteger&, bool Exact, FNumericOperationContext&,
						   FNumericError&) noexcept;
		bool TryFromDouble(double, FRational&, FNumericOperationContext&, FNumericError&) noexcept;
		bool TryToDouble(const FBigInteger&, double&, bool Exact, FNumericOperationContext&,
						 FNumericError&) noexcept;
		bool TryToDouble(const FRational&, double&, bool Exact, FNumericOperationContext&,
						 FNumericError&) noexcept;
		bool TryToFloat(const FBigInteger&, float&, bool Exact, FNumericOperationContext&,
						FNumericError&) noexcept;
		bool TryToFloat(const FRational&, float&, bool Exact, FNumericOperationContext&,
						FNumericError&) noexcept;
	} // namespace Numerics
} // namespace VeroScript
