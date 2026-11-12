# RmlUi

## 使用服务

`RmlUi::CreateBackend` 将 GUI 树接入 Graphics 渲染器。原生宿主需提供字体与文本布局服务，加载图片且没有自定义解码器时也需 Image。渲染借用设备及恰好一个 surface/texture 目标；显式 layout-only 模式无需渲染目标。成功同步及更新后才能渲染。GUI 服务为 `gui` / `veroscript.gui`，接口版本直接跟随 Gui 发行版本，当前为 `0.1.1`。

后端提供类型化布局、文本、基础控件、输入及其声明的 Graphics 合成能力。ML/CSS 属于 GUI 前端，不接受任意浏览器 CSS 或原生 RML。在所属线程关闭，并在关闭借用资源前回收延迟退役对象；清理失败保留资源以供重试。实际能力和限制取决于原生提供者，编辑器插件不会自动安装这些提供者。

## SDK 与集成

```cmake
find_package(VeroScriptRmlUi 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::RmlUi)
```

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/zh-CN/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/zh-CN/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/zh-CN/index.md)

<a id="composition-resources"></a>

## 合成与资源限制

RmlUi 支持嵌套／反向遮罩、图层捕获、透明度、亮度、对比度、反相、灰度、饱和度、褐色、色相旋转、模糊和投影。遮罩与滤镜需要 Graphics 合成，不支持的操作会报错。模糊 sigma 最大 64 像素，投影 sigma 最大 64/3 像素。渐变按几何体栅格化一次并缓存为纹理，每边最多 8192、总计 16,777,216 像素。

已加载的资源标识视为不可变。图像缺失或无效会产生更新／绘制错误；替换失败数据后需换用新标识或重建上下文。图像输入上限为 64 MiB，解码后每边最多 16,384、总计 16,777,216 像素，自定义字体累计保留上限为 128 MiB。标准 `ReadGuiResourceFile` 读取器也限制每个文件最多 64 MiB；自定义读取器不会取消图像或字体累计上限。应在纹理开始使用前配置读取器。

<a id="font-atlas-and-services"></a>

字体图集为 1024 × 1024 像素，字形及效果连同打包边距都必须容纳其中。失败报告 `rmlui.font_atlas_failed`，并保留待重建状态，使重试也报告失败而不是静默漏字。存活的 RmlUi 上下文共享字体注册表，必须使用相同的字体与文本布局服务实例；仅后端 ID 相同并不足够。所有上下文关闭后才可选用其他实例。通过 `RmlUi::FOptions` 或 GUI 后端选项注入资源，不要在上下文存活期间替换。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
