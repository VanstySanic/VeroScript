# 维护 VeroScriptRmlUi

## 源码职责与验证

本模块维护GUI 渲染、布局、输入适配及其内置依赖。公开接口声明位于 [`Public/VeroScriptRmlUi.h`](../../../../Public/VeroScriptRmlUi.h)，模块专属输入位于 [`Resources/Tests`](../../../Tests)。

公共提供者边界、重建要求、验证与法律资源处理统一维护在[多媒体维护](../../../../../../../../Resources/Docs/Developer/zh-CN/multimedia.md)。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/zh-CN/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/zh-CN/GENERAL.md)

文本测量使用有界的内容缓存：宽度键包括段落和行范围、方向、语言、字距、字偶距及回退字体修订；段落分析还匹配实际的文本布局服务。重新创建相同不可变段落的所有者无需重复分析。每个字号字体的宽度缓存最多 1,024 项／512 KiB；段落分析最多 2,048 项／8 MiB 文本。完整字形、光标及选区布局继续使用正常整形路径。

嵌套的自动高度纵向容器使用块流及内部间距元素，避免嵌套 Flex 反复测量固有尺寸。Hidden 子控件保留空间，Collapsed 和浮层不参与此流。横向、Grid、Wrap、Splitter 和可编辑内容保持专用布局路径。双色仿射线性渐变可使用顶点色，其余渐变继续栅格化。隔离的裁剪遮罩使用受限临时目标；预算不足时回退通用合成器。嵌套、软边和反向遮罩保持预乘透明度及 source-over 语义。

测量期间保留最近一个段落的分析，包括单个超过 LRU 文本预算的段落；下一段落分析会替换它，释放字体资源时一并清除。

<a id="native-provider-maintenance"></a>

可选 `VeroScriptRmlUiModule` 提供原生 GUI 服务，须匹配 Core／Gui 构建。本地修补的 RmlUi 头、编译库及补丁／许可清单应同步，不能混用系统 RmlUi 与这些适配器头。原生 SDK 数据路径不要求启用可选 Core 绑定提供者。Font、文本布局、Image 和 Graphics 仍为显式选择的依赖；服务实例寿命和图集失败行为遵循 RmlUi 用户契约。修改资源缓存或同步时须保留这些边界。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
