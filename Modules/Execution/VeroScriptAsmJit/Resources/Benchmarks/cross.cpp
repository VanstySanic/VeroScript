// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptVm.h"
#include "VeroScriptJit.h"
#include "VeroScriptAsmJit.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <windows.h>
#include <psapi.h>

using namespace VeroScript;
using Clock = std::chrono::steady_clock;

int main(int argc, char **argv)
{
	try
	{
		if (argc < 5)
		{
			return 2;
		}
		std::ifstream file(argv[1]);
		std::string src((std::istreambuf_iterator<char>(file)), {});
		std::string backend = argv[2];
		int batch = std::stoi(argv[3]), expected = std::stoi(argv[4]);
		int repeats = argc > 5 ? std::stoi(argv[5]) : 7;
		auto t = Clock::now();
		VeroScriptModuleLoader loader("perf.vs");
		loader.SetSourceOverlay("perf.vs", src);
		auto p = loader.LoadSourceModule(src, "perf.vs");
		if (!p || loader.HasError())
		{
			throw std::runtime_error(loader.GetError().Message);
		}
		auto loaded = Clock::now();
		VeroScriptCompiler cc;
		auto mod = cc.Compile(*p);
		if (!mod || cc.HasError())
		{
			throw std::runtime_error(cc.GetError().Message);
		}
		auto compiled = Clock::now();
		std::cerr << argv[1] << " load_ms=" << std::chrono::duration<double, std::milli>(loaded - t).count()
				  << " compile_ms=" << std::chrono::duration<double, std::milli>(compiled - loaded).count() << "\n";
		std::cout << std::setprecision(10);
		for (int s = -1; s < repeats; s++)
		{
			std::ostringstream out;
			auto begin = Clock::now();
			auto vm = std::make_unique<VeroScriptVm>(out);
			std::string backendError;
			if (!vm->SetJitBackendFactory(CreateAsmJitBackendFactory(), backendError))
			{
				throw std::runtime_error(backendError);
			}
			vm->SetExecutionBackend(
				backend == "vm" ? EExecutionBackend::Vm
				: backend == "jit" ? EExecutionBackend::Jit
				: EExecutionBackend::Mixed);
			if (!vm->Execute(*mod, ERunMode::Script))
			{
				throw std::runtime_error(vm->GetError().Message);
			}
			auto stats = vm->GetJitStats();
			vm.reset();
			auto end = Clock::now();
			if (std::stoll(out.str()) != expected)
			{
				throw std::runtime_error("checksum mismatch: " + out.str() + " expected " + std::to_string(expected));
			}
			PROCESS_MEMORY_COUNTERS pm{};
			GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof(pm));
			std::cout << argv[1] << ',' << backend << ',' << s << ','
					  << std::chrono::duration<double, std::milli>(end - begin).count() / batch << ',' << batch << ','
					  << expected << ',' << pm.PeakWorkingSetSize << ',' << stats.NativeCfgEntries << ','
					  << stats.LinearTrampolineEntries << ',' << stats.VmTrampolineEntries << ','
					  << stats.NativeTypedLoopExecutions << ',' << stats.CostModelVmFallbacks << '\n'
					  << std::flush;
		}
	}
	catch (std::exception const &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
	return 0;
}
