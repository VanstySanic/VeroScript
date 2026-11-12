// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppCoreAot.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

// Build and run on the host; compile generated.cpp with the target SDK.
#if defined(_WIN32)
int wmain(int Argc, wchar_t** Argv)
#else
int main(int Argc, char** Argv)
#endif
{
	if (Argc != 4 && Argc != 5)
	{
		std::cerr << "usage: VeroScriptCoreAotGenerate <source.vs> <target> <output-directory> [--require-native]\n";
		return 2;
	}
	try
	{
		VeroScript::FCoreAotOptions Options;
		#if defined(_WIN32)
		Options.Target = VeroScript::PathUtil::ToUtf8(std::filesystem::path(Argv[2]));
		constexpr std::wstring_view RequireNative = L"--require-native";
		#else
		Options.Target = Argv[2];
		constexpr std::string_view RequireNative = "--require-native";
		#endif
		if (Argc == 5)
		{
			if (std::basic_string_view(Argv[4]) != RequireNative) return 2;
			Options.bRequireAllNative = true;
		}
		const auto InputPath = std::filesystem::absolute(std::filesystem::path(Argv[1]));
		std::ifstream Input(InputPath, std::ios::binary);
		if (!Input || std::filesystem::file_size(InputPath) > 16 * 1024 * 1024)
		{
			std::cerr << "source is unreadable or exceeds the example's 16 MiB input budget\n";
			return 1;
		}
		const std::string Source{std::istreambuf_iterator<char>(Input), {}};
		VeroScript::FCoreAotSourceBundle Bundle;
		std::string Error;
		if (!VeroScript::CompileSourceToCoreAot(Source, VeroScript::PathUtil::ToUtf8(InputPath), {}, Options, Bundle, Error))
		{
			std::cerr << Error << '\n';
			return 1;
		}
		const auto Output = std::filesystem::path(Argv[3]);
		std::filesystem::create_directories(Output);
		const auto Write = [&](const char* Name, const void* Data, std::size_t Size)
		{
			std::ofstream File(Output / Name, std::ios::binary | std::ios::trunc);
			File.write(static_cast<const char*>(Data), static_cast<std::streamsize>(Size));
			if (!File)
			{
				throw std::runtime_error(std::string("cannot write ") + Name);
			}
		};
		Write("generated.cpp", Bundle.Source.data(), Bundle.Source.size());
		Write("program.vsbc", Bundle.Bytecode.data(), Bundle.Bytecode.size());
		Write("program.vsapp", Bundle.Application.data(), Bundle.Application.size());
		Write("aot.json", Bundle.ManifestJson.data(), Bundle.ManifestJson.size());
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
