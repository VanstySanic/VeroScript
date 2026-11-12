# C# frontend

`VeroScriptCSharpFrontend` is the semantic C# declaration front end used by
the C# binding generator. It uses Roslyn internally but
publishes a VeroScript-owned declaration model, so consumers do not depend on
Roslyn syntax or semantic object lifetimes at their public boundary.

## Declaration model

The frontend accepts multiple source files and records partial declarations,
namespaces, classes, structs, interfaces, records, enums, delegates, fields,
properties, events, constructors, methods, operators, attributes, XML
documentation, generic type shape, source locations, and `ref`/`in`/`out`
parameter and return metadata. Roslyn diagnostic IDs and source locations are
preserved for downstream tools.

Type mapping covers C# primitives, one-dimensional arrays, `List<T>`,
`Dictionary<TKey,TValue>`, `SortedDictionary<TKey,TValue>`, `HashSet<T>`,
`Queue<T>`, `Stack<T>`, declared enums, and explicitly known host types.
Nullable types, pointers, delegates, `dynamic`, unresolved types, and open
type parameters fail closed rather than losing their semantics.

Property and nested-type accessibility are evaluated independently. Implicit
CLR base types are distinguished from user-authored inheritance so a binding
consumer does not expose private mutation or mistake `System.ValueType` for a
host base class.

## Relationship to BindGen and Transpiler

This module is a build-time semantic library, not the managed runtime SDK and
not a VeroScript execution engine. `VeroScriptCSharpBindGen` consumes its model
to produce Binding Catalogs, live registration source, and AOT bridge source.
`VeroScriptCSharpTranspiler` consumes the same model for strict C#-to-VeroScript
source conversion and source maps.
Roslyn is a build/tooling dependency of the frontend only; the runtime SDK
remains dependency-free and NativeAOT-compatible.

Use the frontend directly when a build tool needs declaration structure or
source diagnostics. Use the BindGen CLI when the result must be consumed by
VeroScript compilation or host registration.

## Standard container generic arity

<a id="container-mapping-arity"></a>

Standard container mapping requires the declared generic arity: one argument for `List<T>`, `HashSet<T>`, `Queue<T>` and `Stack<T>`, and two for `Dictionary<TKey,TValue>` and `SortedDictionary<TKey,TValue>`. A same-named custom type with a different arity is not converted to a malformed built-in container; it needs an explicit known-host mapping or fails with `VSCF2008`.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
