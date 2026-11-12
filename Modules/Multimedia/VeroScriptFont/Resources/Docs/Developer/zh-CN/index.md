# 维护 VeroScriptFont

## 源码职责与验证

本模块维护字体、字形塑形与文本布局契约。公开接口声明位于 [`Public/VeroScriptFontLayout.h`](../../../../Public/VeroScriptFontLayout.h)，模块专属输入位于 [`Resources/Tests`](../../../Tests)。

公共提供者边界、重建要求、验证与法律资源处理统一维护在[多媒体维护](../../../../../../../Resources/Docs/Developer/zh-CN/multimedia.md)。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/zh-CN/index.md)
- [Standards](../../../../../../../Resources/Docs/Standards/zh-CN/GENERAL.md)

<a id="font-layout-maintenance"></a>

不可变布局持有文本与所选服务；同步的行运行缓存最多保存四份不可变分析，样式片段只选取相交的逻辑运行。仅当已验证布局拥有完全相同的缓冲区和长度时才跳过 UTF-8 校验。回退塑形须保留簇范围校验和字形顺序。`VeroScriptFontContractTests`、`VeroScriptFontLayoutContractTests` 使用不依赖具体提供者的 mock；语料与脚本测试归 FontBackend。绑定显式选择 VM，不依赖 Core 私有测试头。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
