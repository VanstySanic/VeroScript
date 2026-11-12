// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Diagnostic stress run, separate from the throughput harness. Process memory
// includes the compiler, host allocator and runtime; it is not runtime net heap.
#include "VeroScriptSession.h"
#include "VeroScriptAsmJit.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif

using namespace VeroScript;

struct FHost final : IEngineHost
{
	//~ Begin IEngineHost Interface
	void OnPrintLine(std::string_view) override
	{
	}

	void OnDiagnosticLine(std::string_view Text) override
	{
		std::cerr << Text;
	}
	//~ End IEngineHost Interface
};

void Snapshot(const char* Stage, std::size_t Calls, std::size_t Compiles,
	FSessionJitCacheStats Cache = {})
{
	std::size_t PrivateBytes = 0;
	std::size_t PeakWorkingSet = 0;
	#if defined(_WIN32)
	PROCESS_MEMORY_COUNTERS_EX Counters{};
	if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&Counters), sizeof(Counters)))
	{
		throw std::runtime_error("process memory counters unavailable");
	}
	PrivateBytes = Counters.PrivateUsage;
	PeakWorkingSet = Counters.PeakWorkingSetSize;
	#endif
	std::cout << Stage << ',' << Calls << ',' << Compiles << ',' << PrivateBytes << ',' << PeakWorkingSet
		<< ',' << Cache.CompileAttempts << ',' << Cache.RetainedPayloadBytes << ',' << Cache.NativeBytes
		<< ',' << Cache.bResourceLimitReached << '\n';
}

int main(int Argc, char** Argv)
{
	try
	{
		if (Argc != 4)
		{
			throw std::runtime_error("usage: <mixed|jit> <functions> <warm-rounds>");
		}
		const std::string BackendName = Argv[1];
		if (BackendName != "mixed" && BackendName != "jit")
		{
			throw std::runtime_error("invalid backend");
		}
		const int Count = std::stoi(Argv[2]);
		const int Rounds = std::stoi(Argv[3]);
		if (Count < 1 || Count > 4000 || Rounds < 1 || Rounds > 10000)
		{
			throw std::runtime_error("invalid count");
		}
		std::string Source = "void main(){}";
		std::vector<std::string> Names;
		for (int I = 0; I < Count; ++I)
		{
			Names.push_back("value" + std::to_string(I));
			Source += "int " + Names.back() + "(){return " + std::to_string(I + 1) + ";}";
		}
		FHost Host;
		std::cout << "stage,calls,compile_attempts,process_private_bytes,process_peak_working_set_bytes,"
			"context_compile_attempts,retained_payload_bytes,native_reserved_bytes,resource_limit_reached\n";
		Snapshot("before_session", 0, 0);
		auto Session = std::make_unique<VeroScriptSession>(Host);
		Session->SetJitBackendFactory(CreateAsmJitBackendFactory());
		Session->SetExecutionBackend(BackendName == "mixed" ? EExecutionBackend::Mixed : EExecutionBackend::Jit);
		if (!Session->LoadSource(Source, "resource-stress.vs").bSuccess)
		{
			throw std::runtime_error("load failed");
		}
		Snapshot("loaded", 0, 0, Session->GetJitCacheStats());
		std::size_t Calls = 0, Compiles = 0;
		const auto Sweep = [&]
		{
			for (int I = 0; I < Count; ++I)
			{
				FValue Value;
				if (!Session->CallFunction(Names[I], {}, &Value).bSuccess
					|| !std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != I + 1)
				{
					throw std::runtime_error("stress result mismatch");
				}
				++Calls;
				Compiles += Session->GetLastJitStats().CompileAttempts;
			}
		};
		Sweep();
		const std::size_t ColdCompiles = Compiles;
		Snapshot("cold_sweep", Calls, Compiles, Session->GetJitCacheStats());
		for (int Round = 0; Round < Rounds; ++Round)
		{
			Sweep();
			if ((Round + 1) % 50 == 0 || Round + 1 == Rounds)
			{
				Snapshot("warm_sweeps", Calls, Compiles, Session->GetJitCacheStats());
			}
		}
		if (Compiles != ColdCompiles)
		{
			throw std::runtime_error("unexpected warm recompilation");
		}
		Session->Clear();
		Snapshot("cleared", Calls, Compiles, Session->GetJitCacheStats());
		Session.reset();
		Snapshot("destroyed", Calls, Compiles);
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
