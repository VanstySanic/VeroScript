# VeroScript Installer technical documentation

`VeroScriptInstaller` contains the cross-platform installation, repair,
uninstallation, bundle, and package-layout implementation. This document is for
maintainers and release tooling authors. For end-user commands, see the
[installation and distribution guide](../../User/en-US/index.md).

## Module ownership

The module is split into four layers:

- `Core/` owns bundle archives, manifests, package metadata, validation, path
  safety, platform information, retries, and the install engine.
- `App/` owns the graphical Avalonia application, progress/reporting, and UI
  localization.
- `Tool/` owns the command-line bundle/build entry point.
- `Tests/` verifies core, UI-facing, bundle, and packaging contracts.

The core must remain usable without the graphical application. UI text,
progress presentation, and process wiring must not be required to validate a
manifest or perform a safe filesystem operation.

## Bundle and manifest invariants

Bundle archives, product definitions, package manifests, and installer manifests
must be validated before extraction or replacement. Paths are validated against
the intended root; traversal, absolute escape, unexpected foreign content, and
ambiguous package entries must fail closed. Retriable filesystem operations must
not turn a partial operation into a reported success.

Install, repair, and uninstall operations should be idempotent where the public
contract promises it. Atomic replacement, cleanup, rollback behavior, and
failure reporting must be documented from the core implementation and tests.

## Platform integration

Windows and Unix path integration are separate implementations behind the core
interface. PATH updates, shell configuration, permissions, and platform
discovery must preserve the platform-specific rules; do not normalize them into
one unsafe string operation.

## Installation record and rollback details

Installation is staged per component and writes an installation record that
tracks component roots, owned files, product identity, manifest fingerprint,
and any runtime `PATH` mutation. Repair requires the exact installer package
identity, product version, platform, architecture, and manifest fingerprint
that the record describes. Uninstall removes only recorded files and owned PATH
entries; files not owned by the installation are preserved.

Destination roots must not overlap the protected installer payload or one
another, and managed destinations and their ancestors are checked for links.
Relocation validates the existing record, sibling records, destination safety,
and ownership before staging the move. Pre-commit failures trigger rollback;
post-commit cleanup failures retain journals for a later cleanup attempt without
reporting the completed install as failed. `LoadRecordAsync` performs transaction
recovery before reading the record, including GUI repair/uninstall startup.

Current formal installers contain functional components; documentation and examples are distributed through Resources. Legacy manifests and records may still contain auxiliary components for repair/uninstall, but the graphical wizard excludes them from new selections and rejects selections that depend on unavailable components. Record the components actually installed instead of inferring them from the current source tree.

## Localization and release assets

UI source catalogs are owned by this module under `App/Resources/Localization/<culture>/`. The installer embeds these JSON catalogs into its application assembly. Culture names must be qualified canonical BCP 47 tags. Selection and English fallback are catalog-driven. Resource keys, protocol fields, enum names, command options, paths, version values and diagnostic IDs are technical identifiers and must retain their spelling.

## Detailed pages

- [Installer lifecycle and safety](lifecycle-and-safety.md)
- [Installer build and bundle contract](build-and-bundle.md)
- [Release validation procedure](release-validation.md)

## Source of truth

Use `Core/`, `App/`, `Tool/`, project files, bundle/manifest tests, and packaging
smoke tests. Update this document whenever archive layout, manifest schema,
path-safety policy, rollback behavior, platform integration, install lifecycle,
or installer localization changes.

- [Installer automation](automation.md)

- [Release layout and publishing rules](release-layout.md)

[File-association ownership and recovery](file-associations.md)

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
