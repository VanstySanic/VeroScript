// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Assemble the independently distributed Core and Bytecode VM SDKs.
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptCompile.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPath.h"
#include "VeroScriptVm.h"

#include <iostream>
#include <memory>
#include <sstream>

#if defined(VEROSCRIPT_HAS_ASMJIT) || defined(VEROSCRIPT_ASMJIT_X64) || defined(VEROSCRIPT_ENABLE_JIT_TEST_HOOKS)
#error "Core SDK targets must not export concrete backend macros"
#endif


int RunCoreConsumer(int ArgumentCount, const std::string& BackendPath)
{
	VeroScript::FHostBindingRegistry Bindings;
	std::string Error;
	for (const auto Parameter : {VeroScript::ENativeType::Int, VeroScript::ENativeType::String})
	{
		if (!Bindings.RegisterNative("host_value", VeroScript::ENativeType::Int, {Parameter},
			[](const VeroScript::FNativeCallContext& Context, VeroScript::FValue& Result, std::string&)
			{
				Result = std::holds_alternative<std::int32_t>(Context.Args[0])
					? std::get<std::int32_t>(Context.Args[0]) : std::int32_t{42};
				return true;
			}, nullptr, Error))
		{
			std::cerr << Error << '\n';
			return 1;
		}
	}
	VeroScript::FCompileOptions Options;
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Options.Bindings = &Bindings;
	const auto Compiled = VeroScript::VeroScriptCompile::CompileSource(
		"int answer(){return 6*7;} int main() { assert(host_value(42)==42 && host_value(\"answer\")==42); print(answer()); return 0; }", "core-sdk-example.vs", Options);
	if (!Compiled.bSuccess || !Compiled.Module)
	{
		std::cerr << Compiled.Error.Message << '\n';
		return 1;
	}
	std::ostringstream Output;
	auto Vm = std::make_unique<VeroScript::VeroScriptVm>(Output);
	VeroScript::BytecodeVm::Configure(*Vm);
	std::string BackendError;
	if (ArgumentCount > 1 && !Vm->SetJitBackendPath(BackendPath, BackendError))
	{
		std::cerr << BackendError << '\n';
		return 1;
	}
	Vm->SetExecutionBackend(ArgumentCount > 1 ? VeroScript::EExecutionBackend::Jit : VeroScript::EExecutionBackend::Mixed);
	if (!Vm->Execute(*Compiled.Module, VeroScript::ERunMode::Script, {}, &Bindings.GetNativeFunctions()))
	{
		std::cerr << Vm->GetError().Message << '\n';
		return 1;
	}
	if (Output.str() != "42\n")
	{
		std::cerr << "Unexpected Core SDK result: " << Output.str();
		return 1;
	}
	if (ArgumentCount > 1 && Vm->GetJitStats().Dispatches == 0) return 1;
	if (ArgumentCount == 1 && Vm->GetEffectiveExecutionBackend() != VeroScript::EExecutionBackend::Vm) return 1;
	std::cout << "Core SDK compile/VM consumer passed\n";
	return 0;
}

#if defined(_WIN32)
int wmain(int ArgumentCount, wchar_t** Arguments)
{
	const std::string BackendPath = ArgumentCount > 1
		? VeroScript::PathUtil::ToUtf8(std::filesystem::path(Arguments[1])) : std::string{};
	return RunCoreConsumer(ArgumentCount, BackendPath);
}
#else
int main(int ArgumentCount, char** Arguments)
{
	return RunCoreConsumer(ArgumentCount, ArgumentCount > 1 ? Arguments[1] : "");
}
#endif
