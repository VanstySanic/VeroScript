# WebAssembly ABI、产物与发行契约

## 三个公开接口

`VeroScriptWebAssembly.h` 定义 C++ 接口，`VeroScriptWebAssemblyC.h` 定义 WebAssembly C ABI，TypeScript 封装层与类型声明组成浏览器 SDK。共同的结果字段、后端名称、语言标签和输入行为必须一致。独立的原生 Interop ABI 不定义 WebAssembly ABI。

## 名称与兼容性

模块与 CMake 目标使用 `VeroScriptWebAssembly`，C++ 命名空间为 `VeroScript::WebAssembly`，TypeScript 入口为 `veroscript-webassembly.ts`。SDK 归档名为 `VeroScriptWebAssemblySDK`，安装器名为 `VeroScriptWebAssemblyInstaller`。保留 C ABI 名称 `vero_web_*`、`VeroWeb*`、`VERO_WEB_*` 及 ABI 版本 `0.1.1`。安装配置 `web`、组件 `web-sdk` 和产品 ID `dev.veroscript.web` 用于识别已有安装记录。

## 产物与出版验证

出版完整八项文件：常规和快速引擎的 `.mjs`/`.wasm`、封装层 `.ts`/`.d.ts`，以及两种引擎的 `.d.mts` 声明。标准库和本地化目录内嵌于虚拟文件系统。执行绑定与声明类型检查、完整清单审核，并在无头 Chromium 中验证交互输入、Web Worker 执行与终止。产物审核对 Release 应用下载预算：交互 Wasm 为 17 MiB，快速 Wasm 为 11 MiB；这是出版限制，不是 ABI 限制，仅通过源码检查不能证明浏览器验收完成。

当前基线包含精确数值及嵌套、局部和泛型类型作用域。

Web 产品静态装配 `VeroScriptBytecodeVm`。源码检查、编译扩展、直接执行和可重复运行的编译程序均使用显式选定的解释器。

两种 Wasm 引擎均通过 Core 支持初始化捕获、`mutable` 闭包和局部 `static` 变量。值捕获默认只读；复制函数值会共享环境，重新求值 Lambda 则创建新环境。局部静态变量在首次使用时初始化，初始化失败后可以重试。每次程序运行都使用全新状态，包括局部静态变量；交互运行同样如此。Web ABI 和 TypeScript API 保持不变。

## ABI 与本地化

运行时语义版本、WebAssembly ABI 版本、字节码版本、能力标志和选定语言分别记录。运行时发现实际可用的 BCP 47 目录标签，不翻译 API 标识、协议字段、诊断 ID 或版本，也不按固定语言数量推断行为。

## 程序与输入能力

C ABI 导出、TypeScript 声明和能力检测必须一致。一次编译多次执行需要编译、执行和销毁导出；交互式程序执行还需要 Asyncify 交互导出。缺少增量导出时应报告能力不可用，不能伪造程序句柄。

上下文数组的 UTF-8 路径与源码数量相等，仅在调用期间借用。程序句柄归创建它的运行时所有，不能转移到其他实例。结果 JSON 携带诊断、输出、请求的后端策略、计时和 JIT 统计。出版验证须覆盖句柄复用、每次运行的新状态、销毁、交互挂起及语言选择。

<a id="web-execution-policy"></a>

发布模块不选择 JIT 后端。`vm` 解释执行字节码；`mixed` 保持默认值，也通过 VM 执行。`jit` 在应用初始化、`main` 执行或请求输入前返回结构化诊断 `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory`。原生 JIT 执行层归属 `Modules/Execution/VeroScriptAsmJit`；Web 模块不内置也不动态加载该 JIT 后端。原先指定 `jit` 的调用方需要改用 `vm` 或 `mixed`。策略名称与 C ABI ID 保持稳定；`result.backend` 记录请求的策略，所有 JIT 计数均为零。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/release-contract.md) · [fr-FR](../fr-FR/release-contract.md) · [ja-JP](../ja-JP/release-contract.md) · [ko-KR](../ko-KR/release-contract.md) · [zh-CN](release-contract.md) · [zh-TW](../zh-TW/release-contract.md)
