# Python runtime and static tools

## Typed-loop statistic

`native_typed_loop_executions` counts native typed-loop kernel invocations, including an invocation that ends after reporting a source-visible runtime failure. It is an execution-observability counter, not a success counter.

## Runtime adapter

`VeroScriptPython` exposes `Session`, `Value`, `Package`, retained callables,
`HostParameter`, host functions, and host classes over `VeroScriptInterop`.
Sessions and retained callables are creator-thread confined where documented and
must be explicitly closed. Native operations release the GIL where allowed;
callbacks reacquire it and may enter concurrently over time, so callback-owned
state needs external synchronization.

The public runtime surface also includes `ExternalReference` and its reference
kind, `GenericHostClass`, `available_languages`, `get_language`,
`set_language`, and `merge_catalog_directory`. Language discovery is returned
from the deployed catalog tree. Catalog merging validates the baseline and
preserves the distinction between runtime diagnostics and host-product UI text;
Python wrappers must not translate API names, catalog keys, or reference-kind
values.

`Value` uses explicit element types for arrays, maps, ordered maps, sets, queues,
stacks, and deques. Enumeration returns owned snapshots. Mutation callback
exceptions are reported after the native mutation and do not roll it back.
Signed `char`/`int8` is represented as a Python `int` in `-128..127`.
Character map keys and set elements preserve that representation in snapshots,
lookups, and `to_python()`; out-of-range inputs are rejected, not truncated.

## Static front end

`VeroScriptPythonFrontend` parses typed Python declarations without importing or
executing input modules. It records source ranges, functions, classes, enums,
fields, parameters, decorators, documentation, imports, and normalized type
annotations. Unsupported top-level execution and unsupported nested statements
fail with structured diagnostics.

`VeroScriptPythonBindGen` consumes this AST and emits Binding Catalog metadata,
one API reference per input, and a project manifest. `VeroScriptPythonTranspiler`
lowers the supported typed subset to VeroScript, resolves supplied project
relative imports, emits source maps, and validates the complete generated tree.

BindGen and Transpiler also reject generated names that are not ASCII
VeroScript identifiers or that are language keywords such as `do`. This check
still applies when compiler validation is disabled. The Transpiler likewise
enforces the core's 32-level statement-nesting limit and emits Python `pass` as
a valid empty VeroScript block. Classes and top-level functions whose names
begin with `_` are non-exported and cannot be referenced by generated public
contracts.
Numeric constants emitted by the Transpiler must be finite and fit the core's
unsuffixed signed 32-bit integer literal range.
Python list, set, and dictionary literals are accepted only as initializers of
typed declarations, matching the core's brace-initializer grammar.
Optional annotations and Python identity comparisons are rejected until the
typed subset has a nullable-type contract. `None` is emitted only as a bare
return from a `None`-returning function or constructor.
`break` and `continue` require an enclosing emitted `while` loop.
Supported integer `IntEnum` and `IntFlag` expressions are safely folded to the
signed 64-bit literals required by the core enum grammar.
`self` is rewritten to `this` only as an instance-method receiver; ordinary
top-level and static-method identifiers named `self` remain unchanged.
Non-void functions must return on every statically supported path. Annotated
locals must be declared directly in the function body; declarations inside
`if` or `while` blocks are rejected to prevent Python/VeroScript scope
divergence.
Multi-file projects reject exported module-variable collisions with another
module variable or type before emission, matching the core symbol namespace.

## Session execution contract

The Python `Session` surface mirrors the native session lifecycle. The runtime
entry points are `load_source`, `load_file`, `load_package`, `reload_package`,
`build_package_file`, `run`, `reload`, `diagnostics`, and `set_input`.
`set_hot_reload_enabled` controls state-preserving reload, while
`request_cancellation` requests cooperative cancellation from another thread;
it does not forcibly interrupt an active host callback. `set_jit_policy` and
`jit_stats` expose the same adaptive backend policy and last-run statistics as
the core VM. `CAPABILITIES` describes Core services and is decoded with
`RuntimeCapabilities`. Query `session.get_jit_backend_info()` for the
selected module: `native_code` reports native emission and `capabilities`
is a `JitBackendCapabilities` mask. Its `NATIVE_CODE`, `PORTABLE_EXECUTION`,
`NATIVE_TYPED_LOOPS`, and `DIRECT_SCALAR_CALLS` flags describe the selected
backend independently of `HOST_PLATFORM`.
`cost_model_vm_fallbacks` and `native_typed_loop_executions` report the
corresponding last-run decisions and executions.

The Python adapter forwards `reuse_session_code`, `enable_controlled_mixed`, cache limits and statistics to the native Session. Maintain eligibility and generation accounting in [C++ Session](../../../../../Cpp/Resources/Docs/Developer/en-US/session.md#session-jit-cache); the Python-facing API, defaults and quota meanings are maintained in [Python Session](../../User/en-US/session.md#session-jit-cache). Do not impose a separate nonzero-count requirement or disable governed reuse in the adapter.

## Values, calls, and channels

`Value.channel` creates an unbounded or bounded channel. `send`, `try_send`, and
`send_for` are the blocking, non-blocking, and timed send forms; `receive`,
`try_receive`, and `receive_for` are the matching receive forms, and
`close_channel` closes the synchronization endpoint. Channel values are the
explicit cross-worker sharing mechanism; ordinary script globals remain
worker-local.

The session call surface includes `has_function`, `has_method`, `call`,
`call_method`, `call_method_bypassing_interceptor`,
`construct_object`, `retain_callable`, `get_field`, `set_field`, `get_global`,
`set_global`, and `apply_class_defaults`. Bypassing an entry interceptor does
not disable interceptors for nested calls made by the method body.

## Host bindings, reflection, and persistence

Registration entry points include `register_function`, `register_annotation`,
`register_type_alias`, `register_generic_host_type`,
`instantiate_generic_host_type`, `register_host_class`, `register_host_enum`,
`register_host_protocol`, and `register_host_member`. Reflection and binding
exports are available through `export_reflection_json` and
`export_bindings_json`.

`register_serializer`, `serialize_object`, `deserialize_object`, and
`enumerate_object_references` share the Interop external-serialization and
borrowed-reference rules. `set_mutation_callback` reports field and container
mutations, and `set_instance_call_interceptor` receives the resolved target and
may continue or handle it. API names, JSON keys, type names, and diagnostics
remain exact and are not localized.
## Python threading boundary

The blocking, timed, and non-blocking channel operations release the Python GIL
while the native channel operation is in progress. This prevents a waiting
channel from blocking unrelated Python threads, but it does not make a
Session or a Value generally thread-safe. Session operations retain their
native owner-thread rules.

request_cancellation is the only Session operation explicitly safe from
another Python thread. The caller must retain the Session object until the
native request returns. Native callback exceptions are recorded and converted
back to Python after the call boundary. The JIT statistics mapping includes
direct_native_calls as a separate counter.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](runtime-and-tools.md) · [fr-FR](../fr-FR/runtime-and-tools.md) · [ja-JP](../ja-JP/runtime-and-tools.md) · [ko-KR](../ko-KR/runtime-and-tools.md) · [zh-CN](../zh-CN/runtime-and-tools.md) · [zh-TW](../zh-TW/runtime-and-tools.md)
