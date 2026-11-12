// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptVm.h"
#include "VeroScriptVariant.h"
#include "VeroScriptVmCleanup.h"
#include "VeroScriptVmScalarArithmetic.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptVmInternal.h"
#include "VeroScriptVmArguments.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_set>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace VeroScript
{
	namespace
	{
#if defined(__linux__) || defined(__APPLE__)
		struct FInterpreterStackBounds
		{
			std::uintptr_t Low = 0;
			std::uintptr_t High = 0;
		};
		thread_local FInterpreterStackBounds InterpreterStackBounds;
#endif
		bool HasInterpreterStackHeadroom(const std::size_t NativeDepth)
		{
			std::uintptr_t Low = 0;
			std::uintptr_t High = 0;
			const auto Current = reinterpret_cast<std::uintptr_t>(&Low);
			constexpr std::uintptr_t RequiredHeadroom = 128u * 1024u;
#if defined(__linux__) || defined(__APPLE__)
			if (InterpreterStackBounds.Low && Current >= InterpreterStackBounds.Low && Current < InterpreterStackBounds.High)
			{
				return Current - InterpreterStackBounds.Low >= RequiredHeadroom;
			}
#endif
#if defined(_WIN32) && _WIN32_WINNT >= 0x0602
			ULONG_PTR StackLow = 0;
			ULONG_PTR StackHigh = 0;
			GetCurrentThreadStackLimits(&StackLow, &StackHigh);
			Low = StackLow;
			High = StackHigh;
#elif defined(__linux__)
			pthread_attr_t Attributes;
			if (pthread_getattr_np(pthread_self(), &Attributes) == 0)
			{
				void* Address = nullptr;
				std::size_t Size = 0;
				if (pthread_attr_getstack(&Attributes, &Address, &Size) == 0)
				{
					Low = reinterpret_cast<std::uintptr_t>(Address);
					High = Low + Size;
				}
				pthread_attr_destroy(&Attributes);
			}
#elif defined(__APPLE__)
			High = reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(pthread_self()));
			Low = High - pthread_get_stacksize_np(pthread_self());
#endif
#if defined(__linux__) || defined(__APPLE__)
			InterpreterStackBounds = {Low, High};
#endif
			// Synchronous bridges need room both to enter another interpreter and
			// to unwind its finally/destructor paths after a rejected native reentry.
			if (Low && Current >= Low && Current < High)
			{
				return Current - Low >= RequiredHeadroom;
			}
			// Targets without a thread-stack query still bound native reentry.
			return NativeDepth < 64;
		}
	}

	void VeroScriptVm::PrepareJitForExecution(const FBytecodeModule& Module)
	{
		const auto Preferred = PreferredJitFunctionIndex;
		PreferredJitFunctionIndex = static_cast<std::size_t>(-1);
		PrepareBackendForExecution(Module, Preferred);
	}

	bool VeroScriptVm::Run()
	{
		// A completed native/JIT/AOT call leaves no bytecode frame to dispatch.
		if (Frames.size() <= StopFrameDepth) return !bExecutionStateInvalid && !bHasError;
#if defined(__linux__) || defined(__APPLE__)
		if (VmExecutionDomain->ActiveDepth == 0)
		{
			// pthread_getattr_np may inspect process mappings. Query once for this
			// synchronous execution, and refresh if a host changes stack domains.
			InterpreterStackBounds = {};
		}
#endif
		if (VmExecutionDomain->ActiveDepth != 0 && !HasInterpreterStackHeadroom(VmExecutionDomain->ActiveDepth))
		{
			ReportErrorId("diag.call_stack_overflow");
			return false;
		}
		const std::uint64_t CallToken = ++VmCallSerial;
		bool bContinuation = false;
		const auto DiscardScriptTransferForRuntimeFault = [this]()
		{
			if (bHasPendingScriptException && bHasError && !Error.bUncaughtScriptException)
			{
				// A secondary script throw cannot make a runtime fault catchable.
				FScriptError RuntimeError = std::move(Error);
				bHasError = false;
				Error = {};
				(void)ReleasePendingScriptException();
				bHasError = true;
				Error = std::move(RuntimeError);
			}
		};
		for (;;)
		{
			const bool bOk = RunSelectedVm(CallToken, bContinuation);
			bContinuation = true;
			if (bExecutionStateInvalid) return false;
			if (bOk)
			{
				return true;
			}
			if (bExitRequested && !bHasError)
			{
				return true;
			}
			DiscardScriptTransferForRuntimeFault();
			if (bHasPendingScriptException)
			{
				if (TryHandlePendingScriptException())
				{
					continue;
				}
				// Leaving a finally can restore a previously parked runtime fault.
				// Continue its finally path instead of returning with a script transfer.
				DiscardScriptTransferForRuntimeFault();
				if (!bHasError || Error.bUncaughtScriptException)
				{
					return false;
				}
			}
			if (TryHandlePendingRuntimeFinally())
			{
				continue;
			}
			return false;
		}
	}

	bool VeroScriptVm::CallFunctionAt(
		const std::uint16_t FunctionIndex,
		const std::vector<FValue>& Arguments,
		const FObjectHandle ThisObject,
		const bool bAllowJit)
	{
		if (Arguments.size() > std::numeric_limits<std::uint8_t>::max())
		{
			ReportError("function supports at most 255 arguments");
			return false;
		}
		return CallFunctionAt(
			FunctionIndex,
			Arguments.data(),
			static_cast<std::uint8_t>(Arguments.size()),
			ThisObject,
			bAllowJit);
	}

	void VeroScriptVm::PrepareJitForModule(const FBytecodeModule& Module)
	{
		PrepareBackendForExecution(Module, static_cast<std::size_t>(-1));
	}

	bool VeroScriptVm::PopOperandArguments(
		const std::uint8_t ArgumentCount,
		FValue* const StackBuffer8,
		std::vector<FValue>& HeapBuffer,
		FValue*& OutArguments)
	{
		if (ArgumentCount <= 8)
		{
			for (int Index = static_cast<int>(ArgumentCount) - 1; Index >= 0; --Index)
			{
				StackBuffer8[static_cast<std::size_t>(Index)] = Pop();
				if (bHasError)
				{
					return false;
				}
			}
			OutArguments = StackBuffer8;
			return true;
		}
		HeapBuffer.resize(ArgumentCount);
		for (int Index = static_cast<int>(ArgumentCount) - 1; Index >= 0; --Index)
		{
			HeapBuffer[static_cast<std::size_t>(Index)] = Pop();
			if (bHasError)
			{
				return false;
			}
		}
		OutArguments = HeapBuffer.data();
		return true;
	}

	bool VeroScriptVm::EvalArithmetic(
		const EOpcode Op,
		const FValue& Left,
		const FValue& Right,
		FValue& OutResult,
		const FSourceLocation& Location)
	{
		// Fast path: both operands are signed 32-bit integers (the common loop case).
		if (std::holds_alternative<std::int32_t>(Left)
			&& std::holds_alternative<std::int32_t>(Right))
		{
			const std::int32_t L = std::get<std::int32_t>(Left);
			const std::int32_t R = std::get<std::int32_t>(Right);
			std::int32_t Result = 0;
			switch (Op)
			{
			case EOpcode::Add:
				{
					if (!Runtime::CheckedAdd(L, R, Result))
					{
						ReportErrorId("diag.integer_overflow", Location);
						return false;
					}
					break;
				}
			case EOpcode::Sub:
				{
					if (!Runtime::CheckedSub(L, R, Result))
					{
						ReportErrorId("diag.integer_overflow", Location);
						return false;
					}
					break;
				}
			case EOpcode::Mul:
				{
					if (!Runtime::CheckedMul(L, R, Result))
					{
						ReportErrorId("diag.integer_overflow", Location);
						return false;
					}
					break;
				}
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					if (R == 0)
					{
						ReportErrorId("diag.division_by_zero", Location);
						return false;
					}
					if (L == std::numeric_limits<std::int32_t>::min() && R == -1)
					{
						ReportErrorId("diag.integer_overflow", Location);
						return false;
					}
					Result = (Op == EOpcode::Div)
						? static_cast<std::int32_t>(L / R)
						: static_cast<std::int32_t>(L % R);
					break;
				}
			default:
				{
					ReportError("invalid arithmetic opcode", Location);
					return false;
				}
			}
			OutResult = Result;
			return true;
		}

		if (const auto* LeftWide = std::get_if<std::int64_t>(&Left))
		{
			if (const auto* RightWide = std::get_if<std::int64_t>(&Right))
			{
				return EvalWideArithmetic(Op, *LeftWide, *RightWide, OutResult, Location);
			}
			if (const auto* RightNarrow = std::get_if<std::int32_t>(&Right))
			{
				return EvalWideArithmetic(Op, *LeftWide, *RightNarrow, OutResult, Location);
			}
		}
		else if (const auto* RightWide = std::get_if<std::int64_t>(&Right))
		{
			if (const auto* LeftNarrow = std::get_if<std::int32_t>(&Left))
			{
				return EvalWideArithmetic(Op, *LeftNarrow, *RightWide, OutResult, Location);
			}
		}
		return EvalGeneralArithmetic(Op, Left, Right, OutResult, Location);
	}

	bool VeroScriptVm::EvalGeneralArithmetic(const EOpcode Op,
		const FValue& Left, const FValue& Right,
		FValue& OutResult, const FSourceLocation& Location)
	{
		const auto* LeftDouble = std::get_if<double>(&Left);
		const auto* RightDouble = std::get_if<double>(&Right);
		const bool bBothDouble = LeftDouble && RightDouble;

		// Two doubles already satisfy the numeric and signedness checks below.
		// Add is also string / wstring concatenation.
		if (!bBothDouble && Op == EOpcode::Add
			&& (Runtime::IsStringValue(Left) || Runtime::IsStringValue(Right)
				|| Runtime::IsWStringValue(Left) || Runtime::IsWStringValue(Right)))
		{
			if (Runtime::IsWStringValue(Left) || Runtime::IsWStringValue(Right))
			{
				if (!Runtime::IsWStringValue(Left) || !Runtime::IsWStringValue(Right))
				{
					ReportError("cannot mix string and wstring concatenation", Location);
					return false;
				}
				OutResult = std::get<std::u16string>(Left) + std::get<std::u16string>(Right);
				return true;
			}
			if (!Runtime::IsStringValue(Left) || !Runtime::IsStringValue(Right))
			{
				ReportError("string concatenation requires string operands", Location);
				return false;
			}
			OutResult = std::get<std::string>(Left) + std::get<std::string>(Right);
			return true;
		}

		if (!bBothDouble && (!Runtime::IsNumericValue(Left) || !Runtime::IsNumericValue(Right)))
		{
			ReportError("arithmetic operands must be numeric", Location);
			return false;
		}
		if (!bBothDouble
			&& ((Runtime::IsSignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right))
				|| (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right))))
		{
			ReportError("cannot mix signed and unsigned", Location);
			return false;
		}
		if (LeftDouble || RightDouble)
		{
			// Read double operands directly; keep conversion for other numeric representations.
			const double L = LeftDouble ? *LeftDouble : Runtime::ToDouble(Left);
			const double R = RightDouble ? *RightDouble : Runtime::ToDouble(Right);
			if (Op == EOpcode::Div && R == 0.0)
			{
				ReportErrorId("diag.division_by_zero", Location);
				return false;
			}
			switch (Op)
			{
			case EOpcode::Add:
				{
					OutResult = L + R;
					return true;
				}
			case EOpcode::Sub:
				{
					OutResult = L - R;
					return true;
				}
			case EOpcode::Mul:
				{
					OutResult = L * R;
					return true;
				}
			case EOpcode::Div:
				{
					OutResult = L / R;
					return true;
				}
			default:
				{
					ReportError("modulo is only defined for integers", Location);
					return false;
				}
			}
		}
		if (std::holds_alternative<float>(Left) || std::holds_alternative<float>(Right))
		{
			const float L = static_cast<float>(Runtime::ToDouble(Left));
			const float R = static_cast<float>(Runtime::ToDouble(Right));
			if (Op == EOpcode::Div && R == 0.0f)
			{
				ReportErrorId("diag.division_by_zero", Location);
				return false;
			}
			switch (Op)
			{
			case EOpcode::Add:
				{
					OutResult = static_cast<float>(L + R);
					return true;
				}
			case EOpcode::Sub:
				{
					OutResult = static_cast<float>(L - R);
					return true;
				}
			case EOpcode::Mul:
				{
					OutResult = static_cast<float>(L * R);
					return true;
				}
			case EOpcode::Div:
				{
					OutResult = static_cast<float>(L / R);
					return true;
				}
			default:
				{
					ReportError("modulo is only defined for integers", Location);
					return false;
				}
			}
		}
		if (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right))
		{
			if (std::holds_alternative<std::uint64_t>(Left)
				|| std::holds_alternative<std::uint64_t>(Right))
			{
				const std::uint64_t L = Runtime::ToUnsignedInteger64(Left);
				const std::uint64_t R = Runtime::ToUnsignedInteger64(Right);
				if ((Op == EOpcode::Div || Op == EOpcode::Mod) && R == 0)
				{
					ReportErrorId("diag.division_by_zero", Location);
					return false;
				}
				switch (Op)
				{
				case EOpcode::Add:
					{
						OutResult = L + R;
						return true;
					}
				case EOpcode::Sub:
					{
						OutResult = L - R;
						return true;
					}
				case EOpcode::Mul:
					{
						OutResult = L * R;
						return true;
					}
				case EOpcode::Div:
					{
						OutResult = L / R;
						return true;
					}
				case EOpcode::Mod:
					{
						OutResult = L % R;
						return true;
					}
				default:
					{
						break;
					}
				}
				ReportError("invalid arithmetic opcode", Location);
				return false;
			}
			const std::uint32_t L = static_cast<std::uint32_t>(
				Runtime::ToUnsignedInteger64(Left));
			const std::uint32_t R = static_cast<std::uint32_t>(
				Runtime::ToUnsignedInteger64(Right));
			if ((Op == EOpcode::Div || Op == EOpcode::Mod) && R == 0)
			{
				ReportErrorId("diag.division_by_zero", Location);
				return false;
			}
			switch (Op)
			{
			case EOpcode::Add:
				{
					OutResult = static_cast<std::uint32_t>(L + R);
					return true;
				}
			case EOpcode::Sub:
				{
					OutResult = static_cast<std::uint32_t>(L - R);
					return true;
				}
			case EOpcode::Mul:
				{
					OutResult = static_cast<std::uint32_t>(L * R);
					return true;
				}
			case EOpcode::Div:
				{
					OutResult = static_cast<std::uint32_t>(L / R);
					return true;
				}
			case EOpcode::Mod:
				{
					OutResult = static_cast<std::uint32_t>(L % R);
					return true;
				}
			default:
				{
					break;
				}
			}
			ReportError("invalid arithmetic opcode", Location);
			return false;
		}

		if (std::holds_alternative<std::int64_t>(Left)
			|| std::holds_alternative<std::int64_t>(Right))
		{
			return EvalWideArithmetic(Op, Runtime::ToSignedInteger64(Left),
				Runtime::ToSignedInteger64(Right), OutResult, Location);
		}

		// Both operands are signed int32 in an unusual representation path.
		const std::int32_t L = static_cast<std::int32_t>(Runtime::ToSignedInteger64(Left));
		const std::int32_t R = static_cast<std::int32_t>(Runtime::ToSignedInteger64(Right));
		std::int32_t Result = 0;
		switch (Op)
		{
		case EOpcode::Add:
			{
				if (!Runtime::CheckedAdd(L, R, Result))
				{
					ReportErrorId("diag.integer_overflow", Location);
					return false;
				}
				break;
			}
		case EOpcode::Sub:
			{
				if (!Runtime::CheckedSub(L, R, Result))
				{
					ReportErrorId("diag.integer_overflow", Location);
					return false;
				}
				break;
			}
		case EOpcode::Mul:
			{
				if (!Runtime::CheckedMul(L, R, Result))
				{
					ReportErrorId("diag.integer_overflow", Location);
					return false;
				}
				break;
			}
		case EOpcode::Div:
		case EOpcode::Mod:
			{
				if (R == 0)
				{
					ReportErrorId("diag.division_by_zero", Location);
					return false;
				}
				if (L == std::numeric_limits<std::int32_t>::min() && R == -1)
				{
					ReportErrorId("diag.integer_overflow", Location);
					return false;
				}
				Result = (Op == EOpcode::Div)
					? static_cast<std::int32_t>(L / R)
					: static_cast<std::int32_t>(L % R);
				break;
			}
		default:
			{
				ReportError("invalid arithmetic opcode", Location);
				return false;
			}
		}
		OutResult = Result;
		return true;
	}

	bool VeroScriptVm::EvalWideArithmetic(const EOpcode Op,
		const std::int64_t L, const std::int64_t R,
		FValue& OutResult, const FSourceLocation& Location)
	{
		std::int64_t Result = 0;
		const auto Status = VmDetail::EvalSigned64Arithmetic(Op, L, R, Result);
		if (Status != VmDetail::ESignedArithmeticResult::Ok)
		{
			if (Status == VmDetail::ESignedArithmeticResult::DivisionByZero)
			{
				ReportErrorId("diag.division_by_zero", Location);
			}
			else if (Status == VmDetail::ESignedArithmeticResult::Overflow)
			{
				ReportErrorId("diag.integer_overflow", Location);
			}
			else
			{
				ReportError("invalid arithmetic opcode", Location);
			}
			return false;
		}
		OutResult = Result;
		return true;

	}

}
