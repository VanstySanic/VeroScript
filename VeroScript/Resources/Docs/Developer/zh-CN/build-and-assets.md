# 核心构建与运行时资源

[可替换 VM 后端](vm-backends.md)

## 构建目标

参阅[可替换 VM 后端](vm-backends.md#vm-provider-assembly)。

## 资源依赖

该目标依赖 `VeroScriptRuntimeAssets`。它使用共享的 `Resources/Tools/CMake/VeroScriptLocalization.cmake` 部署配置汇总目录，删除过期的目录 JSON，复制发生变化的目录文件，并把标准库文件复制到运行时输出目录。单独构建测试目标时也保留此依赖，确保测试使用当前源码的目录资源。

本地化加载器先尝试 `VERO_LOCALIZATION_DIR`，再搜索部署目录和源码仓库；准确顺序见[诊断本地化](localization-and-resources.md)。有效目录树须包含 `en-US` 基准、规范且带限定信息的 BCP 47 语言目录和唯一消息 ID。翻译文件与键可以是英文的子集，缺失项回退到英文。模块和标准库根目录可非覆盖地合并有效目录。可用语言从已加载的资源中发现。

## 兼容性

公开的 bytecode、package、diagnostic、host-binding 和 locale 行为与是否启用 asmjit 无关。架构专用的原生 leaf 必须描述为执行能力，而不是新的语言或字节码版本。

## 构建基础设施与源码目录补充

本独立仓库的辅助工具位于 `Resources/Tools/CMake`，从仓库根目录直接引用这些路径，无需 `Script/` 前缀。 公开 locale API 声明 `InitializeModuleFromEnvironment`，其参数相对于发现的源码模块，例如 `VeroScriptTooling` 或 `Extensions/Language/Cpp/VeroScriptCpp`。它仅在核心选择源码目录后，以不覆盖已有 ID 的方式合并；显式替换目录会清除这一源码发现状态。`VeroScriptLocaleSourceTests` 验证模块改名后的发现和模块目录补充。

- [本地化资源与部署](localization-and-resources.md)

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/build-and-assets.md) · [fr-FR](../fr-FR/build-and-assets.md) · [ja-JP](../ja-JP/build-and-assets.md) · [ko-KR](../ko-KR/build-and-assets.md) · [zh-CN](build-and-assets.md) · [zh-TW](../zh-TW/build-and-assets.md)
