# 维护 VeroScriptFontBackend

## 源码职责与验证

本模块维护字体塑形、光栅化与双向文本分析提供者。公开接口声明位于 [`Public/VeroScriptFontBackend.h`](../../../../Public/VeroScriptFontBackend.h)，模块专属输入位于 [`Resources/Tests`](../../../Tests)。

公共提供者边界、重建要求、验证与法律资源处理统一维护在[多媒体维护](../../../../../../../../Resources/Docs/Developer/zh-CN/multimedia.md)。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/zh-CN/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/zh-CN/GENERAL.md)

<a id="native-provider-maintenance"></a>

可选 `VeroScriptFontBackendModule` 分别注册字体和文本布局服务。静态宿主使用 `veroscript_font_backend_query`，动态扩展宿主使用 `veroscript_extension_query`；转换前逐项核对 Font 定义的对应服务契约。保留的 face 和分析对象须独立于原始注册句柄持有提供者租约。FreeType／HarfBuzz／SheenBidi 输入、声明和原生提供者构建归本模块；公开契约、默认字体及独立 mock 契约测试归 Font。

## 提供者构建与生命周期

`VEROSCRIPT_BUILD_FONT_BACKEND` 选择本模块；`VEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION` 构建匹配 Core 的提供者，`VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER` 选择命名静态入口。Font 拥有通用接口，不发现具体后端。FreeType／HarfBuzz 字体面、SheenBidi 分析、固定依赖输入及法律资源归本模块。即使后端 ID 相同，font 和 text-layout 仍是独立服务描述。

脚本回归显式链接匹配的 `VeroScriptBytecodeVm`，使用其公开工厂。检查模块租约时，应在释放注册表句柄后分别保留字体面、分析对象和布局快照。允许执行时核验纯原生及匹配 Core 的构建、依赖隔离和迁移位置后的 SDK 消费方，并记录真实失败及未验证目标。

[字体与布局公开契约](../../../../../../VeroScriptFont/Resources/Docs/User/zh-CN/index.md) · [GUI 消费方维护](../../../../../../VeroScriptGui/Resources/Docs/Developer/zh-CN/index.md#gui-text-layout-maintenance)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
