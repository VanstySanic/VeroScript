# C# packages, reflection, and localization

## Package ownership and trust

See [Interop SDK — Packages and hot reload](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#package-container-format).

`VeroScriptPackage` parses an application or library package and exposes its
kind, name, version, metadata, and canonical signed bytes. `GetMetadata()`
returns owned metadata records, `HasCompiledModule` reports whether the parsed
package can still transfer its compiled module, and `GetCanonicalSignedBytes()`
returns the bytes covered by a publisher signature. Parsing validates
the container format, not publisher authenticity:

```CSharp
using var package = VeroScriptPackage.Read(packageBytes);
Console.WriteLine(package.Name);
Console.WriteLine(package.GetCanonicalSignedBytes().Length);

var loaded = session.LoadPackage(package);
```

Successful `LoadPackage` or `ReloadPackage` transfers the compiled module into
the session. The package metadata and canonical bytes remain readable, but the
same package instance cannot transfer its compiled module a second time. Parse
the original bytes again when another transfer is required. Verify the
publisher signature over `GetCanonicalSignedBytes()` before loading an
untrusted package.

`BuildPackageFile` returns package bytes and a `VeroScriptRunResult`. Package
options control application/library kind, name, version, metadata, and whether
debug information is stripped.

## Reflection and binding JSON

`ExportReflectionJson()` returns metadata for loaded script modules.
`ExportBindingsJson()` returns registered host-binding metadata. These JSON
documents are intended for tooling and inspection; they do not replace the
typed managed registration API.

Reflection data may include module, type, field, method, function, global, enum,
and annotation information. Binding descriptors use recursive type arguments;
do not infer a container's key/value types from a legacy display name.

## Localization

`VeroScriptRuntime.CurrentLanguage` selects the process-wide diagnostic language.
Use `GetAvailableLanguagesJson()` to populate a language selector and load or
merge catalogs through `VeroScriptRuntime.LoadCatalogDirectory` or
`VeroScriptRuntime.MergeCatalogDirectory`. The former replaces the catalog
root; the latter merges application catalogs and can optionally override
existing entries. Runtime diagnostics
retain stable message IDs and placeholder arguments; `FormatDiagnostic` uses
the selected catalog to produce display text.

```CSharp
using System.Text.Json;

foreach (var language in JsonSerializer.Deserialize<string[]>(
	VeroScriptRuntime.GetAvailableLanguagesJson()) ?? [])
	Console.WriteLine(language);
VeroScriptRuntime.CurrentLanguage = "fr-FR";
```

Changing the language is thread-safe in the native runtime, but a host should
define when a process-wide language change is allowed so concurrent UI work
does not mix languages. SDK argument or lifetime checks may raise English exceptions; language selection applies to runtime diagnostics.

## Capabilities and compatibility

Check `VeroScriptRuntime.Capabilities` before using optional packages, hot
reload, bindings, reflection, interception, mutation callbacks, container
enumeration, retained callables, generic host types, serialization, execution
governance, or JIT diagnostics. Execution governance covers cooperative
instruction/time limits, cancellation, and the standard-library capability
policy.
`VeroScriptRuntime.AbiVersion` must exactly match the adapter ABI `0x00000101` (0.1.1). Deploy the managed package, native Interop library, and runtime catalogs from matching releases.

See [C# values and ownership — Borrowed values and callables](values.md#managed-call-lifetime).

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
