# Interop SDK

## JIT statistics compatibility

For `vs_interop_session_get_last_jit_stats`, a zero `struct_size` is the legacy form and writes only the original statistics prefix; current callers pass `sizeof(vs_interop_jit_stats)` to receive appended counters. `native_typed_loop_executions` counts native typed-loop kernel invocations, including one that ends after reporting a source-visible runtime failure. It is not a success counter.

`native_leaf_executions`, `native_cfg_executions`, `bridge_executions`, `linear_executions` and `vm_trampoline_executions` count actual ordinary executable entries, including entries that exit by error or control transfer. The corresponding `*_entries` fields count compiled artifacts. These are last-execution counters; a warm cache hit can execute without compiling. `scalar_native_fast_path_calls` counts host native fast-path calls. Direct calls and typed-loop invocations are nested events and must not be added as independent function calls.

The C API exposes aggregate statistics. Optional per-function diagnostic snapshots, fallback reasons and timing are currently available through Core/C++; the C JIT policy does not enable that collection.

`VeroScriptInterop` is a language-neutral process ABI for C, Python, C#, Rust,
and other adapters. It exposes VeroScript sessions, values, calls, bindings,
and packages without depending on C++ object layouts or STL types. For direct
C++ embedding, start with [Host integration](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md); for a cross-language
or stable dynamic-library boundary, use the SDK described here.

Function signatures, structure layouts, constants, and feature availability are
defined by the shipped public header. This page explains the contracts and usage
patterns; consult that header for the complete declaration list.

## ABI and installation

The SDK supplies a `VeroScriptInterop.h` public header valid in C11 and C++17:

```c
#define VS_INTEROP_ABI_VERSION UINT32_C(0x00000101)
```

This release establishes ABI 0.1.1 as the complete baseline. The upper 16 bits
encode the major version; the next 8 bits encode the minor version and the
lowest 8 bits encode the patch version. Bindings require an exact version match. Descriptor and output structures begin with
`struct_size`; future compatible revisions append fields at the end. Fixed
string views and key/value pairs intentionally have no `struct_size` because
their layout is fixed by the header.

The SDK package provides the runtime library, the C11 header, and the CMake
import target. An adapter does not need VeroScript C++ headers or an STL ABI.

```cmake
find_package(VeroScriptInterop CONFIG REQUIRED)
target_link_libraries(MyAdapter PRIVATE VeroScript::VeroScriptInterop)
```

On Windows, deploy the SDK runtime DLLs, localization resources and runtime license notices beside your host executable. Preserve these files when distributing the application; copying only `VeroScriptInterop.dll` omits its deployment dependencies. Obtain integration examples separately from the documentation/source references.

## Opaque object model

The interface uses opaque handles. Adapters must not inspect their layout or
free module-owned memory with the system allocator.

| Handle | Purpose |
| --- | --- |
| `vs_interop_session` | Isolated loading, compilation, execution, and call session. |
| `vs_interop_value` | Script scalar, object, container, function, or host-object value. |
| `vs_interop_callable` | Script callable retained beyond the current native callback. |
| `vs_interop_call_context` | Reads native callback arguments, writes reference arguments, and performs callback-local nested calls. |
| `vs_interop_buffer` | Module-owned error text, package bytes, or serialization output; release with `vs_interop_buffer_destroy`. |
| `vs_interop_package` | Validated in-memory `.vsapp`/`.vslib` package. |
| `vs_interop_intercept_context` | Receiver, resolved target, and arguments for an instance interceptor. |
| `vs_interop_generic_type_context` | Defines a concrete generic host value type, members, and protocols. |
| `vs_interop_external_serialize_context` / `deserialize_context` | Contexts for external object serialization and deserialization callbacks. |

The smallest C workflow is: create a session, apply configuration, load source
or a file, run, read diagnostics, and destroy.

```c
#include "VeroScriptInterop.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static vs_interop_string_view view(const char* text)
{
	vs_interop_string_view result;
	result.data = text;
	result.size = (uint64_t)strlen(text);
	return result;
}

int main(void)
{
	vs_interop_session* session = NULL;
	vs_interop_session_options options = {0};
	options.struct_size = sizeof(options);

	if (vs_interop_session_create(&options, &session) != VS_INTEROP_STATUS_OK)
	{
		return 1;
	}

	const vs_interop_string_view source = view(
		"int main()\n"
		"{\n"
		"\tprint(\"hello\");\n"
		"\treturn 0;\n"
		"}");
	vs_interop_run_result load = {0};
	load.struct_size = sizeof(load);

	const vs_interop_status load_status = vs_interop_session_load_source(
		session,
		source,
		view("main.vs"),
		&load);

	if (load_status != VS_INTEROP_STATUS_OK || !load.success)
	{
		vs_interop_session_destroy(session);
		return 2;
	}

	vs_interop_run_result run = {0};
	run.struct_size = sizeof(run);
	const vs_interop_status run_status = vs_interop_session_run(
		session,
		VS_INTEROP_RUN_SCRIPT,
		&run);

	const int result = run_status == VS_INTEROP_STATUS_OK && run.success ? 0 : 3;
	vs_interop_session_destroy(session);
	return result;
}
```

The script is passed through C string escaping. Real scripts should follow the
project convention of Tab indentation, braces on their own lines, and an
`int main` entry point.

## Status, diagnostics, and exit semantics

All calls return explicit `vs_interop_status` values. Common statuses include:

| Constant | Meaning |
| --- | --- |
| `VS_INTEROP_STATUS_OK` | The call completed. |
| `VS_INTEROP_STATUS_INVALID_ARGUMENT` | A handle, pointer, string view, or array argument is invalid. |
| `VS_INTEROP_STATUS_WRONG_THREAD` | A session or object thread rule was violated. |
| `VS_INTEROP_STATUS_ENGINE_ERROR` | Loading or execution failed. |
| `VS_INTEROP_STATUS_TYPE_MISMATCH` | A value kind or host type does not match. |
| `VS_INTEROP_STATUS_CALLBACK_ERROR` | A host callback reported failure. |
| `VS_INTEROP_STATUS_UNSUPPORTED` | The current build does not provide the capability. |
| `VS_INTEROP_STATUS_OUT_OF_MEMORY` / `BUFFER_TOO_SMALL` / `INTERNAL_ERROR` | Resource, output-buffer, or internal failure. |

`vs_interop_run_result` contains `struct_size`, `success`, `exit_requested`,
`exit_code`, and `diagnostic_count`:

- `success` reports normal compilation/execution, not whether `exit_code` is zero;
- a successful `int main` may return a non-zero exit code;
- `exit_requested` is set only by an explicit `exit(n)`, including `exit(0)`;
- an ordinary `int main` return does not set `exit_requested`;
- on failure, `exit_code` is an engine or API error code rather than a normal script exit code.

Use `vs_interop_session_diagnostic_count` and
`vs_interop_session_get_diagnostic` to read diagnostics. Each diagnostic has a
`message`, `message_id`, `file_label`, `line`, and `column`; arguments can be
read and formatted with the diagnostic-argument and `format_diagnostic`
functions. Returned string views have the lifetime documented for the owning
session operation.

## Localization

Interop localization is maintained by the core as one process-wide state, and
the localization operations themselves are thread-safe. It is not an
independent setting on each `vs_interop_session`. A language change affects
diagnostics formatted by the core after the change, including
`vs_interop_session_format_diagnostic`.

| Function | Purpose |
| --- | --- |
| `vs_interop_locale_set_language` | Selects a loaded language from a canonical or normalizable BCP 47 tag; returns `VS_INTEROP_STATUS_OK` on success. |
| `vs_interop_locale_get_language` | Returns the canonical culture tag that actually matched, such as `en-US` or `zh-CN`. |
| `vs_interop_locale_translate` | Returns module-owned UTF-8 translated text using the loaded process catalogs; unknown English messages are copied unchanged. Free with `vs_interop_buffer_destroy`. |
| `vs_interop_locale_get_available_languages` | Returns a module-owned UTF-8 JSON buffer containing the canonical culture tags currently loaded. |
| `vs_interop_locale_load_catalog_directory` | Validates and replaces the complete process-wide localization tree. |
| `vs_interop_locale_merge_catalog_directory` | Validates a tree and merges it into the active catalogs; `override_existing != 0` replaces existing message IDs. |

The directory must use the `<culture>/<component>.json` layout described in
[Diagnostic localization](../../../../../VeroScript/Resources/Docs/User/en-US/localization.md) and must include a complete
`en-US` baseline. When catalog validation or loading fails,
`load_catalog_directory` and `merge_catalog_directory` return
`VS_INTEROP_STATUS_ENGINE_ERROR` and write a module-owned error buffer to
`out_error`; invalid arguments, allocation failures, and internal exceptions
use their corresponding statuses. Release the error buffer and the available-
language JSON buffer with `vs_interop_buffer_destroy`. The string view from
`vs_interop_locale_get_language` is not owned by the adapter; copy it when it
must be retained.

```c
/* The view(...) helper from the minimal workflow above is reused here. */
int ConfigureLocale(void)
{
	vs_interop_buffer* error = NULL;
	if (vs_interop_locale_load_catalog_directory(
			view("localization"),
			&error) != VS_INTEROP_STATUS_OK)
	{
		if (error != NULL)
		{
			vs_interop_buffer_destroy(error);
		}
		return 1;
	}

	if (vs_interop_locale_set_language(view("zh-CN")) != VS_INTEROP_STATUS_OK)
	{
		return 2;
	}
	return 0;
}
```

`vs_interop_locale_get_available_languages` returns a JSON array containing the
canonical culture tags currently loaded. An adapter should use that returned
array to populate a language selector
without scanning the host filesystem again.

Adapters should check that `vs_interop_get_capabilities()` contains
`VS_INTEROP_CAP_LOCALIZATION` before calling these optional exports. Catalogs
may be loaded before sessions are created or while the process is running, but
a host sharing one process across threads or sessions should define when
language changes are allowed so that one UI batch does not mix languages.

## Session configuration

`vs_interop_session_apply_config` accepts `vs_interop_config`:

| Field | Purpose |
| --- | --- |
| `import_paths` | Ordinary module search paths. |
| `implicit_import_paths` | Files implicitly imported into every source module; this does not edit or re-export source declarations. |
| `script_args` | Arguments passed to `main(string[] args)`. |
| `defines` | Object-like macros or conditional-compilation definitions. |
| `stdlib_search_paths` | Standard-library search roots. |
| `source_overlays` | In-memory path/source overrides. |
| `bytecode_cache_directory`, `use_bytecode_cache` | Bytecode-cache location and switch. |
| `max_call_depth` | Per-run recursion/call-depth limit; `0` uses the environment or default. |
| `max_instructions` | Maximum VM instructions per execution; `0` means unlimited. |
| `timeout_milliseconds` | Wall-clock execution limit; `0` means unlimited. |
| `execution_backend` | `VM = 0`, `Mixed = 1`, `JIT = 2`. |
| `predefined_macros` | Structured function-like macros with name, parameters, body, source name, and variadic flag. |
| `allow_circular_imports` | Whether cyclic imports are allowed; enabled by default. |
| `allow_project_wide_access` | Whether non-exported declarations may be shared project-wide; disabled by default. |
| `enable_execution_control` | Installs the cooperative cancellation token; a successful configuration resets a prior cancellation request. |
| `capability_policy_enabled` | Enables the standard-library side-effect policy. |
| `allowed_script_capabilities` | Bit mask of `FILE_READ`, `FILE_WRITE`, `FILE_DELETE`, `ENVIRONMENT_READ`, `ENVIRONMENT_WRITE`, `CONSOLE_INPUT`, `CONSOLE_OUTPUT`, `TASK_SPAWN`, and `PROCESS_DIRECTORY_CHANGE`. Used only when the policy is enabled. `chdir` needs both `ENVIRONMENT_WRITE` and `PROCESS_DIRECTORY_CHANGE`. |

`max_instructions` and `timeout_milliseconds` are cooperative per-execution
limits; zero disables the corresponding limit. A non-zero limit or
`enable_execution_control` requires cooperative checks on the VM or an eligible
controlled JIT path. Governed Mixed uses the VM by default; set
`vs_interop_jit_policy.enable_controlled_mixed` only after measuring the target
workload. Explicit JIT selection is independent of that switch. Query the selected
backend's `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION` bit and the last-run
`controlled_native_executions` counter; unsupported or VM-preferred entries keep
the governed VM fallback. `vs_interop_session_request_cancel` is thread-safe and
requests cancellation of an execution-control-enabled session. It returns
`VS_INTEROP_STATUS_INVALID_ARGUMENT` when execution control is not enabled and
does not forcibly terminate a native callback already in progress. Keep the
session alive until the request returns.

When `capability_policy_enabled` is non-zero, only the bits in
`allowed_script_capabilities` are granted to standard-library built-ins. Host
callbacks are outside this policy and must enforce their own authorization.
Check `VS_INTEROP_CAP_EXECUTION_GOVERNANCE` before depending on these optional
governance features.

`vs_interop_jit_policy` controls mixed-backend hotness thresholds, maximum
compile attempts, cache capacity, and eager-compile budget.
`vs_interop_session_get_last_jit_stats` returns the most recent compilation,
cache, dispatch, fallback, budget-skip, fast-path, and `direct_native_calls`
counters. `direct_native_calls` counts generation-scoped JIT calls that reached
another native entry without passing through the VM value bridge.

### Reusable session-code cache

First check `VS_INTEROP_CAP_SESSION_JIT_CACHE`. This optional capability exposes
`vs_interop_jit_cache_limits` and `vs_interop_jit_cache_stats`, together with
the session set/get-limits and get-stats functions. Initialize `struct_size`
for every supplied or received extensible structure. The default limits are
8 MiB of native code and 32 MiB of total retained payload; a zero limit
disables retention for that resource. This cache is separate from the bytecode
cache and retains compiled code for eligible ordinary Run, EvalOnce and function
calls. Its statistics are settled cumulative context values, not live or
process-wide memory measurements. Reconfiguration or a context invalidation
clears retained code, while a resource limit safely falls back to VM execution
without changing script semantics.

For reuse, `vs_interop_jit_policy.reuse_session_code` must be nonzero (the
default). A zero `max_compile_attempts` or `max_cached_entries` policy budget means
unlimited for that counter; native-code and payload retention limits still apply.

Eligibility requires a non-VM effective backend and a stable binding revision.
Persistent and hot-reload sessions can reuse generation-bound code. Changes to
native bindings, instance interceptors or mutation callbacks invalidate retained
code. Debugging, retained initialization and bounded cleanup use the VM.
Governed execution can retain compatible controlled code and its governance
identity across calls; it does not reset the context's compilation quota.

### Public macros and packages

`defines` and `predefined_macros` seed each source module. `#define` is private;
`#define_export` publishes the module's final definition. Explicit `import`
activates public macros after its path string; `export import` and `#include`
also re-export them. `implicit_import_paths` exposes declarations only. These
rules also apply to `source_overlays`. Public function-like macros expand in
the caller's environment and do not capture private helper macros. Different
origins with the same macro name conflict; use `#undef` before replacing one.
A cyclic source dependency closure containing any `#define_export`, including an
inactive directive, is rejected regardless of `allow_circular_imports`.

Library packages built through the C ABI retain public macros with either value
of `strip_debug_info`. Conditional exports are fixed at build time; consumer
defines do not reevaluate them. Application packages carry no public macros.
Bytecode uses a fixed `VSBC` identifier and a separate format version that must
match exactly when loading; rebuild packages and caches containing
`VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` from source. The public C ABI remains 0.1.1. Rebuild independent
JIT backends against the current core; incompatible header fingerprints/layouts
are rejected during loading.

## Types and values

`vs_interop_value_create_array` canonicalizes the element type and checks each value. An object slot accepts a script-derived object implementing its declared base class or interface, using the source value’s own module metadata. Container declarations remain invariant: this does not make `array<Derived>` assignable to `array<Base>`, and does not add implicit numeric or container conversions.

`vs_interop_native_type_desc` recursively describes return types, parameters,
and container type arguments for host functions and members. Public native
types include:

| Category | Type identifiers |
| --- | --- |
| Scalars | `VOID`, `BOOL`, `INT32`, `UINT32`, `INT64`, `UINT64`, `FLOAT`, `DOUBLE`, `INT8`, `UINT8`, `INT16`, `UINT16`, `STRING`. |
| Host and enum | `HOST_OBJECT`, `ENUM`; the name field carries a host type, enum name, or function signature. |
| Containers | `ARRAY`, `MAP`, `SET`, `QUEUE`, `STACK`, `ORDERED_MAP`, `DEQUE`, `CHANNEL`, `MULTI_MAP`, `ORDERED_MULTI_MAP`, `PRIORITY_QUEUE`, `MULTI_SET`, `ORDERED_MULTI_SET`. |
| Callable | `FUNCTION`. |

`vs_interop_value_kind` additionally distinguishes `OBJECT`, `NATIVE_ARRAY`,
`WSTRING`, `FUNCTION`, and `HOST_OBJECT`. Fixed-width numeric values are
distinct kinds: `FLOAT` is binary32, `DOUBLE` is binary64, and `INT8`/`UINT8`/
`INT16`/`UINT16` must not be collapsed to `INT32` or an unsigned C `int` by an
adapter.

Use paired `vs_interop_value_set_*`/`get_*` functions for scalars. UTF-8 text
uses the length-delimited `vs_interop_string_view`; wide strings use UTF-16
code units plus an explicit length. Values can be created, cloned, reset, and
destroyed. `clone` copies the value wrapper's reference semantics; it does not
automatically deep-copy a container or object payload. Use
`vs_interop_value_assign(destination, source)` to copy a complete runtime value
into a writable destination while preserving container and object identity
semantics. The operation rejects read-only borrowed destinations.

Arrays, queues, stacks, deques, maps, sets, and channels have dedicated create,
count, read, contains, send, receive, and close operations. Map and set keys
must be language-supported exact types: signed/unsigned integers, UTF-8/UTF-16
strings, scoped enums, or host values. Host values use identity by default, or
a registered equality/hash stamp protocol. A missing map key returns success
with `out_found == 0` and does not overwrite the output value. Receiving from
an open channel may block; closing a channel wakes waiters.

### Value kinds in ABI

ABI 0.1.1 includes the `NULL`, `MULTI_MAP`, `ORDERED_MULTI_MAP`, `PRIORITY_QUEUE`,
`MULTI_SET`, and `ORDERED_MULTI_SET` value kinds. Use
`vs_interop_value_set_null` and `vs_interop_value_is_null` for script
`nullptr`; null is a real value kind, not an invalid handle. Typed native calls
may normalize a null host-object value to the declared nullable host reference.

`vs_interop_value_create_sequence` accepts `PRIORITY_QUEUE` in addition to
arrays, queues, stacks, and deques. Its input is arbitrary insertion order and
the VM normalizes it according to priority-queue semantics. Use
`vs_interop_value_create_map_kind` for ordinary and multi-map variants;
multi-maps preserve every repeated key/value association, whereas ordinary
maps replace a duplicate key. Use `vs_interop_value_create_set_kind` for
ordinary and multi-set variants; multi-sets preserve duplicate elements.

`vs_interop_value_set_host_object_ex` adds an optional
`vs_interop_host_object_is_type` callback. The callback reports whether a
payload is compatible with an expected host type during overload or type
validation. It must not throw, retain the borrowed type-name view, or cross
the ABI boundary with an exception. The legacy host-object constructor is
equivalent to the extended form with no subtype callback.

## Native bindings and calls

Adapters can register:

- `vs_interop_session_register_native` for a compact native function;
- `vs_interop_session_register_host_function` for structured return types, parameters, documentation, parameter names, annotations, and generic metadata;
- `vs_interop_session_register_host_type`, `vs_interop_session_register_host_member`, and `vs_interop_session_register_host_protocol` for enums, structs, classes, constructors, methods, properties, fields, static members, operators, cloning, equality, hashing, and subclass factories.

Parameter flags are bit flags: `VS_INTEROP_PARAM_REF = 0x01` and
`VS_INTEROP_PARAM_CONST_REF = 0x02`. `const ref` must include the reference bit.
Host callbacks can read arguments, access mutable arguments, obtain
`host_this` and the program generation, and perform callback-local invocation
of a script function or retained callable through `vs_interop_call_context`.

Script function, method, constructor, field, and global calls use:

| Capability | Representative functions |
| --- | --- |
| Free functions and methods | `vs_interop_session_call_function`, `vs_interop_session_call_method`. |
| Interceptor bypass | `vs_interop_session_call_method_bypassing_interceptor`. |
| Queries | `vs_interop_session_has_function`, `vs_interop_session_has_method`. |
| Objects and members | `vs_interop_session_construct_object`, `vs_interop_session_get_field`, `vs_interop_session_set_field`, `vs_interop_session_get_global`, `vs_interop_session_set_global`. |
| Retained callables | `vs_interop_session_retain_callable`, `vs_interop_callable_invoke`, `vs_interop_callable_destroy`, `vs_interop_callable_is_valid`. |

Argument handles must be writable because successful calls commit final `ref`/
`out` values back to those handles. Value and `const ref` arguments are not
written back. Arguments are validated before the script executes, so invalid
arguments cannot fail after producing script side effects. A read-only value
borrowed from a native callback context must be cloned before it is retained or
passed to a nested call.

## Reflection, interception, and extension points

Sessions can export two JSON views:

- `vs_interop_session_export_bindings_json` contains functions, host types,
  members, container metadata, parameter flags, parameter names, documentation,
  and annotations for IDE, LSP, and static-checking adapters;
- `vs_interop_session_export_reflection_json` contains loaded-module types,
  fields, methods, globals, and declaration metadata for runtime tools.

An annotation vocabulary describes names and supported arguments; annotation
instances describe which declarations actually carry those annotations. They
are separate data concepts.

The session can also register:

- `vs_interop_session_set_instance_call_interceptor`, returning `UNHANDLED`, `HANDLED`, or `FAILED`; `proceed` uses the already-resolved target and overload;
- `vs_interop_session_set_mutation_callback` for field writes, container additions, removals, sets, and clears, with object type, field, value, and mutation generation;
- `vs_interop_session_apply_class_defaults` for host-provided class defaults;
- `vs_interop_session_register_generic_host_type_template` and `vs_interop_session_instantiate_generic_host_type` for synchronous callback-created host value types;
- `vs_interop_session_register_type_alias` for transparent host aliases;
- `vs_interop_session_register_external_serializer`, `vs_interop_session_serialize_object`, `vs_interop_session_deserialize_object`, and `vs_interop_session_enumerate_object_references` for external encoding, decoding, and recursive reference traversal.

An external deserializer can add named field assignments to a temporary
transaction but cannot access script object storage directly. Unknown host
package metadata is preserved byte-for-byte.

## Packages and hot reload

<a id="package-container-format"></a>

A `.vslib` is a Core library image. A raw Core application image is the compiled payload inside a standard ZIP `.vsapp` container. With Application support enabled, the package byte APIs also accept ordinary application ZIPs and application builds return ZIP containers; without it, application builds return raw Core images. For explicit host environment, signature or protection options, use the Application loading API. The package canonical signed bytes are the original container bytes for a retained application container, or Core canonical bytes for a raw image. Parsing does not establish publisher trust.

Interop can read, validate, build, and execute in-memory `.vsapp` and `.vslib`
packages:

- `vs_interop_package_read` performs core format, version, and structure validation;
- `vs_interop_package_get_kind`, `vs_interop_package_get_name`, `vs_interop_package_get_version`, `vs_interop_package_metadata_count`, and `vs_interop_package_get_metadata` read package information;
- `vs_interop_package_get_canonical_signed_bytes` returns platform-stable borrowed bytes; the host must verify signatures before crossing a trust boundary;
- `vs_interop_session_load_package` loads an application or library package;
- `vs_interop_session_reload_package` reloads transactionally and consumes the module only on success;
- `vs_interop_session_build_package_file` builds package bytes in memory.

Packages and bytecode caches produced by the previous development layout use an
older bytecode magic. Rebuild them from source before loading them with the
current runtime; `vs_interop_package_read` rejects the old layout.

Enable source hot reload with `vs_interop_session_set_hot_reload_enabled`.
`vs_interop_session_reload` and `vs_interop_session_reload_package` can stage a
replacement for initialization on the next execution. If
`VS_INTEROP_CAP_RELOAD_STATUS` is set, initialize
`vs_interop_reload_status.struct_size` and query
`vs_interop_session_get_reload_status` on the creator thread. `PENDING` means
the candidate is staged and the previous generation is still committed.
`ROLLED_BACK` restores the previous generation. `APPLIED_WITH_ERRORS` keeps the
committed candidate after a later execution or cleanup error. `RESET_REQUIRED`
requires clearing the session and loading the program again. If
`vs_interop_session_clear` reports an unsettled rollback, destroy and recreate
the session. Use `operation_id` to correlate snapshots across settlement.

Compatible globals, fields, and object identity can migrate. Incompatible
type changes, live channels, and capturing closures can prevent migration.
Release owned C values tied to the current module before replacing it.
Native side effects are outside the script transaction.

## Threads, borrowing, and destruction

Adapters must observe these boundaries:

- session creation, ordinary session APIs, and final destruction are confined to the creator thread;
- wrong-thread destruction returns `VS_INTEROP_STATUS_WRONG_THREAD` without consuming the handle;
- a retained callable with `owner_thread_only == 0` may be invoked from another thread only when the host serializes every access to the session; the session itself is still not thread-safe;
- callbacks triggered by script execution may originate on VM worker threads and may overlap, so foreign runtimes must acquire their own runtime lock and synchronize shared state;
- callback contexts, string views, container element views, and host metadata arguments are borrowed for the documented callback or operation lifetime; copy strings or clone values before retaining them;
- module-owned strings, bytes, and views must not be freed by the adapter; module-owned `vs_interop_buffer` objects are released only with `vs_interop_buffer_destroy`;
- foreign callbacks must not unwind exceptions across the C ABI; store the error in the context and return `VS_INTEROP_CALLBACK_ERROR`;
- value, object, and container lifetimes follow VeroScript reference semantics; copying a C structure is not a deep-copy operation.

## Capability negotiation and compatibility

`vs_interop_get_capabilities()` reports optional capability bits for packages,
hot reload, host bindings, binding JSON, reflection JSON, instance interception,
mutation callbacks, container values, retained callables, generic host types,
external serialization, JIT diagnostics, localization, distinct numeric
primitives, container enumeration, execution governance, reusable session-code
caching, explicit backend selection, and one-shot initialization. Check the ABI
first, then check capabilities before calling optional
exports. Capability discovery does not change `VS_INTEROP_ABI_VERSION`.

`vs_interop_get_capabilities()` advertises process API features, including
`VS_INTEROP_CAP_JIT_BACKEND_SELECTION`. Its global result does not set
`VS_INTEROP_CAP_NATIVE_JIT` or `VS_INTEROP_CAP_NATIVE_TYPED_LOOPS`: there is no
implicit backend. Query `vs_interop_session_get_jit_backend_info` and inspect
`native_code`, `VS_INTEROP_JIT_CAP_NATIVE_CODE`, and
`VS_INTEROP_JIT_CAP_NATIVE_TYPED_LOOPS` for the selected session backend.
The host platform string does not establish native capability. The ABI-0.1.1 appendages
`cost_model_vm_fallbacks` and `native_typed_loop_executions` in
`vs_interop_jit_stats` are visible only when the caller supplies a sufficiently
large `struct_size`; older callers retain their previous prefix safely.

Compatibility rules do not change existing signatures, enum values, or published
structure layouts. New structure fields are appended and guarded by
`struct_size`. Adapters can record `vs_interop_get_runtime_version` and
`vs_interop_get_host_platform` for diagnostics instead of hard-coding platform
layouts.

The SDK does not prescribe Python, C#, Rust, Unreal, or AOT policy and does not
bundle those runtimes. It depends on the public `VeroScriptCpp` embedding
surface without changing the language core. For browser-safe in-memory entry
points, see [WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/en-US/index.md). For the field-level binding catalog
format, see [Host bindings](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/bindings.md).

## Run modes

`VS_INTEROP_RUN_EVAL` keeps incremental evaluation semantics.
`VS_INTEROP_RUN_EVAL_ONCE`, advertised by `VS_INTEROP_CAP_EVAL_ONCE`, executes
the module initializer once without invoking `main` and allows the selected JIT
backend to execute initialization. `VS_INTEROP_RUN_SCRIPT` additionally invokes
`main`. Set the execution policy separately; without a selected backend, Mixed
uses the VM and forced JIT fails before execution.

In an ordinary session, each run initializes fresh state; EvalOnce does not
make subsequent runs skip initialization. Hot-reload sessions retain initialized
globals. Retained initialization uses the VM. Eligible execution uses the
selected backend.

## Explicit JIT selection

Execution policy and backend selection are separate. Without an installed module environment, Runtime supplies no default module: mixed uses the VM without a selected backend; forced jit fails before execution. Select an independently installed, compatible library file or directory with `vs_interop_session_set_jit_backend` while the Session is idle on its owner thread. `vs_interop_session_set_jit_backend(session, (vs_interop_string_view){NULL, 0})` disables selection. A failed replacement preserves the previous backend. Use `vs_interop_session_get_jit_backend` / `vs_interop_session_get_jit_backend_info` for actual selected identity/native capabilities; `backend-services` alone is the Core service contract. Debugging, retained initialization, and bounded cleanup use the VM. Governed Mixed defaults to VM unless `enable_controlled_mixed` is enabled; explicit JIT is independent of that switch. Eligible controlled entries can use JIT, with unsupported or VM-preferred entries falling back to VM while retaining the checks. File/directory discovery and ABI rules are described in the JIT guide.

Initialize the info structure’s struct_size before querying. Its text views are borrowed from the selected factory; copy them before changing backend selection or destroying the Session.

[JIT](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/en-US/index.md)

## Managed memory

Host API: `vs_interop_memory_*` / `vs_interop_session_set_memory_limits`. See the [`std::memory`](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md) reference for ownership, quotas, view expiry and leases.

Discover this additive extension with `VS_INTEROP_CAP_MEMORY`; existing layouts and ABI version remain unchanged. Supply exactly one context: an idle owner-thread `session`, or `call` during its native callback. Session reentry returns `BUSY`; callback operations use the active VM's governance checks. Initialize `struct_size` for limits, statistics and leases. Zero quotas are unlimited. Applying configuration, Run and Clear preserve the accounting for host-retained regions.

`from_array` requires uint8 elements; `to_array` and memory `clone` copy bytes independently. Address queries require `VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS` when policy is enabled; object IDs and bounded byte access do not. A write lease excludes competing byte access; all leases prevent resize and invalidation, and a read lease excludes byte mutation. Synchronize native work before releasing the token exactly once. Release is thread safe and remains valid after session destruction. Successful external registration adopts `owner`; its callback runs once on the final releasing thread. On failure the host retains responsibility for the owner. Keep its callback code and user data alive until final release.

## Compile annotations and diagnostic severity

Check `vs_interop_get_compile_capabilities()` for
`VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING`. Native C++ hosts create an opaque
service with `CreateAnnotationProcessingService` from `VeroScriptInteropAnnotation.h`
and a frozen registry, using the matching Core SDK and native compiler/STL ABI.
C11 hosts pass only the opaque handle. `vs_interop_annotation_service_retain`
returns another owned handle; release each with `vs_interop_annotation_service_release`.
Attach on the idle creator thread with `vs_interop_session_set_annotation_service`.
The session copies the configuration, so caller handles may be released immediately;
ordinary ApplyConfig preserves it, and NULL explicitly clears it. Configuration
changes from compilation or service-release callbacks are rejected. Keep native code
and callback data alive until all handles and attached sessions release the service.
Handles are process-local and cannot be serialized.

The existing diagnostic layout and C ABI 0.1.1 remain stable. Query severity with
`vs_interop_session_get_diagnostic_severity`: `VS_INTEROP_DIAGNOSTIC_SEVERITY_ERROR`,
`WARNING`, `INFORMATION`, and `HINT` use values 0, 1, 2, and 3 respectively with
the same prefix. Invalid or foreign-thread queries leave the output unchanged.
Notes can accompany successful compilation; use the result status and success flag.
Last-error text selects the primary error when notes precede a failure.

## variant

<a id="exported-value-lifetime"></a>

Exported script-owned values are registered as session roots. Read/use them on the creator thread. Releasing a wrapper on another thread queues cleanup for an owner safe point. Release all owned snapshots before replacing/reloading/clearing a module; closing the session invalidates module-dependent wrappers and runs language cleanup. Independent primitive boxes remain usable after close. No implicit mapping from C# object/dynamic or arbitrary Python objects is performed.

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

A known variant destination boxes a concrete value implicitly. Reading uses explicit cast<T> or try_cast<T>; variant_is<T> tests the exact stored type without conversion. optional<T> and try_cast<T> are Core builtins and require no core.vs import. cast<T> on an empty variant fails; try_cast<T> on an empty variant or ordinary conversion mismatch returns an empty optional. Copy failures, cancellation and limits remain errors.

Values stay HOST_OBJECT; numeric_kind verifies 0 other/unavailable, 1 bigint, 2 rational, and returns 0 for closed or foreign-thread script-owned values. Check VS_INTEROP_CAP_NUMERICS. numeric_parse/to_text/convert take exactly one context: an idle owner-thread session or the borrowed active native call context. Text has explicit UTF-8 length; to_text writes a string value. convert targets fixed-width integer/float/double VS_INTEROP_NATIVE_* tags, exact_float is 0 or 1, and failures preserve outputs. Initialize numeric_limits.struct_size and reserved=0; zero fields select finite Core defaults, not a process heap cap.

`vs_interop_session_set_numeric_limits`
`vs_interop_value_numeric_kind`
`vs_interop_numeric_parse`
`vs_interop_numeric_to_text`
`vs_interop_numeric_convert`

## Module-dependent value ownership

<a id="value-domain-confinement"></a>

Owning a value handle does not detach its payload from a session. Script objects/functions and mutable containers with module-value slots remain confined to the session creator thread and become unavailable when it closes, including empty containers. Host-created storage binds when first used by a session, and shared aliases/clones retain that ownership. VM worker callbacks may export only independent owned values; an empty container with variant/function slots is not independent. Release owned handles after use, including after session close.

## Session backend selection arguments

`vs_interop_session_set_vm_backend` / `vs_interop_session_set_jit_backend`

These Session setters accept a compatible file/directory or an installed backend ID when the supplied module environment provides its resolver. `auto` requests environment candidates; for VM it may use the retained product fallback. `none` or an empty native string disables selection; `{NULL, 0}` map to that empty request. On a new session, omitting a backend setting allows environment selection; it differs from explicitly clearing that setting. When reconfiguring an existing session, omitting the VM selection preserves its execution domain. A failed explicit selection preserves the previous backend. Set on the idle creator thread; VM changes must precede execution because the domain remains frozen after Clear/reload. JIT selection remains independent of VM/Mixed/JIT execution policy.

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

<a id="host-memory-ownership"></a>

## Shared host memory ownership

Create buffers, bounded views and byte snapshots through the Session memory API. Use the Session owner thread while it is idle; inside a registered native callback, use its active memory context. A copied value handle does not transfer its memory access domain.

Set the limits before allocating. Zero means unlimited. The per-buffer limit applies to capacity; the retained limit includes owned capacity and external bytes, including growth peaks. Changing limits below retained use fails without changing the configuration. Run, Clear and reload do not release host-retained regions or reset their accounting. These are byte-storage quotas, not a full heap limit or sandbox.

A write lease excludes competing byte access; all leases prevent resize and invalidation, and read leases exclude writes. Finish native work before releasing a lease. Lease release can run on another thread and remains valid after Session destruction, but new operations cannot use a destroyed Session. External registration retains the owner until the final alias or lease is released; invalidation expires every alias. Raw address observations do not preserve ownership.

Object IDs preserve uint64 identity and do not extend object lifetime. Address queries need the host memory-address capability when policy is enabled. Query original reference arguments only during their native callback; by-value copies have no original reference address. Keep all callback contexts within their callback scope.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
