// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptTestJitCapabilities.h"
#include "VeroScriptCompile.h"
#include "VeroScriptAst.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptMemory.h"
#include "VeroScriptParser.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptVm.h"
#include "VeroScriptVmScalarArithmetic.h"
#include "../ExecutionSupport/VeroScriptVmScalarCallGraph.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <thread>

using namespace VeroScript;

namespace
{
	void Require(bool Value, const std::string& Message);

	void CheckObjectSupportStorage()
	{
		FScriptObject Original;
		const std::string Name = "retained-module-with-a-long-source-identity.vs\x1F"
		                         "Original";
		Original.SetTypeName(Name);
		FScriptObject SameType;
		SameType.SetTypeName(Name);
		Require(&Original.GetTypeName() == &SameType.GetTypeName(), "equal object type names are shared");
		if (Original.GetHostBase() || !Original.GetCompletedCopyBases().empty())
		{
			throw std::runtime_error("ordinary object has nonempty support state");
		}
		Original.EditCompletedCopyBases().push_back(7);
		Original.SetHostBase(std::make_shared<FHostObject>());
		FScriptObject Copy = Original;
		Require(&Original.GetTypeName() == &Copy.GetTypeName(), "object copies share immutable type identity");
		Copy.RuntimeTypeModule = &Original;
		Copy.RuntimeTypeGeneration = 7;
		Copy.RuntimeTypeIndex = 11;
		Copy.SetTypeName("Changed");
		Require(Original.GetTypeName() == Name && Copy.GetTypeName() == "Changed" && !Copy.RuntimeTypeModule && !Copy.RuntimeTypeGeneration && Copy.RuntimeTypeIndex == 0xFFFF,
		    "replacing a type name detaches its identity and invalidates the runtime hint");
		for (int Index = 0; Index < 1100; ++Index)
		{
			FScriptObject Temporary;
			Temporary.SetTypeName("eviction-" + std::to_string(Index));
		}
		Require(Original.GetTypeName() == Name, "type name outlives cache eviction");
		bool ShutdownOk = true;
		std::thread Worker([&ShutdownOk]()
		{
			struct FLateTypeName
			{
				bool& Ok;
				~FLateTypeName()
				{
					try
					{
						FScriptObject Late;
						Late.SetTypeName("late-worker-type");
						Ok = Late.GetTypeName() == "late-worker-type";
					}
					catch (...)
					{
						Ok = false;
					}
				}
			};
			// Initialized before the cache, so this destructor runs after it.
			thread_local FLateTypeName Late{ShutdownOk};
			(void)Late;
			FScriptObject Object;
			Object.SetTypeName("worker-type");
		});
		Worker.join();
		Require(ShutdownOk, "type names remain usable during late thread cleanup");
		Copy.EditCompletedCopyBases().push_back(11);
		if (Original.GetCompletedCopyBases() != std::vector<std::uint16_t>{7} || Copy.GetCompletedCopyBases() != std::vector<std::uint16_t>{7, 11})
		{
			throw std::runtime_error("copy construction state aliased across object copies");
		}
		Copy.SetHostBase({});
		Copy.ClearCompletedCopyBases();
		if (!Original.GetHostBase() || Copy.GetHostBase() || !Copy.GetCompletedCopyBases().empty())
		{
			throw std::runtime_error("object host ownership or support-state clearing changed");
		}
	}

	struct FResult
	{
		bool Success = false;
		std::string Output, Error;
		std::uint32_t Line = 0, Column = 0;
		std::uint64_t Count = 0;
		int ExitCode = 0;
		FJitStats Stats;
	};

	void Require(const bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	FResult Run(const std::shared_ptr<const FBytecodeModule>& Module, const EExecutionBackend Backend,
	    const std::shared_ptr<const IJitBackendFactory>& Factory, const std::uint64_t Limit,
	    const std::size_t DepthLimit = 0, const bool ForceGenericNative = false,
	    const bool EnableCancellationControl = true)
	{
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionBackend(Backend);
		if (ForceGenericNative)
		{
			Vm.SetExecutionCheck([]()
			{
				return true;
			});
		}
		if (DepthLimit != 0)
		{
			Vm.SetMaxCallDepth(DepthLimit);
		}
		if (Limit != 0)
		{
			Vm.SetExecutionLimits({Limit, 0});
			if (EnableCancellationControl)
			{
				Vm.SetExecutionControl(std::make_shared<FExecutionControl>());
			}
		}
		if (Backend != EExecutionBackend::Vm)
		{
			std::string Error;
			Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
		}
		FResult Result;
		Result.Success = Vm.Execute(*Module, ERunMode::Script);
		Result.Output = Output.str();
		if (Vm.HasError())
		{
			Result.Error = Vm.GetError().Message;
			Result.Line = Vm.GetError().Location.Line;
			Result.Column = Vm.GetError().Location.Column;
		}
		Result.Count = Vm.GetExecutedInstructionCount();
		Result.ExitCode = Vm.GetProcessExitCode();
		Result.Stats = Vm.GetJitStats();
		return Result;
	}

	void Compare(const std::string& Source, const std::string& Expected,
	    const std::shared_ptr<const IJitBackendFactory>& Factory, const bool Failure = false,
	    const bool RequireNativeKernel = false)
	{
		auto Compiled = VeroScriptCompile::CompileSource(Source, "performance-regression.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message + "\n" + Source);
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		const auto Base = Run(Module, EExecutionBackend::Vm, {}, 0);
		Require(Base.Success != Failure && Base.Output == Expected, "VM result: " + Base.Output + "; " + Base.Error + "\n" + Source);
		for (const auto Limit : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{12}, std::uint64_t{100000}})
		{
			const auto Reference = Run(Module, EExecutionBackend::Vm, {}, Limit);
			if (!Factory)
			{
				continue;
			}
			for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
			{
				const auto Result = Run(Module, Backend, Factory, Limit);
				Require(Result.Success == Reference.Success && Result.Output == Reference.Output && Result.Error == Reference.Error && Result.Line == Reference.Line && Result.Column == Reference.Column && (Limit == 0 || Result.Count == Reference.Count),
				    "backend mismatch (mode=" + std::to_string(static_cast<int>(Backend)) + ", limit=" + std::to_string(Limit) + "): " + Result.Error + " @" + std::to_string(Result.Line) + ":" + std::to_string(Result.Column) + " / " + Reference.Error + " @" + std::to_string(Reference.Line) + ":" + std::to_string(Reference.Column) + "; counts=" + std::to_string(Result.Count) + "/" + std::to_string(Reference.Count) + "\n" + Source);
				if (RequireNativeKernel && Limit == 0 && (Factory->GetDescription().Capabilities & JitNativeTypedLoopsCapability) != 0)
				{
					Require(Result.Stats.NativeTypedLoopExecutions != 0, "fixture did not enter native typed code\n" + Source);
				}
			}
		}
	}

	void IndexedScalarExpressions(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		Compare("int work(array<int> a,array<int> b,int n){int total=5;"
		        "for(int i=0;i<n;i++)total=(total+a[i]*b[n-i-1])%97;return total;}"
		        "void main(){array<int> a={2,3,4};array<int> b={5,6,7};print(work(a,b,3));}",
		    "57\n", Factory);
		Compare("void main(){array<int> a={1};int total=7;try{"
		        "for(int i=0;i<2;i++)total+=a[i]*3;}finally{print(total);}}",
		    "10\n", Factory, true);
		Compare("void main(){array<int> a={2147483647};int total=1;try{"
		        "for(int i=0;i<1;i++)total+=a[i]+1;}finally{print(total);}}",
		    "1\n", Factory, true);
		Compare("void main(){array<int> a={9};int total=3;int divisor=0;try{"
		        "for(int i=0;i<1;i++)total+=a[i]/divisor;}finally{print(total);}}",
		    "3\n", Factory, true);
		Compare("int calls=0;int touch(int x){calls+=1;print(calls);return x;}"
		        "int work(array<int> a){int total=0;for(int i=0;i<3;i++)"
		        "total+=a[i]*touch(i+1);return total;}"
		        "void main(){array<int> a={2,3,4};print(work(a));print(calls);}",
		    "1\n2\n3\n20\n3\n", Factory);
	}

	void DispatchSelectionBoundaries(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		// Unrelated declarations must not be needed to classify the active entry.
		std::string Unused = "import \"string.vs\";";
		for (int Index = 0; Index < 96; ++Index)
		{
			Unused += "int unused" + std::to_string(Index) +
			    "(int n){return std::strings::char_code(to_string(n),0);}";
		}
		Unused += "int work(int n){return n*3+1;}void main(){print(work(7));}";
		Compare(Unused, "22\n", Factory);

		// A reachable closure larger than the inline analysis budget keeps the
		// complete classifier, including text features at the end of the chain.
		std::string Chain = "import \"string.vs\";int step66(int n){return std::strings::char_code(\"AZ\",n&1);}";
		for (int Index = 65; Index >= 0; --Index)
		{
			Chain += "int step" + std::to_string(Index) + "(int n){return step" +
			    std::to_string(Index + 1) + "(n);}";
		}
		Chain += "void main(){print(step0(0));print(step0(1));}";
		Compare(Chain, "65\n90\n", Factory);
		Compare("import \"string.vs\";int right(int n);int left(int n){if(n==0)return std::strings::char_code(\"AZ\",0);"
		        "return right(n-1);}int right(int n){if(n==0)return std::strings::char_code(\"AZ\",1);"
		        "return left(n-1);}void main(){print(left(4));print(left(5));}",
		    "65\n90\n", Factory);

		// Local-kind scratch is bounded independently of the reachable closure.
		std::string Locals = "int work(int n){int total=0;";
		std::string Sum;
		for (int Index = 0; Index < 260; ++Index)
		{
			Locals += "int v" + std::to_string(Index) + "=n+" + std::to_string(Index) + ";";
			Sum += "total+=v" + std::to_string(Index) + ";";
		}
		Locals += Sum + "return total+len(to_string(v0))+len(to_string(v259));}"
		    "void main(){print(work(1));}";
		Compare(Locals, "33934\n", Factory);
	}

	void ScalarDirectCallGraphs()
	{
		// Exercise every admitted parameter count through actual child frames.
		// A second call reuses the same frame after its nonparameter slot was
		// dirtied, so the zero-local check cannot pass on fresh storage by chance.
		std::uint64_t Serial = 1u;
		for (std::uint16_t Count = 0u; Count <= 8u; ++Count)
		{
			for (const bool ExtraLocal : {false, true})
			{
				if (ExtraLocal && Count == 8u)
				{
					continue;
				}
				FBytecodeModule Module;
				for (std::int32_t Value = 0; Value <= 9; ++Value)
				{
					FBytecodeConstant Constant; Constant.Kind = EConstantKind::I32; Constant.I32 = Value;
					Module.Constants.push_back(Constant);
				}
				Module.Functions.resize(2);
				auto& Leaf = Module.Functions[0]; auto& Caller = Module.Functions[1];
				Leaf.ParamCount = Caller.ParamCount = static_cast<std::uint8_t>(Count);
				Leaf.LocalCount = static_cast<std::uint16_t>(Count + (ExtraLocal ? 1u : 0u)); Caller.LocalCount = Count;
				Leaf.bReturnsVoid = Caller.bReturnsVoid = false;
				const auto Emit = [](auto& Code, const EOpcode Op) {Code.push_back(static_cast<std::uint8_t>(Op));};
				const auto Slot = [&](auto& Code, const EOpcode Op, const std::uint16_t Index)
					{Emit(Code, Op); Bytecode::WriteU16LE(Code, Index);};
				if (ExtraLocal)
				{
					Slot(Leaf.Code, EOpcode::LoadLocal, Count);
					Slot(Leaf.Code, EOpcode::ConstI32, 9u);
					Slot(Leaf.Code, EOpcode::StoreLocal, Count);
				}
				else
				{
					Slot(Leaf.Code, EOpcode::ConstI32, 0u);
				}
				std::array<FValue, 8> Arguments;
				std::int32_t Expected = 0;
				for (std::uint16_t Index = 0u; Index < Count; ++Index)
				{
					const auto Value = static_cast<std::int32_t>(Index) * 17 - 31;
					Arguments[Index] = Value; Expected += Value * (Index + 1);
					Slot(Leaf.Code, EOpcode::LoadLocal, Index);
					Slot(Leaf.Code, EOpcode::ConstI32, static_cast<std::uint16_t>(Index + 1u));
					Emit(Leaf.Code, EOpcode::Mul); Emit(Leaf.Code, EOpcode::Add);
				}
				Emit(Leaf.Code, EOpcode::Return);
				for (int Invocation = 0; Invocation < 2; ++Invocation)
				{
					for (std::uint16_t Index = 0u; Index < Count; ++Index)
					{
						Slot(Caller.Code, EOpcode::LoadLocal, Index);
					}
					Slot(Caller.Code, EOpcode::Call, 0u); Caller.Code.push_back(static_cast<std::uint8_t>(Count));
				}
				Emit(Caller.Code, EOpcode::Add); Emit(Caller.Code, EOpcode::Return);
				const auto Graph = BuildScalarCallGraph(Module, 1u);
				FValue Output = std::string("untouched");
				Require(Graph && TryExecuteScalarCallGraph(*Graph, Count ? Arguments.data() : nullptr, 8u, 32u, Serial++, Output) &&
					std::get<std::int32_t>(Output) == Expected * 2,
					"scalar graph parameter copies preserve all 0-8 ordered arguments and zero reused nonparameter slots");
			}
		}

		auto Branch = VeroScriptCompile::CompileSource(
		    "int choose(int n){if(n<5)return n*2;return n-5;}"
		    "void main(){print(choose(3));print(choose(8));}", "scalar-call-branch.vs");
		Require(Branch.bSuccess, Branch.Error.Message);
		std::shared_ptr<const FBytecodeModule> BranchModule = std::move(Branch.Module);
		const auto BranchResult = Run(BranchModule, EExecutionBackend::Vm, {}, 0);
		Require(BranchResult.Success && BranchResult.Output == "6\n3\n",
		    "scalar branch-only calls preserve both outcomes");

		auto Recursive = VeroScriptCompile::CompileSource(
		    "int right(int n);\n"
		    "int left(int n){if(n<1)return 2;return right(n-1)+3;}\n"
		    "int right(int n){if(n==0)return 5;return left(n-1)+7;}\n"
			"int retain(int n,int value){if(n<1)return value;"
			"return (n&1)+retain(n-1,value+(n&7));}\n"
			"void main(){print(left(8));print(left(80));print(retain(1024,17));}", "scalar-direct-calls.vs");
		Require(Recursive.bSuccess, Recursive.Error.Message);
		std::shared_ptr<const FBytecodeModule> Recursion = std::move(Recursive.Module);
		const auto Result = Run(Recursion, EExecutionBackend::Vm, {}, 0, 1100);
		const auto CheckedRecursion = Run(Recursion, EExecutionBackend::Vm, {}, 0, 1100, true);
		Require(Result.Success && CheckedRecursion.Success && Result.Output == "42\n402\n4113\n"
		        && Result.Output == CheckedRecursion.Output,
		    "recursion preserves its result across compact frame and operand growth");
		for (const auto Limit : {std::size_t{4}, std::size_t{20}})
		{
			const auto Fast = Run(Recursion, EExecutionBackend::Vm, {}, 0, Limit);
			const auto Checked = Run(Recursion, EExecutionBackend::Vm, {}, 0, Limit, true);
			Require(!Fast.Success && !Checked.Success && Fast.Output == Checked.Output
			        && Fast.Error == Checked.Error && Fast.Line == Checked.Line && Fast.Column == Checked.Column,
			    "scalar recursive calls preserve canonical depth diagnostics");
		}

		auto Overflow = VeroScriptCompile::CompileSource(
		    "int grow(int n){\n"
		    "if(n<1)return 2147483647;\n"
		    "return grow(n-1)+1;\n"
		    "}\n"
		    "void main(){try{print(grow(650));}finally{print(7);}}", "scalar-call-overflow.vs");
		Require(Overflow.bSuccess, Overflow.Error.Message);
		std::shared_ptr<const FBytecodeModule> OverflowModule = std::move(Overflow.Module);
		const auto Fast = Run(OverflowModule, EExecutionBackend::Vm, {}, 0, 700);
		const auto Checked = Run(OverflowModule, EExecutionBackend::Vm, {}, 0, 700, true);
		Require(!Fast.Success && !Checked.Success && Fast.Output == "7\n" && Fast.Output == Checked.Output
		        && Fast.Error == Checked.Error && Fast.Line == Checked.Line && Fast.Column == Checked.Column,
		    "failed scalar graph probe preserves arithmetic diagnostics and finally");

		auto Effects = VeroScriptCompile::CompileSource(
		    "int count=0;int touch(int n){count=count+1;return n;}"
		    "int work(int n){return touch(n)+touch(n+1);}"
		    "void main(){print(work(4));print(count);}", "scalar-call-effects.vs");
		Require(Effects.bSuccess, Effects.Error.Message);
		std::shared_ptr<const FBytecodeModule> EffectsModule = std::move(Effects.Module);
		const auto Visible = Run(EffectsModule, EExecutionBackend::Vm, {}, 0);
		Require(Visible.Success && Visible.Output == "9\n2\n",
		    "effectful callees keep authoritative execution without replay");

		auto Throwing = VeroScriptCompile::CompileSource(
		    "int risky(int n){if((n&3)==0){throw n;}return n+3;}"
		    "int bridge(int n){return risky(n)+1;}"
		    "int always_throw(int n){throw n;}int forward_failure(int n){return always_throw(n);}"
		    "int cycle_return(int n);"
		    "int recursive_throw(int n){if(n==0){throw n;}return cycle_return(n-1);}"
		    "int cycle_return(int n){return recursive_throw(n);}"
		    "void main(){int sum=0;int caught=0;int cleaned=0;"
		    "for(int i=0;i<9;i++){try{sum+=bridge(i);}catch(int failure){caught+=failure;}"
		    "finally{cleaned++;}}print(sum);print(caught);print(cleaned);"
		    "try{print(recursive_throw(32));}catch(int failure){print(failure);}}", "scalar-call-throw.vs");
		Require(Throwing.bSuccess, Throwing.Error.Message);
		std::shared_ptr<const FBytecodeModule> ThrowingModule = std::move(Throwing.Module);
		const auto BuildGraph = [&](std::string_view Name)
		{
			const auto& Functions = ThrowingModule->Functions;
			const auto Found = std::find_if(Functions.begin(), Functions.end(), [&](const FBytecodeFunction& Function)
			{
				return Function.Name.find(Name) != std::string::npos;
			});
			Require(Found != Functions.end(), "missing scalar throw fixture function");
			return BuildScalarCallGraph(*ThrowingModule, static_cast<std::uint16_t>(Found - Functions.begin()));
		};
		const auto Graph = BuildGraph("bridge#");
		Require(static_cast<bool>(Graph), "scalar call graph admits a verified throwing branch");
		Require(!BuildGraph("forward_failure#"), "a callee that only throws keeps canonical execution");
		Require(!BuildGraph("recursive_throw#"), "mutually recursive throwing callees keep canonical execution");
		FValue Argument = std::int32_t{0};
		FValue ReturnValue = std::int32_t{97};
		Require(!TryExecuteScalarCallGraph(*Graph, &Argument, 8, 8, 1, ReturnValue)
		        && std::get_if<std::int32_t>(&ReturnValue) && std::get<std::int32_t>(ReturnValue) == 97,
		    "a speculative throw leaves the result untouched");
		Argument = std::int32_t{1};
		Require(TryExecuteScalarCallGraph(*Graph, &Argument, 8, 8, 1, ReturnValue)
		        && std::get_if<std::int32_t>(&ReturnValue) && std::get<std::int32_t>(ReturnValue) == 5,
		    "a throw side exit does not disable later successful calls in the same run");
		const auto ThrowingFast = Run(ThrowingModule, EExecutionBackend::Vm, {}, 0);
		const auto ThrowingChecked = Run(ThrowingModule, EExecutionBackend::Vm, {}, 0, 0, true);
		Require(ThrowingFast.Success && ThrowingChecked.Success && ThrowingFast.Output == "48\n12\n9\n0\n"
		        && ThrowingFast.Output == ThrowingChecked.Output,
		    "throw side exits preserve nested catch and finally");
	}

	void StandardValueLeafBoundaries(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		for (const auto* Source : {
		         "void main(){std::memory::buffer b(2);b.set(0,7);b.set(1,13);int total=0;"
		         "for(int i=0;i<3;i++){total+=to_int(b.get(i%2));}print(total);}",
		         "void main(){std::bigint n(7);for(int i=0;i<3;i++)n=n*std::bigint(17);print(n.to_string());}"})
		{
			auto Compiled = VeroScriptCompile::CompileSource(Source, "value-leaf-boundaries.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
			const auto Complete = Run(Module, EExecutionBackend::Vm, {}, 100000, 0, true);
			Require(Complete.Success && Complete.Count > 0, "generic native boundary reference must complete");
			for (std::uint64_t Limit = 1; Limit <= Complete.Count + 1; ++Limit)
			{
				const auto Reference = Run(Module, EExecutionBackend::Vm, {}, Limit, 0, true);
				for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
				{
					if (Backend != EExecutionBackend::Vm && !Factory)
					{
						continue;
					}
					const auto Result = Run(Module, Backend, Factory, Limit);
					Require(Result.Success == Reference.Success && Result.Count == Reference.Count && Result.Output == Reference.Output && Result.Error == Reference.Error && Result.Line == Reference.Line && Result.Column == Reference.Column,
					    "standard value leaf differs from generic native execution at limit " + std::to_string(Limit));
				}
			}
		}
	}

	void QuotaOnlyExpressionBoundaries()
	{
		for (const bool bOverflow : {false, true})
		{
			const auto Input = bOverflow ? "2147483647" : "7";
			auto Compiled = VeroScriptCompile::CompileSource(
			    std::string("int work(int input){int value=input;\n"
			                "try{value=(value+1)*3;}finally{print(value);}\n"
			                "return value;}void main(){print(work(") + Input + "));}",
			    "quota-only-expression.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
			const auto Complete = Run(Module, EExecutionBackend::Vm, {}, 10000, 0, true, false);
			Require(Complete.Success != bOverflow &&
			        Complete.Output == (bOverflow ? "2147483647\n" : "24\n24\n") && Complete.Count > 0,
			    "quota expression reference preserves assignment, overflow and finally");
			for (std::uint64_t Limit = 1; Limit <= Complete.Count + 1; ++Limit)
			{
				const auto Reference = Run(Module, EExecutionBackend::Vm, {}, Limit, 0, true, false);
				const auto Actual = Run(Module, EExecutionBackend::Vm, {}, Limit, 0, false, false);
				Require(Actual.Success == Reference.Success && Actual.Count == Reference.Count &&
				        Actual.Output == Reference.Output && Actual.Error == Reference.Error &&
				        Actual.Line == Reference.Line && Actual.Column == Reference.Column,
				    "quota-only arithmetic differs at limit " + std::to_string(Limit));
			}
		}
	}

	void LocalScalarIndexOperations(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		Compare("void main(){array<int> items={7,11};array<int> alias=items;"
		        "map<int,int> counts=map<int,int>();map<string,int> named=map<string,int>();"
		        "int i=1;string key=\"sample\";int v=19;"
		        "items[i]=v;counts[i]=v;named[key]=v;v=23;named[key]=v;"
		        "print(alias[i]+counts[i]+named[key]);}", "61\n", Factory);
		Compare("void main(){array<int> items={7,11};int i=-1;int value=19;"
		        "try{items[i]=value;}finally{print(items[1]);}}", "11\n", Factory, true);
		Compare("void main(){map<string,int> counts=map<string,int>();string key=\"absent\";"
		        "try{print(counts[key]);}finally{print(17);}}", "17\n", Factory, true);
	}

	void SignedWideMultiplication(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const auto Check = [](const std::int64_t Left, const std::int64_t Right)
		{
			// An independent unsigned-magnitude bound avoids overflowing the oracle.
			const auto L = Left < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(Left)
			                        : static_cast<std::uint64_t>(Left);
			const auto R = Right < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(Right)
			                         : static_cast<std::uint64_t>(Right);
			const std::uint64_t Bound = (Left < 0) != (Right < 0)
			    ? std::uint64_t{1} << 63u
			    : static_cast<std::uint64_t>(INT64_MAX);
			const bool Overflow = R != 0 && L > Bound / R;
			const std::int64_t Expected = Overflow ? 37 : Left * Right;
			std::int64_t Product = 37;
			const auto Status = VmDetail::EvalSigned64Arithmetic(EOpcode::Mul, Left, Right, Product);
			Require(Status == (Overflow ? VmDetail::ESignedArithmeticResult::Overflow : VmDetail::ESignedArithmeticResult::Ok) && Product == Expected,
			    "signed 64-bit multiply disagrees with the independent bound or changes failed output");
			std::int64_t Aliased = Left;
			Require(VmDetail::EvalSigned64Arithmetic(EOpcode::Mul, Aliased, Right, Aliased) == Status && Aliased == (Overflow ? Left : Expected), "wide multiply changes its aliased input on failure");
			std::vector<FValue> Stack{std::string("prefix"), Left, Right};
			if (Left >= INT32_MIN && Left <= INT32_MAX)
			{
				Stack[1] = static_cast<std::int32_t>(Left);
			}
			const auto OriginalTag = Stack[1].index();
			Require(VmDetail::TryEvalScalarStackArithmetic(EOpcode::Mul, Stack, 1, 2) != Overflow,
			    "wide stack multiply disagrees with its checked scalar operation");
			Require(std::get<std::string>(Stack.front()) == "prefix", "wide multiply changes the caller stack");
			if (Overflow)
			{
				Require(Stack.size() == 3 && Stack[1].index() == OriginalTag && (std::get_if<std::int32_t>(&Stack[1]) ? static_cast<std::int64_t>(std::get<std::int32_t>(Stack[1])) : std::get<std::int64_t>(Stack[1])) == Left && std::get<std::int64_t>(Stack[2]) == Right,
				    "failed wide multiply changes stack operands");
			}
			else
			{
				Require(Stack.size() == 2 && std::get<std::int64_t>(Stack.back()) == Expected,
				    "wide stack multiply changes its result representation");
			}
		};
		const std::array<std::int64_t, 17> Edges{INT64_MIN, INT64_MIN + 1, INT64_MAX, INT64_MAX - 1,
		    INT32_MIN, INT32_MAX, -(std::int64_t{1} << 32u), std::int64_t{1} << 32u,
		    -3037000500ll, 3037000500ll, -3037000499ll, 3037000499ll, -2, -1, 0, 1, 2};
		for (const auto Left : Edges)
		{
			for (const auto Right : Edges)
			{
				Check(Left, Right);
			}
		}
		std::uint64_t State = 0x15A7830BFC9D246Eull;
		const auto Next = [&]()
		{
			State ^= State >> 12u;
			State ^= State << 25u;
			State ^= State >> 27u;
			const auto Bits = State * 2685821657736338717ull;
			std::int64_t Value = 0;
			std::memcpy(&Value, &Bits, sizeof(Value));
			return Value;
		};
		for (unsigned Index = 0; Index < 32768u; ++Index)
		{
			const auto Left = Next();
			const auto Right = Next();
			Check(Left, Right);
			Check(Left >> 32, Right >> 32);
		}
		Compare("int64 mul(int64 a,int64 b){return a*b;} void main(){"
		        "print(mul(3037000499i64,3037000499i64));"
		        "print(mul(-9223372036854775807i64-1i64,1i64));"
		        "print(mul(-9223372036854775807i64-1i64,0i64));}",
		    "9223372030926249001\n-9223372036854775808\n0\n", Factory);
		for (const auto Args : {"3037000500i64,3037000500i64", "-9223372036854775807i64-1i64,-1i64"})
		{
			Compare("int64 mul(int64 a,int64 b){return a*b;} void main(){print(mul(" + std::string(Args) + "));}", "", Factory, true);
		}
	}

	void ScalarAndLengthPaths(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const auto CheckEquality = [](const FValue& Left, const FValue& Right, const bool Expected)
		{
			for (const auto Op : {EOpcode::Eq, EOpcode::Ne})
			{
				std::vector<FValue> Stack{std::string("prefix"), Left, Right};
				Require(VmDetail::TryEvalScalarStackEquality(Op, Stack, 1, 2) && Stack.size() == 2 && std::get<std::string>(Stack.front()) == "prefix" && std::get<bool>(Stack.back()) == (Op == EOpcode::Eq ? Expected : !Expected),
				    "scalar equality changed its value or the caller stack");
			}
		};
		CheckEquality(std::int32_t{INT32_MIN}, std::int32_t{INT32_MIN}, true);
		CheckEquality(std::int64_t{9007199254740993ll}, std::int64_t{9007199254740992ll}, false);
		CheckEquality(std::uint32_t{UINT32_MAX}, std::uint32_t{UINT32_MAX}, true);
		CheckEquality(std::uint64_t{UINT64_MAX}, std::uint64_t{UINT64_MAX - 1}, false);
		CheckEquality(0.0, -0.0, true);
		CheckEquality(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), false);
		CheckEquality(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), true);
		CheckEquality(true, false, false);
		CheckEquality(std::int8_t{-128}, std::int8_t{-128}, true);
		CheckEquality(std::uint8_t{255}, std::uint8_t{254}, false);
		CheckEquality(std::int16_t{-32768}, std::int16_t{-32768}, true);
		CheckEquality(std::uint16_t{65535}, std::uint16_t{0}, false);
		for (const auto& Values : {std::vector<FValue>{std::int32_t{1}, std::int64_t{1}},
		         std::vector<FValue>{std::int32_t{1}, std::uint32_t{1}},
		         std::vector<FValue>{std::string("a"), std::string("a")}})
		{
			auto Stack = Values;
			Require(!VmDetail::TryEvalScalarStackEquality(EOpcode::Eq, Stack, 0, 2) && Stack.size() == Values.size() && Stack[0].index() == Values[0].index() && Stack[1].index() == Values[1].index(), "unsupported equality modified its operands");
		}
		std::vector<FValue> Bounded{std::int32_t{1}, std::int32_t{1}};
		Require(!VmDetail::TryEvalScalarStackEquality(EOpcode::Eq, Bounded, 1, 2) && !VmDetail::TryEvalScalarStackEquality(EOpcode::Eq, Bounded, 0, 0) && Bounded.size() == 2, "scalar equality bypassed a frame or stack bound");
		Compare("int work(int n){int total=0;for(int i=0;i<n;i++){if(i%7==0)total++;if(i%5!=0)total+=2;}"
		        "return total;}void main(){print(work(100));}",
		    "175\n", Factory);
		Compare("int64 work(int64 x,int a,int b){int64 value=x;value=(value*3+a*2-b)/7;"
		        "value=(value+5)%97;return value;}void main(){print(work(40i64,2,4));print(work(-40i64,2,4));}",
		    "22\n-12\n", Factory);
		Compare("int64 work(){int64 value=9007199254740993i64;value=(value*3i64+5)%7;return value;}"
		        "void main(){print(work());}",
		    "6\n", Factory);
		Compare("int64 work(int a,int b){int64 value=1;value=value+(a+b)*0;return value;}"
		        "void main(){print(work(2147483647,1));}",
		    "", Factory, true);
		Compare("int64 work(int a,int b){int64 value=1;value=value+(a/b)*0;return value;}"
		        "void main(){print(work(-2147483647-1,-1));}",
		    "", Factory, true);
		Compare("int64 work(int64 x,int one){int64 value=x;value=(value+one)*0;return value;}"
		        "void main(){print(work(9223372036854775807i64,1));}",
		    "", Factory, true);
		Compare("int64 work(ref int64 x,int a){int64 local=0;x=(x+a)*2;return x;}"
		        "void main(){int64 value=9007199254740993i64;print(work(ref value,3));print(value);}",
		    "18014398509481992\n18014398509481992\n", Factory);
		Compare("int work(int x,int y){int a=x;int b=y;a=(a*3+b*2-5)%97;b=(a+7)/(y+1);return a+b;}"
		        "void main(){print(work(8,2));print(work(-8,2));}",
		    "33\n-31\n", Factory);
		Compare("int work(int a,int b){a=(a+1)*b;return a;}void main(){print(work(2147483647,0));}", "", Factory, true);
		Compare("int work(int a,int b){a=(a+b)/(b-b);return a;}void main(){print(work(7,3));}", "", Factory, true);
		Compare("int work(ref int a,int b){a=(a+b)*2;return a;}void main(){int a=4;print(work(ref a,3));print(a);}",
		    "14\n14\n", Factory);
		Compare("int work(string text,int n){int total=0;for(int i=0;i<n;i++)total=to_int((total+len(text))%1009);return total;}"
		        "void main(){print(work(\"abcd\",7));print(work(\"\",9));}",
		    "28\n0\n", Factory, false, true);
		Compare("int work(){array<int> a={1};array<int> alias=a;int total=0;"
		        "for(int64 i=0;i<len(a);i++){total+=a[i];if(i<5)alias.push(to_int(i)+2);}"
		        "return total+to_int(len(alias));}void main(){print(work());}",
		    "27\n", Factory, false, true);
		Compare("int work(){array<unsigned int> a={1u};array<unsigned int> alias=a;unsigned int total=0u;"
		        "for(int64 i=0;i<len(a);i++){total+=a[i];if(i<3)alias.push(2u);}"
		        "return to_int(total)+to_int(len(alias));}void main(){print(work());}",
		    "11\n", Factory, false, true);
		Compare("int work(){string a=\"abc\";string b=\"q\";string t=\"\";int total=0;"
		        "for(int64 i=0;i<4i64;i++){t=a;a=b;b=t;total=to_int(total+len(a));}"
		        "return total;}void main(){print(work());}",
		    "8\n", Factory, false, true);
		Compare("int work(string a,string b){int total=0;for(int64 i=0;i<5i64;i++){"
		        "if(a==b)total++;if(a!=b)total+=10;total+=to_int(len(a));}return total;}"
		        "void main(){string b=\"sam\";b+=\"e\";print(work(\"same\",b));print(work(\"other\",b));}",
		    "25\n75\n", Factory, false, true);
		Compare("void main(){print(to_int(2147483647i64));print(to_int(-2147483648i64));"
		        "print(to_int64(-2147483647));print(to_string(-9223372036854775807i64));"
		        "print(len(\"\"));print(len(L\"abc\"));}",
		    "2147483647\n-2147483648\n-2147483647\n-9223372036854775807\n0\n3\n", Factory);
		Compare("int work(){array<int> a={4,5};int64 i=2i64;int total=0;"
		        "while(i>=0i64){total+=to_int(len(a));total+=a[i];i--;}return total;}"
		        "void main(){print(work());}",
		    "", Factory, true);
		Compare("int work(){array<int> values={1,2};array<int> alias=values;int total=0;"
		        "for(int64 i=0;i<len(values);i++){alias.push(to_int(i)+10);total+=values[i];"
		        "if(i==2i64)total+=values[99i64];}return total;}void main(){print(work());}",
		    "", Factory, true, true);
	}

	void ScalarClosurePaths(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		Compare("void main(){array<fn(int)->int> calls=array<fn(int)->int>();"
		        "for(int i=0;i<3;i++){int delta=i+2;int scale=i%2+1;"
		        "calls.push([delta,scale](int value)->int{return value*scale+delta;});}"
		        "int total=0;for(int i=0;i<6;i++){fn(int)->int call=calls[i%3];total+=call(i);}print(total);}",
		    "38\n", Factory);
		Compare("void main(){int delta=5;fn(int)->int call=[delta](int value)->int{return value+delta;};"
		        "delta=100;print(call(2));print(call(3));print(delta);}",
		    "7\n8\n100\n", Factory);
		Compare("void main(){int64 bias=9007199254740993i64;"
		        "fn(int64)->int64 wide=[bias](int64 value)->int64{return value+bias;};"
		        "double scale=1.25;fn(double)->double scaled=[scale](double value)->double{return value*scale;};"
		        "uint64 offset=100u64;fn(uint64)->uint64 unsigned_call=[offset](uint64 value)->uint64{return value+offset;};"
		        "bool enabled=true;fn(int)->int choose=[enabled](int value)->int{if(enabled)return value+1;return value-1;};"
		        "assert(scaled(3.0)==3.75);assert(unsigned_call(7u64)==107u64);print(wide(2i64));print(choose(4));}",
		    "9007199254740995\n5\n", Factory);
		Compare("void main(){int state=3;fn(int)->int call=[&state](int value)->int{state+=value;return state;};"
		        "state=7;print(call(2));print(call(3));print(state);}",
		    "9\n12\n12\n", Factory);
		Compare("struct Payload{public:int value=3;};void main(){Payload object=Payload();"
		        "fn(int)->int call=[object](int value)->int{return object.value+value;};"
		        "object.value=20;print(call(4));print(object.value);}",
		    "7\n20\n", Factory);
		Compare("void main(){int increment=1;fn(int)->int call=[increment](int value)->int{return value+increment;};"
		        "print(call(2147483647));}",
		    "", Factory, true);
		Compare("void main(){int divisor=0;fn(int)->int call=[divisor](int value)->int{return value/divisor;};"
		        "print(call(7));}",
		    "", Factory, true);
	}

	void NativeArrayInitializationCleanup(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const std::string Prefix = "int checked=0;int constructed=0;"
		                           "void deep(int n){if(n>0)deep(n-1);}"
		                           "class Owned{public:int id;Owned(){id=++constructed;print(id);}"
		                           "~Owned(){deep(40);print(id+10);}};";
		const std::string Fields = "struct Item{public:Owned value=Owned();int checked=check();};";
		const std::string ExpectedCleanup = "1\n2\n3\n13\n12\n11\n";
		const std::string Fatal = Prefix + "int check(){checked++;assert(checked<3,\"native array initialization stopped\");return checked;}" + Fields + "void main(){Item values[4];print(99);}";
		Compare(Fatal, ExpectedCleanup, Factory, true);
		auto FatalCompiled = VeroScriptCompile::CompileSource(Fatal, "native-array-initialization-fault.vs");
		Require(FatalCompiled.bSuccess, FatalCompiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> FatalModule = std::move(FatalCompiled.Module);
		const auto FatalResult = Run(FatalModule, EExecutionBackend::Vm, {}, 0);
		Require(FatalResult.Error.find("native array initialization stopped") != std::string::npos,
		    "partial native-array cleanup replaced the original runtime fault");
		for (const auto Declaration : {"Item values[4];", "Item values[2][2];"})
		{
			Compare(Prefix + "int check(){checked++;if(checked==3)throw 23;return checked;}" + Fields + "void main(){try{" + Declaration + "print(99);}catch(int value){print(value);print(checked);}}",
			    ExpectedCleanup + "23\n3\n", Factory);
		}
		Compare(Prefix + "int check(){checked++;if(checked==4)throw 23;return checked;}" + Fields + "void main(){try{Item values[2][2];print(99);}catch(int value){print(value);print(checked);}}",
		    "1\n2\n3\n4\n14\n13\n12\n11\n23\n4\n", Factory);
		for (const auto ExitCode : {0, 7})
		{
			auto Compiled = VeroScriptCompile::CompileSource(Prefix + "int check(){checked++;if(checked==3)exit(" + std::to_string(ExitCode) + ");return checked;}" + Fields + "void main(){Item values[4];print(99);}", "native-array-initialization-exit.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
			for (const auto Limit : {std::uint64_t{0}, std::uint64_t{100000}})
			{
				const auto Reference = Run(Module, EExecutionBackend::Vm, {}, Limit);
				Require(Reference.Success && Reference.Error.empty() && Reference.Output == ExpectedCleanup && Reference.ExitCode == ExitCode,
				    "initializer exit became a runtime fault, lost cleanup, or continued initialization: " + Reference.Output + "; " + Reference.Error);
				if (!Factory)
				{
					continue;
				}
				for (const auto Backend : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
				{
					const auto Result = Run(Module, Backend, Factory, Limit);
					Require(Result.Success && Result.Error.empty() && Result.Output == ExpectedCleanup && Result.ExitCode == ExitCode && (Limit == 0 || Result.Count == Reference.Count),
					    "backend changed initializer exit or partial native-array cleanup: " + Result.Output + "; " + Result.Error);
				}
			}
		}
	}

	void FrameReturnPaths(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		Compare("int add(int a,int b){return a+b;}int work(int n){int total=0;"
		        "for(int i=0;i<n;i++)total=add(total,i);return total;}"
		        "bool positive(int value){return value>0;}double twice(double value){return value*2.0;}"
		        "void main(){assert(positive(7));assert(!positive(-1));assert(twice(4.5)==9.0);print(work(40));}",
		    "780\n", Factory);
		Compare("int sum_pair(int a,int b){return a+b;}int single(int a){int b=0;return a+b;}"
		        "void main(){print(sum_pair(10,99));print(single(7));print(sum_pair(-3,5));print(single(-2));}",
		    "109\n7\n2\n-2\n", Factory);
		Compare("enum class Mode{Ready=3,Done=7};Mode identity(Mode value){Mode copy=value;return copy;}"
		        "string suffix(string value){string saved=value;value+=\"!\";assert(saved+\"!\"==value);return value;}"
		        "wstring wide_suffix(wstring value){wstring saved=value;value+=L\"!\";assert(saved+L\"!\"==value);return value;}"
		        "void main(){string text=\"012345678901234567890123456789\";string result=suffix(text);"
		        "assert(text==\"012345678901234567890123456789\");assert(result==text+\"!\");"
		        "string zero=\"a\\0b\";assert(suffix(zero)==\"a\\0b!\");assert(len(zero)==3);"
		        "wstring wide=L\"w\\0x\";assert(wide_suffix(wide)==L\"w\\0x!\");assert(len(wide)==3);"
		        "assert(identity(Mode::Ready)==Mode::Ready);assert(identity(Mode::Done)==Mode::Done);print(len(result));}",
		    "31\n", Factory);
		Compare("import \"string.vs\";string normalized(string value){"
		        "return std::strings::replace(std::strings::trim(value),\"xy\",\"AB\");}"
		        "void main(){string original=\"  xyxy  \";print(normalized(original));"
		        "assert(original==\"  xyxy  \");print(std::strings::remove_suffix(original,\"missing\"));}",
		    "ABAB\n  xyxy  \n", Factory);
		Compare("fn(int)->int bind(int amount){return [amount](int value)->int{return value+amount;};}"
		        "void main(){array<fn(int)->int> calls={bind(2),bind(3),bind(5)};int value=1;"
		        "for(int i=0;i<9;i++){fn(int)->int call=calls[i%3];value=call(value);}print(value);}",
		    "31\n", Factory);
		Compare("interface Operation{virtual int step(int value)=0;};"
		        "class A:public Operation{public:int state=1;int step(int value){state+=value;return state;}};"
		        "class B:public Operation{public:int state=10;int step(int value){state-=value;return state;}};"
		        "void main(){array<Operation> values={A(),B()};int total=0;"
		        "for(int i=0;i<4;i++){Operation value=values[i%2];total+=value.step(i+1);}print(total);}",
		    "19\n", Factory);
		Compare("int trace=0;class Item{public:int id;Item(int value){id=value;}"
		        "~Item(){trace=trace*10+id;}int value(int delta){return id+delta;}};"
		        "int use(){Item item=Item(3);int result=item.value(4);assert(trace==0);return result;}"
		        "void main(){print(use());print(trace);print(Item(4).value(5));print(trace);}",
		    "7\n3\n9\n34\n", Factory);
		Compare("int trace=0;int selected(int value){try{if(value<0)throw 31;return value+1;}"
		        "finally{trace=trace*10+2;}}int caught(){try{return selected(-1);}catch(int error){return error+1;}}"
		        "int& forward(int& value){return value;}void bump(ref int value){value+=3;}"
		        "void main(){int value=5;ref int alias=forward(&value);bump(ref alias);print(value);"
		        "print(selected(4));print(caught());print(trace);}",
		    "8\n5\n32\n22\n", Factory);
		Compare("int depth=0;int recurse(int count){if(count==0)return 1;return recurse(count-1)+1;}"
		        "class Item{public:~Item(){depth=recurse(80);}};"
		        "int work(){Item item=Item();return 7;}void main(){print(work());print(depth);}",
		    "7\n81\n", Factory);
		Compare("int trace=0;class Item{public:~Item(){trace=7;throw 41;}};"
		        "int work(){Item item=Item();return 3;}void main(){try{print(work());}"
		        "catch(int error){print(error);}finally{print(trace);}}",
		    "41\n7\n", Factory);
		auto ObserverCompiled = VeroScriptCompile::CompileSource(
		    "class Item{public:int value=0;int store(int input){value=input;return value;}};"
		    "void main(){Item item=Item();print(item.store(7));item.value=9;print(item.value);}",
		    "frame-return-observer.vs");
		Require(ObserverCompiled.bSuccess, ObserverCompiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> ObserverModule = std::move(ObserverCompiled.Module);
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			for (const auto Limit : {std::uint64_t{0}, std::uint64_t{100000}})
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(Backend);
				Vm.SetExecutionLimits({Limit, 0});
				if (Backend != EExecutionBackend::Vm)
				{
					std::string Error;
					Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(ObserverModule, Error), Error);
				}
				int Writes = 0;
				Vm.SetMutationCallback([&](const FMutationEvent& Event)
				{
					const auto* Value = Event.Value ? std::get_if<std::int32_t>(Event.Value) : nullptr;
					if (Event.Kind == EMutationKind::FieldWrite && Event.FieldName == "value" && Value && (*Value == 7 || *Value == 9))
					{
						Require(Event.Object != nullptr, "scalar method mutation lost the owning object");
						++Writes;
					}
				});
				Require(Vm.Execute(*ObserverModule, ERunMode::Script), Vm.GetError().Message);
				Require(Output.str() == "7\n9\n" && Writes == 2,
				    "scalar method return changed field mutation notifications");
			}
		}

		// A pooled frame must not reuse another module's field/method indices.
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			for (const bool ExtraField : {false, true})
			{
				const std::string Source = std::string("class Item{public:") + (ExtraField ? "int padding=99;int value=17;" : "int value=8;") + "int get(int delta){return value+delta;}};void main(){Item item=Item();print(item.get(1));}";
				auto Compiled = VeroScriptCompile::CompileSource(Source, "frame-cache-reuse.vs");
				Require(Compiled.bSuccess, Compiled.Error.Message);
				std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
				if (Backend != EExecutionBackend::Vm)
				{
					std::string Error;
					Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
				}
				Require(Vm.Execute(*Module, ERunMode::Script), Vm.GetError().Message);
			}
			Require(Output.str() == "9\n18\n", "pooled frame retained stale module cache indices (backend=" + std::to_string(static_cast<int>(Backend)) + ", output=" + Output.str() + ")");
		}
	}

	void BorrowedModuleCacheReuse(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		auto Scalar = VeroScriptCompile::CompileSource("void main(){print(7);}", "scalar-cache-reuse.vs");
		Require(Scalar.bSuccess, Scalar.Error.Message);
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			if (Backend != EExecutionBackend::Vm)
			{
				std::string Error;
				Require(Vm.SetJitBackendFactory(Factory, Error), Error);
			}
			// Replacing borrowed contents at one fixed address deterministically
			// reproduces allocator address reuse without relying on heap placement.
			FBytecodeModule Module;
			for (const bool ExtraField : {false, true, false})
			{
				const std::string Source = std::string("class Item{public:") + (ExtraField ? "int padding=99;int value=17;" : "int value=8;") + "int get(int delta){return value+delta;}};void main(){Item item=Item();print(item.get(1));}";
				auto Compiled = VeroScriptCompile::CompileSource(Source, "borrowed-module-reuse.vs");
				Require(Compiled.bSuccess, Compiled.Error.Message);
				Module = std::move(*Compiled.Module);
				Require(Vm.Execute(Module, ERunMode::Script), Vm.GetError().Message);
			}
			Require(Output.str() == "9\n18\n9\n", "borrowed module layout caches survived replacement (backend=" + std::to_string(static_cast<int>(Backend)) + ", output=" + Output.str() + ")");
			for (const bool ExtraField : {false, true, false})
			{
				const std::string Source = std::string("class Item{public:") + (ExtraField ? "int padding=99;int value=17;" : "int value=8;") + "virtual int get(int delta){return value+delta;}};void main(){Item item=Item();print(item.get(1));}";
				auto Compiled = VeroScriptCompile::CompileSource(Source, "interleaved-module-reuse.vs");
				Require(Compiled.bSuccess, Compiled.Error.Message);
				Module = std::move(*Compiled.Module);
				Require(Vm.Execute(Module, ERunMode::Script), Vm.GetError().Message);
				// First clear populated lookups, then cross an already-empty boundary.
				Require(Vm.Execute(*Scalar.Module, ERunMode::Script), Vm.GetError().Message);
				Require(Vm.Execute(*Scalar.Module, ERunMode::Script), Vm.GetError().Message);
			}
			Require(Output.str() == "9\n18\n9\n9\n7\n7\n18\n7\n7\n9\n7\n7\n",
			    "borrowed caches survived a dirty/clean boundary before address reuse");
		}
	}

	void OwnedModuleCacheReattachment(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			std::string Error;
			if (Backend != EExecutionBackend::Vm)
			{
				Require(Vm.SetJitBackendFactory(Factory, Error), Error);
			}
			auto Module = std::make_shared<FBytecodeModule>();
			for (const bool ExtraField : {false, true, false})
			{
				// Replace contents only after detaching the VM's immutable owner at
				// an idle boundary. No values survive these distinct programs.
				Require(Vm.SetJitProgramOwner(nullptr, Error), Error);
				const std::string Source = std::string("class Item{public:") + (ExtraField ? "int padding=99;int value=17;" : "int value=8;") + "int get(int delta){return value+delta;}};void main(){Item item=Item();print(item.get(1));}";
				auto Compiled = VeroScriptCompile::CompileSource(Source, "owned-module-reattachment.vs");
				Require(Compiled.bSuccess, Compiled.Error.Message);
				*Module = std::move(*Compiled.Module);
				Require(Vm.SetJitProgramOwner(Module, Error), Error);
				Require(Vm.Execute(*Module, ERunMode::Script), Vm.GetError().Message);
			}
			Require(Output.str() == "9\n18\n9\n", "owned module layout caches survived detach/reattach (backend=" + std::to_string(static_cast<int>(Backend)) + ", output=" + Output.str() + ")");
		}
	}

	void StringLocalAppend(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const auto CheckBorrowedAppend = [](auto Unit)
		{
			using TChar = decltype(Unit);
			using TString = std::basic_string<TChar>;
			using TView = std::basic_string_view<TChar>;
			for (const std::size_t Length : {0u, 1u, 7u, 15u, 16u, 31u, 64u, 257u})
			{
				TString Original(Length, Unit);
				for (std::size_t Index = 0; Index < Length; ++Index)
				{
					Original[Index] = static_cast<TChar>(Index % 193u);
				}
				for (const std::size_t Count : {std::size_t{0}, std::min(std::size_t{1}, Length), Length})
				{
					TString Actual = Original;
					const TString Saved = Actual;
					const TView Borrowed(Actual.data(), Count);
					const TString Suffix(Borrowed);
					TString Expected = Actual;
					Expected.append(Suffix);
					Runtime::AppendString(Actual, Borrowed);
					Require(Actual == Expected && Original == Saved,
					    "owned text append must preserve empty, single-unit, binary and self-aliasing snapshots");
				}
			}
			TString Growth(37, Unit);
			Growth.resize(Growth.capacity(), Unit);
			const TString Saved = Growth;
			const TView OneUnit(Growth.data(), 1);
			Runtime::AppendString(Growth, OneUnit);
			Require(Growth == Saved + TString(1, Unit), "single-unit alias must survive string capacity growth");
			Runtime::AppendString(Growth, TView{});
			Require(Growth == Saved + TString(1, Unit), "an empty null view must leave text unchanged");
		};
		CheckBorrowedAppend(char{'q'});
		CheckBorrowedAppend(char16_t{0xD83D});
		std::string Utf8;
		Runtime::AppendString(Utf8, std::string_view("\xC3", 1));
		Runtime::AppendString(Utf8, std::string_view("\xA9\0", 2));
		Require(Utf8 == std::string("\xC3\xA9\0", 3), "narrow append must retain UTF-8 bytes and embedded zeros");
		std::u16string Utf16;
		Runtime::AppendString(Utf16, std::u16string_view(u"\xD83D", 1));
		Runtime::AppendString(Utf16, std::u16string_view(u"\xDE42\0", 2));
		Require(Utf16 == std::u16string{0xD83D, 0xDE42, u'\0'}, "wide append must retain individual surrogate code units");
		FValue Text = std::string("a\0b", 3);
		const FValue Snapshot = Text;
		Require(Runtime::TryAppendStringValue(Text, Text) && std::get<std::string>(Text) == std::string("a\0ba\0b", 6) && std::get<std::string>(Snapshot) == std::string("a\0b", 3),
		    "local string self-append must preserve embedded zeros and value copies");
		const FValue WideSuffix = std::u16string{u'\0', 0xD83D, 0xDE42};
		FValue Wide = std::u16string{u'x'};
		Require(Runtime::TryAppendStringValue(Wide, WideSuffix) && Runtime::TryAppendStringValue(Wide, Wide) && std::get<std::u16string>(Wide) == std::u16string{u'x', u'\0', 0xD83D, 0xDE42, u'x', u'\0', 0xD83D, 0xDE42},
		    "wide string append must preserve all UTF-16 code units");
		const FValue SavedText = Text;
		const FValue SavedWide = Wide;
		Require(!Runtime::TryAppendStringValue(Text, Wide) && !Runtime::TryAppendStringValue(Wide, Text) && !Runtime::TryAppendStringValue(Text, FValue{std::int32_t{7}}) && std::get<std::string>(Text) == std::get<std::string>(SavedText) && std::get<std::u16string>(Wide) == std::get<std::u16string>(SavedWide),
		    "declining string append must not change the target");
		Compare("string work(string suffix){string text=\"a\";string saved=text;"
		        "for(int i=0;i<4;i++)text+=suffix;print(saved);return text;}"
		        "void main(){print(work(\"bc\"));string self=\"x\";"
		        "for(int i=0;i<3;i++)self+=self;print(self);}",
		    "a\nabcbcbcbc\nxxxxxxxx\n", Factory);
		Compare("wstring work(wstring suffix){wstring text=L\"a\";wstring saved=text;"
		        "for(int i=0;i<4;i++)text+=suffix;print(saved);return text;}"
		        "void main(){print(work(L\"bc\"));wstring self=L\"x\";"
		        "for(int i=0;i<3;i++)self+=self;print(self);}",
		    "a\nabcbcbcbc\nxxxxxxxx\n", Factory);
		Compare("string join(string suffix){string text=\"head\";string saved=text;"
		        "for(int i=0;i<3;i++)text=text+suffix;print(saved);return text;}"
		        "void main(){string suffix=\"abcdefghijklmnopqrstuvwxyz0123456789\";print(join(suffix));print(suffix);"
		        "string self=\"x\";for(int i=0;i<6;i++)self=self+self;print(len(self));}",
		    "head\nheadabcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789\n"
		    "abcdefghijklmnopqrstuvwxyz0123456789\n64\n", Factory);
		Compare("wstring join(wstring suffix){wstring text=L\"head\";wstring saved=text;"
		        "for(int i=0;i<3;i++)text=text+suffix;print(saved);return text;}"
		        "void main(){wstring suffix=L\"abcdefghijklmnopqrstuvwxyz0123456789\";print(join(suffix));print(suffix);"
		        "wstring self=L\"x\";for(int i=0;i<6;i++)self=self+self;print(len(self));}",
		    "head\nheadabcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789\n"
		    "abcdefghijklmnopqrstuvwxyz0123456789\n64\n", Factory);
		Compare("void grow(ref string target,ref string suffix){target+=suffix;}"
		        "struct Item{string text;};void main(){string s=\"a\";grow(ref s,ref s);print(s);"
		        "string suffix=\"yz\";array<string> a={\"x\"};grow(ref a[0],ref suffix);print(a[0]);"
		        "Item item=Item();item.text=\"q\";grow(ref item.text,ref suffix);print(item.text);print(suffix);}",
		    "aa\nxyz\nqyz\nyz\n", Factory);
	}

	void StringLocalAppendMutationObserver(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "void grow(ref string target,ref string suffix){target+=suffix;}"
		    "class AppendBox{public:string text=\"a\";array<string> items=array<string>();"
		    "AppendBox(){items.push(\"x\");}};"
		    "void main(){AppendBox box=AppendBox();string suffix=\"!\";string empty=\"\";"
		    "grow(ref box.text,ref suffix);grow(ref box.text,ref empty);"
		    "grow(ref box.items[0],ref suffix);print(box.text);print(box.items[0]);}",
		    "string-local-append-observer.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		bool HasLocalAppend = false;
		for (const auto& Function : Module->Functions)
		{
			if (Function.Name.find("grow#") == std::string::npos)
			{
				continue;
			}
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				const auto Size = Bytecode::InstructionSize(Op);
				Require(Size != 0 && Size <= Function.Code.size() - Ip, "invalid observer fixture bytecode");
				HasLocalAppend = HasLocalAppend || Op == EOpcode::AddLocalLocal;
				Ip += Size;
			}
		}
		Require(HasLocalAppend, "observer fixture must exercise local-to-local append");
		for (const auto Limit : {std::uint64_t{0}, std::uint64_t{100000}})
		{
			for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
			{
				if (Mode != EExecutionBackend::Vm && !Factory)
				{
					continue;
				}
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(Mode);
				Vm.SetExecutionLimits({Limit, 0});
				if (Mode != EExecutionBackend::Vm)
				{
					std::string Error;
					Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
				}
				int TextWrites = 0;
				int ItemSets = 0;
				bool EventHasObject = true;
				Vm.SetMutationCallback([&](const FMutationEvent& Event)
				{
					if (Event.FieldName == "text" && Event.Kind == EMutationKind::FieldWrite)
					{
						// Ignore constructor initialization. Both the real suffix
						// and the empty append must notify after the successful write.
						const auto* Text = Event.Value ? std::get_if<std::string>(Event.Value) : nullptr;
						if (Text && *Text == "a!")
						{
							++TextWrites;
							EventHasObject = EventHasObject && Event.Object != nullptr;
						}
					}
					if (Event.FieldName == "items" && Event.Kind == EMutationKind::ContainerSet)
					{
						++ItemSets;
						EventHasObject = EventHasObject && Event.Object != nullptr && Event.Value != nullptr;
					}
				});
				Require(Vm.Execute(*Module, ERunMode::Script), Vm.GetError().Message);
				Require(Output.str() == "a!\nx!\n", "observer append changed the stored text");
				Require(TextWrites == 2 && ItemSets == 1 && EventHasObject,
				    "string local append must retain one field/container event per successful ref mutation, including empty append");
			}
		}
	}

	void StringReadAndFormattingBoundaries(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const std::string HeapSuffix(128u, 'h');
		const auto IdentitySource = std::string(
			"int calls=0;string make(){calls++;return \"a\\0b\";}string stored=make();"
			"int work(int n,string suffix){string text=\"\";string saved=suffix;"
			"for(int i=0;i<n;i++)text+=to_string(suffix);assert(suffix==saved);return to_int(len(text));}"
			"int global_work(int n){string suffix=stored;string text=\"\";"
			"for(int i=0;i<n;i++)text+=to_string(suffix);assert(suffix==stored);return to_int(len(text));}"
			"void main(){print(work(-1,\"a\"));print(work(0,\"\"));print(work(1,\"a\\0b\"));"
			"print(work(4,\"abcdefgh\"));print(work(32,\"") + HeapSuffix +
			"\"));print(global_work(0));print(global_work(1));print(global_work(4));print(global_work(32));"
			"string self=\"x\\0\";string saved=self;for(int i=0;i<3;i++)self+=to_string(self);"
			"print(len(self));print(len(saved));assert(saved==\"x\\0\");"
			"string left=\"A\";string right=\"B\";left+=to_string(right);right+=\"!\";print(left);print(right);print(calls);}";
		auto IdentityCompiled = VeroScriptCompile::CompileSource(IdentitySource, "string-identity-append.vs");
		Require(IdentityCompiled.bSuccess, IdentityCompiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> IdentityModule = std::move(IdentityCompiled.Module);
		for (const auto Limit : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{12}, std::uint64_t{100000}})
		{
			const auto Canonical = Run(IdentityModule, EExecutionBackend::Vm, {}, Limit, 0, true);
			if (!Limit)
			{
				Require(Canonical.Success && Canonical.Output == "0\n0\n3\n32\n4096\n0\n3\n12\n96\n16\n2\nAB\nB!\n1\n",
					"canonical exact-string format fixture changed snapshots or global initialization");
			}
			for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
			{
				if (Mode != EExecutionBackend::Vm && !Factory)
				{
					continue;
				}
				const auto Fast = Run(IdentityModule, Mode, Factory, Limit);
				Require(Fast.Success == Canonical.Success && Fast.Output == Canonical.Output && Fast.Error == Canonical.Error &&
					Fast.Line == Canonical.Line && Fast.Column == Canonical.Column && (!Limit || Fast.Count == Canonical.Count),
					"exact-string append preserves parameter/global/self snapshots and governed diagnostics/counts");
			}
		}
		Compare("void grow(ref string text,ref string suffix){text+=to_string(suffix);}"
			"void main(){string text=\"a\";grow(ref text,ref text);print(text);"
			"string suffix=\"b\\0c\";grow(ref text,ref suffix);print(len(text));assert(suffix==\"b\\0c\");}",
			"aa\n5\n", Factory);
		Compare("void main(){string text=\"a\";string suffix=\"b\";try{"
			"for(int i=0;i<3;i++)text+=to_string(suffix);print(text);int zero=0;print(1/zero);"
			"}finally{print(text);}}", "abbb\nabbb\n", Factory, true);
		// Exact string identity never used the per-operation conversion/text
		// service limits. Keep that behavior even when the fused output exceeds it.
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Mode != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Mode);
			Vm.SetStandardLibraryLimits({1u, 0u, 0u});
			if (Mode != EExecutionBackend::Vm)
			{
				std::string Error;
				Require(Vm.SetJitBackendFactory(Factory, Error), Error);
			}
			Require(Vm.Execute(*IdentityModule, ERunMode::Script) && Output.str() == "0\n0\n3\n32\n4096\n0\n3\n12\n96\n16\n2\nAB\nB!\n1\n",
				"exact string identity keeps canonical standard-library limit behavior");
		}
		auto WideCompiled = VeroScriptCompile::CompileSource(
			"wstring stored=L\"A\\0\\u4e2d\";"
			"int work(int n,wstring suffix){wstring saved=suffix;string text=\"\";"
			"for(int i=0;i<n;i++)text+=to_string(suffix);assert(suffix==saved);return to_int(len(text));}"
			"int global_work(int n){wstring suffix=stored;string text=\"\";"
			"for(int i=0;i<n;i++)text+=to_string(suffix);assert(suffix==stored);return to_int(len(text));}"
			"void main(){print(work(0,stored));print(work(1,stored));print(work(4,stored));print(work(32,stored));"
			"print(global_work(0));print(global_work(1));print(global_work(4));print(global_work(32));}", "wstring-conversion-append.vs");
		Require(WideCompiled.bSuccess, WideCompiled.Error.Message);
		// Preserve arbitrary UTF-16 code units through the canonical codec too;
		// this must not accidentally become a wide-string byte identity.
		const std::u16string OriginalWide{u'A', u'\0', 0x4e2d};
		auto WideValue = OriginalWide;
		WideValue.append({0xd83d, 0xde42, 0xd800, u'!', 0xdc00});
		for (auto &Constant : WideCompiled.Module->WStringPool)
		{
			if (Constant == OriginalWide)
			{
				Constant = WideValue;
			}
		}
		const auto Bytes = Runtime::ValueToString(FValue{WideValue}).size();
		const auto WideExpected = std::string("0\n") + std::to_string(Bytes) + "\n" + std::to_string(Bytes * 4u) + "\n" +
			std::to_string(Bytes * 32u) + "\n0\n" + std::to_string(Bytes) + "\n" + std::to_string(Bytes * 4u) + "\n" + std::to_string(Bytes * 32u) + "\n";
		std::shared_ptr<const FBytecodeModule> WideModule = std::move(WideCompiled.Module);
		for (const auto Limit : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{12}, std::uint64_t{100000}})
		{
			const auto Canonical = Run(WideModule, EExecutionBackend::Vm, {}, Limit, 0, true);
			if (!Limit)
			{
				Require(Canonical.Success && Canonical.Output == WideExpected, "canonical UTF-16 format fixture changed");
			}
			for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
			{
				if (Mode != EExecutionBackend::Vm && !Factory)
				{
					continue;
				}
				const auto Fast = Run(WideModule, Mode, Factory, Limit);
				Require(Fast.Success == Canonical.Success && Fast.Output == Canonical.Output && Fast.Error == Canonical.Error &&
					Fast.Line == Canonical.Line && Fast.Column == Canonical.Column && (!Limit || Fast.Count == Canonical.Count),
					"wide append retains canonical UTF-16 conversion, source snapshots and execution controls");
			}
		}
		Compare("void grow(ref string text,ref wstring suffix){text+=to_string(suffix);}"
			"void main(){string text=\"a\";wstring suffix=L\"b\\0c\";grow(ref text,ref suffix);print(len(text));"
			"assert(suffix==L\"b\\0c\");}", "4\n", Factory);
		Compare(std::string("int work(wstring suffix){wstring saved=suffix;string text=\"\";"
			"for(int i=0;i<4;i++)text+=to_string(suffix);assert(saved==suffix);return to_int(len(text));}"
			"void main(){print(work(L\"") + HeapSuffix + "\"));print(work(L\"a\"));}", "512\n4\n", Factory);
		Compare("void main(){string text=\"a\";wstring suffix=L\"b\";try{"
			"for(int i=0;i<3;i++)text+=to_string(suffix);print(text);int zero=0;print(1/zero);"
			"}finally{print(text);}}", "abbb\nabbb\n", Factory, true);
		for (const auto& Module : {IdentityModule, WideModule})
		{
			std::vector<std::string> ExpectedEvents;
			std::string ExpectedOutput;
			for (const bool Canonical : {true, false})
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetStandardLibraryLimits({1u, 0u, 0u});
				if (Canonical)
				{
					Vm.SetExecutionCheck([]()
						{
							return true;
						});
				}
				std::vector<std::string> Events;
				Vm.SetMutationCallback([&](const FMutationEvent& Event)
				{
					Events.push_back(std::to_string(static_cast<unsigned>(Event.Kind)) + ":" + std::string(Event.FieldName) + ":" +
						(Event.Value ? Runtime::ValueToString(*Event.Value) : std::string{}));
				});
				Require(Vm.Execute(*Module, ERunMode::Script), "observed string-format append failed: " + Vm.GetError().Message);
				if (Canonical)
				{
					ExpectedOutput = Output.str();
					ExpectedEvents = std::move(Events);
				}
				else
				{
					Require(Output.str() == ExpectedOutput && Events == ExpectedEvents,
						"text-format append preserves observer events and canonical conversion limits");
				}
			}
		}
		// Exercise public queries through ordinary values, global and ref storage.
		// Later arguments that replace the string must observe a by-value snapshot.
		const std::string Queries =
		    "import \"string.vs\";string global=\"xyz\";"
		    "int read(ref string text,int64 index){return std::strings::char_code(text,index);}"
		    "int replace(ref string text){text=\"z\";return 0;}"
		    "string identity(string text){return text;}"
		    "void main(){string text=\"a\\0\xC3\xA9\";int64 index=2;"
		    "print(std::strings::char_code(text,0));print(read(ref text,1));print(read(ref text,index));"
		    "print(std::strings::char_code(text,3));string saved=identity(text);"
		    "print(std::strings::char_code(text,replace(ref text)));print(text);"
		    "print(std::strings::char_code(saved,1));print(std::strings::char_code(saved,2));"
		    "print(std::strings::char_code(global,1));}";
		Compare(Queries, "97\n0\n195\n169\n97\nz\n0\n195\n121\n", Factory);
		Compare("string identity(string value){return value;}wstring wide_identity(wstring value){return value;}"
		        "void main(){string text=\"a\\0b\";string copy=identity(text);text=identity(text);"
		        "text+=\"!\";assert(copy==\"a\\0b\");print(len(text));"
		        "wstring wide=L\"x\\0y\";wstring saved=wide_identity(wide);wide=wide_identity(wide);"
		        "wide+=L\"!\";assert(saved==L\"x\\0y\");print(len(wide));}", "4\n4\n", Factory);
		Compare("int mutate(ref string text){text=\"changed\";return 7;}"
		        "void main(){string text=\"\";for(int i=0;i<4;i++)text+=to_string(i-2);"
		        "print(text);string old=\"old\";old+=to_string(mutate(ref old));print(old);"
		        "int64 low=-9223372036854775807i64-1i64;int64 high=9223372036854775807i64;"
		        "string wide=\"\";wide+=to_string(low);wide+=\";\";wide+=to_string(high);print(wide);}",
		    "-2-101\nold7\n-9223372036854775808;9223372036854775807\n", Factory);
		Compare("int calls=0;string mutate(ref string text){calls++;text=\"changed\";return \"tail\";}"
		        "wstring mutate_wide(ref wstring text){calls++;text=L\"changed\";return L\"wide\";}"
		        "void main(){string chunk=\"ab\\0c\";wstring wide=L\"xy\\0z\";string text=\"\";"
		        "text+=to_string(chunk);assert(text==chunk);print(len(text));"
		        "text=\"\";text+=to_string(wide);assert(text==to_string(wide));print(len(text));"
		        "string suffix=\"!\";text=\"\";text+=to_string(chunk+suffix);assert(text==chunk+suffix);print(len(text));"
		        "array<string> parts={chunk};array<wstring> wide_parts={wide};"
		        "text=\"\";text+=to_string(parts[0]);assert(text==chunk);print(len(text));"
		        "text=\"\";text+=to_string(wide_parts[0]);assert(text==to_string(wide));print(len(text));"
		        "text=\"old\";text+=to_string(mutate(ref text));print(text);"
		        "text=\"start\";text+=to_string(mutate_wide(ref wide));print(text);"
		        "assert(wide==L\"changed\");assert(chunk==\"ab\\0c\");print(calls);}",
		    "4\n4\n5\n4\n4\noldtail\nstartwide\n2\n", Factory);
		for (const auto& Index : {"-1", "3", "2147483648i64", "9223372036854775807i64"})
		{
			Compare("import \"string.vs\";void main(){string text=\"abc\";print(7);try{print(std::strings::char_code(text," +
			        std::string(Index) + "));}finally{print(9);}}", "7\n9\n", Factory, true);
		}
		for (const auto& Source : {
		         "import \"string.vs\";void main(){string text=\"abc\";print(std::strings::char_code(text,0));}",
		         "string identity(string text){return text;}void main(){string text=\"abc\";text=identity(text);print(text);}"})
		{
			auto Compiled = VeroScriptCompile::CompileSource(Source, "borrowed-text-call-depth.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
			for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
			{
				if (Mode != EExecutionBackend::Vm && !Factory)
				{
					continue;
				}
				for (const auto Depth : {std::size_t{1}, std::size_t{2}})
				{
					const auto Fast = Run(Module, Mode, Factory, 0, Depth);
					const auto Canonical = Run(Module, Mode, Factory, 0, Depth, true);
					// Native call-depth preflight can retain the call-setup location;
					// the VM borrowing path must retain the exact canonical call site.
					Require(Fast.Success == (Depth == 2u) && Fast.Success == Canonical.Success &&
					        Fast.Output == Canonical.Output && Fast.Error == Canonical.Error &&
					        Fast.Line == Canonical.Line &&
					        (Mode != EExecutionBackend::Vm || Fast.Column == Canonical.Column),
					    "borrowed string queries and identity leaves retain logical call-depth boundaries; mode=" +
					        std::to_string(static_cast<int>(Mode)) + " depth=" + std::to_string(Depth) +
					        " fast=" + std::to_string(Fast.Success) + " canonical=" + std::to_string(Canonical.Success) +
					        " errors=" + Fast.Error + "/" + Canonical.Error +
					        " locations=" + std::to_string(Fast.Line) + ":" + std::to_string(Fast.Column) + "/" +
					        std::to_string(Canonical.Line) + ":" + std::to_string(Canonical.Column) +
					        " output=" + Fast.Output + "/" + Canonical.Output + " source=" + Source);
				}
			}
		}
	}

	void GrowingArrayFailure(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "int work(array<int> values){array<int> alias=values;int total=0;"
		    "for(int64 i=0;i<len(values);i++){alias.push(to_int(i)+10);total+=values[i];"
		    "if(i==2i64)total+=values[99i64];}return total;}",
		    "growing-array-failure.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		std::uint16_t Function = 0;
		while (Function < Module->Functions.size() && Module->Functions[Function].Name.find("work#") == std::string::npos)
		{
			++Function;
		}
		Require(Function < Module->Functions.size(), "missing growing-array function");
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Mode != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			auto Values = std::make_shared<FDynamicArray>();
			Values->TypeName = "array<int>";
			Values->ElementTypeName = "int";
			Values->ScalarElements = std::vector<std::int32_t>{1, 2};
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Mode);
			if (Mode != EExecutionBackend::Vm)
			{
				std::string Error;
				Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
			}
			Require(!Vm.ExecuteFunction(*Module, Function, {Values}), "invalid index must fail");
			Require(std::get<std::vector<std::int32_t>>(Values->ScalarElements) == std::vector<std::int32_t>{1, 2, 10, 11, 12},
			    "length/push failure lost or duplicated preceding array writes");
		}
	}

	void ArgumentCleanup(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		for (const bool HeapArguments : {false, true})
		{
			for (const bool MultipleExceptions : {false, true})
			{
				const std::string Source = std::string("int trace=0;int caught=0;class Item{public:int id;") + "Item(int value){id=value;}~Item(){trace=trace*10+id;if(id==2)throw 66;" + (MultipleExceptions ? "if(id==1)throw 77;" : "") + "}};Item make(int id){return Item(id);}void take(Item first,Item second" + (HeapArguments ? ",int a,int b,int c,int d,int e,int f,int g" : "") + "){}void fail(){take(make(1),make(2)" + (HeapArguments ? ",0,0,0,0,0,0,0" : "") + ");}void main(){try{fail();}catch(int error){caught=error;}print(trace);print(caught);}";
				Compare(Source, MultipleExceptions ? "21\n77\n" : "21\n66\n", Factory);
				if (Factory && (Factory->GetDescription().Capabilities & JitControlledExecutionCapability) != 0)
				{
					auto Compiled = VeroScriptCompile::CompileSource(Source, "argument-cleanup.vs");
					Require(Compiled.bSuccess, Compiled.Error.Message);
					std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
					const auto Result = Run(Module, EExecutionBackend::Jit, Factory, 100000);
					Require(Result.Success && Result.Stats.ControlledNativeExecutions != 0,
					    "argument cleanup fixture must enter controlled native code");
				}
			}
		}
	}

	void PartialEffects(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "int64 work(array<int> values){int64 total=9223372036854775806i64;"
		    "for(int64 i=0;i<3;i++){values[to_int(i)%2]=to_int(i)+10;total=total+1;}return total;}",
		    "wide-partial-effects.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		std::uint16_t Function = 0;
		while (Function < Module->Functions.size() && Module->Functions[Function].Name.find("work#") == std::string::npos)
		{
			++Function;
		}
		Require(Function < Module->Functions.size(), "missing partial-effects function");
		FResult Reference;
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Mode != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			auto Values = std::make_shared<FDynamicArray>();
			Values->TypeName = "array<int>";
			Values->ElementTypeName = "int";
			Values->ScalarElements = std::vector<std::int32_t>{1, 2};
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Mode);
			if (Mode != EExecutionBackend::Vm)
			{
				std::string Error;
				Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
			}
			Require(!Vm.ExecuteFunction(*Module, Function, {Values}), "overflow must fail");
			Require(std::get<std::vector<std::int32_t>>(Values->ScalarElements) == std::vector<std::int32_t>{10, 11},
			    "failure lost, duplicated or advanced preceding array writes");
			if (Mode == EExecutionBackend::Vm)
			{
				Reference.Error = Vm.GetError().Message;
				Reference.Line = Vm.GetError().Location.Line;
				Reference.Column = Vm.GetError().Location.Column;
			}
			else
			{
				Require(Vm.GetError().Message == Reference.Error && Vm.GetError().Location.Line == Reference.Line && Vm.GetError().Location.Column == Reference.Column, "partial failure source differs from VM");
				if ((Factory->GetDescription().Capabilities & JitNativeTypedLoopsCapability) != 0)
				{
					Require(Vm.GetJitStats().NativeTypedLoopExecutions != 0, "partial-effects fixture did not enter native typed code");
				}
			}
		}
	}

	void NativeValueLeaves(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		Compare("import \"memory.vs\";void main(){std::memory::buffer b=std::memory::buffer(3);"
		        "b.set(0,17);b.set(1,201);auto r=b.readonly_slice(0,2);"
		        "print(b.size());print(r.size());print(b.get(1));print(r.get(0));}",
		    "3\n2\n201\n17\n", Factory);
		Compare("import \"memory.vs\";void main(){std::memory::buffer b=std::memory::buffer(1);"
		        "print(b.size());print(b.get(9223372036854775807i64));}",
		    "1\n", Factory, true);
		Compare("import \"memory.vs\";void main(){std::memory::buffer b=std::memory::buffer(2);"
		        "auto view=b.slice(0,1);b.resize(3);print(view.get(0));}",
		    "", Factory, true);
		Compare("import \"bigint.vs\";import \"rational.vs\";void main(){"
		        "std::bigint x=std::bigint(123);std::bigint saved=x;x=x*std::bigint(97);"
		        "print(x.to_string());print(saved.to_string());"
		        "std::rational r=std::rational(2,3);print((r+r).to_string());}",
		    "11931\n123\n4/3\n", Factory);
		Compare("import \"bigint.vs\";void main(){std::bigint n=std::bigint(17);"
		        "print(n.to_string());print((n/std::bigint(0)).to_string());}",
		    "17\n", Factory, true);
	}

	void ShortReadSelection(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		if (!Factory || Factory->GetDescription().Id != "asmjit") return;
		auto Compiled = VeroScriptCompile::CompileSource(
		    "import \"attribute.vs\";int identity(int value){return value;}int add(int a,int b){return a+b;}"
		    "class Reader{public:int value=3;int sum(int a,int b)const{return a+b;}"
		    "int state(int x)const{int a=value;a=(a+x)%65521;a=(a+x)%65521;a=(a+x)%65521;a=(a+x)%65521;"
		    "a=(a+x)%65521;a=(a+x)%65521;a=(a+x)%65521;a=(a+x)%65521;return a;}"
		    "int forward(int a,int b)const{return sum(a,b);}};"
		    "Reader make_reader(){return Reader();}"
		    "int read_attribute(){std::attribute<int> value(7);return value();}",
		    "short-read.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		FJitProgramView View{Module.get(), Module, {AllocateJitProgramIdentity(), 1, 1}};
		std::string Error;
		auto Cache = FJitBackendCache::Create(Factory, View, {}, {}, ~std::uint64_t{0}, Error);
		Require(static_cast<bool>(Cache), Error);
		for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
		{
			const auto& Function = Module->Functions[Index];
			if (Function.Name.find("identity#") == std::string::npos && Function.Name.find("add#") == std::string::npos && Function.Name.find("Reader::sum#") == std::string::npos && Function.Name.find("Reader::state#") == std::string::npos && Function.Name.find("Reader::forward#") == std::string::npos && Function.Name.find("attribute#3:int::operator()#") == std::string::npos)
			{
				continue;
			}
			Require(Cache->EnsureCompiled(static_cast<std::uint16_t>(Index)) == EJitCompilationResult::Ready,
			    "compile short read selection probe");
			const auto Candidate = Cache->Find(static_cast<std::uint16_t>(Index));
			const bool PreferVm = Function.Name.find("identity#") != std::string::npos || Function.Name.find("attribute#3:int::operator()#") != std::string::npos || (!Test::SupportsAsmJitControlFlow(Factory->GetDescription()) && Function.Name.find("Reader::forward#") != std::string::npos);
			Require(Candidate && Candidate->bPreferVmForExecution == PreferVm,
			    "short reads and boxed wrappers prefer VM while scalar forwarding retains its native plan: " + Function.Name);
			Require(Candidate->bPreferVmForNestedExecution == (Test::SupportsAsmJitControlFlow(Factory->GetDescription()) && Function.Name.find("Reader::state#") != std::string::npos),
			    "bounded receiver preference is scoped to nested interpreter calls: " + Function.Name);
		}
		if (Test::SupportsAsmJitControlFlow(Factory->GetDescription()))
		{
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(EExecutionBackend::Jit);
			Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
			std::uint16_t Make = 0xFFFF;
			for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
			{
				if (Module->Functions[Index].Name.find("make_reader#") != std::string::npos)
				{
					Make = static_cast<std::uint16_t>(Index);
				}
			}
			FValue Object;
			Require(Make != 0xFFFF && Vm.ExecuteFunction(*Module, Make, {}, nullptr, &Object), Vm.GetError().Message);
			FValue Result;
			Require(Vm.ExecuteInstanceMethod(*Module, std::get<FObjectHandle>(Object), "state",
			            {std::int32_t{7}}, nullptr, nullptr, &Result),
			    Vm.GetError().Message);
			const auto Stats = Vm.GetJitStats();
			Require(std::get<std::int32_t>(Result) == 59 && Stats.NativeCfgEntries + Stats.NativeLeafEntries != 0,
			    "standalone receiver invocation must retain native execution");
		}
	}

	void NativeProviderOwnership(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		struct FProviderState
		{
			FHostObjectHandle Value;
			std::shared_ptr<const void> Owner = std::make_shared<int>(7);
		} State;
		std::vector<FNativeFunction> Natives(2);
		Natives[0].Name = "supply_buffer";
		Natives[0].ReturnType = ENativeType::HostObject;
		Natives[0].ReturnHostTypeName = "std.memory.buffer";
		Natives[0].UserData = &State;
		Natives[0].Callback = [](const FNativeCallContext& Context, FValue& Out, std::string& Error)
		{
			auto& Provider = *static_cast<FProviderState*>(Context.UserData);
			const auto Result = Memory::CreateBuffer(Context.MemoryServices->GetMemoryContext(), 3, Out,
			    Context.MemoryServices->GetMemoryWork());
			if (!Result)
			{
				Error = Result.MessageId;
				return false;
			}
			Provider.Value = std::get<FHostObjectHandle>(Out);
			return true;
		};
		Natives[1].Name = "restamp_buffer";
		Natives[1].UserData = &State;
		Natives[1].Callback = [](const FNativeCallContext& Context, FValue&, std::string&)
		{
			auto& Provider = *static_cast<FProviderState*>(Context.UserData);
			Provider.Value->ProviderIdentity = Provider.Owner;
			return true;
		};
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Natives = &Natives;
		auto Compiled = VeroScriptCompile::CompileSource(
		    "import \"memory.vs\";void main(){std::memory::buffer value=supply_buffer();"
		    "restamp_buffer();print(value.size());}",
		    "provider-ownership.vs", Options);
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend != EExecutionBackend::Vm && !Factory)
			{
				continue;
			}
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			if (Backend != EExecutionBackend::Vm)
			{
				std::string Error;
				Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
			}
			Require(!Vm.Execute(*Module, ERunMode::Script, {}, &Natives) && Output.str().empty() && Vm.GetError().Message.find("native registration mismatch") != std::string::npos,
			    "native leaf lost provider ownership validation");
		}
	}

	void IndirectArgumentCleanup(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		for (const int ArgumentCount : {2, 8, 9})
		{
			std::string ExtraParameters, ExtraTypes, ExtraArguments;
			for (int Index = 2; Index < ArgumentCount; ++Index)
			{
				ExtraParameters += ",int a" + std::to_string(Index);
				ExtraTypes += ",int";
				ExtraArguments += ",0";
			}
			const std::string Source = "int trace=0;class Item{public:int id;Item(int n){id=n;}"
			                           "~Item(){trace=trace*10+id;}};Item make(int id){return Item(id);}"
			                           "void take(Item first,Item second" +
			    ExtraParameters + "){}"
			                      "void main(){fn(Item,Item" +
			    ExtraTypes + ")->void call=take;"
			                 "call(make(1),make(2)" +
			    ExtraArguments + ");print(trace);"
			                     "auto no_args=[]()->int{return 29;};print(no_args());}";
			Compare(Source, "21\n29\n", Factory);
		}
	}

	void AdjacentLocalJumpBoundaries()
	{
		for (const auto Immediate : {false, true})
		{
			for (const auto Offset : {0, 100})
			{
				for (const auto Truncated : {false, true})
				{
					for (const auto Input : {7, std::numeric_limits<std::int32_t>::max()})
					{
						auto Compiled = VeroScriptCompile::CompileSource("int work(int value){return value;}", "adjacent-jump.vs");
						Require(Compiled.bSuccess, Compiled.Error.Message);
						std::uint16_t FunctionIndex = 0;
						while (FunctionIndex < Compiled.Module->Functions.size() && Compiled.Module->Functions[FunctionIndex].Name.find("work#") == std::string::npos)
						{
							++FunctionIndex;
						}
						Require(FunctionIndex < Compiled.Module->Functions.size(), "missing adjacent jump fixture");
						auto& Function = Compiled.Module->Functions[FunctionIndex];
						Function.Code.clear();
						Function.Code.push_back(static_cast<std::uint8_t>(Immediate ? EOpcode::AddLocalImm : EOpcode::IncLocal));
						Bytecode::WriteU16LE(Function.Code, 0);
						if (Immediate)
						{
							Bytecode::WriteI32LE(Function.Code, 1);
						}
						const auto JumpOffset = Function.Code.size();
						Function.Code.push_back(static_cast<std::uint8_t>(EOpcode::Jmp));
						Bytecode::WriteI32LE(Function.Code, Offset);
						Function.Code.push_back(static_cast<std::uint8_t>(EOpcode::LoadLocal));
						Bytecode::WriteU16LE(Function.Code, 0);
						Function.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
						if (Truncated)
						{
							Function.Code.resize(JumpOffset + 1u);
						}
						Function.DebugLines.resize(Function.Code.size());
						for (std::size_t Index = 0; Index < Function.DebugLines.size(); ++Index)
						{
							Function.DebugLines[Index] = {static_cast<std::uint32_t>(Index + 10u), 2, 0};
						}
						FResult Reference;
						for (const auto Limit : {std::uint64_t{1000}, std::uint64_t{0}})
						{
							std::ostringstream Output;
							VeroScriptVm Vm(Output);
							VeroScript::Test::Configure(Vm);
							Vm.SetExecutionBackend(EExecutionBackend::Vm);
							Vm.SetExecutionLimits({Limit, 0});
							FValue Value;
							FResult Result;
							Result.Success = Vm.ExecuteFunction(*Compiled.Module, FunctionIndex, {std::int32_t{Input}}, nullptr, &Value);
							if (Result.Success)
							{
								Require(Input == 7 && Offset == 0 && !Truncated && std::holds_alternative<std::int32_t>(Value) && std::get<std::int32_t>(Value) == 8,
								    "adjacent update and jump result");
							}
							else
							{
								Require(Input != 7 || Offset != 0 || Truncated, "valid adjacent jump failed");
								Result.Error = Vm.GetError().Message;
								Result.Line = Vm.GetError().Location.Line;
								Result.Column = Vm.GetError().Location.Column;
							}
							if (Limit != 0)
							{
								Reference = Result;
							}
							else
							{
								Require(Result.Success == Reference.Success && Result.Error == Reference.Error && Result.Line == Reference.Line && Result.Column == Reference.Column,
								    "adjacent jump differs from canonical governed execution: immediate=" + std::to_string(Immediate) + ", offset=" + std::to_string(Offset) + ", truncated=" + std::to_string(Truncated) + ", input=" + std::to_string(Input) + "; " + Result.Error + " @" + std::to_string(Result.Line) + ":" + std::to_string(Result.Column) + " / " + Reference.Error + " @" + std::to_string(Reference.Line) + ":" + std::to_string(Reference.Column));
							}
						}
					}
				}
			}
		}
	}

	void FusedExpressionFailureLocation()
	{
		for (const bool StringLength : {false, true})
		{
			const std::string Source = StringLength
			    ? "int64 work(int x){\nstring text=\"abc\";\nreturn len(\ntext\n);\n}"
			    : "int work(int x){\nint y=0;\ny=\nx\n+3;\nreturn y;\n}";
			auto Compiled = VeroScriptCompile::CompileSource(Source, "fused-location.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::uint16_t Index = 0;
			while (Index < Compiled.Module->Functions.size() && Compiled.Module->Functions[Index].Name.find("work#") == std::string::npos)
			{
				++Index;
			}
			Require(Index < Compiled.Module->Functions.size(), "missing location fixture");
			auto& Function = Compiled.Module->Functions[Index];
			std::size_t Last = 0;
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				if (StringLength && Op == EOpcode::Return)
				{
					break;
				}
				if (StringLength || Op == EOpcode::StoreLocal)
				{
					Last = Ip;
				}
				Ip += Bytecode::InstructionSize(Op);
			}
			const auto Expected = Function.DebugLines[Last];
			// Exercise a runtime fallthrough diagnostic after the final fused instruction.
			Function.Code.resize(Last + Bytecode::InstructionSize(static_cast<EOpcode>(Function.Code[Last])));
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(EExecutionBackend::Vm);
			Require(!Vm.ExecuteFunction(*Compiled.Module, Index, {std::int32_t{7}}), "missing return must fail");
			Require(Vm.GetError().MessageId == "diag.missing_return_in_function" && Vm.GetError().Location.Line == Expected.Line && Vm.GetError().Location.Column == Expected.Column,
			    "fused statement lost its final instruction location: " + std::to_string(Vm.GetError().Location.Line) + ":" + std::to_string(Vm.GetError().Location.Column) + " expected " + std::to_string(Expected.Line) + ":" + std::to_string(Expected.Column));
		}
	}

	void StringLengthStackBoundary()
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "int64 work(string text){\nreturn len(\ntext\n);\n}", "length-stack-boundary.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::uint16_t Index = 0;
		while (Index < Compiled.Module->Functions.size() && Compiled.Module->Functions[Index].Name.find("work#") == std::string::npos)
		{
			++Index;
		}
		Require(Index < Compiled.Module->Functions.size(), "missing length-stack fixture");
		auto& Function = Compiled.Module->Functions[Index];
		const auto OriginalCode = Function.Code;
		const auto OriginalLines = Function.DebugLines;
		Require(static_cast<EOpcode>(OriginalCode.front()) == EOpcode::LoadLocal,
		    "length-stack fixture must begin with the string argument");
		const auto Expected = OriginalLines.front();
		// Exercise the VM's operand limit with a valid final load/len sequence.
		for (const std::size_t Depth : {65535u, 65536u})
		{
			Function.Code.clear();
			Function.DebugLines.clear();
			for (std::size_t Slot = 0; Slot < Depth; ++Slot)
			{
				Function.Code.push_back(static_cast<std::uint8_t>(EOpcode::ConstBool));
				Function.Code.push_back(0u);
			}
			const auto PrefixSize = Function.Code.size();
			Function.Code.insert(Function.Code.end(), OriginalCode.begin(), OriginalCode.end());
			Function.DebugLines.resize(PrefixSize);
			Function.DebugLines.insert(Function.DebugLines.end(), OriginalLines.begin(), OriginalLines.end());
			// A mapped builtin must not replace the earlier failing load's location.
			auto& BuiltinLine = Function.DebugLines[PrefixSize + Bytecode::InstructionSize(EOpcode::LoadLocal)];
			BuiltinLine.Line = Expected.Line + 1u;
			BuiltinLine.Column = Expected.Column + 1u;
			for (const bool Governed : {false, true})
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(EExecutionBackend::Vm);
				if (Governed)
				{
					Vm.SetExecutionLimits({1000000u, 0});
				}
				FValue Result;
				const bool Success = Vm.ExecuteFunction(*Compiled.Module, Index, {std::string("abc")}, nullptr, &Result);
				if (Depth == 65535u)
				{
					Require(Success && std::holds_alternative<std::int64_t>(Result) && std::get<std::int64_t>(Result) == 3,
					    "string length changed at the operand limit");
				}
				else
				{
					Require(!Success && Vm.GetError().Message.find("operand stack overflow") != std::string::npos && Vm.GetError().Location.Line == Expected.Line && Vm.GetError().Location.Column == Expected.Column,
					    "string fusion changed the failing load location at the operand limit");
				}
			}
		}
	}

	void SourceOffsets()
	{
		const std::string Prefix = "// producer\r\n\r\n";
		const std::string Source = Prefix + "struct Text { int value = 7; };\r\n";
		VeroScriptLexer Lexer(Source, "offset-index.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "offset-index.vs");
		auto Program = Parser.ParseProgram();
		Require(Program && !Parser.HasError() && Program->Structs.size() == 1u, "parse source-offset fixture");
		auto& Declaration = Program->Structs.front();
		Require(Declaration.Location.Line == 3u, "fixture declaration line");
		const auto Expected = Prefix.size() + Declaration.Location.Column - 1u;
		Declaration.DeclMeta.SourceRange = {};
		Declaration.Fields.front().DeclMeta.SourceRange = {};
		auto Metadata = std::make_shared<FSourceMetadataSnapshot>();
		FSourceDocument Document;
		Document.Id = Document.ModuleId = "offset-index.vs";
		Document.Text = Source;
		// A retained producer table may differ from the current declaration text.
		Document.LineOffsets = {0u, 1u, 2u};
		Metadata->Documents.push_back(std::move(Document));
		Program->SourceMetadata = Metadata;
		auto Prepared = VeroScriptProgramPreparation::Prepare(std::move(Program));
		Require(Prepared.Succeeded(), Prepared.Diagnostics.empty() ? "prepare source-offset fixture" : Prepared.Diagnostics.front().Message);
		bool Found = false;
		for (const auto& Node : Prepared.Prepared->GetSourceMetadata().Nodes)
		{
			if (Node.Name == "Text" && Node.Target == static_cast<std::uint32_t>(EAnnotationTarget::Struct))
			{
				Require(Node.Range.Begin == Expected && Node.Range.Position.Begin.Line == 3u,
				    "declaration range must follow current text, independently of a retained line table");
				Found = true;
			}
		}
		Require(Found && Metadata->Documents.front().LineOffsets == std::vector<std::size_t>{0u, 1u, 2u},
		    "source-offset collection changed shared producer metadata");
	}

	void CacheInputIdentity()
	{
		const auto Load = []()
		{
			VeroScriptModuleLoader Loader("cache-identity.vs");
			auto Program = Loader.LoadSourceModule("int value(){return 7;}void main(){}", "cache-identity.vs");
			Require(Program != nullptr, Loader.GetError().Message);
			return Program;
		};
		auto First = Load();
		auto Second = Load();
		std::string Identity, Repeated, Changed;
		Require(VeroScriptProgramPreparation::TryGetCacheInputFingerprint(*First, {}, Identity) && VeroScriptProgramPreparation::TryGetCacheInputFingerprint(*Second, {}, Repeated) && !Identity.empty() && Identity == Repeated, "unprocessed input identity must be deterministic");
		--Second->ConstantEvaluationLimits.MaxWork;
		Require(VeroScriptProgramPreparation::TryGetCacheInputFingerprint(*Second, {}, Changed) && Changed != Identity, "constexpr budgets must invalidate early cache identity");
		FAnnotationProcessingOptions Processing;
		Processing.Registry = std::make_shared<FAnnotationProcessorRegistry>();
		Require(!VeroScriptProgramPreparation::TryGetCacheInputFingerprint(*First, Processing, Changed),
		    "processor requests require prepared dependency validation");
		auto Prepared = VeroScriptProgramPreparation::Prepare(std::move(First));
		Require(Prepared.Succeeded(), "prepare cache identity fixture");
		Require(!VeroScriptProgramPreparation::TryGetCacheInputFingerprint(Prepared.Prepared->GetProgram(), {}, Changed),
		    "prepared inputs retain their sealed identity");
	}

	void ShortLivedCache(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		if (!Factory) return;
		auto Compiled = VeroScriptCompile::CompileSource(
		    "int unused(int x){return x*2;}int work(int x){return x+31;}void main(){}", "short-call.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		std::uint16_t Function = 0;
		while (Function < Module->Functions.size() && Module->Functions[Function].Name.find("work#") == std::string::npos)
		{
			++Function;
		}
		Require(Function < Module->Functions.size(), "missing short-lived function");
		for (const bool Borrowed : {true, false})
		{
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(EExecutionBackend::Jit);
			FJitPolicy Policy;
			Policy.bReuseSessionCode = Borrowed;
			Vm.SetJitPolicy(Policy);
			std::string Error;
			Require(Vm.SetJitBackendFactory(Factory, Error), Error);
			if (!Borrowed)
			{
				Require(Vm.SetJitProgramOwner(Module, Error), Error);
			}
			FValue Result;
			Require(Vm.ExecuteFunction(*Module, Function, {std::int32_t{17}}, nullptr, &Result) && std::holds_alternative<std::int32_t>(Result) && std::get<std::int32_t>(Result) == 48,
			    "short-lived JIT invocation changed results");
			Require(Vm.GetJitStats().CompileAttempts == 1, "short-lived function eagerly compiled unrelated bodies");
		}
	}

	void ComplexLoopSelection(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		struct FCase
		{
			std::string Source;
			std::string Output;
			bool bNativeText;
		};
		const std::array<FCase, 4> Cases = {{{"int build(int n){array<string> pieces={\"a\",\"bc\"};string value=\"\";"
		                                                                    "for(int i=0;i<n;i++)value+=pieces[i%2];return to_int(len(value));}"
		                                                                    "void main(){print(build(7));}",
		                                                                       "10\n", true},
		    {"string build(string value,string suffix){for(int i=0;i<4;i++)value+=suffix;return value;}"
		     "void main(){print(build(\"a\",\"xy\"));}",
		        "axyxyxyxy\n", true},
		    {"wstring build(wstring value,wstring suffix){for(int i=0;i<4;i++)value+=suffix;return value;}"
		     "void main(){print(build(L\"A\",L\"BC\"));}",
		        "ABCBCBCBC\n", false},
		    {"int build(int n){deque<int> q=deque<int>();for(int i=0;i<257;i++)q.push_back(i);"
		     "int total=0;for(int i=0;i<n;i++){q.push_back(i);total+=q.pop_front();}return total;}"
		     "void main(){print(build(4));}",
		        "6\n", false}}};
		for (const auto& Case : Cases)
		{
			Compare(Case.Source, Case.Output, Factory, false, Case.bNativeText);
			if (!Factory)
			{
				continue;
			}
			auto Compiled = VeroScriptCompile::CompileSource(Case.Source, "text-loop-selection.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			FJitProgramView View;
			View.Owner = std::move(Compiled.Module);
			View.Module = View.Owner.get();
			View.Identity = {1, 1, 1};
			std::string Error;
			auto Cache = FJitBackendCache::Create(Factory, View, {}, {}, ~0ull, Error);
			Require(Cache != nullptr, Error);
			bool Found = false;
			for (std::size_t Index = 0; Index < View.Module->Functions.size(); ++Index)
			{
				if (View.Module->Functions[Index].Name.find("build#") == std::string::npos)
				{
					continue;
				}
				Require(Cache->EnsureCompiled(static_cast<std::uint16_t>(Index)) == EJitCompilationResult::Ready,
				    "text loop has no backend artifact");
				const auto Artifact = Cache->Find(static_cast<std::uint16_t>(Index));
				const bool NativeText = Case.bNativeText &&
				    (Factory->GetDescription().Capabilities & JitNativeTypedLoopsCapability) != 0;
				Require(Artifact && Artifact->bPreferVmForExecution != NativeText,
				    NativeText ? "covered text work retained the VM cost guard\n" + Case.Source
				               : "unlowered complex work bypassed the VM cost guard\n" + Case.Source);
				Found = true;
			}
			Require(Found, "missing text-loop fixture");
		}
		Compare("int build(int n){string prefix=\"unrelated\";array<int> a={1,2,3};int64 total=0;"
		        "for(int64 i=0;i<n;i++)total+=a[i%3];return to_int(total);}void main(){print(build(7));}",
		    "13\n", Factory, false, true);
	}

	void ScalarArgumentPacking(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		for (const auto Count : {0, 2, 8, 16})
		{
			for (const std::string Type : {"int", "unsigned int", "double", "int64"})
			{
				std::string Parameters, Arguments;
				const std::string Suffix = Type == "unsigned int" ? "u" : Type == "int64" ? "i64"
				                                                                          : "";
				std::string Expression = Type == "double" ? "17.25" : "17" + Suffix;
				std::int64_t Expected = Type == "double" ? 69 : 17;
				for (int Index = 0; Index < Count; ++Index)
				{
					if (Index != 0)
					{
						Parameters += ",";
						Arguments += ",";
					}
					const std::string Name = "p" + std::to_string(Index);
					Parameters += Type + " " + Name;
					const std::int64_t Value = Type == "int64" ? 9007199254741003LL + Index
					    : Type == "unsigned int"               ? 20000000LL + Index
					                                           : Index + 1;
					Arguments += std::to_string(Value) + (Type == "double" ? ".25" : Suffix);
					Expression += "+" + Name + "*" + std::to_string(Index + 1) + Suffix;
					Expected += (Type == "double" ? Value * 4 + 1 : Value) * (Index + 1);
				}
				const std::string Signature = Type + " sum(" + Parameters + "){return " + Expression + ";}";
				const auto Result = [&](const std::string& Target)
				{
					const std::string Call = Target + "(" + Arguments + ")";
					return "print(" + (Type == "double" ? "to_int(" + Call + "*4.0)" : Call) + ");";
				};
				Compare(Signature + "class Receiver{public:" + Signature + "};void main(){"
				                                                           "Receiver receiver=Receiver();" +
				        Result("sum") + Result("receiver.sum") + "}",
				    std::to_string(Expected) + "\n" + std::to_string(Expected) + "\n", Factory);
			}
		}
		Compare("int evaluate(bool enabled,int value,double scale,uint64 wide){"
		        "return enabled?to_int(value*scale)+to_int(wide):-1;}"
		        "class Receiver{public:int evaluate(bool enabled,int value,double scale,uint64 wide){"
		        "return enabled?to_int(value*scale)+to_int(wide):-1;}};"
		        "void main(){Receiver receiver=Receiver();print(evaluate(true,7,2.5,19u64));"
		        "print(receiver.evaluate(true,7,2.5,19u64));print(evaluate(false,7,2.5,19u64));"
		        "print(receiver.evaluate(false,7,2.5,19u64));}",
		    "36\n36\n-1\n-1\n", Factory);
	}

	void RetainedConstructorBounds(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const auto Source = [](const std::string& Addend, const std::string& Multiplier,
		                        const std::string& Count, const std::string& FirstMask = "255",
		                        const std::string& SecondMask = "127")
		{
			return "class Sample{public:int first=0;int second=0;int sum(){return first+second;}};"
			       "int work(int n){array<Sample>a=array<Sample>();for(int i=0;i<n;i++){Sample p=Sample();"
			       "p.first=(i+(" +
			    Addend + "))&" + FirstMask + ";p.second=(i*(" + Multiplier + "))&" + SecondMask + ";a.push(p);}"
			                                                                                      "int total=0;for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}"
			                                                                                      "void main(){print(work(" +
			    Count + "));}";
		};
		Compare(Source("17", "7", "20"), "1732\n", Factory);
		Compare(Source("-17", "-7", "7"), "2315\n", Factory);
		Compare(Source("17", "-2147483647", "2"), "36\n", Factory);
		Compare(Source("17", "7", "0"), "0\n", Factory);
		Compare(Source("17", "7", "-3"), "0\n", Factory);
		Compare(Source("17", "7", "-2147483647-1"), "", Factory, true);
		Compare(Source("2147483645", "1073741823", "4"), "", Factory, true);
		Compare(Source("17", "-1073741824", "4"), "", Factory, true);
		Compare(Source("17", "1073741823", "4"), "", Factory, true);
		Compare(Source("2147483647", "7", "1", "2147483647", "0"), "2147483647\n", Factory);
		Compare(Source("1073741822", "7", "2", "1073741823", "0"), "2147483645\n", Factory);
		// An inconclusive upper bound must retain successful ordinary arithmetic.
		Compare(Source("-1", "0", "3", "1073741823", "0"), "1073741824\n", Factory);
		Compare(Source("17", "7", "3", "1073741823", "1"), "55\n", Factory);
		Compare(Source("17", "7", "17", "0", "0"), "0\n", Factory);
		Compare(Source("-17", "-7", "7", "-1", "-1"), "-245\n", Factory);
		Compare(Source("2147483646", "7", "2", "2147483647", "1"), "", Factory, true);
		Compare(Source("1000000000", "7", "3", "2147483647", "0"), "", Factory, true);
		if (Factory && (Factory->GetDescription().Capabilities & JitNativeTypedLoopsCapability) != 0)
		{
			const std::array<std::pair<std::string, std::size_t>, 7> Samples{{{Source("17", "7", "1", "1073741823", "1"), 1},
			    {Source("17", "7", "2", "1073741823", "1"), 2},
			    {Source("17", "7", "3", "1073741823", "1"), 3},
			    {Source("17", "7", "17", "1073741823", "1"), 17},
			    {Source("2147483647", "7", "1", "2147483647", "0"), 1},
			    {Source("1073741822", "7", "2", "1073741823", "0"), 2},
			    {Source("17", "7", "17", "0", "0"), 17}}};
			for (const auto& [Script, Count] : Samples)
			{
				auto Compiled = VeroScriptCompile::CompileSource(
				    Script, "projection-statistics.vs");
				Require(Compiled.bSuccess, Compiled.Error.Message);
				std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
				const auto Result = Run(Module, EExecutionBackend::Jit, Factory, 0);
				Require(Result.Success && Result.Stats.ArrayFastPathPushes == static_cast<std::size_t>(Count) && Result.Stats.ArrayFastPathGets == static_cast<std::size_t>(Count) && Result.Stats.ScalarMethodFastPathI32 == static_cast<std::size_t>(Count),
				    "projection statistics for count=" + std::to_string(Count) + ": pushes=" + std::to_string(Result.Stats.ArrayFastPathPushes) + ", gets=" + std::to_string(Result.Stats.ArrayFastPathGets) + ", calls=" + std::to_string(Result.Stats.ScalarMethodFastPathI32) + ", error=" + Result.Error);
			}
		}
	}

	void FloatingMapReduction(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const auto Source = [](const std::string& Count, const std::string& Seed,
		                        const std::string& Multiplier = "75", const std::string& Increment = "74",
		                        const std::string& Modulus = "65521", const std::string& ValueModulus = "7",
		                        const std::string& Size = "5")
		{
			return "int work(int n,int seed){int size=" + Size + ";map<int,double> values=map<int,double>();"
			                                                     "values.reserve(size);for(int i=0;i<size;i++)values[i]=(i%31)*0.25+1.0;"
			                                                     "int state=seed;double total=0.0;for(int i=0;i<n;i++){state=(state*" +
			    Multiplier + "+" + Increment + ")%" + Modulus + ";int key=(state+i*13)%size;"
			                                                    "double value=values[key]*1.000001+(state%" +
			    ValueModulus + ")*0.25;"
			                   "if(value>10000.0)value*=0.5;values[key]=value;total+=value;}return to_int(total);}"
			                   "void main(){print(work(" +
			    Count + "," + Seed + "));}";
		};
		Compare(Source("12", "173"), "34\n", Factory);
		Compare(Source("0", "173"), "0\n", Factory);
		Compare(Source("-3", "173"), "0\n", Factory);
		Compare(Source("0", "173", "75", "74", "65521", "7", "0"), "0\n", Factory);
		Compare(Source("5", "0", "75", "74", "1", "1"), "7\n", Factory);
		Compare(Source("1", "2147483647", "1", "0", "2147483647", "2147483647"), "1\n", Factory);
		Compare(Source("1", "65520", "1", "0", "65521", "65520"), "1\n", Factory);
		Compare(Source("5", "173", "75", "74", "65521", "7", "4"), "12\n", Factory);
		Compare(Source("1", "65535", "1", "0", "65536", "32768"), "8192\n", Factory);
		Compare(Source("5", "173", "75", "74", "-1"), "7\n", Factory);
		Compare(Source("3", "-1"), "", Factory, true);
		Compare(Source("3", "173", "75", "74", "0"), "", Factory, true);
		Compare(Source("3", "173", "75", "74", "65521", "0"), "", Factory, true);
		Compare(Source("3", "173", "75", "74", "65521", "7", "0"), "", Factory, true);
		Compare(Source("3", "2", "2147483647"), "", Factory, true);
		Compare(Source("3", "1", "75", "2147483647"), "", Factory, true);
		Compare(Source("3", "2147483646", "1", "0", "2147483647"), "", Factory, true);
		if (Factory && (Factory->GetDescription().Capabilities & JitNativeTypedLoopsCapability) != 0)
		{
			auto Compiled = VeroScriptCompile::CompileSource(Source("12", "173"), "map-reduction-statistics.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
			const auto Result = Run(Module, EExecutionBackend::Jit, Factory, 0);
			Require(Result.Success && Result.Stats.ScalarMapFastPathGets == 12 && Result.Stats.ScalarMapFastPathSets == 17,
			    "floating map reduction must execute and commit container statistics");
		}
	}

	void DescriptorStorage()
	{
		FScriptObject Object;
		Require(!Object.DescriptorData && Object.GetDescriptorData().ReflectionOwnerType.empty(), "ordinary object descriptor");
		Require(!Object.DescriptorData, "reading an ordinary object allocated descriptor data");
		Object.Kind = EScriptObjectKind::Struct;
		Object.SetTypeName("Fixture");
		Object.Fields.push_back(std::int32_t{7});
		auto Data = std::make_shared<FScriptObjectDescriptorData>();
		Data->ReflectionOwnerType = "Owner";
		Data->ReflectionMemberName = "field";
		Data->SourceMetadataNodeId = "original";
		Data->SourceMetadataPath = {1, 2};
		Object.DescriptorData = std::move(Data);
		const auto Clone = Runtime::CloneStruct(Object);
		Require(Clone && Clone->DescriptorData == Object.DescriptorData, "clone shares immutable descriptor");
		Object.Fields[0] = std::int32_t{9};
		Require(std::get<std::int32_t>(Clone->Fields[0]) == 7, "clone must retain independent struct fields");
		auto Replacement = std::make_shared<FScriptObjectDescriptorData>(Object.GetDescriptorData());
		Replacement->SourceMetadataNodeId = "replacement";
		Object.DescriptorData = std::move(Replacement);
		Require(Clone->GetDescriptorData().SourceMetadataNodeId == "original", "descriptor replacement changed clone");
	}

	void WideSorting(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const std::string Source =
		    "void work(array<int> a,int n){"
		    "for(int64 start=n/2-1;start>=0;start--){int64 root=start;while(root<n/2){"
		    "int64 child=root*2+1;int64 largest=root;if(a[largest]<a[child])largest=child;"
		    "if(child+1<n&&a[largest]<a[child+1])largest=child+1;if(largest==root)break;"
		    "int v=a[root];a[root]=a[largest];a[largest]=v;root=largest;}}"
		    "for(int64 end=n-1;end>0;end--){int v=a[0];a[0]=a[end];a[end]=v;int64 root=0;"
		    "while(root<end/2){int64 child=root*2+1;int64 largest=root;"
		    "if(a[largest]<a[child])largest=child;"
		    "if(child+1<end&&a[largest]<a[child+1])largest=child+1;if(largest==root)break;"
		    "v=a[root];a[root]=a[largest];a[largest]=v;root=largest;}}}";
		auto Compiled = VeroScriptCompile::CompileSource(Source, "wide-sorting.vs");
		Require(Compiled.bSuccess, Compiled.Error.Message);
		std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
		std::uint16_t Function = 0;
		while (Function < Module->Functions.size() && Module->Functions[Function].Name.find("work#") == std::string::npos)
		{
			++Function;
		}
		Require(Function < Module->Functions.size(), "missing sort function");
		for (const int Count : {0, 1, 2, 3, 17, 257})
		{
			std::vector<std::int32_t> Input;
			int State = 17;
			for (int Index = 0; Index < Count; ++Index)
			{
				State = (State * 75 + 74) % 65521;
				Input.push_back(State % 31 - 15);
			}
			auto Expected = Input;
			std::sort(Expected.begin(), Expected.end());
			for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
			{
				if (Mode != EExecutionBackend::Vm && !Factory)
				{
					continue;
				}
				auto Values = std::make_shared<FDynamicArray>();
				Values->TypeName = "array<int>";
				Values->ElementTypeName = "int";
				Values->ScalarElements = Input;
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(Mode);
				if (Mode != EExecutionBackend::Vm)
				{
					std::string Error;
					Require(Vm.SetJitBackendFactory(Factory, Error) && Vm.SetJitProgramOwner(Module, Error), Error);
				}
				Require(Vm.ExecuteFunction(*Module, Function, {Values, std::int32_t{Count}}), Vm.GetError().Message);
				Require(std::get<std::vector<std::int32_t>>(Values->ScalarElements) == Expected,
				    "wide sort differs from the complete independently sorted input");
				if (Count >= 17 && Mode != EExecutionBackend::Vm && (Factory->GetDescription().Capabilities & JitNativeTypedLoopsCapability) != 0)
				{
					Require(Vm.GetJitStats().NativeTypedLoopExecutions != 0, "sort did not enter native typed code");
				}
			}
		}
	}
}

int main(int Argc, char** Argv)
{
	try
	{
		CheckObjectSupportStorage();
		std::shared_ptr<const IJitBackendFactory> Factory;
		if (Argc == 2)
		{
			std::string Error;
			Factory = LoadJitBackend(Argv[1], Error);
			Require(Factory != nullptr, Error);
		}
		DescriptorStorage();
		SourceOffsets();
		AdjacentLocalJumpBoundaries();
		FusedExpressionFailureLocation();
		StringLengthStackBoundary();
		CacheInputIdentity();
		ShortLivedCache(Factory);
		ComplexLoopSelection(Factory);
		RetainedConstructorBounds(Factory);
		FloatingMapReduction(Factory);
		{
			FSet::FIntScalarElements Values;
			for (const auto Key : {-1, 0, 1, 63, 64, 127, 128})
			{
				Values.Insert(Key);
			}
			Values.Erase(64);
			std::array<std::uint64_t, 2> Bits;
			Values.BorrowReadOnlyProbe().BuildNonnegativeBitmap(Bits.data(), Bits.size());
			Require(Bits[0] == ((std::uint64_t{1} << 63u) | 3u) && Bits[1] == (std::uint64_t{1} << 63u),
			    "bounded set snapshot includes only live keys in range");
		}
		SignedWideMultiplication(Factory);
		ScalarAndLengthPaths(Factory);
		ScalarClosurePaths(Factory);
		IndexedScalarExpressions(Factory);
		DispatchSelectionBoundaries(Factory);
		ScalarDirectCallGraphs();
		QuotaOnlyExpressionBoundaries();
		NativeArrayInitializationCleanup(Factory);
		FrameReturnPaths(Factory);
		BorrowedModuleCacheReuse(Factory);
		OwnedModuleCacheReattachment(Factory);
		StringLocalAppend(Factory);
		StringLocalAppendMutationObserver(Factory);
		StringReadAndFormattingBoundaries(Factory);
		Compare("void main(){int i=0;try{for(;i<3;i++){}}finally{print(i);}print(9);}", "3\n9\n", Factory);
		Compare("void main(){int i=0;for(;i<3;){try{i++;continue;}finally{print(i);}}print(9);}",
		    "1\n2\n3\n9\n", Factory);
		GrowingArrayFailure(Factory);
		ArgumentCleanup(Factory);
		IndirectArgumentCleanup(Factory);
		NativeValueLeaves(Factory);
		StandardValueLeafBoundaries(Factory);
		LocalScalarIndexOperations(Factory);
		ShortReadSelection(Factory);
		NativeProviderOwnership(Factory);
		ScalarArgumentPacking(Factory);
		Compare("string update(ref string s){s=\"changed\";return \"tail\";}"
		        "void main(){array<string> chunks={\"ab\",\"cd\",\"e\"};string text=\"\";"
		        "for(int i=0;i<7;i++)text+=chunks[(i+1)%3];string snapshot=text;"
		        "text+=text;print(snapshot);print(text);string suffix=\"!\";text+=suffix;"
		        "print(text);string old=\"old\";old+=update(ref old);print(old);}",
		    "cdeabcdeabcd\ncdeabcdeabcdcdeabcdeabcd\ncdeabcdeabcdcdeabcdeabcd!\noldtail\n", Factory);
		Compare("void main(){array<string>a={\"x\"};string s=\"kept\";print(s);s+=a[1];}",
		    "kept\n", Factory, true);
		Compare("void main(){array<string>a={\"x\"};string s=\"kept\";int i=2147483647;print(s);s+=a[(i+1)%1];}",
		    "kept\n", Factory, true);
		Compare("int work(int n){array<int>a={1,4,7,10,13,16,19};int64 r=-7;int total=0;"
		        "for(int64 i=0;i<n;i++){r=(r*3-5)%97;int64 k=(i*5)%7;a[k]=to_int(r/3);total+=a[k];}return total;}"
		        "void main(){print(work(37));}",
		    "-597\n", Factory, false, true);
		Compare("void main(){array<int>a={1,2};uint64 huge=9223372036854775808u64;print(7);"
		        "for(int64 i=0;i<2;i++){a[i]=to_int(i);int64 value=to_int64(huge);print(value);}}",
		    "7\n", Factory, true);
		PartialEffects(Factory);
		WideSorting(Factory);
		{
			auto Compiled = VeroScriptCompile::CompileSource("int work(int n){int delta=1;"
			                                                 "auto get=[delta](int v)->int{return v+delta;};int s=0;for(int i=0;i<n;i++)s=get(s);return s;}"
			                                                 "void main(){print(work(0));print(work(1));}",
			    "inline-depth.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
			for (const auto Depth : {2u, 3u, 8u})
			{
				const auto Reference = Run(Module, EExecutionBackend::Vm, {}, 0, Depth);
				if (!Factory)
				{
					continue;
				}
				for (const auto Mode : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
				{
					const auto Actual = Run(Module, Mode, Factory, 0, Depth);
					Require(Actual.Success == Reference.Success && Actual.Output == Reference.Output && Actual.Error == Reference.Error && Actual.Line == Reference.Line && Actual.Column == Reference.Column,
					    "inlining changed the language call-depth limit: " + Actual.Error);
				}
			}
		}
		Compare("int work(int n,int seed){set<int>a=set<int>();for(int i=0;i<3000;i+=3)a.add(i);"
		        "int s=seed;int total=0;for(int i=0;i<n;i++){s=(s*75+74)%65521;if(a.has(s%3000))total++;}return total+s;}"
		        "void main(){print(work(5000,17));}",
		    "35708\n", Factory);
		Compare("int work(int n){array<uint32> values=array<uint32>();values.reserve(1);values.push(4294967295u);"
		        "array<uint32> first=values;array<uint32> second=values;int total=0;"
		        "for(int i=0;i<n;i++){values.push(to_unsigned(i));first[i+1]=first[i+1]+1u;total+=to_int(second[i+1]);}"
		        "return total+to_int(len(second))+(second[0]==4294967295u?1:0);}void main(){print(work(300));}",
		    "45452\n", Factory);
		{
			std::string Source = "int leaf(int x){int temp=x;temp=temp*3;return(temp+1)%65521;}int work(int seed){int s=seed;";
			int Expected = 17;
			for (int Index = 0; Index < 16; ++Index)
			{
				Source += "s=leaf(s);";
				Expected = (Expected * 3 + 1) % 65521;
			}
			Compare(Source + "return s;}void main(){print(work(17));}", std::to_string(Expected) + "\n", Factory);
		}
		Compare("int fib(int n){if(n<2)return n;return fib(n-1)+fib(n-2);}void main(){print(fib(15));}",
		    "610\n", Factory);
		{
			auto Compiled = VeroScriptCompile::CompileSource("int down(int n){if(n<1)return 0;return down(n-1)+1;}"
			                                                 "void main(){print(down(40));}",
			    "native-recursive-depth.vs");
			Require(Compiled.bSuccess, Compiled.Error.Message);
			std::shared_ptr<const FBytecodeModule> Module = std::move(Compiled.Module);
			for (const auto Depth : {3u, 8u, 40u, 41u, 42u, 64u})
			{
				const auto Reference = Run(Module, EExecutionBackend::Vm, {}, 0, Depth);
				if (!Factory)
				{
					continue;
				}
				for (const auto Mode : {EExecutionBackend::Mixed, EExecutionBackend::Jit})
				{
					const auto Actual = Run(Module, Mode, Factory, 0, Depth);
					Require(Actual.Success == Reference.Success && Actual.Output == Reference.Output && Actual.Error == Reference.Error && Actual.Line == Reference.Line && Actual.Column == Reference.Column,
					    "native recursion changed the language call-depth limit: " + Actual.Error);
				}
			}
		}
		Compare("int down(int n){if(n<1)return 0;return down(n-1)+1;}void main(){print(down(100));}",
		    "100\n", Factory);
		Compare("int work(int delta){auto get=[delta](int v)->int{return(v*3+delta)%65521;};"
		        "int v=17;for(int i=0;i<25;i++)v=get(v);return v;}void main(){print(work(1));print(work(5));}",
		    "25708\n21156\n", Factory);
		Compare("int work(int delta){auto get=[delta](int v)->int{return v+delta;};delta=9;"
		        "int v=1;for(int i=0;i<4;i++)v=get(v);return v;}void main(){print(work(2));print(work(3));}",
		    "9\n13\n", Factory);
		Compare("int work(int delta){auto get=[delta](int v)->int{return v+delta;};"
		        "int v=2147483645;for(int i=0;i<4;i++)v=get(v);return v;}void main(){print(work(2));}",
		    "", Factory, true);
		Compare("interface Op{virtual int step(int v,int s)=0;}"
		        "class A:public Op{public:int step(int v,int s){return(v+1+s%3)%1000000007;}}"
		        "class B:public Op{public:int step(int v,int s){return((v^(s&7))+7)%1000000007;}}"
		        "class C:public Op{public:int step(int v,int s){return(v*3+s)%1000003;}}"
		        "int work(int n,int seed){array<Op>a={A(),B(),C()};int s=seed;int total=0;"
		        "for(int i=0;i<n;i++){s=(s*75+74)%65521;Op op=a[s%3];total=op.step(total,s);}return total+s;}"
		        "void main(){print(work(64,17));}",
		    "285913\n", Factory);
		Compare("void main(){string s=\"a\";string copy=s;for(int i=0;i<5;i++)s+=\"bc\";print(copy+\":\"+s);}",
		    "a:abcbcbcbcbc\n", Factory);
		Compare("void main(){wstring s=L\"a\";wstring copy=s;for(int i=0;i<5;i++)s+=L\"bc\";print(copy+L\":\"+s);}",
		    "a:abcbcbcbcbc\n", Factory);
		Compare("void edit(ref string s){s+=\"c\";}void main(){string s=\"a\";edit(ref s);string t=(s+=\"b\");print(s+\":\"+t);}",
		    "acb:acb\n", Factory);
		Compare("string s=\"a\";string rhs(){s=\"b\";return \"c\";}void main(){s=s+rhs();print(s);}", "ac\n", Factory);
		Compare("void main(){string s=\"a\\0b\";print(len(s));wstring w=L\"a\\0b\";print(len(w));}", "3\n3\n", Factory);
		Compare("string global=\"a\\0bc\";wstring wide=L\"x\\0y\";"
		        "class Text{public:string value=\"12345\";wstring wvalue=L\"123456\";}"
		        "int64 measure(ref string s,ref wstring w){return len(s)+len(w);}"
		        "void main(){Text box=Text();print(len(global));print(len(wide));print(len(box.value));"
		        "print(len(box.wvalue));print(measure(ref global,ref wide));"
		        "print(measure(ref box.value,ref box.wvalue));global+=\"!\";box.value+=\"!\";"
		        "print(len(global));print(len(box.value));}",
		    "4\n3\n5\n6\n7\n11\n5\n6\n", Factory);
		Compare("int work(){array<int> a=array<int>();a.push(1);a.push(2);a.push(3);int total=0;"
		        "for(int64 i=0;i<3;i++){int v=a[i];total+=v;a[i]=v+1;}return total+a[0]+a[1]+a[2];}"
		        "void main(){print(work());}",
		    "15\n", Factory, false, true);
		Compare("int work(){array<int> a=array<int>();a.push(1);a.push(2);int total=0;"
		        "for(uint64 i=0;i<to_uint64(2);i++){total+=a[i];a[i]=to_int(i)+3;}return total+a[0]+a[1];}"
		        "void main(){print(work());}",
		    "10\n", Factory, false, true);
		Compare("int work(int count,int branch){array<int> a={1,2};array<int> b={10,20};array<int> selected=a;"
		        "int64 value=123i64;uint64 wide=456u64;uint32 narrow=789u;bool keep=true;int total=0;"
		        "for(int64 i=0;i<count;i++){if(i==branch){value=42i64;wide=43u64;narrow=44u;keep=false;selected=b;}"
		        "total+=selected[i%2];}print(value);print(wide);print(narrow);print(keep?1:0);print(total);return selected[0];}"
		        "void main(){print(work(4,-1));print(work(4,0));print(work(4,2));print(work(0,0));}",
		    "123\n456\n789\n1\n6\n1\n42\n43\n44\n0\n60\n10\n42\n43\n44\n0\n33\n10\n"
		    "123\n456\n789\n1\n0\n1\n",
		    Factory, false, true);
		Compare("int work(int branch,string replacement){array<int> a={1,2};string value=\"keep\";int total=0;"
		        "for(int i=0;i<4;i++){if(i==branch)value=replacement;total+=a[i%2];}print(value);return total;}"
		        "void main(){print(work(-1,\"changed\"));print(work(0,\"changed\"));print(work(2,\"changed\"));}",
		    "keep\n6\nchanged\n6\nchanged\n6\n", Factory, false, true);
		Compare("int work(){double unused=0.0;array<int> a={1,2,3};int total=0;"
		        "for(int64 i=0;i<3;i++){total+=a[i];}return total;}void main(){print(work());}",
		    "6\n", Factory, false, true);
		Compare("int work(){array<double> a={1.0,2.0,3.0};double total=0.0;"
		        "for(int64 i=0;i<5;i++){total+=a[i%3];}return to_int(total);}void main(){print(work());}",
		    "9\n", Factory);
		Compare("double calc(double a,double b){return(a+b)*(a-b)/(b+0.5);}"
		        "double mixed(double d,int a,int64 b,uint64 c,float f){return d+a+b+c+f;}"
		        "void main(){assert(calc(3.5,1.5)==5.0);assert(mixed(0.5,2,3i64,4u64,1.5f)==11.0);"
		        "double large=1e308;double inf=large*large;assert(inf>large);double nan=inf-inf;assert(nan!=nan);print(11);}",
		    "11\n", Factory);
		Compare("double divide(double a,double b){return a/b;}void main(){print(divide(1.0,0.0));}",
		    "", Factory, true);
		Compare("double divide(double a,double b){return a/b;}void main(){print(divide(1.0,-0.0));}",
		    "", Factory, true);
		Compare("int work(){array<int> a={4,5,6};int total=0;"
		        "for(uint64 i=to_uint64(3);i>to_uint64(0);--i){total+=a[i-to_uint64(1)];}return total;}void main(){print(work());}",
		    "15\n", Factory, false, true);
		for (const auto Index : {"-1i64", "4294967296i64", "9007199254740993i64"})
		{
			Compare(std::string("int work(){array<int> a=array<int>();a.push(7);int64 j=") + Index + ";int sum=0;for(int i=0;i<2;i++){sum+=a[j];}return sum;}void main(){print(work());}", "", Factory, true);
		}
		Compare("int64 work(){int64 s=9007199254740993i64;for(int i=0;i<4;i++){s=s+(-1);}return s;}void main(){print(work());}",
		    "9007199254740989\n", Factory);
		Compare("int64 work(){int64 s=9223372036854775806i64;for(int i=0;i<3;i++)s=s+1;return s;}void main(){print(work());}",
		    "", Factory, true);
		Compare("int64 left(int64 a,int b){return(a+b)+(a-b)+(a*b)+(a/b)+(a%b);}"
		        "int64 right(int a,int64 b){return(a+b)+(a-b)+(a*b)+(a/b)+(a%b);}"
		        "void main(){print(left(-17i64,5));print(right(-17,5i64));}",
		    "-124\n-124\n", Factory);
		Compare("int work(int a,int b){return(a+b)+(a-b)+(a*b)+(a/b)+(a%b);}"
		        "void main(){print(work(-17,5));print(work(17,-5));}",
		    "-124\n-52\n", Factory);
		for (const auto Operation : {"+", "-", "*", "/", "%"})
		{
			const std::string Body = "return a" + std::string(Operation) + "b;";
			const std::string Arguments = std::string(Operation) == "+" ? "9223372036854775807i64,1"
			    : std::string(Operation) == "-"                         ? "-9223372036854775807i64,2"
			    : std::string(Operation) == "*"                         ? "9223372036854775807i64,2"
			                                                            : "7i64,0";
			Compare("int64 work(int64 a,int b){" + Body + "}void main(){print(work(" + Arguments + "));}",
			    "", Factory, true);
		}
		for (const auto Operation : {"/", "%"})
		{
			Compare("int64 work(int64 a,int b){return a" + std::string(Operation) + "b;}void main(){print(work(-9223372036854775807i64-1i64,-1));}", "", Factory, true);
		}
		for (const auto Operation : {"+", "-", "*", "/", "%"})
		{
			const std::string Arguments = std::string(Operation) == "+" ? "2147483647,1"
			    : std::string(Operation) == "-"                         ? "-2147483647,2"
			    : std::string(Operation) == "*"                         ? "2147483647,2"
			                                                            : "7,0";
			Compare("int work(int a,int b){return a" + std::string(Operation) + "b;}void main(){print(work(" + Arguments + "));}", "", Factory, true);
		}
		for (const auto Operation : {"/", "%"})
		{
			Compare("int work(int a,int b){return a" + std::string(Operation) + "b;}void main(){print(work(-2147483647-1,-1));}", "", Factory, true);
		}
		for (const auto Conversion : {"to_int(2147483648i64)", "to_int(-2147483649i64)",
		         "to_int(to_uint64(2147483648i64))", "to_unsigned(-1i64)", "to_unsigned(4294967296i64)",
		         "to_int64(to_uint64(9223372036854775807i64)+to_uint64(1))", "to_uint64(-1i64)"})
		{
			Compare(std::string("int work(){array<int> a=array<int>();a.push(7);int total=0;") + "for(int i=0;i<2;i++){total+=a[i];" + Conversion + ";}return total;}void main(){print(work());}",
			    "", Factory, true, true);
		}
		Compare("int work(){int total=0;for(int i=0;i<3;i++){int x=1;total+=x<<to_int64(3);}return total;}void main(){print(work());}",
		    "24\n", Factory);
		Compare("int work(){int total=0;for(int i=0;i<3;i++){int x=1;total+=x<<to_int64(32);}return total;}void main(){print(work());}",
		    "", Factory, true);
		Compare("int work(){int total=0;for(int i=0;i<3;i++){int x=1;total+=x<<4294967296i64;}return total;}void main(){print(work());}",
		    "", Factory, true);
		std::string Types;
		for (int Index = 0; Index < 128; ++Index)
		{
			Types += "class T" + std::to_string(Index) + "{public:int value=7;int get(){return value;}};";
			Types += "struct S" + std::to_string(Index) + "{public:int value=0;S" + std::to_string(Index) + "(){value=11;}int get(){return value;}};";
		}
		Compare(Types + "void main(){T127 x=T127();S127 y=S127();print(x.get());print(y.get());}", "7\n11\n", Factory);
		Compare("int work(int n){string text=\"x\";for(int k=0;k<5;k++)text+=text;"
		        "int total=0;for(int i=0;i<n;i++)total=to_int((total+len(text))%1000003);return total;}"
		        "void main(){print(work(4));}",
		    "128\n", Factory, false, true);
		Compare("fn(int)->int make(int delta){int scale=2;return[delta,scale](int n)->int{return(n*scale+delta)%97;};}"
		        "void main(){array<fn(int)->int> calls={make(2)};int total=1;for(int i=0;i<4;i++)total=calls[0](total);print(total);}",
		    "46\n", Factory);
		Compare("int inner(int x,int y){return(x+y)*x;}int outer(int x){try{return inner(x,1);}finally{print(31);}}"
		        "void main(){print(outer(46341));}",
		    "31\n", Factory, true);
		Compare("class State{public:int value=1;int step(int n){value=5;return(n+1)*n;}};"
		        "void main(){State state=State();try{print(state.step(46341));}finally{print(state.value);}}",
		    "5\n", Factory, true);
		Compare("class State{public:int value=1;int step(int n){value=value+n;value=value*2;return value;}};"
		        "void main(){State state=State();print(state.step(2));print(state.step(3));}",
		    "6\n18\n", Factory);
		Compare("class PairState{public:int a=1;int b=2;"
		        "int step(int n){a=a+n;b=b+a;return a+b;}};"
		        "void main(){PairState state=PairState();int total=0;"
		        "for(int i=0;i<4;i++)total+=state.step(i&3);"
		        "print(total);print(state.a);print(state.b);}",
		    "47\n7\n16\n", Factory);
		Compare("class PairState{public:int x=0;int y=10;"
		        "int step(int n){x=x+1;y=y+n;return 100/(2-n);}};"
		        "void main(){PairState state=PairState();int total=0;try{"
		        "for(int i=0;i<4;i++)total+=state.step(i&3);"
		        "}finally{print(state.x);print(state.y);print(total);}}",
		    "3\n13\n150\n", Factory, true);
		Compare("import \"attribute.vs\";void main(){int value=17;std::attribute<int> a=std::attribute<int>();"
		        "a.bind([value]()->int{return value*3+2;});print(a());print(a());}",
		    "53\n53\n", Factory);
		Compare("void main(){int value=0;fn()->int first=[&value]()->int{value+=2;return value;};"
		        "fn()->int second=[&value]()->int{value+=3;return value;};"
		        "print(first());print(second());print(first());print(value);}",
		    "2\n5\n7\n7\n", Factory);
		Compare("void main(){int value=1;fn()->int read=[&value]()->int{value=7;return 2147483647+value;};"
		        "try{print(read());}finally{print(value);}}",
		    "7\n", Factory, true);
		Compare("void main(){int a=1;int b=2;fn()->int read=[&a,&b]()->int{a+=b;b+=a;return a+b;};"
		        "print(read());print(a);print(b);}",
		    "8\n3\n5\n", Factory);
		Compare("import \"delegate.vs\";void main(){int delta=7;std::delegate<int,int> d=std::delegate<int,int>();"
		        "d.bind([delta](int x)->int{return (x+delta)*x;});try{print(d.execute(46341));}"
		        "finally{print(37);}}",
		    "37\n", Factory, true);
		std::cout << "performance semantics regression passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
