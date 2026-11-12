# 工具链架构与协议

`VeroScriptTooling` 是命令行、LSP、DAP 与其他宿主共享的语言服务层。它使用公共语言核心契约，并把语义查询与协议传输分开。

LSP 与 DAP 共用 `Private/Internal/VeroScriptToolingJson.h` 和 `.cpp` 中的私有 JSON 值、解析器和序列化器，字符串转义复用 `Shared/Private/VeroScriptJsonString.h`。消息封装、请求关联、调度、取消及协议专有字段仍归各自适配器。调整共享实现时保持确定的对象顺序、数值转换、既有深度上限及错误文本。

## 分层数据流

依赖方向如下：语言核心负责解析、语义分析、模块加载、运行时元数据、诊断和调试器语义；`VeroScriptWorkspace` 提供项目上下文、文档覆盖层、源文件/导入解析、预处理上下文、绑定目录与跨文件状态；公共功能服务查询源文件或工作区并返回稳定结果类型；LSP/DAP 适配器序列化结果并处理传输。功能服务不读取编辑器状态也不写入源文件；适配器不得从协议文本重新构造已有公共服务提供的语义结果。

## 公共功能族

引用收集使用本次请求内的位置集合去重，每次扫描只解析一次各既有路径拼写的身份。
引用查找和重命名通过 `Internal/VeroScriptSourceQuerySession` 在一次请求内保留当前文件的完整解析结果，逐个核对引用的符号身份，并检查磁盘依赖是否仍然有效。该状态不会跨请求保留，解析恢复和处理器生成代码仍走原查询路径。块或函数内的局部符号确认在当前文件声明后，不再扫描其他含同名词的打开模块。

仅调整请求预算、日志或结果数量限制时，LSP 保留语义缓存；分析配置改变时才使其失效。工作区文件发现限制改变时更新符号索引，不清除已解析程序。

`VeroScriptAnalyzer` 提供工作区感知诊断（包括导入文件），并按文件标签/URI 发布；需要清除时也发布空列表。`VeroScriptOutline`、`VeroScriptNavigation`、`VeroScriptHover`、`VeroScriptCallHelp`、`VeroScriptComplete`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptRename`、`VeroScriptFormat` 和 `VeroScriptWorkspaceSymbols` 分别处理能容忍解析错误的结构、查找、补全、调用、引用、编辑、格式化与工作区搜索。

已确认的命名空间段（包括嵌套用户命名空间和 `std::`）必须按语义命名空间处理。宿主绑定目录和软内建符号可参与查找、补全、悬停和工作区符号；宿主专用符号在需要 URI 时使用虚拟宿主绑定位置。补全会将可调用的同名重载合并为一个条目，并在详情中逐行列出每个源签名。调用上下文中的合并条目使用空参数停靠点，不会猜测某个重载的参数。只有客户端声明支持 `textDocument.completion.completionItem.snippetSupport` 时，无重载的可调用项或泛型项才发送代码片段，否则使用普通文本插入。若补全目标后已有左括号 `(`（中间可有空白或注释），则省略可调用项的 snippet，以避免重复生成调用语法。重命名先验证目标，再重新解析每个引用，标准库命名空间不可重命名；格式化返回文本编辑，工具链永不自行应用。

`FCompletionItem::MatchQuality` 是首要文本排序键，`SortPriority` 只用于打破相同质量的并列。
适配器必须通过 LSP `sortText` 保留此顺序，且只能为明确优于其他项的精确、忽略大小写精确或
前缀匹配设置 `preselect`。基元类型词是内置 `Keyword` 条目，容器类型词是
`TypeAlias`／`type` 条目。两者的显示详情会本地化，类型标识符保持稳定，没有源定义，并且
重命名会被拒绝。

命名空间成员补全在标准 LSP 的 `label` 和插入文本中保留叶子名；存在可用的可调用或泛型类型代码片段且客户端声明支持
`textDocument.completion.completionItem.snippetSupport` 时，插入文本才包含该片段，
否则仅插入叶子名。插入文本使用 `insertText`，有替换范围时也使用 `textEdit.newText`。限定命名空间放在
`labelDetails.description` 中。供 Valence 使用的可选
`$valencePresentation` 字段提供最终标签／声明文本及 UTF-16 语义片段；其他 LSP 客户端可忽略。
这些展示元数据不改变过滤、排序或实际插入内容。

命名空间段补全与命名空间成员补全不同。在表达式或语句的限定路径中，命名空间项会在 `label` 和
`filterText` 中保留叶子名称；只有替换范围后面没有分隔符时，插入文本才会追加 `::`。补全
`using namespace` 目标时只插入名称。客户端支持 snippet 时，`print` 等可变参数宿主格式化函数在调用上下文中
使用空参数停靠点（例如 `print(${0})`），而不是从合并后的重载详情中推导参数占位符。

语义令牌方面，LSP 适配器会把已解析范围与词法基线合并；词法基线覆盖语言／类型词、指令、宏和
字面量／注释。在不完整输入时它仍可用，并会在编码 LSP 流前消除重叠范围。

## LSP 协议契约

`VeroScriptLspStdio` 负责 `Content-Length` 分帧、JSON-RPC 关联、请求分派、取消、诊断发布和生命周期状态。已公告请求为 `textDocument/completion`、`completionItem/resolve`、`textDocument/hover`、`textDocument/definition`、`textDocument/declaration`、`textDocument/typeDefinition`、`textDocument/implementation`、`textDocument/prepareTypeHierarchy`、`typeHierarchy/supertypes`、`typeHierarchy/subtypes`、`textDocument/documentSymbol`、`textDocument/selectionRange`、`textDocument/documentLink`、`textDocument/foldingRange`、`textDocument/semanticTokens/full`、`textDocument/semanticTokens/full/delta`、`textDocument/signatureHelp`、`textDocument/references`、`textDocument/documentHighlight`、`textDocument/prepareRename`、`textDocument/rename`、`textDocument/formatting`、`textDocument/rangeFormatting`、`textDocument/onTypeFormatting`、`textDocument/codeAction`（`source.formatDocument`、`source.organizeImports` 和 `refactor.rewrite`）以及 `workspace/symbol`、`workspaceSymbol/resolve`。

生命周期/同步通知为 `initialize`、`initialized`、`textDocument/didOpen`、`textDocument/didChange`、`textDocument/didSave`、`textDocument/didClose`、`shutdown` 和 `exit`；`workspace/didChangeConfiguration` 更新设置。服务器接收工作区文件夹/监视文件通知和 `$/cancelRequest`，取消返回 `-32800`。补全触发字符为 `.`、`:`、`@`，签名帮助触发字符为 `(`、`,`。选择范围从令牌嵌套到行/文档；文档链接解析带引号的 import/include；语义令牌增量返回完整数据或替换编辑。范围格式化支持部分选择，会扩展到完整物理行并返回实际替换范围。LSP 位置为从零开始的 UTF-16，C++ 位置为从一开始计数的字节列，适配器只能转换一次；过期文档版本的变更必须忽略。

## 配置与本地化

服务器从 `initializationOptions` 和 `workspace/didChangeConfiguration.settings` 读取 `veroscript` 设置，其中包括导入/隐式导入路径、`stdlibPath`、定义、预定义宏、循环导入/项目范围访问策略及有序 `bindingCatalogPaths`。目录按顺序合并，重复定义会被拒绝；更改后在下一次非生命周期请求时重新加载，并重新分析已打开文档。目录描述宿主符号和文档，不注册运行时回调。

`initialize.locale` 是 BCP 47 首选项；不支持时保留环境目录，缺少文件或键时回退 `en-US`。诊断标识符与消息参数、绑定标识符、类型/成员名、协议字段、URI、路径和源文本永不翻译。

## 不变量

必须保持公共源位置/范围、附在结果上的文档标识和已接受版本、取消/请求关联/连线顺序、稳定的诊断标识符与替换参数、空功能结果与协议失败的差异，以及工具链生成编辑但不应用编辑的规则。用户可见的方法、配置和编辑器行为请见[VeroScript 语言服务器参考](../../User/zh-CN/lsp.md)。

## 调试与 DAP 边界

`VeroScriptDebugSession` 负责编译、断点、步进、VM 检查、表达式求值、变量写入、输出捕获和 JIT/后端选择。`VeroScriptDapAdapter` 只把它映射为 DAP 形状的请求/事件，不能增加 JSON、stdio/TCP 分帧或第二套断点/求值引擎。`FDapLaunchArgs` 包含源文件/路径、覆盖层、绑定、导入策略、入口即停、调用深度、后端、脚本参数、标准库根、此次启动的环境、工作目录和初始标准输入；即使调试附加请求 `Mixed`/`Jit`，也使用 `Vm`。`Initialize` 分别报告引擎版本、平台和 JIT 能力，不得以其中任一项重建产品版本。

`VeroScriptDebugSession::BindBreakpoint` 负责物理行／预处理映射解析，并且只注册精确的已编译源标签。对无效请求，或没有可绑定位置的不可执行请求，它返回未注册结果及本地化显示消息。适配器保留外部 DAP ID、报告实际绑定行，并在启动前断点重新绑定后发布 `Breakpoint`／线协议 `breakpoint` 的 `changed` 事件。

`ConfigurationDone` 同步拥有运行，只有 stopped 事件回调能执行继续/步进/调用栈/作用域/变量/求值；`Disconnect` 恢复活动运行，并在返回后清除会话。传输层必须在 initialize 响应后发出 `initialized`，而不能由 `VeroScriptDapAdapter::Initialize()` 自行发送。

### DAP 传输契约

核心调试器的 `bReadOnly` 经 `FDebugVariable`、`FDapVariable` 传到 DAP 变量的 `presentationHint.attributes = ["readOnly"]`。不可变 variant 的嵌套载荷和监视表达式结果保留该状态，核心调试器继续负责拒绝对不可变值的写入。

`VeroScriptDapStdio::Run` 通过调用方流读写以 `Content-Length` 分帧的 DAP JSON。`FDapStdioRunOptions` 提供绑定/目录路径、脚本参数、标准库路径、环境、工作目录和初始输入默认值。实时绑定优先于目录，且仅借用到 `Run` 结束；目录只足以编译/分析，原生调用需要实时注册表。

stdio 接受 `program`/内联 `source`、`fileLabel`、`workspace`、`cwd`/`workingDirectory`、`stopOnEntry`、`args`、`input`、`env`/`environment`、`stdlibSearchPaths`/`stdlibPath`、导入/定义/目录/策略、`backend` 和 `maxCallDepth`。脚本参数传给 `main(string[] args)`，不添加脚本路径；`input` 是初始脚本标准输入，DAP 标准输入不转发。环境只在本次运行有效，之后恢复。上限为 4096 个参数、2048 个导入/隐式导入/标准库路径和 1024 个环境项；无效 JSON、后端或调用深度会产生本地化协议诊断。

`VeroScriptDapTcp::Run` 复用 IPv4 分帧，并按顺序服务客户端。`FDapTcpRunOptions::BindAddress` 默认为 `127.0.0.1`；空值或 `0.0.0.0` 表示所有接口，建议使用回环。`Port == 0` 请求临时端口，并通过 `OnListening` 报告；`MaxSessions == 1` 表示一个客户端，`0` 表示不限，负值归一为一。非终止的附加断开只关闭客户端并允许下一会话；终止停止监听循环，宿主终止映射为成功服务器返回，其他失败返回非零。

## LSP 工作区与请求契约

`VeroScriptLspStdio::Run` 是 `Content-Length` JSON-RPC 服务器；工作线程分析可使运行期间观察到取消。initialize 的区域不支持时不得改变环境目录。根目录优先级为：数组 `workspaceFolders`（即使为空）优先；没有数组时使用 `rootUri`；两者都没有才用 `FLspStdioRunOptions::DefaultWorkspaceRoots`。根目录是索引根而非导入路径，只递归 `.vs`，并跳过 `.git`、`.verocache`、`build`、`dist`、`Binaries`、`Intermediate`；已打开文档/显式覆盖层优先。工作区文件夹/监视文件变更会更新索引和分析。

服务器公告完成/解析、悬停、定义/声明/类型定义/实现/类型层次、文档符号/选择范围/折叠范围/文档链接、完整加增量语义令牌、签名/引用/高亮/重命名、完整/范围/键入时格式化、工作区符号/解析以及 `source.formatDocument`/`source.organizeImports` 操作。已取消或超时的请求不得发布或缓存部分索引。默认交互预算为 2000 ms、语义分析预算为 5000 ms；其余限制为 512 个补全、2000 个工作区符号、5000 个引用、10000 个索引文件、单文件 2 MiB；服务器范围的 `lspRequestTimeoutMs`、`lspAnalysisTimeoutMs`、`maxCompletionItems`、`maxWorkspaceSymbolResults`、`maxReferenceResults`、`maxWorkspaceFiles` 和 `maxWorkspaceFileBytes` 会受到上限约束。

`VeroScriptFormat` 是词法格式化器；`FFormatOptions` 默认每级缩进为四个空格，也可通过 `bInsertSpaces=false` 输出制表符。LSP 格式化遵循 `options.insertSpaces` 和 `options.tabSize`：前者为 `false` 时每级输出一个制表符，否则每级输出 `tabSize` 个空格（最多 16，缺失或无效时默认 4）。范围格式化扩展到完整物理行，并在 `FRangeFormatResult` 中返回实际范围；失败返回空 LSP 编辑。整理导入仅删除词法文本相同的重复 import，发现块注释分隔符则拒绝编辑。`VeroScriptWorkspace::WorkspaceSymbols` 合并打开缓冲区/覆盖层/关闭文件；`FWorkspaceSymbolsOptions::ShouldCancel` 只为单次查询借用，绝不保留，已取消查询设置 `bCancelled`，结果不得缓存或发布。默认未自定义缓存会因文档/配置变更失效；查询专属选项、覆盖层、根、限制和取消探针均不进入缓存。

## 源码文档注释

源码定义的预处理宏补全项可以携带紧邻宏之前的连续 `//`/`///` 或 `/* ... */` 注释；宿主补全文档来自绑定元数据。`completionItem/resolve` 会保留已有文档；仅在没有 documentation 时才将 `detail` 作为 Markdown 使用。悬停会提取源码声明之前的连续注释，并用当前语言目录的标签呈现 `@param 名称 说明`、`@return 说明` 和 `@returns 说明`；未知标记保留在注释正文中。这是 Tooling 的显示约定，不会改变编译器对注释的处理。

## 语义着色调度

语义着色使用独立分析线程。按序运行的协议线程拥有文档更新、请求状态和输出；分析任务拥有缓冲区、选项及绑定的副本，并在验证版本和依赖后发布不可变解析及 token 快照。同版本 full/delta 复用完整数据；悬停、导航和补全复用有效解析快照。恢复后的着色快照不会充当交互查询的原始 AST。取消和期限检查覆盖词法、预处理、解析、导入、符号遍历及编码。取消排队任务会唤醒协议线程，过期任务无需等待当前分析即可出队。默认交互预算 2000 毫秒、语义分析预算 5000 毫秒；`lspAnalysisTimeoutMs` 分离分析预算，`lspTraceAnalysis` 开启耗时跟踪。详见[请求预算](../../User/zh-CN/lsp.md)。

### 回调、缓存与生命周期契约

`FSemanticTokensOptions::ShouldCancel` 和 `RecordStage` 仅为此次同步查询借用，在其分析线程调用；捕获对象必须有效到调用返回。取消返回 `bCancelled` 且没有 token，不完整结果不得发布或缓存；耗时观察回调的异常会被忽略。`FSemanticTokensResult::Snapshot` 拥有程序和源码存储，但它是不可见的 Tooling 内部状态，不是受支持的外部缓存或复用 API。

复用需要文档内容／版本、配置和依赖均一致。磁盘记录在读取导入源码／包之前取得，在发布或复用之前再次检查；无效记录禁止复用。覆盖层变更也会使受影响的导入失效。关闭并重新打开同一 URI 会清除解析／token 状态和旧结果 ID，即使新文档版本从较小数字重新开始。分析配置变更会使正在计算的任务失效。相同源码标签跳过重复文件系统规范化；不同标签仍进行规范路径比较。

`VeroScriptLspResponsivenessTest.py` 验证真实 stdio 进程的 full/delta 复用、分析期间悬停、取消、期限、依赖编辑、配置变更及关闭重开。词法和工作区测试覆盖取消后的重试，以及加载与快照构造之间的磁盘变更。这些回归测试保留在直属 Tooling／核心测试区域。

Darwin 上 `Private/Internal/VeroScriptLspThread.h` 为协议与语义工作线程配置 8 MiB pthread 栈，其他平台使用 `std::thread`。保持 join 与所有权约定；增大线程栈不代表提高解析器或分析上限。JSON 浮点输出使用 [Core 十进制转换](../../../../../VeroScript/Resources/Docs/Developer/zh-CN/architecture-and-pipeline.md#floating-formatting-support)说明的私有格式化辅助。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/architecture-and-protocols.md) · [fr-FR](../fr-FR/architecture-and-protocols.md) · [ja-JP](../ja-JP/architecture-and-protocols.md) · [ko-KR](../ko-KR/architecture-and-protocols.md) · [zh-CN](architecture-and-protocols.md) · [zh-TW](../zh-TW/architecture-and-protocols.md)
