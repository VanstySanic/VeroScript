# VeroScript C++ technical documentation

This directory documents the C++ modules as source-bound developer material. It
describes contracts that are implemented by the public headers, CMake targets,
tests, and generated artifacts. It is not a user guide and it must not restate
API names, command-line options, JSON keys, diagnostics, or code examples in a
translated form.

## Module ownership

The C++ group contains five modules:

- `VeroScriptCpp` is the high-level C++ embedding and session surface over the
  language core.
- `VeroScriptCppAot` emits a compile-checked VeroScript program as a C++ source
  bundle. The bundle keeps one translation unit so template instantiation,
  internal linkage, and initialization order remain explicit.
- `VeroScriptCppFrontend` parses the supported C++17 subset into the stable AST
  consumed by the source tools.
- `VeroScriptCppBindGen` converts representable C++ declarations into a
  Binding Catalog. The catalog is metadata; it is not a native invocation
  thunk.
- `VeroScriptCppTranspiler` lowers the supported, semantics-preserving C++17
  subset to VeroScript source and validates generated output with the real
  compiler.

The language-neutral core is `VeroScript/`. The C++ modules may depend on
the core, but the core must not depend on C++ tooling. The frontend is shared by
BindGen and Transpiler; those consumers apply different semantic policies after
parsing.

## Semantic boundary

The C++ tools intentionally accept a bounded subset rather than pretending to
be a general C++ compiler. Unsupported ownership, linkage, pointer, template,
preprocessor, and control-flow constructs must produce diagnostics. A change
that silently approximates one of these constructs is an ABI or source
correctness regression.

The frontend preserves qualified names, aliases, attributes, declaration
ranges, constructors, destructors, friend declarations, type-parameter
templates, and same-file out-of-class definitions when the AST can represent
them. BindGen may retain structured metadata that Transpiler must reject when
VeroScript has no equivalent source construct. These are separate policies and
must not be merged for convenience.

## Output and transaction rules

Multi-input tools parse and merge the complete input set before replacing any
output. Catalogs, API references, source maps, and manifests must be replaced
atomically as one logical result. Duplicate basenames receive deterministic
path-derived disambiguation. A failed parse, merge, validation, or final
VeroScript compilation must not leave a partial output tree.

Generated API references are documentation artifacts. They must never be
compiled as implementation sources, and the JSON Binding Catalog remains the
machine-readable input for compiler and IDE tooling.

## Concurrency and host contracts

Embedding and AOT hosts must keep callback state alive for the complete run and
must synchronize state that can be reached by worker threads. Script globals
and class static fields are isolated per native worker; `channel<T>` is the
explicit synchronized cross-worker capability. Native callbacks, interceptors,
mutation observers, and reachable AOT host symbols may run concurrently.

## Engine and session contract

`VeroScriptEngine` provides stateless `CheckSource`, `CheckFile`, `RunSource`,
and `RunFile` entry points. File checks and runs may use a bytecode cache;
source labels, input/output/error streams, circular-import policy, and
project-wide-access policy are explicit inputs.

`VeroScriptSession` is the stateful surface. Configuration, input streams,
native/host registration, loading, compiling, committing, reloading, clearing,
and execution-changing operations are rejected while a run is in progress.
`LoadFile` establishes the file-backed reload path; source-only modules cannot
be reloaded by path. `Reload`, `ReloadCompiledModule`, and `ReloadPackage` stage
and validate a replacement before committing it. Package kind and entry-point
shape must match, and a reload package must also match the loaded module kind.

`Run(ERunMode::Eval)` executes module initialization without calling `main`.
Cross-call globals persist only with `SetHotReloadEnabled(true)`;
`Clear()` removes loaded state, last-invocation JIT statistics, and reusable
session JIT code. `SetJitPolicy` applies to subsequent execution. Eligible session invocations can reuse compiled code across fresh VMs, while
`SetJitCacheLimits` and `GetJitCacheStats` expose bounded retained resources and
cumulative compilation for the current generation/configuration. Host calls, instance interceptors,
mutation callbacks, field/global access, object construction, callable
retention/invocation, external serialization, and reflection are separate
session operations and must keep their documented idle and lifetime rules.

## Public C++ AOT and tool outputs

The AOT API returns either one generated `.cpp` string or an
`FCppAotSourceBundle` containing relative source files and a manifest. BindGen
returns a merged catalog, optional API references, and project manifests.
Transpiler returns generated source and source maps, while project mode
additionally returns one file record per input and a manifest. The shared output
writer is atomic; output files are not replaced after a failed parse, merge,
lowering, validation, or path collision.

The frontend AST records source ranges, declaration attributes, aliases,
templates, C linkage, storage/semantic qualifiers, bodies, and out-of-class
definitions. `FTypeMapOptions` explicitly controls raw host pointers, rvalue
references, known host types, known enum types, known type parameters, and
known aliases.

## Detailed pages

- [C++ group architecture](architecture.md)
- [C++ embedding and session](session.md)
- [C++ frontend, BindGen, and Transpiler](frontend-and-tools.md)
- [C++ AOT emitter](aot.md)
- [C++ build and test contract](build-and-tests.md)

## Execution governance and session observability

`VeroScriptSession` exposes `SetExecutionLimits`, `SetExecutionControl`,
`RequestCancel`, and `SetCapabilities` in addition to the requested
`EExecutionBackend`. Limits use zero for unlimited fields, cancellation is
cooperative, and capability restrictions are enforced by the VM path so JIT
code cannot bypass them. `SetJitPolicy` and `GetLastJitStats` expose the
adaptive compilation contract without turning statistics into a version value.
Eligible invocation reuse follows the current generation and execution configuration.

The session call surface includes free-function and instance calls, constructor
invocation, field/global access, callable retention/invocation, reflection
enumeration, external serialization, and `CallMethodBypassingInterceptor`.
That bypass affects only the selected entry; nested calls made by the body still
follow interceptor dispatch. `RequestCancel` may be issued while a run is
active, but host callback state remains the host's synchronization
responsibility.

## Source of truth

The public headers under `Public/`, the implementation under each module's
`Private*` tree, the module CMake file, and the focused tests are the source
of truth. This document must be updated in the same change as any public
contract, output format, supported-subset rule, ownership rule, or concurrency
guarantee. Identifiers, paths, target names, diagnostics, and version values are
copied exactly from source and are never localized.

## Session JIT reuse

Session code-reuse eligibility, governed execution, retention limits and accounting are maintained in [C++ Session](session.md#session-jit-cache).

## Retained byte quotas

Zero `MaxNativeBytes` or `MaxRetainedPayloadBytes` permits no retained bytes of that category; these byte limits differ from the unlimited zero policy budgets.

## Cancellation and host delivery

See [C++ embedding and session — Cancellation and host delivery](session.md#session-control-and-streams).

Sessions sharing the same `FExecutionControl` token share its cancellation
state. Complete prior cancellation callers before `Reset()` and reuse, so an
old request cannot cancel the next execution that reuses the token.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
