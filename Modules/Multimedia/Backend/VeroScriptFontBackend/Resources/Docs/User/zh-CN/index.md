# FontBackend

## 使用服务

`FontBackend::CreateFontService()` 提供 `font/font-backend`。FreeType 负责加载、度量、A8/彩色 glyph，HarfBuzz 处理上下文形式、连字、文字、语言及方向。每个 face 拥有输入字节并同步修改，公开服务不暴露第三方类型。

使用原生工厂，或在兼容宿主中通过已注册的 Font 脚本 API 选择 `font-backend`。保留的 face 同时保有提供者模块。安装 Font/FontBackend，并按需安装提供者及 Font Bindings。默认字体及许可归 Font。段落双向分析由 `FTextLayout` 提供；文字覆盖仍取决于所选字体。

## SDK 与集成

```cmake
find_package(VeroScriptFontBackend 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::FontBackend)
```

原生 SDK 不要求 Core。可选脚本绑定和原生提供者必须匹配实际 Core 构建、公开 ABI 及工具链，仅产品版本一致不足以保证兼容。请同时安装原生依赖及需要的 Bindings 组件。

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/zh-CN/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/zh-CN/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/zh-CN/index.md)

FontBackend 拥有 FreeType、HarfBuzz、SheenBidi、段落分析实现及原始许可。CreateFontService() 与 CreateTextLayoutService() 提供独立服务；可选原生提供者构建后，两者的后端 ID 都是 font-backend。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
