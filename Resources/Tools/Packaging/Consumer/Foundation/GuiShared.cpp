// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiWidget.h"

extern "C" int GuiSharedCheck()
{
	std::string Error;
	auto Widget = VeroScript::Gui::FWidget::Create();
	return Widget && Widget->SetId("shared-widget", Error) ? 0 : 1;
}
