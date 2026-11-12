# Python transpiler

`VeroScriptPythonTranspiler` converts a strict, statically typed Python 3.11
subset into VeroScript source. It shares declaration analysis with
`VeroScriptPythonFrontend` and is independent from the Python runtime binding
and from the VeroScript-to-Python AOT backend.

## Supported subset

The current subset includes typed functions, ordinary classes, integer
enums, annotated fields and locals, constructors and methods, scalar and
container literals, calls, assignments, conditionals, while loops, and ordinary
expressions. Ordinary Python classes become VeroScript `class`
declarations so reference and aliasing behavior remains explicit.

`@dataclass` is rejected because generated constructors, equality, and other
runtime behavior are not implemented. Class keyword arguments, including
`metaclass`, are also rejected because their runtime behavior is not implemented.
Use ordinary classes with explicit
constructors and methods within the supported subset.
The built-in `str(...)` conversion is rejected because Python formatting
semantics are not implemented. User classes and functions named `str` remain
callable when declared in or explicitly imported into the current module.

Numeric annotations select VeroScript fixed-width types; Python's
arbitrary-precision integers and floor-division semantics are not silently
emulated. Dynamic imports, async code, comprehensions, generators, descriptors,
properties, multiple inheritance, reflection, and unannotated local
declarations are rejected with diagnostics.

## Single-file command

Class-body field defaults accept immutable scalar literals, supported pure literal
constant expressions, and known integer-enum members. Shared containers, objects,
arbitrary name references, and function calls are rejected. Initialize instance values in an explicit `__init__`; for containers,
create a typed local variable first, then assign it to the instance field.

```powershell
python -m veroscript_python_transpiler input.py `
  -o input.vs `
  --source-map input.vs.map.json `
  --veroscript veroscript
```

The generated source is validated with the real VeroScript compiler by
default. Use `--no-validate` only when an external validation stage is
deliberately responsible for the generated source. No output is replaced until
parsing, lowering, and final validation succeed. `--timeout <seconds>` defaults to `120.0`, bounds
each external validation rather than the total project duration, and must be
finite and positive; the library exposes
the same setting as `timeout_seconds` on both transpiler option types.

## Project command

```powershell
python -m veroscript_python_transpiler package/models.py package/service.py `
  --project-root . `
  --output-dir generated/veroscript `
  --veroscript veroscript
```

Project mode resolves supplied relative imports, preserves package layout,
emits explicit VeroScript imports and exported declarations, validates the
complete generated module graph, and writes source maps plus
`transpile.manifest.json` using staged writes with best-effort rollback. Unresolved imports and ambiguous
cross-module declarations fail before output.

Project imports that rename a module or declaration with `as` are rejected;
use the original names. Metadata imports such as `import typing as t` remain supported.

## Library boundary

The library exposes `transpile`, `transpile_source`, `transpile_file`, and
`transpile_files`, with result records for generated files, source maps, and
diagnostics. The tool is a source converter, not a Python runtime: generated
VeroScript must still be checked and run with the VeroScript toolchain.

`len(str)` counts Unicode code points, including supplementary characters and
combining marks, rather than UTF-8 bytes. String-literal lengths need no runtime
dependency. Other declared string values use a linear Core UTF-8 buffer helper
and import the standard-library `string.vs`. This takes O(n) time and O(n)
temporary storage rather than Python’s O(1) string-length lookup; it requires the Core runtime
and is currently rejected by standalone Python AOT. Generated or existing
`string.vs` files must not shadow that dependency. Container lengths retain their
direct Core operations. Use an annotated local if an expression's string or
container type cannot be established.

Python bindings named `print`, `len`, `abs`, `min`, or `max` are rejected at calls
where Core would intercept them as builtins; ordinary methods and declared `str`
remain callable. Annotation-only fields must be explicitly initialized before
reads. Python missing-attribute and complete definite-assignment semantics are
outside this subset.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
