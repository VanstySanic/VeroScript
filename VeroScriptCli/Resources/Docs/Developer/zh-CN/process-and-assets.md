# CLI 进程与资源契约

## 入口点

`VeroScriptCli` 是面向 `veroscript`、`veroscript-lsp` 与 `veroscript-dap` 的轻量进程层。普通 main 分发 compile/check/run 和 package 操作。REPL/watch 代码增加交互式源代码和文件变更编排。LSP/DAP main 将语言行为委托给 Tooling。

CLI 不得重复实现 compiler、VM、debugger 或 workspace 语义。子命令名称、选项、环境变量、退出码、诊断 ID 与输出格式均为兼容性 token，不做本地化。

## 资源解析

解析必须区分 source root、SDK root、`bin` directory、标准库 directory 与本地化 directory。executable 及其 runtime asset 必须来自兼容安装。environment-variable/`PATH` fallback 是发现行为，并不允许混用 installation。

## 进程行为

对 IDE、SDK、shell consumer 而言，stdout、stderr、diagnostic line handling、input stream、process cancellation 和 exit-code propagation 必须稳定。LSP/DAP framing 属于 Tooling transport implementation；CLI 仅负责 process startup 与 termination。

## Unicode 进程边界

Windows `wmain` 入口在调用 `VeroScript::Cli::Run` 前，将 UTF-16 参数一次性转换为 UTF-8。持有参数的字符串及参数指针数组必须在整个调用期间保持有效；文件操作使用 UTF-8 路径辅助函数。`VeroScriptUnicodeCliTests` 覆盖 VM、mixed 和 JIT 优先模式下的非 ASCII 源文件与导入路径、脚本参数及包执行。终端显示和管道解码仍由消费方负责。

## 注册与分派契约

Core 提供 `VeroScriptCommandLine.h`，CLI 拥有注册表、参数解析与分派。在原生 `Create` 内获取 `INativeExtensionHost::GetService("veroscript.cli", "0.1.1", error)`，使用匹配的 `ICommandLineExtensionService` 同步注册自有 `FCommandLineModule` 描述符。分派前关闭注册；错误声明使整个批次失败。组合角色模块应兼容非 CLI 宿主不提供此服务的情况。 每个模块须至少声明一个命令或选项；声明选项时必须提供 Configure 回调。

ID 和名称区分大小写，使用 ASCII 字母/数字/下划线/连字符，最长 64 字符，首尾不能为连字符；Core 名称保留。最多 256 个模块，每模块命令与选项合计最多 256 项；描述/用法/值标签不含控制字符，最多 8192 字节。选项默认要求值且不允许重复；已指定的标志提供一个空字符串，未指定的选项没有映射项。值类似其他选项时使用 `--name=value`。

Configure 按模块 ID 顺序在普通执行/命令分派前调用一次，仅验证及保存自身选项，不得运行脚本。帮助跳过配置及命令执行，但仍加载并注册模块。Run 接收完整 UTF-8 参数、语言标签、流和选定环境。借用上下文仅在分派期间有效，不得强引用反向拥有注册的环境；原生 Destroy 返回前应结束所属工作线程。宿主持有代码、捕获回调异常并检查退出码 0..255；阻塞原生命令自行处理取消。

[NativeModule 源码示例](../../../Examples/NativeModule/README.md) 演示匹配 SDK 构建与包集成。`VeroScriptCliModulesTests`、`VeroScriptCliRegistryTests` 覆盖解析、名称冲突、Unicode、生命周期、启用/禁用与参数边界。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/process-and-assets.md) · [fr-FR](../fr-FR/process-and-assets.md) · [ja-JP](../ja-JP/process-and-assets.md) · [ko-KR](../ko-KR/process-and-assets.md) · [zh-CN](process-and-assets.md) · [zh-TW](../zh-TW/process-and-assets.md)
