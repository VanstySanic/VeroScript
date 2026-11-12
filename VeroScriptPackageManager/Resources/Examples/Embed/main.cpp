// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPackageManager.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"
#include <iostream>
int main(int argc, char** argv)
{
	if (argc < 3 || argc > 4)
	{
		std::cerr << "Use absolute project root, entry script and optional stdlib directory paths\n";
		return 2;
	}
	using namespace VeroScript::Packages;
	FPackageManager Manager;
	FEnvironmentInput Input;
	Input.Root = argv[1];
	FDiagnostic Error;
	std::shared_ptr<const FInstalledEnvironment> Installed;
	if (!Manager.Load(Input, Installed, Error))
	{
		std::cerr << Error.Code << ": " << Error.Message;
		return 1;
	}
	FCoreEnvironmentOptions Options;
	Options.EntryRoot = argv[1];
	if (argc == 4)
	{
		Options.StdlibSearchPaths = {argv[3]};
	}
	std::shared_ptr<const VeroScript::FModuleEnvironment> Environment;
	if (!Installed->BuildCoreEnvironment(Options, Environment, Error))
	{
		std::cerr << Error.Code << ": " << Error.Message;
		return 1;
	}
	VeroScript::FCompileOptions Compile;
	Compile.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Compile.ModuleEnvironment = Environment;
	auto Code = VeroScript::VeroScriptCompile::CompileFile(argv[2], Compile);
	if (!Code.bSuccess)
	{
		std::cerr << Code.Error.Message;
		return 1;
	}
	VeroScript::VeroScriptVm Vm(std::cout);
	VeroScript::BytecodeVm::Configure(Vm);
	std::string Failure;
	if (!Vm.SetModuleEnvironment(Environment, Failure))
	{
		std::cerr << Failure;
		return 1;
	}
	if (!Vm.Execute(*Code.Module, VeroScript::ERunMode::Script))
	{
		std::cerr << Vm.GetError().Message;
		return 1;
	}
	return Vm.GetProcessExitCode();
}
