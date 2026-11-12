# Stb

Binary PGM/PPM samples are normalized to RGBA8 using `Maxval` (1–65535). Invalid maxima or samples outside 0–Maxval fail without changing output. Full-range 255/65535 decoding retains its previous quantization and byte order behavior.

## Use the service

`Stb::CreateImageService()` supplies `image/stb`. PNG, JPEG, BMP, TGA and the other enabled stb_image formats decode to straight-alpha sRGB RGBA8; GIF supplies its first frame. HDR/float output is disabled. Top-down BMP rows, 16-bit PGM/PPM byte order and CgBI PNG are normalized to that contract. Failure preserves output and explicit decode limits apply.

A service supports concurrent calls with separate outputs/errors and immutable input during each call. Builds without decoder thread-local storage serialize decoding across instances. Use the Image script bindings for `VS::Image`; the optional `VeroScriptStbModule` supplies the provider, not that script API. Install Image, Stb and their applicable Bindings components together.

## SDK and integration

```cmake
find_package(VeroScriptStb 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Stb)
```

The native SDK is usable without Core. Script bindings and native providers are optional and must match the actual Core build, public ABI and toolchain; a matching product version alone is insufficient. Install the native dependencies as well as any requested Bindings components.

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/en-US/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/en-US/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/en-US/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
