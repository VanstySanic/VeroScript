# 工作区、LSP 与 DAP

## 工作区状态

`VeroScriptWorkspace` 是项目和模块上下文的有状态边界，拥有已打开文档覆盖层、源文件/导入解析、跨文件分析、工作区符号、预处理上下文和绑定目录输入。已打开的覆盖层优先于磁盘文件；关闭文档即结束该覆盖层的生命周期。选项包括导入路径、标准库路径、隐式导入、定义、预定义宏、循环导入策略、项目范围访问和有序绑定目录路径。目录按顺序合并，重复定义会被拒绝。目录描述宿主函数、类型、成员、注释、模板和文档，但不注册运行时行为。

LSP 在绑定目录文件时间戳变化时重新加载。完整目录加载成功后才替换工作区中的版本；目录暂时不可用或内容不完整时保留上一次有效绑定，并通过 `window/logMessage` 报告离线状态。后续有效快照会刷新分析。

`TryGetDocument` 返回单个打开缓冲区的独立副本；路径不存在时返回 `false`，且不修改输出。
需要全部缓冲区时使用 `SnapshotDocuments`；LSP 读取单个文档时不会复制无关源码。

## 文档生命周期与失效

LSP 在 `textDocument/didOpen` 创建覆盖层，在 `textDocument/didChange` 应用增量变更，在 `textDocument/didSave` 通过可选 `text` 刷新，并在 `textDocument/didClose` 移除覆盖层。打开、已接受变更、保存或工作区配置变更后都会发布诊断。版本按 URI 跟踪；版本小于或等于最后接受版本的变更会被忽略；重新打开会开始新的覆盖层生命周期。服务器可在请求运行时继续读取输入，并以连线顺序执行有界工作；`$/cancelRequest` 取消未完成请求，并返回 JSON-RPC `-32800`。

## 语言功能流

直接服务包括 `VeroScriptAnalyzer`（含导入文件的工作区诊断）、`VeroScriptComplete`、`VeroScriptCallHelp`、`VeroScriptHover`、`VeroScriptNavigation`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptOutline`、`VeroScriptWorkspaceSymbols`、`VeroScriptRename` 和 `VeroScriptFormat`。LSP 通过完成/解析、悬停、定义/声明/类型定义/实现/类型层次、文档符号、选择/折叠范围、文档链接、完整/增量语义令牌、签名帮助、引用/高亮、准备重命名/重命名、文档/范围/输入时格式化、`source.formatDocument`、`source.organizeImports` 与 `refactor.rewrite` 代码操作，以及工作区符号/解析公开它们。只有能安全删除完全重复的导入时才返回 `source.organizeImports`；类或结构体正文的空光标位置可通过 `refactor.rewrite` 生成尚未实现的继承方法骨架。范围格式化会把选择扩展到完整物理行，并返回实际替换范围，而非要求替换整个文档。

命名空间按语义解析而非标识符大小写判定，其中包括嵌套用户命名空间和 `std::` 树。宿主目录条目和软内建符号可参与结果；仅宿主符号的位置使用虚拟 `veroscript:host-bindings` URI。

补全以 `FCompletionItem::MatchQuality` 作为首要文本排序，`SortPriority` 仅用于打破同一
文本质量的并列。适配器必须保留结果顺序；LSP `sortText` 表示同一顺序，只有精确、忽略大小写
精确或前缀匹配明确优于其他项时才设置 `preselect`。基元类型词在补全、悬停和语义令牌中仍是
`Keyword`；容器类型词则为 `TypeAlias`／`type`。两者都是带本地化显示详情的内置类型，没有源
定义，也绝不是重命名目标。显示文本应使用
`tooling.builtin_type.detail` 与 `tooling.builtin_type.documentation` 目录键；协议字段和类型
标识符不得翻译。

对于限定命名空间成员，`FCompletionItem::Label` 和 `InsertText` 保留叶子名称及 tooling 生成的
snippet，`LabelDescription` 保存限定命名空间。序列化为 LSP 时，只有客户端声明支持
`textDocument.completion.completionItem.snippetSupport` 才发送 snippet；否则以普通文本发送叶子名称。
将命名空间序列化为标准 LSP `labelDetails.description`。
可选的 `$valencePresentation` 还会提供供 Valence 使用的最终标签／声明文本和 UTF-16 语义片段，
其他 LSP 客户端可以忽略它。它不得改变过滤、排序或实际插入内容。

LSP 语义令牌适配器会合并已解析的语义范围与词法基线。后者在每个有效物理行上覆盖语言／类型
词、指令、宏和字面量／注释，在解析恢复期间仍可用，并会在编码 LSP 令牌流前消除重叠范围。

## 诊断与本地化

分析按源文件分组诊断，因此导入文件也会收到各自的 `textDocument/publishDiagnostics` 通知。即使被分析文档没有问题也要发布；上一次有诊断而本次没有的文件必须用空列表明确清除。`initialize.locale` 是 BCP 47 首选项；不受支持时保留进程环境目录，缺少文件或键时回退 `en-US`。稳定标识符、消息参数、协议字段、URI、路径、类型名称和源文本永不翻译。

## 调试

`VeroScriptDebugSession` 将核心加载、执行、停止回调、调用栈、作用域、变量和求值映射为工具链数据；`VeroScriptDapAdapter` 映射 DAP 消息，`VeroScriptDapStdio` 和 `VeroScriptDapTcp` 是独立传输层，不得重新实现 VM 求值、断点或对象生命周期。`VeroScriptDebugSession::SetOutputHandler` 设置 `VeroScriptDebugSession::FOutputHandler`，在 `Run()` 活动期间接收带错误标记的刷出文本；`FDebugRunResult` 在 `Output` 和 `ErrorOutput` 中保存完整流，DAP 在终止前发出 `stdout`/`stderr`。面向用户的 LSP 配置见[语言服务器参考](../../User/zh-CN/lsp.md)。

## 调试会话契约

核心不再隐式提供解释器。原生宿主通过 `SetVmBackendFactory` / `SetVmBackendPath`，或互斥的 `FDebugLoadOptions::VmBackendFactory` / `VmBackendPath` 选择后端。`FDapLaunchArgs` 与 `FDapStdioRunOptions` 传递同一宿主侧选择，这些字段不是 DAP JSON 参数；独立 LSP、DAP 产品已选择官方 VM。调试会话保留同一个执行域，实际执行开始后，`Clear` 和重新加载都不会解除后端选择的冻结；加载选项重复指定同一工厂、目录或等价库路径不触发切换。加载时后端选择被拒绝，会清理旧程序及调试状态，之后的 `Run` 不会执行旧代码。

调试加载未显式指定 `ScriptCompileExtensions` 的 VM 时，编译扩展继承调试域的工厂；工作区查询会保留单次请求显式提供的编译 VM 选择。仅选择编译 VM 不会执行扩展，也不会禁用普通源码的分析缓存；实际扩展根、源码导入及处理策略仍决定准备流程和缓存资格。

`VeroScriptDebugSession` 与协议无关，可直接由 IDE、测试或适配器宿主使用。`LoadSource`/`LoadFile` 返回 `FDebugLoadResult`，并按覆盖层、定义、导入策略和可选的非拥有绑定目录编译；`Clear` 使模块和调试器状态失效；`SetBreakpoint` 返回会话本地 ID，接收文件标签、从一开始计数的行号和可选条件。

`BindBreakpoint` 是适配器使用的严格路径：它通过预处理行映射，把从 1 开始的物理源代码行解析到唯一的已编译源文件，再仅以解析后的预处理位置调用 `SetExactBreakpoint` 注册。对于不活动、缺失、有歧义、越界，或没有可绑定位置的不可执行请求，它不注册断点而返回本地化显示消息。函数内请求可前移到该函数后续的可执行物理行；模块初始化必须命中精确行。

`SetRunMode` 选择继续、步入、步过或步出；`SetExecutionBackend`、`SetMaxCallDepth` 和 `SetJitPolicy` 在 VM 运行前应用；`GetLastJitStats` 返回最近快照。调试附加会强制有效后端为 `Vm` 并使 JIT 缓存失效，因此行为不依赖原生已编译入口。停止处理程序接收 `EDebugStopReason`，输出处理程序接收文本和错误标记；停止时 `GetStackTrace`、`GetLocals`、`GetChildren`、`Evaluate` 有效，`SetLocal` 和 `SetChild` 会更新暂停值并返回结构化求值错误。`FDebugRunResult` 保留诊断、输出、错误和退出代码；`bSuccess` 与退出代码应分别解释。

## DAP 适配器生命周期

`VeroScriptDapAdapter` 是进程内请求/事件映射器，不是连线服务器。宿主依次调用 `Initialize`、`Launch` 或 `Attach`、设置断点、`ConfigurationDone`；`ConfigurationDone` 会阻塞直到 `Run` 返回，并按情况发出 `Stopped`、输出和终止事件。主线程 ID 是适配器稳定的单线程 ID。

`setBreakpoints` 即使替换同一源文件的条目也保留稳定的 DAP ID。加载前条目处于待绑定且未验证状态；加载会经 `BindBreakpoint` 重新绑定并为每项发出 `Breakpoint` 事件，线协议将其序列化为 `reason: "changed"` 的标准 `breakpoint` 事件。返回的 DAP 行号是实际绑定的物理行，不一定是请求行。

continue、`Next`、`StepIn`、`StepOut`、`StackTrace`、`Scopes`、`Variables`、`Evaluate` 和 `SetVariable` 在适配器报告暂停时有效。加载并执行程序的进程内路径应从同步的 stopped 回调调用；在线运行域由协议线程在宿主保持暂停期间处理这些请求。进程内的 `Attach` 方法（线协议 `attachMode: "program"` 或省略）使用同一源/文件加载语义，而非附加到已执行 VM；`Disconnect` 恢复活动运行，并在运行返回后清理。进程内调用可经启动参数传入实时 `IHostBindingCatalog`，进程外调用使用序列化目录，且不得保留指针。DAP 名称、路径、表达式、坐标和事件字段均为机器数据。

宿主预备的持久运行域可通过 四参数的 `VeroScriptDapStdio::Run`（或双参数的 `VeroScriptDapTcp::Run`）传入 `IVeroScriptLiveDebugDomain`，再发送含 `attachMode: "domain"` 和 `domainId` 的 DAP `attach`。响应给出宿主/运行域身份、源码与绑定修订、源码版本和该运行域的目标能力。DAP `initialize` 能力描述适配器整体可提供的加载与附加模式；该运行域具体支持哪些操作，以附加响应为准。`configurationDone` 激活调试连接而不加载或执行程序；后续宿主调用继续使用同一连接。运行域实现负责在执行前安装调试器、在安全点处理命令、使过期栈帧与变量句柄失效，并按宿主约定在断开时恢复暂停调用。此接口不能接管已进入 JIT 的 VM。默认断开保留宿主进程；宿主须让运行域对象存活到 DAP 会话结束。当前协议只将一个活动执行上下文暴露为 DAP 线程 1；有并发脚本调用的宿主须序列化该运行域的调试调用。

DAP `initialize` 响应的 `body.veroscriptSupportsDomainAttach` 仅在服务器传入运行域桥时为 `true`，否则为 `false`。客户端必须确认该字段严格等于 `true`，才能发送在线附加请求；缺失或为 `false` 均表示不可用，不能回退到程序附加或启动，以免旧宿主忽略 `attachMode` 后加载脚本。

`attachMode` 若提供，必须是 `attach` 请求中的字符串 `domain` 或 `program`；`domainId` 必须为字符串，且仅用于运行域附加。参数不合法时请求失败，不回退到加载程序。

## 调试输出与完成语义

`VeroScriptDebugSession::Run` 区分可观察输出和进程完成。输出处理程序按行缓冲标准输出；完整行、`std::console::flush` 或有界待处理缓冲区都会刷出片段，标准错误立即转发。即使没有处理程序，`FDebugRunResult::Output`/`FDebugRunResult::ErrorOutput` 也保存完整流，处理程序异常与被调试程序相互隔离。`bSuccess` 表示执行成功；成功的整数 `main` 可以有非零 `ExitCode`，失败执行则返回诊断/失败退出代码。调试附加强制 VM 并使 JIT 缓存失效，存储的 `SetExecutionBackend` 首选项不能推翻该不变量。源加载记录预处理行映射；断点和调用栈映射到物理源，条件断点/求值使用活动的定义和预定义宏。

## DAP 传输与启动契约

`VeroScriptDapStdio::Run` 通过调用方输入/输出流使用 DAP `Content-Length` 帧。`FDapStdioRunOptions` 提供实时绑定/目录、脚本参数、标准库根、环境、工作目录和初始输入默认值。实时绑定在 `Run` 期间为借用状态且优先于目录路径；目录可支持编译/分析，原生调用需要实时注册表。

Launch 和程序模式的 Attach 接收 `program` 或内联 `source`、`fileLabel`、`workspace`、`cwd`/`workingDirectory`、`stopOnEntry`、`args`、`input`、`env`/`environment`、`stdlibSearchPaths`/`stdlibPath`、导入/隐式导入路径、定义/预定义宏、目录路径、策略、`backend` 和 `maxCallDepth`。`args` 原样传给 `main(string[] args)`，不附加脚本路径；`input` 是初始脚本标准输入，DAP 标准输入不转发；环境只作用于本次运行，之后恢复。执行前会拒绝格式错误的 JSON、不受支持后端、无效调用深度及过大的集合：4096 个参数、2048 个导入/隐式导入/标准库路径、1024 个环境项。`Initialize` 返回协议元数据与能力；方法和机器字段必须精确。`jitCapability` 表示核心 JIT 服务契约（当前为 `backend-services`），不表示选用的后端或原生代码能力；宿主应通过 `IJitBackendFactory::GetDescription()` 获取后端信息。

执行宿主可直接选择后端工厂，或通过 `SetJitBackendPath` 指定 UTF-8 动态库或目录路径。核心加载器校验模块与核心的 ABI，并在工厂及编译产物仍被引用时保留动态库；不会通过 `PATH` 或环境默认值自动寻找后端。Tooling 不加载 JIT 模块：DAP 的 `backend` 仅保存执行偏好，调试仍走 VM。运行域附加时，后端由宿主选择，宿主必须在执行前预备调试桥。

宿主可通过 `FDapTcpRunOptions::Control` 传入共享的 `FDapTcpRunControl`，调用 `RequestStop()` 唤醒套接字等待，并按运行域的断开约定恢复暂停的调用。销毁运行域或卸载模块前应等待 `Run` 线程退出。停止后重新开启端点时应创建新的控制对象。

`VeroScriptDapTcp::Run` 通过 IPv4 顺序接受客户端，默认绑定 `127.0.0.1`，空值或 `0.0.0.0` 表示所有接口；`Port == 0` 选择临时端口并调用 `OnListening`；`MaxSessions == 1` 表示一个客户端，`0` 表示无限制，负值按一处理。正常且非终止的附加断开只关闭客户端，终止请求停止服务器循环；宿主终止映射为成功，其他失败为非零退出。

## DAP 区域与附加终止语义

DAP stdio 将 initialize.arguments.locale 作为 BCP 47 首选项；不受支持时不改变环境目录；在 initialize 响应前应用选择，从而覆盖后续本地化诊断。Launch 和程序模式的 Attach 共享相同源加载选项；在线运行域附加使用上文的独立契约。

成功 Attach 后，`Disconnect` 默认 `terminateDebuggee` 为 false；Launch 会话默认 true；宿主可显式发送该值。false 会释放当前调试会话而不请求终止已附加宿主；true 会请求宿主终止。运行域附加时，只有附加响应声明 `veroscriptCanTerminate: true`，才接受 `terminate` 或请求终止目标的 `disconnect`；否则请求失败且调试连接保持活动。已接受的取消遵循宿主策略。对持续服务客户端的 TCP 宿主而言，这一差别很重要，且仍需遵循 `MaxSessions` 策略。

## 调试已有 Session

宿主提供并保持 `IVeroScriptLiveDebugDomain` 存活；`Info()` 返回同步快照。协议线程请求将 VM 操作排到执行安全点；一条连接只暴露一个活动上下文（thread 1），须串行化被调试调用。`Connect()` 不发回调、不启动或重载代码；失败不保留回调。`Activate()` 完成配置。`SetBreakpoints` 原子替换单个源文件的断点，按请求顺序返回对应结果并保留所给 ID，不为同次更新重复发事件。`Disconnect()` 移除会话断点、排空回调，按宿主策略释放暂停调用；取消仍是独立宿主操作。

适配器断开在线运行域前，等待已经派发的暂停／取消调用返回；关闭期间的新控制请求失败，旧请求不会发给下一条连接，即使重连使用同一个运行域对象。控制派发不得等待断开完成或排给断开所在线程的工作。宿主断开及回调排空在适配器控制锁之外执行；附加、恢复、断开和暂停态访问仍由宿主串行化。

全部桥回调和暂停 VM 的检查、求值、修改及恢复都属于 Session 控制线程。暂停回调须在该线程处理请求。只有 `RequestPause()` 和共享 `FExecutionControl::RequestCancel()` 可跨线程直接控制。借用的程序、绑定、源码 overlay 和入口名视图在回调返回时失效；复制所需元数据。overlay 是当前配置，不是冻结的源码快照；源码及绑定修订由宿主另行公开。

参阅[调试已有 Session](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/session-debugging.md#debug-disconnect-lifetime)。

## 实现归属

参阅[C++ 嵌入与会话 — 实现归属](../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-CN/session.md#session-debug-implementation)。

## 内存检查

调试变量通过 `FMemoryInspection` 传递内存信息。允许的存储地址序列化为十进制字符串 `memoryReference`，`veroMemory` 中可用的 `objectId`、`objectAddress`、`dataAddress`、`byteLength` 同样使用十进制字符串，保留 uint64 精度，缺失值省略。元数据子项只读，不创建可写目标或伪造存储地址。`variablesReference` 仍是不透明的 DAP 句柄，与对象身份无关，不声明任意内存读写能力。

## 通用编译处理与源码元数据

原生宿主通过各查询选项、`FWorkspaceOptions`、`FLspStdioRunOptions`、`FDapStdioRunOptions` 或 `FDapLaunchArgs` 的 `CompileProcessing` 传入 `FCompileProcessingOptions`。工具在加载模块前安装通用处理器，并与 `AnnotationProcessing` 共用核心准备流程；分析、补全、导航、索引和调试加载须使用同一配置。注册表必须已冻结，每次编译创建独立处理器实例。原生处理器及回调不能通过 LSP/DAP JSON 配置传递，宿主应在启动服务时提供。

通用依赖读取器优先于旧注解读取器。缓存检查处理器实际读取的依赖，非缓存处理器的结果不复用；配置变化应调用 `SetOptions`。`ActiveSource` 改写后的坐标通过核心保存的来源区间映射回原始模块，供导航、语义标记、诊断和调试使用。生成声明仍有独立的只读源码身份。悬停和补全读取核心声明文档，遵守其注释保留策略；源码元数据反射类型及成员使用与核心一致的类型签名。

## 脚本编译扩展

各查询、工作区及 LSP/DAP 启动选项的 `ScriptCompileExtensions` 传递核心的扩展根、配置和执行策略。源码中的 `import compiler "extension.vs";` 沿用核心的文件前置规则。路径补全、定义跳转及文档链接识别该指令；跳转只解析目标，不执行处理器。

编辑扩展实现源码时，可通过查询选项的 `Bindings` 提供 `GetScriptCompileServiceCatalog()`，或将该目录导出为 LSP `bindingCatalogPaths` 文件。目录提供 `std::compiler` 类型、签名及仅供编译的占位回调，不给普通业务运行时代码提供执行服务。

语义快照检查脚本回调读取的资源是否存在以及内容是否变化，也检查最初缺失的资源。覆盖层版本及扩展代码变化同样使结果失效。使用自定义资源读取器时，脚本语义结果重新分析，避免保留已关闭扩展任务的代码／资源组合读取回调；full／delta 仍比较已发布的标记流。扩展配置变化后宿主应调用 `SetOptions`。

## 协议编辑与源码坐标

范围格式化和输入时格式化保留多行注释及 raw string 的片段原文。调整周围代码时可格式化完整字面量或文档；片段格式化不得把其中的内容重新解释成代码。

客户端支持动态文件监听时，LSP 同时监听已配置的绑定目录文件和脚本。绑定目录的变更通知会强制重新加载，即使时间戳和文件大小保持不变；没有通知时，查询前检查这两项是否变化。文件无效或离线时保留最后成功的快照，并报告状态。配置变化会替换绑定目录的监听注册。

LSP 和 DAP 的绑定目录路径按照平台的模块身份规则规范化和去重，Windows 大小写别名属于同一个文件。DAP 先验证替换目录和启动配置，再替换已加载程序的绑定；验证阶段拒绝请求时，保留原程序及其原生函数索引。

调试重新加载被取消或抛出异常时，会先清除旧字节码、绑定、源码快照和断点，再向调用者传播异常；随后成功加载仍可复用该调试会话。DAP 将这些加载异常转为失败请求响应，保留连接以接受下一次加载。

DAP 提供线程 1，其他线程的请求会失败。`stackTrace` 遵守 `startFrame` 和 `levels`，数量为零表示剩余全部栈帧，`totalFrames` 始终表示完整栈的数量。分页保留暂停状态中的栈帧身份。

`FDebugStackFrame.Column` 使用物理源码的 UTF-8 字节列号；`FDapStackFrame.Column` 使用从 1 开始的 UTF-16 列号，stdio 适配器再应用客户端的零／一起始偏好。调试加载在核心裁剪源码元数据之前保存列号修正信息，覆盖导入及生成源码；后续磁盘修改不会改变已加载版本的坐标。在线调试域返回的 DAP 栈帧也必须使用 UTF-16 列号。

## 已解析模块环境

分析、查询、工作区和调试选项都接受 `ModuleEnvironment`，与核心编译使用同一份不可变 `FModuleEnvironment`。嵌入宿主可通过 `FLspStdioRunOptions` 和 `FDapStdioRunOptions` 传入。请一次发布完整环境，显式活动绑定目录必须与环境一致，冲突会返回诊断。替换工作区环境会使索引失效；处理器实际读取的资源依赖在没有文档编辑时也会重新核对。

模块映射、覆盖层、源码所有者、依赖可见性、处理配置和资源统一交给核心解析器。导入补全和导航使用同一解析器，原生候选遵守绑定来源和公开签名暴露的类型范围。解析失败且无法恢复作用域时，最终补全回退不枚举宿主目录。标准查询元数据来自核心标准库目录，包含 `std::i18n`；只有 getter 的原生属性保留 `property` 和 `readonly` 语义分类。

提供环境后，LSP 在 `capabilities.experimental.veroscriptModuleEnvironment` 中公布编译／执行指纹及 `moduleSourceMethod: "veroscript/moduleSource"`。以 `{ "uri": "veroscript-module:/..." }` 请求可访问的内存模块，可得到快照的 `uri`、`languageId`、`text`、`readOnly` 和编译指纹；不存在或未授权时返回 `null`。源码响应遵守协议帧预算。客户端要打开此类模块，需为该 URI scheme 接入文档内容提供器，并调用已公布的请求；普通文件导航仍使用文件 URI。

经过合法重导出返回的间接依赖目标也可读取；未返回过的间接依赖不会因此整体开放。`veroscript-module:` 文档为只读，重命名不会返回修改这些文档的编辑。

调试会话保留环境用于执行和外部依赖校验。`TryGetSource` 与 DAP `sourceReference` 返回冻结的内存主文件、内存模块或生成源码；内存主文件即使使用物理文件标签，也保留实际编译文本，不会被缺失或过期的磁盘文件替代。存在覆盖层时返回实际编译版本，未覆盖的模块文本直接使用不可变环境快照。重新加载／清理会使旧源码引用失效。现有在线运行域宿主继续通过运行域的源码回调提供内容。

源码覆盖层遵循加载器对同一路径身份采用的“后提供者生效”规则。编辑器缓冲区在引用、重命名、文档及诊断源码映射中均优先于环境源码快照；同一路径不会因多次覆盖而被当成不同文档重复索引。

成员访问检查保留限定类型身份：不同命名空间的同名类型不会因此共享 private、protected 或友元权限。短名称须能指向同一个无歧义声明；全局声明优先于命名空间中的同名声明。`::Same` 等显式全局接收者名称也指向该全局声明。

整理导入只删除同一连续导入区块中完整、独占一行的重复全局导入声明，并使用当前工作区配置和缓冲区进行模块预处理核对。源码宏、宿主宏和导入的公开宏改写过的行，以及未激活分支均保留；依赖缺失等情况无法确认的行不删除。指令、注释、其他声明及作用域边界都会结束区块；字面量内容和额外语句保留。存在物理续行或词法错误时不提供整理编辑。返回的编辑仅删除重复行，并保留原有换行符。

生成方法补全提供 `FCompletionItem::bOverrideMember`、`bImplementsInterfaceMember`、`Documentation`、`InsertText`、`bInsertTextSnippet`。纯接口实现与继承 override 分开分类，包括带默认体的虚方法；说明使用当前区域，同名重载分组。LSP 使用显式文本编辑，不支持 snippet 的客户端收到普通方法文本。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/workspace-and-debug.md) · [fr-FR](../fr-FR/workspace-and-debug.md) · [ja-JP](../ja-JP/workspace-and-debug.md) · [ko-KR](../ko-KR/workspace-and-debug.md) · [zh-CN](workspace-and-debug.md) · [zh-TW](../zh-TW/workspace-and-debug.md)
