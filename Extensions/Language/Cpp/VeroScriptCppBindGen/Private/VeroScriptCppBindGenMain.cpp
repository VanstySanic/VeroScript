// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppBindGen.h"
#include "VeroScriptCppToolOutput.h"
#include "VeroScriptVersion.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace
{
	void Usage()
	{
		std::fprintf(stdout, "VeroScriptCppBindGen %s\n",
			VeroScript::FormatVersion(VeroScript::VEROSCRIPT_VERSION).c_str());
		std::fputs(
			"Usage: VeroScriptCppBindGen <input...> --catalog <output.json> [options]\n"
			"Options:\n"
			"  --api-reference <output.vs.txt>  Write a human-readable API surface\n"
			"  --api-reference-dir <directory> Write one API surface per input\n"
			"  --manifest <output.json>        Write the project artifact manifest\n"
			"  --version                        Show the version\n"
			"  --help                           Show this help\n",
			stdout);
		std::fflush(stdout);
	}

	std::string JoinPath(const std::string& Directory, const std::string& Name)
	{
		if (Directory.empty())
		{
			return Name;
		}
		const char Last = Directory.back();
		return Directory + (Last == '/' || Last == '\\' ? "" : "/") + Name;
	}

	#if defined(_WIN32)
	std::string Utf8(const wchar_t* Value)
	{
		if (!Value)
		{
			return {};
		}
		const int Length = WideCharToMultiByte(
			CP_UTF8, WC_ERR_INVALID_CHARS, Value, -1, nullptr, 0, nullptr, nullptr);
		if (Length <= 1)
		{
			return {};
		}
		std::string Result(static_cast<std::size_t>(Length), '\0');
		if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Value, -1,
			Result.data(), Length, nullptr, nullptr) == 0)
		{
			return {};
		}
		Result.pop_back();
		return Result;
	}
	#endif
}

int Run(const std::vector<std::string>& Arguments)
{
	using namespace VeroScript::CppBindGen;
	using namespace VeroScript::CppTools;
	if (Arguments.size() < 2)
	{
		Usage();
		return 2;
	}
	std::vector<std::string> Inputs;
	std::string CatalogPath;
	std::string ApiPath;
	std::string ApiDirectory;
	std::string ManifestPath;
	FBindGenOptions Options;
	for (std::size_t Index = 1; Index < Arguments.size(); ++Index)
	{
		const std::string& Argument = Arguments[Index];
		if (Argument == "--help" || Argument == "-h")
		{
			Usage();
			return 0;
		}
		if (Argument == "--version")
		{
			std::fprintf(stdout, "VeroScriptCppBindGen %s\n",
				VeroScript::FormatVersion(VeroScript::VEROSCRIPT_VERSION).c_str());
			std::fflush(stdout);
			return 0;
		}
		if (Argument == "--catalog" || Argument == "--api-reference"
			|| Argument == "--api-reference-dir" || Argument == "--manifest")
		{
			if (Index + 1 >= Arguments.size())
			{
				std::cerr << "missing value for option: " << Argument << '\n';
				return 2;
			}
			if (Argument == "--catalog")
			{
				CatalogPath = Arguments[++Index];
			}
			else if (Argument == "--api-reference")
			{
				ApiPath = Arguments[++Index];
			}
			else if (Argument == "--api-reference-dir")
			{
				ApiDirectory = Arguments[++Index];
			}
			else
			{
				ManifestPath = Arguments[++Index];
			}
		}
		else if (!Argument.empty() && Argument.front() == '-')
		{
			std::cerr << "unknown option: " << Argument << '\n';
			return 2;
		}
		else
		{
			Inputs.push_back(Argument);
		}
	}
	if (Inputs.empty() || CatalogPath.empty())
	{
		Usage();
		return 2;
	}
	if (!ApiPath.empty() && PathsReferToSameFile(CatalogPath, ApiPath))
	{
		std::cerr << "catalog and API-reference paths must be distinct\n";
		return 2;
	}
	if (!ApiPath.empty() && !ApiDirectory.empty())
	{
		std::cerr << "--api-reference and --api-reference-dir are mutually exclusive\n";
		return 2;
	}
	if (!ApiPath.empty() && !ManifestPath.empty())
	{
		std::cerr << "--api-reference with --manifest is ambiguous; use --api-reference-dir\n";
		return 2;
	}
	if (!ApiDirectory.empty() || !ManifestPath.empty())
	{
		Options.bEmitApiReference = !ApiDirectory.empty();
		FBindGenProjectLayout Layout;
		Layout.CatalogPath = CatalogPath;
		Layout.ApiReferenceDirectory = ApiDirectory;
		FBindGenProjectResult Result = GenerateBindingProject(Inputs, Options, Layout);
		for (const auto& Diagnostic : Result.Diagnostics)
		{
			std::cerr << VeroScript::CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
		}
		if (!Result.Succeeded())
		{
			return 1;
		}
		std::vector<FOutputFile> Outputs;
		Outputs.push_back({CatalogPath, std::move(Result.CatalogJson)});
		for (FBindGenApiReference& Reference : Result.ApiReferences)
		{
			Outputs.push_back({JoinPath(ApiDirectory, Reference.OutputName),
				std::move(Reference.Contents)});
		}
		if (!ManifestPath.empty())
		{
			Outputs.push_back({ManifestPath, std::move(Result.ManifestJson)});
		}
		FFilePathSet InputPaths;
		for (const auto& Input : Inputs)
		{
			InputPaths.Insert(Input);
		}

		for (const FOutputFile& Generated : Outputs)
		{
			if (InputPaths.Contains(Generated.Path))
			{
				std::cerr << "input and generated output paths must be distinct\n";
				return 2;
			}
		}
		std::string Error;
		if (!WriteOutputFilesAtomically(Outputs, Error))
		{
			std::cerr << Error << '\n';
			return 1;
		}
		return 0;
	}
	for (const std::string& Input : Inputs)
	{
		if (PathsReferToSameFile(Input, CatalogPath)
			|| (!ApiPath.empty() && PathsReferToSameFile(Input, ApiPath)))
		{
			std::cerr << "input and generated output paths must be distinct\n";
			return 2;
		}
	}
	Options.bEmitApiReference = !ApiPath.empty();
	FBindGenResult Result = GenerateBindingsFromFiles(Inputs, Options);
	for (const auto& Diagnostic : Result.Diagnostics)
	{
		std::cerr << VeroScript::CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
	}
	if (!Result.Succeeded())
	{
		return 1;
	}
	std::string Error;
	std::vector<FOutputFile> Outputs;
	Outputs.push_back({CatalogPath, std::move(Result.CatalogJson)});
	if (!ApiPath.empty())
	{
		Outputs.push_back({ApiPath, std::move(Result.ApiReference)});
	}
	if (!WriteOutputFilesAtomically(Outputs, Error))
	{
		std::cerr << Error << '\n';
		return 1;
	}
	return 0;
}

#if defined(_WIN32)
int wmain(const int Argc, wchar_t** Argv)
{
	std::vector<std::string> Arguments;
	Arguments.reserve(static_cast<std::size_t>(Argc));
	for (int Index = 0; Index < Argc; ++Index)
	{
		Arguments.push_back(Utf8(Argv[Index]));
	}
	return Run(Arguments);
}
#else
int main(const int Argc, char** Argv)
{
	return Run(std::vector<std::string>(Argv, Argv + Argc));
}
#endif
