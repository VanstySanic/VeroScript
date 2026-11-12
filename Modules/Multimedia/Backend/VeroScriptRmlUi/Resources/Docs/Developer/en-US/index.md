# Maintaining VeroScriptRmlUi

## Source ownership and verification

This module owns the GUI rendering, layout and input adapter and its vendored dependencies. Its public surface is declared in [`Public/VeroScriptRmlUi.h`](../../../../Public/VeroScriptRmlUi.h); its module-specific inputs are in [`Resources/Tests`](../../../Tests).

Shared provider boundaries, rebuild requirements, verification and legal-resource handling are maintained in [Multimedia maintenance](../../../../../../../../Resources/Docs/Developer/en-US/multimedia.md).


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/en-US/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/en-US/GENERAL.md)

Text measurement uses bounded content caches: widths include paragraph and line ranges, direction, language, spacing, kerning and the fallback-face revision; paragraph analysis also keys the actual text-layout service. Recreating an immutable paragraph owner does not require new analysis. The per-face width cache is limited to 1,024 entries / 512 KiB, and paragraph analysis to 2,048 entries / 8 MiB of text. Full glyph, caret and selection layout retains the normal shaping path.

Nested auto-height vertical containers use block flow with private gap elements to avoid repeated intrinsic measurement in nested flex layouts. Hidden children retain space; collapsed and floating children do not enter this flow. Horizontal, grid, wrap, splitter and editable-content layouts retain their specialized paths. Two-stop affine linear gradients can use vertex colors; other gradients retain rasterization. Isolated clip-mask passes use bounded scratch targets and fall back to the general compositor when the budget is insufficient. Nested, soft and inverse masks retain premultiplied source-over semantics.

The most recent paragraph analysis remains available while measuring it, including a single paragraph larger than the LRU text budget; it is replaced by the next paragraph analysis or released with the font resources.

<a id="native-provider-maintenance"></a>

The optional `VeroScriptRmlUiModule` supplies the native GUI provider and must match the Core/Gui build. Keep locally patched RmlUi headers, compiled library and patch/notice inventory synchronized; do not mix a system RmlUi build with these adapter headers. The native SDK’s data path does not require the optional Core binding provider. Font, text-layout, Image and Graphics remain selected dependencies; service-instance lifetime and atlas failures follow the public RmlUi guide. Preserve those boundaries when changing resource caches or synchronization.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
