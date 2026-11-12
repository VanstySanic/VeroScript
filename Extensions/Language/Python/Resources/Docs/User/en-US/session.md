# Python sessions and execution

Clearing a session or disabling hot reload can run script destructors and Python callbacks. Callback exceptions are reported immediately as `CallbackError`; cleanup or configuration changes already completed remain in effect.

Closing a live session during an active operation or callback raises `Error` and keeps it usable. Close it after the outer operation returns. During finalization, `closed` is already true and repeated `close()` is harmless.

`Session` owns one VeroScript compiler/runtime session. Use a context manager
so it is closed on the thread that created it:

```Python
from veroscript import Backend, Session

with Session(
	backend=Backend.MIXED,
	import_paths=("modules",),
	script_args=("release",),
	use_bytecode_cache=True,
	allow_circular_imports=True,
) as session:
	loaded = session.load_file("main.vs")
	if not loaded.success:
		print(loaded.diagnostics)
	else:
		run = session.run()
```

## Constructor options

| Option | Meaning |
| --- | --- |
| `backend` | `Backend.VM`, `Backend.MIXED` (default), or `Backend.JIT`. |
| `import_paths` | Additional module search paths. |
| `implicit_import_paths` | Modules implicitly imported into every compiled source. |
| `script_args` | Values passed to `main(string[] args)`. |
| `defines` | Object-like macros or conditional-compilation definitions. |
| `stdlib_search_paths` | Additional standard-library roots. |
| `source_overlays` | Mapping of virtual path to in-memory source. |
| `bytecode_cache_directory` | Cache root when bytecode caching is enabled. |
| `use_bytecode_cache` | Enables file bytecode caching; default `False` for Python embedding. |
| `max_call_depth` | Per-session call-depth limit; `0` uses the runtime default. |
| `max_instructions` | Maximum VM instructions per execution; `0` means unlimited. |
| `timeout_milliseconds` | Wall-clock execution limit; `0` means unlimited. |
| `enable_execution_control` | Enables the cooperative cancellation token for the session. |
| `capability_policy_enabled` | Restricts standard-library side effects to `allowed_script_capabilities` when enabled. |
| `allowed_script_capabilities` | Bit flags for file, environment, console, and task-spawn permissions. |
| `allow_circular_imports` | Allows cyclic imports; Python's default is `False`. |
| `allow_project_wide_access` | Shares non-exported declarations across the configured project scope. |
| `hot_reload_enabled` | Enables state-preserving reload operations; default `False`. |
| `print_callback` | Receives script output lines. |
| `diagnostic_callback` | Receives diagnostic lines. |

`load_source` compiles source from a string; `load_file` reads a file;
`load_package` loads a parsed `Package`; and `reload_package` replaces the
current package. `run(RunMode.SCRIPT)` invokes `main`; `RunMode.EVAL` uses the
incremental evaluation path.

`clear()` discards the loaded program state. `set_input(text)` supplies the
buffer used by standard-library input functions for the next execution, and
`set_hot_reload_enabled(True)` enables state-preserving source reload after
construction.

## Execution limits, cancellation, and capabilities

`max_instructions` and `timeout_milliseconds` are cooperative per-execution
limits. Zero disables the corresponding limit. The VM uses safe points to
apply these limits, and compatible controlled native entries apply the same
governance. Limits and cancellation take effect at the next check after a
blocking native host callback returns.

When `enable_execution_control=True`, call `request_cancellation()` from any
Python thread to request cancellation of the active execution. The session
must remain alive until the request returns. The cancellation request remains
set for that session; create a new `Session` before starting another execution.

When `capability_policy_enabled=True`, only the bits granted by
`allowed_script_capabilities` are available to standard-library side effects:
file read/write/delete, environment read/write, console input/output, task
spawning, and `PROCESS_DIRECTORY_CHANGE`. `chdir` requires both
`ENVIRONMENT_WRITE` and `PROCESS_DIRECTORY_CHANGE`. The policy covers built-ins; host callbacks remain responsible for
their own authorization. With the policy disabled, the default is to allow all
of these built-ins.

## Results and errors

`RunResult` contains:

| Field | Meaning |
| --- | --- |
| `success` | Compilation/execution completed normally. A non-zero script exit is still success. |
| `exit_requested` | The script explicitly called `exit(n)`. |
| `exit_code` | The `int main` or `exit(n)` code, or an engine failure code. |
| `diagnostics` | Structured `Diagnostic` records with message ID, file label, line, column, and arguments. |

Methods that return values, such as `call`, raise `VeroScriptError` when the
returned `RunResult` is unsuccessful. Transport, type, thread, or callback
failures raise `Error`, `TypeMismatchError`, `ThreadError`, or
`CallbackError` as appropriate. Inspect `VeroScriptError.result` for the
structured failure.

## Calls and state access

After loading a module, a session can call functions and methods, construct
objects, access fields, and access globals:

```Python
with Session() as session:
	session.load_source("""
struct Counter { int value = 0; }
int Add(int left, int right) { return left + right; }
""", "api.vs")
	assert session.call("Add", 20, 22) == 42
	instance = session.construct("Counter")
	session.set_field(instance, "value", 7)
	assert session.get_field(instance, "value") == 7
```

Use `has_function(name, argument_count)` and `has_method(instance, name,
argument_count)` before optional calls. Global reads and writes require hot
reload state support according to the core runtime contract.

The public state-access names are `is_loaded`, `get_global(name)`, and
`set_global(name, value)`. `load_source`, `load_file`, `load_package`, and
`reload_package` return a `RunResult`; `call`, `construct`, `call_method`,
`get_field`, and `set_field` raise `VeroScriptError` when the operation result
is unsuccessful. Use `clear()` to discard the loaded program and `reload()` for
state-preserving source reload.

## Backend and JIT policy

`MIXED` is the normal starting point. Supported functions may use optimized
execution while unsupported bodies fall back to the VM. `JIT` requests eager
compilation but is not a strict no-fallback mode. `VM` is interpreter-only.


Use `JitPolicy` only when the host needs to control compilation budgets:

```Python
from veroscript import JitPolicy

session.set_jit_policy(JitPolicy(
	mixed_hot_call_threshold=2,
	max_compile_attempts=256,
	max_cached_entries=256,
	eager_compile_budget=64,
))
stats = session.jit_stats
```

The policy fields control mixed-mode hotness, compile attempts, cache entries,
and eager compilation. Statistics are diagnostic counters, not performance
guarantees. Compare normal non-debug runs with fixed inputs and account for
warm-up; the bytecode cache changes compilation time, not script work. The
`jit_stats` mapping includes `direct_native_calls`, which counts generation-
scoped JIT calls that reached another native entry without passing through the
VM value bridge.

`jit_stats` is an owning snapshot of the last invocation. Actual executable
entries are reported by `native_leaf_executions`, `native_cfg_executions`,
`bridge_executions`, `linear_executions`, `vm_trampoline_executions`,
`controlled_native_executions` and `aot_executions`, including entries that fail
or transfer control. Compilation and cache-entry counts are separate;
`direct_native_calls`, scalar fast paths and typed loops count nested events
and must not be added as independent function entries. VM execution leaves
these executable-entry counters at zero. A new invocation resets them, even
when compiled code is reused; `clear()` resets the session snapshot.
`jit_cache_stats` retains cumulative compilation and resource totals for the
reusable context.

### Reusable session-code cache

<a id="session-jit-cache"></a>

`JitCacheLimits` controls retained code across eligible ordinary `Session.call`/native `CallFunction` invocations; it is separate from the bytecode cache. `JitPolicy.reuse_session_code` defaults to `True`. Zero `max_compile_attempts` or `max_cached_entries` means unlimited counts, including in the retained context. Reuse requires a selected compatible JIT factory and an effective JIT-capable policy. Persistent sessions and stable callbacks can reuse code. Native binding catalogs must provide a runtime revision. Governed native execution can reuse a context with matching governance requirements.

`max_native_bytes` defaults to 8 MiB and `max_retained_payload_bytes` to 32 MiB; zero permits no retention of that resource. Configure and inspect through `session.set_jit_cache_limits(...)`, `session.jit_cache_limits` and `session.jit_cache_stats`. Statistics are settled cumulative context values, not live or process-wide memory measurements. Changing policy, limits, configuration, module, backend or an observable host boundary invalidates the context; a resource limit selects a safe fallback.

Governed `Backend.MIXED` uses the VM by default. `session.set_jit_policy(JitPolicy(enable_controlled_mixed=True))` opts into compatible controlled native execution; explicit `Backend.JIT` is independent of that switch. Unsupported entries fall back before execution. Debugging, retained initialization and bounded cleanup use the VM.

## Reloading and threading

`Session` is creator-thread confined. Close it on the creator thread. Native
calls release the GIL where possible, but one `Session` and one `Value` should
not be concurrently mutated from multiple Python threads. Script callbacks
may run on VM worker threads, so synchronize shared Python state in callbacks.

With hot reload enabled, `reload()` recompiles changed source and attempts to migrate compatible state. Incompatible values can prevent migration. Script rollback leaves native side effects in place.


Check `CAPABILITIES & RuntimeCapabilities.RELOAD_STATUS` before reading `session.reload_status` on its creator thread. The returned `ReloadStatus` snapshot identifies the reload by `operation_id` and separates `current_generation` from `committed_generation`. `PENDING` means the candidate is staged and awaits initialization. `REJECTED` leaves the previous program in place, and `ROLLED_BACK` means restoration finished. `APPLIED_WITH_ERRORS` means the candidate committed before execution or cleanup failed. `RESET_REQUIRED` blocks further execution. Recreate the session if `clear()` cannot reset it. Live external values can prevent staging while preserving the prior status.

## VM selection

The standard Python package includes the static bytecode VM, so ordinary sessions need no external VM library. `RuntimeCapabilities.VM_BACKEND_SELECTION` identifies support for the selection API. On the session's creator thread while idle, use `set_vm_backend(path)` before the first execution to select a compatible VM file or directory; `None` or an empty string clears the selection. A failed replacement preserves the previous VM. Execution freezes the selection, including after `clear()`; create a new session to change it. This selection is independent of `Backend.VM`, `MIXED`, or `JIT` execution policy.

`get_vm_backend_info()` returns an owned `VmBackendInfo` snapshot with `id`, `version`, `target`, `library_path`, `capabilities`, `selected`, and `frozen`. A static VM has an empty `library_path`; `selected` distinguishes this from no selected VM. The snapshot remains valid after replacement or session closure.

## Explicit JIT selection

Execution policy and backend selection are separate. Without an installed module environment, Runtime supplies no default module: mixed uses the VM without a selected backend; forced jit fails before execution. Select an independently installed, compatible library file or directory with `Session.set_jit_backend(path)` while the Session is idle on its owner thread. `set_jit_backend(None)` / `set_jit_backend("")` disables selection. A failed replacement preserves the previous backend. Use `get_jit_backend()` / `get_jit_backend_info()` for actual selected identity/native capabilities; `backend-services` alone is the Core service contract. Eligible calls in persistent sessions can use the selected backend. File/directory discovery and ABI rules are described in the JIT guide.

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/en-US/index.md)

```python
with Session(backend=Backend.MIXED) as session:
	session.set_jit_backend("/absolute/path/to/Jit")
	session.load_source("int main(){ return 0; }")
	result = session.run()
```

## One-shot evaluation and backend features

In ordinary sessions, `run(RunMode.EVAL_ONCE)` runs the module initializer once per call without
invoking `main`, and permits initializer compilation by the selected JIT
backend. `RunMode.EVAL` keeps the incremental path; `RunMode.SCRIPT` also
invokes `main`. `RuntimeCapabilities.EVAL_ONCE` advertises this mode.
Hot-reload sessions preserve initialized globals and use the VM for retained initialization.

`get_jit_backend_info()` returns an owned `JitBackendInfo` snapshot. Its
`capabilities` uses `JitBackendCapabilities`: `NATIVE_CODE`,
`PORTABLE_EXECUTION`, `NATIVE_TYPED_LOOPS`, and `DIRECT_SCALAR_CALLS`.
Use this snapshot to inspect the selected module; `CAPABILITIES` describes
Core runtime services. The snapshot remains valid after backend replacement
or Session closure.

## Compile services and diagnostic severity

`COMPILE_CAPABILITIES` and `CompileCapabilities.ANNOTATION_PROCESSING` describe compile services independently of `CAPABILITIES` and the selected JIT backend. `Diagnostic.severity` is a `DiagnosticSeverity` value: `ERROR`, `WARNING`, `INFORMATION`, or `HINT`. Inspect `RunResult.success` to determine whether the operation succeeded; a nonempty diagnostic list can contain nonfatal messages.

`session.set_annotation_processing_service(handle)` attaches a valid opaque handle created by a native SDK extension while the session is idle. The session copies its configuration, so the producer may then release its own handle. Keep the native processor module and callback data alive until the service is cleared or the session closes. `None` or `0` clears it. Arbitrary integer addresses are not service handles. `register_annotation(...)` declares annotation vocabulary; it does not install a compile-time processor.

## Host console integration

`Session(console_query=..., console_control=..., console_write=...)` accepts optional terminal callbacks. The query returns `ConsoleInfo(output_terminal, color, cursor, clear, width, height)`; zero dimensions mean unknown. The controller receives `ConsoleOperation` and two integers: ANSI palette 0..15/default=-1, line modes after/before/all=0/1/2, zero based column/row, or visibility 0/1. Return `True` for synchronous success. The write callback receives exact text without a newline and returns `bool`. Omitting it keeps buffered print callbacks. Retain/synchronize callbacks through session close; Python callback exceptions are reported as `CallbackError`. Coordinate complete terminal sequences across tasks.

## Resolved module environments and general processing

`Session(module_environment=handle)` accepts an opaque snapshot handle created by `VeroScript::Interop::CreateModuleEnvironment` in the matching native SDK. The session retains the immutable environment; the producer may release its handle after construction. The environment defines source ownership, exact imports, resources, compile services and execution dependencies. It must supply any required stdlib roots and ownership; Python adds no packaged stdlib root for this mode. Create a new session to replace the snapshot. An environment-owned binding catalog rejects local registration, so combine bindings before publishing it. `None` or `0` selects ordinary configuration; arbitrary addresses are invalid. Query `RuntimeCapabilities.MODULE_ENVIRONMENT` for support.

`set_compile_processing_service(handle)` attaches a native SDK general processing service with the same idle, creator-thread and lifetime rules as the annotation service. `None` or `0` clears it. Its capability is `CompileCapabilities.GENERAL_PROCESSING`; configure script extensions separately with `set_script_compile_extensions(...)`.

## Closures and local static storage

Script function values support initialized captures and `mutable`. `Value.clone()`
and `retain_callable()` share the existing closure environment; evaluating the
lambda again creates a separate environment. Reference captures retain their storage
after the creating script function returns. A retained callable becomes invalid
when its session closes.

Local `static` variables belong to an execution state. Ordinary `call()` operations
create fresh states; `hot_reload_enabled=True` retains state between calls. Closing
the session releases its retained static state. Standalone [Python AOT](aot.md) rejects
source lambdas and local `static` declarations.

## Session backend selection arguments

`Session.set_vm_backend` / `set_jit_backend`

These Session setters accept a compatible file/directory or an installed backend ID when the supplied module environment provides its resolver. `auto` requests environment candidates; for VM it may use the retained product fallback. `none` or an empty native string disables selection; `None` / `""` map to that empty request. On a new session, omitting a backend setting allows environment selection; it differs from explicitly clearing that setting. When reconfiguring an existing session, omitting the VM selection preserves its execution domain. A failed explicit selection preserves the previous backend. Set on the idle creator thread; VM changes must precede execution because the domain remains frozen after Clear/reload. JIT selection remains independent of VM/Mixed/JIT execution policy.

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
