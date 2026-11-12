# Image

## 使用服务

`Image::IService::Decode` 输出紧密排列的直通 alpha sRGB RGBA8，失败保留原结果；`Image::Validate` 检查尺寸及缓冲。`FDecodeOptions` 默认每边最多 16,384、16,777,216 像素、64 MiB 输入。资源解析、缓存和 GPU 上传由调用者负责。服务契约为 `image` / `veroscript.image` / `0.1.0`。

解码器由 `stb` 等提供者实现。注册 Image 绑定后，脚本可用 `VS::Image::Service::Create("stb").Load("image.png")` 或 `Decode(bytes)`。尺寸及索引为 `int64`；脚本适配器在复制超大字符串前也检查默认限额。注入的 reader 负责资源解析；未提供时，文件路径基于进程工作目录。

## SDK 与集成

```cmake
find_package(VeroScriptImage 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Image)
```

原生 SDK 不要求 Core。可选脚本绑定和原生提供者必须匹配实际 Core 构建、公开 ABI 及工具链，仅产品版本一致不足以保证兼容。请同时安装原生依赖及需要的 Bindings 组件。

- [Image](index.md) · [Font](../../../../../VeroScriptFont/Resources/Docs/User/zh-CN/index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/zh-CN/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
