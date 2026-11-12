// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "page.vswml";

void main()
{
	VS::Gui::ApplicationOptions options = VS::Gui::ApplicationOptions();
	options.Title = "VeroScript text and drawing";
	VS::Gui::Application app = VS::Gui::Application(options);
	try
	{
		// Load application fonts with app.GetContext().LoadFont(...) before setting the root.
		app.SetRoot(VS::Gui::LoadFactory<VS::Gui::Widget>("page.vswml").Create());
		app.Run();
	}
	finally
	{
		app.Close();
	}
}
