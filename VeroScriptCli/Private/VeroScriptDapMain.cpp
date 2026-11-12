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
	// Thin alias of `veroscript --debug` (same DAP stdio host).
	#if defined(_WIN32)
	if (_setmode(_fileno(stdin), _O_BINARY) == -1
		|| _setmode(_fileno(stdout), _O_BINARY) == -1)
	{
		std::cerr << "veroscript-dap: failed to configure binary stdio\n";
		return 1;
	}
	#endif
	VeroScript::Tooling::FDapStdioRunOptions Options;
	#if VEROSCRIPT_RUNTIME_HAS_VM
	Options.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Options.ScriptCompileExtensions.VmBackendFactory = Options.VmBackendFactory;
	#endif
	return VeroScript::Tooling::VeroScriptDapStdio::Run(std::cin, std::cout, Options);
}
