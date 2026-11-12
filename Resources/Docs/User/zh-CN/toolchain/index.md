# 工具链

VeroScript 工具链涵盖脚本检查与运行、包构建、调试、AOT 生成、绑定目录、宿主集成，以及面向不同宿主环境的 SDK。

## 文档目录

- [示例索引](../../../../../VeroScript/Resources/Docs/User/zh-CN/examples.md)：按语言、标准库和工具链主题组织的可运行示例。
- [C++ SDK](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/index.md)：原生嵌入、宿主绑定、AOT 以及 C++ 声明与转换工具。
- [命令行工具](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md)：运行、检查、导入路径、宏、后端和环境变量。
- [包与分发](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/packages.md)：`.vsapp`、`.vslib`、包版本、检查和分发规则。
- [调试](../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/debugging.md)：标准输入输出、TCP 调试和 IDE 会话中的 DAP。
- [Tooling 模块](../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/index.md)：分析器、格式化、LSP、工作区查询和 DAP。
- [语言服务器](../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/lsp.md)：传输、生命周期、能力、同步、配置和本地化。
- [WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md)：TypeScript/WebAssembly 运行时、虚拟文件、交互输入和浏览器边界。
- [Python SDK](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/index.md)：CPython 会话、值、宿主绑定、包、反射和本地化。
- [C# SDK](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/index.md)：.NET 会话、值所有权、宿主绑定、包、反射和本地化。
- [Interop SDK](../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md)：跨语言会话、值描述符、宿主绑定、回调和线程规则。
- [诊断本地化](../../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md)：动态目录、BCP 47 匹配、搜索顺序和 SDK 边界。

工具链命令应与语言版本保持兼容。用 `veroscript --version` 检查已安装版本；诊断语言可通过 `--lang` 或 `VERO_LANG` 选择。

[字节码 VM](../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/zh-CN/index.md)


<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../index.md)

语言: [en-US](../../en-US/toolchain/index.md) · [fr-FR](../../fr-FR/toolchain/index.md) · [ja-JP](../../ja-JP/toolchain/index.md) · [ko-KR](../../ko-KR/toolchain/index.md) · [zh-CN](index.md) · [zh-TW](../../zh-TW/toolchain/index.md)
