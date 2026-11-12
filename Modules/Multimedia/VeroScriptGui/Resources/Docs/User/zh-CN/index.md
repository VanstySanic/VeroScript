# GUI

Gui 负责控件树、构建器、属性、事件和 ML/CSS；RmlUi 是单独选择的布局与绘制实现。

- [框架与 RmlUi 接入](Reference/Framework.md)
- [标记与样式](Reference/Markup.md)

`vs-gui` / `vs-rmlui`. CMake: `VeroScriptGui` / `VeroScriptRmlUi`; VS: `VS::Gui`.

[VS Code](../../../../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/User/zh-CN/index.md) · [Rider](../../../../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/User/zh-CN/index.md)

`gui-compile` 需要 Application 支持，生成标准 ZIP `.vsapp` 应用容器，包含启动描述、GUI 编译映像、资源元数据及显式嵌入的资源。GUI 检查／预览和通用 CLI 检查／运行／inspect 读取同一容器；启用 GUI 脚本支持的 Runtime 也可以运行。此前的原始 GUI `.vsapp` 文件需要重新编译。后端遵循已安装环境的服务选择规则，宿主、项目配置和命令选项可以覆盖默认值；编辑器不会写死 RmlUi。

脚本适配发行同时提供带顶层目录的 SDK ZIP／tar.gz 与根目录含 `veroscript-package.json` 的 `.vspkg`。后者保留运行资源、动态模块、分析目录和许可证，不包含开发头文件、CMake 导出或静态库。离线安装时需显式提供依赖归档，或在项目依赖中声明其本地路径。

GUI 安装包使用现有 `veroscript.gui.tools` 宿主服务，需要已接入 GuiScript 的 CLI／Runtime 或嵌入宿主。安装不能为纯 Core 自动提供 GUI 服务。SDL3、RmlUi、Stb、FontBackend 是分别安装和选择的后端；通过对应 `--gui-backend`、`--platform-backend` 等选项或项目 `services` 配置使用，安装本身不改变服务选择。

[教程: 集合、导航与对话框](../../../../../../../Resources/Docs/Tutorials/zh-CN/toolchain/24-gui-collections.md)

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[用户文档](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
