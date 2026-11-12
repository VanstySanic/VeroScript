# C++ frontend, BindGen, and Transpiler

## Frontend AST

`VeroScriptCppFrontend` parses a bounded C++17 subset into a stable model of
tokens, source locations, attributes, types, parameters, templates, records,
enums, variables, functions, ordinary `typedef`/`using` aliases, namespaces,
and bodies. It retains
qualified names, C linkage, storage and semantic qualifiers, constructors,
destructors, friends, and same-file out-of-class definitions when representable.

`FParseOptions` controls include/pragma handling, unknown preprocessor
directives, and function-body parsing. `FTypeMapOptions` controls raw host
pointers, rvalue references, known host and enum types, known type parameters,
and project aliases. These are explicit options, not hidden global policy.

The collector merges compatible declarations and forward declarations across
inputs. It also pairs a conventional out-of-class member definition from one
input with the complete record declaration from another. It rejects duplicate
definitions, repeated input paths, internal-linkage collisions, anonymous
namespaces, unsupported templates, and constructs whose ownership or
control-flow meaning cannot be preserved.

## Binding Catalog generation

BindGen emits machine-readable Binding Catalog JSON. It may also emit an API
reference for each input and a project manifest. The authoritative catalog is
merged; API references are review artifacts and must not be compiled. Structured
attributes, aliases, enum identity, overload information, member flags, and
documentation are retained only when the catalog schema can represent them.

Project generation resolves all supplied declarations first, gives colliding
input basenames deterministic names, and replaces catalog, references, and
manifest as one transaction.

## Source Transpiler

Transpiler lowers only constructs with a direct VeroScript meaning. It emits
VeroScript source and declaration-level source maps, and project mode emits one
module and map per input plus a manifest. Generated source is compiled by the
real VeroScript compiler by default. Unsupported ownership, RTTI, pointer
access, advanced templates, unsupported `std` facilities, and unrepresentable
attributes fail explicitly.

Member `operator()` and `operator[]` retain overloads and reference parameters. Source conversion uses `T&` / `const T&` for reference signatures, including reference returns; operator arguments bind references implicitly under the core rules. BindGen preserves operator and reference-parameter flags, but rejects native reference returns because the current Binding Catalog cannot represent their storage and lifetime.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](frontend-and-tools.md) · [fr-FR](../fr-FR/frontend-and-tools.md) · [ja-JP](../ja-JP/frontend-and-tools.md) · [ko-KR](../ko-KR/frontend-and-tools.md) · [zh-CN](../zh-CN/frontend-and-tools.md) · [zh-TW](../zh-TW/frontend-and-tools.md)
