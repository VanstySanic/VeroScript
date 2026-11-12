# Maintaining VeroScriptFont

## Source ownership and verification

This module owns font, shaping and text-layout contracts. Its public surface is declared in [`Public/VeroScriptFontLayout.h`](../../../../Public/VeroScriptFontLayout.h); its module-specific inputs are in [`Resources/Tests`](../../../Tests).

Shared provider boundaries, rebuild requirements, verification and legal-resource handling are maintained in [Multimedia maintenance](../../../../../../../Resources/Docs/Developer/en-US/multimedia.md).


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/en-US/index.md)
- [Standards](../../../../../../../Resources/Docs/Standards/en-US/GENERAL.md)

<a id="font-layout-maintenance"></a>

Immutable layouts retain their text and selected service. Their synchronized line-run cache holds at most four immutable analyses; styled fragments select intersecting logical runs. Skip UTF-8 validation only when a validated layout owns the exact buffer and length. Preserve cluster-range validation and glyph order in fallback shaping. `VeroScriptFontContractTests` and `VeroScriptFontLayoutContractTests` use mocks without a concrete provider; backend corpus and script tests belong to FontBackend. Bindings must select a VM explicitly rather than depend on Core-private test headers.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
