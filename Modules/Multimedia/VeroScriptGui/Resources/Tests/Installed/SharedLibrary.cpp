// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiMarkup.h"
#if VS_GUI_TEST_RMLUI
#include "VeroScriptGuiTestResources.h"
#include "VeroScriptRmlUi.h"
#endif
#include <utility>

extern "C" bool CheckInstalledSharedGui()
{
	using namespace VeroScript;
	std::string Error;
	Gui::FMarkupDiagnostic Diagnostic;
	auto Program = Gui::FMarkupProgram::Compile(
		{"shared.vswml", "<Widget><style>:root { padding: 2px; }</style></Widget>"}, {}, Diagnostic);
	auto Root = Program ? Program->Create({}, Diagnostic) : nullptr;
	if (!Root)
	{
		return false;
	}
	#if VS_GUI_TEST_RMLUI
	auto Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error);
	if (!Backend)
	{
		return false;
	}
	auto Context = Gui::FContext::Create(std::move(Backend), {64, 48, 1}, Error);
	Gui::FLayoutBox Box;
	return Context && Context->SetRoot(Root, Error) && Context->Update(0, Error) && Root->GetLayout(Box, Error) &&
		   Box.Border.Width == 64 && Box.Border.Height == 48 && Context->Close(Error);
	#else
	return Gui::ValidateViewport({64, 48, 1}, Error);
	#endif
}
