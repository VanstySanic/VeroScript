// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStb.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../../ThirdParty/stb/stb_image.h"
#include <array>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace
{
	using FBytes = std::vector<std::uint8_t>;

	void AppendBigEndian(FBytes &Bytes, std::uint32_t Value)
	{
		for (int Shift = 24; Shift >= 0; Shift -= 8)
		{
			Bytes.push_back(static_cast<std::uint8_t>(Value >> Shift));
		}
	}

	void AppendLittleEndian(FBytes &Bytes, std::uint32_t Value)
	{
		for (int Shift = 0; Shift <= 24; Shift += 8)
		{
			Bytes.push_back(static_cast<std::uint8_t>(Value >> Shift));
		}
	}

	FBytes TopDownBmp(std::int32_t Height = -2)
	{
		FBytes Bytes{'B', 'M'};
		AppendLittleEndian(Bytes, 70); // File size.
		AppendLittleEndian(Bytes, 0); // Reserved words.
		AppendLittleEndian(Bytes, 54); // Pixel offset.
		AppendLittleEndian(Bytes, 40); // BITMAPINFOHEADER.
		AppendLittleEndian(Bytes, 2);
		AppendLittleEndian(Bytes, static_cast<std::uint32_t>(Height));
		Bytes.insert(Bytes.end(), {1, 0, 24, 0}); // One plane, BGR24.
		AppendLittleEndian(Bytes, 0); // BI_RGB.
		AppendLittleEndian(Bytes, 16); // Two padded rows.
		Bytes.resize(54, 0);
		Bytes.insert(Bytes.end(), {0, 0, 255, 0, 255, 0, 0, 0, // Top: red, green, padding.
		                              255, 0, 0, 255, 255, 255, 0, 0}); // Bottom: blue, white, padding.
		return Bytes;
	}

	FBytes Pnm16(bool bRgb)
	{
		const std::string Header = bRgb ? "P6\n2 1\n65535\n" : "P5\n4 1\n65535\n";
		FBytes Bytes(Header.begin(), Header.end());
		if (bRgb)
		{
			Bytes.insert(Bytes.end(), {0x12, 0x34, 0xab, 0xcd, 0, 0, 0xff, 0xff, 0x56, 0xef, 0x78, 0x9a});
		}
		else
		{
			Bytes.insert(Bytes.end(), {0, 0, 0x12, 0x34, 0xab, 0xcd, 0xff, 0xff});
		}
		return Bytes;
	}

	FBytes Pnm(bool bRgb, std::uint32_t Maximum, const std::vector<std::uint32_t> &Samples)
	{
		const auto Width = Samples.size() / (bRgb ? 3 : 1);
		const auto Header = std::string(bRgb ? "P6\n# RGB\n" : "P5\n# gray\n") + std::to_string(Width) +
		    " 1\n# maximum\n" + std::to_string(Maximum) + "\n";
		FBytes Bytes(Header.begin(), Header.end());
		for (const auto Sample : Samples)
		{
			if (Maximum > 255)
			{
				Bytes.push_back(static_cast<std::uint8_t>(Sample >> 8));
			}
			Bytes.push_back(static_cast<std::uint8_t>(Sample));
		}
		return Bytes;
	}

	void CheckPnmMaximum(const std::shared_ptr<VeroScript::Image::IService> &Service)
	{
		struct FCase
		{
			bool bRgb;
			std::uint32_t Maximum;
			std::vector<std::uint32_t> Samples;
			FBytes Colors;
		};
		for (const auto &Case : std::vector<FCase>{
		         {false, 1, {0, 1}, {0, 0, 0, 255, 255, 255, 255, 255}},
		         {false, 100, {0, 50, 100}, {0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255}},
		         {true, 31, {31, 0, 15}, {255, 0, 123, 255}},
		         {true, 100, {100, 50, 0}, {255, 128, 0, 255}},
		         {false, 256, {0, 128, 256}, {0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255}},
		         {false, 1023, {0, 512, 1023}, {0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255}},
		         {true, 1023, {1023, 512, 0}, {255, 128, 0, 255}},
		         {false, 65534, {0, 32767, 65534}, {0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255}},
		         {false, 65535, {0xff00}, {255, 255, 255, 255}},
		         {true, 255, {10, 13, 35}, {10, 13, 35, 255}}})
		{
			std::string Error;
			VeroScript::Image::FDecodedImage Image;
			auto Bytes = Pnm(Case.bRgb, Case.Maximum, Case.Samples);
			if (!Service->Decode(Bytes, Image, Error) || !Error.empty() || Image.Height != 1 ||
			    Image.Width != static_cast<std::int32_t>(Case.Colors.size() / 4) || Image.Rgba != Case.Colors)
			{
				throw std::runtime_error("PNM maximum did not map black/white and intermediate samples correctly: " + Error);
			}
			Bytes.pop_back();
			if (Service->Decode(Bytes, Image, Error) || Error.empty() || Image.Rgba != Case.Colors)
			{
				throw std::runtime_error("truncated PNM was accepted or changed output");
			}
		}
		for (const auto Separator : {' ', '\t', '\r'})
		{
			auto Bytes = Pnm(true, 100, {100, 50, 0});
			Bytes[Bytes.size() - 4] = static_cast<std::uint8_t>(Separator);
			std::string Error;
			VeroScript::Image::FDecodedImage Image;
			if (!Service->Decode(Bytes, Image, Error) || Image.Rgba != FBytes{255, 128, 0, 255})
			{
				throw std::runtime_error("PNM raster separator was interpreted as a sample: " + Error);
			}
		}
		auto InvalidSeparator = Pnm(false, 100, {50});
		InvalidSeparator[InvalidSeparator.size() - 2] = 'x';
		for (const auto &Bytes : std::vector<FBytes>{Pnm(false, 0, {0}), Pnm(false, 65536, {0}),
		         Pnm(false, 100, {101}), Pnm(true, 1023, {1023, 1024, 0}), InvalidSeparator})
		{
			std::string Error;
			VeroScript::Image::FDecodedImage Image{1, 1, {1, 2, 3, 255}};
			if (Service->Decode(Bytes, Image, Error) || Error.empty() || Image.Width != 1 || Image.Height != 1 ||
			    Image.Rgba != FBytes{1, 2, 3, 255})
			{
				throw std::runtime_error("invalid PNM maximum/sample was accepted or changed output");
			}
		}
	}

	FBytes TgaBytes(std::uint8_t Type, std::uint8_t Bits, const FBytes &Pixels,
	    const FBytes &Palette = {}, std::uint8_t PaletteBits = 0, std::uint8_t Width = 2)
	{
		FBytes Bytes(18, 0);
		Bytes[0] = 3; // Image ID length.
		Bytes[1] = Palette.empty() ? 0 : 1;
		Bytes[2] = Type;
		Bytes[5] = Palette.empty() ? 0 : 2;
		Bytes[7] = PaletteBits;
		Bytes[12] = Width;
		Bytes[14] = 1;
		Bytes[16] = Bits;
		Bytes[17] = 0x20;
		if (Bits == 32 || ((Type == 3 || Type == 11) && Bits == 16))
		{
			Bytes[17] |= 8; // Eight alpha bits for RGBA or gray-alpha pixels.
		}
		Bytes.insert(Bytes.end(), {'I', 'D', '!'});
		Bytes.insert(Bytes.end(), Palette.begin(), Palette.end());
		Bytes.insert(Bytes.end(), Pixels.begin(), Pixels.end());
		return Bytes;
	}

	void CheckTgaPayload(const std::shared_ptr<VeroScript::Image::IService> &Service)
	{
		struct FCase
		{
			FBytes Input, Expected;
		};
		const FBytes RedGreen{255, 0, 0, 255, 0, 255, 0, 255};
		const std::array<FCase, 12> Cases{{{TgaBytes(3, 8, {12, 34}), {12, 12, 12, 255, 34, 34, 34, 255}},
		    {TgaBytes(3, 16, {12, 128, 34, 255}), {12, 12, 12, 128, 34, 34, 34, 255}},
		    {TgaBytes(2, 15, {0, 0x7c, 0xe0, 3}), RedGreen},
		    {TgaBytes(2, 16, {0, 0x7c, 0xe0, 3}), RedGreen},
		    {TgaBytes(2, 24, {0, 0, 255, 0, 255, 0}), RedGreen},
		    {TgaBytes(2, 32, {0, 0, 255, 128, 0, 255, 0, 255}),
		        {255, 0, 0, 128, 0, 255, 0, 255}},
		    {TgaBytes(1, 8, {0, 1}, {0, 0, 255, 0, 255, 0}, 24), RedGreen},
		    {TgaBytes(1, 16, {0, 0, 1, 0}, {0, 0x7c, 0xe0, 3}, 16), RedGreen},
		    {TgaBytes(1, 8, {0, 1}, {0, 0, 255, 128, 0, 255, 0, 255}, 32),
		        {255, 0, 0, 128, 0, 255, 0, 255}},
		    {TgaBytes(10, 24, {0x81, 0, 0, 255, 0, 0, 255, 0}, {}, 0, 3),
		        {255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255}},
		    {TgaBytes(11, 8, {0x80, 12, 1, 34, 56, 0x80, 78}, {}, 0, 4),
		        {12, 12, 12, 255, 34, 34, 34, 255, 56, 56, 56, 255, 78, 78, 78, 255}},
		    {TgaBytes(9, 8, {0x81, 0, 0, 1}, {0, 0, 255, 0, 255, 0}, 24, 3),
		        {255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255}}}};
		for (const auto &Case : Cases)
		{
			VeroScript::Image::FDecodedImage Result;
			std::string Error;
			const auto Width = static_cast<std::int32_t>(Case.Expected.size() / 4);
			if (!Service->Decode(Case.Input, Result, Error) || Result.Width != Width || Result.Height != 1 ||
			    Result.Rgba != Case.Expected)
			{
				throw std::runtime_error("complete TGA was rejected or changed pixels: " + Error);
			}
			for (std::size_t Length = 0; Length < Case.Input.size(); ++Length)
			{
				const FBytes Prefix(Case.Input.begin(), Case.Input.begin() + Length);
				if (Service->Decode(Prefix, Result, Error) || Error.empty() || Result.Width != Width ||
				    Result.Height != 1 || Result.Rgba != Case.Expected)
				{
					throw std::runtime_error("truncated TGA was accepted or changed output");
				}
			}
		}
		VeroScript::Image::FDecodedImage Result{2, 1, RedGreen};
		std::string Error;
		auto ExcessRun = TgaBytes(10, 24, {0x82, 0, 0, 255}); // Three pixels in a two-pixel image.
		if (Service->Decode(ExcessRun, Result, Error) || Error.empty() || Result.Width != 2 || Result.Height != 1 ||
		    Result.Rgba != RedGreen)
		{
			throw std::runtime_error("TGA RLE packet exceeded remaining pixels or changed output");
		}
		auto NonZeroPalette = TgaBytes(1, 8, {1, 2}, {0, 0, 255, 0, 255, 0}, 24);
		NonZeroPalette[3] = 1;
		if (Service->Decode(NonZeroPalette, Result, Error) || Error.empty() || Result.Width != 2 || Result.Height != 1 ||
		    Result.Rgba != RedGreen)
		{
			throw std::runtime_error("unsupported nonzero TGA palette origin was accepted or changed output");
		}
		auto IgnoredPaletteOrigin = TgaBytes(2, 24, {0, 0, 255, 0, 255, 0});
		IgnoredPaletteOrigin[3] = 1; // Irrelevant when no color map is present.
		if (!Service->Decode(IgnoredPaletteOrigin, Result, Error) || Result.Width != 2 || Result.Height != 1 ||
		    Result.Rgba != RedGreen)
		{
			throw std::runtime_error("irrelevant TGA palette fields changed non-indexed decoding: " + Error);
		}
	}

	void CheckTgaOrigins(const std::shared_ptr<VeroScript::Image::IService> &Service)
	{
		const FBytes Colors{255, 0, 0, 255, 0, 255, 0, 255,
		    0, 0, 255, 255, 255, 255, 255, 255};
		const FBytes Grays{12, 12, 12, 255, 34, 34, 34, 255,
		    56, 56, 56, 255, 78, 78, 78, 255};
		for (const auto Type : std::array<std::uint8_t, 4>{2, 10, 3, 11})
		{
			const bool bGray = Type == 3 || Type == 11;
			const auto &Expected = bGray ? Grays : Colors;
			for (const auto Origin : std::array<std::uint8_t, 4>{0, 0x10, 0x20, 0x30})
			{
				FBytes Pixels;
				for (int Row = 0; Row < 2; ++Row)
				{
					if (Type >= 8)
					{
						Pixels.push_back(1); // A raw RLE packet containing one complete two-pixel row.
					}
					const auto Y = (Origin & 0x20) ? Row : 1 - Row;
					for (int Column = 0; Column < 2; ++Column)
					{
						const auto X = (Origin & 0x10) ? 1 - Column : Column;
						const auto Offset = static_cast<std::size_t>(Y * 2 + X) * 4;
						if (bGray)
						{
							Pixels.push_back(Expected[Offset]);
						}
						else
						{
							Pixels.insert(Pixels.end(), {Expected[Offset + 2], Expected[Offset + 1], Expected[Offset]});
						}
					}
				}
				auto Bytes = TgaBytes(Type, bGray ? 8 : 24, Pixels);
				Bytes[14] = 2;
				Bytes[17] = Origin;
				VeroScript::Image::FDecodedImage Result;
				std::string Error;
				if (!Service->Decode(Bytes, Result, Error) || !Error.empty() || Result.Width != 2 ||
				    Result.Height != 2 || Result.Rgba != Expected)
				{
					throw std::runtime_error("TGA descriptor origin was not normalized: type=" +
					    std::to_string(Type) + " origin=" + std::to_string(Origin) + " " + Error);
				}
			}
		}
	}

	void AppendChunk(FBytes &Bytes, const char *Type, const FBytes &Data)
	{
		AppendBigEndian(Bytes, static_cast<std::uint32_t>(Data.size()));
		const auto Start = Bytes.size();
		Bytes.insert(Bytes.end(), Type, Type + 4);
		Bytes.insert(Bytes.end(), Data.begin(), Data.end());
		std::uint32_t Crc = UINT32_MAX;
		for (auto I = Start; I < Bytes.size(); ++I)
		{
			Crc ^= Bytes[I];
			for (int Bit = 0; Bit < 8; ++Bit)
			{
				Crc = (Crc >> 1) ^ ((Crc & 1) ? 0xedb88320u : 0u);
			}
		}
		AppendBigEndian(Bytes, ~Crc);
	}

	FBytes CgbiPng(const char *UnsupportedChunk = nullptr)
	{
		FBytes Bytes{137, 80, 78, 71, 13, 10, 26, 10};
		AppendChunk(Bytes, "CgBI", {0x40, 0, 0, 0});
		AppendChunk(Bytes, "IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0});
		if (UnsupportedChunk)
		{
			AppendChunk(Bytes, UnsupportedChunk, {});
		}
		// Final raw-DEFLATE stored block: filter byte, then premultiplied B,G,R,A.
		AppendChunk(Bytes, "IDAT", {1, 5, 0, 0xfa, 0xff, 0, 10, 20, 30, 128});
		AppendChunk(Bytes, "IEND", {});
		return Bytes;
	}

	void CheckConcurrentFailures(const std::shared_ptr<VeroScript::Image::IService> &Service)
	{
		std::array<std::future<void>, 2> Calls;
		for (std::size_t I = 0; I < Calls.size(); ++I)
		{
			Calls[I] = std::async(std::launch::async, [Service, I]
			{
				auto Decoder = I == 0 ? Service : VeroScript::Stb::CreateImageService();
				const auto Bytes = CgbiPng(I == 0 ? "ABCD" : "EFGH");
				// One opaque black pixel, including a two-entry global palette and LZW data.
				const FBytes Gif{
				    'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0,
				    0, 0, 0, 255, 255, 255,
				    0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0,
				    2, 2, 0x44, 1, 0, 0x3b};
				VeroScript::Image::FDecodedImage Result;
				Result.Width = 7;
				Result.Height = 5;
				Result.Rgba = {1, 2, 3, 4};
				std::string Error;
				// Exercise the unsupported critical-chunk branch under race detectors.
				for (int Repeat = 0; Repeat < 100; ++Repeat)
				{
					if (Decoder->Decode(Bytes, Result, Error) || Error.empty() || Result.Width != 7 ||
					    Result.Height != 5 || Result.Rgba != FBytes{1, 2, 3, 4})
					{
						throw std::runtime_error("concurrent decode failure changed output or lost its error");
					}
					VeroScript::Image::FDecodedImage GifResult;
					if (!Decoder->Decode(Gif, GifResult, Error) || !Error.empty() || GifResult.Width != 1 ||
					    GifResult.Height != 1 || GifResult.Rgba != FBytes{0, 0, 0, 255})
					{
						throw std::runtime_error("concurrent GIF decode failed");
					}
				}
			});
		}
		for (auto &Call : Calls)
		{
			Call.get();
		}
	}
} // namespace

int main()
{
	try
	{
		using namespace VeroScript::Image;
		std::string Error;
		auto Service = VeroScript::Stb::CreateImageService();
		CheckPnmMaximum(Service);
		CheckTgaPayload(Service);
		CheckTgaOrigins(Service);
		const auto Cgbi = CgbiPng();
		FDecodedImage Straight;
		if (!Service->Decode(Cgbi, Straight, Error) || Straight.Width != 1 || Straight.Height != 1 ||
		    Straight.Rgba != FBytes{60, 40, 20, 128})
		{
			throw std::runtime_error("CgBI PNG did not produce straight RGBA: " + Error);
		}
		int HostWidth = 0, HostHeight = 0, HostChannels = 0;
		std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> HostPixels(
		    stbi_load_from_memory(Cgbi.data(), static_cast<int>(Cgbi.size()), &HostWidth, &HostHeight,
		        &HostChannels, 4),
		    stbi_image_free);
		if (!HostPixels || HostWidth != 1 || HostHeight != 1 ||
		    FBytes(HostPixels.get(), HostPixels.get() + 4) != FBytes{10, 20, 30, 128})
		{
			throw std::runtime_error("service initialization changed the embedding host's stb defaults");
		}
		CheckConcurrentFailures(Service);
		const auto Bmp = TopDownBmp();
		FDecodedImage BmpImage;
		const FBytes BmpPixels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
		if (!Service->Decode(Bmp, BmpImage, Error) || BmpImage.Width != 2 || BmpImage.Height != 2 ||
		    BmpImage.Rgba != BmpPixels)
		{
			throw std::runtime_error("top-down BMP dimensions or row order: " + Error);
		}
		FDecodeOptions BmpLimits;
		BmpLimits.MaxHeight = 1;
		if (Service->Decode(Bmp, BmpImage, Error, BmpLimits) || Error.empty() || BmpImage.Width != 2 ||
		    BmpImage.Height != 2 || BmpImage.Rgba != BmpPixels)
		{
			throw std::runtime_error("top-down BMP height limit or failure output preservation");
		}
		if (Service->Decode(TopDownBmp(std::numeric_limits<std::int32_t>::min()), BmpImage, Error) || Error.empty() ||
		    BmpImage.Width != 2 || BmpImage.Height != 2 || BmpImage.Rgba != BmpPixels)
		{
			throw std::runtime_error("BMP minimum signed height was accepted or changed output");
		}
		for (bool bRgb : {false, true})
		{
			auto Pnm = Pnm16(bRgb);
			FDecodedImage PnmImage;
			const auto PnmWidth = bRgb ? 2 : 4;
			const FBytes PnmPixels = bRgb ? FBytes{0x12, 0xab, 0, 255, 255, 0x56, 0x78, 255}
			                              : FBytes{0, 0, 0, 255, 0x12, 0x12, 0x12, 255, 0xab, 0xab, 0xab, 255,
			                                    255, 255, 255, 255};
			if (!Service->Decode(Pnm, PnmImage, Error) || PnmImage.Width != PnmWidth || PnmImage.Height != 1 ||
			    PnmImage.Rgba != PnmPixels)
			{
				throw std::runtime_error("16-bit PNM sample byte order or RGBA conversion: " + Error);
			}
			Pnm.pop_back();
			if (Service->Decode(Pnm, PnmImage, Error) || Error.empty() || PnmImage.Width != PnmWidth ||
			    PnmImage.Height != 1 || PnmImage.Rgba != PnmPixels)
			{
				throw std::runtime_error("truncated 16-bit PNM was accepted or changed output");
			}
		}
		std::vector<std::uint8_t> Tga(22, 0);
		Tga[2] = 2;
		Tga[12] = Tga[14] = 1;
		Tga[16] = 32;
		Tga[17] = 0x28;
		Tga[18] = 10;
		Tga[19] = 20;
		Tga[20] = 30;
		Tga[21] = 128;
		FDecodedImage Image;
		if (!Service->Decode(Tga, Image, Error) || Image.Width != 1 || Image.Height != 1 ||
		    Image.Rgba != std::vector<std::uint8_t>{30, 20, 10, 128})
		{
			throw std::runtime_error("independent TGA decode / straight alpha: " + Error);
		}
		const auto Original = Image.Rgba;
		// An embedding host can use its own stb implementation and decoder settings.
		auto Rows = Tga;
		Rows[14] = 2;
		Rows.insert(Rows.end(), {1, 2, 3, 255});
		stbi_set_flip_vertically_on_load(1);
		FDecodedImage Unflipped;
		if (!Service->Decode(Rows, Unflipped, Error) ||
		    Unflipped.Rgba != std::vector<std::uint8_t>{30, 20, 10, 128, 3, 2, 1, 255})
		{
			throw std::runtime_error("host stb decoder settings affected the image service");
		}
		int Width = 0, Height = 0, Channels = 0;
		std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> Flipped(
		    stbi_load_from_memory(Rows.data(), static_cast<int>(Rows.size()), &Width, &Height, &Channels, 4),
		    stbi_image_free);
		if (!Flipped || Width != 1 || Height != 2 || Flipped.get()[0] != 3)
		{
			throw std::runtime_error("embedding host's stb decoder settings were changed");
		}
		stbi_set_flip_vertically_on_load(0);
		if (Service->Decode({}, Image, Error) || Error.empty() || Image.Rgba != Original)
		{
			throw std::runtime_error("decode failure changed result");
		}
		FDecodeOptions Limits;
		Limits.MaxSourceBytes = 1;
		if (Service->Decode(Tga, Image, Error, Limits) || Error.empty())
		{
			throw std::runtime_error("source limit ignored");
		}
		Limits = {};
		Limits.MaxPixels = 0;
		if (Service->Decode(Tga, Image, Error, Limits) || Error.empty())
		{
			throw std::runtime_error("pixel limit ignored");
		}
		Image.Rgba.pop_back();
		if (Validate(Image, Error) || Error.empty())
		{
			throw std::runtime_error("invalid pixel buffer accepted");
		}
		std::cout << "Independent Image tests passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
