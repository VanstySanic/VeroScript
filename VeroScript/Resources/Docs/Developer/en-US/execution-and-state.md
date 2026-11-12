# Core execution, state, and packages

[Replaceable VM backends](vm-backends.md)

## VM state

See [Replaceable VM backends](vm-backends.md#vm-provider-assembly).

## Bytecode metadata and control flow

Bytecode carries the metadata required to preserve source and package behavior:
reference-parameter flags, closure capture names and types, declaration-only or
abstract function state, interface and enum tables, debug-file ownership,
exception regions, `finally` ranges, global initializer ranges, and package
component ownership. Reference opcodes, checked and optional casts, `throw`,
and protected-region cleanup are semantic operations, not interchangeable VM
optimizations. Serialization, hot reload, reflection, and tooling must keep
the associated metadata aligned with the executable tables.

## Workers and synchronization

Script globals and class static fields are isolated per native worker. A worker
task registry and its task IDs are worker-local. `channel<T>` is the explicit
synchronized cross-worker capability; host callbacks and reachable native state
require host-provided synchronization. A worker cannot use a task ID from another
worker as if it were local.

## Packages and reload

Package loading validates module metadata and application/library entry shape.
Session reload is opt-in and stages a replacement before commit. File-backed,
compiled-module, and package reloads have distinct inputs. A failed reload must
retain the previous working module and expose a structured diagnostic.

## Debug and diagnostics

Debug locations, stack frames, variables, breakpoints, evaluation, and stop
reasons are public data contracts consumed by Tooling and DAP. Diagnostic IDs,
source ranges, and substitution arguments are stable machine data; localized
messages are presentation.
## Execution governance

`FExecutionLimits` contains an instruction limit and a wall-clock timeout; zero
means unlimited for that field. `FExecutionControl` is a thread-safe shared
control object. A host can call `RequestCancel` while `Run` is active, and the
VM observes the request at cooperative execution points. Cancellation never
forcefully stops a native callback or a managed callback that is already in
progress, so host callback state still requires its normal synchronization.

`FScriptCapabilities` is the standard-library side-effect policy. The policy
can restrict file, environment, console, and task operations. The default
configuration is permissive; when a host supplies restrictions, the execution
path must enforce them before any JIT entry can run. This is a security policy,
not merely an optimization hint. `FEngineConfig` carries these limits, control,
and capabilities together with source overlays and the requested
`EExecutionBackend`.

Execution limits, active cancellation control, and restricted capabilities use
the interpreter path when necessary for enforcement. Debugger attachment, retained initialization ranges and bounded cleanup use `Vm`; the requested
backend remains a configuration value and must not be reported as the effective
backend in diagnostics.

## Reentrant cleanup and exception transfers

Value release can invoke script destructors that reenter execution and relocate call-frame storage. Keep frame indices across these calls and reacquire frame references afterward. Bytecode operand reads must remain above the current frame’s `StackBase`; cross-frame unwind cleanup releases saved operands explicitly.

A pending return or exception belongs to its enclosing `finally` continuation. Loops and locally caught throws inside `finally` preserve that transfer; a transfer leaving the region releases superseded values through the normal ownership path. An uncaught cleanup exception can replace a script exception being propagated, while an exception caught inside a destructor must preserve the outer exception. Execution failure and VM reuse must release pending exception objects. Verify these cases in `VeroScriptExceptionTests`, including debugger and instruction-limit modes; governed execution must still count and check `throw` independently.

## Numeric services

`VeroScriptNumerics.h` supplies immutable `FBigInteger`/`FRational` values and transactional, noexcept `Numerics::Try*` operations. `FNumericError` uses fixed metadata; allocation failures do not allocate error strings. Copy/move/swap/destruction do not allocate and moved-from values are zero. Immutable values may be shared across host threads; each operation needs its own context and synchronized work callback.

`FNumericOperationContext` combines `FNumericLimits` with a synchronous borrowed work callback. A zero limit selects a finite default: 65,536 value bits (hard maximum 1,048,576), 1 MiB input/output, 16 MiB scratch and 16,777,216 work units per operation. Scratch measures requested native working storage, including pending outputs; it is not the VM heap or process RSS. Rejected operations preserve outputs.

Set limits before execution through `VeroScriptVm::SetNumericLimits`, `FEngineConfig::NumericLimits` or `VeroScriptSession::SetNumericLimits`. Workers inherit limits. Numeric work participates in the active instruction budget, timeout and cancellation, including global initialization. `VeroScriptNumericBindings.h` converts verified Core values to/from FValue and supplies the native service interface. C Interop exposes `VS_INTEROP_CAP_NUMERICS`, `vs_interop_numeric_parse/to_text/convert`, verified kind queries and a session limit setter; strings retain their explicit length. Direct language-specific BigInteger/Fraction mapping is not automatic. Standalone AOT rejects numeric runtime operations.

Package hooks use the same services with `Packages::FLimits::HookNumericLimits` before initialization. The package format, registry protocol and JSON number rules do not change.

```cpp
#include "VeroScriptNumerics.h"

VeroScript::FNumericOperationContext context;
VeroScript::FNumericError error;
VeroScript::FBigInteger value;
bool parsed = VeroScript::Numerics::TryParse("18446744073709551616", value, context, error);
```

## variant / FValueRoot

See [Interop SDK — variant](../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#exported-value-lifetime).

`FVariantTypeDescriptor` is validated by the VM before boxing. `Variant::Box` and `Variant::Snapshot` reuse normal cloning. Boxes do not expose mutable payload storage. `VisitValueGraph` includes boxes, objects, closures and containers; retirement never invokes script on a finalizer thread. Reserve result slots before executing callbacks or cloning payloads, then commit without allocation. The outer activity drains retired values after execution. A session close requested inside a callback finishes teardown after the active call.

VM supports the full semantics. Mixed/asmjit safely fall back to VM for unsupported variant instructions. Portable bytecode and packages preserve declarations and instructions. Standalone C++, C# and Python AOT reject variant dependencies explicitly before generating output. The debugger exposes read-only $present, $type and $value children; type<variant>().kind() returns "variant".
Call safe points protect borrowed arguments and receivers during execution. At exit they keep return values, constructed objects and writable argument results alive until the caller can retain them; input-only borrows end with the call. A failure while draining retired values turns an otherwise successful call into a runtime failure; an existing execution error remains primary. Closing reports cleanup failures through the host diagnostic stream.

## Native asynchronous callback roots

An active native call can retain functions and captures through `IScriptCallInvoker::RetainForGeneration`. Store its `FValueRoot`, never the borrowed invoker, VM or Args. Retirement may occur on another thread; access and language cleanup remain on the owner thread. Ordinary exports and completion roots protect the same live graph.

Native safe points drain completion roots; `DrainGeneration` preserves ordinary host borrow roots. Destructor work counts against the active execution budget, cancellation and absolute deadline. Persistent Sessions retain roots for their generation. Bare VM top-level return, replacement, reload or close invalidates them. Ordinary exports still block replacement. A compilation failure preserves the old generation; invalidation after migration starts is not undone by rollback.

## Preparation caches and JIT execution services

`VeroScriptProgramPreparation::TryGetCacheInputFingerprint` admits an early cache lookup only when the parsed input needs no processor execution, package replay or host-binding validation. Combine this fingerprint with the loader's complete source and resolution fingerprints and the compiler build identity. A declined lookup uses normal preparation and prepared-program dependency validation. Source metadata and effective constant-evaluation limits participate in this identity.

`ICompiledFunctionServices::ExecuteBlock` amortizes the native-to-Core boundary for a bounded instruction sequence. Every instruction still performs its normal budget, timeout, cancellation and permission checks; a branch, return or failure ends the sequence. This does not enable unchecked governed execution or change the default governed Mixed policy.

Borrowed, non-retained and isolated worker JIT caches prepare the entry first; reached callees remain eligible for compilation. Retained caches reuse a completed preparation only for the same execution requirements. Unsupported or unprofitable paths continue through VM.

These C++ service changes require rebuilding Core, backends and SDK consumers from matching sources.

Whole-module entry still follows the configured eager compilation budget.

`FJitCompiledFunction::bPreferVmForExecution` lets a backend publish an input-independent VM preference. Core applies cached preferences before activating backend services; fallback statistics, normal VM checks and independent callee dispatch remain available.

## Bounded cleanup after interruption

See [C++ embedding — Bounded cleanup after interruption](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md#bounded-interruption-cleanup).

## Type compatibility memo ownership

`VeroScriptVm::IsActiveObjectTypeAssignableTo` caches exact canonical class/interface compatibility by module and `ProgramGeneration`. The memo is owned by each execution context and clears on module/generation change or when its 4096-entry bound is reached. Owned immutable programs permit reuse outside active calls. Borrowed programs cache only during execution and clear between calls in `BeginExecutionGovernance`; aliases and noncanonical names retain authoritative metadata resolution. Preserve these ownership and invalidation conditions when optimizing JIT calls, value copies or host queries.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](execution-and-state.md) · [fr-FR](../fr-FR/execution-and-state.md) · [ja-JP](../ja-JP/execution-and-state.md) · [ko-KR](../ko-KR/execution-and-state.md) · [zh-CN](../zh-CN/execution-and-state.md) · [zh-TW](../zh-TW/execution-and-state.md)
