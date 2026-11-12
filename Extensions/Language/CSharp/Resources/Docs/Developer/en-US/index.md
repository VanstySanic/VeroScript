# VeroScript C# technical documentation

This directory is the developer documentation shared by the C# language group.
It is bound to the C# source, the Interop header, the CMake targets, and the
managed tests. It is not an end-user tutorial or a duplicate of the public API
XML documentation.

## Module ownership

The group includes these modules:

- `VeroScriptCSharp` is the NativeAOT-compatible .NET 8/9 runtime surface. It
  exposes sessions, values, packages, callbacks, host bindings, containers,
  reflection, and external serialization over `VeroScriptInterop`. It also owns
  optional Android/iOS import profiles, MAUI integration and mobile checks.
- `VeroScriptCSharpAot` provides the C# source and NativeAOT-oriented emitter
  surface and depends directly on the language core.
- `VeroScriptCSharpBindGen` reads supported C# declarations and emits Binding
  Catalog metadata plus registration-oriented generated code.
- `VeroScriptCSharpFrontend` provides the shared semantic declaration model for
  C# build-time tools.
- `VeroScriptCSharpTranspiler` converts supported C# semantics into VeroScript.

The managed adapter must use the stable C ABI. It must not include C++ private
headers or reproduce compiler/runtime behavior in managed code.

## ABI and lifetime rules

Interop handles are opaque ownership objects. Every lease, buffer, package,
callable, callback registration, and session operation must follow the lifetime
rules declared by `VeroScriptInterop.h` and mirrored by the managed wrappers.
Do not expose a pointer, borrowed string, or callback context beyond its
documented lifetime. Dispose operations must be deterministic and safe on the
thread required by the underlying contract.

The C# layer is responsible for marshaling UTF-8/UTF-16 values, native buffers,
status codes, diagnostics, and callback exceptions without changing their
meaning. A wrapper may improve managed ergonomics, but it must not translate an
ABI error into success or silently discard a failed operation.

## Frontend and generator boundary

The frontend parses declarations without executing user code. BindGen consumes
the frontend model and emits metadata; it does not create native invocation
thunks. Unsupported signatures, ownership patterns, default arguments, and
inheritance forms must fail with source diagnostics before generated output is
replaced.

The AOT surface is a separate lowering path. Its accepted language and output
contract must be documented from its emitter and tests, not inferred from the
runtime wrapper.

## Compatibility rules

The C# public surface is governed by the checked API contract and package
verification tests. Changes to public types, marshalling layouts, enum values,
callback calling conventions, package metadata, or target frameworks require a
corresponding compatibility review. Version values, ABI identifiers, JSON keys,
CLI options, and C# identifiers are technical tokens and are never translated.

## Managed runtime details

`VeroScriptRuntime` is the process-level compatibility gate. It exposes the
reported ABI version, runtime semantic version, host platform, capability bits,
current language, available-language JSON, and catalog load/merge operations.
`EnsureCompatible()` must run before a session is used; the package version and
the ABI version are different contracts and must not be compared as one value.

`VeroScriptSession` owns the native session handle and the managed callback and
host-object roots associated with it. Values may be owning handles or borrowed
views. Borrowed values are valid only inside their borrow scope and must not be
stored, disposed, or passed to an operation after the scope ends. Container
enumeration captures callback exceptions and reports them after native
enumeration has returned; a managed exception must not cross the unmanaged
callback boundary unchecked.

`VeroScriptCallable` records its creator thread when owner-thread mode is used.
Disposal during an active invocation is deferred until the invocation releases
the native handle. This is a lifetime guarantee, not permission to invoke a
creator-thread-only callable from another thread. `VeroScriptPackage` owns a
native package handle, exposes metadata and canonical signed bytes, and tracks
whether its compiled module is still available for session loading.

## Binding and generated-code details

Managed host bindings preserve declaration documentation, annotations, type
parameters, parameter metadata, enum underlying types, const/mutable method
flags, operators, clone/equality/hash protocols, subclass factories, and
generic-host-type callbacks. The generated registration model is metadata plus
callbacks; it does not turn a catalog into a native function pointer table by
itself.

## Detailed pages

- [C# runtime and Interop](runtime-and-interop.md)
- [C# bindings, packages, and compatibility](bindings-and-packages.md)
- [C# toolchain and package contract](toolchain-and-packaging.md)

## Managed execution contract

`VeroScriptConfiguration` carries source overlays, import and cache settings,
call-depth, `MaxInstructions`, `TimeoutMilliseconds`, execution-control
enablement, capability policy, allowed script capabilities, and
`VeroScriptExecutionBackend`. Zero instruction and timeout limits mean
unlimited execution. `RequestCancellation` is the cooperative cancellation
entry point and is the only session operation designed to be called from a
non-owner thread; the native session must remain alive until it returns.

`VeroScriptJitPolicy` and `GetLastJitStats` expose adaptive compilation policy and the
most recent run snapshot. Eligible ordinary function calls reuse compiled code across
fresh VM state by default. `SetJitCacheLimits` and `GetJitCacheStats` expose bounded
retained resources and cumulative context statistics when `SessionJitCache` is
advertised. Unsupported JIT entries fall back to the VM. An attached debugger forces
VM execution. Retained initialization also uses the VM, while eligible calls can reuse
code within the same generation.

The managed surface also exposes channels through `VeroScriptValue`, retained
callables, instance-call interception, mutation callbacks, generic host-type
factories, external serializers, reference enumeration, reflection JSON, and
binding JSON. These wrappers preserve the Interop callback-scope and ownership
rules; they do not make borrowed values or owner-thread-only callables safe to
retain or invoke on arbitrary threads.

## Source of truth

Use the files under `VeroScriptCSharp/`, the frontend and BindGen projects, the
Interop public header, CMake configuration, and managed tests as the source of
truth. Update this documentation with the same change as any ABI, ownership,
threading, generated-code, or package-contract change.

Mobile-enabled packing uses this same project and package identity: `VeroScriptIncludeMobile=true` includes desktop .NET 8/9 and both .NET 10 Android/iOS targets with the verified native SDK.


[CSharp on Android and iOS](mobile.md)

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
