# Web 运行时与绑定

也保留单行准确写法：`void main(string[] args)` 与 `int main(string[] args)`。
## 运行时工厂

`createVeroScriptRuntime()` 是面向 interactive input 的 Asyncify-capable factory。 `createFastVeroScriptRuntime()` 省略 whole-program Asyncify stack instrumentation，适用于所有 input 都在 run 前提供的 preferred path。capability flag 允许时，两种 factory 暴露相同 language/result contract。

`runSource` 在 module creation 后 synchronous。 `runSourceInteractive` 在 input provider 等待 logical line 时保持 VM/script state suspend。一个 Wasm module 只能有一个 active interactive run。provider exception 会在 runtime 安全离开 input wait 后 reject promise。

参阅[WebAssembly/WASM SDK](../../User/zh-CN/index.md#runtime-factory-configuration)。

## input 与 result

run 接收 file label、optional virtual file、standard input、script argument 与选择的 `vm`、`mixed`、`jit` backend。支持 `void main(string[]
args)` 和 `int main(string[]
args)`。non-zero integer result 仍是 successful run，并作为 `exitCode` 返回。embedded NUL input 会被 reject。

browser baseline 不 advertise script-level worker thread。unbounded program 通过 terminate owning Web Worker 停止。independent program 应使用 independent runtime module 或 worker。

参阅[WebAssembly ABI、产物与发行契约 — 程序与输入能力](release-contract.md#web-execution-policy)。

字段与方法的可执行反射可在 `vm`、`mixed` 后端运行。发布的浏览器模块没有启用 pthread，因此新的 callable 任务和原有任务均无法启动。`is_file`、`is_directory`、`remove_dir` 只访问模块的虚拟文件系统（包括内置标准库文件），不能访问访客的本地文件。

## 已编译程序

`compileSource` 接收 source file label 与 in-memory virtual file，返回 result 和 optional owned `VeroScriptProgram`。program handle 可用新的 input、argument、backend selection 重复 run 而无需 reparse source graph。每个 run 使用 fresh execution state；compilation diagnostic 保留在 compile result 中。

`VeroScriptProgram.dispose()` release native handle，且在 SDK boundary idempotent。runtime track live program 并在 runtime disposal 时 release。interactive compiled execution 需要 Asyncify artifact、`readLine` provider，并且同一 WebAssembly module 没有其他 active interactive run。

## complete source 与 compiled-program surface

TypeScript SDK 将 runSource map 到 `vero_web_run_source_with_context`，包含 script argument、selected backend 和 paired in-memory virtual file。compileSource map 到 `vero_web_compile_source_with_context`；success result own VeroScriptProgram。VeroScriptProgram.run 使用 `vero_web_run_program`，runInteractive 使用 Asyncify-only `vero_web_run_program_interactive`。program run 获得 fresh VM state，可选择 input、argument、backend。

native C++ surface 提供 `FRuntime::RunSource`、`RunSourceWithInputStream`、`CheckSource`、`SetLanguage`、`GetAvailableLanguages`。C ABI 另外暴露 argument、option、context、compile、program、interactive、context-check entry point。C ABI array/string 仅在 call 期间 borrowed；returned JSON/result pointer 遵循 runtime-owned lifetime rule。VeroScriptResult 将 backend、output、error output、diagnostic、timing、program handle、per-run JIT statistic 作为 separate field。

## result 与 JIT field mapping

`VeroScriptResult.jit` 在检查、编译、API 错误或源码加载失败时为 null。已加载的运行即使在脚本初始化前被拒绝，也会返回单次运行的计数，例如未选择后端时显式请求 `jit`。发布模块的所有计数均为零。`directNativeCalls` 和 `compileAttempts` 是统计值，不是语言或 ABI 版本。

## 受管理内存

参阅[WebAssembly/WASM SDK — 受管理内存](../../User/zh-CN/index.md#managed-memory-contract)。

## 失败清理与快照

输入桥接层分配失败或超出 Wasm32 大小范围时，会恢复原生调用栈并返回 API 失败结果，模块仍可用于后续运行。故障注入覆盖 `vm` 和 `mixed` 的源码及已编译执行，并验证每次失败后的恢复。正常 EOF 行为保持一致。

失败清理保持所有权完整：复制交互输入行时即使抛出异常，也会释放输入分配；已编译程序的结果 JSON 若无法生成，会在句柄返回宿主前移除该程序。只读租约生成一次独立快照，`toArray` 直接返回该快照，避免再次完整复制。回归覆盖快照的独立生命周期，以及已编译运行失败后受管理内存全局变量的重新初始化。

## 核心兼容与诊断

参阅[WebAssembly/WASM SDK — 核心兼容与诊断](../../User/zh-CN/index.md#compiler-diagnostics)。

## 交互输入所有权

交互调用在读取选项后重新检查模块状态。重入请求或运行时已销毁的请求会在接管输入提供器前被拒绝，正在执行的程序及其后续输入保持有效。

字符串指针表按实际编组项数分配，避免自定义或动态数组迭代写入按初始长度分配的较小区域。同步和异步辅助函数均校验 Wasm32 边界。运行时销毁直接遍历受跟踪程序，不再创建第二份数组。

参阅[WebAssembly/WASM SDK — 交互输入所有权](../../User/zh-CN/index.md#input-provider-failures)。

## 控制台输出与能力

参阅[WebAssembly/WASM SDK — 控制台输出与能力](../../User/zh-CN/index.md#console-capabilities)。

即使终端能力不可用，非法的颜色名、清屏模式、光标坐标、参数类型或参数数量仍会产生诊断。请求被拒绝后，运行时仍可复用。捕获文本在执行完成后通过 `VeroScriptResult` 返回。

## 当前语言功能

参阅[WebAssembly/WASM SDK — 当前语言功能](../../User/zh-CN/index.md#compiler-and-application-services)。

已发布的 C ABI 和 TypeScript 选项未开放 `FModuleEnvironment`、原生扩展注册、资源提供器或 `FEngineConfig::I18nServices`。核心允许自定义 C++ WebAssembly 构建采用静态原生注册；已发布引擎不能加载桌面动态库。这些宿主集成需要单独配置构建产物。现有源码覆盖与程序所有权约定保持有效。

## 注解名称匹配

参阅[WebAssembly/WASM SDK — 注解名称匹配](../../User/zh-CN/index.md#annotation-name-matching)。

接受范围重叠且定义冲突的注解注册会产生编译诊断。两个引擎在检查、源码执行及编译程序创建时采用相同策略。已编译程序保留其定义策略，修改后需重新编译。Web C ABI 和 TypeScript 选项不变。源码元数据契约变化后，旧开发版包与缓存需从源码重新生成；SDK 程序句柄仍只保存在内存中。

## 核心执行与限额

参阅[WebAssembly/WASM SDK — 核心执行与限额](../../User/zh-CN/index.md#numeric-and-library-limits)。

重复执行编译程序可复用内部工作区，但每次运行均使用新的脚本全局变量、局部变量和参数，失败后亦如此。核心变化后应重新构建两个 Wasm 引擎并执行现有 SDK 发布检查。旧 SDK 验证记录仅适用于其记录的构建产物。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/runtime-and-bindings.md) · [fr-FR](../fr-FR/runtime-and-bindings.md) · [ja-JP](../ja-JP/runtime-and-bindings.md) · [ko-KR](../ko-KR/runtime-and-bindings.md) · [zh-CN](runtime-and-bindings.md) · [zh-TW](../zh-TW/runtime-and-bindings.md)
