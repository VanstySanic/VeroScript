# 01: Build a Python source-tool pipeline

Prerequisites: install matching Python Frontend, BindGen and Transpiler packages and make `veroscript` available. Work in a writable directory containing your supported typed `host_api.py`, `app.py`, `package/models.py` and `package/service.py`. Substitute your own paths consistently. Runtime binding also requires a configured Python SDK Session; see the [Python SDK guide](../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/index.md).

In this tutorial you will take a small typed Python module through the three
build-time tools: the frontend, BindGen, and the transpiler. The result is a
declaration AST for inspection, a host-binding Catalog, and validated
VeroScript source.

## Step 1: parse declarations

Start with a typed module such as `host_api.py`. Parse it without importing or
executing the module:

```powershell
python -m veroscript_python_frontend host_api.py -o generated/host.python-ast.json
```

Open the JSON and identify the module name, public functions, classes, fields,
and source locations. A parse failure is a source-model problem; it is not a
runtime failure.

## Step 2: generate a host contract

Create a Catalog and readable reference from the same input:

```powershell
python -m veroscript_python_bindgen host_api.py `
  --catalog generated/bindings.json `
  --api-reference generated/host.vs.txt `
  --validate --veroscript veroscript
```

Read the generated reference, then compare one Catalog function with the Python
callable that the host will register. The Catalog does not execute the callable
and does not replace the host registration code.

## Step 3: convert Python source

Convert a typed application module into VeroScript and keep the source map:

```powershell
python -m veroscript_python_transpiler app.py `
  -o generated/app.vs `
  --source-map generated/app.vs.map.json `
  --veroscript veroscript
```

Run `veroscript --check generated/app.vs` before integrating the output. The
transpiler intentionally rejects Python features whose integer, import,
property, async, or reflection semantics cannot be preserved.

## Step 4: use project mode

When modules import one another, pass all inputs together:

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py `
  --project-root . --output-dir generated/bindings `
  --validate --veroscript veroscript

python -m veroscript_python_transpiler package/models.py package/service.py `
  --project-root . --output-dir generated/veroscript `
  --veroscript veroscript
```

Keep the generated manifest and source maps with the matching outputs. Do not
mix artifacts from different input sets.

At runtime, import the modules under application control and bind the merged
catalog in one operation:

The binding fragment assumes `session` is an existing configured Session, `models` and `service` are explicitly imported `package.models`/`package.service` module objects, and `project_bindings` is the parsed mapping from `generated/bindings/bindings.json` or the result of `generate_binding_project`. The source tools do not import these modules on your behalf.

```python
from veroscript_python_bindgen import bind_modules

bind_modules(session, (models, service), project_bindings)
```

The helper checks module coverage before changing the session and registers
all project types before attaching members that refer across module boundaries.

## Step 5: verify runtime binding and values

Use `IntEnum` or `IntFlag` for an enum that will be registered at runtime;
plain `enum.Enum` is rejected. The runtime enum member names and integer values
must exactly match the generated Catalog. Likewise, a Catalog instance method
must still be an instance method when the module is bound, and a `staticMethod`
entry must still be static or class-based.

The value adapter now represents script null explicitly:

```Python
from veroscript import Value

missing = Value(None)
urgent = Value.priority_queue("int", [3, 1, 2])
```

Assigning `None` to a non-nullable typed value raises `TypeError`. Keep these
checks in the host test suite so a Catalog and its runtime module cannot drift
apart.

## Exercises

1. Add an annotated enum and confirm that BindGen includes its values.
2. Add an unannotated local variable and observe the frontend/transpiler
   diagnostic.
3. Make a cross-module import, run project mode, and inspect both manifests.
4. Load the generated Catalog in a Python host and verify that the registered
   implementation matches the generated signature.

Next, read [Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/aot.md) if the final artifact
should be standalone Python rather than VeroScript source.

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](01-python-source-tools.md) · [fr-FR](../../fr-FR/python/01-python-source-tools.md) · [ja-JP](../../ja-JP/python/01-python-source-tools.md) · [ko-KR](../../ko-KR/python/01-python-source-tools.md) · [zh-CN](../../zh-CN/python/01-python-source-tools.md) · [zh-TW](../../zh-TW/python/01-python-source-tools.md)
