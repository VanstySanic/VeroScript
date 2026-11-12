// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontBackend.h"
#include "VeroScriptFontBackendFace.h"
#include "VeroScriptFontLayout.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_TRUETYPE_TABLES_H
#include <algorithm>
#include <climits>
#include <cmath>
#include <hb-ft.h>
#include <hb.h>
#include <mutex>
#include <tuple>

namespace VeroScript::FontBackend
{
	struct FFontBackendFace::FImpl
	{
		~FImpl()
		{
			if (Shaper)
			{
				hb_font_destroy(Shaper);
			}
			if (Face)
			{
				FT_Done_Face(Face);
			}
			if (Library)
			{
				FT_Done_FreeType(Library);
			}
		}
		bool SetSize(std::int32_t Size, float &Scale, std::string &Error)
		{
			Scale = 1;
			if (Size < 1 || Size > 16384)
			{
				Error = "font.invalid_size";
				return false;
			}
			auto Status = FT_Set_Char_Size(Face, 0, static_cast<FT_F26Dot6>(Size) * 64, 0, 0);
			if (Status && FT_HAS_FIXED_SIZES(Face))
			{
				int Best = -1, Distance = INT_MAX;
				for (int I = 0; I < Face->num_fixed_sizes; ++I)
				{
					const int Delta = Face->available_sizes[I].height - Size;
					const int Score = Delta < 0 ? INT_MAX / 2 - Delta : Delta;
					if (Score < Distance)
					{
						Best = I;
						Distance = Score;
					}
				}
				if (Best >= 0 && Face->available_sizes[Best].height > 0)
				{
					Scale = static_cast<float>(Size) / Face->available_sizes[Best].height;
					if (Scale >= .95f)
					{
						Scale = 1;
					}
					Status = FT_Select_Size(Face, Best);
				}
			}
			if (Status)
			{
				Error = "font.size_failed";
				return false;
			}
			return true;
		}
		std::vector<std::uint8_t> Bytes;
		FFaceInfo Info;
		FT_Library Library = nullptr;
		FT_Face Face = nullptr;
		hb_font_t *Shaper = nullptr;
		mutable std::mutex Mutex;
	};

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

	FFontBackendFace::~FFontBackendFace() = default;

	const FFaceInfo &FFontBackendFace::GetInfo() const noexcept
	{
		return Impl->Info;
	}

	std::uint32_t FFontBackendFace::GetGlyphIndex(char32_t Character) const
	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		return Character > 0x10ffff || (Character >= 0xd800 && Character <= 0xdfff)
		    ? 0
		    : FT_Get_Char_Index(Impl->Face, static_cast<FT_ULong>(Character));
	}

	bool FFontBackendFace::GetMetrics(std::int32_t PixelSize, FMetrics &Result, std::string &Error) const
	{
		Error.clear();
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		float Scale;
		if (!Impl->SetSize(PixelSize, Scale, Error))
		{
			return false;
		}
		auto Face = Impl->Face;
		FMetrics Value;
		Value.Ascent = Face->size->metrics.ascender * Scale / 64.f;
		Value.Descent = -Face->size->metrics.descender * Scale / 64.f;
		Value.LineSpacing = Face->size->metrics.height * Scale / 64.f;
		Value.UnderlinePosition = FT_MulFix(-Face->underline_position, Face->size->metrics.y_scale) * Scale / 64.f;
		Value.UnderlineThickness =
		    std::max(1.f, FT_MulFix(Face->underline_thickness, Face->size->metrics.y_scale) * Scale / 64.f);
		const auto X = FT_Get_Char_Index(Face, 'x');
		Value.XHeight =
		    X && !FT_Load_Glyph(Face, X, 0) ? Face->glyph->metrics.height * Scale / 64.f : .5 * Value.LineSpacing;
		Value.bHasEllipsis = FT_Get_Char_Index(Face, 0x2026) != 0;
		Result = Value;
		return true;
	}

	bool FFontBackendFace::Rasterize(std::uint32_t Glyph, std::int32_t PixelSize, FBitmap &Result, std::string &Error) const
	{
		Error.clear();
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		float Scale;
		if (!Impl->SetSize(PixelSize, Scale, Error))
		{
			return false;
		}
		if (Glyph >= static_cast<std::uint64_t>(Impl->Face->num_glyphs) ||
		    FT_Load_Glyph(Impl->Face, Glyph, FT_LOAD_COLOR) ||
		    FT_Render_Glyph(Impl->Face->glyph, FT_RENDER_MODE_NORMAL))
		{
			Error = "font.glyph_failed";
			return false;
		}
		auto Slot = Impl->Face->glyph;
		const auto &Source = Slot->bitmap;
		if (Source.pixel_mode != FT_PIXEL_MODE_MONO && Source.pixel_mode != FT_PIXEL_MODE_GRAY &&
		    Source.pixel_mode != FT_PIXEL_MODE_BGRA)
		{
			Error = "font.unsupported_bitmap";
			return false;
		}
		FBitmap Value;
		Value.bColor = Source.pixel_mode == FT_PIXEL_MODE_BGRA;
		Value.Width = static_cast<std::int32_t>(Source.width * Scale);
		Value.Height = static_cast<std::int32_t>(Source.rows * Scale);
		Value.BearingX = static_cast<std::int32_t>(Slot->bitmap_left * Scale);
		Value.BearingY = static_cast<std::int32_t>(Slot->bitmap_top * Scale);
		Value.Advance = static_cast<std::int32_t>((Slot->metrics.horiAdvance >> 6) * Scale);
		const int Channels = Value.bColor ? 4 : 1;
		if (Value.Width < 0 || Value.Height < 0 ||
		    static_cast<std::uint64_t>(Value.Width) * Value.Height * Channels > 64 * 1024 * 1024)
		{
			Error = "font.bitmap_limit";
			return false;
		}
		Value.Pixels.resize(static_cast<std::size_t>(Value.Width) * Value.Height * Channels);
		if (Scale == 1)
		{
			for (int Y = 0; Y < Value.Height; ++Y)
			{
				const auto *Row = Source.buffer + static_cast<std::ptrdiff_t>(Y) * Source.pitch;
				auto *Destination = Value.Pixels.data() + static_cast<std::size_t>(Y) * Value.Width * Channels;
				if (Source.pixel_mode == FT_PIXEL_MODE_GRAY)
				{
					std::copy_n(Row, Value.Width, Destination);
				}
				else
				{
					for (int X = 0; X < Value.Width; ++X)
					{
						if (!Value.bColor)
						{
							Destination[X] = (Row[X / 8] & (0x80 >> (X % 8))) ? 255 : 0;
						}
						else
						{
							Destination[4 * X] = Row[4 * X + 2];
							Destination[4 * X + 1] = Row[4 * X + 1];
							Destination[4 * X + 2] = Row[4 * X];
							Destination[4 * X + 3] = Row[4 * X + 3];
						}
					}
				}
			}
			Result = std::move(Value);
			return true;
		}
		for (int Y = 0; Y < Value.Height; ++Y)
		{
			for (int X = 0; X < Value.Width; ++X)
			{
				const double Left = X / static_cast<double>(Scale),
				             Right = std::min<double>((X + 1) / static_cast<double>(Scale), Source.width);
				const double Top = Y / static_cast<double>(Scale),
				             Bottom = std::min<double>((Y + 1) / static_cast<double>(Scale), Source.rows);
				for (int C = 0; C < Channels; ++C)
				{
					double Sum = 0;
					for (int SY = static_cast<int>(Top); SY < static_cast<int>(std::ceil(Bottom)); ++SY)
					{
						const auto *Row = Source.buffer + static_cast<std::ptrdiff_t>(SY) * Source.pitch;
						for (int SX = static_cast<int>(Left); SX < static_cast<int>(std::ceil(Right)); ++SX)
						{
							const auto Sample = Source.pixel_mode == FT_PIXEL_MODE_MONO
							    ? ((Row[SX / 8] & (0x80 >> (SX % 8))) ? 255 : 0)
							    : Row[SX * Channels + (Value.bColor && C < 3 ? 2 - C : C)];
							Sum += Sample * (std::min<double>(SX + 1, Right) - std::max<double>(SX, Left)) *
							    (std::min<double>(SY + 1, Bottom) - std::max<double>(SY, Top));
						}
					}
					Value.Pixels[(static_cast<std::size_t>(Y) * Value.Width + X) * Channels + C] =
					    static_cast<std::uint8_t>(std::clamp(Sum / ((Right - Left) * (Bottom - Top)), 0., 255.));
				}
			}
		}
		Result = std::move(Value);
		return true;
	}

	bool FFontBackendFace::Shape(std::string_view Text, const FShapeOptions &Options, FShapedText &Result,
	    std::string &Error) const
	{
		Error.clear();
		if (Text.size() > INT_MAX || Options.Language.size() > INT_MAX || Options.Script.size() > INT_MAX ||
		    (!(Options.Layout && Options.Layout->Owns(Text)) && !Utf8(Text)) || Options.Language.find('\0') != std::string::npos ||
		    Options.Script.find('\0') != std::string::npos ||
		    (Options.Direction != EDirection::Auto && Options.Direction != EDirection::LeftToRight &&
		        Options.Direction != EDirection::RightToLeft))
		{
			Error = "font.invalid_text";
			return false;
		}
		if (Options.ByteOffset < 0 || Options.ByteLength < -1 ||
		    static_cast<std::uint64_t>(Options.ByteOffset) > Text.size())
		{
			Error = "font.invalid_text_range";
			return false;
		}
		const auto Offset = static_cast<std::size_t>(Options.ByteOffset);
		const auto Length =
		    Options.ByteLength < 0 ? Text.size() - Offset : static_cast<std::uint64_t>(Options.ByteLength);
		const auto Boundary = [&](std::size_t N)
		{
			return N == Text.size() || (static_cast<unsigned char>(Text[N]) & 0xc0) != 0x80;
		};
		if (Length > Text.size() - Offset || !Boundary(Offset) || !Boundary(Offset + Length))
		{
			Error = "font.invalid_text_range";
			return false;
		}
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		float Scale;
		if (!Impl->SetSize(Options.PixelSize, Scale, Error))
		{
			return false;
		}
		if (!Impl->Shaper)
		{
			Impl->Shaper = hb_ft_font_create_referenced(Impl->Face);
		}
		hb_ft_font_changed(Impl->Shaper);
		hb_font_set_ptem(Impl->Shaper, static_cast<float>(Options.PixelSize));
		std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> Buffer(hb_buffer_create(), hb_buffer_destroy);
		hb_buffer_add_utf8(Buffer.get(), Text.data(), static_cast<int>(Text.size()), static_cast<unsigned>(Offset),
		    static_cast<int>(Length));
		if (!Options.Language.empty())
		{
			hb_buffer_set_language(Buffer.get(), hb_language_from_string(Options.Language.data(), static_cast<int>(Options.Language.size())));
		}
		if (!Options.Script.empty())
		{
			hb_buffer_set_script(Buffer.get(),
			    hb_script_from_string(Options.Script.data(), static_cast<int>(Options.Script.size())));
		}
		if (Options.Direction != EDirection::Auto)
		{
			hb_buffer_set_direction(Buffer.get(),
			    Options.Direction == EDirection::RightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
		}
		hb_buffer_guess_segment_properties(Buffer.get());
		hb_buffer_set_flags(Buffer.get(),
		    static_cast<hb_buffer_flags_t>((Offset == 0 ? HB_BUFFER_FLAG_BOT : 0) |
		        (Offset + Length == Text.size() ? HB_BUFFER_FLAG_EOT : 0)));
		hb_feature_t Feature{HB_TAG('k', 'e', 'r', 'n'), 0, 0, static_cast<unsigned>(-1)};
		hb_shape(Impl->Shaper, Buffer.get(), Options.bKerning ? nullptr : &Feature, Options.bKerning ? 0 : 1);
		if (!hb_buffer_allocation_successful(Buffer.get()))
		{
			Error = "font.shape_failed";
			return false;
		}
		unsigned Count = 0;
		const auto *Info = hb_buffer_get_glyph_infos(Buffer.get(), &Count);
		const auto *Positions = hb_buffer_get_glyph_positions(Buffer.get(), &Count);
		FShapedText Value;
		Value.Direction = hb_buffer_get_direction(Buffer.get()) == HB_DIRECTION_RTL ? EDirection::RightToLeft
		                                                                            : EDirection::LeftToRight;
		Value.Glyphs.reserve(Count);
		for (unsigned I = 0; I < Count; ++I)
		{
			Value.Glyphs.push_back({Info[I].codepoint, Info[I].cluster,
			    static_cast<std::int32_t>(std::lround(Positions[I].x_advance * Scale)),
			    static_cast<std::int32_t>(std::lround(Positions[I].y_advance * Scale)),
			    static_cast<std::int32_t>(std::lround(Positions[I].x_offset * Scale)),
			    static_cast<std::int32_t>(std::lround(Positions[I].y_offset * Scale))});
		}
		Result = std::move(Value);
		return true;
	}

	bool FFontBackendFace::GetVariations(const std::vector<std::uint8_t> &Bytes, std::int32_t FaceIndex,
	    std::vector<FVariation> &Result, std::string &Error)
	{
		auto Face = FFontBackendFace::Load(Bytes, FaceIndex, Error);
		if (!Face)
		{
			return false;
		}
		const auto Library = Face->Impl->Library;
		const auto Native = Face->Impl->Face;
		std::vector<FVariation> Values;
		FT_MM_Var *Variables = nullptr;
		if (!FT_Get_MM_Var(Native, &Variables))
		{
			const auto Release = [Library](FT_MM_Var *Value)
			{
				FT_Done_MM_Var(Library, Value);
			};
			std::unique_ptr<FT_MM_Var, decltype(Release)> Owned(Variables, Release);
			int WeightAxis = -1, WidthAxis = -1;
			for (unsigned I = 0; I < Variables->num_axis; ++I)
			{
				if (Variables->axis[I].tag == FT_MAKE_TAG('w', 'g', 'h', 't'))
				{
					WeightAxis = static_cast<int>(I);
				}
				if (Variables->axis[I].tag == FT_MAKE_TAG('w', 'd', 't', 'h'))
				{
					WidthAxis = static_cast<int>(I);
				}
			}
			for (unsigned I = 0; I < Variables->num_namedstyles; ++I)
			{
				const auto *Coordinates = Variables->namedstyle[I].coords;
				Values.push_back({WeightAxis >= 0 ? static_cast<int>(Coordinates[WeightAxis] / 65536) : 400,
				    WidthAxis >= 0 ? static_cast<int>(Coordinates[WidthAxis] / 65536) : 100,
				    static_cast<int>(I + 1)});
			}
		}

		std::sort(Values.begin(), Values.end(), [](const auto &A, const auto &B)
		{
			return std::tie(A.Weight, A.Width) < std::tie(B.Weight, B.Width);
		});
		Result = std::move(Values);
		return true;
	}

	std::shared_ptr<FFontBackendFace> FFontBackendFace::Load(const std::vector<std::uint8_t> &Bytes, std::int32_t FaceIndex,
	    std::string &Error, std::int32_t NamedInstance)
	{
		Error.clear();
		if (Bytes.empty() || Bytes.size() > 128 * 1024 * 1024 || Bytes.size() > LONG_MAX || FaceIndex < 0 ||
		    FaceIndex > 65535 || NamedInstance < 0 || NamedInstance > 32767)
		{
			Error = "font.invalid_source";
			return {};
		}
		auto Value = std::make_unique<FImpl>();
		Value->Bytes = Bytes;
		if (FT_Init_FreeType(&Value->Library) ||
		    FT_New_Memory_Face(Value->Library, Value->Bytes.data(), static_cast<FT_Long>(Value->Bytes.size()),
		        (static_cast<FT_Long>(NamedInstance) << 16) | FaceIndex, &Value->Face))
		{
			Error = "font.invalid_font";
			return {};
		}
		auto Face = Value->Face;
		if (!Face->charmap && FT_Select_Charmap(Face, FT_ENCODING_APPLE_ROMAN))
		{
			Error = "font.character_map_unavailable";
			return {};
		}
		Value->Info.Family = Face->family_name ? Face->family_name : "";
		Value->Info.bItalic = (Face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
		auto Table = static_cast<TT_OS2 *>(FT_Get_Sfnt_Table(Face, FT_SFNT_OS2));
		Value->Info.Weight = Table && Table->usWeightClass ? Table->usWeightClass
		    : (Face->style_flags & FT_STYLE_FLAG_BOLD)     ? 700
		                                                   : 400;
		return std::shared_ptr<FFontBackendFace>(new FFontBackendFace(std::move(Value)));
	}

	FFontBackendFace::FFontBackendFace(std::unique_ptr<FImpl> Value) : Impl(std::move(Value))
	{
	}

	class FFontService final : public Font::IService
	{
	public:
		const char *GetBackendId() const noexcept override
		{
			return BackendId;
		}
		std::shared_ptr<Font::IFace> Load(const std::vector<std::uint8_t> &Bytes, std::int32_t Index,
		    std::string &Error, std::int32_t Instance) const override
		{
			return FFontBackendFace::Load(Bytes, Index, Error, Instance);
		}
		bool GetVariations(const std::vector<std::uint8_t> &Bytes, std::int32_t Index, std::vector<FVariation> &Result,
		    std::string &Error) const override
		{
			return FFontBackendFace::GetVariations(Bytes, Index, Result, Error);
		}
	};
	std::shared_ptr<Font::IService> CreateFontService()
	{
		return std::make_shared<FFontService>();
	}
} // namespace VeroScript::FontBackend
