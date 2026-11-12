# Font

## Use the service

`Font::IService` owns loaded font bytes through its faces; `IFace` provides metadata, metrics, shaping and rasterization. Shaped positions use signed 26.6 pixel units and clusters retain original UTF-8 byte offsets. `FShapeOptions` defaults to 16 pixels, automatic direction, kerning enabled and the whole byte range. Bitmaps contain A8 coverage or premultiplied sRGB RGBA8, which differs from Image output. Contract: `font` / `veroscript.font` / `0.1.2`.

Pass an explicit ITextLayoutService as the first argument to FTextLayout::Create(service, text, direction, error). Font owns the generic service/analysis interfaces; the separate FontBackend supplies CreateTextLayoutService() using SheenBidi 3.0.0 / Unicode 17.0.0. Its service contract is text-layout / veroscript.text-layout / 0.1.0. Preserve the service instance in layout cache identity; backend ID alone is insufficient.

`FTextLayout::Create(service, text, direction, error)` analyzes immutable logical text through the explicitly selected layout service. `GetVisualRuns` applies line-specific bidi ordering; a line must stay in one paragraph and begin/end at UTF-8 boundaries. `ShapeLine` retains surrounding context and returns visual clusters and caret stops. The caller still selects line breaks and owns atlases/GPU resources. `ShapeWithFallback` retains selected faces. The default Lato Latin font is registered as `Vero Sans`; additional scripts require suitable supplied fonts. Registered scripts use `VS::Font::Service::Create("font-backend").LoadDefault()`; script positions are pixels.

## SDK and integration

```cmake
find_package(VeroScriptFont 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Font)
```

The native SDK is usable without Core. Script bindings and native providers are optional and must match the actual Core build, public ABI and toolchain; a matching product version alone is insufficient. Install the native dependencies as well as any requested Bindings components.

- [Image](../../../../../VeroScriptImage/Resources/Docs/User/en-US/index.md) · [Font](index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/en-US/index.md)

ShapeLine applies letter spacing once at the last glyph of each visible cluster; returned glyph metrics keep their original advances. A caret X is a logical pen endpoint, not a visual bounding-box edge. Negative spacing can place that endpoint inside a cluster’s visual envelope.

<a id="font-layout-boundaries"></a>

Automatic paragraph direction uses its first strong character; explicit left-to-right/right-to-left fixes the base direction. Line visual runs follow UAX #9 L1/L2 after wrapping. `FTextLine` exposes visual-order glyphs, logical byte-cluster ranges and caret stops; a direction boundary can have two caret positions selected by affinity. Invisible format-only clusters retain logical caret endpoints without glyphs or spacing. Keep clipboard text and offsets logical. Precise ligature-internal carets, complete grapheme navigation, wrapping and alignment remain host responsibilities.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
