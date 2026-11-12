// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptSession.h"
#include "VeroScriptVm.h"
#include "VeroScriptAsmJit.h"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace VeroScript;
using Clock = std::chrono::steady_clock;
struct FCaptureHost final : IEngineHost
{
	//~ Begin IEngineHost Interface
	void OnPrintLine(std::string_view) override
	{
	}

	void OnDiagnosticLine(std::string_view Line) override
	{
		Error.append(Line);
	}
	//~ End IEngineHost Interface

	std::string Error;

};

int main(int Argc, char** Argv)
{
	try
	{
		if (Argc != 4 || std::string(Argv[1]) != "host")
		{
			throw std::runtime_error("usage: host <samples> <calls>");
		}
		const int Samples = std::stoi(Argv[2]), Calls = std::stoi(Argv[3]);
		const char* RotationEnvironment = std::getenv("VEROSCRIPT_BENCH_ROTATION");
		const int SessionRotation = RotationEnvironment ? std::stoi(RotationEnvironment) : 0;
		if (SessionRotation < 0 || SessionRotation > 2)
		{
			throw std::runtime_error("invalid rotation");
		}
		if (Samples < 1 || Calls < 1 || Calls > 1000000)
		{
			throw std::runtime_error("invalid count");
		}
		const std::string Source = "int add(int a,int b){return a+b;} void main(){}";
		VeroScriptModuleLoader Loader("host-perf.vs");
		Loader.SetSourceOverlay("host-perf.vs", Source);
		auto Parsed = Loader.LoadSourceModule(Source, "host-perf.vs");
		VeroScriptCompiler Compiler;
		auto Module = Parsed ? Compiler.Compile(*Parsed) : nullptr;
		if (!Module)
		{
			throw std::runtime_error("host benchmark compilation failed");
		}
		std::uint16_t Index = 0;
		while (Index < Module->Functions.size() && Module->Functions[Index].Name.find("add#") == std::string::npos)
		{
			++Index;
		}
		if (Index == Module->Functions.size())
		{
			throw std::runtime_error("missing add function");
		}
		std::cout << std::setprecision(10)
			<< "case,backend,sample,load_ms,compile_ms,execute_ms,work_batch,execute_per_work_ms,compile_attempts,native_cfg,linear,cost_fallbacks,dispatches,typed_loops\n";
		for (int ApiPosition = 0; ApiPosition < 4; ++ApiPosition)
		{
			const int Api = (ApiPosition + SessionRotation) % 4;
			for (int Rotation = 0; Rotation < 3; ++Rotation)
			{
				const int BackendIndex = (Rotation + SessionRotation) % 3;
				const auto Backend = BackendIndex == 0 ? EExecutionBackend::Vm : BackendIndex == 1 ? EExecutionBackend::Mixed
																								   : EExecutionBackend::Jit;
				FCaptureHost Host;
				VeroScriptSession Session(Host);
				Session.SetJitBackendFactory(CreateAsmJitBackendFactory());
				Session.SetExecutionBackend(Backend);
				Session.SetHotReloadEnabled(Api == 2);
				FJitPolicy Policy;
				Policy.bReuseSessionCode = Api != 3;
				Session.SetJitPolicy(Policy);
				if (!Session.LoadSource(Source, "host-perf.vs").bSuccess)
				{
					throw std::runtime_error(Host.Error);
				}
				std::ostringstream Output;
				auto Vm = std::make_unique<VeroScriptVm>(Output);
				std::string BackendError;
				if (!Vm->SetJitBackendFactory(CreateAsmJitBackendFactory(), BackendError))
				{
					throw std::runtime_error(BackendError);
				}
				Vm->SetExecutionBackend(Backend);
				const std::vector<FValue> Arguments{std::int32_t{17}, std::int32_t{31}};
				std::size_t PreviousVmDispatches = 0;
				for (int Sample = -1; Sample < Samples; ++Sample)
				{
					std::size_t Compiles = 0, Dispatches = 0;
					const auto Begin = Clock::now();
					for (int Call = 0; Call < Calls; ++Call)
					{
						FValue Result;
						const bool Ok = Api == 0 ? Vm->ExecuteFunction(*Module, Index, Arguments, nullptr, &Result)
							: Session.CallFunction("add", Arguments, &Result).bSuccess;
						if (!Ok || !std::holds_alternative<std::int32_t>(Result) || std::get<std::int32_t>(Result) != 48)
						{
							throw std::runtime_error("host call result mismatch: " + Host.Error);
						}
						const auto Stats = Api == 0 ? Vm->GetJitStats() : Session.GetLastJitStats();
						Compiles += Stats.CompileAttempts;
						// The raw reused VM's dispatch counters accumulate across
						// ExecuteFunction calls; ordinary sessions use fresh VMs.
						Dispatches += Api == 0 ? Stats.Dispatches - PreviousVmDispatches : Stats.Dispatches;
						if (Api == 0)
						{
							PreviousVmDispatches = Stats.Dispatches;
						}
					}
					const double Elapsed = std::chrono::duration<double, std::milli>(Clock::now() - Begin).count();
					if (Sample >= 0)
					{
						std::cout << (Api == 0 ? "vm_api" : Api == 1 ? "session"
											 : Api == 2 ? "persistent_session"
																	 : "session_no_reuse")
							<< ',' << ExecutionBackendName(Backend) << ',' << Sample << ",0,0," << Elapsed
							<< ',' << Calls << ',' << Elapsed / Calls << ',' << Compiles << ",0,0,0," << Dispatches << ",0\n";
					}
				}
			}
		}
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
