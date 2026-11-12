// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpSse.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace VeroScript::Http
{
	static void SseError(Network::FError& Out, Network::EError Code, const char* Text) noexcept
	{
		Out.Code = Code;
		Out.SystemCode = 0;
		try
		{
			Out.Message = Text;
		}
		catch (...)
		{
			Out.Message.clear();
		}
	}

	// WHATWG UTF-8 replacement decoding. A valid prefix of an invalid sequence is consumed once.
	static std::string SseUtf8(std::string_view Text, bool& bValid)
	{
		std::string Result;
		bValid = true;
		for (std::size_t I = 0; I < Text.size();)
		{
			const auto Start = I;
			const auto Lead = static_cast<unsigned char>(Text[I++]);
			unsigned Count = Lead < 0x80					? 1
							 : Lead >= 0xc2 && Lead <= 0xdf ? 2
							 : Lead >= 0xe0 && Lead <= 0xef ? 3
							 : Lead >= 0xf0 && Lead <= 0xf4 ? 4
															: 0;
			bool Good = Count != 0;
			for (unsigned N = 1; Good && N < Count; ++N)
			{
				if (I == Text.size())
				{
					Good = false;
					break;
				}
				const auto Next = static_cast<unsigned char>(Text[I]);
				const unsigned Lower = N == 1 && Lead == 0xe0 ? 0xa0 : N == 1 && Lead == 0xf0 ? 0x90 : 0x80;
				const unsigned Upper = N == 1 && Lead == 0xed ? 0x9f : N == 1 && Lead == 0xf4 ? 0x8f : 0xbf;
				Good = Next >= Lower && Next <= Upper;
				if (Good)
				{
					++I;
				}
			}
			if (Good)
			{
				Result.append(Text.substr(Start, I - Start));
			}
			else
			{
				bValid = false;
				Result.append("\xef\xbf\xbd");
			}
		}
		return Result;
	}

	struct FSseReader::FImpl
	{
		explicit FImpl(FSseOptions InOptions) : Options(InOptions)
		{
			if (!Options.MaxLineBytes || !Options.MaxEventBytes || Options.MaxLineBytes > SIZE_MAX - 3 ||
				Options.MaxEventBytes >= SIZE_MAX)
			{
				throw std::invalid_argument("Invalid SSE limits");
			}
		}

		bool ProcessLine(FSseEvent& Out)
		{
			std::string_view Bytes = Line;
			if (bFirstLine && Bytes.substr(0, 3) == "\xef\xbb\xbf")
			{
				Bytes.remove_prefix(3);
			}
			bFirstLine = false;
			bool bValid;
			auto Decoded = SseUtf8(Bytes, bValid);
			Line.clear();
			if (Decoded.size() > Options.MaxLineBytes)
			{
				SseError(Failure, Network::EError::Limit, "SSE decoded line limit exceeded");
				return false;
			}
			if (Decoded.empty())
			{
				if (!Data.empty())
				{
					FSseEvent Event;
					Event.Data = Data.substr(0, Data.size() - 1);
					Event.Event = EventType.empty() ? "message" : EventType;
					Event.Id = IdBuffer;
					LastId = IdBuffer;
					Data.clear();
					EventType.clear();
					Out = std::move(Event);
					return true;
				}
				LastId = IdBuffer;
				EventType.clear();
				return false;
			}
			if (Decoded.front() == ':')
			{
				return false;
			}
			const auto Colon = Decoded.find(':');
			const auto Name = std::string_view(Decoded).substr(0, Colon);
			auto Value = Colon == std::string::npos ? std::string_view{} : std::string_view(Decoded).substr(Colon + 1);
			if (!Value.empty() && Value.front() == ' ')
			{
				Value.remove_prefix(1);
			}
			if (Name == "data")
			{
				if (Data.size() > Options.MaxEventBytes || Value.size() > Options.MaxEventBytes - Data.size())
				{
					SseError(Failure, Network::EError::Limit, "SSE event limit exceeded");
					return false;
				}
				Data.append(Value);
				Data += '\n';
			}
			else if (Name == "event")
			{
				EventType = Value;
			}
			else if (Name == "id" && Value.find('\0') == std::string_view::npos)
			{
				IdBuffer = Value;
			}
			else if (Name == "retry" && !Value.empty())
			{
				std::uint64_t Milliseconds;
				const auto Parsed = std::from_chars(Value.data(), Value.data() + Value.size(), Milliseconds);
				if (Parsed.ec == std::errc{} && Parsed.ptr == Value.data() + Value.size())
				{
					Retry = Milliseconds;
				}
			}
			return false;
		}

		FSseOptions Options;
		std::string Line, Data, EventType, IdBuffer, LastId;
		std::optional<std::uint64_t> Retry;
		Network::FError Failure;
		bool bFirstLine = true, bSkipLf = false, bDone = false;
	};

	FSseReader::FSseReader(FSseOptions Options) : Impl(std::make_unique<FImpl>(Options))
	{
	}
	FSseReader::~FSseReader() = default;
	FSseReader::FSseReader(FSseReader&&) noexcept = default;
	FSseReader& FSseReader::operator=(FSseReader&&) noexcept = default;

	FSseFeedResult FSseReader::Feed(std::string_view Bytes, bool bFinal, FSseEvent& Out)
	{
		auto& S = *Impl;
		std::size_t I = 0;
		try
		{
			if (S.bDone && !Bytes.empty())
			{
				SseError(S.Failure, Network::EError::State, "SSE reader is already finished");
			}
			if (S.Failure)
			{
				return {0, ESseReadState::Failed};
			}
			for (; I < Bytes.size();)
			{
				const char C = Bytes[I++];
				if (S.bSkipLf)
				{
					S.bSkipLf = false;
					if (C == '\n')
					{
						continue;
					}
				}
				if (C == '\r' || C == '\n')
				{
					S.bSkipLf = C == '\r';
					if (S.ProcessLine(Out))
					{
						return {I, ESseReadState::EventReady};
					}
					if (S.Failure)
					{
						return {I, ESseReadState::Failed};
					}
				}
				else
				{
					if (S.Line.size() >= S.Options.MaxLineBytes + (S.bFirstLine ? 3 : 0))
					{
						SseError(S.Failure, Network::EError::Limit, "SSE line limit exceeded");
						return {I, ESseReadState::Failed};
					}
					S.Line += C;
				}
			}
			if (bFinal)
			{
				// EOF does not synthesize an event delimiter or commit an unfinished ID field.
				S.Line.clear();
				S.Data.clear();
				S.EventType.clear();
				S.bDone = true;
			}
			return {I, S.bDone ? ESseReadState::Done : ESseReadState::NeedInput};
		}
		catch (const std::bad_alloc&)
		{
			SseError(S.Failure, Network::EError::Allocation, "SSE allocation failed");
		}
		catch (const std::length_error&)
		{
			SseError(S.Failure, Network::EError::Limit, "SSE capacity exceeded");
		}
		return {I, ESseReadState::Failed};
	}

	const Network::FError& FSseReader::Error() const noexcept
	{
		return Impl->Failure;
	}
	const std::string& FSseReader::LastEventId() const noexcept
	{
		return Impl->LastId;
	}
	std::optional<std::uint64_t> FSseReader::RetryMilliseconds() const noexcept
	{
		return Impl->Retry;
	}
	void FSseReader::Reset(std::string LastEventId)
	{
		auto Next = std::make_unique<FImpl>(Impl->Options);
		Next->LastId = LastEventId;
		Next->IdBuffer = std::move(LastEventId);
		Impl = std::move(Next);
	}

	static bool EncodeSse(
		const FSseEvent* Event, std::string_view Comment, std::string& Out, Network::FError& Error, std::uint64_t MaxBytes)
	{
		Error = {};
		try
		{
			std::string Text;
			auto Append = [&](std::string_view Part)
			{
				if (Part.size() > MaxBytes - std::min<std::uint64_t>(Text.size(), MaxBytes))
				{
					throw std::length_error("SSE output limit exceeded");
				}
				Text.append(Part);
			};
			auto Lines = [&](std::string_view Value, std::string_view Prefix)
			{
				if (Value.size() > MaxBytes)
				{
					throw std::length_error("SSE field limit exceeded");
				}
				bool bValid;
				SseUtf8(Value, bValid);
				if (!bValid || Value.find('\r') != std::string_view::npos)
				{
					throw std::invalid_argument("SSE fields require UTF-8 and LF line endings");
				}
				for (std::size_t Start = 0;;)
				{
					const auto End = Value.find('\n', Start);
					Append(Prefix);
					Append(Value.substr(Start, End == std::string_view::npos ? End : End - Start));
					Append("\n");
					if (End == std::string_view::npos)
					{
						break;
					}
					Start = End + 1;
				}
			};
			if (Event)
			{
				if (Event->Id.find_first_of("\r\n") != std::string::npos || Event->Id.find('\0') != std::string::npos ||
					Event->Event.find_first_of("\r\n") != std::string::npos)
				{
					throw std::invalid_argument("Invalid SSE ID or event type");
				}
				Lines(Event->Id, "id: ");
				Lines(Event->Event, "event: ");
				Lines(Event->Data, "data: ");
				Append("\n");
			}
			else
			{
				Lines(Comment, ": ");
			}
			Out = std::move(Text);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			SseError(Error, Network::EError::Allocation, "SSE allocation failed");
		}
		catch (const std::length_error&)
		{
			SseError(Error, Network::EError::Limit, "SSE output limit exceeded");
		}
		catch (const std::invalid_argument&)
		{
			SseError(Error, Network::EError::InvalidArgument, "Invalid SSE field");
		}
		return false;
	}

	bool TryEncodeSseEvent(const FSseEvent& Event, std::string& Out, Network::FError& Error, std::uint64_t MaxBytes)
	{
		return EncodeSse(&Event, {}, Out, Error, MaxBytes);
	}
	bool TryEncodeSseComment(std::string_view Comment, std::string& Out, Network::FError& Error, std::uint64_t MaxBytes)
	{
		return EncodeSse(nullptr, Comment, Out, Error, MaxBytes);
	}
} // namespace VeroScript::Http
