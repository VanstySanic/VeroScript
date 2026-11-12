# C++ 嵌入与会话

## 无状态引擎

`VeroScriptEngine` 为不需要持久状态的调用方提供 `CheckSource`、`CheckFile`、`RunSource` 和 `RunFile`。文件操作可使用字节码缓存目录；源标签、输入/输出/错误流、循环导入策略和项目范围访问策略均为显式参数。结果包含成败和结构化诊断，写入错误流的一行文字不能替代结果对象。

## 有状态会话

`VeroScriptSession` 拥有加载的模块、宿主注册、输入/输出桥、运行时配置和可选热重载状态。正常流程是：

1. 使用 `IEngineHost` 构造会话。
2. 应用配置，注册原生函数或宿主类型。
3. 加载源、文件或已验证包，或编译并提交模块。
4. 运行、调用函数/方法、检查值或枚举元数据。
5. 仅在状态前提允许时清理或重载。

运行期间拒绝配置、加载、编译、提交、重载、清理和绑定变更。`LoadFile` 记录文件重载路径，`LoadSource` 不会；重载在提交前验证替换模块和包种类。

## 求值与值

`Run(ERunMode::Eval)` 执行模块初始化而不调用 `main`；跨调用的全局状态仅在启用 `SetHotReloadEnabled(true)` 时保留。函数/方法调用可返回 `FValue`、构造对象、访问字段/全局并保留可调用对象。保留可调用对象需显式释放，并可配置为仅所有者线程调用。外部序列化器按已注册运行时类型和编码版本工作；序列化、反序列化及引用枚举是独立操作。

## 宿主绑定与元数据

可注册原生函数、宿主类型、泛型宿主类型模板、成员、操作符、clone 回调、可继承类型、作用域 enum 和子类工厂。注册错误通过稳定诊断 ID 返回。反射枚举已加载类型、字段、方法、函数、全局、enum 和 interface，且仅为只读元数据访问。

## JIT 与清理

<a id="session-jit-cache"></a>

重用要求已选择工厂、有效策略不是 VM，并启用 `FJitPolicy::bReuseSessionCode`。符合条件的 `Run`、`CallFunction`、`CallMethod` 和保留 callable 调用共享同代缓存及编译配额。`MaxCompileAttempts` 或 `MaxCachedEntries` 为零表示无限制，不表示禁用。持久执行和稳定回调可以重用代码；原生目录必须通过 `IHostBindingCatalog::GetRevision()` 发布运行时修订。后端具备所需治理能力时，受控原生执行也可以重用代码；Session 会跨 VM 保留该能力上下文。配置、策略、后端、模块或绑定变化会使保留代码失效；重载失败保留原程序。默认上限为 8 MiB 可执行代码、32 MiB 保留承载。`GetJitCacheStats` 是已稳定的同代计量，`GetLastJitStats` 描述最近调用，不是进程内存测量。

## 执行治理

`SetExecutionLimits` 接受指令和墙钟限制，零表示对应限制无限。`SetExecutionControl` 安装共享线程安全取消对象，运行期间可调用 `RequestCancel`。取消是协作式的，既不会强制终止原生回调，也不会让未同步的宿主状态变得安全。

`SetCapabilities` 控制文件、环境、控制台和任务等标准库副作用，Core 在解释及受支持的原生路径中实施这些权限检查。将 `bAllowProcessDirectoryChange` 设为 `false` 可单独禁止 `chdir` 修改宿主进程工作目录；`chdir` 同时需要环境写入权限。`SetExecutionBackend` 选择 `Vm`、`Mixed` 或 `Jit`。受控 `Mixed` 默认使用 VM；可通过 `SetJitPolicy` 设置 `FJitPolicy::bEnableControlledMixed=true` 启用动态原生执行。显式 `Jit` 和在 `Mixed` 中选择的静态 Core AOT 不受该开关限制。缺少所需治理能力时，在进入原生代码前回退 VM；调试、保留初始化区间和有界清理使用 VM。

## 调用、拦截与持久化

公共调用面包括 `HasFreeFunction`、`HasInstanceMethod`、`CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`ConstructObject`、`RetainCallable`、`InvokeCallable`、字段/全局访问、`ApplyClassDefaults` 和反射枚举。绕过方法只跳过所选入口拦截器，方法体的嵌套调用仍按正常规则拦截。保留可调用对象的所有权、所有者线程模式、弱 `this` 捕获及模块代次失效仍是显式生命周期规则。

`RegisterExternalSerializer`、`SerializeObject`、`DeserializeObject` 和 `EnumerateObjectReferences` 遵循 Interop 序列化与借用引用契约。`Reload`、`ReloadCompiledModule` 与 `ReloadPackage` 事务性地暂存并验证替换物；初始化失败保留最后可工作的模块并报告结构化诊断。

## 保留字节配额

`MaxNativeBytes` 或 `MaxRetainedPayloadBytes` 为零不允许保留对应字节；与表示无限制的零策略预算不同。

## 取消与宿主投递

<a id="session-control-and-streams"></a>

`RequestCancel()` 原子获取当前控制对象的共享快照，所有者在空闲时替换控制对象也安全。空指针时不执行操作，取消不会传递给另一个控制对象。`GetExecutionControl()` 仅提供所有者线程借用引用。销毁 Session 前须等待取消调用和执行结束。宿主行视图仅在回调期间有效；输出回调抛出异常会使流报告失败，复用或销毁时不重放已尝试投递的事件。`ResetForReuse` 恢复流状态，析构抑制回调异常。

共享同一个 `FExecutionControl` 令牌的 Session 会共享取消状态。调用 `Reset()` 并重用令牌前，应等待之前的取消调用结束，避免旧请求取消下一次使用该令牌的执行。

## 实现归属

<a id="session-debug-implementation"></a>

`VeroScriptSessionDebug.h` 定义 Session 调试桥和借用视图，当前实现位于 `VeroScriptSession.cpp`。`VeroScriptLiveDebugDomain.h` 定义由宿主持有的实时调试域契约，`VeroScriptDapAdapter.cpp` 使用此接口。同名 `.cpp` 入口当前仅包含头文件，不能根据文件名推断实现归属。维护时必须遵守上述控制线程与生命周期契约。

## 可选 JIT 诊断

`FJitPolicy::Diagnostics.MaxFunctions` 默认为零，不收集逐函数诊断。需要诊断时，在现有策略的副本上设置记录数、深度上限和可选的 `bMeasureTime`，再调用 `SetJitPolicy`。

`GetLastJitDiagnostics()` 返回最近一次执行的自有快照，在临时 VM 销毁后仍然有效；需要跨后续调用保留时请复制。`Run`、调试执行、函数／方法调用、构造及保留 callable 调用均使用相同入口收集。关闭收集后的下一次执行或 `Clear()` 会清空快照。记录数达到上限时查询 `DroppedRecords`；关闭计时时，时间字段为零。

`GetLastJitStats()` 的 `NativeLeafExecutions`、`NativeCfgExecutions`、`BridgeExecutions`、`LinearExecutions` 和 `VmTrampolineExecutions` 统计实际进入的执行路径。编译次数不代表执行次数；scalar／direct-call 和 typed-loop 计数可能是一次函数调用内的嵌套事件，不能与函数入口计数相加。诊断的 inclusive 时间也不能跨函数直接求和。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](session.md) · [zh-TW](../zh-TW/session.md)
