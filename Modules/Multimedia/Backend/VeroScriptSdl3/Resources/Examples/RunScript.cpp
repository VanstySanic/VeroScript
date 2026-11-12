// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptPlatformBindings.h"
#include "VeroScriptGraphicsBindings.h"
#if VS_EXAMPLE_AUDIO
#include "VeroScriptAudioBindings.h"
#endif
#include "VeroScriptCompile.h"
#include "VeroScriptPath.h"
#include "VeroScriptVm.h"
#include "VeroScriptVmBackendLoader.h"
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
	int Run(const std::filesystem::path& ScriptPath, bool bCheck, const std::filesystem::path& VmBackendPath)
	{
		using namespace VeroScript;
		std::string Error;
		if (!bCheck && VmBackendPath.empty())
		{
			std::cerr << "Execution requires --vm-backend <file-or-directory>\n";
			return 2;
		}
		std::shared_ptr<const IVmBackendFactory> VmFactory;
		if (!VmBackendPath.empty())
		{
			VmFactory = LoadVmBackend(PathUtil::ToUtf8(VmBackendPath), Error);
			if (!VmFactory)
			{
				std::cerr << Error << '\n';
				return 1;
			}
		}
		FHostBindingRegistry Bindings;
		if (!Platform::RegisterScriptBindings(
				Bindings,
				[](const std::string& Backend, Platform::EEventPumpMode Mode, std::string& E)
				{
					if (Backend != "sdl3")
					{
						E = "Unknown platform backend: " + Backend;
						return std::shared_ptr<Platform::IService>{};
					}
					Sdl3::FPlatformOptions Options;
					Options.PumpMode = Mode;
					return Sdl3::CreatePlatform(Options, E);
				},
				Error) ||
			!Graphics::RegisterScriptBindings(
				Bindings,
				[](const std::string& Backend, std::string& E)
				{
					if (Backend != "sdl3")
					{
						E = "Unknown graphics backend: " + Backend;
						return std::shared_ptr<Graphics::IService>{};
					}
					return Sdl3::CreateGraphics({}, E);
				},
				Error))
		{
			std::cerr << Error << '\n';
			return 1;
		}
		#if VS_EXAMPLE_AUDIO
		if (!Audio::RegisterScriptBindings(
				Bindings,
				[](const std::string& Backend, std::string& E)
				{
					if (Backend != "sdl3")
					{
						E = "Unknown audio backend: " + Backend;
						return std::shared_ptr<Audio::IService>{};
					}
					return Sdl3::CreateAudio({}, E);
				},
				Error))
		{
			std::cerr << Error;
			return 1;
		}
		#endif
		std::ifstream Input(ScriptPath, std::ios::binary);
		if (!Input)
		{
			std::cerr << "Unable to read script\n";
			return 2;
		}
		const std::string Source((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
		FCompileOptions Options;
		Options.Bindings = &Bindings;
		Options.ScriptCompileExtensions.VmBackendFactory = VmFactory;
		auto Compiled =
			VeroScriptCompile::CompileSource(Source, PathUtil::ToUtf8(std::filesystem::absolute(ScriptPath)), Options);
		if (!Compiled.bSuccess)
		{
			std::cerr << Compiled.Error.Message << '\n';
			return 1;
		}
		if (bCheck)
		{
			std::cout << "Script checked: " << PathUtil::ToUtf8(ScriptPath) << '\n';
			return 0;
		}
		int ExitCode = 1;
		{
			VeroScriptVm Vm(std::cout);
			if (!Vm.SetVmBackendFactory(VmFactory, Error))
			{
				std::cerr << Error << '\n';
				return 1;
			}
			Vm.SetMemoryHostBindings(&Bindings);
			if (!Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Bindings.GetNativeFunctions()))
			{
				std::cerr << Vm.GetError().Message << '\n';
			}
			else
			{
				ExitCode = Vm.GetProcessExitCode();
			}
		}
		if (!Sdl3::CollectRetired(Error))
		{
			std::cerr << Error << '\n';
			return 1;
		}
		return ExitCode;
	}
}

#if defined(_WIN32)
int wmain(int Count, wchar_t** Args)
#else
int main(int Count, char** Args)
#endif
{
	try
	{
		bool bCheck = false;
		bool bOptions = true;
		std::filesystem::path ScriptPath, VmBackendPath;
		const auto Usage = []()
		{
			std::cerr << "Usage: VeroScriptVisualExample [--check] [--vm-backend <file-or-directory>] [--] program.vs\n";
			return 2;
		};
		for (int Index = 1; Index < Count; ++Index)
		{
			const std::filesystem::path Argument(Args[Index]);
			if (bOptions && Argument == "--")
			{
				bOptions = false;
			}
			else if (bOptions && Argument == "--check")
			{
				if (bCheck)
				{
					return Usage();
				}
				bCheck = true;
			}
			else if (bOptions && Argument == "--vm-backend")
			{
				if (!VmBackendPath.empty() || ++Index >= Count)
				{
					return Usage();
				}
				VmBackendPath = std::filesystem::path(Args[Index]);
				if (VmBackendPath.empty())
				{
					return Usage();
				}
			}
			else
			{
				if (!ScriptPath.empty() || Argument.empty() ||
					(bOptions && VeroScript::PathUtil::ToUtf8(Argument).rfind("--", 0) == 0))
				{
					return Usage();
				}
				ScriptPath = Argument;
			}
		}
		if (ScriptPath.empty())
		{
			return Usage();
		}
		return Run(ScriptPath, bCheck, VmBackendPath);
	}
	catch (const std::exception& Error)
	{
		std::cerr << "VeroScriptVisualExample: " << Error.what() << '\n';
		return 1;
	}
}
