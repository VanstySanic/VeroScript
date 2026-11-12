// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScript.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptVm.h"
#include <iostream>

int main()
{
	using namespace VeroScript;
	FModuleEnvironmentDesc Description;
	Description.EntryOwnerId = "app";
	Description.Scopes = {{"app", {"math"}}, {"math", {}}};
	Description.Modules = {{"math/api.vs", "math.api", "veroscript-module:/math/api.vs", "math", "example-0.1.1",
		std::string("export int answer(){return 42;}")}};
	std::string Error;
	auto Environment = FModuleEnvironment::Create(std::move(Description), Error);
	if (!Environment)
	{
		std::cerr << Error << '\n';
		return 1;
	}
	FCompileOptions Options;
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Options.ModuleEnvironment = Environment;
	auto Result = VeroScriptCompile::CompileSource(
		"import \"math/api.vs\"; int main(){return answer();}", "<module-example>", Options);
	if (!Result.bSuccess)
	{
		std::cerr << Result.Error.Message << '\n';
		return 2;
	}
	VeroScriptVm Vm(std::cout);
	VeroScript::BytecodeVm::Configure(Vm);
	if (!Vm.SetModuleEnvironment(Environment, Error) || !Vm.Execute(*Result.Module, ERunMode::Script, {}))
	{
		std::cerr << Error << Vm.GetError().Message << '\n';
		return 3;
	}
	std::cout << Vm.GetProcessExitCode() << '\n';
	return Vm.GetProcessExitCode() == 42 ? 0 : 4;
}
