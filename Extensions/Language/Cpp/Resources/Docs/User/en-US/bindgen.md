# C++ BindGen

`VeroScriptCppBindGen` converts the supported C++ declaration subset into a
VeroScript Binding Catalog. The catalog is compiler and tooling metadata. It is
not a runtime implementation, and generated callback stubs are safe catalog
stubs rather than native invocation thunks; the host adapter still registers
the actual C++ functions.

## Inputs and outputs

The generator provides `GenerateBindings` for an AST and
`GenerateBindingsFromSource`, `GenerateBindingsFromFile`, and
`GenerateBindingsFromFiles` for source inputs. `GenerateBindingProject` merges
multiple files and returns:

- one authoritative `CatalogJson`;
- optional per-input `ApiReferences` for human documentation;
- a deterministic `ManifestJson`;
- diagnostics and a `Succeeded()` result.

`FBindGenOptions::bEmitApiReference` controls the optional API reference.
Catalog, references, and manifest are committed as one transaction. Colliding
input basenames receive deterministic path hashes, so output does not depend
on scan order.

`FBindGenProjectLayout` names the merged catalog, the API-reference directory,
and the project manifest. Keep the merged catalog authoritative for compiler
and IDE input; per-input references are documentation views and do not replace
the merged declaration set.

The project manifest uses these stable fields:

```json
{
	"version": 1,
	"kind": "veroscript.cpp-binding-project",
	"catalogRole": "merged",
	"catalog": "bindings.json",
	"inputs": ["Include/Types.hpp"],
	"apiReferences": [
		{
			"input": "Include/Types.hpp",
			"path": "api/Types.vs.txt"
		}
	]
}
```

The merged catalog is emitted once. API references are optional human-readable
views; their `input` and `path` entries describe the generated relationship.

## Command-line usage

```powershell
VeroScriptCppBindGen Resources/Examples/BindingInput.hpp Resources/Examples/BindingExtras.hpp `
  --catalog build/BindingInput.bindings.json `
  --api-reference build/BindingInput.vs.txt
```

For a project-level catalog and one reference file per input:

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

The merged catalog is the authoritative compiler input. API-reference files
are for people and documentation; do not compile them as implementation.

The CLI options are `--catalog <output.json>` (required), `--api-reference
<output.vs.txt>` for one reference, `--api-reference-dir <directory>` for one
reference per input, `--manifest <output.json>` for the project manifest,
`--version`, and `--help`. `--api-reference` and `--api-reference-dir` are
mutually exclusive.

## Preserved and rejected declarations

BindGen preserves qualified names, enum identity and underlying types, member
kinds, constructors, operators, const methods, `ref`/`const ref` parameters,
ordinary type aliases, and type-parameter alias templates. Alias defaults are
rejected because they are not representable in the catalog schema. It accepts
integer and portable single-character enum expressions using grouping, prior
enumerators, unary operators, arithmetic, shifts, bitwise, boolean, logical,
relational, and equality operations, with checked range validation.

C++ attributes on types, enums, enumerators, fields, functions, and parameters
are written to structured annotations. Unscoped attributes and the reserved
`veroscript::`/`vero::` namespaces are accepted; unrelated vendor namespaces
must be mapped or are rejected. Valueless attributes remain flag arguments and
named attribute values round-trip in the catalog.

It rejects raw pointers, return references, host-to-host inheritance, deleted
members, mutable static fields, named-method overloads, overloaded free
functions, default arguments, free global variables, reference fields,
internal-linkage free functions, and enum values outside the declared
underlying range. The catalog is metadata only; the host adapter must still
register the actual native implementations.

See [Host bindings](bindings.md) for the complete catalog field reference and
the runtime registration boundary.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
