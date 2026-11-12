# VeroScript C++ 技术文档

本目录是与 C++ 源码绑定的开发者材料，描述由公共头文件、CMake 目标、测试和生成产物实现的契约。它不是用户指南；API 名称、命令行选项、JSON 键、诊断和示例代码不得被翻译。

## 模块归属

- `VeroScriptCpp`：核心之上的高级 C++ 嵌入与会话接口。
- `VeroScriptCppAot`：把通过检查的 VeroScript 程序发射为一个 C++ 源 Bundle；单翻译单元使模板实例化、内部链接和初始化顺序明确。
- `VeroScriptCppFrontend`：解析受支持 C++17 子集，提供给源码工具的稳定 AST。
- `VeroScriptCppBindGen`：把可表示的 C++ 声明转换为 Binding Catalog；目录是元数据而非原生调用 thunk。
- `VeroScriptCppTranspiler`：把保持语义的 C++17 子集降级为 VeroScript 源，并由真实编译器验证。

语言中立核心位于 `VeroScript/`。C++ 模块可依赖核心，核心不得依赖 C++ 工具。前端由 BindGen 与 Transpiler 共享，二者必须在解析后实施不同语义策略。

## 语义边界

C++ 工具只接受有界子集；不支持的所有权、链接、指针、模板、预处理或控制流必须产出诊断。悄然近似任何一种构造都是 ABI 或源码正确性回归。前端在 AST 可表示时保留限定名、别名、属性、声明范围、构造/析构、friend、类型参数模板和同文件类外定义；BindGen 可以保留而 Transpiler 必须拒绝没有 VeroScript 对等物的元数据。这是两项独立策略，不能为了方便合并。

## 输出与事务规则

多输入工具必须在替换任何输出前解析并合并全部输入。目录、API 引用、源码映射和清单作为一个逻辑结果原子替换；重复基名使用确定的路径派生消歧。任何解析、合并、验证或最终编译失败都不得留下部分输出树。生成 API 引用仅为文档，JSON Binding Catalog 才是编译器/IDE 的机器输入。

## 并发与宿主契约

嵌入和 AOT 宿主必须在完整运行期间保持回调状态存活，并同步可能被 worker 到达的状态。脚本全局和类静态字段按原生 worker 隔离，`channel<T>` 是显式同步跨 worker 能力；原生回调、拦截器、变更观察器和可达 AOT 符号可能并发。

## 引擎与会话契约

`VeroScriptEngine` 提供无状态的 `CheckSource`、`CheckFile`、`RunSource` 和 `RunFile`；文件检查和运行可使用 bytecode cache，源标签、I/O/error stream、循环导入策略和项目范围访问策略均为显式输入。`VeroScriptSession` 是有状态接口：运行期间拒绝配置、注册、加载、编译、提交、重载和清理。`LoadFile` 建立文件重载路径，源模块不能按路径重载；所有重载先暂存验证再提交，包种类与入口形状必须匹配。`Reload`、`ReloadCompiledModule` 和 `ReloadPackage` 的失败不得替换工作模块。

`Run(ERunMode::Eval)` 执行模块初始化而不调用 `main`；跨调用的全局状态仅在启用 `SetHotReloadEnabled(true)` 时保留。`Clear()` 清除加载状态、最近 JIT 统计和可复用会话 JIT 代码。`SetJitPolicy` 影响后续执行。普通 `CallFunction` 可在新 VM 间复用已编译代码；`SetJitCacheLimits` 和 `GetJitCacheStats` 提供当前代/配置的受限保留资源与累计编译信息。

## 公共 C++ AOT 与工具输出

AOT API 返回单个生成 `.cpp` 字符串，或包含相对源文件和 manifest 的 `FCppAotSourceBundle`。BindGen 返回合并目录、可选 API 引用和项目 manifest；Transpiler 返回生成源和 source map，项目模式还为每个输入返回一个文件记录和 manifest。共享输出 writer 是原子的：解析、合并、lowering、验证或路径冲突失败后不会替换输出文件。

frontend AST 记录源范围、声明 attribute、alias、template、C linkage、storage/semantic qualifier、body 和类外定义。`FTypeMapOptions` 明确控制 raw host pointer、rvalue reference、已知 host type、已知 enum type、已知 type parameter 与已知 alias。

## 详细页面

- [C++ 组架构](architecture.md)
- [C++ 嵌入与会话](session.md)
- [C++ 前端、BindGen 与 Transpiler](frontend-and-tools.md)
- [C++ AOT 发射器](aot.md)
- [C++ 构建与测试契约](build-and-tests.md)

## 执行治理与会话可观测性

`VeroScriptSession` 还提供 `SetExecutionLimits`、`SetExecutionControl`、`RequestCancel`、`SetCapabilities`、`SetJitPolicy` 和 `GetLastJitStats`，并通过 `EExecutionBackend` 请求执行后端。零限制不限，取消是协作式的，能力限制必须由 VM 强制执行。符合条件的 `Run`、`CallFunction`、`CallMethod` 和保留 callable 调用可共享同代 JIT 缓存，并会在模块或执行配置改变时失效。公共调用面还包含实例调用、构造、字段/全局访问、可调用对象、反射、外部序列化和 `CallMethodBypassingInterceptor`；后者仅绕过所选入口，嵌套调用仍经过拦截器。

## 依据

`Public/` 下公共头、各模块 `Private*` 实现、模块 CMake 文件和专门测试是权威依据。修改公共契约、输出格式、支持子集、所有权或并发保证时必须同步更新本文件；标识符、路径、目标名、诊断和版本值保持源码原样。

## Session JIT 重用

Session 代码复用资格、执行治理、保留限制与记账统一维护在 [C++ Session](session.md#session-jit-cache)。

## 保留字节配额

`MaxNativeBytes` 或 `MaxRetainedPayloadBytes` 为零不允许保留对应字节；与表示无限制的零策略预算不同。

## 取消与宿主投递

参阅[C++ 嵌入与会话 — 取消与宿主投递](session.md#session-control-and-streams)。

共享同一个 `FExecutionControl` 令牌的 Session 会共享取消状态。调用 `Reset()` 并重用令牌前，应等待之前的取消调用结束，避免旧请求取消下一次使用该令牌的执行。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
