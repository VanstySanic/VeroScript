# Maintaining VeroScriptFontBackend

## Source ownership and verification

This module owns font shaping, rasterization and bidirectional text-analysis providers. Its public surface is declared in [`Public/VeroScriptFontBackend.h`](../../../../Public/VeroScriptFontBackend.h); its module-specific inputs are in [`Resources/Tests`](../../../Tests).

Shared provider boundaries, rebuild requirements, verification and legal-resource handling are maintained in [Multimedia maintenance](../../../../../../../../Resources/Docs/Developer/en-US/multimedia.md).


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/en-US/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/en-US/GENERAL.md)

<a id="native-provider-maintenance"></a>

The optional `VeroScriptFontBackendModule` registers font and text-layout services separately. Static hosts use `veroscript_font_backend_query`; dynamic extension hosts use `veroscript_extension_query`. Validate each requested service against Font’s own contract before casting. Retained faces and analysis objects must keep the provider lease alive independently of the original registry handle. Keep FreeType/HarfBuzz/SheenBidi inputs, notices and the native provider build here; Font owns the public contracts, default font and independent mock-contract tests.

## Provider build and lifecycle

`VEROSCRIPT_BUILD_FONT_BACKEND` selects this module. `VEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION` builds its matched-Core provider; `VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER` selects its named static entry. Font owns the generic interfaces and never discovers a concrete backend. Keep FreeType/HarfBuzz faces and SheenBidi analysis, pinned inputs and legal resources here. Font and text-layout remain independent service descriptors, even when their backend IDs match.

Script regressions explicitly link the matching `VeroScriptBytecodeVm` and use its public factory. Retain faces, analysis objects and layout snapshots separately after releasing registry handles when checking module leases. Check native-only and matched-Core builds, dependency isolation and relocated SDK consumers when execution is permitted; record actual failures and skipped targets.

[Public font/layout contracts](../../../../../../VeroScriptFont/Resources/Docs/User/en-US/index.md) · [GUI consumer maintenance](../../../../../../VeroScriptGui/Resources/Docs/Developer/en-US/index.md#gui-text-layout-maintenance)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
