# C# 运行时与 Interop 适配器

## 类型化循环统计

`NativeTypedLoopExecutions` 统计原生类型化循环内核的调用次数，包括报告源码可见运行时错误后结束的调用。它是执行可观测性计数器，而不是成功计数器。

`VeroScriptCSharp` 是 `VeroScriptInterop` 之上的托管包装器。包装器负责易用转换、lease、回调根和托管异常；C ABI 拥有原生布局、状态值和生命周期规则。

## 兼容性闸门

`VeroScriptRuntime` 公开 ABI 版本、语义运行时版本、宿主平台、能力位、当前语言、可用语言 JSON 以及目录加载/合并操作。`EnsureCompatible()` 是托管入口闸门。ABI 版本与产品语义版本是不同值，绝不能合并显示。

`EnsureCompatible()` 将完整 ABI 值与 `SupportedAbiVersion` 比较，当前为 `0x00000101`（0.1.1）。任何不匹配均抛出 `NotSupportedException`；主版本相同或 ABI 数值更大都不足以通过检查。ABI 变更时须同步 `NativeMethods.AbiVersion`、原生头文件、公共 API 契约与包文档。

`LoadCatalogDirectory` 在验证 `en-US` 基线后替换运行时目录树。`MergeCatalogDirectory` 将已验证目录加入当前目录，并可显式允许覆盖现有 ID。宿主必须有意识地选择替换或增补，不能靠复制 JSON 或硬编码文化列表模拟合并。

## 会话与句柄

`VeroScriptSession` 拥有原生会话句柄和已注册托管根；其操作遵循底层空闲/运行中及创建线程规则。`VeroScriptValue` 可拥有原生值，或在 `BorrowScope` 内提供借用视图，借用值不得逃出 scope。`VeroScriptPackage` 拥有包句柄，公开元数据与规范签名字节，并跟踪已编译模块是否仍可加载。

`VeroScriptCallable` 可记录所有者线程。调用进行时释放会延后到原生调用放开句柄；这是生命周期保护，不使跨线程调用合法。

## 编组

适配器显式编组定宽整数、浮点、bool、UTF-8/宽字符串、enum、宿主对象、buffer、数组、map、set、queue、stack、deque 和 channel。UTF-8/UTF-16 辅助函数及原生 buffer lease 必须保留所有权，且在所有者释放前复制借用视图。

托管异常会在原生回调边界捕获并转换到 ABI 错误通道，绝不能未检查地穿过非托管 C 回调帧。

## 配置、取消和 JIT

加载或运行前应用 `VeroScriptConfiguration`。除源覆盖层和导入策略外，执行字段有 `MaxCallDepth`、`MaxInstructions`、`TimeoutMilliseconds`、`EnableExecutionControl`、`CapabilityPolicyEnabled`、`AllowedScriptCapabilities` 和 `ExecutionBackend`；指令/超时为零即不限。当能力策略启用时，允许标志控制标准库文件、环境、控制台与任务副作用，VM 必须先于任何 JIT 入口实施限制。

`RequestCancellation` 标记原生执行控制对象，可从非所有者线程调用；它是协作式的，不能停止已经在原生或托管代码中运行的回调。`SetJitPolicy` 应用自适应编译设置并使可复用已编译状态失效；`GetLastJitStats` 返回最近一次运行范围内的增量快照。普通 `CallFunction` 默认可在新的 VM 实例间复用代码，脚本全局和对象不在调用间保留；将 `VeroScriptJitPolicy.ReuseSessionCode` 设为 false 可退出。复用要求有效后端不是 VM，并使用带修订号的原生绑定目录。绑定、观察回调或程序世代变化会使缓存失效。启用执行治理时，可复用治理要求相匹配的缓存上下文。编译尝试或缓存条目预算为零表示不限制相应计数；保留原生代码和完整载荷的字节上限仍然生效。

启用执行治理时，`Mixed` 默认使用 VM；应先测量目标工作负载，再通过 `VeroScriptJitPolicy.EnableControlledMixed` 选择启用符合条件的受控 JIT。显式 `Jit` 选择不受此开关控制。不支持或成本模型更倾向 VM 的入口仍使用受治理的 VM 回退。后端 `ControlledExecution` 表示支持能力，`ControlledNativeExecutions` 和 `AotExecutions` 分别记录实际受控原生入口和 AOT 入口调用次数。

`SetJitCacheLimits` / `GetJitCacheLimits` 控制原生代码和完整保留载荷上限，默认 8 MiB/32 MiB，零不允许对应资源保留。`GetJitCacheStats` 返回当前上下文的累计编译尝试、稳定保留字节和资源限制锁存，非实时或进程峰值计数。这些可选方法要求 `VeroScriptCapabilities.SessionJitCache`；独立部署的兼容运行时缺少该能力时抛出 `NotSupportedException`。VM 负责初始化保留状态，同一世代内符合条件的调用可复用已编译代码。支持 `VeroScriptCapabilities.ReloadStatus` 时，在创建线程调用 `GetReloadStatus()` 可读取原生操作和世代快照。

## 会话操作与生命周期

会话支持 `LoadSource`、`LoadFile`、`LoadPackage`、`Reload`、`ReloadPackage`、`BuildPackageFile`、`Run`、诊断、输入、函数/方法检查、`CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`Construct`、字段/全局访问和 `ApplyClassDefaults`。绕过形式只跳过入口拦截器，方法体的嵌套调用仍可拦截。

宿主集成包括函数、注解、宿主类型/成员、协议、类型别名、泛型宿主类型、实例拦截器、变更处理器和外部序列化器注册。反射与绑定 JSON 仅为检查输出。`VeroScriptValue` 还公开 sequence/map/set/channel 操作，`VeroScriptCallable` 按其所有者线程和弱 `this` 生命周期策略保留并调用函数。

`GetDiagnostics`、`GetDiagnosticArguments` 和 `FormatDiagnostic` 保留结构化消息 ID、文件标签、源坐标和替换参数。`VeroScriptRunResult` 报告执行是否成功、退出状态、退出代码和诊断数；调用方不得用打印消息取代它。借用值随 `BorrowScope` 到期，回调异常在原生枚举返回后重抛，原生 buffer 必须在 lease 释放前复制。

## 诊断与托管生命周期

`GetDiagnostics`、`GetDiagnosticArguments` 和 `FormatDiagnostic` 会保留结构化消息 ID、文件标签、源坐标和替换参数。`VeroScriptRunResult` 报告执行成功状态、退出状态、退出代码和诊断数量；调用方不得以打印消息取代此结果。借用值会随 `BorrowScope` 到期，回调异常会在原生枚举返回后重新抛出，原生 buffer 必须在其 lease 释放前复制。

## JIT 统计映射

托管 `VeroScriptJitStats` 快照保持标量快速路径、trampoline 入口种类与直接嵌套原生调用之间的区分。`DirectNativeCalls` 不是编译次数，不能与 `ScalarNativeFastPathCalls` 合并。这些值描述最近的执行快照，不会改变请求后端或建立新 ABI/版本契约；不支持或受保护入口继续回退 VM。`CostModelVmFallbacks` 是因有效已编译路径预测成本高于 VM 而选择的 `Fallbacks` 子集；`NativeTypedLoopExecutions` 统计内核调用，包括报告源码可见运行时错误后结束的调用，不是成功计数。必须分别检查 `VeroScriptJitBackendCapabilities.NativeCode` 与 `VeroScriptJitBackendCapabilities.NativeTypedLoops`，两者都不能由 `HostPlatform` 推断。`VeroScriptJitCacheStats` 的生命周期不同：它描述累计、稳定的可复用上下文，并在策略、限制、后端、模块或相关宿主可观测状态变化时重置。

参阅[C# 会话与运行时 — JIT 显式选择](../../User/zh-CN/session.md#jit-backend-capabilities)。

参阅[C# 会话与运行时 — JIT 显式选择](../../User/zh-CN/session.md#diagnostic-translation)。

## 保留调用句柄的释放

参阅[C# 会话与运行时 — 保留调用句柄的释放](../../User/zh-CN/session.md#callable-disposal)。

<a id="managed-marshalling-maintenance"></a>

原生调用保持值和包的托管所有者存活，直到执行及原生视图复制完成。阻塞通道操作与 map/set 访问器保持接收对象存活；参数或元素列表保持索引器返回的每个值包装器存活，直到原生使用结束，包含按需生成的值。这可防止 GC 提前终结对象，显式释放和回调有效范围的规则仍然适用。回归检查在原生接收、访问器及按需列表编组期间强制 GC，并覆盖优化后的 .NET 8/9 执行与 NativeAOT 包消费者。

原生参数与元素编组保存实际提供句柄的值包装器引用快照。列表索引器或托管回调替换输入数组元素时，原始包装器仍存活至原生使用结束，`ref`/`out` 回写仍更新原始包装器。回归测试在 map 构造、直接函数调用及保留 callable 调用中替换数组元素后强制 GC。

[Android 与 iOS / MAUI](../../User/zh-CN/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

移动打包仍使用同一项目和包标识：`VeroScriptIncludeMobile=true` 将桌面 .NET 8/9 与 .NET 10 Android/iOS 目标及已验证的原生 SDK 一起打包。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/runtime-and-interop.md) · [fr-FR](../fr-FR/runtime-and-interop.md) · [ja-JP](../ja-JP/runtime-and-interop.md) · [ko-KR](../ko-KR/runtime-and-interop.md) · [zh-CN](runtime-and-interop.md) · [zh-TW](../zh-TW/runtime-and-interop.md)
