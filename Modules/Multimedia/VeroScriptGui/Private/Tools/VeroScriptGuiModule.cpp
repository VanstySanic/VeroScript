// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiModule.h"
#include "VeroScriptGuiCommandLine.h"
#include <algorithm>
#include <cstring>
#include <exception>

namespace VeroScript::Gui
{
	namespace
	{
		struct FRegistration
		{
			std::shared_ptr<const ICommandLineService> Service;
			FCommandLineRegistration Commands;
		};
		void Report(char* Output, std::uint32_t Capacity, const std::string& Error)
		{
			if (Output && Capacity)
			{
				const auto Size = std::min<std::size_t>(Error.size(), Capacity - 1);
				std::memcpy(Output, Error.data(), Size);
				Output[Size] = 0;
			}
		}
		void* VEROSCRIPT_EXTENSION_CALL Create(const void* Input, char* Output, std::uint32_t Capacity)
		{
			try
			{
				if (!Input)
				{
					return nullptr;
				}
				const auto& Host = *static_cast<const INativeExtensionHost*>(Input);
				std::string Error;
				auto Cli = std::dynamic_pointer_cast<const ICommandLineExtensionService>(
					Host.GetService(CommandLineServiceId, CommandLineServiceVersion, Error));
				auto Tools = std::dynamic_pointer_cast<const ICommandLineService>(
					Host.GetService(ToolServiceId, ToolServiceVersion, Error));
				if (!Cli || !Tools)
				{
					Report(Output, Capacity, "GUI tools require the host CLI and veroscript.gui.tools services");
					return nullptr;
				}
				auto State = std::make_unique<FRegistration>();
				State->Service = std::move(Tools);
				if (State->Service->HasHostRegistration())
				{
					return State.release();
				}
				State->Commands = State->Service->CreateRegistration();
				if (!State->Commands.Owner || !Cli->Register(State->Commands.Module, Error))
				{
					Report(Output, Capacity, Error);
					return nullptr;
				}
				return State.release();
			}
			catch (const std::exception& Exception)
			{
				Report(Output, Capacity, Exception.what());
			}
			catch (...)
			{
				Report(Output, Capacity, "GUI tool registration failed");
			}
			return nullptr;
		}
		void VEROSCRIPT_EXTENSION_CALL Destroy(void* State)
		{
			delete static_cast<FRegistration*>(State);
		}
	} // namespace
} // namespace VeroScript::Gui

extern "C" VEROSCRIPT_GUI_MODULE_API std::uint32_t VEROSCRIPT_EXTENSION_CALL
veroscript_gui_tools_query(std::uint32_t Version, std::uint32_t Size, void* Output)
{
	using namespace VeroScript;
	if (Version != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Output)
	{
		return 0;
	}
	try
	{
		FNativeExtensionDescriptor Descriptor;
		const auto Abi = BuildNativeExtensionAbi();
		const auto* Platform = GetHostPlatformInfo().Platform;
		if (Abi.size() >= sizeof(Descriptor.Abi) || std::strlen(Platform) >= sizeof(Descriptor.Platform))
		{
			return 0;
		}
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		std::memcpy(Descriptor.Platform, Platform, std::strlen(Platform) + 1);
		std::memcpy(Descriptor.Id, "veroscript-gui-tools", sizeof("veroscript-gui-tools"));
		std::memcpy(Descriptor.Version, Gui::Version, sizeof(Gui::Version));
		Descriptor.Roles = NativeExtensionRole(ENativeExtensionRole::CommandLine);
		Descriptor.Flags = NativeExtensionParallelRegistrations;
		Descriptor.Create = Gui::Create;
		Descriptor.Destroy = Gui::Destroy;
		std::memcpy(Output, &Descriptor, sizeof(Descriptor));
		return Version;
	}
	catch (...)
	{
		return 0;
	}
}

#if !defined(VEROSCRIPT_GUI_STATIC_TOOLS)
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t
	VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(std::uint32_t Version, std::uint32_t Size, void* Output)
{
	return veroscript_gui_tools_query(Version, Size, Output);
}
#endif
