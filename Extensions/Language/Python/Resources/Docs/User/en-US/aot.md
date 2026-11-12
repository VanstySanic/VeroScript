# Python AOT

`veroscript aot --target=python` converts a compile-checked VeroScript
program into a readable Python program. The generated program contains its
small compatibility runtime and does not load the VeroScript VM,
`VeroScriptInterop`, or the `veroscript` extension.

## Runtime boundary

The generated program requires a user-provided CPython 3.11–3.15. It runs
with the user's interpreter and bundles no Python SDK, host bindings, or native
VeroScript library. This backend is therefore suitable for source delivery
when the target environment is allowed to run Python, not for embedding the
VeroScript runtime into an existing Python process.

The generated program preserves VeroScript checked arithmetic, conversions,
control flow, imports, standard-library source modules, exceptions, and the
supported concurrency model. Generated workers use operating-system threads;
script globals and static fields are initialized from per-worker snapshots,
while channels are the explicit synchronized capability shared between
workers. Task identifiers belong to one worker and cannot be joined or
cancelled from another worker.

## Single-file output

```powershell
veroscript aot --target=python app.vs -o app.py
python app.py
```

`-I`/`--import-path` can be repeated when application imports are outside the
entry file's directory:

```powershell
veroscript aot --target=python -I modules app.vs -o app.py
```

The command replaces the output file only after generation succeeds. A failed
conversion does not leave a partial Python program.

## Source bundle

Use `--output-dir` when the generated source should remain split into a small
runtime module and a program module:

```powershell
veroscript aot --target=python app.vs --output-dir app_python --bundle-name app
python app_python
python -m app_python
```

The bundle contains `__main__.py`, `__init__.py`,
`_veroscript_runtime.py`, `_veroscript_program.py`, and
`python-aot.manifest.json`. The complete import graph is checked before the
bundle is written. Bundle files are staged before replacement, which is allowed only
for an empty regular directory or one containing a valid VeroScript Python
AOT manifest and no unrelated content.

Final symbolic links and Windows junctions are rejected. During commit the old
directory is moved aside, so the output path may briefly be absent. A failed
commit attempts to restore it.

## Supported boundary and deliberate exclusions

The backend supports scalar and string values, arrays, maps, ordered maps,
sets, queues, stacks, deques, functions, defaults, variadics, namespaces,
imports, enums, classes, structs, interfaces, supported inheritance, checked
casts, optional values, exceptions, non-fallthrough switches, the algorithm,
numeric, and deterministic random source modules, and the standard concurrent
task wrappers.

The following are rejected because the generated form cannot preserve their
runtime contract: `ref`/`out` parameters, open generic user types (apart from
standard concurrent wrappers), overload sets, user destructors, multiple
concrete class bases, class-default blocks, flattened duplicate private type
names, `goto`/labels, switch fallthrough, host bindings, source lambdas, and local
`static` declarations.
An unsupported construct produces a diagnostic and no partial output.

Declare function values with an explicit `fn(...) -> T` type; `auto` inference for
function values is unsupported. Qualified namespace function values are supported.
`Super` receivers and variadic function values are rejected; direct variadic calls
remain supported.

`shared<T>` / `handle<T>` wrappers, callable task entries, and executable
reflection are also rejected. These features remain available through the
Python SDK.

Shutdown closes existing channels and waits for workers together with every generation of tasks they spawn. Channels created during shutdown start closed.

The generated program uses `VERO_JOIN_TIMEOUT_MS` for the bounded shutdown
wait; it defaults to 30000 milliseconds. A worker that calls `exit(n)` fails
that task rather than terminating the parent process.

## Choosing Python AOT or the Python SDK

Use Python AOT when a standalone, inspectable Python source artifact is the
deployment target. Use the [Python SDK](index.md) when the host needs VM or
JIT execution, dynamic loading, host bindings, reflection, hot reload,
retained callables, or runtime package operations.

The standalone source-tool chain is separate from this backend: use the
[Python frontend](frontend.md) to inspect typed Python declarations, the
[Python BindGen](bindgen.md) to describe Python host APIs, and the
[Python transpiler](transpiler.md) to convert typed Python into VeroScript.

## Runtime memory support

Standalone AOT currently does not support `std::memory` queries, buffers, views, or their types. Actual calls or use of these types produce an explicit generation error; importing unused `memory.vs` or `core.vs` remains supported.

[Runtime SDK](index.md)

Standalone Python AOT supports `std::console` text output, terminal capability and size queries, color, cursor, and line controls. Redirected output reports no terminal capabilities, zero dimensions, and unsuccessful control operations; invalid names, modes, or coordinates still report errors. Unknown console builtins are rejected with a source diagnostic.

Windows color, style reset, clearing and cursor controls use native console APIs without enabling ANSI mode. Dimensions and coordinates refer to the visible window; clearing preserves scrollback outside that window.

## Attribute source export

Attribute configuration comparison is a supported read-only builtin exception: `has_same_configuration(ref other)` does not introduce writable aliasing. `try_get()` is supported; `try_get(ref output)` remains unsupported. See the attribute reference for these operation-specific boundaries.

[std::attribute](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/attribute.md)

<a id="native-bundle-api"></a>

Native SDK users include `VeroScriptPythonAot.h`. `FPythonAotSourceBundle` carries `Files` and `ManifestJson`; use `EmitPythonSourceBundleFromSource` / `EmitPythonSourceBundleFromFile`, then `WritePythonSourceBundle` for a staged directory replacement. `BuildPythonSourceBundle` packages already-generated Python. The Python control wrapper’s `veroscript_executable` option selects a CLI executable or SDK root/bin directory; omitted selection uses `VEROSCRIPT_EXECUTABLE`, `VEROSCRIPT_SDK`, then PATH. That external-CLI wrapper is separate from the generated standalone program.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
