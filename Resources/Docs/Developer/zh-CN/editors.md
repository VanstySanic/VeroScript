# 编辑器集成维护

本指南负责编辑器客户端与原生执行／协议宿主之间的公共边界。客户端实现和资源格式仍归下面各模块的指南。

编辑器客户端负责配置、传输与 IDE 展示；语义分析和 LSP/DAP 契约由原生 Tooling/Core 管理。绑定目录只提供元数据。运行域测试使用协议夹具，不能证明真实 VM 安全点或应用/UE 宿主可用。执行前安装 Session 调试桥，由宿主串行调试上下文并管理取消。`jitCapability` 为服务契约；普通 Run 可显式传递兼容模块路径，也可继承 CLI 模块环境的选择（未配置默认为 auto）；LSP、Check 和本地 VM 调试不加载 JIT。

UI 与文档的六语言同步维护，保留命令选项、消息 ID 与协议值，并按客户端实际采用的格式器校验占位符。User 说明操作，Developer 说明客户端架构、构建与发行；SDK、Runtime 及平台兼容性以所选版本和真实验收证据为准。

## 客户端专属实现

- [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/zh-CN/index.md)
- [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/zh-CN/index.md)
- [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/zh-CN/index.md)
- [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/zh-CN/index.md)

## 公共原生契约

- [Tooling / LSP / DAP](../../../../VeroScriptTooling/Resources/Docs/Developer/zh-CN/workspace-and-debug.md)
- [C++ Session / DAP](../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/session-debugging.md)

<a id="acceptance-evidence"></a>

每轮验收记录操作系统／架构、编辑器与 Runtime 版本、完整源码身份、包哈希、真实结果和跳过项。原生执行还需要匹配 Runtime 的独立验收。历史结果仅适用于其记录的产物和环境。

<a id="editor-icon-inputs"></a>

## 共享图标输入

共同轮廓及输出系列由 `Resources/Tools/Assets/generate-icons.cjs` 维护。产品图标使用 `#60A5FA`，文件标记使用 `#5D75AE`，编辑器字形保留各自的主题颜色与原生对齐。完整再生成更新产品／文件的 SVG、PNG、ICO、ICNS 及编辑器 SVG；`--brand-only` 更新产品系列。Node.js、`sharp` 与 `--sharp-module` 的用法见[资源再生成步骤](../../../Assets/icons/README.md#regeneration)。Runtime 打包使用已维护的文件。

VS Code 各扩展在构建时从产品 PNG 刷新展示 PNG。JetBrains 中带 V 的 SVG 由共享生成器维护，Gradle 将插件列表图标放入 `META-INF`，其余图标资源保留在 `Icons`。UI 六语言间保留资源路径、主题后缀和清单引用。具体复制与打包细节归各模块指南。

VS Code 将模块根 README 作为扩展详情页随包提供，JetBrains 使用插件描述文件中的介绍和 vendor 字段。产品展示保持简洁，并链接六语言文档选择页；公开手册与维护步骤继续由各自正本和出版视图负责。

<!-- docs-navigation -->

---

[文档维护](index.md)

[en-US](../en-US/editors.md) · [fr-FR](../fr-FR/editors.md) · [ja-JP](../ja-JP/editors.md) · [ko-KR](../ko-KR/editors.md) · [zh-CN](editors.md) · [zh-TW](../zh-TW/editors.md)
