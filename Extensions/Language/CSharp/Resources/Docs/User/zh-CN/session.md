# C# 会话与运行时

## 运行模式

`Run(VeroScriptRunMode.Script)` 初始化模块并调用 `main`；`Eval` 保留增量求值语义。
`EvalOnce` 只执行初始化、不调用 `main`，并允许已选定的 JIT 后端执行初始化。
普通会话每次运行都初始化新状态；热重载会话保留已初始化的全局状态，初始化保留状态时使用 VM。
此模式要求 `VeroScriptCapabilities.EvalOnce`；旧运行库会在执行前抛出 `NotSupportedException`。
执行策略与 JIT 后端选择分别配置。

## 类型化循环统计

`NativeTypedLoopExecutions` 统计原生类型化循环内核的调用次数，包括报告源码可见运行时错误后结束的调用。它是执行可观测性计数器，而不是成功计数器。

## 部署边界与运行时元数据

引用 `VeroScript.CSharp` 包，并部署与操作系统和进程架构匹配的
`VeroScriptInterop` 原生库。托管包提供 .NET 8 和 .NET 9 资产，但不会安装
.NET 运行时，也不会代应用携带原生 RID 资产。如果原生库不在安全的加载目录中，
使用 `VEROSCRIPT_INTEROP_LIBRARY` 指定绝对路径；相对路径会被拒绝。
请在首次调用 SDK 前设置此路径。后续调用使用已选定的原生库。

`VeroScriptRuntime` 提供 `AbiVersion`、`RuntimeVersion`、`HostPlatform`、
`Capabilities`、`CurrentLanguage` 和可用语言 JSON。应使用这些元数据检查兼容性，
不要从平台名称推断语义。诊断代码和参数在切换显示语言后仍保持稳定。

`VeroScriptSession` 管理配置、源代码、包、宿主绑定、值和诊断。会话与其拥有的值采用创建线程亲和模型；应在同一线程使用并通过 `Dispose` 结束生命周期。

## 部署与配置

托管程序集和原生 `VeroScriptInterop` 必须来自匹配的构建和进程架构。需要显式指定原生库位置时，使用环境变量 `VEROSCRIPT_INTEROP_LIBRARY` 的绝对路径；托管包本身不替应用携带任意平台的原生 RID 资产。

`VeroScriptConfiguration` 主要属性如下：

| 属性 | 作用 |
| --- | --- |
| `ImportPaths`、`ImplicitImportPaths` | 导入搜索路径。 |
| `ScriptArguments`、`Defines`、`PredefinedMacros` | 入口参数和预定义宏。 |
| `StandardLibrarySearchPaths` | 标准库搜索路径。 |
| `SourceOverlays` | 内存源文件覆盖。 |
| `BytecodeCacheDirectory`、`UseBytecodeCache` | 字节码缓存。 |
| `MaxCallDepth` | 调用深度上限。 |
| `MaxInstructions` | 单次执行的 VM 指令数上限；`0` 表示不限制。 |
| `TimeoutMilliseconds` | 单次执行的墙上时钟时间上限；`0` 表示不限制。 |
| `EnableExecutionControl` | 启用会话的协作式取消。 |
| `CapabilityPolicyEnabled` | 启用标准库副作用能力策略。 |
| `AllowedScriptCapabilities` | 在启用策略时授予文件、环境、控制台和创建任务能力。 |
| `ExecutionBackend` | `Vm`、`Mixed` 或 `Jit`；默认使用 `Mixed`。 |
| `AllowCircularImports`、`AllowProjectWideAccess` | 导入和项目访问策略。 |

通过 `ApplyConfiguration` 应用配置；`LoadSource`、`LoadFile`、`LoadPackage` 载入内容；`Reload`、`ReloadPackage` 重新载入；`Clear` 清除加载状态。

## 执行、调用与诊断

`Run` 执行入口；`CallFunction`、`CallMethod`、`Construct`、`GetField`、`SetField`、`GetGlobal` 和 `SetGlobal` 提供结构化调用和访问。`HasFunction` 与 `HasMethod` 可在调用前检查目标。

`Clear` 会丢弃当前加载的程序状态；`SetInput` 为下一次执行提供标准库输入函数使用的缓冲区。

需要状态保留式源代码热重载时，应在调用 `Reload` 前显式启用：

```CSharp
session.HotReloadEnabled = true;
var reloaded = session.Reload();
```

在创建会话的线程上，于 `Reload`、`ReloadPackage` 或初始化替换程序的执行之后调用 `GetReloadStatus()`。`Pending` 表示等待初始化，`RolledBack` 表示已恢复原世代，`AppliedWithErrors` 表示替换程序已提交、随后执行出错。可用 `CommittedGeneration` 和 `OperationId` 确认结果。遇到 `ResetRequired` 时，调用 `Clear` 并重新加载程序。若无法清除会话，则重建会话。查询保留已有诊断，并要求 `VeroScriptCapabilities.ReloadStatus`。

后端解析器拒绝 `ApplyConfiguration` 时，会抛出包含原生原因的 `VeroScriptException`，并保留原取消策略。`HotReloadEnabled` 设置被拒绝时同样抛出异常。通过 `GetReloadStatus()` 核对状态，并按错误中的恢复提示处理。 取消请求失败时，异常报告取消操作自身的原生状态，未启用执行控制时，原有原生错误仍供创建线程查询。

`VeroScriptRunResult.Success` 表示操作是否成功，`ExitRequested` 和 `ExitCode` 描述脚本退出状态；非零退出码不一定表示 ABI 调用失败。通过 `GetDiagnostics`、`GetDiagnosticArguments` 和 `FormatDiagnostic` 读取结构化诊断，不要只解析显示文本。

## 执行限制、取消与能力策略

`MaxInstructions` 和 `TimeoutMilliseconds` 是单次执行的协作式限制；设为 `0` 时分别表示不限制。VM 与符合条件的受控 JIT 路径保持相同的指令计数、执行限制、取消和能力约束。限制和取消请求不会强行终止已经运行的原生或托管宿主回调。

启用执行治理时，`Mixed` 默认使用 VM；应先测量目标工作负载，再通过 `VeroScriptJitPolicy.EnableControlledMixed` 选择启用符合条件的受控 JIT。显式 `Jit` 选择不受此开关控制。不支持或成本模型更倾向 VM 的入口仍使用受治理的 VM 回退。后端 `ControlledExecution` 表示支持能力，`ControlledNativeExecutions` 和 `AotExecutions` 分别记录实际受控原生入口和 AOT 入口调用次数。

启用 `EnableExecutionControl` 后，可以调用 `RequestCancellation()` 请求取消当前执行。该方法是线程安全的，也是唯一允许从非创建线程调用的会话操作。调用返回前必须保持会话有效；取消只会在执行安全点被观察到。
成功再次应用配置会重置会话的取消令牌，因此取消后可以先重新配置，再准备下一次执行。

启用 `CapabilityPolicyEnabled` 后，标准库副作用只能使用 `AllowedScriptCapabilities` 中的标志：`FileRead`、`FileWrite`、`FileDelete`、`EnvironmentRead`、`EnvironmentWrite`、`ConsoleInput`、`ConsoleOutput`、`TaskSpawn` 和 `ProcessDirectoryChange`。`chdir` 同时要求 `EnvironmentWrite` 和 `ProcessDirectoryChange`。该策略只约束内置函数；宿主回调仍需自行执行授权检查。关闭策略时，这些内置能力默认全部允许。

## 后端与性能策略

`VeroScriptExecutionBackend.Vm` 使用虚拟机，`Mixed` 在冷代码和优化执行层之间选择，`Jit` 尽可能提前编译并对不支持的函数回退。后端优化不改变语言语义。

使用 `SetJitPolicy` 设置热调用阈值、编译尝试上限、缓存条目上限和提前编译预算；`GetLastJitStats` 返回编译、命中、回退、缓存及 `DirectNativeCalls` 数据。`DirectNativeCalls` 表示在同一代 JIT 代码中绕过虚拟机值桥、直接到达另一个原生入口的调用次数。会话可复用代码缓存与字节码缓存相互独立。若 `VeroScriptRuntime.Capabilities` 包含 `SessionJitCache`，可通过 `SetJitCacheLimits`、`GetJitCacheLimits` 和 `GetJitCacheStats` 管理在满足条件的会话执行之间保留的已编译代码。`VeroScriptJitPolicy.ReuseSessionCode` 默认是 `true`。`MaxCompileAttempts` 或 `MaxCachedEntries` 为零表示不限制相应计数；原生代码和完整保留载荷的字节上限仍然生效。复用要求有效后端不是 VM，原生函数绑定具有稳定的修订号。绑定或回调变化会使缓存失效。启用执行治理时，可复用治理要求相匹配的缓存上下文。默认上限为 8 MiB 原生代码和 32 MiB 完整保留载荷；设为零会禁止保留对应资源。统计是当前缓存上下文已稳定的累计值，并非实时或进程级内存计数。更改策略、上限、后端、配置、模块代际、绑定或热重载/可观测状态会使该上下文失效；达到资源上限会安全回退，不改变脚本语义。应在固定输入、配置和缓存状态下比较性能，并分别测量宿主回调、值转换和包加载成本。

## 原生 JIT 能力与计数器

`NativeLeafExecutions`、`NativeCfgExecutions`、`BridgeExecutions`、`LinearExecutions`
及 `VmTrampolineExecutions` 统计最近一次执行实际进入普通可执行入口的次数，包含以错误或
控制转移退出的入口。对应的 `*Entries` 属性统计生成的代码产物。热缓存可以不编译而执行；
`GetJitCacheStats().CompileAttempts` 仍保留当前缓存上下文的累计值。
`ScalarNativeFastPathCalls` 统计宿主原生调用；直接调用和类型化循环内核属于嵌套事件，
不能再加到普通入口次数中。旧的兼容 runtime 对未提供的计数器返回零。这些是聚合统计；
可选的逐函数 JIT 诊断仍属于原生 Core/C++ API。

`CostModelVmFallbacks` 是有效编译路径因预测成本高于 VM 而被刻意选择的
`Fallbacks` 子集，不是编译失败，也不改变脚本语义。
`NativeTypedLoopExecutions` 统计经原生后端调用的类型化循环内核次数，包含报告源码
可见运行时错误的调用；它不是成功计数器。必须分别检查 `session.GetJitBackendInfo().CapabilityFlags` 中的
`VeroScriptJitBackendCapabilities.NativeCode` 和
`VeroScriptJitBackendCapabilities.NativeTypedLoops`；不能由 `HostPlatform` 或非零计数器
推断任一能力。

## 生命周期与线程

`VeroScriptSession`、`VeroScriptValue`、`VeroScriptCallable` 以及回调期间的借用值都不应跨创建线程使用。`VeroScriptValue` 是拥有值，使用 `Clone` 复制需要跨越更长生命周期的值；回调中的 `VeroScriptBorrowedValue` 只在回调期间有效。完成后始终调用 `Dispose`。

附加调试器时使用 VM。热重载在 VM 中初始化保留状态，符合条件的调用仍可使用所选 JIT 后端。编译和脚本运行失败通常通过 `VeroScriptRunResult.Success == false` 返回。原生所有权拒绝也可能返回带结构化诊断的失败结果，应检查结果和 `GetDiagnostics()`。其他原生状态错误抛出 `VeroScriptException`；托管参数、生命周期及 ABI 兼容性检查抛出各自对应的托管异常。

会话回调也遵循线程和 ABI 边界：脚本回调可能在 VM 工作线程上并发运行，应同步共享的托管状态。输出、诊断和变更回调中的异常会被拦截；宿主函数、实例拦截器、泛型工厂和序列化器中的异常会转换为回调失败。

## 独立 VM 选择

标准 Interop 产品显式组装字节码 VM。解释器选择独立于 `ExecutionBackend` 和 JIT 选择。
在会话空闲的创建线程调用 `SetVmBackend(path)`，可选择匹配的解释器库文件或目录；
null 或空字符串清除选择，替换失败保留原选择。`GetVmBackendInfo()` 复制身份、版本、目标、
库路径、原始 VM 能力及选中/冻结状态；静态选择的库路径为空。VM 能力位分别表示字节码（1）、
执行治理（2）、调试（4）、热重载（8）和清理（16），与进程能力标志独立。

先检查 `VeroScriptCapabilities.VmBackendSelection`；缺少该功能的同 ABI 旧运行时会在调用
可选导出前抛出 `NotSupportedException`。已提交的执行会冻结 VM 域，包括 JIT 和编译扩展执行。
`Clear` 和重载不会解除冻结；要更换冻结后的选择，应创建新会话。清除选择不会恢复产品 VM，
并可能使后续执行缺少解释器。托管元数据快照在替换选择或释放会话后仍可读取。

## JIT 显式选择

执行策略与后端选择分别设置。未提供已安装模块环境时，Runtime 不提供默认模块：未选后端时 mixed 使用 VM，强制 jit 在执行前失败。会话空闲时，在所有者线程使用 `VeroScriptSession.SetJitBackend(path)` 选择独立安装、兼容的库文件或目录；`SetJitBackend(null)` / `SetJitBackend("")` 清除选择。替换失败保留先前后端。`GetJitBackend()` / `GetJitBackendInfo()` 查询实际身份和原生能力；`backend-services` 仅表示 Core 服务契约。执行治理遵循上文的受控 JIT 策略。文件/目录发现与 ABI 规则见 JIT 指南。

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/zh-CN/index.md)

```CSharp
using var session = new VeroScriptSession();
session.SetJitBackend("/absolute/path/to/Jit");
session.LoadSource("int main(){ return 0; }");
var result = session.Run();
```

<a id="jit-backend-capabilities"></a>

通过 `session.GetJitBackendInfo().CapabilityFlags` 和 `VeroScriptJitBackendCapabilities` 查询所选后端能力。`Capabilities` 保留原生位掩码，包括未知位。快照拥有字符串副本，替换后端或释放会话后仍可读取。未选择后端时，标志为 `None`。全局 `VeroScriptRuntime.Capabilities` 表示 Core 服务，不表示所选后端的原生能力。

<a id="diagnostic-translation"></a>

`VeroScriptRuntime.Translate(englishMessage)` 使用进程级当前语言和目录翻译诊断消息。未知消息原样返回并保留嵌入 NUL，空输入返回空字符串；拒绝 null 输入和非法 UTF-16。绑定将 `vs_interop_locale_translate` 返回的模块自有 UTF-8 缓冲区复制为托管字符串，并在成功或失败时通过 `vs_interop_buffer_destroy` 释放缓冲区。

## 保留调用句柄的释放

<a id="callable-disposal"></a>

`VeroScriptCallable.Dispose()` 会立即使托管包装器失效；如果调用正在进行，原生销毁会推迟到最后一个活动调用返回。释放不会等待原生执行结束，因此回调可以等待另一线程释放此包装器。同一包装器上的调用仍然串行执行。这项生命周期规则不会放宽所有者线程检查，也不会取消宿主必须从外部串行化会话访问的要求。

## 注解服务与诊断严重级别

`VeroScriptRuntime.CompileCapabilities` 独立报告编译期服务，不属于执行或 JIT 能力。`AnnotationProcessing` 支持通过 `SetAnnotationProcessingService(nint)` 接入原生扩展提供的有效、不透明服务句柄。仅在会话空闲时由创建线程设置或清除，`0` 表示清除。会话复制服务配置，调用方可在设置成功后释放自己的句柄；`ApplyConfiguration` 保留服务。处理器代码和回调数据须在最后一次使用结束前保持有效。同 ABI 的旧运行库返回 `None`，并在调用此操作时抛出 `NotSupportedException`。

`GetDiagnostics()` 保留 `Severity`：`Error`、`Warning`、`Information` 或 `Hint`。诊断数非零不代表编译失败：处理器警告、信息和提示可以与 `Success == true` 同时出现。旧运行库缺少严重级别查询时，诊断仍可读取，严重级别按 `Error` 返回。

## 宿主控制台回调

`VeroScriptSessionOptions.ConsoleQuery` 返回 `VeroScriptConsoleInfo`，报告输出终端、颜色/光标/清屏能力以及可见列数和行数。非正尺寸表示未知。控制操作同时需要查询回调与 `ConsoleControl`；省略任一项时不支持控制。`ConsoleControl` 接收 `VeroScriptConsoleOperation` 和两个整数：ANSI 前景/背景色 `0..15`，默认色 `-1`；清行模式 after/before/all 对应 `0/1/2`；从零开始的列/行；光标可见性 `0/1` 使用第一个参数。未使用的参数为零。

`ConsoleWrite` 接收 `std::console::write` 的精确文本，保留嵌入 NUL，不额外添加换行；省略时仍通过 `Print` 缓冲输出。仅在同步操作成功完成后返回 `true`。查询异常表示无能力，控制异常返回失败；写回调返回 `false` 或抛出异常会使脚本执行失败。

回调在会话销毁完成前保持托管引用，可能在 VM 工作线程上并发运行，须同步共享状态。运行时可能在执行边界重置样式、显示光标，包括失败或取消之后。不要在回调中释放会话或重入普通会话操作。终端输入独立于输出能力。新回调需要匹配的原生运行库；同 ABI 的旧运行库保留行输出，但忽略尾部控制台回调。

通过 C# SDK 加载的脚本可在可执行反射和函数签名中使用基本类型别名，包括嵌套值容器与引用存储。例如，可按 `array<int64>` 请求 `array<long>`。这不会放宽有符号性、整数位宽、对象类型身份或引用只读性检查。源码与序列化包执行使用同一套 Core 检查。

宿主协议回调（`Clone`、`Equals`、`Hash`、子类工厂）由原生值持有，直到最后一个携带协议的值释放；会话释放后仍可能调用，最终释放也可能发生在其他线程。回调不得访问已释放的会话。设置托管协议回调需要可选的原生回调持有入口；同 ABI 的旧运行库在注册前抛出 `NotSupportedException`。若协议注册后持有失败，会话会关闭以保护回调数据。泛型工厂回滚只释放一次已转移或托管持有的数据。

## 通用编译服务与模块环境

`SetCompileProcessingService(nint)` 接入原生 SDK 提供的通用编译服务，需要 `CompileCapabilities.GeneralProcessing`；`0` 清除服务。调用时会话必须空闲且位于创建线程。句柄须在调用期间有效；会话复制配置后，调用方可释放原句柄。普通 `ApplyConfiguration` 保留此服务，处理器代码及回调数据仍须保持到最后一次使用。

`ApplyConfiguration(configuration, moduleEnvironment)` 显式选择原生 SDK 创建的不可变模块环境，需要 `Capabilities.ModuleEnvironment`。`0` 移除环境，单参数重载保留当前环境。仅在创建线程的空闲会话上调用；成功后会话持有快照，调用方可释放原句柄。更换环境前先 `Clear()`。环境自有绑定目录不允许本地注册或导入目录，应在原生侧准备合并快照；本地绑定冲突会在修改配置和取消状态之前被拒绝。提供者、绑定及处理器所在原生模块须保持加载直到最后一次使用。旧运行库缺少相应能力时会抛出 `NotSupportedException`。

## Session 后端选择参数

`VeroScriptSession.SetVmBackend` / `SetJitBackend`

这些 Session 设置方法接受匹配的文件／目录；提供的模块环境包含解析器时，也接受已安装后端 ID。`auto` 请求环境候选，VM 还可使用保留的产品后备。`none` 或原生空字符串禁用选择；`null` / `""` 映射为该空请求。新建会话未显式选择时可继承环境；省略与显式清除不同。重新配置现有会话时，省略 VM 选择会保留已有执行域。显式选择失败保留先前后端。仅在创建线程的空闲会话上设置；VM 必须在执行前更换，因为 Clear／reload 不解除执行域固定。JIT 选择与 VM／Mixed／JIT 执行策略独立。

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

`VeroScriptRuntime.CompileCapabilities` 使用 `VeroScriptCompileCapabilities`，与执行能力分开；不提供该查询的旧同 ABI runtime 返回 `VeroScriptCompileCapabilities.None`。`GetJitBackendInfo().CapabilityFlags` 使用包含 `DirectScalarCalls` 的 `VeroScriptJitBackendCapabilities`，表示后端支持，不证明某入口实际执行了原生代码。各结构化诊断由 `VeroScriptDiagnostic.Severity` 分类。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](session.md) · [zh-TW](../zh-TW/session.md)
