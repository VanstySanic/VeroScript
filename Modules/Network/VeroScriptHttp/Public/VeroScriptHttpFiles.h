// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHttp.h"
#include "VeroScriptNetworkFiles.h"

namespace VeroScript::Http
{
	enum class EResumeValidator : std::uint8_t
	{
		StrongETag,
		LastModified
	};
	struct FDownloadResume
	{
		std::string PartialPath;
		std::uint64_t Offset = 0;
		EResumeValidator Kind = EResumeValidator::StrongETag;
		std::string Validator;
	};
	struct FDownloadOptions
	{
		// The download owns its deadline from Request.Operation; File.Operation is not restarted.
		Network::FFileWriteOptions File;
		std::optional<FDownloadResume> Resume;
		std::uint64_t BufferBytes = 256 * 1024;
	};
	struct FDownloadProgress
	{
		Network::FFileProgress File;
		std::uint64_t ReceivedBytes = 0;
		std::optional<std::uint64_t> RepresentationBytes;
		bool bResumed = false, bAlreadyComplete = false;
	};
	struct FDownloadResult
	{
		FResponse Response;
		Network::FFileResult File;
		bool bResumed = false, bAlreadyComplete = false;
	};
	namespace Detail
	{
		struct FDownloadState;
	}
	class FDownloadOperation
	{
		friend FDownloadOperation DownloadFile(
			Network::FContext&, FClient&, Network::FFileExecutor&, FRequest, FDownloadOptions);

	public:
		const Network::FOperation& Operation() const noexcept;
		FDownloadProgress Progress() const;
		std::shared_ptr<const FResponse> ResponseHead() const;

	private:
		Network::FOperation Op;
		std::shared_ptr<Detail::FDownloadState> State;
	};
	// GET only. Existing partial input is copied and never modified. A resumed 200 is rejected;
	// 206/416 require an exact range and matching validator before any representation is committed.
	FDownloadOperation DownloadFile(
		Network::FContext& Context, FClient& Client, Network::FFileExecutor& Files, FRequest Request, FDownloadOptions Options);
	// Opens on the bounded file executor; never stats/reads the file on the network I/O thread.
	// A positive read Length supplies a known upload range. Empty ranges are read through EOF so opening errors
	// propagate.
	FUploadFactory MakeFileUploadFactory(std::shared_ptr<Network::FFileExecutor> Files, Network::FFileReadOptions Options);
} // namespace VeroScript::Http
