// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "page.vswml";

void main()
{
	VS::Gui::ApplicationOptions options = VS::Gui::ApplicationOptions();
	options.Title = "VeroScript GUI framework";
	VS::Gui::Application app = VS::Gui::Application(options);
	try
	{
		auto factory = VS::Gui::LoadFactory<VS::Gui::Widget>("page.vswml");
		app.SetRoot(factory.Create());
		app.Run();
	}
	finally
	{
		app.Close();
	}
}
