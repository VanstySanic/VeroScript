// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptCompile.h"
#include "VeroScriptPath.h"
#include "VeroScriptVm.h"
#include <iostream>

int RunScriptCompileExample(int argc, const std::string& entry, const std::string& stdlib)
{
	VeroScript::FCompileOptions options;
	options.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	#if defined(VEROSCRIPT_EXAMPLE_COMPILE_TIMEOUT_MS)
	// Allow the example's compiler startup to finish under a cross emulator.
	options.ScriptCompileExtensions.TimeoutMilliseconds = VEROSCRIPT_EXAMPLE_COMPILE_TIMEOUT_MS;
	#endif
	if (argc > 2)
	{
		options.StdlibSearchPaths.push_back(stdlib);
	}

	auto result = VeroScript::VeroScriptCompile::CompileFile(entry, options);
	if (!result.bSuccess)
	{
		std::cerr << result.Error.Message << '\n';
		return 1;
	}
	VeroScript::VeroScriptVm vm(std::cout);
	VeroScript::BytecodeVm::Configure(vm);
	return vm.Execute(*result.Module, VeroScript::ERunMode::Script) ? vm.GetProcessExitCode() : 1;
}

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
	return RunScriptCompileExample(argc,
		argc > 1 ? VeroScript::PathUtil::ToUtf8(std::filesystem::path(argv[1])) : "main.vs",
		argc > 2 ? VeroScript::PathUtil::ToUtf8(std::filesystem::path(argv[2])) : "");
}
#else
int main(int argc, char** argv)
{
	return RunScriptCompileExample(argc, argc > 1 ? argv[1] : "main.vs", argc > 2 ? argv[2] : "");
}
#endif
