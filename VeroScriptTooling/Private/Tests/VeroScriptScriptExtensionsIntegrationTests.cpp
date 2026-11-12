// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptSession.h"
#include "VeroScriptHost.h"
#include "VeroScriptInterop.h"
#include "VeroScriptCppAot.h"
#include "VeroScriptCSharpAot.h"
#include "VeroScriptPythonAot.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace VeroScript;
namespace
{
	const char* Extension = R"vs(import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
out.add_declaration(ctx.top_scope(),"class Generated{public:int Get(){return 7;}};");};}
export void register_extension(std::compiler::registration reg){
std::compiler::annotation_definition a=std::compiler::annotation_definition();a.name="@Explain";a.documentation="Script registered documentation";reg.register_annotation(a);
std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
std::compiler::processor_definition d=std::compiler::processor_definition();d.id="integration.generated";d.version="0.1.1";d.phases=array<std::compiler::phase>{std::compiler::phase::types};d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);})vs";
	bool Require(bool value, const std::string& message)
	{
		if (!value)
		{
			std::cerr << message << '\n';
		}

		return value;
	}
	vs_interop_string_view View(const std::string& text)
	{
		return {text.data(), text.size()};
	}
} // namespace
int main()
{
	const auto directory = std::filesystem::temp_directory_path() /
						   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	std::filesystem::create_directories(directory);
	const auto extension = (directory / "extension.vs").generic_string(),
			   label = (directory / "main.vs").generic_string();
	{
		std::ofstream file(extension);
		file << Extension;
	}
	const std::string source =
		"@Explain class Original{};int read(){Generated g=Generated();return g.Get();}int main(){return read();}";
	FScriptCompileExtensionsOptions extensions;
	extensions.VmBackendFactory = VeroScript::Test::VmFactory();
	extensions.Roots = {{extension, {}}};
	std::ostringstream output, errors;
	FStreamEngineHost host(output, errors);
	VeroScriptSession session(host);
	VeroScript::Test::Configure(session);
	FEngineConfig configuration;
	VeroScript::Test::Configure(configuration);
	configuration.ExecutionBackend = EExecutionBackend::Vm;
	configuration.ScriptCompileExtensions = extensions;
	if (!Require(session.ApplyConfig(configuration), "apply script extension integration configuration"))
	{
		return 1;
	}
	auto control = std::make_shared<FExecutionControl>();
	session.SetExecutionControl(control);
	if (!Require(!session.GetScriptCompileExtensions().ExecutionControl,
			"configured null compile control retains Session inheritance"))
	{
		return 1;
	}

	control->RequestCancel();
	if (!Require(
			!session.LoadSource(source, label).bSuccess, "inherited control cancels extension compilation"))
	{
		return 1;
	}

	auto replacementControl = std::make_shared<FExecutionControl>();
	session.SetExecutionControl(replacementControl);
	if (!Require(!session.GetScriptCompileExtensions().ExecutionControl &&
				session.LoadSource(source, label).bSuccess,
			"compilation uses the replacement Session control instead of the cancelled token"))
	{
		return 1;
	}

	auto explicitControl = std::make_shared<FExecutionControl>();
	auto explicitExtensions = extensions;
	explicitExtensions.ExecutionControl = explicitControl;
	session.SetScriptCompileExtensions(explicitExtensions);
	session.SetExecutionControl(control);
	if (!Require(session.GetScriptCompileExtensions().ExecutionControl == explicitControl,
			"explicit compile cancellation control remains independent"))
	{
		return 1;
	}

	session.SetExecutionControl(replacementControl);
	explicitControl->RequestCancel();
	if (!Require(!session.LoadSource(source, label).bSuccess,
			"explicit compile control cancels compilation with a live runtime control"))
	{
		return 1;
	}

	session.SetExecutionControl(std::make_shared<FExecutionControl>());
	if (!Require(!session.LoadSource(source, label).bSuccess,
			"replacing runtime control does not overwrite explicit compile cancellation"))
	{
		return 1;
	}

	session.SetScriptCompileExtensions(extensions);
	if (!Require(session.LoadSource(source, label).bSuccess, "C++ script extension load: " + errors.str()))
	{
		return 1;
	}

	if (!Require(session.Run().ExitCode == 7, "C++ generated method execution"))
	{
		return 1;
	}

	Tooling::FAnalyzeOptions analyze;
	analyze.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	analyze.ScriptCompileExtensions = extensions;
	if (!Require(Tooling::VeroScriptAnalyzer::AnalyzeSource(source, label, analyze).bSuccess,
			"tooling generated declarations"))
	{
		return 1;
	}

	Tooling::FCompletionOptions completion;
	completion.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	completion.ScriptCompileExtensions = extensions;
	const auto completed = Tooling::VeroScriptComplete::CompleteAtSource(
		source, {1, static_cast<std::uint32_t>(source.find("g.Get") + 3)}, label, completion);
	bool found = false;
	for (const auto& item : completed.Items)
	{
		found |= item.Label == "Get";
	}

	if (!Require(completed.bSuccess && found, "tooling completes generated members"))
	{
		return 1;
	}

	Tooling::FHoverOptions hover;
	hover.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	hover.ScriptCompileExtensions = extensions;
	const auto described = Tooling::VeroScriptHover::HoverAtSource(source, {1, 3}, label, hover);
	if (!Require(described.bSuccess && described.Contents.find("Script registered") != std::string::npos,
			"tooling script annotation documentation"))
	{
		return 1;
	}

	FCodegenOptions options;
	options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	options.ScriptCompileExtensions = extensions;
	FCppCodegenBackend cpp;
	FCSharpCodegenBackend cs;
	FPythonCodegenBackend python;
	for (ICodegenBackend* backend : {static_cast<ICodegenBackend*>(&cpp), static_cast<ICodegenBackend*>(&cs),
									 static_cast<ICodegenBackend*>(&python)})
	{
		FCodegenResult result;
		if (!Require(backend->EmitFromSource(source, label, options, result),
				std::string(backend->GetName()) + ": " + result.Error))
		{
			return 1;
		}

		if (!Require(result.Output.find("Generated") != std::string::npos, "AOT retains generated type"))
		{
			return 1;
		}
	}
	FPythonAotSourceBundle bundle;
	std::string error;
	if (!Require(EmitPythonSourceBundleFromSource(source, label, "script_extensions", options, bundle, error),
			"Python bundle: " + error))
	{
		return 1;
	}

	vs_interop_session* interop = nullptr;
	if (!Require(vs_interop_session_create(nullptr, &interop) == VS_INTEROP_STATUS_OK, "Interop create"))
	{
		return 1;
	}

	vs_interop_compile_extension_root root{View(extension), {}};
	vs_interop_script_compile_options native{};
	native.struct_size = sizeof(native);
	native.allow_source_imports = 1;
	native.roots = &root;
	native.root_count = 1;
	native.max_instructions = 1000000;
	native.timeout_milliseconds = 2000;
	if (!Require(vs_interop_session_set_script_compile_options(interop, &native) == VS_INTEROP_STATUS_OK,
			"Interop copies configuration"))
	{
		return 1;
	}

	vs_interop_run_result result{};
	result.struct_size = sizeof(result);
	if (!Require(vs_interop_session_load_source(interop, View(source), View(label), &result) ==
				VS_INTEROP_STATUS_OK,
			"Interop generated type load"))
	{
		return 1;
	}

	if (!Require(vs_interop_session_run(interop, VS_INTEROP_RUN_SCRIPT, &result) == VS_INTEROP_STATUS_OK &&
				result.exit_code == 7,
			"Interop generated method execution"))
	{
		return 1;
	}

	vs_interop_session_destroy(interop);
	// Closed workspace documents also carry extension dependencies. Changing a
	// resource must refresh generated symbols without an open-document event.
	const auto workspaceDirectory = directory / "workspace";
	std::filesystem::create_directory(workspaceDirectory);
	std::string indexExtension = Extension;
	const std::string originalOperation =
		"out.add_declaration(ctx.top_scope(),\"class Generated{public:int Get(){return 7;}};\");";
	indexExtension.replace(indexExtension.find(originalOperation), originalOperation.size(),
		"string name=ctx.read_dependency(\"type.txt\").content();"
		"out.add_declaration(ctx.top_scope(),\"class \"+name+\"{};\");");
	{
		std::ofstream file(extension);
		file << indexExtension;
		std::ofstream mainFile(workspaceDirectory / "main.vs");
		mainFile << "import compiler \"" << extension << "\";class Original{};";
		std::ofstream resource(directory / "type.txt");
		resource << "FirstGenerated";
	}
	Tooling::VeroScriptWorkspace workspace;
	Tooling::FWorkspaceOptions workspaceOptions;
	workspaceOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	workspaceOptions.WorkspaceRoots.push_back(workspaceDirectory.generic_string());
	workspace.SetOptions(workspaceOptions);
	const auto hasSymbol = [](const auto& result, const std::string& name)
	{
		for (const auto& symbol : result.Symbols)
		{
			if (symbol.Name == name)
			{
				return true;
			}
		}

		return false;
	};
	const auto firstSymbols = workspace.WorkspaceSymbols();
	if (!Require(firstSymbols.bSuccess && hasSymbol(firstSymbols, "FirstGenerated"),
			"closed workspace document generates symbols"))
	{
		return 1;
	}

	{
		std::ofstream resource(directory / "type.txt");
		resource << "SecondGenerated";
	}
	const auto secondSymbols = workspace.WorkspaceSymbols();
	if (!Require(secondSymbols.bSuccess && hasSymbol(secondSymbols, "SecondGenerated") &&
				!hasSymbol(secondSymbols, "FirstGenerated"),
			"closed workspace extension resource invalidates symbols"))
	{
		return 1;
	}

	std::filesystem::remove(workspaceDirectory / "main.vs");
	std::filesystem::remove(workspaceDirectory);
	std::filesystem::remove(directory / "type.txt");
	std::filesystem::remove(directory / "extension.vs");
	std::filesystem::remove(directory);
	std::cout << "script extension host, tooling and AOT integration passed\n";
	return 0;
}
