// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVm.h"
#include "VeroScriptVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVmBackendLoader.h"
#include "../../Private/VeroScriptVmIndexedExpressions.h"
#include <limits>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace VeroScript;

#if defined(_MSC_VER) && defined(_M_X64)
static_assert(sizeof(VmIndexedExpressions::FCache) == 2512, "shared enhanced cache must retain the original x64 stack size");
#endif

namespace
{
	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	void CheckConditionalPlans()
	{
		FBytecodeModule Module;
		const auto Make = [](EOpcode Arithmetic, EOpcode Comparison, std::int32_t Offset = 0)
		{
			FBytecodeFunction Function;
			Function.LocalCount = Function.ParamCount = 3;
			const auto Emit = [&](EOpcode Op) { Function.Code.push_back(static_cast<std::uint8_t>(Op)); };
			const auto Local = [&](std::uint16_t Slot) { Emit(EOpcode::LoadLocal); Bytecode::WriteU16LE(Function.Code, Slot); };
			Local(0); Local(1); Emit(Arithmetic); Local(2); Emit(Comparison);
			Emit(EOpcode::JmpIfFalse); Bytecode::WriteI32LE(Function.Code, Offset); Emit(EOpcode::Return);
			return Function;
		};
		for (const auto Op : {EOpcode::Add, EOpcode::Sub, EOpcode::Mul, EOpcode::Div, EOpcode::Mod,
				 EOpcode::BitAnd, EOpcode::BitOr, EOpcode::BitXor, EOpcode::Shl, EOpcode::Shr})
		{
			for (const auto Comparison : {EOpcode::Eq, EOpcode::Ne, EOpcode::Lt, EOpcode::Le, EOpcode::Gt, EOpcode::Ge})
			{
				const auto Function = Make(Op, Comparison);
				VmIndexedExpressions::FCache Cache;
				std::uint32_t Random = 137u;
				for (int Sample = 0; Sample < 32; ++Sample)
				{
					Random = Random * 1664525u + 1013904223u;
					const auto Left = static_cast<std::int32_t>(Random % 1000u) - 500;
					Random = Random * 1664525u + 1013904223u;
					const auto Right = static_cast<std::int32_t>(Random % 30u) + 1;
					std::int32_t Value = 0;
					switch (Op)
					{
					case EOpcode::Add:
						{
							Value = Left + Right;
							break;
						}
					case EOpcode::Sub:
						{
							Value = Left - Right;
							break;
						}
					case EOpcode::Mul:
						{
							Value = Left * Right;
							break;
						}
					case EOpcode::Div:
						{
							Value = Left / Right;
							break;
						}
					case EOpcode::Mod:
						{
							Value = Left % Right;
							break;
						}
					case EOpcode::BitAnd:
						{
							Value = Left & Right;
							break;
						}
					case EOpcode::BitOr:
						{
							Value = Left | Right;
							break;
						}
					case EOpcode::BitXor:
						{
							Value = Left ^ Right;
							break;
						}
					case EOpcode::Shl:
						{
							Value = static_cast<std::int32_t>(static_cast<std::uint32_t>(Left) << Right);
							break;
						}
					case EOpcode::Shr:
						{
							Value = Left >> Right;
							break;
						}
					default:
						{
							throw std::runtime_error("invalid conditional test operation");
						}
					}
					const auto Other = Sample % 2 == 0 ? Value : Value ^ 7;
					bool Expected = false;
					switch (Comparison)
					{
					case EOpcode::Eq:
						{
							Expected = Value == Other;
							break;
						}
					case EOpcode::Ne:
						{
							Expected = Value != Other;
							break;
						}
					case EOpcode::Lt:
						{
							Expected = Value < Other;
							break;
						}
					case EOpcode::Le:
						{
							Expected = Value <= Other;
							break;
						}
					case EOpcode::Gt:
						{
							Expected = Value > Other;
							break;
						}
					case EOpcode::Ge:
						{
							Expected = Value >= Other;
							break;
						}
					default:
						{
							break;
						}
					}
					const std::vector<FValue> Locals{Left, Right, Other};
					VmIndexedExpressions::FConditionalBranch Branch{999, 17, false};
					Require(Cache.TryExecuteConditional(Left, Module, Function, 3, Locals, 8, Branch) &&
					    Branch.After == 16 && Branch.Offset == 0 && Branch.Condition == Expected &&
					    std::get<std::int32_t>(Locals[0]) == Left && std::get<std::int32_t>(Locals[1]) == Right,
					    "conditional plan changed primitive arithmetic/comparison or input state");
				}
			}
		}
		for (int Failure = 0; Failure < 12; ++Failure)
		{
			auto Function = Make(Failure == 1 || Failure == 2 ? EOpcode::Div : Failure == 3 || Failure == 4 ? EOpcode::Shl : EOpcode::Add, EOpcode::Eq);
			std::vector<FValue> Locals{std::int32_t{7}, std::int32_t{1}, std::int32_t{8}};
			if (Failure == 0)
			{
				Locals[0] = std::numeric_limits<std::int32_t>::max();
			}
			if (Failure == 1)
			{
				Locals[1] = std::int32_t{0};
			}
			if (Failure == 2)
			{
				Locals[0] = std::numeric_limits<std::int32_t>::min();
				Locals[1] = std::int32_t{-1};
			}
			if (Failure == 3)
			{
				Locals[1] = std::int32_t{-1};
			}
			if (Failure == 4)
			{
				Locals[1] = std::int32_t{32};
			}
			if (Failure == 5)
			{
				Locals[1] = std::int64_t{1};
			}
			if (Failure == 6)
			{
				Locals[2] = double{8};
			}
			if (Failure == 8)
			{
				Function.Code.pop_back();
				Function.Code.pop_back();
			}
			if (Failure == 9)
			{
				Function = Make(EOpcode::Add, EOpcode::Eq, -15);
			}
			if (Failure == 10)
			{
				Function = Make(EOpcode::Add, EOpcode::Eq, 2);
			}
			if (Failure == 11)
			{
				Function.ExceptionRegions.resize(1);
			}
			VmIndexedExpressions::FCache Cache;
			VmIndexedExpressions::FConditionalBranch Branch{999, 17, true};
			const auto First = std::get<std::int32_t>(Locals[0]);
			Require(!Cache.TryExecuteConditional(First, Module, Function, 3, Locals, Failure == 7 ? 1 : 8, Branch) &&
			    Branch.After == 999 && Branch.Offset == 17 && Branch.Condition && std::get<std::int32_t>(Locals[0]) == First,
			    "declined conditional plan changed caller state or accepted an invalid boundary");
		}
		const auto Backward = Make(EOpcode::Add, EOpcode::Eq, -16);
		VmIndexedExpressions::FCache BackCache;
		VmIndexedExpressions::FConditionalBranch Branch;
		Require(BackCache.TryExecuteConditional(7, Module, Backward, 3, {std::int32_t{7}, std::int32_t{1}, std::int32_t{9}}, 8, Branch) &&
		    !Branch.Condition && Branch.After == 16 && Branch.Offset == -16, "conditional backedge changed its original branch coordinates");
		auto Shared = Make(EOpcode::Add, EOpcode::Eq);
		Shared.ParamCount = 0;
		VmIndexedExpressions::FCache SharedCache;
		std::vector<FValue> MutableLocals{std::int32_t{7}, std::int32_t{1}, std::int32_t{8}};
		std::size_t Next = 3;
		Require(SharedCache.TryExecuteConditional(7, Module, Shared, Next, MutableLocals, 8, Branch) &&
		    SharedCache.TryExecute(7, Module, Shared, Next, MutableLocals, 8, Branch) == VmIndexedExpressions::EFusionResult::Branch &&
		    Next == 3 && Branch.After == 16 && std::get<std::int32_t>(MutableLocals[0]) == 7,
		    "indexed lookup destroyed a shared condition plan or committed caller state");
		const std::array<std::uint8_t, 3> NumericKinds{1u, 1u, 1u};
		Require(!VmIndexedExpressions::FCache::CanFuseInFunction(Module, Shared, NumericKinds.data(), NumericKinds.size()),
		    "pure condition changed indexed feature classification");
		auto Collision = Make(EOpcode::Add, EOpcode::Eq);
		Collision.Code.resize(24, static_cast<std::uint8_t>(EOpcode::Nop));
		const auto Tail = Make(EOpcode::Add, EOpcode::Eq);
		Collision.Code.insert(Collision.Code.end(), Tail.Code.begin(), Tail.Code.end());
		VmIndexedExpressions::FCache CollisionCache;
		const std::vector<FValue> Locals{std::int32_t{7}, std::int32_t{1}, std::int32_t{8}};
		Require(CollisionCache.TryExecuteConditional(7, Module, Collision, 3, Locals, 8, Branch), "first conditional cache claim failed");
		for (int Repeat = 0; Repeat < 128; ++Repeat)
		{
			Branch = {999, 17, true};
			Require(!CollisionCache.TryExecuteConditional(7, Module, Collision, 27, Locals, 8, Branch) &&
			    !CollisionCache.TryExecuteConditional(7, Module, Collision, 3, Locals, 8, Branch) &&
			    Branch.After == 999 && Branch.Offset == 17 && Branch.Condition,
			    "evicted conditional slot was decoded again or changed caller state");
		}
		auto Declined = Collision;
		Declined.Code[6] = static_cast<std::uint8_t>(EOpcode::Nop);
		VmIndexedExpressions::FCache DeclinedCache, FreshCache;
		Require(!DeclinedCache.TryExecuteConditional(7, Module, Declined, 3, Locals, 8, Branch) &&
		    !DeclinedCache.TryExecuteConditional(7, Module, Declined, 27, Locals, 8, Branch) &&
		    FreshCache.TryExecuteConditional(7, Module, Declined, 27, Locals, 8, Branch),
		    "failed condition did not consume its slot budget or a new run retained an old budget");
		auto Many = Make(EOpcode::Add, EOpcode::Eq);
		Many.Code.clear();
		for (int Index = 0; Index < 16; ++Index)
		{
			Many.Code.insert(Many.Code.end(), Tail.Code.begin(), Tail.Code.end());
		}
		VmIndexedExpressions::FCache Bounded;
		for (std::size_t Index = 0; Index < 16; ++Index)
		{
			const bool Success = Bounded.TryExecuteConditional(7, Module, Many, Index * Tail.Code.size() + 3, Locals, 8, Branch);
			Require(Success == (Index < 8), "shared cache exceeded eight condition decodes in one run");
		}
	}

	void CheckConditionalExecution(const std::shared_ptr<const IVmBackendFactory>& Factory)
	{
		const auto Compare = [&](const std::string& Source, const char* ExpectedOutput = nullptr)
		{
			auto Program = VeroScriptCompile::CompileSource(Source, "vm-conditional.vs");
			Require(Program.bSuccess, Program.Error.Message);
			struct FResult { bool Success, Reset; std::string Output, Error; std::uint32_t Line, Column; std::uint64_t Count; };
			const auto Execute = [&](std::uint64_t Limit, bool Reference)
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				std::string Error;
				Require(Vm.SetVmBackendFactory(Factory, Error), Error);
				Vm.SetExecutionBackend(EExecutionBackend::Vm);
				Vm.SetExecutionLimits({Limit, 0});
				if (Reference)
				{
					Vm.SetExecutionCheck([]()
						{
							return true;
						});
				}
				const auto Success = Vm.Execute(*Program.Module, ERunMode::Script);
				return FResult{Success, Vm.RequiresReset(), Output.str(), Vm.GetError().Message,
				    Vm.GetError().Location.Line, Vm.GetError().Location.Column, Vm.GetExecutedInstructionCount()};
			};
			const auto Reference = Execute(0, true);
			if (ExpectedOutput)
			{
				Require(Reference.Success && Reference.Output == ExpectedOutput, "incorrect independent shared-plan fixture result");
			}
			const auto Plain = Execute(0, false);
			Require(Plain.Success == Reference.Success && Plain.Reset == Reference.Reset && Plain.Output == Reference.Output &&
			    Plain.Error == Reference.Error && Plain.Line == Reference.Line && Plain.Column == Reference.Column,
			    "enhanced conditional execution differs from canonical diagnostics, cleanup or result");
			for (std::uint64_t Limit = 1; Limit <= Reference.Count + 1; ++Limit)
			{
				const auto Expected = Execute(Limit, true), Actual = Execute(Limit, false);
				Require(Actual.Success == Expected.Success && Actual.Reset == Expected.Reset && Actual.Output == Expected.Output &&
				    Actual.Error == Expected.Error && Actual.Line == Expected.Line && Actual.Column == Expected.Column && Actual.Count == Expected.Count,
				    "conditional optimization entered governed execution or changed a canonical instruction boundary");
			}
		};
		Compare("string bounce(string value){return value;}void main(){string text=bounce(\"x\");int seed=7;"
		    "for(int i=0;i<4;i++){if((i+seed)%3==0)text+=\"a\";else text+=\"b\";"
		    "if((i*3-1)<seed)print(i);if((i+seed)>=8)print(i+1);"
		    "if((i<<1)!=seed)print(i+2);if((i>>1)<=1)print(i+3);if((i^seed)>2)print(i+4);}print(text);}");
		Compare("string bounce(string value){return value;}int calls=0;int touch(int value){calls++;return value;}"
		    "void main(){string text=bounce(\"x\");int seed=7;for(int i=0;i<3;i++){"
		    "if((touch(i)+seed)%3==0)text+=\"a\";else text+=\"b\";}print(text);print(calls);}");
		Compare("string bounce(string value){return value;}void main(){string text=bounce(\"x\");"
		    "int x=2147483647;int one=1;if((x+one)==0)print(text);}");
		Compare("string bounce(string value){return value;}void main(){string text=bounce(\"x\");"
		    "int x=7;int zero=0;try{if((x/zero)==0)print(text);}finally{print(x);}}");
		Compare("string bounce(string value){return value;}struct Guard{Guard(){}~Guard(){print(77);}};"
		    "void main(){string text=bounce(\"x\");int x=7;int y=2;try{Guard guard=Guard();"
		    "if((x+y)%3==0)print(text);}finally{if((x+y)>1)print(x);}}");
		Compare("string bounce(string value){return value;}void main(){array<int>a={3,5,7};int sum=0;"
		    "string text=bounce(\"x\");for(int i=0;i<6;i++){sum+=a[i%3];"
		    "if((i+1)%2==0)text+=\"a\";else text+=\"b\";}print(sum);print(text);}", "30\nxbababa\n");
		Compare("void main(){array<int>a={2,4,6};int sum=0;try{for(int i=0;i<3;i++){sum+=a[i];"
		    "if((sum+i)%2==0)print(i);}}finally{print(sum);}}", "0\n2\n12\n");
	}
	void Run(const std::shared_ptr<const IVmBackendFactory>& Factory)
	{
		auto Program = VeroScriptCompile::CompileSource(R"(
int cleaned = 0;
int Compute(int count)
{
	int value = 0;
	for (int i = 0; i < count; ++i) value += i;
	return value;
}
void Exhaust()
{
	try { while (true) {} }
	finally { cleaned = 1; }
}
int Read() { return cleaned; }
struct First { public: int pad = 3; int value = 7; };
struct Second { public: int value = 11; int pad = 5; };
int Fields()
{
	First first = First();
	Second second = Second();
	int total = 0;
	for (int i = 0; i < 10; ++i)
	{
		first.value = first.value + 1;
		second.value = second.value + 2;
		total += first.value + second.value;
	}
	return total;
}
void main() { print("{}", Compute(10)); print("{}", Fields()); }
)",
		    "vm-backend.vs");
		Require(Program.bSuccess, Program.Error.Message);
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Vm.SetExecutionBackend(EExecutionBackend::Vm);
		std::string Error;
		Require(Vm.SetVmBackendFactory(Factory, Error), Error);
		Require(Vm.Execute(*Program.Module, ERunMode::Script), Vm.GetError().Message);
		Require(Output.str().find("45") != std::string::npos, "incorrect loop result");
		Require(Output.str().find("\n345\n") != std::string::npos, "field cache mixed receiver layouts");
		Require(Vm.GetVmExecutionDomain()->IsFrozen(), "selection was not frozen");
		Require(!Vm.SetVmBackendFactory({}, Error), "frozen selection could be cleared");
		Require(!Vm.SetVmBackendPath("missing", Error), "frozen selection could be replaced");
		Require(Vm.GetVmBackendFactory() == Factory, "failed configuration replaced the factory");
		Require(Vm.GetVmBackendStats().Entries != 0, "selected backend was not entered");
	}
	void CheckIdleClassificationLifetime(const std::shared_ptr<const IVmBackendFactory>& Factory)
	{
		const auto Compile = [](const char* Source)
		{
			auto Result = VeroScriptCompile::CompileSource(Source, "vm-idle-classification.vs");
			Require(Result.bSuccess, Result.Error.Message);
			return std::shared_ptr<FBytecodeModule>(std::move(Result.Module));
		};
		for (const bool Owned : {false, true})
		{
			auto Module = Compile("int work(int n){string text=\"\";text+=to_string(n);return len(text)+n;}");
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			Vm.SetExecutionBackend(EExecutionBackend::Vm);
			std::string Error;
			Require(Vm.SetVmBackendFactory(Factory, Error), Error);
			if (Owned)
			{
				Require(Vm.SetJitProgramOwner(Module, Error), Error);
			}
			FValue Value;
			const auto Invoke = [&](int Expected)
			{
				Require(Vm.ExecuteFunction(*Module, 0, {std::int32_t{17}}, nullptr, &Value), Vm.GetError().Message);
				Require(std::get<std::int32_t>(Value) == Expected, "idle classification selected stale code");
			};
			Invoke(19);
			Invoke(19);
			auto Replacement = Compile("int work(int n){array<int> values={n,31};return values[0]+values[1];}");
			if (Owned)
			{
				const std::weak_ptr<const FBytecodeModule> Retired = Module;
				Require(Vm.SetJitProgramOwner(Replacement, Error), Error);
				Module = std::move(Replacement);
				Require(Retired.expired(), "idle backend retained the replaced module");
			}
			else
			{
				*Module = std::move(*Replacement);
			}
			Invoke(48);
			Vm.SetProgramGeneration(2);
			Invoke(48);
			const std::weak_ptr<const FBytecodeModule> Retired = Module;
			Require(Vm.SetJitProgramOwner({}, Error), Error);
			Module.reset();
			Require(Retired.expired(), "idle backend retained the retired module");
			Module = Compile("int work(int n){return n+5;}");
			Invoke(22);
		}
	}
}

int main(int Argc, char** Argv)
{
	try
	{
		const auto Factory = BytecodeVm::CreateFactory(GetVmHostServices());
		CheckConditionalPlans();
		Require(Factory->GetDescription().Id == "vs-bytecode", "incorrect official ID");
		Run(Factory);
		CheckIdleClassificationLifetime(Factory);
		CheckConditionalExecution(Factory);
		if (Argc > 1)
		{
			std::string Error, Resolved;
			const auto Dynamic = LoadVmBackend(Argv[1], Error, &Resolved);
			Require(Dynamic != nullptr, Error);
			Require(!Resolved.empty(), "dynamic module path missing");
			Run(Dynamic);
			CheckIdleClassificationLifetime(Dynamic);
			CheckConditionalExecution(Dynamic);
		}
		std::ostringstream Output;
		VeroScriptVm Unconfigured(Output);
		auto Code = VeroScriptCompile::CompileSource("int main() { return 3; }");
		Require(Code.bSuccess, Code.Error.Message);
		Require(!Unconfigured.Execute(*Code.Module, ERunMode::Script), "Core executed without a VM selection");
		Require(!Unconfigured.GetVmExecutionDomain()->IsFrozen(), "preflight failure froze the domain");
		std::string Error;
		Require(Unconfigured.SetVmBackendFactory(Factory, Error), Error);
		Require(Unconfigured.Execute(*Code.Module, ERunMode::Script), Unconfigured.GetError().Message);
		std::cout << "VM backend static/dynamic/selection checks passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
