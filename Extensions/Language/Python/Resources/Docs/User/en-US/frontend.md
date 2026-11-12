# Python frontend

`VeroScriptPythonFrontend` is the static declaration front end shared by the
Python binding generator and the Python-to-VeroScript transpiler. It parses
typed Python source without importing or executing the input module and emits a
stable VeroScript-owned declaration model.

## Boundary and supported surface

The frontend records source locations, module and import information, typed
functions, classes, dataclasses, integer enums, annotated fields, parameters,
decorators, documentation, and normalized type annotations. It also preserves
qualified names and project-relative module layout.

The accepted input is deliberately narrower than Python. Top-level execution,
unsupported executable statements, nested class-body statements, dynamic or
unresolved types, and constructs whose semantics cannot be represented by the
downstream VeroScript tools produce diagnostics. The input is never imported,
so parsing has no application side effects.

## Single-file command

```powershell
python -m veroscript_python_frontend input.py -o input.python-ast.json
```

Use `--module-name` when the source file's logical module name must be
explicit. `--lang` selects the diagnostic language and `--version` prints the
tool version. Output is UTF-8 and is replaced only after parsing succeeds.

## Project command

```powershell
python -m veroscript_python_frontend package/models.py package/service.py `
  --project-root . --output-dir generated/frontend
```

Project mode parses all inputs as one declaration set, preserves package
layout, resolves cross-file declarations, and writes one
`*.python-ast.json` per input plus `frontend.manifest.json`. Duplicate inputs,
duplicate module names, and files outside `--project-root` fail before any
output is replaced. `--output-dir` cannot be combined with `-o` or
`--module-name`.

## Library boundary

The Python API exposes `parse_source`, `parse_file`, and `parse_files` together
with declaration and diagnostic records. Use the frontend when another tool
needs structure; use BindGen when the result must become a host-binding
catalog, or Transpiler when the source itself must become VeroScript.

Diagnostics preserve source locations and stable diagnostic codes. Localization
uses the VeroScript catalogs when available and falls back to authoritative
English text.

## Diagnostic fallback

Use `--lang` to choose the diagnostic language; it overrides `VERO_LANG` and the system locale. Missing or unusable translated messages fall back to English. Diagnostic codes and source locations remain the same in every language.

`--lang` applies to tool messages backed by the VeroScript catalogs. It does not translate `argparse` help or argument-parser errors, nor text forwarded from Python, the operating system or an external compiler; those retain their own language behavior.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
