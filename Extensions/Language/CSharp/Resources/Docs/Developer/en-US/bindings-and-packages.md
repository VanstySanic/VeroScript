# C# bindings, packages, and compatibility

## Host binding model

Managed definitions represent host functions, host types, members, operators,
annotations, enum values, documentation, type parameters, and protocol
callbacks. Host type protocols may include clone, equality, hashing, and
subclass factories. Generic host-type templates are instantiated through the
Interop callback contract and must retain the type identity used by the catalog.

The generated catalog is metadata. Registration callbacks connect live managed
objects and functions; a catalog alone does not invoke a C# method.

## Packages

Package build options include package kind, package name, package version, host
metadata, and debug-info policy. A package is read from bytes, exposes metadata
and canonical signed bytes, and is disposed explicitly. Loading consumes the
compiled module only when the session accepts the package and its kind/entry
shape.

## API compatibility

The checked API contract is the compatibility boundary for public managed types,
constructors, properties, enum values, callback signatures, and target
frameworks. Native layout changes must be reviewed with
`VeroScriptInterop.h`. A wrapper change is incomplete until the managed tests,
public API contract, package verification, and the native ABI consumer remain
consistent.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](bindings-and-packages.md) · [fr-FR](../fr-FR/bindings-and-packages.md) · [ja-JP](../ja-JP/bindings-and-packages.md) · [ko-KR](../ko-KR/bindings-and-packages.md) · [zh-CN](../zh-CN/bindings-and-packages.md) · [zh-TW](../zh-TW/bindings-and-packages.md)
