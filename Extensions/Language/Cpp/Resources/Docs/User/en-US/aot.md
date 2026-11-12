# C++ AOT

The AOT backend translates VeroScript into C++17 source for a native build. It
is suitable for deployments that do not need dynamic script loading at runtime
and want the host compiler to produce the final executable or library.

## Command-line generation

```powershell
veroscript aot script.vs -o generated.cpp
```

The output is a C++ translation unit, not a linked executable. It includes the
required compatibility support code. Compile and link it with a compatible
C++17 toolchain and the required system libraries. Host bindings also require
the corresponding host implementations.

## Programmatic API

`VeroScriptCppAot` exposes two levels of generation:

- `EmitCppFromSource` and `EmitCppFromFile` generate one C++ source string or
  write one output file. File output is transactional, and source and output
  paths must be different.
- `EmitCppFromProgram` emits from an already compiled `FProgram`.
- `EmitCppSourceBundleFromProgram` emits an `FCppAotSourceBundle` containing
  `Files` and `ManifestJson`.
- `BuildCppSourceBundle` reconstructs a bundle from generated C++ source.
- `FCppCodegenBackend` exposes the standard code-generation backend named
  `cpp` with the `.cpp` extension.

Bundle names accept ASCII letters, digits, `_` and `-`. Windows device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`) are rejected without regard to case on every platform, so all generated files remain portable. Names such as `CON_model` and `COM10` remain valid.

Source APIs accept import paths, defines, known host type names, implicit import
paths, and the circular-import and project-wide-access policies. The program
API is appropriate when the host already owns compilation and wants to reuse
the validated program.

An environment without a binding catalog can still use explicit host bindings through `FCodegenOptions::Bindings`. Preparation and verification use the same catalog; the target project must implement the generated free function declarations. Standalone AOT continues to reject programs requiring runtime native package contracts.

## Source bundles

An `FCppAotSourceBundle` is a deterministic source distribution. Its entry
`.cpp` file reconstructs one translation unit from runtime and program
`.generated.inl` fragments; the manifest declares the minimum C++ standard and
thread-library requirement. Do not compile the fragments as separate
translation units or treat the bundle as a set of independently linkable
modules.

The single-translation-unit layout preserves template instantiation, internal
linkage, and global initialization order. A bundle is useful for build systems
that want generated files and metadata as one atomic artifact.

Top-level script globals and class static fields retain VeroScript's per-worker
isolation in generated C++. Global storage is emitted behind accessors with
function-local `thread_local` state. The first access on a worker initializes
globals in script declaration order; the generated entry point establishes that
state before calling `main`. A worker that reaches a global later receives its
own initialized state, so host code must still synchronize explicitly shared
state through the documented concurrency and binding boundaries.

When a lambda in a member function uses the receiver, generated C++ preserves
the language semantics: a class receiver is retained as a shared object and
continues to observe field mutations, while a struct receiver is captured as a
value snapshot. Nested lambdas carry the already captured receiver. A
captureless lambda cannot use `this`, and a lambda that captures a class
receiver from a destructor is rejected because the receiver is already entering
destruction.

The emitter may use automatic storage for a class local when analysis proves
that the object cannot escape its lexical scope. A returned object, a reference
to an object member, or an escaping closure keeps shared ownership instead.
This is a generated-storage optimization; it does not change class identity,
destructor behavior, or the language lifetime rules.

## Supported scope and boundaries

The backend covers the public language surface, including classes, structs,
interfaces, inheritance, virtual dispatch, generic functions and types,
interface constraints, containers, overloads, default and variadic parameters,
reference parameters, lambdas, enums, operators, `nullptr`, `switch`,
`foreach`, labels, jumps, and the documented string, math, file, and path
libraries.

Scalar numeric `++` and `--` evaluate their target once, keep temporary owners alive during the operation, and preserve VM promotion, checked narrowing, and overflow behavior. Overloaded `++` and `--` on user types are currently unsupported; use an explicit update method for those types.

In a constructor field initializer list inside an open generic type, the C++
target supports direct `T`-to-`T` initialization. Constructing an unconstrained
field type parameter with zero or multiple arguments is rejected during AOT
emission; use a concrete field type or construct the value before passing it as
`T`.

VM and AOT share the source-language rules but not every execution boundary.
The VM is the better fit for dynamic loading, debugging, and hot reload. AOT
uses host threads for generated concurrency; validate host bindings, callback
lifetime, synchronization, and deployment-specific runtime services in the
target project.

## Current syntax coverage

C++ AOT also emits signed 8-bit `char` literals, same-type local declaration
groups, `do`/`while`, and classic `for` initializer or step-expression lists.
It preserves lexical shadowing: every declaration initializer is evaluated in
the preceding VeroScript environment before the new binding becomes visible.
The generated C++ may use hygiene temporaries or distinct local names to keep
that source rule intact; those implementation names are not part of the AOT
ABI or a source-level behavior change.

## Runtime memory support

Standalone AOT currently does not support `std::memory` queries, buffers, views, or their types. Actual calls or use of these types produce an explicit generation error; importing unused `memory.vs` or `core.vs` remains supported.

[Runtime SDK](index.md)

## Reference returns and evaluation lifetime

<a id="reference-return-lifetime"></a>

When a reference-returning call is read as a value, AOT copies that value before releasing the saved receiver, including calls without arguments. Reference binding stays separate from value conversion and generic deduction. Numeric references use the same promotion, narrowing checks and overflow checks as ordinary values. Assignment through a reference-returning method evaluates the receiver and arguments, invokes the method once, then evaluates the right side.

A local alias may retain receiver storage for direct class field access or for a resolved, non-overridable method that directly returns its own non-static, non-reference field. The receiver, its bases, fields and known derived types must not require script cleanup or own opaque resources. Rebinding the original receiver does not invalidate such an alias. References to ordinary global storage and direct struct receivers remain supported.

Retaining field storage in the VM does not postpone script destruction. AOT therefore explicitly rejects these class reference paths when managed cleanup is required. Returning a locally retained alias, an unproven reference origin, or a reference escaping retained callable storage also requires the VM backend; the generator reports the unsupported case.

Persistent indexed aliases and indexed reference returns require managed storage paths. Persistent references obtained from temporary value receivers also require managed owners. These cases are explicitly rejected by AOT; use the VM backend.

Implicit struct fields, including generic fields and elements of default-initialized fixed arrays, use normal default-constructor selection and execute the selected constructor body. Explicit field initializers construct their values directly without first default-constructing them.

## Compile processing and source metadata

`FCodegenOptions::CompileProcessing` configures source and declaration processors. Preparation preserves each processor instance across phases, and prepared programs are emitted without running their processors again. Compile-time source metadata and generated declarations can feed C++ AOT. Runtime source metadata queries (annotations, comments, declaration/source nodes and annotation definitions) require the VM; standalone C++ emission reports an explicit error.

## Script extensions and source overlays

`FCppCodegenBackend` forwards `FCodegenOptions::ScriptCompileExtensions`, `SourceOverlays` and `StdlibSearchPaths` to the core loader. `EmitFromFile` uses the same entry overlay resolution as Core/Session, including entries supplied entirely in memory; an empty path is rejected. `EmitFromSource` uses its explicit source argument for the entry and overlays for dependencies.

Compiler scripts execute only during generation. The emitted business C++ does not retain their VM or require compiler-only bindings at runtime. Business host bindings still require their target implementations. Configure compiler-only bindings, controls, limits and dependencies as described in [embedding](embedding.md); runtime source metadata queries still require the VM.

## User pair templates

User-defined `pair<T, U>` types use ordinary generic fields and methods. When this two-parameter template replaces the soft builtin, standalone AOT rejects map `entries()` and key/value pair iteration; use the VM backend for these operations. Iterating map values of a user pair type remains supported.

<a id="cleanup-container-operations"></a>

Standalone C++ AOT rejects `array<T>.remove` / `remove_at` and indexed assignment to `map<K,T>` / `ordered_map<K,T>` when struct `T` requires script cleanup, including cleanup owned by its fields or bases. These restrictions apply to the listed operations; VM, Mixed and JIT fully support them.

Standalone C++ AOT supports retaining or publishing a class's `this` during successful construction. If construction fails while another reference still retains the receiver, the generated program reports a fatal error instead of resuming through a script catch with delayed subobject cleanup. Use the VM or Core AOT backend when this case requires managed recovery. Temporary references released during unwinding do not trigger this restriction.

<a id="standalone-file-io"></a>

Supported standalone C++ file operations accept UTF-8 paths and reject embedded NUL. On Windows, long absolute paths, including complete UNC paths, receive the native long-path handling; relative paths remain relative and are not automatically given an extended-path prefix. Filesystem, permission and operating-system limits still apply. This is separate from the lexical path-query rules in [the foundation reference](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/foundation.md).

Indexed reference arguments also require Core-managed element identity. Standalone C++ source AOT rejects them instead of rebinding a reference to replacement storage or accepting a missing element after later arguments run. Use Core AOT, VM, Mixed or JIT for these calls; ordinary indexed reads and assignments remain supported.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
