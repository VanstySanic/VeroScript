# 宿主集成

VeroScript 可以嵌入 C++ 宿主程序。宿主通常通过 `VeroScriptEngine` 使用一次性的检查或运行入口，通过 `VeroScriptSession` 管理长期存在的模块、绑定、运行状态和热重载。

## 显式选择 VM

Core 不提供隐式解释器。以下示例使用独立分发的静态字节码 VM：查找两个 SDK 包、链接两个目标，并为每份引擎配置或 Session 显式装配。仅链接库不会自动选择后端。

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
```

动态加载时，使用 `FEngineConfig::VmBackendPath` 或 `Session::SetVmBackendPath` 指定已部署的 VM 文件或目录；工厂和路径互斥。开始执行后选择即固定，`Clear()` 不解除固定。除非 `ScriptCompileExtensions` 单独指定工厂或路径，编译脚本会继承所选 VM。不执行编译脚本的普通编译不需要 VM。

对于已有 Session，`FEngineConfig` 未指定 VM 工厂和路径时保留当前选择；冻结前可用 `bClearVmBackend=true` 显式清除。工厂、路径和清除三者互斥。`Clear()` 重置已加载程序，不会解除执行后 VM 域的冻结；此后需要更换解释器时创建新 Session。

## 高层引擎入口

宿主需要包含 SDK 提供的 `VeroScriptEngine.h` 公共头文件。`VeroScriptEngine` 提供源代码和文件两类入口：

- `CheckSource` / `CheckFile`：只进行词法、语法和语义检查；
- `RunSource` / `RunFile`：加载并执行脚本。

这些入口可以接收导入路径、脚本参数、宏、标准库搜索路径、字节码缓存目录、执行后端和绑定目录等配置。

`FEngineConfig` 的公开配置还包括结构化预定义宏、隐式导入路径、内存源覆盖、最大调用深度、循环导入开关、项目范围访问开关和输入流。嵌入式 `RunFile` 默认不启用字节码缓存；需要缓存时应显式设置 `bUseBytecodeCache` 与 `BytecodeCacheDirectory`。执行后端是 `Vm`、`Mixed` 或 `Jit`，默认是 `Mixed`。

字节码缓存包含 Core 实现与公共 API 的身份标识。即使产品和字节码版本不变，Core 代码变化也会使旧缓存失效；同一构建的重复运行仍会复用缓存。

`ExecutionLimits.MaxInstructions` 和 `ExecutionLimits.TimeoutMilliseconds` 是单次执行的协作式限制；`0` 表示不限制。`ExecutionControl` 接受共享的 `FExecutionControl`，宿主可以从其他线程调用 `RequestCancel()` 请求取消。Core 对解释执行和受支持的原生执行实施一致的指令、取消及权限检查。受控 `Mixed` 默认使用 VM；Session 可通过 `SetJitPolicy` 设置 `FJitPolicy::bEnableControlledMixed=true`，启用动态原生执行。显式 `Jit` 和已选静态 Core AOT 不受该开关限制；原生代码缺少所需治理能力时，会在进入前回退 VM。已经运行的原生回调不会被强行终止。控制令牌不会自动重置；再次运行前应调用 `Reset()`。

`Capabilities` 控制标准库副作用，包含文件读/写/删除、环境读/写、控制台输入/输出和创建任务。默认允许全部能力；限制只作用于内置函数，注册回调中的授权仍由宿主负责。
`bAllowProcessDirectoryChange` 可单独禁止 `chdir` 改动整个宿主进程的当前目录；`chdir` 同时要求环境写入权限。

入口返回 `FRunResult`。`bSuccess` 表示编译/执行是否正常完成；成功的 `int main` 或 `exit(n)` 可以产生非零 `ExitCode`。`bExitRequested` 只表示脚本显式调用了 `exit(n)`，包括 `exit(0)`；普通的 `int main` 返回不会设置该标志。失败原因可从 `Error` 和 `Diagnostics` 读取。

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptEngine.h"

#include <iostream>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	config.ScriptArgs = {"alpha", "beta"};

	const VeroScript::FRunResult result = VeroScript::VeroScriptEngine::RunFile(
		"main.vs",
		host,
		config);

	if (!result.bSuccess)
	{
		return result.ExitCode;
	}
	return result.ExitCode;
}
```

`ERunMode` 与执行后端相互独立：`Eval` 使用增量/REPL 初始化路径，`EvalOnce` 对源执行一次性评估，`Script` 在加载后继续调用 `main`。调试、保留初始化区间和有界清理使用 VM；持久会话的普通函数可以使用所选 JIT。

## 嵌入式 VM 调试器

`VeroScriptDebugger` 是直接拥有 `VeroScriptVm` 的宿主可使用的同步进程内调试器。它与 DAP
分开：不会建立线连接，也不会编码 JSON。请包含 `VeroScriptCompile.h`、
`VeroScriptDebugger.h` 和 `VeroScriptVm.h`，将源代码或文件编译为 `FBytecodeModule`，再通过
`SetDebugger` 连接一个非拥有式调试器并执行模块。

使用字节码解释器时，请包含 `VeroScriptBytecodeVmAssembly.h`，并在执行前调用
`VeroScript::BytecodeVm::Configure(vm)`。

`SetBreakpoint` 使用从 1 开始的源代码行号。空文件标签匹配所有文件；非空标签匹配完整标签
或文件名。条件断点会在顶层栈帧中计算表达式。表达式计算失败或条件为 false 时跳过暂停；
布尔真值、非零数字和其他非空结果会被视为 true。

暂停回调会在 VM 暂停期间同步执行。使用 `GetStackTrace`、`GetLocals` 和 `GetChildren` 检查
状态。`Evaluate` 会在选定栈帧中以只读方式计算表达式；要通过调试变量写入值，请使用
`SetLocal` 或 `SetChild`。在回调中调用 `SetRunMode`，可以选择 `Continue`、`StepIn`、
`StepOver` 或 `StepOut`。

`RequestPause()` 是线程安全的协作式请求，会在 VM 安全点被观察，并以
`EDebugStopReason::Pause` 报告；它不会强行中断正在运行的宿主回调。如果之前的暂停请求可能
仍然存在，在复用调试器前调用 `ResetPauseRequest()`。`SetDebugger` 不转移所有权：VM 执行
期间必须保持调试器有效，使用调试器的回调期间也必须保持 VM 有效。连接调试器会强制有效
执行后端为 `Vm`，并使 JIT 缓存失效。

需要异步编辑器集成时，请使用 [Tooling](../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/index.md) 和 [调试](../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/debugging.md)
中介绍的 `VeroScriptDebugSession` 或 DAP 适配器。

## 调试已有 Session

[调试已有 Session](session-debugging.md). `SetDebugBridge`, `ISessionDebugBridge`, `SetDebugBridge(nullptr)`.

## 诊断本地化

引擎入口会自动初始化核心本地化：优先使用 `VERO_LOCALIZATION_DIR`，随后按照[诊断本地化](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md)中的部署搜索顺序寻找目录，并使用 `VERO_LANG` 或系统语言选择文化。宿主不需要为了显示英文诊断额外调用初始化函数。

需要显式控制语言或目录时，可以直接使用 `VeroScript::Locale`：

```cpp
#include "VeroScriptLocale.h"

#include <iostream>
#include <string>

int main()
{
	std::string error;
	if (!VeroScript::Locale::LoadCatalogDirectory("localization", &error))
	{
		std::cerr << error << '\n';
		return 1;
	}

	if (!VeroScript::Locale::TrySetLanguageFromTag("zh-CN"))
	{
		return 2;
	}

	const std::string language = VeroScript::Locale::GetLanguageTag();
	std::cout << language << '\n';
	return 0;
}
```

`LoadCatalogDirectory` 会替换进程级目录树；库或标准库可以使用 `MergeCatalogDirectory` 追加同级 `localization/` 目录，默认不覆盖已经存在的消息 ID。语言和目录操作在核心中是线程安全的，但 `VeroScriptSession` 本身仍不是线程安全对象；宿主应明确语言切换的时机，避免不同请求共享一个进程时互相改写显示语言。消息 ID、占位符和英文回退规则见[诊断本地化](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md)。

## 长期会话

`VeroScriptSession` 适合编辑器、游戏或宿主工具中的长期脚本生命周期。会话可以：

- 加载、运行、重新加载和清除模块；
- 注册原生函数、宿主类型、字段、属性、方法、运算符和泛型宿主类型；
- 设置输出、诊断、调用拦截和状态变更回调；
- 选择 VM、混合或 JIT 后端并配置字节码缓存；
- 设置协作式指令/时间限制、取消和标准库能力；
- 显式启用状态保留式热重载。

会话调用本身不是内部同步的。宿主应使用一个控制线程调用 `Load`、`Run`、`Reload` 和 `Clear`，并在运行不活跃时请求重载。

典型的长期会话顺序如下：

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <string>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::VeroScriptSession session(host);

	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	std::string configError;
	if (!session.ApplyConfig(config, &configError))
	{
		std::cerr << configError << '\n';
		return 1;
	}

	const VeroScript::FRunResult load = session.LoadFile("main.vs");
	if (!load.bSuccess)
	{
		return load.ExitCode;
	}

	const VeroScript::FRunResult run = session.Run();
	return run.ExitCode;
}
```

<a id="apply-config-result"></a>

`ApplyConfig` 拒绝配置时返回 `false`，可通过 `OutError` 获取原因。加载或运行前应检查结果。验证拒绝发生在配置提交之前；分配或复制回调产生的异常可能在部分修改后传播，不保证回滚。

`IEngineHost` 必须比会话存活；它接收 `print` 和诊断行回调。`RegisterNative`、`RegisterHostType` 和 `RegisterHostMember` 必须在 `Load` 之前完成，这样编译器才能解析脚本中的宿主声明。注册表在 `Reload` 和 `Clear` 后仍然存在；绑定目录可以通过 `GetBindingCatalog` 读取，或使用 `ExportBindingsToFile` 导出供 IDE、LSP 和 DAP 使用。

会话还提供 `CompileFile`/`CommitCompiledFile`、`ReloadCompiledModule`、`LoadPackage` 和 `ReloadPackage`，适合编辑器、包加载器和热更新系统把“编译”和“提交”分成两个阶段。包的 `CanonicalSignedBytes` 必须在跨越信任边界前由宿主验证签名。

`BuildPackage`／`ReadPackage` 与 Session 的 `LoadPackage`／`ReloadPackage` 处理 Core 映像（`.vslib` 或原始已编译应用映像）。标准 ZIP `.vsapp` 是外层应用容器，应使用 Application 服务及 `LoadApplication`／`ReloadApplication`。将原始 Core 应用映像保存为 `.vsapp` 文件名不会使其成为标准容器。

## 绑定边界

脚本只能调用宿主明确注册并且编译器能够解析的函数或成员。绑定描述需要提供参数、返回值以及宿主类型、容器元素或键值类型信息；注册重复名称、空回调或与内置名称冲突的绑定会被拒绝。

宿主结构体的值复制需要相应的克隆回调；宿主对象句柄则遵循宿主注册的生命周期和所有权约定。绑定 API 的完整声明以 `VeroScriptCpp` 公共头文件为准。

公开的调用和扩展能力还包括：

- `CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`ConstructObject`、`GetField`/`SetField` 和全局量读写；
- 带 `ref` 参数的调用写回可变参数，失败或退出时不写回；
- `RetainCallable`/`InvokeCallable`，用于把脚本函数值保留到当前宿主回调之外；
- `SetInstanceCallInterceptor`、`SetMutationCallback` 和 `ApplyClassDefaults`；
- `RegisterGenericHostTypeTemplate`/`InstantiateGenericHostType`、类型别名、外部序列化和对象引用枚举；
- `SetJitPolicy`, `SetJitCacheLimits`, `GetJitCacheStats`, `GetLastJitStats`: 重用要求已选择工厂、有效策略不是 VM，并启用 `FJitPolicy::bReuseSessionCode`。符合条件的 `Run`、`CallFunction`、`CallMethod` 和保留 callable 调用共享同代缓存及编译配额。`MaxCompileAttempts` 或 `MaxCachedEntries` 为零表示无限制，不表示禁用。持久执行和稳定回调可以重用代码；原生目录必须通过 `IHostBindingCatalog::GetRevision()` 发布运行时修订。后端具备所需治理能力时，受控原生执行也可以重用代码；Session 会跨 VM 保留该能力上下文。配置、策略、后端、模块或绑定变化会使保留代码失效；重载失败保留原程序。默认上限为 8 MiB 可执行代码、32 MiB 保留承载。`GetJitCacheStats` 是已稳定的同代计量，`GetLastJitStats` 描述最近调用，不是进程内存测量。

`VeroScriptSession` 不在内部同步线程访问。普通会话操作和最终销毁应在创建会话的线程执行。宿主回调可能来自 VM 工作线程，须自行同步外部状态。`RetainCallable` 默认使用 `bOwnerThreadOnly = true`；即使宿主已串行化访问，从其他线程调用保留的函数值仍会失败。需要允许跨线程调用时，必须将 `RetainCallable` 的第二个参数显式设为 `false`，并由宿主串行化整个会话的所有访问，包括执行、配置、重载和销毁。

脚本清理也是正在进行的 Session 操作：清空或替换程序、关闭热重载、释放保留的 callable 和销毁 Session 都可能执行脚本析构函数及宿主回调。这些回调不得重入执行或修改同一 Session 的 API；清理期间 `RetainCallable` 也会拒绝增加保留根。后续 Session 操作应安排在清理返回之后；普通执行回调仍可保留 callable。

## 原生 JIT 可观测性

`FJitStats::CostModelVmFallbacks` 是有效编译路径因预测更慢而被有意留在 VM
中的 `Fallbacks` 子集，不是编译失败，也不改变语义。
`FJitStats::NativeTypedLoopExecutions` 统计经原生后端调用的类型化循环内核次数，
包含报告源码可见运行时错误的调用；它不是成功计数器。二者都是最近一次运行的
观测值；零值不能证明某个目标或能力不可用。

<a id="threads-and-hot-reload"></a>

## 线程和热重载

脚本 VM 执行、宿主会话控制和原生回调之间的线程边界必须由宿主负责协调。热重载是显式选择的能力；函数身份、捕获名称及类型兼容时，闭包及其捕获的 `this` 可以迁移。捕获新增、删除、类型变化、函数无法重映射、活动通道和不兼容的类型变化仍会被拒绝。宿主保留的 callable 句柄仍在其注册代次被成功替换后失效；清理会保留仍被有效全局量或值根使用的共享值图。原生副作用不属于脚本状态事务。

启用状态保留式热重载后，兼容的全局量、对象和容器身份可以迁移；新增字段使用语言默认值。重新加载会先编译、验证并迁移，初始化失败时保留上一份可用模块。禁用热重载或调用 `Clear` 会正常释放保留值，包括引用环。热重载会跳过字节码缓存，以保证 `Reload` 从当前源重新编译。

跨语言的稳定 C ABI 见[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md)；浏览器和 WebAssembly 的内存入口见[WebAssembly/WASM SDK](../../../../../../Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md)。

## 保留字节配额

`MaxNativeBytes` 或 `MaxRetainedPayloadBytes` 为零不允许保留对应字节；与表示无限制的零策略预算不同。

## 显式选择 JIT 后端

未提供已安装模块环境时，Core 与 C++ SDK 不会自动选择具体 JIT 实现。使用 `VeroScriptJitBackendLoader.h`
中的 `LoadJitBackend` 加载后端文件或目录，再把共享工厂赋给
`FEngineConfig::JitBackendFactory`；Session 可使用 `SetJitBackendFactory` 或
`SetJitBackendPath`。空路径移除后端，加载失败保留当前后端及缓存。

流式 `RunSource`／`RunFile` 重载可在 JIT 工厂之后接收 VM 工厂；较短的重载不提供解释器。按路径选择或采用模块环境默认值时，使用 `FStreamEngineHost` 与 `FEngineConfig`，遵循[服务后端选择](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection)。`Vm` 及 `Mixed` 的解释执行部分需要可用的 VM 工厂；显式 `Jit` 还需要 JIT 工厂。已配置的宿主可从模块环境解析工厂，也可接收显式工厂。调试和状态保留式热重载需要具备对应能力的 VM。静态 Core AOT 可在不提供动态 JIT 工厂时使用 `Mixed`，但解释执行的初始化及回退仍需要 VM。

调试挂接会释放保留的 Session JIT 缓存。桥接随后返回空调试器时，后续调用
可以使用仍被选中的工厂重新编译；断开调试不会取消脚本或销毁 Session。
核心与 C++ 公共布局已变化，所有原生消费者须使用匹配的 SDK 重新构建。

## 协作式取消

通过 `ApplyConfig` 或 `SetExecutionControl` 配置共享的 `FExecutionControl`。
协议线程可以调用 `Session::RequestCancel()`，包括所有者在空闲时替换控制对象的期间。
该调用取消当前控制对象的共享快照；空指针时不执行操作，取消不会传递给另一个控制对象。
`GetExecutionControl()` 返回仅供所有者线程使用的借用引用。
取消采用协作方式，无法中断原生回调。重复使用控制对象前应重置取消状态。
销毁 Session 前须等待取消调用和执行结束；断开调试连接仍是独立操作。

共享同一个 `FExecutionControl` 令牌的 Session 会共享取消状态。调用 `Reset()` 并重用令牌前，应等待之前的取消调用结束，避免旧请求取消下一次使用该令牌的执行。

## C++ 宿主管理内存

包含 `VeroScriptMemory.h`，即可使用核心的 `Memory::*` 接口。
`FEngineConfig::MemoryLimits` 对配置式 `RunSource` 和 `RunFile` 都有效。
`MaxBufferBytes` 限制单个自有缓冲区的容量及外部区域长度，`MaxRetainedBytes` 限制自有与借用区域共享的总字节预算；这两个限制的零值均表示无限制。

Session 在各次执行的 VM 之间共享同一个内存上下文。在所有者线程使用
`GetMemoryContext()`、`SetMemoryLimits()` 和 `GetMemoryStats()`。
修改配额要求执行空闲；降低配额失败时保留原配额。`ApplyConfig` 的内存配额被拒绝时，其他执行配置也保持原值。工作线程共享预算，但使用各自的线程域。在原生回调内，通过借用的
`FNativeCallContext::MemoryServices` 获取本次执行的上下文、工作检查、绑定目录及参数地址；不要在回调结束后保留这些服务。`Capabilities.bAllowMemoryAddress` 控制地址访问，裸地址本身不延长字节的生命周期。自定义原生回调暴露地址前应检查 `MemoryAddressesAllowed()`；宿主直接调用核心内存函数时，权限由宿主负责。

`Clear` 和重载保留内存上下文。兼容的热重载保留区域身份，替换模块初始化失败时恢复旧程序。
Session 销毁时先执行脚本清理，再关闭访问域。不要手动关闭借用的上下文，也不要在 Session 销毁后保留其引用。已取得的租借会保留字节，直到调用
`Memory::ReleaseLease`，即使 Session 已销毁也是如此。释放租借是线程安全的，但宿主必须先结束或同步原生访问。外部区域的所有者及释放回调数据必须比全部区域别名和租借存活更久；释放回调不得抛出异常。

参见 SDK 的 `CppMinimal/memory.cpp` 示例及核心的
[内存参考](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)。
独立 C++ AOT 输出不支持运行时内存操作或区域类型，生成器会明确报错；未使用的 `memory.vs` 或 `core.vs` 导入仍受支持。

`RetainCallable` 的第二个参数设为 `false` 不会转移 Session 的内存访问域。
即使宿主已串行化访问，区域操作仍要求 Session 所有者线程；在其他线程通过 callable 访问区域会失败并报告 `diag.memory_wrong_thread`。脚本工作线程使用各自的内存域。

## 模块公开宏

`Defines` 和 `PredefinedMacros` 独立初始化每个模块。Engine 和 Session 使用核心模块加载器：显式导入引入 `#define_export` 宏，`export import` 继续转导出它们。普通 `#define` 保持私有；`ImplicitImportPaths` 仅引入声明。`SourceOverlays` 遵循相同规则。参见[预处理器参考](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/preprocessor.md)。

修改宏依赖后，对文件 Session 调用 `Reload()` 并再次执行以完成初始化，或通过 `LoadSource()` 加载更新后的源码。启用热重载时，`Reload()` 迁移兼容的全局状态；`LoadSource()` 替换模块并重置状态。公开宏接口改变了字节码布局：部署的 `.vslib`／`.vsapp` 必须用当前核心重新构建，原生消费程序也须使用匹配的 Core 和 C++ SDK 重新编译。

## 编译期注解处理

通过 `FEngineConfig::AnnotationProcessing` 配置 Engine 和 Session；Session 也提供 `SetAnnotationProcessing`／`GetAnnotationProcessing`。处理器注册表必须先 `Freeze()`，随后保持不可变。工厂为每次声明准备创建独立处理器；处理器所属动态库及回调捕获的数据须保持有效。C++ AOT 使用 `FCodegenOptions::AnnotationProcessing` 配置 `FCppCodegenBackend`。

处理器在发起编译的线程上同步运行。准备回调期间不得执行或修改同一 Session；这些重入操作会被拒绝。取消沿用核心的 `FAnalysisCancelled` 异常，取消或编译失败保留当前程序。`FRunResult::Diagnostics` 保留 Warning／Information／Hint，错误流也会输出它们；非致命诊断不会令成功结果失败。`RunSource`／`RunFile` 的结果包含编译诊断及执行诊断。

通过 `ReadDependency` 读取影响生成结果的外部输入。缓存命中仍先执行声明准备，处理器身份、配置、生成内容及依赖参与缓存键；不可复现处理器设置 `Cacheable=false`。自定义预算同样适用于导入已有生成声明的库包。公开 API 见 Core 的 `VeroScriptAnnotationProcessing.h` 和 `VeroScriptProgramPreparation.h`，可构建的 SDK 消费示例见 `CppMinimal/main.cpp`。

## 宿主控制台接入

`IEngineHost` 可使用 `VeroScriptConsole.h` 中的类型实现 `QueryConsole` 和 `ExecuteConsole`，默认不提供终端能力。`HasConsoleTextOutput`／`WriteConsoleText` 用于精确输出且不添加换行，`FlushConsoleText` 刷新文本。`FStreamEngineHost` 转发到底层流。回调宿主需要同步工作线程调用，并存活到清理结束。VM 会在执行边界恢复样式和光标显示；终端状态共享，多任务应协调完整的颜色／文本／光标操作序列。

终端接口增加了虚方法，升级时须使用同一构建的 Core／C++ SDK 重新编译宿主及相关原生扩展；使用 JIT 时，也须部署匹配的后端。

## 通用编译处理

Engine 和 Session 传递 `FEngineConfig::CompileProcessing`；Session 另提供 `SetCompileProcessing`/`GetCompileProcessing`。C++ AOT 使用 `FCodegenOptions::CompileProcessing`。编译前必须冻结 `FCompileProcessorRegistry`。每次编译的处理器实例贯穿 `RawSource`、`ActiveSource`、`Types`、`Members`、`Bodies`；对已准备程序进行 AOT 生成时复用已接受的生成结果。回调在编译线程同步执行，不能重入同一个 Session。

通过 `ReadDependency` 读取影响生成的外部输入；不确定性处理器应设置 `Cacheable=false`。生成内容、依赖、注解定义、注释保留策略和预算均参与字节码缓存标识；命中缓存前仍执行处理。替换失败或取消时保留 Session 的活动程序。`CompileProcessing` 与旧 `AnnotationProcessing` 注册表共享核心事务和预算。仅使用旧注解接口时，提高旧操作数或源码预算会同步提高未修改的通用默认值；显式配置的通用注册表或预算仍是上限。

注解名称默认区分大小写。若要接受 ASCII 大小写变体，可将 `RegisterAnnotation` 的最后一个参数设为 `false`，或在 `CompileProcessing.AnnotationDefinitions` 中设置 `FAnnotationDefinition::CaseSensitive=false`。Session 使用自身绑定注册表，不可变模块环境使用其发布目录，C++ AOT 通过 `FCodegenOptions::Bindings` 接收目录。该策略同时影响处理器订阅和运行时反射，可通过 `annotation_info.matches_name()` 与 `annotation_definition_info.case_sensitive()` 查询。源码拼写保持不变；参数名称和元数据键仍精确匹配。`matches_name()`、`find_annotations()` 区分 `@`，`has_annotation()` 也接受省略 `@`。存在匹配重叠的注册会被拒绝，策略变化会使字节码缓存失效。此类反射查询依赖 Core 运行时，可用于 VM、Mixed 及带 Core 上下文的 JIT 宿主入口；独立 C++ AOT 会明确拒绝此类调用。

`CommentRetention` 控制源码注释保留；VM 支持运行时注解和注释查询。本次源码元数据格式变化后，应使用匹配的 Core/C++ 库重新编译原生消费者和二进制脚本包。参见 `VeroScriptCompileProcessing.h`、`VeroScriptSourceMetadata.h` 以及 SDK 的 `CppMinimal/main.cpp` 示例。

升级 Core/C++ SDK 时，使用新构建目录或完整重编译所有原生目标文件。在 Windows 的本地化 MSVC 与 Ninja 环境中，先在开发者命令提示符执行 `chcp 65001`，再进行 CMake 配置和构建，确保探测到的包含前缀与编译器输出编码一致，否则 Ninja 可能漏掉头文件变化。

## 脚本编译扩展

`FEngineConfig::ScriptCompileExtensions` 配置编译脚本；Session 提供 `SetScriptCompileExtensions`/`GetScriptCompileExtensions`。加载前设置 `Roots`、可选的 `RegistrationEntry`、`Configuration`、源码覆盖和标准库路径。源码中的 `import compiler "builder.vs";` 默认可用；`bAllowSourceImports=false` 禁用该入口。SDK 应部署匹配的 `compiler.vs`。

扩展在编译线程上的独立 VM 中执行，各阶段共享状态；生成后的业务代码使用所选执行后端。业务绑定不会自动开放给扩展；需要时显式设置仅供编译使用的 `Bindings`，并保证其在编译期间有效。编译回调不得执行或修改同一 Session；原生回调的授权仍由宿主负责。

扩展的 `ExecutionControl` 为空时继承 Engine/Session 的运行控制器；Session 中随 `SetExecutionControl` 更新。显式提供的控制器保持独立，即使最初与运行控制器相同。设置只影响后续编译或重载。扩展的 `MaxInstructions`、`TimeoutMilliseconds`、路径及桥接限额独立于业务执行限制；取消是协作式的，无法中断原生回调。

扩展身份、配置、源码、生成结果和声明的依赖参与缓存校验。通过编译器依赖服务读取生成输入；额外原生绑定默认禁止复用，只有明确声明确定性并提供稳定的 `BindingConfiguration` 时才允许。扩展编译或重载失败保留 Session 的活动程序。升级后须使用匹配的 Core/C++ SDK 完整重编译原生消费者和二进制脚本包；SDK 的 `CppMinimal` 已覆盖 Session 与 AOT 扩展链路。

`GetScriptCompileExtensions` 返回原始配置，继承控制器仍保持为空。读取配置、修改其他字段后再设置回来，不会改变控制器的继承规则。

## 模块环境与原生扩展

检查、加载或运行前，将 `FModuleEnvironment::Create` 返回的不可变快照设置到 `FEngineConfig::ModuleEnvironment`。Engine 与 Session 使用其模块映射、依赖范围、资源、编译策略和原生契约。映射的内存模块可通过规范的 `veroscript-module:/...` 路径交给 `CheckFile`／`RunFile`／`LoadFile`。导入由快照授权；安装不会自动导入声明。

环境提供 `Bindings` 时，`GetBindingCatalog`、绑定导出和 Session 调试视图均使用该目录。发布环境前，使用同一个暂存登记器（或 `FNativeExtensionRegistry` 的基础登记器）合并绑定，保持类型 ID 一致。Session 拒绝冲突的本地绑定及后续登记；环境拥有已发布目录时，`GetBindingRegistry` 抛出 `std::logic_error`。不提供目录的环境仍可使用 Session 本地登记。

Session 已加载程序时不能替换环境。结束执行，调用 `Clear`，再应用新配置并重新加载。配置被验证拒绝时保留原程序和设置；编译失败保留活动程序。`ApplyConfig` 异常遵循其[配置结果约定](#apply-config-result)。回调与逃逸的原生对象持有扩展代码；卸载宿主资源前先释放任务、可调用句柄和对象。原生库通过 `VeroScriptNativeExtensionLoader.h` 的显式接口加载；消费者和原生模块须使用匹配的 Core／C++ SDK 及共享 CRT 重编译。JIT 后端仍使用独立装载协议。

当前环境编译路径直接使用 Core 准备流程，不使用 Session／Engine 的持久字节码缓存。纯脚本 AOT 支持此快照；需要运行时原生包契约的程序在提供目标运行时适配器前会被明确拒绝。

`GetModuleEnvironment` 可保留当前快照；调试视图借用的 `ModuleEnvironment` 提供映射的内存源码和提供者契约。显式源码覆盖优先。这些表示编译输入，不是已加载主源码的冻结副本；宿主仍应公开实际 `LoadSource` 修订。

## 精确数值 API

Session 的 SetNumericLimits/GetNumericLimits 配置 FNumericLimits。VeroScriptNumerics.h 提供不可变 FBigInteger/FRational 及受检 Numerics::Try*，使用 FNumericOperationContext 的 Limits/Work。零选择有限默认值，工作量、取消、错误及失败保留输出均显式处理。独立源码 AOT 当前拒绝 Core 精确数值操作。

## 执行中断后的有界清理

<a id="bounded-interruption-cleanup"></a>

指令限额、超时或取消会终止正常业务执行。Core 使用独立的一份 **16,384 条逻辑指令与 50 ms** 清理预算，执行已进入作用域的 `finally` 和必要脚本析构；同步嵌套调用及临时清理 VM 共用该预算。已耗尽的原限额或已请求的取消不会立即阻止清理，原有权限仍生效。清理不能创建新任务、等待控制台输入或使用 `exit` 替换原终止错误。

最初的终止错误保持为主错误；清理错误附加在 `FRunResult.Diagnostics` 和 `VeroScriptVm::GetCleanupDiagnostics()`。清理完成后，宿主解除限额或重置取消令牌即可再次调用。这不回滚已经发生的业务修改。

清理未完成、存在未捕获的清理错误，或清理预算耗尽时，`VeroScriptVm::RequiresReset()` 与 `VeroScriptSession::RequiresReset()` 返回 true。若中断发生在已执行中的 finally 或析构内部，未完成的清理不会重放，而会要求重置。后续执行及保留状态操作以 `diag.execution_state_reset_required` 失败。宿主应调用 `Session.Clear()`，再加载新程序；Clear 会放弃不可用状态中未完成的脚本清理并释放原生所有权，不会重置宿主取消令牌。独立 VM 及保留的 Core 状态也须丢弃后重新建立。清理期限仍为协作式限制，无法强行终止正在阻塞的宿主回调；VM、Mixed、JIT 与适用 AOT 路径使用同一套 Core 契约。 若失败重载的恢复未完成，或外部根阻止 `Clear()`，应在 Clear 后检查 `RequiresReset()` 与 `IsLoaded()`；无法重置时重建 Session，不得继续执行不可用实例。

## Session 后端选择参数

`VeroScriptSession::SetVmBackendPath` / `SetJitBackendPath`

这些 Session 设置方法接受匹配的文件／目录；提供的模块环境包含解析器时，也接受已安装后端 ID。`auto` 请求环境候选，VM 还可使用保留的产品后备。`none` 或原生空字符串禁用选择；`""` 映射为该空请求。新建会话未显式选择时可继承环境；省略与显式清除不同。重新配置现有会话时，省略 VM 选择会保留已有执行域。显式选择失败保留先前后端。仅在创建线程的空闲会话上设置；VM 必须在执行前更换，因为 Clear／reload 不解除执行域固定。JIT 选择与 VM／Mixed／JIT 执行策略独立。

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` 提供 `FallbackVmBackendFactory`，显式 `VmBackendFactory`／`VmBackendPath` 优先。Session 会保留产品回退，以便在执行前通过 `SetVmBackendPath("auto")` 恢复。

## 持久执行与重载结算

持久 Session 中的普通函数可以使用所选 Mixed/JIT 后端；保留初始化区间、调试和有界清理继续使用 VM。不支持的原生操作在进入前回退，指令、超时、取消、权限和调用深度规则保持一致。

受支持的被拦截方法体也按后端能力选择执行；`Proceed` 中的嵌套调用保留拦截、引用写回和执行控制。拦截器错误未提供位置时使用调用处；显式位置和原始方法体错误保持不变。

直接转交 `Proceed` 的原始结果会保留按值返回的复制边界，包括结构体、原生值类型和固定数组，不会再次触发复制构造。替换或借用的结果仍按通常规则校验类型并按值复制。声明为引用返回的方法继续保持引用语义。

稳定的原生绑定和回调可以复用同代代码。`IHostBindingCatalog::GetRevision()` 发布运行时绑定修订；返回零的自定义目录含原生回调时不参与复用。`FHostBindingRegistry` 自动更新修订；外部变更影响回调代码或寿命所有权时，调用 `InvalidateRuntimeBindings()`。回调正常读取的可变业务数据不需要更新修订。执行期间不得修改目录。Session 的回调设置接口会使保留代码失效，每次调用都使用当前回调和服务。

`RetainMigratableValue(value, root, error)` 显式把宿主持有的值图加入全局状态使用的同一迁移图，复用物件、容器、别名、循环和闭包的兼容规则。替换初始化待结算时，`root.Read()` 返回空；成功提交或完成回滚后恢复读取。暂存期间不得访问复制出的原始句柄，它们不会自动重定位。普通根仍阻止替换，代次根仍按原规则失效；Load/Clear 仍要求释放可迁移根。 源码重载需要先设置 `SetHotReloadEnabled(true)`；事务式包／应用重载也使用此迁移服务。

在所有者线程读取 `GetReloadStatus()`。`OperationId` 独立于 `PreviousGeneration`、`CandidateGeneration`、`CurrentGeneration` 和 `CommittedGeneration`，因为回滚后可能再次使用同一个候选代次。`Rejected` 表示当前程序未被替换；`Pending` 表示已暂存；`RolledBack` 表示已完成恢复；`Applied` 表示初始化已提交；`AppliedWithErrors` 表示随后业务执行或退役值清理失败。`ResetRequired` 与 `RequiresReset()` 一致，禁止继续执行；回滚或清理无法完成时，应重新创建 Session。快照描述最近一次被接收的重载请求。脚本回滚不撤销宿主原生副作用。

Core、C++ 嵌入层和 Application 源码通过 `PathUtil::FromUtf8` / `ToUtf8` 支持 C++17/C++20 的 UTF-8 路径，不需要关闭 `char8_t`。即使产品版本号相同，原生后端和模块仍须针对匹配的 Core 实现重新构建。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](embedding.md) · [zh-TW](../zh-TW/embedding.md)
