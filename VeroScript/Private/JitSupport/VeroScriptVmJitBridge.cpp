// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptPlatform.h"

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
#include "VeroScriptStandardLibraryServices.h"

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
	namespace
	{
		std::size_t ResolveDefaultMaxCallDepth()
		{
			if (GetHostPlatformInfo().bMobile) return VEROSCRIPT_DEFAULT_MAX_CALL_DEPTH;
			const auto Text = Environment::GetUtf8("VERO_MAX_CALL_DEPTH");
			if (!Text || Text->empty())
			{
				return VEROSCRIPT_DEFAULT_MAX_CALL_DEPTH;
			}
			std::uint64_t Parsed = 0;
			const char* const Begin = Text->data();
			const char* const End = Begin + Text->size();
			const auto Result = std::from_chars(Begin, End, Parsed);
			if (Result.ec != std::errc{} || Result.ptr != End || Parsed == 0 || Parsed > std::numeric_limits<std::size_t>::max())
			{
				return VEROSCRIPT_DEFAULT_MAX_CALL_DEPTH;
			}
			return static_cast<std::size_t>(Parsed);
		}

		bool RequiresExecutionGovernance(
		    const FExecutionLimits& Limits,
		    const std::shared_ptr<FExecutionControl>& Control,
		    const FScriptCapabilities& Capabilities)
		{
			return Control != nullptr || Limits.MaxInstructions != 0 || Limits.TimeoutMilliseconds != 0 ||
			    !Capabilities.bAllowMemoryAddress || !Capabilities.bAllowFileRead ||
			    !Capabilities.bAllowFileWrite || !Capabilities.bAllowFileDelete ||
			    !Capabilities.bAllowEnvironmentRead || !Capabilities.bAllowEnvironmentWrite ||
			    !Capabilities.bAllowConsoleInput || !Capabilities.bAllowConsoleOutput ||
			    !Capabilities.bAllowTaskSpawn || !Capabilities.bAllowProcessDirectoryChange || !Capabilities.bAllowNondeterminism;
		}

	}

	VeroScriptVm::VeroScriptVm(std::ostream& InOutputStream)
	    : FExecutionContext(InOutputStream, std::cin, std::cerr)
	{
		SetMaxCallDepth(0);
	}

	void VeroScriptVm::SetDebugger(VeroScriptDebugger* const InDebugger)
	{
		Debugger = InDebugger;
		if (Debugger != nullptr)
		{
			InvalidateJitCache();
		}
	}

	void VeroScriptVm::SetInstanceCallInterceptor(FInstanceCallInterceptor Interceptor)
	{
		InstanceCallInterceptor = std::move(Interceptor);
		bInstanceCallInterceptorPresent = static_cast<bool>(InstanceCallInterceptor);
	}

	void VeroScriptVm::SetMutationCallback(FMutationCallback Callback)
	{
		MutationCallback = std::move(Callback);
		bMutationObserverPresent = static_cast<bool>(MutationCallback);
	}

	void VeroScriptVm::SetProgramGeneration(const std::uint64_t Generation)
	{
		BackendCache.reset();
		ProgramGeneration = Generation == 0 ? 1 : Generation;
		ResetModuleLookupCaches();
	}

	void VeroScriptVm::SetExecutionBackend(const EExecutionBackend Backend)
	{
		RequestedExecutionBackend = Backend;
	}

	EExecutionBackend VeroScriptVm::GetEffectiveExecutionBackend() const
	{
		if (Debugger != nullptr || bHotReloadModuleInitializing || ExecutionCleanup || (!JitBackendFactory && !AotProgram))
		{
			return EExecutionBackend::Vm;
		}
		return RequestedExecutionBackend;
	}

	void VeroScriptVm::SetMaxCallDepth(const std::size_t Limit)
	{
		CallDepthLimit = Limit == 0 ? ResolveDefaultMaxCallDepth() : Limit;
	}

	void VeroScriptVm::SetExecutionLimits(const FExecutionLimits Limits)
	{
		RequireIdleExecutionConfiguration();
		ExecutionLimits = Limits;
		bExecutionGovernanceEnabled =
		    RequiresExecutionGovernance(
		        ExecutionLimits, ExecutionControl, Capabilities);
	}

	void VeroScriptVm::SetExecutionControl(std::shared_ptr<FExecutionControl> Control)
	{
		RequireIdleExecutionConfiguration();
		ExecutionControl = std::move(Control);
		bExecutionGovernanceEnabled =
		    RequiresExecutionGovernance(
		        ExecutionLimits, ExecutionControl, Capabilities);
	}

	void VeroScriptVm::RequestCancel() const noexcept
	{
		if (ExecutionControl)
		{
			ExecutionControl->RequestCancel();
		}
	}

	bool VeroScriptVm::IsCancellationRequested() const noexcept
	{
		return ExecutionControl && ExecutionControl->IsCancellationRequested();
	}

	void VeroScriptVm::SetCapabilities(const FScriptCapabilities InCapabilities)
	{
		RequireIdleExecutionConfiguration();
		Capabilities = InCapabilities;
		bExecutionGovernanceEnabled =
		    RequiresExecutionGovernance(
		        ExecutionLimits, ExecutionControl, Capabilities);
	}

	void VeroScriptVm::InvalidateJitCache()
	{
		BackendCache.reset();
		BackendStats = {};
		JitStatsAtRunStart = nullptr;
		++JitCacheInvalidations;
		ResetModuleLookupCaches();
	}

	void VeroScriptVm::SetJitPolicy(FJitPolicy Policy)
	{
		BackendCache.reset();
		JitStatsAtRunStart = nullptr;
		if (Policy.MixedHotCallThreshold == 0)
		{
			Policy.MixedHotCallThreshold = 1;
		}
		JitPolicy = Policy;
		++JitCacheInvalidations;
		ResetModuleLookupCaches();
	}

	FJitStats VeroScriptVm::GetJitStats() const
	{
		FJitStats Stats = BackendCache ? BackendCache->GetStats() : BackendStats;
		// Retained-cache budgets/hotness accumulate; a leased session cache reports
		// only this invocation's activity through its owner-held snapshot.
		if (JitStatsAtRunStart)
		{
			#define VERO_RUN_DELTA(Field) Stats.Field -= std::min(Stats.Field, JitStatsAtRunStart->Field)
			VERO_RUN_DELTA(CompileAttempts);
			VERO_RUN_DELTA(CacheHits);
			VERO_RUN_DELTA(CompiledEntries);
			VERO_RUN_DELTA(NativeLeafEntries);
			VERO_RUN_DELTA(NativeCfgEntries);
			VERO_RUN_DELTA(BridgeTrampolineEntries);
			VERO_RUN_DELTA(LinearTrampolineEntries);
			VERO_RUN_DELTA(VmTrampolineEntries);
			VERO_RUN_DELTA(UnsupportedEntries);
			VERO_RUN_DELTA(HotnessDeferrals);
			VERO_RUN_DELTA(HotFunctions);
			VERO_RUN_DELTA(CompileBudgetSkips);
			VERO_RUN_DELTA(CacheCapacitySkips);
			VERO_RUN_DELTA(EagerCompileSkips);
			VERO_RUN_DELTA(InlinedCallSites);
			#undef VERO_RUN_DELTA
		}
		Stats.AotExecutions += AotNativeExecutions;
		Stats.DispatchAttempts += JitDispatchAttempts;
		Stats.Dispatches += JitDispatches;
		Stats.Fallbacks += JitFallbacks;
		Stats.CostModelVmFallbacks += JitCostModelVmFallbacks;
		Stats.Failures += JitFailures;
		Stats.CacheInvalidations += JitCacheInvalidations;
		Stats.ArrayFastPathPushes += JitArrayFastPathPushes;
		Stats.ArrayFastPathGets += JitArrayFastPathGets;
		Stats.ArrayFastPathSets += JitArrayFastPathSets;
		Stats.ScalarMapFastPathGets += JitScalarMapFastPathGets;
		Stats.ScalarMapFastPathSets += JitScalarMapFastPathSets;
		Stats.NativeTypedLoopExecutions += JitNativeTypedLoopExecutions;
		Stats.ScalarMethodFastPathI32 += JitScalarMethodFastPathI32;
		Stats.ScalarMethodFastPathU32 += JitScalarMethodFastPathU32;
		Stats.ScalarMethodFastPathF64 += JitScalarMethodFastPathF64;
		Stats.ScalarMethodFastPathMixed += JitScalarMethodFastPathMixed;
		Stats.ScalarFunctionFastPathI32 += JitScalarFunctionFastPathI32;
		Stats.ScalarFunctionFastPathU32 += JitScalarFunctionFastPathU32;
		Stats.ScalarFunctionFastPathF64 += JitScalarFunctionFastPathF64;
		Stats.ScalarFunctionFastPathMixed += JitScalarFunctionFastPathMixed;
		Stats.ScalarNativeFastPathCalls += JitScalarNativeFastPathCalls;
		Stats.DirectNativeCalls += JitDirectNativeCalls;
		Stats.LinearFrameReuses += JitLinearFrameReuses;
		Stats.LinearScratchReuses += JitLinearScratchReuses;
		Stats.LinearScratchAllocations += JitLinearScratchAllocations;
		Stats.LinearArgumentScratchLeases += JitLinearArgumentScratchLeases;
		Stats.LinearArgumentScratchReuses += JitLinearArgumentScratchReuses;
		return Stats;
	}

	void VeroScriptVm::ResetJitStats()
	{
		JitDiagnostics.reset();
		if (BackendCache)
		{
			BackendCache->ResetExecutionStatistics();
		}
		BackendStats.DispatchAttempts = 0;
		BackendStats.Dispatches = 0;
		BackendStats.Fallbacks = 0;
		BackendStats.CostModelVmFallbacks = 0;
		BackendStats.Failures = 0;
		BackendStats.ArrayFastPathPushes = 0;
		BackendStats.ArrayFastPathGets = 0;
		BackendStats.ArrayFastPathSets = 0;
		BackendStats.ScalarMapFastPathGets = 0;
		BackendStats.ScalarMapFastPathSets = 0;
		BackendStats.NativeTypedLoopExecutions = 0;
		BackendStats.ScalarMethodFastPathI32 = 0;
		BackendStats.ScalarMethodFastPathU32 = 0;
		BackendStats.ScalarMethodFastPathF64 = 0;
		BackendStats.ScalarMethodFastPathMixed = 0;
		BackendStats.ScalarFunctionFastPathI32 = 0;
		BackendStats.ScalarFunctionFastPathU32 = 0;
		BackendStats.ScalarFunctionFastPathF64 = 0;
		BackendStats.ScalarFunctionFastPathMixed = 0;
		BackendStats.ScalarNativeFastPathCalls = 0;
		BackendStats.DirectNativeCalls = 0;
		BackendStats.ControlledNativeExecutions = 0;
		BackendStats.AotExecutions = 0;
		BackendStats.LinearFrameReuses = 0;
		BackendStats.LinearScratchReuses = 0;
		BackendStats.LinearScratchAllocations = 0;
		BackendStats.LinearArgumentScratchLeases = 0;
		BackendStats.LinearArgumentScratchReuses = 0;
		BackendStats.NativeLeafExecutions = 0;
		BackendStats.NativeCfgExecutions = 0;
		BackendStats.BridgeExecutions = 0;
		BackendStats.LinearExecutions = 0;
		BackendStats.VmTrampolineExecutions = 0;

		AotNativeExecutions = 0;
		JitDispatchAttempts = 0;
		JitDispatches = 0;
		JitFallbacks = 0;
		JitCostModelVmFallbacks = 0;
		JitFailures = 0;
		JitCacheInvalidations = 0;
		JitArrayFastPathPushes = 0;
		JitArrayFastPathGets = 0;
		JitArrayFastPathSets = 0;
		JitScalarMapFastPathGets = 0;
		JitScalarMapFastPathSets = 0;
		JitNativeTypedLoopExecutions = 0;
		JitScalarMethodFastPathI32 = 0;
		JitScalarMethodFastPathU32 = 0;
		JitScalarMethodFastPathF64 = 0;
		JitScalarMethodFastPathMixed = 0;
		JitScalarFunctionFastPathI32 = 0;
		JitScalarFunctionFastPathU32 = 0;
		JitScalarFunctionFastPathF64 = 0;
		JitScalarFunctionFastPathMixed = 0;
		JitScalarNativeFastPathCalls = 0;
		JitDirectNativeCalls = 0;
		JitLinearFrameReuses = 0;
		JitLinearScratchReuses = 0;
		JitLinearScratchAllocations = 0;
		JitLinearArgumentScratchLeases = 0;
		JitLinearArgumentScratchReuses = 0;
	}

	bool VeroScriptVm::JitInvokeBuiltin(
	    const EBuiltinId Builtin,
	    const FValue* const Arguments,
	    const std::uint8_t ArgumentCount,
	    bool& bOutHasResult,
	    FValue& OutResult)
	{
		bOutHasResult = false;
		const Builtins::FDescriptor* const Descriptor = Builtins::LookupById(Builtin);
		if (!Descriptor)
		{
			ReportError("unknown builtin");
			return false;
		}
		if (!Descriptor->AcceptsArgCount(ArgumentCount))
		{
			if (Descriptor->ArgCount != Descriptor->MaxArgCount)
			{
				ReportErrorId(
				    "diag.name_expects_between_n_and_m_arguments",
				    {},
				    {std::string(Descriptor->Name),
				        std::to_string(Descriptor->ArgCount),
				        std::to_string(Descriptor->MaxArgCount)});
			}
			else
			{
				ReportError(
				    std::string(Descriptor->Name) + " expects exactly " + std::to_string(Descriptor->ArgCount) + " argument(s)");
			}
			return false;
		}
		// These queries consume owned text without retaining it. Avoid copying both
		// inputs through the operand stack, while sharing the canonical limits,
		// search algorithm and work meter with ordinary builtin dispatch.
		if (ArgumentCount == 2u && Arguments && !Debugger && !HasExecutionGovernance() &&
			!bMutationObserverPresent && !bInstanceCallInterceptorPresent &&
			!bHasError && !bHasPendingScriptException && !bExitRequested &&
			OperandStack.size() <= MaxOperandStackDepth && MaxOperandStackDepth - OperandStack.size() >= 2u &&
			(Builtin == EBuiltinId::Find || Builtin == EBuiltinId::StringLastFind ||
				Builtin == EBuiltinId::StringStartsWith || Builtin == EBuiltinId::StringEndsWith))
		{
			const auto* Text = std::get_if<std::string>(&Arguments[0]);
			const auto* Part = std::get_if<std::string>(&Arguments[1]);
			if (Text && Part)
			{
				const StandardLibrary::FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
				StandardLibrary::FServiceResult Result;
				std::int64_t Position = 0;
				bool Matches = false;
				switch (Builtin)
				{
				case EBuiltinId::Find:
					{
						Result = StandardLibrary::FirstFind(*Text, *Part, Position, Context);
						break;
					}
				case EBuiltinId::StringLastFind:
					{
						Result = StandardLibrary::LastFind(*Text, *Part, Position, Context);
						break;
					}
				case EBuiltinId::StringStartsWith:
					{
						Result = StandardLibrary::StartsWith(*Text, *Part, Matches, Context);
						break;
					}
				case EBuiltinId::StringEndsWith:
					{
						Result = StandardLibrary::EndsWith(*Text, *Part, Matches, Context);
						break;
					}
				default:
					{
						break;
					}
				}
				if (!ReportStandardLibraryResult(Result)) return false;
				if (Builtin == EBuiltinId::Find || Builtin == EBuiltinId::StringLastFind)
				{
					OutResult = Position;
				}
				else
				{
					OutResult = Matches;
				}
				bOutHasResult = true;
				return true;
			}
		}
		const std::size_t StackBefore = OperandStack.size();
		for (std::uint8_t I = 0; I < ArgumentCount; ++I)
		{
			Push(Arguments[I]);
			if (bHasError)
			{
				return false;
			}
		}
		if (!CallBuiltin(Builtin, ArgumentCount))
		{
			return false;
		}
		// exit() owns whole-VM unwinding and intentionally clears OperandStack,
		// including values that predate this builtin invocation. Its success cannot
		// be validated against StackBefore like an ordinary void builtin.
		if (bExitRequested)
		{
			return !bHasError;
		}
		if (Descriptor->bPushVoidResult)
		{
			if (OperandStack.size() != StackBefore)
			{
				ReportError("JIT builtin: unexpected void stack depth");
				return false;
			}
			bOutHasResult = false;
			return true;
		}
		if (OperandStack.size() != StackBefore + 1)
		{
			ReportError("JIT builtin: unexpected value stack depth");
			return false;
		}
		OutResult = Pop();
		bOutHasResult = true;
		return !bHasError;
	}

	void VeroScriptVm::JitReportRuntimeError(std::string Message)
	{
		ReportError(std::move(Message));
	}

	const FObjectHandle& VeroScriptVm::GetJitThisObject() const
	{
		static const FObjectHandle Empty;
		return Frames.empty() ? Empty : Frames.back().ThisObject;
	}

	bool VeroScriptVm::JitLoadGlobal(const std::uint16_t Slot, FValue& OutValue)
	{
		if (bDemandGlobalInitializing && !EnsureGlobalInitialized(Slot))
		{
			return false;
		}
		if (ActiveModule && Slot < ActiveModule->GlobalMetadata.size() && ActiveModule->GlobalMetadata[Slot].bLocalStatic && !CheckLocalStaticAccess(Slot))
		{
			return false;
		}
		if (Slot >= Globals.size())
		{
			ReportErrorId("diag.invalid_global_slot");
			return false;
		}
		FValue* const Resolved = ResolveRefStorage(Globals[Slot]);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		OutValue = *Resolved;
		return true;
	}

	bool VeroScriptVm::JitStoreGlobal(const std::uint16_t Slot, FValue Value)
	{
		if (bDemandGlobalInitializing && !EnsureGlobalInitialized(Slot))
		{
			(void)ReleaseValue(Value, {});
			return false;
		}
		if (ActiveModule && Slot < ActiveModule->GlobalMetadata.size() && ActiveModule->GlobalMetadata[Slot].bLocalStatic && !CheckLocalStaticAccess(Slot))
		{
			(void)ReleaseValue(Value, {});
			return false;
		}
		if (Slot >= Globals.size())
		{
			ReportErrorId("diag.invalid_global_slot");
			return false;
		}
		if (bHotReloadModuleInitializing && Slot < HotReloadRetainedGlobalSlots.size() && HotReloadRetainedGlobalSlots[Slot])
		{
			return ReleaseValue(Value, {});
		}
		FValue* const Resolved = ResolveRefStorage(Globals[Slot]);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		if (!AssignValue(*Resolved, std::move(Value), {}))
		{
			return false;
		}
		if (Resolved != &Globals[Slot])
		{
			NotifyReferenceMutation(Globals[Slot]);
		}
		return !bHasError;
	}

	bool VeroScriptVm::JitEvalArithmetic(
	    const EOpcode Op,
	    const FValue& Left,
	    const FValue& Right,
	    FValue& OutResult)
	{
		return EvalArithmetic(Op, Left, Right, OutResult, {});
	}

	bool VeroScriptVm::JitEvalUnary(
	    const EOpcode Op,
	    const FValue& Operand,
	    FValue& OutResult)
	{
		if (Op == EOpcode::Not)
		{
			if (!ExpectBool(Operand, {}))
			{
				return false;
			}
			OutResult = !std::get<bool>(Operand);
			return true;
		}
		if (Op == EOpcode::Neg)
		{
			if (Runtime::IsUnsignedIntegerValue(Operand))
			{
				if (const auto* Value = std::get_if<std::uint64_t>(&Operand))
				{
					OutResult = std::uint64_t{0} - *Value;
				}
				else
				{
					OutResult = static_cast<std::uint32_t>(
					    0u - static_cast<std::uint32_t>(Runtime::ToUnsignedInteger64(Operand)));
				}
				return true;
			}
			if (Runtime::IsSignedIntegerValue(Operand))
			{
				if (const auto* Value = std::get_if<std::int64_t>(&Operand))
				{
					if (*Value == std::numeric_limits<std::int64_t>::min())
					{
						ReportErrorId("diag.integer_overflow");
						return false;
					}
					OutResult = -*Value;
				}
				else
				{
					const std::int32_t NarrowValue =
					    static_cast<std::int32_t>(Runtime::ToSignedInteger64(Operand));
					if (NarrowValue == std::numeric_limits<std::int32_t>::min())
					{
						ReportErrorId("diag.integer_overflow");
						return false;
					}
					OutResult = static_cast<std::int32_t>(-NarrowValue);
				}
				return true;
			}
			if (const auto* Value = std::get_if<float>(&Operand))
			{
				OutResult = -*Value;
				return true;
			}
			if (const auto* Value = std::get_if<double>(&Operand))
			{
				OutResult = -*Value;
				return true;
			}
			ReportError("invalid operand for unary operator");
			return false;
		}
		if (Op == EOpcode::BitNot)
		{
			if (Runtime::IsUnsignedIntegerValue(Operand))
			{
				if (const auto* Value = std::get_if<std::uint64_t>(&Operand))
				{
					OutResult = ~*Value;
				}
				else
				{
					OutResult = static_cast<std::uint32_t>(
					    ~static_cast<std::uint32_t>(Runtime::ToUnsignedInteger64(Operand)));
				}
				return true;
			}
			if (Runtime::IsSignedIntegerValue(Operand))
			{
				if (const auto* Value = std::get_if<std::int64_t>(&Operand))
				{
					OutResult = ~*Value;
				}
				else
				{
					OutResult = static_cast<std::int32_t>(
					    ~static_cast<std::int32_t>(Runtime::ToSignedInteger64(Operand)));
				}
				return true;
			}
			ReportError("bitwise not requires integer operand");
			return false;
		}
		ReportError("invalid unary opcode");
		return false;
	}

	bool VeroScriptVm::JitEvalBitwise(
	    const EOpcode Op,
	    const FValue& Left,
	    const FValue& Right,
	    FValue& OutResult)
	{
		if (Op != EOpcode::BitAnd && Op != EOpcode::BitOr && Op != EOpcode::BitXor && Op != EOpcode::Shl && Op != EOpcode::Shr)
		{
			ReportError("invalid bitwise opcode");
			return false;
		}
		const bool bShift = Op == EOpcode::Shl || Op == EOpcode::Shr;
		const bool bLeftSigned = Runtime::IsSignedIntegerValue(Left);
		const bool bLeftUnsigned = Runtime::IsUnsignedIntegerValue(Left);
		const bool bRightSigned = Runtime::IsSignedIntegerValue(Right);
		const bool bRightUnsigned = Runtime::IsUnsignedIntegerValue(Right);
		if ((!bLeftSigned && !bLeftUnsigned) || (!bRightSigned && !bRightUnsigned))
		{
			ReportError("bitwise operands must be integers");
			return false;
		}

		if (bShift)
		{
			std::uint64_t Count = 0;
			if (bRightUnsigned)
			{
				Count = Runtime::ToUnsignedInteger64(Right);
			}
			else
			{
				const std::int64_t SignedCount = Runtime::ToSignedInteger64(Right);
				if (SignedCount < 0)
				{
					ReportError("shift count out of range");
					return false;
				}
				Count = static_cast<std::uint64_t>(SignedCount);
			}
			const bool bWide = std::holds_alternative<std::int64_t>(Left) || std::holds_alternative<std::uint64_t>(Left);
			if (Count > (bWide ? 63u : 31u))
			{
				ReportError("shift count out of range");
				return false;
			}
			const std::uint32_t Shift = static_cast<std::uint32_t>(Count);
			if (const auto* U64Value = std::get_if<std::uint64_t>(&Left))
			{
				OutResult = Op == EOpcode::Shl ? *U64Value << Shift : *U64Value >> Shift;
			}
			else if (bLeftUnsigned)
			{
				const std::uint32_t U32Value =
				    static_cast<std::uint32_t>(Runtime::ToUnsignedInteger64(Left));
				OutResult = static_cast<std::uint32_t>(
				    Op == EOpcode::Shl ? U32Value << Shift : U32Value >> Shift);
			}
			else if (const auto* I64Value = std::get_if<std::int64_t>(&Left))
			{
				OutResult = Op == EOpcode::Shl
				    ? static_cast<std::int64_t>(static_cast<std::uint64_t>(*I64Value) << Shift)
				    : static_cast<std::int64_t>(*I64Value >> Shift);
			}
			else
			{
				const std::int32_t I32Value =
				    static_cast<std::int32_t>(Runtime::ToSignedInteger64(Left));
				OutResult = Op == EOpcode::Shl
				    ? static_cast<std::int32_t>(static_cast<std::uint32_t>(I32Value) << Shift)
				    : static_cast<std::int32_t>(I32Value >> Shift);
			}
			return true;
		}

		if ((bLeftSigned && bRightUnsigned) || (bLeftUnsigned && bRightSigned))
		{
			ReportError("cannot mix signed and unsigned");
			return false;
		}
		auto ApplyUnsigned = [Op](const std::uint64_t L, const std::uint64_t R)
		{
			return Op == EOpcode::BitAnd ? L & R
			    : Op == EOpcode::BitOr   ? L | R
			                             : L ^ R;
		};
		auto ApplySigned = [Op](const std::int64_t L, const std::int64_t R)
		{
			return Op == EOpcode::BitAnd ? L & R
			    : Op == EOpcode::BitOr   ? L | R
			                             : L ^ R;
		};
		if (bLeftUnsigned)
		{
			const bool bWide = std::holds_alternative<std::uint64_t>(Left) || std::holds_alternative<std::uint64_t>(Right);
			const std::uint64_t L = Runtime::ToUnsignedInteger64(Left);
			const std::uint64_t R = Runtime::ToUnsignedInteger64(Right);
			const std::uint64_t Result = ApplyUnsigned(L, R);
			OutResult = bWide ? FValue{Result} : FValue{static_cast<std::uint32_t>(Result)};
			return true;
		}
		const bool bWide = std::holds_alternative<std::int64_t>(Left) || std::holds_alternative<std::int64_t>(Right);
		const std::int64_t L = Runtime::ToSignedInteger64(Left);
		const std::int64_t R = Runtime::ToSignedInteger64(Right);
		const std::int64_t Result = ApplySigned(L, R);
		OutResult = bWide ? FValue{Result} : FValue{static_cast<std::int32_t>(Result)};
		return true;
	}

	bool VeroScriptVm::JitTryCheckedScalarCast(
	    const FValue& Value,
	    const std::uint16_t TargetTypeNameIndex,
	    FValue& OutResult,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (!ActiveModule || TargetTypeNameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		const std::string& TargetTypeName =
		    ActiveModule->StringPool[TargetTypeNameIndex];
		if (!Runtime::IsPrimitiveValueTypeName(TargetTypeName) || TargetTypeName == "string" || TargetTypeName == "wstring" || !(Runtime::IsNumericValue(Value) || std::holds_alternative<bool>(Value)))
		{
			return true;
		}
		OutHandled = true;
		OutResult = Value;
		return CheckedCastValue(OutResult, TargetTypeName, {});
	}

	bool VeroScriptVm::JitTryScalarBuiltinConversion(
	    const EBuiltinId Builtin,
	    const FValue& Value,
	    FValue& OutResult,
	    bool& OutHandled)
	{
		OutHandled = false;
		std::string_view TargetType;
		std::string_view OverflowError;
		switch (Builtin)
		{
		case EBuiltinId::ToInt:
			{
				TargetType = "int";
				OverflowError = "to_int overflow";
				break;
			}
		case EBuiltinId::ToUnsigned:
			{
				TargetType = "unsigned int";
				OverflowError = "to_unsigned overflow";
				break;
			}
		case EBuiltinId::ToInt64:
			{
				TargetType = "int64";
				OverflowError = "to_int64 overflow";
				break;
			}
		case EBuiltinId::ToUInt64:
			{
				TargetType = "uint64";
				OverflowError = "to_uint64 overflow";
				break;
			}
		case EBuiltinId::ToFloat:
			{
				TargetType = "float";
				break;
			}
		case EBuiltinId::ToDouble:
			{
				TargetType = "double";
				break;
			}

		case EBuiltinId::ToInt8:
			{
				TargetType = "int8";
				OverflowError = "char conversion overflow";
				break;
			}
		case EBuiltinId::ToUInt8:
			{
				TargetType = "uint8";
				OverflowError = "unsigned char conversion overflow";
				break;
			}
		case EBuiltinId::ToInt16:
			{
				TargetType = "int16";
				OverflowError = "short conversion overflow";
				break;
			}
		case EBuiltinId::ToUInt16:
			{
				TargetType = "uint16";
				OverflowError = "unsigned short conversion overflow";
				break;
			}
		default:
			return true;
		}
		if (!(Runtime::IsNumericValue(Value) || std::holds_alternative<bool>(Value)))
		{
			return true;
		}
		OutHandled = true;
		OutResult = Value;
		std::string ConversionError;
		if (!Runtime::CoerceValueToPrimitiveTypeName(
		        TargetType, OutResult, ConversionError))
		{
			ReportError(OverflowError.empty()
			        ? (ConversionError.empty()
			                  ? "numeric conversion failed"
			                  : ConversionError)
			        : std::string(OverflowError));
			return false;
		}
		return true;
	}

	bool VeroScriptVm::JitEvaluateCondition(
	    const FValue& Value,
	    bool& bOutCondition)
	{
		if (!ExpectBool(Value, {}))
		{
			return false;
		}
		bOutCondition = std::get<bool>(Value);
		return true;
	}

	bool VeroScriptVm::JitLocalGeImm(
	    const std::uint16_t Slot,
	    const std::int32_t Immediate,
	    bool& bOutResult)
	{
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			ReportError("JmpIfLocalGeImm: invalid slot");
			return false;
		}
		FValue& DirectValue = Frames.back().Locals[Slot];
		const FValue* const Value = Runtime::IsIntegerValue(DirectValue) || std::holds_alternative<float>(DirectValue) || std::holds_alternative<double>(DirectValue)
		    ? &DirectValue
		    : ResolveRefStorage(DirectValue);
		if (!Value)
		{
			ReportError("invalid ref binding");
			return false;
		}
		if (const auto* IntValue = std::get_if<std::int32_t>(Value))
		{
			bOutResult = *IntValue >= Immediate;
			return true;
		}
		if (Runtime::IsUnsignedIntegerValue(*Value))
		{
			bOutResult = Runtime::ToUnsignedInteger64(*Value) >= static_cast<std::uint64_t>(static_cast<std::uint32_t>(Immediate));
			return true;
		}
		if (Runtime::IsNumericValue(*Value))
		{
			bOutResult = !(Runtime::ToDouble(*Value) < static_cast<double>(Immediate));
			return true;
		}
		ReportError("JmpIfLocalGeImm requires numeric local");
		return false;
	}

	bool VeroScriptVm::JitLocalGeLocal(
	    const std::uint16_t LeftSlot,
	    const std::uint16_t RightSlot,
	    bool& bOutResult)
	{
		if (Frames.empty() || LeftSlot >= Frames.back().Locals.size() || RightSlot >= Frames.back().Locals.size())
		{
			ReportError("JIT local comparison: invalid slot");
			return false;
		}
		FValue& DirectLeft = Frames.back().Locals[LeftSlot];
		FValue& DirectRight = Frames.back().Locals[RightSlot];
		const auto IsDirectNumeric = [](const FValue& Value)
		{
			return Runtime::IsIntegerValue(Value) || std::holds_alternative<float>(Value) || std::holds_alternative<double>(Value);
		};
		const FValue* const Left = IsDirectNumeric(DirectLeft)
		    ? &DirectLeft
		    : ResolveRefStorage(DirectLeft);
		const FValue* const Right = IsDirectNumeric(DirectRight)
		    ? &DirectRight
		    : ResolveRefStorage(DirectRight);
		if (!Left || !Right)
		{
			ReportError("invalid ref binding");
			return false;
		}
		const auto TryCompareSameType = [&](const auto* const L, const auto* const R)
		{
			if (!L || !R)
			{
				return false;
			}
			bOutResult = !(*L < *R);
			return true;
		};
		if (TryCompareSameType(
		        std::get_if<std::int32_t>(Left), std::get_if<std::int32_t>(Right)) ||
		    TryCompareSameType(
		        std::get_if<std::uint32_t>(Left), std::get_if<std::uint32_t>(Right)) ||
		    TryCompareSameType(
		        std::get_if<std::int64_t>(Left), std::get_if<std::int64_t>(Right)) ||
		    TryCompareSameType(
		        std::get_if<std::uint64_t>(Left), std::get_if<std::uint64_t>(Right)) ||
		    TryCompareSameType(std::get_if<double>(Left), std::get_if<double>(Right)) || TryCompareSameType(std::get_if<float>(Left), std::get_if<float>(Right)) || TryCompareSameType(std::get_if<std::int8_t>(Left), std::get_if<std::int8_t>(Right)) || TryCompareSameType(std::get_if<std::uint8_t>(Left), std::get_if<std::uint8_t>(Right)) || TryCompareSameType(std::get_if<std::int16_t>(Left), std::get_if<std::int16_t>(Right)) || TryCompareSameType(std::get_if<std::uint16_t>(Left), std::get_if<std::uint16_t>(Right)))
		{
			return true;
		}
		bool bLess = false;
		if (!JitCompare(EOpcode::Lt, *Left, *Right, bLess))
		{
			return false;
		}
		bOutResult = !bLess;
		return true;
	}

	bool VeroScriptVm::JitCompareLocals(
	    const EOpcode Op,
	    const std::uint16_t LeftSlot,
	    const std::uint16_t RightSlot,
	    bool& bOutResult)
	{
		if (Op != EOpcode::Eq && Op != EOpcode::Ne && Op != EOpcode::Lt && Op != EOpcode::Le && Op != EOpcode::Gt && Op != EOpcode::Ge)
		{
			ReportError("JIT local comparison: invalid opcode");
			return false;
		}
		if (Frames.empty() || LeftSlot >= Frames.back().Locals.size() || RightSlot >= Frames.back().Locals.size())
		{
			ReportError("JIT local comparison: invalid slot");
			return false;
		}
		FValue& DirectLeft = Frames.back().Locals[LeftSlot];
		FValue& DirectRight = Frames.back().Locals[RightSlot];
		const auto IsDirectValue = [](const FValue& Value)
		{
			return Runtime::IsIntegerValue(Value) || std::holds_alternative<float>(Value) || std::holds_alternative<double>(Value) || std::holds_alternative<bool>(Value) || std::holds_alternative<std::string>(Value) || std::holds_alternative<std::u16string>(Value);
		};
		const FValue* const Left = IsDirectValue(DirectLeft)
		    ? &DirectLeft
		    : ResolveRefStorage(DirectLeft);
		const FValue* const Right = IsDirectValue(DirectRight)
		    ? &DirectRight
		    : ResolveRefStorage(DirectRight);
		if (!Left || !Right)
		{
			ReportError("invalid ref binding");
			return false;
		}

		const auto TryCompareSameType = [&](const auto* const L, const auto* const R)
		{
			if (!L || !R)
			{
				return false;
			}
			using TValue = std::remove_cv_t<std::remove_reference_t<decltype(*L)>>;
			// Preserve the generic comparison contract: bool and narrow string
			// support equality only. Ordering is numeric, plus wstring's defined
			// lexical order. Returning false delegates the diagnostic to JitCompare.
			if constexpr (std::is_same_v<TValue, bool> || std::is_same_v<TValue, std::string>)
			{
				if (Op != EOpcode::Eq && Op != EOpcode::Ne)
				{
					return false;
				}
			}
			bOutResult = Op == EOpcode::Eq ? *L == *R
			    : Op == EOpcode::Ne        ? *L != *R
			    : Op == EOpcode::Lt        ? *L < *R
			    : Op == EOpcode::Le        ? *L <= *R
			    : Op == EOpcode::Gt        ? *L > *R
			                               : *L >= *R;
			return true;
		};
		// The two locals normally retain one stable declared type. Dispatch once on
		// the variant tag instead of probing every numeric alternative before
		// reaching strings/bools; this is especially important for equality in hot
		// loops while preserving the generic mixed-type fallback below.
		if (Left->index() == Right->index())
		{
			const bool bHandled = std::visit(
			    [&](const auto& L) -> bool
			{
				using TValue = std::decay_t<decltype(L)>;
				if constexpr (std::is_same_v<TValue, std::int32_t> || std::is_same_v<TValue, std::uint32_t> || std::is_same_v<TValue, std::int64_t> || std::is_same_v<TValue, std::uint64_t> || std::is_same_v<TValue, double> || std::is_same_v<TValue, float> || std::is_same_v<TValue, std::int8_t> || std::is_same_v<TValue, std::uint8_t> || std::is_same_v<TValue, std::int16_t> || std::is_same_v<TValue, std::uint16_t> || std::is_same_v<TValue, bool> || std::is_same_v<TValue, std::string> || std::is_same_v<TValue, std::u16string>)
				{
					const TValue& R = std::get<TValue>(*Right);
					if constexpr (std::is_same_v<TValue, bool> || std::is_same_v<TValue, std::string>)
					{
						if (Op != EOpcode::Eq && Op != EOpcode::Ne)
						{
							return false;
						}
					}
					bOutResult = Op == EOpcode::Eq ? L == R
					    : Op == EOpcode::Ne        ? L != R
					    : Op == EOpcode::Lt        ? L < R
					    : Op == EOpcode::Le        ? L <= R
					    : Op == EOpcode::Gt        ? L > R
					                               : L >= R;
					return true;
				}
				else
				{
					return false;
				}
			},
			    *Left);
			if (bHandled)
			{
				return true;
			}
		}
		if (TryCompareSameType(
		        std::get_if<std::int32_t>(Left), std::get_if<std::int32_t>(Right)) ||
		    TryCompareSameType(
		        std::get_if<std::uint32_t>(Left), std::get_if<std::uint32_t>(Right)) ||
		    TryCompareSameType(
		        std::get_if<std::int64_t>(Left), std::get_if<std::int64_t>(Right)) ||
		    TryCompareSameType(
		        std::get_if<std::uint64_t>(Left), std::get_if<std::uint64_t>(Right)) ||
		    TryCompareSameType(std::get_if<double>(Left), std::get_if<double>(Right)) || TryCompareSameType(std::get_if<float>(Left), std::get_if<float>(Right)) || TryCompareSameType(std::get_if<std::int8_t>(Left), std::get_if<std::int8_t>(Right)) || TryCompareSameType(std::get_if<std::uint8_t>(Left), std::get_if<std::uint8_t>(Right)) || TryCompareSameType(std::get_if<std::int16_t>(Left), std::get_if<std::int16_t>(Right)) || TryCompareSameType(std::get_if<std::uint16_t>(Left), std::get_if<std::uint16_t>(Right)) || TryCompareSameType(std::get_if<bool>(Left), std::get_if<bool>(Right)) || TryCompareSameType(std::get_if<std::string>(Left), std::get_if<std::string>(Right)) || TryCompareSameType(std::get_if<std::u16string>(Left), std::get_if<std::u16string>(Right)))
		{
			return true;
		}

		return JitCompare(Op, *Left, *Right, bOutResult);
	}

	bool VeroScriptVm::JitCompare(
	    const EOpcode Op,
	    const FValue& Left,
	    const FValue& Right,
	    bool& OutResult)
	{
		if (Op == EOpcode::Eq || Op == EOpcode::Ne)
		{
			if ((Runtime::IsSignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right)) || (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right)))
			{
				ReportError("cannot mix signed and unsigned");
				return false;
			}
			const bool bLeftString = Runtime::IsStringValue(Left);
			const bool bRightString = Runtime::IsStringValue(Right);
			const bool bLeftWString = Runtime::IsWStringValue(Left);
			const bool bRightWString = Runtime::IsWStringValue(Right);
			if (bLeftString || bRightString || bLeftWString || bRightWString)
			{
				if ((bLeftWString || bRightWString) && (bLeftString || bRightString))
				{
					ReportError("cannot mix string and wstring comparison");
					return false;
				}
				if ((bLeftString && !bRightString) || (bRightString && !bLeftString) || (bLeftWString && !bRightWString) || (bRightWString && !bLeftWString))
				{
					ReportError("cannot compare mismatched string types");
					return false;
				}
			}
			bool bEqual = false;
			if (Runtime::IsNumericValue(Left) && Runtime::IsNumericValue(Right))
			{
				if (Runtime::IsSignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right))
				{
					const std::int64_t L = Runtime::ToSignedInteger64(Left);
					const std::int64_t R = Runtime::ToSignedInteger64(Right);
					bEqual = L == R;
				}
				else if (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right))
				{
					const std::uint64_t L = Runtime::ToUnsignedInteger64(Left);
					const std::uint64_t R = Runtime::ToUnsignedInteger64(Right);
					bEqual = L == R;
				}
				else
				{
					bEqual = Runtime::ToDouble(Left) == Runtime::ToDouble(Right);
				}
			}
			else if (!ActiveModule)
			{
				ReportError("JIT compare: no active module");
				return false;
			}
			else
			{
				bEqual = Runtime::ValuesEqual(*ActiveModule, Left, Right);
			}
			OutResult = Op == EOpcode::Eq ? bEqual : !bEqual;
			return true;
		}
		if (Op != EOpcode::Lt && Op != EOpcode::Le && Op != EOpcode::Gt && Op != EOpcode::Ge)
		{
			ReportError("JIT compare: invalid opcode");
			return false;
		}
		if (std::holds_alternative<std::int32_t>(Left) && std::holds_alternative<std::int32_t>(Right))
		{
			const auto L = std::get<std::int32_t>(Left);
			const auto R = std::get<std::int32_t>(Right);
			OutResult = Op == EOpcode::Lt ? L < R
			    : Op == EOpcode::Le       ? L <= R
			    : Op == EOpcode::Gt       ? L > R
			                              : L >= R;
			return true;
		}
		if (std::holds_alternative<std::uint32_t>(Left) && std::holds_alternative<std::uint32_t>(Right))
		{
			const auto L = std::get<std::uint32_t>(Left);
			const auto R = std::get<std::uint32_t>(Right);
			OutResult = Op == EOpcode::Lt ? L < R
			    : Op == EOpcode::Le       ? L <= R
			    : Op == EOpcode::Gt       ? L > R
			                              : L >= R;
			return true;
		}
		if (Runtime::IsSignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right))
		{
			const std::int64_t L = Runtime::ToSignedInteger64(Left);
			const std::int64_t R = Runtime::ToSignedInteger64(Right);
			OutResult = Op == EOpcode::Lt ? L < R
			    : Op == EOpcode::Le       ? L <= R
			    : Op == EOpcode::Gt       ? L > R
			                              : L >= R;
			return true;
		}
		if (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right))
		{
			const std::uint64_t L = Runtime::ToUnsignedInteger64(Left);
			const std::uint64_t R = Runtime::ToUnsignedInteger64(Right);
			OutResult = Op == EOpcode::Lt ? L < R
			    : Op == EOpcode::Le       ? L <= R
			    : Op == EOpcode::Gt       ? L > R
			                              : L >= R;
			return true;
		}
		if (Runtime::IsWStringValue(Left) && Runtime::IsWStringValue(Right))
		{
			const auto& L = std::get<std::u16string>(Left);
			const auto& R = std::get<std::u16string>(Right);
			OutResult = Op == EOpcode::Lt ? L < R
			    : Op == EOpcode::Le       ? L <= R
			    : Op == EOpcode::Gt       ? L > R
			                              : L >= R;
			return true;
		}
		if (!Runtime::IsNumericValue(Left) || !Runtime::IsNumericValue(Right))
		{
			ReportError("comparison operands must be numeric");
			return false;
		}
		if ((Runtime::IsSignedIntegerValue(Left) && Runtime::IsUnsignedIntegerValue(Right)) || (Runtime::IsUnsignedIntegerValue(Left) && Runtime::IsSignedIntegerValue(Right)))
		{
			ReportError("cannot mix signed and unsigned");
			return false;
		}
		const double L = Runtime::ToDouble(Left);
		const double R = Runtime::ToDouble(Right);
		OutResult = Op == EOpcode::Lt ? L < R
		    : Op == EOpcode::Le       ? L <= R
		    : Op == EOpcode::Gt       ? L > R
		                              : L >= R;
		return true;
	}

	void VeroScriptVm::BeginExecutionGovernance()
	{
		if (Frames.empty() && ActiveJitCallDepth == 0 && !bBorrowedExecutionCleanup)
		{
			JitDiagnostics.reset();
		}
		if (!bBorrowedExecutionCleanup)
		{
			ExecutionCleanup.reset();
		}
		// Borrowed modules may be destroyed or replaced between top-level calls.
		// Their address and the host-selected generation alone do not prove that
		// cached field/method slots still describe the same declaration. An owned,
		// immutable module keeps its identity alive and can retain these lookups.
		if (!JitProgramOwner || JitProgramOwner.get() != ActiveModule)
		{
			ResetModuleLookupCaches();
		}
		LanguageCopyPlanModule = nullptr;
		// Error-location pinning is scoped to one top-level execution.  A VM can be
		// reused through Execute/ExecuteFunction; retaining a pin from the previous
		// failure would prevent a later native JIT failure from selecting its own
		// bytecode/source location.
		bJitErrorLocationPinned = false;
		ExecutionGovernance.Begin(ExecutionLimits, ExternalExecutionDeadline);
	}

	bool VeroScriptVm::CheckExecutionGovernance()
	{
		if (ExecutionCleanup) return CheckExecutionCleanup();
		if (bExecutionResetRequired)
		{
			(void)RejectExecutionReuse();
			return false;
		}
		if (ExternalExecutionCheck && !ExternalExecutionCheck())
		{
			return ExecutionInterrupted(EExecutionInterruption::Cancelled);
		}
		return ExecutionDetail::PollCancellationAndDeadline(ExecutionGovernance, ExecutionControl,
		    ExternalExecutionDeadline, [&](EExecutionInterruption Reason)
		{
			return ExecutionInterrupted(Reason);
		});
	}

	bool VeroScriptVm::ExecutionInterrupted(EExecutionInterruption Reason)
	{
		ReportErrorId(Reason == EExecutionInterruption::Cancelled
		        ? "diag.execution_cancelled_by_host"
		        : "diag.execution_time_limit_exceeded");
		BeginExecutionCleanup();
		return false;
	}

	void VeroScriptVm::ResetModuleLookupCaches()
	{
		// Frame caches may survive local-storage pooling, never a program/binding
		// invalidation. The next entry rebuilds them even if addresses are reused.
		for (auto &Frame : FreeFrames)
		{
			Frame.CacheModule = nullptr;
		}
		for (auto &Frame : Frames)
		{
			Frame.CacheModule = nullptr;
		}
		CallableSignatures.clear();
		CallableSignatureModule = nullptr;
		CallableSignatureGeneration = 0;
		ScalarCallPlans.clear();
		ScalarCallPlanModule = nullptr;
		ScalarCallPlanGeneration = 0;
		TypeIndexModule = nullptr;
		TypeIndexGeneration = 0;
		TypeIndexByName.clear();
		LastTypeIndexName.clear();
		LastTypeIndex = 0xFFFF;
		TypeCompatibilityModule = nullptr;
		TypeCompatibilityGeneration = 0;
		TypeCompatibility.clear();
		ObjectTypePlanModule = nullptr;
		ObjectTypePlanGeneration = 0;
		ObjectTypePlans.clear();
		// The only fixed-cache miss writers mark both replacement counters and
		// entries dirty. Repeated scalar executions need no redundant bulk stores.
		if (bModuleLookupArraysDirty)
		{
			JitFieldIc = {};
			JitFieldIcNextWay = {};
			MethodDispatchIc = {};
			MethodDispatchIcNextWay = {};
			bModuleLookupArraysDirty = false;
		}
		JitTypeFieldIc.clear();
		JitTypeFieldIcModule = nullptr;
		JitTypeFieldIcGeneration = 0;
		MethodDispatchTypeIc.clear();
		MethodDispatchTypeIcModule = nullptr;
		MethodDispatchTypeIcGeneration = 0;
		LanguageCopyPlanModule = nullptr;
	}

}
