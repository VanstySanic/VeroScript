// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTextInternal.h"

#include <algorithm>
#include <array>
#include <limits>

namespace VeroScript::StandardLibrary
{
	namespace
	{
		using EStatus = EServiceStatus;
		// Linear search bounds adversarial repeated-prefix input and checks the same
		// work meter during prefix construction, fallback comparisons, and scanning.
		class FSearch
		{
		public:
			FServiceResult Prepare(std::string_view InNeedle, const FServiceContext& Context)
			{
				Needle = InNeedle;
				if (auto R = Context.CheckText(Needle.size()); !R) return R;
				// A single byte has no proper prefix. Keep its original work charge
				// without allocating a prefix table for common split/replace delimiters.
				if (Needle.size() <= 1) return Context.Check(Needle.size());
				if (Needle.size() <= InlinePrefix.size())
				{
					return BuildPrefix(InlinePrefix, Context, [&](std::size_t I, std::size_t Matched)
					{
						InlinePrefix[I] = Matched;
					});
				}
				// Keep large prefix writes inside the metered construction loop.
				LargePrefix.clear();
				LargePrefix.reserve(Needle.size());
				return BuildPrefix(LargePrefix, Context, [&](std::size_t, std::size_t Matched)
				{
					LargePrefix.push_back(Matched);
				});
			}
			template <class F>
			FServiceResult Scan(std::string_view Text, bool Overlap, const FServiceContext& Context, F&& Found, bool FirstOnly = false) const
			{
				Detail::FWorkMeter Meter(Context);
				const auto* const Prefix = Needle.size() <= InlinePrefix.size() ? InlinePrefix.data() : LargePrefix.data();
				for (std::size_t I = 0, Matched = 0; I < Text.size(); ++I)
				{
					if (auto R = Meter.Add(); !R) return R;
					while (Matched && Text[I] != Needle[Matched])
					{
						if (auto R = Meter.Add(); !R) return R;
						Matched = Prefix[Matched - 1];
					}
					if (Text[I] == Needle[Matched])
					{
						++Matched;
					}
					if (Matched == Needle.size())
					{
						if (auto R = Found(I + 1 - Matched); !R) return R;
						if (FirstOnly) return Meter.Flush();
						Matched = Overlap && Needle.size() > 1 ? Prefix[Matched - 1] : 0;
					}
				}
				return Meter.Flush();
			}

		private:
			template <class TPrefix, class FAppend>
			FServiceResult BuildPrefix(const TPrefix& Prefix, const FServiceContext& Context, FAppend&& Append)
			{
				Detail::FWorkMeter Meter(Context);
				for (std::size_t I = 0, Matched = 0; I < Needle.size(); ++I)
				{
					if (auto R = Meter.Add(); !R) return R;
					if (I)
					{
						while (Matched && Needle[I] != Needle[Matched])
						{
							if (auto R = Meter.Add(); !R) return R;
							Matched = Prefix[Matched - 1];
						}
						if (Needle[I] == Needle[Matched])
						{
							++Matched;
						}
					}
					Append(I, Matched);
				}
				return Meter.Flush();
			}

			std::string_view Needle;
			std::array<std::size_t, 16> InlinePrefix;
			std::vector<std::size_t> LargePrefix;
		};

		FServiceResult FindSingleByte(std::string_view Text, std::string_view Needle, std::int64_t& Output,
		    const FServiceContext& Context)
		{
			std::size_t Offset = 0;
			for (;;)
			{
				// The meter polls before examining the last byte of a full block.
				// A caller's work callback can stop or alter that byte at the poll.
				const auto Count = std::min(Detail::WorkBlock - 1, Text.size() - Offset);
				const auto Match = Text.substr(Offset, Count).find(Needle.front());
				if (Match != std::string_view::npos)
				{
					if (auto R = Context.Check(Match + 1); !R) return R;
					Output = static_cast<std::int64_t>(Offset + Match);
					return {};
				}
				Offset += Count;
				if (Offset == Text.size())
				{
					if (auto R = Context.Check(Count); !R) return R;
					Output = -1;
					return {};
				}
				if (auto R = Context.Check(Detail::WorkBlock); !R) return R;
				if (Text[Offset++] == Needle.front())
				{
					if (auto R = Context.Check(); !R) return R;
					Output = static_cast<std::int64_t>(Offset - 1);
					return {};
				}
			}
		}

		FServiceResult Compare(std::string_view Text, std::string_view Part, bool End, bool& Output,
		    const FServiceContext& Context)
		{
			if (auto R = Context.CheckText(Text.size()); !R) return R;
			if (auto R = Context.CheckText(Part.size()); !R) return R;
			if (Part.size() > Text.size())
			{
				Output = false;
				return {};
			}
			const auto Offset = End ? Text.size() - Part.size() : 0;
			for (std::size_t I = 0; I < Part.size();)
			{
				const auto Count = std::min(Detail::WorkBlock, Part.size() - I);
				if (auto R = Context.Check(Count); !R) return R;
				if (Text.substr(Offset + I, Count) != Part.substr(I, Count))
				{
					Output = false;
					return {};
				}
				I += Count;
			}
			Output = true;
			return {};
		}
	}

	FServiceResult StartsWith(std::string_view Text, std::string_view Prefix, bool& Output, const FServiceContext& C)
	{
		return Compare(Text, Prefix, false, Output, C);
	}
	FServiceResult EndsWith(std::string_view Text, std::string_view Suffix, bool& Output, const FServiceContext& C)
	{
		return Compare(Text, Suffix, true, Output, C);
	}

	FServiceResult LastFind(std::string_view Text, std::string_view Needle, std::int64_t& Output, const FServiceContext& C)
	{
		if (auto R = C.CheckText(Text.size()); !R) return R;
		if (auto R = C.CheckText(Needle.size()); !R) return R;
		if (Needle.size() > Text.size())
		{
			Output = -1;
			return {};
		}
		FSearch Search;
		if (auto R = Search.Prepare(Needle, C); !R) return R;
		std::int64_t Found = Needle.empty() ? static_cast<std::int64_t>(Text.size()) : -1;
		if (!Needle.empty())
		{
			if (auto R = Search.Scan(Text, true, C, [&](std::size_t I)
			{
				Found = static_cast<std::int64_t>(I);
				return FServiceResult{};
			});
			    !R)
			{
				return R;
			}
		}
		Output = Found;
		return {};
	}

	FServiceResult FirstFind(std::string_view Text, std::string_view Needle, std::int64_t& Output, const FServiceContext& C)
	{
		if (auto R = C.CheckText(Text.size()); !R) return R;
		if (auto R = C.CheckText(Needle.size()); !R) return R;
		if (Needle.empty())
		{
			Output = 0;
			return {};
		}
		if (Needle.size() > Text.size())
		{
			Output = -1;
			return {};
		}
		FSearch Search;
		if (auto R = Search.Prepare(Needle, C); !R) return R;
		if (Needle.size() == 1) return FindSingleByte(Text, Needle, Output, C);
		std::int64_t Found = -1;
		if (auto R = Search.Scan(Text, false, C, [&](std::size_t I)
		{
			Found = static_cast<std::int64_t>(I);
			return FServiceResult{};
		}, true);
		    !R)
		{
			return R;
		}
		Output = Found;
		return {};
	}

	FServiceResult Repeat(std::string_view Text, std::int64_t Count, std::string& Output, const FServiceContext& C)
	{
		if (Count < 0) return {EStatus::InvalidArgument, 0};
		if (auto R = C.CheckText(Text.size()); !R) return R;
		if (Text.size() && static_cast<std::uint64_t>(Count) > std::numeric_limits<std::size_t>::max() / Text.size())
		{
			return {EStatus::LimitExceeded, 0};
		}
		const auto Size = Text.size() * static_cast<std::size_t>(Count);
		if (auto R = Detail::CheckedLength(Size, C); !R) return R;
		std::string Result;
		Result.reserve(Size);
		if (Size)
		{
			if (auto R = Detail::Append(Result, Text, C); !R) return R;
			// Doubling the already prepared prefix bounds append calls for short input.
			while (Result.size() < Size)
			{
				const auto CopySize = std::min(Result.size(), Size - Result.size());
				for (std::size_t I = 0; I < CopySize;)
				{
					const auto N = std::min(CopySize - I, Detail::WorkBlock);
					if (auto R = C.Check(N); !R) return R;
					Result.append(Result.data() + I, N);
					I += N;
				}
			}
		}
		if (auto R = C.Check(); !R) return R;
		Output.swap(Result);
		return {};
	}

	FServiceResult Replace(std::string_view Text, std::string_view From, std::string_view To,
	    std::string& Output, const FServiceContext& C)
	{
		if (auto R = C.CheckText(Text.size()); !R) return R;
		if (auto R = C.CheckText(To.size()); !R) return R;
		if (auto R = C.CheckText(From.size()); !R) return R;
		if (From.size() > Text.size()) return Repeat(Text, 1, Output, C);
		FSearch Search;
		if (auto R = Search.Prepare(From, C); !R) return R;
		std::size_t Count = 0;
		if (!From.empty())
		{
			if (auto R = Search.Scan(Text, false, C, [&](std::size_t)
			{
				++Count;
				return FServiceResult{};
			});
			    !R)
			{
				return R;
			}
		}
		std::size_t Size = Text.size() - Count * From.size();
		if (To.size() && Count > (std::numeric_limits<std::size_t>::max() - Size) / To.size())
		{
			return {EStatus::LimitExceeded, 0};
		}
		Size += Count * To.size();
		if (auto R = Detail::CheckedLength(Size, C); !R) return R;
		std::string Result;
		Result.reserve(Size);
		std::size_t Start = 0;
		if (Count)
		{
			if (auto R = Search.Scan(Text, false, C, [&](std::size_t I)
			{
				if (auto Appended = Detail::Append(Result, Text.substr(Start, I - Start), C); !Appended) return Appended;
				if (auto Appended = Detail::Append(Result, To, C); !Appended) return Appended;
				Start = I + From.size();
				return FServiceResult{};
			});
			    !R)
			{
				return R;
			}
		}
		if (auto R = Detail::Append(Result, Text.substr(Start), C); !R) return R;
		if (auto R = C.Check(); !R) return R;
		Output.swap(Result);
		return {};
	}

	FServiceResult Trim(std::string_view Text, bool Left, bool Right, std::string& Output, const FServiceContext& C)
	{
		if (auto R = C.CheckText(Text.size()); !R) return R;
		Detail::FWorkMeter Meter(C);
		std::size_t Begin = 0, End = Text.size();
		while (Left && Begin < End && IsAsciiWhitespace(static_cast<unsigned char>(Text[Begin])))
		{
			if (auto R = Meter.Add(); !R) return R;
			++Begin;
		}
		while (Right && End > Begin && IsAsciiWhitespace(static_cast<unsigned char>(Text[End - 1])))
		{
			if (auto R = Meter.Add(); !R) return R;
			--End;
		}
		if (auto R = Meter.Flush(); !R) return R;
		std::string Result;
		Result.reserve(End - Begin);
		if (auto R = Detail::Append(Result, Text.substr(Begin, End - Begin), C); !R) return R;
		if (auto R = C.Check(); !R) return R;
		Output.swap(Result);
		return {};
	}

	FServiceResult Split(std::string_view Text, std::string_view Separator, std::vector<std::string>& Output,
	    const FServiceContext& C)
	{
		if (Separator.empty()) return {EStatus::InvalidArgument, 0};
		if (auto R = C.CheckText(Text.size()); !R) return R;
		FSearch Search;
		if (auto R = C.CheckText(Separator.size()); !R) return R;
		const bool CanMatch = Separator.size() <= Text.size();
		if (CanMatch)
		{
			if (auto R = Search.Prepare(Separator, C); !R) return R;
		}
		std::vector<std::string> Result;
		const auto Add = [&](std::string_view Part) -> FServiceResult
		{
			if (Result.size() == Result.max_size() || (C.Limits.MaxSplitParts && Result.size() >= C.Limits.MaxSplitParts))
			{
				return {EStatus::LimitExceeded, 0};
			}
			std::string Value;
			Value.reserve(Part.size());
			if (auto R = Detail::Append(Value, Part, C); !R) return R;
			Result.push_back(std::move(Value));
			return {};
		};
		std::size_t Start = 0;
		if (CanMatch)
		{
			if (auto R = Search.Scan(Text, false, C, [&](std::size_t I)
			{
				const auto Added = Add(Text.substr(Start, I - Start));
				Start = I + Separator.size();
				return Added;
			});
			    !R)
			{
				return R;
			}
		}
		if (auto R = Add(Text.substr(Start)); !R) return R;
		if (auto R = C.Check(); !R) return R;
		Output.swap(Result);
		return {};
	}

	FServiceResult Join(const std::vector<std::string_view>& Parts, std::string_view Separator,
	    std::string& Output, const FServiceContext& C)
	{
		if (auto R = C.CheckText(Separator.size()); !R) return R;
		std::size_t Size = 0;
		for (std::size_t I = 0; I < Parts.size(); ++I)
		{
			if (auto R = C.CheckText(Parts[I].size()); !R) return R;
			if (auto R = C.Check(1); !R) return R;
			for (auto N : {Parts[I].size(), I ? Separator.size() : std::size_t{0}})
			{
				if (N > std::numeric_limits<std::size_t>::max() - Size) return {EStatus::LimitExceeded, 0};
				Size += N;
			}
		}
		if (auto R = Detail::CheckedLength(Size, C); !R) return R;
		std::string Result;
		Result.reserve(Size);
		for (std::size_t I = 0; I < Parts.size(); ++I)
		{
			if (I)
			{
				if (auto R = Detail::Append(Result, Separator, C); !R) return R;
			}
			if (auto R = Detail::Append(Result, Parts[I], C); !R) return R;
		}
		if (auto R = C.Check(); !R) return R;
		Output.swap(Result);
		return {};
	}

	FServiceResult ValidateUtf8(std::string_view Text, const FServiceContext& C)
	{
		if (auto R = C.CheckText(Text.size()); !R) return R;
		Detail::FWorkMeter Meter(C);
		for (std::size_t I = 0; I < Text.size();)
		{
			const auto Start = I;
			const auto Invalid = [&](std::size_t Offset) -> FServiceResult
			{
				if (auto R = Meter.Add(I - Start); !R) return R;
				if (auto R = Meter.Flush(); !R) return R;
				return {EStatus::InvalidText, Offset};
			};
			const auto Lead = static_cast<unsigned char>(Text[I++]);
			unsigned Remaining = 0;
			std::uint32_t Code = 0, Minimum = 0;
			if (Lead < 0x80)
			{ /* ASCII, including embedded NUL */
			}
			else if (Lead >= 0xc2 && Lead <= 0xdf)
			{
				Remaining = 1;
				Code = Lead & 0x1f;
				Minimum = 0x80;
			}
			else if (Lead >= 0xe0 && Lead <= 0xef)
			{
				Remaining = 2;
				Code = Lead & 0xf;
				Minimum = 0x800;
			}
			else if (Lead >= 0xf0 && Lead <= 0xf4)
			{
				Remaining = 3;
				Code = Lead & 7;
				Minimum = 0x10000;
			}
			else
			{
				return Invalid(Start);
			}
			if (Remaining > Text.size() - I) return Invalid(Start);
			while (Remaining--)
			{
				const auto Byte = static_cast<unsigned char>(Text[I++]);
				if ((Byte & 0xc0) != 0x80) return Invalid(I - 1);
				Code = (Code << 6) | (Byte & 0x3f);
			}
			if (Code < Minimum || Code > 0x10ffff || (Code >= 0xd800 && Code <= 0xdfff))
			{
				return Invalid(Start);
			}
			if (auto R = Meter.Add(I - Start); !R) return R;
		}
		return Meter.Flush();
	}
}
