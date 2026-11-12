// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"

namespace VeroScript::Network
{
	class FContext;
	// A nonempty request never reports Progress with zero bytes.
	enum class EStreamState : std::uint8_t
	{
		Progress,
		WouldBlock,
		End,
		Failed
	};
	struct FStreamResult
	{
		std::uint64_t Bytes = 0;
		EStreamState State = EStreamState::Progress;
		FError Error;
	};
	namespace Detail
	{
		struct FStreamState;
		struct FStreamAccess;
	} // namespace Detail
	struct FBytePipe;
	bool TryCreateBytePipe(
		std::uint64_t Capacity, FBytePipe& Out, FError& Error, std::shared_ptr<const void> Owner = {});

	// Exactly one logical consumer. Copies of the shared handle observe the same cursor.
	class FReadableStream
	{
		friend struct Detail::FStreamAccess;
		friend bool TryCreateBytePipe(std::uint64_t, FBytePipe&, FError&, std::shared_ptr<const void>);

	public:
		~FReadableStream();
		FReadableStream(const FReadableStream&) = delete;
		FReadableStream& operator=(const FReadableStream&) = delete;
		FStreamResult Read(void* Destination, std::int64_t Count);
		// Discards queued bytes and cancels the producer. Idempotent and nonblocking.
		void Cancel() noexcept;
		std::uint64_t BufferedBytes() const noexcept;
		std::uint64_t Capacity() const noexcept;

	private:
		explicit FReadableStream(std::shared_ptr<Detail::FStreamState> State);
		std::shared_ptr<Detail::FStreamState> State;
	};

	class FWritableStream
	{
		friend struct Detail::FStreamAccess;
		friend bool TryCreateBytePipe(std::uint64_t, FBytePipe&, FError&, std::shared_ptr<const void>);

	public:
		~FWritableStream();
		FWritableStream(const FWritableStream&) = delete;
		FWritableStream& operator=(const FWritableStream&) = delete;
		FStreamResult Write(const void* Source, std::int64_t Count);
		// Accepts the entire block or none of it; suitable for replay-on-resume providers.
		FStreamResult WriteAll(const void* Source, std::int64_t Count);
		void Finish() noexcept;
		// Already queued bytes remain readable before the terminal failure.
		void Fail(FError Error) noexcept;
		std::uint64_t BufferedBytes() const noexcept;
		std::uint64_t Capacity() const noexcept;

	private:
		explicit FWritableStream(std::shared_ptr<Detail::FStreamState> State);
		FStreamResult WriteImpl(const void*, std::int64_t, bool);
		std::shared_ptr<Detail::FStreamState> State;
	};

	struct FBytePipe
	{
		std::shared_ptr<FReadableStream> Reader;
		std::shared_ptr<FWritableStream> Writer;
	};
	// Counts the ring's full capacity until its last endpoint is destroyed.
	bool TryCreateBytePipe(FContext& Context, std::uint64_t Capacity, FBytePipe& Out, FError& Error);
} // namespace VeroScript::Network
