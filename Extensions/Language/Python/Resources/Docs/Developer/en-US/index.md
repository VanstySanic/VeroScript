# VeroScript Python technical documentation


This directory documents the Python language group for maintainers and binding
authors. It describes source and binary contracts; it is not a Python user
tutorial. Python syntax, VeroScript syntax, API names, module names, command
options, JSON keys, and diagnostics remain exactly as written in source.

## Module ownership

The group contains five modules:

- `VeroScriptPython` is the CPython adapter over the stable C Interop ABI. It
  owns sessions, typed values, containers, packages, callbacks, reflection,
  retained callables, serialization, and Python-backed host classes.
- `VeroScriptPythonAot` emits standalone Python source or a multi-file
  source package. Generated output does not load the VM, Interop shared library,
  or CPython extension.
- `VeroScriptPythonFrontend` statically parses the supported typed Python 3.11
  declaration subset without importing or executing input modules.
- `VeroScriptPythonBindGen` converts frontend declarations into Binding Catalog
  metadata and per-input API references.
- `VeroScriptPythonTranspiler` lowers the supported typed Python subset to
  VeroScript and validates the generated source with the real compiler.

## Static-tool boundary

Frontend, BindGen, and Transpiler are static tools. They must not import,
execute, or use runtime side effects from an input module. Unsupported dynamic
features, untyped signatures, arbitrary-precision semantics that cannot be
preserved, async behavior, reflection, and unsupported inheritance must fail
explicitly rather than being approximated.

Project mode resolves the complete supplied module graph before output. Relative
package layout, module names, imports, source maps, API references, and manifest
entries must remain mutually consistent. Outputs are staged and committed with best-effort rollback; replacement must
not occur after a failed parse, lowering, import resolution, or final compiler
validation.

## Runtime and generated-code boundary

The CPython adapter releases the GIL around native work where the implementation
allows it and reacquires it for Python callbacks. Creator-thread restrictions,
retained-callable ownership, callback reentrancy, and synchronization of shared
Python state are part of the public contract. A worker callback must not call a
creator-thread-confined session operation.

The standalone AOT runtime preserves VeroScript integer, container, exception,
module, and concurrency semantics only for the documented executable subset.
Generated workers use OS threads, isolate script globals and static fields, and
preserve channel identity as the explicit synchronized sharing mechanism.

## Python object and artifact details

The primary runtime objects are `Session`, `Value`, `Package`, retained callable
handles, `HostParameter`, and host class/function registration objects. A
session must be explicitly closed on its creator thread. `Value` containers use
explicit element types so empty and narrow containers do not depend on Python
type guessing; iteration and `keys()`/`values()`/`items()` return owned
snapshots. Host callbacks and mutation callbacks have separate exception and
threading rules, and callback-owned state must provide its own synchronization.

The AOT bundle contains a manifest and compatibility runtime files. Replacement
stages the files first and is allowed only for an empty regular directory or one carrying
a structurally valid VeroScript Python AOT manifest with no foreign content;
recognized interpreter `__pycache__` files are the only tolerated generated
extras. Final links and Windows junctions are rejected; the output path may
briefly be absent during commit, and failure attempts restoration. The generated program chooses the user's Python interpreter and does
not discover or bundle a Python SDK.

## Packaging and versioning

The wheel and source distribution must carry matching native extension,
Interop library, Python sources, type information, standard library, and
localization assets. `VeroScriptVersion.h` is the authoritative semantic
version source; package metadata and the imported runtime version must agree.

Frontend project mode emits one AST artifact per input plus a project manifest.
BindGen project mode emits one merged Binding Catalog, one API reference per
input, and `bindgen.manifest.json`. Transpiler project mode preserves relative
package layout, resolves supplied project-relative imports, emits source maps,
and writes `transpile.manifest.json` only after complete-graph validation. A
failed import, duplicate module, ambiguous declaration, or final compiler check
must leave the previous output intact.

## Detailed pages

- [Python runtime and static tools](runtime-and-tools.md)
- [Python AOT and packaging](aot-and-packaging.md)
- [Python toolchain and package contract](toolchain-and-packaging.md)

## Source of truth

Use the Python source packages, native adapter code, frontend AST, generator and
transpiler tests, CMake files, package metadata, and distribution validation
tools as the source of truth. Update this document whenever a supported subset,
threading rule, generated artifact, package layout, or ABI mapping changes.


[Python on Android and iOS](mobile.md)

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
