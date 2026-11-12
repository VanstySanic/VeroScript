// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetworkStream.h"

namespace VeroScript::Network
{
	enum class EFileCommitState : std::uint8_t
	{
		Uncommitted,
		Committing,
		Committed,
		Failed
	};

	struct FFileExecutorOptions
	{
		// Shared by every file executor in one context. Subsequent options must match.
		std::uint32_t WorkerThreads = 2, MaxPendingFiles = 32;
		std::uint64_t BlockBytes = 64 * 1024;
		std::chrono::milliseconds PollInterval{2};
	};

	struct FFileReadOptions
	{
		// UTF-8. Relative paths require an explicit absolute BaseDirectory.
		std::string Path, BaseDirectory;
		std::uint64_t Offset = 0;
		std::optional<std::uint64_t> Length;
		std::uint64_t MaxBytes = 256 * 1024 * 1024;
		FOperationOptions Operation;
	};

	struct FFileWriteOptions
	{
		std::string Path, BaseDirectory;
		// Optional stable partial representation, copied into a new temporary file.
		// Its physical size must equal PrefixLength. It is never modified or removed.
		std::string PrefixPath;
		std::uint64_t PrefixLength = 0;
		// Total output bytes, including the optional prefix.
		std::optional<std::uint64_t> ExpectedLength;
		std::uint64_t MaxBytes = 256 * 1024 * 1024;
		bool bReplaceExisting = false, bKeepPartial = false, bFlushToDisk = true;
		FOperationOptions Operation;
	};

	struct FFileProgress
	{
		std::uint64_t Bytes = 0;
		std::optional<std::uint64_t> TotalBytes;
		EFileCommitState Commit = EFileCommitState::Uncommitted;
		// Logical cancellation may precede retirement of a blocking OS file call.
		bool bPhysicalComplete = false;
		int CleanupSystemCode = 0;
		std::string Path, PartialPath;
	};

	struct FFileResult
	{
		std::string Path;
		std::uint64_t Bytes = 0;
		bool bCommitted = false;
	};

	struct FFileExecutorStatistics
	{
		std::uint32_t PendingFiles = 0, ActiveWorkers = 0, WorkerThreads = 0;
	};
	namespace Detail
	{
		struct FFileProgressState;
		struct FFilePool;
	} // namespace Detail
	class FFileExecutor;
	class FFileOperation
	{
		friend class FFileExecutor;

	public:
		const FOperation& Operation() const noexcept;
		// Non-null only for Read. Cancellation/disposal of this reader cancels production.
		std::shared_ptr<FReadableStream> Body() const noexcept;
		FFileProgress Progress() const;

	private:
		FOperation Op;
		std::shared_ptr<FReadableStream> Reader;
		std::shared_ptr<Detail::FFileProgressState> State;
	};
	class FFileExecutor
	{
	public:
		explicit FFileExecutor(FContext& Context, FFileExecutorOptions Options = {});
		~FFileExecutor();
		FFileExecutor(const FFileExecutor&) = delete;
		FFileExecutor& operator=(const FFileExecutor&) = delete;
		FFileOperation Read(FFileReadOptions Options);
		// Claims a single-consumer reader. Writes a sibling temporary file, then atomically commits.
		FFileOperation Write(FFileWriteOptions Options, std::shared_ptr<FReadableStream> Source);
		// Nonblocking cancellation of this executor's jobs. Context shutdown joins physical work.
		void Close() noexcept;
		FFileExecutorStatistics Statistics() const noexcept;
		bool BelongsTo(const FContext& Context) const noexcept;

	private:
		struct FImpl;
		std::shared_ptr<FImpl> Impl;
	};
} // namespace VeroScript::Network
