// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMemoryInternal.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace VeroScript::Memory
{
	using namespace Detail;
	namespace
	{
		void Changed(const FRegion& R)
		{
			if (auto O = R.Descriptor->Owner)
			{
				std::lock_guard<std::mutex> L(O->Mutex);
				// Saturation never resurrects an older version by integer wraparound.
				if (O->Version != std::numeric_limits<std::uint64_t>::max())
				{
					++O->Version;
				}
			}
		}
		FMemoryResult TwoGuards(const FRegion& A, bool Write, const FRegion& B, FGuard& GA, FGuard& GB)
		{
			if (auto S = GA.Acquire(A, Write); !S)
			{
				return S;
			}
			if (A.Descriptor->Owner && A.Descriptor->Owner == B.Descriptor->Owner)
			{
				return {};
			}
			return GB.Acquire(B, false);
		}
		std::size_t Width(std::string_view Type)
		{
			if (Type == "int8" || Type == "uint8" || Type == "char" || Type == "unsigned char")
			{
				return 1;
			}
			if (Type == "int16" || Type == "uint16" || Type == "short" || Type == "unsigned short")
			{
				return 2;
			}
			if (Type == "int" || Type == "int32" || Type == "uint32" || Type == "unsigned int" ||
				Type == "float")
			{
				return 4;
			}
			if (Type == "int64" || Type == "uint64" || Type == "long" || Type == "unsigned long" ||
				Type == "double")
			{
				return 8;
			}
			return 0;
		}
		template <class T> T Signed(std::uint64_t Bits)
		{
			using U = std::make_unsigned_t<T>;
			const auto V = static_cast<U>(Bits);
			const auto Max = static_cast<U>(std::numeric_limits<T>::max());
			if (V <= Max)
			{
				return static_cast<T>(V);
			}
			return static_cast<T>(-1 - static_cast<T>(static_cast<U>(~V)));
		}
	} // namespace
	FMemoryResult Copy(FMemoryContext& C, const FValue& D, std::int64_t DO, const FValue& S, std::int64_t SO,
		std::int64_t CountValue, FMemoryWork Work)
	{
		FRegion DR, SR;
		if (auto R = Resolve(C, D, DR, true); !R)
		{
			return R;
		}
		if (auto R = Resolve(C, S, SR); !R)
		{
			return R;
		}
		if (auto R = Bounds(DR, DO, CountValue); !R)
		{
			return R;
		}
		if (auto R = Bounds(SR, SO, CountValue); !R)
		{
			return R;
		}
		FGuard DG, SG;
		if (auto R = TwoGuards(DR, true, SR, DG, SG); !R)
		{
			return R;
		}
		if (!CountValue)
		{
			return {};
		}
		auto* Destination = DR.Data + static_cast<std::size_t>(DO);
		const auto* Source = SR.Data + static_cast<std::size_t>(SO);
		const auto DA = reinterpret_cast<std::uintptr_t>(Destination),
				   SA = reinterpret_cast<std::uintptr_t>(Source);
		const auto N = static_cast<std::size_t>(CountValue);
		const bool Backward = DA > SA && DA - SA < N;
		bool Wrote = false;
		for (std::size_t P = 0; P < N;)
		{
			const auto K = std::min(ChunkSize, N - P);
			if (auto R = CheckWork(Work, K); !R)
			{
				if (Wrote)
				{
					Changed(DR);
				}
				return R;
			}
			const auto Offset = Backward ? N - P - K : P;
			std::memmove(Destination + Offset, Source + Offset, K);
			Wrote = true;
			P += K;
		}
		Changed(DR);
		return {};
	}

	FMemoryResult Fill(FMemoryContext& C, const FValue& D, std::int64_t Offset, std::int64_t Length,
		std::uint8_t Value, FMemoryWork Work)
	{
		FRegion R;
		if (auto S = Resolve(C, D, R, true); !S)
		{
			return S;
		}
		if (auto S = Bounds(R, Offset, Length); !S)
		{
			return S;
		}
		FGuard G;
		if (auto S = G.Acquire(R, true); !S)
		{
			return S;
		}
		for (std::size_t P = 0, N = static_cast<std::size_t>(Length); P < N;)
		{
			const auto K = std::min(ChunkSize, N - P);
			if (auto S = CheckWork(Work, K); !S)
			{
				if (P)
				{
					Changed(R);
				}
				return S;
			}
			std::memset(R.Data + static_cast<std::size_t>(Offset) + P, Value, K);
			P += K;
		}
		if (Length)
		{
			Changed(R);
		}
		return {};
	}

	FMemoryResult Equal(FMemoryContext& C, const FValue& A, std::int64_t AO, const FValue& B, std::int64_t BO,
		std::int64_t Length, bool& Out, FMemoryWork Work)
	{
		FRegion AR, BR;
		if (auto S = Resolve(C, A, AR); !S)
		{
			return S;
		}
		if (auto S = Resolve(C, B, BR); !S)
		{
			return S;
		}
		if (auto S = Bounds(AR, AO, Length); !S)
		{
			return S;
		}
		if (auto S = Bounds(BR, BO, Length); !S)
		{
			return S;
		}
		FGuard AG, BG;
		if (auto S = TwoGuards(AR, false, BR, AG, BG); !S)
		{
			return S;
		}
		for (std::size_t P = 0, N = static_cast<std::size_t>(Length); P < N;)
		{
			const auto K = std::min(ChunkSize, N - P);
			if (auto S = CheckWork(Work, K); !S)
			{
				return S;
			}
			if (std::memcmp(AR.Data + static_cast<std::size_t>(AO) + P,
					BR.Data + static_cast<std::size_t>(BO) + P, K))
			{
				Out = false;
				return {};
			}
			P += K;
		}
		Out = true;
		return {};
	}

	FMemoryResult Clone(FMemoryContext& C, const FValue& S, FValue& Out, FMemoryWork Work)
	{
		FRegion R;
		if (auto Result = Resolve(C, S, R); !Result)
		{
			return Result;
		}
		FGuard G;
		if (auto Result = G.Acquire(R, false); !Result)
		{
			return Result;
		}
		FValue ResultValue;
		if (auto Result = CreateUninitializedBuffer(C, R.Size, ResultValue); !Result)
		{
			return Result;
		}
		auto D = FindDescriptor(ResultValue);
		for (std::size_t P = 0, N = static_cast<std::size_t>(R.Size); P < N;)
		{
			const auto K = std::min(ChunkSize, N - P);
			if (auto Result = CheckWork(Work, K); !Result)
			{
				return Result;
			}
			std::memcpy(D->Owner->Data() + P, R.Data + P, K);
			P += K;
		}
		Out = std::move(ResultValue);
		return {};
	}

	FMemoryResult FromArray(
		FMemoryContext& C, const FDynamicArrayHandle& Source, FValue& Out, FMemoryWork Work)
	{
		const auto Owner = Source;
		if (!Owner || (Owner->ElementTypeName != "uint8" && Owner->ElementTypeName != "unsigned char"))
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		auto* Bytes = std::get_if<std::vector<std::uint8_t>>(&Owner->ScalarElements);
		if (Owner->HasScalarElements() && !Bytes)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		const auto N = Bytes ? Bytes->size() : Owner->Elements.size();
		if (N > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_range");
		}
		FValue ResultValue;
		if (auto R = CreateUninitializedBuffer(C, static_cast<std::int64_t>(N), ResultValue); !R)
		{
			return R;
		}
		auto* Destination = FindDescriptor(ResultValue)->Owner->Data();
		for (std::size_t P = 0; P < N;)
		{
			const auto K = std::min(ChunkSize, N - P);
			if (auto R = CheckWork(Work, K); !R)
			{
				return R;
			}
			if (auto R = CheckContext(C); !R) return R;
			// A work callback can replace or resize the input's backing storage.
			// Retain its owner, then reacquire and validate every borrowed chunk.
			Bytes = std::get_if<std::vector<std::uint8_t>>(&Owner->ScalarElements);
			if ((Owner->ElementTypeName != "uint8" && Owner->ElementTypeName != "unsigned char")
				|| (Owner->HasScalarElements() && !Bytes))
			{
				return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
			}
			if ((Bytes ? Bytes->size() : Owner->Elements.size()) != N)
			{
				return Fail(EMemoryStatus::InvalidArgument, "diag.memory_range");
			}
			if (Bytes)
			{
				std::memcpy(Destination + P, Bytes->data() + P, K);
			}
			else
			{
				for (auto I = P; I < P + K; ++I)
				{
					const auto* Byte = std::get_if<std::uint8_t>(&Owner->Elements[I]);
					if (!Byte)
					{
						return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
					}
					Destination[I] = *Byte;
				}
			}
			P += K;
		}
		Out = std::move(ResultValue);
		return {};
	}

	FMemoryResult ToArray(FMemoryContext& C, const FValue& S, FDynamicArrayHandle& Out, FMemoryWork Work)
	{
		FRegion R;
		if (auto Result = Resolve(C, S, R); !Result)
		{
			return Result;
		}
		FGuard G;
		if (auto Result = G.Acquire(R, false); !Result)
		{
			return Result;
		}
		try
		{
			auto A = std::make_shared<FDynamicArray>();
			A->TypeName = "array<uint8>";
			A->ElementTypeName = "uint8";
			auto& Bytes = A->ScalarElements.emplace<std::vector<std::uint8_t>>();
			Bytes.reserve(static_cast<std::size_t>(R.Size));
			for (std::size_t P = 0, N = static_cast<std::size_t>(R.Size); P < N;)
			{
				const auto K = std::min(ChunkSize, N - P);
				if (auto Result = CheckWork(Work, K); !Result)
				{
					return Result;
				}
				Bytes.insert(Bytes.end(), R.Data + P, R.Data + P + K);
				P += K;
			}
			Out = std::move(A);
			return {};
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}

	FMemoryResult Detail::ReadByte(FMemoryContext& Context, const FValue& Value,
		const std::int64_t Offset, FValue& Output, const FMemoryWork Work)
	{
		FRegion Region;
		if (auto Result = Resolve(Context, Value, Region); !Result) return Result;
		if (auto Result = Bounds(Region, Offset, 1); !Result) return Result;
		FGuard Guard;
		if (auto Result = Guard.Acquire(Region, false); !Result) return Result;
		if (auto Result = CheckWork(Work, 1); !Result) return Result;
		Output = Region.Data[static_cast<std::size_t>(Offset)];
		return {};
	}

	FMemoryResult Read(FMemoryContext& C, const FValue& V, std::int64_t Offset, std::string_view Type,
		EMemoryByteOrder Order, FValue& Out, FMemoryWork Work)
	{
		const auto N = Width(Type);
		if (!N)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		if (Order != EMemoryByteOrder::Little && Order != EMemoryByteOrder::Big)
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_byte_order");
		}
		FRegion R;
		if (auto S = Resolve(C, V, R); !S)
		{
			return S;
		}
		if (auto S = Bounds(R, Offset, static_cast<std::int64_t>(N)); !S)
		{
			return S;
		}
		FGuard G;
		if (auto S = G.Acquire(R, false); !S)
		{
			return S;
		}
		if (auto S = CheckWork(Work, N); !S)
		{
			return S;
		}
		std::uint64_t Bits = 0;
		for (std::size_t I = 0; I < N; ++I)
		{
			Bits |= static_cast<std::uint64_t>(R.Data[static_cast<std::size_t>(Offset) + I])
					<< ((Order == EMemoryByteOrder::Little ? I : N - I - 1) * 8);
		}
		if (Type == "float")
		{
			const auto U = static_cast<std::uint32_t>(Bits);
			float F;
			std::memcpy(&F, &U, 4);
			Out = F;
		}
		else if (Type == "double")
		{
			double F;
			std::memcpy(&F, &Bits, 8);
			Out = F;
		}
		else if (Type == "int8" || Type == "char")
		{
			Out = Signed<std::int8_t>(Bits);
		}
		else if (Type == "int16" || Type == "short")
		{
			Out = Signed<std::int16_t>(Bits);
		}
		else if (Type == "int32" || Type == "int")
		{
			Out = Signed<std::int32_t>(Bits);
		}
		else if (Type == "int64" || Type == "long")
		{
			Out = Signed<std::int64_t>(Bits);
		}
		else if (N == 1)
		{
			Out = static_cast<std::uint8_t>(Bits);
		}
		else if (N == 2)
		{
			Out = static_cast<std::uint16_t>(Bits);
		}
		else if (N == 4)
		{
			Out = static_cast<std::uint32_t>(Bits);
		}
		else
		{
			Out = Bits;
		}
		return {};
	}

	FMemoryResult Write(FMemoryContext& C, const FValue& V, std::int64_t Offset, std::string_view Type,
		EMemoryByteOrder Order, const FValue& Value, FMemoryWork Work)
	{
		const auto N = Width(Type);
		if (!N)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		if (Order != EMemoryByteOrder::Little && Order != EMemoryByteOrder::Big)
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_byte_order");
		}
		// Reject non-scalar payloads before FValue copying can clone a large value array.
		const bool Numeric = std::visit(
			[](const auto& X)
			{
				return std::is_arithmetic_v<std::decay_t<decltype(X)>>;
			},
			Value);
		if (!Numeric)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		FValue Converted = Value;
		std::string Error;
		if (!Runtime::CoerceValueToPrimitiveTypeName(Type, Converted, Error))
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		std::uint64_t Bits = 0;
		bool Valid = false;
		std::visit(
			[&](const auto& X)
			{
				using T = std::decay_t<decltype(X)>;
				if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
				{
					Bits = static_cast<std::uint64_t>(X);
					Valid = true;
				}
				else if constexpr (std::is_same_v<T, float>)
				{
					std::uint32_t U;
					std::memcpy(&U, &X, 4);
					Bits = U;
					Valid = true;
				}
				else if constexpr (std::is_same_v<T, double>)
				{
					std::memcpy(&Bits, &X, 8);
					Valid = true;
				}
			},
			Converted);
		if (!Valid)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		FRegion R;
		if (auto S = Resolve(C, V, R, true); !S)
		{
			return S;
		}
		if (auto S = Bounds(R, Offset, static_cast<std::int64_t>(N)); !S)
		{
			return S;
		}
		FGuard G;
		if (auto S = G.Acquire(R, true); !S)
		{
			return S;
		}
		if (auto S = CheckWork(Work, N); !S)
		{
			return S;
		}
		for (std::size_t I = 0; I < N; ++I)
		{
			R.Data[static_cast<std::size_t>(Offset) + I] =
				static_cast<std::uint8_t>(Bits >> ((Order == EMemoryByteOrder::Little ? I : N - I - 1) * 8));
		}
		Changed(R);
		return {};
	}
} // namespace VeroScript::Memory
