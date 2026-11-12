// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStb.h"
// Keep decoder symbols and configuration private to this backend.
#define STB_IMAGE_STATIC
#define STBIDEF [[maybe_unused]] static
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
// The service uses its own errors; some upstream failure strings use shared scratch data.
#define STBI_NO_FAILURE_STRINGS
#include <climits>
#include <memory>
#include <utility>
#include <stb_image.h>
#ifndef STBI_THREAD_LOCAL
#include <mutex>
#endif

namespace VeroScript::Stb
{
	using namespace Image;
	namespace
	{
		bool Dimensions(std::int32_t Width, std::int32_t Height, const FDecodeOptions &Options)
		{
			return Width > 0 && Height > 0 && Width <= Options.MaxWidth && Height <= Options.MaxHeight &&
			    static_cast<std::uint64_t>(Width) * Height <= Options.MaxPixels &&
			    static_cast<std::uint64_t>(Width) * Height <= SIZE_MAX / 4;
		}
		#ifndef STBI_NO_PNM
		int ReadPnmMaximum(const std::vector<std::uint8_t> &Bytes)
		{
			// The pinned loader discards Maxval. Reuse its header parsing so normalization
			// interprets comments and separators exactly as the subsequent load does.
			stbi__context Context;
			stbi__start_mem(&Context, Bytes.data(), static_cast<int>(Bytes.size()));
			stbi__skip(&Context, 2);
			char Next = static_cast<char>(stbi__get8(&Context));
			int Value = 0;
			for (int Field = 0; Field < 3; ++Field)
			{
				stbi__pnm_skip_whitespace(&Context, &Next);
				Value = stbi__pnm_getinteger(&Context, &Next);
			}
			return stbi__pnm_isspace(Next) ? Value : 0;
		}
		#endif
		#ifndef STBI_NO_TGA
		bool CompleteTga(const std::vector<std::uint8_t> &Bytes, std::int32_t Width, std::int32_t Height)
		{
			// Other enabled formats have distinctive signatures with a second byte greater than one.
			if (Bytes.size() < 3 || Bytes[1] > 1)
			{
				return true;
			}
			const bool bIndexed = Bytes[1] == 1;
			const auto Type = Bytes[2];
			if (bIndexed ? Type != 1 && Type != 9 : Type != 2 && Type != 3 && Type != 10 && Type != 11)
			{
				return true;
			}
			if (Bytes.size() < 18)
			{
				return false;
			}
			if (bIndexed && (Bytes[3] != 0 || Bytes[4] != 0))
			{
				return false; // The pinned decoder only handles zero-based palettes correctly.
			}
			// stb can ignore missing pixel bytes. Check stored payload bounds before decoding.
			const auto PaletteEntries = static_cast<std::size_t>(Bytes[5]) + (static_cast<std::size_t>(Bytes[6]) << 8);
			const auto PaletteBytes = bIndexed ? PaletteEntries * ((Bytes[7] + 7) / 8) : 0;
			const auto HeaderBytes = 18 + static_cast<std::size_t>(Bytes[0]) + PaletteBytes;
			const auto PixelBytes = static_cast<std::size_t>((Bytes[16] + 7) / 8);
			if (PixelBytes == 0 || HeaderBytes > Bytes.size())
			{
				return false;
			}
			auto Remaining = static_cast<std::uint64_t>(Width) * Height;
			if (Type < 8)
			{
				return Remaining <= (Bytes.size() - HeaderBytes) / PixelBytes;
			}
			auto Cursor = HeaderBytes;
			while (Remaining != 0)
			{
				if (Cursor >= Bytes.size())
				{
					return false;
				}
				const auto Packet = Bytes[Cursor++];
				const auto Count = static_cast<std::size_t>(Packet & 127) + 1;
				const auto Stored = (Packet & 128) ? 1 : Count;
				if (Count > Remaining || Stored > (Bytes.size() - Cursor) / PixelBytes)
				{
					return false;
				}
				Cursor += Stored * PixelBytes;
				Remaining -= Count;
			}
			return true;
		}
		#endif
	} // namespace

	class FImageService final : public Image::IService
	{
	public:
		const char *GetBackendId() const noexcept override
		{
			return "stb";
		}
		bool Decode(const std::vector<std::uint8_t> &Bytes, FDecodedImage &Result, std::string &Error,
		    const FDecodeOptions &Options) const override;
	};
	bool FImageService::Decode(const std::vector<std::uint8_t> &Bytes, FDecodedImage &Result, std::string &Error,
	    const FDecodeOptions &Options) const
	{
		Error.clear();
		#ifndef STBI_THREAD_LOCAL
		// GIF headers also write stb's error pointer when failure strings are disabled.
		// Share this lock across service instances only on toolchains without TLS.
		static std::mutex DecoderMutex;
		const std::lock_guard<std::mutex> Lock(DecoderMutex);
		#endif
		int Width = 0, Height = 0, Channels = 0;
		if (Bytes.empty() || Bytes.size() > INT_MAX || Bytes.size() > Options.MaxSourceBytes ||
		    !stbi_info_from_memory(Bytes.data(), static_cast<int>(Bytes.size()), &Width, &Height, &Channels))
		{
			Error = "image.invalid_image";
			return false;
		}
		// BMP info preserves the signed height; loading normalizes top-down images.
		if (Height < 0 && Height != INT_MIN && Bytes.size() >= 2 && Bytes[0] == 'B' && Bytes[1] == 'M')
		{
			Height = -Height;
		}
		if (!Dimensions(Width, Height, Options))
		{
			Error = "image.invalid_image";
			return false;
		}
		#ifndef STBI_NO_TGA
		stbi__context FormatContext;
		stbi__start_mem(&FormatContext, Bytes.data(), static_cast<int>(Bytes.size()));
		const bool bTga = stbi__tga_test(&FormatContext) != 0;
		if (bTga && !CompleteTga(Bytes, Width, Height))
		{
			Error = "image.decode_failed";
			return false;
		}
		#endif
		bool bPnm = false;
		std::uint32_t PnmMaximum = 0;
		#ifndef STBI_NO_PNM
		bPnm = Bytes.size() >= 2 && Bytes[0] == 'P' && (Bytes[1] == '5' || Bytes[1] == '6');
		if (bPnm)
		{
			const auto Maximum = ReadPnmMaximum(Bytes);
			if (Maximum < 1 || Maximum > 65535)
			{
				Error = "image.invalid_image";
				return false;
			}
			PnmMaximum = static_cast<std::uint32_t>(Maximum);
		}
		#endif
		const bool bPnm16 = PnmMaximum > 255;
		const bool bNormalizePnm = bPnm && PnmMaximum != 255 && PnmMaximum != 65535;
		std::unique_ptr<void, decltype(&stbi_image_free)> Pixels(
		    bPnm16 ? static_cast<void *>(stbi_load_16_from_memory(Bytes.data(), static_cast<int>(Bytes.size()),
		                 &Width, &Height, &Channels, 0))
		           : static_cast<void *>(stbi_load_from_memory(Bytes.data(), static_cast<int>(Bytes.size()),
		                 &Width, &Height, &Channels, bNormalizePnm ? 0 : 4)),
		    stbi_image_free);
		if (!Pixels)
		{
			Error = "image.decode_failed";
			return false;
		}
		if (!Dimensions(Width, Height, Options) || (bPnm && Channels != 1 && Channels != 3))
		{
			Error = "image.invalid_image";
			return false;
		}
		FDecodedImage Candidate;
		Candidate.Width = Width;
		Candidate.Height = Height;
		const auto PixelCount = static_cast<std::size_t>(Width) * Height;
		const auto *Source = static_cast<const stbi_uc *>(Pixels.get());
		if (PnmMaximum == 65535)
		{
			// Preserve the full-range 16-bit fast path and its existing high-byte quantization.
			Candidate.Rgba.resize(PixelCount * 4);
			const auto Stride = static_cast<std::size_t>(Channels) * 2;
			for (std::size_t I = 0; I < PixelCount; ++I, Source += Stride)
			{
				Candidate.Rgba[I * 4] = Source[0];
				Candidate.Rgba[I * 4 + 1] = Source[Channels == 3 ? 2 : 0];
				Candidate.Rgba[I * 4 + 2] = Source[Channels == 3 ? 4 : 0];
				Candidate.Rgba[I * 4 + 3] = 255;
			}
		}
		else if (bNormalizePnm)
		{
			// PNM samples use their declared range. Its 16-bit loader preserves big-endian bytes.
			Candidate.Rgba.resize(PixelCount * 4);
			const auto SampleBytes = bPnm16 ? 2 : 1;
			const auto Stride = static_cast<std::size_t>(Channels) * SampleBytes;
			for (std::size_t I = 0; I < PixelCount; ++I, Source += Stride)
			{
				for (int Channel = 0; Channel < Channels; ++Channel)
				{
					const auto Offset = Channel * SampleBytes;
					const std::uint32_t Sample = bPnm16 ? (static_cast<std::uint32_t>(Source[Offset]) << 8) | Source[Offset + 1]
					                                    : Source[Offset];
					if (Sample > PnmMaximum)
					{
						Error = "image.invalid_image";
						return false;
					}
					Candidate.Rgba[I * 4 + Channel] =
					    static_cast<std::uint8_t>((Sample * 255 + PnmMaximum / 2) / PnmMaximum);
				}
				if (Channels == 1)
				{
					Candidate.Rgba[I * 4 + 1] = Candidate.Rgba[I * 4 + 2] = Candidate.Rgba[I * 4];
				}
				Candidate.Rgba[I * 4 + 3] = 255;
			}
		}
		else
		{
			Candidate.Rgba.assign(Source, Source + PixelCount * 4);
		}
		#ifndef STBI_NO_TGA
		if (bTga && (Bytes[17] & 0x10) != 0)
		{
			// The pinned decoder normalizes vertical origin but ignores the right-hand origin.
			const auto RowWidth = static_cast<std::size_t>(Width);
			for (std::size_t Row = 0; Row < PixelCount; Row += RowWidth)
			{
				for (std::size_t Column = 0; Column < RowWidth / 2; ++Column)
				{
					const auto Left = (Row + Column) * 4;
					const auto Right = (Row + RowWidth - 1 - Column) * 4;
					for (std::size_t Channel = 0; Channel < 4; ++Channel)
					{
						std::swap(Candidate.Rgba[Left + Channel], Candidate.Rgba[Right + Channel]);
					}
				}
			}
		}
		#endif
		Result = std::move(Candidate);
		return true;
	}
	std::shared_ptr<Image::IService> CreateImageService()
	{
		#ifndef STBI_NO_PNG
		// Configure the private decoder once, before any service can reach it.
		// This also works when a host toolchain disables stb thread-local storage.
		static const bool Configured = []
		{
			stbi_convert_iphone_png_to_rgb(1);
			stbi_set_unpremultiply_on_load(1);
			return true;
		}();
		static_cast<void>(Configured);
		#endif
		return std::make_shared<FImageService>();
	}
} // namespace VeroScript::Stb
