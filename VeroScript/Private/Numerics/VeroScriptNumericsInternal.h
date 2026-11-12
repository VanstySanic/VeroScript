// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNumerics.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace VeroScript
{
	struct FBigIntegerData
	{
		std::vector<std::uint32_t> Words;
		std::uint64_t Bits = 0;
		// Immutable limbs can be shared across VMs. Zero means uncached; a zero
		// hash may be recomputed without affecting value identity or thread safety.
		mutable std::atomic<std::uint64_t> Hash{0};
	};
	struct FRationalData
	{
		FBigInteger Numerator;
		FBigInteger Denominator;
	};
	struct FNumericAccess
	{
		static std::size_t Size(const FBigInteger&) noexcept;
		static const std::uint32_t* Words(const FBigInteger&) noexcept;
		static std::uint32_t Word(const FBigInteger&, std::size_t) noexcept;
		static FBigInteger Small(std::uint64_t, bool Negative = false) noexcept;
		static FBigInteger Large(std::shared_ptr<const FBigIntegerData>, bool Negative) noexcept;
		static FBigInteger Negate(FBigInteger) noexcept;
		static FRational Rational(std::shared_ptr<const FRationalData>) noexcept;
	};
} // namespace VeroScript

namespace VeroScript::Numerics::Detail
{
	struct FFailure
	{
		FNumericError Error;
	};
	[[noreturn]] void Fail(ENumericStatus, const char* Reason, std::uint64_t Offset = 0);
	struct FArea
	{
		explicit FArea(FNumericOperationContext&);
		void Check(std::uint64_t Units = 0);
		void Reserve(std::uint64_t Bytes);
		void Release(std::uint64_t Bytes) noexcept;
		void Input(const FBigInteger&);
		void Input(const FRational&);
		void Bits(std::uint64_t Count, bool Final = false);
		void Text(std::uint64_t Bytes, bool Input = false);
		void* AllocateScratch(std::size_t Bytes, std::size_t Alignment);
		void FreeScratch(void* Pointer) noexcept;
		FNumericLimits Limits;
		FNumericWork Work;
		std::uint64_t Units = 0;
		std::uint64_t Bytes = 0;
		// Operation-local small temporaries never escape into immutable results.
		// The regular reservation/work ledger also charges these allocations.
		alignas(std::max_align_t) unsigned char InlineScratch[512];
		std::size_t InlineUsed = 0;
	};
	struct FReservation
	{
		FReservation(FArea&, std::uint64_t);
		~FReservation();
		FArea& Area;
		std::uint64_t Bytes;
	};
	template <class T> struct FAllocator
	{
		using value_type = T;
		explicit FAllocator(FArea* A = nullptr) noexcept : Area(A)
		{
		}
		template <class U> FAllocator(const FAllocator<U>& A) noexcept : Area(A.Area)
		{
		}
		T* allocate(std::size_t Count)
		{
			if (!Area || Count > std::numeric_limits<std::size_t>::max() / sizeof(T))
			{
				Fail(ENumericStatus::LimitExceeded, "allocation size");
			}
			auto Bytes = Count * sizeof(T);
			Area->Reserve(Bytes);
			try
			{
				return static_cast<T*>(Area->AllocateScratch(Bytes, alignof(T)));
			}
			catch (...)
			{
				Area->Release(Bytes);
				throw;
			}
		}
		void deallocate(T* P, std::size_t Count) noexcept
		{
			Area->FreeScratch(P);
			Area->Release(Count * sizeof(T));
		}
		template <class U> bool operator==(const FAllocator<U>& B) const noexcept
		{
			return Area == B.Area;
		}
		template <class U> bool operator!=(const FAllocator<U>& B) const noexcept
		{
			return Area != B.Area;
		}
		FArea* Area;
	};
	using FWords = std::vector<std::uint32_t, FAllocator<std::uint32_t>>;
	using FText = std::vector<char, FAllocator<char>>;
	struct FInteger
	{
		explicit FInteger(FArea&);
		FWords Words;
		bool Negative = false;
	};
	void Resize(FWords&, std::size_t);
	void Normalize(FInteger&);
	inline unsigned WordBits(std::uint32_t Word) noexcept
	{
		unsigned Count = 0;
		if (Word >= (std::uint32_t{1} << 16))
		{
			Word >>= 16;
			Count += 16;
		}
		if (Word >= (std::uint32_t{1} << 8))
		{
			Word >>= 8;
			Count += 8;
		}
		if (Word >= (std::uint32_t{1} << 4))
		{
			Word >>= 4;
			Count += 4;
		}
		if (Word >= (std::uint32_t{1} << 2))
		{
			Word >>= 2;
			Count += 2;
		}
		if (Word >= (std::uint32_t{1} << 1))
		{
			Word >>= 1;
			Count += 1;
		}
		return Count + Word;
	}
	std::uint64_t Bits(const FInteger&) noexcept;
	std::uint64_t HashWords(const std::uint32_t*, std::size_t) noexcept;
	FInteger Small(FArea&, std::uint64_t, bool Negative = false);
	FInteger Read(FArea&, const FBigInteger&);
	FInteger Copy(const FInteger&);
	int CompareAbs(const FInteger&, const FInteger&);
	int Compare(const FInteger&, const FInteger&);
	FInteger Add(const FInteger&, const FInteger&);
	FInteger Subtract(const FInteger&, const FInteger&);
	FInteger Multiply(const FInteger&, const FInteger&);
	void DivMod(const FInteger&, const FInteger&, FInteger&, FInteger&);
	std::uint32_t DivSmall(FInteger&, std::uint32_t);
	void MulSmall(FInteger&, std::uint32_t);
	void AddSmall(FInteger&, std::uint32_t);
	FInteger Shift(const FInteger&, std::uint64_t, bool Left, bool Arithmetic = false);
	FInteger Gcd(FInteger, FInteger);
	FInteger Pow(FInteger, std::uint64_t);
	FInteger DivideExact(const FInteger&, const FInteger&);
	FInteger Bitwise(const FInteger&, const FInteger&, ENumericBinaryOperation);
	FBigInteger Publish(const FInteger&);
	FRational PublishRational(FInteger, FInteger, bool Reduced = false);
	FInteger ParseInteger(FArea&, std::string_view, int Base, std::uint64_t Offset = 0);
	FRational ParseRational(FArea&, std::string_view);
	void FormatInteger(const FInteger&, FText&, int Base = 10);
	void Append(FText&, char);
	void AppendZeros(FText&, std::uint64_t);
	std::string PublishText(const FText&);
	bool RoundUp(const FInteger&, const FInteger&, bool Negative, bool Odd, ENumericRoundingMode);
	std::uint64_t FloatBits(const FInteger&, const FInteger&, unsigned Precision, int MinExponent,
							int MaxExponent, bool Exact);
	void FromDouble(FArea&, double, FInteger&, FInteger&);

	template <class F> bool Boundary(FNumericOperationContext& C, FNumericError& E, F&& Function) noexcept
	{
		E = {};
		try
		{
			FArea A(C);
			Function(A);
			return true;
		}
		catch (const FFailure& X)
		{
			E = X.Error;
		}
		catch (const std::bad_alloc&)
		{
			E = {ENumericStatus::OutOfMemory, "diag.numeric_out_of_memory", "allocation"};
		}
		catch (...)
		{
			E = {ENumericStatus::InternalError, "diag.numeric_internal", "unexpected exception"};
		}
		return false;
	}
} // namespace VeroScript::Numerics::Detail
