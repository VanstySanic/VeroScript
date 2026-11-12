# C# runtime and Interop adapter

## Typed-loop statistic

`NativeTypedLoopExecutions` counts native typed-loop kernel invocations, including an invocation that ends after reporting a source-visible runtime failure. It is an execution-observability counter, not a success counter.

`VeroScriptCSharp` is a managed wrapper over `VeroScriptInterop`. The wrapper
owns ergonomic conversions, leases, callback roots, and managed exceptions; the
C ABI owns native layout, status values, and lifetime rules.

## Compatibility gate

`VeroScriptRuntime` exposes ABI version, semantic runtime version, host platform,
capability bits, current language, available-language JSON, and catalog
load/merge operations. `EnsureCompatible()` is the managed entry gate. ABI
version and product semantic version are distinct values and must never be
combined in a displayed version string.

`EnsureCompatible()` compares the complete ABI value with `SupportedAbiVersion`, currently `0x00000101` (0.1.1). Any mismatch throws `NotSupportedException`; a matching major version or a numerically newer ABI is insufficient. Keep `NativeMethods.AbiVersion`, the native header, public API contract and package documentation aligned when the ABI changes.

`LoadCatalogDirectory` replaces the runtime catalog tree after validating its
`en-US` baseline. `MergeCatalogDirectory` adds a validated tree to the current
catalogs and can explicitly allow existing IDs to be overridden. A managed host
must choose replacement versus augmentation deliberately; it must not emulate
catalog merging by copying JSON files into an output directory or by hardcoding
a culture list.

## Session and handles

`VeroScriptSession` owns the native session handle and registered managed roots.
Session operations must follow the underlying idle/running and creator-thread
rules. `VeroScriptValue` can own a native value or expose a borrowed view inside
a `BorrowScope`; borrowed values cannot escape the scope. `VeroScriptPackage`
owns a package handle, exposes package metadata and canonical signed bytes, and
tracks whether the compiled module is still available for loading.

`VeroScriptCallable` records an owner thread when requested. Disposal while an
invocation is active is deferred until the native call releases the handle.
This protects lifetime only; it does not make cross-thread invocation legal.

## Marshaling

The adapter explicitly marshals fixed-width integers, floating values, booleans,
UTF-8 strings, wide strings, enums, host objects, buffers, arrays, maps, sets,
queues, stacks, deques, and channels. UTF-8/UTF-16 helpers and native buffer
leases must preserve ownership and must copy a borrowed view before its owner is
released.

Managed exceptions are captured at native callback boundaries and converted to
the ABI error channel. They must not cross a C callback frame unmanaged.
## VM execution domains

See [C# sessions and runtime — Independent VM selection](../../User/en-US/session.md#vm-execution-domain).

## Configuration, cancellation, and JIT

`VeroScriptConfiguration` is applied before loading or running a session. Its
execution fields are `MaxCallDepth`, `MaxInstructions`,
`TimeoutMilliseconds`, `EnableExecutionControl`,
`CapabilityPolicyEnabled`, `AllowedScriptCapabilities`, and
`ExecutionBackend`, in addition to source overlays and import policy. Zero
instruction and timeout values leave that limit unlimited. When capability
policy is enabled, the allowed flags govern standard-library file, environment,
console, and task side effects; the VM must enforce a restriction before any
JIT entry can bypass it.

`RequestCancellation` marks the native execution control object and is safe to call
from a non-owner thread. It is cooperative and does not stop a callback already
executing in native or managed code. `SetJitPolicy` applies adaptive compilation
settings and invalidates reusable compiled state. `GetLastJitStats` returns the most
recent run-scoped delta. Eligible ordinary `CallFunction` invocations reuse compiled
code across fresh VM instances by default; script globals and objects do not persist
between calls. Set `VeroScriptJitPolicy.ReuseSessionCode` to false to opt out. Reuse
requires a non-VM effective backend and a revisioned native binding catalog. Changes
to bindings, observers, or the program generation invalidate retained code. Governed
execution can reuse a context with matching governance requirements. Zero
compile-attempt or cached-entry budgets mean unlimited counts; retained native-code
and complete-payload byte ceilings still apply.

Governed `Mixed` execution defaults to the VM. Enable
`VeroScriptJitPolicy.EnableControlledMixed` only after measuring the target
workload. Explicit `Jit` selection is independent of this switch. Unsupported or
VM-preferred entries keep the governed VM fallback. The backend's
`ControlledExecution` capability reports support, while `ControlledNativeExecutions`
and `AotExecutions` report actual controlled native and AOT entry invocations.

`SetJitCacheLimits` and `GetJitCacheLimits` control the native-code and complete
retained-payload ceilings. The defaults are 8 MiB and 32 MiB; zero permits no
retention of that resource. `GetJitCacheStats` returns cumulative compile attempts,
settled retained bytes, and the resource-limit latch for the current cache context. It
is not a live or process peak-memory counter. These optional methods require
`VeroScriptCapabilities.SessionJitCache` and throw `NotSupportedException` when a
separately deployed compatible runtime lacks it. Retained initialization uses the VM.
Eligible calls in the same generation can reuse compiled code. `GetReloadStatus()`
reads the native operation and generation snapshot on the creator thread when
`VeroScriptCapabilities.ReloadStatus` is available.

## Session operations

The managed session supports `LoadSource`, `LoadFile`, `LoadPackage`,
`Reload`, `ReloadPackage`, `BuildPackageFile`, `Run`, diagnostics, input,
function and method existence checks, `CallFunction`, `CallMethod`,
`CallMethodBypassingInterceptor`, `Construct`, field/global access, and
`ApplyClassDefaults`. The bypass form skips only the entry interceptor; nested
calls made by the invoked body remain interceptable.

Host integration includes function, annotation, host type/member, protocol,
type-alias, generic-host-type, instance-interceptor, mutation-handler, and
external-serializer registration. Reflection and binding JSON are inspection
outputs. `VeroScriptValue` also exposes sequence/map/set/channel operations, and
`VeroScriptCallable` retains and invokes a function under its configured
owner-thread and weak-`this` lifetime policy.

## Diagnostics and managed lifetime

`GetDiagnostics`, `GetDiagnosticArguments`, and `FormatDiagnostic` preserve the
structured message ID, file label, source coordinates, and substitution
arguments. `VeroScriptRunResult` reports execution success, exit state, exit
code, and diagnostic count; callers must not replace the result with a printed
message. Borrowed values expire with their `BorrowScope`, callback exceptions
are captured and rethrown after native enumeration, and native buffers are
copied before their lease is released.
## JIT statistics mapping

The managed VeroScriptJitStats snapshot preserves the separation between scalar
fast paths, trampoline entry kinds, and direct nested native calls.
DirectNativeCalls is not a compilation count and must not be combined with
ScalarNativeFastPathCalls. These values describe the last execution snapshot;
they do not change the requested backend or establish a new ABI/version
contract. Unsupported or guarded entries continue to fall back to the VM.
`CostModelVmFallbacks` is the subset of `Fallbacks` selected because a valid
compiled path is predicted to cost more than the VM. `NativeTypedLoopExecutions`
counts kernel invocations, including those ending after a source-visible runtime
failure; it is not a success count. Check
`VeroScriptJitBackendCapabilities.NativeCode` and `VeroScriptJitBackendCapabilities.NativeTypedLoops`, respectively; neither
capability may be inferred from `HostPlatform`.
`VeroScriptJitCacheStats` has a different lifetime: it describes the cumulative,
settled reusable context and is reset when policy, limits, backend, module, or
relevant host-observability state invalidates that context.

See [C# sessions and runtime — Explicit JIT selection](../../User/en-US/session.md#jit-backend-capabilities).

See [C# sessions and runtime — Explicit JIT selection](../../User/en-US/session.md#diagnostic-translation).

## Retained callable disposal

See [C# sessions and runtime — Retained callable disposal](../../User/en-US/session.md#callable-disposal).

<a id="managed-marshalling-maintenance"></a>

Native calls keep managed value and package owners alive until execution and native-view copying finish. Blocking channel operations and map/set visitors retain their receiver, and argument or element lists retain every value wrapper produced by their indexer until native use finishes. This prevents premature GC finalization; explicit disposal and callback-scope rules still apply. Regression checks force GC during native receives, visitors and on-demand list marshalling, using optimized .NET 8/9 execution and NativeAOT package consumers.

Native argument and element marshalling retains a snapshot of the exact value wrappers that supplied the handles. If a list indexer or managed callback replaces an input array slot, the original wrapper stays alive until native use finishes, and `ref`/`out` writeback updates that original wrapper. Regression tests force GC after replacement during map construction, direct function calls and retained callable invocation.

[Android and iOS / MAUI](../../User/en-US/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

Mobile-enabled packing uses this same project and package identity: `VeroScriptIncludeMobile=true` includes desktop .NET 8/9 and both .NET 10 Android/iOS targets with the verified native SDK.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](runtime-and-interop.md) · [fr-FR](../fr-FR/runtime-and-interop.md) · [ja-JP](../ja-JP/runtime-and-interop.md) · [ko-KR](../ko-KR/runtime-and-interop.md) · [zh-CN](../zh-CN/runtime-and-interop.md) · [zh-TW](../zh-TW/runtime-and-interop.md)
