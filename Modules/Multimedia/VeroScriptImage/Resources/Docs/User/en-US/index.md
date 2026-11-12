# Image

## Use the service

`Image::IService::Decode` produces tightly packed straight-alpha sRGB RGBA8; failed decoding preserves the previous result. `Image::Validate` checks dimensions and buffer size. `FDecodeOptions` defaults to 16,384 per dimension, 16,777,216 pixels and 64 MiB of source bytes. The caller owns resource lookup, caching and GPU upload. Contract: `image` / `veroscript.image` / `0.1.0`.

A provider such as `stb` supplies the decoder. With Image Bindings registered, use `VS::Image::Service::Create("stb").Load("image.png")` or `Decode(bytes)`. Script sizes and indices are `int64`; the script adapter applies the default limits, including before copying an oversized source string. An injected reader resolves resources; without one, file paths use the process working directory.

## SDK and integration

```cmake
find_package(VeroScriptImage 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Image)
```

The native SDK is usable without Core. Script bindings and native providers are optional and must match the actual Core build, public ABI and toolchain; a matching product version alone is insufficient. Install the native dependencies as well as any requested Bindings components.

- [Image](index.md) · [Font](../../../../../VeroScriptFont/Resources/Docs/User/en-US/index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/en-US/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
