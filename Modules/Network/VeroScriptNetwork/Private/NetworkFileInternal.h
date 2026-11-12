// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkInternal.h"
#include "VeroScriptNetworkFiles.h"
#include <filesystem>
#include <string>

namespace VeroScript::Network::Detail
{
	inline std::string FilePathUtf8(const std::filesystem::path& Path)
	{
		auto Text = Path.u8string();
		#if defined(__cpp_char8_t)
		// C++20 changes the code-unit type, not the encoding or native separators.
		return {reinterpret_cast<const char*>(Text.data()), Text.size()};
		#else
		return Text;
		#endif
	}

	enum class EFileCommand
	{
		Open,
		Read,
		Prefix,
		Write,
		Finish,
		Abort
	};

	struct FFileProgressState
	{
		std::shared_ptr<FContext> Lifetime;
		std::mutex Mutex;
		std::string Path, PartialPath;
		std::atomic<std::uint64_t> Bytes{0}, Total{0};
		std::atomic<bool> bHasTotal{false}, bPhysicalComplete{false};
		std::atomic<EFileCommitState> Commit{EFileCommitState::Uncommitted};
		std::atomic<int> CleanupSystemCode{0};
	};

	struct FFileOwner
	{
		std::atomic<bool> Closed{false};
	};

	struct FFileJob
	{
		std::shared_ptr<FOperation> Op;
		std::shared_ptr<FFileProgressState> Progress;
		std::shared_ptr<FFileOwner> Owner;
		std::shared_ptr<FByteLease> Memory;
		std::shared_ptr<FOwnedResult> Success;
		std::shared_ptr<FReadableStream> Source;
		std::shared_ptr<FWritableStream> Output;
		FFileReadOptions ReadOptions;
		FFileWriteOptions WriteOptions;
		std::filesystem::path Path, PrefixPath, Temporary;
		std::vector<std::uint8_t> Buffer;
		std::intptr_t File = -1, Prefix = -1;
		std::uint64_t Remaining = 0, PrefixRemaining = 0;
		std::size_t Bytes = 0;
		EError NativeError = EError::None;
		int SystemCode = 0;
		FError SourceError;
		EFileCommand Command = EFileCommand::Open;
		std::atomic<bool> Cancelled{false}, Ready{false};
		bool bWrite = false, bInFlight = false, bClaimed = false;
		~FFileJob();
	};
	// Invoked exclusively on the bounded file workers. No stream/context callbacks.
	void ExecuteFileCommand(FFileJob& Job) noexcept;
} // namespace VeroScript::Network::Detail
