# 核心执行、状态与包

[可替换 VM 后端](vm-backends.md)

## VM 状态

参阅[可替换 VM 后端](vm-backends.md#vm-provider-assembly)。

反射及源码描述对象通过 `FScriptObject::DescriptorData` 持有不可变的 `FScriptObjectDescriptorData`；普通对象不分配描述块。原生消费方通过 `GetDescriptorData()` 读取描述身份，空描述返回共享只读默认值。克隆共享不可变描述，普通字段与生命周期状态仍按原规则复制；更新描述须完整构建新块后替换，不能修改已发布的共享块。此调整改变了 C++ 对象布局，Core、后端和使用公共值头的消费方必须同源重建，不能只凭 0.1.1 版本字符串混用产物。

## 字节码元数据与控制流

字节码携带保持源代码和包行为所需的元数据：引用参数标志、闭包捕获名称与类型、仅声明或 abstract 函数状态、interface 与 enum 表、调试文件所有权、异常区域、`finally` 范围、全局初始化器范围以及包组件所有权。引用 opcode、已检查和可选 cast、`throw` 及受保护区域清理都是语义操作，不能作为可互换的 VM 优化对待。序列化、热重载、反射和工具链必须使关联元数据与可执行表保持对齐。

## Worker 与同步

脚本全局变量和类静态字段按原生 worker 隔离。worker 任务注册表及其任务 ID 也是 worker 本地的。`channel<T>` 是明确的、带同步的跨 worker 能力；宿主回调和可达原生状态需要宿主提供同步。worker 不能把另一 worker 的任务 ID 当作本地 ID 使用。

## 包与重载

包加载会验证模块元数据以及 application/library 入口形状。会话重载必须显式启用，并在提交前暂存替换模块。基于文件、已编译模块和包的重载具有不同输入。重载失败时必须保留原先可工作的模块，并公开结构化诊断。

## 调试与诊断

调试位置、栈帧、变量、断点、求值和停止原因，是 Tooling 与 DAP 消费的公共数据契约。诊断 ID、源范围和替换参数是稳定的机器数据；本地化消息仅承担展示职责。

## 执行治理

`FExecutionLimits` 包含指令上限和墙钟超时；某个字段为零表示该字段不限。`FExecutionControl` 是线程安全的共享控制对象。`Run` 活跃时宿主可调用 `RequestCancel`，VM 会在协作式执行点观察该请求。取消绝不会强制停止已经运行中的原生或托管回调，因此宿主回调状态仍需遵循正常的同步规则。

`FScriptCapabilities` 是标准库副作用策略，可限制文件、环境、控制台和任务操作。默认配置是宽松的；宿主提供限制后，执行路径必须在任何 JIT 入口运行前实施限制。这是安全策略，而不仅是优化提示。`FEngineConfig` 会将这些限制、控制对象和能力策略，与源覆盖层及请求的 `EExecutionBackend` 一并携带。

执行限制、活跃取消控制和受限能力在必要时使用解释器路径以保证强制实施。调试器附加、保留初始化区间和有界清理会使有效后端成为 `Vm`；请求后端仍只是配置值，诊断中不得把它报告为有效后端。

## 重入清理与异常转移

值释放可能调用脚本析构函数，重入执行并使调用帧存储重新分配。跨越这些调用时保存帧索引，返回后重新取得帧引用。字节码读取操作数必须保持在当前帧的 `StackBase` 之上；跨帧展开清理则显式释放保存的操作数。

待处理的返回值或异常属于外围 `finally` 续体。`finally` 内的循环和局部捕获的异常保留该转移；离开区域的转移通过正常所有权路径释放被取代的值。清理中的未捕获异常可以替换正在传播的脚本异常，而析构函数内部已捕获的异常必须保留外层异常。执行失败和 VM 复用必须释放待处理异常对象。使用 `VeroScriptExceptionTests` 检查这些场景，包括调试器和指令上限模式；受治理的执行仍须独立计数和检查 `throw`。

## 数值服务

`VeroScriptNumerics.h` 提供不可变 `FBigInteger`／`FRational` 值及事务性的 noexcept `Numerics::Try*` 运算。`FNumericError` 使用固定元数据，分配失败不会再分配错误字符串。复制、移动、交换、析构不分配，移动后的源值为零。不可变值可在宿主线程间共享；各次运算独立使用上下文，工作回调由宿主同步。

`FNumericOperationContext` 包含 `FNumericLimits` 和同步借用的工作回调。零限制选用有限默认值：值位数 65,536（硬上限 1,048,576）、输入／输出各 1 MiB、scratch 16 MiB、每次运算工作量 16,777,216。scratch 计量原生临时及待提交输出的请求存储，不是 VM 堆或进程 RSS。失败保留输出。

通过 `VeroScriptVm::SetNumericLimits`、`FEngineConfig::NumericLimits` 或 `VeroScriptSession::SetNumericLimits` 在执行前配置。worker 继承限制；数值工作计入活动指令、超时与取消控制，全局初始化同样受控。`VeroScriptNumericBindings.h` 提供经过来源验证的 FValue 交换及原生调用服务。C Interop 提供 `VS_INTEROP_CAP_NUMERICS`、`vs_interop_numeric_parse/to_text/convert`、类型识别与会话限制设置，字符串保留显式长度。各子语言的 BigInteger／Fraction 不自动映射。独立 AOT 明确拒绝数值运行时操作。

安装钩子在初始化前通过 `Packages::FLimits::HookNumericLimits` 配置相同服务。包格式、注册表协议与 JSON 数字规则保持不变。

```cpp
#include "VeroScriptNumerics.h"

VeroScript::FNumericOperationContext context;
VeroScript::FNumericError error;
VeroScript::FBigInteger value;
bool parsed = VeroScript::Numerics::TryParse("18446744073709551616", value, context, error);
```

## variant / FValueRoot

参阅[Interop SDK — variant](../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#exported-value-lifetime)。

装箱前由 VM 验证 `FVariantTypeDescriptor`。`Variant::Box` 和 `Variant::Snapshot` 复用普通复制服务，不公开可写载荷。`VisitValueGraph` 遍历盒、对象、闭包及容器；退休不会在终结线程执行脚本。回调或复制载荷前预留结果槽，随后无分配提交；外层活动在执行结束后清理退休值。回调请求关闭会话时，当前活动调用结束后才完成销毁。

VM 完整支持语义。Mixed／asmjit 对不支持的 variant 指令安全回退 VM。可移植字节码和包保留声明及指令。独立 C++、C#、Python AOT 在生成输出前明确拒绝 variant 依赖。调试器提供只读 $present、$type、$value 子项；type<variant>().kind() 返回 "variant"。
调用安全点在执行期间保活借用参数和接收对象；退出时保活返回值、构造结果及可写参数结果，直到调用方能够登记根，纯输入借用随调用结束。退休值清理失败会使原本成功的调用报告运行时失败；已有执行错误仍优先保留。关闭时的清理失败通过宿主诊断流报告。

## 原生异步回调根

合法 native 调用可通过 `IScriptCallInvoker::RetainForGeneration` 保活函数和捕获值。只保存返回的 `FValueRoot`，不保存借用的 invoker、VM 或 Args。根可在其他线程退休，读取和语言清理仍归创建线程；普通导出根与完成根共同保护存活图。

完成根在 native 调用安全点清理，`DrainGeneration` 不消费普通宿主借用根。析构工作累计计入当前执行预算，并沿用取消与绝对期限。持久 Session 保留同代次根；裸 VM 顶层执行结束、程序替换、重载或关闭使其失效。普通导出根继续阻止程序替换；编译失败不使旧代次失效，进入迁移后的失效不随回滚恢复。

## 准备缓存与 JIT 执行服务

`VeroScriptProgramPreparation::TryGetCacheInputFingerprint` 只对无需处理器执行、包操作重放或宿主绑定校验的已解析输入开放准备前缓存查询。必须同时使用加载器完整的源码与解析路径指纹、编译器构建身份；不满足条件时仍执行正常准备及准备结果的依赖校验。源码元数据和实际生效的常量求值限额也参与输入身份。

`ICompiledFunctionServices::ExecuteBlock` 将有界指令序列的原生／Core 调用边界合并。每条指令仍正常检查和计入限额、期限、取消及权限；分支、返回或失败立即结束序列。受控 Mixed 的默认策略保持原状。

借用、不保留及隔离工作任务的 JIT 缓存先准备入口，到达的被调用函数仍可按需编译。保留的缓存只在执行要求相同时复用已完成的准备。未支持或收益不足的路径继续使用 VM。

这些 C++ 服务变更要求 Core、后端及 SDK 消费方使用匹配源码重建。

整模块入口仍遵循配置的预编译预算。

`FJitCompiledFunction::bPreferVmForExecution` 允许后端发布不依赖实参的 VM 成本选择。Core 在激活后端服务前采用缓存中的选择，保留回退统计、正常 VM 检查和被调用函数的独立分派。

## 执行中断后的有界清理

参阅[宿主集成 — 执行中断后的有界清理](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md#bounded-interruption-cleanup)。

## 类型兼容性缓存所有权

`VeroScriptVm::IsActiveObjectTypeAssignableTo` 按模块与 `ProgramGeneration` 缓存准确规范类名／接口名的兼容性。缓存归各执行上下文所有，模块／代次变化或达到 4096 项上限时清空。自有不可变程序允许在活动调用外复用；借用程序仅在执行期间缓存，并由 `BeginExecutionGovernance` 在调用之间清空。别名与非规范名称仍走权威元数据解析。优化 JIT 调用、值复制或宿主查询时，须保留这些所有权及失效条件。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/execution-and-state.md) · [fr-FR](../fr-FR/execution-and-state.md) · [ja-JP](../ja-JP/execution-and-state.md) · [ko-KR](../ko-KR/execution-and-state.md) · [zh-CN](execution-and-state.md) · [zh-TW](../zh-TW/execution-and-state.md)
