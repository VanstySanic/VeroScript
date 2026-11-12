# VeroScript WebAssembly technical documentation

This directory documents the UI-free WebAssembly SDK. It is bound to
`VeroScriptWebAssembly.h`, `VeroScriptWebAssemblyC.h`, the TypeScript bindings, the Emscripten
build, and browser tests. It does not document an editor, HTML application, CSS
framework, or frontend product.

## Public surfaces

The WebAssembly module has three related surfaces:

- The C++ `VeroScript::WebAssembly::FRuntime` API is the implementation-facing wrapper.
- The WebAssembly C ABI exposes opaque runtime handles, result structures, virtual-file
  inputs, language selection, available-catalog discovery, and run operations.
- The generated TypeScript module and declaration files are the browser-facing
  SDK surface and must remain in parity with the C and C++ contracts.

The WebAssembly runtime depends on the language core, the C++ host API, and shared
standard-library and localization assets. It must not introduce a second
language or bytecode version contract.

## Execution policies

See [WebAssembly ABI, artifacts, and release contract — Program and input capabilities](release-contract.md#web-execution-policy).

The published baseline does not advertise script-level worker threads. An
unbounded script is stopped by terminating its owning Web Worker. Interactive
input suspends the runtime while the provider awaits input; only one
interactive run may be active on one Wasm module.

## Artifact and binding rules

The standard library and localization catalogs are embedded in the generated
virtual filesystem. The release artifact set, generated declarations, and
binding parity checks are a single contract. A change to `VeroScriptWebAssemblyC.h`,
TypeScript bindings, result fields, backend names, language tags, or generated
file names requires updating the parity and browser tests together.

Language selection uses canonical BCP 47 tags discovered from the embedded
catalogs. The implementation must not hard-code the number of supported
languages. Catalog keys, API identifiers, backend names, diagnostics, and
version values are never translated.

## Runtime API details

`createVeroScriptRuntime()` creates the Asyncify-capable runtime for interactive
input, while `createFastVeroScriptRuntime()` is the preferred low-latency
factory when all input is supplied up front. `runSource` is synchronous after
module creation; `runSourceInteractive` returns a promise and keeps VM/script
state suspended while the provider awaits input. Only one interactive run may
be active on one Wasm module. Provider exceptions reject the promise after the
runtime leaves its input wait safely.

`runSource` accepts a file label, optional source overlays, standard input,
script arguments, and a selected backend. Entry points `void main(string[]
args)` and `int main(string[] args)` are supported; a non-zero integer result is
still a successful run and is returned as `exitCode`. Embedded NUL input is
rejected. Spawn is not part of the published browser baseline, so an attempted
script worker reports the structured failure diagnostic rather than hanging the
Wasm module.

When the artifact advertises `supportsCompiledPrograms`, `compileSource` parses
and compiles a source graph once and returns an owned `VeroScriptProgram`.
Repeated `run` calls use fresh execution state while reusing the compiled
program; `runInteractive` is available only on the Asyncify-capable artifact.
Programs must be disposed before their runtime, and runtime disposal releases
any remaining program handles. Compiled-program support is an artifact
capability, not a new bytecode or language version.

## Detailed pages

- [WebAssembly runtime and bindings](runtime-and-bindings.md)
- [WebAssembly ABI, artifacts, and release contract](release-contract.md)
- [WebAssembly build and artifact contract](build-and-artifacts.md)

## C ABI runtime and program handles

The C ABI runtime handle is controlled by one caller at a time. The pointer
returned by a result-producing call is owned by the runtime until the next
operation that replaces that result or until runtime destruction. During an
Asyncify input wait, stateful getters and text operations are unavailable;
hosts must retry after the interactive call completes.

vero_web_supports_threads reports whether the artifact was built with
pthread-backed script concurrency. The default browser artifact instead uses
independent Web Workers for concurrent programs. This capability is separate
from the supportsInteractiveInput and supportsCompiledPrograms SDK flags.

The context entry points accept borrowed virtual-file path/source pairs. The
compile entry point creates a runtime-owned program handle; run_program and
run_program_interactive reuse its compiled module with fresh execution state.
vero_web_destroy_program releases that handle and must be called before a
program-owning runtime is discarded when deterministic cleanup is required.

## Source of truth

Use the public WebAssembly headers, `Bindings/`, the native WebAssembly implementation, the
Emscripten build scripts, artifact audit, declaration parity checks, smoke tests,
and browser tests as the source of truth. This document must change with any
WebAssembly ABI, generated artifact, execution policy, input-provider, cancellation, or
asset-packaging change.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
