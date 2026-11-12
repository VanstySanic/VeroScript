# C++ embedding and session

## Stateless engine

`VeroScriptEngine` exposes `CheckSource`, `CheckFile`, `RunSource`, and `RunFile`
for callers that do not need persistent state. File operations may use the
bytecode cache directory. Source labels, input streams, output streams, error
streams, circular-import policy, and project-wide-access policy are explicit
parameters. The result contains success/failure information and structured
diagnostics; an error line written to a stream is not a substitute for the
result object.

## Stateful session

`VeroScriptSession` owns the loaded module, host registrations, input/output
bridges, runtime configuration, and optional hot-reload state. The normal flow
is:

1. Construct a session with an `IEngineHost`.
2. Apply configuration and register native functions or host types.
3. Load source, a file, or a validated package, or compile and commit a module.
4. Run, call a function/method, inspect values, or enumerate metadata.
5. Clear or reload only when the operation's state preconditions permit it.

Configuration, loading, compilation, committing, reloading, clearing, and
binding mutations are rejected while a run is in progress. `LoadFile` records
the file-backed reload path. `LoadSource` does not create one. Reload operations
validate the replacement and package kind before committing it.

## Evaluation and values

`Run(ERunMode::Eval)` executes module initialization without calling `main`.
Cross-call globals persist only with `SetHotReloadEnabled(true)`. Function and method
calls can return `FValue`, construct objects, access fields/globals, and retain
callables. A retained callable is released explicitly and can be configured for
owner-thread-only invocation. External serializers operate on the registered
runtime type and encoded version; serialization, deserialization, and reference
enumeration are separate operations.

## Host bindings and metadata

Registration covers native functions, host types, generic host-type templates,
members, operators, clone callbacks, inheritable types, scoped enums, and
subclass factories. Registration errors are reported through stable diagnostic
IDs. Reflection enumerates loaded types, fields, methods, functions, globals,
enums, and interfaces and is read-only metadata access.

## JIT and clearing

<a id="session-jit-cache"></a>

Reuse requires a selected factory, a non-VM effective policy and `FJitPolicy::bReuseSessionCode`. Eligible `Run`, `CallFunction`, `CallMethod` and retained-callable invocations share a generation cache and compile quota. Zero `MaxCompileAttempts` or `MaxCachedEntries` means unlimited, not disabled. Persistent sessions and stable callbacks may reuse code; native catalogs must publish a runtime revision through `IHostBindingCatalog::GetRevision()`. Governed native execution can reuse code when the backend supplies all required governance capabilities; the Session preserves that capability context across VMs. Configuration, policy, backend, module or binding changes invalidate retained code; failed reload preserves the previous program. Defaults are 8 MiB executable and 32 MiB retained payload. `GetJitCacheStats` reports settled generation accounting; `GetLastJitStats` describes the most recent invocation, not process-wide memory.

## Execution governance

`SetExecutionLimits` accepts instruction and wall-clock limits; zero means
unlimited for the corresponding field. `SetExecutionControl` installs a shared
thread-safe cancellation object, and `RequestCancel` may be called while
`Run` is active. Cancellation is cooperative and never forcefully terminates a
native callback or makes unsynchronized host state safe.

`SetCapabilities` controls standard-library side effects such as file, environment, console and task operations. Core enforces those permissions on interpreted and supported native paths. Set `bAllowProcessDirectoryChange` to false to deny `chdir` even when environment writes are allowed; `chdir` requires both permissions. `SetExecutionBackend` selects `Vm`, `Mixed` or `Jit`. Governed `Mixed` uses the VM by default; `SetJitPolicy` with `FJitPolicy::bEnableControlledMixed=true` opts into dynamic native execution. Explicit `Jit` and static Core AOT selected in `Mixed` are independent of that switch. Missing governance capabilities select the VM before entry; debugging, retained initialization ranges and bounded cleanup use the VM.

## Calls, interception, and persistence

The public call surface includes `HasFreeFunction`, `HasInstanceMethod`,
`CallFunction`, `CallMethod`, `CallMethodBypassingInterceptor`,
`ConstructObject`, `RetainCallable`, `InvokeCallable`, field/global access,
`ApplyClassDefaults`, and reflection enumeration. The bypass method skips only
the selected entry interceptor. A method body can still make nested calls that
are intercepted normally. Retained callable ownership, owner-thread mode, weak
`this` capture, and module-generation invalidation remain explicit lifetime
rules.

`RegisterExternalSerializer`, `SerializeObject`, `DeserializeObject`, and
`EnumerateObjectReferences` use the Interop serialization and borrowed-reference
contracts. `Reload`, `ReloadCompiledModule`, and `ReloadPackage` stage and
validate replacements transactionally; a failed initialization retains the last
working module and reports a structured diagnostic.

## Retained byte quotas

Zero `MaxNativeBytes` or `MaxRetainedPayloadBytes` permits no retained bytes of that category; these byte limits differ from the unlimited zero policy budgets.

## Cancellation and host delivery

<a id="session-control-and-streams"></a>

`RequestCancel()` atomically snapshots the configured control, including while
the owner replaces it at an idle point. Null is a no-op; cancellation is not transferred to a different control. `GetExecutionControl()` is an owner-thread borrowed reference. Await
cancellation calls and execution before Session destruction. Host line views are
borrowed only during the callback. A throwing sink sets stream failure without
replaying the attempted event on reuse or destruction. `ResetForReuse` restores
stream state; destruction suppresses callback exceptions.

Sessions sharing the same `FExecutionControl` token share its cancellation
state. Complete prior cancellation callers before `Reset()` and reuse, so an
old request cannot cancel the next execution that reuses the token.

## Implementation ownership

<a id="session-debug-implementation"></a>

`VeroScriptSessionDebug.h` defines the Session debug bridge and borrowed views; their implementation currently lives in `VeroScriptSession.cpp`. `VeroScriptLiveDebugDomain.h` defines the host-owned live domain contract, consumed by `VeroScriptDapAdapter.cpp`. The matching `.cpp` entry files currently contain only header includes. Do not infer implementation ownership from their names. Preserve the controlling-thread and lifetime contracts above.

## Optional JIT diagnostics

`FJitPolicy::Diagnostics.MaxFunctions` defaults to zero, disabling per-function collection. To enable it, update a copy of the existing policy with the record/depth bounds and optional `bMeasureTime`, then call `SetJitPolicy`.

`GetLastJitDiagnostics()` returns an owning snapshot of the latest execution, retained after its temporary VM is destroyed. Copy it to retain it across later calls. `Run`, debug execution, function/method calls, construction and retained-callable invocation capture the same snapshot. The next execution with collection disabled, or `Clear()`, clears it. Inspect `DroppedRecords` when the bound is reached. Time fields stay zero when timing is disabled.

`GetLastJitStats()` reports actual executable entries through `NativeLeafExecutions`, `NativeCfgExecutions`, `BridgeExecutions`, `LinearExecutions` and `VmTrampolineExecutions`. Compilation counts are not execution counts. Scalar/direct-call and typed-loop counts can be nested events within one function invocation; do not add them to function-entry counts. Inclusive diagnostic times must not be summed across functions either.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
