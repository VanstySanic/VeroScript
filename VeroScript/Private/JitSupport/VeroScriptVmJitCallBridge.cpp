// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"

#include "VeroScriptVmJitBridgeInternal.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptJit.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptVm::JitCallIndirect(
		const FValue& Callee,
		const FValue* const Arguments,
		const std::uint8_t ArgumentCount)
	{
		const auto* FunctionHandle = std::get_if<FFunctionHandle>(&Callee);
		if (!FunctionHandle || !*FunctionHandle)
		{
			ReportError("JIT CallIndirect requires a function value");
			return false;
		}
		const FFunctionValue& FunctionValue = **FunctionHandle;
		if (!ActiveModule || FunctionValue.ProgramGeneration != ProgramGeneration
			|| FunctionValue.ProgramOwner != ProgramOwner
			|| FunctionValue.FunctionIndex >= ActiveModule->Functions.size())
		{
			ReportError("JIT CallIndirect target is not part of the active module");
			return false;
		}
		const FBytecodeFunction& Target = ActiveModule->Functions[FunctionValue.FunctionIndex];
		if (FunctionValue.FunctionName.empty()
			|| FunctionValue.FunctionName != Target.Name
			|| FunctionValue.TypeName.empty()
			|| FunctionValue.TypeName != Target.ClosureTypeName)
		{
			ReportError("JIT CallIndirect target is stale or has an incompatible signature");
			return false;
		}
		const std::size_t DepthBefore = Frames.size();
		const std::size_t StackBefore = OperandStack.size();
		const bool bAllowJit = FunctionValue.Captures.empty();
		bool bOk = CallFunctionAt(
			FunctionValue.FunctionIndex,
			Arguments,
			ArgumentCount,
			FunctionValue.CapturedThis,
			bAllowJit);
		if (bOk && Frames.size() > DepthBefore)
		{
			FCallFrame& CalleeFrame = Frames.back();
			const std::size_t ParamCount =
				CalleeFrame.Function ? CalleeFrame.Function->ParamCount : 0;
			for (std::size_t CaptureIndex = 0; CaptureIndex < FunctionValue.Captures.size();
				++CaptureIndex)
			{
				const std::size_t Slot = ParamCount + CaptureIndex;
				if (Slot >= CalleeFrame.Locals.size())
				{
					ReportError("JIT closure capture slot out of range");
					bOk = false;
					break;
				}
				CalleeFrame.Locals[Slot] = FunctionValue.Captures[CaptureIndex];
			}
		}
		if (bOk && Frames.size() > DepthBefore)
		{
			const std::size_t SavedStopDepth = StopFrameDepth;
			StopFrameDepth = DepthBefore;
			bOk = Run();
			StopFrameDepth = SavedStopDepth;
			if (!bOk)
			{
				if (bHasError)
				{
					bJitErrorLocationPinned = true;
				}
				(void)DiscardExecutionAbove(DepthBefore, StackBefore, {});
			}
		}

		if (!bOk || bHasError)
		{
			return false;
		}
		// exit() intentionally removes every VM frame and operand. As with the
		// shared nested-call bridge, successful process exit therefore has no
		// ordinary return value whose stack depth can be validated here.
		if (bExitRequested)
		{
			return true;
		}
		if (OperandStack.size() != StackBefore + 1)
		{
			ReportError("JIT CallIndirect produced unexpected stack depth");
			return false;
		}
		return true;
	}

	bool VeroScriptVm::JitCallMethod(
		const std::uint16_t MethodNameIndex,
		const FValue& ThisObject,
		const FValue* const Arguments,
		const std::uint8_t ArgumentCount,
		const std::uint32_t CallSiteOffset)
	{
		return ExecuteCallMethod(
			MethodNameIndex,
			ThisObject,
			Arguments,
			ArgumentCount,
			CallSiteOffset,
			{},
			true);
	}

	bool VeroScriptVm::JitCallNative(
		const std::uint16_t NativeIndex,
		const FValue* const Arguments,
		const std::uint8_t ArgumentCount,
		FValue& OutReturn)
	{
		bool bScalarHandled = false;
		if (!JitTryCallNativeScalar(
				NativeIndex, Arguments, ArgumentCount, bScalarHandled, OutReturn))
		{
			return false;
		}
		if (bScalarHandled)
		{
			return true;
		}

		if (!ActiveNatives || NativeIndex >= ActiveNatives->size())
		{
			ReportError("JIT CallNative: invalid native index");
			return false;
		}
		// The callback may synchronously re-enter the session and replace the active
		// table. Preserve the selected declaration's return contract across that call.
		const ENativeType ReturnType = (*ActiveNatives)[NativeIndex].ReturnType;
		const std::size_t StackBefore = OperandStack.size();
		for (std::uint8_t I = 0; I < ArgumentCount; ++I)
		{
			Push(Arguments[I]);
			if (bHasError)
			{
				return false;
			}
		}
		if (!CallNative(NativeIndex, ArgumentCount, {}))
		{
			(void)DiscardExecutionAbove(Frames.size(), StackBefore, {});
			return false;
		}
		if (ReturnType == ENativeType::Void)
		{
			// Linear JIT models CallNative as producing a value; use the same i32-0
			// sentinel the compiler emits after interpreted void native calls.
			if (OperandStack.size() != StackBefore)
			{
				ReportError("JIT CallNative produced unexpected stack depth");
				return false;
			}
			OutReturn = std::int32_t{0};
			return !bHasError;
		}
		if (OperandStack.size() != StackBefore + 1)
		{
			ReportError("JIT CallNative produced unexpected stack depth");
			return false;
		}
		OutReturn = Pop();
		return !bHasError;
	}

	bool VeroScriptVm::JitCanUseScalarNativeCalls(
		const std::uint32_t* const EncodedGuards,
		const std::size_t GuardCount) const
	{
		if (!ActiveNatives || !EncodedGuards || GuardCount == 0)
		{
			return false;
		}
		for (std::size_t GuardIndex = 0; GuardIndex < GuardCount; ++GuardIndex)
		{
			const std::uint32_t Encoded = EncodedGuards[GuardIndex];
			const std::uint16_t NativeIndex =
				static_cast<std::uint16_t>(Encoded >> 8u);
			const std::uint8_t ArgumentCount =
				static_cast<std::uint8_t>(Encoded & 0xFFu);
			if (NativeIndex >= ActiveNatives->size()
				|| ArgumentCount > 8
				|| !JitBridgeDetail::IsScalarNativeSignature(
					(*ActiveNatives)[NativeIndex], ArgumentCount))
			{
				return false;
			}
		}
		return true;
	}

	bool VeroScriptVm::JitMakeClosure(
		const std::uint16_t FunctionIndex,
		const FValue* const Captures,
		const std::uint8_t CaptureCount,
		FValue& OutClosure)
	{
		if (!ActiveModule || FunctionIndex >= ActiveModule->Functions.size())
		{
			ReportError("JIT MakeClosure: invalid function index");
			return false;
		}
		if (CaptureCount != 0 && !Captures)
		{
			ReportError("JIT MakeClosure: null capture input");
			return false;
		}
		std::vector<FValue> Values;
		if (CaptureCount != 0)
		{
			Values.assign(Captures, Captures + CaptureCount);
		}
		const FObjectHandle Receiver = GetJitThisObject();
		FFunctionHandle FunctionValue;
		if (!CreateClosure(FunctionIndex, std::move(Values), Receiver, FunctionValue)) return false;
		OutClosure = FFunctionHandle{std::move(FunctionValue)};
		return true;
	}
}
