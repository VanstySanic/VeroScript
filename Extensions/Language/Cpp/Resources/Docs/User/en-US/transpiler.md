# C++ Transpiler

`VeroScriptCppTranspiler` converts a strict, semantics-preserving C++17 subset
to VeroScript source. It shares the AST and type mapping from the [C++ front
end](frontend.md), but it is independent from BindGen and from the
language-neutral Interop ABI.

## Single-file conversion

```powershell
VeroScriptCppTranspiler Resources/Examples/Basic.cpp `
  -o build/Basic.vs `
  --source-map build/Basic.vs.map.json
```

The result contains VeroScript source and an optional declaration-level source
map. Generated source is validated by the VeroScript compiler by default, and
output is written only after parsing, lowering, and validation succeed.

`FTranspileOptions` controls generated-source validation, the generated source
path used for source maps and diagnostics, additional known host and enum types,
known type aliases, and explicit attribute mappings. The corresponding project
options carry validation and attribute mappings for project output. A successful
result contains `Source`, `SourceMapJson`, and diagnostics; project mode returns
generated files plus its manifest.

The CLI also accepts repeatable `--attribute-map cpp=vero` mappings. Attributes
in the `veroscript::` and `vero::` namespaces are lowered directly; other
attribute namespaces require an explicit mapping. The mapping is repeatable so
several C++ vendor namespaces can be translated to VeroScript annotation names.
Generated source is validated by default; use `--no-validate` only when a
separate validation stage is responsible for checking the generated files.

The CLI options are:

| Option | Meaning |
| --- | --- |
| `-o`, `--output` | Single-file VeroScript output; requires exactly one input. |
| `--source-map` | Optional single-file declaration-level source map. |
| `--output-dir` | Enables project mode and emits one source and map per input. |
| `--manifest` | Project manifest path; defaults to `transpile.manifest.json` under the output directory. |
| `--attribute-map` | Repeatable exact C++ attribute-to-VeroScript annotation mapping. |
| `--no-validate` | Skips generated-source compilation; reserve for controlled tooling tests. |
| `--version`, `--help` | Print version or usage information. |

## Project conversion

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp `
  --output-dir build/generated `
  --manifest build/generated/transpile.manifest.json
```

Project mode parses and merges every input before emitting anything. It emits
one `.vs` file and source map per input plus a deterministic manifest. The
manifest lists generated modules as implicit imports and enables project-wide
access; pass the same policies to the compiler, runtime, and IDE workspace.
Basename collisions receive deterministic path hashes. Matching declaration and
definition functions and `extern` globals are merged before emission.

The project manifest uses these stable fields:

```json
{
	"version": 1,
	"kind": "veroscript.transpiled-project",
	"allowProjectWideAccess": true,
	"modulePaths": ["Types.vs"],
	"implicitImportPaths": ["Types.vs"],
	"files": [
		{
			"input": "Source/Types.cpp",
			"source": "Types.vs",
			"sourceMap": "Types.vs.map.json"
		}
	]
}
```

The manifest, generated sources, and source maps must come from the same
transpilation run. The paths in the generated-file fields are project-relative.

## Supported subset

The supported subset includes value-based code, namespaces, enums, classes and
structs, constructors, methods, globals, containers, `this->member`, scalar
`static_cast`, `typedef`/`using` aliases, ordinary and `u8` raw strings, and
typed `throw`, `try`/`catch`, catch-all handlers, and rethrow. Alias templates
are expanded in structured declaration types. Class, struct, and free-function
templates with only `typename`/`class` parameters, including trailing default
type arguments, lower to VeroScript open generics. C++ enum initializers use the
same checked evaluator as the front end. `inline static` data members and
non-inline static data members with a matching definition are supported.

Declaration attributes are lowered to VeroScript annotations. Explicit
attributes use `@Name`; parameter attributes are rejected by source conversion
because VeroScript has no parameter-annotation slot, although BindGen retains
them in the catalog. Destructors and supported `friend` declarations are
lowered where their semantics are representable; inline friend definitions are
not accepted. Project mode resolves matching declarations across inputs. Ordinary
method bodies stay in their defining module; special members and static data
retain the core's same-file requirements.

## Rejected semantics

Ownership operations, arbitrary pointer access, RTTI, coroutines, advanced C++
template features, unsupported standard-library facilities, shared ownership,
unresolved `extern` globals, allocation and storage constructs, reference fields
or globals, unsupported
`constexpr`/`consteval`/`constinit`/`noexcept`, `goto`, and unsupported
attribute or friend forms are rejected. The transpiler never silently erases a
semantic feature it cannot represent.

Method-local template parameter lists, explicit C++ function-template argument
calls, non-type parameters, packs, specialization, SFINAE, concepts, and
dependent compile-time expressions remain outside the semantics-preserving
subset. A generic alias referenced inside an opaque function body, friend token
sequence, or initializer is rejected because textual replacement cannot
preserve C++ lookup rules.

Project merging is not a linker. Qualified namespace definitions remain
namespace declarations. Duplicate internal-linkage functions or variables
produce `cpp.merge.internal_linkage_collision`; the tool does not rename them
or select a definition according to input order. Multiple outputs are committed
together so a source map cannot be left out of sync with its generated source.

Project validation compiles the final generated module graph with the manifest's
visibility policies. Errors are mapped back to the original C++ files. A merge
that cannot retain a constructor/destructor's lookup scope is rejected. Out-of-class
definitions for class templates remain unsupported and produce an explicit diagnostic.

## Numeric types and integer literals

Local variables and `static_cast` targets use the same platform mapping as declaration signatures. Integer literals select their type using the C++17 data model of the tool build platform, then use script suffixes; octal, binary and digit separators retain their values. Hexadecimal floating literals and `long double` literals are rejected explicitly. Numeric conversions require values within the target range and report script range errors otherwise; code relying on C++ narrowing truncation or mixed signedness promotion needs an explicit rewrite.

`std::pair<T,U>` maps to the core's soft builtin `pair<T,U>`. If a visible user template of the same name and arity would capture that type, transpilation fails with `cpp.transpile.standard_pair_shadowed`, including in project mode and with generated-source validation disabled. Rename the user template or provide an explicit adapter. Templates of another arity or in unrelated namespaces can coexist.

VeroScript template parameters cannot shadow a global type or ordinary alias.
Such C++ declarations are rejected with `cpp.transpile.template_parameter_shadow_unsupported`
before output, including project mode and disabled generated-source validation.
Rename the C++ template parameter before transpiling. Alias templates are resolved
structurally and retain their own parameter bindings.

Structured declaration types use absolute user type names, including nested template
arguments, so moving declarations between output namespaces cannot rebind them.
Base types retain their original lookup scopes. The target inheritance grammar cannot
express a global base hidden by a lexical namesake; this fails before output with
`cpp.transpile.global_base_shadowed`, including when generated-source validation is disabled.
Builtin container bases and dependent type-parameter bases are rejected with
`cpp.transpile.base_type_unsupported`; inheritance requires a known record type.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
