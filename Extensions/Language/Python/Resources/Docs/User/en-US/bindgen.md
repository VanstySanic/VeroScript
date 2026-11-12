# Python BindGen

`VeroScriptPythonBindGen` converts declarations from the supported typed
Python subset into Binding Catalog v1 JSON. It reads source statically; it does
not import modules, execute decorators, or construct Python objects. The
catalog describes a host surface and is not a runtime implementation.

## Supported declarations

The initial binding surface includes public functions, classes, dataclasses,
`IntEnum` and `IntFlag` enums, annotated instance fields, constructors, instance/static/class
methods, and properties. Python classes and dataclasses are represented as host
classes because Python instances have reference semantics. Annotated fields
produce a live getter and, unless disabled by the generated definition, a
property setter.

Dynamic or untyped signatures, variadics, default arguments, module globals,
unsupported inheritance, and unrepresentable type annotations fail closed.
The generated catalog must contain every referenced enum and its underlying
values. At runtime, `bind_module` and `bind_modules` require the supplied enum
class to derive from `IntEnum` or `IntFlag`; a plain `Enum` is rejected. The
runtime enum members and integer values must exactly match the catalog. Member
descriptors must also match the catalog: an instance method must still be an
instance method, and a `staticMethod` declaration must still be a static or
class method. Stale or mismatched declarations fail before the session is
modified.

The runtime implementation is registered separately through
`VeroScriptPython` host APIs.

## Single-file command

```powershell
python -m veroscript_python_bindgen host_api.py `
  --module-name host `
  --catalog host.bindings.json `
  --api-reference host.vs.txt `
  --validate `
  --veroscript veroscript
```

`--validate` asks the VeroScript CLI to validate the generated catalog.
`--api-reference` emits a readable reference for people; it is not compiled as
an implementation. The catalog is still required even when only a reference
is requested. `--timeout <seconds>` bounds external validation and must be a
finite positive number.

## Project command

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py `
  --project-root . `
  --output-dir generated/bindings `
  --validate `
  --veroscript veroscript
```

`--output-dir` selects project mode, including for a single input. Multiple inputs require it. `--project-root` and `--manifest` are valid only in project mode; `--manifest` overrides the manifest output path. Project mode cannot be combined with `--catalog`, `--api-reference`, or `--module-name`. Outputs are staged together with best-effort rollback; keep the manifest with its matching catalog and references.

Project mode emits one merged `bindings.json`, one API reference per input, and
`bindgen.manifest.json` using staged writes with best-effort rollback. Cross-module typed references are resolved
against the merged declaration set before catalog validation. Use the manifest
with the same generated files; do not treat per-input references as separate
catalogs.

## Library boundary and trust

The library API includes `generate_bindings`, `generate_bindings_from_source`,
`generate_bindings_from_file`, `generate_binding_project`, `validate_catalog`,
`bind_module`, and `bind_modules`. The two binding helpers require modules that
the application has explicitly imported; BindGen never imports analyzed input.
Use `bind_module` for one source module and `bind_modules` for a merged project
catalog. Project binding verifies that every catalog owner has a supplied
module, then registers all host types before cross-module member signatures.
Use the returned diagnostics before publishing artifacts. A catalog can expose
names that are syntactically valid but still fail at runtime if the supplied
module does not provide the matching implementation. `bind_module` and
`bind_modules` do not import modules for you: import the exact modules in the
application, then bind them explicitly. `bind_modules` verifies complete
project coverage before changing the session.

When project modules declare the same type name, use qualified annotations such
as `models.Item`. Ambiguous bare names are rejected, including scalar alias
names such as `Int8`. Types declared in the current module take precedence over
project leaf aliases and scalar aliases.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
