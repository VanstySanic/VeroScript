// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "page.vswml";

using namespace VS::Gui;

void main()
{
	ApplicationOptions options = ApplicationOptions();
	options.Title = "VeroScript GUI Showcase";
	options.Width = 1200;
	options.Height = 840;
	Application app = Application(options);
	try
	{
		auto root = LoadFactory<Stack>("page.vswml").Create();

		// Code-built widgets and ML widgets share the same tree and lifecycle.
		Wrap codeRow = Wrap::Create().Gap(10)[
		{
			Button::Create().Id("builder-button").Text("Built with VS builders").Width(Length(228)).Height(Length(36)).CornerRadius(6).Background(Color(0.025, 0.16, 0.30, 1)).OnClicked([](Button source) -> void
			{
				source.SetText("Builder event received"); source.AnimateBackground(Color(0.025, 0.34, 0.22, 1), 0.25);
			}), VS::Gui::Text::Create().Text("Code-built widgets share the same tree and events.").FontSize(12).TextColor(Color(0.58, 0.68, 0.79, 1))
		}];
		root.Find("code-host").AddChild(codeRow);
		app.SetRoot(root);
		app.Tick();
		print("Showcase ready: drag the splitter, edit values, switch modes, or open Help.");
		app.Run();
	}
	finally
	{
		app.Close();
	}
}
