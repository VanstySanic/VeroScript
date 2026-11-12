# Core build and runtime assets

[Replaceable VM backends](vm-backends.md)

## Build target

See [Replaceable VM backends](vm-backends.md#vm-provider-assembly).

## Asset dependency

The target depends on `VeroScriptRuntimeAssets`. It stages catalogs with the shared `Resources/Tools/CMake/VeroScriptLocalization.cmake` deployment profile, removes obsolete catalog JSON files, copies changed catalogs, and copies standard-library files to the runtime output directory. The explicit dependency also applies to individual test targets so their catalog resources reflect the current sources.

The locale loader accepts `VERO_LOCALIZATION_DIR` and then searches deployed and source-checkout roots; see [Diagnostic localization](localization-and-resources.md) for the exact order. A valid tree requires an `en-US` baseline, qualified canonical BCP 47 culture directories and unique message IDs. Translated files and keys may be subsets of English and fall back to that baseline. Modules and standard-library roots can merge valid catalogs without overriding existing IDs. Available languages are discovered from loaded catalogs.

## Compatibility

Public bytecode, package, diagnostic, host-binding, and locale behavior is
independent of whether asmjit is enabled. Architecture-specific native leaves
must be described as execution capabilities, not as a new language or bytecode
version.

## Build infrastructure and source catalog extension

The standalone repository owns the helpers under `Resources/Tools/CMake`; use these paths directly from the repository root, without a `Script/` prefix. `InitializeModuleFromEnvironment` is declared in the public locale API and uses a path relative to the discovered source module, for example `VeroScriptTooling` or `Extensions/Language/Cpp/VeroScriptCpp`. It merges without overriding existing IDs only when the core selected source catalogs. An explicit catalog replacement clears that source-discovery state. `VeroScriptLocaleSourceTests` verifies renamed-module discovery and module catalog extension.

- [Localization resources and deployment](localization-and-resources.md)

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](build-and-assets.md) · [fr-FR](../fr-FR/build-and-assets.md) · [ja-JP](../ja-JP/build-and-assets.md) · [ko-KR](../ko-KR/build-and-assets.md) · [zh-CN](../zh-CN/build-and-assets.md) · [zh-TW](../zh-TW/build-and-assets.md)
