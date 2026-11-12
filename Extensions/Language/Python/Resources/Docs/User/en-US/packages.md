# Python packages, reflection, and localization

## Packages

See [Interop SDK — Packages and hot reload](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#package-container-format).

`Package.from_file(path)` parses a package from disk, and `package_kind`
identifies whether it is an application or library package. The
`canonical_signed_bytes` property returns the exact canonical byte sequence to
which a publisher signature should be applied. A session can load,
reload, or build packages through `load_package`, `reload_package`, and
`build_package_file`.

A parsed `Package` is an immutable snapshot. Create a new object to read
different bytes; calling `__init__` again raises `veroscript.Error`.

`Session.build_package_file` returns package bytes and a `RunResult`:

```Python
from veroscript import Package, PackageKind, Session

with Session() as builder:
	data, result = builder.build_package_file(
		"main.vs",
		kind=PackageKind.APPLICATION,
		package_name="example",
		package_version=1,
		metadata={"build": b"release"},
	)
	if not result.success:
		raise RuntimeError(result.diagnostics)

package = Package(data)
with Session() as runner:
	loaded = runner.load_package(package)
	if loaded.success:
		runner.run()
```

`PackageKind.APPLICATION` creates a runnable application package;
`PackageKind.LIBRARY` creates a library package. A parsed package owns its
native handle and can outlive the session that created its bytes. Package
parsing validates the container structure, not publisher authenticity. Verify
the publisher signature over the package's canonical signed bytes before
loading untrusted data.

## Reflection and binding metadata

`session.reflection()` returns the runtime reflection JSON decoded into Python
mappings. `session.bindings()` returns metadata for registered host bindings.
These objects describe declarations and tooling metadata; they are not the
runtime implementation of the corresponding types.

Source overlays are useful with reflection and editor workflows:

```Python
with Session(source_overlays={
	"virtual/helper.vs": "export int Answer() { return 42; }",
}) as session:
	session.load_source(
		'import "helper.vs"; int main() { print(Answer()); return 0; }',
		"virtual/main.vs",
	)
	metadata = session.reflection()
```

## Object references

`enumerate_object_references(instance)` accepts a script-object `Value` and must be called on the session’s creating thread while the session is idle. It returns a tuple of `ExternalReference` records. Each `kind` is `ExternalReferenceKind.SCRIPT_OBJECT` or `ExternalReferenceKind.HOST_OBJECT`; each `value` owns a cloned `Value` wrapper. This copies the reference while preserving object identity, not the object graph. The tuple and its values can be retained after enumeration and are not borrowed callback views. Normal value, session, disposal and thread rules still apply.

## Diagnostic localization

```Python
from veroscript import available_languages, get_language, set_language

print(available_languages())
set_language("zh-CN")
print(get_language())
```

Language selection is process-wide. Runtime diagnostics use the selected
catalog and fall back to authoritative English text when a catalog or
translation is absent. Use `merge_catalog_directory(directory,
override_existing=False)` to add application catalogs without replacing the
runtime catalog tree.

## Compatibility and performance

`ABI_VERSION`, `RUNTIME_VERSION`, and `CAPABILITIES` identify the loaded native
runtime. Check capabilities before relying on optional features such as
packages, reflection, container values, JIT diagnostics, hot reload, or
execution governance. Execution governance covers cooperative instruction/time
limits, cancellation, and the standard-library capability policy.

JIT statistics expose compilation, dispatch, fallback, budget, and fast-path
counters. The `direct_native_calls` counter records generation-scoped JIT calls
that reached another native entry without passing through the VM value bridge.
These values are diagnostic evidence for a specific session, not a promise of
a fixed speedup. For measurements, use a normal run, a representative workload,
fixed backend and inputs, and a controlled warm-up.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
