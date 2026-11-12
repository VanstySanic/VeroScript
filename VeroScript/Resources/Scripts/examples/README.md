# VeroScript examples

> **Documentation:** [English](../../Docs/User/en-US/examples.md) · [Français](../../Docs/User/fr-FR/examples.md) · [日本語](../../Docs/User/ja-JP/examples.md) · [한국어](../../Docs/User/ko-KR/examples.md) · [简体中文](../../Docs/User/zh-CN/examples.md) · [繁體中文](../../Docs/User/zh-TW/examples.md)

With `veroscript` on `PATH`, run these commands from this directory:

```text
veroscript attribute_demo.vs
veroscript attribute_weak.vs
veroscript attribute_methods.vs
veroscript memory_operations.vs
veroscript delegates.vs
veroscript object_basics.vs
veroscript windows_path_text.vs
veroscript standard_library_foundation.vs
veroscript i18n_demo.vs
veroscript numerics.vs
veroscript variant.vs
veroscript default_special_members.vs
veroscript nested_types.vs
veroscript local_types.vs
veroscript method_templates.vs
veroscript value_template_parameters.vs
veroscript constexpr.vs
veroscript template_methods_main.vs
veroscript hello.vs
veroscript console_demo.vs
veroscript macro_scope.vs
veroscript macro_exports.vs
veroscript --implicit-import macro_exports_facade.vs macro_implicit.vs
veroscript alias_exports.vs
veroscript alias_reexports.vs
veroscript --check language_features.vs
veroscript --backend=vm portable_basics.vs
```

With an installed module environment, an omitted JIT selection inherits
`services.jit.backend`; when unset, `auto` selects a usable compatible enabled
backend. Without one, the default `mixed` policy uses VM. The following example
manually pins a compatible backend file or directory:

```text
veroscript --jit-backend="/absolute/path/to/backend" --backend=jit integer_aliases.vs
```

Replace the path with your installed backend. On Windows, a path such as
`D:/VeroScript/Jit` is also accepted. Backend packages are independent of the
Runtime and SDK. If the standard library is outside the runtime's resource
layout, pass `--stdlib-path "/absolute/path/to/stdlib"` before the script name.
Embedded hosts supply their standard-library directory or virtual source
resources explicitly.

`attribute_demo.vs` demonstrates fixed values, getters, copies and optional
reads. `attribute_weak.vs` covers expired weak receivers and shared structs.
`attribute_methods.vs` shares a typed method callable with delegate.
`attribute_functions.vs` uses named getters and also exercises standalone AOT.

`variant.vs` demonstrates empty/present boxes, explicit casts, struct snapshots,
class identity and heterogeneous property values. It uses Core VM/Mixed/JIT;
standalone AOT explicitly rejects variant dependencies.

`nested_types.vs` covers segmented generic arguments, nested enum and aliases, and the child declaration catalog. `local_types.vs` covers generic function and lambda blocks, lexical shadowing, and explicit value passing. These examples use Core VM/Mixed/JIT or the Core AOT exporter; standalone source exporters report an explicit unsupported-feature diagnostic.

`constexpr.vs` demonstrates checked constant initialization, static methods,
value template arguments and ordinary runtime calls of constexpr functions.

The examples are grouped by purpose:

- Templates: `method_templates.vs` shows instance/static method parameters and
  typed callable references; `value_template_parameters.vs` shows checked integer,
  enum and dependent-type constants and fixed array extents.
  `template_methods_main.vs` imports a declaration and its separate implementation.
  These use Core VM/Mixed/JIT or Core AOT. Independent source exporters report
  unsupported template features before producing output.

- Basics and control flow: `hello.vs`, `argv_echo.vs`, `object_basics.vs`,
  `portable_basics.vs`, `fib.vs`, `goto_demo.vs`, and `exceptions.vs`.
  `object_basics.vs` also builds a nullable, self-referencing generic class chain.
  `portable_basics.vs` combines a focused standard-library import, an array,
  a value-capturing lambda and a checked result, using only captured output.
  The exception example covers
  typed and catch-all handlers, bare rethrow, and guaranteed `finally` cleanup.
- Core language: `brace_initialization.vs`, `constructor_initializers.vs`,
  `default_delete.vs`, `default_special_members.vs`, `friend_access.vs`, `generic_constraints.vs`,
  `integer_aliases.vs`,
  `numeric_primitives.vs`,
  `pure_virtual_out_of_class.vs`, `template_prefix.vs`, `static_members.vs`,
  `operators.vs`, `ref_foreach.vs`, `reference_returns.vs`, `reflection.vs`,
  `reflection_executable.vs`,
  and `language_features.vs`. `standard_namespace.vs` also demonstrates
  explicit `cast<T>` and non-throwing `try_cast<T>` conversions.
  `reflection.vs` also demonstrates a `mutable` field and its reflected
  `is_mutable()` metadata.
  `reflection_executable.vs` demonstrates typed field reads/writes, instance
  and static method invocation, reference arguments and returns, preservation
  of by-value arguments, and default interface method dispatch.
  `operators.vs` demonstrates implicit reference binding for operator arguments,
  writable/const index references, assignment references and postfix snapshots.
  `constructor_initializers.vs` contrasts declaration-initialized `const`
  fields with constructor-initialized fields and demonstrates base/field/body
  initialization order.
  `friend_access.vs` demonstrates type and exact-signature function friendship;
  `pure_virtual_out_of_class.vs` demonstrates explicit `= 0`, pure virtual
  destructors, and ordinary/generic same-file member definitions;
  `static_members.vs` also demonstrates static classes and structs,
  namespace qualification, `using namespace` lookup, and instance method calls
  directly through a generic type's static field.
- Callables: `lambda.vs` and `delegates.vs` demonstrate the readable
  `function` spelling, the compact `fn` alias, typed delegates, and events.
  `lambda.vs` includes value/reference initialization captures, a shared mutable
  counter, a local static, and a namespace-qualified function captured for later calls.
  `delegates.vs` uses `std::delegate` and `std::multicast_delegate` with
  lowercase members; import `delegate.vs` to load both types.
  It covers zero to three parameters, a void return,
  value captures, unbinding/rebinding, and changes to multicast registrations
  during a broadcast snapshot.
  `delegate_functions.vs` binds named script functions and covers all supported
  parameter counts. It can also be exported through C++, C#, and Python AOT;
  Python AOT does not support the source lambdas in `delegates.vs`.
- Managed memory: `memory_operations.vs` covers reference addresses, shared identity, endian encoding, read-only views, overlapping copy and explicit byte snapshots.
- Containers: `arrays.vs`, `dynamic_array.vs`, `pair_map.vs`, `multimap.vs`,
  `multiset.vs`, and `priority_queue.vs`. `integer_aliases.vs` also demonstrates
  strongly typed enum keys in `map` and `set`. `pair_map.vs` also checks the first
  assignment of struct and weak values; the weak map entry does not retain its target.
- Modules and preprocessing: `include_alias.vs`, `macro_scope.vs`,
  `macro_scope_module.vs`, `preprocessor.vs`, `reexport_facade.vs`, and
  `reexport_main.vs`. Run `macro_scope.vs` to see that `import` makes an
  exported function available while ordinary `#define` macros stay private.
  `macro_exports.vs` imports `macro_exports_facade.vs`, which re-exports
  `macro_exports_module.vs`; it exercises `#define_export`, object/function/variadic
  macros and private isolation, printing `public=42 private-result=7`.
  `alias_exports.vs` imports `alias_exports_module.vs` directly and prints
  `alias exports: direct 42`; `alias_reexports.vs` imports the same declarations
  through `alias_exports_facade.vs` and prints `alias reexports: facade 42`.
  The module exports `using Count = int` and `typedef Label = string` inside
  `ExportedAliases`; its `InternalCount` alias stays private. The direct consumer
  declares its own `using namespace ExportedAliases`, while the facade consumer
  uses qualified names. A `using namespace` directive is local to its module and
  enclosing namespace, starts at its declaration, and cannot be exported.
- Standard library and runtime facilities: `formatting.vs`, `path_helpers.vs`,
  `standard_library_algorithms.vs`, `standard_namespace.vs`, `windows_path_text.vs`,
  `handle_struct.vs`, `concurrent_demo.vs`, `runtime_utilities.vs`, and
  `script_tool.vs`.
  `concurrent_demo.vs` includes callable tasks using a function, static method,
  and value-capturing lambda, plus a void task joined by ID. It also demonstrates
  bounded channels, timed waits, synchronized state and cooperative cancellation.
  A zero-time wait may already receive a completed task's result; the example
  handles both outcomes and consumes each task result once.
  `handle_struct.vs` demonstrates `shared<T>` / `handle<T>` identity, struct-copy
  construction, and `weak` expiration after the final strong owner is released.
- Embedding/tool configuration: `bindings.sample.json` and
  `workspace.analysis.sample.json`. The workspace sample contains language-server
  analysis options. Pass its contents as `initializationOptions.veroscript` or
  `workspace/didChangeConfiguration.settings.veroscript`, or apply the corresponding
  IDE settings. The server does not automatically load this file. Set
  `bindingCatalogPaths` to the actual catalog location; use absolute paths when
  forwarding settings directly. See the [LSP configuration guide](../../../../VeroScriptTooling/Resources/Docs/User/en-US/lsp.md#initialization-and-configuration).
- Binary distribution: `package/package_library.vs` and
  `package/package_app.vs`; see [`package/README.md`](package/README.md) for the
  complete `.vslib` library and `.vsapp` application workflow.

Most files are standalone. A file that is imported by another example, such as
`macro_scope_module.vs`, `macro_exports_module.vs`, `macro_exports_facade.vs`,
`alias_exports_module.vs`, `alias_exports_facade.vs`, `reexport_facade.vs`,
or `package/package_library.vs`,
intentionally has no `main`; run its entry script instead.
`argv_echo.vs` and `script_tool.vs` demonstrate `int main(string[] args)` and
returning a process exit code; `void main` remains supported.

`standard_library_foundation.vs` demonstrates checked parsing, UTF-8 buffers,
lexical paths, wide accumulation, typed clamp, synchronized cells and task groups.
It imports the aggregate `core.vs` and runs through the Core VM/Mixed/JIT paths.

`standard_namespace.vs` demonstrates the recommended `std::` spellings for
built-in containers, utility types, and functions. Module utilities use
`std::io`, `std::console`, `std::filesystem`, `std::strings`, and `std::concurrent`;
mathematical functions remain flat (`std::sqrt`, `std::round`, `std::pow`,
`std::log`, and related functions), while `std::chrono::steady_now()` is the
monotonic elapsed-time clock. `standard_library_algorithms.vs` covers
generic array sorting, search, copy/fill, comparison and rotation; it also covers
unbiased deterministic random ranges and reproducible Fisher-Yates shuffling with
an explicit engine.
Source-library functions retain their
normal imports (`standard_library_algorithms.vs` imports its three focused modules
directly, while applications may import the aggregate `core.vs`).
`standard_namespace.vs` also shows `readonly<T>` for
portable const payloads and the deliberately explicit `const_cast<T>` escape.

## Host permissions and platform use

The ordinary CLI grants standard-library side effects by default. Embedded
hosts may restrict them independently; successful compilation does not grant
permission to execute a restricted operation. The host also provides imports,
resources and the output callback.

| Examples / operations | Runtime requirements |
| --- | --- |
| `hello.vs`, `portable_basics.vs`, language/container/reflection examples | Output capture for `print`; no script filesystem, environment, input or task permission is needed for these operations. |
| `path_helpers.vs`, `windows_path_text.vs` | Text/path manipulation only. `windows_path_text.vs` formats a Windows-style string on any platform; it does not access `C:` or create a file. |
| `runtime_utilities.vs` | File-read permission for `std::filesystem::cwd()`; the displayed directory and clock value depend on the host. |
| `script_tool.vs` | Environment-read permission and any arguments supplied by the host. Its comment about `read_line()` is an optional input extension requiring console-input permission. |
| `concurrent_demo.vs`, `standard_library_foundation.vs` | Task-spawn permission and a host supporting isolated VM workers; run on a worker thread and allow sufficient execution time. |
| `package/` examples | The listed CLI packaging commands read source and write package files on the build host. A runtime host supplies the resulting package bytes or files. |

Start mobile integration with `hello.vs` or `portable_basics.vs`, VM execution,
explicit resources, captured output and an application worker thread. The shared
mobile host defaults to output-only script permissions, so `runtime_utilities.vs`,
`script_tool.vs` and `concurrent_demo.vs` require a deliberate host policy change.
WebAssembly uses its virtual filesystem; callable tasks require the threading
support documented by that SDK. CLI command-line examples do not imply that
Android/iOS applications have a console or a desktop filesystem layout.

Native SDK consumers live in each module's `Resources/Examples/`. The C# mobile
guide includes a MAUI application, and the Python mobile guide includes an
embedded-CPython acceptance script. Their platform builds and device runs remain
separate from the desktop script regression.

## Recommended source style

The VeroScript examples use one real tab per indent level, displayed as four
columns. Their EditorConfig rules select tabs. LSP formatting follows the
requesting client's configured style; use `insertSpaces: false` and `tabSize: 4`
to preserve this convention. Opening braces stay on their own line, with one statement per line.
Type declarations (`class`, `struct`, `interface`, and
`enum`) use a trailing semicolon even though VeroScript also accepts the concise
form without one. Files use descriptive, feature-based names rather than
release-stage names such as `v1` or `v2`.
