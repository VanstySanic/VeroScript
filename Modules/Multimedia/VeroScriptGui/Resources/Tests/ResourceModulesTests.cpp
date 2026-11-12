// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptFontBindings.h"
#include "VeroScriptFontBackend.h"
#include "VeroScriptImageBindings.h"
#include "VeroScriptSession.h"
#include "VeroScriptStb.h"
#include <iostream>
#include <stdexcept>

int main()
{
	try
	{
		using namespace VeroScript;
		struct FHost final : IEngineHost
		{
			void OnPrintLine(std::string_view) override
			{
			}
			void OnDiagnosticLine(std::string_view Text) override
			{
				std::cerr << Text << '\n';
			}
		} Host;
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		std::string Error;
		auto Reader = [](const std::string &Source, std::vector<std::uint8_t> &Bytes, std::string &Failure)
		{
			if (Source != "memory:image")
			{
				Failure = "test.resource_missing";
				return false;
			}
			Bytes.assign(22, 0);
			Bytes[2] = 2;
			Bytes[12] = Bytes[14] = 1;
			Bytes[16] = 32;
			Bytes[17] = 0x28;
			Bytes[20] = 255;
			Bytes[21] = 128;
			return true;
		};
		if (!Image::RegisterScriptBindings(
				Session.GetBindingRegistry(),
				[](const auto &Backend, auto &Failure)
				{
					if (Backend != "stb")
					{
						Failure = "test.image_backend";
						return std::shared_ptr<Image::IService>{};
					}
					return Stb::CreateImageService();
				},
				Error, Reader) ||
			!Font::RegisterScriptBindings(
				Session.GetBindingRegistry(),
				[](const auto &Backend, auto &Failure)
				{
					if (Backend != "font-backend")
					{
						Failure = "test.font_backend";
						return std::shared_ptr<Font::IService>{};
					}
					return FontBackend::CreateFontService();
				},
				Error))
		{
			throw std::runtime_error(Error);
		}
		auto Loaded = Session.LoadSource(R"(
int64 resource_probe()
{
    VS::Image::Service images = VS::Image::Service::Create("stb");
    VS::Image::Bitmap image = images.Load("memory:image");
    VS::Font::Service fonts = VS::Font::Service::Create("font-backend");
    VS::Font::Face face = fonts.LoadDefault();
    VS::Font::ShapedText text = face.Shape("office", 24);
    VS::Font::Bitmap glyph = face.Rasterize(face.GetGlyphIndex(65), 24);
    VS::Font::Metrics metrics = face.GetMetrics(24);
    if (image.GetWidth() != 1 || image.GetHeight() != 1 || image.GetByte(3) != 128) return -1;
    if (glyph.GetByteCount() <= 0 || glyph.IsColor() || metrics.GetAscent() <= 0 || text.GetAdvanceX(0) <= 0) return -2;
    return text.GetGlyphCount();
}
int64 invalid_index()
{
    return VS::Image::Service::Create("stb").Load("memory:image").GetByte(-1);
}
int64 signed_bearing(int64 character, int64 field)
{
    VS::Font::Face face = VS::Font::Service::Create("font-backend").LoadDefault();
    VS::Font::Bitmap glyph = face.Rasterize(face.GetGlyphIndex(character), 24);
    return field == 0 ? glyph.GetBearingX() : glyph.GetBearingY();
}
)",
										 "resource-modules.vs");
		if (!Loaded.bSuccess)
		{
			throw std::runtime_error(Loaded.Error.Message);
		}
		FValue Value;
		auto Result = Session.CallFunction("resource_probe", {}, &Value);
		if (!Result.bSuccess || !std::holds_alternative<std::int64_t>(Value) || std::get<std::int64_t>(Value) <= 0 ||
			std::get<std::int64_t>(Value) >= 6)
		{
			throw std::runtime_error("independent resource script bindings: " + Result.Error.Message);
		}
		Result = Session.CallFunction("invalid_index", {}, &Value);
		if (Result.bSuccess)
		{
			throw std::runtime_error("negative image index accepted");
		}
		auto Face = FontBackend::CreateFontService()->Load(Font::GetDefaultFontData(), 0, Error);
		if (!Face)
		{
			throw std::runtime_error(Error);
		}
		for (int Field = 0; Field < 2; ++Field)
		{
			const char32_t Character = Field == 0 ? U'j' : U'_';
			Font::FBitmap Bitmap;
			if (!Face->Rasterize(Face->GetGlyphIndex(Character), 24, Bitmap, Error))
			{
				throw std::runtime_error(Error);
			}
			const std::int64_t Expected = Field == 0 ? Bitmap.BearingX : Bitmap.BearingY;
			Result = Session.CallFunction(
				"signed_bearing", {static_cast<std::int64_t>(Character), static_cast<std::int64_t>(Field)}, &Value);
			if (Expected >= 0 || !Result.bSuccess || !std::holds_alternative<std::int64_t>(Value) ||
				std::get<std::int64_t>(Value) != Expected)
			{
				throw std::runtime_error("signed font bitmap metrics changed across the script boundary");
			}
		}
		std::cout << "Image/Font script bindings without Gui registration passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
