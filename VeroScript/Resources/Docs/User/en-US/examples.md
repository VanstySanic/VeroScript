# Examples index

The example files validate language and runtime behavior and provide executable cross-references for the documentation; they are not the API specification. For signatures, error conditions, and compatibility requirements, use the language, standard-library, and toolchain references. Examples are distributed through Resources or available from the source repository; replace the filenames below with their actual locations when running them.

Run an example:

```powershell
veroscript hello.vs
veroscript --check reflection.vs
```

[Attributes and method callables](standard-library/attribute.md)：`attribute_demo.vs`、`attribute_functions.vs`、`attribute_methods.vs`、`attribute_weak.vs`。

## Execution backend, resources, and permissions

With an installed module environment, omitting `--jit-backend` inherits its JIT choice; an unset choice defaults to `auto`. Without a usable automatically selected backend, the default `mixed` policy uses VM. To pin a compatible backend file or directory manually:

```powershell
veroscript --jit-backend="/absolute/path/to/backend" --backend=jit integer_aliases.vs
```

Replace the path with your backend installation. Use `--stdlib-path` for a standard-library directory outside the runtime resource layout. Embedded hosts provide the modules or virtual source resources and capture output explicitly.

Start embedded/mobile integration with `portable_basics.vs`: it imports `numeric.vs`, checks a value-capturing lambda and prints `portable basics: 42`. It needs output permission only. `runtime_utilities.vs` needs file-read permission for `std::filesystem::cwd()`, `script_tool.vs` needs environment-read permission, and `concurrent_demo.vs` needs task-spawn permission and isolated VM workers. A zero-time task wait can return a result already completed by the worker; consume it once. `path_helpers.vs` and `windows_path_text.vs` manipulate text and do not access files. Run scripts on an application worker thread under the host's configured execution budget.

The C# mobile guide contains a MAUI example; the Python mobile guide contains an embedded-CPython acceptance script. Mobile compilation, application integration and device acceptance remain separate from desktop script tests.

## Reading the examples

Choose a topic, read the file comment and the linked reference, then change the inputs and run the example again. `int main()` reports a process exit code; `int main(string[] args)` also receives script arguments. `void main()` is supported when no return value is needed.

## Getting started and combined features

| Example | Main topic |
| --- | --- |
| `hello.vs` | Minimal script, `main`, and output. |
| `portable_basics.vs` | Explicit numeric import, array, value-capturing lambda and checked output, with output-only script permissions. |
| `argv_echo.vs` | `int main(string[] args)` and command-line arguments. |
| `object_basics.vs` | Struct methods, an instance declared with its type, and nullable class references. |
| `language_features.vs` | Constraints, boxing, overloads, defaults, and variadics. |
| `fib.vs` | Recursive functions and conditional control flow. |
| `goto_demo.vs` | `goto` and labels. |
| `exceptions.vs` | Typed exceptions, catch-all handlers, rethrow, and `finally`. |

## Values, arrays, containers, and references

| Example | Main topic |
| --- | --- |
| `arrays.vs` | Native arrays and array access. |
| `dynamic_array.vs` | Dynamic arrays. |
| `brace_initialization.vs` | Brace initialization. |
| `integer_aliases.vs` | Integer aliases and explicit types. |
| `numeric_primitives.vs` | Distinct `float`/`double`, numeric suffixes, leading-decimal floating literals, and 8/16/32/64-bit integers. |
| `pair_map.vs` | `pair`, map entry views, destructuring, and first writes of struct and weak values. |
| `multimap.vs` | Multi-maps. |
| `multiset.vs` | Multi-sets and duplicate elements. |
| `priority_queue.vs` | Priority queues. |
| `ref_foreach.vs` | Reference variables and range iteration. |
| `reference_returns.vs` | Reference aliases, reference returns, and class-handle-slot rebinding. |

## Types, generics, and callable values

| Example | Main topic |
| --- | --- |
| `operators.vs` | Operator reference arguments, writable/const index references, assignment references and postfix snapshots. |
| `static_members.vs` | Static members and initialization. |
| `default_delete.vs` | Defaulted and deleted special members. |
| `constructor_initializers.vs` | Constructor initializer lists, `const` fields, and initialization order. |
| `friend_access.vs` | `friend` access rules. |
| `handle_struct.vs` | Shared struct identity with `shared<T>` / `handle<T>`, copy construction and `weak` expiration. |
| `pure_virtual_out_of_class.vs` | Out-of-class definitions and pure virtual syntax. |
| `template_prefix.vs` | Prefix syntax in template member definitions. |
| `method_templates.vs` | Own method parameters and callable references. |
| `value_template_parameters.vs` | Checked value parameters, enum policies and fixed array extents. |
| `constexpr.vs` | Compile-time scalar values, pure functions and runtime calls. |
| `template_methods_main.vs` | Cross-file template method definitions. |
| `generic_constraints.vs` | Generic constraints and `where`. |
| `lambda.vs` | Lambdas, initialized value/reference captures, shared mutable counters and first-reach local statics. |
| `delegates.vs` | `std::delegate` / `std::multicast_delegate`, lowercase members, captures, rebinding and broadcast snapshots. |
| `delegate_functions.vs` | Named-function delegates, zero to three arguments; VM and C++/C#/Python AOT. |
| `reflection.vs` | Reflection of types, fields, methods, declaration metadata, and `mutable` fields. |
| `reflection_executable.vs` | Typed field reads/writes, instance/static/default-interface calls, reference arguments/returns and by-value argument isolation. |

## Modules, preprocessing, and packages

| Example | Main topic |
| --- | --- |
| `include_alias.vs` | Include/import aliases. |
| `alias_exports.vs`, `alias_reexports.vs` | Cross-file `export using` / `export typedef`, namespace lookup and facade re-export; the accompanying module/facade files are import-only. |
| `macro_scope.vs` and `macro_scope_module.vs` | Run the entry script to verify private `#define` isolation; the latter file is import-only. |
| `macro_exports.vs`, `macro_exports_module.vs`, `macro_exports_facade.vs` | Public object/function/variadic macros, facade re-export and private isolation; run `macro_exports.vs`. |
| `macro_implicit.vs` | Implicit public macros without an import in the consumer; run `veroscript --implicit-import macro_exports_facade.vs macro_implicit.vs`. |
| `reexport_facade.vs` | A module re-export facade. |
| `reexport_main.vs` | Re-exporting declarations from an entry module. |
| `preprocessor.vs` | Preprocessor conditions and macros. |
| `package/package_library.vs` | Package-library source. |
| `package/package_app.vs` | Package-application entry point. |
| Package example notes | Package-example layout and commands. |

## Standard library, runtime, and tools

| Example | Main topic |
| --- | --- |
| `memory_operations.vs` | Reference identity, binary buffers, byte order, shared views and independent copies. |
| `formatting.vs` | `print`, `printf`, `format`, and `formatf`. |
| `path_helpers.vs` | Path standard library. |
| `standard_library_algorithms.vs` | Generic algorithms, deterministic random ranges, and reproducible shuffling. |
| `standard_namespace.vs` | Recommended `std::` spellings, `readonly` values, and explicit conversions. |
| `windows_path_text.vs` | Windows-style path text on any platform; no filesystem access. |
| `runtime_utilities.vs` | `range`, the current directory, and time utilities. |
| `concurrent_demo.vs` | Channels, callable tasks with functions, static methods and lambdas, and cooperative cancellation. |
| `script_tool.vs` | Script-tool arguments, environment variables, strings, assertions, and exit status. |

## Host and binding configuration samples

| File | Main topic |
| --- | --- |
| `workspace.analysis.sample.json` | Language-server analysis configuration. |
| `bindings.sample.json` | Minimal host binding catalog. |

Pass the contents of `workspace.analysis.sample.json` as `initializationOptions.veroscript` or `workspace/didChangeConfiguration.settings.veroscript`, or apply the corresponding IDE settings. The language server does not automatically load this file. Set `bindingCatalogPaths` to the actual catalog location; use absolute paths when forwarding settings directly.

Use the [example directory notes](../../../Scripts/examples/README.md) for complete example conventions. Use the [language reference](language-reference/index.md) for language rules, the [standard-library reference](standard-library/index.md) for signatures, and the [CLI reference](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md) for options.

- `console_demo.vs`: `std::console` (Terminal controls).

[Application internationalization — std::i18n](standard-library/i18n.md): `i18n_demo.vs` · `VeroScript/Resources/Examples/I18n/`.

- [Standard library foundation](standard-library/foundation.md)：`standard_library_foundation.vs`

## variant

[variant — Dynamic value box](standard-library/variant.md): `variant.vs`.

```text
veroscript variant.vs
```
<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](examples.md) · [fr-FR](../fr-FR/examples.md) · [ja-JP](../ja-JP/examples.md) · [ko-KR](../ko-KR/examples.md) · [zh-CN](../zh-CN/examples.md) · [zh-TW](../zh-TW/examples.md)
