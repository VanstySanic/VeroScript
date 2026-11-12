// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"

namespace VeroScript::Http
{
	enum class ESseReadState : std::uint8_t
	{
		NeedInput,
		EventReady,
		Done,
		Failed
	};

	struct FSseEvent
	{
		std::string Data;
		std::string Event = "message";
		std::string Id;
	};

	struct FSseOptions
	{
		std::uint64_t MaxLineBytes = 64 * 1024;
		std::uint64_t MaxEventBytes = 1024 * 1024;
	};

	struct FSseFeedResult
	{
		std::size_t Consumed = 0;
		ESseReadState State = ESseReadState::NeedInput;
	};

	class FSseReader
	{
	public:
		explicit FSseReader(FSseOptions Options = {});
		~FSseReader();
		FSseReader(FSseReader&&) noexcept;
		FSseReader& operator=(FSseReader&&) noexcept;
		FSseReader(const FSseReader&) = delete;
		FSseReader& operator=(const FSseReader&) = delete;
		// Returns at most one event. Submit the unconsumed suffix again, including after a final block.
		FSseFeedResult Feed(std::string_view Bytes, bool bFinal, FSseEvent& Out);
		const Network::FError& Error() const noexcept;
		const std::string& LastEventId() const noexcept;
		std::optional<std::uint64_t> RetryMilliseconds() const noexcept;
		void Reset(std::string LastEventId = {});

	private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	// Encodes a complete event including the terminating blank line, committing Out only on success.
	bool TryEncodeSseEvent(
		const FSseEvent& Event, std::string& Out, Network::FError& Error, std::uint64_t MaxBytes = 1024 * 1024);
	bool TryEncodeSseComment(
		std::string_view Comment, std::string& Out, Network::FError& Error, std::uint64_t MaxBytes = 64 * 1024);
} // namespace VeroScript::Http
