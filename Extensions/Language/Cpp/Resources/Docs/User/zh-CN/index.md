# C++ SDK

C++ SDK 是 VeroScript 的原生集成接口，包含运行时嵌入 API 以及面向 C++ 的源代码工具。Python、C#、Rust 等适配器使用的 C ABI 另见 [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md)。

## 模块选择

| 模块 | 用途 |
| --- | --- |
| [嵌入](embedding.md) | 从 C++ 宿主检查、加载、运行、调用、重载和配置 VeroScript。 |
| [宿主绑定](bindings.md) | 描述并注册原生函数、宿主类型、成员、运算符和泛型宿主类型。 |
| [AOT](aot.md) | 为本机构建从 VeroScript 生成 C++17 源代码。 |
| [C++ 前端](frontend.md) | 将受支持的 C++17 子集解析为共享的公开 AST。 |
| [BindGen](bindgen.md) | 从 C++ 声明生成绑定目录和面向人的 API 参考。 |
| [Transpiler](transpiler.md) | 将受支持的 C++17 子集转换为 VeroScript 源码和源映射。 |

前端由 BindGen 和 Transpiler 共享。它有意只接受边界明确的声明/源码子集；无法保持语义的构造会产生诊断并失败，不会静默近似。这些工具都不是通用 C++ 编译器。

## 集成顺序

<a id="explicit-vm-selection"></a>

执行前，通过 `FEngineConfig::VmBackendFactory`／`VmBackendPath` 或 Session 选择 API 配置匹配的 VM。仅链接 C++ SDK 不会选中解释器，脚本编译处理器也需要 VM。所需 SDK 目标及配置步骤见 [VeroScriptBytecodeVm](../../../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/zh-CN/index.md)。

1. 一次性处理使用 `VeroScriptEngine`，长期持有模块和状态使用 `VeroScriptSession`。
2. 加载模块前注册宿主函数和类型。
3. 需要分开报告验证错误时，先调用 `CheckSource`/`CheckFile`，再调用 `RunSource`/`RunFile`。
4. 如果 IDE、LSP、包或静态检查器需要宿主接口，导出并校验绑定目录。
5. 回调和 worker 执行可能并发，宿主拥有的状态必须自行同步。

脚本全局变量和类静态字段按原生线程隔离；脚本需要显式同步共享时使用 `channel<T>`。原生回调、实例拦截器、变更观察器以及 AOT 宿主符号都可能从 worker 并发调用。

## 受管理内存

宿主接口：`VeroScriptSession::GetMemoryContext()` / `VeroScript::Memory`。所有权、配额、视图失效与租借规则参阅 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)。

## 源码工具的诊断语言

C++ 前端及 C++ BindGen/Transpiler 自身的帮助、参数错误和声明诊断目前使用英文。BindGen/Transpiler CLI 不接受 `--lang`，切换运行时诊断语言不会翻译工具自身的消息。诊断代码和源码位置仍可供程序处理。

## 模块环境与后端

提供的模块环境含执行解析器时，Engine／Session 可继承其中的自动后端选择。显式工厂及文件／目录仍优先，原始 `VeroScriptVm` 仍需显式装配。参见[服务后端选择](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection)。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
