# Stb

二进制 PGM／PPM 按 `Maxval`（1–65535）归一化为 RGBA8。无效最大值或越界样本会报错并保留原输出；255／65535 满量程输入保持原有量化和字节序行为。

## 使用服务

`Stb::CreateImageService()` 提供 `image/stb`。PNG、JPEG、BMP、TGA 及其他已启用 stb_image 格式输出直通 alpha sRGB RGBA8；GIF 只取首帧，HDR/浮点输出关闭。top-down BMP、16 位 PGM/PPM 字节序及 CgBI PNG 统一到该契约。失败保留原输出并遵守显式解码限额。

使用独立结果/错误对象且调用期间不修改输入时可并发解码；缺少解码器线程局部存储的构建跨实例串行化。`VS::Image` 来自独立 Image 绑定，可选 `VeroScriptStbModule` 只提供后端。请同时安装 Image、Stb 和适用的 Bindings 组件。

## SDK 与集成

```cmake
find_package(VeroScriptStb 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Stb)
```

原生 SDK 不要求 Core。可选脚本绑定和原生提供者必须匹配实际 Core 构建、公开 ABI 及工具链，仅产品版本一致不足以保证兼容。请同时安装原生依赖及需要的 Bindings 组件。

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/zh-CN/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/zh-CN/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/zh-CN/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
