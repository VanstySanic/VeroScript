// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#if !defined(VEROSCRIPT_AOT_WITH_INTEROP)
#include "VeroScriptBytecodeVmAssembly.h"
#endif
#include <VeroScriptAotProgram.h>
#include <VeroScriptBytecodeIO.h>
#include <VeroScriptVm.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#if defined(VEROSCRIPT_AOT_WITH_INTEROP)
#include <VeroScriptInterop.h>
#if defined(VEROSCRIPT_AOT_WITH_MOBILE)
#include <VeroScriptMobile.h>
#endif
extern "C" vs_interop_status VS_INTEROP_CALL CreateVeroScriptAotProgramInterop(vs_interop_aot_program**, vs_interop_buffer**);
#else
std::shared_ptr<const VeroScript::IAotProgram> CreateVeroScriptAotProgram(std::string&);
#endif

int main(int Argc, char** Argv)
{
	if (Argc != 3) return 1;
	std::ifstream Input(Argv[1], std::ios::binary);
	std::vector<std::uint8_t> Bytes{std::istreambuf_iterator<char>(Input), {}};
	#if defined(VEROSCRIPT_AOT_WITH_INTEROP)
	std::ifstream Application(Argv[2], std::ios::binary);
	std::vector<std::uint8_t> PackageBytes{std::istreambuf_iterator<char>(Application), {}};
	vs_interop_aot_program* Program = nullptr;
	vs_interop_buffer* Error = nullptr;
	if (CreateVeroScriptAotProgramInterop(&Program, &Error) != VS_INTEROP_STATUS_OK) return 2;
	vs_interop_session* Session = nullptr;
	std::string Output;
	vs_interop_session_options Options{};
	Options.struct_size = sizeof(Options);
	Options.user_data = &Output;
	Options.print_callback = [](vs_interop_string_view Text, void* Data)
	{
		static_cast<std::string*>(Data)->append(Text.data, Text.size).append("\n");
	};
	if (vs_interop_session_create(&Options, &Session) != VS_INTEROP_STATUS_OK) return 3;
	if (vs_interop_session_set_aot_program(Session, Program) != VS_INTEROP_STATUS_OK) return 4;
	vs_interop_aot_program_release(Program);
	vs_interop_config Config{};
	Config.struct_size = sizeof(Config);
	Config.execution_backend = VS_INTEROP_BACKEND_MIXED;
	Config.max_instructions = 10000;
	Config.enable_execution_control = 1;
	Config.capability_policy_enabled = 1;
	Config.allowed_script_capabilities = VS_INTEROP_SCRIPT_CAP_CONSOLE_OUTPUT;
	if (vs_interop_session_apply_config(Session, &Config) != VS_INTEROP_STATUS_OK) return 5;
	vs_interop_package* Package = nullptr;
	if (vs_interop_package_read({PackageBytes.data(), PackageBytes.size()}, &Package, &Error) != VS_INTEROP_STATUS_OK) return 6;
	vs_interop_run_result Result{};
	Result.struct_size = sizeof(Result);
	if (vs_interop_session_load_package(Session, Package, {"/application.vsapp", 18}, &Result) != VS_INTEROP_STATUS_OK || !Result.success) return 7;
	vs_interop_package_destroy(Package);
	if (vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) != VS_INTEROP_STATUS_OK || !Result.success)
	{
		std::uint64_t Required = 0;
		vs_interop_session_copy_last_error(Session, nullptr, 0, &Required);
		std::string Failure(Required, '\0');
		vs_interop_session_copy_last_error(Session, Failure.data(), Required, &Required);
		std::cerr << Failure << '\n';
		return 8;
	}
	vs_interop_jit_stats Stats{};
	Stats.struct_size = sizeof(Stats);
	if (vs_interop_session_get_last_jit_stats(Session, &Stats) != VS_INTEROP_STATUS_OK || Stats.aot_executions == 0 || Output != "28\n") return 9;
	Output.clear();
	Config.allowed_script_capabilities = 0;
	if (vs_interop_session_apply_config(Session, &Config) != VS_INTEROP_STATUS_OK) return 10;
	const auto RestrictedStatus = vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result);
	if (RestrictedStatus != VS_INTEROP_STATUS_ENGINE_ERROR || Result.success || !Output.empty())
	{
		std::cerr << "restricted AOT run: status=" << RestrictedStatus << " success=" << Result.success << " output=[" << Output << "]\n";
		return 11;
	}
	if (vs_interop_session_destroy(Session) != VS_INTEROP_STATUS_OK) return 12;
	#if defined(VEROSCRIPT_AOT_WITH_MOBILE)
	if (CreateVeroScriptAotProgramInterop(&Program, &Error) != VS_INTEROP_STATUS_OK) return 13;
	VeroScript::Mobile::FSessionOptions MobileOptions;
	MobileOptions.StdlibDirectory = "/static-aot/stdlib";
	MobileOptions.TimeoutMilliseconds = 5000;
	MobileOptions.ExecutionBackend = VS_INTEROP_BACKEND_MIXED;
	MobileOptions.AotProgramId = "application";
	MobileOptions.AotPrograms = std::make_shared<const VeroScript::Mobile::FAotProgramCatalog>(
	    std::vector<VeroScript::Mobile::FAotProgramRegistration>{{"application", VeroScript::Mobile::RetainAotProgram(Program)}});
	vs_interop_aot_program_release(Program);
	VeroScript::Mobile::FRunner Runner(std::move(MobileOptions));
	const auto MobileResult = Runner.ExecutePackage({PackageBytes.data(), PackageBytes.size()}, "/application.vsapp");
	if (!MobileResult.bSuccess || MobileResult.Output != "28\n")
	{
		std::cerr << "mobile static AOT failed: " << MobileResult.Error << '\n';
		return 14;
	}
	#endif
	#else
	using namespace VeroScript;
	std::string Error;
	auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 0, Error);
	if (!Loaded)
	{
		std::cerr << Error << '\n';
		return 2;
	}
	const auto Program = CreateVeroScriptAotProgram(Error);
	if (!Program)
	{
		std::cerr << Error << '\n';
		return 3;
	}
	std::ostringstream BaselineOut;
	VeroScriptVm Baseline(BaselineOut);
	BytecodeVm::Configure(Baseline);
	Baseline.SetExecutionBackend(EExecutionBackend::Vm);
	Baseline.SetExecutionControl(std::make_shared<FExecutionControl>());
	if (!Baseline.Execute(*Loaded, ERunMode::Script)) return 4;
	const auto Count = Baseline.GetExecutedInstructionCount();
	bool SawNative = false;
	for (std::uint64_t Limit = 1; Limit <= Count + 1; ++Limit)
	{
		std::ostringstream VmOut, AotOut;
		VeroScriptVm Vm(VmOut), Aot(AotOut);
		BytecodeVm::Configure(Vm);
		BytecodeVm::Configure(Aot);
		Vm.SetExecutionBackend(EExecutionBackend::Vm);
		Aot.SetExecutionBackend(EExecutionBackend::Mixed);
		Vm.SetExecutionLimits({Limit, 0});
		Aot.SetExecutionLimits({Limit, 0});
		if (!Aot.SetAotProgram(Program, Error)) return 5;
		const bool VmOk = Vm.Execute(*Loaded, ERunMode::Script), AotOk = Aot.Execute(*Loaded, ERunMode::Script);
		SawNative |= Aot.GetJitStats().AotExecutions != 0;
		if (VmOk != AotOk || VmOut.str() != AotOut.str() || Vm.GetExecutedInstructionCount() != Aot.GetExecutedInstructionCount())
		{
			std::cerr << "AOT quota mismatch at " << Limit << ": " << Aot.GetError().Message << '\n';
			return 6;
		}
		Aot.ResetJitStats();
		if (Aot.GetJitStats().AotExecutions != 0)
		{
			std::cerr << "AOT execution statistics survived ResetJitStats\n";
			return 9;
		}
	}
	if (!SawNative) return 7;
	// A native entry cannot claim a non-void return without producing its value.
	const auto Incomplete = CreateAotProgram(Program->GetDescription(),
	    {{static_cast<std::uint16_t>(Loaded->MainFunctionIndex), +[](void*)
	{
		return 2;
	},
	        ControlledExecutionCapabilities}},
	    Error);
	if (!Incomplete) return 10;
	std::ostringstream IncompleteOut;
	VeroScriptVm IncompleteVm(IncompleteOut);
	BytecodeVm::Configure(IncompleteVm);
	IncompleteVm.SetAotProgram(Incomplete, Error);
	IncompleteVm.SetExecutionBackend(EExecutionBackend::Mixed);
	if (IncompleteVm.Execute(*Loaded, ERunMode::Script) || !IncompleteVm.HasError() || !IncompleteOut.str().empty() || IncompleteVm.GetError().Message.find("invalid AOT return state") == std::string::npos || IncompleteVm.GetJitStats().AotExecutions != 1)
	{
		std::cerr << "AOT accepted a missing return value or replayed its entry\n";
		return 11;
	}
	// Same indices with changed bytecode must be rejected before output.
	auto Changed = *Loaded;
	for (auto& C : Changed.Constants)
	{
		if (C.Kind == EConstantKind::I32)
		{
			++C.I32;
			break;
		}
	}
	std::ostringstream BadOut;
	VeroScriptVm Bad(BadOut);
	BytecodeVm::Configure(Bad);
	Bad.SetAotProgram(Program, Error);
	Bad.SetExecutionBackend(EExecutionBackend::Mixed);
	if (Bad.Execute(Changed, ERunMode::Script) || !BadOut.str().empty() || Bad.GetError().Message.find("identity mismatch") == std::string::npos) return 8;
	#endif
	std::cout << "generated static AOT consumer passed\n";
	return 0;
}
