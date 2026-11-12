# RmlUi

## Use the service

`RmlUi::CreateBackend` adapts GUI trees to the Graphics renderer. A native host provides both font and text-layout services, and Image unless it supplies a decoder for loaded images. Rendering borrows a device and exactly one surface/texture target; explicit layout-only mode requires neither rendering target. Synchronize and update successfully before rendering. GUI service: `gui` / `veroscript.gui`; its interface version follows the Gui release version, currently `0.1.1`.

The adapter supports typed layout, text, basic controls, input and the Graphics composition capabilities it reports. ML/CSS use the GUI frontend, not arbitrary browser CSS or native RML. Close the backend on its owner thread and collect deferred retirement before closing borrowed resources. Failed cleanup retains resources for retry. Layout/renderer/font capabilities and limits are part of the selected native providers; installing an editor plug-in does not supply them.

## SDK and integration

```cmake
find_package(VeroScriptRmlUi 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::RmlUi)
```

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/en-US/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/en-US/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/en-US/index.md)

<a id="composition-resources"></a>

## Composition and resource limits

RmlUi supports nested/inverse masks, layer captures, opacity, brightness, contrast, invert, grayscale, saturate, sepia, hue rotation, blur and drop shadow. Masks and filters require Graphics composition support; unsupported operations report errors. Blur sigma is at most 64 pixels and drop-shadow sigma at most 64/3 pixels. Gradients rasterize once per geometry into cached textures, limited to 8192 per dimension and 16,777,216 pixels.

Loaded resource identities are immutable. Missing or invalid images produce update/render errors; use a new identity or recreate the context after replacing failed bytes. Image input is limited to 64 MiB, decoded images to 16,384 per dimension and 16,777,216 pixels, and retained custom font data to 128 MiB in total. The standard `ReadGuiResourceFile` reader also limits each file to 64 MiB; a custom reader does not remove the image or total font limits. Configure readers before textures are in use.

<a id="font-atlas-and-services"></a>

Font atlas textures are 1024 × 1024 pixels; glyphs/effects must fit including packing padding. Failure reports `rmlui.font_atlas_failed` and remains dirty so retries also report it rather than silently dropping text. Live RmlUi contexts share the font registry and must use the same font and text-layout service instances; equal backend IDs alone are insufficient. Different instances can be selected after all contexts close. Supply these resources through `RmlUi::FOptions` or the GUI backend options; do not replace them while contexts are alive.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
