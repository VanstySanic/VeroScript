# Tooling 模块

`VeroScriptTooling` 是围绕 VeroScript 语言核心提供的公共分析层，面向编辑器、
语言服务器、静态检查器和调试器集成。

## 公共能力

| 能力 | 作用 |
| --- | --- |
| 分析器 | 分析源代码或文件并返回结构化诊断。 |
| 格式化 | 生成完整源文件替换；格式化是词法级的，不会展开宏。 |
| 补全与调用帮助 | 提供上下文补全、合并的重载候选、签名帮助、继承/接口方法实现生成、宿主元数据和预处理器条目。 |
| 悬停与导航 | 提供已解析的声明、文档注释、定义位置和命名空间感知的范围。 |
| 大纲与语义 token | 根据声明提供文档符号和语义高亮，包括命名空间与宏。 |
| 引用与重命名 | 提供跨文件引用、读写信息、重命名准备和工作区文本编辑。 |
| 工作区 | 提供打开缓冲区覆盖、多文件分析、导入解析和工作区符号搜索。 |
| LSP/DAP 传输 | `VeroScriptLspStdio::Run` 提供 LSP，DAP 接口提供调试传输；LSP 还支持 resolve、类型导航、范围、链接、token delta、格式化和格式化代码操作。 |

生成的方法补全项会设置 `FCompletionItem::bOverrideMember`；
`bImplementsInterfaceMember` 仅在生成的方法用于实现纯接口要求时为 true；覆盖虚接口默认方法时，
会归类为继承成员覆盖。`Documentation` 遵循当前 Tooling 语言，
`InsertText` 包含生成的方法骨架，`bInsertTextSnippet` 表示其中是否有 snippet 占位符。
同名重载会合并为一个条目，并在详情中逐行列出签名。LSP 会将生成候选作为明确的文本编辑发送；
客户端不支持 snippet 时会收到普通方法文本。

C++ 查询 API 返回的源码位置从 1 开始。已打开文档的覆盖内容优先于磁盘文件，因此工作区查询可看到尚未保存的导入内容。`FWorkspaceOptions` 提供导入路径、标准库搜索根目录、定义、预定义宏、隐式导入、项目级访问及可选的宿主绑定元数据。

原生嵌入宿主可在工作区／查询选项、`FLspStdioRunOptions` 及 `FDapStdioRunOptions`／`FDapLaunchArgs` 中提供 `AnnotationProcessing`，并使用与编译相同的冻结处理器注册表。LSP 工厂及依赖读取器须能安全处理协议与分析工作线程的并发调用。复用前会检查已记录的依赖内容及存在状态；`Cacheable=false` 的处理器结果正常返回，但不进入缓存。

`FWorkspaceSymbolsOptions.WorkspaceRoots` 提供闭文件符号索引的根目录，并且与
`ImportPaths` 分开。索引项目不会让每个被索引的目录自动成为隐式导入路径。
`MaxWorkspaceFiles` 和 `MaxWorkspaceFileBytes` 分别限制大型或不受信任工作区的
扫描文件数量与文件大小。长时间扫描期间会协作检查 `ShouldCancel`；设置
`FWorkspaceSymbolsResult::bCancelled` 的结果不得发布或缓存为新的索引。
工作区符号收集会删除重复的声明身份，因此同一个声明即使通过多个导入被发现，也只会
返回一次。

`VeroScriptWorkspace::ImplementationDocument` 会查找派生类型、接口或虚成员的覆盖，
以及跨文件／分离定义和注解生成的函数或方法正文。普通声明跳转保持指向声明；
`SourceImplementation` 保存独立正文的物理位置和拥有所有权的文件名，未绑定时文件名为空。
自由函数和非虚方法的直接实现查询不扫描整个工作区。

原生宿主可以在工作区／查询选项、`FLspStdioRunOptions`、`FDapStdioRunOptions` 和
`FDapLaunchArgs` 中提供 `AnnotationProcessing`，使用与编译一致的已冻结处理器注册表。
LSP 的处理器工厂和依赖读取回调必须能安全地在协议、分析线程并发运行。复用语义缓存前
会检查处理器记录的依赖内容与存在状态；`Cacheable=false` 的处理器结果正常返回，但不缓存。

生成成员在查询中保留虚拟声明身份，并拒绝重命名。`GeneratedOrigin` 保存生成注解的物理位置，
标准 LSP 的定义／实现跳转和工作区符号位置使用它；补全与签名帮助仍提供生成成员，物理文档大纲
不包含来自虚拟文档的成员。导航不会为了查来源再次执行处理器。

加载程序调试时会保存生成代码。生成栈帧包含 `sourceReference`，DAP `source` 返回不可变源码快照；
原生调试宿主可以通过 `TryGetGeneratedSource` 读取相同文本。在线运行域宿主可按暂停查询的线程约定，
实现可选的同名接口，从自己持有的 prepared program 提供生成文档。
`FormatRangeDocument` 会将选择范围扩展到完整物理行，并返回 replacement range；
它不会只重写不完整的 token 或行片段。
接受 `FWorkspaceSymbolsOptions` 的 `ImplementationDocument` 重载会应用相同的工作区
限制和取消回调；如果查询被取消，会设置 `FNavigationResult::bCancelled`，不得发布
部分定义结果。
`FormatDocument` 和 `FormatRangeDocument` 还为库调用者提供可选的缩进宽度重载；宽度为
0 时保留工作区默认的每级四空格缩进；`bInsertSpaces=false` 时每级输出一个制表符。无状态格式化器也接受相同的空格／制表符选项。
无状态的 `VeroScriptFormat::FormatRangeSource` 入口也提供相同的完整物理行范围约定，
不需要打开工作区即可使用。

`VeroScriptWorkspace::ImplementationDocument` 也能定位另行定义或由注解生成的函数／方法正文，声明导航仍指向声明。`SourceImplementation` 持有正文文件名及物理位置；文件名为空表示未绑定独立正文。直接查询自由函数或非虚方法的实现不会扫描整个工作区。

格式化器的块体使用 Allman 花括号；`else`、`else if`、`catch`、`finally` 和
`do…while` 的尾部 `while` 均另起一行，并与前一块的右花括号对齐。
表达式初始化器保持其语法形式，字面量内容和预处理指令不会被改写或展开。

范围边界落在块注释、原始字符串或多行预处理指令内部时，不返回格式化编辑。
选择完整指令或后续普通代码后可继续格式化；宏的原文和续行保持不变。

语义类型由解析结果和宿主元数据决定，不根据标识符大小写猜测。用户命名空间和
保留的 `std` 树会在补全、悬停、导航、引用、重命名、大纲和语义 token 中保持
一致的 namespace 符号。补全项可以携带参数或泛型类型的 LSP snippet 占位符。
LSP 适配器只有在客户端声明支持
`textDocument.completion.completionItem.snippetSupport` 时才发送 snippet 格式的插入文本；否则发送普通文本。

命名空间段补全与成员补全不同：在表达式或语句的限定路径中，如果替换范围后面没有分隔符，`InsertText` 会追加
`::`。如果后面已有 `::`，或正在补全 `using namespace` 目标，则只插入叶子名称。

对于限定命名空间中的成员，`FCompletionItem::Label` 和 `InsertText` 保留叶子名称及已有
snippet，`LabelDescription` 则单独保存限定命名空间。在 LSP 中它对应标准字段
`labelDetails.description`；只有客户端声明支持
`textDocument.completion.completionItem.snippetSupport` 时才发送 snippet 格式插入文本，否则使用普通文本。
服务器还可以发送供 Valence 使用的 `$valencePresentation`，
其中包含最终标签／声明文本和 UTF-16 语义片段；其他 LSP 客户端可以忽略这一扩展。这些展示字段
不会改变过滤、排序或实际插入文本。

补全先按文本匹配质量、再按上下文优先级排序：精确匹配、忽略大小写的精确匹配、前缀、
单词边界或 CamelCase、包含、子序列依次降低。`FCompletionItem::MatchQuality` 是主排序键，
`SortPriority` 仅用于同级排序；集成方应保留返回顺序，LSP 的 `sortText` 也编码同一完整顺序。
只有首项处于精确/忽略大小写精确/前缀层级且严格优于其余所有项时，LSP 才为它标记
`preselect`。原始类型和容器类型词都是内置类型：原始类型拼写在补全、悬停和语义令牌中仍为
`keyword`，容器类型拼写则为 `type`／类型别名。两者的定义查询均没有源位置，重命名会被拒绝。
其说明性悬停文本遵循当前 Tooling 语言；类型标识符和协议字段保持不变。

LSP 适配器还提供 `completionItem/resolve`、类型定义和实现导航、选择范围、文档链接、
折叠范围、完整和最小 delta 语义 token、全文档、按完整物理行扩展的范围和输入时格式化，以及
`source.formatDocument`、安全删除完全重复导入的 `source.organizeImports`，以及生成方法实现的
`refactor.rewrite`。取消请求和
工作区文件夹或监视文件通知会正确更新索引；闭文件工作区符号索引会在这些变更之间缓存。
`VeroScriptLspStdio::Run` 接受 `FLspStdioRunOptions`；当客户端没有提供
`workspaceFolders` 或 `rootUri` 时，可用 `DefaultWorkspaceRoots` 指定闭文件索引根目录。
客户端提供的根目录会替换这一默认值。

协议、请求、取消和配置详见[语言服务器](lsp.md)，调试传输详见[调试](debugging.md)。

## DAP 调试适配器

通过 CLI 启动标准输入／输出或 TCP 调试适配器：

```powershell
veroscript --debug hello.vs
veroscript --debug --dap-port=4711 hello.vs
```

`--dap-port` 需要 `--debug`，端口范围为 1–65535。TCP 默认绑定 `127.0.0.1`；`--dap-bind=<地址>` 可更改绑定地址，且必须使用 TCP 模式。程序与参数由 DAP `launch` 或 `attach` 提供。调试使用 VM，其性能不代表 mixed 或 JIT 执行。

源代码行断点通过 `setBreakpoints` 设置；不支持异常断点和函数断点。空的
`setExceptionBreakpoints` 请求仅作为标准客户端启动握手中的清除操作成功；非空筛选器和所有
`setFunctionBreakpoints` 请求都会失败。客户端不应将这些断点类别显示为已启用。

在 `launch` 或 `attach` 前设置的源代码断点处于待绑定、未验证状态。代码加载后，适配器会发出
带相同 DAP ID 和 `reason: "changed"` 的标准 `breakpoint` 事件。只有属于唯一已编译源文件、在
预处理后仍活动且拥有可执行位置的行才会验证成功；绝不会回退到同名文件。客户端必须使用返回的
物理行号：函数内的断点可能前移至下一可执行行。完整绑定规则参见[调试](debugging.md)。

进程内集成使用 `VeroScriptDapAdapter`，生命周期为
`Initialize` → `Launch` 或 `Attach` → `SetBreakpoints` → `ConfigurationDone`。
进程内的 `ConfigurationDone` 是同步调用；线协议宿主必须在执行工作线程中调用它，
这样协议线程才能继续处理 `pause`、`terminate` 和暂停状态请求。`Attach` 与 `Launch`
使用相同的加载器，并不是附加到已经运行的 VM。`Pause` 和 `Terminate` 是线程安全的协作式
控制，`IsStopped()` 和 `IsRunning()` 用于读取当前执行状态。

`VeroScriptDapStdio::Run` 接受 `FDapStdioRunOptions`，可以提供
`DefaultProgram`、`DefaultScriptArgs`、`DefaultStdlibSearchPaths`、`DefaultEnvironment`、
`DefaultWorkingDirectory` 和 `DefaultInput` 等默认值。嵌入式宿主可以设置 `LiveBindings`
以执行已注册的宿主函数；非拥有式绑定目录必须在整个 DAP 运行期间保持有效。没有实时绑定
时，`bindingCatalogPaths` 为分析和启动提供元数据/桩绑定。

`VeroScriptDapTcp::Run` 接受 `FDapTcpRunOptions`。`MaxSessions=0` 会持续接受客户端直到
宿主停止，`Port=0` 会选择临时端口并通过 `OnListening` 报告，`BindAddress` 通常应保持为
`127.0.0.1`。嵌入式宿主自行报告就绪状态时，可以将 `bAnnounceListen` 设为 `false`，
从而关闭默认的错误流监听通知。stdio 宿主在会话正常结束时返回 `0`，协议或 I/O 失败时
返回 `1`，客户端请求终止宿主时返回 `2`。

### 与协议无关的调试会话

`VeroScriptDebugSession` 是 DAP 适配器下方可复用的 C++ 层。如果宿主或自定义适配器需要
管理加载、断点、同步暂停回调、状态检查和运行结果，但不需要自己实现 DAP 线协议，请使用
它。`LoadSource` 和 `LoadFile` 接受与工作区层相同的模块路径、定义、预定义宏、源覆盖、
可选绑定目录、隐式导入路径、导入策略和标准库根目录。

`BindBreakpoint` 是适配器使用的严格源代码断点路径：它将从 1 开始的物理行解析到唯一的已编译
源文件。绑定成功时，`FBreakpointBinding` 返回非零 `Id` 和实际物理 `Line`；无法绑定时返回本地化
`Message`。函数内的行可能前移到下一可执行行，因此调用方必须使用返回行，而不能假定请求行就是
实际绑定行。

在 `Run` 之前，可以配置脚本参数、环境、工作目录、输入、最大调用深度、请求的执行后端或
JIT 策略。`FDebugRunResult` 包含 `bSuccess`、结构化 `Diagnostics`、分开的 `Output` 和
`ErrorOutput`，以及 `ExitCode`。调试器仍会将有效执行后端强制设为 `Vm`；请求的后端和 JIT
策略会保留为配置，JIT 统计主要记录由此产生的缓存失效。

`SetStopHandler` 会在执行暂停期间同步调用。处理器可以使用 `GetStackTrace`、`GetLocals`、
`GetChildren`、`Evaluate`、`SetLocal` 和 `SetChild` 检查或修改状态，然后通过 `SetRunMode`
选择 `Continue`、`StepIn`、`StepOver` 或 `StepOut`。面向异步宿主的 `RequestPause()` 和
`RequestCancel()` 是线程安全的协作式控制。普通会话配置和 `Run` 调用必须在控制线程执行；
在请求操作返回前保持会话有效。

## 公共边界与本地化

请使用 SDK 分发的公共头文件；实现专用文件不属于受支持的集成接口。Tooling 与
CLI 和运行时共用本地化目录。诊断 code 和参数在各种语言之间保持稳定，变化的
只是显示文本。参见[诊断本地化](../../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md)。

## 当前语法感知编辑器覆盖范围

补全、大纲、悬停、签名帮助和语义 token 已识别同类型声明组、`do`/`while`、带表达式列表的经典 `for`
子句，以及有符号 8 位 `char` 字面量。初始化表达式会在其自身声明进入作用域之前解析；遮蔽名称或嵌套 lambda
局部变量不会泄漏到后续声明的初始化表达式或 `for` 步进表达式。跨多条物理行的原始字符串在每条物理行上各有一个
字符串语义 token，且该字面量中看似注释标记的文本绝不会被误归类为注释。

## 工作区分析观察者

VeroScriptWorkspace::AnalyzeAll(OnDocument) 对每个文档同步调用观察者一次，路径和分析结果均为回调期间的借用数据。需要在回调后使用时须复制。遍历期间，观察者不得打开、关闭或修改同一工作区的文档。

## 嵌入分析与调试的 VM 选择

直接嵌入 Tooling 的宿主须显式选择解释器。在执行前调用 `VeroScriptDebugSession::SetVmBackendFactory`／`SetVmBackendPath`，或设置 `FDebugLoadOptions`／`FDapStdioRunOptions` 的 `VmBackendFactory`／`VmBackendPath`；工厂与路径互斥。提交执行后选择冻结，清除／重载保留该域，不能据此更换 VM。设置失败保留原选择。匹配的 Runtime 启动程序会提供产品后端，仅链接 Tooling 不会自动装配。

会执行编译扩展脚本的查询使用分析／工作区选项中的 `ScriptCompileExtensions`，指定 `VmBackendFactory` 或 `VmBackendPath`。调试加载在未单独指定编译扩展 VM 时继承所选解释器。不执行扩展的语法分析和普通分析无需 VM；操作期间须保留后端及其资源所有者。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
