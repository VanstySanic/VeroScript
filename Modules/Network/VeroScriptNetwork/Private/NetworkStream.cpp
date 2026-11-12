// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkStream.h"
#include "NetworkInternal.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace VeroScript::Network
{
	namespace Detail
	{
		struct FStreamState
		{
			std::shared_ptr<const void> Owner;
			std::mutex Mutex;
			FBytes Buffer;
			std::size_t Begin = 0, Size = 0;
			bool Finished = false, Cancelled = false;
			bool Claimed = false;
			FError Error;
			std::shared_ptr<const std::function<void()>> Writable, Readable;
			std::function<void()> Cancel;
		};
		bool FStreamAccess::CreatePipe(const std::shared_ptr<FContext>& Context, std::uint64_t Capacity,
			FBytePipe& Out, FError& Error, std::shared_ptr<const void> Owner)
		{
			FBytePipe Pipe;
			if (!TryCreateBytePipe(Capacity, Pipe, Error, std::move(Owner))) return false;
			try
			{
				Pipe.Reader = RetainObject(Context, std::move(Pipe.Reader));
				Pipe.Writer = RetainObject(Context, std::move(Pipe.Writer));
				Out = std::move(Pipe);
				return true;
			}
			catch (const std::bad_alloc&)
			{
				Error = {};
				Error.Code = EError::Allocation;
				return false;
			}
		}

		static void Signal(const std::shared_ptr<const std::function<void()>>& Function) noexcept
		{
			if (Function)
			{
				try
				{
					(*Function)();
				}
				catch (...)
				{
				}
			}
		}

		void FStreamAccess::OnWritable(FWritableStream& Stream, std::function<void()> Function)
		{
			auto Value = std::make_shared<const std::function<void()>>(std::move(Function));
			std::lock_guard<std::mutex> Lock(Stream.State->Mutex);
			Stream.State->Writable = std::move(Value);
		}
		void FStreamAccess::OnReadable(FReadableStream& Stream, std::function<void()> Function)
		{
			auto Value = std::make_shared<const std::function<void()>>(std::move(Function));
			std::lock_guard<std::mutex> Lock(Stream.State->Mutex);
			Stream.State->Readable = std::move(Value);
		}
		void FStreamAccess::OnCancel(FReadableStream& Stream, std::function<void()> Function)
		{
			bool Cancelled;
			{
				std::lock_guard<std::mutex> Lock(Stream.State->Mutex);
				Cancelled = Stream.State->Cancelled;
				if (!Cancelled)
				{
					Stream.State->Cancel = std::move(Function);
				}
			}
			if (Cancelled && Function)
			{
				Function();
			}
		}
		bool FStreamAccess::ClaimReader(FReadableStream& Stream)
		{
			std::lock_guard<std::mutex> Lock(Stream.State->Mutex);
			if (Stream.State->Claimed || Stream.State->Cancelled)
			{
				return false;
			}
			Stream.State->Claimed = true;
			return true;
		}
	} // namespace Detail

	static FStreamResult Failure(EError Code)
	{
		FStreamResult Result;
		Result.State = EStreamState::Failed;
		Result.Error.Code = Code;
		return Result;
	}

	bool TryCreateBytePipe(std::uint64_t Capacity, FBytePipe& Out, FError& Error, std::shared_ptr<const void> Owner)
	{
		Error = {};
		if (!Capacity || Capacity > static_cast<std::uint64_t>(INT64_MAX) || Capacity > FBytes{}.max_size())
		{
			Error.Code = EError::InvalidArgument;
			return false;
		}
		try
		{
			auto State = std::make_shared<Detail::FStreamState>();
			State->Owner = std::move(Owner);
			State->Buffer.resize(static_cast<std::size_t>(Capacity));
			FBytePipe Result;
			Result.Reader = std::shared_ptr<FReadableStream>(new FReadableStream(State));
			Result.Writer = std::shared_ptr<FWritableStream>(new FWritableStream(std::move(State)));
			Out = std::move(Result);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = EError::Allocation;
		}
		catch (const std::length_error&)
		{
			Error.Code = EError::Limit;
		}
		return false;
	}
	bool TryCreateBytePipe(FContext& Context, std::uint64_t Capacity, FBytePipe& Out, FError& Error)
	{
		auto C = Detail::FAccess::Context(Context);
		if (Capacity > C->Options.MaxBufferBytes)
		{
			Error = {};
			Error.Code = EError::Limit;
			return false;
		}
		auto Lease = Detail::ReserveBytes(C, Capacity, Error);
		return Lease && Detail::FStreamAccess::CreatePipe(C, Capacity, Out, Error, std::move(Lease));
	}

	FReadableStream::~FReadableStream()
	{
		Cancel();
	}

	FStreamResult FReadableStream::Read(void* Destination, std::int64_t Count)
	{
		if (Count < 0 || (Count && !Destination))
		{
			return Failure(EError::InvalidArgument);
		}
		std::shared_ptr<const std::function<void()>> Wake;
		FStreamResult Result;
		try
		{
			std::lock_guard<std::mutex> Lock(State->Mutex);
			if (State->Cancelled)
			{
				return Failure(EError::Cancelled);
			}
			if (!Count)
			{
				return Result;
			}
			if (!State->Size)
			{
				if (State->Error)
				{
					Result.State = EStreamState::Failed;
					Result.Error = State->Error;
				}
				else
				{
					Result.State = State->Finished ? EStreamState::End : EStreamState::WouldBlock;
				}
				return Result;
			}
			const auto N = static_cast<std::size_t>(std::min<std::uint64_t>(Count, State->Size));
			const auto First = std::min(N, State->Buffer.size() - State->Begin);
			std::memcpy(Destination, State->Buffer.data() + State->Begin, First);
			if (N > First)
			{
				std::memcpy(static_cast<std::uint8_t*>(Destination) + First, State->Buffer.data(), N - First);
			}
			State->Begin = (State->Begin + N) % State->Buffer.size();
			State->Size -= N;
			Result.Bytes = N;
			Wake = State->Writable;
		}
		catch (const std::bad_alloc&)
		{
			return Failure(EError::Allocation);
		}
		Detail::Signal(Wake);
		return Result;
	}

	FWritableStream::~FWritableStream()
	{
		FError E;
		E.Code = EError::Closed;
		Fail(std::move(E));
	}

	FStreamResult FWritableStream::Write(const void* Source, std::int64_t Count)
	{
		return WriteImpl(Source, Count, false);
	}

	void FReadableStream::Cancel() noexcept
	{
		std::function<void()> Callback;
		std::shared_ptr<const std::function<void()>> Wake, ReaderWake;
		{
			std::lock_guard<std::mutex> Lock(State->Mutex);
			if (State->Cancelled)
			{
				return;
			}
			State->Cancelled = true;
			State->Size = 0;
			if (!State->Finished)
			{
				Callback.swap(State->Cancel);
			}
			State->Cancel = {};
			Wake = State->Writable;
			ReaderWake = State->Readable;
		}
		if (Callback)
		{
			try
			{
				Callback();
			}
			catch (...)
			{
			}
		}
		Detail::Signal(Wake);
		Detail::Signal(ReaderWake);
	}

	std::uint64_t FReadableStream::BufferedBytes() const noexcept
	{
		std::lock_guard<std::mutex> Lock(State->Mutex);
		return State->Size;
	}

	FStreamResult FWritableStream::WriteAll(const void* Source, std::int64_t Count)
	{
		return WriteImpl(Source, Count, true);
	}

	void FWritableStream::Finish() noexcept
	{
		std::shared_ptr<const std::function<void()>> Wake;
		{
			std::lock_guard<std::mutex> Lock(State->Mutex);
			if (State->Finished || State->Cancelled)
			{
				return;
			}
			State->Finished = true;
			State->Cancel = {};
			Wake = State->Readable;
		}
		Detail::Signal(Wake);
	}

	void FWritableStream::Fail(FError Error) noexcept
	{
		std::shared_ptr<const std::function<void()>> Wake;
		{
			std::lock_guard<std::mutex> Lock(State->Mutex);
			if (State->Finished || State->Cancelled)
			{
				return;
			}
			State->Error = std::move(Error);
			if (!State->Error)
			{
				State->Error.Code = EError::Internal;
			}
			State->Finished = true;
			State->Cancel = {};
			Wake = State->Readable;
		}
		Detail::Signal(Wake);
	}

	std::uint64_t FWritableStream::BufferedBytes() const noexcept
	{
		std::lock_guard<std::mutex> Lock(State->Mutex);
		return State->Size;
	}

	std::uint64_t FWritableStream::Capacity() const noexcept
	{
		return State->Buffer.size();
	}

	std::uint64_t FReadableStream::Capacity() const noexcept
	{
		return State->Buffer.size();
	}

	FWritableStream::FWritableStream(std::shared_ptr<Detail::FStreamState> Value) : State(std::move(Value))
	{
	}

	FReadableStream::FReadableStream(std::shared_ptr<Detail::FStreamState> Value) : State(std::move(Value))
	{
	}

	FStreamResult FWritableStream::WriteImpl(const void* Source, std::int64_t Count, bool All)
	{
		if (Count < 0 || (Count && !Source))
		{
			return Failure(EError::InvalidArgument);
		}
		std::shared_ptr<const std::function<void()>> Wake;
		FStreamResult Result;
		{
			std::lock_guard<std::mutex> Lock(State->Mutex);
			if (State->Cancelled)
			{
				return Failure(EError::Cancelled);
			}
			if (State->Finished)
			{
				return Failure(EError::Closed);
			}
			if (!Count)
			{
				return Result;
			}
			const auto Available = State->Buffer.size() - State->Size;
			if (All && static_cast<std::uint64_t>(Count) > State->Buffer.size())
			{
				return Failure(EError::Limit);
			}
			if (!Available || (All && static_cast<std::uint64_t>(Count) > Available))
			{
				Result.State = EStreamState::WouldBlock;
				return Result;
			}
			const auto N = static_cast<std::size_t>(std::min<std::uint64_t>(Count, Available));
			const auto End = (State->Begin + State->Size) % State->Buffer.size();
			const auto First = std::min(N, State->Buffer.size() - End);
			std::memcpy(State->Buffer.data() + End, Source, First);
			if (N > First)
			{
				std::memcpy(State->Buffer.data(), static_cast<const std::uint8_t*>(Source) + First, N - First);
			}
			State->Size += N;
			Result.Bytes = N;
			Wake = State->Readable;
		}
		Detail::Signal(Wake);
		return Result;
	}
} // namespace VeroScript::Network
