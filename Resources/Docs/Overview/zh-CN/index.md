# VeroScript 综合总览

[GitHub 源码仓库](https://github.com/VanstySanic/VeroScript)

这份总览按使用场景串联语言、工具链、宿主 SDK 和维护文档。详细语义、API 与限制以链接到的模块参考为准；总览维护跨模块的关系与阅读顺序。

## 跨平台部署

| 部署方式 | 目标配置 | 指南 |
| --- | --- | --- |
| 桌面 Runtime 与 SDK | Windows (x86, x64, ARM64); Linux (x64, ARM64); macOS (x64, ARM64) | [安装与分发](../../../../VeroScriptInstaller/Resources/Docs/User/zh-CN/index.md) |
| 移动嵌入 | Android、iOS（实验性适配器） | [移动 SDK](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/zh-CN/index.md) |
| 浏览器和 JavaScript 宿主 | WebAssembly | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md) |

请选择匹配操作系统与 CPU 架构的发行包。包的提供范围和验收状态随产品与版本而异；依赖、可用后端和部署要求请查阅对应产品的平台说明。

## 选择阅读路线

| 目标 | 从这里开始 | 后续参考 |
| --- | --- | --- |
| 编写和运行脚本 | [概述](../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md) → [语言基本特性](../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/index.md) | [CLI](../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md)、[标准库](../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/index.md)、[示例索引](../../../../VeroScript/Resources/Docs/User/zh-CN/examples.md) |
| 安装、更新或分发 | [安装与分发](../../../../VeroScriptInstaller/Resources/Docs/User/zh-CN/index.md) | [包管理](../../../../VeroScriptCli/Resources/Docs/User/zh-CN/packages.md)、[CLI 参考](../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md) |
| 把脚本嵌入宿主 | 按下表选择 SDK | [Interop 边界](../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md)、[宿主集成](../../User/zh-CN/toolchain/index.md) |
| 编辑、诊断和调试 | [CLI 参考](../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md) | [LSP](../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/lsp.md)、[调试](../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/debugging.md)、[诊断本地化](../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md) |
| 开发或维护实现 | [开发阅读路线](#开发阅读路线) | [全部开发文档](../../Developer/zh-CN/index.md) |

先确认安装的组件与版本，再选择执行后端、宿主绑定和包配置。命令及 SDK 示例需要对应组件；参考文档不能代替实际构建和运行验收。

## 选择宿主 SDK

| 宿主 | 用户参考 | 维护入口 |
| --- | --- | --- |
| 原生 C++ | [C++ SDK](../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/index.md) | [C++ 模块](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-CN/index.md) |
| C ABI 或其他语言 | [Interop SDK](../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md) | [ABI 与生命周期](../../../../VeroScriptInterop/Resources/Docs/Developer/zh-CN/abi-and-lifetime.md) |
| CPython | [Python SDK](../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/index.md) | [Python 模块](../../../../Extensions/Language/Python/Resources/Docs/Developer/zh-CN/index.md) |
| .NET / C# | [C# SDK](../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/index.md) | [C# 模块](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/zh-CN/index.md) |
| 浏览器或 JavaScript | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md) | [WebAssembly 模块](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/zh-CN/index.md) |

值、句柄、回调、线程及释放规则应查阅所选 SDK 的契约。跨语言转换工具支持的源码子集、AOT 限制及目标环境也以各模块说明为准，不能由语言名称推断兼容范围。

## 开发阅读路线

1. 从[核心架构与管线](../../../../VeroScript/Resources/Docs/Developer/zh-CN/architecture-and-pipeline.md)和[执行与状态](../../../../VeroScript/Resources/Docs/Developer/zh-CN/execution-and-state.md)理解编译、执行与状态边界；涉及后端和并发时继续阅读 [JIT 与并发](../../../../VeroScript/Resources/Docs/Developer/zh-CN/jit-and-concurrency.md)。
2. 沿宿主边界阅读 [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/zh-CN/index.md)、[C++ 会话](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-CN/session.md)及相应语言适配器文档。
3. 沿工具链边界阅读 [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/zh-CN/index.md)、[工具协议](../../../../VeroScriptTooling/Resources/Docs/Developer/zh-CN/architecture-and-protocols.md)。
4. 发布前核对[构建与资源](../../../../VeroScript/Resources/Docs/Developer/zh-CN/build-and-assets.md)、[安装器构建与打包](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-CN/build-and-bundle.md)及[发行验收流程](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-CN/release-validation.md)。

既有验收结果只适用于记录的源码、产物和环境。产品构建、完整性校验与运行验收状态的记录方式见[发行验收流程](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-CN/release-validation.md)；证据保存在被忽略的输出目录。

## 文档与出版

完整模块目录见[用户入口](../../User/zh-CN/index.md)。文档归属、六语言维护、校验命令和 All/User 打包流程统一维护在[文档维护指南](../../Developer/zh-CN/index.md)。

出版文档之间的链接和锚点支持离线阅读；源码引用及外部网站需要网络。使用说明和教程选择 User，维护指南与规范选择 All。两种出版包均不包含 History。

[用户文档](../../User/zh-CN/index.md) · [开发文档](../../Developer/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
