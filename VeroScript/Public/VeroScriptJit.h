// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

namespace VeroScript
{
	enum class EJitExecutionKind : std::uint8_t
	{
		Vm, VmScalar, NativeLeaf, NativeCfg, ControlledNative, Linear, Bridge, VmTrampoline, Aot, Unknown, Count
	};
	enum class EJitFallbackReason : std::uint8_t
	{
		None, UnsupportedOpcode, UnsupportedType, UnsupportedCall, Ownership, ReferenceParameter,
		Closure, ExceptionHandling, CostModel, Governance, Observation, Debugger, Hotness,
		CompileBudget, CacheCapacity, ResourceBudget, InvocationDepth, EntryUnavailable, RuntimeGuard, Count
	};
	struct FJitDiagnosticOptions
	{
		// Zero disables collection: no clock reads, record allocation or logging.
		std::size_t MaxFunctions = 0;
		std::size_t MaxDepth = 256;
		bool bMeasureTime = false;
	};
	struct FJitPolicy
	{
		// Keep the new switch in x64's existing alignment gap. The original four
		// positional arguments keep their meaning even though this is no longer
		// an aggregate; hosts using designated initialization must set members.
		constexpr FJitPolicy(
			const std::uint32_t InMixedHotCallThreshold = 1,
			const std::size_t InMaxCompileAttempts = 4096,
			const std::size_t InMaxCachedEntries = 4096,
			const std::size_t InEagerCompileBudget = 4096,
			const bool InReuseSessionCode = true) noexcept
			: MixedHotCallThreshold(InMixedHotCallThreshold)
			, bReuseSessionCode(InReuseSessionCode)
			, MaxCompileAttempts(InMaxCompileAttempts)
			, MaxCachedEntries(InMaxCachedEntries)
			, EagerCompileBudget(InEagerCompileBudget)
		{
		}

		// Mixed mode interprets this many calls before compiling a function.
		std::uint32_t MixedHotCallThreshold = 1;
		// Eligible session executions may retain code between fresh VMs.
		// Does not change raw external-module VM APIs or persistent-state execution.
		bool bReuseSessionCode = true;
		// Maximum actual compile attempts between invalidations.
		std::size_t MaxCompileAttempts = 4096;
		// Maximum cached compiled/unsupported decisions between invalidations.
		std::size_t MaxCachedEntries = 4096;
		// Maximum functions eagerly considered when the Jit backend prepares a module.
		// Borrowed/non-retained invocations prepare their entry first; reached callees
		// remain eligible on call. Worker caches have the same short lifetime.
		std::size_t EagerCompileBudget = 4096;
		// The correctness-first controlled native path may cost more than the VM.
		// Keep governed Mixed execution interpreted until the host opts in after
		// measuring its target workload. Explicit Jit and selected static AOT are
		// independent of this throughput policy.
		bool bEnableControlledMixed = false;
		FJitDiagnosticOptions Diagnostics;
	};

	struct FJitStats
	{
		std::size_t CompileAttempts = 0;
		std::size_t CacheHits = 0;
		std::size_t CompiledEntries = 0;
		std::size_t NativeLeafEntries = 0;
		std::size_t NativeCfgEntries = 0;
		std::size_t BridgeTrampolineEntries = 0;
		std::size_t LinearTrampolineEntries = 0;
		std::size_t VmTrampolineEntries = 0;
		std::size_t UnsupportedEntries = 0;
		std::size_t DispatchAttempts = 0;
		std::size_t Dispatches = 0;
		std::size_t Fallbacks = 0;
		// Subset of Fallbacks deliberately selected by the backend cost model
		// because the available Linear path is valid but predicted slower than VM.
		std::size_t CostModelVmFallbacks = 0;
		std::size_t Failures = 0;
		std::size_t CacheInvalidations = 0;
		std::size_t HotnessDeferrals = 0;
		std::size_t HotFunctions = 0;
		std::size_t CompileBudgetSkips = 0;
		std::size_t CacheCapacitySkips = 0;
		std::size_t EagerCompileSkips = 0;
		std::size_t ArrayFastPathPushes = 0;
		std::size_t ArrayFastPathGets = 0;
		std::size_t ArrayFastPathSets = 0;
		std::size_t ScalarMapFastPathGets = 0;
		std::size_t ScalarMapFastPathSets = 0;
		// Native typed-loop kernels actually invoked, including invocations that
		// terminate by reporting a source-visible runtime failure.
		std::size_t NativeTypedLoopExecutions = 0;
		std::size_t ScalarMethodFastPathI32 = 0;
		std::size_t ScalarMethodFastPathU32 = 0;
		std::size_t ScalarMethodFastPathF64 = 0;
		std::size_t ScalarMethodFastPathMixed = 0;
		std::size_t ScalarFunctionFastPathI32 = 0;
		std::size_t ScalarFunctionFastPathU32 = 0;
		std::size_t ScalarFunctionFastPathF64 = 0;
		std::size_t ScalarFunctionFastPathMixed = 0;
		std::size_t ScalarNativeFastPathCalls = 0;
		// Nested generation-scoped NativeCfg entries reached without a VM/FValue
		// bridge. Kept separate from the ABI-specific fast-path totals so tests and
		// profiling can prove that a direct target table is actually exercised.
		std::size_t DirectNativeCalls = 0;
		// Actual generated control flow with canonical per-instruction governance.
		std::size_t ControlledNativeExecutions = 0;
		std::size_t AotExecutions = 0;
		std::size_t InlinedCallSites = 0;
		// Successful VM-local pool acquisitions. These counters expose allocator
		// regressions without participating in compilation or dispatch decisions.
		std::size_t LinearFrameReuses = 0;
		std::size_t LinearScratchReuses = 0;
		std::size_t LinearScratchAllocations = 0;
		std::size_t LinearArgumentScratchLeases = 0;
		std::size_t LinearArgumentScratchReuses = 0;
		// Actual ordinary executable entries, including entries which exit by error
		// or control transfer. Compilation counts above are not execution counts.
		// Scalar/direct calls and typed loops are separately reported nested events;
		// they must not be added to these counts as independent function calls.
		std::size_t NativeLeafExecutions = 0;
		std::size_t NativeCfgExecutions = 0;
		std::size_t BridgeExecutions = 0;
		std::size_t LinearExecutions = 0;
		std::size_t VmTrampolineExecutions = 0;
	};
}
