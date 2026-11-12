// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptProcessingToolingFixture.h"
#include "VeroScriptPath.h"
#include "VeroScriptHostBindingCatalog.h"
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int Argc, char** Argv)
{
	#ifdef _WIN32
	if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
	#endif
	if (Argc == 2 && std::string_view(Argv[1]) == "compiler-catalog")
	{
		std::string Json;
		if (!VeroScript::ExportBindingCatalogJson(*VeroScript::GetScriptCompileServiceCatalog(), Json)) return 2;
		std::cout << Json;
		return 0;
	}
	if (Argc != 3) return 2;
	VeroScript::FCompileProcessingOptions Processing;
	Processing.Registry = VeroScript::Tooling::Tests::BuilderRegistry();
	Processing.AnnotationDefinitions.push_back({"tooling.builder", "tooling", "@Builder", "@Builder(string label)",
		"Generates a builder type.", {{"label", "string label", "The builder label.", {}}}});
	VeroScript::FAnnotationDefinition Relaxed;
	Relaxed.Id = "tooling.gui-widget";
	Relaxed.Name = "@GuiWidget";
	Relaxed.CaseSensitive = false;
	Relaxed.Documentation = "Widget documentation.";
	Relaxed.Arguments = {{"Label", "string", "Label documentation.", {}}, {"Enabled", "bool", "Enabled documentation.", {}}};
	Processing.AnnotationDefinitions.push_back(Relaxed);
	Relaxed.Id = "tooling.strict-widget";
	Relaxed.Name = "@StrictWidget";
	Relaxed.CaseSensitive = true;
	Relaxed.Documentation = "Strict widget documentation.";
	Processing.AnnotationDefinitions.push_back(std::move(Relaxed));
	VeroScript::FAnnotationDefinition Legacy;
	Legacy.Id = "tooling.legacy-options";
	Legacy.Name = "@LegacyOptions";
	Legacy.Detail = "annotation (map<string,int> values, string label)";
	Legacy.Documentation = "Legacy options documentation.";
	Processing.AnnotationDefinitions.push_back(std::move(Legacy));
	Processing.ReadDependency = [Path = std::string(Argv[1])](std::string_view, std::string& Text)
	{
		std::ifstream Input(VeroScript::PathUtil::FromUtf8(Path), std::ios::binary);
		if (!Input) return false;
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		Text = Buffer.str();
		return !Input.bad();
	};
	if (std::string_view(Argv[2]) == "dap")
	{
		VeroScript::Tooling::FDapStdioRunOptions Options;
		Options.CompileProcessing = std::move(Processing);
		return VeroScript::Test::FDapStdio::Run(std::cin, std::cout, Options);
	}
	VeroScript::Tooling::FLspStdioRunOptions Options;
	Options.CompileProcessing = std::move(Processing);
	return VeroScript::Test::FLspStdio::Run(std::cin, std::cout, Options);
}
