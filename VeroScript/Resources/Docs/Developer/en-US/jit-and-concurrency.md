# Core JIT services and concurrency

[Replaceable VM backends](vm-backends.md)

<a id="native-abi-version"></a>

`VEROSCRIPT_VERSION_CODE` encodes the product version (`VEROSCRIPT_VERSION`) as major:8/minor:8/patch:16 bits. The JIT module handshake and JIT/AOT `CoreVersion` fields use this same value; the C AOT descriptor calls it `core_version`. There is no separate JIT contract or API version counter. Binary compatibility still requires public-header fingerprints, descriptor layouts, toolchain and platform checks; AOT also verifies Core implementation identity. Rebuild static C++ consumers and backend binaries with matching headers and libraries. Package release versions remain independent metadata.

## Ownership and selection

See [Replaceable VM backends](vm-backends.md#vm-provider-assembly).

[The optional AsmJit module](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/en-US/index.md)
owns IR, lowering, native emission, portable Linear execution, ABI adapters,
direct targets and execution scratch. Its private headers and architecture
macros are not part of the Core SDK. Core reports `backend-services`; query
the selected factory description for actual target and native capabilities.

## Compilation and invocation

`EJitCompilationResult` distinguishes `Ready`, `Unsupported`, `Deferred` and
`Failed`. Unsupported code and unchanged resource deferrals can use the VM;
compilation failure is an error. Publication follows ownership, service and
resource validation. Program identity includes module and native-binding generations.

`EJitInvocationResult` distinguishes `NotEntered`, `Returned`,
`ControlTransfer` and `Failed`. Only `NotEntered` permits interpreter replay.
Once entered, failures, throw, exit and cancellation propagate without repeating
side effects. Returned values are checked by Core. Executables retain their code
and module owner; invocation contexts, frame tokens and data views are borrowed.
After a nested call or host reentry, reacquire borrowed object/container storage.

The public service headers are `VeroScriptJitRuntimeServices.h`,
`VeroScriptJitFrameServices.h`, `VeroScriptJitValueServices.h`,
`VeroScriptJitCallServices.h` and `VeroScriptJitScalarServices.h`.
They preserve interpreter semantics for copying, references, const fields,
method dispatch, mutation notifications, errors and logical call depth.

An owner supplied through `SetJitProgramOwner` keeps an immutable module generation
alive: do not change its contents through another mutable alias. Replace modules
at an idle boundary. Bare `VeroScriptVm` execution does not automatically advance
the configured program generation or owner. If values survive a program change,
the host must update that identity and keep the defining module alive for cleanup
or supported migration. Session replacement and reload manage this identity.

## Policy, statistics and Session reuse

`FJitPolicy` controls hotness, compile attempts, cached decisions and eager
compilation. Zero policy budgets mean unlimited capacity. `Vm` bypasses the
backend; `Mixed` waits for hotness; `Jit` prefers compilation subject to policy.
Execution governance and backend capabilities select a validated controlled JIT
path or the VM. Debugging and unsupported paths retain the VM.

`bReuseSessionCode` defaults to true. Eligible `Run`, `CallFunction`, `CallMethod`
and retained-callable invocations lease the same generation cache and cumulative
compile quota, including unlimited policy budgets. Reuse requires an injected
factory and a non-VM mode; persistent execution and stable callbacks may reuse code. Native catalogs publish runtime revisions through `IHostBindingCatalog::GetRevision()`. Policy/configuration changes,
Reload and Clear invalidate retained state. Failed reload preserves the prior program.

`FSessionJitCacheLimits` defaults to 8 MiB executable and 32 MiB retained payload;
zero allows no retention for that category. Scratch is included in retained
payload. Resource refusal falls back without repaying consumed compilation quota.
`FSessionJitCacheStats` reports settled generation accounting. `FJitStats` is the
last-execution snapshot; backend-specific path counters describe actual execution.
`NativeTypedLoopExecutions`, `DirectNativeCalls` and `CostModelVmFallbacks`
remain distinct. A cost-model fallback is not an execution failure.

## Concurrency

Core owns task creation, joins, timeouts, cooperative cancellation, channels,
mutexes, semaphores and synchronized values. Blocked channels observe closure.
Workers share immutable factories and the owned program, while instances,
statistics, scratch and errors remain local. Task completion releases language roots.

## Validation

Changes require contract, backend differential, lifetime, debug and concurrency
regressions. Build the module enabled, disabled and enabled without native
emission. A separate source tree without the module must build Core and C++
consumers. Preserve existing performance gates and test explicit SDK injection.

<a id="construction-dispatch"></a>

During construction or destruction, virtual targets must be resolved through Core's current lifetime phase. `bConstructionIncomplete` and `bDestroying` require this check; `GetTypeName` and `RuntimeTypeIndex` still describe the actual object and field layout. An optimization must decline before side effects when it cannot preserve phase dispatch, and must not reuse an ordinary actual-type virtual cache. Explicitly bound non-virtual calls and Core-selected native constructor entries keep their existing paths.

## Execution diagnostics

`PlanReason` / `PlannedKind` describe the current cached plan when the snapshot is queried. `Fallbacks` retains historical rejection events from that execution, including warm-up decisions before successful compilation.

Detailed collection uses the official VM's instrumented dispatch, which can reduce instruction fusion and expose additional Core call boundaries. Its timings and boundary counts are not a measurement of the disabled path. Aggregate actual-entry counters remain available without detailed collection.

`NativeLeafEntries` / `NativeCfgEntries` count compiled artifacts. `NativeLeafExecutions` / `NativeCfgExecutions` count actual executable entries, including entries ending with errors. Controlled entries, typed loops and nested direct calls retain their separate counters. `ScalarNativeFastPathCalls` only counts the host scalar-native fast path. Linear, Bridge and VM trampoline entries have separate counters. These events overlap and must not be summed as independent script calls.

Configure `FJitPolicy::Diagnostics` before execution: set `MaxFunctions` (for example 128), `MaxDepth` (256) and optionally `bMeasureTime`. After execution, query `VeroScriptVm::GetJitDiagnostics()` or `VeroScriptSession::GetLastJitDiagnostics()`. Records contain generation-scoped function identity, module, actual entry kinds, outcomes, fallback histograms and optional inclusive/self nanoseconds. Only Core call boundaries are recorded; inlined operations and direct native recursion without a Core entry have no separate records. Inclusive times overlap; self time subtracts recorded children only. Outermost execution resets the snapshot; reentry and cleanup belong to that execution. `ResetJitStats()` also clears it.

The default `MaxFunctions=0` performs no record allocation, clock reads or logging. Collection is bounded to 4096 functions, depth 32768 and 4096 bytes per name/reason. `DroppedRecords` marks partial coverage. Disable timing for count-only collection. Benchmark with diagnostics disabled and run a separate diagnostic pass.

`GetFunctionReason()` / `GetFunctionFallbackReason()` expose compilation decisions and the latest runtime rejection, including successful VM plans. Mandatory exception, ref and ownership limitations are distinct from cost-model selection; unknown runtime restrictions use `RuntimeGuard`. `CostModelVmFallbacks` excludes mandatory finally/ref fallback. Reasonable VM fallback remains part of normal execution policy.

Finally keeps Core unwinding and bounded cleanup. Getter order/count and explicit binding state are unchanged. A bounded scalar expression may update one plain writable reference capture without callbacks or governance; aliased/multiple captures, readonly wrappers, mutation origins and failed probes retain canonical execution. Reused frame lookup caches are guarded by function, module and generation.

Aggregate counters are mapped through existing C Interop, C++, C#, Python APIs. Detailed snapshots are available in Core/C++. Rebuild matching Core, VM, JIT, Interop and native consumers after these public layout/service changes.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](jit-and-concurrency.md) · [fr-FR](../fr-FR/jit-and-concurrency.md) · [ja-JP](../ja-JP/jit-and-concurrency.md) · [ko-KR](../ko-KR/jit-and-concurrency.md) · [zh-CN](../zh-CN/jit-and-concurrency.md) · [zh-TW](../zh-TW/jit-and-concurrency.md)
