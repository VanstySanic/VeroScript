// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptApplication.h"
#include "VeroScriptBuild.h"
#include "VeroScriptCompile.h"
#include "VeroScriptPackageManager.h"
#include "VeroScriptVm.h"
#include <iostream>
#include <sstream>

int main()
{
	using namespace VeroScript;
	Application::FDiagnostic Error;
	auto Compiled = VeroScriptCompile::CompileSource("int main() { return 41; }");
	if (!Compiled.bSuccess) return 1;
	FPackageBuildOptions Options;
	Options.PackageName = "sdk.application";
	std::vector<Application::FContentFile> Files;
	std::vector<std::uint8_t> Archive;
	if (!Application::BuildApplication(*Compiled.Module, Options, "0.1.1", {}, Files, Error) ||
	    !Application::WriteArchive(Files, Archive, Error))
	{
		return 2;
	}
	auto App = Application::FApplication::Open(std::move(Archive), Application::CreateApplicationServices(), Error);
	Application::FPreparedApplication Prepared;
	if (!App || !App->Prepare(Prepared, Error))
	{
		std::cerr << Error.Message;
		return 3;
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	VeroScript::BytecodeVm::Configure(Vm);
	std::string Failure;
	if (!Vm.SetModuleEnvironment(Prepared.Environment, Failure) || !Vm.Execute(*Prepared.Package->Module, ERunMode::Script) || Vm.GetProcessExitCode() != 41) return 4;
	// Link the independent services without an installer, solver or HTTP client.
	Packages::FDiagnostic PackageError;
	std::shared_ptr<const Packages::FInstalledEnvironment> Environment;
	if (Packages::FPackageManager().LoadDeployment("", Environment, PackageError) || PackageError.Code.empty()) return 5;
	Build::FDiagnostic BuildError;
	std::shared_ptr<const Build::FBuildPlan> Plan;
	if (Build::FBuilder::Plan({}, {}, Plan, BuildError) || BuildError.Code.empty()) return 6;
	std::cout << "Installed application/build/package-runtime SDK consumer passed.\n";
	return 0;
}
