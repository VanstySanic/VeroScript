// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "page.vswml";

void main()
{
	VS::Gui::ApplicationOptions options = VS::Gui::ApplicationOptions();
	options.Title = "VeroScript collections, navigation and dialogs";
	options.Width = 1100;
	options.Height = 800;
	VS::Gui::Application app = VS::Gui::Application(options);
	try
	{
		VS::Gui::Widget root = VS::Gui::LoadFactory<VS::Gui::Widget>("page.vswml").Create();
		array<string> labels = array<string>();
		array<array<string>> rows = array<array<string>>();
		for (int64 index = 0; index < 10000; index++)
		{
			labels.push("Item " + to_string(index));
			array<string> cells = {"Row " + to_string(index), index % 2 == 0 ? "Ready": "Waiting"};
			rows.push(cells);
		}
		cast<VS::Gui::ListView>(root.Find("virtual-list")).SetVirtualItems(labels, 28);
		cast<VS::Gui::TableView>(root.Find("virtual-table")).SetVirtualRows(rows, 28);
		app.SetRoot(root);
		app.Run();
	}
	finally
	{
		app.Close();
	}
}
