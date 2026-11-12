// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTooling.h"
#if VEROSCRIPT_RUNTIME_HAS_VM
#include "VeroScriptBytecodeVm.h"
#endif

#include <iostream>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main()
{
	#if defined(_WIN32)
	if (_setmode(_fileno(stdin), _O_BINARY) == -1
		|| _setmode(_fileno(stdout), _O_BINARY) == -1)
	{
		std::cerr << "veroscript-lsp: failed to configure binary stdio\n";
		return 1;
	}
	#endif
	VeroScript::Tooling::FLspStdioRunOptions Options;
	#if VEROSCRIPT_RUNTIME_HAS_VM
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	#endif
	return VeroScript::Tooling::VeroScriptLspStdio::Run(std::cin, std::cout, Options);
}
