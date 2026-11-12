// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCSharpAot.h"

#include "VeroScriptAst.h"
#include "VeroScriptModuleLoader.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>

int main()
{
	const std::string Source =
		"int square(int value) { return value * value; }\n"
		"void main() { print(square(6)); }\n";
	VeroScript::VeroScriptModuleLoader Loader("bundle.vs");
	Loader.SetSourceOverlay("bundle.vs", Source);
	const std::unique_ptr<VeroScript::FProgram> Program =
		Loader.LoadSourceModule(Source, "bundle.vs");
	VeroScript::FCSharpAotSourceBundle Bundle;
	std::string Error;
	std::string SingleCompilationUnit;
	if (!Program || Loader.HasError()
		|| !VeroScript::EmitCSharpFromProgram(*Program, SingleCompilationUnit, Error)
		|| !VeroScript::EmitCSharpSourceBundleFromProgram(
			*Program, "sample", Bundle, Error)
		|| Bundle.Files.size() != 4
		|| Bundle.Files[0].RelativePath != "sample.csproj"
		|| Bundle.Files[1].RelativePath != "sample.runtime.g.cs"
		|| Bundle.Files[2].RelativePath != "sample.program.g.cs"
		|| Bundle.Files[3].RelativePath != "sample.entry.g.cs"
		|| Bundle.Files[0].Contents.find("<PublishAot>true</PublishAot>") == std::string::npos
		|| Bundle.Files[1].Contents.find("private readonly struct VsString") == std::string::npos
		|| Bundle.Files[2].Contents.find("private static int Vs_S6_square_M_") == std::string::npos
		|| Bundle.Files[3].Contents.find("public static int Main(string[] args)") == std::string::npos
		|| Bundle.Files[1].Contents.find("internal static partial class") == std::string::npos
		|| Bundle.ManifestJson.find("\"entry\":\"sample.csproj\"") == std::string::npos
		|| Bundle.ManifestJson.find(
			"\"compilationUnits\":[\"sample.runtime.g.cs\",\"sample.program.g.cs\",\"sample.entry.g.cs\"]")
			== std::string::npos)
	{
		std::cerr << "C# AOT source-bundle regression: " << Error << '\n';
		return EXIT_FAILURE;
	}

	VeroScript::FCSharpAotSourceBundle Repeated;
	if (!VeroScript::EmitCSharpSourceBundleFromProgram(
			*Program, "sample", Repeated, Error)
		|| Repeated.ManifestJson != Bundle.ManifestJson
		|| Repeated.Files.size() != Bundle.Files.size())
	{
		std::cerr << "C# AOT source bundle is not deterministic\n";
		return EXIT_FAILURE;
	}
	for (std::size_t Index = 0; Index < Bundle.Files.size(); ++Index)
	{
		if (Repeated.Files[Index].RelativePath != Bundle.Files[Index].RelativePath
			|| Repeated.Files[Index].Contents != Bundle.Files[Index].Contents)
		{
			std::cerr << "C# AOT source bundle changed between identical emissions\n";
			return EXIT_FAILURE;
		}
	}

	VeroScript::FCSharpAotSourceBundle DirectBundle;
	if (!VeroScript::BuildCSharpSourceBundle(
			"internal static class Program { public static void Main() {} }\n",
			"direct", DirectBundle, Error)
		|| DirectBundle.Files.size() != 2
		|| DirectBundle.Files[1].RelativePath != "direct.generated.cs"
		|| VeroScript::BuildCSharpSourceBundle(
			"internal static class Program {}\n", "../unsafe", DirectBundle, Error))
	{
		std::cerr << "direct C# source-bundle validation regression\n";
		return EXIT_FAILURE;
	}
	for (const char* Name : {"CON", "nul", "PrN", "AUX", "COM1", "com9", "LPT1", "lpt9", "\xE9"})
	{
		if (VeroScript::BuildCSharpSourceBundle("internal static class Program {}\n", Name, DirectBundle, Error)
			|| !DirectBundle.Files.empty() || !DirectBundle.ManifestJson.empty() || Error.empty())
		{
			std::cerr << "non-portable C# source-bundle name was accepted: " << Name << '\n';
			return EXIT_FAILURE;
		}
	}
	for (const char* Name : {"console", "COM0", "COM10", "nul_suffix", "LPT10"})
	{
		if (!VeroScript::BuildCSharpSourceBundle("internal static class Program {}\n", Name, DirectBundle, Error))
		{
			std::cerr << "portable C# source-bundle name was rejected: " << Name << '\n';
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
