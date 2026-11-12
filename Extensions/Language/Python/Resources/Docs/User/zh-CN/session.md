# Python 会话与执行

清空会话或关闭热重载可能执行脚本析构函数和 Python 回调。回调异常会立即以 `CallbackError` 报告；已经完成的清理或配置变更仍然生效。

在操作或回调执行期间关闭仍然有效的会话会抛出 `Error`，会话仍可继续使用；应在外层操作返回后关闭。关闭会话的析构过程中，`closed` 已为真，重复调用 `close()` 不会再次释放资源。

## 创建会话与解释结果

建议使用 `Session(...)` 上下文管理器，并在创建线程关闭会话。
`load_source`、`load_file`、`load_package` 和 `reload_package` 返回
`RunResult`；`run(RunMode.SCRIPT)` 调用 `main`，`RunMode.EVAL` 使用增量求值路径。

结果包含 `success`、`exit_requested`、`exit_code` 和 `diagnostics`。非零退出码
仍可能表示执行成功。返回值调用失败时会抛出 `VeroScriptError`；应查看结构化的
`VeroScriptError.result`。传输、类型、线程和回调错误使用模块对应的专用异常。

## 重要公开名称

状态访问的准确名称为 `is_loaded`、`get_global(name)` 和 `set_global(name, value)`。
`load_source`、`load_file`、`load_package` 和 `reload_package` 返回 `RunResult`；
`call`、`construct`、`call_method`、`get_field`、`set_field` 在失败时抛出
`VeroScriptError`。

`Session` 管理一次脚本环境的导入路径、宿主绑定、输入、诊断和生命周期。一个会话创建后应在创建线程使用，并在不再需要时调用 `close()` 或离开上下文管理器。

## 配置

构造 `Session` 时可以设置：

| 选项 | 作用 |
| --- | --- |
| `backend` | `Backend.VM`、`Backend.MIXED` 或 `Backend.JIT`；默认是 `MIXED`。 |
| `import_paths`、`implicit_import_paths` | 显式和隐式导入搜索路径。 |
| `script_args`、`defines` | `main` 参数和预定义宏。 |
| `stdlib_search_paths` | 标准库搜索路径。 |
| `source_overlays` | 不写入真实文件系统的源文件覆盖。 |
| `bytecode_cache_directory`、`use_bytecode_cache` | 字节码缓存位置及开关。 |
| `max_call_depth` | 脚本调用深度上限。 |
| `max_instructions` | 单次执行的 VM 指令数上限；`0` 表示不限制。 |
| `timeout_milliseconds` | 单次执行的墙上时钟时间上限；`0` 表示不限制。 |
| `enable_execution_control` | 启用会话的协作式取消令牌。 |
| `capability_policy_enabled` | 启用标准库副作用能力策略。 |
| `allowed_script_capabilities` | 向标准库授予文件、环境、控制台和任务创建能力的位标志。 |
| `allow_circular_imports` | 是否允许循环导入。 |
| `allow_project_wide_access` | 是否允许项目范围访问规则。 |
| `hot_reload_enabled` | 是否启用热重载。 |
| `print_callback`、`diagnostic_callback` | 输出和诊断回调。 |

`load_source`、`load_file` 和 `load_package` 将代码或包载入会话；`reload_package`、`reload` 重新加载；`clear` 清除当前加载状态；`close` 释放会话资源。

## 执行限制、取消与能力策略

`max_instructions` 和 `timeout_milliseconds` 是单次执行的协作式限制；设为 `0` 时分别表示不限制。设置任一限制，或使用 `enable_execution_control=True`，都会使用 VM 安全点路径，即使请求的后端是 `MIXED` 或 `JIT`。限制和取消请求不会强行终止已经运行的宿主回调。

当 `enable_execution_control=True` 时，可以从任意 Python 线程调用 `request_cancellation()` 请求取消当前执行。请求返回前必须保持会话有效。取消请求会保留在该会话中；开始下一次执行前应创建新的 `Session`。

当 `capability_policy_enabled=True` 时，标准库副作用只能使用 `allowed_script_capabilities` 授予的能力：文件读/写/删除、环境读/写、控制台输入/输出、创建任务和进程目录更改。`chdir` 同时要求 `ENVIRONMENT_WRITE` 和 `PROCESS_DIRECTORY_CHANGE`。该策略只约束内置函数；宿主回调仍需自行执行授权检查。关闭策略时，这些内置能力默认全部允许。

## 运行、调用与结果

`run()` 执行入口；`call()` 调用函数；`construct()` 构造宿主可见对象；`call_method()` 调用对象方法；`get_field`、`set_field`、`get_global` 和 `set_global` 访问字段及全局。可以先用 `has_function` 或 `has_method` 检查目标是否存在。

`RunResult` 同时提供成功状态、退出请求、退出码和结构化诊断。`success` 表示操作是否完成，不应只用退出码判断错误。诊断包含严重级别、代码、消息、源位置和格式化参数。

## 后端与性能策略

`Backend.VM` 适合确定性的解释执行；`Backend.MIXED` 在冷代码和可优化代码之间自动选择；`Backend.JIT` 尽可能积极地使用 JIT，并在不支持时回退。语义一致性不依赖于后端。

通过 `set_jit_policy(JitPolicy(...))` 调整 mixed 热调用阈值、编译尝试上限、缓存条目数和提前编译预算；`jit_stats` 属性返回编译、命中、回退及缓存统计，其中包括 `direct_native_calls`。该字段表示在同一代 JIT 代码中绕过虚拟机值桥、直接到达另一个原生入口的调用次数。

`jit_stats` 是最近一次调用的独立快照。实际执行入口由
`native_leaf_executions`、`native_cfg_executions`、`bridge_executions`、
`linear_executions`、`vm_trampoline_executions`、`controlled_native_executions`
和 `aot_executions` 记录，包括以错误或控制转移结束的入口。编译次数与缓存入口数
单独统计；`direct_native_calls`、标量快速路径和类型化循环属于嵌套事件，不能再作为
独立函数入口相加。VM 执行时这些执行入口计数为零。每次新调用都会重置它们，即使
复用已编译代码；`clear()` 会重置会话快照。`jit_cache_stats` 保留可复用上下文的
累计编译和资源统计。

<a id="session-jit-cache"></a>

`JitCacheLimits` 管理合资格普通 `Session.call`／原生 `CallFunction` 调用之间保留的代码，与字节码缓存分开。`JitPolicy.reuse_session_code` 默认 `True`。`max_compile_attempts` 或 `max_cached_entries` 为零表示计数不限，在保留上下文中也是如此。复用需要兼容的 JIT 工厂及允许 JIT 的有效执行策略。持久会话与稳定回调可复用代码。原生绑定目录需要提供运行时修订号。执行治理要求一致时，受控原生执行也可复用上下文。

`max_native_bytes` 默认 8 MiB，`max_retained_payload_bytes` 默认 32 MiB；零禁止保留该项资源。通过 `session.set_jit_cache_limits(...)`、`session.jit_cache_limits`、`session.jit_cache_stats` 设置与观察。统计为稳定的上下文累计值，不是实时或全进程内存计量。策略、限制、配置、模块、后端或可观察宿主边界变化会使上下文失效，达到资源限制时安全回退。

受治理的 `Backend.MIXED` 默认使用 VM。`session.set_jit_policy(JitPolicy(enable_controlled_mixed=True))` 选择兼容的受控原生执行；显式 `Backend.JIT` 与此开关独立。不支持的入口在执行前回退；调试、保留状态的初始化和有界清理使用 VM。

## 输入、热重载与线程

`set_input` 设置脚本输入；`set_hot_reload_enabled` 切换热重载。重新加载前应确保宿主没有继续使用旧的值或保留调用对象。容器返回的是快照，Python 列表或字典的原地修改不会写回脚本容器。

会话、值、保留调用对象和回调句柄都应遵守创建线程限制；不要在多个线程间共享同一个可变会话。跨线程通信应传递序列化数据或在目标线程重新获取值。


读取 `session.reload_status` 前，可用 `CAPABILITIES & RuntimeCapabilities.RELOAD_STATUS` 确认支持。请在创建会话的线程读取。`ReloadStatus` 快照用 `operation_id` 标识本次重载，分别记录 `current_generation` 和 `committed_generation`。`PENDING` 表示候选程序已暂存并等待初始化。`REJECTED` 保留原程序，`ROLLED_BACK` 表示已恢复原程序。`APPLIED_WITH_ERRORS` 表示候选程序已提交，随后执行或清理出错。`RESET_REQUIRED` 会阻止后续执行。若 `clear()` 无法重置，请重建会话。宿主仍持有外部值时，暂存可能被拒绝并保留上次状态。

## VM 选择

标准 Python 包内置静态字节码 VM，普通会话无需额外安装 VM 动态库。`RuntimeCapabilities.VM_BACKEND_SELECTION` 表示支持选择接口。可在首次执行前，由会话创建线程在空闲状态调用 `set_vm_backend(path)`，选择兼容的 VM 文件或目录；`None` 或空字符串清除选择。替换失败会保留原 VM。执行开始后选择被冻结，`clear()` 也不会解除；需要更换时创建新会话。VM 选择与 `Backend.VM`、`MIXED`、`JIT` 执行策略相互独立。

`get_vm_backend_info()` 返回拥有自身数据的 `VmBackendInfo` 快照，包含 `id`、`version`、`target`、`library_path`、`capabilities`、`selected` 和 `frozen`。静态 VM 的 `library_path` 为空，可通过 `selected` 区分静态 VM 与未选择 VM。替换后端或关闭会话后，已有快照仍然有效。

## JIT 显式选择

执行策略与后端选择分别设置。未提供已安装模块环境时，Runtime 不提供默认模块：未选后端时 mixed 使用 VM，强制 jit 在执行前失败。会话空闲时，在所有者线程使用 `Session.set_jit_backend(path)` 选择独立安装、兼容的库文件或目录；`set_jit_backend(None)` / `set_jit_backend("")` 清除选择。替换失败保留先前后端。`get_jit_backend()` / `get_jit_backend_info()` 查询实际身份和原生能力；`backend-services` 仅表示 Core 服务契约。持久会话中的合资格调用可使用选定后端。文件/目录发现与 ABI 规则见 JIT 指南。

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/zh-CN/index.md)

```python
with Session(backend=Backend.MIXED) as session:
	session.set_jit_backend("/absolute/path/to/Jit")
	session.load_source("int main(){ return 0; }")
	result = session.run()
```

## 一次性求值与后端能力

普通会话中，`run(RunMode.EVAL_ONCE)` 每次调用执行一次模块初始化，不调用 `main`，允许已选择的 JIT 后端编译初始化代码。`RunMode.EVAL` 保持增量求值路径；`RunMode.SCRIPT` 还会调用 `main`。`RuntimeCapabilities.EVAL_ONCE` 表示运行库支持该模式。热重载会话保留已初始化的全局状态，保留状态的初始化使用 VM。

`get_jit_backend_info()` 返回拥有自身数据的 `JitBackendInfo` 快照。其 `capabilities` 使用 `JitBackendCapabilities`：`NATIVE_CODE`、`PORTABLE_EXECUTION`、`NATIVE_TYPED_LOOPS`、`DIRECT_SCALAR_CALLS`。该快照描述实际选择的模块；`CAPABILITIES` 描述 Core 服务能力。后端替换或 Session 关闭后，已有快照仍然有效。

## 编译服务与诊断严重级别

`COMPILE_CAPABILITIES` 与 `CompileCapabilities.ANNOTATION_PROCESSING` 描述编译服务，独立于 `CAPABILITIES` 和所选 JIT 后端。`Diagnostic.severity` 使用 `DiagnosticSeverity` 枚举：`ERROR`、`WARNING`、`INFORMATION`、`HINT`。应以 `RunResult.success` 判断操作是否成功；非空诊断列表也可能仅包含非致命信息。

`session.set_annotation_processing_service(handle)` 在会话空闲时接入原生 SDK 扩展创建的有效不透明句柄。会话复制服务配置，接入后生产者可释放自己的句柄；处理器所在原生模块及回调数据须保留到服务清除或会话关闭。`None` 或 `0` 清除服务，任意整数地址不是有效服务句柄。`register_annotation(...)` 仅声明注解词汇，不安装编译期处理器。

## 宿主控制台接入

`Session(console_query=..., console_control=..., console_write=...)` 提供可选终端回调。查询返回 `ConsoleInfo(output_terminal, color, cursor, clear, width, height)`，尺寸为 0 表示未知。控制回调接收 `ConsoleOperation` 及两个整数：ANSI 颜色 0..15／默认 -1、清行模式 after/before/all 对应 0/1/2、从 0 开始的列／行，或光标显示 0/1。同步成功返回 `True`。文本回调接收不添加换行的精确文本并返回 `bool`；省略时保留缓冲打印回调。回调应存活到会话关闭结束，自行同步工作线程调用；异常通过 `CallbackError` 返回。多任务需要协调完整的终端操作序列。

## 已解析模块环境与通用编译处理

`Session(module_environment=handle)` 接收匹配原生 SDK 中 `VeroScript::Interop::CreateModuleEnvironment` 创建的不透明快照句柄。会话持有不可变环境，构造成功后生产者可释放自己的句柄。环境统一提供源码归属、精确导入映射、资源、编译服务与执行依赖；需要标准库时，应由快照提供搜索根及其归属，Python 在此模式下不自动追加包内标准库根。替换快照时创建新会话。环境拥有绑定目录时禁止本地登记，应先组合绑定再发布快照。`None` 或 `0` 使用普通配置；任意整数地址无效。通过 `RuntimeCapabilities.MODULE_ENVIRONMENT` 查询支持情况。

`set_compile_processing_service(handle)` 接入原生 SDK 的通用编译处理服务，遵循注解服务相同的空闲、创建线程与生命周期要求。`None` 或 `0` 清除服务。对应能力为 `CompileCapabilities.GENERAL_PROCESSING`；脚本编译扩展使用 `set_script_compile_extensions(...)` 配置。

## 闭包与局部静态存储

脚本函数值支持初始化捕获与 `mutable`。`Value.clone()` 和 `retain_callable()`
共享原有闭包环境；再次求值同一 lambda 表达式会创建独立环境。引用捕获会在创建它的
脚本函数返回后保留其存储。关闭会话会使保留调用对象失效。

局部 `static` 变量属于执行状态。普通 `call()` 创建新的执行状态；
`hot_reload_enabled=True` 会在调用之间保留状态。关闭会话会释放其持有的静态状态。
独立 [Python AOT](aot.md) 拒绝源码 lambda 和局部 `static` 声明。

## Session 后端选择参数

`Session.set_vm_backend` / `set_jit_backend`

这些 Session 设置方法接受匹配的文件／目录；提供的模块环境包含解析器时，也接受已安装后端 ID。`auto` 请求环境候选，VM 还可使用保留的产品后备。`none` 或原生空字符串禁用选择；`None` / `""` 映射为该空请求。新建会话未显式选择时可继承环境；省略与显式清除不同。重新配置现有会话时，省略 VM 选择会保留已有执行域。显式选择失败保留先前后端。仅在创建线程的空闲会话上设置；VM 必须在执行前更换，因为 Clear／reload 不解除执行域固定。JIT 选择与 VM／Mixed／JIT 执行策略独立。

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](session.md) · [zh-TW](../zh-TW/session.md)
