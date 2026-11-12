// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardLibraryServices.h"
#include "Text/VeroScriptTextInternal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>

namespace VeroScript::StandardLibrary
{
	namespace
	{
		constexpr std::size_t ChunkSize = 4096;
		template <class F>
		FServiceResult ReadChunks(std::istream& Input, std::uint64_t Maximum, const FServiceContext& Context, F&& Append)
		{
			std::array<char, ChunkSize> Chunk;
			std::uint64_t Total = 0;
			for (;;)
			{
				if (auto R = Context.Check(); !R) return R;
				// Read one byte beyond the remaining allowance to distinguish exact EOF
				// from truncation, including the explicit maximum of zero.
				const auto Remaining = Maximum - Total;
				const auto Count = static_cast<std::streamsize>(Remaining >= ChunkSize ? ChunkSize : Remaining + 1);
				try
				{
					Input.read(Chunk.data(), Count);
				}
				catch (const std::ios_base::failure&)
				{
					if (Input.bad() || !Input.eof()) return {EServiceStatus::IoError, 0};
				}
				const auto Received = Input.gcount();
				if (Received < 0 || Input.bad() || (Input.fail() && !Input.eof())) return {EServiceStatus::IoError, 0};
				const auto N = static_cast<std::uint64_t>(Received);
				if (N > Remaining) return {EServiceStatus::LimitExceeded, 0};
				if (auto R = Context.Check(N); !R) return R;
				if (N)
				{
					if (auto R = Append(Chunk.data(), static_cast<std::size_t>(N), Total); !R) return R;
					Total += N;
				}
				if (Input.eof()) return Context.Check();
				if (Received != Count) return {EServiceStatus::IoError, 0};
			}
		}
		std::uint64_t ReadMaximum(const FServiceContext& Context)
		{
			return Context.Limits.MaxReadBytes ? std::min<std::uint64_t>(INT64_MAX, Context.Limits.MaxReadBytes)
			                                   : static_cast<std::uint64_t>(INT64_MAX);
		}
	}

	FServiceResult ReadInput(std::istream& Input, bool Token, std::optional<std::string>& Output, const FServiceContext& C)
	{
		if (auto R = C.Check(); !R) return R;
		std::string Result;
		bool Started = false;
		bool PendingCR = false;
		const auto AppendByte = [&](char Byte) -> FServiceResult
		{
			if (Result.size() == Result.max_size() || Result.size() >= static_cast<std::uint64_t>(INT64_MAX) || (C.Limits.MaxTextBytes && Result.size() >= C.Limits.MaxTextBytes))
			{
				return {EServiceStatus::LimitExceeded, 0};
			}
			Result += Byte;
			return {};
		};
		Detail::FWorkMeter Meter(C);
		for (;;)
		{
			if (auto R = Meter.Add(); !R) return R;
			std::istream::int_type Next = std::istream::traits_type::eof();
			try
			{
				Next = Input.peek();
			}
			catch (const std::ios_base::failure&)
			{
				if (Input.bad() || !Input.eof()) return {EServiceStatus::IoError, 0};
			}
			if (Input.bad() || (Input.fail() && !Input.eof())) return {EServiceStatus::IoError, 0};
			if (std::istream::traits_type::eq_int_type(Next, std::istream::traits_type::eof()))
			{
				if (!Input.eof()) return {EServiceStatus::IoError, 0};
				if (!Started)
				{
					if (auto R = Meter.Flush(); !R) return R;
					Output.reset();
					return {};
				}
				break;
			}
			const char Byte = std::istream::traits_type::to_char_type(Next);
			if (Token && Started && IsAsciiWhitespace(static_cast<unsigned char>(Byte)))
			{
				break;
			}
			try
			{
				(void)Input.get();
			}
			catch (const std::ios_base::failure&)
			{
				return {EServiceStatus::IoError, 0};
			}
			if (!Input) return {EServiceStatus::IoError, 0};
			if (Token && IsAsciiWhitespace(static_cast<unsigned char>(Byte)))
			{
				if (Started)
				{
					break;
				}
				continue;
			}
			Started = true;
			if (!Token && Byte == '\n')
			{
				break;
			}
			// A final CR is stripped at LF or EOF. Keep it pending so a line
			// exactly at its content limit behaves the same with LF and CRLF.
			if (PendingCR)
			{
				if (auto R = AppendByte('\r'); !R) return R;
				PendingCR = false;
			}
			if (!Token && Byte == '\r')
			{
				PendingCR = true;
				continue;
			}
			if (auto R = AppendByte(Byte); !R) return R;
		}
		if (auto R = Meter.Flush(); !R) return R;
		if (auto R = C.Check(); !R) return R;
		Output.emplace(std::move(Result));
		return {};
	}

	FServiceResult ReadText(std::istream& Input, std::string& Output, const FServiceContext& C)
	{
		if (auto R = C.Check(); !R) return R;
		std::string Result;
		const auto R = ReadChunks(Input, ReadMaximum(C), C, [&](const char* Data, std::size_t N, std::uint64_t Total) -> FServiceResult
		{
			if (N > Result.max_size() - Result.size()) return {EServiceStatus::LimitExceeded, 0};
			if (auto Checked = C.CheckText(Total + N); !Checked) return Checked;
			Result.append(Data, N);
			return {};
		});
		if (R)
		{
			Output.swap(Result);
		}
		return R;
	}

	FServiceResult ReadBytes(std::istream& Input, FMemoryContext& Memory, std::optional<std::uint64_t> Maximum,
	    FValue& Output, FMemoryResult& MemoryError, const FServiceContext& C)
	{
		MemoryError = {};
		if (auto R = C.Check(); !R) return R;
		FValue Buffer;
		MemoryError = Memory::CreateBuffer(Memory, 0, Buffer, C.Work);
		if (!MemoryError) return {EServiceStatus::InvalidArgument, 0};
		const auto Limit = Maximum ? std::min(ReadMaximum(C), *Maximum) : ReadMaximum(C);
		std::uint64_t Capacity = 0;
		const auto R = ReadChunks(Input, Limit, C, [&](const char* Data, std::size_t N, std::uint64_t Total) -> FServiceResult
		{
			const auto Size = Total + N;
			if (Size > Capacity)
			{
				const auto SingleLimit = Memory.GetLimits().MaxBufferBytes;
				const auto Ceiling = SingleLimit ? std::min(Limit, SingleLimit) : Limit;
				const auto Desired = std::min(Ceiling, std::max(Size, Capacity > Limit / 2 ? Limit : Capacity * 2));
				if (Desired < Size)
				{
					MemoryError = {EMemoryStatus::LimitExceeded, "diag.memory_limit"};
					return {EServiceStatus::LimitExceeded, 0};
				}
				MemoryError = Memory::Resize(Memory, Buffer, static_cast<std::int64_t>(Desired), true, C.Work);
				if (!MemoryError) return {EServiceStatus::InvalidArgument, 0};
				Capacity = Desired;
			}
			MemoryError = Memory::Resize(Memory, Buffer, static_cast<std::int64_t>(Size), false, C.Work);
			if (!MemoryError) return {EServiceStatus::InvalidArgument, 0};
			FMemoryLease Lease;
			MemoryError = Memory::AcquireLease(Memory, Buffer, true, Lease);
			if (!MemoryError) return {EServiceStatus::InvalidArgument, 0};
			std::memcpy(Lease.WritableData + static_cast<std::size_t>(Total), Data, N);
			MemoryError = Memory::ReleaseLease(Lease.Token);
			return MemoryError ? FServiceResult{} : FServiceResult{EServiceStatus::InvalidArgument, 0};
		});
		if (R)
		{
			Output = std::move(Buffer);
		}
		return R;
	}

	FServiceResult WriteBytes(std::ostream& Output, const FMemoryLease& Lease, const FServiceContext& C)
	{
		if (Lease.Size && !Lease.Data) return {EServiceStatus::InvalidArgument, 0};
		try
		{
			for (std::uint64_t I = 0; I < Lease.Size;)
			{
				const auto N = std::min<std::uint64_t>(ChunkSize, Lease.Size - I);
				if (auto R = C.Check(N); !R) return R;
				Output.write(reinterpret_cast<const char*>(Lease.Data + static_cast<std::size_t>(I)), static_cast<std::streamsize>(N));
				if (!Output) return {EServiceStatus::IoError, 0};
				I += N;
			}
			Output.flush();
			if (!Output) return {EServiceStatus::IoError, 0};
		}
		catch (const std::ios_base::failure&)
		{
			return {EServiceStatus::IoError, 0};
		}
		return C.Check();
	}
}
