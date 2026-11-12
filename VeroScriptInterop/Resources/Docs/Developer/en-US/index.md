# VeroScript Interop technical documentation

`VeroScriptInterop` is the stable C11 process ABI used by non-C++ hosts. This
document is for binding maintainers. The ABI header, implementation, tests, and
generated consumer bindings are authoritative; this document must not invent a
managed-language abstraction that is absent from the C contract.

## ABI surface

The public header defines opaque session, value, callable, call-context, buffer,
package, interception, generic-host-type, and serialization contexts. It also
defines string and byte views, status and enum values, session options, source
overlays, runtime configuration, JIT policy/statistics, native signatures,
host-type descriptions, mutation events, and visitor callbacks.
The optional session JIT-cache capability adds an append-only policy switch,
retained-resource limits, and cumulative cache-context statistics while keeping
the ABI version at 0.1.1 and accepting the original 32-byte policy layout.

The exported functions cover runtime/version/capability queries, buffers,
locale catalogs, session creation and destruction, configuration, source/file
loading, execution, values, calls, packages, host bindings, interceptors,
mutation, reflection, and serialization. The exported names, layouts, calling
convention, enum values, and status meanings are ABI, not prose.

## Ownership and views

Opaque handles have explicit creation, borrowing, retention, and destruction
rules. A view is not an owner. A returned string or byte view must not outlive
the object that owns it, and a buffer must be copied before the owner is
released. Callback contexts are valid only for the documented callback call.
Bindings must make these constraints visible rather than relying on a garbage
collector or reference-counting assumption that the C ABI does not guarantee.

## Callbacks and threads

Callback results are returned through the ABI callback status contract. Callback
exceptions, failed conversions, and invalid user data must be reported without
leaking native resources. Host callbacks may be entered from script workers;
host-owned state therefore requires its own synchronization. Creator-thread
restrictions and callback reentrancy rules must remain aligned with the core and
each language adapter.

## Versioning

`vs_interop_get_abi_version()` reports the ABI contract. Runtime semantic
version, capability bits, package metadata, and ABI version are distinct values.
Do not place a pre-release numeric marker in user-facing version prose or treat
an ABI number as the product version. Additive fields and functions require a
compatibility decision; layout or enum changes require an ABI review and
consumer updates.

## Localization

Locale functions select and enumerate canonical BCP 47 catalog directories.
Locale discovery must not assume a fixed language count. Catalog keys, exported
function names, status codes, ABI values, diagnostics IDs, and version strings
are never translated.

## Session, value, and callback details

Session APIs distinguish owner-thread operations, idle-session operations, and
operations permitted during a run. Loading, clearing, reflection, package
management, configuration, and binding changes are rejected when their
preconditions are not satisfied. `vs_interop_session_copy_last_error()` is the
fallback for operation-level errors; structured diagnostics are read through
the diagnostic count, diagnostic arguments, and formatted-diagnostic APIs.

The value surface includes scalar values, strings, enums, host objects, arrays,
maps, ordered maps, sets, queues, stacks, deques, and channels. Enumeration
callbacks receive borrowed views. Channel capacity is configured before first
use; close wakes blocked operations, and send/receive failures return a status
plus an optional owned UTF-8 error buffer. Nested capability transfers and
invalid type conversions are engine errors rather than successful coercions.

Callback context helpers provide the supported path for returning an error from
native host functions, generic type factories, instance interceptors, and
external serialization/deserialization callbacks. A callback must not destroy
or clear its owning session reentrantly unless the implementation explicitly
reports the operation as an engine error. Callback user data, host-object
payloads, and managed roots must outlive every possible callback entry.

## Detailed pages

- [Interop ABI and lifetime](abi-and-lifetime.md)
- [Interop values, callbacks, and packages](values-and-callbacks.md)

## Source of truth

Use `Public/VeroScriptInterop.h`, the private implementation and tests,
the CMake package configuration, and the C# and Python adapters that consume the
ABI. Update this document with every ownership, layout, callback, threading,
status, locale, capability, or versioning change.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
