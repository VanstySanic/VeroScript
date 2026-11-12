# VeroScript 工具链技术文档

`VeroScriptTooling` 是供 CLI、IDE、LSP、DAP 宿主共享的公共分析、语言服务、工作区和调试器集成层。本文档规定开发者契约，不是编辑器或命令行使用说明。

## 公共入口

C++ 查询服务包括 `VeroScriptAnalyzer`、`VeroScriptCallHelp`、`VeroScriptComplete`、`VeroScriptHover`、`VeroScriptNavigation`、`VeroScriptOutline`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptRename`、`VeroScriptFormat` 和 `VeroScriptWorkspaceSymbols`。状态与协议入口为 `VeroScriptWorkspace`、`VeroScriptLspStdio`、`VeroScriptDebugger`；调试入口为 `VeroScriptDebugSession`、`VeroScriptDapAdapter`、`VeroScriptDapStdio`、`VeroScriptDapTcp`。共享类型由 `VeroScriptToolingTypes` 和 `VeroScriptDapTypes` 定义。

结果和选项的公共类型包括 `FTextPosition`、`FTextRange`、`FDiagnostic`、`FTextEdit`、`FDocumentOverlay`、`FWorkspaceOptions`、`FAnalyzeOptions`、`FCompletionResult`、`FHoverResult`、`FReferencesResult`、`FRenameResult` 和 `FWorkspaceSymbolsResult`。确切字段和默认值以公共头文件为准；本文说明它们跨服务的契约。

## 分层

核心层负责解析、语义分析、模块加载、诊断、运行时元数据和调试器语义。`VeroScriptTooling` 只能使用这些公共契约，不得通过 C++ 嵌入检查私有编译器或 VM 状态。

`VeroScriptWorkspace` 负责文档覆盖层、项目/模块上下文、源文件与导入解析、预处理上下文、绑定目录和跨文件状态。功能服务查询明确给定的源、文件或工作区，不写入用户文件，也不读取全局 IDE 状态。LSP/DAP 适配器序列化结构化结果，并负责帧、关联、能力、取消、协议错误与事件顺序；它们不得重新实现解析器、解析器逻辑、格式化器、字节码求值、断点语义或调试器对象生命周期。

## 源坐标与编辑

C++ 查询 API 的位置与范围使用从一开始计数的源坐标；LSP 适配器负责与从零开始计数的 UTF-16 协议位置转换。宿主不得自行进行第二次转换。结果和编辑绑定于文档标识与源状态；只有版本或覆盖层仍匹配时宿主才可应用 `FTextEdit`，工具链层永不自行应用编辑。

## 本地化与诊断

诊断是包含稳定标识符、源位置、严重性和替换参数的结构化记录。共享目录本地化显示消息；缺少区域或消息时回退到英语。诊断标识符和参数、协议方法、JSON 字段、语言标记、路径、URI、符号名称以及源代码片段永不翻译。

LSP 的 `initialize.locale` 是 BCP 47 首选项。区域不受支持时，进程仍使用由环境选择的目录；与区域相关的标准命名空间说明遵循同一回退规则。绑定目录的标识符、类型、成员、签名和文档键是精确标识符，不是可翻译资源。

## 详细页面

- [工具链架构与协议](architecture-and-protocols.md)
- [工作区、LSP 与 DAP](workspace-and-debug.md)
- [工具链测试与扩展规则](testing-and-extension.md)

## 调试会话与 DAP 接口

`VeroScriptDebugSession` 是协议无关的同步调试器。`LoadSource` 和 `LoadFile` 通过 `FDebugLoadOptions` 接收导入路径、定义、源覆盖层、可选 `IHostBindingCatalog`、隐式导入、循环导入与项目范围访问策略。断点使用文件标签、从一开始计数的行号和可选条件。会话公开运行模式、请求的执行后端、最大调用深度、JIT 策略和最后一次 `FJitStats`；停止的宿主可检查 `FDebugStackFrame`、`FDebugVariable`、求值并写入局部变量或子值。`FDebugRunResult` 分别保存标准输出式文本、错误输出和结构化诊断；成功状态与脚本退出代码相互独立。

`VeroScriptDapAdapter` 是不拥有 JSON、帧或传输的进程内 DAP 形状适配器。生命周期为 `Initialize` -> `Launch` 或 `Attach` -> `SetBreakpoints` -> `ConfigurationDone`；最后一步同步执行并发出 stopped/output/exited/terminated 事件。continue、步进、调用栈、作用域、变量、求值和设值只能在 stopped 事件回调中使用。`Attach` 采用 `Launch` 的加载语义，不是附加到运行中的 VM，并强制有效后端为 `Vm`。实时绑定通过 `FDapLaunchArgs::Bindings` 传入；外部进程使用目录数据。DAP 能力和事件名必须精确来自 `FDapCapabilities` 和 `EDapEventKind`，不得本地化。

## 事实来源

修改时需同步更新 `Public/`、`Private/`、对应的功能/LSP/DAP 聚焦测试和本文档。每个公共契约都应复查结果形状、坐标、文档版本、工作区失效、协议能力、诊断/本地化、取消与调试器集成。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
