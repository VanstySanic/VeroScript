// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHttp.h"

namespace VeroScript::Http
{
	enum class EMultipartReadState : std::uint8_t
	{
		NeedInput,
		EventReady,
		Done,
		Failed
	};
	enum class EMultipartEvent : std::uint8_t
	{
		PartBegin,
		Data,
		PartEnd,
		End
	};
	struct FMultipartOptions
	{
		std::uint32_t MaxParts = 256;
		std::uint64_t MaxHeaderBytes = 64 * 1024;
		std::uint64_t MaxTotalHeaderBytes = 1024 * 1024;
		std::uint64_t MaxPartBodyBytes = 64 * 1024 * 1024;
		std::uint64_t MaxTotalBodyBytes = 256 * 1024 * 1024;
		std::uint64_t MaxInputBytes = 272 * 1024 * 1024;
		std::uint64_t MaxPreambleBytes = 64 * 1024, MaxEpilogueBytes = 64 * 1024;
	};
	struct FMultipartPartHead
	{
		std::string Name;
		std::optional<std::string> Filename;
		std::string ContentType;
		FHeaders Headers;
	};
	struct FMultipartEvent
	{
		EMultipartEvent Type = EMultipartEvent::End;
		std::uint64_t PartIndex = 0;
		// Non-null only for PartBegin/Data respectively. Copies share immutable owned storage.
		std::shared_ptr<const FMultipartPartHead> Head;
		std::shared_ptr<const Network::FBytes> Data;
	};
	struct FMultipartFeedResult
	{
		std::size_t Consumed = 0;
		EMultipartReadState State = EMultipartReadState::NeedInput;
	};
	struct FMultipartWork
	{
		bool (*Check)(void*, std::uint64_t) = nullptr;
		void* UserData = nullptr;
	};
	class FMultipartReader
	{
	public:
		// ContentType supplies the boundary. Context is borrowed only here; its owned state is retained.
		// With no context, the native caller manages aggregate retention, as for standalone byte pipes.
		static bool TryCreate(std::string_view ContentType, std::shared_ptr<FMultipartReader>& Out, Network::FError& Error,
			const FMultipartOptions& Options = {}, Network::FContext* Context = nullptr);
		~FMultipartReader();
		FMultipartReader(const FMultipartReader&) = delete;
		FMultipartReader& operator=(const FMultipartReader&) = delete;
		// Returns one event at most. Re-submit the unconsumed suffix, retaining final=true.
		// Out changes only for EventReady. Body bytes and metadata are never implicitly decoded.
		FMultipartFeedResult Feed(std::string_view Bytes, bool Final, FMultipartEvent& Out, FMultipartWork Work = {});
		const Network::FError& Error() const noexcept;

	private:
		struct FImpl;
		explicit FMultipartReader(std::unique_ptr<FImpl>);
		std::unique_ptr<FImpl> Impl;
	};
} // namespace VeroScript::Http
