# C# AOT

`veroscript aot --target=csharp` converts a compile-checked VeroScript program
into C# source. The generated application can be built normally or published
with .NET NativeAOT. It does not load `VeroScriptInterop` at run time and is
separate from the managed `VeroScriptCSharp` embedding SDK.

## Runtime boundary

The generated project targets .NET 8 or .NET 9. The host supplies the .NET SDK
and chooses framework-dependent, self-contained, or NativeAOT publishing at
build time. VeroScript source is checked as one module graph before C# source
is emitted; the generated source does not depend on a VeroScript installation
after generation.

## Single-file output

```powershell
veroscript aot --target=csharp app.vs -o Program.cs
```

Use `-I`/`--import-path` for application modules that are not beside the entry
file:

```powershell
veroscript aot --target=csharp -I modules app.vs -o Program.cs
```

The output is a C# source file, not an executable. Add it to an SDK-style
project targeting `net8.0` or `net9.0`, then build or publish it with `dotnet`.
The output file is replaced only after successful generation.

## Source bundle

For deterministic multi-file output, use `--output-dir` and `--bundle-name`:

```powershell
veroscript aot --target=csharp app.vs --output-dir generated --bundle-name App
dotnet publish generated/App.csproj -c Release -r win-x64 --self-contained true
```

The bundle contains `App.runtime.g.cs`, `App.program.g.cs`,
`App.entry.g.cs`, `App.csproj`, and `App.manifest.json`. The manifest records
the generated file list and compilation-unit order. The files are ordinary
partial-class units; no textual include or generation-time runtime dependency
is required.

<a id="aot-project-inputs"></a>

### Host inputs in generated projects

The generated `.csproj` uses `EnableDefaultCompileItems=false`: only its explicit `Compile` items are compiled. Add the original managed host declarations and the generated AOT bridge as additional project inputs; placing those files next to the project does not include them automatically. The bridge’s partial-class implementation must be compiled with the generated program in the same assembly. Preserve every generated compilation unit listed in the manifest. Live VM registration source is a separate SDK integration artifact.

Bundle regeneration replaces the generated project file. Reapply your extra `Compile` items after regeneration, or include the generated units and host inputs from an application-owned project. See the [binding tutorial](../../../../../../../Resources/Docs/Tutorials/en-US/csharp/01-csharp-source-tools.md) for concrete paths and the build sequence.

## Supported boundary and deliberate exclusions

The backend supports scalar and string values, mutable `pair`, `auto`, dynamic
arrays, maps, ordered maps, multi-maps, ordered multi-maps, sets, multi-sets,
ordered multi-sets, queues, priority queues, stacks, deques, free functions,
unconstrained open generics, defaults, variadics, overloads, writable `ref`
parameters, extension-call syntax, namespaces, `using namespace`, enums,
globals, imports, structured control flow, `switch`, labels and jumps,
script-value `throw`, typed/reference/catch-all `try`/`catch`/`finally`, checked
arithmetic, source line mapping, and the algorithm, numeric, and deterministic
random source modules. Function values and indirect calls, expression/block
lambdas, by-value/by-reference/mixed captures, `ref`/`in` callable parameters,
reference-returning delegates, script classes and value structs, fields,
constructors, instance/static/virtual methods, single class inheritance,
unconstrained generic object types, reference returns, and local reference
aliases are also supported.

This includes signed 8-bit `char` literals, same-type local declaration groups,
`do`/`while`, and classic `for` initializer or step expression lists. Ordinary
and raw strings preserve their exact VeroScript byte sequence, including
isolated bytes introduced by `\xNN`; Unicode newline scalars are emitted as
escapes so they cannot invalidate generated C# literals or source-location
comments.

Generic type parameters and lexically shadowing locals, fields, loop/catch
bindings, and lambda parameters receive hygienic C# names. An initializer is
emitted against the preceding VeroScript environment before its new binding is
visible, even where C# would otherwise bind it to itself or reject the nesting.

In a constructor field initializer list inside an open generic type, the C#
target supports direct `T`-to-`T` initialization. Constructing an unconstrained
field type parameter with zero or multiple arguments is rejected during AOT
emission; use a concrete field type or construct the value before passing it as
`T`.

Binding Catalog v1 host classes, structs, enums, constructors, methods,
fields/properties, static constants, scalar/string/enum/host values, `ref`/
`in`/`out`, and directly compatible primitive managed containers can be lowered
through `--binding-catalog` and the generated AOT bridge. The bridge uses direct
managed calls and does not require runtime reflection. Mutable struct instance
members and invalid writable `ref` targets are rejected rather than emitted
with lossy semantics.

Lambda parameters and return types may be explicit or inferred from a
`fn(...) -> T` target. Capture factories preserve VeroScript `[=]` snapshots;
`[&]` and explicit `&name` captures use managed closure cells. When a lambda
uses an instance member, a captured class `this` retains the receiver and sees
later field changes, while a captured struct `this` is a value snapshot. Nested
lambdas preserve the already captured receiver. A captureless lambda cannot
use `this`. Unsupported delegate shapes receive generated delegate declarations
when they can still be represented by the AOT contract.

These restrictions are deliberate: the emitter reports a diagnostic instead
of silently changing semantics.

## Deliberate exclusions

Interfaces and interface implementation, multiple concrete class bases,
non-public or struct inheritance, deterministic destructors, friend/
using-member declarations, constrained or defaulted generic type parameters,
member overloads that differ only by method constness or writable versus
read-only reference direction, precompiled bodies, map destructuring,
reference `foreach`, fixed-size native arrays, host operators/indexers,
generic host members, host-property increment/decrement, and host containers
requiring element conversion are rejected. Postfix increment/decrement is
limited to local or global variables. Side-effecting compound l-values and
host-property compound assignments with non-local receivers are rejected.
Duplicate flattened type names across modules must be renamed.

`concurrent.vs`, raw `channel<T>`, `task<T>`, and `task_group<T>` are not
supported by C# AOT. Use the [C# SDK](index.md) with the native VM when the
program requires the concurrency standard library.

## Choosing C# AOT or the C# SDK

Use C# AOT when a generated C# project or NativeAOT executable is the delivery
artifact. Use the [C# SDK](index.md) when the host needs the native VM, runtime
concurrency, dynamic loading, host bindings, reflection, interception,
mutation callbacks, packages, or JIT diagnostics.

For C# declarations, use the [C# frontend](frontend.md) and
[C# BindGen](bindgen.md) to produce the Catalog and, when needed, the
reflection-free AOT bridge consumed by this backend.

C# AOT floating-point to integer conversions truncate toward zero and check the binary value against the target range before casting. They preserve representable 64-bit integer values without decimal rounding. Unsigned targets reject negative inputs, including negative fractions; NaN, infinity and out-of-range values fail.

<a id="aot-floating-conversions"></a>

C# AOT preserves the core conversion through double when converting integers to float, including mixed arithmetic and compound assignments. Mixed floating comparisons use double operands; floating remainder raises the core error that modulo is only defined for integers.

## Runtime memory support

Standalone AOT currently does not support `std::memory` queries, buffers, views, or their types. Actual calls or use of these types produce an explicit generation error; importing unused `memory.vs` or `core.vs` remains supported.

[Runtime SDK](index.md)

## Declaration preparation and capture limits

C# generation uses Core declaration preparation, including out-of-line definitions and generated methods. Native hosts can supply annotation processors through `FCodegenOptions.AnnotationProcessing` when using `FCSharpCodegenBackend`; processor code must remain valid during generation.

Value-captured bindings are read-only by default. A `mutable` lambda preserves changes to its owned captures between calls; copied function values share the environment, while separate evaluations create independent environments. Value initializer captures evaluate once, from left to right, in the enclosing scope. Capturing a const binding by its ordinary name preserves its read-only status. Scalar local `static` variables initialize on first use and share storage by declaration, including across closures from the same lambda expression; generic instantiations and generated entry invocations have separate state. Failed initialization can be retried, and recursive initialization fails. Initialized reference captures, non-scalar local statics, and access to an enclosing function's local static from a nested lambda require the Core runtime path and are rejected by standalone C# generation.

Standalone C# AOT supports console capability queries, dimensions, color, cursor and clear operations. Redirected output reports no controls and emits no escape sequences. The entry boundary restores the original Windows console attributes and cursor state; other terminals reset style and show the cursor after style/visibility changes, including on script failure.

C# AOT shares one generated delegate type for equivalent primitive aliases in callable signatures, including reference parameters/returns and nested value containers. `long`/`int64` and `unsigned long`/`uint64` remain equivalent; signedness, width, nominal object identity and `ref`/`const ref` qualifiers remain distinct.

## User pair templates

User-defined `pair<T, U>` types use ordinary generic fields and methods. When this two-parameter template replaces the soft builtin, standalone AOT rejects map `entries()` and key/value pair iteration; use the VM backend for these operations. Iterating map values of a user pair type remains supported.

Options-aware file generation (`FCSharpCodegenBackend.EmitFromFile` and `EmitCSharpSourceBundleFromFile`) loads the entry and imports through the Core module loader. Immutable module environments and `SourceOverlays` apply to virtual entry paths as well. Standalone output rejects native package contracts that require a target runtime adapter.

The options-aware `EmitCSharpSourceBundleFromProgram` overload also uses the module environment's published binding catalog and rejects conflicting explicit catalogs.

The generated entry initializes script static data and globals in the compiler's recorded order, inside the execution error boundary before `main`. Initializer exceptions and `exit` use the same handling as entry execution. Early reads or references of non-`int` storage before initialization require the Core runtime: its raw zero value cannot be faithfully represented by typed C# fields. Startup checking follows statically known calls and direct callable bindings. Unresolved startup callable targets while such storage remains uninitialized are rejected; creating a deferred callable alone remains supported. Straight-line writes before an early read and ordinary `int` zero forward reads remain supported.

After Core compilation succeeds, excessively deep startup function-call and
type-initializer graphs are rejected with a diagnostic requiring the Core runtime.
This bounds the generator's native
inspection stack; ordinary recursion and sequential startup calls remain supported.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
