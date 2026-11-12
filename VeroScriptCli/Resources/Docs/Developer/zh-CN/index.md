# VeroScript CLI 技术文档

`VeroScriptCli` 是一个轻量进程层。它提供 `veroscript`、`veroscript-lsp` 和 `veroscript-dap` 入口点，而不重新实现编译器、VM、C++ 嵌入 API 或语言服务语义。

## 进程职责

CLI 负责参数解析、命令分发、进程级流、REPL/watch 编排、源代码/文件选择、环境与 SDK 资源查找、退出码策略以及 LSP/DAP 进程 main。核心、C++ session、AOT 和 Tooling 模块仍然是语言行为的事实来源。

命令选项、子命令名称、环境变量、退出码、诊断 ID 和输出格式都是兼容性表面。不得仅为了改善文案而翻译或改变它们。

## 运行时资源

独立进程必须定位版本匹配的标准库和本地化目录。资源解析必须可重定位，且不得悄然使用过期的构建目录副本。资源缺失或不兼容属于启动/配置失败，而不是回退为空语言。

## 协议入口点

LSP 与 DAP main 将协议行为委托给 `VeroScriptTooling`。CLI 必须保留这些服务期望的流分帧、请求顺序、取消、stderr/stdout 分离和进程退出行为。

## 命令与资源细节

普通 main 分发 compile/check/run 和面向 package 的操作；REPL 与 watch 进程增加交互式源代码及文件变更编排。`VeroScriptLspMain` 与 `VeroScriptDapMain` 是仅协议的进程入口点，并将语言行为委托给 `VeroScriptTooling`。CLI 不得创建第二套 Session、debugger 或 workspace state 实现。

CLI 路径解析器必须保留 source root、SDK root、`bin` directory、标准库 directory 和本地化 directory 的区分。发现的 executable 与资源必须来自兼容安装；environment-variable 与 `PATH` fallback 是发现行为，不是混用不同 version 的许可。

## 详细页面

- [CLI 进程与资源契约](process-and-assets.md)

## 事实来源

以 `Private/`、CLI CMake target、process test、Tooling public API 及 packaging/SDK smoke test 为准。命令分发、资源查找、流行为、环境变量、退出码或 LSP/DAP 进程边界变更时，应同步更新本文档。

## 项目初始化

`Private/VeroScriptCliProject.cpp` 使用本地 C++17 文件创建实现 `project init`，不加载包环境或调用编译。
独占创建保证并发调用也不覆盖已有文件；原生 CLI 注册表保留 `project` 命令名。
`VeroScriptProjectInitCliTests` 进程测试覆盖路径、重复执行、冲突、并发创建及写入失败恢复，
仅在原生构建时注册；包含构建服务时，
还会构建并运行生成的应用和文档项目样例。项目与构建模板须符合现有 `0.1.1` Schema；
初始化不构造依赖锁文件或安装状态。
UTF-8 路径转换兼容 C++17 和 C++20；打包工具的 CMake 回归会在两种标准下独立编译初始化器，并将警告视为错误。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
