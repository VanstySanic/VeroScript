# Python AOT and packaging

## Standalone emitter

`VeroScriptPythonAot` emits a readable Python module that contains its small
compatibility runtime and does not load the VeroScript VM, Interop library, or
the CPython extension. The control wrapper can locate a user-provided CLI from
an executable path, SDK root, `bin` directory, environment variables, or
`PATH`.

SDK calls with `FCodegenOptions` carry annotation definitions from `Bindings`
into processing, including `CaseSensitive` name policies. Annotation-only metadata
adds no runtime dependency; runtime host functions and types are unsupported.

## Bundle contract

Bundle mode writes `__main__.py`, `__init__.py`, `_veroscript_runtime.py`,
`_veroscript_program.py`, and `python-aot.manifest.json`. The complete import
graph is checked as one program. Files are staged before replacement, which is allowed
only for an empty regular directory or one carrying a structurally valid VeroScript
manifest with no foreign content; recognized interpreter `__pycache__` files
are tolerated generated content.

Final symbolic links and Windows junctions are rejected. During commit the old
directory is moved aside, so the output path may briefly be absent. A failed
commit attempts to restore it.

## Distribution contract

The wheel and source distribution carry the version-matched native extension,
Interop library, Python sources, type information, standard library, and locale
catalogs. `VeroScriptVersion.h`, package metadata, and the imported runtime
version must agree. Release validation checks the supported CPython and platform
matrix, installed-package type surface, and source-distribution rebuild.

The Python standalone runtime maps math domain/overflow cases to Core floating results instead of leaking Python `ValueError`/`OverflowError`: zero logarithms produce negative infinity, power overflow preserves the odd-integral sign case, and floor/ceil/trunc preserve signed zero. Python and C# generated NaN text follows the target platform, including Darwin spelling. Keep these emitter details in backend regressions; the public finite-result checks belong to [Math](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/math.md#floating-results).

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](aot-and-packaging.md) · [fr-FR](../fr-FR/aot-and-packaging.md) · [ja-JP](../ja-JP/aot-and-packaging.md) · [ko-KR](../ko-KR/aot-and-packaging.md) · [zh-CN](../zh-CN/aot-and-packaging.md) · [zh-TW](../zh-TW/aot-and-packaging.md)
