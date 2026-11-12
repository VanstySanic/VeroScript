// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiMarkup.h"
#if VS_GUI_TEST_SCRIPT
#include "VeroScriptGuiScript.h"
#include "VeroScriptBytecodeVm.h"
#endif
#if VS_GUI_TEST_RMLUI
#include "VeroScriptGuiTestResources.h"
#include "VeroScriptRmlUi.h"
#endif
#if VS_GUI_TEST_SDL3
#include "VeroScriptSdl3.h"
#endif
#include <iostream>

int main()
{
	std::string Error;
	if (!VeroScript::Gui::ValidateViewport({640, 480, 1}, Error))
	{
		return 1;
	}
	VeroScript::Gui::FMarkupSource Source;
	VeroScript::Gui::FMarkupDiagnostic Diagnostic;
	VeroScript::Gui::FMarkupCompileOptions CompileOptions;
	CompileOptions.Read = VeroScript::Gui::ReadMarkupFile;
	if (!CompileOptions.Read({}, std::string(VS_GUI_TEST_EXAMPLES) + "/layout.vswml", Source, Error))
	{
		std::cerr << Error;
		return 9;
	}
	auto Program = VeroScript::Gui::FMarkupProgram::Compile(Source, CompileOptions, Diagnostic);
	auto Root = Program ? Program->Create({}, Diagnostic) : nullptr;
	if (!Root || !VeroScript::Gui::FMarkupProgram::Find(Root, "first"))
	{
		std::cerr << Diagnostic.Format();
		return 10;
	}
	#if VS_GUI_TEST_RMLUI
	auto Backend = VeroScript::RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error);
	if (!Backend)
	{
		std::cerr << Error;
		return 2;
	}
	auto Context = VeroScript::Gui::FContext::Create(std::move(Backend), {640, 480, 1}, Error);
	if (!Context || !Context->SetRoot(Root, Error) || !Context->Update(0, Error))
	{
		std::cerr << Error;
		return 3;
	}
	VeroScript::Gui::FLayoutBox Box;
	if (!Root->GetLayout(Box, Error) || Box.Border.Width != 640)
	{
		return 4;
	}
	VeroScript::Platform::ECursor Cursor = VeroScript::Platform::ECursor::Default;
	VeroScript::Gui::FPointerServices Pointers;
	Pointers.SetCursor = [&](VeroScript::Platform::ECursor Shape, bool bVisible, std::string &)
	{
		Cursor = Shape;
		return bVisible;
	};
	VeroScript::Platform::FEvent Motion;
	Motion.Type = VeroScript::Platform::EEventType::PointerMove;
	Motion.WindowId = 1;
	Motion.X = Motion.Y = 1;
	VeroScript::Gui::FEventResult Delivered;
	if (!Root->Set(VeroScript::Gui::EProperty::Cursor, VeroScript::Gui::ECursor::Crosshair, Error) ||
		!Context->SetPointerServices(std::move(Pointers), Error) || !Context->ProcessEvent(Motion, Delivered, Error) ||
		Cursor != VeroScript::Platform::ECursor::Crosshair || !Context->SetPointerServices({}, Error) ||
		Cursor != VeroScript::Platform::ECursor::Default)
	{
		std::cerr << "Installed GUI pointer bridge failed: " << Error;
		return 13;
	}
	auto Grid = VeroScript::Gui::FWidget::Create();
	auto First = VeroScript::Gui::FWidget::Create(), Second = VeroScript::Gui::FWidget::Create();
	VeroScript::Gui::FControlOptions GridOptions;
	GridOptions.Kind = VeroScript::Gui::EControlKind::Grid;
	if (!Context->GetCapabilities().bExtendedControls || !Grid->SetControlOptions(GridOptions, Error) ||
		!Grid->SetChildren({First, Second}, Error) || !Context->SetRoot(Grid, Error) || !Context->Update(1, Error) ||
		!First->GetLayout(Box, Error) || Box.Border.Width != 320 || !Context->Close(Error))
	{
		std::cerr << "Installed extended GUI layout failed: " << Error;
		return 12;
	}
	#if VS_GUI_TEST_SCRIPT
	Program = VeroScript::Gui::FMarkupProgram::Compile({"installed-script.vswml", "<Widget bind:gap='{ 2 + 3 }'/>"}, {},
													   Diagnostic);
	VeroScript::Gui::FMarkupScriptOptions ScriptOptions;
	ScriptOptions.Engine.VmBackendFactory =
		VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	ScriptOptions.Engine.StdlibSearchPaths.push_back(VS_GUI_TEST_STDLIB);
	auto ScriptRoot =
		Program ? Program->Create(VeroScript::Gui::MakeMarkupScriptFactory(std::move(ScriptOptions)), Diagnostic)
				: nullptr;
	VeroScript::Gui::FPropertyValue Value;
	Backend = VeroScript::RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error);
	Context = Backend ? VeroScript::Gui::FContext::Create(std::move(Backend), {640, 480, 1}, Error) : nullptr;
	if (!ScriptRoot || !Context || !Context->SetRoot(ScriptRoot, Error) || !Context->Update(0, Error) ||
		!ScriptRoot->Get(VeroScript::Gui::EProperty::Gap, Value, Error) || std::get<double>(Value) != 5 ||
		!Context->Close(Error))
	{
		std::cerr << Diagnostic.Format() << Error;
		return 11;
	}
	#endif
	#if VS_GUI_TEST_SDL3
	auto Service = VeroScript::Sdl3::CreateGraphics({}, Error);
	if (!Service)
	{
		std::cerr << Error;
		return 5;
	}
	auto Device = Service->CreateDevice({}, Error);
	if (Device)
	{
		auto Target = Device->CreateTexture({8, 8}, Error);
		VeroScript::RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Target;
		Backend = VeroScript::RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
		if (!Target || !Backend)
		{
			std::cerr << Error;
			return 6;
		}
		Context = VeroScript::Gui::FContext::Create(std::move(Backend), {8, 8, 1}, Error);
		bool bPresented = false;
		std::vector<std::uint8_t> Pixels;
		if (!Context ||
			!Root->Set(VeroScript::Gui::EProperty::Background, VeroScript::Graphics::FColor{1, 0, 0, 1}, Error) ||
			!Context->SetRoot(Root, Error) || !Context->Update(0, Error) || !Context->Render(bPresented, Error) ||
			bPresented || !Device->ReadTexture(Target, Pixels, Error) || Pixels.size() != 8 * 8 * 4 ||
			Pixels[0] != 255 || Pixels[1] != 0 || Pixels[2] != 0 || Pixels[3] != 255)
		{
			std::cerr << "Installed SDL/Gui rendering failed: " << Error;
			return 7;
		}
		if (!Context->Close(Error) || !Target->Close(Error) || !Device->Close(Error))
		{
			std::cerr << Error;
			return 8;
		}
		std::cout << "Installed SDL/Gui rendering passed\n";
	}
	else
	{
		std::cout << "Installed GPU test skipped: " << Error << '\n';
	}
	#endif
	#endif
	std::cout << "Installed GUI framework consumer passed\n";
	return 0;
}
