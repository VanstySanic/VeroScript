# C# sessions and runtime

## Deployment

Reference the `VeroScript.CSharp` package and deploy a trusted
`VeroScriptInterop` shared library for the operating system and architecture.
The managed package does not install a .NET runtime or native RID assets. If
the native library is not in a safe loader directory, set
`VEROSCRIPT_INTEROP_LIBRARY` to an absolute path. Relative library paths are
rejected to avoid working-directory search ambiguity.
Set this path before the first SDK call. Subsequent calls reuse the selected native library.

The package provides .NET 8 and .NET 9 managed assets. The host chooses
framework-dependent or self-contained deployment; the native library must
still match the process architecture.

## Configuration

`VeroScriptConfiguration` includes:

| Property | Meaning |
| --- | --- |
| `ImportPaths` | Additional module search paths. |
| `ImplicitImportPaths` | Files implicitly imported into every source module. |
| `ScriptArguments` | Arguments passed to `main(string[] args)`. |
| `Defines` | Object-like macros or conditional-compilation definitions. |
| `StandardLibrarySearchPaths` | Additional standard-library roots. |
| `SourceOverlays` | In-memory path/source overrides. |
| `PredefinedMacros` | Structured host-style macros. |
| `BytecodeCacheDirectory` and `UseBytecodeCache` | Bytecode-cache location and switch. |
| `MaxCallDepth` | Per-run recursion limit; zero uses the runtime default. |
| `MaxInstructions` | Maximum executed instructions across VM and eligible controlled JIT paths; zero means unlimited. |
| `TimeoutMilliseconds` | Wall-clock execution limit; zero means unlimited. |
| `EnableExecutionControl` | Enables cooperative cancellation for the session. |
| `CapabilityPolicyEnabled` | Enables the standard-library side-effect policy. |
| `AllowedScriptCapabilities` | Grants file, environment, console, and task-spawn capabilities when the policy is enabled. |
| `ExecutionBackend` | `Vm`, `Mixed` (default), or `Jit`. |
| `AllowCircularImports` | Whether cyclic module graphs are allowed. |
| `AllowProjectWideAccess` | Whether non-exported declarations are shared project-wide. |

Apply configuration before loading the source:

```CSharp
session.ApplyConfiguration(new VeroScriptConfiguration
{
	ImportPaths = ["modules"],
	ScriptArguments = ["release"],
	ExecutionBackend = VeroScriptExecutionBackend.Mixed,
	UseBytecodeCache = true,
	AllowCircularImports = true,
});
```

## Loading, running, and calling

`LoadSource` compiles an in-memory source buffer and `LoadFile` loads a file.
`Run(VeroScriptRunMode.Script)` initializes the module and invokes `main`;
`Eval` uses the incremental evaluation path. `EvalOnce` runs initialization
without `main` and permits initializer JIT execution with a selected backend.
Ordinary sessions initialize fresh state on each run; hot-reload sessions retain
initialized globals and use the VM for retained initialization. The mode requires
`VeroScriptCapabilities.EvalOnce`; older runtimes throw `NotSupportedException`
before execution. A session can also load or reload a `VeroScriptPackage`.
`Clear` discards the loaded program state, and `SetInput` supplies the buffer
used by standard-library input functions for the next execution.

After loading, use `IsLoaded` to inspect session state and use
`CallFunction`, `CallMethod`, `Construct`, `GetField`,
`SetField`, `GetGlobal`, and `SetGlobal`. The overload accepting a destination
`VeroScriptValue` avoids allocating a separate return wrapper when the host
already owns one.

Use `HasFunction` and `HasMethod` before optional calls. Enable state-preserving
source reload explicitly before calling `Reload`:

```CSharp
session.HotReloadEnabled = true;
var reloaded = session.Reload();
```

Call `GetReloadStatus()` on the creator thread after `Reload`, `ReloadPackage`, or the
execution that initializes the replacement. `Pending` means the candidate awaits
initialization, `RolledBack` restores the previous generation, and `AppliedWithErrors`
means the candidate committed before a later error. Inspect `CommittedGeneration` and
`OperationId` to identify the outcome. For `ResetRequired`, call `Clear` and reload the
program. Recreate the session if it cannot be cleared. This query preserves diagnostics and
requires `VeroScriptCapabilities.ReloadStatus`.

When a backend resolver rejects `ApplyConfiguration`, it throws `VeroScriptException` with the native reason and preserves the previous cancellation policy. A rejected `HotReloadEnabled` change also throws. Inspect `GetReloadStatus()` and follow the reported recovery instruction. Cancellation failures report the cancellation operation and its native status. When execution control is disabled, the previous native error remains available to the creator thread.

`VeroScriptRunResult` reports `Success`, `ExitRequested`, `ExitCode`, and the
diagnostic count. Compilation and script/runtime failures are normally returned
as `Success == false`. Native ownership rejection can also return a failed run
with structured diagnostics; check the result and `GetDiagnostics()`. Other
native status failures throw `VeroScriptException`; managed argument, lifetime
and ABI compatibility guards throw their corresponding managed exceptions.

## Execution limits, cancellation, and capabilities

`MaxInstructions` and `TimeoutMilliseconds` are cooperative per-execution
limits; zero disables the corresponding limit. VM and eligible controlled JIT
paths preserve instruction accounting, limits, cancellation, and capability
restrictions. Governed `Mixed` defaults to the VM; enable
`VeroScriptJitPolicy.EnableControlledMixed` only after measuring the target
workload. Explicit `Jit` selection is independent of this switch. Unsupported or
VM-preferred entries keep the governed VM fallback. A limit or cancellation
request does not forcibly terminate a native or managed host callback that is
already running.

When `EnableExecutionControl` is true, call `RequestCancellation()` to request
cancellation of the active execution. It is thread-safe and is the one session
operation that may be called from a non-owner thread. Keep the session alive
until the call returns. The request is cooperative and may be observed only at
execution safe points.
Applying a configuration successfully resets the session's cancellation token,
so the session can be prepared for another execution after a cancellation.

When `CapabilityPolicyEnabled` is true, only the flags in
`AllowedScriptCapabilities` are available to standard-library side effects:
`FileRead`, `FileWrite`, `FileDelete`, `EnvironmentRead`, `EnvironmentWrite`,
`ConsoleInput`, `ConsoleOutput`, `TaskSpawn`, and `ProcessDirectoryChange`. `chdir`
requires both `EnvironmentWrite` and `ProcessDirectoryChange`. The policy covers built-ins;
host callbacks remain responsible for their own authorization. With the policy
disabled, these built-ins retain the default allow-all behavior.

## Diagnostics and localization

Use `GetDiagnostics()` for structured records and
`GetDiagnosticArguments(index)` for catalog placeholders. `FormatDiagnostic`
formats one record in the current process language. `VeroScriptRuntime` exposes
`AbiVersion`, `RuntimeVersion`, `HostPlatform`, `Capabilities`,
`CurrentLanguage`, and the available language JSON.
`GetLastError()` returns the most recent session-level error text when a
transport or adapter failure needs additional context.

```CSharp
VeroScriptRuntime.CurrentLanguage = "zh-CN";
var diagnostic = session.FormatDiagnostic(0);
```

Runtime messages use the selected catalog. Adapter-only errors and fallback
placeholders use English text.

## Backends and JIT policy

`Mixed` uses JIT compilation for supported functions and VM fallback for the rest.
`Jit` requests eager compilation but is not a strict no-fallback mode; `Vm` is
interpreter-only. An attached debugger forces VM execution. Hot reload uses the VM for
retained initialization. Eligible calls can use the selected JIT backend.

```CSharp
session.SetJitPolicy(new VeroScriptJitPolicy(
	MixedHotCallThreshold: 2,
	MaxCompileAttempts: 256,
	MaxCachedEntries: 256,
	EagerCompileBudget: 64));
VeroScriptJitStats stats = session.GetLastJitStats();
```

The statistics describe compilation, dispatch, fallback, budget, and fast-path
activity for the most recent execution. `DirectNativeCalls` counts
generation-scoped JIT calls that reached another native entry without passing
through the VM value bridge. These are diagnostic counters, not performance
guarantees. Measure normal, non-debug runs with fixed inputs and account for
warm-up.

### Native JIT capabilities and counters

`NativeLeafExecutions`, `NativeCfgExecutions`, `BridgeExecutions`,
`LinearExecutions`, and `VmTrampolineExecutions` count actual ordinary executable
entries during the most recent execution, including entries that exit with errors
or control transfers. The corresponding `*Entries` properties count generated
artifacts. A warm cache can execute without compiling; `GetJitCacheStats().CompileAttempts`
remains cumulative for the retained cache context. `ScalarNativeFastPathCalls` counts
host native calls; direct calls and typed-loop kernels are nested events, so do not
add them to ordinary entries. Older compatible runtimes return zero for counters
they do not provide. These are aggregate statistics; optional per-function JIT
diagnostics remain part of the native Core/C++ API.

`CostModelVmFallbacks` is the subset of `Fallbacks` deliberately selected
when a valid compiled path is predicted to cost more than the VM; it is not a
compilation failure or a change in script semantics. `NativeTypedLoopExecutions`
counts native typed-loop kernel invocations, including invocations that report a
source-visible runtime failure. It is not a success counter. Check
`session.GetJitBackendInfo().CapabilityFlags` for `VeroScriptJitBackendCapabilities.NativeCode` and
`VeroScriptJitBackendCapabilities.NativeTypedLoops` separately. Do not infer either
capability from `HostPlatform` or from a nonzero counter.

The backend's `ControlledExecution` capability reports support for controlled JIT
execution. `ControlledNativeExecutions` counts controlled native entry invocations,
and `AotExecutions` counts AOT entry invocations. Neither support nor policy opt-in
guarantees that a particular entry executes natively.

### Reusable session-code cache

This cache is separate from the bytecode cache. When `VeroScriptRuntime.Capabilities`
includes `SessionJitCache`, use `SetJitCacheLimits`, `GetJitCacheLimits`, and
`GetJitCacheStats` to govern compiled code retained by eligible session executions.
`VeroScriptJitPolicy.ReuseSessionCode` defaults to `true`. Zero `MaxCompileAttempts`
or `MaxCachedEntries` means unlimited counts, including in the retained context;
native-code and complete-payload byte ceilings still apply. Reuse requires an
effective backend other than VM and a stable revision for native function bindings.
Binding or callback changes invalidate the context. Governed execution can reuse a
context with matching governance requirements. Defaults are 8 MiB of native code and
32 MiB of total retained payload; zero permits no retention of the corresponding
resource. Cache statistics are settled cumulative values for the current cache
context, not live or process-wide memory counters. Changing policy, cache limits,
backend, configuration, module generation, bindings, or hot-reload/observability state
invalidates that context; reaching a resource limit falls back safely rather than
changing script semantics.

## Threading and callbacks

Sessions are creator-thread affine. Script callbacks can arrive concurrently
from VM worker threads, so synchronize shared managed state. Output,
diagnostic, and mutation callbacks cannot report failures across the ABI; their
exceptions are contained. Host-function, interceptor, generic-factory, and
serialization exceptions are converted into callback failures.

## Independent VM selection

<a id="vm-execution-domain"></a>

The standard Interop product assembles the bytecode VM explicitly. VM selection is
independent of `ExecutionBackend` and JIT selection. On the idle creator thread,
use `SetVmBackend(path)` to select a matching interpreter library file or directory;
null or empty clears selection. Failed replacement preserves the previous choice.
`GetVmBackendInfo()` copies its identity, version, target, library path, raw VM
capabilities and selected/frozen state. Static selections have an empty library path.
VM capability bits describe bytecode (1), governance (2), debug (4), hot reload (8)
and cleanup (16), independently of process capability flags.

Check `VeroScriptCapabilities.VmBackendSelection`; older same-ABI runtimes without
this feature throw `NotSupportedException` before calling the optional exports.
Committed execution freezes the domain, including JIT and compilation extensions.
`Clear` and reload do not thaw it: create a new session to change a frozen choice.
Clearing selection does not restore the product VM and can leave execution without
an interpreter. Managed metadata snapshots remain readable after replacement or disposal.

## Explicit JIT selection

Execution policy and backend selection are separate. Without an installed module environment, Runtime supplies no default module: mixed uses the VM without a selected backend; forced jit fails before execution. Select an independently installed, compatible library file or directory with `VeroScriptSession.SetJitBackend(path)` while the Session is idle on its owner thread. `SetJitBackend(null)` / `SetJitBackend("")` disables selection. A failed replacement preserves the previous backend. Use `GetJitBackend()` / `GetJitBackendInfo()` for actual selected identity/native capabilities; `backend-services` alone is the Core service contract. Governed execution follows the controlled JIT policy described above. File/directory discovery and ABI rules are described in the JIT guide.

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/en-US/index.md)

```CSharp
using var session = new VeroScriptSession();
session.SetJitBackend("/absolute/path/to/Jit");
session.LoadSource("int main(){ return 0; }");
var result = session.Run();
```

<a id="jit-backend-capabilities"></a>

Query `session.GetJitBackendInfo().CapabilityFlags` with `VeroScriptJitBackendCapabilities` for the selected backend. `Capabilities` preserves the raw native mask, including unknown bits. The snapshot owns its strings and survives backend replacement or session disposal. With no selected backend, its flags are `None`. Runtime-wide `VeroScriptRuntime.Capabilities` reports Core services rather than the selected backend’s native features.

<a id="diagnostic-translation"></a>

`VeroScriptRuntime.Translate(englishMessage)` translates a diagnostic using the current process-wide language and catalogs. Unknown messages, including embedded NUL characters, are returned unchanged; empty input returns an empty string. Null input and invalid UTF-16 are rejected. The binding copies the module-owned UTF-8 buffer from `vs_interop_locale_translate` into a managed string and releases it with `vs_interop_buffer_destroy` on success or failure.

## Retained callable disposal

<a id="callable-disposal"></a>

`VeroScriptCallable.Dispose()` immediately invalidates the managed wrapper. If an invocation is active, native destruction is deferred until its final active invocation returns. Disposal does not wait behind native execution, so a callback may wait for another thread to dispose this wrapper. Invocations on the same wrapper remain serialized. This lifetime rule does not relax owner-thread checks or the requirement to serialize session access externally.

## Annotation services and diagnostic severity

`VeroScriptRuntime.CompileCapabilities` reports compile-time services independently of execution and JIT capabilities. `AnnotationProcessing` enables `SetAnnotationProcessingService(nint)` with an opaque, valid service handle supplied by a native extension. Attach or clear the service on the creator thread while the session is idle; `0` clears it. The session copies the configuration, so the caller may release its handle after attachment. `ApplyConfiguration` preserves the service. Keep processor code and callback data valid through their last use. Older same-ABI runtimes report `None` and reject this operation with `NotSupportedException`.

`GetDiagnostics()` preserves `Severity`: `Error`, `Warning`, `Information`, or `Hint`. A nonzero diagnostic count does not imply compilation failure: processor warnings, information and hints can accompany `Success == true`. Older runtimes without the severity query remain readable and report `Error`.

## Host console callbacks

`VeroScriptSessionOptions.ConsoleQuery` reports `VeroScriptConsoleInfo`: output terminal, color/cursor/clear support and visible column/row counts. Nonpositive dimensions mean unknown. Controls require both a query and `ConsoleControl`; omitting either leaves controls unsupported. `ConsoleControl` receives `VeroScriptConsoleOperation` and two integers: ANSI foreground/background colors `0..15` or default `-1`; clear-line mode after/before/all `0/1/2`; zero based column/row; cursor visibility `0/1` in the first argument. Unused arguments are zero.

`ConsoleWrite` receives exact `std::console::write` text, including embedded NUL, without an added newline. Omitting it retains buffered `Print` output. Return `true` only after successful synchronous completion. Query exceptions report no capabilities; control exceptions return failure; `false` or an exception from the write callback fails script execution.

Callbacks remain rooted through session destruction, may run concurrently on VM workers and must synchronize shared state. The runtime may reset style and show the cursor at the execution boundary, including after failure or cancellation. Do not dispose the session or reenter ordinary session operations from these callbacks. Terminal input is independent of output capabilities. New callbacks require a matching native runtime; older same-ABI runtimes preserve line output but ignore the trailing console callbacks.

Scripts loaded through the C# SDK can use primitive aliases in executable reflection and callable signatures, including nested value containers and reference storage. For example, `array<long>` can be requested as `array<int64>`. This does not relax signedness, integer width, object identity or reference constness. Source and serialized package execution use the same Core checks.

Host protocol callbacks (`Clone`, `Equals`, `Hash`, subclass factories) remain rooted until their last native stamped value is released, including after session disposal and on another thread. Callbacks must not access the disposed session. Supplying managed protocol callbacks requires the optional native callback-retention export; older same-ABI runtimes raise `NotSupportedException` before registration. If retention fails after protocol registration, the session is closed to prevent invalid callback data. Generic factory rollback releases transferred or managed roots once.

## General compilation services and module environments

`SetCompileProcessingService(nint)` attaches a native SDK compilation pipeline and requires `CompileCapabilities.GeneralProcessing`; `0` clears it. Call on an idle session's creator thread with a valid opaque handle. The session copies the configuration, so the caller may then release its handle. Ordinary `ApplyConfiguration` preserves the service. Processor code and callback data must survive their final use.

`ApplyConfiguration(configuration, moduleEnvironment)` explicitly selects an immutable native SDK snapshot and requires `Capabilities.ModuleEnvironment`. `0` removes it; the one-argument overload preserves the selection. Call on an idle session's creator thread. After success the session retains the snapshot and the caller may release its handle. Call `Clear()` before changing snapshots. Environment-owned catalogs prohibit local registration and catalog imports; prepare a combined native snapshot instead. Conflicting local bindings are rejected before configuration or cancellation state changes. Keep native provider, binding and processor modules loaded through their final use. Unsupported older runtimes throw `NotSupportedException`.

## Session backend selection arguments

`VeroScriptSession.SetVmBackend` / `SetJitBackend`

These Session setters accept a compatible file/directory or an installed backend ID when the supplied module environment provides its resolver. `auto` requests environment candidates; for VM it may use the retained product fallback. `none` or an empty native string disables selection; `null` / `""` map to that empty request. On a new session, omitting a backend setting allows environment selection; it differs from explicitly clearing that setting. When reconfiguring an existing session, omitting the VM selection preserves its execution domain. A failed explicit selection preserves the previous backend. Set on the idle creator thread; VM changes must precede execution because the domain remains frozen after Clear/reload. JIT selection remains independent of VM/Mixed/JIT execution policy.

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

`VeroScriptRuntime.CompileCapabilities` uses `VeroScriptCompileCapabilities`, separately from execution capabilities. Older same-ABI runtimes without the query report `VeroScriptCompileCapabilities.None`. `GetJitBackendInfo().CapabilityFlags` uses `VeroScriptJitBackendCapabilities`, including `DirectScalarCalls`; these flags describe backend support rather than proving a native entry executed. `VeroScriptDiagnostic.Severity` classifies each structured diagnostic.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
