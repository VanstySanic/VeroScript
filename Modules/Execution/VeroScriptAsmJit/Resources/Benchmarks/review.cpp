// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptVm.h"
#include "VeroScriptJit.h"
#include "VeroScriptAsmJit.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <array>
#include <windows.h>
#include <psapi.h>

using Clock = std::chrono::steady_clock;
using namespace VeroScript;

struct FBenchCase
{
	const char *Name;
	const char *Source;
	const char *Expected;
	double BaselineMilliseconds;
	double GateMilliseconds;
};

#include "cases.inc"

double ms(Clock::time_point a, Clock::time_point b)
{
	return std::chrono::duration<double, std::milli>(b - a).count();
}

int main(int argc, char **argv)
{
	try
	{
		std::string filter = argc > 1 ? argv[1] : "all";
		int repeats = argc > 2 ? std::stoi(argv[2]) : 5;
		if (filter == "sizes")
		{
			std::cout << "FValue=" << sizeof(FValue) << " FScriptObject=" << sizeof(FScriptObject)
					  << " FDynamicArray=" << sizeof(FDynamicArray) << " FMap=" << sizeof(FMap)
					  << " VeroScriptVm=" << sizeof(VeroScriptVm) << "\n";
			return 0;
		}
		if (filter == "export")
		{
			std::filesystem::create_directories("sources");
			for (auto &c : Cases)
			{
				std::ofstream("sources/" + std::string(c.Name) + ".vs") << c.Source;
			}
			return 0;
		}
		std::cout
			<< std::setprecision(9)
			<< "case,backend,sample,load_ms,compile_ms,execute_ms,total_ms,peak_working_set,compile_attempts,native_cfg,linear,vm_trampoline,unsupported,cost_fallbacks,typed_loops,direct_calls\n";
		for (auto &c : Cases)
		{
			if (filter != "all" && filter != c.Name)
			{
				continue;
			}
			for (int sample = -1; sample < repeats; sample++)
			{
				// Rotate backend order to reduce ordering bias. Sample -1 is excluded warmup.
				for (int slot = 0; slot < 3; slot++)
				{
					int b = (slot + sample + 3) % 3;
					EExecutionBackend backend = b == 0	 ? EExecutionBackend::Vm
												: b == 1 ? EExecutionBackend::Mixed
														 : EExecutionBackend::Jit;
					auto begin = Clock::now();
					VeroScriptModuleLoader loader("perf.vs");
					loader.SetSourceOverlay("perf.vs", c.Source);
					auto p = loader.LoadSourceModule(c.Source, "perf.vs");
					if (!p || loader.HasError())
					{
						throw std::runtime_error(loader.GetError().Message);
					}
					auto loaded = Clock::now();
					VeroScriptCompiler compiler;
					auto module = compiler.Compile(*p);
					if (!module || compiler.HasError())
					{
						throw std::runtime_error(compiler.GetError().Message);
					}
					auto compiled = Clock::now();
					std::ostringstream output;
					auto vm = std::make_unique<VeroScriptVm>(output);
					std::string backendError;
					if (!vm->SetJitBackendFactory(CreateAsmJitBackendFactory(), backendError))
					{
						throw std::runtime_error(backendError);
					}
					vm->SetExecutionBackend(backend);
					bool ok = vm->Execute(*module, ERunMode::Script);
					auto stats = vm->GetJitStats();
					if (!ok)
					{
						throw std::runtime_error(std::string(c.Name) + "/" + ExecutionBackendName(backend) + ": " +
												 vm->GetError().Message);
					}
					vm.reset();
					auto executed = Clock::now();
					if (output.str() != c.Expected)
					{
						throw std::runtime_error(std::string(c.Name) + " output mismatch: " + output.str());
					}
					PROCESS_MEMORY_COUNTERS pm{};
					GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof(pm));
					if (sample >= 0)
					{
						std::cout << c.Name << ',' << ExecutionBackendName(backend) << ',' << sample << ','
								  << ms(begin, loaded) << ',' << ms(loaded, compiled) << ',' << ms(compiled, executed)
								  << ',' << ms(begin, executed) << ',' << pm.PeakWorkingSetSize << ','
								  << stats.CompileAttempts << ',' << stats.NativeCfgEntries << ','
								  << stats.LinearTrampolineEntries << ',' << stats.VmTrampolineEntries << ','
								  << stats.UnsupportedEntries << ',' << stats.CostModelVmFallbacks << ','
								  << stats.NativeTypedLoopExecutions << ',' << stats.DirectNativeCalls << '\n'
								  << std::flush;
					}
				}
			}
		}
	}
	catch (std::exception const &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
	return 0;
}
