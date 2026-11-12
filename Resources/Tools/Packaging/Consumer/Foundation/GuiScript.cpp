// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiScript.h"
#include "VeroScriptSession.h"
#include "VeroScriptBytecodeVm.h"
#include <iostream>

int main()
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
	std::string Error;
	FEngineConfig Config;
	Config.VmBackendFactory = BytecodeVm::CreateFactory(GetVmHostServices());
	Config.StdlibSearchPaths.push_back(VS_GUI_TEST_STDLIB);
	if (!Gui::RegisterScriptBindings(Session.GetBindingRegistry(), {}, Error) ||
	    !Gui::ConfigureGuiScriptEngine(Config, Error) ||
	    !Session.ApplyConfig(Config, &Error))
	{
		std::cerr << Error;
		return 1;
	}
	const auto Loaded = Session.LoadSource(R"(
using namespace VS::Gui;
void main()
{
    Widget root = Widget::Create().Id("installed").Gap(4);
    assert(root.GetId() == "installed" && root.GetGap() == 4, "installed GUI builder");
    ListView list = ListView::Create().SelectedKey("a")[{ListItem::Create().Key("a").Text("Alpha")}];
    list.Initialize();
    assert(list.GetActualSelectedKey() == "a", "installed collection library");
    ComboBox combo = ComboBox::Create().SelectedKey("a")[{ListItem::Create().Key("a").Text("Alpha")}];
    combo.Initialize();
    assert(combo.GetText() == "Alpha", "installed navigation library");
    Dialog dialog = Dialog::Create();
    dialog.Initialize(); dialog.Show(); dialog.Close("done");
    assert(dialog.GetModal() && dialog.GetResult() == "done", "installed modal library");
    array<string> labels = array<string>();
    for (int64 index = 0; index < 1000; index++) labels.push(to_string(index));
    ListView virtualList = ListView::Create(); virtualList.SetVirtualItems(labels, 28);
    assert(virtualList.GetItemCount() == 1000 && virtualList.GetRealizedItemCount() < 32,
        "installed virtualization library");
    virtualList.Dispose();
    dialog.Dispose(); combo.Dispose(); list.Dispose(); root.Dispose();
}
)",
	    "installed-gui.vs");
	if (!Loaded.bSuccess)
	{
		std::cerr << Loaded.Error.Message;
		return 2;
	}
	const auto Executed = Session.Run();
	if (!Executed.bSuccess)
	{
		std::cerr << Executed.Error.Message;
		return 3;
	}
	return Executed.ExitCode;
}
