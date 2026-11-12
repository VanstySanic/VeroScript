# Font

## 使用服务

`Font::IService` 的 face 保有字体字节，`IFace` 提供元信息、度量、塑形和栅格化。塑形坐标为有符号 26.6 像素，cluster 为原始 UTF-8 字节偏移。`FShapeOptions` 默认 16 像素、自动方向、启用 kerning、整个字节范围。位图为 A8 覆盖或预乘 sRGB RGBA8，不能与 Image 的直通 alpha 混同。服务契约为 `font` / `veroscript.font` / `0.1.2`。

FTextLayout::Create(service,text,direction,error) 的首参必须显式提供 ITextLayoutService。Font 只拥有通用服务/分析接口；独立 FontBackend 通过 SheenBidi 3.0.0 / Unicode 17.0.0 提供 CreateTextLayoutService()。该服务契约为 text-layout / veroscript.text-layout / 0.1.0。布局缓存还须包含服务实例身份，不能只按后端 ID 缓存。

`FTextLayout::Create(service, text, direction, error)` 通过显式选择的布局服务分析不可变逻辑文本。`GetVisualRuns` 进行逐行双向重排，行须位于同一段落并以 UTF-8 边界起止。`ShapeLine` 保留上下文，返回视觉 cluster 和插入点；调用者仍选择换行并管理 atlas/GPU。`ShapeWithFallback` 保有所选 face。默认 Lato Latin 注册为 `Vero Sans`，其他文字需提供合适字体。脚本使用 `VS::Font::Service::Create("font-backend").LoadDefault()`，脚本坐标单位为像素。

## SDK 与集成

```cmake
find_package(VeroScriptFont 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Font)
```

原生 SDK 不要求 Core。可选脚本绑定和原生提供者必须匹配实际 Core 构建、公开 ABI 及工具链，仅产品版本一致不足以保证兼容。请同时安装原生依赖及需要的 Bindings 组件。

- [Image](../../../../../VeroScriptImage/Resources/Docs/User/zh-CN/index.md) · [Font](index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/zh-CN/index.md)

ShapeLine 在每个可见簇的最后一个 glyph 上应用一次字间距，返回的 glyph 度量保留原始推进量。光标 X 是逻辑画笔终点，不是视觉包围盒边缘；负字间距可能使终点落入簇的视觉范围内部。

<a id="font-layout-boundaries"></a>

自动段落方向使用首个强方向字符，显式左右方向则固定基方向；换行后的视觉运行遵循 UAX #9 L1/L2。`FTextLine` 返回视觉顺序字形、逻辑字节簇范围与光标停靠点；方向边界可以有两个光标位置，由 affinity 选择。仅含不可见格式控制的簇保留逻辑光标端点，不产生字形或字间距。剪贴板文本和偏移保持逻辑顺序。精确连字内部光标、完整字素导航、换行与对齐仍由宿主负责。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
