# 调试

VeroScript 调试器使用 Debug Adapter Protocol（DAP），并通过 VM 执行脚本。调试和热重载场景不会使用 JIT 执行路径。

## 标准输入输出模式

```powershell
veroscript --debug script.vs
```

该模式通过标准输入输出传输 DAP 消息，适合由编辑器或其他调试客户端启动并管理。

## TCP 模式

```powershell
veroscript --debug --dap-port=4711 script.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 script.vs
```

`--dap-port` 启动 TCP DAP 服务，端口范围为 `1` 到 `65535`；`--dap-bind` 指定绑定地址，默认是 `127.0.0.1`。只在确实需要远程或独立客户端连接时开放非回环地址。

`veroscript-dap` 是同一调试宿主的薄封装。IDE 的 F6 启动和附加调试也使用 DAP；调试会话断开时，客户端可以通过 `terminateDebuggee` 选择是否结束调试进程。

## 会话生命周期与启动参数

客户端必须先发送 `initialize`，再发送 `launch` 或 `attach`。适配器会返回包括
`supportsTerminateRequest` 在内的能力，以及 `name`、`version`、`platform` 和
`jitCapability` 元数据。线协议宿主会在 `initialize` 响应之后发送 `initialized` 事件。

适配器当前报告以下 DAP 能力：

| 能力 | 值 |
| --- | --- |
| `supportsConfigurationDoneRequest` | `true` |
| `supportsStepInRequest` | `true` |
| `supportsStepOutRequest` | `true` |
| `supportsStepBackRequest` | `false` |
| `supportsConditionalBreakpoints` | `true` |
| `supportsEvaluateRequest` | `true` |
| `supportsSetVariable` | `true` |
| `supportsTerminateRequest` | `true` |

`jitCapability` 表示核心 JIT 服务契约（当前为 `backend-services`），不表示已安装或选用原生后端。

`attachMode: "domain"` 与 `domainId` 用于连接宿主预备的持久运行域，调试连接可跨越多次脚本调用。
客户端必须先确认 `initialize.body.veroscriptSupportsDomainAttach` 严格等于 `true`，否则应拒绝
在线附加，不能回退成加载程序。`configurationDone` 只激活连接，不加载或启动脚本；具体能力以
附加响应为准。默认断开按宿主约定恢复暂停执行并保留宿主。该模式不能接管已经进入 JIT 执行的 VM。
详见[运行域契约](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/session-debugging.md)。

下文的程序加载流程适用于 `launch` 和程序模式的 `attach`（`attachMode: "program"` 或省略）。
两者都通过相同的调试加载器加载程序。`attach` 用于 IDE 连接到由嵌入式
宿主或 TCP 宿主持有真实绑定的会话；它不是附加到已经运行的 VM。设置断点后发送
`configurationDone`，程序才开始执行。

`launch` 和 `attach` 支持以下参数：

| 字段 | 含义 |
| --- | --- |
| `program` | 脚本路径。CLI 调试命令可以提供默认值。 |
| `source` | 内联源代码。非空时优先于 `program`。 |
| `fileLabel` | 内联 `source` 的显示标签和源代码身份。 |
| `workspace` | 相对启动路径的基准目录。 |
| `cwd` | 脚本执行目录和相对路径解析目录；也接受别名 `workingDirectory`。未提供时，在可用的情况下使用 `workspace`，目录必须存在。 |
| `args` | 传给 `main(string[] args)` 的字符串数组，不包含脚本路径。 |
| `input` | 脚本的初始标准输入，与 DAP 协议输入流相互独立。 |
| `env` | 本次调试的环境变量覆盖。规范形式是对象；`environment` 是别名，也接受 `{ "name", "value" }` 项组成的数组。 |
| `stdlibSearchPaths` | 有序的标准库搜索根目录；`stdlibPath` 是单个根目录的兼容别名。 |
| `importPaths`、`implicitImportPaths` | 普通模块搜索路径和隐式导入路径。 |
| `defines`、`predefinedMacros` | 预处理器定义和宿主风格的预定义宏。 |
| `sourceOverlays` | 解析导入模块时使用的内存源文件。 |
| `bindingCatalogPaths` | 按顺序加载的绑定目录文件，作为元数据/桩绑定使用；嵌入式宿主提供 `LiveBindings` 时，真实绑定优先。 |
| `allowCircularImports`、`allowProjectWideAccess` | 导入策略和项目可见性策略。 |
| `stopOnEntry` | 是否在入口点暂停，然后再继续正常执行。 |
| `backend`、`maxCallDepth` | 后端偏好和调用深度限制；调试执行仍使用 VM 路径。 |

`program`、导入路径、标准库路径、隐式导入路径、覆盖文件路径和绑定目录路径中的相对
路径，会以 `cwd`（如果设置）为基准，否则以 `workspace` 为基准。提供 `workspace` 但不
提供 `cwd` 时，工作区也会成为脚本执行目录。环境变量覆盖只在本次调试期间生效，结束后
会恢复。使用真实绑定的附加会话不能同时指定环境变量覆盖或工作目录覆盖。

CLI 允许在脚本路径后提供脚本参数，并将它们作为 DAP 请求的默认参数：

```powershell
veroscript --debug hello.vs red blue
veroscript --debug --dap-port=4711 hello.vs red blue
```

显式的 DAP `args` 数组会替换这些默认值。单次启动最多接受 4096 个脚本参数、三个模块
路径数组各最多 2048 项，以及最多 1024 个环境变量项。环境变量名不能为空、不能包含
`=` 或 NUL；环境变量值不能包含 NUL。

## 异步执行与控制

对于标准输入输出和 TCP 线协议宿主，`configurationDone` 会在 VM 执行完成前得到确认，
VM 在执行工作线程中运行，因此协议读取线程仍可处理控制请求和暂停状态请求。`stopped`
事件的 `reason` 为 `entry`、`breakpoint`、`step` 或 `pause`；当协作式暂停中断正在运行
的 VM 时，原因是 `pause`。

`continue`、`next`、`stepIn` 和 `stepOut` 只能在收到 `stopped` 后使用，并会恢复执行。
`stackTrace`、`scopes`、`variables`、`evaluate` 和 `setVariable` 也只能在暂停期间使用。
应在 `configurationDone` 前或暂停期间设置断点；执行过程中修改断点会被拒绝。`pause`
只能在 VM 运行时使用，并请求一次协作式暂停；`terminate` 请求协作式取消。客户端应等待
随后的 `exited` 和 `terminated` 事件，不应假定请求响应本身已经结束执行。

`threads` 请求会公开一个逻辑 VM 线程，ID 为 `1`，名称为 `main`。这是调试器的脚本执行
视图；线协议线程和执行工作线程不会作为脚本线程公开。

对于 `disconnect`，`launch` 的 `terminateDebuggee` 默认值为 `true`，`attach` 的默认值为
`false`。在多会话 TCP 宿主中，`terminateDebuggee=false` 只结束当前客户端会话并保留监听器；
`true` 会停止宿主监听。`terminate` 始终取消调试运行并请求宿主停止。

## TCP 宿主与嵌入

CLI TCP 宿主默认服务一个客户端会话。SDK 宿主可以使用 `VeroScriptDapTcp::Run` 和
`FDapTcpRunOptions`：`MaxSessions=0` 会持续服务客户端直到宿主终止，`Port=0` 会选择临时
端口并通过 `OnListening` 报告，`BindAddress` 通常应保持为 `127.0.0.1`。嵌入式宿主可以
设置 `FDapStdioRunOptions::LiveBindings`，让 `attach` 会话执行已注册的宿主函数，而不是
绑定目录桩。真实绑定目录必须在 DAP 运行期间保持有效。嵌入式宿主自行报告就绪状态时，
可以将 `bAnnounceListen` 设为 `false`；默认监听通知会写入宿主错误流。
`VeroScriptDapStdio::Run` 在会话正常结束时返回 `0`，协议或 I/O 失败时返回 `1`，客户端
请求终止宿主时返回 `2`。
对于 `VeroScriptDapTcp::Run`，客户端请求终止宿主属于正常返回（`0`）；监听、接受连接或
其他非正常会话失败会返回非零值。

## 断点支持

适配器通过 `setBreakpoints` 支持源代码行断点，不支持异常断点和函数断点。
不带任何过滤器的 `setExceptionBreakpoints` 仅作为标准客户端启动握手中的清空操作成功；
包含异常过滤器的请求以及所有 `setFunctionBreakpoints` 请求都会返回失败的 DAP 响应。
客户端不应将这两类断点显示为已启用。

`setBreakpoints` 使用从 1 开始计数的物理源代码行。在 `launch` 或 `attach` 加载调试目标之前，
返回的断点为 `verified: false` 的待绑定状态。代码加载后，适配器会绑定它，并以相同的 DAP
断点 ID 发出标准 `breakpoint` 事件，`reason` 为 `"changed"`。只有路径唯一解析为已编译源文件、
请求行在预处理后仍处于活动状态且存在可执行位置时，断点才会验证成功；绝不会仅按文件基名
匹配。绑定失败的断点保持未验证，并带有本地化 `message`。

函数内不可执行的物理行可绑定到该函数中后续的可执行物理行；客户端必须采用响应返回的
`line`。模块初始化器中的断点必须命中精确的物理行。被预处理移除的行、超出范围的行以及
不存在或有歧义的源文件都不会绑定。

其他不支持的 DAP 命令同样会返回失败响应，并附带本地化错误消息。可以在 DAP
`initialize` 参数的 `locale` 中设置 BCP 47 标签来选择显示语言。客户端应根据 DAP
的 success/error 字段判断结果，不应根据翻译后的消息文本分支。

## 调试过程中的输出

`std::console::write` 和 `std::console::error` 写入时不会追加换行。
调用 `std::console::flush()` 可以立即显示等待中的标准输出。DAP 适配器会在程序仍在
运行时，将标准输出和错误输出作为不同的 `output` 事件转发；最终运行结果也会分别保留这两个流。

## 执行限制

调试器使用完整的 VM 调试路径，支持断点、单步和变量检查。请求 `--backend=mixed` 或 `--backend=jit` 不会让当前调试执行切换到 JIT；调试结束后，普通运行可以恢复请求的后端。

调试器和宿主绑定的更细粒度集成见[宿主集成](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md)。

## 内存检查

暂停时展开对象或区域，可检查 `[object_id]`、`[byte_length]` 等只读元数据；地址仅在允许时显示。ID 标识存活对象，地址只是瞬时观察值。检查元数据不能赋值。DAP `variablesReference` 是协议句柄，任意 `readMemory` / `writeMemory` 请求仍不支持。

## 调试栈帧中的源码快照

栈帧提供正数 `sourceReference` 时，客户端应使用 DAP `source` 请求文本。返回内容是内联、生成或内存源码的实际编译快照，也包括磁盘文件过时或缺失的源码覆盖。应以只读方式显示；修改同名磁盘文件不会改变正在执行的程序。恢复执行后，已捕获的源码文本仍可读取，但栈帧和变量句柄会失效。重新加载或清空程序后，旧源码引用也会失效，应从当前栈帧重新取得。

## 原生调试宿主生命周期

在所属线程串行处理生命周期操作。Run 执行期间（含回调）会拒绝 LoadSource/LoadFile 和嵌套 Run，保留当前程序；Clear 不执行操作，应在 Run 返回或异常退出后调用。Run 在开始时快照脚本参数，回调内修改仅影响下次执行。替换／清空 SetStopHandler 或 DAP 事件处理器仅影响后续通知；当前回调及捕获值保留到返回。这不允许并发访问。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/debugging.md) · [fr-FR](../fr-FR/debugging.md) · [ja-JP](../ja-JP/debugging.md) · [ko-KR](../ko-KR/debugging.md) · [zh-CN](debugging.md) · [zh-TW](../zh-TW/debugging.md)
