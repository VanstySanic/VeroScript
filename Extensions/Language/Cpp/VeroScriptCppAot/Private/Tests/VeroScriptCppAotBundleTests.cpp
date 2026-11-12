// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppAot.h"

#include "VeroScriptAst.h"
#include "VeroScriptModuleLoader.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

int main()
{
	const std::string Source = "void main() { print(42); }\n";
	VeroScript::VeroScriptModuleLoader Loader("bundle.vs");
	Loader.SetSourceOverlay("bundle.vs", Source);
	const std::unique_ptr<VeroScript::FProgram> Program =
		Loader.LoadSourceModule(Source, "bundle.vs");
	VeroScript::FCppAotSourceBundle Bundle;
	std::string Error;
	std::string SingleTranslationUnit;
	if (!Program || Loader.HasError()
		|| !VeroScript::EmitCppFromProgram(*Program, SingleTranslationUnit, Error)
		|| !VeroScript::EmitCppSourceBundleFromProgram(
			*Program, "sample", Bundle, Error)
		|| Bundle.Files.size() != 3
		|| Bundle.Files[0].RelativePath != "sample.cpp"
		|| Bundle.Files[0].Contents.find("sample.runtime.generated.inl") == std::string::npos
		|| Bundle.Files[0].Contents.find("sample.program.generated.inl") == std::string::npos
		|| Bundle.Files[1].Contents.find("#include <") == std::string::npos
		|| Bundle.Files[2].Contents.find("int main(") == std::string::npos
		|| Bundle.Files[1].Contents + Bundle.Files[2].Contents != SingleTranslationUnit
		|| Bundle.ManifestJson.find(
			"\"requirements\":{\"cxxStandard\":17,\"threads\":false}")
			== std::string::npos
		|| Bundle.ManifestJson.find("\"translationUnits\":[\"sample.cpp\"]")
			== std::string::npos)
	{
		std::cerr << "AOT source bundle regression: " << Error << '\n';
		return EXIT_FAILURE;
	}

	VeroScript::FCppAotSourceBundle DirectBundle;
	if (!VeroScript::BuildCppSourceBundle(
		"int generated = 1;\n", "direct", DirectBundle, Error)
		|| DirectBundle.Files.size() != 2
		|| DirectBundle.Files[1].Contents != "int generated = 1;\n"
		|| DirectBundle.ManifestJson.find("\"threads\":false") == std::string::npos)
	{
		std::cerr << "direct source-bundle regression: " << Error << '\n';
		return EXIT_FAILURE;
	}
	VeroScript::FCppAotSourceBundle LiteralBundle;
	if (!VeroScript::BuildCppSourceBundle(
		"/*\n#include <thread>\n*/\n"
		"void generated() { const char* text = \"#include <thread> std::thread\"; }\n",
		"literal",
		LiteralBundle,
		Error)
		|| LiteralBundle.ManifestJson.find("\"threads\":false") == std::string::npos)
	{
		std::cerr << "source-bundle literal was mistaken for a thread dependency: "
			<< Error << '\n';
		return EXIT_FAILURE;
	}
	VeroScript::FCppAotSourceBundle ThreadedBundle;
	if (!VeroScript::BuildCppSourceBundle(
			" # include <thread> // required by generated worker code\n"
			"void generated() { std::thread worker; }\n",
		"threaded",
		ThreadedBundle,
		Error)
		|| ThreadedBundle.ManifestJson.find("\"threads\":true") == std::string::npos)
	{
		std::cerr << "threaded source-bundle requirements regression: " << Error << '\n';
		return EXIT_FAILURE;
	}

	const std::string ChronoSource =
		"void main() { print(\"#include <thread>\"); print(std::chrono::now()); }\n";
	VeroScript::VeroScriptModuleLoader ChronoLoader("chrono.vs");
	ChronoLoader.SetSourceOverlay("chrono.vs", ChronoSource);
	const std::unique_ptr<VeroScript::FProgram> ChronoProgram =
		ChronoLoader.LoadSourceModule(ChronoSource, "chrono.vs");
	VeroScript::FCppAotSourceBundle ChronoBundle;
	if (!ChronoProgram || ChronoLoader.HasError()
		|| !VeroScript::EmitCppSourceBundleFromProgram(
			*ChronoProgram, "chrono", ChronoBundle, Error)
		|| ChronoBundle.Files.size() != 3
		|| ChronoBundle.Files[1].Contents.find("#include <chrono>") == std::string::npos
		|| ChronoBundle.Files[1].Contents.find("#include <thread>") != std::string::npos
		|| ChronoBundle.Files[1].Contents.find("VsSleepMs") != std::string::npos
		|| ChronoBundle.ManifestJson.find("\"threads\":false") == std::string::npos)
	{
		std::cerr << "chrono-only source-bundle requirements regression: "
			<< Error << '\n';
		return EXIT_FAILURE;
	}
	if (VeroScript::BuildCppSourceBundle(
		"int generated = 1;\n", "../unsafe", DirectBundle, Error))
	{
		std::cerr << "unsafe source-bundle name was accepted\n";
		return EXIT_FAILURE;
	}
	// Extensions do not make DOS device names usable as ordinary files on Windows.
	// Reject these on every platform so a generated bundle stays portable.
	for (const char* Name : {"CON", "con", "PrN", "AuX", "NUL", "COM1", "com9", "Lpt1", "LPT9"})
	{
		if (VeroScript::BuildCppSourceBundle(SingleTranslationUnit, Name, DirectBundle, Error)
			|| Error.empty() || !DirectBundle.Files.empty() || !DirectBundle.ManifestJson.empty())
		{
			std::cerr << "reserved source-bundle name was accepted: " << Name << '\n';
			return EXIT_FAILURE;
		}
		if (VeroScript::EmitCppSourceBundleFromProgram(*Program, Name, DirectBundle, Error)
			|| Error.empty() || !DirectBundle.Files.empty() || !DirectBundle.ManifestJson.empty())
		{
			std::cerr << "reserved emitted source-bundle name was accepted: " << Name << '\n';
			return EXIT_FAILURE;
		}
	}
	for (const char* Name : {"CON_model", "COM0", "COM10", "LPT0", "LPT10"})
	{
		if (!VeroScript::BuildCppSourceBundle(SingleTranslationUnit, Name, DirectBundle, Error)
			|| DirectBundle.Files.size() != 2 || DirectBundle.Files.front().RelativePath != std::string(Name) + ".cpp")
		{
			std::cerr << "valid source-bundle name was rejected: " << Name << ' ' << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	const std::string ReusedSource = "// " + std::string(4096, 'x') + "\nint generated=1;\n";
	DirectBundle.Files = {{"source", ReusedSource}, {"stem", "reused"}};
	if (!VeroScript::BuildCppSourceBundle(DirectBundle.Files[0].Contents,
		DirectBundle.Files[1].Contents, DirectBundle, Error)
		|| DirectBundle.Files.size() != 2 || DirectBundle.Files[0].RelativePath != "reused.cpp"
		|| DirectBundle.Files[1].Contents != ReusedSource)
	{
		std::cerr << "source-bundle reset invalidated borrowed inputs: " << Error << '\n';
		return EXIT_FAILURE;
	}
	Error = "reused_error";
	if (!VeroScript::EmitCppSourceBundleFromProgram(*Program, Error, DirectBundle, Error)
		|| DirectBundle.Files.size() != 3 || DirectBundle.Files[0].RelativePath != "reused_error.cpp")
	{
		std::cerr << "source-bundle emission cleared its borrowed name: " << Error << '\n';
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
