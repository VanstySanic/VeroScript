// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "page.vswml";

void main()
{
	VS::Gui::ApplicationOptions options = VS::Gui::ApplicationOptions();
	options.Title = "VeroScript layout, input and floating controls";
	VS::Gui::Application app = VS::Gui::Application(options);
	try
	{
		app.SetRoot(VS::Gui::LoadFactory<VS::Gui::Stack>("page.vswml").Create());
		app.Run();
	}
	finally
	{
		app.Close();
	}
}
