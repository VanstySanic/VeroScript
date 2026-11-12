// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptAst.h"
#include "VeroScriptCompile.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptTemplateQuery.h"
#include "VeroScriptVm.h"

#include <algorithm>
#include <iostream>
#include <sstream>

int main()
{
	using namespace VeroScript;
	const auto Compiled = VeroScriptCompile::CompileSource(
		"template<typename T,int N=4> struct Buffer{T data[N];int capacity(){return N;}"
		"template<class U> U echo(U value){return value;}};"
		"void main(){Buffer<int> value=Buffer<int>();print(value.echo(7));print(value.capacity());}",
		"sdk-templates.vs");
	if (!Compiled.bSuccess)
	{
		std::cerr << Compiled.Error.Message << '\n';
		return 1;
	}
	const auto &Program = Compiled.PreparedProgram->GetProgram();
	FTemplateCallTypeQuery Query(Program, nullptr, Program.EntryModulePath, {});
	if (Query.ResolveReturnType("Buffer<int,4>", "echo", {"int"}) != "int")
	{
		return 2;
	}
	std::vector<FTemplateDeclarationInfo> Declarations;
	ModuleMeta::EnumerateTemplateDeclarations(*Compiled.Module, Declarations);
	const auto Method = std::find_if(Declarations.begin(), Declarations.end(), [](const auto &Declaration)
	{
		return Declaration.Kind == "method" && Declaration.Name == "echo";
	});
	if (Method == Declarations.end() || Method->Parameters.GetOwnCount() != 1 ||
		Method->EnclosingParameters.GetParameters().size() != 2)
	{
		return 3;
	}
	std::ostringstream Output, Errors;
	std::istringstream Input;
	VeroScriptVm Vm(Output, Input, Errors);
	VeroScript::BytecodeVm::Configure(Vm);
	Vm.SetExecutionBackend(EExecutionBackend::Vm);
	if (!Vm.Execute(*Compiled.Module, ERunMode::Script) || Output.str() != "7\n4\n")
	{
		std::cerr << Vm.GetError().Message << '\n';
		return 4;
	}
	std::cout << Output.str();
	return 0;
}
