// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFont.h"
#include "VeroScriptFontLayout.h"
#include <algorithm>

namespace VeroScript::Font
{
	namespace
	{
		bool Utf8(std::string_view Text)
		{
			for (std::size_t I = 0; I < Text.size();)
			{
				const auto Lead = static_cast<unsigned char>(Text[I++]);
				if (Lead < 128)
				{
					continue;
				}
				const int Count = Lead >= 0xc2 && Lead <= 0xdf ? 1
				    : Lead <= 0xef && Lead >= 0xe0             ? 2
				    : Lead >= 0xf0 && Lead <= 0xf4             ? 3
				                                               : -1;
				if (Count < 0 || Text.size() - I < static_cast<std::size_t>(Count))
				{
					return false;
				}
				std::uint32_t Code = Lead & (0x7f >> Count);
				for (int J = 0; J < Count; ++J)
				{
					const auto Next = static_cast<unsigned char>(Text[I++]);
					if ((Next & 0xc0) != 0x80)
					{
						return false;
					}
					Code = (Code << 6) | (Next & 63);
				}
				if (Code < (Count == 1          ? 0x80u
				                   : Count == 2 ? 0x800u
				                                : 0x10000u) ||
				    Code > 0x10ffff || (Code >= 0xd800 && Code <= 0xdfff))
				{
					return false;
				}
			}
			return true;
		}
	} // namespace
	bool ValidateFaceDescription(const FFontFace &Value, std::string &Error)
	{
		Error.clear();
		if (Value.Source.empty() || Value.Source.find('\0') != std::string::npos || Value.Family.empty() ||
		    Value.Family.size() > 16384 || Value.Family.find('\0') != std::string::npos || !Utf8(Value.Family) ||
		    Value.FaceIndex < 0 || Value.FaceIndex > 65535 || Value.Weight < 1 || Value.Weight > 1000)
		{
			Error = "font.invalid_font_face";
			return false;
		}
		return true;
	}
	bool ShapeWithFallback(const std::shared_ptr<IFace> &Primary, const std::vector<std::shared_ptr<IFace>> &Fallbacks,
	    std::string_view Text, const FShapeOptions &Options, FResolvedText &Result,
	    std::string &Error)
	{
		Error.clear();
		if (!Primary)
		{
			Error = "font.missing_face";
			return false;
		}
		if (Text.size() > INT32_MAX ||
		    (!(Options.Layout && Options.Layout->Owns(Text)) && !Utf8(Text)) ||
		    Options.ByteOffset < 0 || Options.ByteLength < -1 ||
		    static_cast<std::uint64_t>(Options.ByteOffset) > Text.size())
		{
			Error = "font.invalid_text";
			return false;
		}
		const auto Start = static_cast<std::uint32_t>(Options.ByteOffset);
		const auto Length =
		    Options.ByteLength < 0 ? Text.size() - Start : static_cast<std::uint64_t>(Options.ByteLength);
		if (Length > Text.size() - Start)
		{
			Error = "font.invalid_text_range";
			return false;
		}
		const auto Last = static_cast<std::uint32_t>(Start + Length);
		const auto Boundary = [&](std::uint32_t Offset)
		{
			return Offset == Text.size() || (static_cast<unsigned char>(Text[Offset]) & 0xc0) != 0x80;
		};
		if (!Boundary(Start) || !Boundary(Last))
		{
			Error = "font.invalid_text_range";
			return false;
		}
		FShapedText Initial;
		if (!Primary->Shape(Text, Options, Initial, Error))
		{
			return false;
		}
		FResolvedText Candidate;
		Candidate.Direction = Initial.Direction;
		// Resolve missing runs together so fallback ligatures and contextual forms survive.
		std::function<bool(const std::shared_ptr<IFace> &, const FShapedText &, std::uint32_t, std::uint32_t,
		    std::size_t)>
		    Resolve;
		Resolve = [&](const auto &Face, const auto &Shaped, std::uint32_t Begin, std::uint32_t End, std::size_t Next)
		{
			bool HasMissing = false;
			for (const auto &Glyph : Shaped.Glyphs)
			{
				if (Glyph.Cluster < Begin || Glyph.Cluster >= End || !Boundary(Glyph.Cluster))
				{
					Error = "font.invalid_shape_result";
					return false;
				}
				HasMissing |= Glyph.Glyph == 0;
			}
			if (HasMissing)
			{
				while (Next < Fallbacks.size() &&
				    (!Fallbacks[Next] || Fallbacks[Next] == Face || Fallbacks[Next] == Primary))
				{
					++Next;
				}
			}
			if (!HasMissing || Next >= Fallbacks.size())
			{
				for (const auto &Glyph : Shaped.Glyphs)
				{
					Candidate.Glyphs.push_back({Face, Glyph});
				}
				return true;
			}
			std::vector<std::uint32_t> Boundaries{End};
			Boundaries.reserve(Shaped.Glyphs.size() + 1);
			for (const auto &Glyph : Shaped.Glyphs)
			{
				Boundaries.push_back(Glyph.Cluster);
			}
			std::sort(Boundaries.begin(), Boundaries.end());
			Boundaries.erase(std::unique(Boundaries.begin(), Boundaries.end()), Boundaries.end());
			for (std::size_t I = 0; I < Shaped.Glyphs.size();)
			{
				auto GroupEnd = I;
				bool Missing = false;
				const auto Cluster = Shaped.Glyphs[I].Cluster;
				while (GroupEnd < Shaped.Glyphs.size() && Shaped.Glyphs[GroupEnd].Cluster == Cluster)
				{
					Missing |= Shaped.Glyphs[GroupEnd++].Glyph == 0;
				}
				if (Missing)
				{
					auto Low = Cluster;
					auto High = *std::upper_bound(Boundaries.begin(), Boundaries.end(), Cluster);
					// Adjacent missing clusters form a single logical run, including RTL runs.
					while (GroupEnd < Shaped.Glyphs.size())
					{
						const auto C = Shaped.Glyphs[GroupEnd].Cluster;
						auto J = GroupEnd;
						bool GroupMissing = false;
						while (J < Shaped.Glyphs.size() && Shaped.Glyphs[J].Cluster == C)
						{
							GroupMissing |= Shaped.Glyphs[J++].Glyph == 0;
						}
						if (!GroupMissing)
						{
							break;
						}
						Low = std::min(Low, C);
						High = std::max(High, *std::upper_bound(Boundaries.begin(), Boundaries.end(), C));
						GroupEnd = J;
					}
					bool Replaced = false;
					auto Range = Options;
					Range.Direction = Candidate.Direction;
					Range.ByteOffset = Low;
					Range.ByteLength = High - Low;
					for (auto F = Next; F < Fallbacks.size(); ++F)
					{
						if (!Fallbacks[F] || Fallbacks[F] == Face || Fallbacks[F] == Primary)
						{
							continue;
						}
						FShapedText Replacement;
						if (!Fallbacks[F]->Shape(Text, Range, Replacement, Error))
						{
							return false;
						}
						if (std::none_of(Replacement.Glyphs.begin(), Replacement.Glyphs.end(),
						        [](const auto &G)
						{
							return G.Glyph != 0;
						}))
						{
							continue;
						}
						if (!Resolve(Fallbacks[F], Replacement, Low, High, F + 1))
						{
							return false;
						}
						Replaced = true;
						break;
					}
					if (Replaced)
					{
						I = GroupEnd;
						continue;
					}
				}
				for (; I < GroupEnd; ++I)
				{
					Candidate.Glyphs.push_back({Face, Shaped.Glyphs[I]});
				}
			}
			return true;
		};
		if (!Resolve(Primary, Initial, Start, Last, 0))
		{
			return false;
		}
		Result = std::move(Candidate);
		return true;
	}
	bool Validate(const FBitmap &Value, std::string &Error)
	{
		Error.clear();
		if (Value.Width < 0 || Value.Height < 0)
		{
			Error = "font.invalid_bitmap";
			return false;
		}
		const auto Bytes = static_cast<std::uint64_t>(Value.Width) * Value.Height * (Value.bColor ? 4 : 1);
		if (Bytes > 64 * 1024 * 1024 || Bytes != Value.Pixels.size())
		{
			Error = "font.invalid_bitmap";
			return false;
		}
		return true;
	}
} // namespace VeroScript::Font
