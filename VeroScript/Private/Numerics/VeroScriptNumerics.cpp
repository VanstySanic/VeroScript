// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericsInternal.h"

namespace VeroScript
{
	std::size_t FNumericAccess::Size(const FBigInteger& V) noexcept
	{
		return V.Data ? V.Data->Words.size() : V.Small > UINT32_MAX ? 2
		    : V.Small                                               ? 1
		                                                            : 0;
	}
	const std::uint32_t* FNumericAccess::Words(const FBigInteger& V) noexcept
	{
		return V.Data ? V.Data->Words.data() : nullptr;
	}
	std::uint32_t FNumericAccess::Word(const FBigInteger& V, std::size_t I) noexcept
	{
		return V.Data ? (I < V.Data->Words.size() ? V.Data->Words[I] : 0)
		    : I < 2   ? static_cast<std::uint32_t>(V.Small >> (I * 32))
		              : 0;
	}
	FBigInteger FNumericAccess::Small(std::uint64_t N, bool Negative) noexcept
	{
		FBigInteger R;
		R.Small = N;
		R.Negative = N && Negative;
		return R;
	}
	FBigInteger FNumericAccess::Large(std::shared_ptr<const FBigIntegerData> D, bool Negative) noexcept
	{
		FBigInteger R;
		R.Data = std::move(D);
		R.Negative = Negative;
		return R;
	}
	FBigInteger FNumericAccess::Negate(FBigInteger V) noexcept
	{
		if (!V.IsZero())
		{
			V.Negative = !V.Negative;
		}
		return V;
	}
	FRational FNumericAccess::Rational(std::shared_ptr<const FRationalData> D) noexcept
	{
		FRational R;
		R.Data = std::move(D);
		return R;
	}
	FBigInteger::FBigInteger() noexcept = default;
	FBigInteger::FBigInteger(const FBigInteger&) noexcept = default;
	FBigInteger& FBigInteger::operator=(const FBigInteger&) noexcept = default;
	FBigInteger::FBigInteger(FBigInteger&& V) noexcept
	    : Data(std::move(V.Data)), Small(V.Small), Negative(V.Negative)
	{
		V.Small = 0;
		V.Negative = false;
	}

	FBigInteger& FBigInteger::operator=(FBigInteger&& V) noexcept
	{
		if (this != &V)
		{
			Data = std::move(V.Data);
			Small = V.Small;
			Negative = V.Negative;
			V.Small = 0;
			V.Negative = false;
		}
		return *this;
	}

	FBigInteger::~FBigInteger() noexcept = default;

	void FBigInteger::Swap(FBigInteger& V) noexcept
	{
		Data.swap(V.Data);
		std::swap(Small, V.Small);
		std::swap(Negative, V.Negative);
	}

	bool FBigInteger::IsZero() const noexcept
	{
		return !Data && !Small;
	}

	bool FBigInteger::IsNegative() const noexcept
	{
		return Negative;
	}

	int FBigInteger::Sign() const noexcept
	{
		return IsZero() ? 0 : Negative ? -1
		                               : 1;
	}

	std::uint64_t FBigInteger::BitLength() const noexcept
	{
		if (Data)
		{
			return Data->Bits;
		}
		const auto High = static_cast<std::uint32_t>(Small >> 32);
		return High ? 32u + Numerics::Detail::WordBits(High)
		            : Numerics::Detail::WordBits(static_cast<std::uint32_t>(Small));
	}

	std::uint64_t FBigInteger::Hash() const noexcept
	{
		std::uint32_t W[] = {static_cast<std::uint32_t>(Small), static_cast<std::uint32_t>(Small >> 32)};
		auto H = Data ? Data->Hash.load(std::memory_order_relaxed) : 0;
		if (!H)
		{
			H = Numerics::Detail::HashWords(Data ? Data->Words.data() : W, FNumericAccess::Size(*this));
			if (Data)
			{
				Data->Hash.store(H, std::memory_order_relaxed);
			}
		}
		return (H ^ static_cast<std::uint64_t>(Negative)) * 1099511628211ull;
	}

	bool FBigInteger::operator==(const FBigInteger& V) const noexcept
	{
		if (Negative != V.Negative || Small != V.Small)
		{
			return false;
		}
		if (Data == V.Data)
		{
			return true;
		}
		if (!Data || !V.Data || Data->Bits != V.Data->Bits || Data->Words.size() != V.Data->Words.size())
		{
			return false;
		}
		const auto LeftHash = Data->Hash.load(std::memory_order_relaxed);
		const auto RightHash = V.Data->Hash.load(std::memory_order_relaxed);
		if (LeftHash && RightHash && LeftHash != RightHash) return false;
		return Data->Words == V.Data->Words;
	}

	bool FBigInteger::operator!=(const FBigInteger& V) const noexcept
	{
		return !(*this == V);
	}

	FRational::FRational() noexcept = default;

	FRational::FRational(const FRational&) noexcept = default;
	FRational& FRational::operator=(const FRational&) noexcept = default;
	FRational::FRational(FRational&& V) noexcept : Data(std::move(V.Data))
	{
	}

	FRational& FRational::operator=(FRational&& V) noexcept
	{
		if (this != &V)
		{
			Data = std::move(V.Data);
		}
		return *this;
	}

	FRational::~FRational() noexcept = default;

	void FRational::Swap(FRational& V) noexcept
	{
		Data.swap(V.Data);
	}

	FBigInteger FRational::Numerator() const noexcept
	{
		return Data ? Data->Numerator : FBigInteger{};
	}

	FBigInteger FRational::Denominator() const noexcept
	{
		return Data ? Data->Denominator : FNumericAccess::Small(1);
	}

	bool FRational::IsZero() const noexcept
	{
		return !Data;
	}

	bool FRational::IsNegative() const noexcept
	{
		return Data && Data->Numerator.IsNegative();
	}

	bool FRational::IsInteger() const noexcept
	{
		return !Data || Data->Denominator == FNumericAccess::Small(1);
	}

	int FRational::Sign() const noexcept
	{
		return Data ? Data->Numerator.Sign() : 0;
	}

	std::uint64_t FRational::Hash() const noexcept
	{
		return Data ? Data->Numerator.Hash() * 1099511628211ull ^ Data->Denominator.Hash()
		            : FBigInteger{}.Hash() * 1099511628211ull ^ FNumericAccess::Small(1).Hash();
	}

	bool FRational::operator==(const FRational& V) const noexcept
	{
		return Data == V.Data || (Data && V.Data && Data->Numerator == V.Data->Numerator && Data->Denominator == V.Data->Denominator);
	}

	bool FRational::operator!=(const FRational& V) const noexcept
	{
		return !(*this == V);
	}
} // namespace VeroScript

namespace VeroScript::Numerics
{
	bool ValidateLimits(const FNumericLimits& L, FNumericLimits& Out, FNumericError& E) noexcept
	{
		FNumericLimits R = L;
		if (!R.MaxValueBits)
		{
			R.MaxValueBits = 65536;
		}
		if (!R.MaxInputBytes)
		{
			R.MaxInputBytes = 1048576;
		}
		if (!R.MaxOutputBytes)
		{
			R.MaxOutputBytes = 1048576;
		}
		if (!R.MaxScratchBytes)
		{
			R.MaxScratchBytes = 16777216;
		}
		if (!R.MaxWorkUnits)
		{
			R.MaxWorkUnits = 16777216;
		}
		if (R.MaxValueBits > HardMaxValueBits || R.MaxInputBytes > PTRDIFF_MAX ||
		    R.MaxOutputBytes > PTRDIFF_MAX || R.MaxScratchBytes > PTRDIFF_MAX)
		{
			E = {ENumericStatus::InvalidArgument, "diag.numeric_invalid_argument", "numeric limits"};
			return false;
		}
		Out = R;
		E = {};
		return true;
	}
} // namespace VeroScript::Numerics

namespace VeroScript::Numerics::Detail
{
	[[noreturn]] void Fail(ENumericStatus S, const char* Reason, std::uint64_t Offset)
	{
		const char* Id = "diag.numeric_internal";
		switch (S)
		{
		case ENumericStatus::InvalidArgument:
			{
				Id = "diag.numeric_invalid_argument";
				break;
			}
		case ENumericStatus::InvalidText:
			{
				Id = "diag.numeric_invalid_text";
				break;
			}
		case ENumericStatus::DivisionByZero:
			{
				Id = "diag.numeric_division_by_zero";
				break;
			}
		case ENumericStatus::OutOfRange:
			{
				Id = "diag.numeric_out_of_range";
				break;
			}
		case ENumericStatus::InexactConversion:
			{
				Id = "diag.numeric_inexact_conversion";
				break;
			}
		case ENumericStatus::LimitExceeded:
			{
				Id = "diag.numeric_limit";
				break;
			}
		case ENumericStatus::Interrupted:
			{
				Id = "diag.numeric_interrupted";
				break;
			}
		case ENumericStatus::TypeMismatch:
			{
				Id = "diag.numeric_type_mismatch";
				break;
			}
		default:
			{
				break;
			}
		}
		throw FFailure{{S, Id, Reason, Offset}};
	}
	FArea::FArea(FNumericOperationContext& C) : Work(C.Work)
	{
		FNumericError E;
		if (!ValidateLimits(C.Limits, Limits, E))
		{
			throw FFailure{E};
		}
		Check();
	}

	void FArea::Check(std::uint64_t N)
	{
		if (N > Limits.MaxWorkUnits - Units)
		{
			Fail(ENumericStatus::LimitExceeded, "work units");
		}
		Units += N;
		do
		{
			auto Chunk = std::min<std::uint64_t>(N, 1024);
			if (Work.Check && !Work.Check(Work.UserData, Chunk))
			{
				Fail(ENumericStatus::Interrupted, "execution governance");
			}
			N -= Chunk;
		}
		while (N);
	}

	void FArea::Reserve(std::uint64_t N)
	{
		Check();
		if (N > Limits.MaxScratchBytes - Bytes)
		{
			Fail(ENumericStatus::LimitExceeded, "scratch bytes");
		}
		Bytes += N;
	}

	void FArea::Release(std::uint64_t N) noexcept
	{
		Bytes -= N;
	}

	void FArea::Input(const FBigInteger& V)
	{
		Bits(V.BitLength(), true);
	}

	void FArea::Input(const FRational& V)
	{
		Input(V.Numerator());
		Input(V.Denominator());
	}

	void FArea::Bits(std::uint64_t N, bool Final)
	{
		if (N > (Final ? Limits.MaxValueBits : 2 * Limits.MaxValueBits + 64))
		{
			Fail(ENumericStatus::LimitExceeded, "value bits");
		}
	}

	void FArea::Text(std::uint64_t N, bool In)
	{
		if (N > (In ? Limits.MaxInputBytes : Limits.MaxOutputBytes))
		{
			Fail(ENumericStatus::LimitExceeded, In ? "input bytes" : "output bytes");
		}
	}

	void* FArea::AllocateScratch(const std::size_t Count, const std::size_t Alignment)
	{
		const auto Begin = (InlineUsed + Alignment - 1u) & ~(Alignment - 1u);
		if (Count <= 128u && Alignment <= alignof(std::max_align_t) && Begin <= sizeof(InlineScratch) && Count <= sizeof(InlineScratch) - Begin)
		{
			InlineUsed = Begin + Count;
			return InlineScratch + Begin;
		}
		return ::operator new(Count);
	}

	void FArea::FreeScratch(void* Pointer) noexcept
	{
		const auto Address = reinterpret_cast<std::uintptr_t>(Pointer);
		const auto Begin = reinterpret_cast<std::uintptr_t>(InlineScratch);
		if (Address < Begin || Address - Begin >= sizeof(InlineScratch))
		{
			::operator delete(Pointer);
		}
	}

	FReservation::FReservation(FArea& A, std::uint64_t N) : Area(A), Bytes(N)
	{
		Area.Reserve(N);
	}

	FReservation::~FReservation()
	{
		Area.Release(Bytes);
	}

	FInteger::FInteger(FArea& A) : Words(FAllocator<std::uint32_t>(&A))
	{
	}
	void Resize(FWords& W, std::size_t N)
	{
		if (N <= W.size())
		{
			W.resize(N);
			return;
		}
		auto& A = *W.get_allocator().Area;
		// Normalization and division reserve padding slots without increasing the mathematical value.
		if (N > (2 * A.Limits.MaxValueBits + 95) / 32 + 2)
		{
			Fail(ENumericStatus::LimitExceeded, "limb capacity");
		}
		W.reserve(N);
		while (W.size() < N)
		{
			auto End = std::min(N, W.size() + 1024);
			A.Check(End - W.size());
			while (W.size() < End)
			{
				W.push_back(0);
			}
		}
	}
	void Normalize(FInteger& V)
	{
		auto& A = *V.Words.get_allocator().Area;
		std::size_t Count = 0;
		while (!V.Words.empty() && !V.Words.back())
		{
			if (!(Count++ % 1024))
			{
				A.Check(std::min<std::size_t>(1024, V.Words.size()));
			}
			V.Words.pop_back();
		}
		if (V.Words.empty())
		{
			V.Negative = false;
		}
		A.Bits(Bits(V));
	}
	std::uint64_t Bits(const FInteger& V) noexcept
	{
		if (V.Words.empty())
		{
			return 0;
		}
		return (V.Words.size() - 1) * 32 + WordBits(V.Words.back());
	}
	std::uint64_t HashWords(const std::uint32_t* W, std::size_t N) noexcept
	{
		std::uint64_t H = 14695981039346656037ull;
		for (std::size_t I = 0; I < N; ++I)
		{
			for (unsigned J = 0; J < 32; J += 8)
			{
				H = (H ^ ((W[I] >> J) & 255)) * 1099511628211ull;
			}
		}
		return H;
	}
	FInteger Small(FArea& A, std::uint64_t N, bool Negative)
	{
		FInteger R(A);
		if (N)
		{
			Resize(R.Words, N > UINT32_MAX ? 2 : 1);
			R.Words[0] = static_cast<std::uint32_t>(N);
			if (R.Words.size() == 2)
			{
				R.Words[1] = static_cast<std::uint32_t>(N >> 32);
			}
			R.Negative = Negative;
		}
		return R;
	}
	FInteger Read(FArea& A, const FBigInteger& V)
	{
		A.Input(V);
		FInteger R(A);
		Resize(R.Words, FNumericAccess::Size(V));
		for (std::size_t I = 0; I < R.Words.size(); ++I)
		{
			if (!(I % 1024))
			{
				A.Check(std::min<std::size_t>(1024, R.Words.size() - I));
			}
			R.Words[I] = FNumericAccess::Word(V, I);
		}
		R.Negative = V.IsNegative();
		return R;
	}
	FInteger Copy(const FInteger& V)
	{
		auto& A = *V.Words.get_allocator().Area;
		FInteger R(A);
		Resize(R.Words, V.Words.size());
		for (std::size_t I = 0; I < R.Words.size(); ++I)
		{
			if (!(I % 1024))
			{
				A.Check(std::min<std::size_t>(1024, R.Words.size() - I));
			}
			R.Words[I] = V.Words[I];
		}
		R.Negative = V.Negative;
		return R;
	}
	FBigInteger Publish(const FInteger& V)
	{
		auto& A = *V.Words.get_allocator().Area;
		A.Bits(Bits(V), true);
		A.Check();
		if (V.Words.size() <= 2)
		{
			std::uint64_t N = V.Words.empty() ? 0 : V.Words[0];
			if (V.Words.size() == 2)
			{
				N |= static_cast<std::uint64_t>(V.Words[1]) << 32;
			}
			return FNumericAccess::Small(N, V.Negative);
		}
		// Pending immutable results remain charged until this operation commits.
		A.Reserve(sizeof(FBigIntegerData) + V.Words.size() * 4);
		auto D = std::make_shared<FBigIntegerData>();
		D->Words.reserve(V.Words.size());
		for (std::size_t I = 0; I < V.Words.size(); ++I)
		{
			if (!(I % 256))
			{
				A.Check(std::min<std::size_t>(256, V.Words.size() - I) * 2);
			}
			D->Words.push_back(V.Words[I]);
		}
		D->Bits = Bits(V);
		A.Check();
		return FNumericAccess::Large(std::move(D), V.Negative);
	}
} // namespace VeroScript::Numerics::Detail
