// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptGuiServices.h"
#include "VeroScriptGuiTools.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptPath.h"
#include <array>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

int main(int Argc, char **Argv)
{
	using namespace VeroScript;
	try
	{
		if (Argc != 5 && Argc != 6)
		{
			return 2;
		}
		const std::string Selected = Argc == 6 ? Argv[5] : "All";
		if (Selected != "All" && Selected != "Framework" && Selected != "Content" && Selected != "Composite") return 2;
		std::string Error;
		FNativeExtensionRegistry Registry;
		for (int I = 1; I < 5; ++I)
		{
			auto Module = LoadNativeExtension(Argv[I], Error);
			FNativeExtensionRegistrationOptions Options;
			Options.Origin.ProviderId = "gui.preview." + std::to_string(I);
			if (!Module || !Registry.Add(std::move(Module), std::move(Options), Error))
			{
				throw std::runtime_error(Error);
			}
		}
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "preview";
		Description.Scopes.push_back({"preview", {}, false});
		Description.bStrictSources = false;
		Description.bAllowAmbientSearch = true;
		auto Environment = FModuleEnvironment::Create(std::move(Description), Error);
		if (!Environment)
		{
			throw std::runtime_error(Error);
		}
		auto Factories = Gui::MakeRegisteredServiceFactories(Registry.GetProviders(), Environment);
		{
			auto Graphics = Factories.Graphics("sdl3", Error);
			if (!Graphics)
			{
				throw std::runtime_error(Error);
			}
			auto Device = Graphics->CreateDevice({}, Error);
			if (!Device)
			{
				std::cout << "GPU unavailable: " << Error << '\n';
				return 77;
			}
			if (!Device->Close(Error))
			{
				throw std::runtime_error(Error);
			}
		}
		Gui::FToolOptions Options;
		VeroScript::Test::Configure(Options.Engine);
		Options.Engine.StdlibSearchPaths.push_back(VEROSCRIPT_GUI_STDLIB);
		Options.Engine.ModuleEnvironment = std::move(Environment);
		Options.Runtime.Application = std::move(Factories);
		auto Service = Gui::CreateCommandLineService(std::move(Options));
		auto Commands = Service->CreateRegistration();
		FCommandLineContext Context;
		Context.WorkingDirectory = PathUtil::ToUtf8(std::filesystem::current_path());
		Context.Output = &std::cout;
		Context.Errors = &std::cerr;
		if (!Commands.Module.Configure(Context, {}, Commands.Module.UserData, Error))
		{
			throw std::runtime_error(Error);
		}
		const auto &Preview = Commands.Module.Commands.at(2);
		const auto Examples = PathUtil::FromUtf8(VEROSCRIPT_GUI_FRAMEWORK_EXAMPLES).parent_path();
		const std::array<std::pair<std::string, std::filesystem::path>, 3> Pages{{{"Framework", PathUtil::FromUtf8(VEROSCRIPT_GUI_FRAMEWORK_EXAMPLES) / "page.vswml"},
		    {"Content", Examples / "Content/page.vswml"},
		    {"Composite", Examples / "CompositeControls/page.vswml"}}};
		for (const auto &Page : Pages)
		{
			if (Selected != "All" && Selected != Page.first)
			{
				continue;
			}
			std::cout << "GUI preview: compiling and rendering " << Page.first << std::endl;
			const int Result = Preview.Run(Context, {PathUtil::ToUtf8(Page.second), "--frames", "3"}, Preview.UserData);
			if (Result != 0)
			{
				throw std::runtime_error(Page.first + " preview failed: " + std::to_string(Result));
			}
		}
		std::cout << "ML/CSS compile, native provider assembly, three rendered frames and close passed\n";
		return 0;
	}
	catch (const std::exception &Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
