// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppTranspiler.h"
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
		std::fprintf(stdout, "VeroScriptCppTranspiler %s\n",
			VeroScript::FormatVersion(VeroScript::VEROSCRIPT_VERSION).c_str());
		std::fputs(
			"Usage: VeroScriptCppTranspiler <input.cpp> -o <output.vs> [options]\n"
			"       VeroScriptCppTranspiler <input...> --output-dir <dir> [options]\n"
			"Options:\n"
			"  --source-map <output.map.json>  Write declaration-level source mappings\n"
			"  --output-dir <directory>        Emit one source and map per input\n"
			"  --manifest <output.json>        Project manifest (defaults under output-dir)\n"
			"  --attribute-map <cpp>=<vero>    Map a C++ attribute to a Vero annotation (repeatable)\n"
			"  --no-validate                    Do not compile-check generated VeroScript\n"
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

	bool ParseAttributeMapping(
		const std::string& Value,
		VeroScript::CppTranspiler::FAttributeMapping& Out)
	{
		const std::size_t Equals = Value.find('=');
		if (Equals == std::string::npos || Equals == 0 || Equals + 1 >= Value.size()
			|| Value.find('=', Equals + 1) != std::string::npos)
		{
			return false;
		}
		Out.CppName = Value.substr(0, Equals);
		Out.AnnotationName = Value.substr(Equals + 1);
		return true;
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
	using namespace VeroScript::CppTranspiler;
	using namespace VeroScript::CppTools;
	if (Arguments.size() < 2)
	{
		Usage();
		return 2;
	}
	std::vector<std::string> Inputs;
	std::string Output;
	std::string SourceMap;
	std::string OutputDirectory;
	std::string ManifestPath;
	FTranspileOptions Options;
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
			std::fprintf(stdout, "VeroScriptCppTranspiler %s\n",
				VeroScript::FormatVersion(VeroScript::VEROSCRIPT_VERSION).c_str());
			std::fflush(stdout);
			return 0;
		}
		if (Argument == "-o" || Argument == "--output" || Argument == "--source-map"
			|| Argument == "--output-dir" || Argument == "--manifest"
			|| Argument == "--attribute-map")
		{
			if (Index + 1 >= Arguments.size())
			{
				std::cerr << "missing value for option: " << Argument << '\n';
				return 2;
			}
			if (Argument == "--attribute-map")
			{
				FAttributeMapping Mapping;
				if (!ParseAttributeMapping(Arguments[++Index], Mapping))
				{
					std::cerr << "invalid --attribute-map; expected <cpp-name>=<annotation-name>\n";
					return 2;
				}
				Options.AttributeMappings.push_back(std::move(Mapping));
			}
			else if (Argument == "--source-map")
			{
				SourceMap = Arguments[++Index];
			}
			else if (Argument == "--output-dir")
			{
				OutputDirectory = Arguments[++Index];
			}
			else if (Argument == "--manifest")
			{
				ManifestPath = Arguments[++Index];
			}
			else
			{
				Output = Arguments[++Index];
			}
		}
		else if (Argument == "--no-validate")
		{
			Options.bValidateGeneratedSource = false;
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
	if (Inputs.empty() || (Output.empty() == OutputDirectory.empty()))
	{
		Usage();
		return 2;
	}
	if (!OutputDirectory.empty() && (!SourceMap.empty() || !Output.empty()))
	{
		std::cerr << "--output-dir cannot be combined with -o or --source-map\n";
		return 2;
	}
	if (!Output.empty() && (Inputs.size() != 1 || !ManifestPath.empty()))
	{
		std::cerr << "single-file -o mode requires exactly one input and no --manifest\n";
		return 2;
	}
	if (!OutputDirectory.empty())
	{
		if (ManifestPath.empty())
		{
			ManifestPath = JoinPath(OutputDirectory, "transpile.manifest.json");
		}
		FTranspileProjectOptions ProjectOptions;
		ProjectOptions.bValidateGeneratedSources = Options.bValidateGeneratedSource;
		ProjectOptions.AttributeMappings = Options.AttributeMappings;
		FTranspileProjectResult Result = TranspileFiles(Inputs, ProjectOptions);
		for (const auto& Diagnostic : Result.Diagnostics)
		{
			std::cerr << VeroScript::CppFrontend::FormatDiagnostic(Diagnostic) << '\n';
		}
		if (!Result.Succeeded())
		{
			return 1;
		}
		std::vector<FOutputFile> Outputs;
		FFilePathSet InputPaths;
		for (const auto& Input : Inputs)
		{
			InputPaths.Insert(Input);
		}

		for (FTranspiledFile& File : Result.Files)
		{
			const std::string SourcePath = JoinPath(OutputDirectory, File.OutputName);
			const std::string MapPath = SourcePath + ".map.json";
			if (InputPaths.Contains(SourcePath) || InputPaths.Contains(MapPath) || InputPaths.Contains(ManifestPath))
			{
				std::cerr << "input and generated output paths must be distinct\n";
				return 2;
			}
			Outputs.push_back({SourcePath, std::move(File.Source)});
			Outputs.push_back({MapPath, std::move(File.SourceMapJson)});
		}
		Outputs.push_back({ManifestPath, std::move(Result.ManifestJson)});
		std::string Error;
		if (!WriteOutputFilesAtomically(Outputs, Error))
		{
			std::cerr << Error << '\n';
			return 1;
		}
		return 0;
	}
	const std::string& Input = Inputs.front();
	if (PathsReferToSameFile(Input, Output)
		|| (!SourceMap.empty() && (PathsReferToSameFile(Input, SourceMap)
			|| PathsReferToSameFile(Output, SourceMap))))
	{
		std::cerr << "input, output, and source-map paths must be distinct\n";
		return 2;
	}
	Options.GeneratedSourcePath = Output;
	FTranspileResult Result = TranspileFile(Input, Options);
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
	Outputs.push_back({Output, std::move(Result.Source)});
	if (!SourceMap.empty())
	{
		Outputs.push_back({SourceMap, std::move(Result.SourceMapJson)});
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
