// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptAsmJit.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "../VeroScriptAsmJitControlled.h"
#include "../VeroScriptAsmJitCapabilities.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <utility>

using namespace VeroScript;

namespace
{
	std::shared_ptr<const IJitBackendFactory> Factory;
	bool HasControlledNative()
	{
		// x86 advertises scalar native leaves, but has no controlled CFG emitter.
		return Factory->GetDescription().bNativeCode &&
		    AsmJit::JitBackend::GetHostCapabilities().bScalarCfg;
	}
	bool Expect(bool Value, const char* Message)
	{
		if (!Value)
		{
			std::cerr << Message << '\n';
		}
		return Value;
	}
	struct FResult
	{
		bool Ok;
		std::string Output, Error;
		std::uint64_t Count;
		FJitStats Stats;
		std::uint32_t Line = 0, Column = 0;
		int ExitCode = 0;
	};
	enum class EProfile { Token, Quota, Permissions, Observer, Deadline, ExternalDeadline, ExternalCheck };
	FResult Run(const std::shared_ptr<const FBytecodeModule>& Module, EExecutionBackend Mode,
	    std::uint64_t Limit, bool AllowOutput = true, bool Cancelled = false, EProfile Profile = EProfile::Token)
	{
		std::ostringstream Out, Error;
		VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
		std::string SetupError;
		Vm.SetExecutionBackend(Mode);
		Vm.SetExecutionLimits({Limit, Profile == EProfile::Deadline ? 30000u : 0u});
		if (Profile == EProfile::Token || Cancelled)
		{
			auto Control = std::make_shared<FExecutionControl>();
			if (Cancelled)
			{
				Control->RequestCancel();
			}
			Vm.SetExecutionControl(std::move(Control));
		}
		if (Profile == EProfile::ExternalDeadline)
		{
			Vm.SetExecutionDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(30));
		}
		if (Profile == EProfile::ExternalCheck)
		{
			Vm.SetExecutionCheck([]()
				{
					return true;
				});
		}
		if (Profile == EProfile::Observer)
		{
			Vm.SetMutationCallback([](const FMutationEvent &)
				{
				});
		}
		FScriptCapabilities Caps;
		Caps.bAllowConsoleOutput = AllowOutput;
		if (Profile == EProfile::Permissions)
		{
			Caps.bAllowFileWrite = false;
		}
		Vm.SetCapabilities(Caps);
		if (Mode != EExecutionBackend::Vm)
		{
			Vm.SetJitBackendFactory(Factory, SetupError);
			Vm.SetJitProgramOwner(Module, SetupError);
		}
		const bool Ok = Vm.Execute(*Module, ERunMode::Script);
		return {Ok, Out.str(), Vm.HasError() ? Vm.GetError().Message : std::string{},
		    Vm.GetExecutedInstructionCount(), Vm.GetJitStats(),
		    Vm.HasError() ? Vm.GetError().Location.Line : 0, Vm.HasError() ? Vm.GetError().Location.Column : 0,
		    Vm.GetProcessExitCode()};
	}
	bool CompareSource(const char* Source, const char* ExpectedOutput = nullptr, const bool ExpectFailure = false,
	    const bool RequireNative = false)
	{
		auto C = VeroScriptCompile::CompileSource(Source, "controlled-boundary.vs");
		if (!Expect(C.bSuccess, C.Error.Message.c_str())) return false;
		std::shared_ptr<const FBytecodeModule> Module = std::move(C.Module);
		const auto Base = Run(Module, EExecutionBackend::Vm, 0);
		if (ExpectedOutput && !Expect(Base.Ok && Base.Output == ExpectedOutput, "controlled fixture output"))
		{
			std::cerr << "source: " << Source << "\nexpected: [" << ExpectedOutput << "] actual: [" << Base.Output
			          << "] error: " << Base.Error << '\n';
			return false;
		}
		if (ExpectFailure && !Expect(!Base.Ok, "controlled fixture must fail")) return false;
		bool SawNative = false;
		for (std::uint64_t Limit = 0; Limit <= Base.Count + 1; ++Limit)
		{
			const auto Vm = Run(Module, EExecutionBackend::Vm, Limit);
			const auto Native = Run(Module, EExecutionBackend::Jit, Limit);
			SawNative |= Native.Stats.ControlledNativeExecutions != 0;
			if (Vm.Ok != Native.Ok || Vm.Output != Native.Output || Vm.Count != Native.Count || Vm.Error != Native.Error ||
			    Vm.Line != Native.Line || Vm.Column != Native.Column)
			{
				std::cerr << "boundary quota " << Limit << ": VM=" << Vm.Ok << ',' << Vm.Count << ',' << Vm.Error
				          << " native=" << Native.Ok << ',' << Native.Count << ',' << Native.Error << '\n'
				          << "VM output: [" << Vm.Output << "], native output: [" << Native.Output << "]\n";
				return false;
			}
		}
		return !RequireNative || !HasControlledNative() ||
		    Expect(SawNative, "controlled arithmetic fixture never entered native code");
	}

	bool CheckLocalStaticVmPreference()
	{
		struct FCase
		{
			const char* Source;
			const char* Expected;
			bool PreferVm;
		};
		for (const auto& Case : {
		         FCase{"int next(int seed){static int n=seed;return ++n;}int last=0;"
		               "for(int i=0;i<4;++i)last=next(3);print(last);",
		             "7\n", true},
		         FCase{"int next(int seed){static int n=seed;return ++n;}int main(){int last=0;"
		               "for(int i=0;i<4;++i)last=next(3);print(last);return 0;}",
		             "7\n", true},
		         FCase{"int main(){static int n=3;print(++n);return 0;}", "4\n", false}})
		{
			auto Compilation = VeroScriptCompile::CompileSource(Case.Source, "static-native-selection.vs");
			if (!Expect(Compilation.bSuccess, Compilation.Error.Message.c_str())) return false;
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compilation.Module);
			for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Jit})
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(Mode);
				std::string Error;
				if (Mode != EExecutionBackend::Vm && !Expect(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error.c_str()))
				{
					return false;
				}
				if (!Expect(Vm.Execute(*Module, ERunMode::Script), Vm.GetError().Message.c_str()) || !Expect(Output.str() == Case.Expected, "local static selection changed the result"))
				{
					return false;
				}
				if (Mode == EExecutionBackend::Jit && HasControlledNative())
				{
					const auto Stats = Vm.GetJitStats();
					if (Case.PreferVm)
					{
						if (!Expect(Stats.CostModelVmFallbacks > 0 && Stats.ControlledNativeExecutions == 0,
						        "short static calls must avoid repeated native/Core crossings in a VM loop"))
						{
							return false;
						}
					}
					else if (!Expect(Stats.ControlledNativeExecutions > 0, "root local statics must retain native support"))
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	bool CheckPollingSource(const char* Source)
	{
		auto Compilation = VeroScriptCompile::CompileSource(Source, "controlled-polling.vs");
		if (!Expect(Compilation.bSuccess, Compilation.Error.Message.c_str())) return false;
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compilation.Module);
		const auto Base = Run(Module, EExecutionBackend::Vm, 0);
		if (!Expect(Base.Ok, Base.Error.c_str())) return false;
		for (std::uint64_t Stop = 1; Stop <= Base.Count + 1; ++Stop)
		{
			FResult Reference{};
			for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Jit})
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(Mode);
				Vm.SetExecutionLimits({Base.Count + 1, 0});
				Vm.SetExecutionCheck([&]()
				{
					return Vm.GetExecutedInstructionCount() < Stop;
				});
				std::string Error;
				if (!Expect(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error),
				        Error.c_str()))
				{
					return false;
				}
				const bool Ok = Vm.Execute(*Module, ERunMode::Script);
				FResult Result{Ok, Output.str(), Vm.HasError() ? Vm.GetError().Message : std::string{},
				    Vm.GetExecutedInstructionCount(), Vm.GetJitStats(),
				    Vm.HasError() ? Vm.GetError().Location.Line : 0, Vm.HasError() ? Vm.GetError().Location.Column : 0};
				if (Mode == EExecutionBackend::Vm)
				{
					Reference = std::move(Result);
				}
				else if (!Expect(Result.Ok == Reference.Ok && Result.Output == Reference.Output &&
				                 Result.Error == Reference.Error && Result.Count == Reference.Count &&
				                 Result.Line == Reference.Line && Result.Column == Reference.Column,
				             "canonical polling boundary differs across backends"))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool CheckPollingBoundaries()
	{
		return CheckPollingSource(
		           "int main(){int sum=0;for(int i=0;i<4;i++){sum+=i;print(sum);}return sum;}") &&
		    CheckPollingSource(
		        "int main(){int sum=1;sum+=2;print(sum);sum*=3;print(sum);sum-=4;print(sum);return sum;}");
	}

	bool CheckControlledLoopSelection()
	{
		const char* Source =
		    "int step(int value,int index){return(value*3+index)%97;}"
		    "int main(){int value=7;for(int i=0;i<6;i++){value=step(value,i);print(value);}return value;}";
		auto Compilation = VeroScriptCompile::CompileSource(Source, "controlled-loop-selection.vs");
		if (!Expect(Compilation.bSuccess, Compilation.Error.Message.c_str())) return false;
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compilation.Module);
		const auto Reference = Run(Module, EExecutionBackend::Vm, 0);
		const auto Selected = Run(Module, EExecutionBackend::Jit, 0);
		if (!Expect(Reference.Ok && Selected.Ok && Reference.Output == Selected.Output && Reference.Count == Selected.Count, "controlled cost selection changed execution"))
		{
			return false;
		}
		if (HasControlledNative() &&
		    !Expect(Selected.Stats.CostModelVmFallbacks > 0 && Selected.Stats.ControlledNativeExecutions == 0,
		        "controlled loop must avoid opcode-service bridges, including scalar callees"))
		{
			return false;
		}
		return CompareSource(Source);
	}

	bool CheckNonPollingScalarLoopSelection()
	{
		auto Compilation = VeroScriptCompile::CompileSource(
		    "int main(){int sum=7;for(int i=0;i<5;i++){sum=(sum*3+i)%97;}return sum;}",
		    "controlled-scalar-loop-cost.vs");
		if (!Expect(Compilation.bSuccess, Compilation.Error.Message.c_str())) return false;
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compilation.Module);
		const auto Base = Run(Module, EExecutionBackend::Vm, 1000000, true, false, EProfile::Quota);
		if (!Expect(Base.Ok && Base.ExitCode == 13, "scalar loop cost fixture")) return false;
		for (const auto Profile : {EProfile::Quota, EProfile::Permissions, EProfile::Observer})
		{
			for (std::uint64_t Limit = 1; Limit <= Base.Count + 1; ++Limit)
			{
				const auto Reference = Run(Module, EExecutionBackend::Vm, Limit, true, false, Profile);
				const auto Selected = Run(Module, EExecutionBackend::Jit, Limit, true, false, Profile);
				if (!Expect(Reference.Ok == Selected.Ok && Reference.Output == Selected.Output &&
								Reference.Error == Selected.Error && Reference.Count == Selected.Count &&
								Reference.Line == Selected.Line && Reference.Column == Selected.Column &&
								Reference.ExitCode == Selected.ExitCode,
						"non-polling scalar loop changed a quota prefix"))
				{
					return false;
				}
				if (HasControlledNative() && Limit == Base.Count + 1 &&
					!Expect(Selected.Stats.CostModelVmFallbacks > 0 && Selected.Stats.ControlledNativeExecutions == 0,
						"non-polling scalar loop must select the VM's exact arithmetic batching"))
				{
					return false;
				}
			}
		}
		for (const auto Profile : {EProfile::Permissions, EProfile::Observer, EProfile::Token,
		         EProfile::Deadline, EProfile::ExternalDeadline, EProfile::ExternalCheck})
		{
			const auto Reference = Run(Module, EExecutionBackend::Vm, 0, true, false, Profile);
			const auto Selected = Run(Module, EExecutionBackend::Jit, 0, true, false, Profile);
			if (!Expect(Reference.Ok && Selected.Ok && Reference.Count == Selected.Count && Selected.ExitCode == 13,
					"scalar loop governance profile changed its result or count"))
			{
				return false;
			}
			if (!HasControlledNative())
			{
				continue;
			}
			const bool CostVm = Profile == EProfile::Permissions || Profile == EProfile::Observer;
			if (!Expect((Selected.Stats.CostModelVmFallbacks != 0) == CostVm,
					"polling obligations must remain distinct from quota/permission cost selection"))
			{
				return false;
			}
			if (Profile != EProfile::ExternalCheck &&
				!Expect((Selected.Stats.ControlledNativeExecutions != 0) != CostVm,
					"token and deadline scalar native support must remain available"))
			{
				return false;
			}
		}
		if (!HasControlledNative()) return true;
		// Reuse one immutable program, changing only its polling obligation.
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Test::Configure(Vm);
		Vm.SetExecutionBackend(EExecutionBackend::Jit);
		Vm.SetExecutionLimits({1000000, 0});
		std::string Error;
		if (!Vm.SetJitBackendFactory(Factory, Error) || !Vm.SetJitProgramOwner(Module, Error)) return false;
		for (const bool Polling : {false, true, false})
		{
			Vm.SetExecutionControl(Polling ? std::make_shared<FExecutionControl>() : nullptr);
			Vm.ResetJitStats();
			if (!Vm.Execute(*Module, ERunMode::Script) || Vm.GetProcessExitCode() != 13) return false;
			const auto Stats = Vm.GetJitStats();
			if (!Expect((Stats.ControlledNativeExecutions != 0) == Polling,
					"polling mask changes must not reuse an obsolete VM/native cost artifact"))
			{
				return false;
			}
		}
		return true;
	}

	bool CheckUnsupportedOwnedLoopSelection()
	{
		const char* Source =
		    "class Base{public:int value=0;virtual int read(){return value;}};"
		    "class Item:Base{public:Item(int n){value=n;}int read()override{return value+1;}};"
		    "int main(){array<Base> values=array<Base>();for(int i=0;i<4;i++){values.push(Item(i));}"
		    "int sum=0;for(int i=0;i<4;i++){sum+=values[i].read();}return sum;}";
		auto Compilation = VeroScriptCompile::CompileSource(Source, "controlled-owned-object-loop.vs");
		if (!Expect(Compilation.bSuccess, Compilation.Error.Message.c_str())) return false;
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compilation.Module);
		const auto Base = Run(Module, EExecutionBackend::Vm, 0);
		if (!Expect(Base.Ok && Base.ExitCode == 10, "owned object loop fixture")) return false;
		for (std::uint64_t Limit = 1; Limit <= Base.Count + 1; ++Limit)
		{
			const auto Reference = Run(Module, EExecutionBackend::Vm, Limit);
			const auto Selected = Run(Module, EExecutionBackend::Jit, Limit);
			if (!Expect(Reference.Ok == Selected.Ok && Reference.Output == Selected.Output &&
							Reference.Error == Selected.Error && Reference.Count == Selected.Count &&
							Reference.Line == Selected.Line && Reference.Column == Selected.Column &&
							Reference.ExitCode == Selected.ExitCode,
					"owned loop suppression changed a quota prefix"))
			{
				return false;
			}
		}
		const auto Selected = Run(Module, EExecutionBackend::Jit, Base.Count + 1);
		if (HasControlledNative() &&
		    !Expect(Selected.Stats.CostModelVmFallbacks > 0 && Selected.Stats.ControlledNativeExecutions == 0,
		        "unsupported owned loop must not repeatedly enter controlled native constructors/methods"))
		{
			return false;
		}
		return CheckPollingSource(Source) && CompareSource(
		    "class Item{public:~Item(){print(77);}};int main(){try{for(int i=0;i<3;i++){Item item=Item();}}"
		    "finally{print(99);}return 0;}", "77\n77\n99\n77\n");
	}

	bool CheckIndependentControlledKernel()
	{
		const std::string Kernel =
		    "int kernel(int n,int seed){int s=seed,total=0;for(int j=0;j<n;++j){"
		    "s=(s*3+(s^(j+(s^(j+(s^(j+(s^(j+(s^(j+(s^(j+s)))))))))))))%65521;total+=s&255;}return total+s;}";
		for (const int Variant : {0, 1, 2})
		{
			const auto Source = Variant == 1
			    ? "class Box{public:int value=1;" + Kernel + "};int main(){Box box=Box();int sum=0;"
			      "for(int i=0;i<3;++i){sum+=box.kernel(5,i+box.value);}return sum;}"
			    : Variant == 2
			    ? Kernel + "struct Item{int value=0;Item(){value=kernel(5,1);}};int main(){int sum=0;"
			      "for(int i=0;i<3;++i){Item items[1];sum+=items[0].value;}return sum;}"
			    : Kernel + "class Box{public:int value=1;};int main(){Box box=Box();int sum=0;"
			      "for(int i=0;i<3;++i){sum+=kernel(5,i+box.value);}return sum;}";
			auto Compilation = VeroScriptCompile::CompileSource(Source, "controlled-independent-kernel.vs");
			if (!Expect(Compilation.bSuccess, Compilation.Error.Message.c_str())) return false;
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compilation.Module);
			const auto Base = Run(Module, EExecutionBackend::Vm, 0);
			if (!Expect(Base.Ok, Base.Error.c_str())) return false;
			const auto Selected = Run(Module, EExecutionBackend::Jit, 0);
			if (!Expect(Selected.Ok && Selected.ExitCode == Base.ExitCode && Selected.Count == Base.Count,
					"independent controlled kernel changed its value or accounting"))
			{
				return false;
			}
			if (HasControlledNative())
			{
				if (!Expect(Selected.Stats.CostModelVmFallbacks > 0 && Selected.Stats.ControlledNativeExecutions >= 3,
						"a VM-preferred object parent must keep heavy scalar callees independently native"))
				{
					return false;
				}
				AsmJit::FControlledCodeCache Cache;
				FJitCompileRequest Request;
				Request.Program = {Module.get(), Module, {1, 1, 1}};
				Request.FunctionIndex = static_cast<std::uint16_t>(Module->MainFunctionIndex);
				Request.RequiredGovernance = ControlledExecutionCapabilities;
				Request.AvailableServices = CompiledInstructionServiceCapability;
				FJitCompileResponse Response;
				if (!Expect(Cache.Compile(Request, Response) == EJitCompilationResult::Ready && Response.Function &&
								Response.Function->bPreferVmForExecution && !Response.Function->bSuppressNestedBackendOnFallback,
						"unknown or heavy callee closures must not publish whole-tree suppression"))
				{
					return false;
				}
			}
			for (std::uint64_t Limit = 1; Limit <= Base.Count + 1; ++Limit)
			{
				const auto Reference = Run(Module, EExecutionBackend::Vm, Limit);
				const auto Native = Run(Module, EExecutionBackend::Jit, Limit);
				if (!Expect(Reference.Ok == Native.Ok && Reference.Count == Native.Count &&
								Reference.Error == Native.Error && Reference.Line == Native.Line && Reference.Column == Native.Column &&
								Reference.ExitCode == Native.ExitCode,
						"independent controlled kernel changed a quota prefix"))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool CheckOwnedLoopStructuralAdmission()
	{
		if (!HasControlledNative()) return true;
		auto Compilation = VeroScriptCompile::CompileSource(
		    "int main(){static int seed=2;array<int> values=array<int>();"
		    "for(int i=0;i<3;i++){values.push(i+seed);}return len(values);}", "owned-loop-structure.vs");
		if (!Expect(Compilation.bSuccess && Compilation.Module->MainFunctionIndex >= 0, Compilation.Error.Message.c_str())) return false;
		const auto Main = static_cast<std::uint16_t>(Compilation.Module->MainFunctionIndex);
		AsmJit::FControlledCodeCache Cache;
		for (int Variant = 0; Variant != 10; ++Variant)
		{
			auto Candidate = std::make_shared<FBytecodeModule>(*Compilation.Module);
			auto& Code = Candidate->Functions[Main].Code;
			if (Variant == 1)
			{
				Code = {static_cast<std::uint8_t>(EOpcode::NewDynamicArray), 0};
			}
			if (Variant == 2)
			{
				Code.back() = 0xFF;
			}
			if (Variant == 7)
			{
				Code.insert(Code.begin(), 65536, static_cast<std::uint8_t>(EOpcode::Nop));
			}
			if (Variant == 3 || Variant == 4 || Variant == 5)
			{
				bool Changed = false;
				for (std::size_t Offset = 0; Offset < Code.size();)
				{
					const auto Opcode = static_cast<EOpcode>(Code[Offset]);
					const auto Size = Bytecode::InstructionSize(Opcode);
					if (Opcode == EOpcode::Jmp || Opcode == EOpcode::JmpIfFalse ||
					    Opcode == EOpcode::JmpIfLocalGeImm || Opcode == EOpcode::BeginLocalStatic)
					{
						const auto Target = static_cast<std::int64_t>(Offset + Size) + Bytecode::ReadI32LE(Code.data() + Offset + Size - 4);
						if (Variant == 5 && Target <= static_cast<std::int64_t>(Offset))
						{
							std::fill(Code.begin() + Offset, Code.begin() + Offset + Size, static_cast<std::uint8_t>(EOpcode::Nop));
							Changed = true;
						}
						else if (Variant != 5 && !Changed)
						{
							const auto BadTarget = Variant == 3 ? Offset + 1 : Code.size() + 1;
							const auto Delta = static_cast<std::uint32_t>(static_cast<std::int64_t>(BadTarget) - static_cast<std::int64_t>(Offset + Size));
							for (unsigned Byte = 0; Byte < 4; ++Byte)
							{
								Code[Offset + Size - 4 + Byte] = static_cast<std::uint8_t>(Delta >> (Byte * 8));
							}
							Changed = true;
						}
					}
					Offset += Size;
				}
				if (!Expect(Changed, "structural loop mutation did not reach a branch")) return false;
			}
			FJitCompileRequest Request;
			Request.Program = {Candidate.get(), Candidate, {1, 1, 1}};
			Request.FunctionIndex = Main;
			Request.RequiredGovernance = Variant == 8 ? 0 : ControlledExecutionCapabilities;
			Request.AvailableServices = CompiledInstructionServiceCapability;
			if (Variant == 6)
			{
				Request.Program.Owner.reset();
			}
			unsigned Checks = 0;
			if (Variant == 9)
			{
				Request.ExecutionCheck = {[](void* Context) noexcept
				{
					return ++*static_cast<unsigned*>(Context) < 3;
				}, &Checks};
			}
			FJitCompileResponse Response;
			const auto Result = Cache.Compile(Request, Response);
			if (Variant == 0)
			{
				if (!Expect(Result == EJitCompilationResult::Ready && Response.Function &&
								Response.Function->bPreferVmForExecution && Response.Function->bSuppressNestedBackendOnFallback &&
								Response.Reason.empty(),
						"valid owned unsupported loop must publish only a clean VM cost hint"))
				{
					return false;
				}
			}
			else if (!Expect(Result == (Variant == 9 ? EJitCompilationResult::Failed : EJitCompilationResult::Unsupported) &&
								 !Response.Function && !Response.Reason.empty(),
						 "invalid, borrowed, ungoverned or cancelled loop must not publish a VM cost artifact"))
			{
				return false;
			}
		}
		return true;
	}
}

int main(int Argc, char** Argv)
{
	std::string FactoryError;
	const bool Benchmark = Argc == 2 && std::string_view(Argv[1]) == "--benchmark";
	Factory = Argc == 2 && !Benchmark ? LoadJitBackend(Argv[1], FactoryError) : CreateAsmJitBackendFactory();
	if (!Expect(Factory != nullptr, FactoryError.c_str())) return 1;
	if (Benchmark)
	{
		auto C = VeroScriptCompile::CompileSource("int work(int n){int s=0;for(int i=0;i<n;i++)s+=i%17;return s;} int main(){print(work(100000));return 0;}", "controlled-benchmark.vs");
		if (!Expect(C.bSuccess, C.Error.Message.c_str())) return 1;
		std::shared_ptr<const FBytecodeModule> Module = std::move(C.Module);
		for (auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Jit})
		{
			const auto Start = std::chrono::steady_clock::now();
			for (int I = 0; I < 3; ++I)
			{
				const auto R = Run(Module, Mode, 0);
				if (!Expect(R.Ok, R.Error.c_str())) return 1;
				if (!I)
				{
					std::cout << "mode=" << static_cast<int>(Mode) << " instructions=" << R.Count << " native=" << R.Stats.ControlledNativeExecutions << '\n';
				}
			}
			std::cout << "elapsed_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count() << '\n';
		}
		return 0;
	}
	auto Compilation = VeroScriptCompile::CompileSource(
	    "int g=0;template<typename T,int Count> class Worker{public:template<typename U> T work(U n){"
	    "T sum=0;for(int i=0;i<n;i++){sum+=i;g+=1;}return sum;}"
	    "template<typename U> void show(U value){print(value);}};"
	    "int main(){Worker<int,8> worker=Worker<int,8>();worker.show(worker.work(8));print(g);return 0;}",
	    "controlled.vs");
	if (!Expect(Compilation.bSuccess, Compilation.Error.Message.c_str())) return 1;
	std::shared_ptr<const FBytecodeModule> Module = std::move(Compilation.Module);
	const auto Baseline = Run(Module, EExecutionBackend::Vm, 0);
	if (!Expect(Baseline.Ok, Baseline.Error.c_str())) return 1;
	bool SawNative = false;
	for (std::uint64_t Limit = 1; Limit <= Baseline.Count + 1; ++Limit)
	{
		const auto Vm = Run(Module, EExecutionBackend::Vm, Limit);
		const auto Native = Run(Module, EExecutionBackend::Jit, Limit);
		SawNative |= Native.Stats.ControlledNativeExecutions != 0;
		if (Vm.Ok != Native.Ok || Vm.Output != Native.Output || Vm.Count != Native.Count || Vm.Error != Native.Error)
		{
			std::cerr << "quota " << Limit << ": VM=" << Vm.Ok << ',' << Vm.Count << ',' << Vm.Error
			          << " native=" << Native.Ok << ',' << Native.Count << ',' << Native.Error << '\n'
			          << "VM output: [" << Vm.Output << "], native output: [" << Native.Output << "]\n";
			return 1;
		}
	}
	if (HasControlledNative() &&
	    !Expect(SawNative, "governed execution never entered generated machine code"))
	{
		return 1;
	}
	if (!HasControlledNative() &&
		!Expect(!SawNative, "backend without controlled native support must use the VM"))
	{
		return 1;
	}
	for (const bool Enabled : {false, true})
	{
		std::ostringstream Out;
		VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionBackend(EExecutionBackend::Mixed);
		Vm.SetExecutionControl(std::make_shared<FExecutionControl>());
		FJitPolicy Policy;
		Policy.bEnableControlledMixed = Enabled;
		Vm.SetJitPolicy(Policy);
		std::string Error;
		Vm.SetJitBackendFactory(Factory, Error);
		Vm.SetJitProgramOwner(Module, Error);
		if (!Expect(Vm.Execute(*Module, ERunMode::Script) && Out.str() == Baseline.Output,
		        "controlled Mixed selection changed semantics"))
		{
			return 1;
		}
		if (HasControlledNative() && !Expect((Vm.GetJitStats().ControlledNativeExecutions != 0) == Enabled, "controlled Mixed throughput opt-in was ignored"))
		{
			return 1;
		}
	}
	{
		auto C = VeroScriptCompile::CompileSource("template<int Limit> int spin(){int n=0;while(true){n+=1;if(n>Limit)n=0;}return n;}int main(){return spin<10000>();}", "controlled-stop.vs");
		if (!Expect(C.bSuccess, C.Error.Message.c_str())) return 1;
		std::shared_ptr<const FBytecodeModule> Infinite = std::move(C.Module);
		for (auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Jit})
		{
			for (bool Timeout : {false, true})
			{
				std::ostringstream Out;
				VeroScriptVm Vm(Out);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(Mode);
				Vm.SetExecutionLimits({0, Timeout ? 20u : 0u});
				auto Control = std::make_shared<FExecutionControl>();
				Vm.SetExecutionControl(Control);
				std::string Error;
				Vm.SetJitBackendFactory(Factory, Error);
				Vm.SetJitProgramOwner(Infinite, Error);
				std::thread Cancel;
				if (!Timeout)
				{
					Cancel = std::thread([Control]
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(20));
						Control->RequestCancel();
					});
				}
				const bool Ok = Vm.Execute(*Infinite, ERunMode::Script);
				if (Cancel.joinable())
				{
					Cancel.join();
				}
				if (!Expect(!Ok && Out.str().empty() && Vm.GetError().Message.find(Timeout ? "time" : "cancel") != std::string::npos,
				        Vm.GetError().Message.c_str()))
				{
					return 1;
				}
			}
		}
	}
	for (bool Cancelled : {false, true})
	{
		const auto Vm = Run(Module, EExecutionBackend::Vm, 0, false, Cancelled);
		const auto Native = Run(Module, EExecutionBackend::Jit, 0, false, Cancelled);
		if (!Expect(!Vm.Ok && !Native.Ok && Vm.Output == Native.Output && Vm.Error == Native.Error,
		        "permission/cancellation differs across backends"))
		{
			return 1;
		}
	}
	auto Stripped = std::make_shared<FBytecodeModule>(*Module);
	Stripped->DebugFiles.clear();
	Stripped->InitDebugLines.clear();
	for (auto& F : Stripped->Functions)
	{
		F.DebugLines.clear();
	}
	std::vector<std::uint8_t> Bytes;
	std::string Error;
	if (!Expect(VeroScriptBytecodeIO::SerializeBytecode(*Stripped, 0, Bytes, Error), Error.c_str())) return 1;
	auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 0, Error);
	if (!Expect(Loaded != nullptr, Error.c_str())) return 1;
	const auto Reloaded = Run(std::shared_ptr<const FBytecodeModule>(std::move(Loaded)), EExecutionBackend::Jit, 0);
	if (!Expect(Reloaded.Ok && Reloaded.Output == Baseline.Output && Reloaded.Count == Baseline.Count,
	        "debug stripping or bytecode round trip changed instruction accounting"))
	{
		return 1;
	}
	auto Invalid = *Module;
	Invalid.Functions.front().PauseMarkerOffsets = {0, 0};
	if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Invalid, Error), "duplicate cost markers accepted")) return 1;
	for (const auto* Source : {
	         "int leaf(int n){return 10/n;} int bridge(int n){try{return leaf(n);}finally{print(77);}} int main(){print(bridge(2));return 0;}",
	         "int leaf(int n){print(99);return 10/n;} int main(){try{print(leaf(0));}finally{print(77);}return 0;}",
	         "int leaf(){throw 3;} int bridge(){return leaf();} int main(){try{print(bridge());}catch(int n){print(n);}return 0;}",
	         "int bump(ref int n){n+=2;return n;} int main(){int n=3;print(bump(ref n));print(n);return 0;}",
	         "int recur(int n){if(n<=0)return 1;return recur(n-1)+n;}int main(){print(recur(12));return 0;}",
	         "int count=3;int& original(){return count;}int& relay(){return original();}int main(){int& alias=relay();alias=7;print(count);return 0;}",
	         "class Trace {public:~Trace(){print(77);}}; int leaf(int n){return n+1;} int main(){Trace t();print(leaf(4));return 0;}",
	         "int leaf(){exit(7);return 3;} int main(){print(leaf());return 0;}"})
	{
		if (!CompareSource(Source)) return 1;
	}
	for (const auto& Case : {
	         std::pair<const char*, const char*>{"int main(){int sum=0;for(int i=0;i<6;i++){sum+=i;sum+=2;}print(sum);return 0;}", "27\n"},
	         {"int main(){int n=3;int& alias=n;alias+=2;print(n);return 0;}", "5\n"},
	         {"void bump(uint64& n){n+=1u64;}int main(){uint64 n=18446744073709551615u64;bump(ref n);"
	          "print(n);fn(ref unsigned long)->void add=[](unsigned long& value)->void{value+=2u64;};"
	          "add(ref n);print(n);return 0;}",
	             "0\n2\n"},
	         {"void decrease(uint32& n){n-=1u;}int main(){uint32 n=0u;decrease(ref n);print(n);return 0;}",
	             "4294967295\n"},
	         {"int main(){double n=1.5;double& alias=n;alias+=1;print(n);return 0;}", "2.5\n"},
	         {"int main(){uint8 n=254u;++n;print(n);return 0;}", "255\n"},
	         {"int main(){double n=1.5;n+=1;print(n);return 0;}", "2.5\n"}})
	{
		if (!CompareSource(Case.first, Case.second)) return 1;
	}
	for (const auto* Source : {
	         "int main(){uint8 n=255u;n+=1u;print(n);return 0;}",
	         "void bump(uint8& n){n+=1u;}int main(){uint8 n=255u;bump(ref n);print(n);return 0;}",
	         "void bump(int64& n){n+=1i64;}int main(){int64 n=9223372036854775807i64;bump(ref n);print(n);return 0;}",
	         "int main(){int n=2147483647;n+=1;print(n);return 0;}",
	         "int main(){int n=-2147483647;n+=-2;print(n);return 0;}",
	         "int main(){int n=2147483647;int one=1;n+=one;print(n);return 0;}"})
	{
		if (!CompareSource(Source, nullptr, true)) return 1;
	}
	if (!CompareSource("int main(){int n=5;n+=n;print(n);return 0;}", "10\n", false, true)) return 1;
	if (!CheckPollingBoundaries()) return 1;
	if (!CompareSource("int main(){int sum=7;for(int i=0;i<5;i++){sum=(sum*3+i)%97;}return sum;}",
	        "", false, true))
	{
		return 1;
	}
	if (!CheckPollingSource("int main(){int sum=7;for(int i=0;i<5;i++){sum=(sum*3+i)%97;}return sum;}")) return 1;
	if (!CompareSource("int work(int n){int sum=0;bool add=true;for(int i=0;i<n;i++){"
	                   "if(add)sum+=i;add=i<2;}return sum;}int main(){return work(5);}",
	        "", false, true))
	{
		return 1;
	}
	if (!CompareSource("int work(int n){int sum=7;bool flag=true;for(int i=0;i<n;i++){"
	                   "flag=flag==(i%2==0);sum+=to_int(flag!=(i<3));}return sum;}"
	                   "int main(){print(work(6));return 0;}",
	        "11\n", false, true))
	{
		return 1;
	}
	if (!CompareSource("int work(int n){int value=2147483640;for(int i=0;i<n;i++){value+=i+1;}return value;}"
	                   "int main(){return work(5);}",
	        nullptr, true, true))
	{
		return 1;
	}

	if (!CompareSource("int next(int seed){static int n=seed;return ++n;}int main(){print(next(3));print(next(9));return 0;}", "4\n5\n", false, true)) return 1;
	if (!CompareSource("int main(){auto f=[n=0]()mutable->int{return ++n;};print(f());print(f());return 0;}", "1\n2\n")) return 1;
	if (!CheckPollingSource("int next(int seed){static int n=seed;return ++n;}int main(){print(next(3));print(next(9));return 0;}")) return 1;
	if (!CheckControlledLoopSelection() || !CheckLocalStaticVmPreference() ||
		!CheckNonPollingScalarLoopSelection() || !CheckUnsupportedOwnedLoopSelection() ||
		!CheckIndependentControlledKernel() || !CheckOwnedLoopStructuralAdmission())
	{
		return 1;
	}
	std::cout << "controlled execution passed\n";
	return 0;
}
