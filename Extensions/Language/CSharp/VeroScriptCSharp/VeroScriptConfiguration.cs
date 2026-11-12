// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript;

/// <summary>Defines an in-memory source file supplied to the compiler.</summary>
public sealed record VeroScriptSourceOverlay(string Path, string Source);

/// <summary>Defines a preprocessor macro supplied by the host.</summary>
public sealed record VeroScriptPredefinedMacro(
	string Name,
	string Body,
	IReadOnlyList<string>? Parameters = null,
	string SourceName = "<host>",
	bool Variadic = false);

/// <summary>Configures compiler, runtime, import, cache, and source-overlay behavior.</summary>
public sealed class VeroScriptConfiguration
{
	public IReadOnlyList<string> ImportPaths { get; init; } = [];
	public IReadOnlyList<string> ImplicitImportPaths { get; init; } = [];
	public IReadOnlyList<string> ScriptArguments { get; init; } = [];
	public IReadOnlyList<string> Defines { get; init; } = [];
	/// <summary>Trusted directories used to resolve source standard-library modules.</summary>
	/// <remarks>
	/// Import every module used directly by the script. Ordinary imports do not
	/// transitively re-export modules imported by the standard-library source.
	/// </remarks>
	public IReadOnlyList<string> StandardLibrarySearchPaths { get; init; } = [];
	public IReadOnlyList<VeroScriptSourceOverlay> SourceOverlays { get; init; } = [];
	public IReadOnlyList<VeroScriptPredefinedMacro> PredefinedMacros { get; init; } = [];
	public string BytecodeCacheDirectory { get; init; } = string.Empty;
	public ulong MaxCallDepth { get; init; }
	/// <summary>Maximum executed instructions in the VM and eligible controlled JIT paths; zero leaves execution unlimited.</summary>
	public ulong MaxInstructions { get; init; }
	/// <summary>Wall-clock execution limit in milliseconds; zero leaves execution unlimited.</summary>
	public ulong TimeoutMilliseconds { get; init; }
	/// <summary>Enables cooperative host cancellation for this session.</summary>
	public bool EnableExecutionControl { get; init; }
	/// <summary>Enables the standard-library capability policy for this session.</summary>
	public bool CapabilityPolicyEnabled { get; init; }
	/// <summary>Side-effect capabilities granted when <see cref="CapabilityPolicyEnabled"/> is enabled.</summary>
	public VeroScriptScriptCapabilities AllowedScriptCapabilities { get; init; }
	/// <summary>
	/// Selects the execution backend. Mixed is the recommended default: supported
	/// functions use JIT compilation and unsupported functions fall back to the VM.
	/// </summary>
	public VeroScriptExecutionBackend ExecutionBackend { get; init; } = VeroScriptExecutionBackend.Mixed;
	public bool UseBytecodeCache { get; init; }
	public bool AllowCircularImports { get; init; }
	public bool AllowProjectWideAccess { get; init; }
}

/// <summary>Controls mixed/JIT compilation thresholds, budgets, and session reuse.</summary>
/// <param name="MixedHotCallThreshold">Calls interpreted before Mixed mode considers compilation.</param>
/// <param name="MaxCompileAttempts">Maximum compile attempts per module generation; zero is unlimited.</param>
/// <param name="MaxCachedEntries">Maximum cached decisions per module generation; zero is unlimited.</param>
/// <param name="EagerCompileBudget">Functions considered eagerly by JIT mode; zero is unlimited.</param>
public readonly record struct VeroScriptJitPolicy(
	uint MixedHotCallThreshold,
	ulong MaxCompileAttempts,
	ulong MaxCachedEntries,
	ulong EagerCompileBudget)
{
	/// <summary>
	/// Allows ordinary, eligible <c>CallFunction</c> invocations to retain compiled
	/// code across their otherwise fresh VM instances.
	/// </summary>
	/// <remarks>
	/// This defaults to <see langword="true"/> for the four-argument constructor.
	/// Registered native functions, interceptors, mutation callbacks,
	/// hot-reload execution, and the VM backend exclude reuse.
	/// Governed execution can reuse a cache with matching governance requirements.
	/// Zero compile-attempt or cached-entry budgets mean unlimited counts; the
	/// retained native-code and complete-payload byte limits still apply.
	/// </remarks>
	public bool ReuseSessionCode { get; init; } = true;
	/// <summary>Opts governed Mixed execution into eligible controlled JIT paths; defaults to false.</summary>
	/// <remarks>Measure the target workload before enabling. Explicit JIT selection is independent
	/// of this switch. Unsupported or VM-preferred entries retain the VM fallback and governance checks.</remarks>
	public bool EnableControlledMixed { get; init; }
}

/// <summary>Limits JIT payload retained by an eligible session between function calls.</summary>
/// <param name="MaxNativeBytes">Maximum retained executable-code bytes.</param>
/// <param name="MaxRetainedPayloadBytes">Maximum complete retained cache payload.</param>
/// <remarks>
/// Zero permits no retained payload of the corresponding kind. The defaults are
/// 8 MiB of native code and 32 MiB of complete retained payload.
/// </remarks>
public readonly record struct VeroScriptJitCacheLimits(
	ulong MaxNativeBytes,
	ulong MaxRetainedPayloadBytes);

/// <summary>Reports settled resource use for the current session JIT-cache context.</summary>
public sealed class VeroScriptJitCacheStats
{
	/// <summary>Gets cumulative compile attempts in the current cache context.</summary>
	public ulong CompileAttempts { get; internal init; }
	/// <summary>Gets the complete payload currently retained between calls.</summary>
	public ulong RetainedPayloadBytes { get; internal init; }
	/// <summary>Gets executable-code bytes currently retained between calls.</summary>
	public ulong NativeBytes { get; internal init; }
	/// <summary>Gets whether a configured resource limit prevented retention.</summary>
	public bool ResourceLimitReached { get; internal init; }
}

/// <summary>Reports JIT and fast-path statistics from the most recent execution.</summary>
/// <remarks>
/// Execution counters include entries that exit with errors or control transfers. Compilation
/// counters describe generated code; a warm cache can execute without compiling. Direct calls
/// and typed-loop kernels are nested events, not additional ordinary executable entries.
/// Older compatible runtimes report zero for counters they do not provide.
/// </remarks>
public sealed class VeroScriptJitStats
{
	public ulong CompileAttempts { get; internal init; }
	public ulong CacheHits { get; internal init; }
	public ulong CompiledEntries { get; internal init; }
	public ulong NativeLeafEntries { get; internal init; }
	public ulong NativeCfgEntries { get; internal init; }
	public ulong BridgeTrampolineEntries { get; internal init; }
	public ulong LinearTrampolineEntries { get; internal init; }
	public ulong VmTrampolineEntries { get; internal init; }
	public ulong UnsupportedEntries { get; internal init; }
	public ulong DispatchAttempts { get; internal init; }
	public ulong Dispatches { get; internal init; }
	public ulong Fallbacks { get; internal init; }
	/// <summary>Gets VM fallbacks deliberately selected by the JIT cost model.</summary>
	public ulong CostModelVmFallbacks { get; internal init; }
	public ulong Failures { get; internal init; }
	public ulong CacheInvalidations { get; internal init; }
	public ulong HotnessDeferrals { get; internal init; }
	public ulong HotFunctions { get; internal init; }
	public ulong CompileBudgetSkips { get; internal init; }
	public ulong CacheCapacitySkips { get; internal init; }
	public ulong EagerCompileSkips { get; internal init; }
	public ulong ArrayFastPathPushes { get; internal init; }
	public ulong ArrayFastPathGets { get; internal init; }
	public ulong ArrayFastPathSets { get; internal init; }
	public ulong ScalarMapFastPathGets { get; internal init; }
	public ulong ScalarMapFastPathSets { get; internal init; }
	/// <summary>Gets native typed-loop kernel invocations, including source-visible runtime failures.</summary>
	public ulong NativeTypedLoopExecutions { get; internal init; }
	public ulong ControlledNativeExecutions { get; internal init; }
	public ulong AotExecutions { get; internal init; }
	/// <summary>Gets actual native leaf entries during the most recent execution.</summary>
	public ulong NativeLeafExecutions { get; internal init; }
	/// <summary>Gets actual native control-flow graph entries during the most recent execution.</summary>
	public ulong NativeCfgExecutions { get; internal init; }
	/// <summary>Gets actual bridge entries during the most recent execution.</summary>
	public ulong BridgeExecutions { get; internal init; }
	/// <summary>Gets actual linear entries during the most recent execution.</summary>
	public ulong LinearExecutions { get; internal init; }
	/// <summary>Gets actual VM trampoline entries during the most recent execution.</summary>
	public ulong VmTrampolineExecutions { get; internal init; }
	public ulong ScalarMethodFastPathInt32 { get; internal init; }
	public ulong ScalarMethodFastPathUInt32 { get; internal init; }
	public ulong ScalarMethodFastPathDouble { get; internal init; }
	public ulong ScalarMethodFastPathMixed { get; internal init; }
	public ulong ScalarFunctionFastPathInt32 { get; internal init; }
	public ulong ScalarFunctionFastPathUInt32 { get; internal init; }
	public ulong ScalarFunctionFastPathDouble { get; internal init; }
	public ulong ScalarFunctionFastPathMixed { get; internal init; }
	public ulong ScalarNativeFastPathCalls { get; internal init; }
	public ulong DirectNativeCalls { get; internal init; }
	public ulong InlinedCallSites { get; internal init; }
	public ulong LinearFrameReuses { get; internal init; }
	public ulong LinearScratchReuses { get; internal init; }
	public ulong LinearScratchAllocations { get; internal init; }
	public ulong LinearArgumentScratchLeases { get; internal init; }
	public ulong LinearArgumentScratchReuses { get; internal init; }
}

/// <summary>Selects a concrete JIT module independently of execution policy.</summary>
public enum VeroScriptJitBackend : uint
{
	None = 0,
	External = 1,
}

/// <summary>Describes capabilities advertised by a selected JIT backend.</summary>
[Flags]
public enum VeroScriptJitBackendCapabilities : ulong
{
	None = 0,
	NativeCode = 0x1,
	PortableExecution = 0x2,
	NativeTypedLoops = 0x4,
	DirectScalarCalls = 0x8,
	ControlledExecution = 0x10,
}

/// <summary>Copies the session's selected backend metadata, including the interpreter when none is selected.</summary>
public sealed record VeroScriptJitBackendInfo(string Id, string Target, ulong Capabilities, bool NativeCode)
{
	/// <summary>Gets the backend capability flags, preserving any unknown native bits.</summary>
	public VeroScriptJitBackendCapabilities CapabilityFlags => (VeroScriptJitBackendCapabilities)Capabilities;
}
