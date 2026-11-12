# Localization resources and deployment

This page is for catalog maintainers and build/package authors. For language selection, custom catalogs and SDK usage, see [Diagnostic localization](../../User/en-US/localization.md).

## Source ownership and deployment

Source catalogs belong to each module or area under `Resources/Localization/<culture>/`. Only messages used by several Python tools belong to the shared Python area. Builds and packages assemble the catalogs required by their dependencies into the deployed `localization/` directory. Runtime/Core SDK packages include CLI, Tooling, LSP, DAP and Interop dependencies; WebAssembly includes core and C++ embedding catalogs; Python packages include core, C++, Interop and Python tool catalogs. IDE and installer UI catalogs remain separate.

```text
VeroScript/Resources/Localization/<culture>/
  language.json, runtime.json, shared.json, host-binding.json, memory.json, i18n.json
Extensions/Language/Cpp/VeroScriptCpp/Resources/Localization/<culture>/embedding.json
VeroScriptInterop/Resources/Localization/<culture>/interop.json
VeroScriptTooling/Resources/Localization/<culture>/
  tooling.json, lsp.json, dap.json
Extensions/Language/Python/Resources/Localization/<culture>/python-tools.json
Extensions/Language/Python/VeroScriptPythonFrontend/Resources/Localization/<culture>/python-frontend.json
Extensions/Language/Python/VeroScriptPythonBindGen/Resources/Localization/<culture>/python-bindgen.json
Extensions/Language/Python/VeroScriptPythonTranspiler/Resources/Localization/<culture>/python-transpiler.json
Extensions/Language/Python/VeroScriptPythonAot/Resources/Localization/<culture>/python-aot.json
VeroScriptInstaller/App/Resources/Localization/<culture>/
```

When running directly from a source checkout, the core discovers its own catalogs and each SDK or tooling module adds its own Resources/Localization directory. Explicitly loaded and deployed catalogs remain authoritative; source resources do not augment a successfully loaded replacement.

## Source discovery

`VERO_LOCALIZATION_DIR` selects a complete localization root. If it is unset or cannot be loaded, the core tries the following locations in order and stops at the first valid tree:

1. `localization` beside the executable, then `../assets/localization` relative to the executable directory;
2. up to eight executable-directory ancestors: each ancestor's `localization`, then the core catalogs of a source module discovered at that ancestor or among its immediate repository children;
3. `localization` in the current working directory;
4. source-module discovery in the current directory, its parent, then its grandparent; a discovered module contributes `VeroScript/Resources/Localization`.

Source discovery accepts a module containing `VeroScript/Public/VeroScriptVersion.h`, or a repository containing `CMakeLists.txt` with exactly one such immediate child. The outer directory may be renamed; a missing or ambiguous source module is ignored. `InitializeModuleFromEnvironment` adds a module-relative `Resources/Localization` tree only after initialization has selected source catalogs. Successfully loaded explicit or deployed trees keep their own resource set.

## Release checks

- Every distribution must include a complete and valid `en-US` directory;
- every additional language required by a release must be copied, embedded, or packaged with the target host;
- translated culture paths and message IDs must be subsets of the English baseline;
- do not use the old domain-first `en.json`/`zh-CN.json` layout; the current format is `<culture>/<component>.json`;
- diagnostic IDs are more stable than display text, so hosts, LSP, and DAP should use `MessageId` for logic;
- after packaging, verify the available language set, English fallback for missing keys, and diagnostic placeholder substitution.


## Mobile resources and localization

The shared mobile catalog inventory, deployment paths and explicit loading procedure are maintained in [Mobile](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/en-US/index.md#mobile-localization).

Translation buffer ownership, errors and export maintenance are specified in [Interop](../../../../../VeroScriptInterop/Resources/Docs/Developer/en-US/abi-and-lifetime.md#localization-abi).

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](localization-and-resources.md) · [fr-FR](../fr-FR/localization-and-resources.md) · [ja-JP](../ja-JP/localization-and-resources.md) · [ko-KR](../ko-KR/localization-and-resources.md) · [zh-CN](../zh-CN/localization-and-resources.md) · [zh-TW](../zh-TW/localization-and-resources.md)
