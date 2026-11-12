// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Diagnostic executable: not linked into the runtime or its public ABI.
#include "VeroScriptCompiler.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptVm.h"
#include "VeroScriptJit.h"
#include "VeroScriptJitLinear.h"
#include "VeroScriptJitLowering.h"

#include "VeroScriptAsmJit.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::AsmJit;
	using Clock = std::chrono::steady_clock;
	struct FBenchCase
	{
		const char* Name;
		const char* Source;
		const char* Expected;
		double Historical;
		double Gate;
	};
// Deliberately frozen corpus: never silently re-extract cases during comparison.
	#include "cases.inc"

	double Milliseconds(const Clock::time_point Begin, const Clock::time_point End)
	{
		return std::chrono::duration<double, std::milli>(End - Begin).count();
	}

	int Positive(const char* Text)
	{
		std::size_t End = 0;
		const int Value = std::stoi(Text, &End);
		if (Text[End] != '\0' || Value < 1 || Value > 1000000)
		{
			throw std::runtime_error("expected positive integer <= 1000000");
		}
		return Value;
	}

	void Measure(const FBenchCase& Case, const int Repeats, const int Batch)
	{
		for (int Sample = -1; Sample < Repeats; ++Sample)
		{
			for (int Slot = 0; Slot < 3; ++Slot)
			{
				const int BackendIndex = (Slot + Sample + 3) % 3;
				const EExecutionBackend Backend = BackendIndex == 0 ? EExecutionBackend::Vm
				: BackendIndex == 1 ? EExecutionBackend::Mixed
																	: EExecutionBackend::Jit;
				const auto Begin = Clock::now();
				VeroScriptModuleLoader Loader("perf.vs");
				Loader.SetSourceOverlay("perf.vs", Case.Source);
				auto Parsed = Loader.LoadSourceModule(Case.Source, "perf.vs");
				if (!Parsed || Loader.HasError())
				{
					throw std::runtime_error(Loader.GetError().Message);
				}
				const auto Loaded = Clock::now();
				VeroScriptCompiler Compiler;
				auto Module = Compiler.Compile(*Parsed);
				if (!Module || Compiler.HasError())
				{
					throw std::runtime_error(Compiler.GetError().Message);
				}
				const auto Compiled = Clock::now();
				std::ostringstream Output;
				auto Vm = std::make_unique<VeroScriptVm>(Output);
				std::string BackendError;
				if (!Vm->SetJitBackendFactory(CreateAsmJitBackendFactory(), BackendError))
				{
					throw std::runtime_error(BackendError);
				}
				Vm->SetExecutionBackend(Backend);
				if (!Vm->Execute(*Module, ERunMode::Script))
				{
					throw std::runtime_error(Vm->GetError().Message);
				}
				const auto Stats = Vm->GetJitStats();
				Vm.reset();
				const auto Executed = Clock::now();
				if (Output.str() != Case.Expected)
				{
					throw std::runtime_error(std::string(Case.Name) + ": unexpected output: " + Output.str());
				}
				if (Sample < 0)
				{
					continue;
				}
				// Only execute_per_work_ms is normalized. Batch is work calls IN the script,
				// not VM executions; execute_ms still includes JIT/init/teardown.
				std::cout << Case.Name << ',' << ExecutionBackendName(Backend) << ',' << Sample
				<< ',' << Milliseconds(Begin, Loaded) << ',' << Milliseconds(Loaded, Compiled)
				<< ',' << Milliseconds(Compiled, Executed) << ',' << Batch
				<< ',' << Milliseconds(Compiled, Executed) / Batch
				<< ',' << Stats.CompileAttempts << ',' << Stats.NativeCfgEntries
				<< ',' << Stats.LinearTrampolineEntries << ',' << Stats.CostModelVmFallbacks
				<< ',' << Stats.Dispatches << ',' << Stats.NativeTypedLoopExecutions << '\n'
						  << std::flush;
			}
		}
	}
}

int main(int Argc, char** Argv)
{
	try
	{
		if (Argc < 2)
		{
			throw std::runtime_error("usage: matrix <case|all> <samples> OR source <path> <expected-integer-line> <work-batch> <samples>");
		}
		std::cout << std::setprecision(10);
		const std::string Mode = Argv[1];
		if (Mode == "layout")
		{
			if (Argc != 2)
			{
				throw std::runtime_error("usage: layout");
			}
			std::cout << "pointer_bytes=" << sizeof(void*) << " policy_bytes=" << sizeof(FJitPolicy)
				<< " vm_bytes=" << sizeof(VeroScriptVm) << " cache_bytes=" << sizeof(FJitCodeCache)
				<< " attempts_offset=" << offsetof(FJitPolicy, MaxCompileAttempts)
				<< " capacity_offset=" << offsetof(FJitPolicy, MaxCachedEntries)
				<< " eager_offset=" << offsetof(FJitPolicy, EagerCompileBudget) << '\n';
			return 0;
		}
		if (Mode == "inspect")
		{
			if (Argc != 3)
			{
				throw std::runtime_error("usage: inspect <matrix-case>");
			}
			for (const auto& Case : Cases)
			{
				if (std::string(Argv[2]) != Case.Name)
				{
					continue;
				}
				VeroScriptModuleLoader Loader("perf.vs");
				Loader.SetSourceOverlay("perf.vs", Case.Source);
				auto Parsed = Loader.LoadSourceModule(Case.Source, "perf.vs");
				if (!Parsed || Loader.HasError())
				{
					throw std::runtime_error(Loader.GetError().Message);
				}
				VeroScriptCompiler Compiler;
				auto Module = Compiler.Compile(*Parsed);
				if (!Module || Compiler.HasError())
				{
					throw std::runtime_error(Compiler.GetError().Message);
				}
				FJitCodeCache Cache;
				Cache.PrepareModule(*Module, EExecutionBackend::Jit);
				for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
				{
					const auto* Entry = Cache.GetCandidate(static_cast<std::uint16_t>(Index));
					if (!Entry)
					{
						continue;
					}
					JitLowering::FPlan OriginalPlan;
					const bool OriginalScalar = JitLowering::BuildScalarPlan(
						*Module, Module->Functions[Index], OriginalPlan) &&
						OriginalPlan.bScalarEligible;
					std::cout << Module->Functions[Index].Name << " prefer_vm=" << Entry->bPreferVmForExecution
						<< " scalar_set=" << Entry->bUseScalarSetStorage
						<< " entry_kind=" << static_cast<int>(Entry->Kind)
						<< " stack=" << Entry->StackCapacityHint << " boxed_stack=" << Entry->BoxedStackCapacityHint
						<< " original_scalar=" << OriginalScalar << " original_stack=" << OriginalPlan.MaxStackSlots
						<< " original_boxed_stack=" << OriginalPlan.MaxBoxedStackSlots << '\n';
					if (!Entry->TypedLoopPlans)
					{
						continue;
					}
					for (const auto& Plan : Entry->TypedLoopPlans->Plans)
					{
						std::cout << "  header=" << Plan.HeaderOffset << " fused=" << static_cast<int>(Plan.FusedKind)
								<< " direct_affine_set=" << (Plan.FusedKind == JitLinear::FTypedLoopPlan::EFusedKind::DirectAffineSetContains)
								<< " native=" << (Plan.NativeI32Code != nullptr) << '\n';
					}
				}
				return 0;
			}
			throw std::runtime_error("no matching case");
		}
		if ((Mode == "matrix" && Argc != 4) || (Mode == "source" && Argc != 6))
		{
			throw std::runtime_error("incorrect argument count");
		}
		if (Mode != "matrix" && Mode != "source")
		{
			throw std::runtime_error("unknown benchmark mode");
		}
		std::cout << "case,backend,sample,load_ms,compile_ms,execute_ms,work_batch,execute_per_work_ms,compile_attempts,native_cfg,linear,cost_fallbacks,dispatches,typed_loops\n";
		if (Mode == "source")
		{
			std::ifstream File(Argv[2], std::ios::binary);
			if (!File)
			{
				throw std::runtime_error("cannot open source");
			}
			const std::string Source((std::istreambuf_iterator<char>(File)), {});
			const std::string Expected = std::string(Argv[3]) + '\n';
			Measure({"source", Source.c_str(), Expected.c_str(), 0, 0}, Positive(Argv[5]), Positive(Argv[4]));
		}
		else
		{
			bool Found = false;
			const int Repeats = Positive(Argv[3]);
			for (const auto& Case : Cases)
			{
				if (std::string(Argv[2]) != "all" && std::string(Argv[2]) != Case.Name)
				{
					continue;
				}
				Found = true;
				Measure(Case, Repeats, 1);
			}
			if (!Found)
			{
				throw std::runtime_error("no matching case");
			}
		}
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << "RuntimeBenchmarks: " << Error.what() << '\n';
		return 1;
	}
}
