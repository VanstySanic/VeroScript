# VeroScript 教程

这里的内容面向“跟着做出一个结果”的学习过程。每篇教程都尽量从一个小目标开始，逐步增加代码，并在结尾给出练习；语言规则和完整 API 仍以旁边的[文档](../../User/zh-CN/index.md)为准。

## 开始之前

你需要准备：

1. 已安装 VeroScript 命令行工具，并能在终端执行 `veroscript --version`。
2. 一个用于保存脚本的工作目录。
3. 能够编辑纯文本文件的编辑器。

代码块标签标明语言或终端类型。`powershell` 命令在 PowerShell 中运行，`sh`／`bash` 命令在对应的 shell 中运行，并按操作系统调整路径；`text` 展示预期输出。完整程序通常使用 `int main` 并显式返回退出状态；不需要返回状态的短示例也可使用 `void main`。

`--check` 检查程序而不调用应用 `main`；已启用的[脚本编译扩展](../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/compiler.md)仍可在编译器上限与权限政策下执行。

## 推荐学习路线

### 第一阶段：运行第一个脚本

- [01：第一个脚本](getting-started/01-first-script.md)
- [02：检查、运行和读取命令行参数](getting-started/02-cli-and-arguments.md)

### 第二阶段：掌握语言骨架

- [03：变量、条件和循环](language/03-values-and-control-flow.md)
- [04：函数和容器](language/04-functions-and-collections.md)
- [05：结构体、类和接口](language/05-structs-classes-and-interfaces.md)
- [06：引用、引用返回和 Lambda 捕获](language/06-references-and-captures.md)
- [07：模块、导入和预处理器](language/07-modules-and-preprocessor.md)

### 第三阶段：完成实际任务

- [08：字符串、文件和路径](standard-library/08-strings-files-and-paths.md)
- [09：并发和事件](standard-library/09-concurrency-and-events.md)
- [20：读取和复制二进制数据包](standard-library/20-managed-memory.md)
- [22：格式化应用文本并保留快照](standard-library/22-application-i18n.md)

### 第四阶段：交付和集成

- [10：构建包并分发应用](toolchain/10-packages-and-distribution.md)
- [11：调试与 AOT 生成](toolchain/11-debugging-and-aot.md)
- [12：宿主绑定与脚本集成](advanced/12-host-bindings.md)
- [13：运行时反射](advanced/13-reflection.md)
- [16：WebAssembly SDK 异步交互输入](toolchain/16-web-interactive-input.md)
- [17：可选值、弱引用和共享结构体句柄](advanced/17-value-utility-types.md)
- [18：选择和使用 C++ 工具链模块](cpp/index.md)
- [Python 源工具](python/index.md)
- [C# 源工具](csharp/index.md)
- [Tooling 工作流：连接编辑器和调试器](toolchain/17-tooling-workflow.md)
- [19：在移动应用中执行脚本](toolchain/19-mobile-embedding.md)
- [21：安装本地扩展包](toolchain/21-package-manager.md)
- [23：构建库和应用](toolchain/23-application-build.md)
- [24：集合、导航和对话框](toolchain/24-gui-collections.md)


### 第五阶段：规范化项目

- [14：使用 `std` 标准命名空间](advanced/14-standard-namespace.md)
- [15：配置工作区与诊断本地化](toolchain/15-workspace-and-localization.md)

## 如何使用教程

建议先按顺序完成前两阶段。第三阶段的教程可以根据任务选择；第四阶段适合已经能独立编写脚本之后阅读。

每篇教程都可以单独阅读，但不要跳过命令行中的 `--check`：它能在执行前发现语法、类型和导入问题。完成教程后的练习比复制代码更重要，哪怕只修改一个输入或增加一个分支，也能帮助你确认自己理解了代码。

## 参考资料

- [语言基本特性](../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/index.md)：查找语法和语义。
- [标准库参考](../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/index.md)：查找函数签名和边界条件。
- [工具链参考](../../User/zh-CN/toolchain/index.md)：查找 CLI、包、调试和宿主集成选项。
- [可运行示例索引](../../../../VeroScript/Resources/Docs/User/zh-CN/examples.md)：把教程中的主题与随 SDK 或文档分发的示例对应起来。

<!-- docs-navigation -->

---

[教程](index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
