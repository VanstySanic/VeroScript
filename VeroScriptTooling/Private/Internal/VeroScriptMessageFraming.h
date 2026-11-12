// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <charconv>
#include <cctype>
#include <cstddef>
#include <istream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace VeroScript::Tooling::Internal
{
	enum class EMessageReadStatus
	{
		Message,
		EndOfStream,
		InvalidFrame
	};

	struct FMessageReadResult
	{
		EMessageReadStatus Status = EMessageReadStatus::EndOfStream;
		std::string Body;
	};

	// Formatted input and error logging can flush a tied stream implicitly.
	// Protocol output is written on another thread (or under its own mutex), so
	// these hidden flushes must be disabled for the entire session lifetime.
	class FScopedProtocolStreamTies final
	{
	public:
		FScopedProtocolStreamTies(std::istream& Input, std::ostream& Output)
			: Input(Input), InputTie(Input.tie(nullptr)), ErrorTie(std::cerr.tie())
			, bDetachedErrorTie(ErrorTie == &Output)
		{
			if (bDetachedErrorTie)
			{
				std::cerr.tie(nullptr);
			}
		}
		~FScopedProtocolStreamTies()
		{
			Input.tie(InputTie);
			if (bDetachedErrorTie)
			{
				std::cerr.tie(ErrorTie);
			}
		}
		FScopedProtocolStreamTies(const FScopedProtocolStreamTies&) = delete;
		FScopedProtocolStreamTies& operator=(const FScopedProtocolStreamTies&) = delete;

	private:
		std::istream& Input;
		std::ostream* InputTie;
		std::ostream* ErrorTie;
		bool bDetachedErrorTie;
	};

	// LSP/DAP messages are local tooling input, but they may arrive over the DAP
	// TCP adapter. Bound both the header and payload before allocating storage.
	inline constexpr std::size_t MaxMessageHeaderBytes = 64u * 1024u;
	inline constexpr std::size_t MaxMessagePayloadBytes = 64u * 1024u * 1024u;



	inline bool EqualsAsciiIgnoreCase(
		const std::string_view Left,
		const std::string_view Right)
	{
		if (Left.size() != Right.size())
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Left.size(); ++Index)
		{
			const unsigned char A = static_cast<unsigned char>(Left[Index]);
			const unsigned char B = static_cast<unsigned char>(Right[Index]);
			if (std::tolower(A) != std::tolower(B))
			{
				return false;
			}
		}
		return true;
	}

	inline FMessageReadResult ReadContentLengthMessage(std::istream& Input)
	{
		std::optional<std::size_t> ContentLength;
		std::size_t HeaderBytes = 0;
		std::string Line;
		for (;;)
		{
			Line.clear();
			bool bReadAny = false;
			bool bReadNewline = false;
			char Character = '\0';
			while (Input.get(Character))
			{
				bReadAny = true;
				if (HeaderBytes == MaxMessageHeaderBytes)
				{
					return {EMessageReadStatus::InvalidFrame, {}};
				}
				++HeaderBytes;
				if (Character == '\n')
				{
					bReadNewline = true;
					break;
				}
				Line.push_back(Character);
			}
			if (!bReadAny)
			{
				return HeaderBytes == 0
					? FMessageReadResult{EMessageReadStatus::EndOfStream, {}}
					: FMessageReadResult{EMessageReadStatus::InvalidFrame, {}};
			}
			if (!bReadNewline)
			{
				return {EMessageReadStatus::InvalidFrame, {}};
			}
			if (!Line.empty() && Line.back() == '\r')
			{
				Line.pop_back();
			}
			if (Line.empty())
			{
				if (!ContentLength)
				{
					return {EMessageReadStatus::InvalidFrame, {}};
				}
				std::string Body(*ContentLength, '\0');
				Input.read(Body.data(), static_cast<std::streamsize>(*ContentLength));
				if (static_cast<std::size_t>(Input.gcount()) != *ContentLength)
				{
					return {EMessageReadStatus::InvalidFrame, {}};
				}
				return {EMessageReadStatus::Message, std::move(Body)};
			}

			const std::size_t Colon = Line.find(':');
			if (Colon == std::string::npos)
			{
				return {EMessageReadStatus::InvalidFrame, {}};
			}
			const std::string_view Name(Line.data(), Colon);
			if (!EqualsAsciiIgnoreCase(Name, "Content-Length"))
			{
				continue;
			}
			if (ContentLength)
			{
				return {EMessageReadStatus::InvalidFrame, {}};
			}

			std::string_view Value(Line.data() + Colon + 1, Line.size() - Colon - 1);
			while (!Value.empty()
				&& std::isspace(static_cast<unsigned char>(Value.front())))
			{
				Value.remove_prefix(1);
			}
			while (!Value.empty()
				&& std::isspace(static_cast<unsigned char>(Value.back())))
			{
				Value.remove_suffix(1);
			}
			std::size_t Parsed = 0;
			const auto Parse = std::from_chars(
				Value.data(), Value.data() + Value.size(), Parsed);
			if (Value.empty() || Parse.ec != std::errc{} || Parse.ptr != Value.data() + Value.size()
				|| Parsed > MaxMessagePayloadBytes
				|| Parsed > static_cast<std::size_t>(
					std::numeric_limits<std::streamsize>::max()))
			{
				return {EMessageReadStatus::InvalidFrame, {}};
			}
			ContentLength = Parsed;
		}
	}
}
