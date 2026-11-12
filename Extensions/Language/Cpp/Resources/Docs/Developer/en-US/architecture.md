# C++ group architecture

## Dependency direction

The C++ group is an adapter and toolchain layer above `VeroScript/`.
`VeroScriptCpp` provides embedding and session operations. `VeroScriptCppAot`
depends directly on the core. `VeroScriptCppFrontend` owns the shared C++17
declaration model; `VeroScriptCppBindGen` and `VeroScriptCppTranspiler` consume
that model independently. Neither consumer is allowed to change the frontend
AST to make its own lowering policy convenient.

The core does not include C++ tool headers. `VeroScriptInterop` is a separate
stable C ABI and is not an implementation dependency of the C++ frontend,
BindGen, or Transpiler. The C++ embedding layer may use the core's public
runtime and binding interfaces.

## Native debug boundary

`VeroScriptSessionDebug.h` defines the C++ Session bridge consumed by a host or a
Tooling live domain. Session owns bridge registration and scopes shared debugger
ownership around its actual execution VMs. Code replacement/rollback and teardown
notify the bridge; teardown VMs deliberately do not inherit its debugger.
The boundary exports borrowed program/binding/source and execution-context data.
The host copies synchronized metadata and services queued debug commands on the
execution thread at a stopped safe point. There is no Cpp-to-Tooling dependency.
Tooling owns the domain registry, DAP adapter and transport lifecycle; UE owns
editor/PIE, object lifetime integration and game-thread pause policy.

`VeroScriptCppSessionDebugTests` verifies live argument mutation, object calls,
retained callbacks, interpreter selection, reload rollback, exception cleanup,
bridge removal/replacement and protocol-thread command scheduling. This public
boundary changes the Session's C++ layout: rebuild native hosts with the SDK.
`VeroScriptCppJitContractTests` uses an independent public factory implementation
to verify explicit backend selection through configuration and stream entries,
and transitions from warmed code to real VM debugging and back after disconnect.
It does not depend on a concrete JIT implementation.
The installed SDK consumer exercises debugger attachment, live local mutation,
and subsequent execution after removal using exported headers and libraries.
TCP example tests use an isolated child process with bounded I/O and shutdown;
they do not detach a server thread borrowing local Session state.

## Source layout

Each native module exposes supported headers from `Public/` and keeps
implementations and tests under `Private/`. CMake recursively discovers
the private sources, excludes focused test sources from the library target, and
creates test executables separately. A public header must compile in the public
header tests without depending on a private include path.

## Shared output model

The frontend reports diagnostics with a code, message, source location, and
token/declaration data. BindGen and Transpiler preserve those diagnostics in
their result objects. Project operations collect all inputs before producing
catalogs, references, source maps, or manifests. Output helpers reject paths
that refer to the same file and replace files only after the full operation
succeeds.

## Change rules

Changes to a public struct, enum, source location, generated file format, CMake
target, or supported-subset rule require updates to the corresponding focused
tests and the relevant topic page. A README may explain a command, but the
public header and tests are authoritative for behavior.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](architecture.md) · [fr-FR](../fr-FR/architecture.md) · [ja-JP](../ja-JP/architecture.md) · [ko-KR](../ko-KR/architecture.md) · [zh-CN](../zh-CN/architecture.md) · [zh-TW](../zh-TW/architecture.md)
