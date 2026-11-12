# Interop values, callbacks, and packages

## Values and containers

The ABI exposes fixed-width scalar values, strings, enums, host objects, arrays,
maps, ordered maps, sets, queues, stacks, deques, and channels. Container
enumeration callbacks receive borrowed values. An element type or key/value type
must match the VeroScript type contract. Element value type validation returns
`TYPE_MISMATCH`; execution failures can return `ENGINE_ERROR`. No implicit
conversion is performed. Multiset counts sum occurrence-array lengths without
copying elements or allocating a traversal snapshot.

Object slots accept script-derived objects assignable to the declared base class or interface, using the source value's module metadata. For example, a `Child` implementing `IRead` and deriving from `Base` can be inserted into `array<Base>` or `queue<IRead>`. Container declarations remain invariant; this does not convert `array<Child>` into `array<Base>` or add numeric conversions.

Map/set key declarations are validated even for empty containers, including multimap/multiset variants. Unsupported builtin types (`bool`, `float`, `double`, 8-bit unsigned and 16-bit integers, containers, callables and references) return `INVALID_ARGUMENT` without replacing the previous value. Primitive keys are `char`, `int`, `unsigned int`, `int64`, `uint64`, `string` and `wstring`, including spelling aliases. Named enum/host identities, including instantiated host types, are resolved by the consuming session.

Channel capacity is configured before first use. Send, receive, try, timed, count,
capacity, closed, and close operations have separate status behavior. Closing a
channel wakes blocked operations; error text is returned in an owned UTF-8
buffer when the API provides one.

## Callback contexts

Native functions, generic host-type factories, instance-call interceptors,
mutation callbacks, and external serializers/deserializers report failure
through their context setters and callback result values. Callback exceptions
must be converted before returning through C. User data, host-object payloads,
and managed roots must remain alive for every possible callback entry.

Invalid ApplyConfig descriptors are rejected before application. An allocation/internal failure during Core application can leave settings partly applied; retry a complete configuration before relying on them. The C cancellation handle follows the control actually installed in the session on both success and failure. Linux allocation-failure regressions cover enabling/disabling control, cancellation consistency and retry recovery.

External serialization may also enumerate references through the ABI visitor
contract. The reference kind, visitor order, and borrowed-value lifetime are
machine-facing data; a visitor must not retain a borrowed value or mutate the
owning session reentrantly. Map-entry and set-element visitors follow the same
borrowed-view rule and must propagate callback failure without converting a
partial traversal into success.

Reentrant destruction or session clearing is not a general callback feature. If
the implementation rejects it, the ABI must return an engine error and preserve
the owning session rather than partially clearing it.

## Packages and reflection

Package APIs read and destroy package handles, expose kind/name/version,
metadata, canonical signed bytes, and load/reload/build operations. Reflection
exports JSON metadata for loaded types, fields, methods, globals, enums, and
interfaces. JSON field names and type names are machine contract and are not
localized.

`vs_interop_session_build_package_file` reports a failed build through a
structured diagnostic with `message_id` `diag.failed_to_build_package`. Its first
diagnostic argument is the underlying build reason; consumers should use
`vs_interop_session_format_diagnostic` for the selected locale rather than
treating `message` as a replacement for the diagnostic contract.
## Channels

Channel values are created with `vs_interop_value_create_channel` or
`vs_interop_value_create_bounded_channel`. A zero bounded capacity means an
unbounded channel. `vs_interop_value_channel_send` blocks until the value is
accepted, while `vs_interop_value_channel_try_send` is non-blocking and
`vs_interop_value_channel_send_for` applies a timeout. The receive family has
the matching blocking, non-blocking, and timed operations. Count, capacity,
closed-state, and close operations are separate ABI calls. Closing a channel
wakes operations that are waiting on it; hosts must treat the closed result as
terminal and must not reuse a borrowed value after its callback scope ends.

## Generic host-type context

`vs_interop_generic_type_context_*` functions expose the template argument count
and arguments to a generic host-type factory. The factory supplies the resulting
type, members, protocol declarations, and an error through the same borrowed
context. Context accessors and setters are valid only during the factory
callback. The callback must finish registration before returning; the context
is not a retained builder object.

## Calls and interception

`vs_interop_intercept_context_*` exposes the receiver, resolved target,
arguments, `proceed`, and the callback error channel. `proceed` is the
interceptor's continuation for the already-resolved call. A host that wants a
later inbound dispatch can use the session call API that bypasses the entry
interceptor; nested calls from the method body remain interceptable.

`vs_interop_call_context_*` exposes borrowed arguments, mutable arguments,
`host_this`, the program generation, and the error channel. Function and
callable invocation helpers preserve the session's reentry and lifetime rules.
Retained callables use `vs_interop_session_retain_callable`,
`vs_interop_callable_destroy`, `vs_interop_callable_is_valid`, and
`vs_interop_callable_invoke`; owner-thread-only and weak-`this` choices are lifetime policy, not
a cross-thread synchronization mechanism.

## External serialization contexts

Serializer contexts expose the registered type, mutation version, and borrowed
fields. Serializers copy output bytes with
`vs_interop_external_serialize_context_set_bytes`; failure preserves the previously
accepted payload. Deserializers read the byte payload and encoded version, then
add named field assignments to a temporary transaction. Both report failures
through their error setters and callback results.
Reference enumeration is a separate callback traversal and does not extend the
lifetime of its borrowed values. The managed and C++ adapters must copy bytes
or strings that need to survive the callback.

## Annotation name policies

Annotation vocabulary registered with `vs_interop_session_register_annotation` uses exact names by default. Set `name_flags` to `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE` to fold ASCII letters for that annotation definition. Argument names and metadata keys remain exact; source spelling is preserved by reflection. Overlapping exact/folded registrations are rejected without changing the catalog. Set `struct_size` to `sizeof(vs_interop_annotation_desc)`; incomplete descriptors and unknown flags are rejected. Rebuild the host against the matching current Interop header and runtime.

## variant

See [Interop SDK — variant](../../User/en-US/index.md#exported-value-lifetime).

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

A known variant destination boxes a concrete value implicitly. Reading requires an explicit cast<T> or try_cast<T>; variant_is<T> checks the exact stored type and does not convert. try_cast requires core.vs for optional<T>. Empty extraction fails; ordinary conversion mismatch returns an empty optional. Clone failures, cancellation and limits remain errors.
## Session ownership and callback snapshots

Mutable module containers exported to C retain their originating session even when currently empty. While the session remains open, shared C aliases and clones keep the underlying graph alive; releasing one handle does not destroy objects still retained by another. Cleanup becomes eligible at the owning session's safe point after the last retaining reference is released. This adds no session restrictions to independent scalar, host, or enum values.

C-created mutable containers that can hold module-dependent graphs bind when first passed to a session, even while empty. Workers may export only independent values into owned C handles; empty containers with `variant` or function slots are excluded, and external C storage with those slots cannot be forwarded for worker mutation.

Module-dependent object and container graphs keep their originating session when copied. Calls, fields, globals, hooks and serializers reject a foreign session's graph with `ENGINE_ERROR` before metadata access or mutation. Independent scalars and host values retain their normal transfer semantics. Borrowed native-call and deserialization contexts keep their original session during synchronous work in another session; their lifetime still ends with the original callback.

Until replacement-module initialization commits, module-dependent script results cannot be retained in owned C value handles. Assignment and clone attempts return `ENGINE_ERROR`; a rejected assignment preserves the owned destination's previous value. A borrowed callback return slot can pass the result back to the same script invocation, including `vs_interop_intercept_context_proceed(context, out_return)`. Temporary results are still retired at their owning session's safe points.

`vs_interop_value_reset` rejects borrowed callback arguments and return slots with `INVALID_ARGUMENT`. Resetting an owned handle, or replacing its value with an independent scalar, releases its previous session identity for independent use.

Map/set visitors may reset or destroy the owning source handle. Enumeration holds the registered graph until all snapshot views are gone; borrowed entry views remain valid only during their visitor call. Clone a view to retain it. Session execution and configuration reentry remain restricted.

Applying a complete configuration preserves numeric, memory and standard-library limits set through the dedicated setters. External-memory registration adopts its owner only after output publication succeeds; every failure leaves ownership with the caller. Execution exceptions reset valid result outputs to failure, and allocation failures retain `OUT_OF_MEMORY`.

Cleanup of a retired value may fail after its replacement or reset has committed. The operation keeps that published value and reports the cleanup error on the previous session's error channel; it does not retroactively fail publication.

C function values and retained callables use Core closure semantics: clones share one mutable capture environment, separate closure evaluations create independent environments, and reference captures can outlive their source frame. Local static storage belongs to its declaration in the executing program state and is shared across calls or closure instances within that state. Use `vs_interop_session_set_hot_reload_enabled` when state must persist across C calls; ordinary fresh executions initialize and clean up their own state. Its private slots are not exported as ordinary reflected globals. These language features use the selected VM/JIT through the existing C API.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](values-and-callbacks.md) · [fr-FR](../fr-FR/values-and-callbacks.md) · [ja-JP](../ja-JP/values-and-callbacks.md) · [ko-KR](../ko-KR/values-and-callbacks.md) · [zh-CN](../zh-CN/values-and-callbacks.md) · [zh-TW](../zh-TW/values-and-callbacks.md)
