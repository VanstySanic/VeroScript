# VeroScript core technical documentation

[Replaceable VM backends](vm-backends.md)

This directory documents the language core and its native implementation. It is
for maintainers and module authors. It is not a language tutorial or a runtime
user manual. The public headers and tests define the supported contract; this
document records the architecture and the rules that must remain visible when
the implementation changes.

## Pipeline

The core owns the language front end, AST, diagnostics, module loader,
preprocessor, semantic compiler, bytecode, VM, code generation, JIT, packages,
reflection, host bindings, external serialization, concurrency, debugging,
hot reload, and locale selection. The normal pipeline is:

1. Resolve source files, overlays, imports, and package inputs.
2. Tokenize and preprocess while preserving source locations.
3. Parse declarations, expressions, statements, types, and metadata into the
   public AST model.
4. Perform semantic analysis, type resolution, overload and generic handling,
   ownership/lifetime checks, and module visibility checks.
5. Emit bytecode and debug metadata, then link package/module information.
6. Execute through the VM or a policy-selected JIT backend.

Failure at any stage must preserve structured diagnostics and source locations;
it must not silently produce a different program.

## Runtime invariants

`VeroScriptVm`, `FValue`, object/container handles, function values, host values,
and references form one ownership model. Destruction, references, mutation
events, external serialization, and retained callables must preserve object
identity and lifetime. A container mutation callback observes a completed
mutation; an exception from the callback does not roll back the mutation.

Script globals and class static fields are isolated per native worker. Channels
are the explicit synchronized cross-worker capability. Task registries and task
IDs are worker-local. Creator-thread restrictions on sessions and retained
callables are part of the embedding contract.

## Execution backends

See [Core architecture and compilation pipeline — Backend selection](architecture-and-pipeline.md#core-vm-ownership).

## Public and private boundaries

`Public/` is the supported native include surface. `Private/`
contains compiler, parser, VM, JIT, and implementation details. External
consumers and language adapters must use public headers. A small set of
source-tree embedding, tooling, and AOT implementation targets receives private include
paths explicitly from CMake; those are reviewed friend dependencies, not an
installed or supported API, and the allow-list must not grow casually.

The native C++ SDK is a build-matched static-library contract: consumers rebuild
with the exact headers, library, architecture, runtime, and build identity.
Cross-build C++ binary-layout compatibility is not promised. The stable process
ABI is the C11 `VeroScriptInterop` surface. Changes to public C++ layouts, enum
values, bytecode formats, host binding records, diagnostic identifiers, or
package metadata still require compatibility review.

## Localization and assets

The core loads runtime and tooling catalogs from the shared localization asset
root and uses canonical BCP 47 directory names. Locale discovery is directory
and catalog based; the number of locales is not a compile-time constant. Source
identifiers, diagnostics IDs, JSON keys, API names, paths, version values, and
code samples are technical tokens and are not localized.

## Session, packages, and host bindings

The public session surface separates loading source/files/packages, compiling
without committing, committing a compiled module, running, clearing, and
reloading. File-backed reload preserves the reload source path; source overlays
do not create a reload path. Package loading validates the application/library
shape before it becomes the loaded module. Hot reload is opt-in and preserves
the state that the implementation explicitly marks as reloadable; a failed
reload restores the pending rollback state instead of replacing the working
module with a partial result.

Host binding registration is staged through the catalog: native functions,
host types, generic host-type templates and instantiations, members, operators,
clone callbacks, inheritable types, scoped enums, and subclass factories are
validated before use. Reflection enumerates loaded types, fields, instance
methods, free functions, globals, enums, and interfaces without exposing
private compiler structures.

The module loader is also a public integration boundary. Import paths,
implicit imports, stdlib search roots, source overlays, `-D` defines,
predefined macros, host type names, circular-import policy, and project-wide
access policy are explicit inputs. Normalized preprocessor settings contribute
to the source fingerprint. Adding a stdlib root may merge a sibling
`localization/` tree, so a packaged stdlib and its diagnostics remain a single
resource unit rather than two unrelated paths.

Bytecode metadata is more than executable instructions. The module format
retains interfaces, scoped enums, declaration metadata, reference-parameter
flags, closure capture schemas, exception regions, global initializer ranges,
package ownership, exported declarations, and embedded template modules. These
records support reflection, hot reload, source-free package loading, and
tooling; a codegen optimization must not discard them when the corresponding
behavior is observable.

## Detailed pages

- [Core architecture and compilation pipeline](architecture-and-pipeline.md)
- [Core execution, state, and packages](execution-and-state.md)
- [Core JIT and concurrency](jit-and-concurrency.md)
- [Core build and runtime assets](build-and-assets.md)

## Execution governance and JIT observability

The VM exposes cooperative execution governance through `FExecutionLimits`,
`FExecutionControl`, and `FScriptCapabilities`. Instruction and wall-clock
limits use zero for unlimited execution. Cancellation is cooperative:
`RequestCancel` marks the shared control object, but cannot forcibly terminate a
native or managed callback already executing on the host thread.

Capability policy controls standard-library side effects such as file,
environment, console, and task operations. An active restriction must be
enforced by the VM rather than bypassed by JIT code. The requested backend is
`Vm`, `Mixed`, or `Jit`; debugger attachment, retained initialization ranges, bounded cleanup, and execution
governance that requires interpreter enforcement select the effective VM path.

JIT policy and `FJitStats` are public observability contracts. Statistics
distinguish compilation, cache, dispatch, fallback/failure, budget, entry-kind,
and fast-path activity. Unsupported compilation is a normal fallback decision,
not a language diagnostic unless execution itself fails.
## Compiler model updates

The parser/compiler model now preserves static type modifiers and static data
members as module-owned backing globals. Static access resolves through the
declaring type hierarchy and interfaces, while generic instantiations receive
their own substituted static storage. Static-type restrictions are semantic
rules enforced by the compiler, not an emitter convention.

The AST also retains lambda capture metadata, generic type parameters and
bounds, aliases, declaration annotations, and source ranges needed by
diagnostics and tooling. Any lowering path that consumes the AST must preserve
these identities and source locations; unsupported capture or generic forms
must fail explicitly rather than being approximated.

## Source of truth

Use `Public/`, the corresponding `Private/` responsibility tree,
the core CMake target, focused runtime/compiler/VM/JIT tests, and the deployed
standard-library and localization assets. Update this document with every change
to semantic behavior, execution policy, ownership, public ABI, bytecode/package
format, diagnostics, or runtime asset loading.

- [Localization resources and deployment](localization-and-resources.md)

- [Managed memory implementation contract](memory-runtime.md)

- [Compile processing and source metadata](compile-processing.md)
- [Script compile extensions](script-compile-extensions.md)

- [Application i18n runtime contract](i18n-runtime.md)

- [Module environments and native extensions](module-environments.md)

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
