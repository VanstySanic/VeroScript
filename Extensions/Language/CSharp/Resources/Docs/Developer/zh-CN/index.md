# VeroScript C# 技术文档

本目录是 C# 语言组共享的开发者文档，与 C# 源码、Interop 头、CMake 目标和托管测试绑定；它不是终端用户教程，也不复制公共 API XML 文档。

## 模块归属

- `VeroScriptCSharp`：兼容 NativeAOT 的 .NET 8/9 运行时接口，基于 `VeroScriptInterop` 提供会话、值、包、回调、宿主绑定、容器、反射和外部序列化。
- `VeroScriptCSharpAot`：提供 C# 源及面向 NativeAOT 的发射器接口，直接依赖语言核心。
- `VeroScriptCSharpBindGen`：读取支持的 C# 声明，发射 Binding Catalog 元数据和面向注册的生成代码。
- `VeroScriptCSharpFrontend`：为 C# 构建期工具提供共享的语义声明模型。

托管适配器必须使用稳定 C ABI，不能包含 C++ 私有头或在托管代码中复刻编译器/运行时行为。

## ABI 与生命周期规则

Interop 句柄是不透明所有权对象。每个 lease、buffer、包、可调用对象、回调注册和会话操作都必须遵循 `VeroScriptInterop.h` 及托管包装器镜像的生命周期规则；不得在规定期限外暴露指针、借用字符串或回调上下文。包装器负责 UTF-8/UTF-16、buffer、状态码、诊断和回调异常的准确编组，可改善易用性但不得将 ABI 错误改为成功或悄悄丢弃失败。

## 前端与生成器边界

前端不执行用户代码，仅解析声明。BindGen 消费前端模型并发射元数据，不创建原生调用 thunk；不支持的签名、所有权模式、默认参数和继承形式必须在替换生成输出前给出源诊断。AOT 是独立降级路径，其可接受语言和输出契约以发射器与测试为准。

## 兼容性规则

受检查 API 契约与包验证测试管理公共类型、编组布局、enum、回调调用约定、包元数据和目标框架的变更；版本、ABI 标识、JSON 键、CLI 选项和 C# 标识符均为不翻译的技术 token。

## 托管运行时细节

`VeroScriptRuntime` 是进程级兼容性闸门；在使用会话前调用 `EnsureCompatible()`。`VeroScriptSession` 拥有原生句柄和托管根；`VeroScriptValue` 可能是拥有句柄或借用视图，借用值只在 borrow scope 有效，容器枚举在原生返回后报告捕获的回调异常。`VeroScriptCallable` 的创建线程和活跃调用释放规则只保证生命周期，不授权跨线程调用。`VeroScriptPackage` 公开元数据、规范签名字节和可加载模块状态。

## 绑定与生成代码细节

宿主绑定保留文档、注解、类型参数、参数元数据、enum 底层类型、const/mutable 标志、操作符、clone/相等/哈希协议、子类工厂和泛型宿主类型回调；生成注册是元数据加回调，而非函数指针表。

## 详细页面

- [C# 运行时与 Interop](runtime-and-interop.md)
- [C# 绑定、包与兼容性](bindings-and-packages.md)
- [C# 工具链与包契约](toolchain-and-packaging.md)

## 托管执行契约

`VeroScriptConfiguration` 包括执行深度、`MaxInstructions`、`TimeoutMilliseconds`、执行控制、能力策略与 `VeroScriptExecutionBackend`；零指令/超时不限。`RequestCancellation` 是唯一可由非所有者线程调用的会话操作，且会话必须存活到其返回。`VeroScriptJitPolicy`、`GetLastJitStats`、`SetJitCacheLimits` 与 `GetJitCacheStats` 公开自适应编译、最近运行和可复用会话缓存统计；当宣告 `SessionJitCache` 能力时，缓存资源有界。不支持的入口回退到 VM。调试器附加时和初始化保留状态时使用 VM，同一世代内符合条件的调用可复用已编译代码。

## 依据

以 `VeroScriptCSharp/`、前端/BindGen 项目、Interop 公共头、CMake 和托管测试为依据。ABI、所有权、线程、生成代码或包契约变更必须同步更新文档。

`VeroScriptCSharp`: [Android 与 iOS / MAUI](../../User/zh-CN/mobile.md).

移动打包仍使用同一项目和包标识：`VeroScriptIncludeMobile=true` 将桌面 .NET 8/9 与 .NET 10 Android/iOS 目标及已验证的原生 SDK 一起打包。


[Android/iOS 中使用 CSharp](mobile.md)

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
