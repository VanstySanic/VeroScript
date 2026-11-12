// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompile.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptVmBackendLoader.h"
#include "VeroScriptVm.h"
#include "VeroScriptModuleMeta.h"
#include "../../../../../VeroScript/Private/Tests/VeroScriptTestJitCapabilities.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace VeroScript;
namespace
{
	void Require(bool Ok, const std::string &Message)
	{
		if (!Ok)
		{
			throw std::runtime_error(Message);
		}
	}
	std::uint16_t Index(const FBytecodeModule& Module, std::string_view Name, std::uint8_t Arity = 0)
	{
		std::uint16_t Result = 0;
		if (ModuleMeta::TryResolveFreeFunction(Module, Name, Arity, Result) == EMethodResolveStatus::Found) return Result;
		throw std::runtime_error("missing function " + std::string(Name));
	}
}
int main(int Argc, char** Argv)
{
	try
	{
		Require(Argc == 4, "expected VM, JIT and stdlib paths");
		std::string Error;
		auto VmFactory = LoadVmBackend(Argv[1], Error); Require(!!VmFactory, Error);
		auto JitFactory = LoadJitBackend(Argv[2], Error); Require(!!JitFactory, Error);
		const bool NativeCode = JitFactory->GetDescription().bNativeCode;
		FCompileOptions Options; Options.ScriptCompileExtensions.VmBackendFactory = VmFactory;
		Options.StdlibSearchPaths = {Argv[3]};
		auto Compiled = VeroScriptCompile::CompileSource(R"VS(
int answer(){return 7;}
int Sum(int n){int result=0;for(int i=0;i<n;i++)result+=i;return result;}
int Reference(ref int value){return value;}
int Protected(){try{return 7;}finally{int value=1;}}
void main(){print(answer());}
)VS", "jit-diagnostics.vs", Options);
		Require(!!Compiled.Module, Compiled.Error.Message);
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Require(Vm.SetVmBackendFactory(VmFactory, Error), Error);
		Require(Vm.SetJitBackendFactory(JitFactory, Error), Error);
		Vm.SetExecutionBackend(EExecutionBackend::Jit);
		FJitPolicy Policy; Policy.Diagnostics = {32, 32, true}; Vm.SetJitPolicy(Policy);
		FValue Result;
		Require(Vm.ExecuteFunction(*Compiled.Module, Index(*Compiled.Module, "answer"), {}, nullptr, &Result), Vm.GetError().Message);
		Require(std::get<std::int32_t>(Result) == 7, "native result");
		const auto Stats = Vm.GetJitStats();
		Require(NativeCode ? Stats.NativeLeafExecutions + Stats.NativeCfgExecutions == 1
			: Stats.NativeLeafExecutions + Stats.NativeCfgExecutions == 0, "actual ordinary native entry count matches backend capability");
		Require(Stats.ScalarNativeFastPathCalls == 0, "script native entry is not a host scalar-native call");
		const auto Diagnostics = Vm.GetJitDiagnostics();
		Require(Diagnostics.Functions.size() == 1 && Diagnostics.Functions[0].Calls == 1 && Diagnostics.Functions[0].Returned == 1,
			"one logical entry without double-counting its runtime frame");
		Require(Diagnostics.Functions[0].InclusiveNanoseconds >= Diagnostics.Functions[0].SelfNanoseconds, "inclusive/self time");
		Require(Diagnostics.Functions[0].PlannedKind != EJitExecutionKind::Unknown, "successful plans are queryable without a fallback event");
		if (NativeCode)
		{
			Require(Diagnostics.Functions[0].PlanReason.empty(), "native snapshot has no obsolete fallback text");
		}
		Vm.ResetJitStats();
		Require(Vm.ExecuteFunction(*Compiled.Module, Index(*Compiled.Module, "Sum", 1), {std::int32_t{16}}, nullptr, &Result), Vm.GetError().Message);
		Require(std::get<std::int32_t>(Result) == 120, "CFG result");
		Require(Vm.GetJitStats().NativeCfgExecutions ==
		    (Test::SupportsAsmJitControlFlow(JitFactory->GetDescription()) ? 1u : 0u),
		    "actual CFG entry count matches backend capability");
		std::shared_ptr<const FBytecodeModule> Owner = std::move(Compiled.Module);
		FJitProgramView View{Owner.get(), Owner, {91, 1, 1}};
		auto Cache = FJitBackendCache::Create(JitFactory, View, {}, {}, JitFactory->GetDescription().RequiredServices, Error);
		Require(!!Cache, Error);
		Require(Cache->EnsureCompiled(Index(*Owner, "answer")) == EJitCompilationResult::Ready
			&& (!NativeCode || Cache->GetFunctionFallbackReason(Index(*Owner, "answer")) == EJitFallbackReason::None), "native plan has no fallback reason");
		for (const auto Case : {std::pair{"Reference", EJitFallbackReason::ReferenceParameter}, std::pair{"Protected", EJitFallbackReason::ExceptionHandling}})
		{
			const auto Function = Index(*Owner, Case.first, Case.second == EJitFallbackReason::ReferenceParameter ? 1 : 0);
			Require(Cache->EnsureCompiled(Function) == EJitCompilationResult::Ready, "fallback plan is ready");
			Require(Cache->GetFunctionFallbackReason(Function) == Case.second && !Cache->GetFunctionReason(Function).empty(), "ready VM plan retains its reason");
		}
		const char* Source = R"VS(
import "attribute.vs";
int depth=0;int reads=0;
int Read(){try{depth++;reads++;return reads;}finally{depth--;}}
void main(){std::attribute<int> value=std::attribute<int>();value.bind(Read);
int total=0;int output=0;for(int i=0;i<32;i++){if(value.try_get(ref output))total+=output;}
assert(total==528&&reads==32&&depth==0);}
)VS";
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			auto Program = VeroScriptCompile::CompileSource(Source, "attribute-diagnostics.vs", Options);
			Require(!!Program.Module, Program.Error.Message);
			Vm.SetExecutionBackend(Mode); Vm.SetJitPolicy(Policy);
			Require(Vm.Execute(*Program.Module, ERunMode::Script), Vm.GetError().Message);
			const auto Snapshot = Vm.GetJitDiagnostics();
			Require(!Snapshot.Functions.empty(), "VM and JIT entries remain queryable");
			bool Found = false;
			for (const auto& Item : Snapshot.Functions)
			{
				Require(Item.Calls == Item.Returned + Item.ControlTransfers + Item.Failures, "outcome partition");
				if (Item.FunctionIndex == Index(*Program.Module, "Read"))
				{
					Found = true; Require(Item.Calls == 32, "getter evaluation count");
					if (Mode != EExecutionBackend::Vm)
					{
						Require(Item.Fallbacks[static_cast<std::size_t>(EJitFallbackReason::ExceptionHandling)] == 32, "finally fallback attribution");
					}
				}
			}
			Require(Found, "getter trace missing");
			Policy.Diagnostics = {1, 16, false}; Vm.SetJitPolicy(Policy);
			Require(Vm.Execute(*Program.Module, ERunMode::Script), Vm.GetError().Message);
			const auto Bounded = Vm.GetJitDiagnostics();
			Require(Bounded.Functions.size() <= 1 && Bounded.DroppedRecords > 0, "bounded trace");
			for (const auto &Item : Bounded.Functions)
			{
				Require(Item.InclusiveNanoseconds == 0, "timing disabled");
			}
			Policy.Diagnostics = {}; Vm.SetJitPolicy(Policy); Vm.ResetJitStats();
			Require(Vm.Execute(*Program.Module, ERunMode::Script) && Vm.GetJitDiagnostics().Functions.empty(), "disabled trace");
			Policy.Diagnostics = {32, 32, true};
		}
		auto Captured = VeroScriptCompile::CompileSource(R"VS(
void main(){int state=0;fn()->int first=[&state]()->int{state+=2;return state;};
fn()->int second=[&state]()->int{state+=3;return state;};
assert(first()==2);assert(second()==5);assert(first()==7);assert(state==7);}
)VS", "reference-scalar-diagnostics.vs", Options);
		Require(!!Captured.Module, Captured.Error.Message);
		Vm.SetExecutionBackend(EExecutionBackend::Vm); Vm.SetJitPolicy(Policy);
		Require(Vm.Execute(*Captured.Module, ERunMode::Script), Vm.GetError().Message);
		std::size_t ScalarCaptures = 0;
		for (const auto &Item : Vm.GetJitDiagnostics().Functions)
		{
			if (!Captured.Module->Functions[Item.FunctionIndex].Captures.empty())
			{
				ScalarCaptures += Item.Executions[static_cast<std::size_t>(EJitExecutionKind::VmScalar)];
			}
		}
		Require(ScalarCaptures == 3, "shared reference getters use the bounded scalar plan");
		auto Interrupted = VeroScriptCompile::CompileSource(
			"void main(){try{while(true){}}finally{print(\"cleanup\");}}", "diagnostic-cleanup.vs", Options);
		Require(!!Interrupted.Module, Interrupted.Error.Message);
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			Vm.SetExecutionBackend(Mode);
			std::uint64_t ExpectedCount = 0;
			std::string ExpectedError;
			for (const bool Trace : {false, true})
			{
				Policy.Diagnostics = Trace ? FJitDiagnosticOptions{64, 128, true} : FJitDiagnosticOptions{};
				Vm.SetJitPolicy(Policy);
				FExecutionLimits Limits; Limits.MaxInstructions = 20; Vm.SetExecutionLimits(Limits);
				Output.str({}); Output.clear();
				Require(!Vm.Execute(*Interrupted.Module, ERunMode::Script) && !Vm.RequiresReset()
					&& Output.str() == "cleanup\n", "diagnostics preserve bounded interruption cleanup");
				if (!Trace)
				{
					ExpectedCount = Vm.GetExecutedInstructionCount();
					ExpectedError = Vm.GetError().Message;
				}
				else
				{
					Require(Vm.GetExecutedInstructionCount() == ExpectedCount && Vm.GetError().Message == ExpectedError,
						"diagnostics preserve the original limit and error");
					std::size_t Failures = 0;
					for (const auto &Item : Vm.GetJitDiagnostics().Functions)
					{
						Failures += Item.Failures;
					}
					Require(Failures > 0, "interrupted entries report failure");
				}
			}
		}
		Vm.SetExecutionLimits({});
		auto Caught = VeroScriptCompile::CompileSource(
			"void Thrower(){throw 7;}void main(){try{Thrower();}catch(int value){assert(value==7);}}", "diagnostic-transfer.vs", Options);
		Require(!!Caught.Module, Caught.Error.Message);
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			Vm.SetExecutionBackend(Mode);
			Require(Vm.Execute(*Caught.Module, ERunMode::Script), Vm.GetError().Message);
			bool FoundTransfer = false;
			for (const auto &Item : Vm.GetJitDiagnostics().Functions)
			{
				if (Item.FunctionIndex == Index(*Caught.Module, "Thrower"))
				{
					FoundTransfer = Item.Calls == 1 && Item.ControlTransfers == 1 && Item.Returned == 0;
				}
			}
			Require(FoundTransfer, "caught throw preserves the callee control-transfer outcome");
		}
		auto Deep = VeroScriptCompile::CompileSource(
			"int Fail(int n){if(n==0){throw 9;}return Fail(n-1);}void main(){try{Fail(80);}catch(int value){assert(value==9);}}",
			"diagnostic-deep-transfer.vs", Options);
		Require(!!Deep.Module, Deep.Error.Message);
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			Vm.SetExecutionBackend(Mode);
			for (const std::size_t Depth : {std::size_t{128}, std::size_t{8}})
			{
				Policy.Diagnostics = {64, Depth, true}; Vm.SetJitPolicy(Policy);
				Require(Vm.Execute(*Deep.Module, ERunMode::Script), Vm.GetError().Message);
				const auto Snapshot = Vm.GetJitDiagnostics();
				bool Found = false;
				for (const auto &Item : Snapshot.Functions)
				{
					if (Item.FunctionIndex == Index(*Deep.Module, "Fail", 1))
					{
						Found = Item.ControlTransfers == (Depth == 128 ? 81 : 7) && Item.Calls == Item.ControlTransfers;
					}
				}
				Require(Found && (Depth == 128 ? Snapshot.DroppedRecords == 0 : Snapshot.DroppedRecords > 0),
					"deep unwinding and capacity drops preserve recorded outcomes");
			}
		}
		std::cout << "JIT diagnostics and attribute execution passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
