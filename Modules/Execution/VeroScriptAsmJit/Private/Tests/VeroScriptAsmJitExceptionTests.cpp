// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJit.h"
#include "VeroScriptAsmJitRuntime.h"
#include <iostream>

static bool CheckExceptionPlan(const VeroScript::FBytecodeModule& Module, const std::string_view Kind)
{
	using namespace VeroScript::AsmJit;
	FJitCodeCache Cache;
	bool Found = false;
	for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
	{
		const auto& Function = Module.Functions[Index];
		bool Relevant = Function.Name.find("work#") != std::string::npos;
		if (Kind == "safe")
		{
			Relevant = !Function.ExceptionRegions.empty();
			for (std::size_t Ip = 0; !Relevant && Ip < Function.Code.size();)
			{
				const auto Opcode = static_cast<VeroScript::EOpcode>(Function.Code[Ip]);
				Relevant = Opcode == VeroScript::EOpcode::Throw || Opcode == VeroScript::EOpcode::EndFinally;
				Ip += VeroScript::Bytecode::InstructionSize(Opcode);
			}
		}
		if (!Relevant)
		{
			continue;
		}
		Found = true;
		if (Cache.EnsureCompiled(Module, static_cast<std::uint16_t>(Index)) != EJitCompileStatus::Ok) return false;
		const auto* Entry = Cache.GetCandidate(static_cast<std::uint16_t>(Index));
		if (!Entry || (Kind == "loop" && Entry->Kind != EJitEntryKind::LinearTrampoline)
			|| (Kind == "general" && Entry->Kind != EJitEntryKind::VmTrampoline)
			|| (Kind == "safe" && Entry->Kind != EJitEntryKind::VmTrampoline && Entry->Kind != EJitEntryKind::LinearTrampoline))
		{
			std::cerr << "Exception backend plan mismatch: " << Function.Name << '\n';
			return false;
		}
	}
	return Found;
}

#define VEROSCRIPT_EXCEPTION_PLAN_CHECK(Module, Kind) CheckExceptionPlan(Module, Kind)
#define VEROSCRIPT_EXCEPTION_BACKEND_SETUP(Vm) \
	do \
	{ \
		std::string BackendError; \
		if (!(Vm).SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory(), BackendError)) \
		{ \
			std::abort(); \
		} \
	} \
	while (false)
#include "../../../../../VeroScript/Private/Tests/VeroScriptExceptionTests.cpp"
