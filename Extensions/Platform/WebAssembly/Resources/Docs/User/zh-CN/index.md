# WebAssembly/WASM SDK

`VeroScriptWebAssembly` 是面向浏览器及其他 JavaScript 宿主的无界面 WebAssembly SDK。它提供内存中的源代码检查与执行；页面布局、编辑器、输出面板和框架集成由宿主负责。

<a id="runtime-factory-configuration"></a>

TypeScript 工厂在创建 Emscripten 实例前复制宿主配置。同一个配置对象（包括冻结对象）可用于创建独立运行时，不会共享实例导出、堆或资源所有权。继承的 `locateFile` 和 `wasmBinary` 配置也会保留。

## 构件与部署

部署时应将同一次构建生成的模块、WASM 文件和声明文件作为一个整体使用：

| 构件 | 用途 |
| --- | --- |
| `veroscript-engine.mjs` | 支持交互式输入的 Emscripten 模块。 |
| `veroscript-engine.wasm` | 常规运行时的 WebAssembly 二进制文件。 |
| `veroscript-engine-fast.mjs` | 不启用 Asyncify 的低延迟运行时模块。 |
| `veroscript-engine-fast.wasm` | 快速运行时的 WebAssembly 二进制文件。 |
| `veroscript-webassembly.ts` | TypeScript 封装层源码。 |
| `veroscript-webassembly.d.ts` | 封装层和 WebAssembly C ABI 的类型声明。 |
| `veroscript-engine.d.mts`、`veroscript-engine-fast.d.mts` | Emscripten 模块选项声明。 |

标准库和本地化目录会嵌入生成模块的虚拟文件系统，因此 `import "string.vs"` 不要求宿主另外下载标准库文件。模块、WASM、声明和 ABI 必须保持匹配；生成的发布构件不应手工修改。TypeScript 入口会按需加载所选的 Emscripten 引擎：仅导入封装层不会下载任一引擎，选择快速运行时只加载快速引擎，选择常规运行时只加载交互式引擎。

## 选择运行时

脚本可能暂停等待异步输入时使用 `createVeroScriptRuntime`；输入已经由宿主准备好、只需要检查或执行脚本时可使用 `createFastVeroScriptRuntime`：

```TypeScript
import {
	createFastVeroScriptRuntime,
	createVeroScriptRuntime,
} from "./veroscript-webassembly";

const fast = await createFastVeroScriptRuntime();
console.log(fast.supportsInteractiveInput); // false
fast.dispose();

const regular = await createVeroScriptRuntime();
console.log(regular.supportsInteractiveInput); // true
regular.dispose();
```

快速运行时不包含完整的 Asyncify 插桩，调用 `runSourceInteractive` 会报错。常规运行时支持异步 `readLine`，并会在 Promise 完成前保持脚本挂起。

## 执行后端

`runSource` 和 `runSourceInteractive` 的 `backend` 可取 `vm`、`mixed` 或 `jit`：

发布模块不选择 JIT 后端。`vm` 解释执行字节码；`mixed` 保持默认值，也通过 VM 执行。`jit` 在应用初始化、`main` 执行或请求输入前返回结构化诊断 `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory`。原先指定 `jit` 的调用方需要改用 `vm` 或 `mixed`。策略名称与 C ABI ID 保持稳定；`result.backend` 记录请求的策略，所有 JIT 计数均为零。

## TypeScript API

`checkSource` 只校验源代码，不调用 `main`；`runSource` 会先校验再执行。
使用 `supportsCompiledPrograms` 和 `supportsThreads` 检查构件能力；当前发布构件
分别返回 `true` 和 `false`。快速运行时不支持交互式输入。

主要公开类型和函数如下：

| API | 含义 |
| --- | --- |
| `VeroScriptDiagnosticSeverity` | `"error"`、`"warning"`、`"info"` 或 `"hint"`。 |
| `VeroScriptExecutionBackend` | `"vm"`、`"mixed"` 或 `"jit"`。 |
| `VeroScriptLanguage` | 包含规范 BCP 47 标签的开放字符串类型。 |
| `VeroScriptDiagnostic` | 严重级别、消息、代码、文件以及从 1 开始的行列号。 |
| `VeroScriptResult` | ABI、操作、成功状态、退出状态、后端、输出、诊断信息，以及可选的程序句柄、耗时和 JIT 统计。 |
| `VeroScriptJitStatistics` | 每次运行的 `compileAttempts`、`compiledEntries`、`bridgeTrampolineEntries`、`linearTrampolineEntries`、`vmTrampolineEntries`、`unsupportedEntries`、`dispatchAttempts`、`dispatches`、`fallbacks`、`hotFunctions` 和 `directNativeCalls`。其中 `directNativeCalls` 表示绕过虚拟机值桥、到达另一个原生入口的同代 JIT 调用次数。 |
| `VeroScriptVirtualFile` | `{ path: string; source: string }` 形式的导入覆盖文件。 |
| `VeroScriptSourceOptions` | 可选的 `fileLabel` 和虚拟文件列表。 |
| `VeroScriptRunOptions` | 源代码选项以及 `input`、`args` 和 `backend`。 |
| `VeroScriptInteractiveRunOptions` | 源代码选项以及 `args`、`backend` 和 `readLine`。 |
| `VeroScriptInputRequest` | 传递给 `readLine` 的从 0 开始的输入请求索引。 |
| `VeroScriptProgram` | 提供 `disposed`、`run`、`runInteractive` 和 `dispose` 操作的已编译程序。 |
| `VeroScriptCompileResult` | 编译结果和可选的 `VeroScriptProgram`；编译失败不会返回程序。 |
| `VeroScriptProgramRunOptions` | 已编译程序单次运行的 `input`、`args` 和 `backend`。 |
| `VeroScriptProgramInteractiveRunOptions` | 已编译程序交互运行的 `args`、`backend` 和 `readLine`。 |
| `VeroScriptRuntime` | 运行时状态、语言选择、检查、运行和销毁。 |
| `VeroScriptEngineModuleOptions` | 工厂转发给 Emscripten 模块的选项：`locateFile(path, prefix)` 或预先加载的 `wasmBinary`。 |
| `VeroScriptRuntime.supportsCompiledPrograms` | 当前构件是否支持一次编译、多次运行的程序句柄；当前发布构件为 `true`。 |
| `createVeroScriptRuntime(options?)` | 创建支持交互式输入的运行时。 |
| `createFastVeroScriptRuntime(options?)` | 创建非交互式低延迟运行时。 |
| `runtime.compileSource(source, options?)` | 一次编译源代码和虚拟文件，供后续重复运行。 |

典型调用顺序如下：

```TypeScript
import { createVeroScriptRuntime } from "./veroscript-webassembly";

const runtime = await createVeroScriptRuntime();
try {
	runtime.setLanguage("zh-CN");
	const result = runtime.runSource(
		`int main(string[] args)
{
	print("argument count={}", len(args));
	return 0;
}`,
		{
			fileLabel: "main.vs",
			args: ["alpha", "beta"],
			backend: "mixed",
		},
	);
	console.log(result.output, result.diagnostics);
}
finally
{
	runtime.dispose();
}
```

绑定方法调用等通过原生帧重入 VM 的路径受核心的平台栈保护约束。嵌套过深时会返回 `diag.call_stack_overflow`。该保护独立于普通字节码递归的调用深度限制；遇到此诊断时，应减少原生重入的嵌套层数后重试。

## 一次编译，重复运行

对于不会变化、但需要多次运行的源代码，应调用一次 `compileSource`，然后
复用返回的程序。这样可以避免每次重复解析、导入解析、语义分析和字节码生成：

```TypeScript
const compiled = runtime.compileSource(
	`int main(string[] args)
{
	return len(args);
}`,
	{ fileLabel: "main.vs" },
);

if (compiled.program) {
	const first = compiled.program.run({ args: ["one", "two"], backend: "mixed" });
	const second = compiled.program.run({ args: ["one"], backend: "vm" });
	console.log(first.exitCode, first.timings?.executeMs, first.jit?.dispatches);
	console.log(second.exitCode, second.timings?.executeMs);
	compiled.program.dispose();
}
```

编译阶段接受与源代码检查相同的 `fileLabel` 和虚拟 `files` 覆盖，但不接受
运行时的 `input`、`args` 或 `backend`。每次 `program.run` 都使用新的执行状态，
可以单独选择输入、参数和后端；编译后的字节码会复用，而虚拟机状态和 JIT
统计属于单次运行。常规运行时还支持
`program.runInteractive({ args, backend, readLine })`，快速运行时不支持交互式
执行。处理可能来自不同版本的构件时，应检查 `supportsCompiledPrograms` 和
`supportsInteractiveInput`。程序句柄不再需要时调用 `program.dispose()`；销毁
运行时会释放仍然存在的程序句柄。

工厂函数的 `options` 会转发给生成的 Emscripten 模块。若 `.wasm` 文件由自定义
URL 提供，可使用 `locateFile(path, prefix)`；若二进制已经作为 `ArrayBuffer` 或
`Uint8Array` 加载，则可使用 `wasmBinary`。

`checkSource` 只检查源代码，不调用 `main`；`runSource` 会检查并执行源代码。封装层会拒绝 NUL 字符，并在返回结果交给宿主前校验 ABI JSON。

## 结果与退出状态

`VeroScriptResult.success` 表示编译和执行是否正常完成，不等同于 `exitCode === 0`。成功执行的 `int main` 可以返回非零退出码。只有脚本显式调用 `exit(n)` 时 `exitRequested` 才为 `true`；普通的 `int main` 返回不会设置该字段。

`VeroScriptResult` 的基本字段为 `abiVersion`、`operation`、`success`、`exitCode`、`exitRequested`、`backend`、`output`、`errorOutput` 和 `diagnostics`。`backend` 是本次运行请求的执行策略；检查或 API 错误时为 `null`。`output` 和 `errorOutput` 保存捕获的文本，诊断位置使用从 1 开始的行列号。编译和运行结果还可能包含 `programHandle`、`timings`（`compileMs` 和 `executeMs`）以及 `jit`。`VeroScriptJitStatistics` 包含 `compileAttempts`、`compiledEntries`、`bridgeTrampolineEntries`、`linearTrampolineEntries`、`vmTrampolineEntries`、`unsupportedEntries`、`dispatchAttempts`、`dispatches`、`fallbacks`、`hotFunctions` 和 `directNativeCalls`；后者表示同代 JIT 代码中绕过虚拟机值桥、直接到达另一个原生入口的调用次数。

成功的编译结果使用 `operation: "compile"`，并返回非空的 `programHandle`；编译
失败时不会保留程序。`timings.compileMs` 表示编译耗时，`timings.executeMs` 表示
程序运行耗时。可选的 `jit` 对象报告本次运行的 JIT 活动；发布模块的所有 JIT
计数均为零。

## 语言、本地化与虚拟文件

运行时初始语言为确定性的 `en-US`。使用 `availableLanguages` 发现当前构建实际嵌入的目录，再通过 `setLanguage` 选择。Wasm 模块内的底层语言状态是进程级的；同一模块不要并发使用不同语言要求的两个运行时，需要隔离时使用不同的 Web Worker。

`files` 为一次检查或运行提供尚未写入真实文件系统的导入模块：

```TypeScript
const result = runtime.runSource(
	`import "helper.vs";

int main()
{
	return answer();
}`,
	{
		fileLabel: "main.vs",
		files: [{
			path: "helper.vs",
			source: `export int answer()
{
	return 42;
}`,
		}],
	},
);
```

## 交互式输入与宿主边界

脚本使用 `read_line`、类型化输入或 `pause` 时，使用常规运行时和 `runSourceInteractive`：

```TypeScript
const result = await runtime.runSourceInteractive(source, {
	args: [],
	readLine: async ({ index }) => terminal.readLine(index),
});
```

文件结束返回 `null`。一个 Wasm 模块同时只能有一个等待输入的交互式运行；等待期间 TypeScript 封装层会拒绝其他操作和销毁。原始 C ABI 会保护整个模块：创建和文本 getter 返回 null，有状态文本操作返回 API 失败，数值 getter 和程序操作返回 0；内存操作返回 Busy（9），内存结果 getter 返回 0，租约释放仍可调用，销毁请求会被忽略，直到交互调用完成。普通的 `checkSource` 与 `runSource` 在模块创建后是同步的，长时间运行脚本应放到 Web Worker 中，以免阻塞页面。

输入桥接层分配失败或输入行超出 Wasm32 分配范围时，会返回 API 失败结果。模块仍可用于后续运行；`null` 仍表示正常 EOF。

Web 版本不把脚本级 `spawn` 作为跨浏览器并发 API 的保证。需要完整交互流程时，可参考[交互式输入教程](../../../../../../../Resources/Docs/Tutorials/zh-CN/toolchain/16-web-interactive-input.md)。桌面原生宿主的跨语言接口见[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md)；其 ABI 与 WebAssembly ABI 独立。

发布的浏览器构件不使用 pthread，因此不要求 COOP/COEP 响应头。
TypeScript 的 `runtime.supportsThreads` 在两个发布构件中都是 `false`。
`void main(string[] args)` 和 `int main(string[] args)` 都是有效入口；成功的非零
`int main` 仍保持 `success: true`，并通过 `exitCode` 返回。浏览器基线不支持脚本级
`spawn`，尝试创建任务会返回 `diag.failed_to_spawn_task_thread`；不创建任务的通道、
互斥量、信号量和同步值操作仍可在一次执行中使用。需要隔离时，为每个程序使用
独立的 Web Worker。

## C++ facade 与 C ABI

通过 `FRuntime` 成功运行源代码时，JSON 结果也会提供相同的 JIT 统计，其中包括
`directNativeCalls`。

C++ 的 `VeroScript::WebAssembly::FRuntime` 支持语言选择、源代码检查与执行、虚拟文件、输入流、后端名称和 JSON 结果转换。当前 C++ facade 不直接提供已编译程序的所有权 API；需要一次编译后重复运行时，请使用 TypeScript 封装层或 C ABI。不使用 TypeScript 封装层的宿主可以包含 `VeroScriptWebAssemblyC.h`；C ABI 还提供 `vero_web_compile_source_with_context`、`vero_web_run_program`、`vero_web_run_program_interactive` 和 `vero_web_destroy_program`。程序句柄归运行时所有，必须显式释放。后端 ID 为 `0 = VM`、`1 = Mixed`、`2 = JIT`。语言字符串在成功切换语言或运行时销毁前有效；可用语言 JSON 在下一次可用语言查询或运行时销毁前有效；其他返回 JSON 由运行时持有到下一次返回运行时拥有文本的 API 调用或销毁。输入字符串和数组只在调用期间借用。

## 受管理内存

宿主接口：`runtime.memory` / `runtime.memory.setLimits` (bigint)。所有权、配额、视图失效与租借规则参阅 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)。

<a id="managed-memory-contract"></a>

宿主封装支持缓冲区、切片、复制、标量读写、快照和显式租约。大小、偏移、配额、ID 和 64 位整数使用 `bigint`；标量支持小端或大端。`fromArray` 创建独立快照，也适用于 Node.js `Buffer`；`toArray` 返回副本。使用完后释放区域。租约固定底层存储、阻止调整大小，运行时销毁后仍可释放。`withBytes` 只接受同步回调：不要在回调中释放该租约、保留可写视图或使 Wasm 堆增长。只读租约提供副本；堆增长后，可写视图会在下一次回调时重新建立。交互输入等待期间会拒绝有状态的内存操作。

宿主区域与脚本会话的配额相互独立，各上下文初始为单缓冲区 16 MiB、总保留 32 MiB。`runtime.memory.setLimits` 只修改宿主区域配额，零表示不限制。这些配额只覆盖受管理的字节存储，不覆盖整个 Wasm 堆，也不是完整沙箱。

## 核心兼容与诊断

<a id="compiler-diagnostics"></a>

内置编译器会准备跨文件声明，并接受结构化注解；未知注解保留为元数据。浏览器 SDK 不注册原生注解处理器。导入 `delegate.vs` 后使用 `std::delegate` 和 `std::multicast_delegate`，其成员采用小写命名。诊断保留 `error`、`warning`、`info`、`hint` 级别；成功结果也可能包含非错误诊断。源码执行保留编译诊断，可复用程序则在编译结果中提供这些诊断。字节码使用固定 `VSBC` 标识和产品版本 `0.1.1`；旧开发构建生成的包与缓存应从源码重建。

## 交互输入所有权

交互调用在读取选项后重新检查模块状态。重入请求或运行时已销毁的请求会在接管输入提供器前被拒绝，正在执行的程序及其后续输入保持有效。

<a id="input-provider-failures"></a>

SDK 输入提供器抛错或 Promise 被拒绝时，会终止本次执行并以原始异常拒绝；非法返回类型和包含 NUL 的输入行会以 `TypeError` 拒绝。失败会恢复原生调用栈并完成清理，不再被当作 EOF 后继续正常执行脚本。运行时和已编译程序仍可复用。`null` 仍表示正常 EOF。未标记的原始模块提供器保留对拒绝或非法输入的既有 EOF 回退行为。

## 控制台输出与能力

<a id="console-capabilities"></a>

`std::console::write` 将不带换行的文本追加到 `output`；`flush` 刷新标准输出。`std::console::error` 使用宿主的行式诊断通道，末尾未换行的文本在 `errorOutput` 中会补上换行。SDK 不提供终端宿主：能力查询返回 `false`，尺寸为零，有效的颜色、光标和清屏操作返回 `false`，不会添加转义序列或删除已捕获的文本。异步输入支持与终端能力相互独立。

即使终端能力不可用，非法的颜色名、清屏模式、光标坐标、参数类型或参数数量仍会产生诊断。请求被拒绝后，运行时仍可复用。捕获文本在执行完成后通过 `VeroScriptResult` 返回。

## 当前语言功能

<a id="compiler-and-application-services"></a>

两种引擎均支持小写 `pair<T, U>` / `std::pair`、`std::attribute<T>`、`std::bind_method`，以及内嵌的 `attribute.vs`、`callable.vs`、`i18n.vs` 和 `compiler.vs` 模块。应用级 `std::i18n::context` 支持字面文本、保留参数类型的命名格式化和独立快照；其语言设置不改变 SDK 的诊断语言。SDK 未提供应用资源或格式化器提供器。

`import compiler "builder.vs";` 可在 `checkSource`、`compileSource` 或 `runSource` 期间，从 `files` 加载脚本编译扩展。因此，检查虽不调用应用的 `main`，仍可能执行扩展代码。核心采用默认的扩展限额与能力策略。前端需要能够中断编译时，应放入 Web Worker。扩展失败会返回诊断，运行时仍可复用。

## 交互栈工作区

交互输入可以在嵌套的绑定方法调用内暂停执行。深层原生重入仍受宿主调用栈限制；快速引擎需要在执行前提供输入。

## 注解名称匹配

<a id="annotation-name-matching"></a>

注解名称默认区分大小写。脚本编译扩展可在注册前设置 `annotation_definition.case_sensitive = false`，为该定义启用与区域无关的 ASCII 大小写匹配。处理器订阅以及反射 `has_annotation`、`find_annotations` 和 `annotation_info.matches_name` 遵循此策略。`name()` 和 `raw_text()` 保留源码拼写，`definition().case_sensitive()` 返回该策略。对于 `@Name` 形式的注解，`find_annotations` 和 `matches_name` 查询须包含 `@` 前缀；`has_annotation` 允许省略。参数名、元数据键和未注册注解仍区分大小写。

## 核心执行与限额

<a id="numeric-and-library-limits"></a>

核心通过 `bigint.vs`、`rational.vs` 和 `core.vs` 提供 `std::bigint` 与 `std::rational`，在 VM 内保留精确整数及有理数值；导出精确文本时使用 `to_string()`。它们与固定宽度 Wasm ABI 参数所用的 JavaScript `bigint` 相互独立。嵌套泛型类型、继承的嵌套类型和局部类型遵循核心的作用域及访问规则。

`runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts)` 接受无符号 64 位范围的 `bigint`，依次限制单次操作的文本字节数、批量读取字节数和拆分项数。零表示不增加宿主限制，省略参数会将对应值重置为零。修改对后续源码及交互执行、已有编译程序的下一次运行生效；等待交互输入期间会拒绝修改。这些限额与托管内存配额、核心数值限额相互独立，不限制整个 Wasm 堆。Web API 沿用核心默认的有限数值限额。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
