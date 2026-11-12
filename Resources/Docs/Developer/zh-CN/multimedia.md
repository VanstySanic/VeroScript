# 多媒体维护

本公共维护指南负责多媒体模块间的提供者边界与验证流程。公开行为和 API 仍归各模块 User，具体实现细节仍归各模块 Developer。

公开头不得泄漏第三方类型。通用合同归 Media 模块，具体实现和原始声明归后端。更新提供者时保留错误、输出保持及所有权契约；服务合同或 Core 身份变化后同时重建提供者与消费方。

允许执行时，使用模块 CMake 入口及其登记的测试，分别验证安装后 CMake 消费、可选脚本/原生提供者和声明支持的各平台。静态文档与资源检查不能认证渲染、字体、设备、移动端或 IDE。临时测量不得混入 User。

再分发时保留实际依赖的原始许可证及固定来源清单，第三方法律文本维持原文。

## 模块指南

| 模块 | 使用 | 维护 |
| --- | --- | --- |
| VeroScriptPlatform | [用户](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptGraphics | [用户](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptAudio | [用户](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptImage | [用户](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptFont | [用户](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptGui | [用户](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptSdl3 | [用户](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptStb | [用户](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptFontBackend | [用户](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/zh-CN/index.md) |
| VeroScriptRmlUi | [用户](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/User/zh-CN/index.md) | [开发](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/zh-CN/index.md) |

GUI／foundation 聚合组件安装使用 CMake 3.25+ 的 `block(SCOPE_FOR VARIABLES ...)` 作用域。恢复嵌套组件选择，同时传播 `CMAKE_INSTALL_MANIFEST_FILES` 与 `CMAKE_ABSOLUTE_DESTINATION_FILES`，保留 Gui／RmlUi／资源依赖安装及文件清单。已安装 SDK 消费者与最终发行过滤仍须分别检查。

<!-- docs-navigation -->

---

[文档维护](index.md)

[en-US](../en-US/multimedia.md) · [fr-FR](../fr-FR/multimedia.md) · [ja-JP](../ja-JP/multimedia.md) · [ko-KR](../ko-KR/multimedia.md) · [zh-CN](multimedia.md) · [zh-TW](../zh-TW/multimedia.md)
