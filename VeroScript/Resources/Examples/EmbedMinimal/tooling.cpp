// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptAnalyzer.h"
#include "VeroScriptCompile.h"
#include "VeroScriptDapTcp.h"
#include "VeroScriptMacro.h"
#include "VeroScriptVm.h"

#include <iostream>
#include <memory>
#include <sstream>

int main()
{
	// Virtual resources exercise the installed SDK without a source-tree search
	// path.
	VeroScript::FCompileOptions Options;
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Options.SourceOverlays = {{"sdk-macros/library.vs", "#define PRIVATE 1\n#define_export VALUE "
														"21\n#define_export DOUBLE(x) ((x) * 2)\n"},
							  {"sdk-macros/facade.vs", "export import \"library.vs\";\n"}};
	auto Result = VeroScript::VeroScriptCompile::CompileSource(
		"import \"facade.vs\";\n#ifdef PRIVATE\n#error private macro "
		"leaked\n#endif\n"
		"int main() { print(DOUBLE(VALUE)); return 0; }",
		"sdk-macros/main.vs", Options);
	if (!Result.bSuccess || !Result.Module)
	{
		std::cerr << Result.Error.Message << '\n';
		return 1;
	}
	std::ostringstream Output;
	auto Vm = std::make_unique<VeroScript::VeroScriptVm>(Output);
	VeroScript::BytecodeVm::Configure(*Vm);
	Vm->SetExecutionBackend(VeroScript::EExecutionBackend::Vm);
	if (!Vm->Execute(*Result.Module, VeroScript::ERunMode::Script) || Output.str() != "42\n")
	{
		std::cerr << "Unexpected SDK macro result: " << Output.str() << Vm->GetError().Message
				  << '\n';
		return 1;
	}

	const auto Analysis = VeroScript::Tooling::VeroScriptAnalyzer::AnalyzeSource(
		"#define PREFIX ;;;;;;;;;;;;;;;;;\nPREFIX import \"missing.vs\";\n", "sdk-macros/entry.vs");
	if (Analysis.bSuccess || Analysis.Diagnostics.empty())
	{
		std::cerr << "Missing SDK import diagnostic\n";
		return 1;
	}
	const auto &Error = Analysis.Diagnostics.front();
	if (!Error.bPhysicalLocation || Error.Range.StartLine != 2 || Error.Range.StartColumn != 8)
	{
		std::cerr << "Unexpected SDK diagnostic position: " << Error.Range.StartLine << ':'
				  << Error.Range.StartColumn << '\n';
		return 1;
	}

	// Validate transitive socket linkage without opening a listener or awaiting a
	// client.
	VeroScript::Tooling::FDapTcpRunOptions Tcp;
	Tcp.BindAddress = "invalid-address";
	Tcp.bAnnounceListen = false;
	if (VeroScript::Tooling::VeroScriptDapTcp::Run(Tcp) != 1)
	{
		std::cerr << "Invalid TCP address was accepted\n";
		return 1;
	}
	std::cout << "Tooling SDK macro, diagnostics and TCP linkage passed\n";
	return 0;
}
