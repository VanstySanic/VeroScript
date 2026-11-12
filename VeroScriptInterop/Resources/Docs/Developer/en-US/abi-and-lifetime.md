# Interop ABI and lifetime

`VeroScriptInterop` is a C11-compatible process ABI. The public header defines
opaque handles for sessions, values, callables, packages, buffers, call
contexts, interception, generic host types, and external serialization.

## VM backend selection

`VS_INTEROP_CAP_VM_BACKEND_SELECTION` discovers `vs_interop_session_set_vm_backend` and `vs_interop_session_get_vm_backend_info`. The standard Interop product explicitly assembles the static bytecode VM; custom builds may use `VEROSCRIPT_INTEROP_DEFAULT_VM=OFF` and select a matching external VM. This selection is independent of VM/Mixed/JIT policy. Set it on the idle creator thread before execution; an empty path clears it. A failed change preserves the previous selection. Core freezes the selection when execution is committed, including JIT execution; `vs_interop_session_clear` does not thaw it.

Initialize the output `struct_size`. Failed metadata queries preserve output. `VS_INTEROP_VM_SELECTED` and `VS_INTEROP_VM_FROZEN` describe the domain; `VS_INTEROP_VM_CAP_*` describes backend capabilities. Text views last until replacement or session destruction; a static selection has an empty `library_path`. ApplyConfig preserves the selection, and script compilation extensions inherit the session VM.

## Version and status

ABI version, runtime semantic version, capability bits, execution backend, run
mode, package kind, value kind, and status codes are distinct fields. The ABI
query functions report them separately. A binding must not display or compare an
ABI value as if it were the product version.

The current `VS_INTEROP_ABI_VERSION` is `0x00000101` (0.1.1), encoded as `(major << 16) | (minor << 8) | patch`: 16 bits for major and 8 bits each for minor and patch. C# and CPython adapters require exact equality; capability bits and append-only fields governed by `struct_size` do not waive that version check. Review ABI changes independently of each business’s package version.

Every operation returns an ABI status where applicable. The last-error buffer is
for operation-level failures; structured diagnostics are read through session
diagnostic APIs and formatted only at the presentation boundary.

## Ownership

Opaque handles are explicit owners or borrowed references depending on the
function. String and byte views are non-owning. Buffers returned by the ABI must
be destroyed by the matching buffer function or copied before their owner is
released. Callback contexts and borrowed enumeration values are valid only for
the documented call.

The C header is compiled and linked from C as well as C++; calling convention,
struct layout, enum values, nullability, and symbol names are therefore part of
the ABI. Additive functions still require synchronized header, implementation,
tests, and adapter updates.

## Host protocol ownership

Host values retain native clone/equality/hash bridge metadata after their source
session closes. The foreign `user_data` lifetime remains the host's responsibility.
After protocol registration, `vs_interop_session_retain_callback_user_data` can
transfer that lifetime to the matching protocol bridges. Ownership transfers
only on `OK`; unmatched data, duplicate transfers, null release functions, and
rejected calls do not invoke the release function. The release function runs
exactly once after the session and its last related host value release their
bridges, potentially on another thread. This additive export preserves existing
C structure layouts and ABI version 0.1.1.

## Session JIT-cache extension

`VS_INTEROP_CAP_SESSION_JIT_CACHE` gates the session cache limit/statistic
exports. The extension appends `reuse_session_code` to
`vs_interop_jit_policy`; callers compiled against the original 32-byte layout
remain valid and receive enabled reuse by default. Current callers send the full
structure and can set the byte to zero. This preserves ABI 0.1.1 while allowing an
explicit opt-out.

Reuse retains compiled code for eligible execution, including persistent
sessions. It requires a compatible backend and a stable binding revision.
Changes to interceptors or mutation callbacks invalidate retained code. The
effective VM backend bypasses this cache.
Governed execution may retain a compatible controlled backend cache, including its
governance identity; repeated calls consume one cumulative compilation quota.
Zero compile-attempt/cached-entry policy budgets are unlimited for those counters;
retained native-code and payload limits still bound the context.
`vs_interop_session_set_jit_cache_limits` invalidates the current context. Its
native-code and complete-payload defaults are 8 MiB and 32 MiB; zero permits no
retained resource of that kind.

Governed Mixed defaults to VM. `enable_controlled_mixed` opts in after measuring
the target workload; explicit JIT selection is independent. The selected backend
advertises `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION`; actual controlled native entry
is reported by `controlled_native_executions`. Unsupported or VM-preferred entries
retain Core's cooperative checks on the VM path.

`vs_interop_session_get_jit_cache_stats` reports cumulative compile attempts,
settled retained bytes, and the resource-limit latch for the current context.
It is distinct from the last-run delta returned by
`vs_interop_session_get_last_jit_stats` and is not a process peak-memory limit.
Policy, limits, backend, module, and relevant host-observability changes may
invalidate the context and reset its statistics.

## New context and channel contracts

Channel operations use the value handle contract and do not turn a value into a
borrowed callback object. Blocking, timed, and non-blocking operations have
distinct symbols and result semantics; `close` is the synchronization point
for waiters. Hosts must not infer a timeout or closed result from an unrelated
`VeroScriptStatus` value.

Generic host-type, call, interception, and external serialization context
pointers are borrowed for the duration of their callback. Their accessors,
mutators, error setters, and continuation/invocation functions are valid only
while that callback is active. No context pointer, argument view, field view,
or byte view may be retained after return. A callback exception or host failure
must be converted to the context error channel before returning.

Callable handles have explicit retain, validity, invoke, and destroy operations.
Owner-thread-only invocation and weak-`this` capture are independent options;
they do not make a session generally thread-safe. External serializer bytes and
reference visitor values must be copied when the host needs them after the
callback. These rules apply equally to the C, C++, and managed adapters.


<a id="localization-abi"></a>

## Localization ABI

`vs_interop_locale_translate` allocates a module-owned buffer using normal locale initialization and process language selection. Unknown bytes, including NUL, are preserved; invalid input clears output. Destroy the buffer on every path. Keep the C header and `.def` exports aligned and verify native validation and six-language substitution. Memory-allocation failure may retain an English error fallback.

## Publication checks

Standalone and complete-release Interop packaging compile and run the C11
header test against the staged SDK, linking every public function. A missing
export or import-library entry rejects the package. The external example accepts
`VEROSCRIPT_SDK_ABI_TEST_SOURCE` to enable this check. Linux shared-library tests
also inject allocation failure into the unloaded-module reflection error path
and require `OUT_OF_MEMORY` without an exception escaping the C ABI; the ordinary
unloaded-module case returns `ENGINE_ERROR`.

The boundary tests also cover cold version-query allocation failure (empty view,
successful retry), serializer replacement failure (previous bytes preserved),
allocation-free multiset counts, and the memory guard's original failure status.
UTF-16 view allocation failure returns `OUT_OF_MEMORY` without changing output
parameters; retry preserves all code units and later reads reuse the cached view.

Console callbacks extend the session-options prefix without changing the C ABI
version. Only complete trailing fields are read; old options remain valid. Query
and control callbacks describe terminal operations, while the optional write
callback receives exact UTF-8 bytes without an added newline. All use the existing
callback lifetime and worker-thread rules.
The extended tests run native, interception, and mutation callbacks from script
destructors during final session teardown. Host callback data must remain alive
until destruction finishes.

Macro regressions use the public C ABI to build, parse, import, and execute a
library with re-exported object/function/variadic macros, both with and without
debug stripping. They check build-time conditional exports, caller expansion,
private-macro isolation, physical import-error locations, and rejection of
`VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` payloads by the current `VSBC` reader.

The installed native annotation SDK test checks frozen-registry validation,
independent service retain/release, ApplyConfig preservation, creator-thread
checks, and configuration reentry from processors and release callbacks. It
generates/invokes a method, verifies all four diagnostic severities and primary
error selection, and confirms that clearing the service disables generation.
The native creator requires the matching Core C++ SDK; the five added C exports
preserve ABI 0.1.1 and existing diagnostic layouts.

The test also exercises service-functor copy and destruction during ApplyConfig:
configuration reentry is rejected, allocation failure returns `OUT_OF_MEMORY`
without losing the installed service, and successful retry clears stale errors.

Regression checks also reject ordinary function calls, execution and source
replacement from those copy/destruction callbacks, preserving the loaded program.
Invalid configuration descriptors are validated before service functors are copied,
so lifecycle callbacks cannot replace the original validation error. Native callback
context invocation remains available for supported nested script execution.

Additional regressions seed the result with old success/exit flags before a
rejected reentry and verify a fresh failed result and session diagnostic, including
calls with NULL result output. Expired callables report failure without changing
the return value. During synchronous session destruction the opaque wrapper stays
allocated until final callbacks finish, with the session marked closed; callback
session API access and repeated destruction are safely rejected. After destroy
returns the handle is invalid. Deferred teardown from an active call requires
callback data to remain alive until that call returns.

Host-protocol data can be transferred with `vs_interop_session_retain_callback_user_data`.
Only success transfers ownership; the release callback runs once after the final
owning registry/value reference, possibly after session teardown on another thread.
Later ordinary and generic protocols sharing that data reuse its owner. Regression
tests cover escaping copied values, foreign-thread final release and allocation
failure followed by safe member-registration retry.

## General and script compilation processing

`VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING` identifies the native service in `VeroScriptInteropCompileProcessing.h`. Create it with `CreateCompileProcessingService` and a frozen `FCompileProcessorRegistry`; retain/release create independent opaque handles. `vs_interop_session_set_compile_processing_service` attaches its configuration; NULL clears it. ApplyConfig preserves the service. Use matching Core/Interop native SDKs and keep processor code/data alive until final release. Setting requires the idle creator thread; processor and service copy/release reentry is rejected.

`VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS` identifies `vs_interop_session_set_script_compile_options`. Supply the complete current `struct_size`. Roots, optional registration entries, configuration and allowed directories are copied. Paths/entry names reject embedded NUL; duplicate configuration keys, invalid pointer/count pairs, reserved bytes and invalid booleans are rejected without replacing accepted options. Bounds are 64 roots and 4096 configuration entries/allowed directories. Empty registration entries select `register_extension`.

NULL restores Core defaults: no configured roots, source imports enabled, 1,000,000 instructions and 2,000 ms. Explicit zero execution limits are unlimited. Disabling source imports still permits configured roots. Normal overlays/import/stdlib paths apply; an extension importing stdlib source needs a matching directory containing `compiler.vs`. Compile extensions run on an isolated VM, inherit session cancellation, and are preserved by ApplyConfig. Business bindings are not implicitly exposed. Generated business packages do not run producer extensions; runtime JIT executes generated business code.

C regressions verify cross-phase state, copied inputs, generation/diagnostics, rejected input rollback, budget and cancellation failures, recovery, source import policy and package execution. Native installed consumers verify service lifetime, configuration preservation and callback/copy/release reentry.

## Resolved module environments

`VS_INTEROP_CAP_MODULE_ENVIRONMENT` identifies the environment bridge. With matching native Core/Interop SDKs, create an immutable `FModuleEnvironment` and wrap it with `CreateModuleEnvironment` from `VeroScriptInteropModuleEnvironment.h`. C uses only the opaque handle: `vs_interop_module_environment_retain` creates another owned handle, and each handle is released once. A configured session independently retains the snapshot, its resources, native code owners and dependency contracts.

`vs_interop_session_apply_config_with_environment(session, config, environment)` applies a complete configuration and selects the snapshot; NULL removes it. Ordinary `vs_interop_session_apply_config` preserves the current selection. Both require the idle creator thread. Clear a loaded program before changing snapshots. A conflict with existing local bindings returns `ENGINE_ERROR` before configuration or cancellation changes. An environment-owned catalog rejects local binding mutation with `ENGINE_ERROR`; prepare a combined immutable catalog in native code. An environment without a catalog permits ordinary C registration.

Mapped memory/file sources, compilation resources, localization services and exact external dependency checks follow the Core environment contract for loading, execution, packages and external JIT. This bridge consumes an already resolved environment; it does not install packages or discover native extensions. Keep published callbacks immutable and use matching native SDK binaries. Owners may be released during configuration or final session destruction; their destructors must not throw, and configuration/execution reentry is rejected. Allocation/internal failure during Core application still requires retrying a complete configuration before relying on its settings.

A backend-resolution rejection returns `ENGINE_ERROR` before configuration or cancellation state is committed. Retry with a valid environment. A rejected hot-reload setting also returns `ENGINE_ERROR`. If bounded rollback remains `RESET_REQUIRED`, destroy and recreate the session before further execution.

<a id="interop-regression-targets"></a>

The module’s CMake gates include `VeroScriptInteropTests`, `VeroScriptInteropExtendedTests`, `VeroScriptInteropTypeTests`, `VeroScriptInteropMemoryTests` and the C11 `VeroScriptInteropCHeaderTest`. Installed native consumers are covered by `VeroScriptInteropNativeSdkTests` when enabled. Keep actual exported symbols aligned with the public ABI manifest and validate the separately obtained C11 example against an extracted SDK. Exercise compiler-service lifetime, wrong-thread rejection, callbacks and retained values with the matching Core/VM. Record actual results and skips; source coverage alone is not release acceptance.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](abi-and-lifetime.md) · [fr-FR](../fr-FR/abi-and-lifetime.md) · [ja-JP](../ja-JP/abi-and-lifetime.md) · [ko-KR](../ko-KR/abi-and-lifetime.md) · [zh-CN](../zh-CN/abi-and-lifetime.md) · [zh-TW](../zh-TW/abi-and-lifetime.md)
