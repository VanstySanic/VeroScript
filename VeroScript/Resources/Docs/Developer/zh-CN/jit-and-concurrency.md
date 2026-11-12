# 核心 JIT 服务与并发

[可替换 VM 后端](vm-backends.md)

<a id="native-abi-version"></a>

`VEROSCRIPT_VERSION_CODE` 按 major:8／minor:8／patch:16 位编码产品版本 `VEROSCRIPT_VERSION`。JIT 模块握手及 JIT／AOT 的 `CoreVersion` 统一使用该值，C AOT 描述字段为 `core_version`；没有独立的 JIT 契约或 API 版本计数。二进制兼容性仍检查公开头指纹、描述结构布局、工具链和平台，AOT 另外检查 Core 实现身份。静态 C++ 消费方及后端二进制须使用匹配的头文件和库重建；包自身的发行版本仍是独立的包信息。

## 所有权与选择

参阅[可替换 VM 后端](vm-backends.md#vm-provider-assembly)。

[可选 AsmJit 模块](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/zh-CN/index.md)拥有 IR、lowering、原生发码、可移植 Linear 执行、ABI 适配、直接调用目标和执行存储。其私有头与架构宏不属于核心 SDK。核心能力字符串为 `backend-services`；实际目标和原生能力查询所选工厂的描述。

## 动态后端与安装

核心提供 `LoadJitBackend(path, error)`、VM/Session 的 `SetJitBackendPath` 和模块 ABI 校验。CLI 使用 `--jit-backend=<文件或目录>`；C、C#、Python 接口向核心转交路径。默认不查找后端、不读取环境变量或 JSON；Mixed 使用 VM，强制 Jit 在未选择后端时返回错误。执行治理依据后端能力选择经过验证的受控 JIT 或 VM；调试及不支持的路径仍使用 VM。

指定目录按 UTF-8 路径顺序检查根目录及两层子目录，跳过不兼容候选，选首个可用后端。文件路径固定具体版本。切换失败保留原选择，工厂、worker、实例和执行产物均保有动态库生命期。Windows 使用 `.dll`，Linux 使用 `.so`，macOS 使用 `.dylib`；需匹配公共头、工具链、C++ ABI、架构及运行库配置。

安装根可选任意盘符或位置：`Runtime/<版本>/`、`SDK/<产品>/<版本>/`、`Jit/<后端>/<版本>/`。当前各版本为 **0.1.1**。仅 Runtime 可选加入 PATH；多个目录可共存，命令命中顺序由 PATH 顺序决定。SDK 通过构建配置定位，JIT 通过明确路径定位。嵌入宿主可将后端随应用放在任意目录，并计算绝对路径传入。

## 编译与调用协议

`EJitCompilationResult` 区分 `Ready`、`Unsupported`、`Deferred` 和 `Failed`。不支持和资源暂缓可回退 VM；编译失败报错。产物经过拥有权、服务与资源校验后才能发布。程序身份包含模块代际及原生绑定代际。

`EJitInvocationResult` 区分 `NotEntered`、`Returned`、`ControlTransfer` 和 `Failed`。仅 `NotEntered` 允许解释器从头执行。进入后的错误、throw、exit 和取消直接传播，不重复副作用。核心检查返回值。执行产物保有代码与模块拥有者；调用上下文、帧令牌和数据视图均为借用。嵌套调用或宿主重入之后须重新获取对象与容器存储。

公共服务头为 `VeroScriptJitRuntimeServices.h`、`VeroScriptJitFrameServices.h`、`VeroScriptJitValueServices.h`、`VeroScriptJitCallServices.h` 和 `VeroScriptJitScalarServices.h`。复制、引用、const 字段、方法分派、同步变更通知、错误和逻辑调用深度沿用核心语义。

`SetJitProgramOwner` 提供的拥有者保活的是不可变模块代际，不得经其他可写别名修改其内容；模块在空闲边界替换。裸 `VeroScriptVm` 执行不会自动推进配置的程序代际或更换拥有者。若程序更换后仍保留值，宿主须更新该身份，并保留定义模块至清理或受支持的迁移完成。Session 的替换与热重载路径会管理这些身份。

## 策略、统计与 Session 复用

`FJitPolicy` 控制热度、编译次数、缓存决定及预编译。策略预算为零表示不限。`Vm` 不进入后端；`Mixed` 等待热度；`Jit` 在策略范围内优先编译。执行治理依据后端能力选择经过验证的受控 JIT 或 VM；调试及不支持的路径仍使用 VM。

`bReuseSessionCode` 默认开启。符合条件的 `Run`、`CallFunction`、`CallMethod` 和保留 callable 调用租借同代缓存，并共享累计编译额度；预算不限也可复用。复用要求已注入工厂且有效模式不是 VM；持久执行与稳定回调可以复用；原生目录通过 `IHostBindingCatalog::GetRevision()` 发布运行时修订。策略、配置、Reload 和 Clear 使保留状态失效；失败 Reload 保留原程序。

`FSessionJitCacheLimits` 默认限制可执行存储 8 MiB、保留载荷 32 MiB；零表示不允许保留该类资源。执行 scratch 计入保留载荷。资源拒绝可回退，但不退还已消耗的编译次数。`FSessionJitCacheStats` 报告已结算的代际记账；`FJitStats` 是最近一次执行快照，后端路径计数代表实际运行。`NativeTypedLoopExecutions`、`DirectNativeCalls` 和 `CostModelVmFallbacks` 分开统计；成本回退不属于执行失败。

## 并发

核心负责任务创建、等待、超时、协作取消、channel、mutex、semaphore 和同步值。阻塞 channel 操作观察关闭状态。worker 共享不可变工厂及有拥有者的程序，实例、统计、执行存储和错误各自独立；任务结束释放语言根。

## 验证

改动须通过契约、后端差分、生命周期、调试和并发回归。分别构建模块开启、模块关闭及仅关闭原生发码三种配置，并在不含后端目录的源码副本中构建核心和 C++ 消费者。保留原有性能门槛，验证 SDK 显式注入。

<a id="construction-dispatch"></a>

构造或析构期间，虚调用目标必须由 Core 按当前生命周期阶段解析。`bConstructionIncomplete`、`bDestroying` 表示需要此检查；`GetTypeName` 和 `RuntimeTypeIndex` 仍表示对象的实际类型与字段布局。优化路径若无法保持阶段分派，必须在产生副作用前退回，不能复用普通的实际类型虚调用缓存。显式静态绑定的非虚调用，以及由 Core 已选定目标的原生构造入口，保留原有路径。

## 执行诊断

`FJitStats` 的 `NativeLeafEntries` / `NativeCfgEntries` 是编译产物数，不能用来判断是否运行过机器码。`NativeLeafExecutions` / `NativeCfgExecutions` 在实际入口累计；`ControlledNativeExecutions`、`NativeTypedLoopExecutions` 和 `DirectNativeCalls` 分别统计受控入口、循环和嵌套直接调用。`ScalarNativeFastPathCalls` 仅指宿主标量原生调用快路。Linear、Bridge、VM trampoline 的入口另有计数。这些事件存在包含关系，不能直接相加当成脚本调用总数。

需要逐函数归因时，在执行前配置：

```cpp
VeroScript::FJitPolicy policy;
policy.Diagnostics.MaxFunctions = 128;
policy.Diagnostics.MaxDepth = 256;
policy.Diagnostics.bMeasureTime = true;
session.SetJitPolicy(policy);
// Read session.GetLastJitDiagnostics() after Run / CallFunction.
```

裸 VM 使用 `GetJitDiagnostics()`。快照包含同代函数索引、名称、模块、实际执行类别、调用及结果计数、回退原因分布，以及可选的 inclusive / self 纳秒耗时。记录的是 Core 调用边界；内联操作和未经过 Core 的直接原生递归不会产生额外逐函数记录。inclusive 时间包含已记录子调用，不能跨函数求和；self 时间仅扣除已记录的子调用。快照在最外层执行入口重置，重入及清理归入当前执行。`ResetJitStats()` 同时清空快照。

`PlanReason` / `PlannedKind` 在读取快照时查询当前缓存计划；`Fallbacks` 保留本轮执行中实际发生的历史回退。因此预热后已进入原生代码的函数仍可能有早期热度回退计数。

`MaxFunctions=0` 默认关闭：不读取时钟、不分配记录、不输出日志。函数上限为 4096、栈深上限为 32768，每个名称／原因最多保留 4096 字节；超过限制由 `DroppedRecords` 表示，此时数据为局部统计。只需分类时关闭 `bMeasureTime`，避免计时成本。性能对比应在诊断关闭的情况下计时，再单独运行诊断。

开启详细诊断时，官方 VM 复用带观测分派，可能减少指令融合并显露更多 Core 调用边界；时间与边界数不能当作关闭诊断时的逐项成本。无需开启详细诊断即可读取实际原生入口等聚合计数。

`FJitBackendCache::GetFunctionReason()` 和 `GetFunctionFallbackReason()` 同时支持编译决定及最近的运行时入口拒绝。成功生成的 VM 计划也保留原因。异常清理、引用参数、所有权、闭包、治理／观察条件、热度和预算，与真正的代价模型回退分开统计；未能精确分类的运行时限制使用 `RuntimeGuard`。`CostModelVmFallbacks` 不再包含必须交给 VM 的 finally／ref 限制。它们仍是合法回退，不会为增加原生计数而强制执行较慢或不完整的计划。

带 finally 的函数继续使用 Core 的展开与有界清理；不改变 getter 求值次数、顺序或显式绑定的共享状态。短整数表达式允许单个无变更来源的可写引用捕获使用有界标量计划；多个可能别名的捕获、只读包装、观察器、治理、重入敏感情况及失败探测保留普通 VM。函数／模块／代际不匹配时，复用调用帧的成员缓存失效。

新增统计已映射到 C Interop 以及 C++、C#、Python 的现有统计入口。详细快照由 Core / C++ 提供。公开布局及 VM 服务有扩展，须重建匹配的 Core、VM、JIT、Interop 和原生消费方。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/jit-and-concurrency.md) · [fr-FR](../fr-FR/jit-and-concurrency.md) · [ja-JP](../ja-JP/jit-and-concurrency.md) · [ko-KR](../ko-KR/jit-and-concurrency.md) · [zh-CN](jit-and-concurrency.md) · [zh-TW](../zh-TW/jit-and-concurrency.md)
