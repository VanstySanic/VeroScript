# FontBackend

## Use the service

`FontBackend::CreateFontService()` supplies `font/font-backend`. FreeType loads fonts, metrics and A8/color glyphs; HarfBuzz shapes contextual forms, ligatures, script, language and direction. Each face owns its bytes and synchronizes mutation. Public service objects expose no FreeType/HarfBuzz types.

Use the native factories, or the registered Font script API with backend `font-backend` in a compatible host. The provider keeps a module lease through retained faces. Install Font and FontBackend, plus the optional provider/Font Bindings components when needed. The default font and its separate license belong to Font. Paragraph bidi analysis belongs to `FTextLayout`; shaping support also depends on the selected fonts.

## SDK and integration

```cmake
find_package(VeroScriptFontBackend 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::FontBackend)
```

The native SDK is usable without Core. Script bindings and native providers are optional and must match the actual Core build, public ABI and toolchain; a matching product version alone is insufficient. Install the native dependencies as well as any requested Bindings components.

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/en-US/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/en-US/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/en-US/index.md)

FontBackend owns FreeType, HarfBuzz and SheenBidi, including the paragraph-analysis implementation and original notices. Native factories CreateFontService() and CreateTextLayoutService() provide separate font and text-layout services; both register under backend font-backend when the optional native provider is built.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
