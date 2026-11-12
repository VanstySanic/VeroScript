# C# BindGen

`VeroScriptCSharpBindGen` consumes the semantic C# frontend model and emits Binding Catalog v1 metadata. Optional outputs are reflection-free live registration for `VeroScriptCSharp`, an API reference, and an AOT bridge for generated C# programs.

## Choose the outputs

The CLI requires `--catalog`. `--registration`, `--aot-bridge`, `--api-reference` and `--api-reference-dir` select additional outputs; omit the registration and bridge options for metadata-only generation. A Catalog describes the binding contract and does not install executable callbacks. Compile live registration with `VeroScript.CSharp`. Compile the AOT bridge with the original managed declarations and the generated C# program; that path uses direct calls without reflection or `VeroScriptInterop`.

The library has different defaults: `CSharpBindGenOptions.EmitRegistrationSource` and `EmitApiReference` are `true`, while `EmitAotBridgeSource` is `false`. To generate metadata without either executable adapter, explicitly set both `EmitRegistrationSource=false` and `EmitAotBridgeSource=false`. A limitation of an optional adapter does not automatically make the corresponding Catalog metadata unsupported.

## Shared input rules

Public classes, structs, enums and public members are considered by default. `--require-attribute` selects types marked with `[VeroScriptBinding]`; `[VeroScriptIgnore]` excludes declarations. Generic host types or members, async, pointers, nullable or delegate types, optional or `params` parameters, operators, reference returns, reference fields, indexers, required members and non-representable overloads are rejected. Abstract-class constructors are omitted. Additional output-specific restrictions are described below.

### Container keys

Map and ordered-map keys, and set elements, may be `sbyte`, `int`, `uint`,
`long`, `ulong`, `string`, or a declared enum. `sbyte` maps to VeroScript's
signed 8-bit `char`; other key shapes fail before a Binding Catalog reaches the
runtime importer.

## Live registration limits

Live generated containers are one-level arrays/lists, maps, sets, queues or stacks with scalar, string or declared-enum elements. Map keys and set elements must also satisfy the key rules above. Nested containers and host-object elements are not supported by this generator. Multi-maps, multi-sets, priority queues and deques require the SDK value API or an explicit host wrapper.

Live registration rejects static container fields, properties and methods with `VSCB3004`. It also rejects structs exposing public instance fields, properties or non-constructor methods with `VSCB3001`, including read-only instance members. These are live-generator restrictions: metadata-only generation and the AOT bridge have their own checks. The AOT bridge rejects public struct instance methods, writable instance fields and ordinary public setters with `VSCB3006`, because it cannot preserve boxed-receiver writeback; see [C# AOT](aot.md).

## Single-file command

```powershell
veroscript-csharp-bindgen HostApi.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference generated/Bindings.vs.txt
```

## Project command and outputs

```powershell
veroscript-csharp-bindgen Models/A.cs Models/B.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference-dir generated/api `
  --manifest generated/bindings.manifest.json
```

Project mode merges partial declarations, assigns stable references to inputs and prepares all selected outputs before replacement. A generation error leaves existing outputs untouched; a write failure triggers best-effort restoration. `--reference` is repeatable. `--language-version` selects the C# syntax version and defaults to `latest`. `--api-reference` and `--api-reference-dir` are mutually exclusive. To combine API-reference files with `--manifest`, use `--api-reference-dir`; `--manifest` alone does not require API-reference output.

## Execution boundaries

Live callbacks may run concurrently on VM workers; synchronize shared host state. The AOT bridge calls the managed declarations directly. Keep the Catalog, bridge, original declarations and generated program from the same generation together.

## Generated container boundaries

Binding Catalog metadata can describe ordered maps. Live registration rejects `SortedDictionary<TKey, TValue>` with `VSCB3002`, and the generated AOT bridge rejects it with `VSCB3003`; use an explicit host wrapper for executable integration. Live container marshalling creates managed snapshots: only `ref`/`out` parameters are written back after a successful callback. By-value container mutations do not update the script container. AOT bridges pass supported managed containers directly and preserve their object identity.

## Linked output paths

On Windows, `\\?\`, `\\.\` and volume GUID path aliases are compared by their final filesystem location. These aliases cannot bypass the same checks; distinct output paths remain supported.

The BindGen and Transpiler CLIs resolve existing directory and file links before staging output. Paths through symlinks or Windows junctions cannot overwrite an input source or name the same output twice. Distinct outputs inside linked directories remain supported.

Abstract host classes retain supported methods and factory-returned instances; their constructors are omitted from the Catalog, live registration and AOT bridge. Nested types that depend on a generic outer type are rejected with `VSCB1001`, including attribute-selected exports.

AOT `ref`, `out`, `in` and `ref readonly` parameters preserve shared storage for directly compatible scalars and supported containers. Reference parameters requiring string, character, enum or host-object representation conversion fail with `VSCB3007`; generate the Catalog with `EmitRegistrationSource=false` and `EmitAotBridgeSource=false` to supply an explicit partial bridge.

## Output-name collisions

BindGen API-reference names are compared without case on every host. Same-stem inputs receive stable path hashes; any remaining collision receives a numeric suffix. Allocation follows input path order, so reordering the input list does not change these names. Use the returned API-reference records or manifest to locate the generated `.api.vs.txt` files.

<a id="binding-marker-attributes"></a>

Marker attributes belong to the input declarations; the SDK does not inject their definitions. Library options default to `BindingAttributeName="VeroScriptBindingAttribute"` and `IgnoreAttributeName="VeroScriptIgnoreAttribute"`, and can select equivalent names. Generated live registration defaults to `VeroScript.GeneratedBindings.VeroScriptCSharpBindings`; `RegistrationNamespace` and `RegistrationClassName` customize it. These names do not enable registration when that output is disabled.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
