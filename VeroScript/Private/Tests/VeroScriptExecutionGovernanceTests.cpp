// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptExecutionGovernance.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptBytecodeLinker.h"
#include "VeroScriptCompile.h"
#include "VeroScriptPath.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptPackage.h"
#include "VeroScriptVm.h"
#include "ExecutionSupport/VeroScriptSha256.h"
#include "../../../Modules/Execution/VeroScriptBytecodeVm/Private/VeroScriptVmLocalExpressions.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>

using namespace VeroScript;

namespace
{
	bool CheckLocalShiftExpressions()
	{
		FBytecodeModule Module;
		for (const auto Opcode : {EOpcode::Shl, EOpcode::Shr})
		{
			FBytecodeFunction Function;
			Function.LocalCount = 3;
			const auto Emit = [&](EOpcode Op) { Function.Code.push_back(static_cast<std::uint8_t>(Op)); };
			const auto Slot = [&](EOpcode Op, std::uint16_t Index) { Emit(Op); Bytecode::WriteU16LE(Function.Code, Index); };
			Slot(EOpcode::LoadLocal, 0);
			Slot(EOpcode::LoadLocal, 1);
			Emit(Opcode);
			Slot(EOpcode::LoadLocal, 0);
			Emit(EOpcode::BitXor);
			Slot(EOpcode::StoreLocal, 2);
			VmLocalExpressions::FCache Cache;
			const auto ExpectedShift = [&](std::int32_t Left, std::int32_t Count)
			{
				// An independent arithmetic reference for the canonical 32-bit bits.
				const std::int64_t Scale = std::int64_t{1} << Count;
				if (Opcode == EOpcode::Shl)
				{
					const auto Bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(Left)) * Scale;
					return static_cast<std::int32_t>(static_cast<std::uint32_t>(Bits));
				}
				return static_cast<std::int32_t>(Left / Scale - (Left < 0 && Left % Scale != 0 ? 1 : 0));
			};
			std::vector<std::int32_t> Counts{std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()};
			for (std::int32_t Count = -1; Count <= 32; ++Count)
			{
				Counts.push_back(Count);
			}
			for (const auto Left : {std::numeric_limits<std::int32_t>::min(), -7, 0, 1, std::numeric_limits<std::int32_t>::max()})
			{
				for (const auto Count : Counts)
				{
					std::vector<FValue> Locals{Left, Count, std::int32_t{-99}};
					std::size_t Next = 3;
					const bool Valid = Count >= 0 && Count < 32;
					const auto Success = Cache.TryExecute(Left, Module, Function, Next, Locals, 8);
					if (Success != Valid || Next != (Valid ? Function.Code.size() : 3u) ||
					    std::get<std::int32_t>(Locals[0]) != Left || std::get<std::int32_t>(Locals[1]) != Count ||
					    std::get<std::int32_t>(Locals[2]) != (Valid ? ExpectedShift(Left, Count) ^ Left : -99))
					{
						std::cerr << "local shift changed its width, bits, count diagnostic, or transaction boundary\n";
						return false;
					}
				}
			}
			for (int Failure = 0; Failure < 7; ++Failure)
			{
				std::vector<FValue> Locals{std::int32_t{-7}, std::int32_t{1}, std::int32_t{-99}};
				if (Failure == 0)
				{
					Locals[1] = std::int32_t{-1};
				}
				if (Failure == 1)
				{
					Locals[1] = std::int32_t{32};
				}
				if (Failure == 2)
				{
					Locals[1] = std::int64_t{1};
				}
				if (Failure == 3)
				{
					Locals[1] = std::uint32_t{1};
				}
				if (Failure == 4)
				{
					Locals[1] = double{1};
				}
				if (Failure == 5)
				{
					Locals[2] = std::int64_t{-99};
				}
				const auto Room = Failure == 6 ? 1u : 8u;
				std::size_t Next = 3;
				if (Cache.TryExecute(-7, Module, Function, Next, Locals, Room) || Next != 3 ||
				    std::get<std::int32_t>(Locals[0]) != -7 ||
				    (Failure == 5 ? std::get<std::int64_t>(Locals[2]) : std::get<std::int32_t>(Locals[2])) != -99)
				{
					std::cerr << "declined shift changed state\n";
					return false;
				}
			}
		}
		struct FArithmeticFailure
		{
			EOpcode Opcode;
			std::int32_t Left, Right;
		};
		for (const auto& Failure : {
		         FArithmeticFailure{EOpcode::Add, std::numeric_limits<std::int32_t>::max(), 1},
		         FArithmeticFailure{EOpcode::Sub, std::numeric_limits<std::int32_t>::min(), 1},
		         FArithmeticFailure{EOpcode::Mul, std::numeric_limits<std::int32_t>::max(), 2},
		         FArithmeticFailure{EOpcode::Div, 7, 0},
		         FArithmeticFailure{EOpcode::Div, std::numeric_limits<std::int32_t>::min(), -1},
		         FArithmeticFailure{EOpcode::Mod, 7, 0}})
		{
			FBytecodeFunction Function;
			Function.LocalCount = 3;
			Function.Code = {static_cast<std::uint8_t>(EOpcode::LoadLocal), 0, 0,
			    static_cast<std::uint8_t>(EOpcode::LoadLocal), 1, 0,
			    static_cast<std::uint8_t>(Failure.Opcode),
			    static_cast<std::uint8_t>(EOpcode::StoreLocal), 2, 0};
			VmLocalExpressions::FCache Cache;
			std::vector<FValue> Locals{Failure.Left, Failure.Right, std::int32_t{-99}};
			std::size_t Next = 3;
			if (Cache.TryExecute(Failure.Left, Module, Function, Next, Locals, 8) || Next != 3 ||
			    std::get<std::int32_t>(Locals[0]) != Failure.Left ||
			    std::get<std::int32_t>(Locals[1]) != Failure.Right || std::get<std::int32_t>(Locals[2]) != -99)
			{
				std::cerr << "declined arithmetic changed state\n";
				return false;
			}
		}
		return true;
	}

	bool CheckGovernedArithmeticBoundaries()
	{
		struct FResult
		{
			bool Success, Reset;
			std::uint64_t Count;
			std::string Output, ErrorId, Error;
			std::uint32_t Line, Column;
			std::size_t CleanupErrors;
		};
		const auto Run = [](const FBytecodeModule& Module, std::uint64_t Limit, bool Reference, int Policy)
		{
			std::ostringstream Out;
			VeroScriptDebugger Debugger;
			VeroScriptVm Vm(Out);
			Test::Configure(Vm);
			Vm.SetExecutionBackend(EExecutionBackend::Vm);
			Vm.SetExecutionLimits({Limit, Policy == 4 ? 0ull : 30000ull});
			if (Policy != 4)
			{
				Vm.SetExecutionControl(std::make_shared<FExecutionControl>());
			}
			// A synchronous host check forces canonical dispatch, providing a
			// reference which still charges and polls every original instruction.
			if (Reference)
			{
				Vm.SetExecutionCheck([]()
					{
						return true;
					});
			}
			if (Policy == 1)
			{
				Vm.SetMutationCallback([](const FMutationEvent &)
					{
					});
			}
			if (Policy == 2)
			{
				Vm.SetDebugger(&Debugger);
			}
			if (Policy == 3)
			{
				Vm.SetInstanceCallInterceptor([](const FInstanceCallInterceptContext&, FValue&, FScriptError&)
				{
					return EInstanceCallInterceptResult::Unhandled;
				});
			}
			const bool Success = Vm.Execute(Module, ERunMode::Script);
			const auto Error = Vm.GetError();
			return FResult{Success, Vm.RequiresReset(), Vm.GetExecutedInstructionCount(), Out.str(),
			    Error.MessageId, Error.Message, Error.Location.Line, Error.Location.Column, Vm.GetCleanupDiagnostics().size()};
		};
		for (const auto* Source : {
		    "void work(){int x=7;int y=2;try{x=(x+y)*(x-1)%11;}finally{print(x);}}work();",
		    "struct Guard{Guard(){}~Guard(){print(77);}};void work(){Guard g=Guard();int x=7;int y=2;try{x=(x+y)*(x-1)%11;}finally{print(x);}}work();",
		    "void work(int y){int x=7;try{x=(x+y)/(y-y);}finally{print(x);}}work(2);",
		    "void work(){int x=2147483647;int y=1;try{x=(x+y)*2;}finally{print(x);}}work();",
		    "void work(int y){int x=-2147483647-1;try{x=((x<<y)^(x>>y))&2147483647;}finally{print(x);}}work(31);",
		    "struct Guard{Guard(){}~Guard(){print(77);}};void work(int y){Guard g=Guard();int x=7;try{x=(x<<y)^(x>>y);}finally{print(x);}}work(3);",
		    "void work(int y){int x=7;try{x=(x<<y)^3;}finally{print(x);}}work(32);",
		    "void work(int y){int x=7;try{x=(x>>y)^3;}finally{print(x);}}work(-1);",
		    "void work(){int x=7;int y=2;try{for(;;){}}finally{x=(x+y)*(x-1)%11;print(x);}}work();"})
		{
			auto Compilation = VeroScriptCompile::CompileSource(Source, "governed-arithmetic-prefix.vs");
			if (!Compilation.bSuccess)
			{
				std::cerr << Compilation.Error.Message << '\n';
				return false;
			}
			const bool Infinite = std::string_view(Source).find("for(;;)") != std::string_view::npos;
			const auto Base = Run(*Compilation.Module, Infinite ? 48 : 0, true, 0);
			for (int Policy = 0; Policy != 5; ++Policy)
			{
				for (std::uint64_t Limit = 1; Limit <= (Infinite ? 48 : Base.Count + 1); ++Limit)
				{
					const auto Reference = Run(*Compilation.Module, Limit, true, Policy);
					const auto Optimized = Run(*Compilation.Module, Limit, false, Policy);
					if (Reference.Success != Optimized.Success || Reference.Reset != Optimized.Reset ||
					    Reference.Count != Optimized.Count || Reference.Output != Optimized.Output ||
					    Reference.ErrorId != Optimized.ErrorId || Reference.Error != Optimized.Error ||
					    Reference.Line != Optimized.Line || Reference.Column != Optimized.Column ||
					    Reference.CleanupErrors != Optimized.CleanupErrors)
					{
						std::cerr << "governed arithmetic changed a VM boundary, quota=" << Limit << ", policy=" << Policy << '\n';
						return false;
					}
				}
			}
		}
		return true;
	}

	bool CheckRetainedConstructAliases()
	{
		auto Compilation = VeroScriptCompile::CompileSource(
		    "class Source{public:int value=42;};"
		    "class Wrapper{public:int value;Wrapper(Source source){value=source.value;}};void main(){}",
		    "retained-construct-alias.vs");
		if (!Compilation.bSuccess)
		{
			std::cerr << Compilation.Error.Message << '\n';
			return false;
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Test::Configure(Vm);
		FObjectHandle Source;
		if (!Vm.ConstructObject(*Compilation.Module, "Source", {}, Source, nullptr) || !Source)
		{
			return false;
		}
		std::vector<FValue> Arguments{Source};
		FObjectHandle Control;
		if (!Vm.ConstructObject(*Compilation.Module, "Wrapper", Arguments, Control, nullptr) || !Control || std::get<std::int32_t>(Control->Fields[0]) != 42)
		{
			return false;
		}
		const std::weak_ptr<FScriptObject> Weak = Source;
		Source.reset();
		auto& AliasedOutput = std::get<FObjectHandle>(Arguments[0]);
		if (!Vm.ConstructObject(*Compilation.Module, "Wrapper", Arguments, AliasedOutput, nullptr) || !AliasedOutput || std::get<std::int32_t>(AliasedOutput->Fields[0]) != 42 || !Weak.expired() || Vm.HasError())
		{
			std::cerr << "construction changed its aliased argument before consuming it\n";
			return false;
		}
		if (Vm.ConstructObject(*Compilation.Module, "Wrapper", {}, AliasedOutput, nullptr) || AliasedOutput || !Vm.HasError())
		{
			return false;
		}
		AliasedOutput = Control;
		if (Vm.ConstructObject(*Compilation.Module, "", {}, AliasedOutput, nullptr) || AliasedOutput || !Vm.HasError())
		{
			return false;
		}
		return true;
	}

	bool CheckRetainedReadAliases()
	{
		auto Compilation = VeroScriptCompile::CompileSource(
		    "class Owner{public:int first=42;int second=7;};void main(){}", "retained-read-alias.vs");
		if (!Compilation.bSuccess)
		{
			std::cerr << Compilation.Error.Message << '\n';
			return false;
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Test::Configure(Vm);
		FObjectHandle Object;
		if (!Vm.ConstructObject(*Compilation.Module, "Owner", {}, Object, nullptr) || !Object)
		{
			return false;
		}
		if (!Vm.ReadObjectField(*Compilation.Module, Object, "first", Object->Fields[0]) || std::get<std::int32_t>(Object->Fields[0]) != 42 || !Vm.ReadObjectField(*Compilation.Module, Object, "second", Object->Fields[0]) || std::get<std::int32_t>(Object->Fields[0]) != 7 || std::get<std::int32_t>(Object->Fields[1]) != 7)
		{
			std::cerr << "retained read changed its aliased source before reading\n";
			return false;
		}
		FValue Value = std::string{"sentinel"};
		if (Vm.ReadObjectField(*Compilation.Module, Object, "missing", Value) || std::get<std::int32_t>(Value) != 0 || Vm.GetError().MessageId != "diag.unknown_field")
		{
			return false;
		}
		Value = std::string{"sentinel"};
		if (Vm.ReadObjectField(*Compilation.Module, {}, "first", Value) || std::get<std::int32_t>(Value) != 0 || !Vm.HasError())
		{
			return false;
		}
		Value = std::string{"first"};
		const auto& Name = std::get<std::string>(Value);
		if (!Vm.ReadObjectField(*Compilation.Module, Object, Name, Value) || std::get<std::int32_t>(Value) != 7 || Vm.HasError())
		{
			return false;
		}
		// Replacing the only host-owned receiver value must not invalidate the
		// borrowed FObjectHandle reference while the read is still in progress.
		FObjectHandle OnlyOwner;
		if (!Vm.ConstructObject(*Compilation.Module, "Owner", {}, OnlyOwner, nullptr) || !OnlyOwner)
		{
			return false;
		}
		const std::weak_ptr<FScriptObject> Weak = OnlyOwner;
		FValue Holder = std::move(OnlyOwner);
		if (!Vm.ReadObjectField(*Compilation.Module, std::get<FObjectHandle>(Holder), "first", Holder) || std::get<std::int32_t>(Holder) != 42 || !Weak.expired() || Vm.HasError())
		{
			std::cerr << "retained read did not preserve its aliased receiver lifetime\n";
			return false;
		}
		return true;
	}

	bool CheckRetainedReadAfterCleanup()
	{
		auto Compilation = VeroScriptCompile::CompileSource(
		    "class Owner{public:int value=42;};Owner root=Owner();int cleaned=0;"
		    "void exhaust(){try{for(;;){}}finally{cleaned=1;}}void main(){}",
		    "retained-read-cleanup.vs");
		if (!Compilation.bSuccess)
		{
			std::cerr << Compilation.Error.Message << '\n';
			return false;
		}
		FHotReloadState Persistent;
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Test::Configure(Vm);
		if (!Vm.ExecutePersistent(*Compilation.Module, ERunMode::Script, Persistent)) return false;
		FObjectHandle Object;
		for (const auto& Value : Persistent.Globals)
		{
			if (const auto* Handle = std::get_if<FObjectHandle>(&Value); Handle && *Handle)
			{
				Object = *Handle;
			}
		}
		if (!Object) return false;
		const auto Exhaust = [&]()
		{
			Vm.SetExecutionLimits({20, 0});
			const bool Failed = !Vm.ExecuteFreeFunction(*Compilation.Module, "exhaust", {}, &Persistent);
			Vm.SetExecutionLimits({0, 0});
			return Failed && Vm.HasError() && !Vm.RequiresReset() && !Persistent.bExecutionResetRequired && Vm.GetError().MessageId == "diag.runtime_execution_instruction_limit_exceeded";
		};
		FValue ReadValue = std::int32_t{-1};
		if (!Exhaust() || !Vm.ReadObjectField(*Compilation.Module, Object, "value", ReadValue) || std::get<std::int32_t>(ReadValue) != 42 || Vm.HasError() || !Vm.GetError().Message.empty() || !Vm.GetCleanupDiagnostics().empty() || !Exhaust() || !Vm.ValidateObjectLayout(*Compilation.Module, Object) || Vm.HasError() || !Vm.GetError().Message.empty() || !Vm.GetCleanupDiagnostics().empty())
		{
			std::cerr << "retained read retained the preceding completed cleanup failure: "
			          << Vm.GetError().Message << '\n';
			return false;
		}
		Object.reset();
		return Vm.DiscardPersistentState(*Compilation.Module, Persistent);
	}

	bool CheckRejectedRequestsAfterCleanup()
	{
		auto Compilation = VeroScriptCompile::CompileSource(
		    "class Owner{public:int value=42;};Owner root=Owner();int cleaned=0;"
		    "void exhaust(){try{for(;;){}}finally{cleaned=1;}}int read(){return cleaned;}void main(){}",
		    "rejected-request-cleanup.vs");
		if (!Compilation.bSuccess)
		{
			std::cerr << Compilation.Error.Message << '\n';
			return false;
		}
		FBytecodeModule MissingDependency = *Compilation.Module;
		MissingDependency.RequiredExternalDependencies.push_back(
		    {"cleanup.required", "0.1.1", "test-abi", "test-build", "test-content"});
		FHotReloadState Persistent;
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		Test::Configure(Vm);
		if (!Vm.ExecutePersistent(*Compilation.Module, ERunMode::Script, Persistent)) return false;
		FObjectHandle Object;
		for (const auto& Value : Persistent.Globals)
		{
			if (const auto* Handle = std::get_if<FObjectHandle>(&Value); Handle && *Handle)
			{
				Object = *Handle;
			}
		}
		if (!Object || Compilation.Module->MainFunctionIndex < 0) return false;
		const auto MainIndex = static_cast<std::uint16_t>(Compilation.Module->MainFunctionIndex);
		const std::vector<FValue> TooMany(256, std::int32_t{0});
		const std::vector<FValue> OneArgument{std::int32_t{0}};
		std::vector<FValue> WriteBack;
		const auto Callable = std::make_shared<FFunctionValue>();
		for (int Operation = 0; Operation < 23; ++Operation)
		{
			Vm.SetExecutionLimits({20, 0});
			if (Vm.ExecuteFreeFunction(*Compilation.Module, "exhaust", {}, &Persistent) || !Vm.HasError() || Vm.RequiresReset() || Persistent.bExecutionResetRequired || Vm.GetError().MessageId != "diag.runtime_execution_instruction_limit_exceeded")
			{
				return false;
			}
			Vm.SetExecutionLimits({0, 0});
			FValue Value = std::int32_t{-1};
			FValue InvalidReference = FThisFieldRef{};
			FObjectHandle Constructed = Object;
			bool Succeeded = false;
			const char* Expected = "";
			switch (Operation)
			{
			case 0:
				{
					Expected = "unknown field";
					Succeeded = Vm.ReadObjectField(*Compilation.Module, Object, "missing", Value);
					break;
				}
			case 1:
				{
					Expected = "ReadObjectField requires";
					Succeeded = Vm.ReadObjectField(*Compilation.Module, {}, "value", Value);
					break;
				}
			case 2:
				{
					Expected = "ReadObjectField requires";
					Succeeded = Vm.ReadObjectField(*Compilation.Module, Object, "", Value);
					break;
				}
			case 3:
				{
					Expected = "object layout validation requires";
					Succeeded = Vm.ValidateObjectLayout(*Compilation.Module, {});
					break;
				}
			case 4:
				{
					Expected = "unknown field";
					Succeeded = Vm.WriteObjectField(*Compilation.Module, Object, "missing", Value);
					break;
				}
			case 5:
				{
					Expected = "WriteObjectField requires";
					Succeeded = Vm.WriteObjectField(*Compilation.Module, {}, "value", Value);
					break;
				}
			case 6:
				{
					Expected = "WriteObjectField requires";
					Succeeded = Vm.WriteObjectField(*Compilation.Module, Object, "", Value);
					break;
				}
			case 7:
				{
					Expected = "field write requires";
					Succeeded = Vm.WriteObjectFields(*Compilation.Module, {}, {});
					break;
				}
			case 8:
				{
					Expected = "unknown or duplicate field";
					Succeeded = Vm.WriteObjectFields(*Compilation.Module, Object, {{"missing", Value}});
					break;
				}
			case 9:
				{
					Expected = "invalid retained reference storage";
					Succeeded = Vm.AssignRetainedValue(*Compilation.Module, InvalidReference, Value);
					break;
				}
			case 10:
				{
					Expected = "ConstructObject requires";
					Succeeded = Vm.ConstructObject(*Compilation.Module, "", {}, Constructed, &Persistent);
					break;
				}
			case 11:
				{
					Expected = "ConstructObject supports";
					Succeeded = Vm.ConstructObject(*Compilation.Module, "Owner", TooMany, Constructed, &Persistent);
					break;
				}
			case 12:
				{
					Expected = "instance method supports";
					Succeeded = Vm.ExecuteInstanceMethod(*Compilation.Module, Object, "missing", TooMany, &Persistent);
					break;
				}
			case 13:
				{
					Expected = "instance method write-back requires";
					Succeeded = Vm.ExecuteInstanceMethod(*Compilation.Module, Object, "missing", OneArgument,
					    &Persistent, nullptr, nullptr, nullptr, &WriteBack);
					break;
				}
			case 14:
				{
					Expected = "free function supports";
					Succeeded = Vm.ExecuteFreeFunction(*Compilation.Module, "read", TooMany, &Persistent);
					break;
				}
			case 15:
				{
					Expected = "free function write-back requires";
					Succeeded = Vm.ExecuteFreeFunction(*Compilation.Module, "read", OneArgument,
					    &Persistent, nullptr, nullptr, nullptr, &WriteBack);
					break;
				}
			case 16:
				{
					Expected = "unbound script callable";
					Succeeded = Vm.ExecuteCallable(*Compilation.Module, {}, {}, &Persistent);
					break;
				}
			case 17:
				{
					Expected = "script callable supports";
					Succeeded = Vm.ExecuteCallable(*Compilation.Module, Callable, TooMany, &Persistent);
					break;
				}
			case 18:
				{
					Expected = "script callable write-back requires";
					Succeeded = Vm.ExecuteCallable(*Compilation.Module, Callable, OneArgument,
					    &Persistent, nullptr, nullptr, nullptr, &WriteBack);
					break;
				}
			case 19:
				{
					Expected = "bound instance call has an invalid";
					Succeeded = Vm.ExecuteBoundInstanceFunction(*Compilation.Module, {}, MainIndex, &Persistent);
					break;
				}
			case 20:
				{
					Expected = "bound instance entry must be";
					Succeeded = Vm.ExecuteBoundInstanceFunction(*Compilation.Module, Object, MainIndex, &Persistent);
					break;
				}
			case 21:
				{
					Expected = "missing external dependency: cleanup.required";
					Succeeded = Vm.ExecuteFreeFunction(MissingDependency, "read", {}, &Persistent);
					break;
				}
			case 22:
				{
					Expected = "bound instance call has an invalid";
					Succeeded = Vm.ExecuteBoundInstanceFunction(*Compilation.Module, Object,
					    std::numeric_limits<std::uint16_t>::max(), &Persistent);
					break;
				}

			default:
				{
					break;
				}
			}
			if (Succeeded || !Vm.HasError() || Vm.RequiresReset() || Persistent.bExecutionResetRequired || !Vm.GetCleanupDiagnostics().empty() || Vm.GetError().Message.find(Expected) == std::string::npos || ((Operation == 10 || Operation == 11) && Constructed))
			{
				std::cerr << "request " << Operation << " retained a completed cleanup failure: "
				          << Vm.GetError().Message << '\n';
				return false;
			}
			if (!Vm.ExecuteFreeFunction(*Compilation.Module, "read", {}, &Persistent, nullptr, &Value) || std::get<std::int32_t>(Value) != 1 || Vm.HasError() || Vm.RequiresReset() || !Vm.GetError().Message.empty() || !Vm.GetCleanupDiagnostics().empty())
			{
				return false;
			}
		}
		Object.reset();
		return Vm.DiscardPersistentState(*Compilation.Module, Persistent);
	}

	bool CheckRetainedOperationsAfterReset()
	{
		auto Compilation = VeroScriptCompile::CompileSource(
		    "class Owner{public:int first=1;int second=2;};Owner root=Owner();int retained=3;"
		    "void exhaust(){try{for(;;){}}finally{for(;;){}}}void main(){}",
		    "retained-write-reset.vs");
		if (!Compilation.bSuccess)
		{
			std::cerr << Compilation.Error.Message << '\n';
			return false;
		}
		FHotReloadState Persistent;
		const auto FindValues = [&](FObjectHandle& Object, FValue*& Scalar)
		{
			for (auto& Value : Persistent.Globals)
			{
				if (const auto* Handle = std::get_if<FObjectHandle>(&Value); Handle && *Handle)
				{
					Object = *Handle;
				}
				else if (std::holds_alternative<std::int32_t>(Value))
				{
					Scalar = &Value;
				}
			}
			return Object && Object->Fields.size() == 2 && Scalar;
		};
		const auto ValuesEqual = [](const FObjectHandle& Object, const FValue& Scalar,
		                             const std::int32_t First, const std::int32_t Second, const std::int32_t Retained)
		{
			return std::get<std::int32_t>(Object->Fields[0]) == First && std::get<std::int32_t>(Object->Fields[1]) == Second && std::get<std::int32_t>(Scalar) == Retained;
		};
		std::ostringstream Output;
		{
			VeroScriptVm Vm(Output);
			Test::Configure(Vm);
			if (!Vm.ExecutePersistent(*Compilation.Module, ERunMode::Script, Persistent)) return false;
			FObjectHandle Object;
			FValue* Scalar = nullptr;
			if (!FindValues(Object, Scalar) || !ValuesEqual(Object, *Scalar, 1, 2, 3)) return false;
			Vm.SetExecutionLimits({100, 0});
			if (Vm.ExecuteFreeFunction(*Compilation.Module, "exhaust", {}, &Persistent) || !Vm.RequiresReset() || !Persistent.bExecutionResetRequired || Vm.GetError().MessageId != "diag.runtime_execution_instruction_limit_exceeded")
			{
				std::cerr << "cleanup exhaustion did not require reset: " << Vm.GetError().Message << '\n';
				return false;
			}
			Object.reset();
			Scalar = nullptr;
			if (!FindValues(Object, Scalar) || !ValuesEqual(Object, *Scalar, 1, 2, 3)) return false;
			const auto Rejected = [&](const bool Succeeded, const char* Operation)
			{
				if (!Succeeded && Vm.HasError() && Vm.RequiresReset() && Persistent.bExecutionResetRequired && Vm.GetError().MessageId == "diag.execution_state_reset_required" && ValuesEqual(Object, *Scalar, 1, 2, 3))
				{
					return true;
				}
				std::cerr << Operation << " accepted a retained operation after cleanup exhaustion: "
				          << Vm.GetError().Message << '\n';
				return false;
			};
			// The output may alias retained storage; rejection must precede clearing it.
			if (!Rejected(Vm.ValidateObjectLayout(*Compilation.Module, Object), "ValidateObjectLayout") || !Rejected(Vm.ReadObjectField(*Compilation.Module, Object, "first", Object->Fields[0]), "ReadObjectField") || !Rejected(Vm.WriteObjectField(*Compilation.Module, Object, "first", std::int32_t{11}), "WriteObjectField") || !Rejected(Vm.WriteObjectFields(*Compilation.Module, Object, {{"first", std::int32_t{21}}, {"second", std::int32_t{22}}}), "WriteObjectFields") || !Rejected(Vm.AssignRetainedValue(*Compilation.Module, *Scalar, std::int32_t{31}), "AssignRetainedValue"))
			{
				return false;
			}
			FObjectHandle* Root = nullptr;
			for (auto& Value : Persistent.Globals)
			{
				if (auto* Handle = std::get_if<FObjectHandle>(&Value); Handle && Handle->get() == Object.get())
				{
					Root = Handle;
				}
			}
			if (!Root || !Rejected(Vm.ConstructObject(*Compilation.Module, "Owner", {}, *Root, &Persistent), "ConstructObject") || Root->get() != Object.get())
			{
				return false;
			}
			VeroScriptVm NewVm(Output);
			Test::Configure(NewVm);
			if (NewVm.ConstructObject(*Compilation.Module, "Owner", {}, *Root, &Persistent) || Root->get() != Object.get() || !NewVm.RequiresReset() || NewVm.GetError().MessageId != "diag.execution_state_reset_required")
			{
				std::cerr << "rejected construction changed retained output before checking the state reset latch\n";
				return false;
			}
			Object.reset();
			if (!Vm.DiscardPersistentState(*Compilation.Module, Persistent)) return false;
		}
		// Standalone Core resets by discarding the unusable VM and retained state.
		VeroScriptVm FreshVm(Output);
		Test::Configure(FreshVm);
		if (!FreshVm.ExecutePersistent(*Compilation.Module, ERunMode::Script, Persistent)) return false;
		FObjectHandle Object;
		FValue* Scalar = nullptr;
		FValue ReadValue = std::int32_t{-1};
		if (!FindValues(Object, Scalar) || !FreshVm.WriteObjectField(*Compilation.Module, Object, "first", std::int32_t{11}) || !ValuesEqual(Object, *Scalar, 11, 2, 3) || !FreshVm.WriteObjectFields(*Compilation.Module, Object, {{"first", std::int32_t{21}}, {"second", std::int32_t{22}}}) || !ValuesEqual(Object, *Scalar, 21, 22, 3) || !FreshVm.AssignRetainedValue(*Compilation.Module, *Scalar, std::int32_t{31}) || !ValuesEqual(Object, *Scalar, 21, 22, 31) || !FreshVm.ReadObjectField(*Compilation.Module, Object, "first", ReadValue) || std::get<std::int32_t>(ReadValue) != 21 || FreshVm.HasError() || FreshVm.RequiresReset())
		{
			std::cerr << "fresh Core state rejected a retained operation: " << FreshVm.GetError().Message << '\n';
			return false;
		}
		Object.reset();
		return FreshVm.DiscardPersistentState(*Compilation.Module, Persistent);
	}
}

int main()
{
	if (!CheckLocalShiftExpressions()) return 25;
	if (!CheckGovernedArithmeticBoundaries()) return 24;
	FExecutionGovernanceState State;
	const auto External = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	State.Begin({0, std::numeric_limits<std::uint64_t>::max()}, External);
	if (State.Deadline != External || !State.Charge(2, 3) || State.Charge(2, 3) || State.ExecutedInstructions != 2) return 1;
	State.ExecutedInstructions = std::numeric_limits<std::uint64_t>::max();
	if (State.Charge(1, 0)) return 2;
	{
		FExecutionGovernanceState PollState;
		auto Control = std::make_shared<FExecutionControl>();
		EExecutionInterruption Reason = EExecutionInterruption::Timeout;
		unsigned Interruptions = 0;
		const auto Interrupted = [&](EExecutionInterruption Value)
		{
			Reason = Value;
			++Interruptions;
			return false;
		};
		const auto Unlimited = std::chrono::steady_clock::time_point::max();
		if (!ExecutionDetail::PollCancellationAndDeadline(PollState, Control, Unlimited, Interrupted) || Interruptions) return 20;
		PollState.Deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
		Control->RequestCancel();
		if (ExecutionDetail::PollCancellationAndDeadline(PollState, Control, Unlimited, Interrupted) || Reason != EExecutionInterruption::Cancelled || Interruptions != 1)
		{
			return 21;
		}
		Control->Reset();
		if (ExecutionDetail::PollCancellationAndDeadline(PollState, Control, Unlimited, Interrupted) || Reason != EExecutionInterruption::Timeout || Interruptions != 2 || PollState.ExecutedInstructions)
		{
			return 22;
		}
	}
	const auto Hash = [](std::string_view Text, const char* Expected)
	{
		std::string Digest;
		return ExecutionSupport::Sha256(reinterpret_cast<const std::uint8_t*>(Text.data()), Text.size(), Digest) && Digest == Expected;
	};
	if (!Hash("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") ||
	    !Hash("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") ||
	    !Hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"))
	{
		return 3;
	}
	std::string Digest;
	if (ExecutionSupport::Sha256(nullptr, 0, Digest, {[](void*) noexcept
	{
		return false;
	}, nullptr}))
	{
		return 4;
	}
	FBytecodeModule Root, Dependency;
	Dependency.InitCode = {static_cast<std::uint8_t>(EOpcode::Nop), static_cast<std::uint8_t>(EOpcode::ConstBool), 1, static_cast<std::uint8_t>(EOpcode::Pop)};
	Dependency.InitPauseMarkerOffsets = {0};
	Dependency.PackageComponents.push_back({"library", 1});
	Dependency.PackageInitSegments.push_back({"library", 1, 4});
	std::unique_ptr<FBytecodeModule> Linked;
	std::string Error;
	if (!LinkBytecodeModules(Root, {&Dependency}, Linked, Error) || Linked->InitPauseMarkerOffsets != std::vector<std::uint32_t>{0})
	{
		std::cerr << "split initializer lost its cost marker: " << Error << '\n';
		return 5;
	}
	Dependency.PackageInitSegments.front().BeginOffset = 0;
	auto Inconsistent = Dependency;
	Inconsistent.InitPauseMarkerOffsets.clear();
	if (LinkBytecodeModules(Root, {&Dependency, &Inconsistent}, Linked, Error))
	{
		return 6;
	}
	// Importing a stripped library must not reconstruct costs from missing debug
	// lines. Exercise normal parsing, package linking and compiler peepholes.
	auto Library = VeroScriptCompile::CompileSource("export int cost_value(){int x=0;for(int i=0;i<4;i++)x+=i;return x;}", "cost-library.vs");
	if (!Library.bSuccess) return 7;
	const auto Temporary = std::filesystem::temp_directory_path() / ("veroscript-costs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(Temporary);
	struct FCleanup
	{
		std::filesystem::path Path;
		~FCleanup()
		{
			std::error_code E;
			std::filesystem::remove_all(Path, E);
		}
	} Cleanup{Temporary};
	std::uint64_t Counts[2]{};
	for (int I = 0; I != 2; ++I)
	{
		FPackageBuildOptions Options;
		Options.Kind = EPackageKind::Library;
		Options.PackageName = "cost-library";
		Options.bStripDebugInfo = I != 0;
		std::vector<std::uint8_t> Bytes;
		if (!BuildPackage(*Library.Module, Options, Bytes, Error))
		{
			std::cerr << Error << '\n';
			return 8;
		}
		const auto File = Temporary / (I ? "stripped.vslib" : "debug.vslib");
		{
			std::ofstream Stream(File, std::ios::binary);
			Stream.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
		}
		auto Consumer = VeroScriptCompile::CompileSource("import \"" + PathUtil::ToUtf8(File) + "\";int main(){print(cost_value());return 0;}", PathUtil::ToUtf8(Temporary / "main.vs"));
		if (!Consumer.bSuccess)
		{
			std::cerr << Consumer.Error.Message << '\n';
			return 9;
		}
		std::ostringstream Out;
		VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
		Vm.SetExecutionBackend(EExecutionBackend::Vm);
		Vm.SetExecutionControl(std::make_shared<FExecutionControl>());
		if (!Vm.Execute(*Consumer.Module, ERunMode::Script) || Out.str() != "6\n") return 10;
		Counts[I] = Vm.GetExecutedInstructionCount();
	}
	if (Counts[0] != Counts[1])
	{
		std::cerr << "stripped library changed costs: " << Counts[0] << "," << Counts[1] << '\n';
		return 11;
	}
	auto Shadowed = VeroScriptCompile::CompileSource("int value(){return 17;} int main(){optional<string> item=optional<string>(\"ok\");print(item.get());return 0;}", "optional-shadow.vs");
	if (!Shadowed.bSuccess)
	{
		std::cerr << Shadowed.Error.Message << '\n';
		return 12;
	}
	std::ostringstream ShadowOutput;
	VeroScriptVm ShadowVm(ShadowOutput);
	VeroScript::Test::Configure(ShadowVm);
	if (!ShadowVm.Execute(*Shadowed.Module, ERunMode::Script) || ShadowOutput.str() != "ok\n") return 13;
	if (!CheckRetainedConstructAliases()) return 17;
	if (!CheckRetainedReadAliases()) return 15;
	if (!CheckRetainedReadAfterCleanup()) return 16;
	if (!CheckRejectedRequestsAfterCleanup()) return 18;
	if (!CheckRetainedOperationsAfterReset()) return 14;
	std::cout << "execution governance and program digest passed\n";
	return 0;
}
