// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompile.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptVm.h"
#include "VeroScriptJitBackend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace VeroScript;
using Clock = std::chrono::steady_clock;

namespace
{
	double Elapsed(const Clock::time_point Begin)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - Begin).count();
	}

	double Numeric(const FValue& Value)
	{
		if (const auto* Number = std::get_if<std::int32_t>(&Value)) return *Number;
		if (const auto* Number = std::get_if<std::int64_t>(&Value)) return static_cast<double>(*Number);
		if (const auto* Number = std::get_if<double>(&Value)) return *Number;
		throw std::runtime_error("non-numeric benchmark result");
	}
}

int main(int Argc, char** Argv)
{
	try
	{
		if (Argc != 11)
		{
			throw std::runtime_error("expected source backend mode profile n seed samples expected stdlib action");
		}
		std::ifstream Input(std::filesystem::u8path(Argv[1]), std::ios::binary);
		const std::string Source{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
		if (Source.empty())
		{
			throw std::runtime_error("missing source");
		}
		const std::string Action = Argv[10];
		if (Action != "run" && Action != "once" && Action != "compile")
		{
			throw std::runtime_error("invalid action");
		}
		const int N = std::stoi(Argv[5]);
		const int Seed = std::stoi(Argv[6]);
		const int Samples = std::stoi(Argv[7]);
		const double Expected = std::stod(Argv[8]);
		if (Samples < 0 || Samples > 10000 || !std::isfinite(Expected))
		{
			throw std::runtime_error("invalid sample count or expected result");
		}
		FCompileOptions Options;
		Options.StdlibSearchPaths = {Argv[9]};
		const auto CompileBegin = Clock::now();
		auto Compiled = VeroScriptCompile::CompileSource(Source, Argv[1], Options);
		const double CompileMs = Elapsed(CompileBegin);
		if (!Compiled.bSuccess)
		{
			throw std::runtime_error(Compiled.Error.Message);
		}
		if (Action == "compile")
		{
			std::cout << std::setprecision(17) << "{\"compile_ms\":" << CompileMs
			          << ",\"types\":" << Compiled.Module->Types.size()
			          << ",\"functions\":" << Compiled.Module->Functions.size() << "}\n";
			return 0;
		}
		std::uint16_t Function = 0;
		while (Function < Compiled.Module->Functions.size() && Compiled.Module->Functions[Function].Name.find("work#") == std::string::npos)
		{
			++Function;
		}
		if (Function == Compiled.Module->Functions.size())
		{
			throw std::runtime_error("missing work function");
		}
		std::shared_ptr<const FBytecodeModule> Owner = std::move(Compiled.Module);
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		EExecutionBackend Backend = EExecutionBackend::Vm;
		if (!TryParseExecutionBackend(Argv[3], Backend))
		{
			throw std::runtime_error("invalid backend");
		}
		Vm.SetExecutionBackend(Backend);
		std::string Error;
		const auto SetupBegin = Clock::now();
		if (Backend != EExecutionBackend::Vm)
		{
			if (!Vm.SetJitBackendPath(Argv[2], Error) || !Vm.SetJitProgramOwner(Owner, Error))
			{
				throw std::runtime_error(Error);
			}
		}
		const std::string Profile = Argv[4];
		if (Profile != "plain" && Profile != "combined" && Profile != "quota")
		{
			throw std::runtime_error("invalid execution profile");
		}
		if (Profile != "plain")
		{
			Vm.SetExecutionLimits({1000000000, Profile == "combined" ? 30000ull : 0ull});
			if (Profile == "combined")
			{
				Vm.SetExecutionControl(std::make_shared<FExecutionControl>());
				FScriptCapabilities Capabilities;
				Capabilities.bAllowFileRead = false;
				Capabilities.bAllowFileWrite = false;
				Capabilities.bAllowFileDelete = false;
				Capabilities.bAllowEnvironmentRead = false;
				Capabilities.bAllowEnvironmentWrite = false;
				Capabilities.bAllowConsoleInput = false;
				Capabilities.bAllowTaskSpawn = false;
				Capabilities.bAllowMemoryAddress = false;
				Vm.SetCapabilities(Capabilities);
			}
		}
		const double SetupMs = Elapsed(SetupBegin);
		std::vector<FValue> Arguments{std::int32_t{N}};
		if (Owner->Functions[Function].ParamCount == 2)
		{
			Arguments.emplace_back(std::int32_t{Seed});
		}
		double Value = 0;
		const auto Invoke = [&]()
		{
			FValue Returned;
			if (!Vm.ExecuteFunction(*Owner, Function, Arguments, nullptr, &Returned))
			{
				throw std::runtime_error(Vm.GetError().Message);
			}
			Value = Numeric(Returned);
			if (!std::isfinite(Value) || std::abs(Value - Expected) > 1e-9 * (std::max)(1.0, std::abs(Expected)))
			{
				throw std::runtime_error("checksum mismatch: " + std::to_string(Value) + " != " + std::to_string(Expected));
			}
		};
		const auto FirstBegin = Clock::now();
		Invoke();
		const double FirstMs = Elapsed(FirstBegin);
		const auto FirstStats = Vm.GetJitStats();
		if (Action == "once")
		{
			std::cout << std::setprecision(17) << "{\"compile_ms\":" << CompileMs << ",\"setup_ms\":" << SetupMs
			          << ",\"first_ms\":" << FirstMs << ",\"value\":" << Value << "}\n";
			return 0;
		}
		const auto WarmupBegin = Clock::now();
		for (int Warmup = 0; Warmup < 3 || Elapsed(WarmupBegin) < 200.0; ++Warmup)
		{
			Invoke();
		}
		int Batch = 1;
		for (;;)
		{
			const auto Begin = Clock::now();
			for (int Index = 0; Index < Batch; ++Index)
			{
				Invoke();
			}
			if (Elapsed(Begin) >= 30.0 || Batch >= 2048)
			{
				break;
			}
			Batch *= 2;
		}
		const auto Before = Vm.GetJitStats();
		std::vector<double> Times;
		for (int Sample = 0; Sample < Samples; ++Sample)
		{
			const auto Begin = Clock::now();
			for (int Index = 0; Index < Batch; ++Index)
			{
				Invoke();
			}
			Times.push_back(Elapsed(Begin) / Batch);
		}
		const auto Stats = Vm.GetJitStats();
		std::cout << std::setprecision(17) << "{\"compile_ms\":" << CompileMs << ",\"setup_ms\":" << SetupMs
		          << ",\"first_ms\":" << FirstMs << ",\"value\":" << Value << ",\"batch\":" << Batch << ",\"samples_ms\":[";
		for (std::size_t Index = 0; Index < Times.size(); ++Index)
		{
			if (Index != 0)
			{
				std::cout << ',';
			}
			std::cout << Times[Index];
		}
		std::cout << "],\"stats\":{\"compile_attempts\":" << Stats.CompileAttempts
		          << ",\"warm_compile_attempts\":" << (Stats.CompileAttempts - Before.CompileAttempts)
		          << ",\"cold_compile_attempts\":" << FirstStats.CompileAttempts
		          << ",\"native_cfg\":" << Stats.NativeCfgEntries << ",\"native_leaf\":" << Stats.NativeLeafEntries
		          << ",\"linear\":" << Stats.LinearTrampolineEntries << ",\"vm_trampoline\":" << Stats.VmTrampolineEntries
		          << ",\"unsupported\":" << Stats.UnsupportedEntries << ",\"dispatches\":" << Stats.Dispatches
		          << ",\"fallbacks\":" << Stats.Fallbacks << ",\"cost_fallbacks\":" << Stats.CostModelVmFallbacks
		          << ",\"typed_loops\":" << Stats.NativeTypedLoopExecutions
		          << ",\"controlled_native\":" << Stats.ControlledNativeExecutions
		          << ",\"instructions\":" << Vm.GetExecutedInstructionCount() << "}}\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
