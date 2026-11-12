// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#if VS_TEST_PLATFORM
#include "VeroScriptPlatformApi.h"
#endif
#if VS_TEST_GRAPHICS
#include "VeroScriptGraphics2D.h"
#endif
#if VS_TEST_AUDIO
#include "VeroScriptAudio.h"
#endif
#if VS_TEST_SDL3
#include "VeroScriptSdl3.h"
#endif
#if VS_TEST_IMAGE
#include "VeroScriptImage.h"
#endif
#if VS_TEST_FONT
#include "VeroScriptFont.h"
#endif
#if VS_TEST_STB
#include "VeroScriptStb.h"
#endif
#if VS_TEST_FONT_BACKEND
#include "VeroScriptFontBackend.h"
#endif
#if VS_TEST_GUI || VS_TEST_RMLUI
#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiMarkup.h"
#endif
#if VS_TEST_RMLUI
#include "VeroScriptRmlUi.h"
#endif
#include <iostream>
#include <string>

int main()
{
	std::string Error;
	#if VS_TEST_IMAGE
	VeroScript::Image::FDecodedImage Pixel{1, 1, {255, 0, 0, 255}};
	if (!VeroScript::Image::Validate(Pixel, Error))
	{
		return 10;
	}
	#endif
	#if VS_TEST_FONT
	if (VeroScript::Font::GetDefaultFontData().empty())
	{
		return 11;
	}
	#endif
	#if VS_TEST_STB
	auto Images = VeroScript::Stb::CreateImageService();
	if (!Images || std::string(Images->GetBackendId()) != "stb")
	{
		return 12;
	}
	#endif
	#if VS_TEST_FONT_BACKEND
	auto Fonts = VeroScript::FontBackend::CreateFontService();
	auto Layouts = VeroScript::FontBackend::CreateTextLayoutService();
	auto Paragraph = VeroScript::Font::FTextLayout::Create(Layouts, "installed layout", VeroScript::Font::EDirection::Auto, Error);
	auto Face = Fonts ? Fonts->Load(VeroScript::Font::GetDefaultFontData(), 0, Error) : nullptr;
	if (!Face || !Paragraph || std::string(Fonts->GetBackendId()) != "font-backend" ||
		std::string(Layouts->GetBackendId()) != "font-backend")
	{
		std::cerr << Error;
		return 13;
	}
	#endif
	#if VS_TEST_GUI || VS_TEST_RMLUI
	VeroScript::Gui::FMarkupDiagnostic Diagnostic;
	auto Markup = VeroScript::Gui::FMarkupProgram::Compile(
		{"installed.vswml", "<Widget id='root'><Widget id='child'/></Widget>"}, {}, Diagnostic);
	auto Root = Markup ? Markup->Create({}, Diagnostic) : nullptr;
	if (!Root || !VeroScript::Gui::FMarkupProgram::Find(Root, "child"))
	{
		std::cerr << Diagnostic.Format();
		return 14;
	}
	#endif
	#if VS_TEST_RMLUI
	VeroScript::RmlUi::FOptions GuiOptions;
	#if VS_TEST_FONT_BACKEND
	GuiOptions.Fonts = Fonts;
	GuiOptions.TextLayouts = Layouts;
	#endif
	auto Backend = VeroScript::RmlUi::CreateBackend(GuiOptions, Error);
	#if VS_TEST_FONT_BACKEND
	auto Context = Backend ? VeroScript::Gui::FContext::Create(std::move(Backend), {640, 480, 1}, Error) : nullptr;
	if (!Context || !Context->SetRoot(Root, Error) || !Context->Update(0, Error) || !Context->Close(Error))
	{
		std::cerr << Error;
		return 15;
	}
	#else
	if (Backend || Error.empty())
	{
		return 16;
	}
	#endif
	#endif
	#if VS_TEST_PLATFORM
	VeroScript::Platform::FWindowOptions Options;
	if (!VeroScript::Platform::ValidateWindowOptions(Options, Error))
	{
		return 1;
	}
	#endif
	#if VS_TEST_GRAPHICS
	if (!VeroScript::Graphics::ValidateColor({}, Error))
	{
		return 2;
	}
	#endif
	#if VS_TEST_AUDIO
	if (!VeroScript::Audio::ValidateStreamOptions({}, Error))
	{
		std::cerr << Error;
		return 3;
	}
	#endif
	#if VS_TEST_SDL3
	auto Platform = VeroScript::Sdl3::CreatePlatform({}, Error);
	if (!Platform)
	{
		std::cerr << Error;
		return 4;
	}
	VeroScript::Platform::FWindowOptions WindowOptions;
	WindowOptions.bVisible = false;
	auto Window = Platform->CreateWindow(WindowOptions, Error);
	if (!Window || !Window->Close(Error))
	{
		std::cerr << Error;
		return 5;
	}
	Window.reset();
	Platform.reset();
	if (!VeroScript::Sdl3::CollectRetired(Error))
	{
		return 6;
	}
	#endif
	return 0;
}
