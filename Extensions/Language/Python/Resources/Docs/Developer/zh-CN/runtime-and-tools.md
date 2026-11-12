# Python 运行时与静态工具

## 类型化循环统计

`native_typed_loop_executions` 统计原生类型化循环内核的调用次数，包括报告源码可见运行时错误后结束的调用。它是执行可观测性计数器，而不是成功计数器。

## 运行时适配器

`VeroScriptPython` 经 `VeroScriptInterop` 公开 `Session`、`Value`、`Package`、保留可调用对象、`HostParameter`、宿主函数和宿主类。文档规定的会话和可调用对象受创建线程限制且必须显式关闭。允许时原生操作释放 GIL，回调重新获取 GIL 并可能随时间并发进入，因此回调拥有状态需要外部同步。

公共运行时还包括 `ExternalReference` 及其引用种类、`GenericHostClass`、`available_languages`、`get_language`、`set_language` 和 `merge_catalog_directory`。语言发现来自已部署目录树。目录合并会验证基线，并区分运行时诊断与宿主产品 UI 文本；包装器不得翻译 API 名、目录键或引用种类值。

`Value` 为 array、map、ordered map、set、queue、stack 和 deque 使用显式元素类型；枚举返回拥有的快照。有符号 `char`/`int8` 在 Python 中表示为 `-128..127` 的 `int`；字符 map 键和 set 元素在快照、查询及 `to_python()` 中保持该表示，越界输入会被拒绝而非截断。变更回调异常在原生变更后报告，不会回滚变更。

## 静态前端

`VeroScriptPythonFrontend` 解析带类型 Python 声明而不导入或执行输入模块，记录源范围、函数、类、enum、字段、参数、decorator、文档、导入和规范化类型注解。不支持的顶层执行和嵌套语句须以结构化诊断失败。

`VeroScriptPythonBindGen` 消费 AST，发射 Binding Catalog 元数据、每输入一个 API 引用和项目清单。`VeroScriptPythonTranspiler` 将支持的类型子集降级为 VeroScript，解析提供的项目相对导入，发射源码映射，并验证完整生成树。BindGen 和 Transpiler 还会拒绝不符合 VeroScript ASCII 标识符规则或属于 `do` 等语言关键字的生成名称；即使关闭编译器验证，该检查仍然生效。Transpiler 同时执行核心的 32 层语句嵌套限制，并将 Python `pass` 发射为合法的空 VeroScript 块。名称以下划线 `_` 开头的类和顶层函数不会导出，也不能被生成的公共契约引用。发射的数值常量必须是有限值，并落在核心无后缀有符号 32 位整数字面量范围内。Python list、set 和 dictionary 字面量仅允许作为类型化声明的初始化器，以匹配核心的大括号初始化语法。在类型化子集具备可空类型契约之前，Optional 注解和 Python 身份比较均会被拒绝。`break` 和 `continue` 必须位于发射出的外围 `while` 循环内。

受支持的整数 `IntEnum` 和 `IntFlag` 表达式会被安全求值，并发射为核心 enum 语法要求的有符号 64 位字面量。

仅当 `self` 是实例方法接收者时才会将其重写为 `this`；名为 `self` 的普通标识符保持不变。`None` 仅在声明为 `-> None` 的函数或构造函数中发射为无返回值的 `return;`。

非 `void` 函数必须在所有静态支持的路径上返回值。带类型标注的局部变量必须直接声明在函数体中；为避免 Python 与 VeroScript 的作用域产生差异，不允许在 `if` 或 `while` 块内声明。

多文件项目会在发射前拒绝导出模块变量与其他模块变量或类型的冲突，以匹配核心符号命名空间。

## 会话执行契约

Python `Session` 镜像原生生命周期，入口包括 `load_source`、`load_file`、`load_package`、`reload_package`、`build_package_file`、`run`、`reload`、`diagnostics` 和 `set_input`。`set_hot_reload_enabled` 控制保留状态的重载，`request_cancellation` 可从另一线程协作请求取消，不能强制中断活跃宿主回调。`set_jit_policy` 和 `jit_stats` 与核心 VM 一样公开自适应后端策略和最近运行统计。`RuntimeCapabilities` 解码 `CAPABILITIES` 中的 Core 服务能力。实际选择的模块通过 `session.get_jit_backend_info()` 查询：`native_code` 表示原生发码，`capabilities` 是 `JitBackendCapabilities` 掩码，包含 `NATIVE_CODE`、`PORTABLE_EXECUTION`、`NATIVE_TYPED_LOOPS` 和 `DIRECT_SCALAR_CALLS`，不能从 `HOST_PLATFORM` 推断。`cost_model_vm_fallbacks` 和 `native_typed_loop_executions` 分别统计最近运行中对应的决策与执行次数。

Python 适配器将 `reuse_session_code`、`enable_controlled_mixed`、缓存限制和统计转发给原生 Session。资格与世代记账统一维护在 [C++ Session](../../../../../Cpp/Resources/Docs/Developer/zh-CN/session.md#session-jit-cache)，Python API、默认值与配额含义维护在 [Python Session](../../User/zh-CN/session.md#session-jit-cache)。适配器不另设非零计数前提，也不一律禁止受治理执行复用。

## 值、调用与 channel

`Value.channel` 创建无界或有界 channel。`send`、`try_send`、`send_for` 分别为阻塞、非阻塞和定时发送；`receive`、`try_receive`、`receive_for` 为对应接收，`close_channel` 关闭同步端点。channel 值是显式跨 worker 共享机制，普通脚本全局仍为 worker 本地。

调用面包括 `has_function`、`has_method`、`call`、`call_method`、`call_method_bypassing_interceptor`、`construct_object`、`retain_callable`、`get_field`、`set_field`、`get_global`、`set_global` 和 `apply_class_defaults`。绕过入口拦截器不会禁用方法体发起嵌套调用的拦截器。

## 宿主绑定、反射与持久化

注册入口含 `register_function`、`register_annotation`、`register_type_alias`、`register_generic_host_type`、`instantiate_generic_host_type`、`register_host_class`、`register_host_enum`、`register_host_protocol` 和 `register_host_member`。反射和绑定导出使用 `export_reflection_json`、`export_bindings_json`。

`register_serializer`、`serialize_object`、`deserialize_object` 与 `enumerate_object_references` 遵循 Interop 外部序列化和借用引用规则。`set_mutation_callback` 报告字段/容器变更；`set_instance_call_interceptor` 接收已解析目标，可继续或处理调用。API、JSON 键、类型名和诊断均保持原样。

## Python 线程边界

阻塞、定时和非阻塞 channel 操作在原生 channel 操作期间释放 Python GIL。这避免等待 channel 阻塞无关 Python 线程，但不会使 `Session` 或 `Value` 普遍线程安全；会话操作仍遵守原生所有者线程规则。

`request_cancellation` 是唯一明确可从其他 Python 线程安全调用的 `Session` 操作，调用方必须保留 `Session` 到原生请求返回。原生回调异常会记录并在调用边界后转回 Python。JIT 统计映射将 `direct_native_calls` 作为独立计数器。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/runtime-and-tools.md) · [fr-FR](../fr-FR/runtime-and-tools.md) · [ja-JP](../ja-JP/runtime-and-tools.md) · [ko-KR](../ko-KR/runtime-and-tools.md) · [zh-CN](runtime-and-tools.md) · [zh-TW](../zh-TW/runtime-and-tools.md)
