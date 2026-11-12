# Interop SDK

## JIT 统计兼容性

对于 `vs_interop_session_get_last_jit_stats`，`struct_size` 为零表示旧版调用形式，仅写入原有统计字段前缀；当前调用方必须传递 `sizeof(vs_interop_jit_stats)` 才能接收追加计数器。`native_typed_loop_executions` 统计原生类型化循环内核的调用次数，包括报告源码可见运行时错误后结束的调用。它不是成功计数器。

`native_leaf_executions`、`native_cfg_executions`、`bridge_executions`、`linear_executions` 和 `vm_trampoline_executions` 统计普通可执行入口的实际进入次数，包括因错误或控制转移退出的调用。对应的 `*_entries` 字段统计编译产物。这些计数属于最近一次执行，命中热缓存时可以没有编译次数而仍有执行次数。`scalar_native_fast_path_calls` 仅统计宿主原生快路调用。直接调用和类型化循环调用属于嵌套事件，不应相加作为独立函数调用次数。

C API 提供聚合统计。可选的逐函数诊断快照、回退原因与计时目前通过 Core/C++ 提供，C JIT 策略不会启用该诊断采集。

`VeroScriptInterop` 是面向 C、Python、C#、Rust 以及其他语言适配器的语言无关进程 ABI。它把 VeroScript 的会话、值、调用、绑定和包能力包装为不依赖 C++ 对象布局与 STL 类型的 C 接口。需要在 C++ 内部直接嵌入时，应优先阅读[宿主集成](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md)；需要跨语言或稳定的动态库边界时，使用本页所述 SDK。

函数签名、结构布局、常量以及功能可用性以随 SDK 提供的公共头文件为准。本页说明契约和使用方式；完整声明列表请直接查阅该头文件。

## ABI 与安装

SDK 提供的 `VeroScriptInterop.h` 公共头文件同时兼容 C11 和 C++17。当前 ABI 常量为：

```c
#define VS_INTEROP_ABI_VERSION UINT32_C(0x00000101)
```

当前基线为 ABI 0.1.1。高 16 位编码主版本，中间 8 位编码次版本，低 8 位编码补丁版本；绑定要求 ABI 版本完全一致。结构化描述和输出结构以 `struct_size` 开头；兼容扩展通过在结构尾部追加字段完成。固定的字符串视图和键值对视图不带 `struct_size`，其布局由头文件固定。

安装 SDK 后即可获得运行库、C11 头文件和 CMake 导入目标。适配器不需要依赖 VeroScript 的 C++ 头文件或 STL ABI。

CMake 消费者可以使用：

```cmake
find_package(VeroScriptInterop CONFIG REQUIRED)
target_link_libraries(MyAdapter PRIVATE VeroScript::VeroScriptInterop)
```

Windows 上应把 SDK 运行库 DLL、本地化资源和运行库法律声明部署在宿主可执行文件旁；分发应用时保留这些文件。只复制 `VeroScriptInterop.dll` 会遗漏必要依赖。集成示例请通过文档或源码参考另行获取。

## 核心对象模型

接口使用不透明句柄。适配器不能读取句柄内部布局，也不能使用系统 `free` 释放模块拥有的内存。

| 句柄 | 作用 |
| --- | --- |
| `vs_interop_session` | 隔离的加载、编译、运行和调用会话。 |
| `vs_interop_value` | 脚本标量、对象、容器、函数或宿主对象值。 |
| `vs_interop_callable` | 可跨当前原生回调保留的脚本调用对象。 |
| `vs_interop_call_context` | 原生函数回调期间读取参数、写回引用参数或发起回调局部嵌套调用。 |
| `vs_interop_buffer` | 模块拥有的错误文本、包字节或序列化结果；只能用 `vs_interop_buffer_destroy` 释放。 |
| `vs_interop_package` | 已校验的 `.vsapp`/`.vslib` 内存包。 |
| `vs_interop_intercept_context` | 实例方法拦截器的接收者、目标方法和参数。 |
| `vs_interop_generic_type_context` | 泛型宿主值类型工厂定义具体类型、成员和协议。 |
| `vs_interop_external_serialize_context` / `deserialize_context` | 外部对象序列化与反序列化回调上下文。 |

最小的 C 调用流程是“创建会话 → 应用配置 → 加载源或文件 → 运行 → 读取诊断 → 销毁”：

```c
#include "VeroScriptInterop.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static vs_interop_string_view view(const char* text)
{
	vs_interop_string_view result;
	result.data = text;
	result.size = (uint64_t)strlen(text);
	return result;
}

int main(void)
{
	vs_interop_session* session = NULL;
	vs_interop_session_options options = {0};
	options.struct_size = sizeof(options);

	if (vs_interop_session_create(&options, &session) != VS_INTEROP_STATUS_OK)
	{
		return 1;
	}

	const vs_interop_string_view source = view(
		"int main()\n"
		"{\n"
		"\tprint(\"hello\");\n"
		"\treturn 0;\n"
		"}");
	vs_interop_run_result load = {0};
	load.struct_size = sizeof(load);

	const vs_interop_status load_status = vs_interop_session_load_source(
		session,
		source,
		view("main.vs"),
		&load);

	if (load_status != VS_INTEROP_STATUS_OK || !load.success)
	{
		vs_interop_session_destroy(session);
		return 2;
	}

	vs_interop_run_result run = {0};
	run.struct_size = sizeof(run);
	const vs_interop_status run_status = vs_interop_session_run(
		session,
		VS_INTEROP_RUN_SCRIPT,
		&run);

	const int result = run_status == VS_INTEROP_STATUS_OK && run.success ? 0 : 3;
	vs_interop_session_destroy(session);
	return result;
}
```

示例中的脚本通过 C 字符串转义传入；实际脚本仍应使用项目约定的 Tab 缩进、独占一行的左花括号和 `int main` 入口。

## 状态、诊断和退出语义

所有返回状态都是显式的 `vs_interop_status`。常用状态包括：

| 常量 | 含义 |
| --- | --- |
| `VS_INTEROP_STATUS_OK` | 调用完成。 |
| `VS_INTEROP_STATUS_INVALID_ARGUMENT` | 句柄、指针、字符串视图或数组参数无效。 |
| `VS_INTEROP_STATUS_WRONG_THREAD` | 调用违反会话或对象的线程约束。 |
| `VS_INTEROP_STATUS_ENGINE_ERROR` | 引擎执行或加载失败。 |
| `VS_INTEROP_STATUS_TYPE_MISMATCH` | 值种类或宿主类型不匹配。 |
| `VS_INTEROP_STATUS_CALLBACK_ERROR` | 宿主回调报告失败。 |
| `VS_INTEROP_STATUS_UNSUPPORTED` | 当前构建不支持该能力。 |
| `VS_INTEROP_STATUS_OUT_OF_MEMORY` / `BUFFER_TOO_SMALL` / `INTERNAL_ERROR` | 资源、输出缓冲区或内部错误。 |

`vs_interop_run_result` 包含 `struct_size`、`success`、`exit_requested`、`exit_code` 和 `diagnostic_count`：

- `success` 表示编译/执行是否正常完成，不表示 `exit_code` 是否为零；
- 成功的 `int main` 可以返回非零退出码；
- `exit_requested` 只表示脚本显式调用了 `exit(n)`，包括 `exit(0)`；
- 普通的 `int main` 返回不会设置 `exit_requested`；
- 失败时 `exit_code` 是引擎或 API 错误码，不能当作脚本正常退出码解释。

诊断通过 `vs_interop_session_diagnostic_count`、`vs_interop_session_get_diagnostic` 读取。每项包含 `message`、`message_id`、`file_label`、`line` 和 `column`；诊断参数可用 `diagnostic_argument_count`、`diagnostic_argument` 和 `format_diagnostic` 获取。返回的字符串视图必须遵循对应会话操作的生命周期。

## 本地化

Interop 的本地化状态由核心进程统一维护，并且本地化操作本身是线程安全的；它不是每个 `vs_interop_session` 独立的一份配置。一个进程中的语言切换会影响之后由核心格式化的诊断，包括 `vs_interop_session_format_diagnostic`。

| 函数 | 作用 |
| --- | --- |
| `vs_interop_locale_set_language` | 根据规范或可规范化的 BCP 47 标签选择已加载的语言；成功返回 `VS_INTEROP_STATUS_OK`。 |
| `vs_interop_locale_get_language` | 返回当前实际匹配到的规范文化标签，例如 `en-US` 或 `zh-CN`。 |
| `vs_interop_locale_translate` | 通过已加载的进程级目录返回模块所有的 UTF-8 翻译，未知英文原样复制。用 `vs_interop_buffer_destroy` 释放。 |
| `vs_interop_locale_get_available_languages` | 返回由模块拥有的 UTF-8 JSON 数组缓冲区，其中包含当前已加载的规范文化标签。 |
| `vs_interop_locale_load_catalog_directory` | 校验并替换完整的进程级本地化目录树。 |
| `vs_interop_locale_merge_catalog_directory` | 校验目录并把它合并到当前目录树；`override_existing != 0` 时覆盖已有消息 ID。 |

目录必须遵循[诊断本地化](../../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md)中的 `<culture>/<component>.json` 布局，并包含完整的 `en-US` 基准。目录校验或加载失败时，`load_catalog_directory` 和 `merge_catalog_directory` 返回 `VS_INTEROP_STATUS_ENGINE_ERROR`，并通过 `out_error` 返回由模块拥有的错误缓冲区；无效参数、内存不足或内部异常会返回相应的其他状态。调用方必须使用 `vs_interop_buffer_destroy` 释放错误缓冲区和可用语言 JSON 缓冲区。`vs_interop_locale_get_language` 返回的字符串视图不是调用方拥有的内存，适配器应在需要长期保存时复制内容。

```c
/* The view(...) helper from the minimal workflow above is reused here. */
int ConfigureLocale(void)
{
	vs_interop_buffer* error = NULL;
	if (vs_interop_locale_load_catalog_directory(
			view("localization"),
			&error) != VS_INTEROP_STATUS_OK)
	{
		if (error != NULL)
		{
			vs_interop_buffer_destroy(error);
		}
		return 1;
	}

	if (vs_interop_locale_set_language(view("zh-CN")) != VS_INTEROP_STATUS_OK)
	{
		return 2;
	}
	return 0;
}
```

`vs_interop_locale_get_available_languages` 返回包含当前已加载规范文化标签的 JSON 数组。适配器应使用返回的数组填充语言选择器，而不需要重复扫描宿主文件系统。

适配器应先检查 `vs_interop_get_capabilities()` 是否包含 `VS_INTEROP_CAP_LOCALIZATION`，再调用这些可选导出。语言目录可以在创建会话前加载，也可以在运行期间加载；但如果多个线程或会话共享一个进程，宿主仍应明确约定语言切换时机，避免让同一批用户界面同时看到不同语言。

## 会话配置

`vs_interop_session_apply_config` 接收 `vs_interop_config`。该结构的公开字段如下：

| 字段 | 作用 |
| --- | --- |
| `import_paths` | 普通模块搜索路径。 |
| `implicit_import_paths` | 对每个源模块隐式导入的文件；不会修改源文件，也不会自动重新导出声明。 |
| `script_args` | 传给 `main(string[] args)` 的脚本参数。 |
| `defines` | 对象宏或条件编译定义。 |
| `stdlib_search_paths` | 标准库搜索根目录。 |
| `source_overlays` | 内存中的路径/源代码覆盖项。 |
| `bytecode_cache_directory`、`use_bytecode_cache` | 字节码缓存目录和开关。 |
| `max_call_depth` | 单次执行的递归/调用深度上限；`0` 使用环境变量或默认值。 |
| `max_instructions` | 单次执行的 VM 指令数上限；`0` 表示不限制。 |
| `timeout_milliseconds` | 单次执行的墙上时钟时间上限；`0` 表示不限制。 |
| `execution_backend` | `VM = 0`、`Mixed = 1`、`JIT = 2`。 |
| `predefined_macros` | 结构化函数宏，包括名称、参数、函数体、源名称和可变参数标志。 |
| `allow_circular_imports` | 是否允许循环导入，默认允许。 |
| `allow_project_wide_access` | 是否允许项目范围内访问非导出声明，默认关闭。 |
| `enable_execution_control` | 安装协作式取消令牌；成功应用配置时会重置之前的取消请求。 |
| `capability_policy_enabled` | 启用标准库副作用能力策略。 |
| `allowed_script_capabilities` | `FILE_READ`、`FILE_WRITE`、`FILE_DELETE`、`ENVIRONMENT_READ`、`ENVIRONMENT_WRITE`、`CONSOLE_INPUT`、`CONSOLE_OUTPUT`、`TASK_SPAWN` 和 `PROCESS_DIRECTORY_CHANGE` 的位掩码。仅在启用策略时使用；`chdir` 同时要求 `ENVIRONMENT_WRITE` 和 `PROCESS_DIRECTORY_CHANGE`。 |

`max_instructions` 和 `timeout_milliseconds` 是单次执行的协作式限制；`0` 表示不限制。非零限制或 `enable_execution_control` 要求 VM 或满足条件的受控 JIT 路径执行协作式检查。受控 Mixed 默认使用 VM；测量目标负载后，可用 `vs_interop_jit_policy.enable_controlled_mixed` 显式启用原生路径，显式 JIT 选择独立于此开关。查询所选后端的 `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION`，并用 `controlled_native_executions` 确认实际执行；不支持或成本模型优先选择 VM 的入口仍保留治理检查。`vs_interop_session_request_cancel` 线程安全；未启用执行控制时返回 `VS_INTEROP_STATUS_INVALID_ARGUMENT`，不会强行终止已运行的原生回调。调用返回前必须保持会话有效。

当 `capability_policy_enabled` 非零时，标准库内置函数只能使用 `allowed_script_capabilities` 授予的能力；宿主回调不受此策略约束，必须自行执行授权检查。依赖这些可选治理能力前，应检查 `VS_INTEROP_CAP_EXECUTION_GOVERNANCE`。

`vs_interop_jit_policy` 可设置混合后端的热点阈值、最大编译尝试数、缓存容量和预编译预算；`vs_interop_session_get_last_jit_stats` 返回最近一次运行的编译、缓存、分派、回退、预算跳过、快速路径和 `direct_native_calls` 统计。`direct_native_calls` 表示在同一代 JIT 代码中绕过虚拟机值桥、直接到达另一个原生入口的调用次数。

### 会话可复用代码缓存

应先检查 `VS_INTEROP_CAP_SESSION_JIT_CACHE`。这项可选能力公开 `vs_interop_jit_cache_limits`、`vs_interop_jit_cache_stats`，以及会话的缓存上限设置/读取和统计读取函数。对每个传入或接收的可扩展结构体都要初始化 `struct_size`。默认上限为 8 MiB 原生代码和 32 MiB 完整保留载荷；将上限设为零会禁止保留该资源。此缓存与字节码缓存相互独立，只为满足条件的普通 `Run`/`EvalOnce`/`CallFunction` 上下文保留已编译代码。其统计是已稳定的累计上下文值，而非实时或进程级内存计量。重新配置或上下文失效会清除保留代码；达到资源上限会安全回退到 VM 执行，不改变脚本语义。

要复用代码，`vs_interop_jit_policy.reuse_session_code` 必须非零（默认值），且有效后端不是 VM。`max_compile_attempts` 或 `max_cached_entries` 为零表示对应计数不限额，保留代码仍受原生代码和完整载荷上限约束。绑定修订号必须稳定。持久会话和热重载会话可以复用与程序世代对应的代码。原生绑定、实例拦截器或变更回调发生变化时，保留代码失效。调试、持久状态初始化和有界清理使用 VM。满足条件的受控执行可复用匹配的缓存及治理身份；重复调用不会重置上下文的累计编译配额。

### 公开宏与包

`defines` 和 `predefined_macros` 为每个源模块提供预处理初始宏。`#define` 为私有宏，
`#define_export` 公开模块的最终宏定义。显式 `import` 在路径字符串之后激活公开宏；
`export import` 和 `#include` 还会再导出这些宏。`implicit_import_paths` 仅导入声明。
这些规则同样适用于 `source_overlays`。公开函数式宏在调用方环境中展开，不捕获私有
辅助宏。不同来源的同名公开宏会冲突；有意替换前先使用 `#undef`。
源码导入环的依赖闭包中出现任何 `#define_export`（包括非活动分支中的指令）时，
都会被拒绝，不受 `allow_circular_imports` 影响。

C ABI 构建的库包在两种 `strip_debug_info` 设置下都保留公开宏。条件导出在构建时
确定，使用方的宏定义不会重新选择导出分支；应用包不携带公开宏接口。
字节码使用固定 `VSBC` 标识和独立格式版本，加载要求版本完全匹配；含
`VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` 的包和缓存须从源码重新构建。
公开 C ABI 仍为 0.1.1。独立 JIT 后端也应使用当前核心重建；加载器会拒绝头文件
指纹或布局不兼容的模块。

## 类型和值

`vs_interop_value_create_array` 规范化元素类型并逐个检查值。对象槽位可接受继承所声明基类或实现所声明接口的脚本派生对象，判定使用源值自身的模块元数据。容器声明仍保持不变性：这不意味着可以把 `array<Derived>` 赋给 `array<Base>`，也不会引入隐式数值或容器转换。

Interop 的类型描述用 `vs_interop_native_type_desc` 递归表达宿主函数或成员的返回类型、参数类型及容器类型参数。公共原生类型包括：

| 类别 | 类型标识 |
| --- | --- |
| 标量 | `VOID`、`BOOL`、`INT32`、`UINT32`、`INT64`、`UINT64`、`FLOAT`、`DOUBLE`、`INT8`、`UINT8`、`INT16`、`UINT16`、`STRING`。 |
| 宿主和枚举 | `HOST_OBJECT`、`ENUM`。名称字段用于宿主类型名、枚举名或函数签名。 |
| 容器 | `ARRAY`、`MAP`、`SET`、`QUEUE`、`STACK`、`ORDERED_MAP`、`DEQUE`、`CHANNEL`、`MULTI_MAP`、`ORDERED_MULTI_MAP`、`PRIORITY_QUEUE`、`MULTI_SET`、`ORDERED_MULTI_SET`。 |
| 可调用对象 | `FUNCTION`。 |

值句柄的 `vs_interop_value_kind` 还区分 `OBJECT`、`NATIVE_ARRAY`、`WSTRING`、`FUNCTION` 和 `HOST_OBJECT`。当前固定宽度数值是独立值种类：`FLOAT` 是 binary32，`DOUBLE` 是 binary64；`INT8`、`UINT8`、`INT16`、`UINT16` 不应在适配器中退化为 `INT32` 或无符号 C `int`。

标量使用成对的 `vs_interop_value_set_*`/`get_*` 函数；字符串使用长度明确的 UTF-8 `vs_interop_string_view`，宽字符串使用 UTF-16 单元和长度。值句柄可创建、复制、重置和销毁；`clone` 复制值包装器的引用语义，不会自动深复制容器或对象载荷。使用 `vs_interop_value_assign(destination, source)` 可以将完整的运行时值复制到可写目标，同时保留容器和对象身份语义；只读借用目标会被拒绝。

数组、队列、栈、双端队列、映射、集合和通道通过专门的创建、计数、读取、包含、发送、接收和关闭函数操作。映射和集合键必须是语言支持的精确类型：有符号/无符号整数、UTF-8/UTF-16 字符串、限定枚举和宿主值。宿主值默认按身份比较，也可以注册带相等和哈希戳的协议。映射查找不存在的键时返回成功并把 `out_found` 设为 `0`，不会覆盖输出值；打开通道接收可能阻塞，关闭通道会唤醒等待者。

## 原生绑定与调用

适配器可以注册三类信息：

- `vs_interop_session_register_native`：快速注册原生函数；
- `vs_interop_session_register_host_function`：使用结构化返回类型、参数、文档、参数名、注解和泛型元数据注册函数；
- `vs_interop_session_register_host_type`、`vs_interop_session_register_host_member`、`vs_interop_session_register_host_protocol`：注册枚举、结构体、类、构造函数、方法、属性、字段、静态成员、运算符、克隆、相等、哈希和子类工厂。

宿主函数和成员的参数标志是位标志：`VS_INTEROP_PARAM_REF = 0x01`、`VS_INTEROP_PARAM_CONST_REF = 0x02`。`const ref` 必须同时包含引用位。宿主回调可以从 `vs_interop_call_context` 读取参数、读取可变参数、获取 `host_this` 和程序代际，并调用回调局部的脚本函数或可保留 callable。

调用脚本函数、方法、构造函数、字段和全局量的入口分别是：

| 能力 | 代表函数 |
| --- | --- |
| 自由函数/方法 | `vs_interop_session_call_function`、`vs_interop_session_call_method`。 |
| 拦截绕过 | `vs_interop_session_call_method_bypassing_interceptor`。 |
| 查询 | `vs_interop_session_has_function`、`vs_interop_session_has_method`。 |
| 对象和成员 | `vs_interop_session_construct_object`、`vs_interop_session_get_field`、`vs_interop_session_set_field`、`vs_interop_session_get_global`、`vs_interop_session_set_global`。 |
| 保留 callable | `vs_interop_session_retain_callable`、`vs_interop_callable_invoke`、`vs_interop_callable_destroy`、`vs_interop_callable_is_valid`。 |

调用参数句柄必须可写，因为成功调用会把 `ref`/`out` 的最终值写回原句柄；值参数和 `const ref` 参数不会被写回。调用开始前会先验证参数，验证失败不会产生脚本副作用。回调上下文取得的只读值是借用值，需要在回调结束后保留时先 `clone`。

## 反射、拦截与扩展能力

会话可以导出两类 JSON：

- `vs_interop_session_export_bindings_json`：函数、宿主类型、成员、容器元数据、参数标志、参数名称、文档和注解，适合 IDE、LSP 和静态检查；
- `vs_interop_session_export_reflection_json`：已加载模块的类型、字段、方法、全局量和声明信息，适合运行时工具。

绑定描述中的注解词汇表和声明上的注解实例是两种不同数据：词汇表描述工具可以认识的名称和参数，实例表示注解实际附着到了哪个声明。

此外还可以按会话注册：

- `vs_interop_session_set_instance_call_interceptor`：拦截实例方法，支持 `UNHANDLED`、`HANDLED` 和 `FAILED`；回调中的 `proceed` 使用已经解析的目标，适合实现观测或权限层；
- `vs_interop_session_set_mutation_callback`：接收字段写入、容器增加、删除、设置和清空事件，并携带对象类型、字段、值和对象修改代际；
- `vs_interop_session_apply_class_defaults`：把宿主提供的类默认值应用到脚本对象；
- `vs_interop_session_register_generic_host_type_template` 和 `vs_interop_session_instantiate_generic_host_type`：由同步工厂按具体类型参数构造宿主值类型；
- `vs_interop_session_register_type_alias`：注册透明宿主类型别名；
- `vs_interop_session_register_external_serializer`、`vs_interop_session_serialize_object`、`vs_interop_session_deserialize_object` 和 `vs_interop_session_enumerate_object_references`：接入宿主对象的外部编码、解码和递归引用遍历。

外部反序列化回调只能向临时事务添加命名字段赋值，不能直接取得脚本对象存储。未知的宿主包元数据会按字节保留。

## 包与热重载

<a id="package-container-format"></a>

`.vslib` 是 Core 库映像；原始 Core 应用映像是标准 ZIP `.vsapp` 容器中的已编译载荷。启用 Application 支持时，包字节 API 也接受普通应用 ZIP，应用构建返回 ZIP 容器；未启用时，应用构建返回原始 Core 映像。需要显式配置宿主环境、签名或保护选项时，使用 Application 加载 API。保留应用容器时，包的规范签名字节是原始容器字节；原始映像则使用 Core 规范字节。解析成功不代表发行者可信。

Interop 可以在内存中读取、校验、构建和执行 `.vsapp`/`.vslib`：

- `vs_interop_package_read` 解析内存字节并执行核心格式、版本和结构校验；
- `vs_interop_package_get_kind`、`vs_interop_package_get_name`、`vs_interop_package_get_version`、`vs_interop_package_metadata_count` 和 `vs_interop_package_get_metadata` 读取包信息；
- `vs_interop_package_get_canonical_signed_bytes` 返回平台稳定的借用字节范围；跨越信任边界时，宿主必须先验证签名；
- `vs_interop_session_load_package` 载入应用或库包；
- `vs_interop_session_reload_package` 以事务方式重载包，只有成功后才消费模块；
- `vs_interop_session_build_package_file` 在内存中构建包字节。

旧开发版生成的包与字节码缓存使用旧格式标识，须从源码重新构建后才能供当前运行时使用；`vs_interop_package_read` 会拒绝旧布局。

用 `vs_interop_session_set_hot_reload_enabled` 启用源文件热重载。
`vs_interop_session_reload` 和 `vs_interop_session_reload_package` 可先暂存候选模块，
在下一次执行时初始化。若有 `VS_INTEROP_CAP_RELOAD_STATUS`，设置
`vs_interop_reload_status.struct_size`，并在创建线程调用
`vs_interop_session_get_reload_status`。`PENDING` 表示候选模块已暂存，旧世代仍是
已提交世代。`ROLLED_BACK` 表示已恢复旧世代。`APPLIED_WITH_ERRORS` 表示候选模块已
提交，之后的执行或清理出现错误。`RESET_REQUIRED` 表示需清除会话并重新加载程序。
若 `vs_interop_session_clear` 报告回滚未完成，请销毁并重新创建会话。
可用 `operation_id` 关联同一次重载的各阶段快照。

兼容的全局量、字段和对象身份可以迁移。不兼容的类型变化、活动通道和捕获闭包可能阻止
迁移。替换模块前先释放依赖该模块的 C 自有值。宿主原生副作用不属于脚本事务。

## 线程、借用和销毁规则

这是适配器必须遵守的边界：

- 会话创建、普通会话 API 和最终销毁都限制在创建线程；
- 错误线程销毁返回 `VS_INTEROP_STATUS_WRONG_THREAD`，不会消费句柄；
- `owner_thread_only == 0` 的保留 callable 可以在其他线程调用，但前提是宿主串行化该会话的全部访问；会话本身仍不是并发安全对象；
- 脚本回调可能来自 VM 工作线程并且可能并发执行，Python 等外部运行时必须在回调中获取自己的运行时锁，并同步共享状态；
- 回调上下文、字符串视图、容器元素视图和宿主元数据参数都是借用数据，只在头文件规定的回调或操作期间有效；需要跨越该边界时复制字符串或克隆值；
- 不要释放模块拥有的字符串、字节和视图；模块生成的 `vs_interop_buffer` 只能使用 `vs_interop_buffer_destroy`；
- 外部回调不能让异常穿过 C ABI；应把错误写入上下文并返回 `VS_INTEROP_CALLBACK_ERROR`；
- 对象、容器和值的生命周期遵循 VeroScript 的引用语义，适配器不能通过复制 C 结构体来推断深复制。

## 能力协商与兼容性

`vs_interop_get_capabilities()` 返回可选能力位，包括：包、热重载、宿主绑定、绑定 JSON、反射 JSON、实例拦截、修改回调、容器值、保留 callable、泛型宿主类型、外部序列化、JIT 诊断、本地化、独立数值基本类型、容器遍历、执行治理、可复用会话代码缓存、显式后端选择和单次初始化。适配器应先检查 ABI，再根据能力位决定是否调用可选导出；能力发现不改变 `VS_INTEROP_ABI_VERSION`。

`vs_interop_get_capabilities()` 报告进程 API 功能，包括
`VS_INTEROP_CAP_JIT_BACKEND_SELECTION`。运行时没有隐式后端，因此全局结果不设置
`VS_INTEROP_CAP_NATIVE_JIT` 或 `VS_INTEROP_CAP_NATIVE_TYPED_LOOPS`。应查询
`vs_interop_session_get_jit_backend_info`，使用 `native_code`、
`VS_INTEROP_JIT_CAP_NATIVE_CODE` 和 `VS_INTEROP_JIT_CAP_NATIVE_TYPED_LOOPS`
判断当前会话所选后端的能力；不能通过平台字符串推断原生支持。ABI 0.1.1 追加到 `vs_interop_jit_stats` 的
`cost_model_vm_fallbacks` 与 `native_typed_loop_executions` 只有调用方提供足够大的
`struct_size` 才可见；旧调用方会安全保留原有前缀。

公共接口的兼容性规则是：不改变已有函数签名、枚举值和已发布结构布局；新增结构字段只能追加，并由 `struct_size` 控制可见范围。适配器应使用 `vs_interop_get_runtime_version` 和 `vs_interop_get_host_platform` 记录诊断信息，而不要硬编码平台布局。

Interop SDK 不规定 Python、C#、Rust、Unreal 或 AOT 的特定策略，也不把它们的运行时打包进库。它依赖 `VeroScriptCpp` 的公开嵌入能力，但不改变语言核心。Web 场景的浏览器安全入口见[WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md)，绑定目录 JSON 的字段级格式见[宿主绑定](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/bindings.md)。

## ABI 的值种类

ABI 0.1.1 包含 `NULL`、`MULTI_MAP`、`ORDERED_MULTI_MAP`、`PRIORITY_QUEUE`、
`MULTI_SET` 和 `ORDERED_MULTI_SET` 值种类。使用
`vs_interop_value_set_null` 与 `vs_interop_value_is_null` 表示 VeroScript
的 `nullptr`；null 是独立的真实值，不是无效句柄。

`vs_interop_value_create_sequence` 支持 `PRIORITY_QUEUE`。
`vs_interop_value_create_map_kind` 可创建普通映射和多重映射；多重映射保留
每一条重复的键值关联，普通映射遇到重复键时替换已有值。
`vs_interop_value_create_set_kind` 可创建普通集合和多重集合；多重集合保留
重复元素。

`vs_interop_value_set_host_object_ex` 接受可选的
`vs_interop_host_object_is_type` 回调，用于在重载或类型校验期间判断宿主
对象子类型是否兼容。回调不能抛出异常，也不能保存借用的类型名视图。

## 运行模式

`VS_INTEROP_RUN_EVAL` 保留增量求值语义。`VS_INTEROP_CAP_EVAL_ONCE`
声明的 `VS_INTEROP_RUN_EVAL_ONCE` 只执行一次模块初始化，不调用 `main`，
并允许所选 JIT 后端执行初始化。`VS_INTEROP_RUN_SCRIPT` 还会调用 `main`。
执行策略单独设置；未选择后端时，Mixed 使用 VM，强制 JIT 在执行前失败。

普通会话每次运行都会初始化新的状态；EvalOnce 不会让后续运行跳过初始化。
热重载会话保留已初始化的全局变量。保留状态的初始化使用 VM，符合条件的执行使用所选后端。

## JIT 显式选择

执行策略与后端选择分别设置。未提供已安装模块环境时，Runtime 不提供默认模块：未选后端时 mixed 使用 VM，强制 jit 在执行前失败。会话空闲时，在所有者线程使用 `vs_interop_session_set_jit_backend` 选择独立安装、兼容的库文件或目录；`vs_interop_session_set_jit_backend(session, (vs_interop_string_view){NULL, 0})` 清除选择。替换失败保留先前后端。`vs_interop_session_get_jit_backend` / `vs_interop_session_get_jit_backend_info` 查询实际身份和原生能力；`backend-services` 仅表示 Core 服务契约。调试、保留状态的初始化和有界清理使用 VM。受控 Mixed 默认使用 VM，可通过 `enable_controlled_mixed` 启用；显式 JIT 不依赖该开关。符合条件的受控入口可使用 JIT，不支持或优先选择 VM 的入口保留检查并回退到 VM。文件/目录发现与 ABI 规则见 JIT 指南。

查询前设置结构的 struct_size。文本视图借用所选工厂存储，在更换后端或销毁会话前复制。

[JIT](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/zh-CN/index.md)

## 受管理内存

宿主接口：`vs_interop_memory_*` / `vs_interop_session_set_memory_limits`。所有权、配额、视图失效与租借规则参阅 [`std::memory`](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)。

通过 `VS_INTEROP_CAP_MEMORY` 查询此增量扩展；既有结构布局和 ABI 版本不变。每次只能提供一种上下文：所有者线程上空闲的 `session`，或原生回调期间有效的 `call`。执行中重入 session 返回 `BUSY`；回调操作使用当前 VM 的执行治理检查。限制、统计及租借结构须先设置 `struct_size`。配额为 0 表示无限制。应用配置、Run 和 Clear 均保留宿主持有区域的配额计数。

`from_array` 要求 uint8 数组；`to_array` 和 memory `clone` 生成独立字节副本。策略启用时，地址查询要求 `VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS`，对象 ID 和有界字节访问不受影响。写租借排斥竞争的字节访问，读租借排斥字节修改，所有租借均阻止调整大小和失效操作；同步完成原生工作后，恰好释放一次 token。释放允许跨线程及在 session 销毁后进行。外部区域注册成功后接管 `owner`，在最终释放线程调用一次释放回调；失败时仍由宿主负责 owner。回调代码及用户数据必须保持有效直到最终释放。

## 编译注解处理与诊断级别

通过 `vs_interop_get_compile_capabilities()` 查询
`VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING`。原生 C++ 宿主使用
`VeroScriptInteropAnnotation.h` 的 `CreateAnnotationProcessingService` 和已冻结注册表
创建不透明服务，需要配套 Core SDK 及一致的编译器/STL ABI；C11 宿主仅传递服务句柄。
`vs_interop_annotation_service_retain` 返回另一个独立拥有的句柄，各自使用
`vs_interop_annotation_service_release` 释放。在空闲的创建线程上调用
`vs_interop_session_set_annotation_service` 附加服务。会话复制配置，因此宿主可立即释放
传入句柄；普通 ApplyConfig 保留服务，传入 NULL 才显式清除。编译回调或服务释放回调
中的配置重入会被拒绝。原生代码和回调数据必须存活至所有句柄及附加会话都释放服务。
句柄仅限本进程，不能序列化。

现有诊断结构布局和 C ABI 0.1.1 保持兼容。通过
`vs_interop_session_get_diagnostic_severity` 查询级别，
`VS_INTEROP_DIAGNOSTIC_SEVERITY_` 前缀的 `ERROR`、`WARNING`、`INFORMATION`、`HINT`
分别为 0、1、2、3。无效索引或错误线程查询不修改输出。成功编译也可以携带非错误
诊断，应检查结果 status 和 success；非错误诊断之后发生失败时，last-error 文本选择
主要错误。

## variant

<a id="exported-value-lifetime"></a>

导出的脚本所有值登记为会话根。读取和使用必须在创建线程进行；其他线程释放包装器只排队，在拥有者安全点清理。替换、重载或清空模块前释放所有快照；关闭会话使依赖模块的包装器失效并执行语言清理。独立基本值盒在关闭后仍可使用。不会自动映射 C# object/dynamic 或任意 Python 对象。

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

目标已确定为 variant 时允许具体值隐式装箱。读取须显式 cast<T> 或 try_cast<T>；variant_is<T> 精确检查存储类型，不做转换。optional<T> 与 try_cast<T> 是 Core 内建功能，无需导入 core.vs。对空 variant 执行 cast<T> 会失败；try_cast<T> 遇到空 variant 或普通转换不匹配时返回空 optional。复制失败、取消和限额仍为错误。

值仍为 HOST_OBJECT；numeric_kind 校验0其他/不可用、1bigint、2rational，已关闭或跨线程脚本值返回0。检查 VS_INTEROP_CAP_NUMERICS。numeric_parse/to_text/convert 必须且只能传空闲所属线程 Session 或活动原生调用的借用 context 之一。文本有明确 UTF-8 长度，to_text 写入普通字符串值；convert 目标为固定整数/float/double 的 VS_INTEROP_NATIVE_*，exact_float 为0/1，失败保留输出。初始化 limits.struct_size 与 reserved=0，零限制选择有限 Core 默认值，不是进程堆上限。

`vs_interop_session_set_numeric_limits`
`vs_interop_value_numeric_kind`
`vs_interop_numeric_parse`
`vs_interop_numeric_to_text`
`vs_interop_numeric_convert`

## 模块相关值的所有权

<a id="value-domain-confinement"></a>

拥有值句柄不代表其载荷脱离会话。脚本对象／函数及具有模块值槽的可变容器即使为空，也受会话创建线程约束，并在会话关闭后不可用。宿主创建的存储首次被会话使用时建立此归属，共享别名和克隆保留该归属。VM 工作线程回调只能导出独立拥有的值；包含 variant／函数槽的空容器也不是独立值。使用后仍须释放拥有的句柄，包括会话已关闭的情况。

## Session 后端选择参数

`vs_interop_session_set_vm_backend` / `vs_interop_session_set_jit_backend`

这些 Session 设置方法接受匹配的文件／目录；提供的模块环境包含解析器时，也接受已安装后端 ID。`auto` 请求环境候选，VM 还可使用保留的产品后备。`none` 或原生空字符串禁用选择；`{NULL, 0}` 映射为该空请求。新建会话未显式选择时可继承环境；省略与显式清除不同。重新配置现有会话时，省略 VM 选择会保留已有执行域。显式选择失败保留先前后端。仅在创建线程的空闲会话上设置；VM 必须在执行前更换，因为 Clear／reload 不解除执行域固定。JIT 选择与 VM／Mixed／JIT 执行策略独立。

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

<a id="host-memory-ownership"></a>

## 宿主共享内存所有权

通过 Session 内存 API 创建缓冲区、有界视图和字节快照。Session 空闲时在所属线程操作；在注册的原生回调内使用当前活跃的内存上下文。复制值句柄不会转移内存访问域。

分配前设置限额，0 表示无限。单缓冲区限额针对容量，保留总量包括拥有容量与外部字节，也包括增长时的峰值。降低到当前占用以下会失败并保留原配置。Run、Clear 和重载不会释放宿主保留的区域，也不清零核算。这些限额只约束字节存储，不是全部堆限额或完整沙箱。

写租借排斥其他字节访问；所有租借阻止 resize 和失效操作，读租借排斥写入。完成原生工作后再释放租借。释放可在其他线程及 Session 销毁后进行，但已销毁 Session 不能用于新操作。外部存储保留所有者直到最后一个别名或租借释放；失效操作使所有别名失效。原始地址观察不保留所有权。

对象 ID 保留 uint64 精度，不延长对象寿命。启用策略时，地址查询需要宿主内存地址能力。仅在原生回调内查询参数的原始引用目标；按值副本没有原始引用地址。不得将回调上下文保留到回调之外。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
