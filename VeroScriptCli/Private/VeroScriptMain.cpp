// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCli.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

int wmain(const int argc, wchar_t** argv)
{
	try
	{
		// The runtime and all file APIs consume UTF-8. CRT narrow argv uses the
		// active Windows code page and loses paths, source, and script arguments.
		std::vector<std::string> Arguments;
		Arguments.reserve(static_cast<std::size_t>(argc));
		for (int Index = 0; Index < argc; ++Index)
		{
			const int Size = WideCharToMultiByte(
				CP_UTF8, WC_ERR_INVALID_CHARS, argv[Index], -1, nullptr, 0, nullptr, nullptr);
			if (Size == 0)
			{
				throw std::runtime_error("invalid Unicode command-line argument");
			}
			std::string Argument(static_cast<std::size_t>(Size), '\0');
			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[Index], -1,
					Argument.data(), Size, nullptr, nullptr) != Size)
			{
				throw std::runtime_error("unable to encode command-line argument as UTF-8");
			}
			Argument.pop_back();
			Arguments.push_back(std::move(Argument));
		}
		std::vector<char*> Pointers;
		Pointers.reserve(Arguments.size() + 1);
		for (std::string& Argument : Arguments)
		{
			Pointers.push_back(Argument.data());
		}
		Pointers.push_back(nullptr);
		return VeroScript::Cli::Run(argc, Pointers.data());
	}
	catch (const std::exception& Error)
	{
		std::cerr << "VeroScript: " << Error.what() << '\n';
		return 1;
	}
}
#else
int main(int argc, char** argv)
{
	try
	{
		return VeroScript::Cli::Run(argc, argv);
	}
	catch (const std::exception& Error)
	{
		std::cerr << "VeroScript: " << Error.what() << '\n';
		return 1;
	}
}
#endif
